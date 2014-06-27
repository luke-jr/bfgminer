/*
 * Copyright 2014 Nate Woolls
 * Copyright 2014 ZeusMiner Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "miner.h"
#include "driver-icarus.h"
#include "lowl-vcom.h"

#define ZEUSMINER_IO_SPEED			115200

#define ZEUSMINER_CHIP_GEN1_CORES	8
#define ZEUSMINER_CHIP_CORES		ZEUSMINER_CHIP_GEN1_CORES
#define ZEUSMINER_CHIPS_COUNT_MAX	1
#define ZEUSMINER_CHIPS_COUNT		6
#define ZEUSMINER_DEFAULT_CLOCK		328
#define ZEUSMINER_MIN_CLOCK			200
#define ZEUSMINER_MAX_CLOCK			383


BFG_REGISTER_DRIVER(zeusminer_drv)

static
const struct bfg_set_device_definition zeusminer_set_device_funcs[];

// device helper functions

static
uint32_t zeusminer_calc_clk_header(uint16_t freq)
{
	//set clk_reg based on chip_clk
	uint32_t clk_reg = (uint32_t)(freq * 2 / 3);

	//clock speed mask for header
	uint32_t clk_header = (clk_reg << 24)+ ((0xff - clk_reg) << 16);

	return clk_header;
}

// ICARUS_INFO functions - driver-icarus.h

// device detection

static
bool zeusminer_detect_one(const char *devpath)
{
	struct device_drv *drv = &zeusminer_drv;

	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");

	char scrypt_golden_ob[] =
	"55aa"																//Freq is set to 0x55*1.5=85Mhz
	"0001"																//We want to find a Nonce which result’s diff is at least 32768
	"00038000"															//Starting Nonce
	"063b0b1b"															//Bits (target in compact form)
	"028f3253"															//Timestamp
	"5e900609c15dc49a42b1d8492a6dd4f8f15295c989a1decf584a6aa93be26066"	//Merkle root
	"d3185f55ef635b5865a7a79b7fa74121a6bb819da416328a9bd2f8cef72794bf"	//Previous hash
	"02000000";															//Version

	const char scrypt_golden_nonce[] = "00038d26";

	*info = (struct ICARUS_INFO){
		.baud = ZEUSMINER_IO_SPEED,
		.timing_mode = MODE_DEFAULT,
		.do_icarus_timing = true,
		.work_division = 1,
		.fpga_count = 1,
		.probe_read_count = 5,
		.golden_nonce = (char*)scrypt_golden_nonce,
		.chips = ZEUSMINER_CHIPS_COUNT,
		.freq = ZEUSMINER_DEFAULT_CLOCK,
	};

	//pick up any user-defined settings passed in via --set
	drv_set_defaults(drv, zeusminer_set_device_funcs, info, devpath, detectone_meta_info.serial, 1);

	//3 bits of 32 bits nonce are reserved for cores’ combination
	//10 bits of 32 bits nonce are reserved for chips’ combination
	//the nonce range is split into 2^(10+3) parts
	info->work_division = 8192;

	info->fpga_count = info->chips * ZEUSMINER_CHIP_CORES;

	//send the requested Chip Speed with the detect golden OB
	//we use the time this request takes in order to calc hashes
	//so we need to use the same Chip Speed used when hashing
	uint32_t clk_header = zeusminer_calc_clk_header(info->freq);
	char clk_header_str[10];
	sprintf(clk_header_str, "%08x", clk_header + 1);
	memcpy(scrypt_golden_ob, clk_header_str, 8);

	info->golden_ob = scrypt_golden_ob;

	if (!icarus_detect_custom(devpath, drv, info))
	{
		free(info);
		return false;
	}

	double duration_sec;
	const double hash_count = (double)0xd26;
	uint64_t default_hashes_per_core = (((info->freq * 2) / 3) * 1024) / ZEUSMINER_CHIP_CORES;

	if (info->ignore_golden_nonce)
		duration_sec = hash_count / default_hashes_per_core;
	else
		duration_sec = ((double)(info->golden_tv.tv_sec) + ((double)(info->golden_tv.tv_usec)) / ((double)1000000));

	//determines how the hash rate is calculated when no nonce is returned
	info->Hs = (double)(duration_sec / hash_count / info->chips / ZEUSMINER_CHIP_CORES);

	//set the read_count (how long to wait for a result) based on chips, cores, and time to find a nonce
	int chips_count_max = ZEUSMINER_CHIPS_COUNT_MAX;
	if (info->chips > chips_count_max)
		chips_count_max = upper_power_of_two(info->chips);
	//golden_speed_per_core is the number of hashes / second / core
	uint64_t golden_speed_per_core = (uint64_t)(hash_count / duration_sec);
	//don't combine the following two lines - overflows leaving info->read_count at 0
	info->read_count = (uint32_t)((4294967296 * 10) / (ZEUSMINER_CHIP_CORES * chips_count_max * golden_speed_per_core * 2));
	info->read_count = info->read_count * 3/4;

	return true;
}

// support for --set-device
// must be set before probing the device

static
const char *zeusminer_set_clock(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct ICARUS_INFO * const info = device->device_data;

	int val = atoi(setting);

	if (val < ZEUSMINER_MIN_CLOCK || val > ZEUSMINER_MAX_CLOCK) {
		sprintf(replybuf, "invalid clock: '%s' valid range %d-%d",
				setting, ZEUSMINER_MIN_CLOCK, ZEUSMINER_MAX_CLOCK);
		return replybuf;
	}

	info->freq = val;

	return NULL;
}

static
const char *zeusminer_set_chips(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct ICARUS_INFO * const info = device->device_data;

	info->chips = atoi(setting);

	return NULL;
}

static
const char *zeusminer_set_ignore_golden_nonce(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct ICARUS_INFO * const info = device->device_data;

	info->ignore_golden_nonce = atoi(setting) == 1;

	return NULL;
}

static
const struct bfg_set_device_definition zeusminer_set_device_funcs[] = {
	{ "clock", zeusminer_set_clock, NULL },
	{ "chips", zeusminer_set_chips, NULL },
	{ "ignore_golden_nonce",zeusminer_set_ignore_golden_nonce, NULL },
	{ NULL },
};

static
bool zeusminer_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, zeusminer_detect_one);
}

// device_drv functions - miner.h

static
bool zeusminer_thread_init(struct thr_info * const thr)
{
	struct cgpu_info * const device = thr->cgpu;

	device->min_nonce_diff = 1./0x10000;

	return icarus_init(thr);
}

static
bool zeusminer_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info * const device = thr->cgpu;
	struct icarus_state * const state = thr->cgpu_data;
	struct ICARUS_INFO * const info = device->device_data;

	uint32_t clk_header = zeusminer_calc_clk_header(info->freq);
	uint32_t diff = work->nonce_diff * 0x10000;
	uint32_t target_me = 0xffff / diff;
	uint32_t header = clk_header + target_me;

	swabn(state->ob_bin, (uint8_t *)&header, 4);
	swabn(state->ob_bin + 4, work->data, 80);

	return true;
}

// device_drv definition - miner.h

static
void zeusminer_drv_init()
{
	// based on Icarus
	zeusminer_drv = icarus_drv;

	// metadata
	zeusminer_drv.dname = "zeusminer";
	zeusminer_drv.name = "ZUS";
	zeusminer_drv.supported_algos = POW_SCRYPT;

	// detect device
	zeusminer_drv.lowl_probe = zeusminer_lowl_probe;

	// initialize thread
	zeusminer_drv.thread_init = zeusminer_thread_init;

	// Icarus scanhash mining hooks
	zeusminer_drv.job_prepare = zeusminer_job_prepare;

	// specify driver probe priority
	++zeusminer_drv.probe_priority;
}

struct device_drv zeusminer_drv =
{
	.drv_init = zeusminer_drv_init,
};
