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
		crc = crc16ffff(&pkt[2], 3 + (size_t)payloadsz);
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
		b = (crc != crc16ffff(&pkt[2], sz + 1));
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

struct littlefury_state {
	int chips_enabled;
	bool powered;
};

static
bool littlefury_thread_init(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct cgpu_info *proc;
	struct spi_port *spi;
	struct bitfury_device *bitfury;
	struct littlefury_state * const lfstate = malloc(sizeof(*lfstate));
	int i = 0;
	
	*lfstate = (struct littlefury_state){
		.chips_enabled = 0,
	};
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		struct thr_info * const proc_thr = proc->thr[0];
		
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
		
		proc_thr->cgpu_data = lfstate;
		++lfstate->chips_enabled;
	}
	
	timer_set_now(&thr->tv_poll);
	cgpu->status = LIFE_INIT2;
	return true;
}

static void littlefury_common_error(struct cgpu_info *, enum dev_reason);

static
bool littlefury_power_on(struct cgpu_info * const dev)
{
	struct thr_info * const master_thr = dev->thr[0];
	struct littlefury_state * const lfstate = master_thr->cgpu_data;
	
	applog(LOG_DEBUG, "%s: Turning power on", dev->dev_repr);
	if (!littlefury_set_power(LOG_WARNING, dev->dev_repr, dev->device_fd, true))
	{
		applog(LOG_ERR, "%s: Unable to power on chip(s)", dev->dev_repr);
		littlefury_common_error(dev, REASON_THREAD_FAIL_INIT);
		serial_close(dev->device_fd);
		dev->device_fd = -1;
		lfstate->powered = false;
		return false;
	}
	
	lfstate->powered = true;
	return true;
}

static
void littlefury_chip_init(struct cgpu_info * const proc)
{
	struct bitfury_device * const bitfury = proc->device_data;
	bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
	bitfury_init_chip(proc);
}

static
void littlefury_disable(struct thr_info * const thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct littlefury_state * const lfstate = thr->cgpu_data;
	
	bitfury_disable(thr);
	
	// If all chips disabled, kill power and close device
	if (!--lfstate->chips_enabled)
	{
		applog(LOG_DEBUG, "%s: 0 chips enabled, turning off power", dev->dev_repr);
		lfstate->powered = false;
		if (!littlefury_set_power(LOG_ERR, dev->dev_repr, dev->device_fd, false))
			applog(LOG_WARNING, "%s: Unable to power off chip(s)", dev->dev_repr);
	}
	else
		applog(LOG_DEBUG, "%s: %d chips enabled, power remains on", dev->dev_repr, lfstate->chips_enabled);
}

static
void littlefury_enable(struct thr_info * const thr)
{
	struct cgpu_info *proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct thr_info * const master_thr = dev->thr[0];
	struct littlefury_state * const lfstate = thr->cgpu_data;
	
	++lfstate->chips_enabled;
	
	if (dev->device_fd != -1 && !lfstate->powered)
		littlefury_power_on(dev);
	
	if (dev->device_fd != -1)
	{
		struct bitfury_device * const bitfury = proc->device_data;
		bitfury_send_reinit(bitfury->spi, bitfury->slot, bitfury->fasync, bitfury->osc6_bits);
		bitfury_init_chip(proc);
	}
	
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
bool littlefury_get_stats(struct cgpu_info * const dev)
{
	if (dev != dev->device)
		return true;
	
	uint16_t bufsz = 2;
	uint8_t buf[bufsz];
	
	if (bitfury_do_packet(LOG_WARNING, dev->dev_repr, dev->device_fd, buf, &bufsz, LFOP_TEMP, NULL, 0) && bufsz == sizeof(buf))
	{
		float temp = upk_u16be(buf, 0);
		temp = (1.3979 * temp) - 295.23;
		for_each_managed_proc(proc, dev)
			proc->temp = temp;
	}
	
	return true;
}

static
void littlefury_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu, *proc;
	struct littlefury_state * const lfstate = master_thr->cgpu_data;
	int fd = dev->device_fd;
	
	if (unlikely(fd == -1))
	{
		fd = serial_open(dev->device_path, 0, 10, true);
		if (unlikely(fd == -1))
		{
			applog(LOG_ERR, "%s: Failed to open %s",
			       dev->dev_repr, dev->device_path);
			littlefury_common_error(dev, REASON_THREAD_FAIL_INIT);
			return;
		}
		
		dev->device_fd = fd;
		lfstate->powered = false;
	}
	
	if (unlikely((!lfstate->powered) && lfstate->chips_enabled > 0))
	{
		if (!littlefury_power_on(dev))
			return;
		
		for (proc = dev; proc; proc = proc->next_proc)
		{
			if (proc->deven != DEV_ENABLED || proc->thr[0]->pause)
				continue;
			littlefury_chip_init(proc);
		}
	}
	
	littlefury_get_stats(dev);
	
	bitfury_do_io(master_thr);
	
	if (!timer_isset(&master_thr->tv_poll))
		// We want to keep polling temperature
		timer_set_delay_from_now(&master_thr->tv_poll, 1000000);
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
