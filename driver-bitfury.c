/*
 * Copyright 2013 bitfury
 * Copyright 2013 legkodymov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#include "miner.h"
#include <unistd.h>
#include <sha2.h>

#include "fpgautils.h"
#include "libbitfury.h"
#include "util.h"
#include "spidevc.h"
#include "tm_i2c.h"

#define GOLDEN_BACKLOG 5

struct device_drv bitfury_drv;

static
bool metabank_spi_txrx(struct spi_port *port)
{
	struct cgpu_info * const proc = port->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	tm_i2c_set_oe(bitfury->slot);
	const bool rv = sys_spi_txrx(port);
	tm_i2c_clear_oe(bitfury->slot);
	return rv;
}

static
struct bitfury_device **metabank_detect_chips(int *out_count) {
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int n = 0;
	int i, j;
	static bool slot_on[32];
	struct timespec t1, t2;
	struct bitfury_device dummy_bitfury;
	struct cgpu_info dummy_cgpu;

	if (tm_i2c_init() < 0) {
		printf("I2C init error\n");
		*out_count = 0;
		return NULL;
	}


	devicelist = malloc(100 * sizeof(*devicelist));
	dummy_cgpu.device_data = &dummy_bitfury;
	
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);
	for (i = 0; i < 32; i++) {
		int slot_detected = tm_i2c_detect(i) != -1;
		slot_on[i] = slot_detected;
		tm_i2c_clear_oe(i);
		cgsleep_ms(1);
	}

	for (i = 0; i < 32; i++) {
		if (slot_on[i]) {
			int chip_n;
			
			port = malloc(sizeof(*port));
			*port = *sys_spi;
			port->cgpu = &dummy_cgpu;
			port->txrx = metabank_spi_txrx;
			dummy_bitfury.slot = i;
			
			chip_n = libbitfury_detectChips1(port);
			if (chip_n)
			{
				applog(LOG_WARNING, "BITFURY slot %d: %d chips detected", i, chip_n);
				for (j = 0; j < chip_n; ++j)
				{
					devicelist[n] = bitfury = malloc(sizeof(*bitfury));
					bitfury->spi = port;
					bitfury->slot = i;
					bitfury->fasync = j;
					n++;
				}
			}
			else
				free(port);
		}
	}

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t2);

	*out_count = n;
	return devicelist;
}

void libbitfury_shutdownChips(struct cgpu_info *proc) {
	struct bitfury_device *bitfury;
	for ( ; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data;
		send_shutdown(bitfury->spi, bitfury->slot, bitfury->fasync);
	}
	tm_i2c_close();
}


// Forward declarations
static bool bitfury_prepare(struct thr_info *thr);
int calc_stat(time_t * stat_ts, time_t stat, struct timeval now);
double shares_to_ghashes(int shares, int seconds);

static
int bitfury_autodetect()
{
	RUNONCE(0);
	
	int chip_n;
	struct cgpu_info *bitfury_info;

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bitfury_drv;
	bitfury_info->threads = 1;

	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	bitfury_info->device_data = metabank_detect_chips(&chip_n);
	if (!chip_n) {
		applog(LOG_WARNING, "No Bitfury chips detected!");
		return 0;
	} else {
		applog(LOG_WARNING, "BITFURY: %d chips detected!", chip_n);
	}

	bitfury_info->procs = chip_n;
	add_cgpu(bitfury_info);
	
	return 1;
}

static void bitfury_detect(void)
{
	noserial_detect_manual(&bitfury_drv, bitfury_autodetect);
}


static
bool bitfury_init(struct thr_info *thr)
{
	struct bitfury_device **devicelist = thr->cgpu->device_data;
	struct cgpu_info *proc;
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
		proc->device_data = devicelist[proc->proc_id];
	
	free(devicelist);
	
	return true;
}

static int64_t bitfury_scanHash(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct cgpu_info *proc;
	struct thr_info *pthr;
	struct bitfury_device *bitfury;
	struct timeval now;
	char line[2048];
	int short_stat = 10;
	static time_t short_out_t;
	int long_stat = 1800;
	static time_t long_out_t;
	static int first = 0; //TODO Move to detect()
	int i;

	if (!first) {
		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			bitfury = proc->device_data;
			bitfury->osc6_bits = 54;
			send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		}
	}
	first = 1;

	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		const int chip = proc->proc_id;
		bitfury = proc->device_data;
		
		bitfury->job_switched = 0;
		if(!bitfury->work) {
			bitfury->work = get_queued(thr->cgpu);
			if (bitfury->work == NULL)
				return 0;
			work_to_payload(&bitfury->payload, bitfury->work);
		}
		
		payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
		libbitfury_sendHashData1(chip, bitfury);
	}

	cgsleep_ms(5);

	cgtime(&now);
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		pthr = proc->thr[0];
		bitfury = proc->device_data;
		
		if (bitfury->job_switched) {
			int i,j;
			unsigned int * const res = bitfury->results;
			struct work * const work = bitfury->work;
			struct work * const owork = bitfury->owork;
			struct work * const o2work = bitfury->o2work;
			i = bitfury->results_n;
			for (j = i - 1; j >= 0; j--) {
				if (owork) {
					submit_nonce(pthr, owork, bswap_32(res[j]));
					bitfury->stat_ts[bitfury->stat_counter++] =
						now.tv_sec;
					if (bitfury->stat_counter == BITFURY_STAT_N) {
						bitfury->stat_counter = 0;
					}
				}
				if (o2work) {
					// TEST
					//submit_nonce(pthr, owork, bswap_32(res[j]));
				}
			}
			bitfury->results_n = 0;
			bitfury->job_switched = 0;
			if (bitfury->old_nonce && o2work) {
					submit_nonce(pthr, o2work, bswap_32(bitfury->old_nonce));
					i++;
			}
			if (bitfury->future_nonce) {
					submit_nonce(pthr, work, bswap_32(bitfury->future_nonce));
					i++;
			}

			if (o2work)
				work_completed(cgpu, o2work);

			bitfury->o2work = bitfury->owork;
			bitfury->owork = bitfury->work;
			bitfury->work = NULL;
			hashes_done2(pthr, 0x100000000, NULL);
		}
	}

	if (now.tv_sec - short_out_t > short_stat) {
		int shares_first = 0, shares_last = 0, shares_total = 0;
		char stat_lines[32][256] = {{0}};
		int len, k;
		double gh[32][8] = {{0}};
		double ghsum = 0, gh1h = 0, gh2h = 0;

		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			const int chip = proc->proc_id;
			bitfury = proc->device_data;
			
			int shares_found = calc_stat(bitfury->stat_ts, short_stat, now);
			double ghash;
			len = strlen(stat_lines[bitfury->slot]);
			ghash = shares_to_ghashes(shares_found, short_stat);
			gh[bitfury->slot][chip & 0x07] = ghash;
			snprintf(stat_lines[bitfury->slot] + len, 256 - len, "%.1f-%3.0f ", ghash, bitfury->mhz);

			if(short_out_t && ghash < 1.0) {
				applog(LOG_WARNING, "Chip_id %d FREQ CHANGE\n", chip);
				send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits - 1);
				cgsleep_ms(1);
				send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
			}
			shares_total += shares_found;
			shares_first += chip < 4 ? shares_found : 0;
			shares_last += chip > 3 ? shares_found : 0;
		}
		sprintf(line, "vvvvwww SHORT stat %ds: wwwvvvv", short_stat);
		applog(LOG_WARNING, "%s", line);
		for(i = 0; i < 32; i++)
			if(strlen(stat_lines[i])) {
				len = strlen(stat_lines[i]);
				ghsum = 0;
				gh1h = 0;
				gh2h = 0;
				for(k = 0; k < 4; k++) {
					gh1h += gh[i][k];
					gh2h += gh[i][k+4];
					ghsum += gh[i][k] + gh[i][k+4];
				}
				snprintf(stat_lines[i] + len, 256 - len, "- %2.1f + %2.1f = %2.1f slot %i ", gh1h, gh2h, ghsum, i);
				applog(LOG_WARNING, "%s", stat_lines[i]);
			}
		short_out_t = now.tv_sec;
	}

	if (now.tv_sec - long_out_t > long_stat) {
		int shares_first = 0, shares_last = 0, shares_total = 0;
		char stat_lines[32][256] = {{0}};
		int len, k;
		double gh[32][8] = {{0}};
		double ghsum = 0, gh1h = 0, gh2h = 0;

		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			const int chip = proc->proc_id;
			bitfury = proc->device_data;
			
			int shares_found = calc_stat(bitfury->stat_ts, long_stat, now);
			double ghash;
			len = strlen(stat_lines[bitfury->slot]);
			ghash = shares_to_ghashes(shares_found, long_stat);
			gh[bitfury->slot][chip & 0x07] = ghash;
			snprintf(stat_lines[bitfury->slot] + len, 256 - len, "%.1f-%3.0f ", ghash, bitfury->mhz);

			shares_total += shares_found;
			shares_first += chip < 4 ? shares_found : 0;
			shares_last += chip > 3 ? shares_found : 0;
		}
		sprintf(line, "!!!_________ LONG stat %ds: ___________!!!", long_stat);
		applog(LOG_WARNING, "%s", line);
		for(i = 0; i < 32; i++)
			if(strlen(stat_lines[i])) {
				len = strlen(stat_lines[i]);
				ghsum = 0;
				gh1h = 0;
				gh2h = 0;
				for(k = 0; k < 4; k++) {
					gh1h += gh[i][k];
					gh2h += gh[i][k+4];
					ghsum += gh[i][k] + gh[i][k+4];
				}
				snprintf(stat_lines[i] + len, 256 - len, "- %2.1f + %2.1f = %2.1f slot %i ", gh1h, gh2h, ghsum, i);
				applog(LOG_WARNING, "%s", stat_lines[i]);
			}
		long_out_t = now.tv_sec;
	}

	return 0;
}

double shares_to_ghashes(int shares, int seconds) {
	return (double)shares / (double)seconds * 4.84387;  //orig: 4.77628
}

int calc_stat(time_t * stat_ts, time_t stat, struct timeval now) {
	int j;
	int shares_found = 0;
	for(j = 0; j < BITFURY_STAT_N; j++) {
		if (now.tv_sec - stat_ts[j] < stat) {
			shares_found++;
		}
	}
	return shares_found;
}

static bool bitfury_prepare(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	get_now_datestamp(cgpu->init, sizeof(cgpu->init));

	applog(LOG_INFO, "INFO bitfury_prepare");
	return true;
}

static void bitfury_shutdown(struct thr_info *thr)
{
	applog(LOG_INFO, "INFO bitfury_shutdown");
	libbitfury_shutdownChips(thr->cgpu);
}

struct device_drv bitfury_drv = {
	.dname = "bitfury",
	.name = "BFY",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.thread_init = bitfury_init,
	.scanwork = bitfury_scanHash,
	.thread_shutdown = bitfury_shutdown,
	.minerloop = hash_queued_work,
};

