/*
 * Copyright 2013-2015 Luke Dashjr
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
#include "driver-icarus.h"
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

#define DUALMINER_SCRYPT_SM_HASH_TIME   0.00001428571429
#define DUALMINER_SCRYPT_DM_HASH_TIME	0.00003333333333
#define DUALMINER_SHA2_DM_HASH_TIME     0.00000000300000

#define DUALMINER_SCRYPT_READ_TIMEOUT_MS 4800  // 4.8s to read
#define DUALMINER_SHA2_READ_TIMEOUT_MS   1600  // 1.6s to read

#define DUALMINER_0_9V_SHA2_UNITS  60
#define DUALMINER_1_2V_SHA2_UNITS   0

#define DUALMINER_DM_DEFAULT_FREQUENCY  550
#define DUALMINER_SM_DEFAULT_FREQUENCY  850

static
const char sha2_golden_ob[] =
	"55aa0f00a08701004a548fe471fa3a9a"
	"1371144556c3f64d2500b4826008fe4b"
	"bf7698c94eba7946ce22a72f4f672614"
	"1a0b3287";

static
const char sha2_golden_nonce[] = "a2870100";

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
const char scrypt_golden_nonce[] = "dd0c0500";

BFG_REGISTER_DRIVER(dualminer_drv)
static
const struct bfg_set_device_definition dualminer_set_device_funcs[];

// device helper functions

static inline
bool dualminer_is_scrypt(struct ICARUS_INFO * const info)
{
#ifdef USE_SCRYPT
	return info->scrypt;
#else
	return false;
#endif
}

static
void dualminer_teardown_device(int fd)
{
	// set data terminal ready (DTR) status
	set_serial_dtr(fd, BGV_HIGH);
	// set request to send (RTS) status
	set_serial_rts(fd, BGV_LOW);
}

static
void dualminer_init_hashrate(struct cgpu_info * const cgpu)
{
	int fd = cgpu->device_fd;
	struct ICARUS_INFO *info = cgpu->device_data;

	// get clear to send (CTS) status
	if ((gc3355_get_cts_status(fd) != 1) &&  // 0.9v - dip-switch set to B
		(dualminer_is_scrypt(info)))
		// adjust hash-rate for voltage
		info->Hs = DUALMINER_SCRYPT_DM_HASH_TIME;
}

// runs when job starts and the device has been reset (or first run)
static
void dualminer_init_firstrun(struct cgpu_info *icarus)
{
	struct ICARUS_INFO * const info = icarus->device_data;
	int fd = icarus->device_fd;

	gc3355_init_dualminer(fd, opt_pll_freq, !info->dual_mode, false, dualminer_is_scrypt(info));
	
	dualminer_init_hashrate(icarus);

	applog(LOG_DEBUG, "%"PRIpreprv": dualminer: Init: pll=%d, scrypt: %d, scrypt only: %d",
		   icarus->proc_repr,
		   opt_pll_freq,
		   dualminer_is_scrypt(info),
		   dualminer_is_scrypt(info) && !info->dual_mode);
}

// set defaults for options that the user didn't specify
static
void dualminer_set_defaults(int fd)
{
	// set opt_sha2_units defaults depending on dip-switch
	if (opt_sha2_units == -1)
	{
		// get clear to send (CTS) status
		if (gc3355_get_cts_status(fd) == 1)
			opt_sha2_units = DUALMINER_1_2V_SHA2_UNITS;  // dip-switch in L position
		else
			opt_sha2_units = DUALMINER_0_9V_SHA2_UNITS;  // dip-switch in B position
	}
	
	// set opt_pll_freq defaults depending on dip-switch
	if (opt_pll_freq <= 0)
	{
		// get clear to send (CTS) status
		if (gc3355_get_cts_status(fd) == 1)
			opt_pll_freq = DUALMINER_SM_DEFAULT_FREQUENCY; // 1.2v - dip-switch in L position
		else
			opt_pll_freq = DUALMINER_DM_DEFAULT_FREQUENCY; // 0.9v - dip-switch in B position
	}
}

float dualminer_min_nonce_diff(struct cgpu_info * const proc, const struct mining_algorithm * const malgo)
{
	struct ICARUS_INFO * const info = proc ? proc->device_data : NULL;
	switch (malgo->algo)
	{
#ifdef USE_SCRYPT
		case POW_SCRYPT:
			return ((!info) || dualminer_is_scrypt(info)) ? (1./0x10000) : -1.;
#endif
#ifdef USE_SHA256D
		case POW_SHA256D:
			return (info && dualminer_is_scrypt(info)) ? -1. : 1.;
#endif
		default:
			return -1.;
	}
}

// ICARUS_INFO functions - icarus-common.h

// runs after fd is opened but before the device detection code
static
bool dualminer_detect_init(const char *devpath, int fd, struct ICARUS_INFO * const info)
{
	dualminer_set_defaults(fd);
	
	gc3355_init_dualminer(fd, opt_pll_freq, !info->dual_mode, true, dualminer_is_scrypt(info));

	return true;
}

// runs each time a job starts
static
bool dualminer_job_start(struct thr_info * const thr)
{
	struct cgpu_info *icarus = thr->cgpu;
	struct ICARUS_INFO * const info = icarus->device_data;
	struct icarus_state * const state = thr->cgpu_data;
	int fd = icarus->device_fd;

	if (state->firstrun)
		// runs when job starts and the device has been reset (or first run)
		dualminer_init_firstrun(icarus);

	if (dualminer_is_scrypt(info))
	{
		if (info->dual_mode)
			gc3355_scrypt_reset(fd);
		else
			gc3355_scrypt_only_reset(fd);

		// See https://github.com/gridseed/gc3355-doc/blob/master/GC3355_DataSheet.pdf
		// WAIT: Before start a new transaction, WAIT Cycle must be inserted.
		// WAIT Cycle value is programmable register in UART and default wait
		// time is UART receive 32 bits time (One DATA Cycle).
		// Note: prevents register corruption
		cgsleep_ms(100);
	}

	return icarus_job_start(thr);
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
		.work_division = 1,
		.detect_init_func = dualminer_detect_init,
		.job_start_func = dualminer_job_start,
#ifdef USE_SCRYPT
		.scrypt = (get_mining_goal("default")->malgo->algo == POW_SCRYPT),
#endif
	};

	drv_set_defaults(drv, dualminer_set_device_funcs, info, devpath, detectone_meta_info.serial, 1);

	if (dualminer_is_scrypt(info))
	{
		info->golden_ob = (char*)scrypt_golden_ob;
		info->golden_nonce = (char*)scrypt_golden_nonce;
		info->Hs = DUALMINER_SCRYPT_SM_HASH_TIME;
	}
	else
	{
		info->golden_ob = (char*)sha2_golden_ob;
		info->golden_nonce = (char*)sha2_golden_nonce;
		info->Hs = DUALMINER_SHA2_DM_HASH_TIME;
	}

	if (!icarus_detect_custom(devpath, drv, info))
	{
		free(info);
		return false;
	}

	if (dualminer_is_scrypt(info))
		info->read_timeout_ms = DUALMINER_SCRYPT_READ_TIMEOUT_MS; // 4.8s to read
	else
		info->read_timeout_ms = DUALMINER_SHA2_READ_TIMEOUT_MS; // 1.6s to read

	return true;
}

// support for --set-device dualminer:dual_mode=1
// most be set before probing the device

static
const char *dualminer_set_dual_mode(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	int val = atoi(setting);
	info->dual_mode = val == 1;
	return NULL;
}

#ifdef USE_SCRYPT
static
const char *dualminer_set_scrypt(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct ICARUS_INFO * const info = proc->device_data;
	info->scrypt = atoi(newvalue);
	return NULL;
}
#endif

static
const struct bfg_set_device_definition dualminer_set_device_funcs[] = {
	{"dual_mode", dualminer_set_dual_mode, "set to 1 to enable dual algorithm mining"},
#ifdef USE_SCRYPT
	{"scrypt", dualminer_set_scrypt, "set to 1 to put in scrypt mode"},
#endif
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

	memset(state->ob_bin, 0, info->ob_size);

	if (dualminer_is_scrypt(info))
		gc3355_scrypt_prepare_work(state->ob_bin, work);
	else
		gc3355_sha2_prepare_work(state->ob_bin, work);

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
	dualminer_drv.drv_min_nonce_diff = dualminer_min_nonce_diff;
	dualminer_drv.lowl_probe = dualminer_lowl_probe;
	dualminer_drv.thread_shutdown = dualminer_thread_shutdown;
	dualminer_drv.job_prepare = dualminer_job_prepare;
	dualminer_drv.set_device = dualminer_set_device;

	// currently setup specifically to probe after ZeusMiner
	dualminer_drv.probe_priority = -50;
}

struct device_drv dualminer_drv =
{
	.drv_init = dualminer_drv_init,
};
