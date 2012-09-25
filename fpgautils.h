/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef FPGAUTILS_H
#define FPGAUTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

struct device_api;

typedef bool(*detectone_func_t)(const char*);
typedef int(*autoscan_func_t)();

extern int _serial_detect(struct device_api *api, detectone_func_t, autoscan_func_t, int flags);
#define serial_detect_fauto(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 1)
#define serial_detect_auto(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 0)
#define serial_detect_auto_byname(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 2)
#define serial_detect(api, detectone)  \
	_serial_detect(api, detectone,     NULL, 0)
#define noserial_detect(api, autoscan)  \
	_serial_detect(api, NULL     , autoscan, 0)
extern int serial_autodetect_devserial(detectone_func_t, const char*prodname);
extern int serial_autodetect_udev     (detectone_func_t, const char*prodname);
extern int serial_autodetect_ftdi     (detectone_func_t, const char*needle, const char *needle2);

extern struct device_api *serial_claim(const char *devpath, struct device_api *);

extern int serial_open(const char *devpath, unsigned long baud, uint8_t timeout, bool purge);
extern ssize_t _serial_read(int fd, char *buf, size_t buflen, char *eol);
#define serial_read(fd, buf, count)  \
	_serial_read(fd, (char*)(buf), count, NULL)
#define serial_read_line(fd, buf, bufsiz, eol)  \
	_serial_read(fd, buf, bufsiz, &eol)
#define serial_close(fd)  close(fd)

extern FILE *open_bitstream(const char *dname, const char *filename);

#endif
