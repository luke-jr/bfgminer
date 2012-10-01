/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "compat.h"
#include "dynclock.h"
#include "fpgautils.h"
#include "icarus-common.h"
#include "miner.h"

#define CAIRNSMORE1_IO_SPEED 115200

// This is a general ballpark
#define CAIRNSMORE1_HASH_TIME 0.0000000024484

#define CAIRNSMORE1_MINIMUM_CLOCK  5
#define CAIRNSMORE1_DEFAULT_CLOCK  200
#define CAIRNSMORE1_MAXIMUM_CLOCK  200

struct device_api cairnsmore_api;

static bool cairnsmore_detect_one(const char *devpath)
{
	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");

	info->baud = CAIRNSMORE1_IO_SPEED;
	info->work_division = 2;
	info->fpga_count = 2;
	info->quirk_reopen = false;
	info->Hs = CAIRNSMORE1_HASH_TIME;
	info->timing_mode = MODE_LONG;
	info->do_icarus_timing = true;

	if (!icarus_detect_custom(devpath, &cairnsmore_api, info)) {
		free(info);
		return false;
	}
	return true;
}

static int cairnsmore_detect_auto(void)
{
	return
	serial_autodetect_udev     (cairnsmore_detect_one, "*Cairnsmore1*") ?:
	serial_autodetect_devserial(cairnsmore_detect_one, "Cairnsmore1") ?:
	serial_autodetect_ftdi     (cairnsmore_detect_one, "Cairnsmore1", NULL) ?:
	0;
}

static void cairnsmore_detect()
{
	// Actual serial detection is handled by Icarus driver
	serial_detect_auto_byname(&cairnsmore_api, cairnsmore_detect_one, cairnsmore_detect_auto);
}

static bool cairnsmore_send_cmd(int fd, uint8_t cmd, uint8_t data)
{
	unsigned char pkt[64] =
		"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		"vdi\xb7"
		"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		"BFG0" "\xff\xff\xff\xff" "\x23\0\0\0";
	pkt[32] = 0xda ^ cmd ^ data;
	pkt[33] = data;
	pkt[34] = cmd;
	return write(fd, pkt, sizeof(pkt)) == sizeof(pkt);
}

bool cairnsmore_supports_dynclock(int fd)
{
	if (!cairnsmore_send_cmd(fd, 0, 1))
		return false;

	uint32_t nonce = 0;
	{
		struct timeval tv_finish;
		struct thr_info dummy = {
			.work_restart = false,
			.work_restart_fd = -1,
		};
		icarus_gets((unsigned char*)&nonce, fd, &tv_finish, &dummy, 1);
	}
	switch (nonce) {
		case 0x0023b2ee:  // big    endian
		case 0xeeb22300:  // little endian
			// Hashed the command, so it's not supported
			return false;
		default:
			// TODO: nonce from a real job... handle it
		case 0:
			return true;
	}
}

static bool cairnsmore_change_clock_func(struct thr_info *thr, int bestM)
{
	struct cgpu_info *cm1 = thr->cgpu;
	struct ICARUS_INFO *info = cm1->cgpu_data;

	if (unlikely(!cairnsmore_send_cmd(cm1->device_fd, 0, bestM)))
		return false;

	// Adjust Hs expectations for frequency change
	info->Hs = info->Hs * (double)bestM / (double)info->dclk.freqM;

	char repr[0x10];
	sprintf(repr, "%s %u", cm1->api->name, cm1->device_id);
	dclk_msg_freqchange(repr, 2.5 * (double)info->dclk.freqM, 2.5 * (double)bestM, NULL);
	info->dclk.freqM = bestM;

	return true;
}

static bool cairnsmore_init(struct thr_info *thr)
{
	struct cgpu_info *cm1 = thr->cgpu;
	struct ICARUS_INFO *info = cm1->cgpu_data;
	struct icarus_state *state = thr->cgpu_data;

	if (cairnsmore_supports_dynclock(cm1->device_fd)) {
		info->dclk_change_clock_func = cairnsmore_change_clock_func;

		dclk_prepare(&info->dclk);
		info->dclk.freqMaxM = CAIRNSMORE1_MAXIMUM_CLOCK / 2.5;
		info->dclk.freqM =
		info->dclk.freqMDefault = CAIRNSMORE1_DEFAULT_CLOCK / 2.5;
		cairnsmore_send_cmd(cm1->device_fd, 0, info->dclk.freqM);
		applog(LOG_WARNING, "%s %u: Frequency set to %u Mhz (range: %u-%u)",
		       cm1->api->name, cm1->device_id,
		       CAIRNSMORE1_DEFAULT_CLOCK, CAIRNSMORE1_MINIMUM_CLOCK, CAIRNSMORE1_MAXIMUM_CLOCK
		);
	} else {
		applog(LOG_WARNING, "%s %u: Frequency scaling not supported",
			cm1->api->name, cm1->device_id
		);
	}
	// Commands corrupt the hash state, so next scanhash is a firstrun
	state->firstrun = true;

	return true;
}

void convert_icarus_to_cairnsmore(struct cgpu_info *cm1)
{
	struct ICARUS_INFO *info = cm1->cgpu_data;
	info->Hs = CAIRNSMORE1_HASH_TIME;
	info->fullnonce = info->Hs * (((double)0xffffffff) + 1);
	info->timing_mode = MODE_LONG;
	info->do_icarus_timing = true;
	cm1->api = &cairnsmore_api;
	renumber_cgpu(cm1);
	cairnsmore_init(cm1->thr[0]);
}

static struct api_data *cairnsmore_api_extra_device_status(struct cgpu_info *cm1)
{
	struct ICARUS_INFO *info = cm1->cgpu_data;
	struct api_data*root = NULL;

	if (info->dclk.freqM) {
		double frequency = 2.5 * info->dclk.freqM;
		root = api_add_freq(root, "Frequency", &frequency, true);
	}

	return root;
}

static bool cairnsmore_identify(struct cgpu_info *cm1)
{
	struct ICARUS_INFO *info = cm1->cgpu_data;
	if (!info->dclk.freqM)
		return false;
	
	cairnsmore_send_cmd(cm1->device_fd, 1, 1);
	sleep(5);
	cairnsmore_send_cmd(cm1->device_fd, 1, 0);
	cm1->flash_led = true;
	return true;
}

extern struct device_api icarus_api;

__attribute__((constructor(1000)))
static void cairnsmore_api_init()
{
	cairnsmore_api = icarus_api;
	cairnsmore_api.dname = "cairnsmore";
	cairnsmore_api.name = "ECM";
	cairnsmore_api.api_detect = cairnsmore_detect;
	cairnsmore_api.thread_init = cairnsmore_init;
	cairnsmore_api.identify_device = cairnsmore_identify;
	cairnsmore_api.get_api_extra_device_status = cairnsmore_api_extra_device_status;
}
