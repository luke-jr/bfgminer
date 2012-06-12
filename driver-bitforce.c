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

#define BITFORCE_SLEEP_US 4500000
#define BITFORCE_SLEEP_MS (BITFORCE_SLEEP_US/1000)
#define BITFORCE_TIMEOUT_MS 30000

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
		applog(LOG_ERR, "BitForce Detect: Didn't recognise BitForce on %s", devpath);
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
	
	mutex_init(&bitforce->dev_lock);

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
	serial_detect_auto("bitforce", bitforce_detect_one, bitforce_detect_auto);
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

static bool bitforce_init(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char *devpath = bitforce->device_path;
	char pdevbuf[0x100];
	char *s;

	applog(LOG_DEBUG, "BFL%i: Re-initalizing", bitforce->device_id);

	BFclose(fdDev);

	fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BitForce init: Failed to open %s", devpath);
		return false;
	}

	bitforce->device_fd = fdDev;

	mutex_lock(&bitforce->dev_lock);
	BFwrite(fdDev, "ZGX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->dev_lock);
	
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZGX)");
		return false;
	}

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_ERR, "BitForce init: Didn't recognise BitForce on %s", devpath);
		return false;
	}
	
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}
	
	return true;
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	char *s;

	mutex_lock(&bitforce->dev_lock);
	BFwrite(fdDev, "ZLX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->dev_lock);
	
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading temp from BitForce (ZLX)");
		return false;
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
	return true;
}

static bool bitforce_send_work(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	unsigned char ob[61] = ">>>>>>>>12345678901234567890123456789012123456789012>>>>>>>>";
	char *s;

	mutex_lock(&bitforce->dev_lock);
	BFwrite(fdDev, "ZDX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZDX)");
		mutex_unlock(&bitforce->dev_lock);
		return false;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce ZDX reports: %s", pdevbuf);
		mutex_unlock(&bitforce->dev_lock);
		return false;
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
	mutex_unlock(&bitforce->dev_lock);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (block data)");
		return false;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce block data reports: %s", pdevbuf);
		return false;
	}
	return true;
}

static uint64_t bitforce_get_result(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;

	char pdevbuf[0x100];
	char *pnoncebuf;
	uint32_t nonce;
	int i;

	i = BITFORCE_SLEEP_MS;
	while (i < BITFORCE_TIMEOUT_MS) {
    	mutex_lock(&bitforce->dev_lock);
		BFwrite(fdDev, "ZFX", 3);
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	    mutex_unlock(&bitforce->dev_lock);
		if (unlikely(!pdevbuf[0])) {
			applog(LOG_ERR, "Error reading from BitForce (ZFX)");
	    	mutex_unlock(&bitforce->dev_lock);
			return 0;
		}
		if (pdevbuf[0] != 'B')
			break;
		usleep(10000);
		i += 10;
	}

	if (i >= BITFORCE_TIMEOUT_MS) {
		applog(LOG_ERR, "BitForce took longer than 30s");
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_THREAD_ZERO_HASH;
		bitforce->thread_zero_hash_count++;
		return 1;
	}

	applog(LOG_DEBUG, "BitForce waited %dms until %s\n", i, pdevbuf);
	work->blk.nonce = 0xffffffff;
	if (pdevbuf[2] == '-') 
		return 0xffffffff;   /* No valid nonce found */
	else if (pdevbuf[0] == 'I') 
		return 1;          /* Device idle */
	else if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
		applog(LOG_WARNING, "BitForce result reports: %s", pdevbuf);
		return 1;
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

static void bitforce_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;

	BFclose(fdDev);
}

#define CHECK_INTERVAL_MS 200

static uint64_t bitforce_scanhash(struct thr_info *thr, struct work *work, uint64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	bool dev_enabled = (bitforce->deven == DEV_ENABLED);
	static enum dev_enable last_dev_state = DEV_ENABLED;
	int wait_ms = 0;
	
	if (bitforce->deven == DEV_DISABLED) {
		bitforce_shutdown(thr);
		return 1;
	}
	
	// if device has just gone from disabled to enabled, re-initialise it
	if (last_dev_state == DEV_DISABLED && dev_enabled)
		bitforce_init(bitforce);
	last_dev_state = bitforce->deven;

	if (!bitforce_send_work(thr, work))
		return 0;

	while (wait_ms < BITFORCE_SLEEP_MS) {
		usleep(CHECK_INTERVAL_MS * 1000);
		wait_ms += CHECK_INTERVAL_MS;
		if (work_restart[thr->id].restart) {
			applog(LOG_DEBUG, "BFL%i: New work detected, discarding current job", bitforce->device_id);
		    return 1; //we have discard all work; equivilent to 0 hashes done.
		}
	}

	return bitforce_get_result(thr, work);
}

static bool bitforce_get_stats(struct cgpu_info *bitforce)
{
	return bitforce_get_temp(bitforce);
}

struct device_api bitforce_api = {
	.dname = "bitforce",
	.name = "BFL",
	.api_detect = bitforce_detect,
	.get_statline_before = get_bitforce_statline_before,
	.get_stats = bitforce_get_stats,
	.thread_prepare = bitforce_thread_prepare,
	.scanhash = bitforce_scanhash,
	.thread_shutdown = bitforce_shutdown
};
