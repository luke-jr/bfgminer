/*
 * Copyright 2012 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <termios.h>
#include <unistd.h>

#include "elist.h"
#include "miner.h"


struct device_api bitforce_api;

static bool bitforce_detect_one(const char *devpath)
{
	char pdevbuf[0x100];
	int i = 0;

	if (total_devices == MAX_DEVICES)
		return false;

	FILE *fileDev = fopen(devpath, "r+b");
	if (unlikely(!fileDev))
	{
		applog(LOG_DEBUG, "BitForce Detect: Failed to open %s", devpath);
		return false;
	}
	setbuf(fileDev, NULL);
	fprintf(fileDev, "ZGX");
	if (!fgets(pdevbuf, sizeof(pdevbuf), fileDev))
	{
		applog(LOG_ERR, "Error reading from BitForce (ZGX)");
		return 0;
	}
	fclose(fileDev);
	if (unlikely(!strstr(pdevbuf, "SHA256")))
	{
		applog(LOG_DEBUG, "BitForce Detect: Didn't recognize BitForce on %s", devpath);
		return false;
	}

	// We have a real BitForce!
	struct cgpu_info *bitforce;
	bitforce = calloc(1, sizeof(*bitforce));
	devices[total_devices++] = bitforce;
	bitforce->api = &bitforce_api;
	bitforce->device_id = i++;
	bitforce->device_path = strdup(devpath);
	bitforce->enabled = true;
	bitforce->threads = 1;

	return true;
}

static void bitforce_detect_auto()
{
	DIR *D;
	struct dirent *de;
	const char udevdir[] = "/dev/serial/by-id";
	char devpath[sizeof(udevdir) + 1 + NAME_MAX];
	char *devfile = devpath + sizeof(udevdir);

	D = opendir(udevdir);
	if (!D)
		return;
	memcpy(devpath, udevdir, sizeof(udevdir) - 1);
	devpath[sizeof(udevdir) - 1] = '/';
	while ( (de = readdir(D)) ) {
		if (!strstr(de->d_name, "BitFORCE_SHA256"))
			continue;
		strcpy(devfile, de->d_name);
		bitforce_detect_one(devpath);
	}
	closedir(D);
}

static void bitforce_detect()
{
	struct string_elist *iter, *tmp;

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		if (bitforce_detect_one(iter->string))
			string_elist_del(iter);
	}

	bitforce_detect_auto();
}

static bool bitforce_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;

	struct timeval now;

	FILE *fileDev = fopen(bitforce->device_path, "r+b");
	if (unlikely(!fileDev))
	{
		applog(LOG_ERR, "Failed to open BitForce on %s", bitforce->device_path);
		return false;
	}

	{
		int nDevFD = fileno(fileDev);
		struct termios pattr;
		tcgetattr(nDevFD, &pattr);
		pattr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
		pattr.c_oflag &= ~OPOST;
		pattr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
		pattr.c_cflag &= ~(CSIZE | PARENB);
		pattr.c_cflag |= CS8;
		tcsetattr(nDevFD, TCSANOW, &pattr);
	}
	setbuf(fileDev, NULL);
	bitforce->device_file = fileDev;

	applog(LOG_INFO, "Opened BitForce on %s", bitforce->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(bitforce->init, &now);

	return true;
}

static uint64_t bitforce_scanhash(struct thr_info *thr, struct work *work, uint64_t max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	FILE *fileDev = bitforce->device_file;

	char pdevbuf[0x100];
	unsigned char ob[61] = ">>>>>>>>12345678901234567890123456789012123456789012>>>>>>>>";
	int i;
	char *pnoncebuf;
	uint32_t nonce;

	fprintf(fileDev, "ZDX");
	if (!fgets(pdevbuf, sizeof(pdevbuf), fileDev)) {
		applog(LOG_ERR, "Error reading from BitForce (ZDX)");
		return 0;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K'))
	{
		applog(LOG_ERR, "BitForce ZDX reports: %s", pdevbuf);
		return 0;
	}

	memcpy(ob + 8, work->midstate, 32);
	memcpy(ob + 8 + 32, work->data + 64, 12);
	fwrite(ob, 60, 1, fileDev);
	applog(LOG_DEBUG, "BitForce block data: %s", bin2hex(ob + 8, 44));

	if (!fgets(pdevbuf, sizeof(pdevbuf), fileDev))
	{
		applog(LOG_ERR, "Error reading from BitForce (block data)");
		return 0;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K'))
	{
		applog(LOG_ERR, "BitForce block data reports: %s", pdevbuf);
		return 0;
	}

	usleep(4500000);
	i = 4500;
	while (1) {
		fprintf(fileDev, "ZFX");
		if (!fgets(pdevbuf, sizeof(pdevbuf), fileDev))
		{
			applog(LOG_ERR, "Error reading from BitForce (ZFX)");
			return 0;
		}
		if (pdevbuf[0] != 'B')
		    break;
		usleep(10000);
		i += 10;
	}
	applog(LOG_DEBUG, "BitForce waited %dms until %s\n", i, pdevbuf);
	work->blk.nonce = 0xffffffff;
	if (pdevbuf[2] == '-')
		return 0xffffffff;
	else
	if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
		applog(LOG_ERR, "BitForce result reports: %s", pdevbuf);
		return 0;
	}

	pnoncebuf = &pdevbuf[12];

	while (1) {
		hex2bin((void*)&nonce, pnoncebuf, 4);
#ifndef __BIG_ENDIAN__
		nonce = swab32(nonce);
#endif

		submit_nonce(thr, work, nonce);
		if (pnoncebuf[8] != ',')
			break;
		pnoncebuf += 9;
	}

	return 0xffffffff;
}

struct device_api bitforce_api = {
	.name = "BFL",
	.api_detect = bitforce_detect,
	// .reinit_device = TODO
	.thread_prepare = bitforce_thread_prepare,
	.scanhash = bitforce_scanhash,
};
