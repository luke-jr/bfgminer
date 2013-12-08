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
#include <string.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "libbitfury.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-usb.h"
#include "miner.h"

#define HASHBUSTER_USB_PRODUCT "HashBuster"

#define HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER 61

BFG_REGISTER_DRIVER(hashbusterusb_drv)

static
bool hashbusterusb_io(struct lowl_usb_endpoint * const h, unsigned char *buf, unsigned char *cmd)
{
	char x[0x81];
	
	bool rv = true;
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, cmd, 0x40);
		applog(LOG_DEBUG, "%s(%p): SEND: %s", __func__, h, x);
	}
	
	do // Workaround for PIC USB buffer corruption. We should repeat last packet if receive FF
	{
		do
		{
			usb_write(h, cmd, 64);
		} while (usb_read(h, buf, 64) != 64);
	} while(buf[0]==0xFF);
	
	if (unlikely(opt_dev_protocol))
	{
		bin2hex(x, buf, 0x40);
		applog(LOG_DEBUG, "%s(%p): RECV: %s", __func__, h, x);
	}
	return rv;
}

static
bool hashbusterusb_spi_config(struct lowl_usb_endpoint * const h, const uint8_t mode, const uint8_t miso, const uint32_t freq)
{
	uint8_t buf[0x40] = {'\x01', '\x01'};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbusterusb_spi_disable(struct lowl_usb_endpoint * const h)
{
	uint8_t buf[0x40] = {'\x01', '\x00'};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbusterusb_spi_reset(struct lowl_usb_endpoint * const h, uint8_t chips)
{
	uint8_t buf[0x40] = {'\x02', '\x00', chips};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	return (buf[1] == '\x00');
}

static
bool hashbusterusb_spi_transfer(struct lowl_usb_endpoint * const h, void * const buf, const void * const data, size_t datasz)
{
	if (datasz > HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
		return false;
	uint8_t cbuf[0x40] = {'\x03', '\x00', datasz};
	memcpy(&cbuf[3], data, datasz);
	if (!hashbusterusb_io(h, cbuf, cbuf))
		return false;
	if (cbuf[2] != datasz)
		return false;
	memcpy(buf, &cbuf[3], datasz);
	return true;
}

static
bool hashbusterusb_spi_txrx(struct spi_port * const port)
{
	struct lowl_usb_endpoint * const h = port->userp;
	const uint8_t *wrbuf = spi_gettxbuf(port);
	uint8_t *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	
	hashbusterusb_spi_disable(h);
	hashbusterusb_spi_reset(h, 0x10);
	
	hashbusterusb_spi_config(h, port->mode, 0, port->speed);
	
	while (bufsz >= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER)
	{
		if (!hashbusterusb_spi_transfer(h, rdbuf, wrbuf, HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER))
			return false;
		rdbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		wrbuf += HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
		bufsz -= HASHBUSTER_MAX_BYTES_PER_SPI_TRANSFER;
	}
	
	if (bufsz > 0)
	{
		if (!hashbusterusb_spi_transfer(h, rdbuf, wrbuf, bufsz))
			return false;
	}
	
	return true;
}

static
bool hashbusterusb_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_id(info, &lowl_usb, 0xFA04, 0x000D);
}

static
bool hashbusterusb_lowl_probe(const struct lowlevel_device_info * const info)
{
	struct cgpu_info *cgpu = NULL;
	struct bitfury_device **devicelist, *bitfury;
	struct spi_port *port;
	int j;
	struct cgpu_info dummy_cgpu;
	const char * const product = info->product;
	char *serial = info->serial;
	libusb_device_handle *h;
	
	if (info->lowl != &lowl_usb)
		applogr(false, LOG_DEBUG, "%s: Matched \"%s\" %s, but lowlevel driver is not usb_generic!",
		       __func__, product, info->devid);
	
	if (info->vid != 0xFA04 || info->pid != 0x000D)
		applogr(false, LOG_DEBUG, "%s: Wrong VID/PID", __func__);
	
	libusb_device *dev = info->lowl_data;
	if ( (j = libusb_open(dev, &h)) )
		applogr(false, LOG_ERR, "%s: Failed to open %s: %s",
		        __func__, info->devid, bfg_strerror(j, BST_LIBUSB));
	if ( (j = libusb_set_configuration(h, 1)) )
		applogr(false, LOG_ERR, "%s: Failed to set configuration 1 on %s: %s",
		        __func__, info->devid, bfg_strerror(j, BST_LIBUSB));
	if ( (j = libusb_claim_interface(h, 0)) )
		applogr(false, LOG_ERR, "%s: Failed to claim interface 0 on %s: %s",
		        __func__, info->devid, bfg_strerror(j, BST_LIBUSB));
	struct lowl_usb_endpoint * const ep = usb_open_ep_pair(h, 0x81, 64, 0x01, 64);
	usb_ep_set_timeouts_ms(ep, 100, 0);
	
	unsigned char OUTPacket[64];
	unsigned char INPacket[64];
	OUTPacket[0] = 0xFE;
	hashbusterusb_io(ep, INPacket, OUTPacket);
	if (INPacket[1] == 0x18)
	{
		// Turn on miner PSU
		OUTPacket[0] = 0x10;
		OUTPacket[1] = 0x00;
		OUTPacket[2] = 0x01;
		hashbusterusb_io(ep, INPacket, OUTPacket);
	}
	
	OUTPacket[0] = '\x20';
	hashbusterusb_io(ep, INPacket, OUTPacket);
	if (!memcmp(INPacket, "\x20\0", 2))
	{
		// 64-bit BE serial number
		uint64_t sernum = 0;
		for (j = 0; j < 8; ++j)
			sernum |= (uint64_t)INPacket[j + 2] << (j * 8);
		serial = malloc((8 * 2) + 1);
		sprintf(serial, "%08"PRIX64, sernum);
	}
	else
		serial = maybe_strdup(info->serial);
	
	int chip_n;
	
	port = malloc(sizeof(*port));
	port->cgpu = &dummy_cgpu;
	port->txrx = hashbusterusb_spi_txrx;
	port->userp = ep;
	port->repr = hashbusterusb_drv.dname;
	port->logprio = LOG_DEBUG;
	port->speed = 100000;
	port->mode = 0;
	
	chip_n = libbitfury_detectChips1(port);

	if (unlikely(!chip_n))
		chip_n = libbitfury_detectChips1(port);

	if (unlikely(!chip_n))
	{
		applog(LOG_WARNING, "%s: No chips found on %s (serial \"%s\")",
		       __func__, info->devid, serial);
fail:
		usb_close_ep(ep);
		free(port);
		free(serial);
		libusb_release_interface(h, 0);
		libusb_close(h);
		return false;
	}
	
	if (bfg_claim_libusb(&hashbusterusb_drv, true, dev))
		goto fail;
	
	{
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
			.drv = &hashbusterusb_drv,
			.procs = chip_n,
			.device_data = devicelist,
			.cutofftemp = 200,
			.threads = 1,
			.device_path = strdup(info->devid),
			.dev_manufacturer = maybe_strdup(info->manufacturer),
			.dev_product = maybe_strdup(product),
			.dev_serial = serial,
			.deven = DEV_ENABLED,
		};
	}
	
	return add_cgpu(cgpu);
}

static
bool hashbusterusb_init(struct thr_info * const thr)
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
bool hashbusterusb_get_stats(struct cgpu_info * const cgpu)
{
	struct cgpu_info *proc;
	if (cgpu != cgpu->device)
		return true;
	
	struct bitfury_device * const bitfury = cgpu->device_data;
	struct spi_port * const spi = bitfury->spi;
	struct lowl_usb_endpoint * const h = spi->userp;
	uint8_t buf[0x40] = {'\x04'};
	if (!hashbusterusb_io(h, buf, buf))
		return false;
	if (buf[1])
	{
		for (proc = cgpu; proc; proc = proc->next_proc)
			proc->temp = buf[1];
	}
	return true;
}

struct device_drv hashbusterusb_drv = {
	.dname = "hashbusterusb",
	.name = "HBR",
	.lowl_match = hashbusterusb_lowl_match,
	.lowl_probe = hashbusterusb_lowl_probe,
	
	.thread_init = hashbusterusb_init,
	.thread_disable = bitfury_disable,
	.thread_enable = bitfury_enable,
	.thread_shutdown = bitfury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = bitfury_do_io,
	.job_process_results = bitfury_job_process_results,
	
	.get_stats = hashbusterusb_get_stats,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	.set_device = bitfury_set_device,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
