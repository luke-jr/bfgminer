/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 Xiangfu
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

#include "config.h"
#include "miner.h"

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
#ifdef HAVE_SYS_EPOLL_H
  #include <sys/epoll.h>
  #define HAVE_EPOLL
#endif

#include "compat.h"
#include "dynclock.h"
#include "driver-icarus.h"
#include "lowl-vcom.h"

// The serial I/O speed - Linux uses a define 'B115200' in bits/termios.h
#define ICARUS_IO_SPEED 115200

// The number of bytes in a nonce (always 4)
// This is NOT the read-size for the Icarus driver
// That is defined in ICARUS_INFO->read_size
#define ICARUS_NONCE_SIZE 4

#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

#define ICARUS_READ_TIME(baud, read_size) ((double)read_size * (double)8.0 / (double)(baud))

// Defined in deciseconds
// There's no need to have this bigger, since the overhead/latency of extra work
// is pretty small once you get beyond a 10s nonce range time and 10s also
// means that nothing slower than 429MH/s can go idle so most icarus devices
// will always mine without idling
#define ICARUS_READ_COUNT_LIMIT_MAX 100

// In timing mode: Default starting value until an estimate can be obtained
// 5 seconds allows for up to a ~840MH/s device
#define ICARUS_READ_COUNT_TIMING	(5 * TIME_FACTOR)

// For a standard Icarus REV3
#define ICARUS_REV3_HASH_TIME 0.00000000264083

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

#if (TIME_FACTOR != 10)
#error TIME_FACTOR must be 10
#endif

static struct timeval history_sec = { HISTORY_SEC, 0 };

static const char *MODE_DEFAULT_STR = "default";
static const char *MODE_SHORT_STR = "short";
static const char *MODE_SHORT_STREQ = "short=";
static const char *MODE_LONG_STR = "long";
static const char *MODE_LONG_STREQ = "long=";
static const char *MODE_VALUE_STR = "value";
static const char *MODE_UNKNOWN_STR = "unknown";

#define END_CONDITION 0x0000ffff
#define DEFAULT_DETECT_THRESHOLD 1

BFG_REGISTER_DRIVER(icarus_drv)
extern const struct bfg_set_device_definition icarus_set_device_funcs[];

extern void convert_icarus_to_cairnsmore(struct cgpu_info *);

static inline
uint32_t icarus_nonce32toh(const struct ICARUS_INFO * const info, const uint32_t nonce)
{
	return info->nonce_littleendian ? le32toh(nonce) : be32toh(nonce);
}

#define icarus_open2(devpath, baud, purge)  serial_open(devpath, baud, ICARUS_READ_FAULT_DECISECONDS, purge)
#define icarus_open(devpath, baud)  icarus_open2(devpath, baud, false)

static
void icarus_log_protocol(int fd, const void *buf, size_t bufLen, const char *prefix)
{
	char hex[(bufLen * 2) + 1];
	bin2hex(hex, buf, bufLen);
	applog(LOG_DEBUG, "%s fd=%d: DEVPROTO: %s %s", icarus_drv.dname, fd, prefix, hex);
}

int icarus_gets(unsigned char *buf, int fd, struct timeval *tv_finish, struct thr_info *thr, int read_count, int read_size)
{
	ssize_t ret = 0;
	int rc = 0;
	int epollfd = -1;
	int epoll_timeout = ICARUS_READ_FAULT_DECISECONDS * 100;
	int read_amount = read_size;
	bool first = true;

#ifdef HAVE_EPOLL
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = fd,
	};
	struct epoll_event evr[2];
	if (thr && thr->work_restart_notifier[1] != -1) {
	epollfd = epoll_create(2);
	if (epollfd != -1) {
		if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
			close(epollfd);
			epollfd = -1;
		}
		{
			ev.data.fd = thr->work_restart_notifier[0];
			if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, thr->work_restart_notifier[0], &ev))
				applog(LOG_ERR, "%s: Error adding work restart fd to epoll", __func__);
			else
			{
				epoll_timeout *= read_count;
				read_count = 1;
			}
		}
	}
	else
		applog(LOG_ERR, "%s: Error creating epoll", __func__);
	}
#endif

	// Read reply 1 byte at a time to get earliest tv_finish
	while (true) {
#ifdef HAVE_EPOLL
		if (epollfd != -1 && (ret = epoll_wait(epollfd, evr, 2, epoll_timeout)) != -1)
		{
			if (ret == 1 && evr[0].data.fd == fd)
				ret = read(fd, buf, 1);
			else
			{
				if (ret)
					notifier_read(thr->work_restart_notifier);
				ret = 0;
			}
		}
		else
#endif
		ret = read(fd, buf, 1);
		if (ret < 0)
			return ICA_GETS_ERROR;

		if (first)
			cgtime(tv_finish);

		if (ret >= read_amount)
		{
			if (epollfd != -1)
				close(epollfd);

			if (opt_dev_protocol && opt_debug)
				icarus_log_protocol(fd, buf, read_size, "RECV");

			return ICA_GETS_OK;
		}

		if (ret > 0) {
			buf += ret;
			read_amount -= ret;
			first = false;
			continue;
		}
			
		if (thr && thr->work_restart) {
			if (epollfd != -1)
				close(epollfd);
			applog(LOG_DEBUG, "%s: Interrupted by work restart", __func__);
			return ICA_GETS_RESTART;
		}

		rc++;
		if (rc >= read_count) {
			if (epollfd != -1)
				close(epollfd);
			applog(LOG_DEBUG, "%s: No data in %.2f seconds",
			       __func__,
			       (float)rc * epoll_timeout / 1000.);
			return ICA_GETS_TIMEOUT;
		}
	}
}

int icarus_write(int fd, const void *buf, size_t bufLen)
{
	size_t ret;

	if (opt_dev_protocol && opt_debug)
		icarus_log_protocol(fd, buf, bufLen, "SEND");

	if (unlikely(fd == -1))
		return 1;
	
	ret = write(fd, buf, bufLen);
	if (unlikely(ret != bufLen))
		return 1;

	return 0;
}

#define icarus_close(fd) serial_close(fd)

void do_icarus_close(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	const int fd = icarus->device_fd;
	if (fd == -1)
		return;
	icarus_close(fd);
	icarus->device_fd = -1;
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

static
const char *icarus_set_timing(struct cgpu_info * const proc, const char * const optname, const char * const buf, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	double Hs;
	char *eq;

	if (strcasecmp(buf, MODE_SHORT_STR) == 0) {
		// short
		info->read_count = ICARUS_READ_COUNT_TIMING;
		info->read_count_limit = 0;  // 0 = no limit

		info->timing_mode = MODE_SHORT;
		info->do_icarus_timing = true;
	} else if (strncasecmp(buf, MODE_SHORT_STREQ, strlen(MODE_SHORT_STREQ)) == 0) {
		// short=limit
		info->read_count = ICARUS_READ_COUNT_TIMING;

		info->timing_mode = MODE_SHORT;
		info->do_icarus_timing = true;

		info->read_count_limit = atoi(&buf[strlen(MODE_SHORT_STREQ)]);
		if (info->read_count_limit < 0)
			info->read_count_limit = 0;
		if (info->read_count_limit > ICARUS_READ_COUNT_LIMIT_MAX)
			info->read_count_limit = ICARUS_READ_COUNT_LIMIT_MAX;
	} else if (strcasecmp(buf, MODE_LONG_STR) == 0) {
		// long
		info->read_count = ICARUS_READ_COUNT_TIMING;
		info->read_count_limit = 0;  // 0 = no limit

		info->timing_mode = MODE_LONG;
		info->do_icarus_timing = true;
	} else if (strncasecmp(buf, MODE_LONG_STREQ, strlen(MODE_LONG_STREQ)) == 0) {
		// long=limit
		info->read_count = ICARUS_READ_COUNT_TIMING;

		info->timing_mode = MODE_LONG;
		info->do_icarus_timing = true;

		info->read_count_limit = atoi(&buf[strlen(MODE_LONG_STREQ)]);
		if (info->read_count_limit < 0)
			info->read_count_limit = 0;
		if (info->read_count_limit > ICARUS_READ_COUNT_LIMIT_MAX)
			info->read_count_limit = ICARUS_READ_COUNT_LIMIT_MAX;
	} else if ((Hs = atof(buf)) != 0) {
		// ns[=read_count]
		info->Hs = Hs / NANOSEC;
		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		info->read_count = 0;
		if ((eq = strchr(buf, '=')) != NULL)
			info->read_count = atoi(eq+1);

		if (info->read_count < 1)
			info->read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;

		if (unlikely(info->read_count < 1))
			info->read_count = 1;

		info->read_count_limit = 0;  // 0 = no limit
		
		info->timing_mode = MODE_VALUE;
		info->do_icarus_timing = false;
	} else {
		// Anything else in buf just uses DEFAULT mode

		info->fullnonce = info->Hs * (((double)0xffffffff) + 1);

		info->read_count = 0;
		if ((eq = strchr(buf, '=')) != NULL)
			info->read_count = atoi(eq+1);

		int def_read_count = ICARUS_READ_COUNT_TIMING;

		if (info->timing_mode == MODE_DEFAULT) {
			if (proc->drv == &icarus_drv) {
				info->do_default_detection = 0x10;
			} else {
				def_read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;
			}

			info->do_icarus_timing = false;
		}
		if (info->read_count < 1)
			info->read_count = def_read_count;
		
		info->read_count_limit = 0;  // 0 = no limit
	}

	info->min_data_count = MIN_DATA_COUNT;

	applog(LOG_DEBUG, "%"PRIpreprv": Init: mode=%s read_count=%d limit=%dms Hs=%e",
		proc->proc_repr,
		timing_mode_str(info->timing_mode),
		info->read_count, info->read_count_limit, info->Hs);
	
	return NULL;
}

static uint32_t mask(int work_division)
{
	return 0xffffffff / work_division;
}

// Number of bytes remaining after reading a nonce from Icarus
int icarus_excess_nonce_size(int fd, struct ICARUS_INFO *info)
{
	// How big a buffer?
	int excess_size = info->read_size - ICARUS_NONCE_SIZE;

	// Try to read one more to ensure the device doesn't return
	// more than we want for this driver
	excess_size++;

	unsigned char excess_bin[excess_size];
	// Read excess_size from Icarus
	struct timeval tv_now;
	timer_set_now(&tv_now);
	//icarus_gets(excess_bin, fd, &tv_now, NULL, 1, excess_size);
	int bytes_read = read(fd, excess_bin, excess_size);
	// Number of bytes that were still available

	return bytes_read;
}

bool icarus_detect_custom(const char *devpath, struct device_drv *api, struct ICARUS_INFO *info)
{
	struct timeval tv_start, tv_finish;
	int fd;

	unsigned char nonce_bin[ICARUS_NONCE_SIZE];
	char nonce_hex[(sizeof(nonce_bin) * 2) + 1];

	drv_set_defaults(api, icarus_set_device_funcs, info, devpath, detectone_meta_info.serial, 1);

	int baud = info->baud;
	int work_division = info->work_division;
	int fpga_count = info->fpga_count;

	applog(LOG_DEBUG, "%s: Attempting to open %s", api->dname, devpath);

	fd = icarus_open2(devpath, baud, true);
	if (unlikely(fd == -1)) {
		applog(LOG_DEBUG, "%s: Failed to open %s", api->dname, devpath);
		return false;
	}
	
	// Set a default so that individual drivers need not specify
	// e.g. Cairnsmore
	BFGINIT(info->probe_read_count, 1);
	if (info->read_size == 0)
		info->read_size = ICARUS_DEFAULT_READ_SIZE;
	
	if (!info->golden_ob)
	{
		// Block 171874 nonce = (0xa2870100) = 0x000187a2
		// NOTE: this MUST take less time to calculate
		//	than the timeout set in icarus_open()
		//	This one takes ~0.53ms on Rev3 Icarus
		info->golden_ob =
			"4679ba4ec99876bf4bfe086082b40025"
			"4df6c356451471139a3afa71e48f544a"
			"00000000000000000000000000000000"
			"0000000087320b1a1426674f2fa722ce";
		/* NOTE: This gets sent to basically every port specified in --scan-serial,
		 *       even ones that aren't Icarus; be sure they can all handle it, when
		 *       this is changed...
		 *       BitForce: Ignores entirely
		 *       ModMiner: Starts (useless) work, gets back to clean state
		 */
		
		info->golden_nonce = "000187a2";
	}

	if (info->detect_init_func)
		info->detect_init_func(devpath, fd, info);
	
	int ob_size = strlen(info->golden_ob) / 2;
	unsigned char ob_bin[ob_size];
	BFGINIT(info->ob_size, ob_size);

	if (!info->ignore_golden_nonce)
	{
		hex2bin(ob_bin, info->golden_ob, sizeof(ob_bin));
		icarus_write(fd, ob_bin, sizeof(ob_bin));
		cgtime(&tv_start);
		
		memset(nonce_bin, 0, sizeof(nonce_bin));
		// Do not use info->read_size here, instead read exactly ICARUS_NONCE_SIZE
		// We will then compare the bytes left in fd with info->read_size to determine
		// if this is a valid device
		icarus_gets(nonce_bin, fd, &tv_finish, NULL, info->probe_read_count, ICARUS_NONCE_SIZE);
		
		// How many bytes were left after reading the above nonce
		int bytes_left = icarus_excess_nonce_size(fd, info);
		
		icarus_close(fd);
		
		bin2hex(nonce_hex, nonce_bin, sizeof(nonce_bin));
		if (strncmp(nonce_hex, info->golden_nonce, 8))
		{
			applog(LOG_DEBUG,
				   "%s: "
				   "Test failed at %s: get %s, should: %s",
				   api->dname,
				   devpath, nonce_hex, info->golden_nonce);
			return false;
		}
		
		if (info->read_size - ICARUS_NONCE_SIZE != bytes_left)
		{
			applog(LOG_DEBUG,
				   "%s: "
				   "Test failed at %s: expected %d bytes, got %d",
				   api->dname,
				   devpath, info->read_size, ICARUS_NONCE_SIZE + bytes_left);
			return false;
		}
	}
	else
		icarus_close(fd);
	
	applog(LOG_DEBUG,
		"%s: "
		"Test succeeded at %s: got %s",
	       api->dname,
			devpath, nonce_hex);

	if (serial_claim_v(devpath, api))
		return false;

	/* We have a real Icarus! */
	struct cgpu_info *icarus;
	icarus = calloc(1, sizeof(struct cgpu_info));
	icarus->drv = api;
	icarus->device_path = strdup(devpath);
	icarus->device_fd = -1;
	icarus->threads = 1;
	icarus->set_device_funcs = icarus_set_device_funcs;
	add_cgpu(icarus);

	applog(LOG_INFO, "Found %"PRIpreprv" at %s",
		icarus->proc_repr,
		devpath);

	applog(LOG_DEBUG, "%"PRIpreprv": Init: baud=%d work_division=%d fpga_count=%d",
		icarus->proc_repr,
		baud, work_division, fpga_count);

	icarus->device_data = info;

	timersub(&tv_finish, &tv_start, &(info->golden_tv));
	icarus_set_timing(icarus, NULL, "", NULL, NULL);

	return true;
}

static bool icarus_detect_one(const char *devpath)
{
	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");

	// TODO: try some higher speeds with the Icarus and BFL to see
	// if they support them and if setting them makes any difference
	// N.B. B3000000 doesn't work on Icarus
	info->baud = ICARUS_IO_SPEED;
	info->reopen_mode = IRM_TIMEOUT;
	info->Hs = ICARUS_REV3_HASH_TIME;
	info->timing_mode = MODE_DEFAULT;
	info->read_size = ICARUS_DEFAULT_READ_SIZE;

	if (!icarus_detect_custom(devpath, &icarus_drv, info)) {
		free(info);
		return false;
	}
	return true;
}

static
bool icarus_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, icarus_detect_one);
}

static bool icarus_prepare(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;

	struct icarus_state *state;
	thr->cgpu_data = state = calloc(1, sizeof(*state));
	state->firstrun = true;

#ifdef HAVE_EPOLL
	int epollfd = epoll_create(2);
	if (epollfd != -1)
	{
		close(epollfd);
		notifier_init(thr->work_restart_notifier);
	}
#endif

	icarus->status = LIFE_INIT2;
	
	return true;
}

bool icarus_init(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO *info = icarus->device_data;
	struct icarus_state * const state = thr->cgpu_data;
	
	int fd = icarus_open2(icarus->device_path, info->baud, true);
	icarus->device_fd = fd;
	if (unlikely(-1 == fd)) {
		applog(LOG_ERR, "%s: Failed to open %s",
		       icarus->dev_repr,
		       icarus->device_path);
		return false;
	}
	applog(LOG_INFO, "%s: Opened %s", icarus->dev_repr, icarus->device_path);
	
	BFGINIT(info->job_start_func, icarus_job_start);
	BFGINIT(state->ob_bin, calloc(1, info->ob_size));
	
	if (!info->work_division)
	{
		struct timeval tv_finish;
		
		// For reading the nonce from Icarus
		unsigned char res_bin[info->read_size];
		// For storing the the 32-bit nonce
		uint32_t res;
		
		applog(LOG_DEBUG, "%"PRIpreprv": Work division not specified - autodetecting", icarus->proc_repr);
		
		// Special packet to probe work_division
		unsigned char pkt[64] =
			"\x2e\x4c\x8f\x91\xfd\x59\x5d\x2d\x7e\xa2\x0a\xaa\xcb\x64\xa2\xa0"
			"\x43\x82\x86\x02\x77\xcf\x26\xb6\xa1\xee\x04\xc5\x6a\x5b\x50\x4a"
			"BFGMiner Probe\0\0"
			"BFG\0\x64\x61\x01\x1a\xc9\x06\xa9\x51\xfb\x9b\x3c\x73";
		
		icarus_write(fd, pkt, sizeof(pkt));
		memset(res_bin, 0, sizeof(res_bin));
		if (ICA_GETS_OK == icarus_gets(res_bin, fd, &tv_finish, NULL, info->read_count, info->read_size))
		{
			memcpy(&res, res_bin, sizeof(res));
			res = icarus_nonce32toh(info, res);
		}
		else
			res = 0;
		
		switch (res) {
			case 0x04C0FDB4:
				info->work_division = 1;
				break;
			case 0x82540E46:
				info->work_division = 2;
				break;
			case 0x417C0F36:
				info->work_division = 4;
				break;
			case 0x60C994D5:
				info->work_division = 8;
				break;
			default:
				applog(LOG_ERR, "%"PRIpreprv": Work division autodetection failed (assuming 2): got %08x", icarus->proc_repr, res);
				info->work_division = 2;
		}
		applog(LOG_DEBUG, "%"PRIpreprv": Work division autodetection got %08x (=%d)", icarus->proc_repr, res, info->work_division);
	}
	
	if (!info->fpga_count)
		info->fpga_count = info->work_division;
	
	if (!is_power_of_two(info->work_division))
		info->work_division = upper_power_of_two_u32(info->work_division);
	info->nonce_mask = mask(info->work_division);
	
	return true;
}

static bool icarus_reopen(struct cgpu_info *icarus, struct icarus_state *state, int *fdp)
{
	struct ICARUS_INFO *info = icarus->device_data;

	// Reopen the serial port to workaround a USB-host-chipset-specific issue with the Icarus's buggy USB-UART
	do_icarus_close(icarus->thr[0]);
	*fdp = icarus->device_fd = icarus_open(icarus->device_path, info->baud);
	if (unlikely(-1 == *fdp)) {
		applog(LOG_ERR, "%"PRIpreprv": Failed to reopen on %s", icarus->proc_repr, icarus->device_path);
		dev_error(icarus, REASON_DEV_COMMS_ERROR);
		state->firstrun = true;
		return false;
	}
	return true;
}

static
bool icarus_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info * const icarus = thr->cgpu;
	struct icarus_state * const state = thr->cgpu_data;
	uint8_t * const ob_bin = state->ob_bin;
	
	swab256(ob_bin, work->midstate);
	bswap_96p(&ob_bin[0x34], &work->data[0x40]);
	if (!(memcmp(&ob_bin[56], "\xff\xff\xff\xff", 4)
	   || memcmp(&ob_bin, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32))) {
		// This sequence is used on cairnsmore bitstreams for commands, NEVER send it otherwise
		applog(LOG_WARNING, "%"PRIpreprv": Received job attempting to send a command, corrupting it!",
		       icarus->proc_repr);
		ob_bin[56] = 0;
	}
	
	return true;
}

bool icarus_job_start(struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO *info = icarus->device_data;
	struct icarus_state *state = thr->cgpu_data;
	const uint8_t * const ob_bin = state->ob_bin;
	int fd = icarus->device_fd;
	int ret;

	// Handle dynamic clocking for "subclass" devices
	// This needs to run before sending next job, since it hashes the command too
	if (info->dclk.freqM && likely(!state->firstrun)) {
		dclk_preUpdate(&info->dclk);
		dclk_updateFreq(&info->dclk, info->dclk_change_clock_func, thr);
	}
	
	cgtime(&state->tv_workstart);

	ret = icarus_write(fd, ob_bin, info->ob_size);
	if (ret) {
		do_icarus_close(thr);
		applog(LOG_ERR, "%"PRIpreprv": Comms error (werr=%d)", icarus->proc_repr, ret);
		dev_error(icarus, REASON_DEV_COMMS_ERROR);
		return false;	/* This should never happen */
	}

	if (opt_debug) {
		char ob_hex[(info->ob_size * 2) + 1];
		bin2hex(ob_hex, ob_bin, info->ob_size);
		applog(LOG_DEBUG, "%"PRIpreprv" sent: %s",
			icarus->proc_repr,
			ob_hex);
	}

	return true;
}

static
struct work *icarus_process_worknonce(const struct ICARUS_INFO * const info, struct icarus_state *state, uint32_t *nonce)
{
	*nonce = icarus_nonce32toh(info, *nonce);
	if (test_nonce(state->last_work, *nonce, false))
		return state->last_work;
	if (likely(state->last2_work && test_nonce(state->last2_work, *nonce, false)))
		return state->last2_work;
	return NULL;
}

static
void handle_identify(struct thr_info * const thr, int ret, const bool was_first_run)
{
	const struct cgpu_info * const icarus = thr->cgpu;
	const struct ICARUS_INFO * const info = icarus->device_data;
	struct icarus_state * const state = thr->cgpu_data;
	int fd = icarus->device_fd;
	struct timeval tv_now;
	double delapsed;
	
	// For reading the nonce from Icarus
	unsigned char nonce_bin[info->read_size];
	// For storing the the 32-bit nonce
	uint32_t nonce;
	
	if (fd == -1)
		return;
	
	// If identify is requested (block erupters):
	// 1. Don't start the next job right away (above)
	// 2. Wait for the current job to complete 100%
	
	if (!was_first_run)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Waiting for current job to finish", icarus->proc_repr);
		while (true)
		{
			cgtime(&tv_now);
			delapsed = tdiff(&tv_now, &state->tv_workstart);
			if (delapsed + 0.1 > info->fullnonce)
				break;
			
			// Try to get more nonces (ignoring work restart)
			memset(nonce_bin, 0, sizeof(nonce_bin));
			ret = icarus_gets(nonce_bin, fd, &tv_now, NULL, (info->fullnonce - delapsed) * 10, info->read_size);
			if (ret == ICA_GETS_OK)
			{
				memcpy(&nonce, nonce_bin, sizeof(nonce));
				nonce = icarus_nonce32toh(info, nonce);
				submit_nonce(thr, state->last_work, nonce);
			}
		}
	}
	else
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Current job should already be finished", icarus->proc_repr);
	
	// 3. Delay 3 more seconds
	applog(LOG_DEBUG, "%"PRIpreprv": Identify: Leaving idle for 3 seconds", icarus->proc_repr);
	cgsleep_ms(3000);
	
	// Check for work restart in the meantime
	if (thr->work_restart)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Work restart requested during delay", icarus->proc_repr);
		goto no_job_start;
	}
	
	// 4. Start next job
	if (!state->firstrun)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Identify: Starting next job", icarus->proc_repr);
		if (!info->job_start_func(thr))
no_job_start:
			state->firstrun = true;
	}
	
	state->identify = false;
}

static
void icarus_transition_work(struct icarus_state *state, struct work *work)
{
	if (state->last2_work)
		free_work(state->last2_work);
	state->last2_work = state->last_work;
	state->last_work = copy_work(work);
}

static int64_t icarus_scanhash(struct thr_info *thr, struct work *work,
				__maybe_unused int64_t max_nonce)
{
	struct cgpu_info *icarus;
	int fd;
	int ret;

	struct ICARUS_INFO *info;

	struct work *nonce_work;
	int64_t hash_count;
	struct timeval tv_start = {.tv_sec=0}, elapsed;
	struct timeval tv_history_start, tv_history_finish;
	double Ti, Xi;
	int i;
	bool was_hw_error = false;
	bool was_first_run;

	struct ICARUS_HISTORY *history0, *history;
	int count;
	double Hs, W, fullnonce;
	int read_count;
	bool limited;
	uint32_t values;
	int64_t hash_count_range;

	elapsed.tv_sec = elapsed.tv_usec = 0;

	icarus = thr->cgpu;
	struct icarus_state *state = thr->cgpu_data;
	was_first_run = state->firstrun;

	icarus->drv->job_prepare(thr, work, max_nonce);

	// Wait for the previous run's result
	fd = icarus->device_fd;
	info = icarus->device_data;
	
	// For reading the nonce from Icarus
	unsigned char nonce_bin[info->read_size];
	// For storing the the 32-bit nonce
	uint32_t nonce;

	if (unlikely(fd == -1) && !icarus_reopen(icarus, state, &fd))
		return -1;
	
	if (!state->firstrun) {
		if (state->changework)
		{
			state->changework = false;
			ret = ICA_GETS_RESTART;
		}
		else
		{
			read_count = info->read_count;
keepwaiting:
			/* Icarus will return info->read_size bytes nonces or nothing */
			memset(nonce_bin, 0, sizeof(nonce_bin));
			ret = icarus_gets(nonce_bin, fd, &state->tv_workfinish, thr, read_count, info->read_size);
			switch (ret) {
				case ICA_GETS_RESTART:
					// The prepared work is invalid, and the current work is abandoned
					// Go back to the main loop to get the next work, and stuff
					// Returning to the main loop will clear work_restart, so use a flag...
					state->changework = true;
					return 0;
				case ICA_GETS_ERROR:
					do_icarus_close(thr);
					applog(LOG_ERR, "%"PRIpreprv": Comms error (rerr)", icarus->proc_repr);
					dev_error(icarus, REASON_DEV_COMMS_ERROR);
					if (!icarus_reopen(icarus, state, &fd))
						return -1;
					break;
				case ICA_GETS_TIMEOUT:
					if (info->reopen_mode == IRM_TIMEOUT && !icarus_reopen(icarus, state, &fd))
						return -1;
				case ICA_GETS_OK:
					break;
			}
		}

		tv_start = state->tv_workstart;
		timersub(&state->tv_workfinish, &tv_start, &elapsed);
	}
	else
	{
		if (fd == -1 && !icarus_reopen(icarus, state, &fd))
			return -1;
		
		// First run; no nonce, no hashes done
		ret = ICA_GETS_ERROR;
	}

#ifndef WIN32
	tcflush(fd, TCOFLUSH);
#endif

	if (ret == ICA_GETS_OK)
	{
		memcpy(&nonce, nonce_bin, sizeof(nonce));
		nonce_work = icarus_process_worknonce(info, state, &nonce);
		if (likely(nonce_work))
		{
			if (nonce_work == state->last2_work)
			{
				// nonce was for the last job; submit and keep processing the current one
				submit_nonce(thr, nonce_work, nonce);
				goto keepwaiting;
			}
			if (info->continue_search)
			{
				read_count = info->read_count - ((timer_elapsed_us(&state->tv_workstart, NULL) / (1000000 / TIME_FACTOR)) + 1);
				if (read_count)
				{
					submit_nonce(thr, nonce_work, nonce);
					goto keepwaiting;
				}
			}
		}
		else
			was_hw_error = true;
	}
	
	// Handle dynamic clocking for "subclass" devices
	// This needs to run before sending next job, since it hashes the command too
	if (info->dclk.freqM && likely(ret == ICA_GETS_OK || ret == ICA_GETS_TIMEOUT)) {
		int qsec = ((4 * elapsed.tv_sec) + (elapsed.tv_usec / 250000)) ?: 1;
		for (int n = qsec; n; --n)
			dclk_gotNonces(&info->dclk);
		if (was_hw_error)
			dclk_errorCount(&info->dclk, qsec);
	}
	
	// Force a USB close/reopen on any hw error (or on request, eg for baud change)
	if (was_hw_error || info->reopen_now)
	{
		info->reopen_now = false;
		if (info->reopen_mode == IRM_CYCLE)
		{}  // Do nothing here, we reopen after sending the job
		else
		if (!icarus_reopen(icarus, state, &fd))
			state->firstrun = true;
	}

	if (unlikely(state->identify))
	{
		// Delay job start until later...
	}
	else
	if (unlikely(icarus->deven != DEV_ENABLED || !info->job_start_func(thr)))
		state->firstrun = true;

	if (info->reopen_mode == IRM_CYCLE && !icarus_reopen(icarus, state, &fd))
		state->firstrun = true;

	work->blk.nonce = 0xffffffff;

	if (ret == ICA_GETS_ERROR) {
		state->firstrun = false;
		icarus_transition_work(state, work);
		hash_count = 0;
		goto out;
	}

	// OK, done starting Icarus's next job... now process the last run's result!

	if (ret == ICA_GETS_OK && !was_hw_error)
	{
		submit_nonce(thr, nonce_work, nonce);
		
		icarus_transition_work(state, work);
		
		hash_count = (nonce & info->nonce_mask);
		hash_count++;
		hash_count *= info->fpga_count;

		applog(LOG_DEBUG, "%"PRIpreprv" nonce = 0x%08x = 0x%08" PRIx64 " hashes (%"PRId64".%06lus)",
		       icarus->proc_repr,
		       nonce,
		       (uint64_t)hash_count,
		       (int64_t)elapsed.tv_sec, (unsigned long)elapsed.tv_usec);
	}
	else
	{
		double estimate_hashes = elapsed.tv_sec;
		estimate_hashes += ((double)elapsed.tv_usec) / 1000000.;
		
		if (ret == ICA_GETS_OK)
		{
			inc_hw_errors(thr, state->last_work, nonce);
			estimate_hashes -= ICARUS_READ_TIME(info->baud, info->read_size);
		}
		
		icarus_transition_work(state, work);
		
		estimate_hashes /= info->Hs;

		// If some Serial-USB delay allowed the full nonce range to
		// complete it can't have done more than a full nonce
		if (unlikely(estimate_hashes > 0xffffffff))
			estimate_hashes = 0xffffffff;

		applog(LOG_DEBUG, "%"PRIpreprv" %s nonce = 0x%08"PRIx64" hashes (%"PRId64".%06lus)",
		       icarus->proc_repr,
		       (ret == ICA_GETS_OK) ? "bad" : "no",
		       (uint64_t)estimate_hashes,
		       (int64_t)elapsed.tv_sec, (unsigned long)elapsed.tv_usec);

		hash_count = estimate_hashes;
		
		if (ret != ICA_GETS_OK)
			goto out;
	}

	// Only ICA_GETS_OK gets here
	
	if (info->do_default_detection && elapsed.tv_sec >= DEFAULT_DETECT_THRESHOLD) {
		int MHs = (double)hash_count / ((double)elapsed.tv_sec * 1e6 + (double)elapsed.tv_usec);
		--info->do_default_detection;
		applog(LOG_DEBUG, "%"PRIpreprv": Autodetect device speed: %d MH/s", icarus->proc_repr, MHs);
		if (MHs <= 370 || MHs > 420) {
			// Not a real Icarus: enable short timing
			applog(LOG_WARNING, "%"PRIpreprv": Seems too %s to be an Icarus; calibrating with short timing", icarus->proc_repr, MHs>380?"fast":"slow");
			info->timing_mode = MODE_SHORT;
			info->do_icarus_timing = true;
			info->do_default_detection = 0;
		}
		else
		if (MHs <= 380) {
			// Real Icarus?
			if (!info->do_default_detection) {
				applog(LOG_DEBUG, "%"PRIpreprv": Seems to be a real Icarus", icarus->proc_repr);
				info->read_count = (int)(info->fullnonce * TIME_FACTOR) - 1;
			}
		}
		else
		if (MHs <= 420) {
			// Enterpoint Cairnsmore1
			size_t old_repr_len = strlen(icarus->proc_repr);
			char old_repr[old_repr_len + 1];
			strcpy(old_repr, icarus->proc_repr);
			convert_icarus_to_cairnsmore(icarus);
			info->do_default_detection = 0;
			applog(LOG_WARNING, "%"PRIpreprv": Detected Cairnsmore1 device, upgrading driver to %"PRIpreprv, old_repr, icarus->proc_repr);
		}
	}

	// Ignore possible end condition values ... and hw errors
	// TODO: set limitations on calculated values depending on the device
	// to avoid crap values caused by CPU/Task Switching/Swapping/etc
	if (info->do_icarus_timing
	&&  !was_hw_error
	&&  ((nonce & info->nonce_mask) > END_CONDITION)
	&&  ((nonce & info->nonce_mask) < (info->nonce_mask & ~END_CONDITION))) {
		cgtime(&tv_history_start);

		history0 = &(info->history[0]);

		if (history0->values == 0)
			timeradd(&tv_start, &history_sec, &(history0->finish));

		Ti = (double)(elapsed.tv_sec)
			+ ((double)(elapsed.tv_usec))/((double)1000000)
			- ((double)ICARUS_READ_TIME(info->baud, info->read_size));
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
				memcpy(&(info->history[i]),
					&(info->history[i-1]),
					sizeof(struct ICARUS_HISTORY));

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
			if (info->read_count_limit > 0 && read_count > info->read_count_limit) {
				read_count = info->read_count_limit;
				limited = true;
			} else
				limited = false;

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

//			applog(LOG_DEBUG, "%"PRIpreprv" Re-estimate: read_count=%d%s fullnonce=%fs history count=%d Hs=%e W=%e values=%d hash range=0x%08lx min data count=%u", icarus->proc_repr, read_count, limited ? " (limited)" : "", fullnonce, count, Hs, W, values, hash_count_range, info->min_data_count);
			applog(LOG_DEBUG, "%"PRIpreprv" Re-estimate: Hs=%e W=%e read_count=%d%s fullnonce=%.3fs",
					icarus->proc_repr,
					Hs, W, read_count,
					limited ? " (limited)" : "", fullnonce);
		}
		info->history_count++;
		cgtime(&tv_history_finish);

		timersub(&tv_history_finish, &tv_history_start, &tv_history_finish);
		timeradd(&tv_history_finish, &(info->history_time), &(info->history_time));
	}

out:
	if (unlikely(state->identify))
		handle_identify(thr, ret, was_first_run);
	
	return hash_count;
}

static struct api_data *icarus_drv_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct ICARUS_INFO *info = cgpu->device_data;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_int(root, "read_count", &(info->read_count), false);
	root = api_add_int(root, "read_count_limit", &(info->read_count_limit), false);
	root = api_add_double(root, "fullnonce", &(info->fullnonce), false);
	root = api_add_int(root, "count", &(info->count), false);
	root = api_add_hs(root, "Hs", &(info->Hs), false);
	root = api_add_double(root, "W", &(info->W), false);
	root = api_add_uint(root, "total_values", &(info->values), false);
	root = api_add_uint64(root, "range", &(info->hash_count_range), false);
	root = api_add_uint64(root, "history_count", &(info->history_count), false);
	root = api_add_timeval(root, "history_time", &(info->history_time), false);
	root = api_add_uint(root, "min_data_count", &(info->min_data_count), false);
	root = api_add_uint(root, "timing_values", &(info->history[0].values), false);
	root = api_add_const(root, "timing_mode", timing_mode_str(info->timing_mode), false);
	root = api_add_bool(root, "is_timing", &(info->do_icarus_timing), false);
	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "work_division", &(info->work_division), false);
	root = api_add_int(root, "fpga_count", &(info->fpga_count), false);

	return root;
}

static
const char *icarus_set_baud(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	const int baud = atoi(newvalue);
	if (!valid_baud(baud))
		return "Invalid baud setting";
	if (info->baud != baud)
	{
		info->baud = baud;
		info->reopen_now = true;
	}
	return NULL;
}

static
const char *icarus_set_probe_timeout(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	info->probe_read_count = atof(newvalue) * 10.0 / ICARUS_READ_FAULT_DECISECONDS;
	return NULL;
}

static
const char *icarus_set_work_division(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	const int work_division = atoi(newvalue);
	if (!is_power_of_two(work_division))
		return "Invalid work_division: must be a power of two";
	if (info->user_set & IUS_FPGA_COUNT)
	{
		if (info->fpga_count > work_division)
			return "work_division must be >= fpga_count";
	}
	else
		info->fpga_count = work_division;
	info->user_set |= IUS_WORK_DIVISION;
	info->work_division = work_division;
	info->nonce_mask = mask(work_division);
	return NULL;
}

static
const char *icarus_set_fpga_count(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	const int fpga_count = atoi(newvalue);
	if (fpga_count < 1 || fpga_count > info->work_division)
		return "Invalid fpga_count: must be >0 and <=work_division";
	info->fpga_count = fpga_count;
	return NULL;
}

static
const char *icarus_set_reopen(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	if ((!strcasecmp(newvalue, "never")) || !strcasecmp(newvalue, "-r"))
		info->reopen_mode = IRM_NEVER;
	else
	if (!strcasecmp(newvalue, "timeout"))
		info->reopen_mode = IRM_TIMEOUT;
	else
	if ((!strcasecmp(newvalue, "cycle")) || !strcasecmp(newvalue, "r"))
		info->reopen_mode = IRM_CYCLE;
	else
	if (!strcasecmp(newvalue, "now"))
		info->reopen_now = true;
	else
		return "Invalid reopen mode";
	return NULL;
}

static void icarus_shutdown(struct thr_info *thr)
{
	do_icarus_close(thr);
	free(thr->cgpu_data);
}

const struct bfg_set_device_definition icarus_set_device_funcs[] = {
	// NOTE: Order of parameters below is important for --icarus-options
	{"baud"         , icarus_set_baud         , "serial baud rate"},
	{"work_division", icarus_set_work_division, "number of pieces work is split into"},
	{"fpga_count"   , icarus_set_fpga_count   , "number of chips working on pieces"},
	{"reopen"       , icarus_set_reopen       , "how often to reopen device: never, timeout, cycle, (or now for a one-shot reopen)"},
	// NOTE: Below here, order is irrelevant
	{"probe_timeout", icarus_set_probe_timeout},
	{"timing"       , icarus_set_timing       , "timing of device; see README.FPGA"},
	{NULL},
};

struct device_drv icarus_drv = {
	.dname = "icarus",
	.name = "ICA",
	.probe_priority = -115,
	.lowl_probe = icarus_lowl_probe,
	.get_api_stats = icarus_drv_stats,
	.thread_prepare = icarus_prepare,
	.thread_init = icarus_init,
	.scanhash = icarus_scanhash,
	.job_prepare = icarus_job_prepare,
	.thread_disable = close_device_fd,
	.thread_shutdown = icarus_shutdown,
};
