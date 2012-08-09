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

#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#ifndef WIN32
#include <errno.h>
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

#ifdef HAVE_LIBUDEV
int
serial_autodetect_udev(detectone_func_t detectone, const char*prodname)
{
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
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	return found;
}
#else
int
serial_autodetect_udev(__maybe_unused detectone_func_t detectone, __maybe_unused const char*prodname)
{
	return 0;
}
#endif

int
serial_autodetect_devserial(detectone_func_t detectone, const char*prodname)
{
#ifndef WIN32
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
		if (detectone(devpath))
			++found;
	}
	closedir(D);

	return found;
#else
	return 0;
#endif
}

int
_serial_detect(const char*dname, detectone_func_t detectone, autoscan_func_t autoscan, bool forceauto)
{
	struct string_elist *iter, *tmp;
	const char*s, *p;
	bool inhibitauto = false;
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
		}
	}

	if ((forceauto || !inhibitauto) && autoscan)
		found += autoscan();

	return found;
}

/* NOTE: Linux only supports uint8_t (decisecond) timeouts; limiting it in
 *       this interface buys us warnings when bad constants are passed in.
 */
int
serial_open(const char*devpath, unsigned long baud, uint8_t timeout, bool purge)
{
#ifdef WIN32
	HANDLE hSerial = CreateFile(devpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (unlikely(hSerial == INVALID_HANDLE_VALUE))
	{
		DWORD e = GetLastError();
		switch (e) {
		case ERROR_ACCESS_DENIED:
			applog(LOG_ERR, "Do not have user privileges required to open %s", devpath);
			break;
		case ERROR_SHARING_VIOLATION:
			applog(LOG_ERR, "%s is already in use by another process", devpath);
			break;
		default:
			applog(LOG_DEBUG, "Open %s failed, GetLastError:%u", devpath, e);
			break;
		}
		return -1;
	}

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

	// Code must specify a valid timeout value (0 means don't timeout)
	const DWORD ctoms = ((DWORD)timeout * 100);
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
	{
		if (errno == EACCES)
			applog(LOG_ERR, "Do not have user privileges required to open %s", devpath);
		else
			applog(LOG_DEBUG, "Open %s failed, errno:%d", devpath, errno);

		return -1;
	}

	struct termios my_termios;

	tcgetattr(fdDev, &my_termios);

	switch (baud) {
	case 0:
		break;
	case 57600:
		cfsetispeed( &my_termios, B57600 );
		cfsetospeed( &my_termios, B57600 );
		break;
	case 115200:
		cfsetispeed( &my_termios, B115200 );
		cfsetospeed( &my_termios, B115200 );
		break;
	// TODO: try some higher speeds with the Icarus and BFL to see
	// if they support them and if setting them makes any difference
	default:
		applog(LOG_WARNING, "Unrecognized baud rate: %lu", baud);
	}

	my_termios.c_cflag |= CS8;
	my_termios.c_cflag |= CREAD;
	my_termios.c_cflag |= CLOCAL;
	my_termios.c_cflag &= ~(CSIZE | PARENB);

	my_termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK |
				ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	my_termios.c_oflag &= ~OPOST;
	my_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	// Code must specify a valid timeout value (0 means don't timeout)
	my_termios.c_cc[VTIME] = (cc_t)timeout;
	my_termios.c_cc[VMIN] = 0;

	tcsetattr(fdDev, TCSANOW, &my_termios);
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
