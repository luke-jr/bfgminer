/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

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
		applog(LOG_ERR, "ft232r scan: Error getting USB device list: %s", libusb_error_name(n));
		ft232r_devinfo_list = calloc(1, sizeof(struct ft232r_device_info *));
		return;
	}

	ft232r_devinfo_list = malloc(sizeof(struct ft232r_device_info *) * (n + 1));

	for (i = 0; i < n; ++i) {
		err = libusb_get_device_descriptor(list[i], &desc);
		if (unlikely(err)) {
			applog(LOG_ERR, "ft232r scan: Error getting device descriptor: %s", libusb_error_name(err));
			continue;
		}
		if (!(desc.idVendor == FT232R_IDVENDOR && desc.idProduct == FT232R_IDPRODUCT))
			continue;

		err = libusb_open(list[i], &handle);
		if (unlikely(err)) {
			applog(LOG_ERR, "ft232r scan: Error opening device: %s", libusb_error_name(err));
			continue;
		}

		n = libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf)-1);
		if (unlikely(n < 0)) {
			libusb_close(handle);
			applog(LOG_ERR, "ft232r scan: Error getting iProduct string: %s", libusb_error_name(n));
			continue;
		}
		buf[n] = '\0';
		info = malloc(sizeof(struct ft232r_device_info));
		info->product = strdup((char*)buf);

		n = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buf, sizeof(buf)-1);
		libusb_close(handle);
		if (unlikely(n < 0)) {
			applog(LOG_ERR, "ft232r scan: Error getting iSerialNumber string: %s", libusb_error_name(n));
			n = 0;
		}
		buf[n] = '\0';
		info->serial = strdup((char*)buf);
		info->libusb_dev = libusb_ref_device(list[i]);
		ft232r_devinfo_list[found++] = info;

		applog(LOG_DEBUG, "ft232r scan: Found \"%s\" serial \"%s\"", info->product, info->serial);
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
