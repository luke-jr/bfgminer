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

#include "miner.h"

// options configurable by the end-user

extern
int opt_sha2_units;

// GridSeed common code begins here

#define GC3355_COMMAND_DELAY				20000

#define SCRYPT_UNIT_OPEN  0
#define SCRYPT_UNIT_CLOSE 1

extern
char *opt_dualminer_sha2_gating;

extern
int opt_pll_freq;

extern
bool opt_hubfans;

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
void gc3355_set_pll_freq(int fd, int pll_freq);

#define gc3355_get_cts_status(fd)  (get_serial_cts(fd) ? 0 : 1)
#define gc3355_set_rts_status(fd, val)  set_serial_rts(fd, val)
#define gc3355_set_dtr_status(fd, val)  set_serial_dtr(fd, val)

#endif
