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

#define GOLDEN_BACKLOG 5

struct device_drv bitfury_drv;

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
	chip_n = libbitfury_detectChips1(sys_spi);
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
	struct cgpu_info *proc;
	struct bitfury_device *bitfury;
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data = malloc(sizeof(struct bitfury_device));
		*bitfury = (struct bitfury_device){
			.spi = sys_spi,
			.fasync = proc->proc_id,
		};
	}
	
	return true;
}

int64_t bitfury_scanHash(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct bitfury_device * const sds = cgpu->device_data;
	struct cgpu_info *proc;
	struct thr_info *pthr;
	struct bitfury_device *bitfury;
	struct timeval now;
	char line[2048];
	int short_stat = 10;
	int long_stat = 1800;
	int i;

	if (!sds->first)
	{
		 // TODO: Move to init
		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			bitfury = proc->device_data;
			bitfury->osc6_bits = 54;
			send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		}
	}
	sds->first = 1;

	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		const int chip = proc->proc_id;
		pthr = proc->thr[0];
		bitfury = proc->device_data;
		
		bitfury->job_switched = 0;
		if(!bitfury->work) {
			bitfury->work = get_queued(thr->cgpu);
			if (bitfury->work == NULL)
				return 0;
			work_to_payload(&bitfury->payload, bitfury->work);
		}
		
		payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
		libbitfury_sendHashData1(chip, bitfury, pthr);
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
			hashes_done2(pthr, 0xbd000000, NULL);
		}
	}

	if (now.tv_sec - sds->short_out_t > short_stat) {
		int shares_first = 0, shares_last = 0, shares_total = 0;
		char stat_lines[32][256] = {{0}};
		int len, k;
		double gh[32][8] = {{0}};
		double ghsum = 0, gh1h = 0, gh2h = 0;
		unsigned strange_counter = 0;

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

			if(sds->short_out_t && ghash < 0.5) {
				applog(LOG_WARNING, "Chip_id %d FREQ CHANGE", chip);
				send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits - 1);
				cgsleep_ms(1);
				send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
			}
			shares_total += shares_found;
			shares_first += chip < 4 ? shares_found : 0;
			shares_last += chip > 3 ? shares_found : 0;
			strange_counter += bitfury->strange_counter;
			bitfury->strange_counter = 0;
		}
		sprintf(line, "vvvvwww SHORT stat %ds: wwwvvvv", short_stat);
		applog(LOG_WARNING, "%s", line);
		sprintf(line, "stranges: %u", strange_counter);
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
		sds->short_out_t = now.tv_sec;
	}

	if (now.tv_sec - sds->long_out_t > long_stat) {
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
		sds->long_out_t = now.tv_sec;
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

bool bitfury_prepare(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	get_now_datestamp(cgpu->init, sizeof(cgpu->init));

	applog(LOG_INFO, "INFO bitfury_prepare");
	return true;
}

void bitfury_shutdown(struct thr_info *thr) {
	struct cgpu_info *cgpu = thr->cgpu, *proc;
	struct bitfury_device *bitfury;
	
	applog(LOG_INFO, "INFO bitfury_shutdown");
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		bitfury = proc->device_data;
		send_shutdown(bitfury->spi, bitfury->slot, bitfury->fasync);
	}
}

struct device_drv bitfury_drv = {
	.dname = "bitfury_gpio",
	.name = "BFY",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.thread_init = bitfury_init,
	.scanwork = bitfury_scanHash,
	.thread_shutdown = bitfury_shutdown,
	.minerloop = hash_queued_work,
};

