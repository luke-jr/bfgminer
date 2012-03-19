/*
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#ifndef WIN32
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include <windows.h>
#include <io.h>
#endif
#include <unistd.h>

#include "config.h"

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

#include "elist.h"
#include "miner.h"


struct device_api bitforce_api;

static int BFopen(const char *devpath)
{
#ifdef WIN32
	HANDLE hSerial = CreateFile(devpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (unlikely(hSerial == INVALID_HANDLE_VALUE))
		return -1;
	
	COMMTIMEOUTS cto = {30000, 0, 30000, 0, 30000};
	SetCommTimeouts(hSerial, &cto);
	
	return _open_osfhandle((LONG)hSerial, 0);
#else
	int fdDev = open(devpath, O_RDWR | O_CLOEXEC | O_NOCTTY);
	if (likely(fdDev != -1)) {
		struct termios pattr;
		
		tcgetattr(fdDev, &pattr);
		pattr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
		pattr.c_oflag &= ~OPOST;
		pattr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
		pattr.c_cflag &= ~(CSIZE | PARENB);
		pattr.c_cflag |= CS8;
		tcsetattr(fdDev, TCSANOW, &pattr);
	}
	tcflush(fdDev, TCOFLUSH);
	tcflush(fdDev, TCIFLUSH);
	return fdDev;
#endif
}

static void BFgets(char *buf, size_t bufLen, int fd)
{
	do
		--bufLen;
	while (likely(bufLen && read(fd, buf, 1) && (buf++)[0] != '\n'))
		;
	buf[0] = '\0';
}

static void BFwrite(int fd, const void *buf, ssize_t bufLen)
{
	ssize_t ret = write(fd, buf, bufLen);

	if (unlikely(ret != bufLen))
		quit(1, "BFwrite failed");
}

#define BFclose(fd) close(fd)

static bool bitforce_detect_one(const char *devpath)
{
	char *s;
	char pdevbuf[0x100];

	if (total_devices == MAX_DEVICES)
		return false;

	int fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_DEBUG, "BitForce Detect: Failed to open %s", devpath);
		return false;
	}
	BFwrite(fdDev, "ZGX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZGX)");
		return 0;
	}
	BFclose(fdDev);
	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_DEBUG, "BitForce Detect: Didn't recognise BitForce on %s", devpath);
		return false;
	}

	// We have a real BitForce!
	struct cgpu_info *bitforce;
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->api = &bitforce_api;
	bitforce->device_path = strdup(devpath);
	bitforce->deven = DEV_ENABLED;
	bitforce->threads = 1;
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}

	return add_cgpu(bitforce);
}

static bool bitforce_detect_auto_udev()
{
#ifdef HAVE_LIBUDEV
	struct udev *udev = udev_new();
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	struct udev_list_entry *list_entry;
	bool foundany = false;
	
	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_property(enumerate, "ID_MODEL", "BitFORCE*SHA256");
	udev_enumerate_scan_devices(enumerate);
	udev_list_entry_foreach(list_entry, udev_enumerate_get_list_entry(enumerate)) {
		struct udev_device *device = udev_device_new_from_syspath(
			udev_enumerate_get_udev(enumerate),
			udev_list_entry_get_name(list_entry)
		);
		if (!device)
			continue;
		
		const char *devpath = udev_device_get_devnode(device);
		if (devpath) {
			foundany = true;
			bitforce_detect_one(devpath);
		}
		
		udev_device_unref(device);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	
	return foundany;
#else
	return false;
#endif
}

static bool bitforce_detect_auto_devserial()
{
#ifndef WIN32
	DIR *D;
	struct dirent *de;
	const char udevdir[] = "/dev/serial/by-id";
	char devpath[sizeof(udevdir) + 1 + NAME_MAX];
	char *devfile = devpath + sizeof(udevdir);
	bool foundany = false;
	
	D = opendir(udevdir);
	if (!D)
		return false;
	memcpy(devpath, udevdir, sizeof(udevdir) - 1);
	devpath[sizeof(udevdir) - 1] = '/';
	while ( (de = readdir(D)) ) {
		if (!strstr(de->d_name, "BitFORCE_SHA256"))
			continue;
		foundany = true;
		strcpy(devfile, de->d_name);
		bitforce_detect_one(devpath);
	}
	closedir(D);
	
	return foundany;
#else
	return false;
#endif
}

static void bitforce_detect_auto()
{
	bitforce_detect_auto_udev() ?:
	bitforce_detect_auto_devserial() ?:
	0;
}

static void bitforce_detect()
{
	struct string_elist *iter, *tmp;
	bool found = false;
	bool autoscan = false;

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		if (!strcmp(iter->string, "auto"))
			autoscan = true;
		else if (bitforce_detect_one(iter->string)) {
			string_elist_del(iter);
			found = true;
		}
	}

	if (autoscan || !found)
		bitforce_detect_auto();
}

static void get_bitforce_statline_before(char *buf, struct cgpu_info *bitforce)
{
	float gt = bitforce->temp;
	if (gt > 0)
		tailsprintf(buf, "%5.1fC ", gt);
	else
		tailsprintf(buf, "       ", gt);
	tailsprintf(buf, "        | ");
}

static bool bitforce_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;

	struct timeval now;

	int fdDev = BFopen(bitforce->device_path);
	if (unlikely(-1 == fdDev)) {
		applog(LOG_ERR, "Failed to open BitForce on %s", bitforce->device_path);
		return false;
	}

	bitforce->device_fd = fdDev;

	applog(LOG_INFO, "Opened BitForce on %s", bitforce->device_path);
	gettimeofday(&now, NULL);
	get_datestamp(bitforce->init, &now);

	return true;
}

static uint64_t bitforce_scanhash(struct thr_info *thr, struct work *work, uint64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;

	char pdevbuf[0x100];
	unsigned char ob[61] = ">>>>>>>>12345678901234567890123456789012123456789012>>>>>>>>";
	int i;
	char *pnoncebuf;
	char *s;
	uint32_t nonce;

	BFwrite(fdDev, "ZDX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZDX)");
		return 0;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce ZDX reports: %s", pdevbuf);
		return 0;
	}

	memcpy(ob + 8, work->midstate, 32);
	memcpy(ob + 8 + 32, work->data + 64, 12);
	BFwrite(fdDev, ob, 60);
	if (opt_debug) {
		s = bin2hex(ob + 8, 44);
		applog(LOG_DEBUG, "BitForce block data: %s", s);
		free(s);
	}

	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (block data)");
		return 0;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce block data reports: %s", pdevbuf);
		return 0;
	}

	BFwrite(fdDev, "ZLX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZKX)");
		return 0;
	}
	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = strtof(s + 1, NULL);
		if (temp > 0) {
			bitforce->temp = temp;
			if (temp > bitforce->cutofftemp) {
				applog(LOG_WARNING, "Hit thermal cutoff limit on %s %d, disabling!", bitforce->api->name, bitforce->device_id);
				bitforce->deven = DEV_RECOVER;
			}
		}
	}

	usleep(4500000);
	i = 4500;
	while (1) {
		BFwrite(fdDev, "ZFX", 3);
		BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
		if (unlikely(!pdevbuf[0])) {
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
	else if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
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
	.dname = "bitforce",
	.name = "PGA",
	.api_detect = bitforce_detect,
	.get_statline_before = get_bitforce_statline_before,
	.thread_prepare = bitforce_thread_prepare,
	.scanhash = bitforce_scanhash,
};
