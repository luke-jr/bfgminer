/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <libusb-1.0/libusb.h>

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
	ssize_t n, i, found = 0;
	libusb_device **list;
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle;
	struct ft232r_device_info *info;
	int err;
	unsigned char buf[0x100];

	ft232r_scan_free();

	n = libusb_get_device_list(NULL, &list);
	if (unlikely(n < 0)) {
		applog(LOG_ERR, "ft232r_scan: Error getting USB device list: %s", libusb_error_name(n));
		ft232r_devinfo_list = calloc(1, sizeof(struct ft232r_device_info *));
		return;
	}

	ft232r_devinfo_list = malloc(sizeof(struct ft232r_device_info *) * (n + 1));

	for (i = 0; i < n; ++i) {
		err = libusb_get_device_descriptor(list[i], &desc);
		if (unlikely(err)) {
			applog(LOG_ERR, "ft232r_scan: Error getting device descriptor: %s", libusb_error_name(err));
			continue;
		}
		if (!(desc.idVendor == FT232R_IDVENDOR && desc.idProduct == FT232R_IDPRODUCT))
			continue;

		err = libusb_open(list[i], &handle);
		if (unlikely(err)) {
			applog(LOG_ERR, "ft232r_scan: Error opening device: %s", libusb_error_name(err));
			continue;
		}

		n = libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf)-1);
		if (unlikely(n < 0)) {
			libusb_close(handle);
			applog(LOG_ERR, "ft232r_scan: Error getting iProduct string: %s", libusb_error_name(n));
			continue;
		}
		buf[n] = '\0';
		info = malloc(sizeof(struct ft232r_device_info));
		info->product = strdup((char*)buf);

		n = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buf, sizeof(buf)-1);
		libusb_close(handle);
		if (unlikely(n < 0)) {
			applog(LOG_ERR, "ft232r_scan: Error getting iSerialNumber string: %s", libusb_error_name(n));
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
}

int ft232r_detect(const char *product_needle, const char *serial, foundusb_func_t cb)
{
	struct ft232r_device_info *info;
	int found = 0;

	for (struct ft232r_device_info **infop = ft232r_devinfo_list; (info = *infop); ++infop) {
		if (!strstr(info->product, product_needle))
			continue;
		if (serial && strcmp(serial, info->serial))
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

#define FTDI_BAUDRATE_3M  0,0

#define FTDI_INDEX       1
#define FTDI_TIMEOUT  1000

struct ft232r_device_handle {
	libusb_device_handle *h;
	uint8_t i;
	uint8_t o;
	unsigned char ibuf[256];
	uint8_t ibufLen;
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
	libusb_detach_kernel_driver(devh, 0);
	if (libusb_set_configuration(devh, 1)) {
		applog(LOG_ERR, "ft232r_open: Error setting configuration");
		return NULL;
	}
	if (libusb_claim_interface(devh, 0)) {
		applog(LOG_ERR, "ft232r_open: Error claiming interface");
		return NULL;
	}
	if (libusb_set_interface_alt_setting(devh, 0, 0)) {
		applog(LOG_ERR, "ft232r_open: Error setting interface alt");
		return NULL;
	}
	if (libusb_control_transfer(devh, FTDI_REQTYPE_OUT, FTDI_REQUEST_SET_BAUDRATE, FTDI_BAUDRATE_3M, NULL, 0, FTDI_TIMEOUT) < 0) {
		applog(LOG_ERR, "ft232r_open: Error performing control transfer");
		return NULL;
	}

	ftdi = calloc(1, sizeof(*ftdi));
	ftdi->h = devh;

	struct libusb_config_descriptor *cfg;
	if (libusb_get_config_descriptor(dev, 1, &cfg)) {
		applog(LOG_ERR, "ft232r_open: Error getting config descriptor");
		return NULL;
	}
	const struct libusb_interface_descriptor *altcfg = &cfg->interface[0].altsetting[0];
	if (altcfg->bNumEndpoints < 2) {
		applog(LOG_ERR, "ft232r_open: Too few endpoints");
		return NULL;
	}
	ftdi->i = altcfg->endpoint[0].bEndpointAddress;
	ftdi->o = altcfg->endpoint[1].bEndpointAddress;
	libusb_free_config_descriptor(cfg);

	return ftdi;
}

bool ft232r_purge_buffers(struct ft232r_device_handle *dev, enum ft232r_reset_purge purge)
{
	if (purge & FTDI_PURGE_RX)
		if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_PURGE_RX, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
			return false;
	if (purge & FTDI_PURGE_TX)
		if (libusb_control_transfer(dev->h, FTDI_REQTYPE_OUT, FTDI_REQUEST_RESET, FTDI_PURGE_TX, FTDI_INDEX, NULL, 0, FTDI_TIMEOUT))
			return false;
	return true;
}

bool ft232r_set_bitmode(struct ft232r_device_handle *dev, uint8_t mask, uint8_t mode)
{
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

ssize_t ft232r_write(struct ft232r_device_handle *dev, void *data, size_t count)
{
	return ft232r_readwrite(dev, dev->o, data, count);
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
	if (!dev->ibufLen) {
		int transferred = ft232r_readwrite(dev, dev->i, dev->ibuf, sizeof(dev->ibuf));
		if (transferred <= 0)
			return transferred;
		dev->ibufLen = transferred;
	}
	
	if (count > dev->ibufLen)
		count = dev->ibufLen;
	memcpy(data, dev->ibuf, count);
	if (count < dev->ibufLen) {
		dev->ibufLen -= count;
		memmove(dev->ibuf, &dev->ibuf[count], dev->ibufLen);
	}
	return count;
}

ssize_t ft232r_read_all(struct ft232r_device_handle *dev, void *data, size_t count)
{
	return ft232r_rw_all(ft232r_read, dev, data, count);
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
