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
#include "fpgautils.h"
#include "libbitfury.h"
#include "logging.h"
#include "miner.h"
#include "spidevc.h"
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

struct device_drv littlefury_drv;

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
ssize_t keep_reading(int fd, void *buf, size_t count)
{
	ssize_t r, rv = 0;
	
	while (count)
	{
		r = read(fd, buf, count);
		if (unlikely(r <= 0))
		{
			applog(LOG_ERR, "Read of fd %d returned %d", fd, (int)r);
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
		r = keep_reading(fd, pkt, 5);
		if (5 != r || pkt[0] != 0xab || pkt[1] != 0xcd || pkt[2] != op)
		{
			char hex[(r * 2) + 1];
			bin2hex(hex, pkt, r);
			applog(prio, "%s: DEVPROTO: RECV %s", repr, hex);
			applog(prio, "%s: Failed to read correct packet header", repr);
			return false;
		}
		sz = (((unsigned)pkt[3] << 8) | pkt[4]) + 2;
		r = keep_reading(fd, &pkt[5], sz);
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
	
	rbufsz = 1;
	if (!bitfury_do_packet(logprio, repr, fd, rdbuf, &rbufsz, LFOP_SPI, NULL, 0))
		return false;
	
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
bool littlefury_detect_one(const char *devpath)
{
	int fd, chips;
	uint8_t buf[255];
	uint16_t bufsz;
	struct spi_port spi;
	struct cgpu_info dummy;
	char *devname;
	
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
	
	bufsz = sizeof(buf);
	if (!(bitfury_do_packet(LOG_DEBUG, littlefury_drv.dname, fd, buf, &bufsz, LFOP_REGPWR, "\1", 1) && bufsz && buf[0]))
		applog(LOG_WARNING, "%s: Unable to power on chip(s) for %s",
		       littlefury_drv.dname, devpath);
	
	dummy.device = &dummy;
	dummy.device_fd = fd;
	spi = (struct spi_port){
		.txrx = littlefury_txrx,
		.cgpu = &dummy,
		.repr = littlefury_drv.dname,
		.logprio = LOG_DEBUG,
	};
	
	chips = libbitfury_detectChips1(&spi);
	if (!chips) {
		applog(LOG_WARNING, "%s: No Bitfury chips detected on %s",
		       littlefury_drv.dname, devpath);
		free(devname);
		goto err;
	} else {
		applog(LOG_DEBUG, "%s: %d chips detected",
		       littlefury_drv.dname, chips);
	}
	
	bufsz = sizeof(buf);
	bitfury_do_packet(LOG_DEBUG, littlefury_drv.dname, fd, buf, &bufsz, LFOP_REGPWR, "\0", 1);
	
	serial_close(fd);
	
	struct cgpu_info *cgpu;
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &littlefury_drv,
		.device_path = strdup(devpath),
		.device_fd = -1,
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
		.name = devname,
		.cutofftemp = 85,
	};
	
	return add_cgpu(cgpu);

err:
	return false;
}

static
int littlefury_detect_auto(void)
{
	return serial_autodetect(littlefury_detect_one, "LittleFury");
}

static
void littlefury_detect(void)
{
	serial_detect_auto(&littlefury_drv, littlefury_detect_one, littlefury_detect_auto);
}

static
bool littlefury_thread_init(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct cgpu_info *proc;
	struct spi_port *spi;
	struct bitfury_device *bitfury;
	uint8_t buf[1];
	uint16_t bufsz = 1;
	int fd;
	int i = 0;
	
	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		spi = malloc(sizeof(*spi));
		*spi = (struct spi_port){
			.txrx = littlefury_txrx,
			.cgpu = proc,
			.repr = proc->proc_repr,
			.logprio = LOG_ERR,
		};
		
		bitfury = malloc(sizeof(*bitfury));
		*bitfury = (struct bitfury_device){
			.spi = spi,
			.fasync = i++,
		};
		
		proc->device_data = bitfury;
	}
	
	fd = cgpu->device_fd = serial_open(cgpu->device_path, 0, 10, true);
	if (unlikely(fd == -1))
	{
		applog(LOG_DEBUG, "%s: %s %s",
		       cgpu->dev_repr, "Failed to open", cgpu->device_path);
		return true;
	}
	
	if (!(bitfury_do_packet(LOG_DEBUG, littlefury_drv.dname, fd, buf, &bufsz, LFOP_REGPWR, "\1", 1) && bufsz && buf[0]))
		applog(LOG_WARNING, "%s: Unable to power on chip(s)", cgpu->dev_repr);
	
	return true;
}

static
bool littlefury_do_io(struct thr_info *thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bitfury_device * const bitfury = proc->device_data;
	bitfury->results_n = 0;
	libbitfury_sendHashData1(proc->proc_id, bitfury, thr);
	if (bitfury->job_switched && thr->next_work)
	{
		mt_job_transition(thr);
		// TODO: Delay morework until right before it's needed
		timer_set_now(&thr->tv_morework);
		job_start_complete(thr);
	}
	if (thr->work && bitfury->results_n)
		for (int i = bitfury->results_n; i--; )
			submit_nonce(thr, thr->work, be32toh(bitfury->results[i]));
	timer_set_delay_from_now(&thr->tv_poll, 10000);
	return true;
}

static
void littlefury_poll(struct thr_info *thr)
{
	littlefury_do_io(thr);
}

static
bool littlefury_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct bitfury_device * const bitfury = thr->cgpu->device_data;
	work_to_payload(&bitfury->payload, thr->next_work);
	payload_to_atrvec(bitfury->atrvec, &bitfury->payload);
	return true;
}

static
void littlefury_job_start(struct thr_info *thr)
{
	littlefury_do_io(thr);
}

static
int64_t littlefury_job_process_results(struct thr_info *thr, struct work *work, bool stopping)
{
	if (unlikely(stopping))
		timer_unset(&thr->tv_poll);
	return 0x100000000;
}

static void littlefury_shutdown(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	const int fd = cgpu->device->device_fd;
	uint8_t buf[1];
	uint16_t bufsz = 1;
	
	bitfury_shutdown(thr);
	if (!(bitfury_do_packet(LOG_DEBUG, cgpu->dev_repr, fd, buf, &bufsz, LFOP_REGPWR, "\0", 1) && bufsz && !buf[0]))
		applog(LOG_WARNING, "%s: Unable to power off chip(s)", cgpu->dev_repr);
}

struct device_drv littlefury_drv = {
	.dname = "littlefury",
	.name = "LFY",
	.drv_detect = littlefury_detect,
	
	.minerloop = minerloop_async,
	.thread_init = littlefury_thread_init,
	.job_prepare = littlefury_job_prepare,
	.job_start = littlefury_job_start,
	.poll = littlefury_poll,
	.job_process_results = littlefury_job_process_results,
	
	.minerloop = hash_queued_work,
	.thread_init = littlefury_thread_init,
	.scanwork = bitfury_scanHash,
	.thread_shutdown = littlefury_shutdown,
};
