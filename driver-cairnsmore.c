/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "fpgautils.h"
#include "icarus-common.h"
#include "miner.h"

#define CAIRNSMORE1_IO_SPEED 115200
#define CAIRNSMORE1_HASH_TIME 0.0000000024484

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
	serial_detect_auto_byname(cairnsmore_api.dname, cairnsmore_detect_one, cairnsmore_detect_auto);
}

void convert_icarus_to_cairnsmore(struct cgpu_info *cm1)
{
	struct ICARUS_INFO *info = cm1->cgpu_data;
	info->Hs = CAIRNSMORE1_HASH_TIME;
	info->fullnonce = info->Hs * (((double)0xffffffff) + 1);
	info->read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;
	cm1->api = &cairnsmore_api;
	renumber_cgpu(cm1);
}

extern struct device_api icarus_api;

__attribute__((constructor(1000)))
static void cairnsmore_api_init()
{
	cairnsmore_api = icarus_api;
	cairnsmore_api.dname = "cairnsmore";
	cairnsmore_api.name = "ECM";
	cairnsmore_api.api_detect = cairnsmore_detect;
}
