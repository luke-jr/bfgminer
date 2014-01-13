/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 Angus Gratton
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"

BFG_REGISTER_DRIVER(drillbit_drv)

#define DRILLBIT_MIN_VERSION 2
#define DRILLBIT_MAX_VERSION 3

#define DRILLBIT_MAX_WORK_RESULTS 0x400
#define DRILLBIT_MAX_RESULT_NONCES 0x10

enum drillbit_capability {
	DBC_TEMP      = 1,
	DBC_EXT_CLOCK = 2,
};

enum drillbit_voltagecfg {
	DBV_650mV = 0,
	DBV_750mV = 2,
	DBV_850mV = 1,
	DBV_950mV = 3,
};

static
bool drillbit_lowl_match(const struct lowlevel_device_info * const info)
{
	if (!lowlevel_match_id(info, &lowl_vcom, 0, 0))
		return false;
	return (info->manufacturer && strstr(info->manufacturer, "Drillbit"));
}

static
bool drillbit_detect_one(const char * const devpath)
{
	uint8_t buf[0x10];
	const int fd = serial_open(devpath, 0, 1, true);
	if (fd == -1)
		applogr(false, LOG_DEBUG, "%s: %s: Failed to open", __func__, devpath);
	if (1 != write(fd, "I", 1))
	{
		applog(LOG_DEBUG, "%s: %s: Error writing 'I'", __func__, devpath);
err:
		serial_close(fd);
		return false;
	}
	if (sizeof(buf) != serial_read(fd, buf, sizeof(buf)))
	{
		applog(LOG_DEBUG, "%s: %s: Short read in response to 'I'",
		       __func__, devpath);
		goto err;
	}
	serial_close(fd);
	
	const unsigned protover = buf[0];
	const unsigned long serialno = (uint32_t)buf[9] | ((uint32_t)buf[0xa] << 8) | ((uint32_t)buf[0xb] << 16) | ((uint32_t)buf[0xc] << 24);
	char * const product = (void*)&buf[1];
	buf[9] = '\0';  // Ensure it is null-terminated (clobbers serial, but we already parsed it)
	unsigned chips = buf[0xd];
	uint16_t caps = (uint16_t)buf[0xe] | ((uint16_t)buf[0xf] << 8);
	if (!product[0])
		applogr(false, LOG_DEBUG, "%s: %s: Null product name", __func__, devpath);
	if (!serialno)
		applogr(false, LOG_DEBUG, "%s: %s: Serial number is zero", __func__, devpath);
	if (!chips)
		applogr(false, LOG_DEBUG, "%s: %s: No chips found", __func__, devpath);
	
	int loglev = LOG_WARNING;
	if (!strcmp(product, "DRILLBIT"))
	{
		// Hack: first production firmwares all described themselves as DRILLBIT, so fill in the gaps
		if (chips == 1)
			strcpy(product, "Thumb");
		else
			strcpy(product, "Eight");
	}
	else
	if (chips == 8 && !strcmp(product, "Eight"))
	{}  // Known device
	else
	if (chips == 1 && !strcmp(product, "Thumb"))
	{}  // Known device
	else
		loglev = LOG_DEBUG;
	
	if (protover < DRILLBIT_MIN_VERSION || (loglev == LOG_DEBUG && protover > DRILLBIT_MAX_VERSION))
		applogr(false, loglev, "%s: %s: Unknown device protocol version %u.",
		        __func__, devpath, protover);
	if (protover > DRILLBIT_MAX_VERSION)
		applogr(false, loglev, "%s: %s: Device firmware uses newer Drillbit protocol %u. We only support up to %u. Find a newer BFGMiner!",
		        __func__, devpath, protover, (unsigned)DRILLBIT_MAX_VERSION);
	
	if (protover == 2 && chips == 1)
		// Production firmware Thumbs don't set any capability bits, so fill in the EXT_CLOCK one
		caps |= DBC_EXT_CLOCK;
	
	char *serno = malloc(9);
	snprintf(serno, 9, "%08lx", serialno);
	
	if (chips > 0x100)
	{
		applog(LOG_WARNING, "%s: %s: %u chips reported, but driver only supports up to 256",
		       __func__, devpath, chips);
		chips = 0x100;
	}
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &drillbit_drv,
		.device_path = strdup(devpath),
		.dev_product = strdup(product),
		.dev_serial = serno,
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
		//.device_data = ,
	};
	return add_cgpu(cgpu);
}

static
bool drillbit_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, drillbit_detect_one);
}

static
bool drillbit_check_response(const char * const repr, const int fd, struct cgpu_info * const dev, const char expect)
{
	uint8_t ack;
	if (1 != serial_read(fd, &ack, 1))
		applogr(false, LOG_ERR, "%s: Short read in response to '%c'",
		        repr, expect);
	if (ack != expect)
		applogr(false, LOG_ERR, "%s: Wrong response to '%c': %u",
		        dev->dev_repr, expect, (unsigned)ack);
	return true;
}

static
bool drillbit_reset(struct cgpu_info * const dev)
{
	const int fd = dev->device_fd;
	if (unlikely(fd == -1))
		return false;
	
	if (1 != write(fd, "R", 1))
		applogr(false, LOG_ERR, "%s: Error writing reset command", dev->dev_repr);
	
	return drillbit_check_response(dev->dev_repr, fd, dev, 'R');
}

static
bool drillbit_send_config(struct cgpu_info * const dev)
{
	const int fd = dev->device_fd;
	if (unlikely(fd == -1))
		return false;
	
	const uint8_t core_voltage = DBV_850mV;
	const uint8_t clock_level = 40;
	const uint8_t clock_div2 = 0;
	const uint8_t use_ext_clock = 0;
	const uint16_t ext_clock_freq = 200;
	const uint8_t buf[7] = {'C', core_voltage, clock_level, clock_div2, use_ext_clock, ext_clock_freq};
	
	if (sizeof(buf) != write(fd, buf, sizeof(buf)))
		applogr(false, LOG_ERR, "%s: Error sending config", dev->dev_repr);
	
	return drillbit_check_response(dev->dev_repr, fd, dev, 'C');
}

static
bool drillbit_init(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	const int fd = serial_open(dev->device_path, 0, 10, true);
	if (fd == -1)
		return false;
	
	dev->device_fd = fd;
	if (!(drillbit_reset(dev) && drillbit_send_config(dev)))
	{
		serial_close(fd);
		return false;
	}
	
	timer_set_delay_from_now(&master_thr->tv_poll, 10000);
	return true;
}

static
bool drillbit_job_prepare(struct thr_info * const thr, struct work * const work, __maybe_unused const uint64_t max_nonce)
{
	struct cgpu_info * const proc = thr->cgpu;
	const int chipid = proc->proc_id;
	struct cgpu_info * const dev = proc->device;
	const int fd = dev->device_fd;
	uint8_t buf[0x31];
	
	buf[0] = 'W';
	buf[1] = chipid;
	buf[2] = 0;  // high bits of chipid
	memcpy(&buf[3], work->midstate, 0x20);
	memcpy(&buf[0x23], &work->data[0x40], 0xc);
	
	if (sizeof(buf) != write(fd, buf, sizeof(buf)))
		applogr(false, LOG_ERR, "%"PRIpreprv": Error sending work %d",
		        proc->proc_repr, work->id);
	
	if (!drillbit_check_response(proc->proc_repr, fd, dev, 'W'))
		applogr(false, LOG_ERR, "%"PRIpreprv": Error queuing work %d",
		        proc->proc_repr, work->id);
	
	applog(LOG_DEBUG, "%"PRIpreprv": Queued work %d",
	       proc->proc_repr, work->id);
	
	work->blk.nonce = 0xffffffff;
	return true;
}

static
void drillbit_first_job_start(struct thr_info __maybe_unused * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	if (unlikely(!thr->work))
	{
		applog(LOG_DEBUG, "%"PRIpreprv": No current work, assuming immediate start",
		       proc->proc_repr);
		mt_job_transition(thr);
		job_start_complete(thr);
		timer_set_now(&thr->tv_morework);
	}
}

static
int64_t drillbit_job_process_results(struct thr_info *thr, struct work *work, bool stopping)
{
	return 0xbd000000;
}

static
struct cgpu_info *drillbit_find_proc(struct cgpu_info * const dev, int chipid)
{
	struct cgpu_info *proc = dev;
	for (int i = 0; i < chipid; ++i)
	{
		proc = proc->next_proc;
		if (unlikely(!proc))
			return NULL;
	}
	return proc;
}

static
bool bitfury_fudge_nonce2(struct work * const work, uint32_t * const nonce_p)
{
	if (!work)
		return false;
	const uint32_t m7    = *((uint32_t *)&work->data[64]);
	const uint32_t ntime = *((uint32_t *)&work->data[68]);
	const uint32_t nbits = *((uint32_t *)&work->data[72]);
	return bitfury_fudge_nonce(work->midstate, m7, ntime, nbits, nonce_p);
}

static
bool drillbit_get_work_results(struct cgpu_info * const dev)
{
	const int fd = dev->device_fd;
	if (fd == -1)
		return false;
	
	uint8_t buf[4 + (4 * DRILLBIT_MAX_RESULT_NONCES)];
	uint32_t total;
	int i, j;
	
	if (1 != write(fd, "E", 1))
		applogr(false, LOG_ERR, "%s: Error sending request for work results", dev->dev_repr);
	
	if (sizeof(total) != serial_read(fd, &total, sizeof(total)))
		applogr(false, LOG_ERR, "%s: Short read in response to 'E'", dev->dev_repr);
	total = le32toh(total);
	
	if (total > DRILLBIT_MAX_WORK_RESULTS)
		applogr(false, LOG_ERR, "%s: Impossible number of total work: %lu",
		        dev->dev_repr, (unsigned long)total);
	
	for (i = 0; i < total; ++i)
	{
		if (sizeof(buf) != serial_read(fd, buf, sizeof(buf)))
			applogr(false, LOG_ERR, "%s: Short read on %dth total work",
			        dev->dev_repr, i);
		const int chipid = buf[0];
		struct cgpu_info * const proc = drillbit_find_proc(dev, chipid);
		struct thr_info * const thr = proc->thr[0];
		if (unlikely(!proc))
		{
			applog(LOG_ERR, "%s: Unknown chip id %d", dev->dev_repr, chipid);
			continue;
		}
		const bool is_idle = buf[3];
		int nonces = buf[2];
		if (nonces > DRILLBIT_MAX_RESULT_NONCES)
		{
			applog(LOG_ERR, "%"PRIpreprv": More than %d nonces claimed, impossible",
			       proc->proc_repr, (int)DRILLBIT_MAX_RESULT_NONCES);
			nonces = DRILLBIT_MAX_RESULT_NONCES;
		}
		applog(LOG_DEBUG, "%"PRIpreprv": Handling completion of %d nonces. is_idle=%d work=%p next_work=%p",
		       proc->proc_repr, nonces, is_idle, thr->work, thr->next_work);
		const uint32_t *nonce_p = (void*)&buf[4];
		for (j = 0; j < nonces; ++j, ++nonce_p)
		{
			uint32_t nonce = bitfury_decnonce(*nonce_p);
			if (bitfury_fudge_nonce2(thr->work, &nonce))
				submit_nonce(thr, thr->work, nonce);
			else
			if (bitfury_fudge_nonce2(thr->next_work, &nonce))
			{
				applog(LOG_DEBUG, "%"PRIpreprv": Result for next work, transitioning",
				       proc->proc_repr);
				submit_nonce(thr, thr->next_work, nonce);
				mt_job_transition(thr);
				job_start_complete(thr);
			}
			else
			if (bitfury_fudge_nonce2(thr->prev_work, &nonce))
			{
				applog(LOG_DEBUG, "%"PRIpreprv": Result for PREVIOUS work",
				       proc->proc_repr);
				submit_nonce(thr, thr->prev_work, nonce);
			}
			else
				inc_hw_errors(thr, thr->work, nonce);
		}
		if (is_idle && thr->next_work)
		{
			applog(LOG_DEBUG, "%"PRIpreprv": Chip went idle without any results for next work",
			       proc->proc_repr);
			mt_job_transition(thr);
			job_start_complete(thr);
		}
		if (!thr->next_work)
			timer_set_now(&thr->tv_morework);
	}
	
	return true;
}

static
void drillbit_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	
	drillbit_get_work_results(dev);
	
	timer_set_delay_from_now(&master_thr->tv_poll, 10000);
}

struct device_drv drillbit_drv = {
	.dname = "drillbit",
	.name = "DRB",
	
	.lowl_match = drillbit_lowl_match,
	.lowl_probe = drillbit_lowl_probe,
	
	.thread_init = drillbit_init,
	
	.minerloop = minerloop_async,
	.job_prepare = drillbit_job_prepare,
	.job_start = drillbit_first_job_start,
	.job_process_results = drillbit_job_process_results,
	.poll = drillbit_poll,
};
