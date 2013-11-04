/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libusb.h>

#include "fpgautils.h"
#include "logging.h"
#include "lowlevel.h"
#include "miner.h"
#include "util.h"

static
char *lowl_libusb_dup_string(libusb_device_handle * const handle, const uint8_t idx, const char * const idxname, const char * const fname)
{
	if (!idx)
		return NULL;
	unsigned char buf[0x100];
	const int n = libusb_get_string_descriptor_ascii(handle, idx, buf, sizeof(buf)-1);
	if (unlikely(n < 0)) {
		applog(LOG_ERR, "%s: Error getting USB string %d (%s): %s",
		       fname, idx, idxname, bfg_strerror(n, BST_LIBUSB));
		return NULL;
	}
	if (n == 0)
		return NULL;
	buf[n] = '\0';
	return strdup((void*)buf);
}


static
void usb_devinfo_free(struct lowlevel_device_info * const info)
{
	libusb_device * const dev = info->lowl_data;
	if (dev)
		libusb_unref_device(dev);
}

static
struct lowlevel_device_info *usb_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_list = NULL;
	ssize_t count, i;
	libusb_device **list;
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle;
	struct lowlevel_device_info *info;
	int err;

	if (unlikely(!have_libusb))
		return NULL;
	
	count = libusb_get_device_list(NULL, &list);
	if (unlikely(count < 0)) {
		applog(LOG_ERR, "%s: Error getting USB device list: %s",
		       __func__, bfg_strerror(count, BST_LIBUSB));
		return NULL;
	}

	for (i = 0; i < count; ++i) {
		err = libusb_get_device_descriptor(list[i], &desc);
		if (unlikely(err)) {
			applog(LOG_ERR, "%s: Error getting device descriptor: %s",
			       __func__, bfg_strerror(err, BST_LIBUSB));
			continue;
		}

		info = malloc(sizeof(struct lowlevel_device_info));
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_usb,
			.devid = bfg_make_devid_libusb(list[i]),
			.lowl_data = libusb_ref_device(list[i]),
			.vid = desc.idVendor,
			.pid = desc.idProduct,
		};
		
		err = libusb_open(list[i], &handle);
		if (unlikely(err))
			applog(LOG_ERR, "%s: Error opening device: %s",
			       __func__, bfg_strerror(err, BST_LIBUSB));
		else
		{
			info->manufacturer = lowl_libusb_dup_string(handle, desc.iManufacturer, "iManufacturer", __func__);
			info->product = lowl_libusb_dup_string(handle, desc.iProduct, "iProduct", __func__);
			info->serial = lowl_libusb_dup_string(handle, desc.iSerialNumber, "iSerialNumber", __func__);
			libusb_close(handle);
		}

		LL_PREPEND(devinfo_list, info);
	}

	libusb_free_device_list(list, 1);
	
	return devinfo_list;
}

struct libusb_device_handle *lowl_usb_open(struct lowlevel_device_info * const info)
{
	libusb_device * const dev = info->lowl_data;
	
	if (!dev)
		return NULL;
	
	libusb_device_handle *devh;

	if (libusb_open(dev, &devh)) {
		applog(LOG_ERR, "%s: Error opening device", __func__);
		return NULL;
	}
	return devh;
}

void lowl_usb_close(struct libusb_device_handle * const devh)
{
	libusb_close(devh);
}

struct lowlevel_driver lowl_usb = {
	.dname = "usb",
	.devinfo_scan = usb_devinfo_scan,
	.devinfo_free = usb_devinfo_free,
};
