/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2014 Nate Woolls
 * Copyright 2014 Dualminer Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "miner.h"
#include "icarus-common.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "deviceapi.h"
#include "logging.h"
#include "util.h"
#include "gc3355.h"

#ifndef WIN32
  #include <sys/ioctl.h>
#else
  #include <io.h>
#endif

#define DUALMINER_IO_SPEED 115200

#define DUALMINER_SCRYPT_HASH_TIME		0.00001428571429
#define DUALMINER_SCRYPT_DM_HASH_TIME	0.00003333333333
#define DUALMINER_SHA2_HASH_TIME		0.00000000300000

#define DUALMINER_SCRYPT_READ_COUNT 48  // 4.8s to read
#define DUALMINER_SHA2_READ_COUNT	16  // 1.6s to read

static
const char sha2_golden_ob[] =
	"55aa0f00a08701004a548fe471fa3a9a"
	"1371144556c3f64d2500b4826008fe4b"
	"bf7698c94eba7946ce22a72f4f672614"
	"1a0b3287";

static
const char sha2_golden_nonce[] = "000187a2";

static
const char scrypt_golden_ob[] =
	"55aa1f00000000000000000000000000"
	"000000000000000000000000aaaaaaaa"
	"711c0000603ebdb6e35b05223c54f815"
	"5ac33123006b4192e7aafafbeb9ef654"
	"4d2973d700000002069b9f9e3ce8a677"
	"8dea3d7a00926cd6eaa9585502c9b83a"
	"5601f198d7fbf09be9559d6335ebad36"
	"3e4f147a8d9934006963030b4e54c408"
	"c837ebc2eeac129852a55fee1b1d88f6"
	"000c050000000600";

static
const char scrypt_golden_nonce[] = "00050cdd";

enum
{
	RTS_LOW = 0,
	RTS_HIGH = 1
};

BFG_REGISTER_DRIVER(dualminer_drv)
static
const struct bfg_set_device_definition dualminer_set_device_funcs[];

// device helper functions

static
void dualminer_bootstrap_device(int fd)
{
	gc3355_dual_reset(fd);

	if (opt_scrypt && !opt_dual_mode)
		gc3355_opt_scrypt_only_init(fd);
	else
		gc3355_dualminer_init(fd);

	usleep(1000);
}

static
void dualminer_teardown_device(int fd)
{
	if (opt_scrypt)
		gc3355_open_scrypt_unit(fd, SCRYPT_UNIT_CLOSE);
	else
		gc3355_open_sha2_unit(fd, "0");

	gc3355_set_rts_status(fd, RTS_LOW);
}

static
void dualminer_init_firstrun(struct cgpu_info *icarus)
{
	struct ICARUS_INFO *info = icarus->device_data;
	int fd = icarus->device_fd;

	dualminer_bootstrap_device(fd);

	if (opt_scrypt)
		gc3355_set_rts_status(fd, RTS_HIGH);

	gc3355_init(fd, opt_dualminer_sha2_gating, !opt_dual_mode);
	applog(LOG_DEBUG, "%"PRIpreprv": scrypt: %d, scrypt only: %d; have fan: %d\n", icarus->proc_repr, opt_scrypt, opt_scrypt, opt_hubfans);

	if (gc3355_get_cts_status(fd) != 1)
	{
		// Scrypt + SHA2 mode
		if (opt_scrypt)
			info->Hs = DUALMINER_SCRYPT_DM_HASH_TIME;
	}

	if (opt_scrypt)
		icarus->min_nonce_diff = 1./0x10000;

	applog(LOG_DEBUG, "%"PRIpreprv": dualminer: Init: pll=%d, sha2num=%d", icarus->proc_repr, opt_pll_freq, opt_sha2_number);
}

// ICARUS_INFO functions - icarus-common.h

static
bool dualminer_detect_init(const char *devpath, int fd)
{
	dualminer_bootstrap_device(fd);

	return true;
}

static
bool dualminer_job_start_init(const struct thr_info *thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct icarus_state * const state = thr->cgpu_data;
	int fd = icarus->device_fd;

	if (state->firstrun)
		dualminer_init_firstrun(icarus);

	if (opt_scrypt)
	{
		if (opt_dual_mode)
			gc3355_dualminer_init(fd);
		else
			gc3355_opt_scrypt_init(fd);
	}

	return true;
}

// device detection

static
bool dualminer_detect_one(const char *devpath)
{
	struct device_drv *drv = &dualminer_drv;

	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");

	*info = (struct ICARUS_INFO){
		.baud = DUALMINER_IO_SPEED,
		.timing_mode = MODE_DEFAULT,
		.do_icarus_timing = false,
		.nonce_littleendian = true,
		.work_division = 2,
		.fpga_count = 2,
		.detect_init_func = dualminer_detect_init,
		.job_start_init_func = dualminer_job_start_init
	};

	if (opt_scrypt)
	{
		info->golden_ob = (char*)scrypt_golden_ob;
		info->golden_nonce = (char*)scrypt_golden_nonce;
		info->Hs = DUALMINER_SCRYPT_HASH_TIME;
	}
	else
	{
		info->golden_ob = (char*)sha2_golden_ob;
		info->golden_nonce = (char*)sha2_golden_nonce;
		info->Hs = DUALMINER_SHA2_HASH_TIME;
	}

	drv_set_defaults(drv, dualminer_set_device_funcs, info, devpath, detectone_meta_info.serial, 1);

	if (!icarus_detect_custom(devpath, drv, info))
	{
		free(info);
		return false;
	}

	if (opt_scrypt)
		info->read_count = DUALMINER_SCRYPT_READ_COUNT; // 4.8s to read
	else
		info->read_count = DUALMINER_SHA2_READ_COUNT; // 1.6s to read

	return true;
}

// support for --set-device dualminer:dual_mode=1
// most be set before probing the device

static
const char *dualminer_set_dual_mode(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	int val = atoi(setting);
	opt_dual_mode = val == 1;
	return NULL;
}

static
const struct bfg_set_device_definition dualminer_set_device_funcs[] = {
	{"dual_mode", dualminer_set_dual_mode, "set to 1 to enable dual algorithm mining with two BFGMiner processes"},
	{NULL},
};

// device_drv functions - miner.h

static
bool dualminer_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, dualminer_detect_one);
}

static
void dualminer_thread_shutdown(struct thr_info *thr)
{
	// dualminer teardown
	dualminer_teardown_device(thr->cgpu->device_fd);

	// icarus teardown
	do_icarus_close(thr);
	free(thr->cgpu_data);
}

static
bool dualminer_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info * const icarus = thr->cgpu;
	struct icarus_state * const state = thr->cgpu_data;
	struct ICARUS_INFO * const info = icarus->device_data;
	int fd = icarus->device_fd;

	memset(state->ob_bin, 0, sizeof(state->ob_bin));

	if (opt_scrypt)
	{
		state->ob_bin[0] = 0x55;
		state->ob_bin[1] = 0xaa;
		state->ob_bin[2] = 0x1f;
		state->ob_bin[3] = 0x00;
		memcpy(state->ob_bin + 4, work->target, 32);
		memcpy(state->ob_bin + 36, work->midstate, 32);
		memcpy(state->ob_bin + 68, work->data, 80);
		state->ob_bin[148] = 0xff;
		state->ob_bin[149] = 0xff;
		state->ob_bin[150] = 0xff;
		state->ob_bin[151] = 0xff;
	}
	else
	{
		uint8_t temp_bin[64];
		memset(temp_bin, 0, 64);
		memcpy(temp_bin, work->midstate, 32);
		memcpy(temp_bin+52, work->data + 64, 12);
		state->ob_bin[0] = 0x55;
		state->ob_bin[1] = 0xaa;
		state->ob_bin[2] = 0x0f;
		state->ob_bin[3] = 0x00;
		memcpy(state->ob_bin + 8, temp_bin, 32);
		memcpy(state->ob_bin + 40, temp_bin + 52, 12);
	}

	return true;
}

// support for --set-device dualminer:clock=freq
static
char *dualminer_set_device(struct cgpu_info *cgpu, char *option, char *setting, char *replybuf)
{
	if (strcasecmp(option, "clock") == 0)
	{
		int val = atoi(setting);
		opt_pll_freq = val;
		return NULL;
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

// device_drv definition - miner.h

static
void dualminer_drv_init()
{
	dualminer_drv = icarus_drv;
	dualminer_drv.dname = "dualminer";
	dualminer_drv.name = "DMU";
	dualminer_drv.lowl_probe = dualminer_lowl_probe;
	dualminer_drv.thread_shutdown = dualminer_thread_shutdown;
	dualminer_drv.job_prepare = dualminer_job_prepare;
	dualminer_drv.set_device = dualminer_set_device;
	++dualminer_drv.probe_priority;
}

struct device_drv dualminer_drv =
{
	.drv_init = dualminer_drv_init,
};
