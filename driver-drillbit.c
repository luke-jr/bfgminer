/*
 * Copyright 2013-2014 Luke Dashjr
 * Copyright 2013-2014 Angus Gratton
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
#include <string.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"

BFG_REGISTER_DRIVER(drillbit_drv)

#define DRILLBIT_MIN_VERSION 2
#define DRILLBIT_MAX_VERSION 4

#define DRILLBIT_MAX_WORK_RESULTS 0x400
#define DRILLBIT_MAX_RESULT_NONCES 0x10

enum drillbit_capability {
	DBC_TEMP      = 1,
	DBC_EXT_CLOCK = 2,
};

struct drillbit_board {
	unsigned core_voltage;
	unsigned clock_freq;
	bool clock_div2;
	bool use_ext_clock;
	bool need_reinit;
	bool trigger_identify;
	uint16_t caps;
	uint8_t protover;
};

static
bool drillbit_lowl_match(const struct lowlevel_device_info * const info)
{
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
	uint16_t caps = buf[0xe] | ((uint16_t)buf[0xf] << 8);
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
	if ((chips >= 8) && (chips <= 64) && (chips % 8 == 0) && !strcmp(product, "Eight"))
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
	
	if (chips > 0x100)
	{
		applog(LOG_WARNING, "%s: %s: %u chips reported, but driver only supports up to 256",
		       __func__, devpath, chips);
		chips = 0x100;
	}
	
	if (serial_claim_v(devpath, &drillbit_drv))
		return false;
	
	intptr_t device_data = caps | ((intptr_t)protover << 16); // Store capabilities & protocol version in device_data, temporarily

	char *serno = malloc(9);
	snprintf(serno, 9, "%08lx", serialno);
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &drillbit_drv,
		.device_path = strdup(devpath),
		.dev_product = strdup(product),
		.dev_serial = serno,
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
		.device_data = (void *)device_data,
	};
	return add_cgpu(cgpu);
}

static
bool drillbit_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, drillbit_detect_one);
}

static
void drillbit_problem(struct cgpu_info * const dev)
{
	struct thr_info * const master_thr = dev->thr[0];
	
	if (dev->device_fd != -1)
	{
		serial_close(dev->device_fd);
		dev->device_fd = -1;
	}
	timer_set_delay_from_now(&master_thr->tv_poll, 5000000);
}

#define problem(...)  do{  \
	drillbit_problem(dev);  \
	applogr(__VA_ARGS__);  \
}while(0)

static
bool drillbit_check_response(const char * const repr, const int fd, struct cgpu_info * const dev, const char expect)
{
	uint8_t ack;
	if (1 != serial_read(fd, &ack, 1))
		problem(false, LOG_ERR, "%s: Short read in response to '%c'",
		        repr, expect);
	if (ack != expect)
		problem(false, LOG_ERR, "%s: Wrong response to '%c': %u",
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
		problem(false, LOG_ERR, "%s: Error writing reset command", dev->dev_repr);
	
	return drillbit_check_response(dev->dev_repr, fd, dev, 'R');
}

static
bool drillbit_send_config(struct cgpu_info * const dev)
{
	const int fd = dev->device_fd;
	if (unlikely(fd == -1))
		return false;
	
	const struct drillbit_board * const board = dev->device_data;
	uint8_t buf[7] = {'C'};
	if(board->protover < 4) {
		if(board->core_voltage < 750)
			buf[1] = 0; // 650mV
		else if(board->core_voltage < 850)
			buf[1] = 1; // 750mV
		else if(board->core_voltage < 950)
			buf[1] = 2; // 850mV
		else
			buf[1] = 3; // 950mV
		if(board->clock_freq < 64) // internal clock level, either direct or MHz/5
			buf[2] = board->clock_freq;
		else
			buf[2] = board->clock_freq / 5;
		buf[3] = board->clock_div2 ? 1 : 0;
		buf[4] = board->use_ext_clock ? 1 : 0;
		buf[5] = board->clock_freq;
		buf[6] = board->clock_freq >> 8;
	}
	else {
		buf[1] = board->core_voltage;
		buf[2] = board->core_voltage >> 8;
		buf[3] = board->clock_freq;
		buf[4] = board->clock_freq >> 8;
		buf[5] = board->clock_div2 ? 1 : 0;
		buf[6] = board->use_ext_clock ? 1 : 0;
	}

	if (sizeof(buf) != write(fd, buf, sizeof(buf)))
		problem(false, LOG_ERR, "%s: Error sending config", dev->dev_repr);
	
	return drillbit_check_response(dev->dev_repr, fd, dev, 'C');
}

static bool drillbit_resend_jobs(struct cgpu_info *proc);

static
bool drillbit_reconfigure(struct cgpu_info * const dev, const bool reopen)
{
	struct thr_info * const master_thr = dev->thr[0];
	int fd = dev->device_fd;
	if (reopen || fd == -1)
	{
		if (fd != -1)
			serial_close(fd);
		
		dev->device_fd = fd = serial_open(dev->device_path, 0, 10, true);
		if (fd == -1)
			return false;
	}
	
	if (!(drillbit_reset(dev) && drillbit_send_config(dev)))
	{
		serial_close(fd);
		dev->device_fd = -1;
		return false;
	}
	
	for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
		drillbit_resend_jobs(proc);
	
	timer_set_delay_from_now(&master_thr->tv_poll, 10000);
	
	return true;
}

static
bool drillbit_ensure_configured(struct cgpu_info * const dev)
{
	if (dev->device_fd != -1)
		return true;
	return drillbit_reconfigure(dev, false);
}

static
bool drillbit_init(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	
	dev->device_fd = -1;

	intptr_t device_data = (intptr_t)dev->device_data; // capabilities & protocol version stored here

	struct drillbit_board * const board = malloc(sizeof(*board));
	*board = (struct drillbit_board){
		.core_voltage = 850,
		.clock_freq = 200,
		.clock_div2 = false,
		.use_ext_clock = false,
		.caps = device_data,
		.protover = device_data >> 16,
	};
	dev->device_data = board;
	
	drillbit_reconfigure(dev, false);
	
	return true;
}

static
bool drillbit_job_prepare(struct thr_info * const thr, struct work * const work, __maybe_unused const uint64_t max_nonce)
{
	struct cgpu_info * const proc = thr->cgpu;
	const int chipid = proc->proc_id;
	struct cgpu_info * const dev = proc->device;
	uint8_t buf[0x2f];
	
	if (!drillbit_ensure_configured(dev))
		return false;
	const int fd = dev->device_fd;
	
	buf[0] = 'W';
	buf[1] = chipid;
	buf[2] = 0;  // high bits of chipid
	memcpy(&buf[3], work->midstate, 0x20);
	memcpy(&buf[0x23], &work->data[0x40], 0xc);
	
	if (sizeof(buf) != write(fd, buf, sizeof(buf)))
		problem(false, LOG_ERR, "%"PRIpreprv": Error sending work %d",
		        proc->proc_repr, work->id);
	
	if (!drillbit_check_response(proc->proc_repr, fd, dev, 'W'))
		problem(false, LOG_ERR, "%"PRIpreprv": Error queuing work %d",
		        proc->proc_repr, work->id);
	
	applog(LOG_DEBUG, "%"PRIpreprv": Queued work %d",
	       proc->proc_repr, work->id);
	
	work->blk.nonce = 0xffffffff;
	return true;
}

static
bool drillbit_resend_jobs(struct cgpu_info * const proc)
{
	struct thr_info * const thr = proc->thr[0];
	bool rv = true;
	
	if (thr->work)
		if (!drillbit_job_prepare(thr, thr->work, 0))
		{
			applog(LOG_WARNING, "%"PRIpreprv": Failed to resend %s work",
			       proc->proc_repr, "current");
			rv = false;
		}
	if (thr->next_work)
	{
		if (!drillbit_job_prepare(thr, thr->next_work, 0))
		{
			applog(LOG_WARNING, "%"PRIpreprv": Failed to resend %s work",
			       proc->proc_repr, "next");
			rv = false;
		}
		if (!rv)
		{
			// Fake transition so we kinda recover eventually
			mt_job_transition(thr);
			job_start_complete(thr);
			timer_set_now(&thr->tv_morework);
		}
	}
	return rv;
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
	
	do {
		if (1 != write(fd, "E", 1))
			problem(false, LOG_ERR, "%s: Error sending request for work results", dev->dev_repr);
	
		if (sizeof(total) != serial_read(fd, &total, sizeof(total)))
			problem(false, LOG_ERR, "%s: Short read in response to 'E'", dev->dev_repr);
		total = le32toh(total);
	
		if (total > DRILLBIT_MAX_WORK_RESULTS)
			problem(false, LOG_ERR, "%s: Impossible number of total work: %lu",
				dev->dev_repr, (unsigned long)total);
	
		for (i = 0; i < total; ++i)
		{
			if (sizeof(buf) != serial_read(fd, buf, sizeof(buf)))
				problem(false, LOG_ERR, "%s: Short read on %dth total work",
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
			applog(LOG_DEBUG, "%"PRIpreprv": Handling completion of %d nonces from chip %d. is_idle=%d work=%p next_work=%p",
				proc->proc_repr, nonces, chipid, is_idle, thr->work, thr->next_work);
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
	} while(total > 0);
	
	return true;
}

static
void drillbit_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	struct drillbit_board * const board = dev->device_data;
	
	if (!drillbit_ensure_configured(dev))
		return;
	
	drillbit_get_work_results(dev);
	
	if (board->need_reinit)
	{
		applog(LOG_NOTICE, "%s: Reinitialisation needed for configuration changes",
		       dev->dev_repr);
		drillbit_reconfigure(dev, false);
		board->need_reinit = false;
	}
	if (board->trigger_identify)
	{
		const int fd = dev->device_fd;
		applog(LOG_DEBUG, "%s: Sending identify command", dev->dev_repr);
		if (1 != write(fd, "L", 1))
			applog(LOG_ERR, "%s: Error writing identify command", dev->dev_repr);
		drillbit_check_response(dev->dev_repr, fd, dev, 'L');
		board->trigger_identify = false;
	}
	
	timer_set_delay_from_now(&master_thr->tv_poll, 10000);
}

static bool drillbit_identify(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct drillbit_board * const board = dev->device_data;
	board->trigger_identify = true;
	return true;
}

static
bool drillbit_get_stats(struct cgpu_info * const dev)
{
	if (dev != dev->device)
		return true;
	
	struct drillbit_board * const board = dev->device_data;
	if (!(board->caps & DBC_TEMP))
		return true;
	
	const int fd = dev->device_fd;
	if (fd == -1)
		return false;
	
	if (1 != write(fd, "T", 1))
		problem(false, LOG_ERR, "%s: Error requesting temperature", dev->dev_repr);
	
	uint8_t buf[2];
	
	if (sizeof(buf) != serial_read(fd, buf, sizeof(buf)))
		problem(false, LOG_ERR, "%s: Short read in response to 'T'", dev->dev_repr);
	
	float temp = ((uint16_t)buf[0]) | ((uint16_t)buf[1] << 8);
	temp /= 10.;
	for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
		proc->temp = temp;
	
	return true;
}

static
void drillbit_clockcfg_str(char * const buf, size_t bufsz, struct drillbit_board * const board)
{
	snprintf(buf, bufsz, "%u", board->clock_freq);
	if (board->clock_div2)
		tailsprintf(buf, bufsz, ":2");
}

static
struct api_data *drillbit_api_stats(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct drillbit_board * const board = dev->device_data;
	struct api_data *root = NULL;
	char buf[0x100];
	
	drillbit_clockcfg_str(buf, sizeof(buf), board);
	root = api_add_string(root, "ClockCfg", buf, true);
	
	float volts = board->core_voltage / 1000.0;
	root = api_add_volts(root, "Voltage", &volts, true);
	
	return root;
}

static
char *drillbit_set_device(struct cgpu_info * const proc, char * const option, char *setting, char * const replybuf)
{
	struct cgpu_info * const dev = proc->device;
	struct drillbit_board * const board = dev->device_data;
	
	if (!strcasecmp(option, "help"))
	{
		sprintf(replybuf,
			"voltage: 0.65, 0.75, 0.85, or 0.95 (volts)\n"
			"clock: %sL0-L63 for internal clock levels; append :2 to activate div2",
			(board->caps & DBC_EXT_CLOCK) ? "0-255 (MHz) using external clock (80-230 recommended), or " : ""
		);
		return replybuf;
	}
	
	if (!strcasecmp(option, "voltage"))
	{
		// NOTE: Do not use replybuf in here without implementing it in drillbit_tui_handle_choice
		if (!setting || !*setting)
			return "Missing voltage setting";
		const int val = atof(setting) * 1000;
		board->core_voltage = val;
		board->need_reinit = true;
		
		return NULL;
	}
	
	if (!strcasecmp(option, "clock"))
	{
		// NOTE: Do not use replybuf in here without implementing it in drillbit_tui_handle_choice
		const bool use_ext_clock = !(setting[0] == 'L');
		char *end = &setting[use_ext_clock ? 0 : 1];
		const long int num = strtol(end, &end, 0);
		const bool div2 = (end[0] == ':' && end[1] == '2');
		// NOTE: board assignments are ordered such that it is safe to race
		if (use_ext_clock)
		{
			if (!(board->caps & DBC_EXT_CLOCK))
				return "External clock not supported by this device";
			board->use_ext_clock = true;
		}
		if (num < 0 || num > 0xffff)
			return "Clock frequency out of range (0-65535)";
		board->clock_div2 = div2;
		board->clock_freq = num;
		board->need_reinit = true;
		return NULL;
	}
	
	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

#ifdef HAVE_CURSES
static
void drillbit_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock [V]oltage ");
}

static
const char *drillbit_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	char *val;
	switch (input)
	{
		case 'c': case 'C':
			val = curses_input("Set clock (80-230 MHz using external clock, or L0-L63 for internal clock levels; append :2 to activate div2");
			return drillbit_set_device(proc, "clock", val, NULL) ?: "Requesting clock change";
		case 'v': case 'V':
			val = curses_input("Set voltage (0.65, 0.75, 0.85, or 0.95)");
			return drillbit_set_device(proc, "voltage", val, NULL) ?: "Requesting voltage change";
	}
	return NULL;
}

static
void drillbit_wlogprint_status(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct drillbit_board * const board = dev->device_data;
	char buf[0x100];
	
	drillbit_clockcfg_str(buf, sizeof(buf), board);
	wlogprint("Clock: %s\n", buf);
	wlogprint("Voltage: %.2f\n", board->core_voltage / 1000.0);
}
#endif

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
	.get_stats = drillbit_get_stats,
	.identify_device = drillbit_identify,
	
	.get_api_stats = drillbit_api_stats,
	.set_device = drillbit_set_device,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = drillbit_wlogprint_status,
	.proc_tui_wlogprint_choices = drillbit_tui_wlogprint_choices,
	.proc_tui_handle_choice = drillbit_tui_handle_choice,
#endif
};
