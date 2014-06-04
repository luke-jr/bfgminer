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
#define GRIDSEED_MAX_QUEUED          10
#define GRIDSEED_HASH_SPEED			0.0851128926	// in ms

BFG_REGISTER_DRIVER(gridseed_drv)

/*
 * helper functions
 */

static
struct cgpu_info *gridseed_alloc_device(const char *path, struct device_drv *driver, struct gc3355_orb_info *info)
{
	struct cgpu_info *device = calloc(1, sizeof(struct cgpu_info));
	if (unlikely(!device))
		quit(1, "Failed to malloc cgpu_info");
	
	device->drv = driver;
	device->device_path = strdup(path);
	device->device_fd = -1;
	device->threads = 1;
	device->device_data = info;
	
	return device;
}

static
struct gc3355_orb_info *gridseed_alloc_info()
{
	struct gc3355_orb_info *info = calloc(1, sizeof(struct gc3355_orb_info));
	if (unlikely(!info))
		quit(1, "Failed to malloc gc3355_orb_info");
	
	info->freq = GRIDSEED_DEFAULT_FREQUENCY;
	
	return info;
}

static
void gridseed_empty_work(int fd)
{
	unsigned char buf[GC3355_READ_SIZE];
	gc3355_read(fd, (char *)buf, GC3355_READ_SIZE);
}

/*
 * device detection
 */

static
bool gridseed_detect_custom(const char *path, struct device_drv *driver, struct gc3355_orb_info *info)
{
	int fd = gc3355_open(path);
	if(fd < 0)
		return false;
	
	gridseed_empty_work(fd);
	
	int64_t fw_version = gc3355_get_firmware_version(fd);
	
	if (fw_version == -1)
	{
		applog(LOG_ERR, "%s: Invalid detect response from %s", gridseed_drv.dname, path);
		gc3355_close(fd);
		return false;
	}
	
	struct cgpu_info *device = gridseed_alloc_device(path, driver, info);
	
	if (serial_claim_v(path, driver))
		return false;
	
	if (!add_cgpu(device))
		return false;
	
	device->device_fd = fd;

	info->chips = GC3355_ORB_DEFAULT_CHIPS;
	if((fw_version & 0xffff) == 0x1402)
		info->chips = GC3355_BLADE_DEFAULT_CHIPS;
	
	gc3355_init_usborb(device->device_fd, info->freq, false, false);
	
	applog(LOG_INFO, "Found %"PRIpreprv" at %s", device->proc_repr, path);
	applog(LOG_DEBUG, "%"PRIpreprv": Init: firmware=%"PRId64", chips=%d", device->proc_repr, fw_version, info->chips);
	
	return true;
}

static
bool gridseed_detect_one(const char *path)
{
	struct gc3355_orb_info *info = gridseed_alloc_info();
	
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
bool gridseed_thread_prepare(struct thr_info *thr)
{
	thr->cgpu_data = calloc(1, sizeof(*thr->cgpu_data));
	
	if (opt_scrypt)
	{
		struct cgpu_info *device = thr->cgpu;
		device->min_nonce_diff = 1./0x10000;
	}
	
	return true;
}

static
void gridseed_thread_shutdown(struct thr_info *thr)
{
	struct cgpu_info *device = thr->cgpu;
	gc3355_close(device->device_fd);
	
	free(thr->cgpu_data);
}

/*
 * scanhash mining loop
 */

// send work to the device
static
bool gridseed_prepare_work(struct thr_info __maybe_unused *thr, struct work *work)
{
	struct cgpu_info *device = thr->cgpu;
	struct gc3355_orb_info *info = device->device_data;

	int work_size = opt_scrypt ? 156 : 52;
	unsigned char cmd[work_size];
	
	cgtime(&info->scanhash_time);

	//from GC3355 docs if we are using FIFO
	work->id = 12345678;

	if (opt_scrypt)
	{
		gc3355_scrypt_reset(device->device_fd);
		gc3355_scrypt_prepare_work(cmd, work);
	}
	else
		gc3355_sha2_prepare_work(cmd, work, true);
	
	// send work
	if (sizeof(cmd) != gc3355_write(device->device_fd, cmd, sizeof(cmd)))
	{
		applog(LOG_ERR, "%s: Failed to send work", device->dev_repr);
		return false;
	}

	return true;
}

static
void gridseed_submit_nonce(struct thr_info * const thr, const unsigned char buf[GC3355_READ_SIZE], struct work * const work)
{
	uint32_t nonce = *(uint32_t *)(buf + 4);
	nonce = le32toh(nonce);
	submit_nonce(thr, work, nonce);
}

static
int64_t gridseed_estimate_hashes(struct thr_info *thr)
{
	struct cgpu_info *device = thr->cgpu;
	struct gc3355_orb_info *info = device->device_data;
	struct timeval old_scanhash_time = info->scanhash_time;
	cgtime(&info->scanhash_time);
	int elapsed_ms = ms_tdiff(&info->scanhash_time, &old_scanhash_time);

	return GRIDSEED_HASH_SPEED * (double)elapsed_ms * (double)(info->freq * info->chips);
}

// read from device for nonce or command
static
int64_t gridseed_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
	struct cgpu_info *device = thr->cgpu;

	unsigned char buf[GC3355_READ_SIZE];
	int read = 0;
	int fd = device->device_fd;

	while (!thr->work_restart && (read = gc3355_read(fd, (char *)buf, GC3355_READ_SIZE)) > 0)
	{
		if (buf[0] == 0x55)
		{
			switch(buf[1]) {
				case 0xaa:
					// Queue length result
					// could watch for watchdog reset here
					break;
				case 0x10: // BTC result
				case 0x20: // LTC result
				{
					gridseed_submit_nonce(thr, buf, work);
					break;
				}
			}
		}
		else
		{
			applog(LOG_ERR, "%"PRIpreprv": Unrecognized response", device->proc_repr);
			break;
		}
	}

	return gridseed_estimate_hashes(thr);
}

/*
 * specify settings / options
 */

// support for --set-device dualminer:clock=freq
static
char *gridseed_set_device(struct cgpu_info *device, char *option, char *setting, char *replybuf)
{
	int val = atoi(setting);
	struct gc3355_orb_info *info = device->device_data;

	if (strcasecmp(option, "clock") == 0)
	{
		info->freq = val;
		int fd = device->device_fd;
		
		gc3355_set_pll_freq(fd, val);
		
		return NULL;
	}

	if (strcasecmp(option, "chips") == 0)
	{
		info->chips = val;

		return NULL;
	}
	
	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

struct device_drv gridseed_drv =
{
	// metadata
	.dname = "gridseed",
	.name = "GSD",
	.supported_algos = POW_SCRYPT,
	
	// detect device
	.lowl_probe = gridseed_lowl_probe,
	
	// initialize device
	.thread_prepare = gridseed_thread_prepare,
	
	// specify mining type - scanhash
	.minerloop = minerloop_scanhash,
	
	// scanhash mining hooks
	.prepare_work = gridseed_prepare_work,
	.scanhash = gridseed_scanhash,
	
	// teardown device
	.thread_shutdown = gridseed_thread_shutdown,
	
	// specify settings / options
	.set_device = gridseed_set_device,
};
