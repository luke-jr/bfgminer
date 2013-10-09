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
int bfsb_autodetect()
{
	RUNONCE(0);
	
	struct cgpu_info *cgpu = NULL, *proc1 = NULL, *prev_cgpu = NULL;
	int proc_count = 0;
	
	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	
	
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int i, j;
	bool slot_on[32];
	struct timespec t1, t2;
	struct bitfury_device dummy_bitfury;
	struct cgpu_info dummy_cgpu;

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
				
				devicelist = malloc(sizeof(*devicelist) * chip_n);
				for (j = 0; j < chip_n; ++j)
				{
					devicelist[j] = bitfury = malloc(sizeof(*bitfury));
					*bitfury = (struct bitfury_device){
						.spi = port,
						.slot = i,
						.fasync = j,
					};
				}
				
				cgpu = malloc(sizeof(*cgpu));
				*cgpu = (struct cgpu_info){
					.drv = &bfsb_drv,
					.procs = chip_n,
					.device_data = devicelist,
				};
				add_cgpu_slave(cgpu, prev_cgpu);
				
				proc_count += chip_n;
				if (!proc1)
					proc1 = cgpu;
				prev_cgpu = cgpu;
			}
			else
				free(port);
		}
	}

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t2);
	
	if (proc1)
		proc1->threads = 1;
	
	return proc_count;
}

static void bfsb_detect(void)
{
	noserial_detect_manual(&bfsb_drv, bfsb_autodetect);
}

static
bool bfsb_init(struct thr_info *thr)
{
	struct bitfury_device **devicelist;
	struct cgpu_info *proc;
	struct bitfury_device *bitfury;
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
	{
		devicelist = proc->device_data;
		bitfury = devicelist[proc->proc_id];
		proc->device_data = bitfury;
		bitfury->spi->cgpu = proc;
		bitfury_init_chip(proc);
		bitfury->osc6_bits = 54;
		send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		
		if (proc->proc_id == proc->procs - 1)
			free(devicelist);
	}
	
	timer_set_now(&thr->tv_poll);
	
	return true;
}

static
void bfsb_disable(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	
	applog(LOG_DEBUG, "%"PRIpreprv": Shutting down chip (disable)", proc->proc_repr);
	send_shutdown(bitfury->spi, bitfury->slot, bitfury->fasync);
}

static
void bfsb_enable(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	
	applog(LOG_DEBUG, "%"PRIpreprv": Reinitialising chip (enable)", proc->proc_repr);
	send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	bitfury_init_chip(proc);
}

extern void bitfury_shutdown(struct thr_info *);

static void bfsb_shutdown(struct thr_info *thr)
{
	bitfury_shutdown(thr);
	spi_bfsb_select_bank(-1);
}

static struct api_data *bfsb_api_device_detail(struct cgpu_info *cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct api_data *root = bitfury_api_device_detail(cgpu);
	
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
	.job_start = bitfury_noop_job_start,
	.job_process_results = bitfury_job_process_results,
	.get_api_extra_device_detail = bfsb_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	.set_device = bitfury_set_device,
	.thread_disable = bfsb_disable,
	.thread_enable = bfsb_enable,
	.thread_shutdown = bfsb_shutdown,
};
