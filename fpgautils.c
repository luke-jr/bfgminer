/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2013 Con Kolivas
 * Copyright 2012 Andrew Smith
 * Copyright 2013 Xiangfu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#ifdef HAVE_LIBUSB
#include <libusb.h>
#endif

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
#else  /* WIN32 */
#include <windows.h>
#include <io.h>

#include <utlist.h>

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
#include <sys/ioctl.h>
#endif

#include "logging.h"
#include "miner.h"
#include "fpgautils.h"

#define SEARCH_NEEDLES_BEGIN()  {  \
	const char *needle;  \
	bool __cont = false;  \
	va_list ap;  \
	va_copy(ap, needles);  \
	while ( (needle = va_arg(ap, const char *)) )  \
	{

#define SEARCH_NEEDLES_END(...)  \
	}  \
	va_end(ap);  \
	if (__cont)  \
	{  \
		__VA_ARGS__;  \
	}  \
}

static inline
bool search_needles(const char *haystack, va_list needles)
{
	bool rv = true;
	SEARCH_NEEDLES_BEGIN()
		if (!strstr(haystack, needle))
		{
			rv = false;
			break;
		}
	SEARCH_NEEDLES_END()
	return rv;
}

#define SEARCH_NEEDLES(haystack)  search_needles(haystack, needles)

static
int _detectone_wrap(const detectone_func_t detectone, const char * const param, const char *fname)
{
	if (bfg_claim_serial(NULL, false, param))
	{
		applog(LOG_DEBUG, "%s: %s is already claimed, skipping probe", fname, param);
		return 0;
	}
	return detectone(param);
}
#define detectone(param)  _detectone_wrap(detectone, param, __func__)

struct detectone_meta_info_t detectone_meta_info;

void clear_detectone_meta_info(void)
{
	detectone_meta_info = (struct detectone_meta_info_t){
		.manufacturer = NULL,
	};
}

#ifdef HAVE_LIBUDEV
static
void _decode_udev_enc(char *o, const char *s)
{
	while(s[0])
	{
		if (s[0] == '\\' && s[1] == 'x' && s[2] && s[3])
		{
			hex2bin((void*)(o++), &s[2], 1);
			s += 4;
		}
		else
			(o++)[0] = (s++)[0];
	}
	o[0] = '\0';
}

static
char *_decode_udev_enc_dup(const char *s)
{
	if (!s)
		return NULL;
	
	char *o = malloc(strlen(s) + 1);
	if (!o)
	{
		applog(LOG_ERR, "Failed to malloc in _decode_udev_enc_dup");
		return NULL;
	}
	
	_decode_udev_enc(o, s);
	return o;
}

static
int _serial_autodetect_udev(detectone_func_t detectone, va_list needles)
{
	struct udev *udev = udev_new();
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	struct udev_list_entry *list_entry;
	char found = 0;

	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_scan_devices(enumerate);
	udev_list_entry_foreach(list_entry, udev_enumerate_get_list_entry(enumerate)) {
		struct udev_device *device = udev_device_new_from_syspath(
			udev_enumerate_get_udev(enumerate),
			udev_list_entry_get_name(list_entry)
		);
		if (!device)
			continue;

		const char *model = udev_device_get_property_value(device, "ID_MODEL");
		if (!(model && SEARCH_NEEDLES(model)))
		{
			udev_device_unref(device);
			continue;
		}

		detectone_meta_info = (struct detectone_meta_info_t){
			.manufacturer = _decode_udev_enc_dup(udev_device_get_property_value(device, "ID_VENDOR_ENC")),
			.product = _decode_udev_enc_dup(udev_device_get_property_value(device, "ID_MODEL_ENC")),
			.serial = _decode_udev_enc_dup(udev_device_get_property_value(device, "ID_SERIAL_SHORT")),
		};
		
		const char *devpath = udev_device_get_devnode(device);
		if (devpath && detectone(devpath))
			++found;

		free((void*)detectone_meta_info.manufacturer);
		free((void*)detectone_meta_info.product);
		free((void*)detectone_meta_info.serial);
		udev_device_unref(device);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	clear_detectone_meta_info();

	return found;
}
#else
#	define _serial_autodetect_udev(...)  (0)
#endif

#ifndef WIN32
static
int _serial_autodetect_devserial(detectone_func_t detectone, va_list needles)
{
	DIR *D;
	struct dirent *de;
	const char udevdir[] = "/dev/serial/by-id";
	char devpath[sizeof(udevdir) + 1 + NAME_MAX];
	char *devfile = devpath + sizeof(udevdir);
	char found = 0;

	// No way to split this out of the filename reliably
	clear_detectone_meta_info();
	
	D = opendir(udevdir);
	if (!D)
		return 0;
	memcpy(devpath, udevdir, sizeof(udevdir) - 1);
	devpath[sizeof(udevdir) - 1] = '/';
	while ( (de = readdir(D)) ) {
		if (!SEARCH_NEEDLES(de->d_name))
			continue;
		
		strcpy(devfile, de->d_name);
		if (detectone(devpath))
			++found;
	}
	closedir(D);

	return found;
}
#else
#	define _serial_autodetect_devserial(...)  (0)
#endif

#ifndef WIN32
static
char *_sysfs_do_read(char *buf, size_t bufsz, const char *devpath, char *devfile, const char *append)
{
	FILE *F;
	
	strcpy(devfile, append);
	F = fopen(devpath, "r");
	if (F)
	{
		if (fgets(buf, bufsz, F))
		{
			size_t L = strlen(buf);
			while (isCspace(buf[--L]))
				buf[L] = '\0';
		}
		else
			buf[0] = '\0';
		fclose(F);
	}
	else
		buf[0] = '\0';
	
	return buf[0] ? buf : NULL;
}

static
void _sysfs_find_tty(detectone_func_t detectone, char *devpath, char *devfile, const char *prod, char *pfound)
{
	DIR *DT;
	struct dirent *de;
	char ttybuf[0x10] = "/dev/";
	char manuf[0x40], serial[0x40];
	char *mydevfile = strdup(devfile);
	
	DT = opendir(devpath);
	if (!DT)
		goto out;
	
	while ( (de = readdir(DT)) )
	{
		if (strncmp(de->d_name, "tty", 3))
			continue;
		if (!de->d_name[3])
		{
			// "tty" directory: recurse (needed for ttyACM)
			sprintf(devfile, "%s/tty", mydevfile);
			_sysfs_find_tty(detectone, devpath, devfile, prod, pfound);
			continue;
		}
		if (strncmp(&de->d_name[3], "USB", 3) && strncmp(&de->d_name[3], "ACM", 3))
			continue;
		
		
		detectone_meta_info = (struct detectone_meta_info_t){
			.manufacturer = _sysfs_do_read(manuf, sizeof(manuf), devpath, devfile, "/manufacturer"),
			.product = prod,
			.serial = _sysfs_do_read(serial, sizeof(serial), devpath, devfile, "/serial"),
		};
		
		strcpy(&ttybuf[5], de->d_name);
		if (detectone(ttybuf))
			++*pfound;
	}
	closedir(DT);
	
out:
	free(mydevfile);
}

static
int _serial_autodetect_sysfs(detectone_func_t detectone, va_list needles)
{
	DIR *D, *DS;
	struct dirent *de;
	const char devroot[] = "/sys/bus/usb/devices";
	const size_t devrootlen = sizeof(devroot) - 1;
	char devpath[sizeof(devroot) + (NAME_MAX * 3)];
	char prod[0x40];
	char *devfile, *upfile;
	char found = 0;
	size_t len, len2;
	
	D = opendir(devroot);
	if (!D)
		return 0;
	memcpy(devpath, devroot, devrootlen);
	devpath[devrootlen] = '/';
	while ( (de = readdir(D)) )
	{
		len = strlen(de->d_name);
		upfile = &devpath[devrootlen + 1];
		memcpy(upfile, de->d_name, len);
		devfile = upfile + len;
		
		if (!_sysfs_do_read(prod, sizeof(prod), devpath, devfile, "/product"))
			continue;
		
		if (!SEARCH_NEEDLES(prod))
			continue;
		
		devfile[0] = '\0';
		DS = opendir(devpath);
		if (!DS)
			continue;
		devfile[0] = '/';
		++devfile;
		
		while ( (de = readdir(DS)) )
		{
			if (strncmp(de->d_name, upfile, len))
				continue;
			
			len2 = strlen(de->d_name);
			memcpy(devfile, de->d_name, len2 + 1);
			
			_sysfs_find_tty(detectone, devpath, devfile, prod, &found);
		}
		closedir(DS);
	}
	closedir(D);
	clear_detectone_meta_info();
	
	return found;
}
#else
#	define _serial_autodetect_sysfs(...)  (0)
#endif

#ifdef WIN32
#define LOAD_SYM(sym)  do { \
	if (!(sym = dlsym(dll, #sym))) {  \
		applog(LOG_DEBUG, "Failed to load " #sym ", not using FTDI autodetect");  \
		goto out;  \
	}  \
} while(0)

#ifdef UNTESTED_FTDI_DETECTONE_META_INFO
static
char *_ftdi_get_string(char *buf, int i, DWORD flags)
{
	if (FT_OK != FT_ListDevices((PVOID)i, buf, FT_LIST_BY_INDEX | flags))
		return NULL;
	return buf[0] ? buf : NULL;
}
#endif

static
int _serial_autodetect_ftdi(detectone_func_t detectone, va_list needles)
{
	char devpath[] = "\\\\.\\COMnnnnn";
	char *devpathnum = &devpath[7];
	char **bufptrs;
	char *buf;
#ifdef UNTESTED_FTDI_DETECTONE_META_INFO
	char manuf[64], serial[64];
#endif
	int found = 0;
	DWORD i;

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
	
	clear_detectone_meta_info();
	for (i = numDevs; i > 0; ) {
		--i;
		bufptrs[i][64] = '\0';
		
		if (!SEARCH_NEEDLES(bufptrs[i]))
			continue;
		
		FT_HANDLE ftHandle;
		if (FT_OK != FT_Open(i, &ftHandle))
			continue;
		LONG lComPortNumber;
		ftStatus = FT_GetComPortNumber(ftHandle, &lComPortNumber);
		FT_Close(ftHandle);
		if (FT_OK != ftStatus || lComPortNumber < 0)
			continue;
		
		applog(LOG_ERR, "FT_GetComPortNumber(%p (%ld), %ld)", ftHandle, (long)i, (long)lComPortNumber);
#ifdef UNTESTED_FTDI_DETECTONE_META_INFO
		detectone_meta_info = (struct detectone_meta_info_t){
			.product = bufptrs[i],
			.serial = _ftdi_get_string(serial, i, FT_OPEN_BY_SERIAL_NUMBER),
		};
#endif
		
		sprintf(devpathnum, "%d", (int)lComPortNumber);
		
		if (detectone(devpath))
			++found;
	}

out:
	clear_detectone_meta_info();
	dlclose(dll);
	return found;
}
#else
#	define _serial_autodetect_ftdi(...)  (0)
#endif

#undef detectone

int _serial_autodetect(detectone_func_t detectone, ...)
{
	int rv;
	va_list needles;
	
	va_start(needles, detectone);
	rv = (
		_serial_autodetect_udev     (detectone, needles) ?:
		_serial_autodetect_sysfs    (detectone, needles) ?:
		_serial_autodetect_devserial(detectone, needles) ?:
		_serial_autodetect_ftdi     (detectone, needles) ?:
		0);
	va_end(needles);
	return rv;
}

enum bfg_device_bus {
	BDB_SERIAL,
	BDB_USB,
};

// TODO: claim USB side of USB-Serial devices
typedef
struct my_dev_t {
	enum bfg_device_bus bus;
	union {
		struct {
			uint8_t usbbus;
			uint8_t usbaddr;
		};
#ifndef WIN32
		dev_t dev;
#else
		int com;
#endif
	};
} my_dev_t;

struct _device_claim {
	struct device_drv *drv;
	my_dev_t dev;
	UT_hash_handle hh;
};

static
struct device_drv *bfg_claim_any(struct device_drv * const api, const char * const verbose, const my_dev_t * const dev)
{
	static struct _device_claim *claims = NULL;
	struct _device_claim *c;
	
	HASH_FIND(hh, claims, dev, sizeof(*dev), c);
	if (c)
	{
		if (verbose)
			applog(LOG_DEBUG, "%s device %s already claimed by other driver: %s",
			       api->dname, verbose, c->drv->dname);
		return c->drv;
	}
	
	if (!api)
		return NULL;
	
	c = malloc(sizeof(*c));
	c->dev = *dev;
	c->drv = api;
	HASH_ADD(hh, claims, dev, sizeof(*dev), c);
	return NULL;
}

struct device_drv *bfg_claim_serial(struct device_drv * const api, const bool verbose, const char * const devpath)
{
	my_dev_t dev;
	
	memset(&dev, 0, sizeof(dev));
	dev.bus = BDB_SERIAL;
#ifndef WIN32
	{
		struct stat my_stat;
		if (stat(devpath, &my_stat))
			return NULL;
		dev.dev = my_stat.st_rdev;
	}
#else
	{
		char *p = strstr(devpath, "COM"), *p2;
		if (!p)
			return NULL;
		dev.com = strtol(&p[3], &p2, 10);
		if (p2 == p)
			return NULL;
	}
#endif
	
	return bfg_claim_any(api, (verbose ? devpath : NULL), &dev);
}

struct device_drv *bfg_claim_usb(struct device_drv * const api, const bool verbose, const uint8_t usbbus, const uint8_t usbaddr)
{
	my_dev_t dev;
	char *desc = NULL;
	
	// We should be able to just initialize a const my_dev_t for this, but Xcode's clang is broken
	// Affected: Apple LLVM version 4.2 (clang-425.0.28) (based on LLVM 3.2svn) AKA Xcode 4.6.3
	// Works with const: GCC 4.6.3, LLVM 3.1
	memset(&dev, 0, sizeof(dev));
	dev.bus = BDB_USB;
	dev.usbbus = usbbus;
	dev.usbaddr = usbaddr;
	
	if (verbose)
	{
		desc = alloca(3 + 1 + 3 + 1);
		sprintf(desc, "%03u:%03u", (unsigned)usbbus, (unsigned)usbaddr);
	}
	
	return bfg_claim_any(api, desc, &dev);
}

#ifdef HAVE_LIBUSB
void cgpu_copy_libusb_strings(struct cgpu_info *cgpu, libusb_device *usb)
{
	unsigned char buf[0x20];
	libusb_device_handle *h;
	struct libusb_device_descriptor desc;
	
	if (LIBUSB_SUCCESS != libusb_open(usb, &h))
		return;
	if (libusb_get_device_descriptor(usb, &desc))
	{
		libusb_close(h);
		return;
	}
	
	if ((!cgpu->dev_manufacturer) && libusb_get_string_descriptor_ascii(h, desc.iManufacturer, buf, sizeof(buf)) >= 0)
		cgpu->dev_manufacturer = strdup((void *)buf);
	if ((!cgpu->dev_product) && libusb_get_string_descriptor_ascii(h, desc.iProduct, buf, sizeof(buf)) >= 0)
		cgpu->dev_product = strdup((void *)buf);
	if ((!cgpu->dev_serial) && libusb_get_string_descriptor_ascii(h, desc.iSerialNumber, buf, sizeof(buf)) >= 0)
		cgpu->dev_serial = strdup((void *)buf);
	
	libusb_close(h);
}
#endif

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
#define IOSPEED(baud)  \
		case B ## baud:  \
			return baud;  \
// END
#include "iospeeds_local.h"
#undef IOSPEED
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
#endif  /* TERMIOS_DEBUG */

speed_t tiospeed_t(int baud)
{
	switch (baud) {
#define IOSPEED(baud)  \
		case baud:  \
			return B ## baud;  \
// END
#include "iospeeds_local.h"
#undef IOSPEED
	default:
		return B0;
	}
}

#endif  /* WIN32 */

bool valid_baud(int baud)
{
	switch (baud) {
#define IOSPEED(baud)  \
		case baud:  \
			return true;  \
// END
#include "iospeeds_local.h"
#undef IOSPEED
		default:
			return false;
	}
}

/* NOTE: Linux only supports uint8_t (decisecond) timeouts; limiting it in
 *       this interface buys us warnings when bad constants are passed in.
 */
int serial_open(const char *devpath, unsigned long baud, uint8_t timeout, bool purge)
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
			applog(LOG_DEBUG, "Open %s failed, GetLastError:%u", devpath, (unsigned)e);
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

	return _open_osfhandle((intptr_t)hSerial, 0);
#else
	int fdDev = open(devpath, O_RDWR | O_CLOEXEC | O_NOCTTY);

	if (unlikely(fdDev == -1))
	{
		if (errno == EACCES)
			applog(LOG_ERR, "Do not have user privileges required to open %s", devpath);
		else
			applog(LOG_DEBUG, "Open %s failed: %s", devpath, bfg_strerror(errno, BST_ERRNO));

		return -1;
	}
	
#if defined(LOCK_EX) && defined(LOCK_NB)
	if (likely(!flock(fdDev, LOCK_EX | LOCK_NB)))
		applog(LOG_DEBUG, "Acquired exclusive advisory lock on %s", devpath);
	else
	if (errno == EWOULDBLOCK)
	{
		applog(LOG_ERR, "%s is already in use by another process", devpath);
		close(fdDev);
		return -1;
	}
	else
		applog(LOG_WARNING, "Failed to acquire exclusive lock on %s: %s (ignoring)", devpath, bfg_strerror(errno, BST_ERRNO));
#endif

	struct termios my_termios;

	tcgetattr(fdDev, &my_termios);

#ifdef TERMIOS_DEBUG
	termios_debug(devpath, &my_termios, "before");
#endif

	if (baud)
	{
		speed_t speed = tiospeed_t(baud);
		if (speed == B0)
			applog(LOG_WARNING, "Unrecognized baud rate: %lu", baud);
		else
		{
			cfsetispeed(&my_termios, speed);
			cfsetospeed(&my_termios, speed);
		}
	}

	my_termios.c_cflag &= ~(CSIZE | PARENB);
	my_termios.c_cflag |= CS8;
	my_termios.c_cflag |= CREAD;
#ifdef USE_AVALON
//	my_termios.c_cflag |= CRTSCTS;
#endif
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

#define bailout(...)  do {  \
	applog(__VA_ARGS__);  \
	return NULL;  \
} while(0)

#define check_magic(L)  do {  \
	if (1 != fread(buf, 1, 1, f))  \
		bailout(LOG_ERR, "%s: Error reading bitstream ('%c')",  \
		        repr, L);  \
	if (buf[0] != L)  \
		bailout(LOG_ERR, "%s: Firmware has wrong magic ('%c')",  \
		        repr, L);  \
} while(0)

#define read_str(eng)  do {  \
	if (1 != fread(buf, 2, 1, f))  \
		bailout(LOG_ERR, "%s: Error reading bitstream (" eng " len)",  \
		        repr);  \
	len = (ubuf[0] << 8) | ubuf[1];  \
	if (len >= sizeof(buf))  \
		bailout(LOG_ERR, "%s: Firmware " eng " too long",  \
		        repr);  \
	if (1 != fread(buf, len, 1, f))  \
		bailout(LOG_ERR, "%s: Error reading bitstream (" eng ")",  \
		        repr);  \
	buf[len] = '\0';  \
} while(0)

void _bitstream_not_found(const char *repr, const char *fn)
{
	applog(LOG_ERR, "ERROR: Unable to load '%s', required for %s to work!", fn, repr);
	applog(LOG_ERR, "ERROR: Please read README.FPGA for instructions");
}

FILE *open_xilinx_bitstream(const char *dname, const char *repr, const char *fwfile, unsigned long *out_len)
{
	char buf[0x100];
	unsigned char *ubuf = (unsigned char*)buf;
	unsigned long len;
	char *p;

	FILE *f = open_bitstream(dname, fwfile);
	if (!f)
	{
		_bitstream_not_found(repr, fwfile);
		return NULL;
	}
	if (1 != fread(buf, 2, 1, f))
		bailout(LOG_ERR, "%s: Error reading bitstream (magic)",
		        repr);
	if (buf[0] || buf[1] != 9)
		bailout(LOG_ERR, "%s: Firmware has wrong magic (9)",
		        repr);
	if (-1 == fseek(f, 11, SEEK_CUR))
		bailout(LOG_ERR, "%s: Firmware seek failed",
		        repr);
	check_magic('a');
	read_str("design name");
	applog(LOG_DEBUG, "%s: Firmware file %s info:",
	       repr, fwfile);
	applog(LOG_DEBUG, "  Design name: %s", buf);
	p = strrchr(buf, ';') ?: buf;
	p = strrchr(buf, '=') ?: p;
	if (p[0] == '=')
		++p;
	unsigned long fwusercode = (unsigned long)strtoll(p, &p, 16);
	if (p[0] != '\0')
		bailout(LOG_ERR, "%s: Bad usercode in bitstream file",
		        repr);
	if (fwusercode == 0xffffffff)
		bailout(LOG_ERR, "%s: Firmware doesn't support user code",
		        repr);
	applog(LOG_DEBUG, "  Version: %u, build %u", (unsigned)((fwusercode >> 8) & 0xff), (unsigned)(fwusercode & 0xff));
	check_magic('b');
	read_str("part number");
	applog(LOG_DEBUG, "  Part number: %s", buf);
	check_magic('c');
	read_str("build date");
	applog(LOG_DEBUG, "  Build date: %s", buf);
	check_magic('d');
	read_str("build time");
	applog(LOG_DEBUG, "  Build time: %s", buf);
	check_magic('e');
	if (1 != fread(buf, 4, 1, f))
		bailout(LOG_ERR, "%s: Error reading bitstream (data len)",
		        repr);
	len = ((unsigned long)ubuf[0] << 24) | ((unsigned long)ubuf[1] << 16) | (ubuf[2] << 8) | ubuf[3];
	applog(LOG_DEBUG, "  Bitstream size: %lu", len);

	*out_len = len;
	return f;
}

bool load_bitstream_intelhex(bytes_t *rv, const char *dname, const char *repr, const char *fn)
{
	char buf[0x100];
	size_t sz;
	uint8_t xsz, xrt;
	uint16_t xaddr;
	FILE *F = open_bitstream(dname, fn);
	if (!F)
		return false;
	while (!feof(F))
	{
		if (unlikely(ferror(F)))
		{
			applog(LOG_ERR, "Error reading '%s'", fn);
			goto ihxerr;
		}
		fgets(buf, sizeof(buf), F);
		if (unlikely(buf[0] != ':'))
			goto ihxerr;
		if (unlikely(!(
			hex2bin(&xsz, &buf[1], 1)
		 && hex2bin((unsigned char*)&xaddr, &buf[3], 2)
		 && hex2bin(&xrt, &buf[7], 1)
		)))
		{
			applog(LOG_ERR, "Error parsing in '%s'", fn);
			goto ihxerr;
		}
		switch (xrt)
		{
			case 0:  // data
				break;
			case 1:  // EOF
				fclose(F);
				return true;
			default:
				applog(LOG_ERR, "Unsupported record type in '%s'", fn);
				goto ihxerr;
		}
		xaddr = be16toh(xaddr);
		sz = bytes_len(rv);
		bytes_resize(rv, xaddr + xsz);
		if (sz < xaddr)
			memset(&bytes_buf(rv)[sz], 0xff, xaddr - sz);
		if (unlikely(!(hex2bin(&bytes_buf(rv)[xaddr], &buf[9], xsz))))
		{
			applog(LOG_ERR, "Error parsing data in '%s'", fn);
			goto ihxerr;
		}
		// TODO: checksum
	}
	
ihxerr:
	fclose(F);
	bytes_reset(rv);
	return false;
}

bool load_bitstream_bytes(bytes_t *rv, const char *dname, const char *repr, const char *fileprefix)
{
	FILE *F;
	size_t fplen = strlen(fileprefix);
	char fnbuf[fplen + 4 + 1];
	int e;
	
	bytes_reset(rv);
	memcpy(fnbuf, fileprefix, fplen);
	
	strcpy(&fnbuf[fplen], ".bin");
	F = open_bitstream(dname, fnbuf);
	if (F)
	{
		char buf[0x100];
		size_t sz;
		while ( (sz = fread(buf, 1, sizeof(buf), F)) )
			bytes_append(rv, buf, sz);
		e = ferror(F);
		fclose(F);
		if (unlikely(e))
		{
			applog(LOG_ERR, "Error reading '%s'", fnbuf);
			bytes_reset(rv);
		}
		else
			return true;
	}
	
	strcpy(&fnbuf[fplen], ".ihx");
	if (load_bitstream_intelhex(rv, dname, repr, fnbuf))
		return true;
	
	// TODO: Xilinx
	
	_bitstream_not_found(repr, fnbuf);
	return false;
}

#ifndef WIN32

int get_serial_cts(int fd)
{
	int flags;

	if (!fd)
		return -1;

	ioctl(fd, TIOCMGET, &flags);
	return (flags & TIOCM_CTS) ? 1 : 0;
}

int set_serial_rts(int fd, int rts)
{
	int flags;

	if (!fd)
		return -1;

	ioctl(fd, TIOCMGET, &flags);
	
	if (rts)
		flags |= TIOCM_RTS;
	else
		flags &= ~TIOCM_RTS;

	ioctl(fd, TIOCMSET, &flags);
	return flags & TIOCM_CTS;
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
