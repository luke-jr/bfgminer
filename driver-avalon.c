/*
 * Copyright 2012-2013 Xiangfu
 * Copyright 2012 Luke Dashjr
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
#include <stdbool.h>
#include <stdint.h>
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

#include "deviceapi.h"
#include "elist.h"
#include "miner.h"
#include "fpgautils.h"
#include "driver-avalon.h"
#include "logging.h"

static int option_offset = -1;
struct avalon_info **avalon_info;
struct device_api avalon_api;

static inline uint8_t rev8(uint8_t d)
{
	int i;
	uint8_t out = 0;

	/* from left to right */
	for (i = 0; i < 8; i++)
		if (d & (1 << i))
			out |= (1 << (7 - i));

	return out;
}

static int avalon_init_task(struct avalon_task *at,
			    uint8_t reset, uint8_t ff, uint8_t fan,
			    uint8_t timeout, uint8_t asic_num,
			    uint8_t miner_num, uint8_t nonce_elf,
			    uint8_t gate_miner, int frequency)
{
	uint8_t *buf;
	static bool first = true;

	if (unlikely(!at))
		return -1;

	if (unlikely(timeout <= 0 || asic_num <= 0 || miner_num <= 0))
		return -1;

	memset(at, 0, sizeof(struct avalon_task));

	if (unlikely(reset)) {
		at->reset = 1;
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = true;
	}

	at->flush_fifo = (ff ? 1 : 0);
	at->fan_eft = (fan ? 1 : 0);

	if (unlikely(first && !at->reset)) {
		at->fan_eft = 1;
		at->timer_eft = 1;
		first = false;
	}

	at->fan_pwm_data = (fan ? fan : AVALON_DEFAULT_FAN_MAX_PWM);
	at->timeout_data = timeout;
	at->asic_num = asic_num;
	at->miner_num = miner_num;
	at->nonce_elf = nonce_elf;

	at->gate_miner_elf = 1;
	at->asic_pll = 1;

	if (unlikely(gate_miner)) {
		at-> gate_miner = 1;
		at->asic_pll = 0;
	}

	buf = (uint8_t *)at;
	buf[5] = 0x00;
	buf[8] = 0x74;
	buf[9] = 0x01;
	buf[10] = 0x00;
	buf[11] = 0x00;
	if (frequency == 256) {
		buf[6] = 0x03;
		buf[7] = 0x08;
	} else if (frequency == 270) {
		buf[6] = 0x73;
		buf[7] = 0x08;
	} else if (frequency == 282) {
		buf[6] = 0xd3;
		buf[7] = 0x08;
	} else if (frequency == 300) {
		buf[6] = 0x63;
		buf[7] = 0x09;
	}

	return 0;
}

static inline void avalon_create_task(struct avalon_task *at,
				      struct work *work)
{
	memcpy(at->midstate, work->midstate, 32);
	memcpy(at->data, work->data + 64, 12);
}

static int avalon_send_task(int fd, const struct avalon_task *at,
			    struct cgpu_info *avalon)

{
	size_t ret;
	int full;
	struct timespec p;
	uint8_t buf[AVALON_WRITE_SIZE + 4 * AVALON_DEFAULT_ASIC_NUM];
	size_t nr_len;
	struct avalon_info *info;
	uint64_t delay = 32000000; /* Default 32ms for B19200 */
	uint32_t nonce_range;
	int i;

	if (at->nonce_elf)
		nr_len = AVALON_WRITE_SIZE + 4 * at->asic_num;
	else
		nr_len = AVALON_WRITE_SIZE;

	memcpy(buf, at, AVALON_WRITE_SIZE);

	if (at->nonce_elf) {
		nonce_range = (uint32_t)0xffffffff / at->asic_num;
		for (i = 0; i < at->asic_num; i++) {
			buf[AVALON_WRITE_SIZE + (i * 4) + 3] =
				(i * nonce_range & 0xff000000) >> 24;
			buf[AVALON_WRITE_SIZE + (i * 4) + 2] =
				(i * nonce_range & 0x00ff0000) >> 16;
			buf[AVALON_WRITE_SIZE + (i * 4) + 1] =
				(i * nonce_range & 0x0000ff00) >> 8;
			buf[AVALON_WRITE_SIZE + (i * 4) + 0] =
				(i * nonce_range & 0x000000ff) >> 0;
		}
	}
#if defined(__BIG_ENDIAN__) || defined(MIPSEB)
	uint8_t tt = 0;

	tt = (buf[0] & 0x0f) << 4;
	tt |= ((buf[0] & 0x10) ? (1 << 3) : 0);
	tt |= ((buf[0] & 0x20) ? (1 << 2) : 0);
	tt |= ((buf[0] & 0x40) ? (1 << 1) : 0);
	tt |= ((buf[0] & 0x80) ? (1 << 0) : 0);
	buf[0] = tt;

	tt = (buf[4] & 0x0f) << 4;
	tt |= ((buf[4] & 0x10) ? (1 << 3) : 0);
	tt |= ((buf[4] & 0x20) ? (1 << 2) : 0);
	tt |= ((buf[4] & 0x40) ? (1 << 1) : 0);
	tt |= ((buf[4] & 0x80) ? (1 << 0) : 0);
	buf[4] = tt;
#endif
	if (likely(avalon)) {
		info = avalon_info[avalon->device_id];
		delay = nr_len * 10 * 1000000000ULL;
		delay = delay / info->baud;
	}

	if (at->reset)
		nr_len = 1;
	if (opt_debug) {
		applog(LOG_DEBUG, "Avalon: Sent(%lu):", (unsigned long)nr_len);
		hexdump((uint8_t *)buf, nr_len);
	}
	ret = write(fd, buf, nr_len);
	if (unlikely(ret != nr_len))
		return AVA_SEND_ERROR;

	p.tv_sec = 0;
	p.tv_nsec = (long)delay + 4000000;
	nanosleep(&p, NULL);
	applog(LOG_DEBUG, "Avalon: Sent: Buffer delay: %ld", p.tv_nsec);

	full = avalon_buffer_full(fd);
	applog(LOG_DEBUG, "Avalon: Sent: Buffer full: %s",
	       ((full == AVA_BUFFER_FULL) ? "Yes" : "No"));

	if (unlikely(full == AVA_BUFFER_FULL))
		return AVA_SEND_BUFFER_FULL;

	return AVA_SEND_BUFFER_EMPTY;
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
				applog(LOG_WARNING,
				       "Avalon: No data in %.2f seconds",
				       (float)rc/(float)AVALON_TIME_FACTOR);
			}
			return AVA_GETS_TIMEOUT;
		}

		if (thr && thr->work_restart) {
			if (opt_debug) {
				applog(LOG_WARNING,
				       "Avalon: Work restart at %.2f seconds",
				       (float)(rc)/(float)AVALON_TIME_FACTOR);
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
	int ret, read_count = AVALON_RESET_FAULT_DECISECONDS * AVALON_TIME_FACTOR;

	if (likely(thr)) {
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

	if (unlikely(!work))
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

	info->matching_work[i]++;
	*nonce = ar->nonce;
#if defined (__BIG_ENDIAN__) || defined(MIPSEB)
	*nonce = swab32(*nonce);
#endif

	applog(LOG_DEBUG, "Avalon: match to work[%d](%p): %d",i, work[i],
	       info->matching_work[i]);
	return i;
}

static int avalon_reset(int fd, struct avalon_result *ar)
{
	struct avalon_task at;
	uint8_t *buf;
	int ret, i = 0;
	struct timespec p;

	avalon_init_task(&at, 1, 0,
			 AVALON_DEFAULT_FAN_MAX_PWM,
			 AVALON_DEFAULT_TIMEOUT,
			 AVALON_DEFAULT_ASIC_NUM,
			 AVALON_DEFAULT_MINER_NUM,
			 0, 0,
			 AVALON_DEFAULT_FREQUENCY);
	ret = avalon_send_task(fd, &at, NULL);
	if (ret == AVA_SEND_ERROR)
		return 1;

	avalon_get_result(fd, ar, NULL, NULL);

	buf = (uint8_t *)ar;
	if (buf[0] == 0xAA && buf[1] == 0x55 &&
	    buf[2] == 0xAA && buf[3] == 0x55) {
		for (i = 4; i < 11; i++)
			if (buf[i] != 0)
				break;
	}

	if (i != 11) {
		applog(LOG_ERR, "Avalon: Reset failed! not an Avalon?"
		       " (%d: %02x %02x %02x %02x)",
		       i, buf[0], buf[1], buf[2], buf[3]);
		/* FIXME: return 1; */
	}

	p.tv_sec = 0;
	p.tv_nsec = AVALON_RESET_PITCH;
	nanosleep(&p, NULL);

	applog(LOG_WARNING, "Avalon: Reset succeeded");
	return 0;
}

static void avalon_idle(struct cgpu_info *avalon)
{
	int i, ret;
	struct avalon_task at;

	int fd = avalon->device_fd;
	struct avalon_info *info = avalon_info[avalon->device_id];
	int avalon_get_work_count = info->miner_count;

	i = 0;
	while (true) {
		avalon_init_task(&at, 0, 0, info->fan_pwm,
				 info->timeout, info->asic_count,
				 info->miner_count, 1, 1, info->frequency);
		ret = avalon_send_task(fd, &at, avalon);
		if (unlikely(ret == AVA_SEND_ERROR ||
			     (ret == AVA_SEND_BUFFER_EMPTY &&
			      (i + 1 == avalon_get_work_count * 2)))) {
			applog(LOG_ERR, "AVA%i: Comms error", avalon->device_id);
			return;
		}
		if (i + 1 == avalon_get_work_count * 2)
			break;

		if (ret == AVA_SEND_BUFFER_FULL)
			break;

		i++;
	}
	applog(LOG_ERR, "Avalon: Goto idle mode");
}

static void get_options(int this_option_offset, int *baud, int *miner_count,
			int *asic_count, int *timeout, int *frequency)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3, *colon4;
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
	*miner_count = AVALON_DEFAULT_MINER_NUM - 8;
	*asic_count = AVALON_DEFAULT_ASIC_NUM;
	*timeout = AVALON_DEFAULT_TIMEOUT;
	*frequency = AVALON_DEFAULT_FREQUENCY;

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
	case 38400:
		*baud = 38400;
		break;
	case 19200:
		*baud = 19200;
		break;
	default:
		quit(1,
			"Invalid avalon-options for baud (%s) "
			"must be 115200, 57600, 38400 or 19200", buf);
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
				quit(1,
					"Invalid avalon-options for "
					"miner_count (%s) must be 1 ~ %d",
					colon, AVALON_DEFAULT_MINER_NUM);
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
				quit(1,
					"Invalid avalon-options for "
					"asic_count (%s) must be 1 ~ %d",
					colon2, AVALON_DEFAULT_ASIC_NUM);
			}

			if (colon3 && *colon3) {
				colon4 = strchr(colon3, ':');
				if (colon4)
					*(colon4++) = '\0';

				tmp = atoi(colon3);
				if (tmp > 0 && tmp <= 0xff)
					*timeout = tmp;
				else {
					quit(1,
						"Invalid avalon-options for "
						"timeout (%s) must be 1 ~ %d",
						colon3, 0xff);
				}
				if (colon4 && *colon4) {
					tmp = atoi(colon4);
					switch (tmp) {
					case 256:
					case 270:
					case 282:
					case 300:
						*frequency = tmp;
						break;
					default:
						quit(1,
							"Invalid avalon-options for "
							"frequency must be 256/270/282/300");
					}
				}
			}
		}
	}
}

static bool avalon_detect_one(const char *devpath)
{
	struct avalon_info *info;
	struct avalon_result ar;
	int fd, ret;
	int baud, miner_count, asic_count, timeout, frequency = 0;

	int this_option_offset = ++option_offset;
	get_options(this_option_offset, &baud, &miner_count, &asic_count,
		    &timeout, &frequency);

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s "
	       "(baud=%d miner_count=%d asic_count=%d timeout=%d frequency=%d)",
	       devpath, baud, miner_count, asic_count, timeout, frequency);

	fd = avalon_open2(devpath, baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		return false;
	}

	ret = avalon_reset(fd, &ar);

	if (ret) {
		; /* FIXME: I think IT IS avalon and wait on reset;
		   * avalon_close(fd);
		   * return false; */
	}

	/* We have a real Avalon! */
	struct cgpu_info *avalon;
	avalon = calloc(1, sizeof(struct cgpu_info));
	avalon->api = &avalon_api;
	avalon->device_path = strdup(devpath);
	avalon->device_fd = fd;
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
	info->read_count = ((float)info->timeout * AVALON_HASH_TIME_FACTOR *
			    AVALON_TIME_FACTOR) / (float)info->miner_count;

	info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM;
	info->temp_max = 0;
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;
	info->frequency = frequency;

	/* Set asic to idle mode after detect */
	avalon_idle(avalon);
	avalon->device_fd = -1;

	avalon_close(fd);
	return true;
}

static inline void avalon_detect()
{
	serial_detect(&avalon_api, avalon_detect_one);
}

static void avalon_init(struct cgpu_info *avalon)
{
	struct avalon_result ar;
	int fd, ret;

	avalon->device_fd = -1;
	fd = avalon_open(avalon->device_path,
			     avalon_info[avalon->device_id]->baud);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon: Failed to open on %s",
		       avalon->device_path);
		return;
	}

	ret = avalon_reset(fd, &ar);
	if (ret) {
		avalon_close(fd);
		return;
	}

	avalon->device_fd = fd;
	applog(LOG_INFO, "Avalon: Opened on %s", avalon->device_path);

	return;
}

static bool avalon_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct timeval now;

	avalon_init(avalon);
	if (avalon->device_fd == -1)
		return false;

	gettimeofday(&now, NULL);
	get_datestamp(avalon->init, &now);
	avalon->status = LIFE_INIT2;
	return true;
}

static void avalon_free_work(struct thr_info *thr, struct work **work)
{
	struct cgpu_info *avalon;
	struct avalon_info *info;
	int i;

	if (unlikely(!work))
		return;

	avalon = thr->cgpu;
	info = avalon_info[avalon->device_id];

	for (i = 0; i < info->miner_count; i++)
		if (likely(work[i])) {
			free_work(work[i]);
			work[i] = NULL;
		}
}

static void do_avalon_close(struct thr_info *thr)
{
	struct avalon_result ar;
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon_info[avalon->device_id];

	nmsleep(1000);
	avalon_reset(avalon->device_fd, &ar);
	avalon_idle(avalon);
	avalon_close(avalon->device_fd);
	avalon->device_fd = -1;

	info->no_matching_work = 0;
	avalon_free_work(thr, info->bulk0);
	avalon_free_work(thr, info->bulk1);
	avalon_free_work(thr, info->bulk2);
	avalon_free_work(thr, info->bulk3);
}

static inline void record_temp_fan(struct avalon_info *info, struct avalon_result *ar, float *temp_avg)
{
	int max;

	info->fan0 = ar->fan0 * AVALON_FAN_FACTOR;
	info->fan1 = ar->fan1 * AVALON_FAN_FACTOR;
	info->fan2 = ar->fan2 * AVALON_FAN_FACTOR;

	info->temp0 = ar->temp0;
	info->temp1 = ar->temp1;
	info->temp2 = ar->temp2;
	if (ar->temp0 & 0x80) {
		ar->temp0 &= 0x7f;
		info->temp0 = 0 - ((~ar->temp0 & 0x7f) + 1);
	}
	if (ar->temp1 & 0x80) {
		ar->temp1 &= 0x7f;
		info->temp1 = 0 - ((~ar->temp1 & 0x7f) + 1);
	}
	if (ar->temp2 & 0x80) {
		ar->temp2 &= 0x7f;
		info->temp2 = 0 - ((~ar->temp2 & 0x7f) + 1);
	}

	*temp_avg = info->temp2;

	max = info->temp_max;
	if (info->temp0 > max)
		max = info->temp0;
	if (info->temp1 > max)
		max = info->temp1;
	if (info->temp2 > max)
		max = info->temp2;
	if (max >= 100) {	/* FIXME: fix the root cause on fpga controller firmware */
		if (opt_debug) {
			applog(LOG_DEBUG, "Avalon: temp_max: %d", max);
			hexdump((uint8_t *)ar, AVALON_READ_SIZE);
		}
		return;
	}

	info->temp_max = max;
}

static inline void adjust_fan(struct avalon_info *info)
{
	int temp_new;

	temp_new = info->temp_sum / info->temp_history_count;

	if (temp_new < 35) {
		info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM;
		info->temp_old = temp_new;
	} else if (temp_new > 55) {
		info->fan_pwm = AVALON_DEFAULT_FAN_MAX_PWM;
		info->temp_old = temp_new;
	} else if (abs(temp_new - info->temp_old) >= 2) {
		info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM + (temp_new - 35) * 6.4;
		info->temp_old = temp_new;
	}
}

static int64_t avalon_scanhash(struct thr_info *thr, struct work **work,
			       __maybe_unused int64_t max_nonce)
{
	struct cgpu_info *avalon;
	int fd, ret, full;

	struct avalon_info *info;
	struct avalon_task at;
	struct avalon_result ar;
	int i, work_i0, work_i1, work_i2, work_i3;
	int avalon_get_work_count;

	struct timeval tv_start, tv_finish, elapsed;
	uint32_t nonce;
	int64_t hash_count;
	static int first_try = 0;
	int result_count, result_wrong;

	avalon = thr->cgpu;
	info = avalon_info[avalon->device_id];
	avalon_get_work_count = info->miner_count;

	if (unlikely(avalon->device_fd == -1))
		if (!avalon_prepare(thr)) {
			applog(LOG_ERR, "AVA%i: Comms error(open)",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			/* fail the device if the reopen attempt fails */
			return -1;
		}
	fd = avalon->device_fd;
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif

	for (i = 0; i < avalon_get_work_count; i++) {
		info->bulk0[i] = info->bulk1[i];
		info->bulk1[i] = info->bulk2[i];
		info->bulk2[i] = info->bulk3[i];
		info->bulk3[i] = work[i];
		applog(LOG_DEBUG, "Avalon: bulk0/1/2 buffer [%d]: %p, %p, %p, %p",
		       i, info->bulk0[i], info->bulk1[i], info->bulk2[i], info->bulk3[i]);
	}

	i = 0;
	while (true) {
		avalon_init_task(&at, 0, 0, info->fan_pwm,
				 info->timeout, info->asic_count,
				 info->miner_count, 1, 0, info->frequency);
		avalon_create_task(&at, work[i]);
		ret = avalon_send_task(fd, &at, avalon);
		if (unlikely(ret == AVA_SEND_ERROR ||
			     (ret == AVA_SEND_BUFFER_EMPTY &&
			      (i + 1 == avalon_get_work_count) &&
			      first_try))) {
			avalon_free_work(thr, info->bulk0);
			avalon_free_work(thr, info->bulk1);
			avalon_free_work(thr, info->bulk2);
			avalon_free_work(thr, info->bulk3);
			do_avalon_close(thr);
			applog(LOG_ERR, "AVA%i: Comms error(buffer)",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			first_try = 0;
			nmsleep(1000);
			return 0;	/* This should never happen */
		}
		if (ret == AVA_SEND_BUFFER_EMPTY && (i + 1 == avalon_get_work_count)) {
			first_try = 1;
			return 0xffffffff;
		}

		work[i]->blk.nonce = 0xffffffff;

		if (ret == AVA_SEND_BUFFER_FULL)
			break;

		i++;
	}
	if (unlikely(first_try))
		first_try = 0;

	elapsed.tv_sec = elapsed.tv_usec = 0;
	gettimeofday(&tv_start, NULL);

	result_count = 0;
	result_wrong = 0;
	hash_count = 0;
	while (true) {
		work_i0 = work_i1 = work_i2 = work_i3 = -1;

		full = avalon_buffer_full(fd);
		applog(LOG_DEBUG, "Avalon: Buffer full: %s",
		       ((full == AVA_BUFFER_FULL) ? "Yes" : "No"));
		if (unlikely(full == AVA_BUFFER_EMPTY))
			break;

		ret = avalon_get_result(fd, &ar, thr, &tv_finish);
		if (unlikely(ret == AVA_GETS_ERROR)) {
			avalon_free_work(thr, info->bulk0);
			avalon_free_work(thr, info->bulk1);
			avalon_free_work(thr, info->bulk2);
			avalon_free_work(thr, info->bulk3);
			do_avalon_close(thr);
			applog(LOG_ERR,
			       "AVA%i: Comms error(read)", avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;
		}
		if (unlikely(ret == AVA_GETS_TIMEOUT)) {
			timersub(&tv_finish, &tv_start, &elapsed);
			applog(LOG_DEBUG, "Avalon: no nonce in (%ld.%06lds)",
			       (long)elapsed.tv_sec, (long)elapsed.tv_usec);
			continue;
		}
		if (unlikely(ret == AVA_GETS_RESTART)) {
			avalon_free_work(thr, info->bulk0);
			avalon_free_work(thr, info->bulk1);
			avalon_free_work(thr, info->bulk2);
			avalon_free_work(thr, info->bulk3);
			continue;
		}
		result_count++;

		work_i0 = avalon_decode_nonce(thr, info->bulk0, &ar, &nonce);
		work_i1 = avalon_decode_nonce(thr, info->bulk1, &ar, &nonce);
		work_i2 = avalon_decode_nonce(thr, info->bulk2, &ar, &nonce);
		work_i3 = avalon_decode_nonce(thr, info->bulk3, &ar, &nonce);
		if ((work_i0 < 0) && (work_i1 < 0) && (work_i2 < 0) && (work_i3 < 0)) {
			info->no_matching_work++;
			result_wrong++;

			if (opt_debug) {
				timersub(&tv_finish, &tv_start, &elapsed);
				applog(LOG_DEBUG,"Avalon: no matching work: %d"
				       " (%ld.%06lds)", info->no_matching_work,
				       (long)elapsed.tv_sec, (long)elapsed.tv_usec);
			}
			continue;
		}
		if (work_i0 >= 0)
			submit_nonce(thr, info->bulk0[work_i0], nonce);
		if (work_i1 >= 0)
			submit_nonce(thr, info->bulk1[work_i1], nonce);
		if (work_i2 >= 0)
			submit_nonce(thr, info->bulk2[work_i2], nonce);
		if (work_i3 >= 0)
			submit_nonce(thr, info->bulk3[work_i3], nonce);

		hash_count += nonce;
		if (opt_debug) {
			timersub(&tv_finish, &tv_start, &elapsed);
			applog(LOG_DEBUG,
			       "Avalon: nonce = 0x%08"PRIx32" = 0x%08"PRIx64" hashes "
			       "(%ld.%06lds)", nonce, (uint64_t)hash_count,
			       (long)elapsed.tv_sec, (long)elapsed.tv_usec);
		}
	}
	if (result_count == result_wrong) {
		/* This mean FPGA controller give all wrong result
		 * try to reset the Avalon */
		avalon_free_work(thr, info->bulk0);
		avalon_free_work(thr, info->bulk1);
		avalon_free_work(thr, info->bulk2);
		avalon_free_work(thr, info->bulk3);
		do_avalon_close(thr);
		applog(LOG_ERR,
		       "AVA%i: FPGA controller mess up", avalon->device_id);
		dev_error(avalon, REASON_DEV_COMMS_ERROR);
		return 0;
	}

	avalon_free_work(thr, info->bulk0);

	record_temp_fan(info, &ar, &(avalon->temp));
	applog(LOG_INFO,
	       "Avalon: Fan1: %d/m, Fan2: %d/m, Fan3: %d/m\t"
	       "Temp1: %dC, Temp2: %dC, Temp3: %dC, TempMAX: %dC",
	       info->fan0, info->fan1, info->fan2,
	       info->temp0, info->temp1, info->temp2, info->temp_max);
	info->temp_history_index++;
	info->temp_sum += info->temp2;
	applog(LOG_DEBUG, "Avalon: temp_index: %d, temp_count: %d, temp_old: %d",
	       info->temp_history_index, info->temp_history_count, info->temp_old);
	if (info->temp_history_index == info->temp_history_count) {
		adjust_fan(info);
		info->temp_history_index = 0;
		info->temp_sum = 0;
	}

	/*
	 * FIXME: Each work split to 10 pieces, each piece send to a
	 * asic(256MHs). one work can be mulit-nonce back. it is not
	 * easy calculate correct hash on such situation. so I simplely
	 * add each nonce to hash_count. base on Utility/m hash_count*2
	 * give a very good result.
	 *
	 * Any patch will be great.
	 */
	return (hash_count * 2);
}

// minerloop_scanhash hacked to handle Avalon's many processors
static
void minerloop_avalon(struct thr_info *mythr)
{
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	const struct device_api *api = cgpu->api;
	struct timeval tv_start, tv_end;
	struct timeval tv_hashes;
	uint32_t max_nonce = api->can_limit_work ? api->can_limit_work(mythr) : 0xffffffff;
	int64_t hashes;
	struct avalon_info *info = avalon_info[cgpu->device_id];
	int i;
	int avalon_get_work_count = info->miner_count;
	struct work **work = calloc(1,
				    avalon_get_work_count * sizeof(struct work *));
	if (!work)
		quit(1, "Faile on Avalon calloc");
	const bool primary = (!mythr->device_thread) || mythr->primary_thread;
	
	while (1) {
		mythr->work_restart = false;
		for (i = 0; i < avalon_get_work_count; i++)
			request_work(mythr);
		for (i = 0; i < avalon_get_work_count; i++) {
			work[i] = get_work(mythr);
			work[i]->blk.nonce = 0;
		}
		for (i = 0; i < avalon_get_work_count; i++) {
			if (api->prepare_work && !api->prepare_work(mythr, work[i])) {
				applog(LOG_ERR, "work prepare failed, exiting "
					"mining thread %d", thr_id);
				break;
			}
			gettimeofday(&(work[i]->tv_work_start), NULL);
		}
		
		do {
			thread_reportin(mythr);
			gettimeofday(&tv_start, NULL);
			hashes = api->scanhash_queue(mythr, work, max_nonce);
			gettimeofday(&tv_end, NULL);
			thread_reportin(mythr);
			
			timersub(&tv_end, &tv_start, &tv_hashes);
			if (!hashes_done(mythr, hashes, &tv_hashes, api->can_limit_work ? &max_nonce : NULL))
				goto disabled;
			
			if (unlikely(mythr->work_restart)) {
				/* Apart from device_thread 0, we stagger the
				 * starting of every next thread to try and get
				 * all devices busy before worrying about
				 * getting work for their extra threads */
				if (!primary) {
					struct timespec rgtp;

					rgtp.tv_sec = 0;
					rgtp.tv_nsec = 250 * mythr->device_thread * 1000000;
					nanosleep(&rgtp, NULL);
				}
				break;
			}
			
			if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
disabled:
				mt_disable(mythr);
		} while (false);
	}
	free(work);
}

static struct api_data *avalon_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon_info *info = avalon_info[cgpu->device_id];

	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "miner_count", &(info->miner_count),false);
	root = api_add_int(root, "asic_count", &(info->asic_count), false);
	root = api_add_int(root, "read_count", &(info->read_count), false);
	root = api_add_int(root, "timeout", &(info->timeout), false);
	root = api_add_int(root, "frequency", &(info->frequency), false);

	root = api_add_int(root, "fan1", &(info->fan0), false);
	root = api_add_int(root, "fan2", &(info->fan1), false);
	root = api_add_int(root, "fan3", &(info->fan2), false);

	root = api_add_int(root, "temp1", &(info->temp0), false);
	root = api_add_int(root, "temp2", &(info->temp1), false);
	root = api_add_int(root, "temp3", &(info->temp2), false);
	root = api_add_int(root, "temp_max", &(info->temp_max), false);

	root = api_add_int(root, "no_matching_work", &(info->no_matching_work), false);
	root = api_add_int(root, "matching_work_count1", &(info->matching_work[0]), false);
	root = api_add_int(root, "matching_work_count2", &(info->matching_work[1]), false);
	root = api_add_int(root, "matching_work_count3", &(info->matching_work[2]), false);
	root = api_add_int(root, "matching_work_count4", &(info->matching_work[3]), false);
	root = api_add_int(root, "matching_work_count5", &(info->matching_work[4]), false);
	root = api_add_int(root, "matching_work_count6", &(info->matching_work[5]), false);
	root = api_add_int(root, "matching_work_count7", &(info->matching_work[6]), false);
	root = api_add_int(root, "matching_work_count8", &(info->matching_work[7]), false);
	root = api_add_int(root, "matching_work_count9", &(info->matching_work[8]), false);
	root = api_add_int(root, "matching_work_count10", &(info->matching_work[9]), false);
	root = api_add_int(root, "matching_work_count11", &(info->matching_work[10]), false);
	root = api_add_int(root, "matching_work_count12", &(info->matching_work[11]), false);
	root = api_add_int(root, "matching_work_count13", &(info->matching_work[12]), false);
	root = api_add_int(root, "matching_work_count14", &(info->matching_work[13]), false);
	root = api_add_int(root, "matching_work_count15", &(info->matching_work[14]), false);
	root = api_add_int(root, "matching_work_count16", &(info->matching_work[15]), false);
	root = api_add_int(root, "matching_work_count17", &(info->matching_work[16]), false);
	root = api_add_int(root, "matching_work_count18", &(info->matching_work[17]), false);
	root = api_add_int(root, "matching_work_count19", &(info->matching_work[18]), false);
	root = api_add_int(root, "matching_work_count20", &(info->matching_work[19]), false);
	root = api_add_int(root, "matching_work_count21", &(info->matching_work[20]), false);
	root = api_add_int(root, "matching_work_count22", &(info->matching_work[21]), false);
	root = api_add_int(root, "matching_work_count23", &(info->matching_work[22]), false);
	root = api_add_int(root, "matching_work_count24", &(info->matching_work[23]), false);

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
	.minerloop = minerloop_avalon,
	.scanhash_queue = avalon_scanhash,
	.get_api_stats = avalon_api_stats,
	.reinit_device = avalon_init,
	.thread_shutdown = avalon_shutdown,
};
