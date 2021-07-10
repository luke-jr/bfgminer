/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdlib.h>

#include "deviceapi.h"
#include "driver-klondike.h"
#include "logging.h"
#include "lowlevel.h"

BFG_REGISTER_DRIVER(hashbusteravalon_drv)

static
bool hashbusteravalon_lowl_match(const struct lowlevel_device_info * const info)
{
	if (!lowlevel_match_id(info, &lowl_usb, 0xfa05, 0x0001))
		return false;
	return (info->manufacturer && strstr(info->manufacturer, "HashBuster"));
}

static
bool hashbusteravalon_lowl_probe(const struct lowlevel_device_info * const info)
{
	struct klondike_info * const klninfo = malloc(sizeof(*klninfo));
	if (unlikely(!klninfo))
		applogr(false, LOG_ERR, "%s: Failed to malloc klninfo", __func__);
	
	*klninfo = (struct klondike_info){
		.clock = 2425,
		.max_work_count = 0x20,
		.old_work_ms = 30000,
		.reply_wait_time = 5,
	};
	
	return klondike_lowl_probe_custom(info, &hashbusteravalon_drv, klninfo);
}

static void hashbusteravalon_drv_init()
{
	hashbusteravalon_drv = klondike_drv;
	hashbusteravalon_drv.dname = "hashbusteravalon";
	hashbusteravalon_drv.name = "HBA";
	hashbusteravalon_drv.lowl_match = hashbusteravalon_lowl_match;
	hashbusteravalon_drv.lowl_probe = hashbusteravalon_lowl_probe;
}

struct device_drv hashbusteravalon_drv = {
	.drv_init = hashbusteravalon_drv_init,
};
