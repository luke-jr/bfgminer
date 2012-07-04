/*
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"

#include "fpgautils.h"
#include "miner.h"


struct device_api bitforce_api;

#define BFopen(devpath)  serial_open(devpath, 0, -1, true)

static void BFgets(char *buf, size_t bufLen, int fd)
{
	do
		--bufLen;
	while (likely(bufLen && read(fd, buf, 1) && (buf++)[0] != '\n'))
		;
	buf[0] = '\0';
}

static ssize_t BFwrite2(int fd, const void *buf, ssize_t bufLen)
{
	return write(fd, buf, bufLen);
}

#define BFwrite(fd, buf, bufLen)  do {  \
	if ((bufLen) != BFwrite2(fd, buf, bufLen)) {  \
		applog(LOG_ERR, "Error writing to BitForce (" #buf ")");  \
		return 0;  \
	}  \
} while(0)

#define BFclose(fd) close(fd)

static bool bitforce_detect_one(const char *devpath)
{
	char *s;
	char pdevbuf[0x100];

	applog(LOG_DEBUG, "BitForce Detect: Attempting to open %s", devpath);

	int fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BitForce Detect: Failed to open %s", devpath);
		return false;
	}
	BFwrite(fdDev, "ZGX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZGX)");
		return 0;
	}
	BFclose(fdDev);
	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_DEBUG, "BitForce Detect: Didn't recognise BitForce on %s", devpath);
		return false;
	}

	// We have a real BitForce!
	struct cgpu_info *bitforce;
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->api = &bitforce_api;
	bitforce->device_path = strdup(devpath);
	bitforce->deven = DEV_ENABLED;
	bitforce->threads = 1;
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}

	return add_cgpu(bitforce);
}

static char bitforce_detect_auto()
{
	return
	serial_autodetect_udev     (bitforce_detect_one, "BitFORCE*SHA256") ?:
	serial_autodetect_devserial(bitforce_detect_one, "BitFORCE_SHA256") ?:
	0;
}

static void bitforce_detect()
{
	serial_detect_auto(bitforce_api.dname, bitforce_detect_one, bitforce_detect_auto);
}

static void get_bitforce_statline_before(char *buf, struct cgpu_info *bitforce)
{
	float gt = bitforce->temp;
	if (gt > 0)
		tailsprintf(buf, "%5.1fC ", gt);
	else
		tailsprintf(buf, "       ", gt);
	tailsprintf(buf, "        | ");
}

static bool bitforce_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;

	struct timeval now;

	int fdDev = BFopen(bitforce->device_path);
	if (unlikely(-1 == fdDev)) {
		applog(LOG_ERR, "Failed to open BitForce on %s", bitforce->device_path);
		return false;
	}

	bitforce->device_fd = fdDev;

	applog(LOG_INFO, "Opened BitForce on %s", bitforce->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(bitforce->init, &now);

	return true;
}

static uint64_t bitforce_scanhash(struct thr_info *thr, struct work *work, uint64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;

	int i;
	char pdevbuf[0x100];
	unsigned char ob[61] = ">>>>>>>>12345678901234567890123456789012123456789012>>>>>>>>";
	struct timeval tdiff;
	char *pnoncebuf;
	char *s;
	uint32_t nonce;

	BFwrite(fdDev, "ZDX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZDX)");
		return 0;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce ZDX reports: %s", pdevbuf);
		return 0;
	}

	memcpy(ob + 8, work->midstate, 32);
	memcpy(ob + 8 + 32, work->data + 64, 12);
	BFwrite(fdDev, ob, 60);
	if (opt_debug) {
		s = bin2hex(ob + 8, 44);
		applog(LOG_DEBUG, "BitForce block data: %s", s);
		free(s);
	}

	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (block data)");
		return 0;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce block data reports: %s", pdevbuf);
		return 0;
	}

	BFwrite(fdDev, "ZLX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZKX)");
		return 0;
	}
	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = strtof(s + 1, NULL);
		if (temp > 0) {
			bitforce->temp = temp;
			if (temp > bitforce->cutofftemp) {
				applog(LOG_WARNING, "Hit thermal cutoff limit on %s %d, disabling!", bitforce->api->name, bitforce->device_id);
				bitforce->deven = DEV_RECOVER;

				bitforce->device_last_not_well = time(NULL);
				bitforce->device_not_well_reason = REASON_DEV_THERMAL_CUTOFF;
				bitforce->dev_thermal_cutoff_count++;
			}
		}
	}

	/* Start looking for results. Stupid polling every 10ms... */
	i = 0;
	while (42) {
		if (unlikely(thr->work_restart))
			return 0;
		usleep(10000);
		i += 10;

		BFwrite(fdDev, "ZFX", 3);
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
		if (unlikely(!pdevbuf[0])) {
			applog(LOG_ERR, "Error reading from BitForce (ZFX)");
			return 0;
		}
		if (pdevbuf[0] != 'B')
		    break;

		/* After 2/3 of the average cycle time (~3.4s), request more work */
		if (i == 3400)
			queue_request(thr, false);
	}
	applog(LOG_DEBUG, "BitForce waited %dms until %s\n", i, pdevbuf);
	work->blk.nonce = 0xffffffff;
	if (pdevbuf[2] == '-')
		return 0xffffffff;
	else if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
		applog(LOG_ERR, "BitForce result reports: %s", pdevbuf);
		return 0;
	}

	pnoncebuf = &pdevbuf[12];

	while (1) {
		hex2bin((void*)&nonce, pnoncebuf, 4);
#ifndef __BIG_ENDIAN__
		nonce = swab32(nonce);
#endif

		submit_nonce(thr, work, nonce);
		if (pnoncebuf[8] != ',')
			break;
		pnoncebuf += 9;
	}

	return 0xffffffff;
}

struct device_api bitforce_api = {
	.dname = "bitforce",
	.name = "BFL",
	.api_detect = bitforce_detect,
	.get_statline_before = get_bitforce_statline_before,
	.thread_prepare = bitforce_thread_prepare,
	.scanhash = bitforce_scanhash,
};
