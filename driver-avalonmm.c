/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utlist.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "util.h"

#define AVALONMM_MAX_MODULES  4

BFG_REGISTER_DRIVER(avalonmm_drv)

#define AVALONMM_PKT_DATA_SIZE  0x20
#define AVALONMM_PKT_SIZE  (AVALONMM_PKT_DATA_SIZE + 7)

enum avalonmm_cmd {
	AMC_DETECT     = 0x0a,
};

enum avalonmm_reply {
	AMR_DETECT_ACK = 0x19,
};

static
bool avalonmm_write_cmd(const int fd, const enum avalonmm_cmd cmd, const void *data, size_t datasz)
{
	uint8_t packets = ((datasz + AVALONMM_PKT_DATA_SIZE - 1) / AVALONMM_PKT_DATA_SIZE) ?: 1;
	uint8_t pkt[AVALONMM_PKT_SIZE] = {'A', 'V', cmd, 1, packets};
	uint16_t crc;
	ssize_t r;
	while (true)
	{
		size_t copysz = AVALONMM_PKT_DATA_SIZE;
		if (datasz < copysz)
		{
			copysz = datasz;
			memset(&pkt[5 + copysz], '\0', AVALONMM_PKT_DATA_SIZE - copysz);
		}
		if (copysz)
			memcpy(&pkt[5], data, copysz);
		crc = crc16xmodem(&pkt[5], AVALONMM_PKT_DATA_SIZE);
		pk_u16be(pkt, 5 + AVALONMM_PKT_DATA_SIZE, crc);
		r = write(fd, pkt, sizeof(pkt));
		if (opt_dev_protocol)
		{
			char hex[(sizeof(pkt) * 2) + 1];
			bin2hex(hex, pkt, sizeof(pkt));
			applog(LOG_DEBUG, "DEVPROTO fd=%d SEND: %s => %d", fd, hex, (int)r);
		}
		if (sizeof(pkt) != r)
			return false;
		datasz -= copysz;
		if (!datasz)
			break;
		data += copysz;
		++pkt[3];
	}
	return true;
}

static
ssize_t avalonmm_read(const int fd, const int logprio, enum avalonmm_reply *out_reply, void * const bufp, size_t bufsz)
{
	uint8_t *buf = bufp;
	uint8_t pkt[AVALONMM_PKT_SIZE];
	uint8_t packets = 0, got = 0;
	uint16_t good_crc, actual_crc;
	ssize_t r;
	while (true)
	{
		r = serial_read(fd, pkt, sizeof(pkt));
		if (opt_dev_protocol)
		{
			if (r >= 0)
			{
				char hex[(r * 2) + 1];
				bin2hex(hex, pkt, r);
				applog(LOG_DEBUG, "DEVPROTO fd=%d RECV: %s", fd, hex);
			}
			else
				applog(LOG_DEBUG, "DEVPROTO fd=%d RECV (%d)", fd, (int)r);
		}
		if (r != sizeof(pkt))
			applogr(-1, logprio, "%s: read failed", __func__);
		if (memcmp(pkt, "AV", 2))
			applogr(-1, logprio, "%s: bad header", __func__);
		good_crc = crc16xmodem(&pkt[5], AVALONMM_PKT_DATA_SIZE);
		actual_crc = upk_u16le(pkt, 5 + AVALONMM_PKT_DATA_SIZE);
		if (good_crc != actual_crc)
			applogr(-1, logprio, "%s: bad CRC (good=%04x actual=%04x)", __func__, good_crc, actual_crc);
		*out_reply = pkt[2];
		if (!got)
		{
			if (pkt[3] != 1)
				applogr(-1, logprio, "%s: first packet is not index 1", __func__);
			++got;
			packets = pkt[4];
		}
		else
		{
			if (pkt[3] != ++got)
				applogr(-1, logprio, "%s: packet %d is not index %d", __func__, got, got);
			if (pkt[4] != packets)
				applogr(-1, logprio, "%s: packet %d total packet count is %d rather than original value of %d", __func__, got, pkt[4], packets);
		}
		if (bufsz)
		{
			if (likely(bufsz > AVALONMM_PKT_DATA_SIZE))
			{
				memcpy(buf, &pkt[5], AVALONMM_PKT_DATA_SIZE);
				bufsz -= AVALONMM_PKT_DATA_SIZE;
				buf += AVALONMM_PKT_DATA_SIZE;
			}
			else
			{
				memcpy(buf, &pkt[5], bufsz);
				bufsz = 0;
			}
		}
		if (got == packets)
			break;
	}
	return (((ssize_t)got) * AVALONMM_PKT_DATA_SIZE);
}

static
bool avalonmm_detect_one(const char * const devpath)
{
	uint8_t buf[AVALONMM_PKT_DATA_SIZE] = {0};
	enum avalonmm_reply reply;
	int total_modules = 0;
	const int fd = serial_open(devpath, 0, 1, true);
	if (fd == -1)
		applogr(false, LOG_DEBUG, "%s: Failed to open %s", __func__, devpath);
	
	for (int i = 0; i < AVALONMM_MAX_MODULES; ++i)
	{
		pk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4, i);
		avalonmm_write_cmd(fd, AMC_DETECT, buf, AVALONMM_PKT_DATA_SIZE);
	}
	
	while (avalonmm_read(fd, LOG_DEBUG, &reply, NULL, 0) > 0)
	{
		if (reply != AMR_DETECT_ACK)
			continue;
		int i = upk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4);
		applog(LOG_DEBUG, "%s: Confirmed module %d", __func__, i);
		++total_modules;
	}
	
	serial_close(fd);
	
	if (!total_modules)
		return false;
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &avalonmm_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.procs = total_modules,
		.threads = 1,
	};
	
	return add_cgpu(cgpu);
}

static
bool avalonmm_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, avalonmm_detect_one);
}

struct device_drv avalonmm_drv = {
	.dname = "avalonmm",
	.name = "AVM",
	
	.lowl_probe = avalonmm_lowl_probe,
};
