/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 * To avoid doubt: Programs using the minergate protocol to interface with
 * BFGMiner are considered part of the Corresponding Source, and not an
 * independent work. This means that such programs distrbuted with BFGMiner
 * must be released under the terms of the GPLv3 license, or sufficiently
 * compatible terms.
 */

#include "config.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "miner.h"

#define MINERGATE_MAX_NONCE_DIFF  0x20

static const char * const minergate_stats_file = "/var/run/mg_rate_temp";

#define MINERGATE_PROTOCOL_VER  6
#define MINERGATE_MAGIC  0xcaf4
static const int minergate_max_responses = 300;
#define MINERGATE_PKT_HEADER_SZ       8
#define MINERGATE_PKT_REQ_ITEM_SZ  0x34
#define MINERGATE_PKT_REQ_MAX     100
#define MINERGATE_PKT_RSP_ITEM_SZ  0x14
#define MINERGATE_PKT_RSP_MAX     300
#define MINERGATE_POLL_US      100000
#define MINERGATE_RETRY_US    5000000

#define MINERGATE_PKT_REQ_SZ  (MINERGATE_PKT_HEADER_SZ + (MINERGATE_PKT_REQ_ITEM_SZ * MINERGATE_PKT_REQ_MAX))
#define MINERGATE_PKT_RSP_SZ  (MINERGATE_PKT_HEADER_SZ + (MINERGATE_PKT_RSP_ITEM_SZ * MINERGATE_PKT_RSP_MAX))

BFG_REGISTER_DRIVER(minergate_drv)

enum minergate_reqpkt_flags {
	MRPF_FIRST = 1,
	MRPF_FLUSH = 2,
};

struct minergate_state {
	work_device_id_t next_jobid;
	unsigned ready_to_queue;
	uint8_t *req_buffer;
	long *stats;
	unsigned stats_count;
	struct work *flushed_work;
};

static
int minergate_open(const char * const devpath)
{
	size_t devpath_len = strlen(devpath);
	struct sockaddr_un sa = {
		.sun_family = AF_UNIX,
	};
#ifdef UNIX_PATH_MAX
	if (devpath_len >= UNIX_PATH_MAX)
#else
	if (devpath_len >= sizeof(sa.sun_path))
#endif
		return -1;
	const int fd = socket(PF_UNIX, SOCK_STREAM, 0);
	strcpy(sa.sun_path, devpath);
	if (connect(fd, &sa, sizeof(sa)))
	{
		close(fd);
		return -1;
	}
	return fd;
}

static
ssize_t minergate_read(const int fd, void * const buf_p, size_t bufLen)
{
	uint8_t *buf = buf_p;
	ssize_t rv, ret = 0;
	while (bufLen > 0)
	{
		rv = read(fd, buf, bufLen);
		if (rv <= 0)
		{
			if (ret > 0)
				return ret;
			return rv;
		}
		buf += rv;
		bufLen -= rv;
		ret += rv;
	}
	return ret;
}

static
bool minergate_detect_one(const char * const devpath)
{
	bool rv = false;
	const int fd = minergate_open(devpath);
	if (unlikely(fd < 0))
		applogr(false, LOG_DEBUG, "%s: %s: Cannot connect", minergate_drv.dname, devpath);
	
	int epfd = -1;
	uint8_t buf[MINERGATE_PKT_REQ_SZ] = {0xbf, 0x90, MINERGATE_PROTOCOL_VER, MRPF_FIRST, 0,0, 0 /* req count */,};
	pk_u16le(buf, 4, MINERGATE_MAGIC);
	if (MINERGATE_PKT_REQ_SZ != write(fd, buf, MINERGATE_PKT_REQ_SZ))
		return_via_applog(out, , LOG_DEBUG, "%s: %s: write incomplete or failed", minergate_drv.dname, devpath);
	
	epfd = epoll_create(1);
	if (epfd < 0)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s failed", minergate_drv.dname, devpath, "epoll_create");
	
	struct epoll_event eev;
	eev.events = EPOLLIN;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &eev))
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s failed", minergate_drv.dname, devpath, "epoll_ctl");
	
	size_t read_bytes = 0;
	static const size_t read_expect = MINERGATE_PKT_HEADER_SZ;
	ssize_t r;
	while (read_bytes < read_expect)
	{
		if (epoll_wait(epfd, &eev, 1, 1000) != 1)
			return_via_applog(out, , LOG_DEBUG, "%s: %s: Timeout waiting for response", minergate_drv.dname, devpath);
		r = read(fd, &buf[read_bytes], read_expect - read_bytes);
		if (r <= 0)
			return_via_applog(out, , LOG_DEBUG, "%s: %s: %s failed", minergate_drv.dname, devpath, "read");
		read_bytes += r;
	}
	
	if (buf[1] != 0x90)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s mismatch", minergate_drv.dname, devpath, "request_id");
	if (buf[2] != MINERGATE_PROTOCOL_VER)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s mismatch", minergate_drv.dname, devpath, "Protocol version");
	if (upk_u16le(buf, 4) != MINERGATE_MAGIC)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s mismatch", minergate_drv.dname, devpath, "magic");
	
	uint16_t responses = upk_u16le(buf, 6);
	if (responses > minergate_max_responses)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: More than maximum responses", minergate_drv.dname, devpath);
	
	if (bfg_claim_any2(&minergate_drv, devpath, "unix", devpath))
		goto out;
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &minergate_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.threads = 1,
	};
	rv = add_cgpu(cgpu);
	
out:
	close(fd);
	if (epfd >= 0)
		close(epfd);
	return rv;
}

static
int minergate_detect_auto(void)
{
	return minergate_detect_one("/tmp/connection_pipe") ? 1 : 0;
}

static
void minergate_detect(void)
{
	generic_detect(&minergate_drv, minergate_detect_one, minergate_detect_auto, 0);
}

static
bool minergate_init(struct thr_info * const thr)
{
	struct cgpu_info * const dev = thr->cgpu;
	
	const int fd = minergate_open(dev->device_path);
	dev->device_fd = fd;
	if (fd < 0)
		applogr(false, LOG_ERR, "%s: Cannot connect", dev->dev_repr);
	
	struct minergate_state * const state = malloc(sizeof(*state) + MINERGATE_PKT_REQ_SZ);
	if (!state)
		applogr(false, LOG_ERR, "%s: %s failed", dev->dev_repr, "malloc");
	*state = (struct minergate_state){
		.req_buffer = (void*)&state[1]
	};
	thr->cgpu_data = state;
	thr->work = thr->work_list = NULL;
	
	mutex_init(&dev->device_mutex);
	memset(state->req_buffer, 0, MINERGATE_PKT_REQ_SZ);
	pk_u8(state->req_buffer, 2, MINERGATE_PROTOCOL_VER);
	state->req_buffer[3] = MRPF_FIRST | MRPF_FLUSH;
	pk_u16le(state->req_buffer, 4, MINERGATE_MAGIC);
	timer_set_delay_from_now(&thr->tv_poll, 0);
	
	return true;
}

static
bool minergate_queue_full(struct thr_info * const thr)
{
	static const unsigned max_minergate_jobs = 300, max_requests = 100;
	struct minergate_state * const state = thr->cgpu_data;
	bool qf;
	
	if (HASH_COUNT(thr->work) + state->ready_to_queue >= max_minergate_jobs)
		qf = true;
	else
	if (state->ready_to_queue >= max_requests)
		qf = true;
	else
	if (state->req_buffer[3] & MRPF_FLUSH)
		// Job flush occurs after new jobs get queued, so we have to wait until it completes
		qf = true;
	else
		qf = false;
	
	thr->queue_full = qf;
	return qf;
}

static
bool minergate_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info * const dev = thr->cgpu;
	struct minergate_state * const state = thr->cgpu_data;
	
	if (minergate_queue_full(thr))
		return false;
	
	work->device_id = (uint32_t)(state->next_jobid++);
	work->tv_stamp.tv_sec = 0;
	
	uint8_t * const my_buf = &state->req_buffer[MINERGATE_PKT_HEADER_SZ + (MINERGATE_PKT_REQ_ITEM_SZ * state->ready_to_queue++)];
	pk_u32be(my_buf,  0, work->device_id);
	memcpy(&my_buf[   4], &work->data[0x48], 4);  // nbits
	memcpy(&my_buf[   8], &work->data[0x44], 4);  // ntime
	memcpy(&my_buf[0x0c], &work->data[0x40], 4);  // merkle-tail
	memcpy(&my_buf[0x10], work->midstate, 0x20);
	
	if (work->work_difficulty >= MINERGATE_MAX_NONCE_DIFF)
		work->nonce_diff = MINERGATE_MAX_NONCE_DIFF;
	else
		work->nonce_diff = work->work_difficulty;
	const uint16_t zerobits = log2(floor(work->nonce_diff * 4294967296));
	work->nonce_diff = pow(2, zerobits) / 4294967296;
	pk_u8(my_buf, 0x30, zerobits);
	
	pk_u8(my_buf, 0x31,    0);  // ntime limit
	pk_u8(my_buf, 0x32,    0);  // ntime offset
	pk_u8(my_buf, 0x33,    0);  // reserved
	
	struct work *oldwork;
	HASH_FIND(hh, thr->work, &work->device_id, sizeof(work->device_id), oldwork);
	if (unlikely(oldwork))
	{
		applog(LOG_WARNING, "%s: Reusing allocated device id %"PRIwdi, dev->dev_repr, work->device_id);
		HASH_DEL(thr->work, oldwork);
		free_work(oldwork);
	}
	HASH_ADD(hh, thr->work, device_id, sizeof(work->device_id), work);
	LL_PREPEND(thr->work_list, work);
	timer_set_delay_from_now(&thr->tv_poll, 0);
	minergate_queue_full(thr);
	
	return true;
}

static
void minergate_queue_flush(struct thr_info * const thr)
{
	struct minergate_state * const state = thr->cgpu_data;
	struct work *work, *worktmp;
	
	// Flush internal ready-to-queue list
	LL_FOREACH_SAFE(thr->work_list, work, worktmp)
	{
		HASH_DEL(thr->work, work);
		LL_DELETE(thr->work_list, work);
		free_work(work);
	}
	state->ready_to_queue = 0;
	
	// Trigger minergate flush
	state->req_buffer[3] |= MRPF_FLUSH;
	
	timer_set_delay_from_now(&thr->tv_poll, 0);
}

static
void minergate_poll(struct thr_info * const thr)
{
	struct cgpu_info * const dev = thr->cgpu;
	struct minergate_state * const state = thr->cgpu_data;
	const int fd = dev->device_fd;
	
	if (opt_dev_protocol || state->ready_to_queue)
		applog(LOG_DEBUG, "%s: Polling with %u new jobs", dev->dev_repr, state->ready_to_queue);
	pk_u16le(state->req_buffer, 6, state->ready_to_queue);
	if (MINERGATE_PKT_REQ_SZ != write(fd, state->req_buffer, MINERGATE_PKT_REQ_SZ))
		return_via_applog(err, , LOG_ERR, "%s: write incomplete or failed", dev->dev_repr);
	
	uint8_t flags = state->req_buffer[3];
	state->req_buffer[3] = 0;
	state->ready_to_queue = 0;
	thr->work_list = NULL;
	
	uint8_t buf[MINERGATE_PKT_RSP_SZ];
	if (minergate_read(fd, buf, MINERGATE_PKT_RSP_SZ) != MINERGATE_PKT_RSP_SZ)
		return_via_applog(err, , LOG_ERR, "%s: %s failed", dev->dev_repr, "read");
	
	if (upk_u8(buf, 2) != MINERGATE_PROTOCOL_VER || upk_u16le(buf, 4) != MINERGATE_MAGIC)
		return_via_applog(err, , LOG_ERR, "%s: Protocol mismatch", dev->dev_repr);
	
	uint8_t *jobrsp = &buf[MINERGATE_PKT_HEADER_SZ];
	struct work *work;
	uint16_t rsp_count = upk_u16le(buf, 6);
	if (rsp_count || opt_dev_protocol)
		applog(LOG_DEBUG, "%s: Received %u job completions", dev->dev_repr, rsp_count);
	uint32_t nonce;
	int64_t hashes = 0;
	for (unsigned i = 0; i < rsp_count; ++i, (jobrsp += MINERGATE_PKT_RSP_ITEM_SZ))
	{
		work_device_id_t jobid = upk_u32be(jobrsp, 0);
		nonce = upk_u32le(jobrsp, 8);
		HASH_FIND(hh, thr->work, &jobid, sizeof(jobid), work);
		if (!work)
		{
			applog(LOG_ERR, "%s: Unknown job %"PRIwdi, dev->dev_repr, jobid);
			if (nonce)
			{
				inc_hw_errors3(thr, NULL, &nonce, 1.);
				nonce = upk_u32le(jobrsp, 0xc);
				if (nonce)
					inc_hw_errors3(thr, NULL, &nonce, 1.);
			}
			else
				inc_hw_errors_only(thr);
			continue;
		}
		if (nonce)
		{
			submit_nonce(thr, work, nonce);
			
			nonce = upk_u32be(jobrsp, 0xc);
			if (nonce)
				submit_nonce(thr, work, nonce);
		}
		
		HASH_DEL(thr->work, work);
		applog(LOG_DEBUG, "%s: %s job %"PRIwdi" completed", dev->dev_repr, work->tv_stamp.tv_sec ? "Flushed" : "Active", work->device_id);
		if (!work->tv_stamp.tv_sec)
			hashes += 100000000 * work->nonce_diff;
		free_work(work);
	}
	hashes_done2(thr, hashes, NULL);
	
	if (flags & MRPF_FLUSH)
	{
		// Mark all remaining jobs as flushed so we don't count them in hashes_done
		struct work *worktmp;
		HASH_ITER(hh, thr->work, work, worktmp)
		{
			work->tv_stamp.tv_sec = 1;
		}
	}
	
	minergate_queue_full(thr);
	timer_set_delay_from_now(&thr->tv_poll, MINERGATE_POLL_US);
	return;

err:
	// TODO: reconnect
	timer_set_delay_from_now(&thr->tv_poll, MINERGATE_RETRY_US);
}

static
bool minergate_get_stats(struct cgpu_info * const dev)
{
	static const int skip_stats = 1;
	struct thr_info * const thr = dev->thr[0];
	struct minergate_state * const state = thr->cgpu_data;
	
	FILE *F = fopen(minergate_stats_file, "r");
	char buf[0x100];
	if (F)
	{
		char *p = fgets(buf, sizeof(buf), F);
		fclose(F);
		if (p)
		{
			long nums[0x80];
			char *endptr;
			unsigned i;
			float max_temp = 0;
			for (i = 0; 1; ++i)
			{
				if (!p[0])
					break;
				nums[i] = strtol(p, &endptr, 0);
				if (p == endptr)
					break;
				if (i >= skip_stats && nums[i] > max_temp)
					max_temp = nums[i];
				while (endptr[0] && !isspace(endptr[0]))
					++endptr;
				while (endptr[0] && isspace(endptr[0]))
					++endptr;
				p = endptr;
			}
			i -= skip_stats;
			long *new_stats = malloc(sizeof(*state->stats) * i);
			memcpy(new_stats, &nums[skip_stats], sizeof(*nums) * i);
			mutex_lock(&dev->device_mutex);
			free(state->stats);
			state->stats = new_stats;
			state->stats_count = i;
			mutex_unlock(&dev->device_mutex);
			dev->temp = max_temp;
		}
	}
	
	return true;
}

static
struct api_data *minergate_api_extra_device_status(struct cgpu_info * const dev)
{
	struct thr_info * const thr = dev->thr[0];
	struct minergate_state * const state = thr->cgpu_data;
	struct api_data *root = NULL;
	
	mutex_lock(&dev->device_mutex);
	if (state->stats_count > 1)
	{
		char buf[0x10];
		for (unsigned i = 0; i < state->stats_count; ++i)
		{
			float temp = state->stats[i];
			if (!temp)
				continue;
			sprintf(buf, "Temperature%u", i);
			root = api_add_temp(root, buf, &temp, true);
		}
	}
	mutex_unlock(&dev->device_mutex);
	
	return root;
}

struct device_drv minergate_drv = {
	.dname = "minergate",
	.name = "MGT",
	.drv_detect = minergate_detect,
	
	.thread_init = minergate_init,
	.minerloop = minerloop_queue,
	
	.queue_append = minergate_queue_append,
	.queue_flush = minergate_queue_flush,
	.poll = minergate_poll,
	
	.get_stats = minergate_get_stats,
	.get_api_extra_device_status = minergate_api_extra_device_status,
};
