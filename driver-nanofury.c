/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 Vladimir Strinski
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "fpgautils.h"
#include "libbitfury.h"
#include "logging.h"
#include "lowlevel.h"
#include "mcp2210.h"
#include "miner.h"
#include "util.h"

#define NANOFURY_USB_PRODUCT "NanoFury"

#define NANOFURY_GP_PIN_LED 0
#define NANOFURY_GP_PIN_SCK_OVR 5
#define NANOFURY_GP_PIN_PWR_EN 6

#define NANOFURY_MAX_BYTES_PER_SPI_TRANSFER 60			// due to MCP2210 limitation

BFG_REGISTER_DRIVER(nanofury_drv)

// Bit-banging reset, to reset more chips in chain - toggle for longer period... Each 3 reset cycles reset first chip in chain
static
bool nanofury_spi_reset(struct mcp2210_device * const mcp)
{
	int r;
	char tx[1] = {0x81};  // will send this waveform: - _ _ _  _ _ _ -
	char buf[1];
	
	// SCK_OVRRIDE
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_SCK_OVR, MGV_HIGH))
		return false;
	
	for (r = 0; r < 16; ++r)
		if (!mcp2210_spi_transfer(mcp, tx, buf, 1))
			return false;
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) == MGV_ERROR)
		return false;
	
	return true;
}

static
bool nanofury_spi_txrx(struct spi_port * const port)
{
	struct cgpu_info * const cgpu = port->cgpu;
	struct thr_info * const thr = cgpu->thr[0];
	struct mcp2210_device * const mcp = thr->cgpu_data;
	const void *wrbuf = spi_gettxbuf(port);
	void *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	const uint8_t *ptrwrbuf = wrbuf;
	uint8_t *ptrrdbuf = rdbuf;
	
	nanofury_spi_reset(mcp);
	
	// start by sending chunks of 60 bytes...
	while (bufsz >= NANOFURY_MAX_BYTES_PER_SPI_TRANSFER)
	{
		if (!mcp2210_spi_transfer(mcp, ptrwrbuf, ptrrdbuf, NANOFURY_MAX_BYTES_PER_SPI_TRANSFER))
			return false;
		ptrrdbuf += NANOFURY_MAX_BYTES_PER_SPI_TRANSFER;
		ptrwrbuf += NANOFURY_MAX_BYTES_PER_SPI_TRANSFER;
		bufsz -= NANOFURY_MAX_BYTES_PER_SPI_TRANSFER;
	}
	
	// send any remaining bytes...
	if (bufsz > 0)
	{
		if (!mcp2210_spi_transfer(mcp, ptrwrbuf, ptrrdbuf, bufsz))
			return false;
	}
	
	return true;
}

static
void nanofury_device_off(struct mcp2210_device * const mcp)
{
	// Try to reset everything back to input
	for (int i = 0; i < 9; ++i)
		mcp2210_get_gpio_input(mcp, i);
}

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
	nanofury_device_off(mcp);
	return false;
}

static
bool nanofury_foundlowl(struct lowlevel_device_info * const info, __maybe_unused void *userp)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	struct mcp2210_device *mcp;
	
	if (info->lowl != &lowl_mcp2210)
	{
		if (info->lowl != &lowl_hid && info->lowl != &lowl_usb)
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
		mcp2210_close(mcp);
		return false;
	}
	nanofury_device_off(mcp);
	mcp2210_close(mcp);
	
	if (bfg_claim_hid(&nanofury_drv, true, info->path))
		return false;
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &nanofury_drv,
		.device_data = info,
		.threads = 1,
		// TODO: .name
		.device_path = strdup(info->path),
		.dev_manufacturer = maybe_strdup(info->manufacturer),
		.dev_product = maybe_strdup(product),
		.dev_serial = maybe_strdup(serial),
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

static
bool nanofury_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct lowlevel_device_info * const info = cgpu->device_data;
	struct spi_port *port;
	struct bitfury_device *bitfury;
	struct mcp2210_device *mcp;
	
	mcp = mcp2210_open(info);
	lowlevel_devinfo_free(info);
	if (!mcp)
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to open mcp2210 device", cgpu->proc_repr);
		return false;
	}
	if (!nanofury_checkport(mcp))
	{
		applog(LOG_ERR, "%"PRIpreprv": checkport failed", cgpu->proc_repr);
		mcp2210_close(mcp);
		return false;
	}
	
	port = malloc(sizeof(*port));
	bitfury = malloc(sizeof(*bitfury));
	
	if (!(port && bitfury))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to allocate spi_port and bitfury_device structures", cgpu->proc_repr);
		free(port);
		free(bitfury);
		mcp2210_close(mcp);
		return false;
	}
	
	thr->cgpu_data = mcp;
	*port = (struct spi_port){
		.txrx = nanofury_spi_txrx,
		.cgpu = cgpu,
		.repr = cgpu->proc_repr,
		.logprio = LOG_ERR,
	};
	*bitfury = (struct bitfury_device){
		.spi = port,
	};
	cgpu->device_data = bitfury;
	bitfury->osc6_bits = 50;
	bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	bitfury_init_chip(cgpu);
	
	timer_set_now(&thr->tv_poll);
	cgpu->status = LIFE_INIT2;
	return true;
}

static
void nanofury_disable(struct thr_info * const thr)
{
	struct mcp2210_device * const mcp = thr->cgpu_data;
	
	bitfury_disable(thr);
	nanofury_device_off(mcp);
}

static
void nanofury_enable(struct thr_info * const thr)
{
	struct mcp2210_device * const mcp = thr->cgpu_data;
	
	nanofury_checkport(mcp);
	bitfury_enable(thr);
}

static
void nanofury_shutdown(struct thr_info * const thr)
{
	struct mcp2210_device * const mcp = thr->cgpu_data;
	
	nanofury_device_off(mcp);
}

struct device_drv nanofury_drv = {
	.dname = "nanofury",
	.name = "NFY",
	.drv_detect = nanofury_detect,
	
	.thread_init = nanofury_init,
	.thread_disable = nanofury_disable,
	.thread_enable = nanofury_enable,
	.thread_shutdown = nanofury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	.set_device = bitfury_set_device,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
