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
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "deviceapi.h"
#include "driver-bitfury.h"
#include "libbitfury.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-spi.h"
#include "lowl-vcom.h"
#include "miner.h"
#include "util.h"


enum littlefury_opcode {
	LFOP_VERSION = 0,
	LFOP_SPI     = 1,
	LFOP_REGVOLT = 2,
	LFOP_REGINFO = 3,
	LFOP_REGPWR  = 4,
	LFOP_TEMP    = 5,
	LFOP_LED     = 6,
	LFOP_ADC     = 7,
};

BFG_REGISTER_DRIVER(littlefury_drv)

static uint16_t crc16tab[] = {
	0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
	0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
	0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
	0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
	0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
	0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
	0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
	0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
	0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
	0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
	0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
	0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
	0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
	0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
	0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
	0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
	0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
	0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
	0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
	0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
	0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
	0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
	0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
	0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
	0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
	0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
	0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
	0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
	0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
	0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
	0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
	0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0,
};

static
uint16_t crc16_floating(uint16_t next_byte, uint16_t seed)
{
	return ((seed << 8) ^ crc16tab[(seed >> 8) ^ next_byte]) & 0xFFFF;
}

static
uint16_t crc16(void *p, size_t sz)
{
	const uint8_t * const s = p;
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < sz; ++i)
		crc = crc16_floating(s[i], crc);
	return crc;
}

static
ssize_t keep_reading(int prio, int fd, void *buf, size_t count)
{
	ssize_t r, rv = 0;
	
	while (count)
	{
		r = read(fd, buf, count);
		if (unlikely(r <= 0))
		{
			applog(prio, "Read of fd %d returned %d", fd, (int)r);
			return rv ?: r;
		}
		rv += r;
		count -= r;
		buf += r;
	}
	
	return rv;
}

static
bool bitfury_do_packet(int prio, const char *repr, const int fd, void * const buf, uint16_t * const bufsz, const uint8_t op, const void * const payload, const uint16_t payloadsz)
{
	uint16_t crc;
	size_t sz;
	ssize_t r;
	uint8_t pkt[0x407];
	bool b;
	
	{
		sz = 2 + 1 + 2 + payloadsz + 2;
		pkt[0] = 0xab;
		pkt[1] = 0xcd;
		pkt[2] = op;
		pkt[3] = payloadsz >> 8;
		pkt[4] = payloadsz & 0xff;
		if (payloadsz)
			memcpy(&pkt[5], payload, payloadsz);
		crc = crc16(&pkt[2], 3 + (size_t)payloadsz);
		pkt[sz - 2] = crc >> 8;
		pkt[sz - 1] = crc & 0xff;
		if (unlikely(opt_dev_protocol))
		{
			char hex[(sz * 2) + 1];
			bin2hex(hex, pkt, sz);
			applog(LOG_DEBUG, "%s: DEVPROTO: SEND %s", repr, hex);
		}
		r = write(fd, pkt, sz);
		if (sz != r)
		{
			applog(prio, "%s: Failed to write packet (%d bytes succeeded)", repr, (int)r);
			return false;
		}
	}
	
	{
		r = keep_reading(prio, fd, pkt, 5);
		if (5 != r || pkt[0] != 0xab || pkt[1] != 0xcd || pkt[2] != op)
		{
			char hex[(r * 2) + 1];
			bin2hex(hex, pkt, r);
			applog(prio, "%s: DEVPROTO: RECV %s", repr, hex);
			applog(prio, "%s: Failed to read correct packet header", repr);
			return false;
		}
		sz = (((unsigned)pkt[3] << 8) | pkt[4]) + 2;
		r = keep_reading(prio, fd, &pkt[5], sz);
		if (sz != r)
		{
			r += 5;
			char hex[(r * 2) + 1];
			bin2hex(hex, pkt, r);
			applog(prio, "%s: DEVPROTO: RECV %s", repr, hex);
			applog(prio, "%s: Failed to read packet payload (len=%d)", repr, (int)sz);
			return false;
		}
		crc = (pkt[sz + 3] << 8) | pkt[sz + 4];
		b = (crc != crc16(&pkt[2], sz + 1));
		if (unlikely(opt_dev_protocol || b))
		{
			char hex[((sz + 5) * 2) + 1];
			bin2hex(hex, pkt, sz + 5);
			applog(b ? prio : LOG_DEBUG, "%s: DEVPROTO: RECV %s", repr, hex);
			if (b)
			{
				applog(prio, "%s: Packet checksum mismatch", repr);
				return false;
			}
		}
		sz -= 2;
		memcpy(buf, &pkt[5], (*bufsz < sz ? *bufsz : sz));
		*bufsz = sz;
	}
	
	return true;
}

static
bool littlefury_set_power(const int loglev, const char * const repr, const int fd, const bool power)
{
	const uint8_t pflg = (power ? '\1' : '\0');
	uint8_t buf[1] = { pflg };
	uint16_t bufsz = 1;
	return bitfury_do_packet(loglev, repr, fd, buf, &bufsz, LFOP_REGPWR, buf, 1) && bufsz && (buf[0] == pflg);
}

static
bool littlefury_txrx(struct spi_port *port)
{
	const struct cgpu_info * const cgpu = port->cgpu;
	const void *wrbuf = spi_gettxbuf(port);
	void *rdbuf = spi_getrxbuf(port);
	size_t bufsz = spi_getbufsz(port);
	uint16_t rbufsz, xfer;
	const int logprio = port->logprio;
	const char * const repr = port->repr;
	const int fd = cgpu->device->device_fd;
	
	if (unlikely(fd == -1))
		return false;
	
	rbufsz = 1;
	if (!bitfury_do_packet(logprio, repr, fd, rdbuf, &rbufsz, LFOP_SPI, NULL, 0))
	{
		littlefury_set_power(LOG_DEBUG, cgpu->dev_repr, fd, false);
		serial_close(fd);
		cgpu->device->device_fd = -1;
		return false;
	}
	
	while (bufsz)
	{
		xfer = (bufsz > 1024) ? 1024 : bufsz;
		rbufsz = xfer;
		if (!bitfury_do_packet(logprio, repr, fd, rdbuf, &rbufsz, LFOP_SPI, wrbuf, xfer))
			return false;
		if (rbufsz < xfer)
		{
			applog(port->logprio, "%s: SPI: Got fewer bytes back than sent (%d < %d)",
			       repr, rbufsz, xfer);
			return false;
		}
		bufsz -= xfer;
		rdbuf += xfer;
		wrbuf += xfer;
	}
	
	return true;
}

static
bool littlefury_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_product(info, "LittleFury");
}

static
int littlefury_chip_count(struct cgpu_info * const info)
{
	/* Do not allocate spi_port on the stack! OS X, at least, has a 512 KB default stack size for secondary threads */
	struct spi_port *spi = malloc(sizeof(*spi));
	spi->txrx = littlefury_txrx;
	spi->cgpu = info;
	spi->repr = littlefury_drv.dname;
	spi->logprio = LOG_DEBUG;
	
	const int chip_count = libbitfury_detectChips1(spi);
	
	free(spi);
	
	return chip_count;
}

static
bool littlefury_detect_one(const char *devpath)
{
	int fd, chips;
	uint8_t buf[255];
	uint16_t bufsz;
	struct cgpu_info dummy;
	char *devname = NULL;
	
	fd = serial_open(devpath, 0, 10, true);
	applog(LOG_DEBUG, "%s: %s %s",
	       littlefury_drv.dname,
	       ((fd == -1) ? "Failed to open" : "Successfully opened"),
	       devpath);
	
	if (unlikely(fd == -1))
		goto err;
	
	bufsz = sizeof(buf);
	if (!bitfury_do_packet(LOG_DEBUG, littlefury_drv.dname, fd, buf, &bufsz, LFOP_VERSION, NULL, 0))
		goto err;
	
	if (bufsz < 4)
	{
		applog(LOG_DEBUG, "%s: Incomplete version response", littlefury_drv.dname);
		goto err;
	}
	
	devname = malloc(bufsz - 3);
	memcpy(devname, (char*)&buf[4], bufsz - 4);
	devname[bufsz - 4] = '\0';
	applog(LOG_DEBUG, "%s: Identified %s %d.%d.%d (features %02x)",
	       littlefury_drv.dname, devname, buf[0], buf[1], buf[2], buf[3]);
	
	if (!littlefury_set_power(LOG_DEBUG, littlefury_drv.dname, fd, true))
		applog(LOG_WARNING, "%s: Unable to power on chip(s) for %s",
		       littlefury_drv.dname, devpath);
	
	dummy.device = &dummy;
	dummy.device_fd = fd;
	
	chips = littlefury_chip_count(&dummy);
	
	if (!chips) {
		applog(LOG_WARNING, "%s: No Bitfury chips detected on %s",
		       littlefury_drv.dname, devpath);
		goto err;
	} else {
		applog(LOG_DEBUG, "%s: %d chips detected",
		       littlefury_drv.dname, chips);
	}
	
	littlefury_set_power(LOG_DEBUG, littlefury_drv.dname, fd, false);
	
	if (serial_claim_v(devpath, &littlefury_drv))
		goto err;
	
	serial_close(fd);
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &littlefury_drv,
		.set_device_funcs = bitfury_set_device_funcs,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
		.name = devname,
		.cutofftemp = 85,
	};
	// NOTE: Xcode's clang has a bug where it cannot find fields inside anonymous unions (more details in fpgautils)
	cgpu->device_fd = -1;
	
	return add_cgpu(cgpu);

err:
	if (fd != -1)
		serial_close(fd);
	free(devname);
	return false;
}

static
bool littlefury_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, littlefury_detect_one);
}

static
bool littlefury_thread_init(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct cgpu_info *proc;
	struct spi_port *spi;
	struct bitfury_device *bitfury;
	int i = 0;
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		spi = malloc(sizeof(*spi));
		
		/* Be careful, read lowl-spi.h comments for warnings */
		memset(spi, 0, sizeof(*spi));
		spi->txrx = littlefury_txrx;
		spi->cgpu = proc;
		spi->repr = proc->proc_repr;
		spi->logprio = LOG_ERR;
		
		bitfury = malloc(sizeof(*bitfury));
		*bitfury = (struct bitfury_device){
			.spi = spi,
			.fasync = i++,
		};
		
		proc->device_data = bitfury;
		
		bitfury->osc6_bits = 50;
	}
	
	timer_set_now(&thr->tv_poll);
	cgpu->status = LIFE_INIT2;
	return true;
}

static
void littlefury_disable(struct thr_info * const thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	
	bitfury_disable(thr);
	
	// If all chips disabled, kill power and close device
	bool any_running = false;
	for (proc = dev; proc; proc = proc->next_proc)
		if (proc->deven == DEV_ENABLED && !proc->thr[0]->pause)
		{
			any_running = true;
			break;
		}
	if (!any_running)
	{
		if (!littlefury_set_power(LOG_ERR, dev->dev_repr, dev->device_fd, false))
			applog(LOG_WARNING, "%s: Unable to power off chip(s)", dev->dev_repr);
		serial_close(dev->device_fd);
		dev->device_fd = -1;
		timer_unset(&dev->thr[0]->tv_poll);
	}
}

static
void littlefury_enable(struct thr_info * const thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct thr_info * const master_thr = dev->thr[0];
	
	if (!timer_isset(&master_thr->tv_poll))
		timer_set_now(&master_thr->tv_poll);
}

static void littlefury_shutdown(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	const int fd = cgpu->device->device_fd;
	
	bitfury_shutdown(thr);
	if (!littlefury_set_power(LOG_ERR, cgpu->dev_repr, fd, false))
		applog(LOG_WARNING, "%s: Unable to power off chip(s)", cgpu->dev_repr);
}

static
void littlefury_common_error(struct cgpu_info * const dev, const enum dev_reason reason)
{
	for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
	{
		struct thr_info * const thr = proc->thr[0];
		dev_error(proc, reason);
		inc_hw_errors_only(thr);
	}
}

static
void littlefury_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu, *proc;
	int fd = dev->device_fd;
	
	if (unlikely(fd == -1))
	{
		uint8_t buf[1];
		uint16_t bufsz = 1;
		
		fd = serial_open(dev->device_path, 0, 10, true);
		if (unlikely(fd == -1))
		{
			applog(LOG_ERR, "%s: Failed to open %s",
			       dev->dev_repr, dev->device_path);
			littlefury_common_error(dev, REASON_THREAD_FAIL_INIT);
			return;
		}
		
		if (!(bitfury_do_packet(LOG_DEBUG, littlefury_drv.dname, fd, buf, &bufsz, LFOP_REGPWR, "\1", 1) && bufsz && buf[0]))
		{
			applog(LOG_ERR, "%s: Unable to power on chip(s)", dev->dev_repr);
			serial_close(fd);
			littlefury_common_error(dev, REASON_THREAD_FAIL_INIT);
			return;
		}
		
		dev->device_fd = fd;
		
		for (proc = dev; proc; proc = proc->next_proc)
		{
			if (proc->deven != DEV_ENABLED || proc->thr[0]->pause)
				continue;
			struct bitfury_device * const bitfury = proc->device_data;
			bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
			bitfury_init_chip(proc);
		}
	}
	
	return bitfury_do_io(master_thr);
}

static
void littlefury_reinit(struct cgpu_info * const proc)
{
	timer_set_now(&proc->thr[0]->tv_poll);
}

struct device_drv littlefury_drv = {
	.dname = "littlefury",
	.name = "LFY",
	.lowl_match = littlefury_lowl_match,
	.lowl_probe = littlefury_lowl_probe,
	
	.thread_init = littlefury_thread_init,
	.thread_disable = littlefury_disable,
	.thread_enable = littlefury_enable,
	.reinit_device = littlefury_reinit,
	.thread_shutdown = littlefury_shutdown,
	
	.minerloop = minerloop_async,
	.job_prepare = bitfury_job_prepare,
	.job_start = bitfury_noop_job_start,
	.poll = littlefury_poll,
	.job_process_results = bitfury_job_process_results,
	
	.get_api_extra_device_detail = bitfury_api_device_detail,
	.get_api_extra_device_status = bitfury_api_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitfury_wlogprint_status,
	.proc_tui_wlogprint_choices = bitfury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitfury_tui_handle_choice,
#endif
};
