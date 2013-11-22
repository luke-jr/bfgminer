/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libusb.h>

#include "logging.h"
#include "lowlevel.h"
#include "lowl-usb.h"
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
		// This could be LOG_ERR, but it's annoyingly common :/
		applog(LOG_DEBUG, "%s: Error getting USB string %d (%s): %s",
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

bool lowl_usb_attach_kernel_driver(const struct lowlevel_device_info * const info)
{
	libusb_device * const dev = info->lowl_data;
	libusb_device_handle *devh;
	bool rv = false;
	
	if (libusb_open(dev, &devh))
		return false;
	
	if (libusb_kernel_driver_active(devh, 0) == 0)
		if (!libusb_attach_kernel_driver(devh, 0))
		{
			applog(LOG_DEBUG, "Reattaching kernel driver for %s", info->devid);
			rv = true;
		}
	
	libusb_close(devh);
	
	return rv;
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

struct device_drv *bfg_claim_usb(struct device_drv * const api, const bool verbose, const uint8_t usbbus, const uint8_t usbaddr)
{
	char * const devpath = bfg_make_devid_usb(usbbus, usbaddr);
	struct device_drv * const rv = bfg_claim_any(api, verbose ? "" : NULL, devpath);
	free(devpath);
	return rv;
}

#ifdef HAVE_LIBUSB
void cgpu_copy_libusb_strings(struct cgpu_info *cgpu, libusb_device *usb)
{
	unsigned char buf[0x20];
	libusb_device_handle *h;
	struct libusb_device_descriptor desc;
	
	if (LIBUSB_SUCCESS != libusb_open(usb, &h))
		return;
	if (libusb_get_device_descriptor(usb, &desc))
	{
		libusb_close(h);
		return;
	}
	
	if ((!cgpu->dev_manufacturer) && libusb_get_string_descriptor_ascii(h, desc.iManufacturer, buf, sizeof(buf)) >= 0)
		cgpu->dev_manufacturer = strdup((void *)buf);
	if ((!cgpu->dev_product) && libusb_get_string_descriptor_ascii(h, desc.iProduct, buf, sizeof(buf)) >= 0)
		cgpu->dev_product = strdup((void *)buf);
	if ((!cgpu->dev_serial) && libusb_get_string_descriptor_ascii(h, desc.iSerialNumber, buf, sizeof(buf)) >= 0)
		cgpu->dev_serial = strdup((void *)buf);
	
	libusb_close(h);
}
#endif

void lowl_usb_close(struct libusb_device_handle * const devh)
{
	libusb_close(devh);
}

struct lowlevel_driver lowl_usb = {
	.dname = "usb",
	.devinfo_scan = usb_devinfo_scan,
	.devinfo_free = usb_devinfo_free,
};
