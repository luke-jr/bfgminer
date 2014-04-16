/*
 * Copyright 2013 bitfury
 * Copyright 2013 Anatoly Legkodymov
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

#include <stdbool.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "libbitfury.h"
#include "lowl-spi.h"
#include "tm_i2c.h"

BFG_REGISTER_DRIVER(metabank_drv)

static
bool metabank_spi_txrx(struct spi_port *port)
{
	static int current_slot = -1;
	struct cgpu_info * const proc = port->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	
	if (current_slot != bitfury->slot)
	{
		if (current_slot != -1)
			tm_i2c_clear_oe(current_slot);
		tm_i2c_set_oe(bitfury->slot);
		current_slot = bitfury->slot;
	}
	
	const bool rv = sys_spi_txrx(port);
	return rv;
}

static
int metabank_autodetect()
{
	RUNONCE(0);
	
	struct cgpu_info *cgpu = NULL, *proc1 = NULL, *prev_cgpu = NULL;
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int i, j;
	int proc_count = 0;
	bool slot_on[32];
	struct bitfury_device dummy_bitfury;
	struct cgpu_info dummy_cgpu;
	
	applog(LOG_INFO, "INFO: bitfury_detect");
	spi_init();
	if (!sys_spi)
		return 0;
	
	if (tm_i2c_init() < 0) {
		applog(LOG_DEBUG, "%s: I2C init error", metabank_drv.dname);
		return 0;
	}
	
	dummy_cgpu.device_data = &dummy_bitfury;
	
	for (i = 0; i < 32; i++) {
		slot_on[i] = 0;
	}
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
					.drv = &metabank_drv,
					.set_device_funcs = bitfury_set_device_funcs,
					.procs = chip_n,
					.device_data = devicelist,
					.cutofftemp = 50,
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
	
	if (proc1)
		proc1->threads = 1;
	
	return proc_count;
}

static void metabank_detect(void)
{
	noserial_detect_manual(&metabank_drv, metabank_autodetect);
}

static
bool metabank_init(struct thr_info *thr)
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
		bitfury->osc6_bits = 53;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_freq_stat(&bitfury->chip_stat, 52, 56);
		
		if (proc->proc_id == proc->procs - 1)
			free(devicelist);
	}
	
	timer_set_now(&thr->tv_poll);
	
	return true;
}

static void metabank_shutdown(struct thr_info *thr)
{
	bitfury_shutdown(thr);
	tm_i2c_close();
}

static bool metabank_get_stats(struct cgpu_info *cgpu)
{
	struct bitfury_device * const bitfury = cgpu->device_data;
	float t;

	t = tm_i2c_gettemp(bitfury->slot) * 0.1;

	if (t < -27) //Sometimes tm_i2c_gettemp() returns strange result, ignoring it.
		return false;

	cgpu->temp = t;

	return true;
}

static struct api_data *metabank_api_extra_device_detail(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct bitfury_device * const bitfury = cgpu->device_data;
	
	root = bitfury_api_device_detail(cgpu);

	root = api_add_uint(root, "Slot", &(bitfury->slot), false);

	return root;
}

static struct api_data *metabank_api_extra_device_status(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	float vc0, vc1;
	struct bitfury_device * const bitfury = cgpu->device_data;
	
	root = bitfury_api_device_status(cgpu);

	vc0 = tm_i2c_getcore0(bitfury->slot);
	vc1 = tm_i2c_getcore1(bitfury->slot);

	root = api_add_volts(root, "Slot VC0", &vc0, true);
	root = api_add_volts(root, "Slot VC1", &vc1, true);

	return root;
}

struct device_drv metabank_drv = {
	.dname = "metabank",
	.name = "MBF",
	.drv_detect = metabank_detect,
	
	.thread_init = metabank_init,
	.thread_enable = bitfury_enable,
	.thread_disable = bitfury_disable,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.thread_shutdown = metabank_shutdown,
	.get_api_extra_device_detail = metabank_api_extra_device_detail,
	.get_api_extra_device_status = metabank_api_extra_device_status,
	.get_stats = metabank_get_stats,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
