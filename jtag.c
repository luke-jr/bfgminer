/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

// NOTE: This code is based on code Luke-Jr wrote originally for LPC1343CodeBase

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ft232r.h"
#include "jtag.h"

// NOTE: The order of tms and tdi here are inverted from LPC1343CodeBase
bool jtag_clock(struct jtag_port *jp, bool tms, bool tdi, bool *tdo)
{
	unsigned char buf[3];
	memset(buf, (*jp->state & jp->ignored)
	          | (tms ? jp->tms : 0)
	          | (tdi ? jp->tdi : 0), sizeof(buf));
	buf[0] |= jp->tck;
	if (ft232r_write_all(jp->ftdi, buf, sizeof(buf)) != sizeof(buf))
		return false;
	if (ft232r_read_all(jp->ftdi, buf, sizeof(buf)) != sizeof(buf))
		return false;
	if (tdo)
		*tdo = (buf[2] & jp->tdo);
	*jp->state = buf[2];
	return true;
}

static bool jtag_rw_bit(struct jtag_port *jp, void *buf, uint8_t mask, bool tms, bool do_read)
{
	uint8_t *byte = buf;
	bool tdo;
	if (!jtag_clock(jp, tms, byte[0] & mask, do_read ? &tdo : NULL))
		return false;
	if (do_read) {
		if (tdo)
			byte[0] |= mask;
		else
			byte[0] &= ~mask;
	}
	return true;
}

// Expects to start at the Capture step, to handle 0-length gracefully
bool _jtag_llrw(struct jtag_port *jp, void *buf, size_t bitlength, bool do_read, int stage)
{
	uint8_t *data = buf;
	int i, j;
	div_t d;
	
	if (!bitlength)
		return jtag_clock(jp, true, false, NULL);

	if (stage & 1)
		if (!jtag_clock(jp, false, false, NULL))
			return false;

	d = div(bitlength - 1, 8);

	for (i = 0; i < d.quot; ++i) {
		for (j = 0x80; j; j /= 2) {
			if (!jtag_rw_bit(jp, &data[i], 0x80, false, do_read))
				return false;
		}
	}
	for (j = 0; j < d.rem; ++j)
		if (!jtag_rw_bit(jp, &data[i], 0x80 >> j, false, do_read))
			return false;
	if (stage & 2) {
		if (!jtag_rw_bit(jp, &data[i], 0x80 >> j, true, do_read))
			return false;
		if (!jtag_clock(jp, true, false, NULL))  // Update
			return false;
	}
	else
		if (!jtag_rw_bit(jp, &data[i], 0x80 >> j, false, do_read))
			return false;
	return true;
}

bool jtag_reset(struct jtag_port *jp)
{
	for (int i = 0; i < 5; ++i)
		if (!jtag_clock(jp, true, false, NULL))
			return false;
	return jtag_clock(jp, false, false, NULL);
}

// Returns -1 for failure, -2 for unknown, or zero and higher for number of devices
ssize_t jtag_detect(struct jtag_port *jp)
{
	// TODO: detect more than 1 device
	int i;
	bool tdo;
	
	if (!(1
	 && jtag_write(jp, JTAG_REG_IR, "\xff", 8)
	 && jtag_clock(jp, true , false, NULL)  // Select DR
	 && jtag_clock(jp, false, false, NULL)  // Capture DR
	 && jtag_clock(jp, false, false, NULL)  // Shift DR
	))
		return false;
	for (i = 0; i < 4; ++i)
		if (!jtag_clock(jp, false, false, NULL))
			return false;
	if (!jtag_clock(jp, false, false, &tdo))
		return false;
	if (tdo)
		return -1;
	for (i = 0; i < 4; ++i)
	{
		if (!jtag_clock(jp, false, true, &tdo))
			return -1;
		if (tdo)
			break;
	}
	if (!jtag_reset(jp))
		return -1;
	return i < 2 ? i : -2;
}

bool _jtag_rw(struct jtag_port *jp, enum jtagreg r, void *buf, size_t bitlength, bool do_read, int stage)
{
	if (!jtag_clock(jp, true, false, NULL))  // Select DR
		return false;
	if (r == JTAG_REG_IR)
		if (!jtag_clock(jp, true, false, NULL))  // Select IR
			return false;
	if (!jtag_clock(jp, false, false, NULL))  // Capture
		return false;
	return _jtag_llrw(jp, buf, bitlength, do_read, stage);  // Exit1
}

bool jtag_run(struct jtag_port *jp)
{
	return jtag_clock(jp, false, false, NULL);
}
