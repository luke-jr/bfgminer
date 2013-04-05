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
#include <stdio.h>

typedef bool(*detectone_func_t)(const char*);
typedef int(*autoscan_func_t)();

extern int _serial_detect(struct device_drv *drv, detectone_func_t, autoscan_func_t, bool force_autoscan);
#define serial_detect_fauto(drv, detectone, autoscan)  \
	_serial_detect(drv, detectone, autoscan, true)
#define serial_detect_auto(drv, detectone, autoscan)  \
	_serial_detect(drv, detectone, autoscan, false)
#define serial_detect(drv, detectone)  \
	_serial_detect(drv, detectone, NULL, false)
extern int serial_autodetect_devserial(detectone_func_t, const char *prodname);
extern int serial_autodetect_udev(detectone_func_t, const char *prodname);

extern int serial_open(const char *devpath, unsigned long baud, signed short timeout, bool purge);
extern ssize_t _serial_read(int fd, char *buf, size_t buflen, char *eol);
#define serial_read(fd, buf, count)  \
	_serial_read(fd, (char*)(buf), count, NULL)
#define serial_read_line(fd, buf, bufsiz, eol)  \
	_serial_read(fd, buf, bufsiz, &eol)
#define serial_close(fd)  close(fd)

extern FILE *open_bitstream(const char *dname, const char *filename);

extern int get_serial_cts(int fd);

#ifndef WIN32
extern const struct timeval tv_timeout_default;
extern const struct timeval tv_inter_char_default;

extern size_t _select_read(int fd, char *buf, size_t bufsiz, struct timeval *timeout, struct timeval *char_timeout, int finished);
extern size_t _select_write(int fd, char *buf, size_t siz, struct timeval *timeout);

#define select_open(devpath) \
	serial_open(devpath, 0, 0, false)

#define select_open_purge(devpath, purge)\
	serial_open(devpath, 0, 0, purge)

#define select_write(fd, buf, siz) \
	_select_write(fd, buf, siz, (struct timeval *)(&tv_timeout_default))

#define select_write_full _select_write

#define select_read(fd, buf, bufsiz) \
	_select_read(fd, buf, bufsiz, (struct timeval *)(&tv_timeout_default), \
			(struct timeval *)(&tv_inter_char_default), -1)

#define select_read_til(fd, buf, bufsiz, eol) \
	_select_read(fd, buf, bufsiz, (struct timeval *)(&tv_timeout_default), \
			(struct timeval *)(&tv_inter_char_default), eol)

#define select_read_wait(fd, buf, bufsiz, timeout) \
	_select_read(fd, buf, bufsiz, timeout, \
			(struct timeval *)(&tv_inter_char_default), -1)

#define select_read_wait_til(fd, buf, bufsiz, timeout, eol) \
	_select_read(fd, buf, bufsiz, timeout, \
			(struct timeval *)(&tv_inter_char_default), eol)

#define select_read_wait_both(fd, buf, bufsiz, timeout, char_timeout) \
	_select_read(fd, buf, bufsiz, timeout, char_timeout, -1)

#define select_read_full _select_read

#define select_close(fd)  close(fd)

#endif // ! WIN32

#endif
