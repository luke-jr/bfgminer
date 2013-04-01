/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "compat.h"
#include "deviceapi.h"
#include "miner.h"
#include "fpgautils.h"

#define BITFORCE_SLEEP_MS 500
#define BITFORCE_TIMEOUT_S 7
#define BITFORCE_TIMEOUT_MS (BITFORCE_TIMEOUT_S * 1000)
#define BITFORCE_LONG_TIMEOUT_S 25
#define BITFORCE_LONG_TIMEOUT_MS (BITFORCE_LONG_TIMEOUT_S * 1000)
#define BITFORCE_CHECK_INTERVAL_MS 10
#define WORK_CHECK_INTERVAL_MS 50
#define MAX_START_DELAY_MS 100
#define tv_to_ms(tval) ((unsigned long)(tval.tv_sec * 1000 + tval.tv_usec / 1000))
#define TIME_AVG_CONSTANT 8

enum bitforce_proto {
	BFP_WORK,
	BFP_RANGE,
	BFP_QUEUE,
};

static const char *protonames[] = {
	"full work",
	"nonce range",
	"work queue",
};

struct device_api bitforce_api;

// Code must deal with a timeout
#define BFopen(devpath)  serial_open(devpath, 0, 250, true)

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

static ssize_t bitforce_send(int fd, int procid, const void *buf, ssize_t bufLen)
{
	if (!procid)
		return BFwrite(fd, buf, bufLen);
	
	if (bufLen > 255)
		return -1;
	
	size_t bufLeft = bufLen + 3;
	char realbuf[bufLeft], *bufp;
	ssize_t rv;
	memcpy(&realbuf[3], buf, bufLen);
	realbuf[0] = '@';
	realbuf[1] = procid;
	realbuf[2] = bufLen;
	bufp = realbuf;
	while (true)
	{
		rv = BFwrite(fd, bufp, bufLeft);
		if (rv <= 0)
			return rv;
		bufLeft -= rv;
	}
	return bufLen;
}

static
void bitforce_cmd1(int fd, int procid, void *buf, size_t bufsz, const char *cmd)
{
	bitforce_send(fd, procid, cmd, 3);
	BFgets(buf, bufsz, fd);
}

static
void bitforce_cmd2(int fd, int procid, void *buf, size_t bufsz, const char *cmd, void *data, size_t datasz)
{
	bitforce_cmd1(fd, procid, buf, bufsz, cmd);
	if (strncasecmp(buf, "OK", 2))
		return;
	bitforce_send(fd, procid, data, datasz);
	BFgets(buf, bufsz, fd);
}

#define BFclose(fd) close(fd)

static bool bitforce_detect_one(const char *devpath)
{
	int fdDev = serial_open(devpath, 0, 10, true);
	struct cgpu_info *bitforce;
	char pdevbuf[0x100];
	size_t pdevbuf_len;
	char *s;
	int procs = 1;
	bool sc = false;

	applog(LOG_DEBUG, "BFL: Attempting to open %s", devpath);

	if (unlikely(fdDev == -1)) {
		applog(LOG_DEBUG, "BFL: Failed to open %s", devpath);
		return false;
	}

	bitforce_cmd1(fdDev, 0, pdevbuf, sizeof(pdevbuf), "ZGX");
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_DEBUG, "BFL: Error reading/timeout (ZGX)");
		return 0;
	}

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_DEBUG, "BFL: Didn't recognise BitForce on %s", devpath);
		BFclose(fdDev);
		return false;
	}

	applog(LOG_DEBUG, "Found BitForce device on %s", devpath);
	for ( bitforce_cmd1(fdDev, 0, pdevbuf, sizeof(pdevbuf), "ZCX");
	      strncasecmp(pdevbuf, "OK", 2);
	      BFgets(pdevbuf, sizeof(pdevbuf), fdDev) )
	{
		pdevbuf_len = strlen(pdevbuf);
		if (unlikely(!pdevbuf_len))
			continue;
		pdevbuf[pdevbuf_len-1] = '\0';  // trim newline
		applog(LOG_DEBUG, "  %s", pdevbuf);
		if (!strncasecmp(pdevbuf, "DEVICES IN CHAIN:", 17))
			procs = atoi(&pdevbuf[17]);
		else
		if (!strncasecmp(pdevbuf, "DEVICE:", 7) && strstr(pdevbuf, "SC"))
			sc = true;
	}
	BFclose(fdDev);
	
	// We have a real BitForce!
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->api = &bitforce_api;
	bitforce->device_path = strdup(devpath);
	bitforce->deven = DEV_ENABLED;
	bitforce->procs = procs;
	bitforce->threads = 1;

	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>")))) {
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}
	bitforce->cgpu_data = (void*)sc;

	mutex_init(&bitforce->device_mutex);

	return add_cgpu(bitforce);
}

static int bitforce_detect_auto(void)
{
	return serial_autodetect(bitforce_detect_one, "BitFORCE", "SHA256");
}

static void bitforce_detect(void)
{
	serial_detect_auto(&bitforce_api, bitforce_detect_one, bitforce_detect_auto);
}

struct bitforce_data {
	unsigned char next_work_ob[70];  // Data aligned for 32-bit access
	unsigned char *next_work_obs;    // Start of data to send
	unsigned char next_work_obsz;
	const char *next_work_cmd;
	char noncebuf[0x200];  // Large enough for 3 works of queue results
	int poll_func;
	enum bitforce_proto proto;
	bool sc;
	bool queued;
};

static void bitforce_clear_buffer(struct cgpu_info *);

static
void bitforce_comm_error(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	int *p_fdDev = &bitforce->device->device_fd;
	
	data->noncebuf[0] = '\0';
	applog(LOG_ERR, "%"PRIpreprv": Comms error", bitforce->proc_repr);
	dev_error(bitforce, REASON_DEV_COMMS_ERROR);
	++bitforce->hw_errors;
	++hw_errors;
	BFclose(*p_fdDev);
	int fd = *p_fdDev = BFopen(bitforce->device_path);
	if (fd == -1)
	{
		applog(LOG_ERR, "%s: Error reopening %s", bitforce->dev_repr, bitforce->device_path);
		return;
	}
	/* empty read buffer */
	bitforce_clear_buffer(bitforce);
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
		applog(LOG_ERR, "%s: Failed to open %s", bitforce->dev_repr, bitforce->device_path);
		return false;
	}

	bitforce->device_fd = fdDev;

	applog(LOG_INFO, "%s: Opened %s", bitforce->dev_repr, bitforce->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(bitforce->init, &now);

	return true;
}

static void bitforce_clear_buffer(struct cgpu_info *bitforce)
{
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;
	char pdevbuf[0x100];
	int count = 0;

	if (!fdDev)
		return;

	applog(LOG_DEBUG, "%"PRIpreprv": Clearing read buffer", bitforce->proc_repr);

	mutex_lock(mutexp);
	do {
		pdevbuf[0] = '\0';
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	} while (pdevbuf[0] && (++count < 10));
	mutex_unlock(mutexp);
}

void bitforce_init(struct cgpu_info *bitforce)
{
	const char *devpath = bitforce->device_path;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int *p_fdDev = &bitforce->device->device_fd;
	int fdDev = *p_fdDev, retries = 0;
	char pdevbuf[0x100];
	char *s;

	applog(LOG_WARNING, "%"PRIpreprv": Re-initialising", bitforce->proc_repr);

	bitforce_clear_buffer(bitforce);

	mutex_lock(mutexp);
	if (fdDev) {
		BFclose(fdDev);
		sleep(5);
	}
	*p_fdDev = 0;

	fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		mutex_unlock(mutexp);
		applog(LOG_ERR, "%s: Failed to open %s", bitforce->dev_repr, devpath);
		return;
	}

	do {
		bitforce_cmd1(fdDev, 0, pdevbuf, sizeof(pdevbuf), "ZGX");
		if (unlikely(!pdevbuf[0])) {
			mutex_unlock(mutexp);
			applog(LOG_ERR, "%s: Error reading/timeout (ZGX)", bitforce->dev_repr);
			return;
		}

		if (retries++)
			nmsleep(10);
	} while (!strstr(pdevbuf, "BUSY") && (retries * 10 < BITFORCE_TIMEOUT_MS));

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		mutex_unlock(mutexp);
		applog(LOG_ERR, "%s: Didn't recognise BitForce on %s returned: %s", bitforce->dev_repr, devpath, pdevbuf);
		return;
	}
	
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>")))) {
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}

	*p_fdDev = fdDev;
	bitforce->sleep_ms = BITFORCE_SLEEP_MS;

	mutex_unlock(mutexp);
}

static void bitforce_flash_led(struct cgpu_info *bitforce)
{
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;

	if (!fdDev)
		return;

	/* Do not try to flash the led if we're polling for a result to
	 * minimise the chance of interleaved results */
	if (bitforce->polling)
		return;

	/* It is not critical flashing the led so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(mutexp))
		return;

	char pdevbuf[0x100];
	bitforce_cmd1(fdDev, bitforce->proc_id, pdevbuf, sizeof(pdevbuf), "ZMX");

	/* Once we've tried - don't do it until told to again */
	bitforce->flash_led = false;

	/* However, this stops anything else getting a reply
	 * So best to delay any other access to the BFL */
	sleep(4);

	mutex_unlock(mutexp);

	return; // nothing is returned by the BFL
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;
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
	if (mutex_trylock(mutexp))
		return false;

	bitforce_cmd1(fdDev, bitforce->proc_id, pdevbuf, sizeof(pdevbuf), "ZLX");
	mutex_unlock(mutexp);
	
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "%"PRIpreprv": Error: Get temp returned empty string/timed out", bitforce->proc_repr);
		bitforce->hw_errors++;
		++hw_errors;
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
		}
	} else {
		/* Use the temperature monitor as a kind of watchdog for when
		 * our responses are out of sync and flush the buffer to
		 * hopefully recover */
		applog(LOG_WARNING, "%"PRIpreprv": Garbled response probably throttling, clearing buffer", bitforce->proc_repr);
		dev_error(bitforce, REASON_DEV_THROTTLE);
		/* Count throttling episodes as hardware errors */
		bitforce->hw_errors++;
		++hw_errors;
		bitforce_clear_buffer(bitforce);
		return false;
	}

	return true;
}

static inline
void dbg_block_data(struct cgpu_info *bitforce)
{
	if (!opt_debug)
		return;
	
	struct bitforce_data *data = bitforce->cgpu_data;
	char *s;
	s = bin2hex(&data->next_work_ob[8], 44);
	applog(LOG_DEBUG, "%"PRIpreprv": block data: %s", bitforce->proc_repr, s);
	free(s);
}

static void bitforce_change_mode(struct cgpu_info *, enum bitforce_proto);

static
bool bitforce_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	int fdDev = bitforce->device->device_fd;
	unsigned char *ob_ms = &data->next_work_ob[8];
	unsigned char *ob_dt = &ob_ms[32];
	
	// If polling job_start, cancel it
	if (data->poll_func == 1)
	{
		thr->tv_poll.tv_sec = -1;
		data->poll_func = 0;
	}
	
	memcpy(ob_ms, work->midstate, 32);
	memcpy(ob_dt, work->data + 64, 12);
	switch (data->proto)
	{
		case BFP_RANGE:
		{
			uint32_t *ob_nonce = (uint32_t*)&(ob_dt[32]);
			ob_nonce[0] = htobe32(work->blk.nonce);
			ob_nonce[1] = htobe32(work->blk.nonce + bitforce->nonces);
			// FIXME: if nonce range fails... we didn't increment enough
			work->blk.nonce += bitforce->nonces + 1;
			break;
		}
		case BFP_QUEUE:
			if (thr->work)
			{
				pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
				char pdevbuf[0x100];
				
				if (unlikely(!fdDev))
					return false;
				
				mutex_lock(mutexp);
				if (data->queued)
					bitforce_cmd1(fdDev, bitforce->proc_id, pdevbuf, sizeof(pdevbuf), "ZQX");
				bitforce_cmd2(fdDev, bitforce->proc_id, pdevbuf, sizeof(pdevbuf), data->next_work_cmd, data->next_work_obs, data->next_work_obsz);
				mutex_unlock(mutexp);
				if (unlikely(strncasecmp(pdevbuf, "OK", 2))) {
					applog(LOG_WARNING, "%"PRIpreprv": Does not support work queue, disabling", bitforce->proc_repr);
					bitforce_change_mode(bitforce, BFP_WORK);
				}
				else
				{
					dbg_block_data(bitforce);
					data->queued = true;
				}
			}
			// fallthru...
		case BFP_WORK:
			work->blk.nonce = 0xffffffff;
	}
	
	return true;
}

static
void bitforce_change_mode(struct cgpu_info *bitforce, enum bitforce_proto proto)
{
	struct bitforce_data *data = bitforce->cgpu_data;
	
	if (data->proto == proto)
		return;
	if (data->proto == BFP_RANGE)
	{
		bitforce->nonces = 0xffffffff;
		bitforce->sleep_ms *= 5;
		switch (proto)
		{
			case BFP_WORK:
				data->next_work_cmd = "ZDX";
				break;
			case BFP_QUEUE:
				data->next_work_cmd = "ZNX";
			default:
				;
		}
		if (data->sc)
		{
			// "S|---------- MidState ----------||-DataTail-|E"
			data->next_work_ob[7] = 45;
			data->next_work_ob[8+32+12] = '\xAA';
			data->next_work_obsz = 46;
		}
		else
		{
			// ">>>>>>>>|---------- MidState ----------||-DataTail-|>>>>>>>>"
			memset(&data->next_work_ob[8+32+12], '>', 8);
			data->next_work_obsz = 60;
		}
	}
	else
	if (proto == BFP_RANGE)
	{
		/* Split work up into 1/5th nonce ranges */
		bitforce->nonces = 0x33333332;
		bitforce->sleep_ms /= 5;
		data->next_work_cmd = "ZPX";
		if (data->sc)
		{
			data->next_work_ob[7] = 53;
			data->next_work_obsz = 54;
		}
		else
			data->next_work_obsz = 68;
	}
	data->proto = proto;
	bitforce->kname = protonames[proto];
}

static
void bitforce_job_start(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;
	unsigned char *ob = data->next_work_obs;
	char pdevbuf[0x100];
	struct timeval tv_now;

	if (data->queued)
	{
		// get_results collected more accurate job start time
		mt_job_transition(thr);
		job_start_complete(thr);
		data->queued = false;
		timer_set_delay(&thr->tv_morework, &bitforce->work_start_tv, bitforce->sleep_ms * 1000);
		return;
	}

	if (!fdDev)
		goto commerr;
re_send:
	mutex_lock(mutexp);
	bitforce_cmd2(fdDev, bitforce->proc_id, pdevbuf, sizeof(pdevbuf), data->next_work_cmd, ob, data->next_work_obsz);
	if (!pdevbuf[0] || !strncasecmp(pdevbuf, "B", 1)) {
		mutex_unlock(mutexp);
		gettimeofday(&tv_now, NULL);
		timer_set_delay(&thr->tv_poll, &tv_now, WORK_CHECK_INTERVAL_MS * 1000);
		data->poll_func = 1;
		return;
	} else if (unlikely(strncasecmp(pdevbuf, "OK", 2))) {
		mutex_unlock(mutexp);
		switch (data->proto)
		{
			case BFP_RANGE:
				applog(LOG_WARNING, "%"PRIpreprv": Does not support nonce range, disabling", bitforce->proc_repr);
				bitforce_change_mode(bitforce, BFP_WORK);
				goto re_send;
			case BFP_QUEUE:
				applog(LOG_WARNING, "%"PRIpreprv": Does not support work queue, disabling", bitforce->proc_repr);
				bitforce_change_mode(bitforce, BFP_WORK);
				goto re_send;
			default:
				;
		}
		applog(LOG_ERR, "%"PRIpreprv": Error: Send work reports: %s", bitforce->proc_repr, pdevbuf);
		goto commerr;
	}

	mt_job_transition(thr);
	mutex_unlock(mutexp);

	dbg_block_data(bitforce);

	gettimeofday(&tv_now, NULL);
	bitforce->work_start_tv = tv_now;
	
	timer_set_delay(&thr->tv_morework, &tv_now, bitforce->sleep_ms * 1000);
	
	job_start_complete(thr);
	return;

commerr:
	bitforce_comm_error(thr);
	job_start_abort(thr, true);
}

static inline int noisy_stale_wait(unsigned int mstime, struct work*work, bool checkend, struct cgpu_info*bitforce)
{
	int rv = stale_wait(mstime, work, checkend);
	if (rv)
		applog(LOG_NOTICE, "%"PRIpreprv": Abandoning stale search to restart",
		       bitforce->proc_repr);
	return rv;
}
#define noisy_stale_wait(mstime, work, checkend)  noisy_stale_wait(mstime, work, checkend, bitforce)

static char _discardedbuf[0x10];

static
void bitforce_job_get_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;
	unsigned int delay_time_ms;
	struct timeval elapsed;
	struct timeval now;
	char *pdevbuf = &data->noncebuf[0];
	bool stale;
	int count;

	gettimeofday(&now, NULL);
	timersub(&now, &bitforce->work_start_tv, &elapsed);
	bitforce->wait_ms = tv_to_ms(elapsed);
	bitforce->polling = true;
	
	if (!fdDev)
		goto commerr;

	stale = stale_work(work, true);
	
	if (unlikely(bitforce->wait_ms < bitforce->sleep_ms))
	{
		// We're likely here because of a work restart
		// Since Bitforce cannot stop a work without losing results, only do it if the current job is finding stale shares
		// BFP_QUEUE does not support stopping work at all
		if (data->proto == BFP_QUEUE || !stale)
		{
			delay_time_ms = bitforce->sleep_ms - bitforce->wait_ms;
			timer_set_delay(&thr->tv_poll, &now, delay_time_ms * 1000);
			data->poll_func = 2;
			return;
		}
	}

	while (1) {
		const char *cmd = (data->proto == BFP_QUEUE) ? "ZOX" : "ZFX";
		mutex_lock(mutexp);
		bitforce_cmd1(fdDev, bitforce->proc_id, pdevbuf, sizeof(data->noncebuf), cmd);
		if (!strncasecmp(pdevbuf, "COUNT:", 6))
		{
			count = atoi(&pdevbuf[6]);
			size_t cls = strlen(pdevbuf);
			char *pmorebuf = &pdevbuf[cls];
			size_t szleft = sizeof(data->noncebuf) - cls, sz;
			
			if (count && data->queued)
			{
				gettimeofday(&now, NULL);
				bitforce->work_start_tv = now;
			}
			
			while (true)
			{
				BFgets(pmorebuf, szleft, fdDev);
				if (!strncasecmp(pmorebuf, "OK", 2))
				{
					pmorebuf[0] = '\0';  // process expects only results
					break;
				}
				sz = strlen(pmorebuf);
				szleft -= sz;
				pmorebuf += sz;
				if (unlikely(!szleft))
				{
					// Out of buffer space somehow :(
					applog(LOG_DEBUG, "%"PRIpreprv": Ran out of buffer space for results, discarding extra data", bitforce->proc_repr);
					pmorebuf = _discardedbuf;
					szleft = sizeof(_discardedbuf);
				}
			}
		}
		else
			count = -1;
		mutex_unlock(mutexp);

		gettimeofday(&now, NULL);
		if (!count)
			goto noqr;
		timersub(&now, &bitforce->work_start_tv, &elapsed);

		if (elapsed.tv_sec >= BITFORCE_LONG_TIMEOUT_S) {
			applog(LOG_ERR, "%"PRIpreprv": took %lums - longer than %lums", bitforce->proc_repr,
				tv_to_ms(elapsed), (unsigned long)BITFORCE_LONG_TIMEOUT_MS);
			goto out;
		}

		if (pdevbuf[0] && strncasecmp(pdevbuf, "B", 1)) /* BFL does not respond during throttling */
			break;

		if (stale && data->proto != BFP_QUEUE)
		{
			applog(LOG_NOTICE, "%"PRIpreprv": Abandoning stale search to restart",
			       bitforce->proc_repr);
			goto out;
		}

noqr:
		/* if BFL is throttling, no point checking so quickly */
		delay_time_ms = (pdevbuf[0] ? BITFORCE_CHECK_INTERVAL_MS : 2 * WORK_CHECK_INTERVAL_MS);
		timer_set_delay(&thr->tv_poll, &now, delay_time_ms * 1000);
		data->poll_func = 2;
		return;
	}

	if (count < 0 && pdevbuf[0] == 'N')
		count = strncasecmp(pdevbuf, "NONCE-FOUND", 11) ? 1 : 0;
	// At this point, 'count' is:
	//   negative, in case of some kind of error
	//   zero, if NO-NONCE (FPGA either completed with no results, or rebooted)
	//   positive, if at least one job completed successfully

	if (elapsed.tv_sec > BITFORCE_TIMEOUT_S) {
		applog(LOG_ERR, "%"PRIpreprv": took %lums - longer than %lums", bitforce->proc_repr,
			tv_to_ms(elapsed), (unsigned long)BITFORCE_TIMEOUT_MS);
		dev_error(bitforce, REASON_DEV_OVER_HEAT);
		++bitforce->hw_errors;
		++hw_errors;

		/* If the device truly throttled, it didn't process the job and there
		 * are no results. But check first, just in case we're wrong about it
		 * throttling.
		 */
		if (count > 0)
			goto out;
	} else if (count >= 0) {/* Hashing complete (NONCE-FOUND or NO-NONCE) */
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
			  applog(LOG_DEBUG, "%"PRIpreprv": Wait time changed to: %d, waited %u", bitforce->proc_repr, bitforce->sleep_ms, bitforce->wait_ms);

		/* Work out the average time taken. Float for calculation, uint for display */
		bitforce->avg_wait_f += (tv_to_ms(elapsed) - bitforce->avg_wait_f) / TIME_AVG_CONSTANT;
		bitforce->avg_wait_d = (unsigned int) (bitforce->avg_wait_f + 0.5);
	}

	applog(LOG_DEBUG, "%"PRIpreprv": waited %dms until %s", bitforce->proc_repr, bitforce->wait_ms, pdevbuf);
	if (count < 0 && strncasecmp(pdevbuf, "I", 1)) {
		bitforce->hw_errors++;
		++hw_errors;
		applog(LOG_WARNING, "%"PRIpreprv": Error: Get result reports: %s", bitforce->proc_repr, pdevbuf);
		bitforce_clear_buffer(bitforce);
	}
out:
	bitforce->polling = false;
	job_results_fetched(thr);
	return;

commerr:
	bitforce_comm_error(thr);
	goto out;
}

static
void bitforce_process_result_nonces(struct thr_info *thr, struct work *work, char *pnoncebuf)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	uint32_t nonce;
	
	while (1) {
		hex2bin((void*)&nonce, pnoncebuf, 4);
		nonce = be32toh(nonce);
		if (unlikely(data->proto == BFP_RANGE && (nonce >= work->blk.nonce ||
			/* FIXME: blk.nonce is probably moved on quite a bit now! */
			(work->blk.nonce > 0 && nonce < work->blk.nonce - bitforce->nonces - 1)))) {
				applog(LOG_WARNING, "%"PRIpreprv": Disabling broken nonce range support", bitforce->proc_repr);
				bitforce_change_mode(bitforce, BFP_WORK);
		}
			
		submit_nonce(thr, work, nonce);
		if (strncmp(&pnoncebuf[8], ",", 1))
			break;
		pnoncebuf += 9;
	}
}

static
bool bitforce_process_qresult_line_i(struct thr_info *thr, char *midstate, char *datatail, char *buf, struct work *work)
{
	if (!work)
		return false;
	if (memcmp(work->midstate, midstate, 32))
		return false;
	if (memcmp(&work->data[64], datatail, 12))
		return false;
	
	if (!atoi(&buf[90]))
		return true;
	
	bitforce_process_result_nonces(thr, work, &buf[92]);
	
	return true;
}

static
void bitforce_process_qresult_line(struct thr_info *thr, char *buf, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	char midstate[32], datatail[12];
	
	hex2bin((void*)midstate, buf, 32);
	hex2bin((void*)datatail, &buf[65], 12);
	
	if (!( bitforce_process_qresult_line_i(thr, midstate, datatail, buf, work)
	    || bitforce_process_qresult_line_i(thr, midstate, datatail, buf, thr->work)
	    || bitforce_process_qresult_line_i(thr, midstate, datatail, buf, thr->prev_work)
	    || bitforce_process_qresult_line_i(thr, midstate, datatail, buf, thr->next_work) ))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to find work for queued results", bitforce->proc_repr);
		++bitforce->hw_errors;
		++hw_errors;
	}
}

static inline
char *next_line(char *in)
{
	while (in[0] && in[0] != '\n')
		++in;
	return in;
}

static
int64_t bitforce_job_process_results(struct thr_info *thr, struct work *work, __maybe_unused bool stopping)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	char *pnoncebuf = &data->noncebuf[0];
	int count;
	
	if (!strncasecmp(pnoncebuf, "NO-", 3))
		return bitforce->nonces;   /* No valid nonce found */
	
	if (!strncasecmp(pnoncebuf, "NONCE-FOUND", 11))
	{
		bitforce_process_result_nonces(thr, work, &pnoncebuf[12]);
		count = 1;
	}
	else
	if (!strncasecmp(pnoncebuf, "COUNT:", 6))
	{
		count = 0;
		pnoncebuf = next_line(pnoncebuf);
		while (pnoncebuf[0])
		{
			bitforce_process_qresult_line(thr, pnoncebuf, work);
			++count;
			pnoncebuf = next_line(pnoncebuf);
		}
	}

	// FIXME: This might have changed in the meantime (new job start, or broken)
	return bitforce->nonces * count;
}

static void bitforce_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int *p_fdDev = &bitforce->device->device_fd;

	BFclose(*p_fdDev);
	*p_fdDev = 0;
}

static void biforce_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;

	bitforce_init(bitforce);
}

static bool bitforce_get_stats(struct cgpu_info *bitforce)
{
	return bitforce_get_temp(bitforce);
}

static bool bitforce_identify(struct cgpu_info *bitforce)
{
	bitforce->flash_led = true;
	return true;
}

static bool bitforce_thread_init(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	unsigned int wait;
	struct bitforce_data *data;
	bool sc = (bool)bitforce->cgpu_data;
	
	for ( ; bitforce; bitforce = bitforce->next_proc)
	{
		bitforce->cgpu_data = data = malloc(sizeof(*data));
		*data = (struct bitforce_data){
			.next_work_ob = ">>>>>>>>|---------- MidState ----------||-DataTail-||Nonces|>>>>>>>>",
			.proto = BFP_RANGE,
			.sc = sc,
		};
		if (sc)
		{
			// ".......S|---------- MidState ----------||-DataTail-||Nonces|E"
			data->next_work_ob[8+32+12+8] = '\xAA';
			data->next_work_obs = &data->next_work_ob[7];
		}
		else
			data->next_work_obs = &data->next_work_ob[0];
		bitforce->sleep_ms = BITFORCE_SLEEP_MS;
		bitforce_change_mode(bitforce, BFP_WORK);
		/* Initially enable support for nonce range and disable it later if it
		 * fails */
		if (opt_bfl_noncerange)
			bitforce_change_mode(bitforce, BFP_RANGE);
	}
	
	bitforce = thr->cgpu;

	/* Pause each new thread at least 100ms between initialising
	 * so the devices aren't making calls all at the same time. */
	wait = thr->id * MAX_START_DELAY_MS;
	applog(LOG_DEBUG, "%s: Delaying start by %dms", bitforce->dev_repr, wait / 1000);
	nmsleep(wait);

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

void bitforce_poll(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->cgpu_data;
	int poll = data->poll_func;
	thr->tv_poll.tv_sec = -1;
	data->poll_func = 0;
	switch (poll)
	{
		case 1:
			bitforce_job_start(thr);
			break;
		case 2:
			bitforce_job_get_results(thr, thr->work);
			break;
		default:
			applog(LOG_ERR, "%"PRIpreprv": Unexpected poll from device API!", thr->cgpu->proc_repr);
	}
}

struct device_api bitforce_api = {
	.dname = "bitforce",
	.name = "BFL",
	.api_detect = bitforce_detect,
	.get_api_stats = bitforce_api_stats,
	.minerloop = minerloop_async,
	.reinit_device = bitforce_init,
	.get_statline_before = get_bitforce_statline_before,
	.get_stats = bitforce_get_stats,
	.identify_device = bitforce_identify,
	.thread_prepare = bitforce_thread_prepare,
	.thread_init = bitforce_thread_init,
	.job_prepare = bitforce_job_prepare,
	.job_start = bitforce_job_start,
	.job_get_results = bitforce_job_get_results,
	.poll = bitforce_poll,
	.job_process_results = bitforce_job_process_results,
	.thread_shutdown = bitforce_shutdown,
	.thread_enable = biforce_thread_enable
};
