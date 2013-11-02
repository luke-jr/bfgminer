/*
 * Copyright 2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include "deviceapi.h"
#include "fpgautils.h"
#include "logging.h"
#include "miner.h"

struct device_drv dummy_drv;

static
bool dummy_send_command(const int fd, const uint8_t cmd, const void * const data, const size_t datasz)
{
	if (1 != write(fd, &cmd, 1))
		return false;
	if (datasz)
		if (write(fd, data, datasz) != datasz)
			return false;
	return true;
}

static
int dummy_recv_reply(const int fd, void * const bufp, const uint8_t cmd)
{
	uint8_t chk, len;
	uint8_t * const buf = bufp;
	
	if (1 != read(fd, &chk, 1) || (cmd && chk != cmd))
		return -1;
	if (1 != read(fd, &len, 1))
		return -1;
	buf[0] = chk;
	if (len)
		if (len != read(fd, &buf[1], len))
			return -1;
	if (1 != read(fd, &chk, 1) || chk != '\x0a')
		return -1;
	return len;
}

static
int dummy_do_command(const int fd, void * const buf, const uint8_t cmd, const void * const data, const size_t datasz)
{
	if (!dummy_send_command(fd, cmd, data, datasz))
		return -1;
	return dummy_recv_reply(fd, buf, tolower(cmd));
}

static
bool dummy_detect_one(const char * const devpath)
{
	uint8_t buf[0x100];
	int n;
	const int fd = serial_open(devpath, 38400, 1, true);
	if (fd == -1)
		return false;
	
	buf[0] = '\0';
	n = dummy_do_command(fd, buf, 'V', NULL, 0);
	if (buf[1] != 'M' || buf[2] != 'P')
	{
		serial_close(fd);
		return false;
	}
	
	n = dummy_do_command(fd, buf, 'R', NULL, 0);
	n = dummy_do_command(fd, buf, 'I', NULL, 0);
	serial_close(fd);
	
	if (serial_claim_v(devpath, &dummy_drv))
		return false;
	
	struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &dummy_drv,
		.device_path = strdup(devpath),
		.device_fd = -1,
		.threads = 1,
		.procs = buf[1],
	};
	add_cgpu(cgpu);
	
	return true;
}

static int dummy_detect_auto()
{
	return serial_autodetect(dummy_detect_one, "Dummy BFGMiner Device");
}

static void dummy_detect()
{
	serial_detect_auto(&dummy_drv, dummy_detect_one, dummy_detect_auto);
}

static bool dummy_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	
	int fd = serial_open(cgpu->device_path, 38400, 1, true);
	if (unlikely(-1 == fd))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to open %s",
		       cgpu->proc_repr, cgpu->device_path);
		return false;
	}
	
	cgpu->device_fd = fd;
	
	applog(LOG_INFO, "%"PRIpreprv": Opened %s", cgpu->proc_repr, cgpu->device_path);
	
	return true;
}

static
bool dummy_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	const int fd = cgpu->device_fd;
	uint8_t buf[0x100];
	
	if (thr->queue_full)
		return false;
	
	memcpy(&buf[   0], work->midstate , 0x20);
	memcpy(&buf[0x20], &work->data[64], 0x0c);
	dummy_do_command(fd, buf, 'Q', buf, 0x2c);
	
	if (thr->work)
	{
		dummy_send_command(fd, 'W', &buf[2], 1);
		thr->queue_full = true;
	}
	else
		dummy_send_command(fd, 'J', &buf[2], 1);
	
	return true;
}

static
void dummy_queue_flush(struct thr_info * const thr)
{
	// TODO
}

static
void dummy_poll(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	const int fd = cgpu->device_fd;
	struct thr_info *mythr;
	uint8_t buf[0x100];
	int n, i, engineid;
	uint32_t nonce;
	
	n = dummy_recv_reply(fd, buf, 0);
	if (n == -1)
		goto out;
	
	if (buf[0] == 'j')
		goto out;
	
	if (buf[1] & 0x80)
	{
		engineid = buf[1] & 0x7f;
		proc = cgpu;
		for (i = 0; i < engineid; ++i)
			proc = proc->next_proc;
		mythr = proc->thr[0];
		nonce = buf[2] | (buf[3] << 8) | (buf[4] << 16) | (buf[5] << 24);
		submit_nonce(mythr, thr->work, nonce);
		goto out;
	}
	
	thr->queue_full = false;
	
out:
	timer_set_delay_from_now(&thr->tv_poll, 10000);
}

struct device_drv dummy_drv = {
	.dname = "dummy",
	.name = "EMP",
	.drv_detect = dummy_detect,
	
	.thread_init = dummy_init,
	
	.minerloop = minerloop_queue,
	.queue_append = dummy_queue_append,
	.queue_flush = dummy_queue_flush,
	.poll = dummy_poll,
};
