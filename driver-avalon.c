/*
 * Copyright 2012-2013 Xiangfu
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2012-2014 Luke Dashjr
 * Copyright 2012-2013 Andrew Smith
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
  #include <sys/select.h>
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include "compat.h"
  #include <windows.h>
  #include <io.h>
#endif

#include "deviceapi.h"
#include "miner.h"
#include "driver-avalon.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "util.h"

BFG_REGISTER_DRIVER(avalon_drv)

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
	switch (frequency) {
		case 256:
			buf[6] = 0x03;
			buf[7] = 0x08;
			break;
		default:
		case 270:
			buf[6] = 0x73;
			buf[7] = 0x08;
			break;
		case 282:
			buf[6] = 0xd3;
			buf[7] = 0x08;
			break;
		case 300:
			buf[6] = 0x63;
			buf[7] = 0x09;
			break;
		case 325:
			buf[6] = 0x28;
			buf[7] = 0x0a;
			break;
		case 350:
			buf[6] = 0xf0;
			buf[7] = 0x0a;
			break;
		case 375:
			buf[6] = 0xb8;
			buf[7] = 0x0b;
			break;
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
		info = avalon->device_data;
		delay = nr_len * 10 * 1000000000ULL;
		delay = delay / info->baud;
	}

	if (at->reset)
		nr_len = 1;
	if (opt_debug) {
		char x[(nr_len * 2) + 1];
		bin2hex(x, buf, nr_len);
		applog(LOG_DEBUG, "Avalon: Sent(%u): %s", (unsigned int)nr_len, x);
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

static inline int avalon_gets(int fd, uint8_t *buf, int read_count,
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
		{
			applog(LOG_ERR, "Avalon: Error on read in avalon_gets: %s", bfg_strerror(errno, BST_ERRNO));
			return AVA_GETS_ERROR;
		}

		if (first && likely(tv_finish))
			cgtime(tv_finish);

		if (ret >= read_amount)
			return AVA_GETS_OK;

		if (ret > 0) {
			buf += ret;
			read_amount -= ret;
			first = false;
			continue;
		}

		if (thr && thr->work_restart) {
			if (opt_debug) {
				applog(LOG_WARNING,
				       "Avalon: Work restart at %.2f seconds",
				       (float)(rc)/(float)AVALON_TIME_FACTOR);
			}
			return AVA_GETS_RESTART;
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
	}
}

static int avalon_get_result(int fd, struct avalon_result *ar,
			     struct thr_info *thr, struct timeval *tv_finish)
{
	struct cgpu_info *avalon;
	struct avalon_info *info;
	uint8_t result[AVALON_READ_SIZE];
	int ret, read_count;

	avalon = thr->cgpu;
	info = avalon->device_data;
	read_count = info->read_count;

	memset(result, 0, AVALON_READ_SIZE);
	ret = avalon_gets(fd, result, read_count, thr, tv_finish);

	if (ret == AVA_GETS_OK) {
		if (opt_debug) {
			char x[(AVALON_READ_SIZE * 2) + 1];
			bin2hex(x, result, AVALON_READ_SIZE);
			applog(LOG_DEBUG, "Avalon: get: %s", x);
		}
		memcpy((uint8_t *)ar, result, AVALON_READ_SIZE);
	}

	return ret;
}

static bool avalon_decode_nonce(struct thr_info *thr, struct avalon_result *ar,
				uint32_t *nonce)
{
	struct cgpu_info *avalon;
	struct avalon_info *info;
	struct work *work;

	avalon = thr->cgpu;
	if (unlikely(!avalon->works))
		return false;

	work = clone_queued_work_bymidstate(avalon, (char *)ar->midstate, 32,
					   (char *)ar->data, 64, 12);
	if (!work)
		return false;

	info = avalon->device_data;
	info->matching_work[work->subid]++;
	*nonce = htole32(ar->nonce);
	submit_nonce(thr, work, *nonce);
	
	free_work(work);

	return true;
}

static void avalon_get_reset(int fd, struct avalon_result *ar)
{
	int ret;
	const int read_count = AVALON_RESET_FAULT_DECISECONDS * AVALON_TIME_FACTOR;
	
	memset(ar, 0, AVALON_READ_SIZE);
	ret = avalon_gets(fd, (uint8_t*)ar, read_count, NULL, NULL);
	
	if (ret == AVA_GETS_OK && opt_debug) {
		char x[(AVALON_READ_SIZE * 2) + 1];
		bin2hex(x, ar, AVALON_READ_SIZE);
		applog(LOG_DEBUG, "Avalon: get: %s", x);
	}
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

	avalon_get_reset(fd, ar);

	buf = (uint8_t *)ar;
	/* Sometimes there is one extra 0 byte for some reason in the buffer,
	 * so work around it. */
	if (buf[0] == 0)
		buf = (uint8_t  *)(ar + 1);
	if (buf[0] == 0xAA && buf[1] == 0x55 &&
	    buf[2] == 0xAA && buf[3] == 0x55) {
		for (i = 4; i < 11; i++)
			if (buf[i] != 0)
				break;
	}

	p.tv_sec = 0;
	p.tv_nsec = AVALON_RESET_PITCH;
	nanosleep(&p, NULL);

	if (i != 11) {
		applog(LOG_ERR, "Avalon: Reset failed! not an Avalon?"
		       " (%d: %02x %02x %02x %02x)",
		       i, buf[0], buf[1], buf[2], buf[3]);
		/* FIXME: return 1; */
	} else
		applog(LOG_WARNING, "Avalon: Reset succeeded");
	return 0;
}

static void avalon_idle(struct cgpu_info *avalon)
{
	int i, ret;
	struct avalon_task at;

	int fd = avalon->device_fd;
	struct avalon_info *info = avalon->device_data;
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

static
const char *avalon_set_baud(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct avalon_info * const info = proc->device_data;
	const int baud = atoi(newvalue);
	if (!valid_baud(baud))
		return "Invalid baud setting";
	info->baud = baud;
	return NULL;
}

static
const char *avalon_set_miner_count(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct avalon_info * const info = proc->device_data;
	const int miner_count = atoi(newvalue);
	if (miner_count <= 0 || miner_count > AVALON_DEFAULT_MINER_NUM)
		return "Invalid miner_count: must be 1 ~ " AVALON_DEFAULT_MINER_NUM_S;
	info->miner_count = miner_count;
	return NULL;
}

static
const char *avalon_set_asic_count(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct avalon_info * const info = proc->device_data;
	const int asic_count = atoi(newvalue);
	if (asic_count <= 0 || asic_count > AVALON_DEFAULT_ASIC_NUM)
		return "Invalid asic_count: must be 1 ~ " AVALON_DEFAULT_ASIC_NUM_S;
	info->asic_count = asic_count;
	return NULL;
}

static
const char *avalon_set_timeout(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct avalon_info * const info = proc->device_data;
	const int timeout = atoi(newvalue);
	if (timeout <= 0 || timeout > 0xff)
		return "Invalid timeout: must be 1 ~ 255";
	info->timeout = timeout;
	return NULL;
}

static
const char *avalon_set_clock(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct avalon_info * const info = proc->device_data;
	const int clock = atoi(newvalue);
	switch (clock) {
		default:
			return "Invalid clock: must be 256/270/282/300/325/350/375";
		case 256:
		case 270:
		case 282:
		case 300:
		case 325:
		case 350:
		case 375:
			info->frequency = clock;
	}
	return NULL;
}

const struct bfg_set_device_definition avalon_set_device_funcs[] = {
	// NOTE: Order of parameters below is important for --avalon-options
	{"baud"       , avalon_set_baud       , "serial baud rate"},
	{"miner_count", avalon_set_miner_count, ""},
	{"asic_count" , avalon_set_asic_count , ""},
	{"timeout"    , avalon_set_timeout    , "how long the device will work on a work item before accepting new work"},
	{"clock"      , avalon_set_clock      , "clock speed: 256, 270, 282, 300, 325, 350, or 375"},
	{NULL},
};

#ifdef HAVE_CURSES
static
void avalon_wlogprint_status(struct cgpu_info * const proc)
{
	struct avalon_info *info = proc->device_data;
	
	if (((info->temp0?1:0) + (info->temp1?1:0) + (info->temp2?1:0)) > 1)
	{
		wlogprint("Temperatures:");
		if (info->temp0)  wlogprint(" %uC", (unsigned)info->temp0);
		if (info->temp1)  wlogprint(" %uC", (unsigned)info->temp1);
		if (info->temp2)  wlogprint(" %uC", (unsigned)info->temp2);
	}
	wlogprint("\n");
	
	wlogprint("Clock speed: %d\n", info->frequency);
}

static
void avalon_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock speed ");
}

static
const char *avalon_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	switch (input)
	{
		case 'c': case 'C':
			return proc_set_device_tui_wrapper(proc, NULL, avalon_set_clock, "Set clock speed (256, 270, 282, 300, 325, 350, or 375)", NULL);
	}
	return NULL;
}
#endif

/* Non blocking clearing of anything in the buffer */
static void avalon_clear_readbuf(int fd)
{
	ssize_t ret;

	do {
		char buf[AVALON_FTDI_READSIZE];
#ifndef WIN32
		struct timeval timeout;
		fd_set rd;

		timeout.tv_sec = timeout.tv_usec = 0;
		FD_ZERO(&rd);
		FD_SET((SOCKETTYPE)fd, &rd);
		ret = select(fd + 1, &rd, NULL, NULL, &timeout);
		if (ret > 0)
#endif
			// Relies on serial timeout for Windows
			ret = read(fd, buf, AVALON_FTDI_READSIZE);
	} while (ret > 0);
}

static
void avalon_zero_stats(struct cgpu_info * const cgpu)
{
	struct avalon_info *info = cgpu->device_data;
	
	info->temp_max = 0;
	info->no_matching_work = 0;
	
	for (int i = 0; i < info->miner_count; ++i)
		info->matching_work[i] = 0;
}

static bool avalon_detect_one(const char *devpath)
{
	struct avalon_info *info;
	struct avalon_result ar;
	int fd, ret;
	struct cgpu_info *avalon;

	if (serial_claim(devpath, &avalon_drv))
		return false;
	
	info = malloc(sizeof(*info));
	if (unlikely(!info))
		applogr(false, LOG_ERR, "Failed to malloc avalon_info data");
	*info = (struct avalon_info){
		.baud = AVALON_IO_SPEED,
		.miner_count = AVALON_DEFAULT_MINER_NUM - 8,
		.asic_count = AVALON_DEFAULT_ASIC_NUM,
		.timeout = AVALON_DEFAULT_TIMEOUT,
		.frequency = AVALON_DEFAULT_FREQUENCY,
	};
	drv_set_defaults(&avalon_drv, avalon_set_device_funcs, info, devpath, detectone_meta_info.serial, 1);

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s "
	       "(baud=%d miner_count=%d asic_count=%d timeout=%d frequency=%d)",
	       devpath, info->baud, info->miner_count, info->asic_count, info->timeout, info->frequency);

	fd = avalon_open2(devpath, info->baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		free(info);
		return false;
	}
	avalon_clear_readbuf(fd);

	/* We have a real Avalon! */
	avalon = calloc(1, sizeof(struct cgpu_info));
	avalon->drv = &avalon_drv;
	avalon->device_path = strdup(devpath);
	avalon->device_fd = fd;
	avalon->threads = AVALON_MINER_THREADS;
	avalon->set_device_funcs = avalon_set_device_funcs;
	add_cgpu(avalon);

	ret = avalon_reset(fd, &ar);
	if (ret) {
		; /* FIXME: I think IT IS avalon and wait on reset;
		   * avalon_close(fd);
		   * free(info);
		   * return false; */
	}
	
	applog(LOG_INFO, "Avalon Detect: Found at %s, mark as %d",
	       devpath, avalon->device_id);

	avalon->device_data = info;

	info->read_count = ((float)info->timeout * AVALON_HASH_TIME_FACTOR *
			    AVALON_TIME_FACTOR) / (float)info->miner_count;

	info->fan_pwm = AVALON_DEFAULT_FAN_MIN_PWM;
	avalon_zero_stats(avalon);
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;

	/* Set asic to idle mode after detect */
	avalon_idle(avalon);
	avalon->device_fd = -1;

	avalon_close(fd);
	return true;
}

static
bool avalon_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, avalon_detect_one);
}

static void __avalon_init(struct cgpu_info *avalon)
{
	applog(LOG_INFO, "Avalon: Opened on %s", avalon->device_path);
}

static void avalon_init(struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;
	struct avalon_result ar;
	int fd, ret;
	
	cgpu_set_defaults(avalon);
	avalon->set_device_funcs = NULL;

	avalon->device_fd = -1;
	fd = avalon_open(avalon->device_path, info->baud);
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
	__avalon_init(avalon);
}

static bool avalon_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon->device_data;

	free(avalon->works);
	avalon->works = calloc(info->miner_count * sizeof(struct work *),
			       AVALON_ARRAY_SIZE);
	if (!avalon->works)
		quithere(1, "Failed to calloc avalon works");
	if (avalon->device_fd == -1)
		avalon_init(avalon);
	else
		__avalon_init(avalon);

	avalon->status = LIFE_INIT2;
	return true;
}

static void avalon_free_work(struct thr_info *thr)
{
	struct cgpu_info *avalon;
	struct avalon_info *info;
	struct work **works;
	int i;

	avalon = thr->cgpu;
	avalon->queued = 0;
	if (unlikely(!avalon->works))
		return;
	works = avalon->works;
	info = avalon->device_data;

	for (i = 0; i < info->miner_count * 4; i++) {
		if (works[i]) {
			work_completed(avalon, works[i]);
			works[i] = NULL;
		}
	}
}

static void do_avalon_close(struct thr_info *thr)
{
	struct avalon_result ar;
	struct cgpu_info *avalon = thr->cgpu;
	struct avalon_info *info = avalon->device_data;

	avalon_free_work(thr);
	cgsleep_ms(1000);
	avalon_reset(avalon->device_fd, &ar);
	avalon_idle(avalon);
	avalon_close(avalon->device_fd);
	avalon->device_fd = -1;

	info->no_matching_work = 0;
}

static inline void record_temp_fan(struct avalon_info *info, struct avalon_result *ar, float *temp_avg)
{
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

	*temp_avg = info->temp2 > info->temp1 ? info->temp2 : info->temp1;

	if (info->temp0 > info->temp_max)
		info->temp_max = info->temp0;
	if (info->temp1 > info->temp_max)
		info->temp_max = info->temp1;
	if (info->temp2 > info->temp_max)
		info->temp_max = info->temp2;
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

/* We use a replacement algorithm to only remove references to work done from
 * the buffer when we need the extra space for new work. */
static bool avalon_fill(struct cgpu_info *avalon)
{
	struct avalon_info *info = avalon->device_data;
	int subid, slot, mc;
	struct work *work;

	mc = info->miner_count;
	if (avalon->queued >= mc)
		return true;
	work = get_queued(avalon);
	if (unlikely(!work))
		return false;
	subid = avalon->queued++;
	work->subid = subid;
	slot = avalon->work_array * mc + subid;
	if (likely(avalon->works[slot]))
		work_completed(avalon, avalon->works[slot]);
	avalon->works[slot] = work;
	if (avalon->queued >= mc)
		return true;
	return false;
}

static void avalon_rotate_array(struct cgpu_info *avalon)
{
	avalon->queued = 0;
	if (++avalon->work_array >= AVALON_ARRAY_SIZE)
		avalon->work_array = 0;
}

static int64_t avalon_scanhash(struct thr_info *thr)
{
	struct cgpu_info *avalon;
	struct work **works;
	int fd, ret = AVA_GETS_OK, full;

	struct avalon_info *info;
	struct avalon_task at;
	struct avalon_result ar;
	int i;
	int avalon_get_work_count;
	int start_count, end_count;

	struct timeval tv_start, tv_finish, elapsed;
	uint32_t nonce;
	int64_t hash_count;
	static int first_try = 0;
	int result_wrong;

	avalon = thr->cgpu;
	works = avalon->works;
	info = avalon->device_data;
	avalon_get_work_count = info->miner_count;

	if (unlikely(avalon->device_fd == -1)) {
		if (!avalon_prepare(thr)) {
			applog(LOG_ERR, "AVA%i: Comms error(open)",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			/* fail the device if the reopen attempt fails */
			return -1;
		}
	}
	fd = avalon->device_fd;
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif

	start_count = avalon->work_array * avalon_get_work_count;
	end_count = start_count + avalon_get_work_count;
	i = start_count;
	while (true) {
		avalon_init_task(&at, 0, 0, info->fan_pwm,
				 info->timeout, info->asic_count,
				 info->miner_count, 1, 0, info->frequency);
		avalon_create_task(&at, works[i]);
		ret = avalon_send_task(fd, &at, avalon);
		if (unlikely(ret == AVA_SEND_ERROR ||
			     (ret == AVA_SEND_BUFFER_EMPTY &&
			      (i + 1 == end_count) &&
			      first_try))) {
			do_avalon_close(thr);
			applog(LOG_ERR, "AVA%i: Comms error(buffer)",
			       avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			first_try = 0;
			cgsleep_ms(1000);
			avalon_init(avalon);
			return 0;	/* This should never happen */
		}
		if (ret == AVA_SEND_BUFFER_EMPTY && (i + 1 == end_count)) {
			first_try = 1;
			avalon_rotate_array(avalon);
			return 0xffffffff;
		}

		works[i]->blk.nonce = 0xffffffff;

		if (ret == AVA_SEND_BUFFER_FULL)
			break;

		i++;
	}
	if (unlikely(first_try))
		first_try = 0;

	elapsed.tv_sec = elapsed.tv_usec = 0;
	cgtime(&tv_start);

	result_wrong = 0;
	hash_count = 0;
	while (true) {
		full = avalon_buffer_full(fd);
		applog(LOG_DEBUG, "Avalon: Buffer full: %s",
		       ((full == AVA_BUFFER_FULL) ? "Yes" : "No"));
		if (unlikely(full == AVA_BUFFER_EMPTY))
			break;

		ret = avalon_get_result(fd, &ar, thr, &tv_finish);
		if (unlikely(ret == AVA_GETS_ERROR)) {
			do_avalon_close(thr);
			applog(LOG_ERR,
			       "AVA%i: Comms error(read)", avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);
			return 0;
		}
		if (unlikely(ret == AVA_GETS_RESTART))
			break;
		if (unlikely(ret == AVA_GETS_TIMEOUT)) {
			timersub(&tv_finish, &tv_start, &elapsed);
			applog(LOG_DEBUG, "Avalon: no nonce in (%ld.%06lds)",
			       (long)elapsed.tv_sec, (long)elapsed.tv_usec);
			continue;
		}

		if (!avalon_decode_nonce(thr, &ar, &nonce)) {
			info->no_matching_work++;
			result_wrong++;

			if (unlikely(result_wrong >= avalon_get_work_count))
				break;

			if (opt_debug) {
				timersub(&tv_finish, &tv_start, &elapsed);
				applog(LOG_DEBUG,"Avalon: no matching work: %d"
				" (%ld.%06lds)", info->no_matching_work,
				(long)elapsed.tv_sec, (long)elapsed.tv_usec);
			}
			continue;
		}

		hash_count += 0xffffffff;
		if (opt_debug) {
			timersub(&tv_finish, &tv_start, &elapsed);
			applog(LOG_DEBUG,
			       "Avalon: nonce = 0x%08"PRIx32" = 0x%08"PRIx64" hashes "
			       "(%ld.%06lds)", nonce, (uint64_t)hash_count,
			       (long)elapsed.tv_sec, (long)elapsed.tv_usec);
		}
	}
	if (hash_count && avalon->results < AVALON_ARRAY_SIZE)
		avalon->results++;
	if (unlikely((result_wrong >= avalon_get_work_count) ||
	    (!hash_count && ret != AVA_GETS_RESTART && --avalon->results < 0))) {
		/* Look for all invalid results, or consecutive failure
		 * to generate any results suggesting the FPGA
		 * controller has screwed up. */
		do_avalon_close(thr);
		applog(LOG_ERR,
			"AVA%i: FPGA controller messed up, %d wrong results",
			avalon->device_id, result_wrong);
		dev_error(avalon, REASON_DEV_COMMS_ERROR);
		cgsleep_ms(1000);
		avalon_init(avalon);
		return 0;
	}

	avalon_rotate_array(avalon);

	if (hash_count) {
		record_temp_fan(info, &ar, &(avalon->temp));
		avalon->temp = info->temp_max;
		applog(LOG_INFO,
		       "Avalon: Fan1: %d/m, Fan2: %d/m, Fan3: %d/m\t"
		       "Temp1: %dC, Temp2: %dC, Temp3: %dC, TempMAX: %dC",
		       info->fan0, info->fan1, info->fan2,
		       info->temp0, info->temp1, info->temp2, info->temp_max);
		info->temp_history_index++;
		info->temp_sum += avalon->temp;
		applog(LOG_DEBUG, "Avalon: temp_index: %d, temp_count: %d, temp_old: %d",
		       info->temp_history_index, info->temp_history_count, info->temp_old);
		if (info->temp_history_index == info->temp_history_count) {
			adjust_fan(info);
			info->temp_history_index = 0;
			info->temp_sum = 0;
		}
	}

	/* This hashmeter is just a utility counter based on returned shares */
	return hash_count;
}

static struct api_data *avalon_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct avalon_info *info = cgpu->device_data;
	int i;

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
	for (i = 0; i < info->miner_count; i++) {
		char mcw[24];

		sprintf(mcw, "match_work_count%d", i + 1);
		root = api_add_int(root, mcw, &(info->matching_work[i]), false);
	}

	return root;
}

static void avalon_shutdown(struct thr_info *thr)
{
	do_avalon_close(thr);
}

struct device_drv avalon_drv = {
	.dname = "avalon",
	.name = "AVA",
	.lowl_probe_by_name_only = true,
	.lowl_probe = avalon_lowl_probe,
	.thread_prepare = avalon_prepare,
	.minerloop = hash_queued_work,
	.queue_full = avalon_fill,
	.scanwork = avalon_scanhash,
	.zero_stats = avalon_zero_stats,
	.get_api_stats = avalon_api_stats,
	.reinit_device = avalon_init,
	.thread_shutdown = avalon_shutdown,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = avalon_wlogprint_status,
	.proc_tui_wlogprint_choices = avalon_tui_wlogprint_choices,
	.proc_tui_handle_choice = avalon_tui_handle_choice,
#endif
};
