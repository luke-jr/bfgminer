/*
 * Copyright 2013 Andreas Auer
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * MiniMiner One with Avalon ASIC
 */

#include "config.h"
#include "miner.h"
#include "fpgautils.h"
#include "logging.h"

#include "deviceapi.h"
#include "sha2.h"

#include "driver-bf1.h"

#include <stdio.h>

struct device_drv bf1_drv;

struct bitfury_payload
{
	unsigned char midstate[32];
	unsigned int junk[8];
	unsigned m7;
	unsigned ntime;
	unsigned nbits;
	unsigned nnonce;
};

static inline void flip80(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;
	int i;

	for (i = 0; i < 20; i++)
		dest[i] = swab32(src[i]);
}

void work_to_payload(struct bitfury_payload *p, struct work *w) {
	unsigned char flipped_data[80];

	memset(p, 0, sizeof(struct bitfury_payload));
	flip80(flipped_data, w->data);

	memcpy(p->midstate, w->midstate, 32);
	p->m7 = bswap_32(*(unsigned *)(flipped_data + 64));
	p->ntime = bswap_32(*(unsigned *)(flipped_data + 68));
	p->nbits = bswap_32(*(unsigned *)(flipped_data + 72));
	applog(LOG_INFO, "INFO nonc: %08x bitfury_scanHash MS0: %08x, ", p->nnonce, ((unsigned int *)p->midstate)[0]);
	applog(LOG_INFO, "INFO merkle[7]: %08x, ntime: %08x, nbits: %08x", p->m7, p->ntime, p->nbits);
}

//------------------------------------------------------------------------------
uint32_t bf1_decnonce(uint32_t in)
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
int bf1_rehash(unsigned char *midstate, unsigned m7, unsigned ntime, unsigned nbits, unsigned nnonce)
{
	uint8_t   in[16];
	uint8_t   hash1[32];
	uint32_t *in32 = (uint32_t *)in;
	uint8_t  *hex;
	uint32_t *mid32 = (uint32_t *)midstate;
	uint32_t  out32[8];
	uint8_t  *out = (uint8_t *) out32;
	uint32_t  i;
	sha2_context ctx;

	memset( &ctx, 0, sizeof(sha2_context));
	memcpy(ctx.state, mid32, 8*4);
	ctx.total[0] = 64;
	ctx.total[1] = 0;

	nnonce = bswap_32(nnonce);
	in32[0] = bswap_32(m7);
	in32[1] = bswap_32(ntime);
	in32[2] = bswap_32(nbits);
	in32[3] = nnonce;

	sha2_update(&ctx, in, 16);
	sha2_finish(&ctx, out);
	sha2(out, 32, out);

	if (out32[7] == 0)
	{
		hex = bin2hex(midstate, 32);
		hex = bin2hex(out, 32);
		applog(LOG_INFO, "! MS0: %08x, m7: %08x, ntime: %08x, nbits: %08x, nnonce: %08x\n\t\t\t out: %s\n", mid32[0], m7, ntime, nbits, nnonce, hex);
		return 1;
	}
#if 0
	else
	{
		applog(LOG_INFO, "DATA: %08X %08X %08X %08X", m7, ntime, nbits, nnonce);
	}
#endif
	return 0;
}

//------------------------------------------------------------------------------
static bool bf1_detect_custom(const char *devpath, struct device_drv *api, struct BF1Info *info)
{
	int fd = serial_open(devpath, info->baud, 1, true);

	if(fd < 0)
	{
		return false;
	}

	char buf[sizeof(struct BF1Identity)+1];
	int len;

	write(fd, "I", 1);
	len = serial_read(fd, buf, sizeof(buf));
	if(len != sizeof(buf))
	{
		serial_close(fd);
		return false;
	}

	struct BF1Identity *id = (struct BF1Identity *)&buf[1];
	info->id = *id;
	applog(LOG_DEBUG, "%d, %s %08x", info->id.version, info->id.product, info->id.serial);

	char buf_state[sizeof(struct BF1State)+1];
	len = 0;
	write(fd, "R", 1);

	while(len == 0)
	{
		len = serial_read(fd, buf, sizeof(buf_state));
		nmsleep(100);
	}
	serial_close(fd);

	if(len != sizeof(buf_state))
	{
		applog(LOG_ERR, "Not responding to reset: %d", len);
		return false;
	}

	if(serial_claim(devpath, api))
	{
		const char *claimedby = serial_claim(devpath, api)->dname;
		applog(LOG_DEBUG, "Bitfury BF1 device %s already claimed by other driver: %s", devpath, claimedby);
		return false;
	}

	/* We have a real MiniMiner ONE! */
	struct cgpu_info *bf1;
	bf1 = calloc(1, sizeof(struct cgpu_info));
	bf1->drv = api;
	bf1->device_path = strdup(devpath);
	bf1->device_fd = -1;
	bf1->threads = 1;
	add_cgpu(bf1);

	applog(LOG_INFO, "Found %"PRIpreprv" at %s", bf1->proc_repr, devpath);

	applog(LOG_DEBUG, "%"PRIpreprv": Init: baud=%d",
		bf1->proc_repr, info->baud);

	bf1->device_data = info;

	return true;
}

//------------------------------------------------------------------------------
static bool bf1_detect_one(const char *devpath)
{
	struct BF1Info *info = calloc(1, sizeof(struct BF1Info));
	if (unlikely(!info))
		quit(1, "Failed to malloc bf1Info");

	applog(LOG_INFO, "Detect Bitfury BF1 device: %s", devpath);
	info->baud = BF1_BAUD;

	if (!bf1_detect_custom(devpath, &bf1_drv, info))
	{
		free(info);
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
static int bf1_detect_auto(void)
{
	applog(LOG_INFO, "Autodetect Bitfury BF1 devices");
	return serial_autodetect(bf1_detect_one, "Bitfury_BF1");
}

//------------------------------------------------------------------------------
static void bf1_detect()
{
	applog(LOG_INFO, "Searching for Bitfury BF1 devices");
	//serial_detect(&bf1_drv, bf1_detect_one);
	serial_detect_auto(&bf1_drv, bf1_detect_one, bf1_detect_auto);
}

//------------------------------------------------------------------------------
static bool bf1_init(struct thr_info *thr)
{
	applog(LOG_INFO, "Bitfury BF1 init");

	struct cgpu_info *bf1 = thr->cgpu;
	struct BF1Info *info = (struct BF1Info *)bf1->device_data;

	int fd = serial_open(bf1->device_path, info->baud, 1, true);
	if (unlikely(-1 == fd))
	{
		applog(LOG_ERR, "Failed to open Bitfury BF1 on %s", bf1->device_path);
		return false;
	}

	bf1->device_fd = fd;

	applog(LOG_INFO, "Opened Bitfury BF1 on %s", bf1->device_path);

	struct timeval tv_now;
	gettimeofday(&tv_now, NULL);
	timer_set_delay(&thr->tv_poll, &tv_now, 1000000);

	info->work = 0;
	info->prev_work = 0;

	return true;
}

//------------------------------------------------------------------------------
static void bf1_get_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *board = thr->cgpu;
	struct BF1Info *info = (struct BF1Info *)board->device_data;

	info->rx_len = serial_read(board->device_fd, info->rx_buffer, sizeof(info->rx_buffer));
	applog(LOG_INFO, "Length: %d", info->rx_len);

	job_results_fetched(thr);
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
static void bf1_process_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *board = thr->cgpu;
	struct BF1Info *info = (struct BF1Info *)board->device_data;

	uint32_t results[16*6];
	uint32_t num_results;

	uint32_t m7    = *((uint32_t *)&work->data[64]);
	uint32_t ntime = *((uint32_t *)&work->data[68]);
	uint32_t nbits = *((uint32_t *)&work->data[72]);

	num_results = 0;
	for(int i=0; i<info->rx_len; i+=7)
	{
		struct BF1State *state = (struct BF1State *)(info->rx_buffer + i + 1);

		if(duplicate(results, num_results, state->nonce))
		{
			continue;
		}

		uint32_t nonce = bf1_decnonce(state->nonce);
		results[num_results++] = state->nonce;

		//applog(LOG_INFO, "Len: %d Cmd: %c State: %c Switched: %d Nonce: %08X", info->rx_len, info->rx_buffer[i], state->state, state->switched, nonce);
		if(bf1_rehash(work->midstate, m7, ntime, nbits, nonce))
		{
			submit_nonce(thr, work, nonce);
		}
		if(bf1_rehash(work->midstate, m7, ntime, nbits, nonce-0x400000))
		{
			submit_nonce(thr, work, nonce-0x400000);
		}
		if(bf1_rehash(work->midstate, m7, ntime, nbits, nonce-0x800000))
		{
			submit_nonce(thr, work, nonce-0x800000);
		}
		if(bf1_rehash(work->midstate, m7, ntime, nbits, nonce+0x2800000))
		{
			submit_nonce(thr, work, nonce+0x2800000);
		}
		if(bf1_rehash(work->midstate, m7, ntime, nbits, nonce+0x2C00000))
		{
			submit_nonce(thr, work, nonce+0x2C00000);
		}
		if(bf1_rehash(work->midstate, m7, ntime, nbits, nonce+0x400000))
		{
			submit_nonce(thr, work, nonce+0x400000);
		}
	}
}

//------------------------------------------------------------------------------
static bool bf1_work_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t nonce)
{
	work->blk.nonce = 0xFFFFFFFFUL;

	return true;
}

//------------------------------------------------------------------------------
static int64_t bf1_scanwork(struct thr_info *thr)
{
	struct cgpu_info *board = thr->cgpu;
	struct BF1Info *info = (struct BF1Info *)board->device_data;

	struct BF1HashData hash_data;
	uint32_t hashes = 0;
	uint32_t count = 0;

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

	applog(LOG_INFO, "Work Task sending");
	while(1)
	{
		uint8_t buffer[7];
		int len;
		len = serial_read(board->device_fd, buffer, 7);
		if(len > 0)
			break;
	}

	applog(LOG_INFO, "Work Task sent");
	while(1)
	{
		info->rx_len = serial_read(board->device_fd, info->rx_buffer, sizeof(info->rx_buffer));
		if(info->rx_len > 0)
			break;
	}
	applog(LOG_INFO, "Work Task accepted");

	if(info->prev_work)
	{
		bf1_process_results(thr, info->prev_work);
		work_completed(board, info->prev_work);
		info->prev_work = 0;
	}
	info->prev_work = info->work;
	info->work = 0;

	hashes = 0xffffffff;
	applog(LOG_INFO, "WORK completed");

	return hashes;
}

//------------------------------------------------------------------------------
static void bf1_poll(struct thr_info *thr)
{
	struct cgpu_info *board = thr->cgpu;
	struct BF1Info *info = (struct BF1Info *)board->device_data;
/*
	uint8_t rx_buf[128];
	int len = 0;
	len = serial_read(board->device_fd, rx_buf, sizeof(rx_buf));

	applog(LOG_INFO, "POLL: serial read: %d", len);
*/
	struct timeval tv_now;
	gettimeofday(&tv_now, NULL);
	timer_set_delay(&thr->tv_poll, &tv_now, 1000000);
}

//------------------------------------------------------------------------------
static void bf1_statline_before(char *buf, struct cgpu_info *cgpu)
{
	char before[] = "                    ";
	if(cgpu->device_data)
	{
		struct BF1Info *info = (struct BF1Info *)cgpu->device_data;

		int len = strlen(info->id.product);
		memcpy(before, info->id.product, len);

		sprintf(before + len + 1, "%08X", info->id.serial);
	}
	tailsprintf(buf, "%s | ", &before[0]);
}

//------------------------------------------------------------------------------
static void bf1_shutdown(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	serial_close(cgpu->device_fd);
}

//------------------------------------------------------------------------------
struct device_drv bf1_drv = {
	.dname = "Bitfury BF1",
	.name = "BF1",
	.minerloop = hash_queued_work,

	.drv_detect = bf1_detect,
	.get_statline_before = bf1_statline_before,

	.thread_init = bf1_init,
	.thread_shutdown = bf1_shutdown,
/*
	.job_prepare = bf1_work_prepare,
	.job_get_results = bf1_get_results,
	.job_process_results = bf1_process_results,
	.job_start = bf1_scanwork,
*/

	.poll = bf1_poll,

	.scanwork = bf1_scanwork,
};
