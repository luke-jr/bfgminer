/*
 * Copyright 2013 bitfury
 * Copyright 2013 legkodymov
 * Copyright 2013 Luke Dashjr
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

#include "deviceapi.h"
#include "libbitfury.h"
#include "spidevc.h"
#include "tm_i2c.h"

struct device_drv metabank_drv;

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
	bool slot_on[32];
	struct timespec t1, t2;
	struct bitfury_device dummy_bitfury;
	struct cgpu_info dummy_cgpu;
	int max_devices = 100;

	if (tm_i2c_init() < 0) {
		applog(LOG_DEBUG, "%s: I2C init error", metabank_drv.dname);
		*out_count = 0;
		return NULL;
	}


	devicelist = malloc(max_devices * sizeof(*devicelist));
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
					if (unlikely(n >= max_devices))
					{
						max_devices *= 2;
						devicelist = realloc(devicelist, max_devices * sizeof(*devicelist));
					}
					devicelist[n] = bitfury = malloc(sizeof(*bitfury));
					*bitfury = (struct bitfury_device){
						.spi = port,
						.slot = i,
						.fasync = j,
					};
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

static
int metabank_autodetect()
{
	RUNONCE(0);
	
	int chip_n;
	struct cgpu_info *bitfury_info;

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &metabank_drv;
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

static void metabank_detect(void)
{
	noserial_detect_manual(&metabank_drv, metabank_autodetect);
}

extern bool bitfury_prepare(struct thr_info *);

static
bool metabank_init(struct thr_info *thr)
{
	struct bitfury_device **devicelist = thr->cgpu->device_data;
	struct cgpu_info *proc;
	struct bitfury_device *bitfury;
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
	{
		bitfury = devicelist[proc->proc_id];
		proc->device_data = bitfury;
		bitfury->spi->cgpu = proc;
		bitfury_init_oldbuf(proc);
	}
	
	free(devicelist);
	
	return true;
}

extern int64_t bitfury_scanHash(struct thr_info *);
extern void bitfury_shutdown(struct thr_info *);

static void metabank_shutdown(struct thr_info *thr)
{
	bitfury_shutdown(thr);
	tm_i2c_close();
}

extern bool bitfury_job_prepare(struct thr_info *, struct work *, uint64_t max_nonce);
extern void bitfury_do_io(struct thr_info *);
extern int64_t bitfury_job_process_results(struct thr_info *, struct work *, bool stopping);

struct device_drv metabank_drv = {
	.dname = "metabank",
	.name = "MBF",
	.drv_detect = metabank_detect,
	.thread_init = metabank_init,
	
#if 0
	.minerloop = hash_queued_work,
	.thread_prepare = bitfury_prepare,
	.scanwork = bitfury_scanHash,
#endif
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_do_io,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.thread_shutdown = metabank_shutdown,
};
