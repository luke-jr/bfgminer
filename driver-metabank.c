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
#include "driver-bitfury.h"
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
		bitfury->osc6_bits = 54;
		send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	}
	
	free(devicelist);
	
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

static struct api_data *metabank_api_extra_device_status(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	float vc0, vc1;
	struct bitfury_device * const bitfury = cgpu->device_data;

	root = api_add_uint(root, "Slot", &(bitfury->slot), false);
	root = api_add_int(root, "Clock Bits", (int*)&bitfury->osc6_bits, false);

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
	.get_api_extra_device_status = metabank_api_extra_device_status,
	.get_stats = metabank_get_stats,
};
