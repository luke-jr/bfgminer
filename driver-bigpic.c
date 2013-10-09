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

#include "libbitfury.h"
#include "deviceapi.h"
#include "sha2.h"

#include "driver-bigpic.h"

#include <stdio.h>

struct device_drv bigpic_drv;

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

	if (1 != write(fd, "I", 1))
	{
		applog(LOG_ERR, "%s: Failed writing id request to %s",
		       bigpic_drv.dname, devpath);
		return false;
	}
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
	if (1 != write(fd, "R", 1))
	{
		applog(LOG_ERR, "%s: Failed writing reset request to %s",
		       bigpic_drv.dname, devpath);
		return false;
	}


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
	return serial_autodetect(bigpic_detect_one, "Bitfury_BF1");
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

	info->tx_buffer[0] = 'W';

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

		uint32_t nonce = bitfury_decnonce(state.nonce);
		results[num_results++] = state.nonce;

		//applog(LOG_DEBUG, "%"PRIpreprv": Len: %d Cmd: %c State: %c Switched: %d Nonce: %08X", board->proc_repr, info->rx_len, info->rx_buffer[i], state->state, state->switched, nonce);
		if (bitfury_fudge_nonce(work->midstate, m7, ntime, nbits, &nonce))
		{
			submit_nonce(thr, work, nonce);
			nonces--;
			continue;
		}
		inc_hw_errors(thr, work, nonce);
	}
}

static
bool bigpic_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info *board = thr->cgpu;
	struct bigpic_info *info = (struct bigpic_info *)board->device_data;
	
	memcpy(&info->tx_buffer[ 1], work->midstate, 32);
	memcpy(&info->tx_buffer[33], &work->data[64], 12);
	
	work->blk.nonce = 0xffffffff;
	return true;
}

static
void bigpic_job_start(struct thr_info *thr)
{
	struct cgpu_info *board = thr->cgpu;
	struct bigpic_info *info = (struct bigpic_info *)board->device_data;
	
	if (opt_dev_protocol && opt_debug)
	{
		char hex[91];
		bin2hex(hex, info->tx_buffer, 45);
		applog(LOG_DEBUG, "%"PRIpreprv": SEND: %s",
		       board->proc_repr, hex);
	}
	
	if (45 != write(board->device_fd, info->tx_buffer, 45))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed writing work task", board->proc_repr);
		dev_error(board, REASON_DEV_COMMS_ERROR);
		job_start_abort(thr, true);
		return;
	}
	
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
	
	mt_job_transition(thr);
	// TODO: Delay morework until right before it's needed
	timer_set_now(&thr->tv_morework);
	job_start_complete(thr);
}

static
int64_t bigpic_job_process_results(struct thr_info *thr, struct work *work, bool stopping)
{
	// FIXME: not sure how to handle stopping
	bigpic_process_results(thr, work);
	
	return 0xBD000000;
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
	if (sizeof(buf) != write(cgpu->device_fd, buf, sizeof(buf)))
		return false;
	
	return true;
}

//------------------------------------------------------------------------------
struct device_drv bigpic_drv = {
	.dname = "bigpic",
	.name = "BPM",

	.drv_detect = bigpic_detect,

	.identify_device = bigpic_identify,

	.thread_init = bigpic_init,
	
	.minerloop = minerloop_async,
	.job_prepare = bigpic_job_prepare,
	.job_start = bigpic_job_start,
	.job_process_results = bigpic_job_process_results,
	
	.thread_shutdown = bigpic_shutdown,
};
