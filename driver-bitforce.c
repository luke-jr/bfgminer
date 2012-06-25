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

#define BITFORCE_SLEEP_MS 2000
#define BITFORCE_TIMEOUT_MS 15000
#define BITFORCE_CHECK_INTERVAL_MS 10
#define WORK_CHECK_INTERVAL_MS 50
#define MAX_START_DELAY_US 100000

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

static ssize_t BFwrite(int fd, const void *buf, ssize_t bufLen)
{
	if ((bufLen) != write(fd, buf, bufLen)) {
		applog(LOG_ERR, "BFL: Error writing: %s", buf); 
		return 0;
	} else
		return bufLen;
}

#define BFclose(fd) close(fd)

static bool bitforce_detect_one(const char *devpath)
{
	char *s;
	char pdevbuf[0x100];

	applog(LOG_DEBUG, "BFL: Attempting to open %s", devpath);

	int fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BFL: Failed to open %s", devpath);
		return false;
	}
	BFwrite(fdDev, "ZGX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL: Error reading (ZGX)");
		return 0;
	}
	BFclose(fdDev);
	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_ERR, "BFL: Didn't recognise BitForce on %s", devpath);
		return false;
	}

	// We have a real BitForce!
	struct cgpu_info *bitforce;
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->api = &bitforce_api;
	bitforce->device_path = strdup(devpath);
	bitforce->deven = DEV_ENABLED;
	bitforce->threads = 1;
	bitforce->sleep_ms = BITFORCE_SLEEP_MS;
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}
	
	mutex_init(&bitforce->device_mutex);

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
		applog(LOG_ERR, "BFL%i: Failed to open %s", bitforce->device_id, bitforce->device_path);
		return false;
	}

	bitforce->device_fd = fdDev;

	applog(LOG_INFO, "BFL%i: Opened %s", bitforce->device_id, bitforce->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(bitforce->init, &now);

	return true;
}

void bitforce_init(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char *devpath = bitforce->device_path;
	char pdevbuf[0x100];
	char *s;

	applog(LOG_INFO, "BFL%i: Re-initalizing", bitforce->device_id);

	if (fdDev) {
		BFclose(fdDev);
		bitforce->device_fd = 0;
	}

	fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BFL%i: Failed to open %s", bitforce->device_id, devpath);
		return;
	}

	bitforce->device_fd = fdDev;

	mutex_lock(&bitforce->device_mutex);
	BFwrite(fdDev, "ZGX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->device_mutex);
	
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL%i: Error reading (ZGX)", bitforce->device_id);
		return;
	}

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_ERR, "BFL%i: Didn't recognise BitForce on %s", bitforce->device_id, devpath);
		return;
	}
	
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	char *s;

	mutex_lock(&bitforce->device_mutex);
	BFwrite(fdDev, "ZLX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->device_mutex);
	
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL%i: Error reading (ZLX)", bitforce->device_id);
		bitforce->temp = 0;
		return false;
	}
	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = strtof(s + 1, NULL);
		if (temp > 0) {
			bitforce->temp = temp;
			if (temp > bitforce->cutofftemp) {
				applog(LOG_WARNING, "BFL%i: Hit thermal cutoff limit, disabling!", bitforce->device_id);
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

	mutex_lock(&bitforce->device_mutex);
	BFwrite(fdDev, "ZDX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL%i: Error reading (ZDX)", bitforce->device_id);
		mutex_unlock(&bitforce->device_mutex);
		return false;
	}
	if (pdevbuf[0] == 'B'){
		applog(LOG_WARNING, "BFL%i: Throttling", bitforce->device_id);
		mutex_unlock(&bitforce->device_mutex);
		return true;
	}
	else if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BFL%i: ZDX reports: %s", bitforce->device_id, pdevbuf);
		mutex_unlock(&bitforce->device_mutex);
		return false;
	}
	memcpy(ob + 8, work->midstate, 32);
	memcpy(ob + 8 + 32, work->data + 64, 12);

	BFwrite(fdDev, ob, 60);
	if (opt_debug) {
		s = bin2hex(ob + 8, 44);
		applog(LOG_DEBUG, "BFL%i: block data: %s", bitforce->device_id, s);
		free(s);
	}
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->device_mutex);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL%i: Error reading (block data)", bitforce->device_id);
		return false;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BFL%i: block data reports: %s", bitforce->device_id, pdevbuf);
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

	while (bitforce->wait_ms < BITFORCE_TIMEOUT_MS) {
		mutex_lock(&bitforce->device_mutex);
		BFwrite(fdDev, "ZFX", 3);
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
		mutex_unlock(&bitforce->device_mutex);
		if (unlikely(!pdevbuf[0])) {
			applog(LOG_ERR, "BFL%i: Error reading (ZFX)", bitforce->device_id);
			mutex_unlock(&bitforce->device_mutex);
			return 0;
		}
		if (pdevbuf[0] != 'B')
			break;
		usleep(BITFORCE_CHECK_INTERVAL_MS*1000);
		bitforce->wait_ms += BITFORCE_CHECK_INTERVAL_MS;
	}
	if (bitforce->wait_ms >= BITFORCE_TIMEOUT_MS) {
		applog(LOG_ERR, "BFL%i: took longer than 15s", bitforce->device_id);
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_DEV_OVER_HEAT;
		bitforce->dev_over_heat_count++;
		return 1;
	} else {
	    /* Simple timing adjustment */
		if (bitforce->wait_ms > (bitforce->sleep_ms + WORK_CHECK_INTERVAL_MS))
			bitforce->sleep_ms += WORK_CHECK_INTERVAL_MS;
		else if (bitforce->wait_ms == bitforce->sleep_ms)
			bitforce->sleep_ms -= WORK_CHECK_INTERVAL_MS;		
	}

	applog(LOG_DEBUG, "BFL%i: waited %dms until %s", bitforce->device_id, bitforce->wait_ms, pdevbuf);
	work->blk.nonce = 0xffffffff;
	if (pdevbuf[2] == '-') 
		return 0xffffffff;   /* No valid nonce found */
	else if (pdevbuf[0] == 'I') 
		return 1;          /* Device idle */
	else if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
		applog(LOG_WARNING, "BFL%i: result reports: %s", bitforce->device_id, pdevbuf);
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

	BFclose(bitforce->device_fd);
	bitforce->device_fd = 0;
}

static void biforce_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;

	bitforce_init(bitforce);
}

static uint64_t bitforce_scanhash(struct thr_info *thr, struct work *work, uint64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	bitforce->wait_ms = 0;
	uint64_t ret;

	if (ret = bitforce_send_work(thr, work)) {
		while (bitforce->wait_ms < bitforce->sleep_ms) {
			usleep(WORK_CHECK_INTERVAL_MS*1000);
			bitforce->wait_ms += WORK_CHECK_INTERVAL_MS;
			if (work_restart[thr->id].restart) {
				applog(LOG_DEBUG, "BFL%i: Work restart, discarding after %dms", bitforce->device_id, bitforce->wait_ms);
				return 1; //we have discarded all work; equivilent to 0 hashes done.
			}
		}
		ret = bitforce_get_result(thr, work);
	}

	if (!ret) {
		ret = 1;
 		applog(LOG_ERR, "BFL%i: Comms error, going to recover mode", bitforce->device_id);
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_THREAD_ZERO_HASH;
		bitforce->thread_zero_hash_count++;
		bitforce->deven = DEV_RECOVER;
	}
	return ret;
}

static bool bitforce_get_stats(struct cgpu_info *bitforce)
{
	return bitforce_get_temp(bitforce);
}

static bool bitforce_thread_init(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	unsigned int wait;

	/* Pause each new thread a random time between 0-100ms 
	so the devices aren't making calls all at the same time. */
	wait = (rand() * MAX_START_DELAY_US)/RAND_MAX;
	applog(LOG_DEBUG, "BFL%i: Delaying start by %dms", bitforce->device_id, wait/1000);
	usleep(wait);

	return true;
}

struct device_api bitforce_api = {
	.dname = "bitforce",
	.name = "BFL",
	.api_detect = bitforce_detect,
	.reinit_device = bitforce_init,
	.get_statline_before = get_bitforce_statline_before,
	.get_stats = bitforce_get_stats,
	.thread_prepare = bitforce_thread_prepare,
	.thread_init = bitforce_thread_init,
	.scanhash = bitforce_scanhash,
	.thread_shutdown = bitforce_shutdown,
	.thread_enable = biforce_thread_enable
};
