/*
 * Copyright 2014 Nate Woolls
 * Copyright 2014 GridSeed Team
 * Copyright 2014 Dualminer Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef bfgminer_gc3355_h
#define bfgminer_gc3355_h

#include <stdbool.h>
#include <stdint.h>

#include "miner.h"

// options configurable by the end-user

extern
int opt_sha2_units;

extern
int opt_pll_freq;

// GridSeed common code begins here

#define GC3355_COMMAND_DELAY				20000
#define GC3355_ORB_DEFAULT_CHIPS			5
#define GC3355_READ_SIZE					12

struct gc3355_orb_info
{
	uint16_t freq;
	int needwork;
};

#define gc3355_open(path)  serial_open(path, 115200, 1, true)
#define gc3355_close(fd)  serial_close(fd)

extern
int gc3355_read(int fd, char *buf, size_t size);

extern
ssize_t gc3355_write(int fd, const void * const buf, const size_t size);

extern
void gc3355_init_usborb(int fd, int pll_freq, bool scrypt_only, bool detect_only);

extern
void gc3355_init_usbstick(int fd, int pll_freq, bool scrypt_only, bool detect_only);

extern
void gc3355_scrypt_init(int fd);

extern
void gc3355_scrypt_reset(int fd);

extern
void gc3355_scrypt_only_reset(int fd);

extern
void gc3355_scrypt_prepare_work(unsigned char cmd[156], struct work *work);

extern
void gc3355_sha2_prepare_work(unsigned char cmd[52], struct work *work, bool simple);

extern
uint32_t gc3355_get_firmware_version(int fd);

extern
void gc3355_set_pll_freq(int fd, int pll_freq);

#define gc3355_get_cts_status(fd)  (get_serial_cts(fd) ? 0 : 1)
#define gc3355_set_rts_status(fd, val)  set_serial_rts(fd, val)
#define gc3355_set_dtr_status(fd, val)  set_serial_dtr(fd, val)

#endif
