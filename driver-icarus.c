/*
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Xiangfu <xiangfu@openmobilefree.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

/*
 * Those code should be works fine with V2 and V3 bitstream of Icarus.
 * Operation:
 *   No detection implement.
 *   Input: 64B = 32B midstate + 20B fill bytes + last 12 bytes of block head.
 *   Return: send back 32bits immediately when Icarus found a valid nonce.
 *           no query protocol implemented here, if no data send back in ~11.3
 *           seconds (full cover time on 32bit nonce range by 380MH/s speed)
 *           just send another work.
 * Notice:
 *   1. Icarus will start calculate when you push a work to them, even they
 *      are busy.
 *   2. The 2 FPGAs on Icarus will distribute the job, one will calculate the
 *      0 ~ 7FFFFFFF, another one will cover the 80000000 ~ FFFFFFFF.
 *   3. It's possible for 2 FPGAs both find valid nonce in the meantime, the 2
 *      valid nonce will all be send back.
 *   4. Icarus will stop work when: a valid nonce has been found or 32 bits
 *      nonce range is completely calculated.
 */

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
#ifdef HAVE_SYS_EPOLL_H
  #include <sys/epoll.h>
  #define HAVE_EPOLL
#endif

#include "elist.h"
#include "miner.h"

// 8 second timeout
#define ICARUS_READ_FAULT_DECISECONDS (1)
#define ICARUS_READ_FAULT_COUNT	(80)

struct device_api icarus_api;

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

static int icarus_open(const char *devpath)
{
#ifndef WIN32
	struct termios my_termios;

	int serialfd = open(devpath, O_RDWR | O_CLOEXEC | O_NOCTTY);

	if (serialfd == -1)
		return -1;

	tcgetattr(serialfd, &my_termios);
	my_termios.c_cflag = B115200;
	my_termios.c_cflag |= CS8;
	my_termios.c_cflag |= CREAD;
	my_termios.c_cflag |= CLOCAL;
	my_termios.c_cflag &= ~(CSIZE | PARENB);

	my_termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK |
				ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	my_termios.c_oflag &= ~OPOST;
	my_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	my_termios.c_cc[VTIME] = ICARUS_READ_FAULT_DECISECONDS;
	my_termios.c_cc[VMIN] = 0;
	tcsetattr(serialfd, TCSANOW, &my_termios);

	tcflush(serialfd, TCOFLUSH);
	tcflush(serialfd, TCIFLUSH);

	return serialfd;
#else
	COMMCONFIG comCfg;

	HANDLE hSerial = CreateFile(devpath, GENERIC_READ | GENERIC_WRITE, 0,
				    NULL, OPEN_EXISTING, 0, NULL);
	if (unlikely(hSerial == INVALID_HANDLE_VALUE))
		return -1;

	// thanks to af_newbie for pointers about this
	memset(&comCfg, 0 , sizeof(comCfg));
	comCfg.dwSize = sizeof(COMMCONFIG);
	comCfg.wVersion = 1;
	comCfg.dcb.DCBlength = sizeof(DCB);
	comCfg.dcb.BaudRate = 115200;
	comCfg.dcb.fBinary = 1;
	comCfg.dcb.fDtrControl = DTR_CONTROL_ENABLE;
	comCfg.dcb.fRtsControl = RTS_CONTROL_ENABLE;
	comCfg.dcb.ByteSize = 8;

	SetCommConfig(hSerial, &comCfg, sizeof(comCfg));

	const DWORD ctoms = ICARUS_READ_FAULT_DECISECONDS * 100;
	COMMTIMEOUTS cto = {ctoms, 0, ctoms, 0, ctoms};
	SetCommTimeouts(hSerial, &cto);

	return _open_osfhandle((LONG)hSerial, 0);
#endif
}

static int icarus_gets(unsigned char *buf, size_t bufLen, int fd, volatile unsigned long *wr, int read_count)
{
	ssize_t ret = 0;
	int rc = 0;
	int epollfd = -1;

#ifdef HAVE_EPOLL
	struct epoll_event ev, evr;
	epollfd = epoll_create(1);
	if (epollfd != -1) {
		ev.events = EPOLLIN;
		ev.data.fd = fd;
		if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
			close(epollfd);
			epollfd = -1;
		}
	}
#endif

	while (bufLen) {
#ifdef HAVE_EPOLL
		if (epollfd != -1 && epoll_wait(epollfd, &evr, 1, ICARUS_READ_FAULT_DECISECONDS * 100) != 1)
			ret = 0;
		else
#endif
		ret = read(fd, buf, 1);
		if (ret == 1) {
			bufLen--;
			buf++;
			continue;
		}

		rc++;
		if (*wr) {
			rc *= ICARUS_READ_FAULT_DECISECONDS;
			applog(LOG_DEBUG,
			       "Icarus Read: Work restart at %d.%d seconds", rc / 10, rc % 10);
			return 1;
		}
		if (rc >= read_count) {
			if (epollfd != -1)
				close(epollfd);
			rc *= ICARUS_READ_FAULT_DECISECONDS;
			applog(LOG_DEBUG,
			       "Icarus Read: No data in %d.%d seconds", rc / 10, rc % 10);
			return 1;
		}
	}

	if (epollfd != -1)
		close(epollfd);

	return 0;
}

static int icarus_write(int fd, const void *buf, size_t bufLen)
{
	size_t ret;

	ret = write(fd, buf, bufLen);
	if (unlikely(ret != bufLen))
		return 1;

	return 0;
}

#define icarus_close(fd) close(fd)

static bool icarus_detect_one(const char *devpath)
{
	int fd;

	// Block 171874 nonce = (0xa2870100) = 0x000187a2
	// N.B. golden_ob MUST take less time to calculate
	//	than the timeout set in icarus_open()
	//	This one takes ~0.53ms on Rev3 Icarus
	const char golden_ob[] =
		"4679ba4ec99876bf4bfe086082b40025"
		"4df6c356451471139a3afa71e48f544a"
		"00000000000000000000000000000000"
		"0000000087320b1a1426674f2fa722ce";

	const char golden_nonce[] = "000187a2";

	unsigned char ob_bin[64], nonce_bin[4];
	char *nonce_hex;

	if (total_devices == MAX_DEVICES)
		return false;

	fd = icarus_open(devpath);
	if (unlikely(fd == -1)) {
		applog(LOG_ERR, "Icarus Detect: Failed to open %s", devpath);
		return false;
	}

	hex2bin(ob_bin, golden_ob, sizeof(ob_bin));
	icarus_write(fd, ob_bin, sizeof(ob_bin));

	memset(nonce_bin, 0, sizeof(nonce_bin));
	volatile unsigned long wr = 0;
	icarus_gets(nonce_bin, sizeof(nonce_bin), fd, &wr, 1);

	icarus_close(fd);

	nonce_hex = bin2hex(nonce_bin, sizeof(nonce_bin));
	if (nonce_hex) {
		if (strncmp(nonce_hex, golden_nonce, 8)) {
			applog(LOG_ERR, 
			       "Icarus Detect: "
			       "Test failed at %s: get %s, should: %s",
			       devpath, nonce_hex, golden_nonce);
			free(nonce_hex);
			return false;
		}
		applog(LOG_DEBUG, 
		       "Icarus Detect: "
		       "Test succeeded at %s: got %s",
			       devpath, nonce_hex);
		free(nonce_hex);
	} else
		return false;

	/* We have a real Icarus! */
	struct cgpu_info *icarus;
	icarus = calloc(1, sizeof(struct cgpu_info));
	icarus->api = &icarus_api;
	icarus->device_path = strdup(devpath);
	icarus->threads = 1;
	add_cgpu(icarus);

	applog(LOG_INFO, "Found Icarus at %s, mark as %d",
	       devpath, icarus->device_id);

	return true;
}

static void icarus_detect()
{
	struct string_elist *iter, *tmp;
	const char*s;

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		s = iter->string;
		if (!strncmp("icarus:", iter->string, 7))
			s += 7;
		if (!strcmp(s, "auto") || !strcmp(s, "noauto"))
			continue;
		if (icarus_detect_one(s))
			string_elist_del(iter);
	}
}

static bool icarus_prepare(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;

	struct timeval now;

	applog(LOG_INFO, "Opened Icarus on %s", icarus->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(icarus->init, &now);

	return true;
}

static uint64_t icarus_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused uint64_t max_nonce)
{
	volatile unsigned long *wr = &work_restart[thr->id].restart;

	struct cgpu_info *icarus;
	int fd;
	int ret;

	unsigned char ob_bin[64], nonce_bin[4];
	char *ob_hex, *nonce_hex;
	uint32_t nonce;
	uint32_t hash_count;
	struct timeval tv_start, tv_end, elapsed;

	icarus = thr->cgpu;

	fd = icarus_open(icarus->device_path);
	if (unlikely(-1 == fd)) {
		applog(LOG_ERR, "Failed to open Icarus on %s",
		       icarus->device_path);
		return 0;
	}

	memset(ob_bin, 0, sizeof(ob_bin));
	memcpy(ob_bin, work->midstate, 32);
	memcpy(ob_bin + 52, work->data + 64, 12);
	rev(ob_bin, 32);
	rev(ob_bin + 52, 12);
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif

	gettimeofday(&tv_start, NULL);

	ret = icarus_write(fd, ob_bin, sizeof(ob_bin));
	if (ret) {
		icarus_close(fd);
		return 0;	/* This should never happen */
	}

	ob_hex = bin2hex(ob_bin, sizeof(ob_bin));
	if (ob_hex) {
		applog(LOG_DEBUG, "Icarus %d sent: %s",
		       icarus->device_id, ob_hex);
		free(ob_hex);
	}

	/* Icarus will return 8 bytes nonces or nothing */
	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = icarus_gets(nonce_bin, sizeof(nonce_bin), fd, wr,
	                  ICARUS_READ_FAULT_COUNT);

	gettimeofday(&tv_end, NULL);
	timeval_subtract(&elapsed, &tv_end, &tv_start);

	memcpy((char *)&nonce, nonce_bin, sizeof(nonce_bin));

	work->blk.nonce = 0xffffffff;
	icarus_close(fd);

	// aborted before becoming idle, get new work
	if (nonce == 0 && ret) {
		uint32_t ESTIMATE_HASHES;
		if (unlikely(elapsed.tv_sec > 12 || (elapsed.tv_sec == 11 && elapsed.tv_usec > 300067)))
			ESTIMATE_HASHES = 0xffffffff;
		else
			// Approximately how much of the nonce Icarus scans in 1 second...
			// 0x16a7a561 would be if it was exactly 380 MH/s
			// 0x168b7b4b was the average over a 201-sample period based on time to find actual shares
			ESTIMATE_HASHES = (0x168b7b4b * elapsed.tv_sec) + (0x17a * elapsed.tv_usec);
		applog(LOG_DEBUG, "Icarus %d no nonce = 0x%08x hashes (%ld.%06lds)",
			icarus->device_id, ESTIMATE_HASHES, elapsed.tv_sec, elapsed.tv_usec);
		return ESTIMATE_HASHES;
	}

#ifndef __BIG_ENDIAN__
	nonce = swab32(nonce);
#endif
	submit_nonce(thr, work, nonce);

	nonce_hex = bin2hex(nonce_bin, sizeof(nonce_bin));
	if (nonce_hex) {
		applog(LOG_DEBUG, "Icarus %d returned (elapsed %ld.%06ld seconds): %s",
		       icarus->device_id, elapsed.tv_sec, elapsed.tv_usec, nonce_hex);
		free(nonce_hex);
	}

	hash_count = (nonce & 0x7fffffff);
        if (hash_count == 0)
		hash_count = 2;
        else {
                if (hash_count++ == 0x7fffffff)
                        hash_count = 0xffffffff;
                else
                        hash_count <<= 1;
        }

	applog(LOG_DEBUG, "Icarus %d nonce = 0x%08x = 0x%08x hashes (%ld.%06lds)",
			icarus->device_id, nonce, hash_count, elapsed.tv_sec, elapsed.tv_usec);

        return hash_count;
}

static void icarus_shutdown(struct thr_info *thr)
{
	struct cgpu_info *icarus;

	if (thr->cgpu) {
		icarus = thr->cgpu;

		if (icarus->device_path)
			free(icarus->device_path);

		devices[icarus->device_id] = NULL;
		free(icarus);

		thr->cgpu = NULL;
	}
}

struct device_api icarus_api = {
	.dname = "icarus",
	.name = "ICA",
	.api_detect = icarus_detect,
	.thread_prepare = icarus_prepare,
	.scanhash = icarus_scanhash,
	.thread_shutdown = icarus_shutdown,
};
