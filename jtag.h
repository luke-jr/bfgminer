/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFGMINER_JTAG_H
#define BFGMINER_JTAG_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

struct jtag_port_a {
	struct ft232r_device_handle *ftdi;
	uint8_t state;
	bool async;
	uint8_t bufread;
};

struct jtag_port {
	struct jtag_port_a *a;
	uint8_t tck;
	uint8_t tms;
	uint8_t tdi;
	uint8_t tdo;
	uint8_t ignored;
};

enum jtagreg {
	JTAG_REG_DR,
	JTAG_REG_IR,
};

extern bool jtag_clock(struct jtag_port *, bool tms, bool tdi, bool *tdo);
extern bool _jtag_llrw(struct jtag_port *, void *buf, size_t bitlength, bool do_read, int stage);
extern bool jtag_reset(struct jtag_port *);
extern ssize_t jtag_detect(struct jtag_port *);
extern bool _jtag_rw(struct jtag_port *, enum jtagreg r, void *buf, size_t bitlength, bool do_read, int stage);
#define jtag_read(jp, r, data, bitlen)  _jtag_rw(jp, r, data, bitlen, true, 0xff)
#define jtag_sread(jp, r, data, bitlen)  _jtag_rw(jp, r, data, bitlen, true, 1)
#define jtag_sread_more(jp, data, bitlen, finish)  _jtag_llrw(jp, data, bitlen, true, (finish) ? 2 : 0)
// Cast is used to accept const data - while it ignores the compiler attribute, it still won't modify the data
#define jtag_write(jp, r, data, bitlen)  _jtag_rw(jp, r, (void*)data, bitlen, false, 0xff)
#define jtag_swrite(jp, r, data, bitlen)  _jtag_rw(jp, r, (void*)data, bitlen, false, 1)
#define jtag_swrite_more(jp, data, bitlen, finish)  _jtag_llrw(jp, (void*)data, bitlen, false, (finish) ? 2 : 0)
extern bool jtag_run(struct jtag_port *);

#endif
