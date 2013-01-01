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

// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
#define AVALON_IO_SPEED 115200

// The size of a successful nonce read
#define AVALON_READ_SIZE 4	/* Should be 48 */

#define TIME_FACTOR 10

// Ensure the sizes are correct for the Serial read
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

#define AVALON_READ_FAULT_DECISECONDS 1

// One for each possible device
struct device_api avalon_api;

static void rev(unsigned char *s, size_t l)
{
	size_t i, j;
	unsigned char t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

#define avalon_open2(devpath, baud, purge)  serial_open(devpath, baud, AVALON_READ_FAULT_DECISECONDS, purge)
#define avalon_open(devpath, baud)  avalon_open2(devpath, baud, false)

#define AVA_GETS_ERROR -1
#define AVA_GETS_OK 0
#define AVA_GETS_RESTART 1
#define AVA_GETS_TIMEOUT 2

/* TODO: this should be a avalon_read_thread
 * 1. receive data from avalon
 * 2. match the data to avalon_send_buffer
 * 3. send AVALON_FOUND_NONCE signal to work-thread_id
 */
static int avalon_gets(unsigned char *buf, int fd, struct thr_info *thr)
{
	ssize_t ret = 0;
	int rc = 0;
	int read_amount = AVALON_READ_SIZE;
	// Read reply 1 byte at a time to get earliest tv_finish
	while (true) {
		ret = read(fd, buf, 1);

		/* Not return should be continue */
		if (ret < 0)
			return AVA_GETS_ERROR;

		/* Match the work in avalon_send_buffer
		 * send signal to miner thread */
		if (ret >= read_amount)
			return AVA_GETS_OK;

		if (ret > 0) {
			buf += ret;
			read_amount -= ret;
			continue;
		}
			
		/* There is no TIMEOUT in avalon read */
		rc++;
		if (rc >= 8) {
			if (opt_debug) {
				applog(LOG_DEBUG,
					"Avalon Read: No data in %.2f seconds",
					(float)rc/(float)TIME_FACTOR);
			}
			return AVA_GETS_TIMEOUT;
		}

		/* TODO: not sure about this */
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

/* TODO: add mutex on write
 * 1. add the last_write time
 * 2. there are have to N ms befoew two task write */
static int avalon_write(int fd, const void *buf, size_t bufLen)
{
	size_t ret;

	ret = write(fd, buf, bufLen);
	if (unlikely(ret != bufLen))
		return 1;

	return 0;
}

#define avalon_close(fd) close(fd)

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

	unsigned char ob_bin[64], nonce_bin[AVALON_READ_SIZE];
	char *nonce_hex;

	applog(LOG_DEBUG, "Avalon Detect: Attempting to open %s", devpath);

	fd = avalon_open2(devpath, AVALON_IO_SPEED, true);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Avalon Detect: Failed to open %s", devpath);
		return false;
	}

	hex2bin(ob_bin, golden_ob, sizeof(ob_bin));
	avalon_write(fd, ob_bin, sizeof(ob_bin));

	memset(nonce_bin, 0, sizeof(nonce_bin));
	avalon_gets(nonce_bin, fd, NULL);

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
		"Avalon Detect: "
		"Test succeeded at %s: got %s",
			devpath, nonce_hex);
	free(nonce_hex);

	/* We have a real Avalon! */
	struct cgpu_info *avalon;
	avalon = calloc(1, sizeof(struct cgpu_info));
	avalon->api = &avalon_api;
	avalon->device_path = strdup(devpath);
	avalon->device_fd = -1;
	avalon->threads = 1;	/* The miner_thread */
	add_cgpu(avalon);

	applog(LOG_INFO, "Found Avalon at %s, mark as %d",
		devpath, avalon->device_id);

	return true;
}

static void avalon_detect()
{
	serial_detect(&avalon_api, avalon_detect_one);
}

/* TODO:
 * 1. check if the avalon_read_thread started
 * 2. start the avalon_read_thread */
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
 * 1. write work to device (mutex on write)
 *    add work to avalon_send_buffer (mutex on add)
 * 2. wait signal from avalon_read_thread in N seconds
 * 3. N seconds reach, retrun 0xffffffff
 * 4. receive AVALON_FOUND_NONCE signal
 * 5. remove work from avalon_send_buff
 * 6. submit_nonce */
static int64_t avalon_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused int64_t max_nonce)
{
	struct cgpu_info *avalon;
	int fd;
	int ret;

	unsigned char ob_bin[64], nonce_bin[AVALON_READ_SIZE];
	char *ob_hex;
	uint32_t nonce;
	int curr_hw_errors;
	bool was_hw_error;

	avalon = thr->cgpu;
	if (avalon->device_fd == -1)
		if (!avalon_prepare(thr)) {
			applog(LOG_ERR, "AVA%i: Comms error", avalon->device_id);
			dev_error(avalon, REASON_DEV_COMMS_ERROR);

			// fail the device if the reopen attempt fails
			return -1;
		}

	fd = avalon->device_fd;

	memset(ob_bin, 0, sizeof(ob_bin));
	memcpy(ob_bin, work->midstate, 32);
	memcpy(ob_bin + 52, work->data + 64, 12);
	rev(ob_bin, 32);
	rev(ob_bin + 52, 12);
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif
	ret = avalon_write(fd, ob_bin, sizeof(ob_bin));
	if (ret) {
		do_avalon_close(thr);
		applog(LOG_ERR, "AVA%i: Comms error", avalon->device_id);
		dev_error(avalon, REASON_DEV_COMMS_ERROR);
		return 0;	/* This should never happen */
	}


	if (opt_debug) {
		ob_hex = bin2hex(ob_bin, sizeof(ob_bin));
		applog(LOG_DEBUG, "Avalon %d sent: %s",
			avalon->device_id, ob_hex);
		free(ob_hex);
	}

	/* Avalon will return 4 bytes (AVALON_READ_SIZE) nonces or nothing */
	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = avalon_gets(nonce_bin, fd, thr);
	if (ret == AVA_GETS_ERROR) {
		do_avalon_close(thr);
		applog(LOG_ERR, "AVA%i: Comms error", avalon->device_id);
		dev_error(avalon, REASON_DEV_COMMS_ERROR);
		return 0;
	}

	work->blk.nonce = 0xffffffff;

	// aborted before becoming idle, get new work
	if (ret == AVA_GETS_TIMEOUT || ret == AVA_GETS_RESTART) {
		return 0xFFFFFFFF;
	}

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

	// ignore possible end condition values ... and hw errors
	return 0xffffffff;
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
