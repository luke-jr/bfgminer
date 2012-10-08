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

struct device_api x6500_api = {
	.dname = "x6500",
	.name = "XBS",
	.api_detect = x6500_detect,
// 	.thread_init = x6500_fpga_init,
// 	.scanhash = x6500_fpga_scanhash,
// 	.thread_shutdown = x6500_fpga_shutdown,
};
