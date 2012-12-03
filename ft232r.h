/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFGMINER_FT232R_H
#define BFGMINER_FT232R_H

#include <stdbool.h>
#include <stdint.h>

#include <libusb.h>

enum ft232r_reset_purge {
	FTDI_PURGE_RX   = 1,
	FTDI_PURGE_TX   = 2,
	FTDI_PURGE_BOTH = 3,
};

struct ft232r_device_handle;

typedef bool(*foundusb_func_t)(libusb_device *, const char *product, const char *serial);

extern void ft232r_scan();
extern void ft232r_scan_free();
extern int ft232r_detect(const char *product_needle, const char *serial, foundusb_func_t);
extern struct ft232r_device_handle *ft232r_open(libusb_device *);
extern void ft232r_close(struct ft232r_device_handle *);
extern bool ft232r_purge_buffers(struct ft232r_device_handle *, enum ft232r_reset_purge);
extern bool ft232r_set_bitmode(struct ft232r_device_handle *, uint8_t mask, uint8_t mode);
extern ssize_t ft232r_flush(struct ft232r_device_handle *);
extern ssize_t ft232r_write(struct ft232r_device_handle *, void *data, size_t count);
extern ssize_t ft232r_write_all(struct ft232r_device_handle *, void *data, size_t count);
extern ssize_t ft232r_read(struct ft232r_device_handle *, void *buf, size_t count);
extern ssize_t ft232r_read_all(struct ft232r_device_handle *, void *data, size_t count);
extern bool ft232r_get_pins(struct ft232r_device_handle *, uint8_t *pins);
extern bool ft232r_set_cbus_bits(struct ft232r_device_handle *dev, bool sc, bool cs);
extern bool ft232r_get_cbus_bits(struct ft232r_device_handle *dev, bool *out_sio0, bool *out_sio1);

#endif
