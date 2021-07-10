/*
 * Copyright 2013-2014 Luke Dashjr
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
#include "lowl-ftdi.h"
#include "miner.h"
#include "util.h"

#define BFX_ADBUS_DIRECTIONS  0xfb
#define BFX_ACBUS_DIRECTIONS  0xff

BFG_REGISTER_DRIVER(bfx_drv)
static const struct bfg_set_device_definition bfx_set_device_funcs[];

struct bfx_state {
	struct lowlevel_device_info *lowl_info;
	struct ft232r_device_handle *ftdi;
};

static
struct ft232r_device_handle *bfx_open(const struct lowlevel_device_info * const info)
{
	struct ft232r_device_handle * const ftdi = ft232h_open_mpsse(info);
	if (!ftdi)
		applogr(NULL, LOG_ERR, "%s: Failed to open", __func__);
	if (!ft232h_mpsse_set_adbus(ftdi, 8, BFX_ADBUS_DIRECTIONS))
	{
		applog(LOG_ERR, "%s: Failed to set A%cBUS pins", __func__, 'D');
		goto err;
	}
	if (!ft232h_mpsse_set_acbus(ftdi, 0, BFX_ACBUS_DIRECTIONS))
	{
		applog(LOG_ERR, "%s: Failed to set A%cBUS pins", __func__, 'C');
		goto err;
	}
	if (!ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH))
		applog(LOG_WARNING, "%s: Failed to purge buffers", __func__);
	
	return ftdi;

err:
	ft232h_mpsse_set_adbus(ftdi, 0, 0);
	ft232h_mpsse_set_acbus(ftdi, 0, 0);
	ft232r_close(ftdi);
	return NULL;
}

static
void bfx_device_off(struct ft232r_device_handle * const ftdi)
{
	// Try to reset everything back to input
	ft232h_mpsse_set_adbus(ftdi, 0, 0);
	ft232h_mpsse_set_acbus(ftdi, 0, 0);
}

static
bool bfx_set_cs(struct ft232r_device_handle * const ftdi, bool high)
{
	const uint8_t val = high ? 8 : 0;
	return ft232h_mpsse_set_adbus(ftdi, val, BFX_ADBUS_DIRECTIONS);
}

static
bool bfx_spi_reset(struct ft232r_device_handle * const ftdi)
{
	uint8_t buf[0x10] = { 0xff };
	for (int i = 1; i < sizeof(buf); ++i)
		buf[i] = ~buf[i - 1];
	
	bfx_set_cs(ftdi, true);
	ft232r_write_all(ftdi, buf, sizeof(buf));
	bfx_set_cs(ftdi, false);
	ft232r_purge_buffers(ftdi, FTDI_PURGE_BOTH);
	
	return true;
}

static
bool bfx_spi_txrx(struct spi_port * const port)
{
	struct cgpu_info * const cgpu = port->cgpu;
	struct bfx_state * const state = port->userp;
	struct ft232r_device_handle * const ftdi = state->ftdi;
	const void *wrbuf = spi_gettxbuf(port);
	void *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	
	bfx_spi_reset(ftdi);
	if (ft232h_mpsse_readwrite_all(ftdi, rdbuf, wrbuf, bufsz) != bufsz)
		goto err;
	
	return true;

err:
	bfx_device_off(ftdi);
	if (cgpu)
	{
		struct thr_info * const thr = cgpu->thr[0];
		hashes_done2(thr, -1, NULL);
	}
	return false;
}

static
bool bfx_lowl_probe(const struct lowlevel_device_info * const info)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	struct ft232r_device_handle *ftdi;
	struct spi_port *port;
	struct bfx_state *state;
	int chips;
	
	if (info->lowl != &lowl_ft232r)
	{
		if (info->lowl != &lowl_usb)
			applog(LOG_DEBUG, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not ft232r!",
			       __func__, product, serial);
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		return false;
	}
	
	ftdi = bfx_open(info);
	if (!ftdi)
		return false;
	
	state = malloc(sizeof(*state));
	*state = (struct bfx_state){
		.ftdi = ftdi,
	};
	port = calloc(1, sizeof(*port));
	port->userp = state;
	port->txrx = bfx_spi_txrx;
	port->repr = bfx_drv.dname;
	port->logprio = LOG_DEBUG;
	
	{
		struct bitfury_device dummy_bitfury = {
			.spi = port,
		};
		drv_set_defaults(&bfx_drv, bitfury_set_device_funcs_probe, &dummy_bitfury, NULL, NULL, 1);
	}
	
	chips = libbitfury_detectChips1(port);
	free(port);
	
	bfx_device_off(ftdi);
	ft232r_close(ftdi);
	
	if (!chips)
	{
		free(state);
		applog(LOG_DEBUG, "%s: 0 chips detected", __func__);
		return false;
	}
	
	if (lowlevel_claim(&bfx_drv, true, info))
	{
		free(state);
		return false;
	}
	
	state->lowl_info = lowlevel_ref(info);
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &bfx_drv,
		.set_device_funcs = bfx_set_device_funcs,
		.device_data = state,
		.threads = 1,
		.procs = chips,
		// TODO: .name
		.dev_manufacturer = maybe_strdup(info->manufacturer),
		.dev_product = maybe_strdup(product),
		.dev_serial = maybe_strdup(serial),
		.deven = DEV_ENABLED,
		// TODO: .cutofftemp
	};

	return add_cgpu(cgpu);
}

static
bool bfx_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct bfx_state * const state = cgpu->device_data;
	struct lowlevel_device_info * const info = state->lowl_info;
	struct spi_port *port;
	struct bitfury_device *bitfury;
	struct ft232r_device_handle *ftdi;
	
	ftdi = bfx_open(info);
	lowlevel_devinfo_free(info);
	if (!ftdi)
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to open ft232r device", cgpu->proc_repr);
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
		ft232r_close(ftdi);
		return false;
	}
	
	/* Be careful, read lowl-spi.h comments for warnings */
	memset(port, 0, sizeof(*port));
	port->txrx = bfx_spi_txrx;
	port->cgpu = cgpu;
	port->repr = cgpu->proc_repr;
	port->logprio = LOG_ERR;
	
	state->ftdi = ftdi;
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
		bitfury->osc6_bits = 50;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_chip(proc);
		proc->status = LIFE_INIT2;
	}
	
	timer_set_now(&thr->tv_poll);
	return true;
}

static
void bfx_disable(struct thr_info * const thr)
{
	struct bfx_state * const state = thr->cgpu_data;
	struct ft232r_device_handle * const ftdi = state->ftdi;
	
	bitfury_disable(thr);
	bfx_device_off(ftdi);
}

static
void bfx_reinit(struct cgpu_info * const cgpu)
{
	struct thr_info * const thr = cgpu->thr[0];
	struct bfx_state * const state = thr->cgpu_data;
	struct ft232r_device_handle * const ftdi = state->ftdi;
	
	bfx_device_off(ftdi);
	cgsleep_ms(1);
	bitfury_enable(thr);
}

static
void bfx_shutdown(struct thr_info * const thr)
{
	struct bfx_state * const state = thr->cgpu_data;
	struct ft232r_device_handle * const ftdi = state->ftdi;
	
	if (ftdi)
		bfx_device_off(ftdi);
}

static const struct bfg_set_device_definition bfx_set_device_funcs[] = {
	{"osc6_bits", bitfury_set_osc6_bits, "range 1-"BITFURY_MAX_OSC6_BITS_S" (slow to fast)"},
	{NULL},
};

struct device_drv bfx_drv = {
	.dname = "bfx",
	.name = "BFX",
	.lowl_probe = bfx_lowl_probe,
	
	.thread_init = bfx_init,
	.thread_disable = bfx_disable,
	.thread_enable = bitfury_enable,
	.reinit_device = bfx_reinit,
	.thread_shutdown = bfx_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
