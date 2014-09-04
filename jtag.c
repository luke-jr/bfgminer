/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

// NOTE: This code is based on code Luke-Jr wrote originally for LPC1343CodeBase

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lowl-ftdi.h"
#include "jtag.h"
#include "logging.h"
#include "miner.h"

//#define DEBUG_JTAG_CLOCK

#define FTDI_READ_BUFFER_SIZE 100

static
unsigned char jtag_clock_byte(struct jtag_port *jp, bool tms, bool tdi)
{
	return (jp->a->state & jp->ignored)
	          | (tms ? jp->tms : 0)
	          | (tdi ? jp->tdi : 0);
}

// NOTE: The order of tms and tdi here are inverted from LPC1343CodeBase
bool jtag_clock(struct jtag_port *jp, bool tms, bool tdi, bool *tdo)
{
	unsigned char bufsz = tdo ? 3 : 2;
	unsigned char buf[3];
	memset(buf, jtag_clock_byte(jp, tms, tdi), sizeof(buf));
	buf[2] =
	buf[1] |= jp->tck;
	if (ft232r_write_all(jp->a->ftdi, buf, bufsz) != bufsz)
		return false;
	jp->a->state = buf[2];
	if (jp->a->async) {
		if (unlikely(tdo))
			applog(LOG_WARNING, "jtag_clock: request for tdo in async mode not possible");
#ifdef DEBUG_JTAG_CLOCK
		applog(LOG_DEBUG, "%p %02x tms=%d tdi=%d tdo=?async", jp, (unsigned)buf[2], (int)tms, (int)tdi);
#endif
		return true;
	}
	jp->a->bufread += bufsz;
	if (jp->a->bufread < FTDI_READ_BUFFER_SIZE - sizeof(buf) && !tdo) {
		// By deferring unnecessary reads, we can avoid some USB latency
#ifdef DEBUG_JTAG_CLOCK
		applog(LOG_DEBUG, "%p %02x tms=%d tdi=%d tdo=?defer", jp, (unsigned)buf[2], (int)tms, (int)tdi);
#endif
		return true;
	}
#if 0 /* untested */
	else if (!tdo) {
		if (ft232r_purge_buffers(jp->a->ftdi, FTDI_PURGE_BOTH)) {
			jp->bufread = 0;
#ifdef DEBUG_JTAG_CLOCK
		applog(LOG_DEBUG, "%p %02x tms=%d tdi=%d tdo=?purge", jp, (unsigned)buf[2], (int)tms, (int)tdi);
#endif
			return true;
		}
	}
#endif
	uint8_t rbufsz = jp->a->bufread;
	jp->a->bufread = 0;
	unsigned char rbuf[rbufsz];
	if (ft232r_read_all(jp->a->ftdi, rbuf, rbufsz) != rbufsz)
		return false;
	if (tdo) {
		*tdo = (rbuf[rbufsz-1] & jp->tdo);
#ifdef DEBUG_JTAG_CLOCK
		char x[(rbufsz * 2) + 1];
		bin2hex(x, rbuf, rbufsz);
	applog(LOG_DEBUG, "%p %02x tms=%d tdi=%d tdo=%d (%u:%s)", jp, (unsigned)rbuf[rbufsz-1], (int)tms, (int)tdi, (int)(bool)(rbuf[rbufsz-1] & jp->tdo), (unsigned)rbufsz, x);
	} else {
		applog(LOG_DEBUG, "%p %02x tms=%d tdi=%d tdo=?ignore", jp, (unsigned)buf[2], (int)tms, (int)tdi);
#endif
	}
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

static inline
bool getbit(void *data, uint32_t bitnum)
{
	unsigned char *cdata = data;
	div_t d = div(bitnum, 8);
	unsigned char b = cdata[d.quot];
	return b & (1<<(7 - d.rem));
}

static inline
void setbit(void *data, uint32_t bitnum, bool nv)
{
	unsigned char *cdata = data;
	div_t d = div(bitnum, 8);
	unsigned char *p = &cdata[d.quot];
	unsigned char o = (1<<(7 - d.rem));
	if (nv)
		*p |= o;
	else
		*p &= ~o;
}

// Expects to start at the Capture step, to handle 0-length gracefully
bool _jtag_llrw(struct jtag_port *jp, void *buf, size_t bitlength, bool do_read, int stage)
{
	uint8_t *data = buf;
	
	if (!bitlength)
		return jtag_clock(jp, true, false, NULL);

	if (stage & 1)
		if (!jtag_clock(jp, false, false, NULL))
			return false;

#ifndef DEBUG_JTAG_CLOCK
	// This alternate implementation is designed to minimize ft232r reads (which are slow)
	if (do_read) {
		unsigned char rbuf[FTDI_READ_BUFFER_SIZE];
		unsigned char wbuf[3];
		ssize_t rbufsz, bitspending = 0;
		size_t databitoff = 0, i;

		--bitlength;
		for (i = 0; i < bitlength; ++i) {
			wbuf[0] = jtag_clock_byte(jp, false, getbit(data, i));
			wbuf[1] = wbuf[0] | jp->tck;
			if (ft232r_write_all(jp->a->ftdi, wbuf, 2) != 2)
				return false;
			jp->a->bufread += 2;
			++bitspending;
			if (jp->a->bufread > FTDI_READ_BUFFER_SIZE - 2) {
				// The next bit would overflow, so read now
				rbufsz = jp->a->bufread;
				if (ft232r_read_all(jp->a->ftdi, rbuf, rbufsz) != rbufsz)
					return false;
				for (ssize_t j = rbufsz - ((bitspending - 1) * 2); j < rbufsz; j += 2)
					setbit(data, databitoff++, (rbuf[j] & jp->tdo));
				bitspending = 1;
				jp->a->bufread = 0;
			}
		}
		// Last bit needs special treatment
		wbuf[0] = jtag_clock_byte(jp, (stage & 2), getbit(data, i));
		wbuf[2] = wbuf[1] = wbuf[0] | jp->tck;
		if (ft232r_write_all(jp->a->ftdi, wbuf, sizeof(wbuf)) != sizeof(wbuf))
			return false;
		rbufsz = jp->a->bufread + 3;
		if (ft232r_read_all(jp->a->ftdi, rbuf, rbufsz) != rbufsz)
			return false;
		--rbufsz;
		for (ssize_t j = rbufsz - (bitspending * 2); j < rbufsz; j += 2)
			setbit(data, databitoff++, (rbuf[j] & jp->tdo));
		setbit(data, databitoff++, (rbuf[rbufsz] & jp->tdo));
		jp->a->bufread = 0;
		
		if (stage & 2) {
			if (!jtag_clock(jp, true, false, NULL))  // Update
				return false;
		}
		
		return true;
	}
#endif

	int i, j;
	div_t d;

	d = div(bitlength - 1, 8);

	for (i = 0; i < d.quot; ++i) {
		for (j = 0x80; j; j /= 2) {
			if (!jtag_rw_bit(jp, &data[i], j, false, do_read))
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
		return -1;
	for (i = 0; i < 4; ++i)
		if (!jtag_clock(jp, false, false, NULL))
			return -1;
	if (!jtag_clock(jp, false, false, &tdo))
		return -1;
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
