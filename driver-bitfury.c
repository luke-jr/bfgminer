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

// Forward declarations
static bool bitfury_prepare(struct thr_info *thr);

static
int bitfury_autodetect()
{
	RUNONCE(0);
	
	int chip_n;
	struct cgpu_info *bitfury_info;
	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	chip_n = libbitfury_detectChips(sys_spi);
	if (!chip_n) {
		applog(LOG_WARNING, "No Bitfury chips detected!");
		return 0;
	} else {
		applog(LOG_WARNING, "BITFURY: %d chips detected!", chip_n);
	}

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bitfury_drv;
	bitfury_info->threads = 1;
	bitfury_info->chip_n = chip_n;
	add_cgpu(bitfury_info);
	
	return 1;
}

static void bitfury_detect(void)
{
	noserial_detect_manual(&bitfury_drv, bitfury_autodetect);
}


static int64_t bitfury_scanHash(struct thr_info *thr)
{
	static struct bitfury_device devices[100];
	int chip_n;
	int chip;
	uint64_t hashes = 0;
	static bool second_run = false;

	chip_n = thr->cgpu->chip_n;

	for (chip = 0; chip < chip_n; chip++) {
		devices[chip].spi = sys_spi;
		if(!devices[chip].work) {
			devices[chip].work = get_queued(thr->cgpu);
			if (devices[chip].work == NULL) {
				return 0;
			}
			work_to_payload(&(devices[chip].payload), devices[chip].work);
		}
		
		devices[chip].chip = chip;
		payload_to_atrvec(devices[chip].atrvec, &devices[chip].payload);
		libbitfury_sendHashData1(&devices[chip], second_run);
		
		if (devices[chip].job_switched) {
			int i,j;
			unsigned int *res = devices[chip].results;
			struct work *owork = devices[chip].owork;
			i = devices[chip].results_n;
			for (j = i - 1; j >= 0; j--) {
				if (owork) {
					submit_nonce(thr, owork, bswap_32(res[j]));
				}
			}

			if (owork)
				work_completed(thr->cgpu, owork);

			devices[chip].owork = devices[chip].work;
			devices[chip].work = NULL;
			hashes += 0xffffffffull * i;
		}
	}
	
	second_run = true;
	
	return hashes;
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

