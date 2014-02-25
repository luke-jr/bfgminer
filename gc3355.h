/*
 * Copyright 2014 Nate Woolls
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

extern
char *opt_dualminer_pll;

extern
char *opt_dualminer_btc_gating;

extern
int opt_pll_freq;

extern
int opt_btc_number;

extern
void gc3355_dual_reset(int fd);

extern
void gc3355_opt_ltc_only_init(int fd);

extern
void gc3355_dualminer_init(int fd);

extern
void gc3355_opt_scrypt_init(int fd);

extern
void gc3355_init(int fd, char *pll_freq, char *btc_unit, bool is_ltc_only);

extern
void gc3355_open_btc_unit(int fd, char *opt_btc_gating);

extern
void gc3355_open_ltc_unit(int fd, int status);

extern
int gc3355_get_cts_status(int fd);

extern
void gc3355_set_rts_status(int fd, unsigned int value);

#endif
