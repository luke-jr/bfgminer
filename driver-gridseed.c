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

#define GRIDSEED_DEFAULT_FREQUENCY		600
#define GRIDSEED_MAX_QUEUED				10

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
	device->procs = GC3355_ORB_DEFAULT_CHIPS;
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

	uint32_t fw_version = gc3355_get_firmware_version(fd);

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

	gc3355_init_usborb(device->device_fd, info->freq, false, false);

	applog(LOG_INFO, "Found %"PRIpreprv" at %s", device->proc_repr, path);
	applog(LOG_DEBUG, "%"PRIpreprv": Init: firmware=%d", device->proc_repr, fw_version);

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
bool gridseed_set_queue_full(const struct cgpu_info * const device, int needwork);

static
bool gridseed_thread_init(struct thr_info *master_thr)
{
	struct cgpu_info * const device = master_thr->cgpu, *proc;
	gridseed_set_queue_full(device, 0);
	timer_set_now(&master_thr->tv_poll);

	// kick off queue minerloop
	gridseed_set_queue_full(device, device->procs * 2);

	return true;
}

static
void gridseed_thread_shutdown(struct thr_info *thr)
{
	struct cgpu_info *device = thr->cgpu;
	gc3355_close(device->device_fd);

	free(thr->cgpu_data);
}

static
void gridseed_reinit_device(struct cgpu_info * const proc)
{
	timer_set_now(&proc->thr[0]->tv_poll);
}

/*
 * queued mining loop
 */

static
bool gridseed_set_queue_full(const struct cgpu_info * const device, int needwork)
{
	struct gc3355_orb_info * const info = device->device_data;
	struct thr_info * const master_thr = device->thr[0];

	if (needwork != -1)
		info->needwork = needwork;

	const bool full = (device->device_fd == -1 || !info->needwork);

	if (full == master_thr->queue_full)
		return full;

	for (const struct cgpu_info *proc = device; proc; proc = proc->next_proc)
	{
		struct thr_info * const thr = proc->thr[0];
		thr->queue_full = full;
	}

	return full;
}

static
bool gridseed_send_work(const struct cgpu_info * const device, struct work *work)
{
	struct gc3355_orb_info * const info = device->device_data;
	int work_size = opt_scrypt ? 156 : 52;
	unsigned char cmd[work_size];

	if (opt_scrypt)
	{
		gc3355_scrypt_reset(device->device_fd);
		gc3355_scrypt_prepare_work(cmd, work);
	}
	else
	{
		gc3355_sha2_prepare_work(cmd, work, true);
	}

	// send work
	if (sizeof(cmd) != gc3355_write(device->device_fd, cmd, sizeof(cmd)))
	{
		applog(LOG_ERR, "%s: Failed to send work", device->dev_repr);
		return false;
	}

	return true;
}

static
void gridseed_prune_queue(const struct cgpu_info * const device, struct work *work)
{
	struct thr_info * const master_thr = device->thr[0];

	// prune queue
	int prunequeue = HASH_COUNT(master_thr->work_list) - GRIDSEED_MAX_QUEUED;
	if (prunequeue > 0)
	{
		struct work *tmp;
		applog(LOG_DEBUG, "%s: Pruning %d old work item%s",
		       device->dev_repr, prunequeue, prunequeue == 1 ? "" : "s");
		HASH_ITER(hh, master_thr->work_list, work, tmp)
		{
			HASH_DEL(master_thr->work_list, work);
			free_work(work);
			if (--prunequeue < 1)
				break;
		}
	}
}

// send work to the device & queue work
static
bool gridseed_queue_append(struct thr_info * const thr, struct work *work)
{
	const struct cgpu_info * const device = thr->cgpu->device;
	struct gc3355_orb_info * const info = device->device_data;
	struct thr_info * const master_thr = device->thr[0];

	// if queue is full (-1 is a check flag) do not append new work
	if (gridseed_set_queue_full(device, -1))
		return false;

	// send work
	if (!gridseed_send_work(device, work))
		return false;

	// store work in queue
	HASH_ADD(hh, master_thr->work_list, id, sizeof(work->id), work);

	// prune queue
	gridseed_prune_queue(device, work);

	// sets info->needwork equal to 2nd arg and updates "full" flags
	gridseed_set_queue_full(device, info->needwork - 1);

	return true;
}

static
void gridseed_queue_flush(struct thr_info * const thr)
{
	const struct cgpu_info *device = thr->cgpu;
	if (device != device->device)
		return;

	gridseed_set_queue_full(device, device->procs);
}

static
const struct cgpu_info *gridseed_proc_by_id(const struct cgpu_info * const dev, int procid)
{
	const struct cgpu_info *proc = dev;
	for (int i = 0; i < procid; ++i)
	{
		proc = proc->next_proc;
		if (unlikely(!proc))
			return NULL;
	}
	return proc;
}

static
void gridseed_submit_nonce(struct thr_info * const master_thr, const unsigned char buf[GC3355_READ_SIZE])
{
	struct work *work;
	uint32_t nonce;
	int workid;
	struct cgpu_info * const device = master_thr->cgpu;
	struct gc3355_orb_info * const info = device->device_data;

	// extract workid from buffer
	memcpy(&workid, buf + 8, 4);
	// extract nonce from buffer
	memcpy(&nonce, buf + 4, 4);
	// extract chip # from nonce
	const int chip = nonce / ((uint32_t)0xffffffff / GC3355_ORB_DEFAULT_CHIPS);
	// find processor by device & chip
	const struct cgpu_info *proc = gridseed_proc_by_id(device, chip);
	// default process to device
	if (unlikely(!proc))
		proc = device;
	// the thread specific to the ASIC chip:
	struct thr_info * thr = proc->thr[0];

	nonce = htole32(nonce);

	// find the queued work for this nonce, by workid
	HASH_FIND(hh, master_thr->work_list, &workid, sizeof(workid), work);
	if (work)
	{
		submit_nonce(thr, work, nonce);

		HASH_DEL(master_thr->work_list, work);

		gridseed_set_queue_full(device, info->needwork + 2);
	}
}

static
void gridseed_estimate_hashes(const struct cgpu_info * const device)
{
	const struct cgpu_info *proc = device;
	const struct gc3355_orb_info *info = device->device_data;

	while (true)
	{
		hashes_done2(proc->thr[0], info->freq * 0xA4, NULL);
		proc = proc->next_proc;
		if (unlikely(!proc))
			return;
	}
}

#define GRIDSEED_SHORT_WORK_DELAY_MS	20
#define	GRIDSEED_LONG_WORK_DELAY_MS		30

// read from device for nonce or command
static
void gridseed_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const device = master_thr->cgpu;
	int fd = device->device_fd;
	unsigned char buf[GC3355_READ_SIZE];
	int read = 0;
	struct timeval tv_timeout;
	timer_set_delay_from_now(&tv_timeout, GRIDSEED_LONG_WORK_DELAY_MS * 1000); // X MS
	bool timeout = false;

	while (!master_thr->work_restart && (read = gc3355_read(device->device_fd, (char *)buf, GC3355_READ_SIZE)) > 0)
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
					gridseed_submit_nonce(master_thr, buf);
					break;
				}
			}
		} else
		{
			applog(LOG_ERR, "%"PRIpreprv": Unrecognized response", device->proc_repr);
			break;
		}

		if (timer_passed(&tv_timeout, NULL))
		{
			// allow work to be sent to the device
			applog(LOG_DEBUG, "%s poll: timeout met", device->dev_repr);
			timeout = true;
			break;
		}
	}

	gridseed_estimate_hashes(device);

	// allow work to be sent to the device
	timer_set_delay_from_now(&master_thr->tv_poll, GRIDSEED_SHORT_WORK_DELAY_MS * 1000); // X MS
}

/*
 * specify settings / options
 */

// support for --set-device dualminer:clock=freq
static
char *gridseed_set_device(struct cgpu_info *device, char *option, char *setting, char *replybuf)
{
	if (strcasecmp(option, "clock") == 0)
	{
		int val = atoi(setting);

		struct gc3355_orb_info *info = (struct gc3355_orb_info *)(device->device_data);
		info->freq = val;
		int fd = device->device_fd;

		gc3355_set_pll_freq(fd, val);

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
	.thread_init = gridseed_thread_init,
	.reinit_device = gridseed_reinit_device,

	// specify mining type - scanhash
	.minerloop = minerloop_queue,

	// queued mining hooks
	.queue_append = gridseed_queue_append,
	.queue_flush = gridseed_queue_flush,
	.poll = gridseed_poll,

	// teardown device
	.thread_shutdown = gridseed_thread_shutdown,

	// specify settings / options
	.set_device = gridseed_set_device,
};
