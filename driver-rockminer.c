/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "deviceapi.h"
#include "lowl-vcom.h"
#include "miner.h"

#define ROCKMINER_MIN_FREQ_MHZ  200

#define ROCKMINER_MAX_CHIPS  64
#define ROCKMINER_WORK_REQ_SIZE  0x40
#define ROCKMINER_REPLY_SIZE        8

enum rockminer_replies {
	ROCKMINER_REPLY_NONCE_FOUND = 0,
};

BFG_REGISTER_DRIVER(rockminer_drv)

struct rockminer_chip_data {
	uint8_t next_work_req[ROCKMINER_WORK_REQ_SIZE];
};

static
void rockminer_job_buf_init(uint8_t * const buf, const uint8_t chipid)
{
	memset(&buf[0x20], 0, 0x10);
	buf[0x30] = 0xaa;
	// 0x31 is frequency, filled in elsewhere
	buf[0x32] = chipid;
	buf[0x33] = 0x55;
}

static
void rockminer_job_buf_set_freq(uint8_t * const buf, const unsigned short freq)
{
	buf[0x31] = (freq / 10) - 1;
}

static const uint8_t golden_midstate[] = {
	0x4a, 0x54, 0x8f, 0xe4, 0x71, 0xfa, 0x3a, 0x9a,
	0x13, 0x71, 0x14, 0x45, 0x56, 0xc3, 0xf6, 0x4d,
	0x25, 0x00, 0xb4, 0x82, 0x60, 0x08, 0xfe, 0x4b,
	0xbf, 0x76, 0x98, 0xc9, 0x4e, 0xba, 0x79, 0x46,
};

static const uint8_t golden_datatail[] = {
	                        0xce, 0x22, 0xa7, 0x2f,
	0x4f, 0x67, 0x26, 0x14, 0x1a, 0x0b, 0x32, 0x87,
};

static const uint8_t golden_result[] = {
	0x00, 0x01, 0x87, 0xa2,
};

int8_t rockminer_bisect_chips(const int fd, uint8_t * const buf)
{
	static const int max_concurrent_tests = 4;
	int concurrent_tests = max_concurrent_tests;
	uint8_t tests[max_concurrent_tests];
	uint8_t reply[ROCKMINER_REPLY_SIZE];
	uint8_t minvalid = 0, maxvalid = ROCKMINER_MAX_CHIPS - 1;
	uint8_t pertest;
	char msg[0x10];
	ssize_t rsz;
	
	do {
		pertest = (maxvalid + 1 - minvalid) / concurrent_tests;
		if (!pertest)
			pertest = 1;
		msg[0] = '\0';
		for (int i = 0; i < concurrent_tests; ++i)
		{
			uint8_t chipid = (minvalid + pertest * (i + 1)) - 1;
			if (chipid > maxvalid)
			{
				concurrent_tests = i;
				break;
			}
			tests[i] = chipid;
			
			buf[0x32] = chipid;
			if (write(fd, buf, ROCKMINER_WORK_REQ_SIZE) != ROCKMINER_WORK_REQ_SIZE)
				applogr(-1, LOG_DEBUG, "%s(%d): Error sending request for chip %d", __func__, fd, chipid);
			
			tailsprintf(msg, sizeof(msg), "%d ", chipid);
		}
		
		msg[strlen(msg)-1] = '\0';
		applog(LOG_DEBUG, "%s(%d): Testing chips %s (within range %d-%d)", __func__, fd, msg, minvalid, maxvalid);
		
		while ( (rsz = read(fd, reply, sizeof(reply))) == sizeof(reply))
		{
			const uint8_t chipid = reply[5] & 0x3f;
			if (chipid > minvalid)
			{
				applog(LOG_DEBUG, "%s(%d): Saw chip %d", __func__, fd, chipid);
				minvalid = chipid;
				if (minvalid >= tests[concurrent_tests-1])
					break;
			}
		}
		
		for (int i = concurrent_tests; i--; )
		{
			if (tests[i] > minvalid)
			{
				applog(LOG_DEBUG, "%s(%d): Didn't see chip %d", __func__, fd, tests[i]);
				maxvalid = tests[i] - 1;
			}
			else
				break;
		}
	} while (minvalid != maxvalid);
	
	return maxvalid + 1;
}

static
bool rockminer_detect_one(const char * const devpath)
{
	int fd, chips;
	uint8_t buf[ROCKMINER_WORK_REQ_SIZE], reply[ROCKMINER_REPLY_SIZE];
	ssize_t rsz;
	
	fd = serial_open(devpath, 0, 1, true);
	if (fd < 0)
		return_via_applog(err, , LOG_DEBUG, "%s: %s %s", rockminer_drv.dname, "Failed to open", devpath);
	
	applog(LOG_DEBUG, "%s: %s %s", rockminer_drv.dname, "Successfully opened", devpath);
	
	rockminer_job_buf_init(buf, 0);
	rockminer_job_buf_set_freq(buf, ROCKMINER_MIN_FREQ_MHZ);
	memcpy(&buf[   0], golden_midstate, 0x20);
	memcpy(&buf[0x34], golden_datatail,  0xc);
	
	if (write(fd, buf, sizeof(buf)) != sizeof(buf))
		return_via_applog(err, , LOG_DEBUG, "%s: %s %s", rockminer_drv.dname, "Error sending request to ", devpath);
	
	while (true)
	{
		rsz = read(fd, reply, sizeof(reply));
		if (rsz != sizeof(reply))
			return_via_applog(err, , LOG_DEBUG, "%s: Short read from %s (%d)", rockminer_drv.dname, devpath, rsz);
		if ((!memcmp(reply, golden_result, sizeof(golden_result))) && (reply[4] & 0xf) == ROCKMINER_REPLY_NONCE_FOUND)
			break;
	}
	
	applog(LOG_DEBUG, "%s: Found chip 0 on %s, probing for total chip count", rockminer_drv.dname, devpath);
	chips = rockminer_bisect_chips(fd, buf);
	applog(LOG_DEBUG, "%s: Identified %d chips on %s", rockminer_drv.dname, chips, devpath);
	
	if (serial_claim_v(devpath, &rockminer_drv))
		goto err;
	
	serial_close(fd);
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &rockminer_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
	};
	// NOTE: Xcode's clang has a bug where it cannot find fields inside anonymous unions (more details in fpgautils)
	cgpu->device_fd = -1;
	
	return add_cgpu(cgpu);

err:
	if (fd >= 0)
		serial_close(fd);
	return false;
}

static
bool rockminer_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, rockminer_detect_one);
}

struct device_drv rockminer_drv = {
	.dname = "rockminer",
	.name = "RKM",
	.lowl_probe = rockminer_lowl_probe,
};
