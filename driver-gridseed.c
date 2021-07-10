/*
 * Copyright 2014 Luke Dashjr
 * Copyright 2014 Nate Woolls
 * Copyright 2014 GridSeed Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>

#include "deviceapi.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "gc3355.h"

#define GRIDSEED_DEFAULT_FREQUENCY  600
// 60Kh/s at 700MHz in ms
#define GRIDSEED_HASH_SPEED         0.08571428571429
// GridSeed driver currently scans a full nonce range
#define GRIDSEED_MAX_NONCE          0xffffffff

BFG_REGISTER_DRIVER(gridseed_drv)

static const struct bfg_set_device_definition gridseed_set_device_funcs_probe[];
static const struct bfg_set_device_definition gridseed_set_device_funcs_live[];

/*
 * helper functions
 */

static
struct cgpu_info *gridseed_alloc_device(const char *path, struct device_drv *driver, struct gc3355_info *info)
{
	struct cgpu_info *device = calloc(1, sizeof(struct cgpu_info));
	if (unlikely(!device))
		quit(1, "Failed to malloc cgpu_info");
	
	device->drv = driver;
	device->device_path = strdup(path);
	device->device_fd = -1;
	device->threads = 1;
	device->device_data = info;
	device->set_device_funcs = gridseed_set_device_funcs_live;
	
	return device;
}

static
struct gc3355_info *gridseed_alloc_info()
{
	struct gc3355_info *info = calloc(1, sizeof(struct gc3355_info));
	if (unlikely(!info))
		quit(1, "Failed to malloc gc3355_info");
	
	info->freq = GRIDSEED_DEFAULT_FREQUENCY;
	
	return info;
}

static
void gridseed_empty_work(int fd)
{
	unsigned char buf[GC3355_READ_SIZE];
	gc3355_read(fd, (char *)buf, GC3355_READ_SIZE);
}

static
struct thr_info *gridseed_thread_by_chip(const struct cgpu_info * const device, uint32_t const chip)
{
	const struct cgpu_info *proc = device_proc_by_id(device, chip);
	if (unlikely(!proc))
		proc = device;
	return proc->thr[0];
}

// return the number of hashes done in elapsed_ms
static
int64_t gridseed_calculate_chip_hashes_ms(const struct cgpu_info * const device, int const elapsed_ms)
{
	struct gc3355_info *info = device->device_data;
	return GRIDSEED_HASH_SPEED * (double)elapsed_ms * (double)(info->freq);
}

// return the number of hashes done since start_tv
static
int64_t gridseed_calculate_chip_hashes(const struct cgpu_info * const device, struct timeval const start_tv)
{
	struct timeval now_tv;
	timer_set_now(&now_tv);
	int elapsed_ms = ms_tdiff(&now_tv, &start_tv);

	return gridseed_calculate_chip_hashes_ms(device, elapsed_ms);
}

// adjust calculated hashes that overflow possible values
static
int64_t gridseed_fix_hashes_done(int64_t const hashes_done)
{
	int64_t result = hashes_done;

	// not possible to complete more than 0xffffffff nonces
	if (unlikely(result > 0xffffffff))
		result = 0xffffffff;

	return result;
}

// report on hashes done since start_tv
// return the number of hashes done since start_tv
static
int64_t gridseed_hashes_done(struct cgpu_info * const device, struct timeval const start_tv, int64_t previous_hashes)
{
	int64_t total_chip_hashes = gridseed_calculate_chip_hashes(device, start_tv);
	total_chip_hashes = gridseed_fix_hashes_done(total_chip_hashes);

	int64_t previous_chip_hashes = previous_hashes / device->procs;
	int64_t recent_chip_hashes = total_chip_hashes - previous_chip_hashes;
	int64_t total_hashes = 0;

	for_each_managed_proc(proc, device)
	{
		total_hashes += recent_chip_hashes;
		hashes_done2(proc->thr[0], recent_chip_hashes, NULL);
	}

	return total_hashes;
}

// return duration in seconds for device to scan a nonce range
static
uint32_t gridseed_nonce_range_duration(const struct cgpu_info * const device)
{
	struct gc3355_info *info = device->device_data;

	// total hashrate of this device:
	uint32_t hashes_per_sec = gridseed_calculate_chip_hashes_ms(device, 1000) * info->chips;
	// amount of time it takes this device to scan a nonce range:
	uint32_t nonce_range_sec = 0xffffffff / hashes_per_sec;

	return nonce_range_sec;
}

/*
 * device detection
 */

static
bool gridseed_detect_custom(const char *path, struct device_drv *driver, struct gc3355_info *info)
{
	int fd = gc3355_open(path);
	if(fd < 0)
		return false;
	
	gridseed_empty_work(fd);
	
	int64_t fw_version = gc3355_get_firmware_version(fd);
	
	if (fw_version == -1)
	{
		applog(LOG_DEBUG, "%s: Invalid detect response from %s", gridseed_drv.dname, path);
		gc3355_close(fd);
		return false;
	}
	
	if (serial_claim_v(path, driver))
		return false;
	
	info->chips = GC3355_ORB_DEFAULT_CHIPS;
	if((fw_version & 0xffff) == 0x1402)
		info->chips = GC3355_BLADE_DEFAULT_CHIPS;
	
	//pick up any user-defined settings passed in via --set
	drv_set_defaults(driver, gridseed_set_device_funcs_probe, info, path, detectone_meta_info.serial, 1);
	
	struct cgpu_info *device = gridseed_alloc_device(path, driver, info);
	device->device_fd = fd;
	device->procs = info->chips;
	
	if (!add_cgpu(device))
		return false;
	
	gc3355_init_miner(device->device_fd, info->freq);
	
	applog(LOG_INFO, "Found %"PRIpreprv" at %s", device->proc_repr, path);
	applog(LOG_DEBUG, "%"PRIpreprv": Init: firmware=%"PRId64", chips=%d", device->proc_repr, fw_version, info->chips);
	
	return true;
}

static
bool gridseed_detect_one(const char *path)
{
	struct gc3355_info *info = gridseed_alloc_info();
	
	if (!gridseed_detect_custom(path, &gridseed_drv, info))
	{
		free(info);
		return false;
	}
	return true;
}

static
bool gridseed_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, gridseed_detect_one);
}

/*
 * setup & shutdown
 */

static
void gridseed_thread_shutdown(struct thr_info *thr)
{
	struct cgpu_info *device = thr->cgpu;

	gc3355_close(device->device_fd);
}

/*
 * scanhash mining loop
 */

// send work to the device
static
bool gridseed_job_start(const struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info *device = thr->cgpu;
	unsigned char cmd[156];

	gc3355_scrypt_reset(device->device_fd);
	gc3355_scrypt_prepare_work(cmd, work);

	// See https://github.com/gridseed/gc3355-doc/blob/master/GC3355_DataSheet.pdf
	// WAIT: Before start a new transaction, WAIT Cycle must be inserted.
	// WAIT Cycle value is programmable register in UART and default wait
	// time is UART receive 32 bits time (One DATA Cycle).
	// Note: prevents register corruption
	cgsleep_ms(100);
	
	// send work
	if (sizeof(cmd) != gc3355_write(device->device_fd, cmd, sizeof(cmd)))
	{
		applog(LOG_ERR, "%s: Failed to send work", device->dev_repr);
		dev_error(device, REASON_DEV_COMMS_ERROR);
		return false;
	}

	// after sending work to the device, minerloop_scanhash-based
	// drivers must set work->blk.nonce to the last nonce to hash
	work->blk.nonce = GRIDSEED_MAX_NONCE;

	return true;
}

static
void gridseed_submit_nonce(struct thr_info * const thr, const unsigned char buf[GC3355_READ_SIZE], struct work * const work)
{
	struct cgpu_info *device = thr->cgpu;
	
	uint32_t nonce = *(uint32_t *)(buf + 4);
	nonce = le32toh(nonce);
	uint32_t chip = nonce / (GRIDSEED_MAX_NONCE / device->procs);
	
	struct thr_info *proc_thr = gridseed_thread_by_chip(device, chip);
	
	submit_nonce(proc_thr, work, nonce);
}

// read from device for nonce or command
// unless the device can target specific nonce ranges, the scanhash routine should loop
// until the device has processed the work item, scanning the full nonce range
// return the total number of hashes done
static
int64_t gridseed_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
	struct cgpu_info *device = thr->cgpu;
	struct timeval start_tv, nonce_range_tv, report_hashes_tv;

	// amount of time it takes this device to scan a nonce range:
	uint32_t nonce_full_range_sec = gridseed_nonce_range_duration(device);
	// timer to break out of scanning should we close in on an entire nonce range
	// should break out before the range is scanned, so we are doing 99% of the range
	uint64_t nonce_near_range_usec = (nonce_full_range_sec * 1000000. * 0.99);
	timer_set_delay_from_now(&nonce_range_tv, nonce_near_range_usec);

	// timer to calculate hashes every 10s
	const uint32_t report_delay = 10 * 1000000;
	timer_set_delay_from_now(&report_hashes_tv, report_delay);

	// start the job
	timer_set_now(&start_tv);
	gridseed_job_start(thr, work);

	// scan for results
	unsigned char buf[GC3355_READ_SIZE];
	int read = 0;
	int fd = device->device_fd;
	int64_t total_hashes = 0;
	bool range_nearly_scanned = false;

	while (!thr->work_restart                                                   // true when new work is available (miner.c)
	    && ((read = gc3355_read(fd, (char *)buf, GC3355_READ_SIZE)) >= 0)       // only check for failure - allow 0 bytes
	    && !(range_nearly_scanned = timer_passed(&nonce_range_tv, NULL)))       // true when we've nearly scanned a nonce range
	{
		if (timer_passed(&report_hashes_tv, NULL))
		{
			total_hashes += gridseed_hashes_done(device, start_tv, total_hashes);
			timer_set_delay_from_now(&report_hashes_tv, report_delay);
		}

		if (read == 0)
			continue;

		if ((buf[0] == 0x55) && (buf[1] == 0x20))
			gridseed_submit_nonce(thr, buf, work);
		else
			applog(LOG_ERR, "%"PRIpreprv": Unrecognized response", device->proc_repr);
	}

	if (read == -1)
	{
		applog(LOG_ERR, "%s: Failed to read result", device->dev_repr);
		dev_error(device, REASON_DEV_COMMS_ERROR);
	}

	// calculate remaining hashes for elapsed time
	// e.g. work_restart ~report_delay after report_hashes_tv
	gridseed_hashes_done(device, start_tv, total_hashes);

	return 0;
}

/*
 * specify settings / options via RPC or command line
 */

// support for --set-device
// must be set before probing the device

static
void gridseed_set_clock_freq(struct cgpu_info * const device, int const val)
{
	struct gc3355_info * const info = device->device_data;

	if ((info->freq != val) &&                          // method called for each processor, we only want to set pll once
	    (device->device_fd > 0))                        // we may not be mining yet, in which case just store freq
	    gc3355_set_pll_freq(device->device_fd, val);    // clock was set via RPC or TUI

	info->freq = val;
}

static
const char *gridseed_set_clock(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	gridseed_set_clock_freq(device, atoi(setting));

	return NULL;
}

static
const char *gridseed_set_chips(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct gc3355_info * const info = device->device_data;
	int val = atoi(setting);
	
	info->chips = val;
	
	return NULL;
}

// for setting clock and chips during probe / detect
static
const struct bfg_set_device_definition gridseed_set_device_funcs_probe[] = {
	{ "clock", gridseed_set_clock, NULL },
	{ "chips", gridseed_set_chips, NULL },
	{ NULL },
};

// for setting clock while mining
static
const struct bfg_set_device_definition gridseed_set_device_funcs_live[] = {
	{ "clock", gridseed_set_clock, NULL },
	{ NULL },
};

/*
 * specify settings / options via TUI
 */

#ifdef HAVE_CURSES
static
void gridseed_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock speed ");
}

static
const char *gridseed_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	static char buf[0x100];  // Static for replies

	switch (input)
	{
		case 'c': case 'C':
		{
			sprintf(buf, "Set clock speed");
			char * const setting = curses_input(buf);

			gridseed_set_clock_freq(proc->device, atoi(setting));

			return "Clock speed changed\n";
		}
	}
	return NULL;
}

static
void gridseed_wlogprint_status(struct cgpu_info * const proc)
{
	struct gc3355_info * const info = proc->device->device_data;
	wlogprint("Clock speed: %d\n", info->freq);
}
#endif

struct device_drv gridseed_drv =
{
	// metadata
	.dname = "gridseed",
	.name = "GSD",
	.drv_min_nonce_diff = common_scrypt_min_nonce_diff,
	
	// detect device
	.lowl_probe = gridseed_lowl_probe,
	
	// specify mining type - scanhash
	.minerloop = minerloop_scanhash,
	
	// scanhash mining hooks
	.scanhash = gridseed_scanhash,
	
	// teardown device
	.thread_shutdown = gridseed_thread_shutdown,

	// TUI support - e.g. setting clock via UI
#ifdef HAVE_CURSES
	.proc_wlogprint_status = gridseed_wlogprint_status,
	.proc_tui_wlogprint_choices = gridseed_tui_wlogprint_choices,
	.proc_tui_handle_choice = gridseed_tui_handle_choice,
#endif
};
