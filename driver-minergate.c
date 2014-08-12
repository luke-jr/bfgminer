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

#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "deviceapi.h"
#include "miner.h"

static const int minergate_max_responses = 300;

BFG_REGISTER_DRIVER(minergate_drv)

enum minergate_reqpkt_flags {
	MRPF_FIRST = 1,
	MRPF_FLUSH = 2,
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
bool minergate_detect_one(const char * const devpath)
{
	bool rv = false;
	const int fd = minergate_open(devpath);
	if (unlikely(fd < 0))
		applogr(false, LOG_DEBUG, "%s: %s: Cannot connect", minergate_drv.dname, devpath);
	
	int epfd = -1;
	static const int minergate_version = 6;
#define req_sz (8 + (24 * 100))
	uint8_t buf[req_sz] = {0xbf, 0x90, minergate_version, MRPF_FIRST, 0,0, 0 /* req count */,};
	pk_u16le(buf, 4, 0xcaf4);
	if (req_sz != write(fd, buf, req_sz))
		return_via_applog(out, , LOG_DEBUG, "%s: %s: write incomplete or failed", minergate_drv.dname, devpath);
	
	epfd = epoll_create(1);
	if (epfd < 0)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s failed", minergate_drv.dname, devpath, "epoll_create");
	
	struct epoll_event eev;
	eev.events = EPOLLIN;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &eev))
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s failed", minergate_drv.dname, devpath, "epoll_ctl");
	
	size_t read_bytes = 0;
	static const size_t read_expect = 8;
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
	if (buf[2] != minergate_version)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s mismatch", minergate_drv.dname, devpath, "minergate_version");
	if (upk_u16le(buf, 4) != 0xcaf4)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: %s mismatch", minergate_drv.dname, devpath, "magic");
	
	uint16_t responses = upk_u16le(buf, 6);
	if (responses > minergate_max_responses)
		return_via_applog(out, , LOG_DEBUG, "%s: %s: More than maximum responses", minergate_drv.dname, devpath);
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &minergate_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
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

struct device_drv minergate_drv = {
	.dname = "minergate",
	.name = "MGT",
	.drv_detect = minergate_detect,
};
