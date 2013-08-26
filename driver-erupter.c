/*
 * Copyright 2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include "miner.h"
#include "fpgautils.h"
#include "icarus-common.h"

#define ERUPTER_IO_SPEED 115200
#define ERUPTER_HASH_TIME 0.0000000029761

extern struct device_drv erupter_drv;
extern struct device_drv erupter_drv_emerald;

static bool _erupter_detect_one(const char *devpath, struct device_drv *drv)
{
	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");

	*info = (struct ICARUS_INFO){
		.baud = ERUPTER_IO_SPEED,
		.Hs = ERUPTER_HASH_TIME,
		.timing_mode = MODE_DEFAULT,
		.continue_search = true,
	};

	if (!icarus_detect_custom(devpath, drv, info)) {
		free(info);
		return false;
	}
	return true;
}

static bool erupter_emerald_detect_one(const char *devpath)
{
	// For detection via BEE:*
	return _erupter_detect_one(devpath, &erupter_drv_emerald);
}

static bool erupter_detect_one(const char *devpath)
{
	struct device_drv *drv = &erupter_drv;
	
	// For autodetection
	if (unlikely(detectone_meta_info.product && strstr(detectone_meta_info.product, "Emerald")))
		drv = &erupter_drv_emerald;
	
	return _erupter_detect_one(devpath, drv);
}

static int erupter_emerald_detect_auto(void)
{
	return serial_autodetect(erupter_emerald_detect_one, "Block", "Erupter", "Emerald");
}

static int erupter_detect_auto(void)
{
	return serial_autodetect(erupter_detect_one, "Block", "Erupter");
}

static void erupter_drv_init();

static void erupter_detect()
{
	erupter_drv_init();
	// Actual serial detection is handled by Icarus driver
	serial_detect_auto_byname(&erupter_drv, erupter_detect_one, erupter_detect_auto);
	serial_detect_auto_byname(&erupter_drv_emerald, erupter_emerald_detect_one, erupter_emerald_detect_auto);
}

static bool erupter_identify(struct cgpu_info *erupter)
{
	struct thr_info *thr = erupter->thr[0];
	struct icarus_state *state = thr->cgpu_data;
	state->identify = true;
	return true;
}

static void erupter_drv_init()
{
	erupter_drv = icarus_drv;
	erupter_drv.dname = "erupter";
	erupter_drv.name = "BES";
	erupter_drv.drv_detect = erupter_detect;
	erupter_drv.identify_device = erupter_identify;
	
	erupter_drv_emerald = erupter_drv;
	erupter_drv_emerald.name = "BEE";
}

struct device_drv erupter_drv = {
	// Needed to get to erupter_drv_init at all
	.drv_detect = erupter_detect,
};

struct device_drv erupter_drv_emerald;
