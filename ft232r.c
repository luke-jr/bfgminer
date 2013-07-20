/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <libusb.h>

#include "compat.h"
#include "fpgautils.h"
#include "ft232r.h"
#include "logging.h"
#include "miner.h"

#define FT232R_IDVENDOR   0x0403
#define FT232R_IDPRODUCT  0x6001

struct ft232r_device_info {
	libusb_device *libusb_dev;
	char *product;
	char *serial;
};

static struct ft232r_device_info **ft232r_devinfo_list;

void ft232r_scan_free()
{
	if (!ft232r_devinfo_list)
		return;

	struct ft232r_device_info *info;

	for (struct ft232r_device_info **infop = ft232r_devinfo_list; (info = *infop); ++infop) {
		libusb_unref_device(info->libusb_dev);
		free(info->product);
		free(info->serial);
		free(info);
	}
	free(ft232r_devinfo_list);
	ft232r_devinfo_list = NULL;
}

void ft232r_scan()
{
	ssize_t count, n, i, found = 0;
	libusb_device **list;
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle;
	struct ft232r_device_info *info;
	int err;
	unsigned char buf[0x100];
	int skipped = 0;

	ft232r_scan_free();

	count = libusb_get_device_list(NULL, &list);
	if (unlikely(count < 0)) {
		applog(LOG_ERR, "ft232r_scan: Error getting USB device list: %s", bfg_strerror(count, BST_LIBUSB));
		ft232r_devinfo_list = calloc(1, sizeof(struct ft232r_device_info *));
		return;
	}

	ft232r_devinfo_list = malloc(sizeof(struct ft232r_device_info *) * (count + 1));

	for (i = 0; i < count; ++i) {
		if (bfg_claim_libusb(NULL, false, list[i]))
		{
			++skipped;
			continue;
		}
		
		err = libusb_get_device_descriptor(list[i], &desc);
		if (unlikely(err)) {
			applog(LOG_ERR, "ft232r_scan: Error getting device descriptor: %s", bfg_strerror(err, BST_LIBUSB));
			continue;
		}
		if (!(desc.idVendor == FT232R_IDVENDOR && desc.idProduct == FT232R_IDPRODUCT)) {
			applog(LOG_DEBUG, "ft232r_scan: Found %04x:%04x - not a ft232r", desc.idVendor, desc.idProduct);
			continue;
		}

		err = libusb_open(list[i], &handle);
		if (unlikely(err)) {
			applog(LOG_ERR, "ft232r_scan: Error opening device: %s", bfg_strerror(err, BST_LIBUSB));
			continue;
		}

		n = libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf)-1);
		if (unlikely(n < 0)) {
			libusb_close(handle);
			applog(LOG_ERR, "ft232r_scan: Error getting iProduct string: %s", bfg_strerror(n, BST_LIBUSB));
			continue;
		}
		buf[n] = '\0';
		info = malloc(sizeof(struct ft232r_device_info));
		info->product = strdup((char*)buf);

		n = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buf, sizeof(buf)-1);
		libusb_close(handle);
		if (unlikely(n < 0)) {
			applog(LOG_ERR, "ft232r_scan: Error getting iSerialNumber string: %s", bfg_strerror(n, BST_LIBUSB));
			n = 0;
		}
		buf[n] = '\0';
		info->serial = strdup((char*)buf);
		info->libusb_dev = libusb_ref_device(list[i]);
		ft232r_devinfo_list[found++] = info;

		applog(LOG_DEBUG, "ft232r_scan: Found \"%s\" serial \"%s\"", info->product, info->serial);
	}

	ft232r_devinfo_list[found] = NULL;
	libusb_free_device_list(list, 1);
	
	if (skipped)
		applog(LOG_DEBUG, "%s: Skipping probe of %d claimed devices", __func__, skipped);
}

int ft232r_detect(const char *product_needle, const char *serial, foundusb_func_t cb)
{
	struct ft232r_device_info *info;
	int found = 0;

	for (struct ft232r_device_info **infop = ft232r_devinfo_list; (info = *infop); ++infop) {
		if (serial) {
			// If we are searching for a specific serial, pay no attention to the product id
			if (strcmp(serial, info->serial))
				continue;
		}
		else
		if (!strstr(info->product, product_needle))
			continue;
		if (!info->libusb_dev)
			continue;
		if (!cb(info->libusb_dev, info->product, info->serial))
			continue;
		info->libusb_dev = NULL;
		++found;
	}

	return found;
}

#define FTDI_REQTYPE  (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE)
#define FTDI_REQTYPE_IN   (FTDI_REQTYPE | LIBUSB_ENDPOINT_IN)
#define FTDI_REQTYPE_OUT  (FTDI_REQTYPE | LIBUSB_ENDPOINT_OUT)

#define FTDI_REQUEST_RESET           0
#define FTDI_REQUEST_SET_BAUDRATE    3
#define FTDI_REQUEST_SET_BITMODE  0x0b
#define FTDI_REQUEST_GET_PINS     0x0c
#define FTDI_REQUEST_GET_BITMODE  0x0c

#define FTDI_BAUDRATE_3M  0,0

#define FTDI_INDEX       1
#define FTDI_TIMEOUT  1000

struct ft232r_device_handle {
	libusb_device_handle *h;
	uint8_t i;
	uint8_t o;
	unsigned char ibuf[256];
	int ibufLen;
	uint16_t osz;
	unsigned char *obuf;
	uint16_t obufsz;
};

struct ft232r_device_handle *ft232r_open(libusb_device *dev)
{
	// FIXME: Cleanup on errors
	libusb_device_handle *devh;
	struct ft232r_device_handle *ftdi;

	if (libusb_open(dev, &devh)) {
		applog(LOG_ERR, "ft232r_open: Error opening device");
		return NULL;
	}
	libusb_reset_device(devh);
	libusb_detach_kernel_driver(devh, 0);
	if (libusb_set_configuration(devh, 1)) {
		applog(LOG_ERR, "ft232r_open: Error setting configuration");
		return NULL;
	}
	if (libusb_claim_interface(devh, 0)) {
		applog(LOG_ERR, "ft232r_open: Error claiming interface");
		return NULL;
	}
	if (libusb_control_transfer(devh, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BAUDRATE, FTDI_BAUDRATE_3M, NULL, 0, FTDI_TIMEOUT) < 0) {
		applog(LOG_ERR, "ft232r_open: Error performing control transfer");
		return NULL;
	}

	struct libusb_config_descriptor *cfg;
	if (libusb_get_config_descriptor(dev, 0, &cfg)) {
		applog(LOG_ERR, "ft232r_open: Error getting config descriptor");
		return NULL;
	}
	const struct libusb_interface_descriptor *altcfg = &cfg->interface[0].altsetting[0];
	if (altcfg->bNumEndpoints < 2) {
		applog(LOG_ERR, "ft232r_open: Too few endpoints");
		return NULL;
	}
	ftdi = calloc(1, sizeof(*ftdi));
	ftdi->h = devh;
	ftdi->i = altcfg->endpoint[0].bEndpointAddress;
	ftdi->o = altcfg->endpoint[1].bEndpointAddress;
	ftdi->osz = 0x1000;
	ftdi->obuf = malloc(ftdi->osz);
	libusb_free_config_descriptor(cfg);

	return ftdi;
}

void ft232r_close(struct ft232r_device_handle *dev)
{
	libusb_release_interface(dev->h, 0);
	libusb_reset_device(dev->h);
	libusb_close(dev->h);
}

bool ft232r_purge_buffers(struct ft232r_device_handle *dev, enum ft232r_reset_purge purge)
{
	if (ft232r_flush(dev) < 0)
		return false;

	if (purge & FTDI_PURGE_RX) {
		if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_PURGE_RX, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
			return false;
		dev->ibufLen = 0;
	}
	if (purge & FTDI_PURGE_TX)
		if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_PURGE_TX, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
			return false;
	return true;
}

bool ft232r_set_bitmode(struct ft232r_device_handle *dev, uint8_t mask, uint8_t mode)
{
	if (ft232r_flush(dev) < 0)
		return false;
	if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BITMODE, mask, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
		return false;
	return !libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BITMODE, (mode << 8) | mask, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT);
}

static ssize_t ft232r_readwrite(struct ft232r_device_handle *dev, unsigned char endpoint, void *data, size_t count)
{
	int transferred;
	switch (libusb_bulk_transfer(dev->h, endpoint, data, count, &transferred, FTDI_TIMEOUT)) {
		case LIBUSB_ERROR_TIMEOUT:
			if (!transferred) {
				errno = ETIMEDOUT;
				return -1;
			}
		case 0:
			return transferred;
		default:
			errno = EIO;
			return -1;
	}
}

ssize_t ft232r_flush(struct ft232r_device_handle *dev)
{
	if (!dev->obufsz)
		return 0;
	ssize_t r = ft232r_readwrite(dev, dev->o, dev->obuf, dev->obufsz);
	if (r == dev->obufsz) {
		dev->obufsz = 0;
	} else if (r > 0) {
		dev->obufsz -= r;
		memmove(dev->obuf, &dev->obuf[r], dev->obufsz);
	}
	return r;
}

ssize_t ft232r_write(struct ft232r_device_handle *dev, void *data, size_t count)
{
	uint16_t bufleft;
	ssize_t r;
	
	bufleft = dev->osz - dev->obufsz;
	
	if (count < bufleft) {
		// Just add to output buffer
		memcpy(&dev->obuf[dev->obufsz], data, count);
		dev->obufsz += count;
		return count;
	}
	
	// Fill up buffer and flush
	memcpy(&dev->obuf[dev->obufsz], data, bufleft);
	dev->obufsz += bufleft;
	r = ft232r_flush(dev);
	
	if (unlikely(r <= 0)) {
		// In this case, no bytes were written supposedly, so remove this data from buffer
		dev->obufsz -= bufleft;
		return r;
	}
	
	// Even if not all <bufleft> bytes from this write got out, the remaining are still buffered
	return bufleft;
}

typedef ssize_t (*ft232r_rwfunc_t)(struct ft232r_device_handle *, void*, size_t);

static ssize_t ft232r_rw_all(ft232r_rwfunc_t rwfunc, struct ft232r_device_handle *dev, void *data, size_t count)
{
	char *p = data;
	ssize_t writ, total = 0;

	while (count && (writ = rwfunc(dev, p, count)) > 0) {
		p += writ;
		count -= writ;
		total += writ;
	}
	return total ?: writ;
}

ssize_t ft232r_write_all(struct ft232r_device_handle *dev, void *data, size_t count)
{
	return ft232r_rw_all(ft232r_write, dev, data, count);
}

ssize_t ft232r_read(struct ft232r_device_handle *dev, void *data, size_t count)
{
	ssize_t r;
	int adj;
	
	// Flush any pending output before reading
	r = ft232r_flush(dev);
	if (r < 0)
		return r;
	
	// First 2 bytes of every 0x40 are FTDI status or something
	while (dev->ibufLen <= 2) {
		// TODO: Implement a timeout for status byte repeating
		int transferred = ft232r_readwrite(dev, dev->i, dev->ibuf, sizeof(dev->ibuf));
		if (transferred <= 0)
			return transferred;
		dev->ibufLen = transferred;
		for (adj = 0x40; dev->ibufLen > adj; adj += 0x40 - 2) {
			dev->ibufLen -= 2;
			memmove(&dev->ibuf[adj], &dev->ibuf[adj+2], dev->ibufLen - adj);
		}
	}
	unsigned char *ibufs = &dev->ibuf[2];
	size_t ibufsLen = dev->ibufLen - 2;
	
	if (count > ibufsLen)
		count = ibufsLen;
	memcpy(data, ibufs, count);
	dev->ibufLen -= count;
	ibufsLen -= count;
	if (ibufsLen) {
		memmove(ibufs, &ibufs[count], ibufsLen);
		applog(LOG_DEBUG, "ft232r_read: %"PRIu64" bytes extra", (uint64_t)ibufsLen);
	}
	return count;
}

ssize_t ft232r_read_all(struct ft232r_device_handle *dev, void *data, size_t count)
{
	return ft232r_rw_all(ft232r_read, dev, data, count);
}

bool ft232r_get_pins(struct ft232r_device_handle *dev, uint8_t *pins)
{
	return libusb_control_transfer(dev->h, FTDI_REQTYPE_IN, FTDI_REQUEST_GET_PINS, 0, FTDI_INDEX, pins, 1, FTDI_TIMEOUT) == 1;
}

bool ft232r_get_bitmode(struct ft232r_device_handle *dev, uint8_t *out_mode)
{
	return libusb_control_transfer(dev->h, FTDI_REQTYPE_IN, FTDI_REQUEST_GET_BITMODE, 0, FTDI_INDEX, out_mode, 1, FTDI_TIMEOUT) == 1;
}

bool ft232r_set_cbus_bits(struct ft232r_device_handle *dev, bool sc, bool cs)
{
	uint8_t pin_state = (cs ? (1<<2) : 0)
	                  | (sc ? (1<<3) : 0);
	return ft232r_set_bitmode(dev, 0xc0 | pin_state, 0x20);
}

bool ft232r_get_cbus_bits(struct ft232r_device_handle *dev, bool *out_sio0, bool *out_sio1)
{
	uint8_t data;
	if (!ft232r_get_bitmode(dev, &data))
		return false;
	*out_sio0 = data & 1;
	*out_sio1 = data & 2;
	return true;
}

#if 0
int main() {
	libusb_init(NULL);
	ft232r_scan();
	ft232r_scan_free();
	libusb_exit(NULL);
}
void applog(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	puts("");
	va_end(ap);
}
#endif
