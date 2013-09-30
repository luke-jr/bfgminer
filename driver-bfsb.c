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
#include "driver-bitfury.h"

struct device_drv bfsb_drv;

static
bool bfsb_spi_txrx(struct spi_port *port)
{
	struct cgpu_info * const proc = port->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	spi_bfsb_select_bank(bitfury->slot);
	const bool rv = sys_spi_txrx(port);

	return rv;
}

static
struct bitfury_device **bfsb_detect_chips(int *out_count) {
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int n = 0;
	int i, j;
	bool slot_on[32];
	struct timespec t1, t2;
	struct bitfury_device dummy_bitfury;
	struct cgpu_info dummy_cgpu;
	int max_devices = 100;


	devicelist = malloc(max_devices * sizeof(*devicelist));
	dummy_cgpu.device_data = &dummy_bitfury;
	
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t1);
	for (i = 0; i < 4; i++) {
		slot_on[i] = 1;
	}

	for (i = 0; i < 32; i++) {
		if (slot_on[i]) {
			int chip_n;
			
			port = malloc(sizeof(*port));
			*port = *sys_spi;
			port->cgpu = &dummy_cgpu;
			port->txrx = bfsb_spi_txrx;
			port->speed = 625000;
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
int bfsb_autodetect()
{
	RUNONCE(0);
	
	int chip_n;
	struct cgpu_info *bitfury_info;

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bfsb_drv;
	bitfury_info->threads = 1;

	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	bitfury_info->device_data = bfsb_detect_chips(&chip_n);
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

static void bfsb_detect(void)
{
	noserial_detect_manual(&bfsb_drv, bfsb_autodetect);
}

static
bool bfsb_init(struct thr_info *thr)
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
		bitfury->osc6_bits = 54;
		send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	}
	
	free(devicelist);
	
	return true;
}

extern void bitfury_shutdown(struct thr_info *);

static void bfsb_shutdown(struct thr_info *thr)
{
	bitfury_shutdown(thr);
	spi_bfsb_select_bank(-1);
}

static struct api_data *bfsb_api_device_status(struct cgpu_info *cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct api_data *root = bitfury_api_device_status(cgpu);
	
	root = api_add_uint(root, "Slot", &(bitfury->slot), false);
	
	return root;
}

struct device_drv bfsb_drv = {
	.dname = "bfsb",
	.name = "BSB",
	.drv_detect = bfsb_detect,
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.thread_init = bfsb_init,
	.poll = bitfury_do_io,
	.job_start = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	.get_api_extra_device_status = bfsb_api_device_status,
	.thread_shutdown = bfsb_shutdown,
};
