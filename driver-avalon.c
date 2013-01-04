/*
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 2013 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * Those code should be works fine with V2 and V3 bitstream of Avalon.
 * Operation:
 *   No detection implement.
 *   Input: 64B = 32B midstate + 20B fill bytes + last 12 bytes of block head.
 *   Return: send back 32bits immediately when Avalon found a valid nonce.
 *           no query protocol implemented here, if no data send back in ~11.3
 *           seconds (full cover time on 32bit nonce range by 380MH/s speed)
 *           just send another work.
 * Notice:
 *   1. Avalon will start calculate when you push a work to them, even they
 *      are busy.
 *   2. The 2 FPGAs on Avalon will distribute the job, one will calculate the
 *      0 ~ 7FFFFFFF, another one will cover the 80000000 ~ FFFFFFFF.
 *   3. It's possible for 2 FPGAs both find valid nonce in the meantime, the 2
 *      valid nonce will all be send back.
 *   4. Avalon will stop work when: a valid nonce has been found or 32 bits
 *      nonce range is completely calculated.
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

static struct timeval history_sec = { HISTORY_SEC, 0 };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_LONG_STR = "long";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

// One for each possible device
static struct AVALON_INFO **avalon_info;

// Looking for options in --avalon-timing and --avalon-options:
//
// Code increments this each time we start to look at a device
// However, this means that if other devices are checked by
// the Avalon code (e.g. BFL) they will count in the option offset
//
// This, however, is deterministic so that's OK
//
// If we were to increment after successfully finding an Avalon
// that would be random since an Avalon may fail and thus we'd
// not be able to predict the option order
//
// This also assumes that serial_detect() checks them sequentially
// and in the order specified on the command line
//
static int option_offset = -1;

struct device_api avalon_api;

static void rev(uint8_t *s, size_t l)
{
	size_t i, j;
	uint8_t t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

static inline void avalon_create_task(uint8_t *ob_bin, struct work *work)
{
	memset(ob_bin, 0, sizeof(ob_bin));
	memcpy(ob_bin, work->midstate, 32);
	memcpy(ob_bin + 52, work->data + 64, 12);
	rev(ob_bin, 32);
	rev(ob_bin + 52, 12);
}

static int avalon_gets(uint8_t *buf, int fd, struct timeval *tv_finish,
		       struct thr_info *thr, int read_count)
{
	ssize_t ret = 0;
	int rc = 0;
	int read_amount = AVALON_READ_SIZE;
	bool first = true;

	/* FIXME: we should set RTS to 0 and CTS to be 1, before read? */
	int done = avalon_task_done(fd);
	if (opt_debug)
		applog(LOG_DEBUG, "Avalon: finished all task?: %d", done);
	if (done) {
		;/* TODO: return here. and tell avalon all task are done */
	}
	// Read reply 1 byte at a time to get earliest tv_finish
	while (true) {
		/* FIXME: should be remove!!! */
		if (opt_debug) {
			applog(LOG_DEBUG,
			       "Avalon Read: times: %d, %.2f", rc,
			       (float)rc/(float)TIME_FACTOR);
		}

		ret = read(fd, buf, 1);
		if (ret < 0)
			return AVA_GETS_ERROR;

		if (first)
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
				applog(LOG_DEBUG,
				       "Avalon Read: No data in %.2f seconds",
				       (float)rc/(float)TIME_FACTOR);
			}
			return AVA_GETS_TIMEOUT;
		}

		if (thr && thr->work_restart) {
			if (opt_debug) {
				applog(LOG_DEBUG,
				       "Avalon Read: Work restart at %.2f seconds",
				       (float)(rc)/(float)TIME_FACTOR);
			}
			return AVA_GETS_RESTART;
		}
	}
}

static int avalon_get_result(uint8_t *nonce_bin, int fd,
			    struct timeval *tv_finish, struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct AVALON_INFO *info = avalon_info[avalon->device_id];
	int ret;

	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = avalon_gets(nonce_bin, fd, tv_finish, thr, info->read_count);

	return ret;
}

static int avalon_decode_nonce(struct work **work, uint32_t *nonce,
			       uint8_t *nonce_bin)
{
	/* FIXME: Avalon return: reserved_nonce_midstate_data, */

	/* FIXME: should be modify to avalon data format */
	memcpy((uint8_t *)nonce, nonce_bin, sizeof(nonce_bin));
#if !defined (__BIG_ENDIAN__) && !defined(MIPSEB)
	*nonce = swab32(*nonce);
#endif

	/* TODO: find the nonce work, return index */
	return 0;
}

static int avalon_send_task(int fd, const void *buf, size_t bufLen)
{
	size_t ret;

	/* FIXME: we should set RTS to 1 and wait CTS became 1, before write? */
	int empty = avalon_buffer_empty(fd);
	if (empty < 0)
		return AVA_SEND_ERROR;

	if (!empty) {
		/* FIXME: the buffer was full; return AVA_SEND_FULL; */
	}

	ret = write(fd, buf, bufLen);
	if (unlikely(ret != bufLen))
		return AVA_SEND_ERROR;

	/* From the document. avalon needs some time space between two write */
	struct timespec p;
	p.tv_sec = 0;
	p.tv_nsec = 5 * 1000;
	nanosleep(&p, NULL);

	return AVA_SEND_OK;
}

#define avalon_close(fd) close(fd)

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
		info->Hs = AVALON_REV3_HASH_TIME;
		info->read_count = AVALON_READ_COUNT_TIMING;

		info->timing_mode = MODE_SHORT;
		info->do_avalon_timing = true;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		info->Hs = AVALON_REV3_HASH_TIME;
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
		// Anything else in buf just uses DEFAULT mode
		info->Hs = AVALON_REV3_HASH_TIME;
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

	// yes we can calculate these,
	// but this way it's easy to see what they are
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
					// default to the same
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
	// Block 171874 nonce = (0xa2870100) = 0x000187a2
	// N.B. golden_ob MUST take less time to calculate
	//	than the timeout set in avalon_open()
	//	This one takes ~0.53ms on Rev3 Avalon
	const char golden_ob[] =
		"4679ba4ec99876bf4bfe086082b40025"
		"4df6c356451471139a3afa71e48f544a"
		"00000000000000000000000000000000"
		"0000000087320b1a1426674f2fa722ce";

	const char golden_nonce[] = "000187a2";
	const uint32_t golden_nonce_val = 0x000187a2;

	uint8_t ob_bin[64], nonce_bin[AVALON_READ_SIZE];
	char *nonce_hex;

	struct AVALON_INFO *info;
	struct timeval tv_start, tv_finish;
	int fd;

	int baud, work_division, asic_count;
	int this_option_offset = ++option_offset;
	get_options(this_option_offset, &baud, &work_division, &asic_count);

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s", devpath);
	fd = avalon_open2(devpath, baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		return false;
	}

	hex2bin(ob_bin, golden_ob, sizeof(ob_bin));
	avalon_send_task(fd, ob_bin, sizeof(ob_bin));
	gettimeofday(&tv_start, NULL);

	memset(nonce_bin, 0, sizeof(nonce_bin));
	/* FIXME: how much time on avalon finish reset */ 
	avalon_gets(nonce_bin, fd, &tv_finish, NULL, 10/* set to 1s now */);

	avalon_close(fd);

	nonce_hex = bin2hex(nonce_bin, sizeof(nonce_bin));
	if (strncmp(nonce_hex, golden_nonce, 8)) {
		applog(LOG_ERR,
			"Avalon Detect: "
			"Test failed at %s: get %s, should: %s",
			devpath, nonce_hex, golden_nonce);
		free(nonce_hex);
		return false;
	}
	applog(LOG_DEBUG,
	       "Avalon Detect: Test succeeded at %s: got %s",
	       devpath, nonce_hex);
	free(nonce_hex);

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

	applog(LOG_INFO, "Found Avalon at %s, mark as %d",
	       devpath, avalon->device_id);

	applog(LOG_DEBUG,
	       "Avalon: Init: %d baud=%d work_division=%d asic_count=%d",
		avalon->device_id, baud, work_division, asic_count);

	// Since we are adding a new device on the end it
	// needs to always be allocated
	avalon_info[avalon->device_id] = (struct AVALON_INFO *)
		malloc(sizeof(struct AVALON_INFO));
	if (unlikely(!(avalon_info[avalon->device_id])))
		quit(1, "Failed to malloc AVALON_INFO");

	info = avalon_info[avalon->device_id];

	// Initialise everything to zero for a new device
	memset(info, 0, sizeof(struct AVALON_INFO));

	info->baud = baud;
	info->work_division = work_division;
	info->asic_count = asic_count;
	info->nonce_mask = mask(work_division);
	info->golden_hashes =
		(golden_nonce_val & info->nonce_mask) * asic_count;
	timersub(&tv_finish, &tv_start, &(info->golden_tv));

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
		applog(LOG_ERR, "Failed to open Avalon on %s",
		       avalon->device_path);
		return false;
	}
	avalon->device_fd = fd;

	applog(LOG_INFO, "Opened Avalon on %s", avalon->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(avalon->init, &now);

	return true;
}

static int64_t avalon_scanhash(struct thr_info *thr, struct work **work,
			       __maybe_unused int64_t max_nonce)
{
	struct cgpu_info *avalon;
	int fd;
	int ret;

	struct AVALON_INFO *info;

	uint8_t ob_bin[64], nonce_bin[AVALON_READ_SIZE];
	char *ob_hex;
	uint32_t nonce;
	int64_t hash_count;
	int i, work_i;
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
			// fail the device if the reopen attempt fails
			return -1;
		}
	fd = avalon->device_fd;
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif
	/* Write task to device one by one */
	for (i = 0; i < AVALON_GET_WORK_COUNT; i++) {
		avalon_create_task(ob_bin, work[i]);
		ret = avalon_send_task(fd, ob_bin, sizeof(ob_bin));
		if (opt_debug) {
			ob_hex = bin2hex(ob_bin, sizeof(ob_bin));
			applog(LOG_DEBUG, "Avalon %d sent: %s",
			       avalon->device_id, ob_hex);
			free(ob_hex);
		}
		if (ret == AVA_SEND_ERROR) {
			do_avalon_close(thr);
			applog(LOG_ERR, "AVA%i: Comms error",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;	/* This should never happen */
		}
	}

	elapsed.tv_sec = elapsed.tv_usec = 0;
	gettimeofday(&tv_start, NULL);

	/* count may != AVALON_GET_WORK_COUNT */
	for (i = 0; i < AVALON_GET_WORK_COUNT; i++) {
		ret = avalon_get_result(nonce_bin, fd, &tv_finish, thr);
		if (ret == AVA_GETS_ERROR ) {
			do_avalon_close(thr);
			applog(LOG_ERR, "AVA%i: Comms error", avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;
		}

		// aborted before becoming idle, get new work
		if (ret == AVA_GETS_TIMEOUT || ret == AVA_GETS_RESTART) {
			timersub(&tv_finish, &tv_start, &elapsed);

			// ONLY up to just when it aborted
			// We didn't read a reply so we don't subtract AVALON_READ_TIME
			estimate_hashes = ((double)(elapsed.tv_sec) +
					   ((double)(elapsed.tv_usec)) /
					   ((double)1000000)) / info->Hs;

			// If some Serial-USB delay allowed the full nonce range to
			// complete it can't have done more than a full nonce
			if (unlikely(estimate_hashes > 0xffffffff))
				estimate_hashes = 0xffffffff;

			if (opt_debug) {
				applog(LOG_DEBUG,
				       "Avalon %d no nonce = 0x%08llx hashes "
				       "(%ld.%06lds)",
				       avalon->device_id, estimate_hashes,
				       elapsed.tv_sec, elapsed.tv_usec);
			}

			return estimate_hashes;
		}

		work_i = avalon_decode_nonce(work, &nonce, nonce_bin);
		/* FIXME: Should be a check on return, no work_i maybe hardware error */
		work[work_i]->blk.nonce = 0xffffffff;
		curr_hw_errors = avalon->hw_errors;
		submit_nonce(thr, work[work_i], nonce);
		was_hw_error = (curr_hw_errors > avalon->hw_errors);

		// Force a USB close/reopen on any hw error
		if (was_hw_error)
			do_avalon_close(thr);

		hash_count = (nonce & info->nonce_mask);
		hash_count++;
		hash_count *= info->asic_count;
	}

	if (opt_debug || info->do_avalon_timing)
		timersub(&tv_finish, &tv_start, &elapsed);

	if (opt_debug) {
		applog(LOG_DEBUG,
		       "Avalon %d nonce = 0x%08x = 0x%08llx hashes "
		       "(%ld.%06lds)",
		       avalon->device_id, nonce, hash_count,
		       elapsed.tv_sec, elapsed.tv_usec);
	}

	// ignore possible end condition values ... and hw errors
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

			// Initialise history0 to zero for summary calculation
			memset(history0, 0, sizeof(struct AVALON_HISTORY));

			// We just completed a history data set
			// So now recalc read_count based on the
			// whole history thus we will
			// initially get more accurate until it
			// completes INFO_HISTORY
			// total data sets
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

			// All history data
			Hs = (history0->values*history0->sumXiTi - history0->sumXi*history0->sumTi)
				/ (history0->values*history0->sumXi2 - history0->sumXi*history0->sumXi);
			W = history0->sumTi/history0->values - Hs*history0->sumXi/history0->values;
			hash_count_range = history0->hash_count_max - history0->hash_count_min;
			values = history0->values;

			// Initialise history0 to zero for next data set
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

//			applog(LOG_WARNING, "Avalon %d Re-estimate: read_count=%d fullnonce=%fs history count=%d Hs=%e W=%e values=%d hash range=0x%08lx min data count=%u", avalon->device_id, read_count, fullnonce, count, Hs, W, values, hash_count_range, info->min_data_count);
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

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
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
	.get_api_stats = avalon_api_stats,
	.thread_prepare = avalon_prepare,
	.scanhash_queue = avalon_scanhash,
	.thread_shutdown = avalon_shutdown,
};
