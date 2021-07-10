/*
 * Copyright 2014 Nate Woolls
 * Copyright 2014 ZeusMiner Team
 * Copyright 2014-2015 Luke Dashjr
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
#include <math.h>

#include "miner.h"
#include "driver-icarus.h"
#include "lowl-vcom.h"

#define ZEUSMINER_IO_SPEED          115200

#define ZEUSMINER_CHIP_GEN1_CORES   8
#define ZEUSMINER_CHIP_CORES        ZEUSMINER_CHIP_GEN1_CORES
#define ZEUSMINER_CHIPS_COUNT_MAX   1
#define ZEUSMINER_CHIPS_COUNT       6
#define ZEUSMINER_DEFAULT_CLOCK     328
#define ZEUSMINER_MIN_CLOCK         200
#define ZEUSMINER_MAX_CLOCK         383


BFG_REGISTER_DRIVER(zeusminer_drv)

static
const struct bfg_set_device_definition zeusminer_set_device_funcs_probe[];

static
const struct bfg_set_device_definition zeusminer_set_device_funcs_live[];

// device helper functions

static
uint32_t zeusminer_calc_clk_header(uint16_t freq)
{
	//set clk_reg based on chip_clk
	uint32_t clk_reg = (uint32_t)freq * 2 / 3;
	
	//clock speed mask for header
	uint32_t clk_header = (clk_reg << 24) + ((0xff - clk_reg) << 16);
	
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
	"0001"																//We want to find a Nonce which result's diff is at least 32768
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
		// if do_icarus_timing is true, the timing adjustment may
		// result in a read_count that considers the device Idle
		.do_icarus_timing = false,
		.probe_read_count = 5,
		.golden_nonce = scrypt_golden_nonce,
		.chips = ZEUSMINER_CHIPS_COUNT,
		.freq = ZEUSMINER_DEFAULT_CLOCK,
	};
	
	//pick up any user-defined settings passed in via --set
	drv_set_defaults(drv, zeusminer_set_device_funcs_probe, info, devpath, detectone_meta_info.serial, 1);
	
	info->work_division = upper_power_of_two_u32(info->chips * ZEUSMINER_CHIP_CORES);
	info->fpga_count = info->chips * ZEUSMINER_CHIP_CORES;
	
	//send the requested Chip Speed with the detect golden OB
	//we use the time this request takes in order to calc hashes
	//so we need to use the same Chip Speed used when hashing
	uint32_t clk_header = zeusminer_calc_clk_header(info->freq);
	char clk_header_str[10];
	sprintf(clk_header_str, "%08x", clk_header + 1);
	memcpy(scrypt_golden_ob, clk_header_str, 8);
	
	info->golden_ob = scrypt_golden_ob;
	
	if (!icarus_detect_custom(devpath, drv, info) &&
		//ZM doesn't respond to detection 1 out of ~30 times
		!icarus_detect_custom(devpath, drv, info))
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
		chips_count_max = upper_power_of_two_u32(info->chips);
	//golden_speed_per_core is the number of hashes / second / core
	uint64_t golden_speed_per_core = (uint64_t)(hash_count / duration_sec);
	//don't combine the following two lines - overflows leaving info->read_count at 0
	info->read_timeout_ms = ((uint64_t)(0x100000000 * 1000)) / (ZEUSMINER_CHIP_CORES * chips_count_max * golden_speed_per_core * 2);
	info->read_timeout_ms = info->read_timeout_ms * 3 / 4;
	
	return true;
}

// support for --set-device
// must be set before probing the device

static
bool zeusminer_set_clock_freq(struct cgpu_info * const device, int const freq)
{
	struct ICARUS_INFO * const info = device->device_data;

	if (freq < ZEUSMINER_MIN_CLOCK || freq > ZEUSMINER_MAX_CLOCK)
		return false;

	info->freq = freq;

	return true;
}

static
const char *zeusminer_set_clock(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	int val = atoi(setting);
	
	if (!zeusminer_set_clock_freq(device, val))
	{
		sprintf(replybuf, "invalid clock: '%s' valid range %d-%d",
		        setting, ZEUSMINER_MIN_CLOCK, ZEUSMINER_MAX_CLOCK);
		return replybuf;
	}
	
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

// for setting clock and chips during probe / detect
static
const struct bfg_set_device_definition zeusminer_set_device_funcs_probe[] = {
	{ "clock", zeusminer_set_clock, NULL },
	{ "chips", zeusminer_set_chips, NULL },
	{ "ignore_golden_nonce",zeusminer_set_ignore_golden_nonce, NULL },
	{ NULL },
};

// for setting clock while mining
static
const struct bfg_set_device_definition zeusminer_set_device_funcs_live[] = {
	{ "clock", zeusminer_set_clock, NULL },
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
	
	device->set_device_funcs = zeusminer_set_device_funcs_live;
	
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
	
	pk_u32be(state->ob_bin, 0, header);
	bswap_32mult(&state->ob_bin[4], work->data, 80/4);
	
	return true;
}

// display the Chip # in the UI when viewing per-proc details
static
bool zeusminer_override_statline_temp2(char *buf, size_t bufsz, struct cgpu_info *device, __maybe_unused bool per_processor)
{
	if (per_processor && ((device->proc_id % ZEUSMINER_CHIP_CORES) == 0))
	{
		tailsprintf(buf, bufsz, "C:%-3d", device->proc_id / ZEUSMINER_CHIP_CORES);
		return true;
	}
	return false;
}

// return the Chip # in via the API when procdetails is called
static
struct api_data *zeusminer_get_api_extra_device_detail(struct cgpu_info *device)
{
	int chip = device->proc_id / ZEUSMINER_CHIP_CORES;
	return api_add_int(NULL, "Chip", &chip, true);
}

/*
 * specify settings / options via TUI
 */

#ifdef HAVE_CURSES
static
void zeusminer_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock speed ");
}

static
const char *zeusminer_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	static char buf[0x100];  // Static for replies

	switch (input)
	{
		case 'c': case 'C':
		{
			sprintf(buf, "Set clock speed");
			char * const setting = curses_input(buf);

			if (zeusminer_set_clock_freq(proc->device, atoi(setting)))
			{
				return "Clock speed changed\n";
			}
			else
			{
				sprintf(buf, "Invalid clock: '%s' valid range %d-%d",
						setting, ZEUSMINER_MIN_CLOCK, ZEUSMINER_MAX_CLOCK);
				return buf;
			}
		}
	}
	return NULL;
}

static
void zeusminer_wlogprint_status(struct cgpu_info * const proc)
{
	struct ICARUS_INFO * const info = proc->device->device_data;
	wlogprint("Clock speed: %d\n", info->freq);
}
#endif

// device_drv definition - miner.h

static
void zeusminer_drv_init()
{
	// based on Icarus
	zeusminer_drv = icarus_drv;
	
	// metadata
	zeusminer_drv.dname = "zeusminer";
	zeusminer_drv.name = "ZUS";
	zeusminer_drv.drv_min_nonce_diff = common_scrypt_min_nonce_diff;
	
	// detect device
	zeusminer_drv.lowl_probe = zeusminer_lowl_probe;
	
	// initialize thread
	zeusminer_drv.thread_init = zeusminer_thread_init;
	
	// Icarus scanhash mining hooks
	zeusminer_drv.job_prepare = zeusminer_job_prepare;
	
	// specify driver probe priority
	// currently setup specifically to probe before DualMiner
	zeusminer_drv.probe_priority = -100;

	// output the chip # when viewing per-proc stats
	// so we can easily ID chips vs cores
	zeusminer_drv.override_statline_temp2 = zeusminer_override_statline_temp2;

	// output the chip # via RPC API
	zeusminer_drv.get_api_extra_device_detail = zeusminer_get_api_extra_device_detail;

	// TUI support - e.g. setting clock via UI
#ifdef HAVE_CURSES
	zeusminer_drv.proc_wlogprint_status = zeusminer_wlogprint_status;
	zeusminer_drv.proc_tui_wlogprint_choices = zeusminer_tui_wlogprint_choices;
	zeusminer_drv.proc_tui_handle_choice = zeusminer_tui_handle_choice;
#endif
}

struct device_drv zeusminer_drv = {
	.drv_init = zeusminer_drv_init,
};
