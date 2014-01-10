/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 Angus Gratton
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"

BFG_REGISTER_DRIVER(drillbit_drv)

#define DRILLBIT_MIN_VERSION 2
#define DRILLBIT_MAX_VERSION 3

enum drillbit_capability {
	DBC_TEMP      = 1,
	DBC_EXT_CLOCK = 2,
};

static
bool drillbit_lowl_match(const struct lowlevel_device_info * const info)
{
	if (!lowlevel_match_id(info, &lowl_vcom, 0, 0))
		return false;
	return (info->manufacturer && strstr(info->manufacturer, "Drillbit"));
}

static
bool drillbit_detect_one(const char * const devpath)
{
	uint8_t buf[0x10];
	const int fd = serial_open(devpath, 0, 1, true);
	if (fd == -1)
		applogr(false, LOG_DEBUG, "%s: %s: Failed to open", __func__, devpath);
	if (1 != write(fd, "I", 1))
	{
		applog(LOG_DEBUG, "%s: %s: Error writing 'I'", __func__, devpath);
err:
		serial_close(fd);
		return false;
	}
	if (sizeof(buf) != serial_read(fd, buf, sizeof(buf)))
	{
		applog(LOG_DEBUG, "%s: %s: Short read in response to 'I'",
		       __func__, devpath);
		goto err;
	}
	serial_close(fd);
	
	const unsigned protover = buf[0];
	const unsigned long serialno = (uint32_t)buf[9] | ((uint32_t)buf[0xa] << 8) | ((uint32_t)buf[0xb] << 16) | ((uint32_t)buf[0xc] << 24);
	char * const product = (void*)&buf[1];
	buf[9] = '\0';  // Ensure it is null-terminated (clobbers serial, but we already parsed it)
	const uint8_t chips = buf[0xd];
	uint16_t caps = (uint16_t)buf[0xe] | ((uint16_t)buf[0xf] << 8);
	if (!product[0])
		applogr(false, LOG_DEBUG, "%s: %s: Null product name", __func__, devpath);
	if (!serialno)
		applogr(false, LOG_DEBUG, "%s: %s: Serial number is zero", __func__, devpath);
	if (!chips)
		applogr(false, LOG_DEBUG, "%s: %s: No chips found", __func__, devpath);
	
	int loglev = LOG_WARNING;
	if (!strcmp(product, "DRILLBIT"))
	{
		// Hack: first production firmwares all described themselves as DRILLBIT, so fill in the gaps
		if (chips == 1)
			strcpy(product, "Thumb");
		else
			strcpy(product, "Eight");
	}
	else
	if (chips == 8 && !strcmp(product, "Eight"))
	{}  // Known device
	else
	if (chips == 1 && !strcmp(product, "Thumb"))
	{}  // Known device
	else
		loglev = LOG_DEBUG;
	
	if (protover < DRILLBIT_MIN_VERSION || (loglev == LOG_DEBUG && protover > DRILLBIT_MAX_VERSION))
		applogr(false, loglev, "%s: %s: Unknown device protocol version %u.",
		        __func__, devpath, protover);
	if (protover > DRILLBIT_MAX_VERSION)
		applogr(false, loglev, "%s: %s: Device firmware uses newer Drillbit protocol %u. We only support up to %u. Find a newer BFGMiner!",
		        __func__, devpath, protover, (unsigned)DRILLBIT_MAX_VERSION);
	
	if (protover == 2 && chips == 1)
		// Production firmware Thumbs don't set any capability bits, so fill in the EXT_CLOCK one
		caps |= DBC_EXT_CLOCK;
	
	char *serno = malloc(9);
	snprintf(serno, 9, "%08lx", serialno);
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &drillbit_drv,
		.device_path = strdup(devpath),
		.dev_product = strdup(product),
		.dev_serial = serno,
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
		//.device_data = ,
	};
	return add_cgpu(cgpu);
}

static
bool drillbit_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, drillbit_detect_one);
}

struct device_drv drillbit_drv = {
	.dname = "drillbit",
	.name = "DRB",
	
	.lowl_match = drillbit_lowl_match,
	.lowl_probe = drillbit_lowl_probe,
	
// 	.thread_init = drillbit_init,
	
// 	.minerloop = minerloop_async,
// 	.job_prepare = hashfast_job_prepare,
// 	.job_start = hashfast_noop_job_start,
// 	.job_process_results = hashfast_job_process_results,
// 	.poll = hashfast_poll,
};
