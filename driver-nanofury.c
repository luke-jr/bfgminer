/*
 * Copyright 2013-2014 Luke Dashjr
 * Copyright 2013-2014 Vladimir Strinski
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
#define NANOFURY_GP_PIN_PWR_EN0 7

#define NANOFURY_MAX_BYTES_PER_SPI_TRANSFER 60			// due to MCP2210 limitation

BFG_REGISTER_DRIVER(nanofury_drv)
static const struct bfg_set_device_definition nanofury_set_device_funcs[];

struct nanofury_state {
	struct lowlevel_device_info *lowl_info;
	struct mcp2210_device *mcp;
	struct timeval identify_started;
	bool identify_requested;
	unsigned long current_baud;
	bool ledalternating;
	bool ledvalue;
	bool powered_off;
};

// Bit-banging reset, to reset more chips in chain - toggle for longer period... Each 3 reset cycles reset first chip in chain
static
bool nanofury_spi_reset(struct mcp2210_device * const mcp)
{
	int r;
	char tx[1] = {0x81};  // will send this waveform: - _ _ _  _ _ _ -
	char buf[1];
	
	// SCK_OVRRIDE
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_SCK_OVR, BGV_HIGH))
		return false;
	
	for (r = 0; r < 16; ++r)
		if (!mcp2210_spi_transfer(mcp, tx, buf, 1))
			return false;
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) == BGV_ERROR)
		return false;
	
	return true;
}

static void nanofury_device_off(struct mcp2210_device *, struct nanofury_state *);

static
bool nanofury_spi_txrx(struct spi_port * const port)
{
	struct cgpu_info * const cgpu = port->cgpu;
	struct nanofury_state * const state = port->userp;
	struct mcp2210_device * const mcp = state->mcp;
	const void *wrbuf = spi_gettxbuf(port);
	void *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	const uint8_t *ptrwrbuf = wrbuf;
	uint8_t *ptrrdbuf = rdbuf;
	
	if (state->current_baud != port->speed)
	{
		applog(LOG_NOTICE, "%"PRIpreprv": Changing baud from %lu to %lu",
		       cgpu ? cgpu->proc_repr : nanofury_drv.dname,
		       (unsigned long)state->current_baud, (unsigned long)port->speed);
		if (!mcp2210_configure_spi(mcp, port->speed, 0xffff, 0xffef, 0, 0, 0))
			goto err;
		state->current_baud = port->speed;
	}
	
	nanofury_spi_reset(mcp);
	
	// start by sending chunks of 60 bytes...
	while (bufsz >= NANOFURY_MAX_BYTES_PER_SPI_TRANSFER)
	{
		if (!mcp2210_spi_transfer(mcp, ptrwrbuf, ptrrdbuf, NANOFURY_MAX_BYTES_PER_SPI_TRANSFER))
			goto err;
		ptrrdbuf += NANOFURY_MAX_BYTES_PER_SPI_TRANSFER;
		ptrwrbuf += NANOFURY_MAX_BYTES_PER_SPI_TRANSFER;
		bufsz -= NANOFURY_MAX_BYTES_PER_SPI_TRANSFER;
	}
	
	// send any remaining bytes...
	if (bufsz > 0)
	{
		if (!mcp2210_spi_transfer(mcp, ptrwrbuf, ptrrdbuf, bufsz))
			goto err;
	}
	
	return true;

err:
	mcp2210_spi_cancel(mcp);
	nanofury_device_off(mcp, state);
	if (cgpu)
	{
		struct thr_info * const thr = cgpu->thr[0];
		hashes_done2(thr, -1, NULL);
	}
	return false;
}

static
void nanofury_send_led_gpio(struct nanofury_state * const state)
{
	struct mcp2210_device * const mcp = state->mcp;
	mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_LED, state->ledvalue ? BGV_HIGH : BGV_LOW);
}

static
void nanofury_do_led_alternating(struct nanofury_state * const state)
{
	state->ledvalue = !state->ledvalue;
	nanofury_send_led_gpio(state);
}

static
void nanofury_device_off(struct mcp2210_device * const mcp, struct nanofury_state * const state)
{
	// Try to reset everything back to input
	for (int i = 0; i < 9; ++i)
		mcp2210_get_gpio_input(mcp, i);
	if (state)
		state->powered_off = true;
}

static
bool nanofury_power_enable(struct mcp2210_device * const mcp, const bool poweron, struct nanofury_state * const state)
{
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_PWR_EN, poweron ? BGV_HIGH : BGV_LOW))
		return false;
	
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_PWR_EN0, poweron ? BGV_LOW : BGV_HIGH))
		return false;
	
	if (state)
		state->powered_off = !poweron;
	
	return true;
}

static
bool nanofury_checkport(struct mcp2210_device * const mcp, const unsigned long baud, struct nanofury_state * const state)
{
	int i;
	const char tmp = 0;
	char tmprx;
	
	// default: set everything to input
	for (i = 0; i < 9; ++i)
		if (BGV_ERROR == mcp2210_get_gpio_input(mcp, i))
			goto fail;
	
	// configure the pins that we need:
	
	// LED
	if (!mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_LED, BGV_HIGH))
		goto fail;
	
	nanofury_power_enable(mcp, true, state);
	
	// cancel any outstanding SPI transfers
	mcp2210_spi_cancel(mcp);
	
	// configure SPI
	// This is the only place where speed, mode and other settings are configured!!!
	if (!mcp2210_configure_spi(mcp, baud, 0xffff, 0xffef, 0, 0, 0))
		goto fail;
	if (!mcp2210_set_spimode(mcp, 0))
		goto fail;
	
	if (!mcp2210_spi_transfer(mcp, &tmp, &tmprx, 1))
		goto fail;
	
	// after this command SCK_OVRRIDE should read the same as current SCK value (which for mode 0 should be 0)
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) != BGV_LOW)
		goto fail;
	
	// switch SCK to polarity (default SCK=1 in mode 2)
	if (!mcp2210_set_spimode(mcp, 2))
		goto fail;
	if (!mcp2210_spi_transfer(mcp, &tmp, &tmprx, 1))
		goto fail;
	
	// after this command SCK_OVRRIDE should read the same as current SCK value (which for mode 2 should be 1)
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) != BGV_HIGH)
		goto fail;
	
	// switch SCK to polarity (default SCK=0 in mode 0)
	if (!mcp2210_set_spimode(mcp, 0))
		goto fail;
	if (!mcp2210_spi_transfer(mcp, &tmp, &tmprx, 1))
		goto fail;
	
	if (mcp2210_get_gpio_input(mcp, NANOFURY_GP_PIN_SCK_OVR) != BGV_LOW)
		goto fail;
	
	return true;

fail:
	nanofury_device_off(mcp, state);
	return false;
}

static
bool nanofury_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_mcp2210, NANOFURY_USB_PRODUCT);
}

static
bool nanofury_lowl_probe(const struct lowlevel_device_info * const info)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	struct mcp2210_device *mcp;
	struct spi_port *port;
	struct nanofury_state *state;
	int chips;
	
	if (info->lowl != &lowl_mcp2210)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		if (info->lowl != &lowl_hid && info->lowl != &lowl_usb)
			applog(LOG_DEBUG, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not mcp2210!",
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
	
	state = malloc(sizeof(*state));
	*state = (struct nanofury_state){
		.mcp = mcp,
		.ledvalue = true,
	};
	port = calloc(1, sizeof(*port));
	port->userp = state;
	port->txrx = nanofury_spi_txrx;
	port->repr = nanofury_drv.dname;
	port->logprio = LOG_DEBUG;
	port->speed = 200000;
	
	{
		struct bitfury_device dummy_bitfury = {
			.spi = port,
		};
		drv_set_defaults(&nanofury_drv, bitfury_set_device_funcs_probe, &dummy_bitfury, NULL, NULL, 1);
	}
	
	if (!nanofury_checkport(mcp, port->speed, NULL))
	{
		applog(LOG_WARNING, "%s: Matched \"%s\" serial \"%s\", but failed to detect nanofury",
		       __func__, product, serial);
		mcp2210_close(mcp);
		return false;
	}
	state->current_baud = port->speed;
	
	chips = libbitfury_detectChips1(port);
	free(port);
	
	nanofury_device_off(mcp, NULL);
	mcp2210_close(mcp);
	
	if (lowlevel_claim(&nanofury_drv, true, info))
	{
		free(state);
		return false;
	}
	
	state->lowl_info = lowlevel_ref(info);
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &nanofury_drv,
		.set_device_funcs = nanofury_set_device_funcs,
		.device_data = state,
		.threads = 1,
		.procs = chips,
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

static
bool nanofury_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct nanofury_state * const state = cgpu->device_data;
	struct lowlevel_device_info * const info = state->lowl_info;
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
	if (!nanofury_checkport(mcp, state->current_baud, state))
	{
		applog(LOG_ERR, "%"PRIpreprv": checkport failed", cgpu->proc_repr);
		mcp2210_close(mcp);
		return false;
	}
	
	port = malloc(sizeof(*port));
	bitfury = malloc(sizeof(*bitfury) * cgpu->procs);
	
	if (!(port && bitfury && state))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to allocate structures", cgpu->proc_repr);
		free(port);
		free(bitfury);
		free(state);
		mcp2210_close(mcp);
		return false;
	}
	
	/* Be careful, read lowl-spi.h comments for warnings */
	memset(port, 0, sizeof(*port));
	port->txrx = nanofury_spi_txrx;
	port->cgpu = cgpu;
	port->repr = cgpu->proc_repr;
	port->logprio = LOG_ERR;
	port->speed = state->current_baud;
		
	
	const int init_osc6_bits = 50;
	const int ramp_osc6_bits = (cgpu->procs > 1) ? 5 : init_osc6_bits;
	
	state->mcp = mcp;
	port->userp = state;
	for (proc = cgpu; proc; (proc = proc->next_proc), ++bitfury)
	{
		struct thr_info * const mythr = proc->thr[0];
		*bitfury = (struct bitfury_device){
			.spi = port,
			.fasync = proc->proc_id,
		};
		proc->device_data = bitfury;
		mythr->cgpu_data = state;
		bitfury->osc6_bits = ramp_osc6_bits;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_chip(proc);
	}
	
	--bitfury;
	while (bitfury->osc6_bits < init_osc6_bits)
	{
		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			bitfury = proc->device_data;
			bitfury->osc6_bits += 5;
			bitfury_send_freq(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		}
	}
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		bitfury_init_chip(proc);
		proc->status = LIFE_INIT2;
	}
	
	nanofury_send_led_gpio(state);
	timer_set_now(&thr->tv_poll);
	return true;
}

static
void nanofury_disable(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct nanofury_state * const state = thr->cgpu_data;
	struct mcp2210_device * const mcp = state->mcp;
	
	bitfury_disable(thr);
	
	// Before powering off, ensure no other chip needs power
	for_each_managed_proc(oproc, dev)
		if (oproc->deven == DEV_ENABLED)
			return;
	
	applog(LOG_NOTICE, "%s: Last chip disabled, shutting off power",
	       dev->dev_repr);
	nanofury_device_off(mcp, state);
}

static
void nanofury_enable(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct nanofury_state * const state = thr->cgpu_data;
	struct mcp2210_device * const mcp = state->mcp;
	
	if (state->powered_off)
	{
		// All chips were disabled, so we need to power back on
		applog(LOG_DEBUG, "%s: Enabling power",
		       dev->dev_repr);
		nanofury_checkport(mcp, state->current_baud, state);
		nanofury_send_led_gpio(state);
	}
	
	bitfury_enable(thr);
}

static
void nanofury_reinit(struct cgpu_info * const proc)
{
	struct thr_info * const thr = proc->thr[0];
	struct cgpu_info * const dev = proc->device;
	struct nanofury_state * const state = thr->cgpu_data;
	struct mcp2210_device * const mcp = state->mcp;
	
	nanofury_device_off(mcp, state);
	cgsleep_ms(1);
	for_each_managed_proc(oproc, dev)
		if (oproc->deven == DEV_ENABLED)
			nanofury_enable(oproc->thr[0]);
}

static
double _nanofury_total_diff1(struct cgpu_info * const dev)
{
	double d = 0.;
	for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
		d += proc->diff1;
	return d;
}

static
void nanofury_poll(struct thr_info * const thr)
{
	struct cgpu_info * const dev = thr->cgpu;
	struct nanofury_state * const state = thr->cgpu_data;
	struct mcp2210_device * const mcp = state->mcp;
	double diff1_before = 0.;
	
	if (state->identify_requested)
	{
		if (!timer_isset(&state->identify_started))
			mcp2210_set_gpio_output(mcp, NANOFURY_GP_PIN_LED, state->ledvalue ? BGV_LOW : BGV_HIGH);
		timer_set_delay_from_now(&state->identify_started, 5000000);
		state->identify_requested = false;
	}
	
	if (state->ledalternating && !timer_isset(&state->identify_started))
		diff1_before = _nanofury_total_diff1(dev);
	
	bitfury_do_io(thr);
	
	if (state->ledalternating && (timer_isset(&state->identify_started) || diff1_before != _nanofury_total_diff1(dev)))
		nanofury_do_led_alternating(state);
	
	if (timer_passed(&state->identify_started, NULL))
	{
		// Also used when setting ledmode
		nanofury_send_led_gpio(state);
		timer_unset(&state->identify_started);
	}
}

static
bool nanofury_identify(struct cgpu_info * const cgpu)
{
	struct nanofury_state * const state = cgpu->thr[0]->cgpu_data;
	state->identify_requested = true;
	return true;
}

static
void nanofury_shutdown(struct thr_info * const thr)
{
	struct nanofury_state * const state = thr->cgpu_data;
	if (!state)
		return;
	struct mcp2210_device * const mcp = state->mcp;
	
	if (mcp)
		nanofury_device_off(mcp, state);
}

const char *nanofury_set_ledmode(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct thr_info * const thr = proc->thr[0];
	struct nanofury_state * const state = thr->cgpu_data;
	
	if (!strcasecmp(setting, "on"))
	{
		state->ledvalue = true;
		state->ledalternating = false;
	}
	else
	if (!strcasecmp(setting, "off"))
		state->ledvalue = state->ledalternating = false;
	else
	if (!strcasecmp(setting, "alternating"))
		state->ledalternating = true;
	else
		return "Invalid LED mode; must be on/off/alternating";
	
	if (!timer_isset(&state->identify_started))
		timer_set_now(&state->identify_started);
	
	return NULL;
}

static const struct bfg_set_device_definition nanofury_set_device_funcs[] = {
	{"baud", bitfury_set_baud, "SPI baud rate"},
	{"osc6_bits", bitfury_set_osc6_bits, "range 1-"BITFURY_MAX_OSC6_BITS_S" (slow to fast)"},
	{"ledmode", nanofury_set_ledmode, "on/off/alternating"},
	{NULL},
};

struct device_drv nanofury_drv = {
	.dname = "nanofury",
	.name = "NFY",
	.lowl_match = nanofury_lowl_match,
	.lowl_probe = nanofury_lowl_probe,
	
	.thread_init = nanofury_init,
	.thread_disable = nanofury_disable,
	.thread_enable = nanofury_enable,
	.reinit_device = nanofury_reinit,
	.thread_shutdown = nanofury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = nanofury_poll,
	.job_process_results = bitfury_job_process_results,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	.identify_device = nanofury_identify,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
