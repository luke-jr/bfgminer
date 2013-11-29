/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 Vladimir Strinski
 * Copyright 2013 HashBuster team
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

#define HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER 61

BFG_REGISTER_DRIVER(hashbuster2_drv)

static
bool hashbuster2_io(libusb_device_handle * const h, unsigned char *buf, unsigned char *cmd)
{
	int result;
	char x[0x81];
	
	bool rv = true;
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, cmd, 0x40);
		applog(LOG_DEBUG, "%s(%p): SEND: %s", __func__, h, x);
	}
	
	do // Workaround for PIC USB buffer corruption. We should repeat last packet if receive FF
	{
		libusb_bulk_transfer(h, 0x01, cmd, 64, &result, 0);
		libusb_bulk_transfer(h, 0x81, buf, 64, &result, 0);
	} while(buf[0]==0xFF);
	
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, buf, 0x40);
		applog(LOG_DEBUG, "%s(%p): RECV: %s", __func__, h, x);
	}
	return rv;
}

static
bool hashbuster2_spi_config(libusb_device_handle * const h, const uint8_t mode, const uint8_t miso, const uint32_t freq)
{
	uint8_t buf[0x40] = {'\x01', '\x01'};
	if (!hashbuster2_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbuster2_spi_disable(libusb_device_handle * const h)
{
	uint8_t buf[0x40] = {'\x01', '\x00'};
	if (!hashbuster2_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbuster2_spi_reset(libusb_device_handle * const h, uint8_t chips)
{
	uint8_t buf[0x40] = {'\x02', '\x00', chips};
	if (!hashbuster2_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbuster2_spi_transfer(libusb_device_handle * const h, void * const buf, const void * const data, size_t datasz)
{
	if (datasz > HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
		return false;
	uint8_t cbuf[0x40] = {'\x03', '\x00', datasz};
	memcpy(&cbuf[3], data, datasz);
	if (!hashbuster2_io(h, cbuf, cbuf))
		return false;
	if (cbuf[2] != datasz)
		return false;
	memcpy(buf, &cbuf[3], datasz);
	return true;
}

static
bool hashbuster2_spi_txrx(struct spi_port * const port)
{
	libusb_device_handle * const h = port->userp;
	const uint8_t *wrbuf = spi_gettxbuf(port);
	uint8_t *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	
	hashbuster2_spi_disable(h);
	hashbuster2_spi_reset(h, 0x10);
	
	hashbuster2_spi_config(h, port->mode, 0, port->speed);
	
	while (bufsz >= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
	{
		if (!hashbuster2_spi_transfer(h, rdbuf, wrbuf, HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER))
			return false;
		rdbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		wrbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		bufsz -= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
	}
	
	if (bufsz > 0)
	{
		if (!hashbuster2_spi_transfer(h, rdbuf, wrbuf, bufsz))
			return false;
	}
	
	return true;
}

static
bool hashbuster2_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_id(info, &lowl_usb, 0xFA04, 0x000D);
}

static
bool hashbuster2_lowl_probe(const struct lowlevel_device_info * const info)
{
	struct cgpu_info *cgpu = NULL;
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int j;
	struct cgpu_info dummy_cgpu;
	const char * const product = info->product;
	const char * const serial = info->serial;
	libusb_device_handle *h;
	
	if (info->lowl != &lowl_usb)
		applogr(false, LOG_WARNING, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not usb_generic!",
		       __func__, product, serial);
	
	if (info->vid != 0xFA04 || info->pid != 0x000D)
		applogr(false, LOG_WARNING, "%s: Wrong VID/PID", __func__);
	
	libusb_init(NULL);
	libusb_set_debug(NULL,3);
	
	libusb_device *dev = info->lowl_data;
	libusb_open(dev, &h);
	if (libusb_kernel_driver_active(h, 0))
		libusb_detach_kernel_driver(h, 0);
	libusb_set_configuration(h, 1);
	libusb_claim_interface(h, 0);
	
	unsigned char OUTPacket[64];
	unsigned char INPacket[64];
	OUTPacket[0] = 0xFE;
	hashbuster2_io(h, INPacket, OUTPacket);
	if (INPacket[1] == 0x18)
	{
		// Turn on miner PSU
		OUTPacket[0] = 0x10;
		OUTPacket[1] = 0x00;
		OUTPacket[2] = 0x01;
		hashbuster2_io(h, INPacket, OUTPacket);
	}
	
	int chip_n;
	
	port = malloc(sizeof(*port));
	port->cgpu = &dummy_cgpu;
	port->txrx = hashbuster2_spi_txrx;
	port->userp=h;
	port->repr = hashbuster2_drv.dname;
	port->logprio = LOG_DEBUG;
	port->speed = 100000;
	port->mode = 0;
	
	chip_n = libbitfury_detectChips1(port);
	if (chip_n)
	{
		applog(LOG_WARNING, "BITFURY slot %d: %d chips detected", 0, chip_n);
		
		devicelist = malloc(sizeof(*devicelist) * chip_n);
		for (j = 0; j < chip_n; ++j)
		{
			devicelist[j] = bitfury = malloc(sizeof(*bitfury));
			*bitfury = (struct bitfury_device){
				.spi = port,
				.slot = 0,
				.fasync = j,
			};
		}
		
		cgpu = malloc(sizeof(*cgpu));
		*cgpu = (struct cgpu_info){
			.drv = &hashbuster2_drv,
			.procs = chip_n,
			.device_data = devicelist,
			.cutofftemp = 200,
			.threads = 1,
			.dev_manufacturer = maybe_strdup(info->manufacturer),
			.dev_product = maybe_strdup(product),
			.dev_serial = maybe_strdup(serial),
			.deven = DEV_ENABLED,
		};
	}
	
	return add_cgpu(cgpu);
}

static
bool hashbuster2_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	
	struct bitfury_device **devicelist;
	struct bitfury_device *bitfury;
	
	for (proc = thr->cgpu; proc; proc = proc->next_proc)
	{
		devicelist = proc->device_data;
		bitfury = devicelist[proc->proc_id];
		proc->device_data = bitfury;
		bitfury->spi->cgpu = proc;
		bitfury_init_chip(proc);
		bitfury->osc6_bits = 53;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_freq_stat(&bitfury->chip_stat, 52, 56);
		
		if (proc->proc_id == proc->procs - 1)
			free(devicelist);
	}
	
	timer_set_now(&thr->tv_poll);
	cgpu->status = LIFE_INIT2;
	return true;
}

static
bool hashbuster2_get_stats(struct cgpu_info * const cgpu)
{
	struct cgpu_info *proc;
	if (cgpu != cgpu->device)
		return true;
	
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct spi_port * const spi = bitfury->spi;
	libusb_device_handle * const h = spi->userp;
	uint8_t buf[0x40] = {'\x04'};
	if (!hashbuster2_io(h, buf, buf))
		return false;
	if (buf[1])
	{
		for (proc = cgpu; proc; proc = proc->next_proc)
			proc->temp = buf[1];
	}
	return true;
}

struct device_drv hashbuster2_drv = {
	.dname = "hashbuster2",
	.name = "HBR",
	.lowl_match = hashbuster2_lowl_match,
	.lowl_probe = hashbuster2_lowl_probe,
	
	.thread_init = hashbuster2_init,
	.thread_disable = bitfury_disable,
	.thread_enable = bitfury_enable,
	.thread_shutdown = bitfury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.get_stats = hashbuster2_get_stats,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	.set_device = bitfury_set_device,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
