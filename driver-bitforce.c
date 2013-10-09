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

#include <ctype.h>
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
#include "util.h"

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
#define BITFORCE_QRESULT_LINE_LEN 165
#define BITFORCE_MAX_QUEUED_MAX 40
#define BITFORCE_MIN_QUEUED_MAX 10
#define BITFORCE_MAX_QRESULTS 16
#define BITFORCE_GOAL_QRESULTS 5
#define BITFORCE_MIN_QRESULT_WAIT BITFORCE_CHECK_INTERVAL_MS
#define BITFORCE_MAX_QRESULT_WAIT 1000
#define BITFORCE_MAX_BQUEUE_AT_ONCE 5

enum bitforce_proto {
	BFP_WORK,
	BFP_RANGE,
	BFP_QUEUE,
	BFP_BQUEUE,
	BFP_PQUEUE,
};

static const char *protonames[] = {
	"full work",
	"nonce range",
	"work queue",
	"bulk queue",
	"parallel queue",
};

struct device_drv bitforce_drv;
struct device_drv bitforce_queue_api;

// Code must deal with a timeout
#define BFopen(devpath)  serial_open(devpath, 0, 250, true)

static void BFgets(char *buf, size_t bufLen, int fd)
{
	char *obuf = buf;
	do {
		buf[0] = '\0';
		--bufLen;
	} while (likely(bufLen && read(fd, buf, 1) == 1 && (buf++)[0] != '\n'));

	buf[0] = '\0';
	
	if (unlikely(opt_dev_protocol))
		applog(LOG_DEBUG, "DEVPROTO: GETS (fd=%d): %s", fd, obuf);
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
	realbuf[1] = bufLen;
	realbuf[2] = procid;
	bufp = realbuf;
	do
	{
		rv = BFwrite(fd, bufp, bufLeft);
		if (rv <= 0)
			return rv;
		bufLeft -= rv;
	}
	while (bufLeft > 0);
	return bufLen;
}

static
void bitforce_cmd1b(int fd, int procid, void *buf, size_t bufsz, const char *cmd, size_t cmdsz)
{
	if (unlikely(opt_dev_protocol))
		applog(LOG_DEBUG, "DEVPROTO: CMD1 (fd=%d xlink=%d): %s", fd, procid, cmd);
	
	bitforce_send(fd, procid, cmd, cmdsz);
	BFgets(buf, bufsz, fd);
}
#define bitforce_cmd1(fd, xlinkid, buf, bufsz, cmd)  bitforce_cmd1b(fd, xlinkid, buf, bufsz, cmd, 3)

static
void bitforce_cmd2(int fd, int procid, void *buf, size_t bufsz, const char *cmd, void *data, size_t datasz)
{
	bitforce_cmd1(fd, procid, buf, bufsz, cmd);
	if (strncasecmp(buf, "OK", 2))
		return;
	
	if (unlikely(opt_dev_protocol))
	{
		char hex[(datasz * 2) + 1];
		bin2hex(hex, data, datasz);
		applog(LOG_DEBUG, "DEVPROTO: CMD2 (fd=%d xlink=%d): %s", fd, procid, hex);
	}
	
	bitforce_send(fd, procid, data, datasz);
	BFgets(buf, bufsz, fd);
}

#define BFclose(fd) close(fd)

struct bitforce_init_data {
	bool sc;
	long devmask;
	int *parallels;
};

static
int bitforce_chips_to_plan_for(int parallel, int chipcount) {
	if (parallel < 1)
		return parallel;
	if (chipcount > 15) return 32;
	if (chipcount >  7) return 16;
	if (chipcount >  3) return  8;
	if (chipcount >  1) return  4;
	if (chipcount     ) return  2;
	                    return  1;
}

static bool bitforce_detect_one(const char *devpath)
{
	int fdDev = serial_open(devpath, 0, 10, true);
	struct cgpu_info *bitforce;
	char pdevbuf[0x100];
	size_t pdevbuf_len;
	char *s;
	int procs = 1, parallel = -1;
	long maxchipno = 0;
	struct bitforce_init_data *initdata;

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

	if (serial_claim_v(devpath, &bitforce_drv))
		return false;
	
	applog(LOG_DEBUG, "Found BitForce device on %s", devpath);
	initdata = malloc(sizeof(*initdata));
	*initdata = (struct bitforce_init_data){
		.sc = false,
	};
	bitforce_cmd1(fdDev, 0, pdevbuf, sizeof(pdevbuf), "ZCX");
	for (int i = 0; (!pdevbuf[0]) && i < 4; ++i)
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	for ( ;
	      strncasecmp(pdevbuf, "OK", 2);
	      BFgets(pdevbuf, sizeof(pdevbuf), fdDev) )
	{
		pdevbuf_len = strlen(pdevbuf);
		if (unlikely(!pdevbuf_len))
			continue;
		pdevbuf[pdevbuf_len-1] = '\0';  // trim newline
		
		applog(LOG_DEBUG, "  %s", pdevbuf);
		
		if (!strncasecmp(pdevbuf, "PROCESSOR ", 10))
			maxchipno = max(maxchipno, atoi(&pdevbuf[10]));
		else
		if (!strncasecmp(pdevbuf, "DEVICES IN CHAIN:", 17))
			procs = atoi(&pdevbuf[17]);
		else
		if (!strncasecmp(pdevbuf, "CHAIN PRESENCE MASK:", 20))
			initdata->devmask = strtol(&pdevbuf[20], NULL, 16);
		else
		if (!strncasecmp(pdevbuf, "DEVICE:", 7) && strstr(pdevbuf, "SC"))
			initdata->sc = true;
		else
		if (!strncasecmp(pdevbuf, "CHIP PARALLELIZATION: YES @", 27))
			parallel = atoi(&pdevbuf[27]);
	}
	parallel = bitforce_chips_to_plan_for(parallel, maxchipno);
	initdata->parallels = malloc(sizeof(initdata->parallels[0]) * procs);
	initdata->parallels[0] = parallel;
	parallel = abs(parallel);
	for (int proc = 1; proc < procs; ++proc)
	{
		applog(LOG_DEBUG, "Slave board %d:", proc);
		initdata->parallels[proc] = -1;
		maxchipno = 0;
		bitforce_cmd1(fdDev, proc, pdevbuf, sizeof(pdevbuf), "ZCX");
		for (int i = 0; (!pdevbuf[0]) && i < 4; ++i)
			BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
		for ( ;
		      strncasecmp(pdevbuf, "OK", 2);
		      BFgets(pdevbuf, sizeof(pdevbuf), fdDev) )
		{
			pdevbuf_len = strlen(pdevbuf);
			if (unlikely(!pdevbuf_len))
				continue;
			pdevbuf[pdevbuf_len-1] = '\0';  // trim newline
			
			applog(LOG_DEBUG, "  %s", pdevbuf);
			
			if (!strncasecmp(pdevbuf, "PROCESSOR ", 10))
				maxchipno = max(maxchipno, atoi(&pdevbuf[10]));
			else
			if (!strncasecmp(pdevbuf, "CHIP PARALLELIZATION: YES @", 27))
				initdata->parallels[proc] = atoi(&pdevbuf[27]);
		}
		initdata->parallels[proc] = bitforce_chips_to_plan_for(initdata->parallels[proc], maxchipno);
		parallel += abs(initdata->parallels[proc]);
	}
	BFclose(fdDev);
	
	if (unlikely((procs != 1 || parallel != 1) && !initdata->sc))
	{
		// Only bitforce_queue supports parallelization and XLINK, so force SC mode and hope for the best
		applog(LOG_WARNING, "SC features detected with non-SC device; this is not supported!");
		initdata->sc = true;
	}
	
	// We have a real BitForce!
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->drv = &bitforce_drv;
	if (initdata->sc)
		bitforce->drv = &bitforce_queue_api;
	bitforce->device_path = strdup(devpath);
	bitforce->deven = DEV_ENABLED;
	bitforce->procs = parallel;
	bitforce->threads = 1;
	if (initdata->sc)
		bitforce->cutofftemp = 85;

	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>")))) {
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}
	bitforce->device_data = initdata;

	mutex_init(&bitforce->device_mutex);

	return add_cgpu(bitforce);
}

static int bitforce_detect_auto(void)
{
	return serial_autodetect(bitforce_detect_one, "BitFORCE", "SHA256");
}

static void bitforce_detect(void)
{
	serial_detect_auto(&bitforce_drv, bitforce_detect_one, bitforce_detect_auto);
}

struct bitforce_data {
	int xlink_id;
	unsigned char next_work_ob[70];  // Data aligned for 32-bit access
	unsigned char *next_work_obs;    // Start of data to send
	unsigned char next_work_obsz;
	const char *next_work_cmd;
	char noncebuf[14 + ((BITFORCE_MAX_QRESULTS+1) * BITFORCE_QRESULT_LINE_LEN)];
	int poll_func;
	enum bitforce_proto proto;
	bool sc;
	int queued;
	int queued_max;
	int parallel;
	bool parallel_protocol;
	bool already_have_results;
	bool just_flushed;
	int ready_to_queue;
	bool want_to_send_queue;
	unsigned result_busy_polled;
	unsigned sleep_ms_default;
	struct timeval tv_hashmeter_start;
	float temp[2];
	long *volts;
	int volts_count;
	
	bool probed;
	bool supports_fanspeed;
};

struct bitforce_proc_data {
	struct cgpu_info *cgpu;
	bool handles_board;  // The first processor handles the queue for the entire board
};

static void bitforce_clear_buffer(struct cgpu_info *);

static
void bitforce_comm_error(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	int *p_fdDev = &bitforce->device->device_fd;
	
	data->noncebuf[0] = '\0';
	applog(LOG_ERR, "%"PRIpreprv": Comms error", bitforce->proc_repr);
	dev_error(bitforce, REASON_DEV_COMMS_ERROR);
	inc_hw_errors_only(thr);
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

static bool bitforce_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = BFopen(bitforce->device_path);

	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "%s: Failed to open %s", bitforce->dev_repr, bitforce->device_path);
		return false;
	}

	bitforce->device_fd = fdDev;

	applog(LOG_INFO, "%s: Opened %s", bitforce->dev_repr, bitforce->device_path);

	return true;
}

static void __bitforce_clear_buffer(int fdDev)
{
	char pdevbuf[0x100];
	int count = 0;

	do {
		pdevbuf[0] = '\0';
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	} while (pdevbuf[0] && (++count < 10));
}

static void bitforce_clear_buffer(struct cgpu_info *bitforce)
{
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev;
	
	mutex_lock(mutexp);
	
	fdDev = bitforce->device->device_fd;
	if (fdDev)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Clearing read buffer", bitforce->proc_repr);
		__bitforce_clear_buffer(fdDev);
	}
	mutex_unlock(mutexp);
}

void work_list_del(struct work **head, struct work *);

void bitforce_reinit(struct cgpu_info *bitforce)
{
	struct bitforce_data *data = bitforce->device_data;
	struct thr_info *thr = bitforce->thr[0];
	struct bitforce_proc_data *procdata = thr->cgpu_data;
	const char *devpath = bitforce->device_path;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int *p_fdDev = &bitforce->device->device_fd;
	int fdDev, retries = 0;
	char pdevbuf[0x100];
	char *s;
	
	if (!procdata->handles_board)
		return;

	mutex_lock(mutexp);
	fdDev = *p_fdDev;
	
	applog(LOG_WARNING, "%"PRIpreprv": Re-initialising", bitforce->proc_repr);

	if (fdDev) {
		BFclose(fdDev);
		cgsleep_ms(5000);
		*p_fdDev = 0;
	}

	fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		mutex_unlock(mutexp);
		applog(LOG_ERR, "%s: Failed to open %s", bitforce->dev_repr, devpath);
		return;
	}

	__bitforce_clear_buffer(fdDev);
	
	do {
		bitforce_cmd1(fdDev, 0, pdevbuf, sizeof(pdevbuf), "ZGX");
		if (unlikely(!pdevbuf[0])) {
			mutex_unlock(mutexp);
			BFclose(fdDev);
			applog(LOG_ERR, "%s: Error reading/timeout (ZGX)", bitforce->dev_repr);
			return;
		}

		if (retries++)
			cgsleep_ms(10);
	} while (strstr(pdevbuf, "BUSY") && (retries * 10 < BITFORCE_TIMEOUT_MS));

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		mutex_unlock(mutexp);
		BFclose(fdDev);
		applog(LOG_ERR, "%s: Didn't recognise BitForce on %s returned: %s", bitforce->dev_repr, devpath, pdevbuf);
		return;
	}
	
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>")))) {
		s[0] = '\0';
		free((void*)bitforce->name);
		bitforce->name = strdup(pdevbuf + 7);
	}

	*p_fdDev = fdDev;

	bitforce->sleep_ms = data->sleep_ms_default;
	
	if (bitforce->drv == &bitforce_queue_api)
	{
		struct work *work, *tmp;
		
		timer_set_delay_from_now(&thr->tv_poll, 0);
		notifier_wake(thr->notifier);
		
		bitforce_cmd1(fdDev, data->xlink_id, pdevbuf, sizeof(pdevbuf), "ZQX");
		DL_FOREACH_SAFE(thr->work_list, work, tmp)
			work_list_del(&thr->work_list, work);
		data->queued = 0;
		data->ready_to_queue = 0;
		data->already_have_results = false;
		data->just_flushed = true;
		thr->queue_full = false;
	}

	mutex_unlock(mutexp);

}

static void bitforce_flash_led(struct cgpu_info *bitforce)
{
	struct bitforce_data *data = bitforce->device_data;
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
	bitforce_cmd1(fdDev, data->xlink_id, pdevbuf, sizeof(pdevbuf), "ZMX");

	/* Once we've tried - don't do it until told to again */
	bitforce->flash_led = false;

	/* However, this stops anything else getting a reply
	 * So best to delay any other access to the BFL */
	cgsleep_ms(4000);

	mutex_unlock(mutexp);

	return; // nothing is returned by the BFL
}

static
float my_strtof(const char *nptr, char **endptr)
{
	float f = strtof(nptr, endptr);
	
	/* Cope with older software  that breaks and reads nonsense
	 * values */
	if (f > 100)
		f = strtod(nptr, endptr);
	
	return f;
}

static
void set_float_if_gt_zero(float *var, float value)
{
	if (value > 0)
		*var = value;
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;
	char pdevbuf[0x40];
	char voltbuf[0x40];
	char *s;
	struct cgpu_info *chip_cgpu;

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

	if (data->sc)
	{
		if (unlikely(!data->probed))
		{
			bitforce_cmd1(fdDev, data->xlink_id, voltbuf, sizeof(voltbuf), "Z9X");
			if (strncasecmp(voltbuf, "ERR", 3))
				data->supports_fanspeed = true;
			data->probed = true;
		}
		bitforce_cmd1(fdDev, data->xlink_id, voltbuf, sizeof(voltbuf), "ZTX");
	}
	bitforce_cmd1(fdDev, data->xlink_id, pdevbuf, sizeof(pdevbuf), "ZLX");
	mutex_unlock(mutexp);
	
	if (data->sc && likely(voltbuf[0]))
	{
		// Process voltage info
		// "NNNxxx,NNNxxx,NNNxxx"
		int n = 1;
		for (char *p = voltbuf; p[0]; ++p)
			if (p[0] == ',')
				++n;
		
		long *out = malloc(sizeof(long) * n);
		if (!out)
			goto skipvolts;
		
		n = 0;
		char *saveptr, *v;
		for (v = strtok_r(voltbuf, ",", &saveptr); v; v = strtok_r(NULL, ",", &saveptr))
			out[n++] = strtol(v, NULL, 10);
		
		data->volts_count = 0;
		free(data->volts);
		data->volts = out;
		data->volts_count = n;
	}
	
skipvolts:
	if (unlikely(!pdevbuf[0])) {
		struct thr_info *thr = bitforce->thr[0];
		applog(LOG_ERR, "%"PRIpreprv": Error: Get temp returned empty string/timed out", bitforce->proc_repr);
		inc_hw_errors_only(thr);
		return false;
	}

	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = my_strtof(s + 1, &s);
		
		set_float_if_gt_zero(&data->temp[0], temp);
		for ( ; s[0]; ++s)
		{
			if (!strncasecmp(s, "TEMP", 4) && (s = strchr(&s[4], ':')))
			{
				float temp2 = my_strtof(s + 1, &s);
				set_float_if_gt_zero(&data->temp[1], temp2);
				if (temp2 > temp)
					temp = temp2;
			}
		}

		if (temp > 0)
		{
			chip_cgpu = bitforce;
			for (int i = 0; i < data->parallel; ++i, (chip_cgpu = chip_cgpu->next_proc))
				chip_cgpu->temp = temp;
		}
	} else {
		struct thr_info *thr = bitforce->thr[0];
		/* Use the temperature monitor as a kind of watchdog for when
		 * our responses are out of sync and flush the buffer to
		 * hopefully recover */
		applog(LOG_WARNING, "%"PRIpreprv": Garbled response probably throttling, clearing buffer", bitforce->proc_repr);
		dev_error(bitforce, REASON_DEV_THROTTLE);
		/* Count throttling episodes as hardware errors */
		inc_hw_errors_only(thr);
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
	
	struct bitforce_data *data = bitforce->device_data;
	char s[89];
	bin2hex(s, &data->next_work_ob[8], 44);
	applog(LOG_DEBUG, "%"PRIpreprv": block data: %s", bitforce->proc_repr, s);
}

static void bitforce_change_mode(struct cgpu_info *, enum bitforce_proto);

static
bool bitforce_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
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
		case BFP_BQUEUE:
			quithere(1, "%"PRIpreprv": Impossible BFP_BQUEUE", bitforce->proc_repr);
		case BFP_PQUEUE:
			quithere(1, "%"PRIpreprv": Impossible BFP_PQUEUE", bitforce->proc_repr);
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
					bitforce_cmd1(fdDev, data->xlink_id, pdevbuf, sizeof(pdevbuf), "ZQX");
				bitforce_cmd2(fdDev, data->xlink_id, pdevbuf, sizeof(pdevbuf), data->next_work_cmd, data->next_work_obs, data->next_work_obsz);
				mutex_unlock(mutexp);
				if (unlikely(strncasecmp(pdevbuf, "OK", 2))) {
					applog(LOG_WARNING, "%"PRIpreprv": Does not support work queue, disabling", bitforce->proc_repr);
					bitforce_change_mode(bitforce, BFP_WORK);
				}
				else
				{
					dbg_block_data(bitforce);
					data->queued = 1;
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
	struct bitforce_data *data = bitforce->device_data;
	
	if (data->proto == proto)
		return;
	if (data->proto == BFP_RANGE)
	{
		bitforce->nonces = 0xffffffff;
		bitforce->sleep_ms *= 5;
		data->sleep_ms_default *= 5;
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
		data->sleep_ms_default /= 5;
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
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fdDev = bitforce->device->device_fd;
	unsigned char *ob = data->next_work_obs;
	char pdevbuf[0x100];
	struct timeval tv_now;

	data->result_busy_polled = 0;
	
	if (data->queued)
	{
		uint32_t delay;
		
		// get_results collected more accurate job start time
		mt_job_transition(thr);
		job_start_complete(thr);
		data->queued = 0;
		delay = (uint32_t)bitforce->sleep_ms * 1000;
		if (unlikely(data->already_have_results))
			delay = 0;
		timer_set_delay(&thr->tv_morework, &bitforce->work_start_tv, delay);
		return;
	}

	if (!fdDev)
		goto commerr;
re_send:
	mutex_lock(mutexp);
	bitforce_cmd2(fdDev, data->xlink_id, pdevbuf, sizeof(pdevbuf), data->next_work_cmd, ob, data->next_work_obsz);
	if (!pdevbuf[0] || !strncasecmp(pdevbuf, "B", 1)) {
		mutex_unlock(mutexp);
		cgtime(&tv_now);
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

	cgtime(&tv_now);
	bitforce->work_start_tv = tv_now;
	
	timer_set_delay(&thr->tv_morework, &tv_now, bitforce->sleep_ms * 1000);
	
	job_start_complete(thr);
	return;

commerr:
	bitforce_comm_error(thr);
	job_start_abort(thr, true);
}

static char _discardedbuf[0x10];

static
int bitforce_zox(struct thr_info *thr, const char *cmd)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fd = bitforce->device->device_fd;
	char *pdevbuf = &data->noncebuf[0];
	int count;
	
	mutex_lock(mutexp);
	bitforce_cmd1(fd, data->xlink_id, pdevbuf, sizeof(data->noncebuf), cmd);
	if (!strncasecmp(pdevbuf, "INPROCESS:", 10))
		BFgets(pdevbuf, sizeof(data->noncebuf), fd);
	if (!strncasecmp(pdevbuf, "COUNT:", 6))
	{
		count = atoi(&pdevbuf[6]);
		size_t cls = strlen(pdevbuf);
		char *pmorebuf = &pdevbuf[cls];
		size_t szleft = sizeof(data->noncebuf) - cls, sz;
		
		if (count && data->queued)
			cgtime(&bitforce->work_start_tv);
		
		while (true)
		{
			BFgets(pmorebuf, szleft, fd);
			if (!strncasecmp(pmorebuf, "OK", 2))
			{
				pmorebuf[0] = '\0';  // process expects only results
				break;
			}
			sz = strlen(pmorebuf);
			if (!sz)
			{
				applog(LOG_ERR, "%"PRIpreprv": Timeout during %s", bitforce->proc_repr, cmd);
				break;
			}
			szleft -= sz;
			pmorebuf += sz;
			if (unlikely(szleft < BITFORCE_QRESULT_LINE_LEN))
			{
				// Out of buffer space somehow :(
				applog(LOG_ERR, "%"PRIpreprv": Ran out of buffer space for results, discarding extra data", bitforce->proc_repr);
				pmorebuf = _discardedbuf;
				szleft = sizeof(_discardedbuf);
			}
		}
	}
	else
		count = -1;
	mutex_unlock(mutexp);
	
	return count;
}

static inline char *next_line(char *);

static
void bitforce_job_get_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	int fdDev = bitforce->device->device_fd;
	unsigned int delay_time_ms;
	struct timeval elapsed;
	struct timeval now;
	char *pdevbuf = &data->noncebuf[0];
	bool stale;
	int count;

	cgtime(&now);
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
		if (data->already_have_results)
		{
			data->already_have_results = false;
			strcpy(pdevbuf, "COUNT:0");
			count = 1;
			break;
		}
		
		const char *cmd = (data->proto == BFP_QUEUE) ? "ZOX" : "ZFX";
		count = bitforce_zox(thr, cmd);

		cgtime(&now);
		timersub(&now, &bitforce->work_start_tv, &elapsed);

		if (elapsed.tv_sec >= BITFORCE_LONG_TIMEOUT_S) {
			applog(LOG_ERR, "%"PRIpreprv": took %lums - longer than %lums", bitforce->proc_repr,
				tv_to_ms(elapsed), (unsigned long)BITFORCE_LONG_TIMEOUT_MS);
			goto out;
		}

		if (count > 0)
		{
			// Check that queue results match the current work
			// Also, if there are results from the next work, short-circuit this wait
			unsigned char midstate[32], datatail[12];
			char *p;
			int i;
			
			p = pdevbuf;
			for (i = 0; i < count; ++i)
			{
				p = next_line(p);
				hex2bin(midstate, p, 32);
				hex2bin(datatail, &p[65], 12);
				if (!(memcmp(work->midstate, midstate, 32) || memcmp(&work->data[64], datatail, 12)))
					break;
			}
			if (i == count)
			{
				// Didn't find the one we're waiting on
				// Must be extra stuff in the queue results
				char xmid[65];
				char xdt[25];
				bin2hex(xmid, work->midstate, 32);
				bin2hex(xdt, &work->data[64], 12);
				applog(LOG_WARNING, "%"PRIpreprv": Found extra garbage in queue results: %s",
				       bitforce->proc_repr, pdevbuf);
				applog(LOG_WARNING, "%"PRIpreprv": ...while waiting on: %s,%s",
				       bitforce->proc_repr, xmid, xdt);
				count = 0;
			}
			else
			if (i == count - 1)
				// Last one found is what we're looking for
			{}
			else
				// We finished the next job too!
				data->already_have_results = true;
		}
		
		if (!count)
			goto noqr;
		if (pdevbuf[0] && strncasecmp(pdevbuf, "B", 1)) /* BFL does not respond during throttling */
			break;

		data->result_busy_polled = bitforce->wait_ms;
		
		if (stale && data->proto != BFP_QUEUE)
		{
			applog(LOG_NOTICE, "%"PRIpreprv": Abandoning stale search to restart",
			       bitforce->proc_repr);
			goto out;
		}

noqr:
		data->result_busy_polled = bitforce->wait_ms;
		
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
		inc_hw_errors_only(thr);

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

		if (!data->result_busy_polled)
		{
			// No busy polls before results received
			if (bitforce->wait_ms > delay_time_ms + (WORK_CHECK_INTERVAL_MS * 8))
				// ... due to poll being rather late; ignore it as an anomaly
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on first poll after %ums, later than scheduled %ums (ignoring)",
				       bitforce->proc_repr, bitforce->wait_ms, delay_time_ms);
			else
			if (bitforce->sleep_ms > data->sleep_ms_default + (BITFORCE_CHECK_INTERVAL_MS * 0x20))
			{
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on first poll after %ums, on delayed schedule %ums; Wait time changed to: %ums (default sch)",
				       bitforce->proc_repr, bitforce->wait_ms, delay_time_ms, data->sleep_ms_default);
				bitforce->sleep_ms = data->sleep_ms_default;
			}
			else
			{
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on first poll after %ums, on default schedule %ums; Wait time changed to: %ums (check interval)",
				       bitforce->proc_repr, bitforce->wait_ms, delay_time_ms, BITFORCE_CHECK_INTERVAL_MS);
				bitforce->sleep_ms = BITFORCE_CHECK_INTERVAL_MS;
			}
		}
		else
		{
			if (data->result_busy_polled - bitforce->sleep_ms > WORK_CHECK_INTERVAL_MS)
			{
				bitforce->sleep_ms = data->result_busy_polled - (WORK_CHECK_INTERVAL_MS / 2);
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on Nth poll after %ums (busy poll at %ums, sch'd %ums); Wait time changed to: %ums",
				       bitforce->proc_repr, bitforce->wait_ms, data->result_busy_polled, delay_time_ms, bitforce->sleep_ms);
			}
			else
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on Nth poll after %ums (busy poll at %ums, sch'd %ums); Wait time unchanged",
				       bitforce->proc_repr, bitforce->wait_ms, data->result_busy_polled, delay_time_ms);
		}

		/* Work out the average time taken. Float for calculation, uint for display */
		bitforce->avg_wait_f += (tv_to_ms(elapsed) - bitforce->avg_wait_f) / TIME_AVG_CONSTANT;
		bitforce->avg_wait_d = (unsigned int) (bitforce->avg_wait_f + 0.5);
	}

	applog(LOG_DEBUG, "%"PRIpreprv": waited %dms until %s", bitforce->proc_repr, bitforce->wait_ms, pdevbuf);
	if (count < 0 && strncasecmp(pdevbuf, "I", 1)) {
		inc_hw_errors_only(thr);
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
	struct bitforce_data *data = bitforce->device_data;
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
	
	char *end;
	if (strtol(&buf[90], &end, 10))
		bitforce_process_result_nonces(thr, work, &end[1]);
	
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
		inc_hw_errors_only(thr);
	}
}

static inline
char *next_line(char *in)
{
	while (in[0] && (in++)[0] != '\n')
	{}
	return in;
}

static
int64_t bitforce_job_process_results(struct thr_info *thr, struct work *work, __maybe_unused bool stopping)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
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
	else
		return 0;

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

	bitforce_reinit(bitforce);
}

static bool bitforce_get_stats(struct cgpu_info *bitforce)
{
	struct bitforce_proc_data *procdata = bitforce->thr[0]->cgpu_data;
	
	if (!procdata->handles_board)
		return true;
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
	struct bitforce_proc_data *procdata;
	struct bitforce_init_data *initdata = bitforce->device_data;
	bool sc = initdata->sc;
	int xlink_id = 0, boardno = 0;
	struct bitforce_proc_data *first_on_this_board;
	char buf[100];
	int fd = bitforce->device_fd;
	
	for ( ; bitforce; bitforce = bitforce->next_proc)
	{
		thr = bitforce->thr[0];
		
		if (unlikely(xlink_id > 30))
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to find XLINK address", bitforce->proc_repr);
			dev_error(bitforce, REASON_THREAD_FAIL_INIT);
			bitforce->reinit_backoff = 1e10;
			continue;
		}
		
		bitforce->sleep_ms = BITFORCE_SLEEP_MS;
		bitforce->device_data = data = malloc(sizeof(*data));
		*data = (struct bitforce_data){
			.xlink_id = xlink_id,
			.next_work_ob = ">>>>>>>>|---------- MidState ----------||-DataTail-||Nonces|>>>>>>>>",
			.proto = BFP_RANGE,
			.sc = sc,
			.sleep_ms_default = BITFORCE_SLEEP_MS,
			.parallel = abs(initdata->parallels[boardno]),
			.parallel_protocol = (initdata->parallels[boardno] != -1),
		};
		thr->cgpu_data = procdata = malloc(sizeof(*procdata));
		*procdata = (struct bitforce_proc_data){
			.handles_board = true,
			.cgpu = bitforce,
		};
		if (sc)
		{
			// ".......S|---------- MidState ----------||-DataTail-||Nonces|E"
			data->next_work_ob[8+32+12+8] = '\xAA';
			data->next_work_obs = &data->next_work_ob[7];
			
			if (bitforce->drv == &bitforce_queue_api)
			{
				bitforce_change_mode(bitforce, data->parallel_protocol ? BFP_PQUEUE : BFP_BQUEUE);
				bitforce->sleep_ms = data->sleep_ms_default = 100;
				timer_set_delay_from_now(&thr->tv_poll, 0);
				data->queued_max = data->parallel * 2;
				if (data->queued_max < BITFORCE_MIN_QUEUED_MAX)
					data->queued_max = BITFORCE_MIN_QUEUED_MAX;
				if (data->queued_max > BITFORCE_MAX_QUEUED_MAX)
					data->queued_max = BITFORCE_MAX_QUEUED_MAX;
			}
			else
				bitforce_change_mode(bitforce, BFP_QUEUE);
			
			// Clear job queue to start fresh; ignore response
			bitforce_cmd1(fd, data->xlink_id, buf, sizeof(buf), "ZQX");
		}
		else
		{
			data->next_work_obs = &data->next_work_ob[0];
			
			// Unconditionally change away from cold-initialized BFP_RANGE, to allow for setting up other variables
			bitforce_change_mode(bitforce, BFP_WORK);
			/* Initially enable support for nonce range and disable it later if it
			 * fails */
			if (opt_bfl_noncerange)
				bitforce_change_mode(bitforce, BFP_RANGE);
		}
		bitforce->status = LIFE_INIT2;
		
		first_on_this_board = procdata;
		for (int proc = 1; proc < data->parallel; ++proc)
		{
			bitforce = bitforce->next_proc;
			assert(bitforce);
			thr = bitforce->thr[0];
			
			thr->queue_full = true;
			thr->cgpu_data = procdata = malloc(sizeof(*procdata));
			*procdata = *first_on_this_board;
			procdata->handles_board = false;
			procdata->cgpu = bitforce;
			bitforce->device_data = data;
			bitforce->status = LIFE_INIT2;
			bitforce->kname = first_on_this_board->cgpu->kname;
		}
		applog(LOG_DEBUG, "%s: Board %d: %"PRIpreprv"-%"PRIpreprv, bitforce->dev_repr, boardno, first_on_this_board->cgpu->proc_repr, bitforce->proc_repr);
		
		++boardno;
		while (xlink_id < 31 && !(initdata->devmask & (1 << ++xlink_id)))
		{}
	}
	
	// NOTE: This doesn't restore the first processor, but it does get us the last one; this is sufficient for the delay debug and start of the next loop below
	bitforce = thr->cgpu;

	free(initdata->parallels);
	free(initdata);

	/* Pause each new thread at least 100ms between initialising
	 * so the devices aren't making calls all at the same time. */
	wait = thr->id * MAX_START_DELAY_MS;
	applog(LOG_DEBUG, "%s: Delaying start by %dms", bitforce->dev_repr, wait / 1000);
	cgsleep_ms(wait);

	if (sc)
	{
		// Clear results queue last, to start fresh; ignore response
		for (bitforce = bitforce->device; bitforce; bitforce = bitforce->next_proc)
			bitforce_zox(thr, "ZOX");
	}
	
	return true;
}

#ifdef HAVE_CURSES
static
void bitforce_tui_wlogprint_choices(struct cgpu_info *cgpu)
{
	struct bitforce_data *data = cgpu->device_data;
	if (data->supports_fanspeed)
		wlogprint("[F]an control ");
}

static
const char *bitforce_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	struct bitforce_data *data = cgpu->device_data;
	pthread_mutex_t *mutexp;
	int fd;
	static char replybuf[0x100];
	
	if (!data->supports_fanspeed)
		return NULL;
	switch (input)
	{
		case 'f': case 'F':
		{
			int fanspeed;
			char *intvar;

			intvar = curses_input("Set fan speed (range 0-5 for low to fast or 9 for auto)");
			if (!intvar)
				return "Invalid fan speed\n";
			fanspeed = atoi(intvar);
			free(intvar);
			if ((fanspeed < 0 || fanspeed > 5) && fanspeed != 9)
				return "Invalid fan speed\n";
			
			char cmd[4] = "Z0X";
			cmd[1] += fanspeed;
			mutexp = &cgpu->device->device_mutex;
			mutex_lock(mutexp);
			fd = cgpu->device->device_fd;
			bitforce_cmd1(fd, data->xlink_id, replybuf, sizeof(replybuf), cmd);
			mutex_unlock(mutexp);
			return replybuf;
		}
	}
	return NULL;
}

static
void bitforce_wlogprint_status(struct cgpu_info *cgpu)
{
	struct bitforce_data *data = cgpu->device_data;
	if (data->temp[0] > 0 && data->temp[1] > 0)
		wlogprint("Temperatures: %4.1fC %4.1fC\n", data->temp[0], data->temp[1]);
	if (data->volts_count)
	{
		// -> "NNN.xxx / NNN.xxx / NNN.xxx"
		size_t sz = (data->volts_count * 10) + 1;
		char buf[sz];
		char *s = buf;
		int rv = 0;
		for (int i = 0; i < data->volts_count; ++i)
		{
			long v = data->volts[i];
			_SNP("%ld.%03d / ", v / 1000, (int)(v % 1000));
		}
		if (rv >= 3 && s[-2] == '/')
			s[-3] = '\0';
		wlogprint("Voltages: %s\n", buf);
	}
}
#endif

static struct api_data *bitforce_drv_stats(struct cgpu_info *cgpu)
{
	struct bitforce_data *data = cgpu->device_data;
	struct api_data *root = NULL;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_uint(root, "Sleep Time", &(cgpu->sleep_ms), false);
	if (data->proto != BFP_BQUEUE && data->proto != BFP_PQUEUE)
		root = api_add_uint(root, "Avg Wait", &(cgpu->avg_wait_d), false);
	if (data->temp[0] > 0 && data->temp[1] > 0)
	{
		root = api_add_temp(root, "Temperature0", &(data->temp[0]), false);
		root = api_add_temp(root, "Temperature1", &(data->temp[1]), false);
	}
	
	for (int i = 0; i < data->volts_count; ++i)
	{
		float voltage = data->volts[i];
		char key[] = "VoltageNN";
		snprintf(&key[7], 3, "%d", i);
		voltage /= 1e3;
		root = api_add_volts(root, key, &voltage, true);
	}

	return root;
}

void bitforce_poll(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
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

static
char *bitforce_set_device(struct cgpu_info *proc, char *option, char *setting, char *replybuf)
{
	struct bitforce_data *data = proc->device_data;
	pthread_mutex_t *mutexp = &proc->device->device_mutex;
	int fd;
	
	if (!strcasecmp(option, "help"))
	{
		sprintf(replybuf, "fanmode: range 0-5 (low to fast) or 9 (auto)");
		return replybuf;
	}
	
	if (!strcasecmp(option, "fanmode"))
	{
		if (!data->supports_fanspeed)
		{
			sprintf(replybuf, "fanmode not supported");
			return replybuf;
		}
		if (!setting || !*setting)
		{
			sprintf(replybuf, "missing fanmode setting");
			return replybuf;
		}
		if (setting[1] || ((setting[0] < '0' || setting[0] > '5') && setting[0] != '9'))
		{
			sprintf(replybuf, "invalid fanmode setting");
			return replybuf;
		}
		
		char cmd[4] = "Z5X";
		cmd[1] = setting[0];
		mutex_lock(mutexp);
		fd = proc->device->device_fd;
		bitforce_cmd1(fd, data->xlink_id, replybuf, 256, cmd);
		mutex_unlock(mutexp);
		return replybuf;
	}
	
	if (!strcasecmp(option, "_cmd1"))
	{
		mutex_lock(mutexp);
		fd = proc->device->device_fd;
		bitforce_cmd1b(fd, data->xlink_id, replybuf, 8000, setting, strlen(setting));
		mutex_unlock(mutexp);
		return replybuf;
	}
	
	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

struct device_drv bitforce_drv = {
	.dname = "bitforce",
	.name = "BFL",
	.drv_detect = bitforce_detect,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitforce_wlogprint_status,
	.proc_tui_wlogprint_choices = bitforce_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitforce_tui_handle_choice,
#endif
	.get_api_stats = bitforce_drv_stats,
	.minerloop = minerloop_async,
	.reinit_device = bitforce_reinit,
	.get_stats = bitforce_get_stats,
	.set_device = bitforce_set_device,
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


static inline
void bitforce_set_queue_full(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	
	thr->queue_full = (data->queued + data->ready_to_queue >= data->queued_max) || (data->ready_to_queue >= BITFORCE_MAX_BQUEUE_AT_ONCE);
}

static
bool bitforce_send_queue(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int fd = bitforce->device->device_fd;
	struct work *work;
	
	if (unlikely(!(fd && data->ready_to_queue)))
		return false;
	
	char buf[0x100];
	int queued_ok;
	size_t qjs_sz = (32 + 12 + 2);
	size_t qjp_sz = 4 + (qjs_sz * data->ready_to_queue);
	uint8_t qjp[qjp_sz], *qjs;
	qjp[0] = qjp_sz - 1;
	qjp[1] = 0xc1;
	qjp[2] = data->ready_to_queue;
	qjp[qjp_sz - 1] = 0xfe;
	qjs = &qjp[qjp_sz - 1];
	
	work = thr->work_list->prev;
	for (int i = data->ready_to_queue; i > 0; --i, work = work->prev)
	{
		*(--qjs) = 0xaa;
		memcpy(qjs -= 12, work->data + 64, 12);
		memcpy(qjs -= 32, work->midstate, 32);
		*(--qjs) = 45;
	}
	
	mutex_lock(mutexp);
	bitforce_cmd2(fd, data->xlink_id, buf, sizeof(buf), "ZWX", qjp, qjp_sz);
	mutex_unlock(mutexp);
	
	if (!strncasecmp(buf, "ERR:QUEUE", 9))
	{
		// Queue full :(
		applog(LOG_DEBUG, "%"PRIpreprv": Device queue full while attempting to append %d jobs (queued<=%d)",
	           bitforce->proc_repr,
	           data->ready_to_queue, data->queued);
		thr->queue_full = true;
		return false;
	}
	if (strncasecmp(buf, "OK:QUEUED", 9))
	{
		// TODO: ERR:UNKNOWN COMMAND
		applog(LOG_DEBUG, "%"PRIpreprv": Unexpected error attempting to append %d jobs (queued<=%d): %s",
	           bitforce->proc_repr,
	           data->ready_to_queue, data->queued, buf);
		return false;
	}
	
	if (!data->queued)
		cgtime(&data->tv_hashmeter_start);
	
	queued_ok = atoi(&buf[9]);
	data->queued += queued_ok;
	applog(LOG_DEBUG, "%"PRIpreprv": Successfully queued %d/%d jobs on device (queued<=%d)",
	       bitforce->proc_repr,
	       queued_ok, data->ready_to_queue, data->queued);
	data->ready_to_queue -= queued_ok;
	thr->queue_full = data->ready_to_queue;
	data->just_flushed = false;
	data->want_to_send_queue = false;
	
	return true;
}

void work_list_del(struct work **head, struct work *work)
{
	DL_DELETE(*head, work);
	free_work(work);
}

static
bool bitforce_queue_do_results(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	int fd = bitforce->device->device_fd;
	int count;
	int fcount;
	char *noncebuf, *buf, *end;
	unsigned char midstate[32], datatail[12];
	struct work *work, *tmpwork, *thiswork;
	struct timeval tv_now, tv_elapsed;
	long chipno = 0;  // Initialized value is used for non-parallelized boards
	struct cgpu_info *chip_cgpu;
	struct thr_info *chip_thr;
	int counts[data->parallel];
	
	if (unlikely(!fd))
		return false;
	
again:
	noncebuf = &data->noncebuf[0];
	count = bitforce_zox(thr, "ZOX");
	
	if (unlikely(count < 0))
	{
		applog(LOG_ERR, "%"PRIpreprv": Received unexpected queue result response: %s", bitforce->proc_repr, noncebuf);
		inc_hw_errors_only(thr);
		return false;
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Received %d queue results on poll (max=%d)", bitforce->proc_repr, count, (int)BITFORCE_MAX_QRESULTS);
	if (!count)
		return true;
	
	fcount = 0;
	for (int i = 0; i < data->parallel; ++i)
		counts[i] = 0;
	noncebuf = next_line(noncebuf);
	while ((buf = noncebuf)[0])
	{
		if ( (noncebuf = next_line(buf)) )
			noncebuf[-1] = '\0';
		
		if (strlen(buf) <= 90)
		{
			applog(LOG_ERR, "%"PRIpreprv": Gibberish within queue results: %s", bitforce->proc_repr, buf);
			continue;
		}
		
		hex2bin(midstate, buf, 32);
		hex2bin(datatail, &buf[65], 12);
		
		thiswork = NULL;
		DL_FOREACH(thr->work_list, work)
		{
			if (unlikely(memcmp(work->midstate, midstate, 32)))
				continue;
			if (unlikely(memcmp(&work->data[64], datatail, 12)))
				continue;
			thiswork = work;
			break;
		}
		
		end = &buf[89];
		chip_cgpu = bitforce;
		if (data->parallel_protocol)
		{
			chipno = strtol(&end[1], &end, 16);
			if (chipno >= data->parallel)
			{
				applog(LOG_ERR, "%"PRIpreprv": Chip number out of range for queue result: %s", chip_cgpu->proc_repr, buf);
				chipno = 0;
			}
			for (int i = 0; i < chipno; ++i)
				chip_cgpu = chip_cgpu->next_proc;
		}
		chip_thr = chip_cgpu->thr[0];
		
		applog(LOG_DEBUG, "%"PRIpreprv": Queue result: %s", chip_cgpu->proc_repr, buf);
		
		if (unlikely(!thiswork))
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to find work for queue results: %s", chip_cgpu->proc_repr, buf);
			inc_hw_errors_only(chip_thr);
			goto next_qline;
		}
		
		if (unlikely(!end[0]))
		{
			applog(LOG_ERR, "%"PRIpreprv": Missing nonce count in queue results: %s", chip_cgpu->proc_repr, buf);
			goto finishresult;
		}
		if (strtol(&end[1], &end, 10))
		{
			if (unlikely(!end[0]))
			{
				applog(LOG_ERR, "%"PRIpreprv": Missing nonces in queue results: %s", chip_cgpu->proc_repr, buf);
				goto finishresult;
			}
			bitforce_process_result_nonces(chip_thr, work, &end[1]);
		}
		++fcount;
		++counts[chipno];
		
finishresult:
		if (data->parallel == 1)
		{

		// Queue results are in order, so anything queued prior this is lost
		// Delete all queued work up to, and including, this one
		DL_FOREACH_SAFE(thr->work_list, work, tmpwork)
		{
			work_list_del(&thr->work_list, work);
			--data->queued;
			if (work == thiswork)
				break;
		}

		}
		else
		{
			// Parallel processors means the results might not be in order
			// This could leak if jobs get lost, hence the sanity checks using "ZqX"
			work_list_del(&thr->work_list, thiswork);
			--data->queued;
		}
next_qline: (void)0;
	}
	
	bitforce_set_queue_full(thr);
	
	if (count >= BITFORCE_MAX_QRESULTS)
		goto again;
	
	if (data->parallel == 1 && (
	        (fcount < BITFORCE_GOAL_QRESULTS && bitforce->sleep_ms < BITFORCE_MAX_QRESULT_WAIT && data->queued > 1)
	     || (fcount > BITFORCE_GOAL_QRESULTS && bitforce->sleep_ms > BITFORCE_MIN_QRESULT_WAIT)  ))
	{
		unsigned int old_sleep_ms = bitforce->sleep_ms;
		bitforce->sleep_ms = (uint32_t)bitforce->sleep_ms * BITFORCE_GOAL_QRESULTS / (fcount ?: 1);
		if (bitforce->sleep_ms > BITFORCE_MAX_QRESULT_WAIT)
			bitforce->sleep_ms = BITFORCE_MAX_QRESULT_WAIT;
		if (bitforce->sleep_ms < BITFORCE_MIN_QRESULT_WAIT)
			bitforce->sleep_ms = BITFORCE_MIN_QRESULT_WAIT;
		applog(LOG_DEBUG, "%"PRIpreprv": Received %d queue results after %ums; Wait time changed to: %ums (queued<=%d)",
		       bitforce->proc_repr, fcount, old_sleep_ms, bitforce->sleep_ms, data->queued);
	}
	else
		applog(LOG_DEBUG, "%"PRIpreprv": Received %d queue results after %ums; Wait time unchanged (queued<=%d)",
		       bitforce->proc_repr, fcount, bitforce->sleep_ms, data->queued);
	
	cgtime(&tv_now);
	timersub(&tv_now, &data->tv_hashmeter_start, &tv_elapsed);
	chip_cgpu = bitforce;
	for (int i = 0; i < data->parallel; ++i, (chip_cgpu = chip_cgpu->next_proc))
	{
		chip_thr = chip_cgpu->thr[0];
		hashes_done(chip_thr, (uint64_t)bitforce->nonces * counts[i], &tv_elapsed, NULL);
	}
	data->tv_hashmeter_start = tv_now;
	
	return true;
}

static
bool bitforce_queue_append(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	bool rv, ndq;
	
	bitforce_set_queue_full(thr);
	rv = !thr->queue_full;
	if (rv)
	{
		DL_APPEND(thr->work_list, work);
		++data->ready_to_queue;
		applog(LOG_DEBUG, "%"PRIpreprv": Appending to driver queue (max=%u, ready=%d, queued<=%d)",
		       bitforce->proc_repr,
		       (unsigned)data->queued_max, data->ready_to_queue, data->queued);
		bitforce_set_queue_full(thr);
	}
	else
	if (!data->ready_to_queue)
		return rv;
	
	ndq = !data->queued;
	if ((ndq)              // Device is idle
	 || (data->ready_to_queue >= BITFORCE_MAX_BQUEUE_AT_ONCE)  // ...or 5 items ready to go
	 || (thr->queue_full)            // ...or done filling queue
	 || (data->just_flushed)         // ...or queue was just flushed (only remaining job is partly done already)
	)
	{
		if (!bitforce_send_queue(thr))
		{
			// Problem sending queue, retry again in a few seconds
			applog(LOG_ERR, "%"PRIpreprv": Failed to send queue", bitforce->proc_repr);
			inc_hw_errors_only(thr);
			data->want_to_send_queue = true;
		}
	}
	
	return rv;
}

struct _jobinfo {
	uint8_t key[32+12];
	int instances;
	UT_hash_handle hh;
};

static
void bitforce_queue_flush(struct thr_info *thr)
{
	struct bitforce_proc_data *procdata = thr->cgpu_data;
	if (!procdata->handles_board)
		return;
	
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	char *buf = &data->noncebuf[0], *buf2 = NULL;
	const char *cmd = "ZqX";
	unsigned flushed;
	struct _jobinfo *processing = NULL, *item, *this;
	
	if (data->parallel == 1)
		// Pre-parallelization neither needs nor supports "ZqX"
		cmd = "ZQX";
	// TODO: Call "ZQX" most of the time: don't need to do sanity checks so often
	bitforce_zox(thr, cmd);
	if (!strncasecmp(buf, "OK:FLUSHED", 10))
		flushed = atoi(&buf[10]);
	else
	if ((!strncasecmp(buf, "COUNT:", 6)) && (buf2 = strstr(buf, "FLUSHED:")) )
	{
		flushed = atoi(&buf2[8]);
		buf2 = next_line(buf2);
	}
	else
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Failed to flush device queue: %s", bitforce->proc_repr, buf);
		flushed = 0;
	}
	
	data->queued -= flushed;
	
	applog(LOG_DEBUG, "%"PRIpreprv": Flushed %u jobs from device and %d from driver (queued<=%d)",
	       bitforce->proc_repr, flushed, data->ready_to_queue, data->queued);
	
	flushed += data->ready_to_queue;
	data->ready_to_queue = 0;
	while (flushed--)
		work_list_del(&thr->work_list, thr->work_list->prev);
	bitforce_set_queue_full(thr);
	data->just_flushed = true;
	data->want_to_send_queue = false;
	
	// "ZqX" returns jobs in progress, allowing us to sanity check
	// NOTE: Must process buffer into hash table BEFORE calling bitforce_queue_do_results, which clobbers it
	// NOTE: Must do actual sanity check AFTER calling bitforce_queue_do_results, to ensure we don't delete completed jobs
	if (buf2)
	{
		// First, turn buf2 into a hash
		for ( ; buf2[0]; buf2 = next_line(buf2))
		{
			this = malloc(sizeof(*this));
			hex2bin(&this->key[ 0], &buf2[ 0], 32);
			hex2bin(&this->key[32], &buf2[65], 12);
			HASH_FIND(hh, processing, &this->key[0], sizeof(this->key), item);
			if (likely(!item))
			{
				this->instances = 1;
				HASH_ADD(hh, processing, key, sizeof(this->key), this);
			}
			else
			{
				// This should really only happen in testing/benchmarking...
				++item->instances;
				free(this);
			}
		}
	}
	
	bitforce_queue_do_results(thr);
	
	if (buf2)
	{
		struct work *work, *tmp;
		uint8_t key[32+12];
		
		// Now iterate over the work_list and delete anything not in the hash
		DL_FOREACH_SAFE(thr->work_list, work, tmp)
		{
			memcpy(&key[ 0],  work->midstate, 32);
			memcpy(&key[32], &work->data[64], 12);
			HASH_FIND(hh, processing, &key[0], sizeof(key), item);
			if (unlikely(!item))
			{
				char hex[89];
				bin2hex(hex, key, 32+12);
				applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device is missing queued job! %s", bitforce->proc_repr, hex);
				work_list_del(&thr->work_list, work);
				continue;
			}
			if (likely(!--item->instances))
			{
				HASH_DEL(processing, item);
				free(item);
			}
		}
		if (unlikely( (flushed = HASH_COUNT(processing)) ))
		{
			//applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device is working on %d unknown jobs!", bitforce->proc_repr, flushed);
			// FIXME: Probably these were jobs finished after ZqX, included in the result check we just did
			// NOTE: We need to do that result check first to avoid deleting work_list items for things just solved
			HASH_ITER(hh, processing, item, this)
			{
				HASH_DEL(processing, item);
				free(item);
			}
		}
	}
}

static
void bitforce_queue_poll(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	unsigned long sleep_us;
	
	if (data->queued)
		bitforce_queue_do_results(thr);
	sleep_us = (unsigned long)bitforce->sleep_ms * 1000;
	
	if (data->want_to_send_queue)
		if (!bitforce_send_queue(thr))
			if (!data->queued)
			{
				applog(LOG_ERR, "%"PRIpreprv": Failed to send queue, and queue empty; retrying after 1 second", bitforce->proc_repr);
				inc_hw_errors_only(thr);
				sleep_us = 1000000;
			}
	
	timer_set_delay_from_now(&thr->tv_poll, sleep_us);
}

static void bitforce_queue_thread_deven(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu, *thisbf;
	struct bitforce_data *data = bitforce->device_data;
	struct thr_info *thisthr;
	
	for (thisbf = bitforce; thisbf && thisbf->device_data == data; thisbf = thisbf->next_proc)
	{
		thisthr = bitforce->thr[0];
		
		thisthr->pause = thr->pause;
		thisbf->deven = bitforce->deven;
	}
}

static void bitforce_queue_thread_disable(struct thr_info *thr)
{
	// Disable other threads sharing the same queue
	bitforce_queue_thread_deven(thr);
}

static void bitforce_queue_thread_enable(struct thr_info *thr)
{
	// TODO: Maybe reinit?
	
	// Enable other threads sharing the same queue
	bitforce_queue_thread_deven(thr);
}

struct device_drv bitforce_queue_api = {
	.dname = "bitforce_queue",
	.name = "BFL",
	.minerloop = minerloop_queue,
	.reinit_device = bitforce_reinit,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitforce_wlogprint_status,
	.proc_tui_wlogprint_choices = bitforce_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitforce_tui_handle_choice,
#endif
	.get_api_stats = bitforce_drv_stats,
	.get_stats = bitforce_get_stats,
	.set_device = bitforce_set_device,
	.identify_device = bitforce_identify,
	.thread_prepare = bitforce_thread_prepare,
	.thread_init = bitforce_thread_init,
	.queue_append = bitforce_queue_append,
	.queue_flush = bitforce_queue_flush,
	.poll = bitforce_queue_poll,
	.thread_shutdown = bitforce_shutdown,
	.thread_disable = bitforce_queue_thread_disable,
	.thread_enable = bitforce_queue_thread_enable,
};
