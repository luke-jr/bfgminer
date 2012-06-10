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

#define BITFORCE_SLEEP_US 4500000
#define BITFORCE_SLEEP_MS (BITFORCE_SLEEP_US/1000)
#define BITFORCE_TIMEOUT_MS 30000

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

static ssize_t BFwrite2(int fd, const void *buf, ssize_t bufLen)
{
	return write(fd, buf, bufLen);
}

#define BFwrite(fd, buf, bufLen)  do {  \
	if ((bufLen) != BFwrite2(fd, buf, bufLen)) {  \
		applog(LOG_ERR, "Error writing to BitForce (" #buf ")");  \
		return 0;  \
	}  \
} while(0)

#define BFclose(fd) close(fd)

static bool bitforce_detect_one(const char *devpath)
{
	char *s;
	char pdevbuf[0x100];

	if (total_devices == MAX_DEVICES)
		return false;

	int fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BitForce Detect: Failed to open %s", devpath);
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
		applog(LOG_ERR, "BitForce Detect: Didn't recognise BitForce on %s", devpath);
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
	const char*s;
	bool found = false;
	bool autoscan = false;

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		s = iter->string;
		if (!strncmp("bitforce:", iter->string, 9))
			s += 9;
		if (!strcmp(s, "auto"))
			autoscan = true;
		else
		if (!strcmp(s, "noauto"))
			found = true;
		else if (bitforce_detect_one(s)) {
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

static bool bitforce_init(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char *devpath = bitforce->device_path;
	char pdevbuf[0x100];
	char *s;

	BFclose(fdDev);

	fdDev = BFopen(devpath);
	if (unlikely(fdDev == -1)) {
		applog(LOG_ERR, "BitForce init: Failed to open %s", devpath);
		return false;
	}

	bitforce->device_fd = fdDev;

	BFwrite(fdDev, "ZGX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZGX)");
		return false;
	}

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_ERR, "BitForce init: Didn't recognise BitForce on %s", devpath);
		return false;
	}
	
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		bitforce->name = strdup(pdevbuf + 7);
	}
	
	return true;
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	char *s;

	BFwrite(fdDev, "ZLX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading temp from BitForce (ZLX)");
		return false;
	}
	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = strtof(s + 1, NULL);
		if (temp > 0) {
			bitforce->temp = temp;
			if (temp > bitforce->cutofftemp) {
				applog(LOG_WARNING, "Hit thermal cutoff limit on %s %d, setting idle", bitforce->api->name, bitforce->device_id);
				bitforce->deven = DEV_IDLE;

				bitforce->device_last_not_well = time(NULL);
				bitforce->device_not_well_reason = REASON_DEV_THERMAL_CUTOFF;
				bitforce->dev_thermal_cutoff_count++;
			}
		}
	}
	return true;
}

static bool bitforce_send_work(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;
	char pdevbuf[0x100];
	unsigned char ob[61] = ">>>>>>>>12345678901234567890123456789012123456789012>>>>>>>>";
	char *s;

	BFwrite(fdDev, "ZDX", 3);
	BFgets(pdevbuf, sizeof(pdevbuf), fdDev);
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_ERR, "Error reading from BitForce (ZDX)");
		return false;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce ZDX reports: %s", pdevbuf);
		return false;
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
		return false;
	}
	if (unlikely(pdevbuf[0] != 'O' || pdevbuf[1] != 'K')) {
		applog(LOG_ERR, "BitForce block data reports: %s", pdevbuf);
		return false;
	}
	return true;
}

static uint64_t bitforce_get_result(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;

	char pdevbuf[0x100];
	char *pnoncebuf;
	uint32_t nonce;
	int i;

	i = BITFORCE_SLEEP_MS;
	while (i < BITFORCE_TIMEOUT_MS) {
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

	if (i >= BITFORCE_TIMEOUT_MS) {
		applog(LOG_ERR, "BitForce took longer than 30s");
		bitforce->device_last_not_well = time(NULL);
		bitforce->device_not_well_reason = REASON_THREAD_ZERO_HASH;
		bitforce->thread_zero_hash_count++;
		return 1;
	}

	applog(LOG_DEBUG, "BitForce waited %dms until %s\n", i, pdevbuf);
	work->blk.nonce = 0xffffffff;
	if (pdevbuf[2] == '-') 
		return 0xffffffff;   /* No valid nonce found */
	else if (pdevbuf[0] == 'I')
		return 1;          /* Device idle */
	else if (strncasecmp(pdevbuf, "NONCE-FOUND", 11)) {
		applog(LOG_WARNING, "BitForce result reports: %s", pdevbuf);
		return 1;
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

static uint64_t bitforce_scanhash(struct thr_info *thr, struct work *work, uint64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	bool dev_enabled = (bitforce->deven == DEV_ENABLED);
	static enum dev_enable last_dev_state = DEV_ENABLED;
	
	// if device has just gone from disabled to enabled, re-initialise it
	if (last_dev_state == DEV_DISABLED && dev_enabled)
		bitforce_init(bitforce);

	if (dev_enabled)
		if (!bitforce_send_work(thr, work))
			return 0;

	if (!bitforce_get_temp(bitforce))
		return 0;

	usleep(BITFORCE_SLEEP_US);

	if (dev_enabled)
		return bitforce_get_result(thr, work);
	else
		return 1;
}

static void bitforce_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	int fdDev = bitforce->device_fd;

	BFclose(fdDev);
}

struct device_api bitforce_api = {
	.dname = "bitforce",
	.name = "BFL",
	.api_detect = bitforce_detect,
	.get_statline_before = get_bitforce_statline_before,
	.thread_prepare = bitforce_thread_prepare,
	.scanhash = bitforce_scanhash,
	.thread_shutdown = bitforce_shutdown
};
