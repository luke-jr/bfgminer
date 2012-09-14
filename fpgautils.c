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
#include <stdlib.h>
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
#else  /* WIN32 */
#include <windows.h>
#include <io.h>

#define dlsym (void*)GetProcAddress
#define dlclose FreeLibrary

typedef unsigned long FT_STATUS;
typedef PVOID FT_HANDLE;
__stdcall FT_STATUS (*FT_ListDevices)(PVOID pArg1, PVOID pArg2, DWORD Flags);
__stdcall FT_STATUS (*FT_Open)(int idx, FT_HANDLE*);
__stdcall FT_STATUS (*FT_GetComPortNumber)(FT_HANDLE, LPLONG lplComPortNumber);
__stdcall FT_STATUS (*FT_Close)(FT_HANDLE);
const uint32_t FT_OPEN_BY_DESCRIPTION =       2;
const uint32_t FT_LIST_ALL         = 0x20000000;
const uint32_t FT_LIST_NUMBER_ONLY = 0x80000000;
enum {
	FT_OK,
};
#endif  /* WIN32 */

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

#ifdef WIN32
#define LOAD_SYM(sym)  do { \
	if (!(sym = dlsym(dll, #sym))) {  \
		applog(LOG_DEBUG, "Failed to load " #sym ", not using FTDI autodetect");  \
		goto out;  \
	}  \
} while(0)

int serial_autodetect_ftdi(detectone_func_t detectone, const char *needle, const char *needle2)
{
	char devpath[] = "\\\\.\\COMnnnnn";
	char *devpathnum = &devpath[7];
	char **bufptrs;
	char *buf;
	int found = 0;
	int i;

	FT_STATUS ftStatus;
	DWORD numDevs;
	HMODULE dll = LoadLibrary("FTD2XX.DLL");
	if (!dll) {
		applog(LOG_DEBUG, "FTD2XX.DLL failed to load, not using FTDI autodetect");
		return 0;
	}
	LOAD_SYM(FT_ListDevices);
	LOAD_SYM(FT_Open);
	LOAD_SYM(FT_GetComPortNumber);
	LOAD_SYM(FT_Close);
	
	ftStatus = FT_ListDevices(&numDevs, NULL, FT_LIST_NUMBER_ONLY);
	if (ftStatus != FT_OK) {
		applog(LOG_DEBUG, "FTDI device count failed, not using FTDI autodetect");
		goto out;
	}
	applog(LOG_DEBUG, "FTDI reports %u devices", (unsigned)numDevs);

	buf = alloca(65 * numDevs);
	bufptrs = alloca(sizeof(*bufptrs) * (numDevs + 1));

	for (i = 0; i < numDevs; ++i)
		bufptrs[i] = &buf[i * 65];
	bufptrs[numDevs] = NULL;
	ftStatus = FT_ListDevices(bufptrs, &numDevs, FT_LIST_ALL | FT_OPEN_BY_DESCRIPTION);
	if (ftStatus != FT_OK) {
		applog(LOG_DEBUG, "FTDI device list failed, not using FTDI autodetect");
		goto out;
	}
	
	for (i = numDevs; i > 0; ) {
		--i;
		bufptrs[i][64] = '\0';
		
		if (!(strstr(bufptrs[i], needle) && (!needle2 || strstr(bufptrs[i], needle2))))
			continue;
		
		FT_HANDLE ftHandle;
		if (FT_OK != FT_Open(i, &ftHandle))
			continue;
		LONG lComPortNumber;
		ftStatus = FT_GetComPortNumber(ftHandle, &lComPortNumber);
		FT_Close(ftHandle);
		if (FT_OK != ftStatus || lComPortNumber < 0)
			continue;
		
		sprintf(devpathnum, "%d", (int)lComPortNumber);
		
		if (detectone(devpath))
			++found;
	}

out:
	dlclose(dll);
	return found;
}
#else
int serial_autodetect_ftdi(__maybe_unused detectone_func_t detectone, __maybe_unused const char *needle, __maybe_unused const char *needle2)
{
	return 0;
}
#endif

struct device_api *serial_claim(const char *devpath, struct device_api *api);

int
_serial_detect(const char*dname, detectone_func_t detectone, autoscan_func_t autoscan, int flags)
{
	struct string_elist *iter, *tmp;
	const char*s, *p;
	bool inhibitauto = false;
	char found = 0;
	bool forceauto = flags & 1;
	bool hasname;
	size_t dnamel = strlen(dname);

	list_for_each_entry_safe(iter, tmp, &scan_devices, list) {
		s = iter->string;
		if ((p = strchr(s, ':')) && p[1] != '\0') {
			size_t plen = p - s;
			if (plen != dnamel || strncasecmp(s, dname, plen))
				continue;
			s = p + 1;
			hasname = true;
		}
		else
			hasname = false;
		if (!strcmp(s, "auto"))
			forceauto = true;
		else
		if (!strcmp(s, "noauto"))
			inhibitauto = true;
		else
		if ((flags & 2) && !hasname)
			continue;
		else
		if (!detectone)
		{}  // do nothing
		else
		if (serial_claim(s, NULL))
		{
			applog(LOG_DEBUG, "%s is already claimed... skipping probes", s);
			string_elist_del(iter);
		}
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

#ifndef WIN32
typedef dev_t my_dev_t;
#else
typedef int my_dev_t;
#endif

struct _device_claim {
	struct device_api *api;
	my_dev_t dev;
	UT_hash_handle hh;
};

struct device_api *serial_claim(const char *devpath, struct device_api *api)
{
	static struct _device_claim *claims = NULL;
	struct _device_claim *c;
	my_dev_t dev;

#ifndef WIN32
	{
		struct stat my_stat;
		if (stat(devpath, &my_stat))
			return NULL;
		dev = my_stat.st_rdev;
	}
#else
	{
		char *p = strstr(devpath, "COM"), *p2;
		if (!p)
			return NULL;
		dev = strtol(&p[3], &p2, 10);
		if (p2 == p)
			return NULL;
	}
#endif

	HASH_FIND(hh, claims, &dev, sizeof(dev), c);
	if (c)
		return c->api;

	if (!api)
		return NULL;

	c = malloc(sizeof(*c));
	c->dev = dev;
	c->api = api;
	HASH_ADD(hh, claims, dev, sizeof(dev), c);
	return NULL;
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

#ifdef TERMIOS_DEBUG
	termios_debug(devpath, &my_termios, "before");
#endif

	switch (baud) {
	case 0:
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
