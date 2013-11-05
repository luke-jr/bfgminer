/*
 * Copyright 2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include "deviceapi.h"
#include "fpgautils.h"
#include "miner.h"
#include "util.h"

BFG_REGISTER_DRIVER(technobit_drv)

#define TECHNOBIT_PKT_HEADER_SIZE (1 + 1 + 1 + 2)
#define TECHNOBIT_LOWEST_ADDRESS  (0x3000)
#define TECHNOBIT_HIGHEST_ADDRESS (0x7000)  /* ??? */
#define TECHNOBIT_MAX_DATA_SIZE 510

struct technobit_state {
	uint8_t datain[TECHNOBIT_PKT_HEADER_SIZE + (255 * 2) + 1];
	size_t datainlen;
};

static
uint8_t technobit_chksum(const void * const data, const size_t datasz)
{
	const uint8_t *bytes = data;
	uint8_t r = 0;
	size_t i;
	
	for (i = 0; i < datasz; ++i, ++bytes)
		r += *bytes;
	
	return r;
}

static
bool technobit_send(const int fd, const uint8_t cmd, const uint16_t addr, const void * const data, const size_t datasz)
{
	const size_t sz2 = TECHNOBIT_PKT_HEADER_SIZE + datasz;
	const size_t pktsz = sz2 + 1;
	uint8_t pkt[pktsz];
	pkt[0] = '\x53';
	pkt[1] = datasz / 2;
	pkt[2] = cmd;
	pkt[3] = addr & 0xff;
	pkt[4] = addr >> 8;
	memcpy(&pkt[5], data, datasz);
	pkt[sz2] = technobit_chksum(pkt, sz2);
	
	if (opt_dev_protocol)
	{
		char hex[(pktsz * 2) + 1];
		bin2hex(hex, pkt, pktsz);
		applog(LOG_DEBUG, "%s(%d,'%c',0x%04x,...,%u): %s",
		       __func__, fd, (int)cmd, (unsigned)addr, (unsigned)datasz, hex);
	}
	
	return (write(fd, pkt, pktsz) == pktsz);
}

static
bool technobit_recv(struct technobit_state * const state, const int fd, uint8_t * const out_cmd, uint16_t * const out_addr, void * const out_datap, size_t * const out_datasz)
{
	void ** const out_datap2 = out_datap;
	int skip = 0;
	const ssize_t r = read(fd, &state->datain[state->datainlen], sizeof(state->datain) - state->datainlen);
	if (r <= 0)
		return false;
	if (opt_dev_protocol)
	{
		char hex[(r * 2) + 1];
		bin2hex(hex, &state->datain[state->datainlen], r);
		applog(LOG_DEBUG, "%s: %s", __func__, hex);
	}
	state->datainlen += r;
	
reskip2:
	while (state->datain[skip] != '\x53')
		++skip;
	state->datainlen -= skip;
	memmove(state->datain, &state->datain[skip], state->datainlen);
	
	if (state->datainlen <= TECHNOBIT_PKT_HEADER_SIZE)
		return false;
	
	const uint16_t addr = (state->datain[4] << 8) | state->datain[3];
	if (addr < TECHNOBIT_LOWEST_ADDRESS || addr > TECHNOBIT_HIGHEST_ADDRESS)
	{
reskip:
		skip = 1;
		goto reskip2;
	}
	
	const size_t datasz = state->datain[1] * 2;
	const size_t pktsz = TECHNOBIT_PKT_HEADER_SIZE + datasz;
	if (state->datainlen <= pktsz)
		return false;
	
	const uint8_t chksum = technobit_chksum(state->datain, pktsz);
	if (state->datain[pktsz] != chksum)
		goto reskip;
	
	*out_cmd = state->datain[2];
	*out_addr = addr;
	*out_datap2 = &state->datain[TECHNOBIT_PKT_HEADER_SIZE];
	*out_datasz = datasz;
	return true;
}

static
bool technobit_request_read(const int fd, const uint16_t addr, uint16_t sz)
{
	const uint8_t data[2] = { sz & 0xff, sz >> 8 };
	return technobit_send(fd, 'R', addr, data, sizeof(data));
}

static
bool technobit_detect_one(const char * const devpath)
{
	struct cgpu_info *cgpu;
	
	// TODO
	
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &technobit_drv,
		.device_path = strdup(devpath),
		.device_fd = -1,
		.deven = DEV_ENABLED,
		.threads = 1,
	};
	return add_cgpu(cgpu);
}

static void technobit_detect(void)
{
	generic_detect(&technobit_drv, technobit_detect_one, NULL, 0);
}


static
bool technobit_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct technobit_state *state;
	
	state = cgpu->device_data = malloc(sizeof(*state));
	*state = (struct technobit_state){
		.datainlen = 0,
	};
	
	// TODO
	// REQUEST  16 bits read from 3004
	// receive  16 bits read from 6800
	// receive 192 bits read from 3000
	// receive  32 bits read from 6494
	// receive  64 bits read from 649c
	// REQUEST 736 bits write to  4000
	// REQUEST  16 bits read from 3004
	// receive  16 bits read from 6800
	// receive confirmation of 736 bits write to 4000
	// recieve  16 bits read from 3004
	// recieve  16 bits read from 6800
	// REQUEST 736 bits write to  4000
	// REQUEST  16 bits read from 3004
	// recieve  16 bits read from 6800
	// recieve  
	
	timer_set_now(&thr->tv_poll);
	return true;
}

static
bool technobit_queue_append(struct thr_info * const thr, struct work * const work)
{
	//struct cgpu_info * const cgpu = thr->cgpu;
	// TODO
	thr->queue_full = true;
	return false;
}

static
void technobit_queue_flush(struct thr_info * const thr)
{
	// TODO
}

static
void technobit_poll(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct technobit_state * const state = cgpu->device_data;
	int fd = cgpu->device_fd;
	
	if (unlikely(fd == -1))
	{
		fd = cgpu->device_fd = serial_open(cgpu->device_path, 38400, 0, true);
		if (fd == -1)
			applogr(, LOG_ERR, "%"PRIpreprv": Failed to open %s",
			        cgpu->proc_repr, cgpu->device_path);
		technobit_request_read(fd, 0x3004, 1);
	}
	
	uint8_t cmd, *datap;
	uint16_t addr;
	size_t datasz;
	
	if (technobit_recv(state, fd, &cmd, &addr, &datap, &datasz))
	{
		if (opt_debug)
		{
			char hexdata[(datasz * 2) + 1];
			bin2hex(hexdata, datap, datasz);
			applog(LOG_DEBUG, "%"PRIpreprv": Received cmd=%02x addr=%04x data=%s",
			       cgpu->proc_repr, cmd, addr, hexdata);
		}
	}
	
	timer_set_delay_from_now(&thr->tv_poll, 10000);
}


struct device_drv technobit_drv = {
	.dname = "technobit",
	.name = "HEX",
	.drv_detect = technobit_detect,
	
	.thread_init = technobit_init,
	
	.minerloop = minerloop_queue,
	.queue_append = technobit_queue_append,
	.queue_flush = technobit_queue_flush,
	.poll = technobit_poll,
};
