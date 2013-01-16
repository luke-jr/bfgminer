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

static int option_offset = -1;

struct avalon_info **avalon_info;
struct device_api avalon_api;
static int avalon_init_task(struct thr_info *thr, struct avalon_task *at,
			    uint8_t reset, uint8_t ff, uint8_t fan,
			    uint8_t timeout_p, uint8_t asic_num_p,
			    uint8_t miner_num_p)
{
	static bool first = true;

	uint8_t timeout;
	uint8_t asic_num;
	uint8_t miner_num;

	struct cgpu_info *avalon;
	struct avalon_info *info;

	if (!at)
		return -1;

	if (!thr && (timeout_p <= 0 || asic_num_p <= 0 || miner_num_p <= 0))
		return -1;

	timeout = timeout_p;
	miner_num = miner_num_p;
	asic_num = asic_num_p;

	if (thr) {
		avalon = thr->cgpu;
		info = avalon_info[avalon->device_id];
		timeout = info->timeout;
		miner_num = info->miner_count;
		asic_num = info->asic_count;
	}

	memset(at, 0, sizeof(struct avalon_task));

	if (reset) {
		at->reset = 1;
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = true;
	}

	at->flush_fifo = (ff ? 1 : 0);
	at->fan_eft = (fan ? 1 : 0);

	if (first && !at->reset) {
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = false;
	}

	at->fan_pwm_data = (fan ? fan : AVALON_DEFAULT_FAN_PWM);
	at->timeout_data = timeout;
	at->asic_num = asic_num;
	at->miner_num = miner_num;

	at->nonce_elf = 1;

	return 0;
}

static inline void avalon_create_task(struct avalon_task *at,
				      struct work *work)
{
	memcpy(at->midstate, work->midstate, 32);
	memcpy(at->data, work->data + 64, 12);
}

static int avalon_send_task(int fd, const struct avalon_task *at,
			    struct thr_info *thr)
{
	size_t ret;
	int full;
	struct timespec p;
	uint8_t *buf;
	size_t nr_len;
	struct cgpu_info *avalon;
	struct avalon_info *info;
	uint64_t delay = 32000000; /* default 32ms for B19200 */
	uint32_t nonce_range;
	int i;

	nr_len = AVALON_WRITE_SIZE + 4 * at->asic_num;
	buf = calloc(1, AVALON_WRITE_SIZE + nr_len);
	if (!buf)
		return AVA_SEND_ERROR;

	memcpy(buf, at, AVALON_WRITE_SIZE);

	nonce_range = (uint32_t)0xffffffff / at->asic_num;
	/* FIXME: the nonce_range  */
	for (i = 0; i < at->asic_num; i++) {
		buf[AVALON_WRITE_SIZE + (i * 4) + 3] = (i * nonce_range & 0xff000000) >> 24;
		buf[AVALON_WRITE_SIZE + (i * 4) + 2] = (i * nonce_range & 0x00ff0000) >> 16;
		buf[AVALON_WRITE_SIZE + (i * 4) + 1] = (i * nonce_range & 0x0000ff00) >> 8;
		buf[AVALON_WRITE_SIZE + (i * 4) + 0] = (i * nonce_range & 0x000000ff) >> 0;
	}
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


	if (thr) {
		avalon = thr->cgpu;
		info = avalon_info[avalon->device_id];
		delay = nr_len * 10 * 1000000000ULL;
		delay = delay / info->baud;
	}

	p.tv_sec = 0;
	p.tv_nsec = (long)delay + 4000000;
	nanosleep(&p, NULL);
	applog(LOG_DEBUG, "Avalon: Sent: Buffer delay: %ld", p.tv_nsec);

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
	struct avalon_info *info;
	uint8_t result[AVALON_READ_SIZE];
	int ret, read_count = AVALON_RESET_FAULT_DECISECONDS * TIME_FACTOR;

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

static int avalon_decode_nonce(struct thr_info *thr, struct work **work,
			       struct avalon_result *ar, uint32_t *nonce)
{
	struct cgpu_info *avalon;
	struct avalon_info *info;
	int avalon_get_work_count, i;

	if (!work)
		return -1;

	avalon = thr->cgpu;
	info = avalon_info[avalon->device_id];
	avalon_get_work_count = info->miner_count;

	for (i = 0; i < avalon_get_work_count; i++) {
		if (work[i] &&
		    !memcmp(ar->data, work[i]->data + 64, 12) &&
		    !memcmp(ar->midstate, work[i]->midstate, 32))
			break;
	}
	if (i == avalon_get_work_count)
		return -1;

	*nonce = ar->nonce;
#if defined (__BIG_ENDIAN__) || defined(MIPSEB)
	*nonce = swab32(*nonce);
#endif

	applog(LOG_DEBUG, "Avalon: match to work[%d]: %p", i, work[i]);
	return i;
}

static int avalon_reset(int fd, uint8_t timeout_p, uint8_t asic_num_p,
			uint8_t miner_num_p)
{
	struct avalon_task at;
	struct avalon_result ar;
	uint8_t *buf;
	int ret, i;
	struct timespec p;

	avalon_init_task(NULL,
			 &at, 1, 0,
			 AVALON_DEFAULT_FAN_PWM,
			 timeout_p, asic_num_p, miner_num_p);
	ret = avalon_send_task(fd, &at, NULL);
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

	applog(LOG_ERR,
	       "Avalon: Fan1: %d, Fan2: %d, Fan3: %d. Temp1: %d, Temp2: %d, Temp3: %d",
	       ar.fan0, ar.fan1, ar.fan2, ar.temp0, ar.temp1, ar.temp2);

	applog(LOG_ERR, "Avalon: Reset succeeded");
	return 0;
}

static void do_avalon_close(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	avalon_close(avalon->device_fd);
	avalon->device_fd = -1;
	/* FIXME: we should free the bulk0/1/2 */
}

static void set_timing_mode(struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon_info[avalon->device_id];

	info->Hs = ((info->timeout * AVALON_HASH_TIME_FACTOR) / (double)0xffffffff);
	info->fullnonce = info->timeout * AVALON_HASH_TIME_FACTOR;
	info->read_count = ((int)(info->fullnonce * TIME_FACTOR) - 1) / info->miner_count;
}

static void get_options(int this_option_offset, int *baud, int *miner_count,
			int *asic_count, int *timeout)
{
	char err_buf[BUFSIZ+1];
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3;
	size_t max;
	int i, tmp;

	if (opt_avalon_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_avalon_options;
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
	*miner_count = AVALON_DEFAULT_MINER_NUM;
	*asic_count = AVALON_DEFAULT_ASIC_NUM;
	*timeout = AVALON_DEFAULT_TIMEOUT;

	if (!(*buf))
		return;

	colon = strchr(buf, ':');
	if (colon)
		*(colon++) = '\0';

	tmp = atoi(buf);
	switch (tmp) {
	case 115200:
		*baud = 115200;
		break;
	case 57600:
		*baud = 57600;
		break;
	case 19200:
		*baud = 19200;
		break;
	default:
		sprintf(err_buf,
			"Invalid avalon-options for baud (%s) "
			"must be 115200, 57600 or 19200", buf);
		quit(1, err_buf);
	}

	if (colon && *colon) {
		colon2 = strchr(colon, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon) {
			tmp = atoi(colon);
			if (tmp > 0 && tmp <= AVALON_DEFAULT_MINER_NUM) {
				*miner_count = tmp;
			} else {
				sprintf(err_buf,
					"Invalid avalon-options for "
					"miner_count (%s) must be 1 ~ %d",
					colon, AVALON_DEFAULT_MINER_NUM);
				quit(1, err_buf);
			}
		}

		if (colon2 && *colon2) {
			colon3 = strchr(colon2, ':');
			if (colon3)
				*(colon3++) = '\0';

			tmp = atoi(colon2);
			if (tmp > 0 && tmp <= AVALON_DEFAULT_ASIC_NUM)
				*asic_count = tmp;
			else {
				sprintf(err_buf,
					"Invalid avalon-options for "
					"asic_count (%s) must be 1 ~ %d",
					colon2, AVALON_DEFAULT_ASIC_NUM);
				quit(1, err_buf);
			}

			if (colon3 && *colon3) {
				tmp = atoi(colon3);
				if (tmp > 0 && tmp <= 0xff)
					*timeout = tmp;
				else {
					sprintf(err_buf,
						"Invalid avalon-options for "
						"timeout (%s) must be 1 ~ %d",
						colon3, 0xff);
					quit(1, err_buf);
				}

			}
		}
	}
}

static bool avalon_detect_one(const char *devpath)
{
	struct avalon_info *info;
	int fd, ret;
	int baud, miner_count, asic_count, timeout;

	int this_option_offset = ++option_offset;
	get_options(this_option_offset, &baud, &miner_count, &asic_count,
		    &timeout);

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s "
	       "(baud=%d miner_count=%d asic_count=%d timeout=%d)",
	       devpath, baud, miner_count, asic_count, timeout);

	fd = avalon_open2(devpath, baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		return false;
	}

	ret = avalon_reset(fd, timeout, asic_count, miner_count);
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
			      sizeof(struct avalon_info *) *
			      (total_devices + 1));

	applog(LOG_INFO, "Avalon Detect: Found at %s, mark as %d",
	       devpath, avalon->device_id);

	avalon_info[avalon->device_id] = (struct avalon_info *)
		malloc(sizeof(struct avalon_info));
	if (unlikely(!(avalon_info[avalon->device_id])))
		quit(1, "Failed to malloc avalon_info");

	info = avalon_info[avalon->device_id];

	memset(info, 0, sizeof(struct avalon_info));

	info->baud = baud;
	info->miner_count = miner_count;
	info->asic_count = asic_count;
	info->timeout = timeout;

	set_timing_mode(avalon);

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
	int fd, ret;

	struct avalon_info *info = avalon_info[avalon->device_id];

	avalon->device_fd = -1;
	fd = avalon_open(avalon->device_path,
			     avalon_info[avalon->device_id]->baud);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon: Failed to open on %s",
		       avalon->device_path);
		return false;
	}
	ret = avalon_reset(fd, info->timeout, info->asic_count,
			   info->miner_count);
	if (ret)
		return false;
	avalon->device_fd = fd;

	applog(LOG_INFO, "Avalon: Opened on %s", avalon->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(avalon->init, &now);

	return true;
}

static void avalon_free_work(struct thr_info *thr, struct work **work)
{
	struct cgpu_info *avalon;
	struct avalon_info *info;
	int avalon_get_work_count, i;

	if (!work)
		return;

	avalon = thr->cgpu;
	info = avalon_info[avalon->device_id];
	avalon_get_work_count = info->miner_count;

	for (i = 0; i < avalon_get_work_count; i++)
		if (work[i]) {
			free_work(work[i]);
			work[i] = NULL;
		}
}
static int64_t avalon_scanhash(struct thr_info *thr, struct work **bulk_work,
			       __maybe_unused int64_t max_nonce)
{
	struct cgpu_info *avalon;
	int fd;
	int ret;
	int full;

	struct avalon_info *info;
	struct avalon_task at;
	struct avalon_result ar;
	static struct work *bulk0[AVALON_DEFAULT_MINER_NUM] = {
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
	static struct work *bulk1[AVALON_DEFAULT_MINER_NUM] = {
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
	static struct work *bulk2[AVALON_DEFAULT_MINER_NUM] = {
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
	struct work **work = NULL;
	int i, work_i0, work_i1, work_i2;
	int avalon_get_work_count;

	uint32_t nonce;
	int64_t hash_count;
	struct timeval tv_start, tv_finish, elapsed;

	int curr_hw_errors;
	bool was_hw_error;

	int64_t estimate_hashes;

	avalon = thr->cgpu;
	info = avalon_info[avalon->device_id];
	avalon_get_work_count = info->miner_count;

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
	for (i = 0; i < avalon_get_work_count; i++) {
		bulk0[i] = bulk1[i];
		bulk1[i] = bulk2[i];
		bulk2[i] = work[i];
		applog(LOG_DEBUG, "Avalon: bulk0/1/2 buffer [%d]: %p, %p, %p",
		       i, bulk0[i], bulk1[i], bulk2[i]);
	}

	i = 0;
	while (true) {
		avalon_init_task(thr, &at, 0, 0, 0, 0, 0, 0);
		avalon_create_task(&at, work[i]);
		ret = avalon_send_task(fd, &at, thr);
		if (ret == AVA_SEND_ERROR ||
		    (ret == AVA_SEND_BUFFER_EMPTY &&
		     (i + 1 == avalon_get_work_count))) {
			avalon_free_work(thr, bulk0);
			avalon_free_work(thr, bulk1);
			avalon_free_work(thr, bulk2);
			do_avalon_close(thr);
			applog(LOG_ERR, "AVA%i: Comms error",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			sleep(1);
			return 0;	/* This should never happen */
		}

		work[i]->blk.nonce = 0xffffffff;

		if (ret == AVA_SEND_BUFFER_FULL)
			break;

		i++;
	}

	elapsed.tv_sec = elapsed.tv_usec = 0;
	gettimeofday(&tv_start, NULL);

	while(true) {
		full = avalon_buffer_full(fd);
		applog(LOG_DEBUG, "Avalon: Buffer full: %s",
		       ((full == AVA_BUFFER_FULL) ? "Yes" : "No"));
		if (full == AVA_BUFFER_EMPTY)
			break;

		work_i0 = work_i1 = work_i2 = -1;
		ret = avalon_get_result(fd, &ar, thr, &tv_finish);
		if (ret == AVA_GETS_ERROR) {
			avalon_free_work(thr, bulk0);
			avalon_free_work(thr, bulk1);
			avalon_free_work(thr, bulk2);
			do_avalon_close(thr);
			applog(LOG_ERR,
			       "AVA%i: Comms error", avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;
		}
		/* aborted before becoming idle, get new work */
		if (ret == AVA_GETS_TIMEOUT) {
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
		}
		if (ret == AVA_GETS_RESTART) {
			avalon_free_work(thr, bulk0);
			avalon_free_work(thr, bulk1);
			avalon_free_work(thr, bulk2);
			continue;
		}
		avalon->temp = ar.temp0;
		work_i0 = avalon_decode_nonce(thr, bulk0, &ar, &nonce);
		work_i1 = avalon_decode_nonce(thr, bulk1, &ar, &nonce);
		work_i2 = avalon_decode_nonce(thr, bulk2, &ar, &nonce);

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

		hash_count = nonce;
		hash_count++;
		hash_count *= info->asic_count;
	}
	avalon_free_work(thr, bulk0);

	if (opt_debug) {
		timersub(&tv_finish, &tv_start, &elapsed);
		applog(LOG_DEBUG,
		       "Avalon: nonce = 0x%08x = 0x%08llx hashes "
		       "(%ld.%06lds)",
		       nonce, hash_count, elapsed.tv_sec, elapsed.tv_usec);
	}

	applog(LOG_ERR,
	       "Avalon: Fan1: %d, Fan2: %d, Fan3: %d. Temp1: %d, Temp2: %d, Temp3: %d",
	       ar.fan0, ar.fan1, ar.fan2, ar.temp0, ar.temp1, ar.temp2);

	return hash_count;
}

static struct api_data *avalon_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon_info *info = avalon_info[cgpu->device_id];

	root = api_add_int(root, "read_count", &(info->read_count), false);
	root = api_add_double(root, "fullnonce", &(info->fullnonce), false);
	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "miner_count", &(info->miner_count),false);
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
