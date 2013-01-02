/*
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
#include "avalon.h"

/* Ensure the sizes are correct for the Serial read */
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

/* One for each possible device */
struct device_api avalon_api;

static inline void rev(uint8_t *s, size_t l)
{
	size_t i, j;
	uint8_t t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

/* TODO: modify from lancelot to avalon */
static inline void avalon_create_task(uint8_t *t, struct work *work)
{
	memset(t, 0, 64);
	memcpy(t, work->midstate, 32);
	memcpy(t + 52, work->data + 64, 12);
	rev(t, 32);
	rev(t + 52, 12);
}

/* TODO:receive data from avalon */
static void *avalon_gets(void *userdata)
{
	ssize_t ret = 0;
	int rc = 0;
	int read_amount = AVALON_READ_SIZE;
	uint8_t buf[AVALON_READ_SIZE];

	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	struct cgpu_info *avalon = mythr->cgpu;
	struct device_api *api = avalon->api;

	int fd = avalon->device_fd;

	uint8_t *buf_p = buf;
	// Read reply 1 byte at a time to get earliest tv_finish
	while (true) {
		ret = read(fd, buf_p, 1);

		/* Not return should be continue */
		if (ret < 0)
			continue;

		if (ret >= read_amount) {
			/* Match the work in avalon_send_buffer
			 * send signal to miner thread */
			buf_p = buf;
			if (opt_debug) {
				applog(LOG_DEBUG,
				       "Avalon Read: counte: ", ret);
			}
			continue;
		}

		if (ret > 0) {
			buf_p += ret;
			read_amount -= ret;
			continue;
		}
			
		/* There is no TIMEOUT in avalon read */
		rc++;
		if (rc >= 8) {
			if (opt_debug) {
				applog(LOG_DEBUG,
				       "Avalon Read: No data in %d seconds", rc);
			}
			buf_p = buf;
			rc = 0;
			continue;
		}

		if (mythr && mythr->work_restart) {
			rc = 0;
			buf_p = buf;
			if (opt_debug) {
				applog(LOG_DEBUG,
					"Avalon Read: Work restart at %.2f seconds",
					(float)(rc)/(float)TIME_FACTOR);
			}
			continue;
		}
		/* TODO: maybe we should nanosleep() a little here */
	}
out:
	if (api->thread_shutdown)
		api->thread_shutdown(mythr);

	thread_reportin(mythr);
	applog(LOG_ERR, "Thread %d failure, exiting", thr_id);
	tq_freeze(mythr->q);

	return NULL;
}

int avalon_gets2(uint8_t *nonce_bin, int *fd, int n)
{
}

/* TODO:
 * 1. there are have to add N ms before two task write */
static int avalon_write(int fd, const void *buf, size_t bufLen)
{
	size_t ret;

	ret = write(fd, buf, bufLen);
	if (unlikely(ret != bufLen))
		return 1;

	return 0;
}

static void do_avalon_close(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;
	avalon_close(avalon->device_fd);
	avalon->device_fd = -1;
}

/* TODO: send AVALON_RESET to device. it will retrun avalon info */
static bool avalon_detect_one(const char *devpath)
{
	int fd;

	const char golden_ob[] =
		"4679ba4ec99876bf4bfe086082b40025"
		"4df6c356451471139a3afa71e48f544a"
		"00000000000000000000000000000000"
		"0000000087320b1a1426674f2fa722ce";

	const char golden_nonce[] = "000187a2";

	uint8_t ob_bin[64], nonce_bin[AVALON_READ_SIZE];
	char *nonce_hex = "000187a2";

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s", devpath);

	fd = avalon_open2(devpath, AVALON_IO_SPEED, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		return false;
	}

	hex2bin(ob_bin, golden_ob, sizeof(ob_bin));
	avalon_write(fd, ob_bin, sizeof(ob_bin));

	memset(nonce_bin, 0, sizeof(nonce_bin));
	avalon_gets2(nonce_bin, fd, NULL);

	avalon_close(fd);

	if (strncmp(nonce_hex, golden_nonce, 8)) {
		applog(LOG_ERR,
			"Avalon Detect: "
			"Test failed at %s: get %s, should: %s",
			devpath, nonce_hex, golden_nonce);
		free(nonce_hex);
		/* return false; FIXME: for testing. already return true */
	}
	applog(LOG_DEBUG,
		"Avalon Detect: "
		"Test succeeded at %s: got %s",
			devpath, nonce_hex);

	/* We have a real Avalon! */
	struct cgpu_info *avalon;
	avalon = calloc(1, sizeof(struct cgpu_info));
	avalon->api = &avalon_api;
	avalon->device_path = strdup(devpath);
	avalon->device_fd = -1;
	avalon->threads = AVALON_MINER_THREADS;	/* The miner_thread */
	mutex_init(&avalon->device_mutex);
	add_cgpu(avalon);

	applog(LOG_INFO, "Found Avalon at %s, mark as %d",
		devpath, avalon->device_id);

	return true;
}

static void avalon_detect()
{
	serial_detect(&avalon_api, avalon_detect_one);
}

static bool avalon_prepare(struct thr_info *thr)
{
	struct cgpu_info *avalon = thr->cgpu;

	struct timeval now;

	avalon->device_fd = -1;

	int fd = avalon_open(avalon->device_path, AVALON_IO_SPEED);
	if (unlikely(-1 == fd)) {
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

/* TODO:
 * 1. write work to device
 * 2. while CTS HIGH, read data
 * 3. match to work
 * 4. submit nonce */
static int64_t avalon_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused int64_t max_nonce)
{
	struct cgpu_info *avalon;
	int fd;
	int ret;

	uint8_t ob_bin[64], nonce_bin[AVALON_READ_SIZE];
	char *ob_hex;
	uint32_t nonce;
	int64_t hash_count;
	int curr_hw_errors, i;
	bool was_hw_error;

	int count;
	int read_count;
	uint32_t values;

	avalon = thr->cgpu;
	if (avalon->device_fd == -1)
			return -1;

	fd = avalon->device_fd;

	avalon_create_task(ob_bin, work);
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif
	/* TODO: write 20 task */
	ret = avalon_write(fd, ob_bin, sizeof(ob_bin));
	if (ret) {
		do_avalon_close(thr);
		applog(LOG_ERR, "AVA%i: Comms error", avalon->device_id);
		dev_error(avalon, REASON_DEV_COMMS_ERROR);
		return 0;	/* This should never happen */
	}

	/* Avalon will return 4 bytes (AVALON_READ_SIZE) nonces or nothing */
	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = avalon_gets(thr);
	if (ret == AVA_GETS_ERROR) {
		do_avalon_close(thr);
		applog(LOG_ERR, "ICA%i: Comms error", avalon->device_id);
		dev_error(avalon, REASON_DEV_COMMS_ERROR);
		return 0;
	}

	work->blk.nonce = 0xffffffff;

	memcpy((char *)&nonce, nonce_bin, sizeof(nonce_bin));

#if !defined (__BIG_ENDIAN__) && !defined(MIPSEB)
	nonce = swab32(nonce);
#endif

	curr_hw_errors = avalon->hw_errors;
	submit_nonce(thr, work, nonce);
	was_hw_error = (curr_hw_errors > avalon->hw_errors);

	// Force a USB close/reopen on any hw error
	if (was_hw_error)
		do_avalon_close(thr);

	if (opt_debug) {
		ob_hex = bin2hex(ob_bin, sizeof(ob_bin));
		applog(LOG_DEBUG, "Avalon %d sent: %s",
			avalon->device_id, ob_hex);
		free(ob_hex);
	}

	return hash_count;
}

/* TODO: close the avalon_read_thread */
static void avalon_shutdown(struct thr_info *thr)
{
	do_avalon_close(thr);
}

struct device_api avalon_api = {
	.dname = "avalon",
	.name = "AVA",
	.api_detect = avalon_detect,
	.thread_prepare = avalon_prepare,
	.scanhash = avalon_scanhash,
	.thread_shutdown = avalon_shutdown,
};
