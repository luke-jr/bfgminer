/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <libusb-1.0/libusb.h>

#include "dynclock.h"
#include "jtag.h"
#include "logging.h"
#include "miner.h"
#include "fpgautils.h"
#include "ft232r.h"

#define X6500_USB_PRODUCT "X6500 FPGA Miner"
#define X6500_BITSTREAM_FILENAME "fpgaminer_top_fixed7_197MHz.bit"
#define X6500_BISTREAM_USERID "\2\4$B"
#define X6500_MINIMUM_CLOCK    2
#define X6500_DEFAULT_CLOCK  200
#define X6500_MAXIMUM_CLOCK  210

struct device_api x6500_api;

static bool x6500_foundusb(libusb_device *dev, const char *product, const char *serial)
{
	struct cgpu_info *x6500;
	x6500 = calloc(1, sizeof(*x6500));
	x6500->api = &x6500_api;
	mutex_init(&x6500->device_mutex);
	x6500->device_path = strdup(serial);
	x6500->device_fd = -1;
	x6500->deven = DEV_ENABLED;
	x6500->threads = 2;
	x6500->name = strdup(product);
	x6500->cutofftemp = 85;
	x6500->cgpu_data = dev;

	return add_cgpu(x6500);
}

static bool x6500_detect_one(const char *serial)
{
	return ft232r_detect(X6500_USB_PRODUCT, serial, x6500_foundusb);
}

static int x6500_detect_auto()
{
	return ft232r_detect(X6500_USB_PRODUCT, NULL, x6500_foundusb);
}

static void x6500_detect()
{
	serial_detect_auto(&x6500_api, x6500_detect_one, x6500_detect_auto);
}

static bool x6500_prepare(struct thr_info *thr)
{
	if (thr->device_thread)
		return true;
	
	struct cgpu_info *x6500 = thr->cgpu;
	mutex_init(&x6500->device_mutex);
	struct ft232r_device_handle *ftdi = ft232r_open(x6500->cgpu_data);
	x6500->cgpu_data = NULL;
	if (!ftdi)
		return false;
	if (!ft232r_set_bitmode(ftdi, 0xee, 4))
		return false;
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		return false;
	x6500->device_ft232r = ftdi;
	x6500->cgpu_data = calloc(1, 1);
	
	return true;
}

struct x6500_fpga_data {
	struct jtag_port jtag;
};

static bool x6500_fpga_init(struct thr_info *thr)
{
	struct cgpu_info *x6500 = thr->cgpu;
	struct ft232r_device_handle *ftdi = x6500->device_ft232r;
	struct x6500_fpga_data *fpga;
	uint8_t pinoffset = thr->device_thread ? 0x10 : 1;
	
	if (!ftdi)
		return false;
	
	fpga = calloc(1, sizeof(*fpga));
	fpga->jtag.ftdi = ftdi;
	fpga->jtag.tck = pinoffset << 3;
	fpga->jtag.tms = pinoffset << 2;
	fpga->jtag.tdi = pinoffset << 1;
	fpga->jtag.tdo = pinoffset << 0;
	fpga->jtag.ignored = ~(fpga->jtag.tdo | fpga->jtag.tdi | fpga->jtag.tms | fpga->jtag.tck);
	fpga->jtag.state = x6500->cgpu_data;
	
	applog(LOG_ERR, "jtag pins: tck=%02x tms=%02x tdi=%02x tdo=%02x", pinoffset << 3, pinoffset << 2, pinoffset << 1, pinoffset << 0);
	
	mutex_lock(&x6500->device_mutex);
	if (!jtag_reset(&fpga->jtag)) {
		applog(LOG_ERR, "jtag reset failed");
		return false;
	}
	applog(LOG_ERR, "jtag detect returned %d", (int)jtag_detect(&fpga->jtag));
	mutex_unlock(&x6500->device_mutex);
}

struct device_api x6500_api = {
	.dname = "x6500",
	.name = "XBS",
	.api_detect = x6500_detect,
	.thread_prepare = x6500_prepare,
	.thread_init = x6500_fpga_init,
// 	.scanhash = x6500_fpga_scanhash,
// 	.thread_shutdown = x6500_fpga_shutdown,
};
