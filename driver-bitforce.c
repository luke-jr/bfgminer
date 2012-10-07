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
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"

#ifdef WIN32

#include <windows.h>

#define dlsym (void*)GetProcAddress
#define dlclose FreeLibrary

typedef unsigned long FT_STATUS;
typedef PVOID FT_HANDLE;
__stdcall FT_STATUS (*FT_ListDevices)(PVOID pArg1, PVOID pArg2, DWORD Flags);
__stdcall FT_STATUS (*FT_Open)(int idx, FT_HANDLE*);
__stdcall FT_STATUS (*FT_GetComPortNumber)(FT_HANDLE, LPLONG lplComPortNumber);
__stdcall FT_STATUS (*FT_Close)(FT_HANDLE);
const uint32_t FT_OPEN_BY_DESCRIPTION =       2;
const uint32_t FT_LIST_ALL         = 0x20000000;
const uint32_t FT_LIST_NUMBER_ONLY = 0x80000000;
enum {
	FT_OK,
};

// Code must deal with a timeout. Make it 1 second on windows, 0.1 on linux.
#define BFopen(devpath)  serial_open(devpath, 0, 10, true)
#else /* WIN32 */
#define BFopen(devpath)  serial_open(devpath, 0, 1, true)
#endif /* WIN32 */

#include "compat.h"
#include "miner.h"
#include "fpgautils.h"

#define BITFORCE_SLEEP_MS 500
#define BITFORCE_TIMEOUT_S 7
#define BITFORCE_TIMEOUT_MS (BITFORCE_TIMEOUT_S * 1000)
#define BITFORCE_LONG_TIMEOUT_S 30
#define BITFORCE_LONG_TIMEOUT_MS (BITFORCE_LONG_TIMEOUT_S * 1000)
#define BITFORCE_CHECK_INTERVAL_MS 10
#define WORK_CHECK_INTERVAL_MS 50
#define MAX_START_DELAY_US 100000
#define tv_to_ms(tval) (tval.tv_sec * 1000 + tval.tv_usec / 1000)
#define TIME_AVG_CONSTANT 8

#define KNAME_WORK  "full work"
#define KNAME_RANGE "nonce range"

struct device_api bitforce_api;

static void BFgets(char *buf, size_t bufLen, int fd)
{
	do {
		buf[0] = '\0';
		--bufLen;
	} while (likely(bufLen && read(fd, buf, 1) == 1 && (buf++)[0] != '\n'));

	buf[0] = '\0';
}

static ssize_t BFwrite(int fd, const void *buf, ssize_t bufLen)
{
	if ((bufLen) != write(fd, buf, bufLen))
		return 0;
	else
		return bufLen;
}

#define BFclose(fd) close(fd)

static bool bitforce_detect_one(const char *devpath)
{
	int fdDev = BFopen(devpath);
	struct cgpu_info *bitforce;
	char pdevbuf[0x100];
	char *s;

	applog(LOG_DEBUG, "BFL: Attempting to open %s", devpath);

	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BFL: Failed to open %s", devpath);
		return false;
	}

	BFwrite(fdDev, "ZGX", 3);
	pdevbuf[0] = '\0';
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL: Error reading/timeout (ZGX)");
		return 0;
	}

	BFclose(fdDev);
	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_ERR, "BFL: Didn't recognise BitForce on %s", devpath);
		return false;
	}

	// We have a real BitForce!
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->api = &bitforce_api;
	bitforce->device_path = strdup(devpath);
	bitforce->deven = DEV_ENABLED;
	bitforce->threads = 1;
	/* Initially enable support for nonce range and disable it later if it
	 * fails */
	if (opt_bfl_noncerange) {
		bitforce->nonce_range = true;
		bitforce->sleep_ms = BITFORCE_SLEEP_MS;
		bitforce->kname = KNAME_RANGE;
	} else {
		bitforce->sleep_ms = BITFORCE_SLEEP_MS * 5;
		bitforce->kname = KNAME_WORK;
	}

	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>")))) {
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}

	mutex_init(&bitforce->device_mutex);

	return add_cgpu(bitforce);
}

#define LOAD_SYM(sym)  do { \
	if (!(sym = dlsym(dll, #sym))) {  \
		applog(LOG_DEBUG, "Failed to load " #sym ", not using FTDI bitforce autodetect");  \
		goto out;  \
	}  \
} while(0)

#ifdef WIN32
static int bitforce_autodetect_ftdi(void)
{
	char devpath[] = "\\\\.\\COMnnnnn";
	char *devpathnum = &devpath[7];
	char **bufptrs;
	char *buf;
	int found = 0;
	DWORD i;

	FT_STATUS ftStatus;
	DWORD numDevs;
	HMODULE dll = LoadLibrary("FTD2XX.DLL");
	if (!dll) {
		applog(LOG_DEBUG, "FTD2XX.DLL failed to load, not using FTDI bitforce autodetect");
		return 0;
	}
	LOAD_SYM(FT_ListDevices);
	LOAD_SYM(FT_Open);
	LOAD_SYM(FT_GetComPortNumber);
	LOAD_SYM(FT_Close);
	
	ftStatus = FT_ListDevices(&numDevs, NULL, FT_LIST_NUMBER_ONLY);
	if (ftStatus != FT_OK) {
		applog(LOG_DEBUG, "FTDI device count failed, not using FTDI bitforce autodetect");
		goto out;
	}
	applog(LOG_DEBUG, "FTDI reports %u devices", (unsigned)numDevs);

	buf = alloca(65 * numDevs);
	bufptrs = alloca(sizeof(*bufptrs) * (numDevs + 1));

	for (i = 0; i < numDevs; ++i)
		bufptrs[i] = &buf[i * 65];
	bufptrs[numDevs] = NULL;
	ftStatus = FT_ListDevices(bufptrs, &numDevs, FT_LIST_ALL | FT_OPEN_BY_DESCRIPTION);
	if (ftStatus != FT_OK) {
		applog(LOG_DEBUG, "FTDI device list failed, not using FTDI bitforce autodetect");
		goto out;
	}
	
	for (i = numDevs; i > 0; ) {
		--i;
		bufptrs[i][64] = '\0';
		
		if (!(strstr(bufptrs[i], "BitFORCE") && strstr(bufptrs[i], "SHA256")))
			continue;
		
		FT_HANDLE ftHandle;
		if (FT_OK != FT_Open(i, &ftHandle))
			continue;
		LONG lComPortNumber;
		ftStatus = FT_GetComPortNumber(ftHandle, &lComPortNumber);
		FT_Close(ftHandle);
		if (FT_OK != ftStatus || lComPortNumber < 0)
			continue;
		
		sprintf(devpathnum, "%d", (int)lComPortNumber);
		
		if (bitforce_detect_one(devpath))
			++found;
	}

out:
	dlclose(dll);
	return found;
}
#else
static int bitforce_autodetect_ftdi(void)
{
	return 0;
}
#endif

static int bitforce_detect_auto(void)
{
	return (serial_autodetect_udev     (bitforce_detect_one, "BitFORCE*SHA256") ?:
		serial_autodetect_devserial(bitforce_detect_one, "BitFORCE_SHA256") ?:
		bitforce_autodetect_ftdi() ?:
		0);
}

static void bitforce_detect(void)
{
	serial_detect_auto(&bitforce_api, bitforce_detect_one, bitforce_detect_auto);
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
	int fdDev = BFopen(bitforce->device_path);
	struct timeval now;

	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BFL%i: Failed to open %s", bitforce->device_id, bitforce->device_path);
		return false;
	}

	bitforce->device_fd = fdDev;

	applog(LOG_INFO, "BFL%i: Opened %s", bitforce->device_id, bitforce->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(bitforce->init, &now);

	return true;
}

static void bitforce_clear_buffer(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	int count = 0;

	if (!fdDev)
		return;

	applog(LOG_DEBUG, "BFL%i: Clearing read buffer", bitforce->device_id);

	mutex_lock(&bitforce->device_mutex);
	do {
		pdevbuf[0] = '\0';
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	} while (pdevbuf[0] && (++count < 10));
	mutex_unlock(&bitforce->device_mutex);
}

void bitforce_init(struct cgpu_info *bitforce)
{
	char *devpath = bitforce->device_path;
	int fdDev = bitforce->device_fd, retries = 0;
	char pdevbuf[0x100];
	char *s;

	applog(LOG_WARNING, "BFL%i: Re-initialising", bitforce->device_id);

	bitforce_clear_buffer(bitforce);

	mutex_lock(&bitforce->device_mutex);
	if (fdDev) {
		BFclose(fdDev);
		sleep(5);
	}
	bitforce->device_fd = 0;

	fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		mutex_unlock(&bitforce->device_mutex);
		applog(LOG_ERR, "BFL%i: Failed to open %s", bitforce->device_id, devpath);
		return;
	}

	do {
		BFwrite(fdDev, "ZGX", 3);
		pdevbuf[0] = '\0';
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);

		if (unlikely(!pdevbuf[0])) {
			mutex_unlock(&bitforce->device_mutex);
			applog(LOG_ERR, "BFL%i: Error reading/timeout (ZGX)", bitforce->device_id);
			return;
		}

		if (retries++)
			nmsleep(10);
	} while (!strstr(pdevbuf, "BUSY") && (retries * 10 < BITFORCE_TIMEOUT_MS));

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		mutex_unlock(&bitforce->device_mutex);
		applog(LOG_ERR, "BFL%i: Didn't recognise BitForce on %s returned: %s", bitforce->device_id, devpath, pdevbuf);
		return;
	}
	
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>")))) {
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}

	bitforce->device_fd = fdDev;
	bitforce->sleep_ms = BITFORCE_SLEEP_MS;

	mutex_unlock(&bitforce->device_mutex);
}

static void bitforce_flash_led(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;

	if (!fdDev)
		return;

	/* Do not try to flash the led if we're polling for a result to
	 * minimise the chance of interleaved results */
	if (bitforce->polling)
		return;

	/* It is not critical flashing the led so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(&bitforce->device_mutex))
		return;

	BFwrite(fdDev, "ZMX", 3);

	/* Once we've tried - don't do it until told to again */
	bitforce->flash_led = false;

	/* However, this stops anything else getting a reply
	 * So best to delay any other access to the BFL */
	sleep(4);

	mutex_unlock(&bitforce->device_mutex);

	return; // nothing is returned by the BFL
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	char *s;

	if (!fdDev)
		return false;

	/* Do not try to get the temperature if we're polling for a result to
	 * minimise the chance of interleaved results */
	if (bitforce->polling)
		return true;

	// Flash instead of Temp - doing both can be too slow
	if (bitforce->flash_led) {
		bitforce_flash_led(bitforce);
 		return true;
	}

	/* It is not critical getting temperature so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(&bitforce->device_mutex))
		return false;

	BFwrite(fdDev, "ZLX", 3);
	pdevbuf[0] = '\0';
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->device_mutex);
	
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL%i: Error: Get temp returned empty string/timed out", bitforce->device_id);
		bitforce->hw_errors++;
		return false;
	}

	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = strtof(s + 1, NULL);

		/* Cope with older software  that breaks and reads nonsense
		 * values */
		if (temp > 100)
			temp = strtod(s + 1, NULL);

		if (temp > 0) {
			bitforce->temp = temp;
			if (unlikely(bitforce->cutofftemp > 0 && temp > bitforce->cutofftemp)) {
				applog(LOG_WARNING, "BFL%i: Hit thermal cutoff limit, disabling!", bitforce->device_id);
				bitforce->deven = DEV_RECOVER;

				bitforce->device_last_not_well = time(NULL);
				bitforce->device_not_well_reason = REASON_DEV_THERMAL_CUTOFF;
				bitforce->dev_thermal_cutoff_count++;
			}
		}
	} else {
		/* Use the temperature monitor as a kind of watchdog for when
		 * our responses are out of sync and flush the buffer to
		 * hopefully recover */
		applog(LOG_WARNING, "BFL%i: Garbled response probably throttling, clearing buffer", bitforce->device_id);
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_DEV_THROTTLE;
		bitforce->dev_throttle_count++;
		/* Count throttling episodes as hardware errors */
		bitforce->hw_errors++;
		bitforce_clear_buffer(bitforce);
		return false;
	}

	return true;
}

static bool bitforce_send_work(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;
	unsigned char ob[70];
	char pdevbuf[0x100];
	char *s;

	if (!fdDev)
		return false;
re_send:
	mutex_lock(&bitforce->device_mutex);
	if (bitforce->nonce_range)
		BFwrite(fdDev, "ZPX", 3);
	else
		BFwrite(fdDev, "ZDX", 3);
	pdevbuf[0] = '\0';
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (!pdevbuf[0] || !strncasecmp(pdevbuf, "B", 1)) {
		mutex_unlock(&bitforce->device_mutex);
		nmsleep(WORK_CHECK_INTERVAL_MS);
		goto re_send;
	} else if (unlikely(strncasecmp(pdevbuf, "OK", 2))) {
		mutex_unlock(&bitforce->device_mutex);
		if (bitforce->nonce_range) {
			applog(LOG_WARNING, "BFL%i: Does not support nonce range, disabling", bitforce->device_id);
			bitforce->nonce_range = false;
			bitforce->sleep_ms *= 5;
			bitforce->kname = KNAME_WORK;
			goto re_send;
		}
		applog(LOG_ERR, "BFL%i: Error: Send work reports: %s", bitforce->device_id, pdevbuf);
		return false;
	}

	sprintf((char *)ob, ">>>>>>>>");
	memcpy(ob + 8, work->midstate, 32);
	memcpy(ob + 8 + 32, work->data + 64, 12);
	if (!bitforce->nonce_range) {
		sprintf((char *)ob + 8 + 32 + 12, ">>>>>>>>");
		work->blk.nonce = bitforce->nonces = 0xffffffff;
		BFwrite(fdDev, ob, 60);
	} else {
		uint32_t *nonce;

		nonce = (uint32_t *)(ob + 8 + 32 + 12);
		*nonce = htobe32(work->blk.nonce);
		nonce = (uint32_t *)(ob + 8 + 32 + 12 + 4);
		/* Split work up into 1/5th nonce ranges */
		bitforce->nonces = 0x33333332;
		*nonce = htobe32(work->blk.nonce + bitforce->nonces);
		work->blk.nonce += bitforce->nonces + 1;
		sprintf((char *)ob + 8 + 32 + 12 + 8, ">>>>>>>>");
		BFwrite(fdDev, ob, 68);
	}

	pdevbuf[0] = '\0';
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	mutex_unlock(&bitforce->device_mutex);

	if (opt_debug) {
		s = bin2hex(ob + 8, 44);
		applog(LOG_DEBUG, "BFL%i: block data: %s", bitforce->device_id, s);
		free(s);
	}

	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "BFL%i: Error: Send block data returned empty string/timed out", bitforce->device_id);
		return false;
	}

	if (unlikely(strncasecmp(pdevbuf, "OK", 2))) {
		applog(LOG_ERR, "BFL%i: Error: Send block data reports: %s", bitforce->device_id, pdevbuf);
		return false;
	}

	gettimeofday(&bitforce->work_start_tv, NULL);
	return true;
}

static int64_t bitforce_get_result(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;
	unsigned int delay_time_ms;
	struct timeval elapsed;
	struct timeval now;
	char pdevbuf[0x100];
	char *pnoncebuf;
	uint32_t nonce;

	if (!fdDev)
		return -1;

	while (1) {
		if (unlikely(thr->work_restart))
			return 0;

		mutex_lock(&bitforce->device_mutex);
		BFwrite(fdDev, "ZFX", 3);
		pdevbuf[0] = '\0';
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
		mutex_unlock(&bitforce->device_mutex);

		gettimeofday(&now, NULL);
		timersub(&now, &bitforce->work_start_tv, &elapsed);

		if (elapsed.tv_sec >= BITFORCE_LONG_TIMEOUT_S) {
			applog(LOG_ERR, "BFL%i: took %dms - longer than %dms", bitforce->device_id,
				tv_to_ms(elapsed), BITFORCE_LONG_TIMEOUT_MS);
			return 0;
		}

		if (pdevbuf[0] && strncasecmp(pdevbuf, "B", 1)) /* BFL does not respond during throttling */
			break;

		/* if BFL is throttling, no point checking so quickly */
		delay_time_ms = (pdevbuf[0] ? BITFORCE_CHECK_INTERVAL_MS : 2 * WORK_CHECK_INTERVAL_MS);
		nmsleep(delay_time_ms);
		bitforce->wait_ms += delay_time_ms;
	}

	if (elapsed.tv_sec > BITFORCE_TIMEOUT_S) {
		applog(LOG_ERR, "BFL%i: took %dms - longer than %dms", bitforce->device_id,
			tv_to_ms(elapsed), BITFORCE_TIMEOUT_MS);
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_DEV_OVER_HEAT;
		bitforce->dev_over_heat_count++;

		if (!pdevbuf[0])	/* Only return if we got nothing after timeout - there still may be results */
			return 0;
	} else if (!strncasecmp(pdevbuf, "N", 1)) {/* Hashing complete (NONCE-FOUND or NO-NONCE) */
		/* Simple timing adjustment. Allow a few polls to cope with
		 * OS timer delays being variably reliable. wait_ms will
		 * always equal sleep_ms when we've waited greater than or
		 * equal to the result return time.*/
		delay_time_ms = bitforce->sleep_ms;

		if (bitforce->wait_ms > bitforce->sleep_ms + (WORK_CHECK_INTERVAL_MS * 2))
			bitforce->sleep_ms += (bitforce->wait_ms - bitforce->sleep_ms) / 2;
		else if (bitforce->wait_ms == bitforce->sleep_ms) {
			if (bitforce->sleep_ms > WORK_CHECK_INTERVAL_MS)
				bitforce->sleep_ms -= WORK_CHECK_INTERVAL_MS;
			else if (bitforce->sleep_ms > BITFORCE_CHECK_INTERVAL_MS)
				bitforce->sleep_ms -= BITFORCE_CHECK_INTERVAL_MS;
		}

		if (delay_time_ms != bitforce->sleep_ms)
			  applog(LOG_DEBUG, "BFL%i: Wait time changed to: %d, waited %u", bitforce->device_id, bitforce->sleep_ms, bitforce->wait_ms);

		/* Work out the average time taken. Float for calculation, uint for display */
		bitforce->avg_wait_f += (tv_to_ms(elapsed) - bitforce->avg_wait_f) / TIME_AVG_CONSTANT;
		bitforce->avg_wait_d = (unsigned int) (bitforce->avg_wait_f + 0.5);
	}

	applog(LOG_DEBUG, "BFL%i: waited %dms until %s", bitforce->device_id, bitforce->wait_ms, pdevbuf);
	if (!strncasecmp(&pdevbuf[2], "-", 1))
		return bitforce->nonces;   /* No valid nonce found */
	else if (!strncasecmp(pdevbuf, "I", 1))
		return 0;	/* Device idle */
	else if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
		bitforce->hw_errors++;
		applog(LOG_WARNING, "BFL%i: Error: Get result reports: %s", bitforce->device_id, pdevbuf);
		bitforce_clear_buffer(bitforce);
		return 0;
	}

	pnoncebuf = &pdevbuf[12];

	while (1) {
		hex2bin((void*)&nonce, pnoncebuf, 4);
#ifndef __BIG_ENDIAN__
		nonce = swab32(nonce);
#endif
		if (unlikely(bitforce->nonce_range && (nonce >= work->blk.nonce ||
			(work->blk.nonce > 0 && nonce < work->blk.nonce - bitforce->nonces - 1)))) {
				applog(LOG_WARNING, "BFL%i: Disabling broken nonce range support", bitforce->device_id);
				bitforce->nonce_range = false;
				work->blk.nonce = 0xffffffff;
				bitforce->sleep_ms *= 5;
				bitforce->kname = KNAME_WORK;
		}
			
		submit_nonce(thr, work, nonce);
		if (strncmp(&pnoncebuf[8], ",", 1))
			break;
		pnoncebuf += 9;
	}

	return bitforce->nonces;
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

static int64_t bitforce_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	bool send_ret;
	int64_t ret;

	send_ret = bitforce_send_work(thr, work);

	if (!restart_wait(bitforce->sleep_ms))
		return 0;

	bitforce->wait_ms = bitforce->sleep_ms;

	if (send_ret) {
		bitforce->polling = true;
		ret = bitforce_get_result(thr, work);
		bitforce->polling = false;
	} else
		ret = -1;

	if (ret == -1) {
		ret = 0;
		applog(LOG_ERR, "BFL%i: Comms error", bitforce->device_id);
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_DEV_COMMS_ERROR;
		bitforce->dev_comms_error_count++;
		bitforce->hw_errors++;
		/* empty read buffer */
		bitforce_clear_buffer(bitforce);
	}
	return ret;
}

static bool bitforce_get_stats(struct cgpu_info *bitforce)
{
	return bitforce_get_temp(bitforce);
}

static void bitforce_identify(struct cgpu_info *bitforce)
{
	bitforce->flash_led = true;
}

static bool bitforce_thread_init(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	unsigned int wait;

	/* Pause each new thread at least 100ms between initialising
	 * so the devices aren't making calls all at the same time. */
	wait = thr->id * MAX_START_DELAY_US;
	applog(LOG_DEBUG, "BFL%i: Delaying start by %dms", bitforce->device_id, wait / 1000);
	usleep(wait);

	return true;
}

static struct api_data *bitforce_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_uint(root, "Sleep Time", &(cgpu->sleep_ms), false);
	root = api_add_uint(root, "Avg Wait", &(cgpu->avg_wait_d), false);

	return root;
}

struct device_api bitforce_api = {
	.dname = "bitforce",
	.name = "BFL",
	.api_detect = bitforce_detect,
	.get_api_stats = bitforce_api_stats,
	.reinit_device = bitforce_init,
	.get_statline_before = get_bitforce_statline_before,
	.get_stats = bitforce_get_stats,
	.identify_device = bitforce_identify,
	.thread_prepare = bitforce_thread_prepare,
	.thread_init = bitforce_thread_init,
	.scanhash = bitforce_scanhash,
	.thread_shutdown = bitforce_shutdown,
	.thread_enable = biforce_thread_enable
};
