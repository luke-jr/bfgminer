/*
 * Copyright 2012 Luke Dashjr
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#ifndef WIN32
#include <termios.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include <windows.h>
#include <io.h>
#endif

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

#include "elist.h"
#include "fpgautils.h"
#include "logging.h"
#include "miner.h"

char
serial_autodetect_udev(detectone_func_t detectone, const char*prodname)
{
#ifdef HAVE_LIBUDEV
	if (total_devices == MAX_DEVICES)
		return 0;

	struct udev *udev = udev_new();
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	struct udev_list_entry *list_entry;
	char found = 0;

	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_property(enumerate, "ID_MODEL", prodname);
	udev_enumerate_scan_devices(enumerate);
	udev_list_entry_foreach(list_entry, udev_enumerate_get_list_entry(enumerate)) {
		struct udev_device *device = udev_device_new_from_syspath(
			udev_enumerate_get_udev(enumerate),
			udev_list_entry_get_name(list_entry)
		);
		if (!device)
			continue;

		const char *devpath = udev_device_get_devnode(device);
		if (devpath && detectone(devpath))
			++found;

		udev_device_unref(device);

		if (total_devices == MAX_DEVICES)
			break;
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	return found;
#else
	return 0;
#endif
}

char
serial_autodetect_devserial(detectone_func_t detectone, const char*prodname)
{
#ifndef WIN32
	if (total_devices == MAX_DEVICES)
		return 0;

	DIR *D;
	struct dirent *de;
	const char udevdir[] = "/dev/serial/by-id";
	char devpath[sizeof(udevdir) + 1 + NAME_MAX];
	char *devfile = devpath + sizeof(udevdir);
	char found = 0;

	D = opendir(udevdir);
	if (!D)
		return 0;
	memcpy(devpath, udevdir, sizeof(udevdir) - 1);
	devpath[sizeof(udevdir) - 1] = '/';
	while ( (de = readdir(D)) ) {
		if (!strstr(de->d_name, prodname))
			continue;
		strcpy(devfile, de->d_name);
		if (detectone(devpath)) {
			++found;
			if (total_devices == MAX_DEVICES)
				break;
		}
	}
	closedir(D);

	return found;
#else
	return 0;
#endif
}

char
_serial_detect(const char*dname, detectone_func_t detectone, autoscan_func_t autoscan, bool force_autoscan)
{
	if (total_devices == MAX_DEVICES)
		return 0;

	struct string_elist *iter, *tmp;
	const char*s, *p;
	bool inhibitauto = false;
	bool forceauto = false;
	char found = 0;
	size_t dnamel = strlen(dname);

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		s = iter->string;
		if ((p = strchr(s, ':')) && p[1] != '\0') {
			size_t plen = p - s;
			if (plen != dnamel || strncasecmp(s, dname, plen))
				continue;
			s = p + 1;
		}
		if (!strcmp(s, "auto"))
			forceauto = true;
		else
		if (!strcmp(s, "noauto"))
			inhibitauto = true;
		else
		if (detectone(s)) {
			string_elist_del(iter);
			inhibitauto = true;
			++found;
			if (total_devices == MAX_DEVICES)
				break;
		}
	}

	if ((forceauto || !inhibitauto) && autoscan && total_devices < MAX_DEVICES)
		found += autoscan();

	return found;
}

int
serial_open(const char*devpath, unsigned long baud, signed short timeout, bool purge)
{
#ifdef WIN32
	HANDLE hSerial = CreateFile(devpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (unlikely(hSerial == INVALID_HANDLE_VALUE))
		return -1;

	// thanks to af_newbie for pointers about this
	COMMCONFIG comCfg = {0};
	comCfg.dwSize = sizeof(COMMCONFIG);
	comCfg.wVersion = 1;
	comCfg.dcb.DCBlength = sizeof(DCB);
	comCfg.dcb.BaudRate = baud;
	comCfg.dcb.fBinary = 1;
	comCfg.dcb.fDtrControl = DTR_CONTROL_ENABLE;
	comCfg.dcb.fRtsControl = RTS_CONTROL_ENABLE;
	comCfg.dcb.ByteSize = 8;

	SetCommConfig(hSerial, &comCfg, sizeof(comCfg));

	const DWORD ctoms = (timeout == -1) ? 30000 : (timeout * 100);
	COMMTIMEOUTS cto = {ctoms, 0, ctoms, 0, ctoms};
	SetCommTimeouts(hSerial, &cto);

	if (purge) {
		PurgeComm(hSerial, PURGE_RXABORT);
		PurgeComm(hSerial, PURGE_TXABORT);
		PurgeComm(hSerial, PURGE_RXCLEAR);
		PurgeComm(hSerial, PURGE_TXCLEAR);
	}

	return _open_osfhandle((LONG)hSerial, 0);
#else
	int fdDev = open(devpath, O_RDWR | O_CLOEXEC | O_NOCTTY);

	if (unlikely(fdDev == -1))
		return -1;

	struct termios pattr;
	tcgetattr(fdDev, &pattr);
	pattr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	pattr.c_oflag &= ~OPOST;
	pattr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	pattr.c_cflag &= ~(CSIZE | PARENB);
	pattr.c_cflag |= CS8;

	switch (baud) {
	case 0: break;
	case 115200: pattr.c_cflag = B115200; break;
	default:
		applog(LOG_WARNING, "Unrecognized baud rate: %lu", baud);
	}
	pattr.c_cflag |= CREAD | CLOCAL;

	if (timeout >= 0) {
		pattr.c_cc[VTIME] = (cc_t)timeout;
		pattr.c_cc[VMIN] = 0;
	}

	tcsetattr(fdDev, TCSANOW, &pattr);
	if (purge)
		tcflush(fdDev, TCIOFLUSH);
	return fdDev;
#endif
}

ssize_t
_serial_read(int fd, char *buf, size_t bufsiz, char *eol)
{
	ssize_t len, tlen = 0;
	while (bufsiz) {
		len = read(fd, buf, eol ? 1 : bufsiz);
		if (len < 1)
			break;
		tlen += len;
		if (eol && *eol == buf[0])
			break;
		buf += len;
		bufsiz -= len;
	}
	return tlen;
}

static FILE*
_open_bitstream(const char*path, const char*subdir, const char*filename)
{
	char fullpath[PATH_MAX];
	strcpy(fullpath, path);
	strcat(fullpath, "/");
	if (subdir) {
		strcat(fullpath, subdir);
		strcat(fullpath, "/");
	}
	strcat(fullpath, filename);
	return fopen(fullpath, "rb");
}
#define _open_bitstream(path, subdir)  do {  \
	f = _open_bitstream(path, subdir, filename);  \
	if (f)  \
		return f;  \
} while(0)

#define _open_bitstream3(path)  do {  \
	_open_bitstream(path, dname);  \
	_open_bitstream(path, "bitstreams");  \
	_open_bitstream(path, NULL);  \
} while(0)

FILE*
open_bitstream(const char*dname, const char*filename)
{
	FILE *f;

	_open_bitstream3(opt_kernel_path);
	_open_bitstream3(cgminer_path);
	_open_bitstream3(".");

	return NULL;
}
