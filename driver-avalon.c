/*
 * Copyright 2012 2013 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#ifndef WIN32
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include <windows.h>
  #include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "fpgautils.h"
#include "driver-avalon.h"
#include "hexdump.c"

static struct timeval history_sec = { HISTORY_SEC, 0 };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_LONG_STR = "long";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

static int option_offset = -1;

static struct AVALON_INFO **avalon_info;
struct device_api avalon_api;
static int avalon_init_task(struct avalon_task *at, uint8_t reset, uint8_t ff,
			    uint8_t fan, uint8_t timeout, uint8_t chip_num,
			    uint8_t miner_num)
{
	static bool first = true;

	if (!at)
		return -1;

	memset(at, 0, sizeof(struct avalon_task));

	if (reset) {
		at->reset = 1;
		first = true;
	}

	at->flush_fifo = (ff ? 1 : 0);
	at->fan_eft = (fan ? 1 : 0);

	if (timeout || chip_num || miner_num) {
		at->timer_eft = 1;
	}
	if (first && !at->reset) {
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = false;
	}

	at->fan_pwm_data = (fan ? fan : AVALON_DEFAULT_FAN_PWM);
	at->timeout_data = (timeout ? timeout : AVALON_DEFAULT_TIMEOUT);
	at->chip_num = (chip_num ? chip_num : AVALON_DEFAULT_CHIP_NUM);
	at->miner_num = (miner_num ? miner_num : AVALON_DEFAULT_MINER_NUM);

	at->nonce_elf = 1;

	return 0;
}

static inline void avalon_create_task(struct avalon_task *at,
				      struct work *work)
{
	memcpy(at->midstate, work->midstate, 32);
	memcpy(at->data, work->data + 64, 12);
}

static int avalon_send_task(int fd, const struct avalon_task *at)
{
	size_t ret;
	int full;
	struct timespec p;
	uint8_t *buf;
	int nr_len;

	nr_len = AVALON_WRITE_SIZE + 4 * at->chip_num;
	buf = calloc(1, AVALON_WRITE_SIZE + nr_len);
	if (!buf)
		return AVA_SEND_ERROR;

	memcpy(buf, at, AVALON_WRITE_SIZE);
#if defined(__BIG_ENDIAN__) || defined(MIPSEB)
	uint8_t tt = 0;
	tt = (buf[0] & 0x0f) << 4;
	tt |= ((buf[0] & 0x10) ? (1 << 3) : 0);
	tt |= ((buf[0] & 0x20) ? (1 << 2) : 0);
	tt |= ((buf[0] & 0x40) ? (1 << 1) : 0);
	tt |= ((buf[0] & 0x80) ? (1 << 0) : 0);
	buf[0] = tt;
	buf[4] = rev8(buf[4]);
#endif
	if (opt_debug) {
		applog(LOG_DEBUG, "Avalon: Sent(%d):", nr_len);
		hexdump((uint8_t *)buf, nr_len);
	}
	ret = write(fd, buf, nr_len);
	free(buf);
	if (unlikely(ret != nr_len))
		return AVA_SEND_ERROR;

	p.tv_sec = 0;
	p.tv_nsec = AVALON_SEND_WORK_PITCH;
	nanosleep(&p, NULL);

	full = avalon_buffer_full(fd);
	applog(LOG_DEBUG, "Avalon: Sent: Buffer full: %s",
	       ((full == AVA_BUFFER_FULL) ? "Yes" : "No"));

	if (full == AVA_BUFFER_EMPTY)
		return AVA_SEND_BUFFER_EMPTY;

	return AVA_SEND_BUFFER_FULL;
}

static int avalon_gets(int fd, uint8_t *buf, int read_count,
		       struct thr_info *thr, struct timeval *tv_finish)
{
	ssize_t ret = 0;
	int rc = 0;
	int read_amount = AVALON_READ_SIZE;
	bool first = true;

	/* Read reply 1 byte at a time to get earliest tv_finish */
	while (true) {
		ret = read(fd, buf, 1);
		if (ret < 0)
			return AVA_GETS_ERROR;

		if (first && tv_finish != NULL)
			gettimeofday(tv_finish, NULL);

		if (ret >= read_amount)
			return AVA_GETS_OK;

		if (ret > 0) {
			buf += ret;
			read_amount -= ret;
			first = false;
			continue;
		}

		rc++;
		if (rc >= read_count) {
			if (opt_debug) {
				applog(LOG_ERR,
				       "Avalon: No data in %.2f seconds",
				       (float)rc/(float)TIME_FACTOR);
			}
			return AVA_GETS_TIMEOUT;
		}

		if (thr && thr->work_restart) {
			if (opt_debug) {
				applog(LOG_ERR,
				       "Avalon: Work restart at %.2f seconds",
				       (float)(rc)/(float)TIME_FACTOR);
			}
			return AVA_GETS_RESTART;
		}
	}
}

static int avalon_get_result(int fd, struct avalon_result *ar,
			     struct thr_info *thr, struct timeval *tv_finish)
{
	struct cgpu_info *avalon;
	struct AVALON_INFO *info;
	uint8_t result[AVALON_READ_SIZE];
	int ret, read_count = 16;

	if (thr) {
		avalon = thr->cgpu;
		info = avalon_info[avalon->device_id];
		read_count = info->read_count;
	}

	memset(result, 0, AVALON_READ_SIZE);
	ret = avalon_gets(fd, result, read_count, thr, tv_finish);

	if (ret == AVA_GETS_OK) {
		if (opt_debug) {
			applog(LOG_DEBUG, "Avalon: get:");
			hexdump((uint8_t *)result, AVALON_READ_SIZE);
		}
		memcpy((uint8_t *)ar, result, AVALON_READ_SIZE);
	}

	return ret;
}

static int avalon_decode_nonce(struct work **work, struct avalon_result *ar,
			       uint32_t *nonce)
{
	int i;

	for (i = 0; i < AVALON_GET_WORK_COUNT; i++) {
		if (!work || !work[i])
			return -1;
	}

	*nonce = ar->nonce;
#if defined (__BIG_ENDIAN__) || defined(MIPSEB)
	*nonce = swab32(*nonce);
#endif

	for (i = 0; i < AVALON_GET_WORK_COUNT; i++) {
		if (!memcmp(ar->data, work[i]->data + 64, 12) && 
		    !memcmp(ar->midstate, work[i]->midstate, 32))
			break;
	}
	if (i == AVALON_GET_WORK_COUNT)
		return -1;

	applog(LOG_DEBUG, "Avalon: match to work: %d", i);
	return i;
}

static int avalon_reset(int fd)
{
	struct avalon_task at;
	struct avalon_result ar;
	uint8_t *buf;
	int ret, i;
	struct timespec p;

	avalon_init_task(&at,
			 1,
			 0,
			 AVALON_DEFAULT_FAN_PWM,
			 AVALON_DEFAULT_TIMEOUT,
			 AVALON_DEFAULT_CHIP_NUM,
			 AVALON_DEFAULT_MINER_NUM);
	ret = avalon_send_task(fd, &at);
	if (ret == AVA_SEND_ERROR)
		return 1;

	avalon_get_result(fd, &ar, NULL, NULL);

	buf = (uint8_t *)&ar;
	for (i = 0; i < 11; i++)
		if (buf[i] != 0)
			break;
	/* FIXME: add more avalon info base on return */

	if (i != 11) {
		applog(LOG_ERR, "Avalon: Reset failed! not a Avalon?");
		return 1;
	}

	p.tv_sec = 1;
	p.tv_nsec = AVALON_RESET_PITCH;
	nanosleep(&p, NULL);

	applog(LOG_ERR, "Avalon: Reset succeeded");
	return 0;
}

static void do_avalon_close(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	avalon_close(avalon->device_fd);
	avalon->device_fd = -1;
}

static const char *timing_mode_str(enum timing_mode timing_mode)
{
	switch(timing_mode) {
	case MODE_DEFAULT:
		return MODE_DEFAULT_STR;
	case MODE_SHORT:
		return MODE_SHORT_STR;
	case MODE_LONG:
		return MODE_LONG_STR;
	case MODE_VALUE:
		return MODE_VALUE_STR;
	default:
		return MODE_UNKNOWN_STR;
	}
}

static void set_timing_mode(int this_option_offset, struct cgpu_info *avalon)
{
	struct AVALON_INFO *info = avalon_info[avalon->device_id];
	double Hs;
	char buf[BUFSIZ+1];
	char *ptr, *comma, *eq;
	size_t max;
	int i;

	if (opt_icarus_timing == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_icarus_timing;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	info->Hs = 0;
	info->read_count = 0;

	if (strcasecmp(buf, MODE_SHORT_STR) == 0) {
		info->Hs = AVALON_HASH_TIME;
		info->read_count = AVALON_READ_COUNT_TIMING;

		info->timing_mode = MODE_SHORT;
		info->do_avalon_timing = true;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		info->Hs = AVALON_HASH_TIME;
		info->read_count = AVALON_READ_COUNT_TIMING;

		info->timing_mode = MODE_LONG;
		info->do_avalon_timing = true;
	} else if ((Hs = atof(buf)) != 0) {
		info->Hs = Hs / NANOSEC;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		if ((eq = strchr(buf, '=')) != NULL)
			info->read_count = atoi(eq+1);

		if (info->read_count < 1)
			info->read_count =
				(int)(info->fullnonce * TIME_FACTOR) - 1;

		if (unlikely(info->read_count < 1))
			info->read_count = 1;

		info->timing_mode = MODE_VALUE;
		info->do_avalon_timing = false;
	} else {
		/* Anything else in buf just uses DEFAULT mode */
		info->Hs = AVALON_HASH_TIME;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		if ((eq = strchr(buf, '=')) != NULL)
			info->read_count = atoi(eq+1);

		if (info->read_count < 1)
			info->read_count =
				(int)(info->fullnonce * TIME_FACTOR) - 1;

		info->timing_mode = MODE_DEFAULT;
		info->do_avalon_timing = false;
	}

	info->min_data_count = MIN_DATA_COUNT;

	applog(LOG_DEBUG, "Avalon: Init: %d mode=%s read_count=%d Hs=%e",
	       avalon->device_id, timing_mode_str(info->timing_mode),
	       info->read_count, info->Hs);
}

static uint32_t mask(int work_division)
{
	char err_buf[BUFSIZ+1];
	uint32_t nonce_mask = 0x7fffffff;

	switch (work_division) {
	case 1:
		nonce_mask = 0xffffffff;
		break;
	case 2:
		nonce_mask = 0x7fffffff;
		break;
	case 4:
		nonce_mask = 0x3fffffff;
		break;
	case 8:
		nonce_mask = 0x1fffffff;
		break;
	default:
		sprintf(err_buf,
			"Invalid2 avalon-options for work_division (%d)"
			" must be 1, 2, 4 or 8", work_division);
		quit(1, err_buf);
	}

	return nonce_mask;
}

static void get_options(int this_option_offset, int *baud, int *work_division,
			int *asic_count)
{
	char err_buf[BUFSIZ+1];
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2;
	size_t max;
	int i, tmp;

	if (opt_icarus_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_icarus_options;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	*baud = AVALON_IO_SPEED;
	*work_division = 2;
	*asic_count = 2;

	if (*buf) {
		colon = strchr(buf, ':');
		if (colon)
			*(colon++) = '\0';

		if (*buf) {
			tmp = atoi(buf);
			switch (tmp) {
			case 115200:
				*baud = 115200;
				break;
			case 57600:
				*baud = 57600;
				break;
			default:
				sprintf(err_buf,
					"Invalid avalon-options for baud (%s) "
					"must be 115200 or 57600", buf);
				quit(1, err_buf);
			}
		}

		if (colon && *colon) {
			colon2 = strchr(colon, ':');
			if (colon2)
				*(colon2++) = '\0';

			if (*colon) {
				tmp = atoi(colon);
				if (tmp == 1 || tmp == 2 ||
				    tmp == 4 || tmp == 8) {
					*work_division = tmp;
					*asic_count = tmp;
				} else {
					sprintf(err_buf,
						"Invalid avalon-options for "
						"work_division (%s) must be 1,"
						" 2, 4 or 8", colon);
					quit(1, err_buf);
				}
			}

			if (colon2 && *colon2) {
				tmp = atoi(colon2);
				if (tmp > 0 && tmp <= *work_division)
					*asic_count = tmp;
				else {
					sprintf(err_buf,
						"Invalid avalon-options for "
						"asic_count (%s) must be >0 "
						"and <=work_division (%d)",
						colon2, *work_division);
					quit(1, err_buf);
				}
			}
		}
	}
}

static bool avalon_detect_one(const char *devpath)
{
	struct AVALON_INFO *info;
	int fd, ret;
	int baud, work_division, asic_count;

	int this_option_offset = ++option_offset;
	get_options(this_option_offset, &baud, &work_division, &asic_count);

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s", devpath);
	fd = avalon_open2(devpath, baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		return false;
	}

	ret = avalon_reset(fd);
	avalon_close(fd);

	if (ret)
		return false;

	/* We have a real Avalon! */
	struct cgpu_info *avalon;
	avalon = calloc(1, sizeof(struct cgpu_info));
	avalon->api = &avalon_api;
	avalon->device_path = strdup(devpath);
	avalon->device_fd = -1;
	avalon->threads = AVALON_MINER_THREADS;
	add_cgpu(avalon);
	avalon_info = realloc(avalon_info,
			      sizeof(struct AVALON_INFO *) *
			      (total_devices + 1));

	applog(LOG_INFO, "Avalon Detect: Found at %s, mark as %d",
	       devpath, avalon->device_id);

	applog(LOG_DEBUG,
	       "Avalon: Init: %d baud=%d work_division=%d asic_count=%d",
		avalon->device_id, baud, work_division, asic_count);

	avalon_info[avalon->device_id] = (struct AVALON_INFO *)
		malloc(sizeof(struct AVALON_INFO));
	if (unlikely(!(avalon_info[avalon->device_id])))
		quit(1, "Failed to malloc AVALON_INFO");

	info = avalon_info[avalon->device_id];

	memset(info, 0, sizeof(struct AVALON_INFO));

	info->baud = baud;
	info->work_division = work_division;
	info->asic_count = asic_count;
	info->nonce_mask = mask(work_division);

	set_timing_mode(this_option_offset, avalon);

	return true;
}

static inline void avalon_detect()
{
	serial_detect(&avalon_api, avalon_detect_one);
}

static bool avalon_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct timeval now;
	int fd;

	avalon->device_fd = -1;
	fd = avalon_open(avalon->device_path,
			     avalon_info[avalon->device_id]->baud);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon: Failed to open on %s",
		       avalon->device_path);
		return false;
	}
	avalon_reset(fd);
	avalon->device_fd = fd;

	applog(LOG_INFO, "Avalon: Opened on %s", avalon->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(avalon->init, &now);

	return true;
}

static void avalon_free_work(struct work **work)
{
	int i;

	if (!work)
		return;

	for (i = 0; i < AVALON_GET_WORK_COUNT; i++)
		if (work[i])
			free_work(work[i++]);
}
static int64_t avalon_scanhash(struct thr_info *thr, struct work **bulk_work,
			       __maybe_unused int64_t max_nonce)
{
	struct cgpu_info *avalon;
	int fd;
	int ret;
	int full;

	struct AVALON_INFO *info;
	struct avalon_task at;
	struct avalon_result ar;
	static struct work *bulk0[3] = {NULL, NULL, NULL};
	static struct work *bulk1[3] = {NULL, NULL, NULL};
	static struct work *bulk2[3] = {NULL, NULL, NULL};
	struct work **work = NULL;
	int i, work_i0, work_i1, work_i2;

	uint32_t nonce;
	int64_t hash_count;
	int read_count;
	int count;
	struct timeval tv_start, tv_finish, elapsed;
	struct timeval tv_history_start, tv_history_finish;
	double Ti, Xi;

	int curr_hw_errors;
	bool was_hw_error;

	struct AVALON_HISTORY *history0, *history;
	double Hs, W, fullnonce;
	int64_t estimate_hashes;
	uint32_t values;
	int64_t hash_count_range;

	avalon = thr->cgpu;
	info = avalon_info[avalon->device_id];

	if (avalon->device_fd == -1)
		if (!avalon_prepare(thr)) {
			applog(LOG_ERR, "AVA%i: Comms error",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			/* fail the device if the reopen attempt fails */
			return -1;
		}
	fd = avalon->device_fd;
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif
	work = bulk_work;
	for (i = 0; i < AVALON_GET_WORK_COUNT; i++) {
		bulk0[i] = bulk1[i];
		bulk1[i] = bulk2[i];
		bulk2[i] = bulk_work[i];
	}

	i = 0;
	while (true) {
		avalon_init_default_task(&at);
		avalon_create_task(&at, work[i]);
		ret = avalon_send_task(fd, &at);
		if (ret == AVA_SEND_ERROR) {
			avalon_free_work(bulk0);
			avalon_free_work(bulk1);
			avalon_free_work(bulk2);
			do_avalon_close(thr);
			applog(LOG_ERR, "AVA%i: Comms error",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;	/* This should never happen */
		}

		work[i]->blk.nonce = 0xffffffff;

		if (ret == AVA_SEND_BUFFER_FULL)
			break;

		i++;
		if (i == AVALON_GET_WORK_COUNT &&
		    ret != AVA_SEND_BUFFER_FULL) {
			return 0xffffffff;
		}
	}

	elapsed.tv_sec = elapsed.tv_usec = 0;
	gettimeofday(&tv_start, NULL);

	/* count may != AVALON_GET_WORK_COUNT */
	while(true) {
		full = avalon_buffer_full(fd);
		applog(LOG_DEBUG, "Avalon: Buffer full: %s",
		       ((full == AVA_BUFFER_FULL) ? "Yes" : "No"));
		if (full == AVA_BUFFER_EMPTY) {
			applog(LOG_DEBUG, "Avalon: Finished bulk task!");
			break;
		}

		work_i0 = work_i1 = work_i2 = -1;
		ret = avalon_get_result(fd, &ar, thr, &tv_finish);
		if (ret == AVA_GETS_ERROR) {
			avalon_free_work(bulk0);
			avalon_free_work(bulk1);
			avalon_free_work(bulk2);
			do_avalon_close(thr);
			applog(LOG_ERR,
			       "AVA%i: Comms error", avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;
		}
		/* aborted before becoming idle, get new work */
		if (ret == AVA_GETS_TIMEOUT || ret == AVA_GETS_RESTART) {
			timersub(&tv_finish, &tv_start, &elapsed);

			estimate_hashes = ((double)(elapsed.tv_sec) +
					   ((double)(elapsed.tv_usec)) /
					   ((double)1000000)) / info->Hs;

			/* If Serial-USB delay allowed the full nonce range to
			 * complete it can't have done more than a full nonce
			 */
			if (unlikely(estimate_hashes > 0xffffffff))
				estimate_hashes = 0xffffffff;

			applog(LOG_DEBUG,
			       "Avalon: no nonce = 0x%08llx hashes "
			       "(%ld.%06lds)",
			       estimate_hashes, elapsed.tv_sec,
			       elapsed.tv_usec);

			continue;
			//return estimate_hashes;
		}
		work_i0 = avalon_decode_nonce(bulk0, &ar, &nonce);
		if (work_i0 < 0)
			applog(LOG_DEBUG,
			       "Avalon: can not match nonce to bulk0");
		work_i1 = avalon_decode_nonce(bulk1, &ar, &nonce);
		if (work_i1 < 0)
			applog(LOG_DEBUG,
			       "Avalon: can not match nonce to bulk1");
		work_i2 = avalon_decode_nonce(bulk2, &ar, &nonce);
		if (work_i2 < 0)
			applog(LOG_DEBUG,
			       "Avalon: can not match nonce to bulk2");

		curr_hw_errors = avalon->hw_errors;
		if (work_i0 >= 0)
			submit_nonce(thr, bulk0[work_i0], nonce);
		if (work_i1 >= 0)
			submit_nonce(thr, bulk1[work_i1], nonce);
		if (work_i2 >= 0)
			submit_nonce(thr, bulk2[work_i2], nonce);

		was_hw_error = (curr_hw_errors > avalon->hw_errors);

		/* Force a USB close/reopen on any hw error */
		if (was_hw_error)
			do_avalon_close(thr);

		hash_count = (nonce & info->nonce_mask);
		hash_count++;
		hash_count *= info->asic_count;
	}
	avalon_free_work(bulk0);

	if (opt_debug || info->do_avalon_timing)
		timersub(&tv_finish, &tv_start, &elapsed);

	if (opt_debug) {
		applog(LOG_DEBUG,
		       "Avalon: nonce = 0x%08x = 0x%08llx hashes "
		       "(%ld.%06lds)",
		       nonce, hash_count, elapsed.tv_sec, elapsed.tv_usec);
	}

	/* ignore possible end condition values ... and hw errors */
	if (info->do_avalon_timing
	    && !was_hw_error
	    && ((nonce & info->nonce_mask) > END_CONDITION)
	    && ((nonce & info->nonce_mask) <
		(info->nonce_mask & ~END_CONDITION))) {
		gettimeofday(&tv_history_start, NULL);

		history0 = &(info->history[0]);

		if (history0->values == 0)
			timeradd(&tv_start, &history_sec, &(history0->finish));

		Ti = (double)(elapsed.tv_sec)
			+ ((double)(elapsed.tv_usec))/((double)1000000)
			- ((double)AVALON_READ_TIME(info->baud));
		Xi = (double)hash_count;
		history0->sumXiTi += Xi * Ti;
		history0->sumXi += Xi;
		history0->sumTi += Ti;
		history0->sumXi2 += Xi * Xi;

		history0->values++;

		if (history0->hash_count_max < hash_count)
			history0->hash_count_max = hash_count;
		if (history0->hash_count_min > hash_count ||
		    history0->hash_count_min == 0)
			history0->hash_count_min = hash_count;

		if (history0->values >= info->min_data_count
		    &&  timercmp(&tv_start, &(history0->finish), >)) {
			for (i = INFO_HISTORY; i > 0; i--)
				memcpy(&(info->history[i]),
				       &(info->history[i-1]),
				       sizeof(struct AVALON_HISTORY));

			/* Init history0 to zero for summary calculation */
			memset(history0, 0, sizeof(struct AVALON_HISTORY));

			/* We just completed a history data set
			 * So now recalc read_count based on the
			 * whole history thus we will
			 * initially get more accurate until it
			 * completes INFO_HISTORY
			 * total data sets */
			count = 0;
			for (i = 1 ; i <= INFO_HISTORY; i++) {
				history = &(info->history[i]);
				if (history->values >= MIN_DATA_COUNT) {
					count++;

					history0->sumXiTi += history->sumXiTi;
					history0->sumXi += history->sumXi;
					history0->sumTi += history->sumTi;
					history0->sumXi2 += history->sumXi2;
					history0->values += history->values;

					if (history0->hash_count_max < history->hash_count_max)
						history0->hash_count_max = history->hash_count_max;
					if (history0->hash_count_min > history->hash_count_min || history0->hash_count_min == 0)
						history0->hash_count_min = history->hash_count_min;
				}
			}

			/* All history data */
			Hs = (history0->values*history0->sumXiTi - history0->sumXi*history0->sumTi)
				/ (history0->values*history0->sumXi2 - history0->sumXi*history0->sumXi);
			W = history0->sumTi/history0->values - Hs*history0->sumXi/history0->values;
			hash_count_range = history0->hash_count_max - history0->hash_count_min;
			values = history0->values;

			/* Initialise history0 to zero for next data set */
			memset(history0, 0, sizeof(struct AVALON_HISTORY));

			fullnonce = W + Hs * (((double)0xffffffff) + 1);
			read_count = (int)(fullnonce * TIME_FACTOR) - 1;

			info->Hs = Hs;
			info->read_count = read_count;

			info->fullnonce = fullnonce;
			info->count = count;
			info->W = W;
			info->values = values;
			info->hash_count_range = hash_count_range;

			if (info->min_data_count < MAX_MIN_DATA_COUNT)
				info->min_data_count *= 2;
			else if (info->timing_mode == MODE_SHORT)
				info->do_avalon_timing = false;

/*			applog(LOG_WARNING, "Avalon %d Re-estimate: read_count=%d fullnonce=%fs history count=%d Hs=%e W=%e values=%d hash range=0x%08lx min data count=%u", avalon->device_id, read_count, fullnonce, count, Hs, W, values, hash_count_range, info->min_data_count);*/
			applog(LOG_WARNING, "Avalon %d Re-estimate: Hs=%e W=%e read_count=%d fullnonce=%.3fs",
			       avalon->device_id, Hs, W, read_count, fullnonce);
		}
		info->history_count++;
		gettimeofday(&tv_history_finish, NULL);

		timersub(&tv_history_finish, &tv_history_start, &tv_history_finish);
		timeradd(&tv_history_finish, &(info->history_time), &(info->history_time));
	}

	return hash_count;
}

static struct api_data *avalon_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct AVALON_INFO *info = avalon_info[cgpu->device_id];

	/* Warning, access to these is not locked - but we don't really
	 * care since hashing performance is way more important than
	 * locking access to displaying API debug 'stats'
	 * If locking becomes an issue for any of them, use copy_data=true also */
	root = api_add_int(root, "read_count", &(info->read_count), false);
	root = api_add_double(root, "fullnonce", &(info->fullnonce), false);
	root = api_add_int(root, "count", &(info->count), false);
	root = api_add_hs(root, "Hs", &(info->Hs), false);
	root = api_add_double(root, "W", &(info->W), false);
	root = api_add_uint(root, "total_values", &(info->values), false);
	root = api_add_uint64(root, "range", &(info->hash_count_range), false);
	root = api_add_uint64(root, "history_count", &(info->history_count),
			      false);
	root = api_add_timeval(root, "history_time", &(info->history_time),
			       false);
	root = api_add_uint(root, "min_data_count", &(info->min_data_count),
			    false);
	root = api_add_uint(root, "timing_values", &(info->history[0].values),
			    false);
	root = api_add_const(root, "timing_mode",
			     timing_mode_str(info->timing_mode), false);
	root = api_add_bool(root, "is_timing", &(info->do_avalon_timing),
			    false);
	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "work_division", &(info->work_division),
			   false);
	root = api_add_int(root, "asic_count", &(info->asic_count), false);

	return root;
}

static void avalon_shutdown(struct thr_info *thr)
{
	do_avalon_close(thr);
}

struct device_api avalon_api = {
	.dname = "avalon",
	.name = "AVA",
	.api_detect = avalon_detect,
	.thread_prepare = avalon_prepare,
	.scanhash_queue = avalon_scanhash,
	.get_api_stats = avalon_api_stats,
	.thread_shutdown = avalon_shutdown,
};
