/*
 * Copyright 2013-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <uthash.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "miner.h"
#include "util.h"

BFG_REGISTER_DRIVER(bifury_drv)
static const struct bfg_set_device_definition bifury_set_device_funcs[];

const char bifury_init_cmds[] = "flush\ntarget ffffffff\nmaxroll 0\n";

static
ssize_t bifury_write(const struct cgpu_info * const dev, const void * const buf, const size_t count)
{
	const int fd = dev->device_fd;
	if (opt_dev_protocol)
	{
		const int psz = (((const char*)buf)[count-1] == '\n') ? (count - 1) : count;
		applog(LOG_DEBUG, "%s: DEVPROTO: SEND %.*s", dev->dev_repr, psz, (const char*)buf);
	}
	return write(fd, buf, count);
}

static
void *bifury_readln(int fd, bytes_t *leftover)
{
	uint8_t buf[0x40];
	ssize_t r;
	
parse:
	if ( (r = bytes_find(leftover, '\n')) >= 0)
	{
		uint8_t *ret = malloc(r+1);
		if (r)
			memcpy(ret, bytes_buf(leftover), r);
		ret[r] = '\0';
		bytes_shift(leftover, r + 1);
		return ret;
	}
	if ( (r = read(fd, buf, sizeof(buf))) > 0)
	{
		bytes_append(leftover, buf, r);
		goto parse;
	}
	return NULL;
}

struct bifury_state {
	bytes_t buf;
	work_device_id_t last_work_id;
	int needwork;
	bool has_needwork;
	uint8_t *osc6_bits;
	bool send_clock;
	unsigned max_queued;
	bool free_after_job;
};

static
bool bifury_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_product(info, "bi\xe2\x80\xa2""fury");
}

const char *bifury_init_chips(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	int *procs_p = proc->device_data;
	
	if (!setting || !*setting)
		return "missing setting";
	
	const int val = atoi(setting);
	if (val < 1)
		return "invalid setting";
	
	*procs_p = val;
	
	return NULL;
}

static const struct bfg_set_device_definition bifury_set_device_funcs_probe[] = {
	{"chips", bifury_init_chips, NULL},
	{NULL},
};

static
bool bifury_detect_one(const char * const devpath)
{
	char buf[0x40], *p, *q, *s;
	bytes_t reply = BYTES_INIT;
	int major, minor, hwrev, chips;
	struct cgpu_info *cgpu;
	struct timeval tv_timeout;
	const int fd = serial_open(devpath, 0, 10, true);
	applog(LOG_DEBUG, "%s: %s %s",
	       bifury_drv.dname,
	       ((fd == -1) ? "Failed to open" : "Successfully opened"),
	       devpath);
	
	if (unlikely(fd == -1))
		return false;
	
	while (read(fd, buf, sizeof(buf)) == sizeof(buf))
	{}
	
	if (opt_dev_protocol)
		applog(LOG_DEBUG, "%s fd=%d: DEVPROTO: SEND %s", bifury_drv.dname, fd, "version");
	if (8 != write(fd, "version\n", 8))
	{
		applog(LOG_DEBUG, "%s: Error sending version request", bifury_drv.dname);
		goto err;
	}
	
	timer_set_delay_from_now(&tv_timeout, 1000000);
	while (true)
	{
		p = bifury_readln(fd, &reply);
		if (p)
		{
			if (opt_dev_protocol)
				applog(LOG_DEBUG, "%s fd=%d: DEVPROTO: RECV %s",
				       bifury_drv.dname, fd, p);
			if (!strncmp("version ", p, 8))
				break;
			free(p);
		}
		if (timer_passed(&tv_timeout, NULL))
		{
			applog(LOG_DEBUG, "%s: Timed out waiting for response to version request",
			       bifury_drv.dname);
			goto err;
		}
	}
	
	bytes_free(&reply);
	serial_close(fd);
	
	s = p;
	major = strtol(&p[8], &p, 10);
	if (p == &buf[8] || p[0] != '.')
		goto parseerr;
	minor = strtol(&p[1], &q, 10);
	if (p == q || strncmp(" rev ", q, 5))
		goto parseerr;
	hwrev = strtol(&q[5], &p, 10);
	if (p == q || strncmp(" chips ", p, 7))
		goto parseerr;
	chips = strtol(&p[7], &q, 10);
	if (p == q || chips < 1)
		goto parseerr;
	free(s);
	
	applog(LOG_DEBUG, "%s: Found firmware %d.%d on hardware rev %d with %d chips",
	       bifury_drv.dname, major, minor, hwrev, chips);
	
	if (serial_claim_v(devpath, &bifury_drv))
		return false;
	
	drv_set_defaults(&bifury_drv, bifury_set_device_funcs_probe, &chips, devpath, detectone_meta_info.serial, 1);
	
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &bifury_drv,
		.device_path = strdup(devpath),
		.set_device_funcs = bifury_set_device_funcs,
		.deven = DEV_ENABLED,
		.procs = chips,
		.threads = 1,
		.cutofftemp = 75,
	};
	// NOTE: Xcode's clang has a bug where it cannot find fields inside anonymous unions (more details in fpgautils)
	cgpu->device_fd = -1;
	
	return add_cgpu(cgpu);

parseerr:
	applog(LOG_DEBUG, "%s: Error parsing version response", bifury_drv.dname);
	free(s);
	return false;

err:
	bytes_free(&reply);
	serial_close(fd);
	return false;
}

static
bool bifury_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, bifury_detect_one);
}

static
bool bifury_set_queue_full(const struct cgpu_info * const dev, int needwork)
{
	struct bifury_state * const state = dev->device_data;
	struct thr_info * const master_thr = dev->thr[0];
	const int fd = dev->device_fd;
	if (needwork != -1)
		state->needwork = needwork;
	const bool full = (fd == -1 || !state->needwork);
	if (full == master_thr->queue_full)
		return full;
	for (const struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
	{
		struct thr_info * const thr = proc->thr[0];
		thr->queue_full = full;
	}
	return full;
}

void bifury_send_clock(const struct cgpu_info * const dev)
{
	struct bifury_state * const state = dev->device_data;
	const struct cgpu_info *proc;
	size_t clockbufsz = 5 + (3 * dev->procs) + 1 + 1;
	char clockbuf[clockbufsz];
	strcpy(clockbuf, "clock");
	proc = dev;
	for (int i = 0; i < dev->procs; ++i, (proc = proc->next_proc))
	{
		const struct thr_info * const thr = proc->thr[0];
		int clk;
		if (proc->deven == DEV_ENABLED && !thr->pause)
			clk = state->osc6_bits[i];
		else
			clk = 0;
		tailsprintf(clockbuf, clockbufsz, " %d", clk);
	}
	tailsprintf(clockbuf, clockbufsz, "\n");
	--clockbufsz;
	if (clockbufsz != bifury_write(dev, clockbuf, clockbufsz))
	{
		state->send_clock = true;
		applog(LOG_ERR, "%s: Failed to send clock assignments",
		       dev->dev_repr);
	}
	else
		state->send_clock = false;
}

static
bool bifury_thread_init(struct thr_info *master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu, *proc;
	struct bifury_state * const state = malloc(sizeof(*state));
	if (!state)
		return false;
	*state = (struct bifury_state){
		.buf = BYTES_INIT,
		.osc6_bits = malloc(sizeof(*state->osc6_bits) * dev->procs),
		.max_queued = dev->procs * 5 + 6,
		.free_after_job = true,
	};
	for (int i = 0; i < dev->procs; ++i)
		state->osc6_bits[i] = 54;
	for (proc = dev; proc; proc = proc->next_proc)
	{
		proc->device_data = state;
		proc->status = LIFE_INIT2;
	}
	bifury_set_queue_full(dev, 0);
	timer_set_now(&master_thr->tv_poll);
	return true;
}

static
void bifury_reinit(struct cgpu_info * const proc)
{
	timer_set_now(&proc->thr[0]->tv_poll);
}

void bifury_trigger_send_clock(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct bifury_state * const state = proc->device_data;
	
	state->send_clock = true;
}

static
void bifury_common_error(struct cgpu_info * const dev, const enum dev_reason reason)
{
	for (struct cgpu_info *proc = dev; proc; proc = proc->next_proc)
	{
		struct thr_info * const thr = proc->thr[0];
		dev_error(proc, reason);
		inc_hw_errors_only(thr);
	}
}

static
bool bifury_queue_append(struct thr_info * const thr, struct work *work)
{
	const struct cgpu_info * const dev = thr->cgpu->device;
	struct bifury_state * const state = dev->device_data;
	if (bifury_set_queue_full(dev, -1))
		return false;
	
	struct thr_info * const master_thr = dev->thr[0];
	char buf[5 + 0x98 + 1 + 8 + 1];
	memcpy(buf, "work ", 5);
	bin2hex(&buf[5], work->data, 0x4c);
	work->device_id = ++state->last_work_id;
	sprintf(&buf[5 + 0x98], " %08x", work->device_id);
	buf[5 + 0x98 + 1 + 8] = '\n';
	if (sizeof(buf) != bifury_write(dev, buf, sizeof(buf)))
	{
		applog(LOG_ERR, "%s: Failed to send work", dev->dev_repr);
		return false;
	}
	HASH_ADD(hh, master_thr->work_list, device_id, sizeof(work->device_id), work);
	int prunequeue = HASH_COUNT(master_thr->work_list) - state->max_queued;
	if (prunequeue > 0)
	{
		struct work *tmp;
		applog(LOG_DEBUG, "%s: Pruning %d old work item%s",
		       dev->dev_repr, prunequeue, prunequeue == 1 ? "" : "s");
		HASH_ITER(hh, master_thr->work_list, work, tmp)
		{
			HASH_DEL(master_thr->work_list, work);
			free_work(work);
			if (--prunequeue < 1)
				break;
		}
	}
	bifury_set_queue_full(dev, state->needwork - 1);
	return true;
}

static
void bifury_queue_flush(struct thr_info * const thr)
{
	const struct cgpu_info *dev = thr->cgpu;
	if (dev != dev->device)
		return;
	const int fd = dev->device_fd;
	if (fd != -1)
		bifury_write(dev, "flush\n", 6);
	bifury_set_queue_full(dev, dev->procs);
}

static
void bifury_handle_cmd(struct cgpu_info * const dev, const char * const cmd)
{
	struct thr_info * const master_thr = dev->thr[0];
	struct bifury_state * const state = dev->device_data;
	struct thr_info *thr;
	struct work *work;
	char *p;
	
	if (!strncmp(cmd, "submit ", 7))
	{
		// submit <nonce> <jobid> <timestamp> <chip>
		uint32_t nonce = strtoll(&cmd[7], &p, 0x10);
		const uint32_t jobid = strtoll(&p[1], &p, 0x10);
		const uint32_t ntime = strtoll(&p[1], &p, 0x10);
		const int chip = atoi(&p[1]);
		const struct cgpu_info *proc = device_proc_by_id(dev, chip);
		if (unlikely(!proc))
			proc = dev;
		thr = proc->thr[0];
		
		HASH_FIND(hh, master_thr->work_list, &jobid, sizeof(jobid), work);
		if (work)
		{
			const uint32_t work_ntime = be32toh(*(uint32_t*)&work->data[68]);
			submit_noffset_nonce(thr, work, nonce, ntime - work_ntime);
			hashes_done2(thr, 0x100000000, NULL);
		}
		else
		if (!jobid)
			applog(LOG_DEBUG, "%s: Dummy submit ignored", dev->dev_repr);
		else
			inc_hw_errors2(thr, NULL, &nonce);
		if (!state->has_needwork)
			bifury_set_queue_full(dev, state->needwork + 2);
	}
	else
	if (!strncmp(cmd, "temp ", 5))
	{
		struct cgpu_info *proc;
		const int decicelsius = atoi(&cmd[5]);
		if (decicelsius)
		{
			const float celsius = 0.1 * (float)decicelsius;
			for (proc = dev; proc; proc = proc->next_proc)
				proc->temp = celsius;
			if (decicelsius >= 800)
				// Thermal overheat causes restart losing the clock, so resend it while temperature is over 80 C
				state->send_clock = true;
		}
	}
	else
	if (!strncmp(cmd, "job ", 4))
	{
		// job <jobid> <timestamp> <chip>
		const uint32_t jobid = strtoll(&cmd[4], &p, 0x10);
		strtoll(&p[1], &p, 0x10);
		const int chip = atoi(&p[1]);
		const struct cgpu_info * const proc = device_proc_by_id(dev, chip);
		
		HASH_FIND(hh, master_thr->work_list, &jobid, sizeof(jobid), work);
		if (likely(work))
		{
			if (unlikely(!proc))
				applog(LOG_DEBUG, "%s: Unknown chip id: %s",
				       dev->dev_repr, cmd);
			if (state->free_after_job)
			{
				HASH_DEL(master_thr->work_list, work);
				free_work(work);
			}
		}
		else
			applog(LOG_WARNING, "%s: Unknown job id: %s",
			       dev->dev_repr, cmd);
	}
	else
	if (!strncmp(cmd, "hwerror ", 8))
	{
		const int chip = atoi(&cmd[8]);
		const struct cgpu_info * const proc = device_proc_by_id(dev, chip);
		if (unlikely(!proc))
			applogr(, LOG_DEBUG, "%s: Unknown chip id: %s",
			        dev->dev_repr, cmd);
		thr = proc->thr[0];
		inc_hw_errors2(thr, NULL, UNKNOWN_NONCE);
	}
	else
	if (!strncmp(cmd, "needwork ", 9))
	{
		const int needwork = atoi(&cmd[9]);
		state->has_needwork = true;
		bifury_set_queue_full(dev, needwork);
		applog(LOG_DEBUG, "%s: needwork=%d", dev->dev_repr, state->needwork);
	}
}

static
void bifury_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	struct bifury_state * const state = dev->device_data;
	int fd = dev->device_fd;
	char *cmd;
	
	if (unlikely(fd == -1))
	{
		fd = serial_open(dev->device_path, 0, 1, true);
		if (unlikely(fd == -1))
		{
			applog(LOG_ERR, "%s: Failed to open %s",
			       dev->dev_repr, dev->device_path);
			bifury_common_error(dev, REASON_THREAD_FAIL_INIT);
			return;
		}
		
		dev->device_fd = fd;
		if (sizeof(bifury_init_cmds)-1 != bifury_write(dev, bifury_init_cmds, sizeof(bifury_init_cmds)-1))
		{
			applog(LOG_ERR, "%s: Failed to send configuration", dev->dev_repr);
			bifury_common_error(dev, REASON_THREAD_FAIL_INIT);
			serial_close(fd);
			dev->device_fd = -1;
			return;
		}
		
		bifury_set_queue_full(dev, dev->procs * 2);
		state->send_clock = true;
	}
	
	if (state->send_clock)
		bifury_send_clock(dev);
	
	while ( (cmd = bifury_readln(fd, &state->buf)) )
	{
		if (opt_dev_protocol)
			applog(LOG_DEBUG, "%s: DEVPROTO: RECV %s", dev->dev_repr, cmd);
		bifury_handle_cmd(dev, cmd);
		free(cmd);
	}
}

static
struct api_data *bifury_api_device_status(struct cgpu_info * const proc)
{
	struct bifury_state * const state = proc->device_data;
	struct api_data *root = NULL;
	int osc6_bits = state->osc6_bits[proc->proc_id];
	
	root = api_add_int(root, "Clock Bits", &osc6_bits, true);
	
	return root;
}

const char *bifury_set_osc6_bits(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct bifury_state * const state = proc->device_data;
	
	if (!setting || !*setting)
		return "missing setting";
	
	const int val = atoi(setting);
	if (val < 33 || val > 63)
		return "invalid setting";
	
	state->osc6_bits[proc->proc_id] = val;
	state->send_clock = true;
	
	return NULL;
}

const char *bifury_set_max_queued(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct bifury_state * const state = proc->device_data;
	
	if (!setting || !*setting)
		return "missing setting";
	
	const long val = strtol(setting, NULL, 0);
	if (val < 1 || val > UINT_MAX)
		return "invalid setting";
	
	state->max_queued = val;
	
	return NULL;
}

const char *bifury_set_free_after_job(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct bifury_state * const state = proc->device_data;
	
	if (!setting || !*setting)
		return "missing setting";
	
	char *end;
	const bool val = bfg_strtobool(setting, &end, 0);
	if (end[0] && !isspace(end[0]))
		return "invalid setting";
	
	state->free_after_job = val;
	
	return NULL;
}

#ifdef HAVE_CURSES
void bifury_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[O]scillator bits ");
}

const char *bifury_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	struct bifury_state * const state = proc->device_data;
	
	switch (input)
	{
		case 'o': case 'O':
		{
			const int val = curses_int("Set oscillator bits (range 33-63; slow to fast)");
			if (val < 33 || val > 63)
				return "Invalid oscillator bits\n";
			
			state->osc6_bits[proc->proc_id] = val;
			state->send_clock = true;
			
			return "Oscillator bits changing\n";
		}
	}
	return NULL;
}

void bifury_wlogprint_status(struct cgpu_info * const proc)
{
	const struct bifury_state * const state = proc->device_data;
	const int osc6_bits = state->osc6_bits[proc->proc_id];
	wlogprint("Oscillator bits: %d\n", osc6_bits);
}
#endif

static const struct bfg_set_device_definition bifury_set_device_funcs[] = {
	{"max_queued", bifury_set_max_queued, NULL},
	{"free_after_job", bifury_set_free_after_job, NULL},
	{"osc6_bits", bifury_set_osc6_bits, "range 33-63 (slow to fast)"},
	{NULL},
};

struct device_drv bifury_drv = {
	.dname = "bifury",
	.name = "BIF",
	.lowl_match = bifury_lowl_match,
	.lowl_probe = bifury_lowl_probe,
	
	.thread_init = bifury_thread_init,
	.reinit_device = bifury_reinit,
	.thread_disable = bifury_trigger_send_clock,
	.thread_enable = bifury_trigger_send_clock,
	
	.minerloop = minerloop_queue,
	.queue_append = bifury_queue_append,
	.queue_flush = bifury_queue_flush,
	.poll = bifury_poll,
	
	.get_api_extra_device_status = bifury_api_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bifury_wlogprint_status,
	.proc_tui_wlogprint_choices = bifury_tui_wlogprint_choices,
	.proc_tui_handle_choice = bifury_tui_handle_choice,
#endif
};
