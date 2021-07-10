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

#include "miner.h"
#include "driver-icarus.h"
#include "lowlevel.h"
#include "lowl-vcom.h"

#define ERUPTER_IO_SPEED 115200
#define ERUPTER_HASH_TIME 0.0000000029761

BFG_REGISTER_DRIVER(erupter_drv)
BFG_REGISTER_DRIVER(erupter_drv_emerald)

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

static
bool erupter_emerald_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_product(info, "Block", "Erupter", "Emerald");
}

static bool erupter_emerald_detect_one(const char *devpath)
{
	// For detection via BEE:*
	return _erupter_detect_one(devpath, &erupter_drv_emerald);
}

static
bool erupter_emerald_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, erupter_emerald_detect_one);
}

static
bool erupter_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_vcom, "Block", "Erupter");
}

static bool erupter_detect_one(const char *devpath)
{
	struct device_drv *drv = &erupter_drv;
	
	// For autodetection
	if (unlikely(detectone_meta_info.product && strstr(detectone_meta_info.product, "Emerald")))
		drv = &erupter_drv_emerald;
	
	return _erupter_detect_one(devpath, drv);
}

static
bool erupter_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, erupter_detect_one);
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
	erupter_drv.lowl_match = erupter_lowl_match;
	erupter_drv.lowl_probe = erupter_lowl_probe;
	erupter_drv.identify_device = erupter_identify;
	erupter_drv.probe_priority = -120;
	
	erupter_drv_emerald = erupter_drv;
	erupter_drv_emerald.name = "BEE";
	erupter_drv_emerald.lowl_match = erupter_emerald_lowl_match;
	erupter_drv_emerald.lowl_probe = erupter_emerald_lowl_probe;
	erupter_drv_emerald.probe_priority = -119;
}

struct device_drv erupter_drv = {
	.drv_init = erupter_drv_init,
};

struct device_drv erupter_drv_emerald = {
	.drv_init = erupter_drv_init,
};
