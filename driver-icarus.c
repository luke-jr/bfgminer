/*
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Xiangfu <xiangfu@openmobilefree.com>
 * Copyright 2012 Andrew Smith
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

#include "elist.h"
#include "miner.h"

// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
#define ICARUS_IO_SPEED 115200

// The size of a successful nonce read
#define ICARUS_READ_SIZE 4

// Ensure the sizes are correct for the Serial read
#if (ICARUS_READ_SIZE != 4)
#error ICARUS_READ_SIZE must be 4
#endif
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

#define ICARUS_READ_TIME ((double)ICARUS_READ_SIZE * (double)8.0 / (double)ICARUS_IO_SPEED)

// Fraction of a second, USB timeout is measured in
// i.e. 10 means 1/10 of a second
#define TIME_FACTOR 10
// In Linux it's 10 per second, thus value = 10/TIME_FACTOR =
#define LINUX_TIMEOUT_VALUE 1
// In Windows it's 1000 per second, thus value = 1000/TIME_FACTOR =
#define WINDOWS_TIMEOUT_VALUE 100

// In timing mode: Default starting value until an estimate can be obtained
// 5 seconds allows for up to a ~840MH/s device
#define ICARUS_READ_COUNT_TIMING	(5 * TIME_FACTOR)

// For a standard Icarus REV3 (to 5 places)
// Since this rounds up a the last digit - it is a slight overestimate
// Thus the hash rate will be a VERY slight underestimate
// (by a lot less than the displayed accuracy)
#define ICARUS_HASH_TIME 0.0000000026316
#define NANOSEC 1000000000.0

// Icarus Rev3 doesn't send a completion message when it finishes
// the full nonce range, so to avoid being idle we must abort the
// work (by starting a new work) shortly before it finishes
//
// Thus we need to estimate 2 things:
//	1) How many hashes were done if the work was aborted
//	2) How high can the timeout be before the Icarus is idle,
//		to minimise the number of work started
//	We set 2) to 'the calculated estimate' - 1
//	to ensure the estimate ends before idle
//
// The simple calculation used is:
//	Tn = Total time in seconds to calculate n hashes
//	Hs = seconds per hash
//	Xn = number of hashes
//	W  = code overhead per work
//
// Rough but reasonable estimate:
//	Tn = Hs * Xn + W	(of the form y = mx + b)
//
// Thus:
//	Line of best fit (using least squares)
//
//	Hs = (n*Sum(XiTi)-Sum(Xi)*Sum(Ti))/(n*Sum(Xi^2)-Sum(Xi)^2)
//	W = Sum(Ti)/n - (Hs*Sum(Xi))/n
//
// N.B. W is less when aborting work since we aren't waiting for the reply
//	to be transferred back (ICARUS_READ_TIME)
//	Calculating the hashes aborted at n seconds is thus just n/Hs
//	(though this is still a slight overestimate due to code delays)
//

// Both below must be exceeded to complete a set of data
// Minimum how long after the first, the last data point must be
#define HISTORY_SEC 60
// Minimum how many points a single ICARUS_HISTORY should have
#define MIN_DATA_COUNT 5
// The value above used is doubled each history until it exceeds:
#define MAX_MIN_DATA_COUNT 100

static struct timeval history_sec = { HISTORY_SEC, 0 };

// Store the last INFO_HISTORY data sets
// [0] = current data, not yet ready to be included as an estimate
// Each new data set throws the last old set off the end thus
// keeping a ongoing average of recent data
#define INFO_HISTORY 10

struct ICARUS_HISTORY {
	struct timeval finish;
	double sumXiTi;
	double sumXi;
	double sumTi;
	double sumXi2;
	uint32_t values;
	uint32_t hash_count_min;
	uint32_t hash_count_max;
};

enum timing_mode { MODE_DEFAULT, MODE_SHORT, MODE_LONG, MODE_VALUE };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_LONG_STR = "long";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

struct ICARUS_INFO {
	// time to calculate the golden_ob
	uint64_t golden_hashes;
	struct timeval golden_tv;

	struct ICARUS_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	// seconds per Hash
	double Hs;
	int read_count;

	enum timing_mode timing_mode;
	bool do_icarus_timing;

	double fullnonce;
	int count;
	double W;
	uint32_t values;
	uint64_t hash_count_range;

	// Determine the cost of history processing
	// (which will only affect W)
	uint64_t history_count;
	struct timeval history_time;
};

// One for each possible device
static struct ICARUS_INFO *icarus_info[MAX_DEVICES];

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
	my_termios.c_cc[VTIME] = LINUX_TIMEOUT_VALUE; /* how long to block */
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
	comCfg.dcb.BaudRate = ICARUS_IO_SPEED;
	comCfg.dcb.fBinary = 1;
	comCfg.dcb.fDtrControl = DTR_CONTROL_ENABLE;
	comCfg.dcb.fRtsControl = RTS_CONTROL_ENABLE;
	comCfg.dcb.ByteSize = 8;

	SetCommConfig(hSerial, &comCfg, sizeof(comCfg));

	// How long to block
	COMMTIMEOUTS cto = {WINDOWS_TIMEOUT_VALUE, 0, WINDOWS_TIMEOUT_VALUE, 0, WINDOWS_TIMEOUT_VALUE};
	SetCommTimeouts(hSerial, &cto);

	return _open_osfhandle((LONG)hSerial, 0);
#endif
}

static int icarus_gets(unsigned char *buf, int fd, struct timeval *tv_finish, int thr_id, int read_count)
{
	ssize_t ret = 0;
	int rc = 0;
	int read_amount = ICARUS_READ_SIZE;

	while (true) {
		ret = read(fd, buf, read_amount);
		gettimeofday(tv_finish, NULL);
		if (ret >= read_amount)
			return 0;

		// Allow a partial I/O
		if (ret > 0) {
			buf += ret;
			read_amount -= ret;
			continue;
		}
			
		rc++;
		if (rc >= read_count) {
			if (opt_debug) {
				applog(LOG_DEBUG,
					"Icarus Read: No data in %.2f seconds",
					(float)rc/(float)TIME_FACTOR);
			}
			return 1;
		}

		if (thr_id >= 0 && work_restart[thr_id].restart) {
			if (opt_debug) {
				applog(LOG_DEBUG,
					"Icarus Read: Work restart at %.2f seconds",
					(float)(rc)/(float)TIME_FACTOR);
			}
			return 1;
		}
	}
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

static void set_timing_mode(struct cgpu_info *icarus)
{
	struct ICARUS_INFO *info = icarus_info[icarus->device_id];
	double Hs;
	char buf[BUFSIZ+1];
	char *ptr, *comma;
	size_t max;
	int i;

	if (opt_icarus_timing == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_icarus_timing;
		for (i = 0; i < icarus->device_id; i++) {
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

	if (strcasecmp(buf, MODE_SHORT_STR) == 0) {
		info->Hs = ICARUS_HASH_TIME;
		info->read_count = ICARUS_READ_COUNT_TIMING;

		info->timing_mode = MODE_SHORT;
		info->do_icarus_timing = true;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		info->Hs = ICARUS_HASH_TIME;
		info->read_count = ICARUS_READ_COUNT_TIMING;

		info->timing_mode = MODE_LONG;
		info->do_icarus_timing = true;
	} else if ((Hs = atof(buf)) != 0) {
		info->Hs = Hs / NANOSEC;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);
		info->read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;

		info->timing_mode = MODE_VALUE;
		info->do_icarus_timing = false;
	} else {
		// Anything else in buf just uses DEFAULT mode

		info->Hs = ICARUS_HASH_TIME;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);
		info->read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;

		info->timing_mode = MODE_DEFAULT;
		info->do_icarus_timing = false;
	}

	info->min_data_count = MIN_DATA_COUNT;

	applog(LOG_DEBUG, "Icarus: Init: %d mode=%s read_count=%d Hs=%e",
		icarus->device_id, timing_mode_str(info->timing_mode), info->read_count, info->Hs);

}

static bool icarus_detect_one(const char *devpath)
{
	struct ICARUS_INFO *info;
	struct timeval tv_start, tv_finish;
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
	const uint32_t golden_nonce_val = 0x000187a2;

	unsigned char ob_bin[64], nonce_bin[ICARUS_READ_SIZE];
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
	gettimeofday(&tv_start, NULL);

	memset(nonce_bin, 0, sizeof(nonce_bin));
	icarus_gets(nonce_bin, fd, &tv_finish, -1, 1);

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

	if (icarus_info[icarus->device_id] == NULL) {
		icarus_info[icarus->device_id] = (struct ICARUS_INFO *)malloc(sizeof(struct ICARUS_INFO));
		if (unlikely(!(icarus_info[icarus->device_id])))
			quit(1, "Failed to malloc ICARUS_INFO");
	}

	info = icarus_info[icarus->device_id];

	// Initialise everything to zero for a new device
	memset(info, 0, sizeof(struct ICARUS_INFO));

	info->golden_hashes = (golden_nonce_val & 0x7fffffff) << 1;
	timersub(&tv_finish, &tv_start, &(info->golden_tv));

	set_timing_mode(icarus);

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

	int fd = icarus_open(icarus->device_path);
	if (unlikely(-1 == fd)) {
		applog(LOG_ERR, "Failed to open Icarus on %s",
		       icarus->device_path);
		return false;
	}

	icarus->device_fd = fd;

	applog(LOG_INFO, "Opened Icarus on %s", icarus->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(icarus->init, &now);

	return true;
}

static uint64_t icarus_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused uint64_t max_nonce)
{
	const int thr_id = thr->id;
	struct cgpu_info *icarus;
	int fd;
	int ret;

	struct ICARUS_INFO *info;

	unsigned char ob_bin[64], nonce_bin[ICARUS_READ_SIZE];
	char *ob_hex;
	uint32_t nonce;
	uint64_t hash_count;
	struct timeval tv_start, tv_finish, elapsed;
	struct timeval tv_history_start, tv_history_finish;
	double Ti, Xi;
	int i;

	struct ICARUS_HISTORY *history0, *history;
	int count;
	double Hs, W, fullnonce;
	int read_count;
	uint64_t estimate_hashes;
	uint32_t values;
	uint64_t hash_count_range;

	elapsed.tv_sec = elapsed.tv_usec = 0;

	icarus = thr->cgpu;
	fd = icarus->device_fd;

	memset(ob_bin, 0, sizeof(ob_bin));
	memcpy(ob_bin, work->midstate, 32);
	memcpy(ob_bin + 52, work->data + 64, 12);
	rev(ob_bin, 32);
	rev(ob_bin + 52, 12);
#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif
	ret = icarus_write(fd, ob_bin, sizeof(ob_bin));
	if (ret)
		return 0;	/* This should never happen */

	info = icarus_info[icarus->device_id];

	if (opt_debug || info->do_icarus_timing)
		gettimeofday(&tv_start, NULL);

	if (opt_debug) {
		ob_hex = bin2hex(ob_bin, sizeof(ob_bin));
		if (ob_hex) {
			applog(LOG_DEBUG, "Icarus %d sent: %s",
				icarus->device_id, ob_hex);
			free(ob_hex);
		}
	}

	/* Icarus will return 4 bytes (ICARUS_READ_SIZE) nonces or nothing */
	memset(nonce_bin, 0, sizeof(nonce_bin));
	ret = icarus_gets(nonce_bin, fd, &tv_finish, thr_id, info->read_count);

	work->blk.nonce = 0xffffffff;
	memcpy((char *)&nonce, nonce_bin, sizeof(nonce_bin));

	// aborted before becoming idle, get new work
	if (nonce == 0 && ret) {
		timersub(&tv_finish, &tv_start, &elapsed);

		// ONLY up to just when it aborted
		// We didn't read a reply so we don't subtract ICARUS_READ_TIME
		estimate_hashes = ((double)(elapsed.tv_sec)
					+ ((double)(elapsed.tv_usec))/((double)1000000)) / info->Hs;

		// If some Serial-USB delay allowed the full nonce range to
		// complete it can't have done more than a full nonce
		if (unlikely(estimate_hashes > 0xffffffff))
			estimate_hashes = 0xffffffff;

		if (opt_debug) {
			applog(LOG_DEBUG, "Icarus %d no nonce = 0x%08llx hashes (%ld.%06lds)",
					icarus->device_id, estimate_hashes,
					elapsed.tv_sec, elapsed.tv_usec);
		}

		return estimate_hashes;
	}

#if !defined (__BIG_ENDIAN__) && !defined(MIPSEB)
	nonce = swab32(nonce);
#endif

	submit_nonce(thr, work, nonce);

	hash_count = (nonce & 0x7fffffff);
	if (hash_count++ == 0x7fffffff)
		hash_count = 0xffffffff;
	else
		hash_count <<= 1;

	if (opt_debug || info->do_icarus_timing)
		timersub(&tv_finish, &tv_start, &elapsed);

	if (opt_debug) {
		applog(LOG_DEBUG, "Icarus %d nonce = 0x%08x = 0x%08llx hashes (%ld.%06lds)",
				icarus->device_id, nonce, hash_count, elapsed.tv_sec, elapsed.tv_usec);
	}

	// ignore possible end condition values
	if (info->do_icarus_timing && (nonce & 0x7fffffff) > 0x000fffff && (nonce & 0x7fffffff) < 0x7ff00000) {
		gettimeofday(&tv_history_start, NULL);

		history0 = &(info->history[0]);

		if (history0->values == 0)
			timeradd(&tv_start, &history_sec, &(history0->finish));

		Ti = (double)(elapsed.tv_sec)
			+ ((double)(elapsed.tv_usec))/((double)1000000)
			- ICARUS_READ_TIME;
		Xi = (double)hash_count;
		history0->sumXiTi += Xi * Ti;
		history0->sumXi += Xi;
		history0->sumTi += Ti;
		history0->sumXi2 += Xi * Xi;

		history0->values++;

		if (history0->hash_count_max < hash_count)
			history0->hash_count_max = hash_count;
		if (history0->hash_count_min > hash_count || history0->hash_count_min == 0)
			history0->hash_count_min = hash_count;

		if (history0->values >= info->min_data_count
		&&  timercmp(&tv_start, &(history0->finish), >)) {
			for (i = INFO_HISTORY; i > 0; i--)
				memcpy(&(info->history[i]), &(info->history[i-1]), sizeof(struct ICARUS_HISTORY));

			// Initialise history0 to zero for summary calculation
			memset(history0, 0, sizeof(struct ICARUS_HISTORY));

			// We just completed a history data set
			// So now recalc read_count based on the whole history thus we will
			// initially get more accurate until it completes INFO_HISTORY
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
			memset(history0, 0, sizeof(struct ICARUS_HISTORY));

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
				info->do_icarus_timing = false;

//			applog(LOG_WARNING, "Icarus %d Re-estimate: read_count=%d fullnonce=%fs history count=%d Hs=%e W=%e values=%d hash range=0x%08lx min data count=%u", icarus->device_id, read_count, fullnonce, count, Hs, W, values, hash_count_range, info->min_data_count);
			applog(LOG_WARNING, "Icarus %d Re-estimate: Hs=%e W=%e read_count=%d fullnonce=%.3fs",
					icarus->device_id, Hs, W, read_count, fullnonce);
		}
		info->history_count++;
		gettimeofday(&tv_history_finish, NULL);

		timersub(&tv_history_finish, &tv_history_start, &tv_history_finish);
		timeradd(&tv_history_finish, &(info->history_time), &(info->history_time));
	}

	return hash_count;
}

static void icarus_api_stats(char *buf, struct cgpu_info *cgpu, bool isjson)
{
	struct ICARUS_INFO *info = icarus_info[cgpu->device_id];

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	sprintf(buf, isjson
		? "\"read_count\":%d,\"fullnonce\":%f,\"count\":%d,\"Hs\":%.15f,\"W\":%f,\"total_values\":%u,\"range\":%ld,\"history_count\":%lu,\"history_time\":%f,\"min_data_count\":%u,\"timing_values\":%u"
		: "read_count=%d,fullnonce=%f,count=%d,Hs=%.15f,W=%f,total_values=%u,range=%ld,history_count=%lu,history_time=%f,min_data_count=%u,timing_values=%u",
		info->read_count, info->fullnonce,
		info->count, info->Hs, info->W,
		info->values, info->hash_count_range,
		info->history_count,
		(double)(info->history_time.tv_sec)
			+ ((double)(info->history_time.tv_usec))/((double)1000000),
		info->min_data_count, info->history[0].values);
}

static void icarus_shutdown(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	icarus_close(icarus->device_fd);
}

struct device_api icarus_api = {
	.dname = "icarus",
	.name = "ICA",
	.api_detect = icarus_detect,
	.get_api_stats = icarus_api_stats,
	.thread_prepare = icarus_prepare,
	.scanhash = icarus_scanhash,
	.thread_shutdown = icarus_shutdown,
};
