/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 NanoFury
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "mcp2210.h"
#include "miner.h"

#define NANOFURY_USB_PRODUCT "NanoFury"

#define NANOFURY_GP_PIN_LED 0
#define NANOFURY_GP_PIN_SCK_OVR 5
#define NANOFURY_GP_PIN_PWR_EN 6

#define NANOFURY_MAX_BYTES_PER_SPI_TRANSFER 60			// due to MCP2210 limitation

struct device_drv nanofury_drv;

static
bool nanofury_checkport(struct mcp2210_device * const mcp)
{
	int i;
	const char tmp = 0;
	char tmprx;
	
	// default: set everything to input
	for (i = 0; i < 9; ++i)
		if (MGV_ERROR == mcp2210_get_gpio_input(mcp, i))
			goto fail;
	
	// configure the pins that we need:
	
	// LED
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_LED, MGV_HIGH))
		goto fail;
	
	// PWR_EN
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_PWR_EN, MGV_HIGH))
		goto fail;
	
	// configure SPI
	// This is the only place where speed, mode and other settings are configured!!!
	if (!mcp2210_configure_spi(mcp, 200000, 0xffff, 0xffef, 0, 0, 0))
		goto fail;
	if (!mcp2210_set_spimode(mcp, 0))
		goto fail;
	
	if (!mcp2210_spi_transfer(mcp, &tmp, &tmprx, 1))
		goto fail;
	
	// after this command SCK_OVRRIDE should read the same as current SCK value (which for mode 0 should be 0)
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) != MGV_LOW)
		goto fail;
	
	// switch SCK to polarity (default SCK=1 in mode 2)
	if (!mcp2210_set_spimode(mcp, 2))
		goto fail;
	if (!mcp2210_spi_transfer(mcp, &tmp, &tmprx, 1))
		goto fail;
	
	// after this command SCK_OVRRIDE should read the same as current SCK value (which for mode 2 should be 1)
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) != MGV_HIGH)
		goto fail;
	
	// switch SCK to polarity (default SCK=0 in mode 0)
	if (!mcp2210_set_spimode(mcp, 0))
		goto fail;
	if (!mcp2210_spi_transfer(mcp, &tmp, &tmprx, 1))
		goto fail;
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) != MGV_LOW)
		goto fail;
	
	return true;

fail:
	return false;
}

static
bool nanofury_foundlowl(struct lowlevel_device_info * const info)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	struct mcp2210_device *mcp;
	
	if (info->lowl != &lowl_mcp2210)
	{
		applog(LOG_WARNING, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not mcp2210!",
		       __func__, product, serial);
		return false;
	}
	
	mcp = mcp2210_open(info);
	if (!mcp)
	{
		applog(LOG_WARNING, "%s: Matched \"%s\" serial \"%s\", but mcp2210 lowlevel driver failed to open it",
		       __func__, product, serial);
		return false;
	}
	if (!nanofury_checkport(mcp))
	{
		applog(LOG_WARNING, "%s: Matched \"%s\" serial \"%s\", but failed to detect nanofury",
		       __func__, product, serial);
		// TODO: mcp2210_close(mcp);
		return false;
	}
	// TODO: mcp2210_close(mcp);
	
	// TODO: claim device
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &nanofury_drv,
		.device_data = info,
		.threads = 1,
		// TODO: .name
		// TODO: .device_path
		// TODO: .dev_manufacturer/.dev_product/.dev_serial
		.deven = DEV_ENABLED,
		// TODO: .cutofftemp
	};

	return add_cgpu(cgpu);
}

static bool nanofury_detect_one(const char *serial)
{
	return lowlevel_detect_serial(nanofury_foundlowl, serial);
}

static int nanofury_detect_auto()
{
	return lowlevel_detect(nanofury_foundlowl, NANOFURY_USB_PRODUCT);
}

static void nanofury_detect()
{
	serial_detect_auto(&nanofury_drv, nanofury_detect_one, nanofury_detect_auto);
}

struct device_drv nanofury_drv = {
	.dname = "nanofury",
	.name = "NFY",
	.drv_detect = nanofury_detect,
	
	// .thread_prepare = nanofury_prepare,
	// .thread_init = nanofury_thread_init,
	
	// .minerloop = minerloop_async,
	// .poll = nanofury_poll,
	// .job_prepare = nanofury_job_prepare,
	// .job_start = nanofury_job_start,
	
	// .get_stats = nanofury_get_stats,
	// .get_api_extra_device_status = get_nanofury_api_extra_device_status,
	// .set_device = nanofury_set_device,
	
#ifdef HAVE_CURSES
	// .proc_wlogprint_status = nanofury_wlogprint_status,
	// .proc_tui_wlogprint_choices = nanofury_tui_wlogprint_choices,
	// .proc_tui_handle_choice = nanofury_tui_handle_choice,
#endif
};
