/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
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

#include "miner.h"

#ifndef WIN32
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
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
#include <sys/ioctl.h>
#endif

#include "elist.h"
#include "logging.h"
#include "miner.h"
#include "fpgautils.h"

#ifdef HAVE_LIBUDEV
int serial_autodetect_udev(detectone_func_t detectone, const char*prodname)
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
int serial_autodetect_udev(__maybe_unused detectone_func_t detectone, __maybe_unused const char*prodname)
{
	return 0;
}
#endif

int serial_autodetect_devserial(__maybe_unused detectone_func_t detectone, __maybe_unused const char*prodname)
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

int _serial_detect(struct device_drv *drv, detectone_func_t detectone, autoscan_func_t autoscan, bool forceauto)
{
	struct string_elist *iter, *tmp;
	const char *dev, *colon;
	bool inhibitauto = false;
	char found = 0;
	size_t namel = strlen(drv->name);
	size_t dnamel = strlen(drv->dname);

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		dev = iter->string;
		if ((colon = strchr(dev, ':')) && colon[1] != '\0') {
			size_t idlen = colon - dev;

			// allow either name:device or dname:device
			if ((idlen != namel || strncasecmp(dev, drv->name, idlen))
			&&  (idlen != dnamel || strncasecmp(dev, drv->dname, idlen)))
				continue;

			dev = colon + 1;
		}
		if (!strcmp(dev, "auto"))
			forceauto = true;
		else if (!strcmp(dev, "noauto"))
			inhibitauto = true;
		else if (detectone(dev)) {
			string_elist_del(iter);
			inhibitauto = true;
			++found;
		}
	}

	if ((forceauto || !inhibitauto) && autoscan)
		found += autoscan();

	return found;
}

// This code is purely for debugging but is very useful for that
// It also took quite a bit of effort so I left it in
// #define TERMIOS_DEBUG 1
// Here to include it at compile time
// It's off by default
#ifndef WIN32
#ifdef TERMIOS_DEBUG

#define BITSSET "Y"
#define BITSNOTSET "N"

int tiospeed(speed_t speed)
{
	switch (speed) {
	case B0:
		return 0;
	case B50:
		return 50;
	case B75:
		return 75;
	case B110:
		return 110;
	case B134:
		return 134;
	case B150:
		return 150;
	case B200:
		return 200;
	case B300:
		return 300;
	case B600:
		return 600;
	case B1200:
		return 1200;
	case B1800:
		return 1800;
	case B2400:
		return 2400;
	case B4800:
		return 4800;
	case B9600:
		return 9600;
	case B19200:
		return 19200;
	case B38400:
		return 38400;
	case B57600:
		return 57600;
	case B115200:
		return 115200;
	case B230400:
		return 230400;
	case B460800:
		return 460800;
	case B500000:
		return 500000;
	case B576000:
		return 576000;
	case B921600:
		return 921600;
	case B1000000:
		return 1000000;
	case B1152000:
		return 1152000;
	case B1500000:
		return 1500000;
	case B2000000:
		return 2000000;
	case B2500000:
		return 2500000;
	case B3000000:
		return 3000000;
	case B3500000:
		return 3500000;
	case B4000000:
		return 4000000;
	default:
		return -1;
	}
}

void termios_debug(const char *devpath, struct termios *my_termios, const char *msg)
{
	applog(LOG_DEBUG, "TIOS: Open %s attributes %s: ispeed=%d ospeed=%d",
			devpath, msg, tiospeed(cfgetispeed(my_termios)), tiospeed(cfgetispeed(my_termios)));

#define ISSETI(b) ((my_termios->c_iflag | (b)) ? BITSSET : BITSNOTSET)

	applog(LOG_DEBUG, "TIOS:  c_iflag: IGNBRK=%s BRKINT=%s IGNPAR=%s PARMRK=%s INPCK=%s ISTRIP=%s INLCR=%s IGNCR=%s ICRNL=%s IUCLC=%s IXON=%s IXANY=%s IOFF=%s IMAXBEL=%s IUTF8=%s",
			ISSETI(IGNBRK), ISSETI(BRKINT), ISSETI(IGNPAR), ISSETI(PARMRK),
			ISSETI(INPCK), ISSETI(ISTRIP), ISSETI(INLCR), ISSETI(IGNCR),
			ISSETI(ICRNL), ISSETI(IUCLC), ISSETI(IXON), ISSETI(IXANY),
			ISSETI(IXOFF), ISSETI(IMAXBEL), ISSETI(IUTF8));

#define ISSETO(b) ((my_termios->c_oflag | (b)) ? BITSSET : BITSNOTSET)
#define VALO(b) (my_termios->c_oflag | (b))

	applog(LOG_DEBUG, "TIOS:  c_oflag: OPOST=%s OLCUC=%s ONLCR=%s OCRNL=%s ONOCR=%s ONLRET=%s OFILL=%s OFDEL=%s NLDLY=%d CRDLY=%d TABDLY=%d BSDLY=%d VTDLY=%d FFDLY=%d",
			ISSETO(OPOST), ISSETO(OLCUC), ISSETO(ONLCR), ISSETO(OCRNL),
			ISSETO(ONOCR), ISSETO(ONLRET), ISSETO(OFILL), ISSETO(OFDEL),
			VALO(NLDLY), VALO(CRDLY), VALO(TABDLY), VALO(BSDLY),
			VALO(VTDLY), VALO(FFDLY));

#define ISSETC(b) ((my_termios->c_cflag | (b)) ? BITSSET : BITSNOTSET)
#define VALC(b) (my_termios->c_cflag | (b))

	applog(LOG_DEBUG, "TIOS:  c_cflag: CBAUDEX=%s CSIZE=%d CSTOPB=%s CREAD=%s PARENB=%s PARODD=%s HUPCL=%s CLOCAL=%s"
#ifdef LOBLK
			" LOBLK=%s"
#endif
			" CMSPAR=%s CRTSCTS=%s",
			ISSETC(CBAUDEX), VALC(CSIZE), ISSETC(CSTOPB), ISSETC(CREAD),
			ISSETC(PARENB), ISSETC(PARODD), ISSETC(HUPCL), ISSETC(CLOCAL),
#ifdef LOBLK
			ISSETC(LOBLK),
#endif
			ISSETC(CMSPAR), ISSETC(CRTSCTS));

#define ISSETL(b) ((my_termios->c_lflag | (b)) ? BITSSET : BITSNOTSET)

	applog(LOG_DEBUG, "TIOS:  c_lflag: ISIG=%s ICANON=%s XCASE=%s ECHO=%s ECHOE=%s ECHOK=%s ECHONL=%s ECHOCTL=%s ECHOPRT=%s ECHOKE=%s"
#ifdef DEFECHO
			" DEFECHO=%s"
#endif
			" FLUSHO=%s NOFLSH=%s TOSTOP=%s PENDIN=%s IEXTEN=%s",
			ISSETL(ISIG), ISSETL(ICANON), ISSETL(XCASE), ISSETL(ECHO),
			ISSETL(ECHOE), ISSETL(ECHOK), ISSETL(ECHONL), ISSETL(ECHOCTL),
			ISSETL(ECHOPRT), ISSETL(ECHOKE),
#ifdef DEFECHO
			ISSETL(DEFECHO),
#endif
			ISSETL(FLUSHO), ISSETL(NOFLSH), ISSETL(TOSTOP), ISSETL(PENDIN),
			ISSETL(IEXTEN));

#define VALCC(b) (my_termios->c_cc[b])
	applog(LOG_DEBUG, "TIOS:  c_cc: VINTR=0x%02x VQUIT=0x%02x VERASE=0x%02x VKILL=0x%02x VEOF=0x%02x VMIN=%u VEOL=0x%02x VTIME=%u VEOL2=0x%02x"
#ifdef VSWTCH
			" VSWTCH=0x%02x"
#endif
			" VSTART=0x%02x VSTOP=0x%02x VSUSP=0x%02x"
#ifdef VDSUSP
			" VDSUSP=0x%02x"
#endif
			" VLNEXT=0x%02x VWERASE=0x%02x VREPRINT=0x%02x VDISCARD=0x%02x"
#ifdef VSTATUS
			" VSTATUS=0x%02x"
#endif
			,
			VALCC(VINTR), VALCC(VQUIT), VALCC(VERASE), VALCC(VKILL),
			VALCC(VEOF), VALCC(VMIN), VALCC(VEOL), VALCC(VTIME),
			VALCC(VEOL2),
#ifdef VSWTCH
			VALCC(VSWTCH),
#endif
			VALCC(VSTART), VALCC(VSTOP), VALCC(VSUSP),
#ifdef VDSUSP
			VALCC(VDSUSP),
#endif
			VALCC(VLNEXT), VALCC(VWERASE),
			VALCC(VREPRINT), VALCC(VDISCARD)
#ifdef VSTATUS
			,VALCC(VSTATUS)
#endif
			);
}
#endif
#endif

int serial_open(const char *devpath, unsigned long baud, signed short timeout, bool purge)
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
			applog(LOG_DEBUG, "Open %s failed, GetLastError:%d", devpath, (int)e);
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
	const DWORD ctoms = (timeout * 100);
	COMMTIMEOUTS cto = {ctoms, 0, ctoms, 0, ctoms};
	SetCommTimeouts(hSerial, &cto);

	if (purge) {
		PurgeComm(hSerial, PURGE_RXABORT);
		PurgeComm(hSerial, PURGE_TXABORT);
		PurgeComm(hSerial, PURGE_RXCLEAR);
		PurgeComm(hSerial, PURGE_TXCLEAR);
	}

	return _open_osfhandle((intptr_t)hSerial, 0);
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

#ifdef TERMIOS_DEBUG
	termios_debug(devpath, &my_termios, "before");
#endif

	switch (baud) {
	case 0:
		break;
	case 19200:
		cfsetispeed(&my_termios, B19200);
		cfsetospeed(&my_termios, B19200);
		break;
	case 38400:
		cfsetispeed(&my_termios, B38400);
		cfsetospeed(&my_termios, B38400);
		break;
	case 57600:
		cfsetispeed(&my_termios, B57600);
		cfsetospeed(&my_termios, B57600);
		break;
	case 115200:
		cfsetispeed(&my_termios, B115200);
		cfsetospeed(&my_termios, B115200);
		break;
	// TODO: try some higher speeds with the Icarus and BFL to see
	// if they support them and if setting them makes any difference
	// N.B. B3000000 doesn't work on Icarus
	default:
		applog(LOG_WARNING, "Unrecognized baud rate: %lu", baud);
	}

	my_termios.c_cflag &= ~(CSIZE | PARENB);
	my_termios.c_cflag |= CS8;
	my_termios.c_cflag |= CREAD;
	my_termios.c_cflag |= CLOCAL;

	my_termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK |
				ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	my_termios.c_oflag &= ~OPOST;
	my_termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	// Code must specify a valid timeout value (0 means don't timeout)
	my_termios.c_cc[VTIME] = (cc_t)timeout;
	my_termios.c_cc[VMIN] = 0;

#ifdef TERMIOS_DEBUG
	termios_debug(devpath, &my_termios, "settings");
#endif

	tcsetattr(fdDev, TCSANOW, &my_termios);

#ifdef TERMIOS_DEBUG
	tcgetattr(fdDev, &my_termios);
	termios_debug(devpath, &my_termios, "after");
#endif

	if (purge)
		tcflush(fdDev, TCIOFLUSH);
	return fdDev;
#endif
}

ssize_t _serial_read(int fd, char *buf, size_t bufsiz, char *eol)
{
	ssize_t len, tlen = 0;
	while (bufsiz) {
		len = read(fd, buf, eol ? 1 : bufsiz);
		if (unlikely(len == -1))
			break;
		tlen += len;
		if (eol && *eol == buf[0])
			break;
		buf += len;
		bufsiz -= len;
	}
	return tlen;
}

static FILE *_open_bitstream(const char *path, const char *subdir, const char *filename)
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

FILE *open_bitstream(const char *dname, const char *filename)
{
	FILE *f;

	_open_bitstream3(opt_kernel_path);
	_open_bitstream3(cgminer_path);
	_open_bitstream3(".");

	return NULL;
}

#ifndef WIN32

static bool _select_wait_read(int fd, struct timeval *timeout)
{
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (select(fd+1, &rfds, NULL, NULL, timeout) > 0)
		return true;
	else
		return false;
}

// Default timeout 100ms - only for device initialisation
const struct timeval tv_timeout_default = { 0, 100000 };
// Default inter character timeout = 1ms - only for device initialisation
const struct timeval tv_inter_char_default = { 0, 1000 };

// Device initialisation function - NOT for work processing
size_t _select_read(int fd, char *buf, size_t bufsiz, struct timeval *timeout, struct timeval *char_timeout, int finished)
{
	struct timeval tv_time, tv_char;
	ssize_t siz, red = 0;
	char got;

	// timeout is the maximum time to wait for the first character
	tv_time.tv_sec = timeout->tv_sec;
	tv_time.tv_usec = timeout->tv_usec;

	if (!_select_wait_read(fd, &tv_time))
		return 0;

	while (4242) {
		if ((siz = read(fd, buf, 1)) < 0)
			return red;

		got = *buf;
		buf += siz;
		red += siz;
		bufsiz -= siz;

		if (bufsiz < 1 || (finished >= 0 && got == finished))
			return red;

		// char_timeout is the maximum time to wait for each subsequent character
		// this is OK for initialisation, but bad for work processing
		// work processing MUST have a fixed size so this doesn't come into play
		tv_char.tv_sec = char_timeout->tv_sec;
		tv_char.tv_usec = char_timeout->tv_usec;

		if (!_select_wait_read(fd, &tv_char))
			return red;
	}

	return red;
}

// Device initialisation function - NOT for work processing
size_t _select_write(int fd, char *buf, size_t siz, struct timeval *timeout)
{
	struct timeval tv_time, tv_now, tv_finish;
	fd_set rfds;
	ssize_t wrote = 0, ret;

	cgtime(&tv_now);
	timeradd(&tv_now, timeout, &tv_finish);

	// timeout is the maximum time to spend trying to write
	tv_time.tv_sec = timeout->tv_sec;
	tv_time.tv_usec = timeout->tv_usec;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	while (siz > 0 && (tv_now.tv_sec < tv_finish.tv_sec || (tv_now.tv_sec == tv_finish.tv_sec && tv_now.tv_usec < tv_finish.tv_usec)) && select(fd+1, NULL, &rfds, NULL, &tv_time) > 0) {
		if ((ret = write(fd, buf, 1)) > 0) {
			buf++;
			wrote++;
			siz--;
		}
		else if (ret < 0)
			return wrote;

		cgtime(&tv_now);
	}

	return wrote;
}

int get_serial_cts(int fd)
{
	int flags;

	if (!fd)
		return -1;

	ioctl(fd, TIOCMGET, &flags);
	return (flags & TIOCM_CTS) ? 1 : 0;
}
#else
int get_serial_cts(const int fd)
{
	if (!fd)
		return -1;
	const HANDLE fh = (HANDLE)_get_osfhandle(fd);
	if (!fh)
		return -1;

	DWORD flags;
	if (!GetCommModemStatus(fh, &flags))
		return -1;

	return (flags & MS_CTS_ON) ? 1 : 0;
}
#endif // ! WIN32
