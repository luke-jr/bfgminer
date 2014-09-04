/*
 * Copyright 2012-2014 Luke Dashjr
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
char *lowl_libusb_dup_string(libusb_device_handle * const handle, const uint8_t idx, const char * const idxname, const char * const fname, const char * const devid)
{
	if (!idx)
		return NULL;
	unsigned char buf[0x100];
	const int n = libusb_get_string_descriptor_ascii(handle, idx, buf, sizeof(buf)-1);
	if (unlikely(n < 0)) {
		// This could be LOG_ERR, but it's annoyingly common :/
		applog(LOG_DEBUG, "%s: Error getting USB string %d (%s) from %s: %s",
		       fname, idx, idxname, devid, bfg_strerror(n, BST_LIBUSB));
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
			applog(LOG_DEBUG, "%s: Error opening device %s: %s",
			       __func__, info->devid, bfg_strerror(err, BST_LIBUSB));
		else
		{
			info->manufacturer = lowl_libusb_dup_string(handle, desc.iManufacturer, "iManufacturer", __func__, info->devid);
			info->product = lowl_libusb_dup_string(handle, desc.iProduct, "iProduct", __func__, info->devid);
			info->serial = lowl_libusb_dup_string(handle, desc.iSerialNumber, "iSerialNumber", __func__, info->devid);
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

struct lowl_usb_endpoint {
	struct libusb_device_handle *devh;
	
	unsigned char endpoint_r;
	int packetsz_r;
	bytes_t _buf_r;
	unsigned timeout_ms_r;
	
	unsigned char endpoint_w;
	int packetsz_w;
	unsigned timeout_ms_w;
};

struct lowl_usb_endpoint *usb_open_ep(struct libusb_device_handle * const devh, const uint8_t epid, const int pktsz)
{
	struct lowl_usb_endpoint * const ep = malloc(sizeof(*ep));
	ep->devh = devh;
	if (epid & 0x80)
	{
		// Read endpoint
		ep->endpoint_r = epid;
		ep->packetsz_r = pktsz;
		bytes_init(&ep->_buf_r);
	}
	else
	{
		// Write endpoint
		ep->endpoint_w = epid;
		ep->packetsz_w = epid;
		ep->packetsz_r = -1;
	}
	return ep;
};

struct lowl_usb_endpoint *usb_open_ep_pair(struct libusb_device_handle * const devh, const uint8_t epid_r, const int pktsz_r, const uint8_t epid_w, const int pktsz_w)
{
	struct lowl_usb_endpoint * const ep = malloc(sizeof(*ep));
	*ep = (struct lowl_usb_endpoint){
		.devh = devh,
		.endpoint_r = epid_r,
		.packetsz_r = pktsz_r,
		._buf_r = BYTES_INIT,
		.endpoint_w = epid_w,
		.packetsz_w = pktsz_w,
	};
	return ep;
}

void usb_ep_set_timeouts_ms(struct lowl_usb_endpoint * const ep, const unsigned timeout_ms_r, const unsigned timeout_ms_w)
{
	ep->timeout_ms_r = timeout_ms_r;
	ep->timeout_ms_w = timeout_ms_w;
}

ssize_t usb_read(struct lowl_usb_endpoint * const ep, void * const data, size_t datasz)
{
	unsigned timeout;
	size_t xfer;
	if ( (xfer = bytes_len(&ep->_buf_r)) < datasz)
	{
		bytes_extend_buf(&ep->_buf_r, datasz + ep->packetsz_r - 1);
		unsigned char *p = &bytes_buf(&ep->_buf_r)[xfer];
		int pxfer;
		int rem = datasz - xfer, rsz;
		timeout = xfer ? 0 : ep->timeout_ms_r;
		while (rem > 0)
		{
			rsz = (rem / ep->packetsz_r) * ep->packetsz_r;
			if (rsz < rem)
				rsz += ep->packetsz_r;
			switch (libusb_bulk_transfer(ep->devh, ep->endpoint_r, p, rsz, &pxfer, timeout))
			{
				case 0:
				case LIBUSB_ERROR_TIMEOUT:
					if (!pxfer)
						// Behaviour is like tcsetattr-style timeout
						return 0;
					p += pxfer;
					rem -= pxfer;
					// NOTE: Need to maintain _buf_r length so data is saved in case of error
					xfer += pxfer;
					bytes_resize(&ep->_buf_r, xfer);
					break;
				case LIBUSB_ERROR_PIPE:
				case LIBUSB_ERROR_NO_DEVICE:
					errno = EPIPE;
					return -1;
				default:
					errno = EIO;
					return -1;
			}
			timeout = 0;
		}
	}
	memcpy(data, bytes_buf(&ep->_buf_r), datasz);
	bytes_shift(&ep->_buf_r, datasz);
	return datasz;
}

ssize_t usb_write(struct lowl_usb_endpoint * const ep, const void * const data, size_t datasz)
{
	unsigned timeout = ep->timeout_ms_w;
	unsigned char *p = (void*)data;
	size_t rem = datasz;
	int pxfer;
	while (rem > 0)
	{
		switch (libusb_bulk_transfer(ep->devh, ep->endpoint_w, p, rem, &pxfer, timeout))
		{
			case 0:
			case LIBUSB_ERROR_TIMEOUT:
				p += pxfer;
				rem -= pxfer;
				break;
			case LIBUSB_ERROR_PIPE:
			case LIBUSB_ERROR_NO_DEVICE:
				errno = EPIPE;
				return (datasz - rem) ?: -1;
			default:
				errno = EIO;
				return (datasz - rem) ?: -1;
		}
		timeout = 0;
	}
	errno = 0;
	return datasz;
}

void usb_close_ep(struct lowl_usb_endpoint * const ep)
{
	if (ep->packetsz_r != -1)
		bytes_free(&ep->_buf_r);
	free(ep);
}

struct lowlevel_driver lowl_usb = {
	.dname = "usb",
	.devinfo_scan = usb_devinfo_scan,
	.devinfo_free = usb_devinfo_free,
};
