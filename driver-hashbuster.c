/*
 * Copyright 2013-2014 Luke Dashjr
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
#include "libbitfury.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-hid.h"
#include "miner.h"

#define HASHBUSTER_USB_PRODUCT "HashBuster"

#define HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER 62

BFG_REGISTER_DRIVER(hashbuster_drv)

static
bool hashbuster_io(hid_device * const h, void * const buf, const void * const cmd)
{
	const uint8_t cmdbyte = *((uint8_t *)cmd);
	char x[0x81];
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, cmd, 0x40);
		applog(LOG_DEBUG, "%s(%p): SEND: %s", __func__, h, x);
	}
	const bool rv = likely(
		0x40 == hid_write(h, cmd, 0x40) &&
		0x40 == hid_read (h, buf, 0x40) &&
		((uint8_t *)buf)[0] == cmdbyte
	);
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, buf, 0x40);
		applog(LOG_DEBUG, "%s(%p): RECV: %s", __func__, h, x);
	}
	return rv;
}

static
bool hashbuster_spi_config(hid_device * const h, const uint8_t mode, const uint8_t miso, const uint32_t freq)
{
	uint8_t buf[0x40] = {'\x01', '\x01', mode, miso};
	switch (freq)
	{
		case 100000:
			buf[4] = '\0';
			break;
		case 750000:
			buf[4] = '\x01';
			break;
		case 3000000:
			buf[4] = '\x02';
			break;
		case 12000000:
			buf[4] = '\x03';
			break;
		default:
			return false;
	}
	if (!hashbuster_io(h, buf, buf))
		return false;
	return (buf[1] == '\x0f');
}

static
bool hashbuster_spi_disable(hid_device * const h)
{
	uint8_t buf[0x40] = {'\x01'};
	if (!hashbuster_io(h, buf, buf))
		return false;
	return (buf[1] == '\x0f');
}

static
bool hashbuster_spi_reset(hid_device * const h, uint8_t chips)
{
	uint8_t buf[0x40] = {'\x02', chips};
	if (!hashbuster_io(h, buf, buf))
		return false;
	return (buf[1] == 0xff);
}

static
bool hashbuster_spi_transfer(hid_device * const h, void * const buf, const void * const data, size_t datasz)
{
	if (datasz > HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
		return false;
	uint8_t cbuf[0x40] = {'\x03', datasz};
	memcpy(&cbuf[2], data, datasz);
	if (!hashbuster_io(h, cbuf, cbuf))
		return false;
	if (cbuf[1] != datasz)
		return false;
	memcpy(buf, &cbuf[2], datasz);
	return true;
}

static
bool hashbuster_spi_txrx(struct spi_port * const port)
{
	hid_device * const h = port->userp;
	const uint8_t *wrbuf = spi_gettxbuf(port);
	uint8_t *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	
	hashbuster_spi_disable(h);
	hashbuster_spi_reset(h, 0x10);
	
	hashbuster_spi_config(h, port->mode, 0, port->speed);
	
	while (bufsz >= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
	{
		if (!hashbuster_spi_transfer(h, rdbuf, wrbuf, HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER))
			return false;
		rdbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		wrbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		bufsz -= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
	}
	
	if (bufsz > 0)
	{
		if (!hashbuster_spi_transfer(h, rdbuf, wrbuf, bufsz))
			return false;
	}
	
	return true;
}

static
bool hashbuster_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_hid, HASHBUSTER_USB_PRODUCT);
}

static
int hashbuster_chip_count(hid_device *h)
{
	/* Do not allocate spi_port on the stack! OS X, at least, has a 512 KB default stack size for secondary threads */
	struct spi_port *spi = malloc(sizeof(*spi));
	spi->txrx = hashbuster_spi_txrx;
	spi->userp = h;
	spi->repr = hashbuster_drv.dname;
	spi->logprio = LOG_DEBUG;
	spi->speed = 100000;
	spi->mode = 0;
	
	const int chip_count = libbitfury_detectChips1(spi);
	
	free(spi);
	
	return chip_count;
}

static
bool hashbuster_lowl_probe(const struct lowlevel_device_info * const info)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	char * const path = info->path;
	hid_device *h;
	uint8_t buf[0x40] = {'\xfe'};
	
	if (info->lowl != &lowl_hid)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		applogr(false, LOG_DEBUG, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not hid!",
		       __func__, product, serial);
	}
	
	if (info->vid != 0xFA04 || info->pid != 0x0011)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		applogr(false, LOG_DEBUG, "%s: Wrong VID/PID", __func__);
	}
	
	h = hid_open_path(path);
	if (!h)
		applogr(false, LOG_WARNING, "%s: Failed to open HID path %s",
		       __func__, path);
	
	if ((!hashbuster_io(h, buf, buf)) || buf[1] != 0x07)
	{
		hid_close(h);
		applogr(false, LOG_DEBUG, "%s: Identify sequence didn't match on %s",
		        __func__, path);
	}
		
	const int chip_n = hashbuster_chip_count(h);
	
	hid_close(h);
	
	if (lowlevel_claim(&hashbuster_drv, true, info))
		return false;
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &hashbuster_drv,
		.set_device_funcs = bitfury_set_device_funcs,
		.device_data = lowlevel_ref(info),
		.threads = 1,
		.procs = chip_n,
		.device_path = strdup(info->path),
		.dev_manufacturer = maybe_strdup(info->manufacturer),
		.dev_product = maybe_strdup(product),
		.dev_serial = maybe_strdup(serial),
		.deven = DEV_ENABLED,
	};

	return add_cgpu(cgpu);
}

static
bool hashbuster_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct bitfury_device *bitfury;
	struct spi_port *port;
	hid_device *h;
	
	h = hid_open_path(cgpu->device_path);
	lowlevel_devinfo_free(cgpu->device_data);
	
	if (!h)
	{
		hid_close(h);
		applogr(false, LOG_ERR, "%s: Failed to open hid device", cgpu->dev_repr);
	}
	
	port = malloc(sizeof(*port));
	if (!port)
		applogr(false, LOG_ERR, "%s: Failed to allocate spi_port", cgpu->dev_repr);
	
	/* Be careful, read lowl-spi.h comments for warnings */
	memset(port, 0, sizeof(*port));
	port->txrx = hashbuster_spi_txrx;
	port->userp = h;
	port->cgpu = cgpu;
	port->repr = cgpu->dev_repr;
	port->logprio = LOG_ERR;
	port->speed = 100000;
	port->mode = 0;
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		bitfury = malloc(sizeof(*bitfury));
		
		if (!bitfury)
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to allocate bitfury_device",
			       cgpu->proc_repr);
			proc->status = LIFE_DEAD2;
			continue;
		}
		
		*bitfury = (struct bitfury_device){
			.spi = port,
		};
		proc->device_data = bitfury;
		bitfury_init_chip(proc);
		bitfury->osc6_bits = 53;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_freq_stat(&bitfury->chip_stat, 52, 56);
	}
	
	timer_set_now(&thr->tv_poll);
	cgpu->status = LIFE_INIT2;
	return true;
}

static
bool hashbuster_get_stats(struct cgpu_info * const cgpu)
{
	struct cgpu_info *proc;
	if (cgpu != cgpu->device)
		return true;
	
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct spi_port * const spi = bitfury->spi;
	hid_device * const h = spi->userp;
	uint8_t buf[0x40] = {'\x04'};
	if (!hashbuster_io(h, buf, buf))
		return false;
	if (buf[1])
	{
		for (proc = cgpu; proc; proc = proc->next_proc)
			proc->temp = buf[1];
	}
	return true;
}

struct device_drv hashbuster_drv = {
	.dname = "hashbuster",
	.name = "HBR",
	.lowl_match = hashbuster_lowl_match,
	.lowl_probe = hashbuster_lowl_probe,
	
	.thread_init = hashbuster_init,
	.thread_disable = bitfury_disable,
	.thread_enable = bitfury_enable,
	.thread_shutdown = bitfury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.get_stats = hashbuster_get_stats,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
