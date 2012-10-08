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

#include <libusb-1.0/libusb.h>

typedef bool(*foundusb_func_t)(libusb_device *, const char *product, const char *serial);

extern void ft232r_scan();
extern void ft232r_scan_free();
extern int ft232r_detect(const char *product_needle, const char *serial, foundusb_func_t);

#endif
