/*
 * Copyright 2013 DI Andreas Auer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * Big Picture Mining USB miner with Bitfury ASIC
 */

#include "config.h"
#include "miner.h"
#include "fpgautils.h"
#include "logging.h"

#include "deviceapi.h"
#include "sha2.h"

#include "driver-bigpic.h"

#include <stdio.h>

struct device_drv bigpic_drv;

//------------------------------------------------------------------------------
uint32_t bigpic_decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24; in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2)&0x3FFFFF;

	/* Extraction */
	if (in & 1) out |= (1 << 23);
	if (in & 2) out |= (1 << 22);

	out -= 0x800004;
	return out;
}

//------------------------------------------------------------------------------
int bigpic_rehash(unsigned char *midstate, unsigned m7, unsigned ntime, unsigned nbits, unsigned nnonce)
{
	uint8_t   in[16];
	uint32_t *in32 = (uint32_t *)in;
	char      hex[65];
	uint32_t *mid32 = (uint32_t *)midstate;
	uint32_t  out32[8];
	uint8_t  *out = (uint8_t *) out32;
	sha256_ctx ctx;

	memset( &ctx, 0, sizeof(sha256_ctx));
	memcpy(ctx.h, mid32, 8*4);
	ctx.tot_len = 64;
	ctx.len = 0;

	nnonce = bswap_32(nnonce);
	in32[0] = bswap_32(m7);
	in32[1] = bswap_32(ntime);
	in32[2] = bswap_32(nbits);
	in32[3] = nnonce;

	sha256_update(&ctx, in, 16);
	sha256_final(&ctx, out);
	sha256(out, 32, out);

	if (out32[7] == 0)
	{
		bin2hex(hex, out, 32);
		applog(LOG_DEBUG, "! MS0: %08x, m7: %08x, ntime: %08x, nbits: %08x, nnonce: %08x", mid32[0], m7, ntime, nbits, nnonce);
		applog(LOG_DEBUG, "                         out: %s", hex);
		return 1;
	}
	return 0;
}

//------------------------------------------------------------------------------
static bool bigpic_detect_custom(const char *devpath, struct device_drv *api, struct bigpic_info *info)
{
	int fd = serial_open(devpath, info->baud, 1, true);

	if(fd < 0)
	{
		return false;
	}

	char buf[sizeof(struct bigpic_identity)+1];
	int len;

	write(fd, "I", 1);
	len = serial_read(fd, buf, sizeof(buf));
	if(len != 14)
	{
		serial_close(fd);
		return false;
	}

	info->id.version = buf[1];
	memcpy(info->id.product, buf+2, 8);
	memcpy(&info->id.serial, buf+10, 4);
	applog(LOG_DEBUG, "%s: %s: %d, %s %08x",
	       bigpic_drv.dname,
	       devpath,
	       info->id.version, info->id.product, info->id.serial);

	char buf_state[sizeof(struct bigpic_state)+1];
	len = 0;
	write(fd, "R", 1);

	while(len == 0)
	{
		len = serial_read(fd, buf, sizeof(buf_state));
		cgsleep_ms(100);
	}
	serial_close(fd);

	if(len != 7)
	{
		applog(LOG_ERR, "%s: %s not responding to reset: %d",
		       bigpic_drv.dname,
		       devpath, len);
		return false;
	}

	if (serial_claim_v(devpath, api))
		return false;

	struct cgpu_info *bigpic;
	bigpic = calloc(1, sizeof(struct cgpu_info));
	bigpic->drv = api;
	bigpic->device_path = strdup(devpath);
	bigpic->device_fd = -1;
	bigpic->threads = 1;
	add_cgpu(bigpic);

	applog(LOG_INFO, "Found %"PRIpreprv" at %s", bigpic->proc_repr, devpath);

	applog(LOG_DEBUG, "%"PRIpreprv": Init: baud=%d",
		bigpic->proc_repr, info->baud);

	bigpic->device_data = info;

	return true;
}

//------------------------------------------------------------------------------
static bool bigpic_detect_one(const char *devpath)
{
	struct bigpic_info *info = calloc(1, sizeof(struct bigpic_info));
	if (unlikely(!info))
		quit(1, "Failed to malloc bigpicInfo");

	info->baud = BPM_BAUD;

	if (!bigpic_detect_custom(devpath, &bigpic_drv, info))
	{
		free(info);
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
static int bigpic_detect_auto(void)
{
	return serial_autodetect(bigpic_detect_one, "Bitfury", "BF1");
}

//------------------------------------------------------------------------------
static void bigpic_detect()
{
	serial_detect_auto(&bigpic_drv, bigpic_detect_one, bigpic_detect_auto);
}

//------------------------------------------------------------------------------
static bool bigpic_init(struct thr_info *thr)
{
	struct cgpu_info *bigpic = thr->cgpu;
	struct bigpic_info *info = (struct bigpic_info *)bigpic->device_data;

	applog(LOG_DEBUG, "%"PRIpreprv": init", bigpic->proc_repr);

	int fd = serial_open(bigpic->device_path, info->baud, 1, true);
	if (unlikely(-1 == fd))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to open %s",
		       bigpic->proc_repr, bigpic->device_path);
		return false;
	}

	bigpic->device_fd = fd;

	applog(LOG_INFO, "%"PRIpreprv": Opened %s", bigpic->proc_repr, bigpic->device_path);

	struct timeval tv_now;
	gettimeofday(&tv_now, NULL);
	timer_set_delay(&thr->tv_poll, &tv_now, 1000000);

	info->work = 0;
	info->prev_work[0] = 0;
	info->prev_work[1] = 0;

	return true;
}

//------------------------------------------------------------------------------
static bool duplicate(uint32_t *results, uint32_t size, uint32_t test_nonce)
{
	for(uint32_t i=0; i<size; i++)
	{
		if(results[i] == test_nonce)
		{
			return true;
		}
	}

	return false;
}

//------------------------------------------------------------------------------
static void bigpic_process_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *board = thr->cgpu;
	struct bigpic_info *info = (struct bigpic_info *)board->device_data;

	uint32_t results[16*6];
	uint32_t num_results;

	uint32_t m7    = *((uint32_t *)&work->data[64]);
	uint32_t ntime = *((uint32_t *)&work->data[68]);
	uint32_t nbits = *((uint32_t *)&work->data[72]);

	int32_t nonces = (info->rx_len / 7) - 1;

	num_results = 0;
	for(int i=0; i<info->rx_len; i+=7)
	{
		struct bigpic_state state;
		state.state = info->rx_buffer[i + 1];
		state.switched = info->rx_buffer[i + 2];
		memcpy(&state.nonce, info->rx_buffer + i + 3, 4);

		if(duplicate(results, num_results, state.nonce))
		{
			continue;
		}

		uint32_t nonce = bigpic_decnonce(state.nonce);
		results[num_results++] = state.nonce;

		//applog(LOG_DEBUG, "%"PRIpreprv": Len: %d Cmd: %c State: %c Switched: %d Nonce: %08X", board->proc_repr, info->rx_len, info->rx_buffer[i], state->state, state->switched, nonce);
		if(bigpic_rehash(work->midstate, m7, ntime, nbits, nonce))
		{
			submit_nonce(thr, work, nonce);
			nonces--;
			continue;
		}
		if(bigpic_rehash(work->midstate, m7, ntime, nbits, nonce-0x400000))
		{
			submit_nonce(thr, work, nonce-0x400000);
			nonces--;
			continue;
		}
		if(bigpic_rehash(work->midstate, m7, ntime, nbits, nonce-0x800000))
		{
			submit_nonce(thr, work, nonce-0x800000);
			nonces--;
			continue;
		}
		if(bigpic_rehash(work->midstate, m7, ntime, nbits, nonce+0x2800000))
		{
			submit_nonce(thr, work, nonce+0x2800000);
			nonces--;
			continue;
		}
		if(bigpic_rehash(work->midstate, m7, ntime, nbits, nonce+0x2C00000))
		{
			submit_nonce(thr, work, nonce+0x2C00000);
			nonces--;
			continue;
		}
		if(bigpic_rehash(work->midstate, m7, ntime, nbits, nonce+0x400000))
		{
			submit_nonce(thr, work, nonce+0x400000);
			nonces--;
			continue;
		}
		inc_hw_errors(thr, work, nonce);
	}
}

//------------------------------------------------------------------------------
static int64_t bigpic_scanwork(struct thr_info *thr)
{
	struct cgpu_info *board = thr->cgpu;
	struct bigpic_info *info = (struct bigpic_info *)board->device_data;

	uint32_t hashes = 0;

	if(!info->work)
	{
		info->work = get_queued(board);
		if(info->work == NULL)
			return 0;
	}

	uint8_t sendbuf[45];
	sendbuf[0] = 'W';
	memcpy(sendbuf + 1, info->work->midstate, 32);
	memcpy(sendbuf + 33, info->work->data + 64, 12);
	write(board->device_fd, sendbuf, sizeof(sendbuf));

	applog(LOG_DEBUG, "%"PRIpreprv": Work Task sending: %d",
	       board->proc_repr, info->work->id);
	while(1)
	{
		uint8_t buffer[7];
		int len;
		len = serial_read(board->device_fd, buffer, 7);
		if(len > 0)
			break;
	}

	applog(LOG_DEBUG, "%"PRIpreprv": Work Task sent", board->proc_repr);
	while(1)
	{
		info->rx_len = serial_read(board->device_fd, info->rx_buffer, sizeof(info->rx_buffer));
		if(info->rx_len > 0)
			break;
	}
	applog(LOG_DEBUG, "%"PRIpreprv": Work Task accepted", board->proc_repr);

	applog(LOG_DEBUG, "%"PRIpreprv": Nonces sent back: %d",
	       board->proc_repr, info->rx_len / 7);
/*
	if(info->prev_work[1])
	{
		applog(LOG_DEBUG, "%"PRIpreprv": PREV[1]", board->proc_repr);
		bigpic_process_results(thr, info->prev_work[1]);
		work_completed(board, info->prev_work[1]);
		info->prev_work[1] = 0;
	}
*/
	if(info->prev_work[0])
	{
		applog(LOG_DEBUG, "%"PRIpreprv": PREV[0]", board->proc_repr);
		bigpic_process_results(thr, info->prev_work[0]);
	}
	info->prev_work[1] = info->prev_work[0];
	info->prev_work[0] = info->work;
	info->work = 0;

	//hashes = 0xffffffff;
	hashes = 0xBD000000;
	applog(LOG_DEBUG, "%"PRIpreprv": WORK completed", board->proc_repr);

	return hashes;
}

//------------------------------------------------------------------------------
static void bigpic_poll(struct thr_info *thr)
{
/*
	struct cgpu_info *board = thr->cgpu;
	uint8_t rx_buf[128];
	int len = 0;
	len = serial_read(board->device_fd, rx_buf, sizeof(rx_buf));

	applog(LOG_DEBUG, "%"PRIpreprv": POLL: serial read: %d", board->proc_repr, len);
*/
	struct timeval tv_now;
	gettimeofday(&tv_now, NULL);
	timer_set_delay(&thr->tv_poll, &tv_now, 1000000);
}

//------------------------------------------------------------------------------
static void bigpic_shutdown(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	serial_close(cgpu->device_fd);
}

//------------------------------------------------------------------------------
static bool bigpic_identify(struct cgpu_info *cgpu)
{
	char buf[] = "L";
	write(cgpu->device_fd, buf, sizeof(buf));
	
	return true;
}

//------------------------------------------------------------------------------
struct device_drv bigpic_drv = {
	.dname = "bigpic",
	.name = "BPM",
	.minerloop = hash_queued_work,

	.drv_detect = bigpic_detect,

	.identify_device = bigpic_identify,

	.thread_init = bigpic_init,
	.thread_shutdown = bigpic_shutdown,

	.poll = bigpic_poll,

	.scanwork = bigpic_scanwork,
};
