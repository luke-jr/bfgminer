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

#include "miner.h"
#include <unistd.h>
#include <sha2.h>
#include "libbitfury.h"
#include "util.h"
#include "spidevc.h"

#define GOLDEN_BACKLOG 5

struct device_drv bitfury_drv;

// Forward declarations
static bool bitfury_prepare(struct thr_info *thr);
int calc_stat(time_t * stat_ts, time_t stat, struct timeval now);
double shares_to_ghashes(int shares, int seconds);

static void bitfury_detect(void)
{
	int chip_n;
	struct cgpu_info *bitfury_info;

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bitfury_drv;
	bitfury_info->threads = 1;

	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return;
	chip_n = libbitfury_detectChips(sys_spi, bitfury_info->devices);
	if (!chip_n) {
		applog(LOG_WARNING, "No Bitfury chips detected!");
		return;
	} else {
		applog(LOG_WARNING, "BITFURY: %d chips detected!", chip_n);
	}

	bitfury_info->chip_n = chip_n;
	add_cgpu(bitfury_info);
}

static int64_t bitfury_scanHash(struct thr_info *thr)
{
	static struct bitfury_device *devices; // TODO Move somewhere to appropriate place
	int chip_n;
	int chip;
	uint64_t hashes = 0;
	struct timeval now;
	char line[2048];
	int short_stat = 10;
	static time_t short_out_t;
	int long_stat = 1800;
	static time_t long_out_t;
	static int first = 0; //TODO Move to detect()
	int i;

	devices = thr->cgpu->devices;
	chip_n = thr->cgpu->chip_n;

	if (!first) {
		for (i = 0; i < chip_n; i++) {
			devices[i].osc6_bits = 54;
		}
		for (i = 0; i < chip_n; i++) {
			send_reinit(devices[i].spi, devices[i].slot, devices[i].fasync, devices[i].osc6_bits);
		}
	}
	first = 1;

	for (chip = 0; chip < chip_n; chip++) {
		devices[chip].spi = sys_spi;
		devices[chip].job_switched = 0;
		if(!devices[chip].work) {
			devices[chip].work = get_queued(thr->cgpu);
			if (devices[chip].work == NULL) {
				return 0;
			}
			work_to_payload(&(devices[chip].payload), devices[chip].work);
		}
	}

	libbitfury_sendHashData(devices, chip_n);
	cgsleep_ms(5);

	cgtime(&now);
	chip = 0;
	for (;chip < chip_n; chip++) {
		if (devices[chip].job_switched) {
			int i,j;
			unsigned int *res = devices[chip].results;
			struct work *work = devices[chip].work;
			struct work *owork = devices[chip].owork;
			struct work *o2work = devices[chip].o2work;
			i = devices[chip].results_n;
			for (j = i - 1; j >= 0; j--) {
				if (owork) {
					submit_nonce(thr, owork, bswap_32(res[j]));
					devices[chip].stat_ts[devices[chip].stat_counter++] =
						now.tv_sec;
					if (devices[chip].stat_counter == BITFURY_STAT_N) {
						devices[chip].stat_counter = 0;
					}
				}
				if (o2work) {
					// TEST
					//submit_nonce(thr, owork, bswap_32(res[j]));
				}
			}
			devices[chip].results_n = 0;
			devices[chip].job_switched = 0;
			if (devices[chip].old_nonce && o2work) {
					submit_nonce(thr, o2work, bswap_32(devices[chip].old_nonce));
					i++;
			}
			if (devices[chip].future_nonce) {
					submit_nonce(thr, work, bswap_32(devices[chip].future_nonce));
					i++;
			}

			if (o2work)
				work_completed(thr->cgpu, o2work);

			devices[chip].o2work = devices[chip].owork;
			devices[chip].owork = devices[chip].work;
			devices[chip].work = NULL;
			hashes += 0xffffffffull * i;
		}
	}

	if (now.tv_sec - short_out_t > short_stat) {
		int shares_first = 0, shares_last = 0, shares_total = 0;
		char stat_lines[32][256] = {{0}};
		int len, k;
		double gh[32][8] = {{0}};
		double ghsum = 0, gh1h = 0, gh2h = 0;

		for (chip = 0; chip < chip_n; chip++) {
			int shares_found = calc_stat(devices[chip].stat_ts, short_stat, now);
			double ghash;
			len = strlen(stat_lines[devices[chip].slot]);
			ghash = shares_to_ghashes(shares_found, short_stat);
			gh[devices[chip].slot][chip & 0x07] = ghash;
			snprintf(stat_lines[devices[chip].slot] + len, 256 - len, "%.1f-%3.0f ", ghash, devices[chip].mhz);

			if(short_out_t && ghash < 1.0) {
				applog(LOG_WARNING, "Chip_id %d FREQ CHANGE\n", chip);
				send_freq(devices[chip].spi, devices[chip].slot, devices[chip].fasync, devices[chip].osc6_bits - 1);
				cgsleep_ms(1);
				send_freq(devices[chip].spi, devices[chip].slot, devices[chip].fasync, devices[chip].osc6_bits);
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

		for (chip = 0; chip < chip_n; chip++) {
			int shares_found = calc_stat(devices[chip].stat_ts, long_stat, now);
			double ghash;
			len = strlen(stat_lines[devices[chip].slot]);
			ghash = shares_to_ghashes(shares_found, long_stat);
			gh[devices[chip].slot][chip & 0x07] = ghash;
			snprintf(stat_lines[devices[chip].slot] + len, 256 - len, "%.1f-%3.0f ", ghash, devices[chip].mhz);

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

	return hashes;
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
	int chip_n;

	chip_n = thr->cgpu->chip_n;

	applog(LOG_INFO, "INFO bitfury_shutdown");
	libbitfury_shutdownChips(thr->cgpu->devices, chip_n);
}

struct device_drv bitfury_drv = {
	.dname = "bitfury",
	.name = "BFY",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.scanwork = bitfury_scanHash,
	.thread_shutdown = bitfury_shutdown,
	.minerloop = hash_queued_work,
};

