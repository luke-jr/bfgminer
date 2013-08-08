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

//------------------------------------------------------------------------------
static bool bf1_checkNonce(struct cgpu_info *cgpu,
                            struct work *work,
                            int nonce)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[80];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	unsigned char hash2[32];
	uint32_t *hash2_32 = (uint32_t *)hash2;

	swap32[76/4] = htobe32(nonce);

	swap32yes(swap32, data32, 76 / 4);

	sha2(swap, 80, hash1);
	sha2(hash1, 32, hash2);

	if(hash2_32[7] == 0)
		return true;

	return false;
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
	write(fd, "R", 1);
	len = serial_read(fd, buf, sizeof(buf_state));
	serial_close(fd);

	if(len != sizeof(buf_state))
	{
		return false;
	}

	serial_close(fd);


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

	info->baud = BF1_BAUD;

	if (!bf1_detect_custom(devpath, &bf1_drv, info))
	{
		free(info);
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
static void bf1_detect()
{
	applog(LOG_WARNING, "Searching for Bitfury BF1 devices");
	serial_detect(&bf1_drv, bf1_detect_one);
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
	//notifier_init(thr->work_restart_notifier);

	applog(LOG_INFO, "Opened Bitfury BF1 on %s", bf1->device_path);

	return true;
}

//------------------------------------------------------------------------------
static int64_t bf1_scanwork(struct thr_info *thr)
{
	struct cgpu_info *board = thr->cgpu;
	struct BF1Info *info = (struct BF1Info *)board->device_data;

	struct BF1HashData hash_data;
	uint32_t hashes = 0;

	if(!info->work)
	{
		info->work = get_queued(board);
		if(info->work == NULL)
		{
			return 0;
		}
	}

	uint8_t sendbuf[45];
	sendbuf[0] = 'W';
	memcpy(sendbuf + 1, work->midstate, 32);
	memcpy(sendbuf + 33, work->data + 64, 12);
	write(board->device_fd, sendbuf, sizeof(sendbuf));

	applog(LOG_DEBUG, "%"PRIpreprv": sent hashdata", board->proc_repr);

	char rxbuffer[1024];
	int len;
	len = serial_read(board->device_fd, rxbuffer, sizeof(rxbuffer));
	for(int i=0; i<len; i+=7)
	{
		applog(LOG_INFO, "Cmd: %c State: %c Valid: %d Nonce: %08X", rxbuffer[0], rxbuffer[1], rxbuffer[2], *((uint32_t *)&rxbuffer[3]));
		hashes = 0xFFFFFFFFUL;
	}

	return hashes;
/*

	bool overflow = false;
	int count = 0;
	uint32_t nonce_count = 0;

	uint8_t buffer[128];
	while (!(overflow || thr->work_restart))
	{
		if (!restart_wait(thr, 250))
		{
			applog(LOG_DEBUG, "%"PRIpreprv": New work detected", board->proc_repr);
			break;
		}

		int len = serial_read(board->device_fd, buffer, sizeof(struct bf1State)+1);
		if(len == (sizeof(struct bf1State)+1))
		{
			struct bf1State *state = (struct bf1State *)(buffer+1);
			applog(LOG_DEBUG, "State: %c, %d, %08x", state->state, state->nonce_valid, state->nonce);

			if(state->nonce_valid == 1)
			{
				nonce_count = state->nonce;
				if(bf1_checkNonce(board, work, state->nonce))
				{
					applog(LOG_INFO, "Golden nonce found: %08X", state->nonce);
					submit_nonce(thr, work, state->nonce);
				}
				break;
			}
			else if(state->state == 'R')
			{
				nonce_count = 0xFFFFFFFFUL;
				break;
			}
		}

		if (thr->work_restart)
		{
			applog(LOG_DEBUG, "%"PRIpreprv": New work detected", board->proc_repr);
			break;
		}
	}

	work->blk.nonce = 0xffffffff;
	return nonce_count;
*/
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
	.drv_detect = bf1_detect,
	.get_statline_before = bf1_statline_before,
	.scanwork = bf1_scanwork,
	.thread_init = bf1_init,
	.thread_shutdown = bf1_shutdown,
};
