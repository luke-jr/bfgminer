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
#include <stdbool.h>
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

#ifdef HAVE_WIN_DDKUSB
#include <setupapi.h>
#include <usbioctl.h>
#include <usbiodef.h>
#endif

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
const uint32_t FT_OPEN_BY_SERIAL_NUMBER =     1;
const uint32_t FT_OPEN_BY_DESCRIPTION =       2;
const uint32_t FT_LIST_ALL         = 0x20000000;
const uint32_t FT_LIST_BY_INDEX    = 0x40000000;
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
#include "lowlevel.h"
#include "miner.h"
#include "util.h"

#include "fpgautils.h"

struct lowlevel_driver lowl_vcom;

struct detectone_meta_info_t detectone_meta_info;

void clear_detectone_meta_info(void)
{
	detectone_meta_info = (struct detectone_meta_info_t){
		.manufacturer = NULL,
	};
}

static char *_vcom_unique_id(const char *);

struct lowlevel_device_info *_vcom_devinfo_findorcreate(struct lowlevel_device_info ** const devinfo_list, const char * const devpath)
{
	struct lowlevel_device_info *devinfo;
	char * const devid = _vcom_unique_id(devpath);
	HASH_FIND_STR(*devinfo_list, devid, devinfo);
	if (!devinfo)
	{
		devinfo = malloc(sizeof(*devinfo));
		*devinfo = (struct lowlevel_device_info){
			.lowl = &lowl_vcom,
			.path = strdup(devpath),
			.devid = devid,
		};
		HASH_ADD_KEYPTR(hh, *devinfo_list, devinfo->devid, strlen(devid), devinfo);
	}
	else
		free(devid);
	return devinfo;
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
void _vcom_devinfo_scan_udev(struct lowlevel_device_info ** const devinfo_list)
{
	struct udev *udev = udev_new();
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	struct udev_list_entry *list_entry;
	struct lowlevel_device_info *devinfo;

	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_property(enumerate, "ID_SERIAL", "*");
	udev_enumerate_scan_devices(enumerate);
	udev_list_entry_foreach(list_entry, udev_enumerate_get_list_entry(enumerate)) {
		struct udev_device *device = udev_device_new_from_syspath(
			udev_enumerate_get_udev(enumerate),
			udev_list_entry_get_name(list_entry)
		);
		if (!device)
			continue;

		const char * const devpath = udev_device_get_devnode(device);
		devinfo = _vcom_devinfo_findorcreate(devinfo_list, devpath);
		
		BFGINIT(devinfo->manufacturer, _decode_udev_enc_dup(udev_device_get_property_value(device, "ID_VENDOR_ENC")));
		BFGINIT(devinfo->product, _decode_udev_enc_dup(udev_device_get_property_value(device, "ID_MODEL_ENC")));
		BFGINIT(devinfo->serial, _decode_udev_enc_dup(udev_device_get_property_value(device, "ID_SERIAL_SHORT")));
		
		udev_device_unref(device);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
}
#endif

#ifndef WIN32
static
void _vcom_devinfo_scan_devserial(struct lowlevel_device_info ** const devinfo_list)
{
	DIR *D;
	struct dirent *de;
	const char udevdir[] = "/dev/serial/by-id";
	char devpath[sizeof(udevdir) + 1 + NAME_MAX];
	char *devfile = devpath + sizeof(udevdir);
	struct lowlevel_device_info *devinfo;
	
	D = opendir(udevdir);
	if (!D)
		return;
	memcpy(devpath, udevdir, sizeof(udevdir) - 1);
	devpath[sizeof(udevdir) - 1] = '/';
	while ( (de = readdir(D)) ) {
		if (strncmp(de->d_name, "usb-", 4))
			continue;
		strcpy(devfile, de->d_name);
		devinfo = _vcom_devinfo_findorcreate(devinfo_list, devpath);
		if (!(devinfo->manufacturer || devinfo->product || devinfo->serial))
			devinfo->product = strdup(devfile);
	}
	closedir(D);
}
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
void _sysfs_find_tty(char *devpath, char *devfile, const char *prod, struct lowlevel_device_info ** const devinfo_list)
{
	struct lowlevel_device_info *devinfo;
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
			_sysfs_find_tty(devpath, devfile, prod, devinfo_list);
			continue;
		}
		if (strncmp(&de->d_name[3], "USB", 3) && strncmp(&de->d_name[3], "ACM", 3))
			continue;
		
		strcpy(&ttybuf[5], de->d_name);
		devinfo = _vcom_devinfo_findorcreate(devinfo_list, ttybuf);
		BFGINIT(devinfo->manufacturer, maybe_strdup(_sysfs_do_read(manuf, sizeof(manuf), devpath, devfile, "/manufacturer")));
		BFGINIT(devinfo->product, maybe_strdup(prod));
		BFGINIT(devinfo->serial, maybe_strdup(_sysfs_do_read(serial, sizeof(serial), devpath, devfile, "/serial")));
	}
	closedir(DT);
	
out:
	free(mydevfile);
}

static
void _vcom_devinfo_scan_sysfs(struct lowlevel_device_info ** const devinfo_list)
{
	DIR *D, *DS;
	struct dirent *de;
	const char devroot[] = "/sys/bus/usb/devices";
	const size_t devrootlen = sizeof(devroot) - 1;
	char devpath[sizeof(devroot) + (NAME_MAX * 3)];
	char prod[0x40];
	char *devfile, *upfile;
	size_t len, len2;
	
	D = opendir(devroot);
	if (!D)
		return;
	memcpy(devpath, devroot, devrootlen);
	devpath[devrootlen] = '/';
	while ( (de = readdir(D)) )
	{
		len = strlen(de->d_name);
		upfile = &devpath[devrootlen + 1];
		memcpy(upfile, de->d_name, len);
		devfile = upfile + len;
		
		if (!_sysfs_do_read(prod, sizeof(prod), devpath, devfile, "/product"))
			prod[0] = '\0';
		
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
			
			_sysfs_find_tty(devpath, devfile, prod[0] ? prod : NULL, devinfo_list);
		}
		closedir(DS);
	}
	closedir(D);
}
#endif

#ifdef HAVE_WIN_DDKUSB

static const GUID WIN_GUID_DEVINTERFACE_USB_HOST_CONTROLLER = { 0x3ABF6F2D, 0x71C4, 0x462A, {0x8A, 0x92, 0x1E, 0x68, 0x61, 0xE6, 0xAF, 0x27} };

static
char *windows_usb_get_port_path(HANDLE hubh, const int portno)
{
	size_t namesz;
	ULONG rsz;
	
	{
		USB_NODE_CONNECTION_NAME pathinfo = {
			.ConnectionIndex = portno,
		};
		if (!(DeviceIoControl(hubh, IOCTL_USB_GET_NODE_CONNECTION_NAME, &pathinfo, sizeof(pathinfo), &pathinfo, sizeof(pathinfo), &rsz, NULL) && rsz >= sizeof(pathinfo)))
			applogfailinfor(NULL, LOG_ERR, "ioctl (1)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
		namesz = pathinfo.ActualLength;
	}
	
	const size_t bufsz = sizeof(USB_NODE_CONNECTION_NAME) + namesz;
	uint8_t buf[bufsz];
	USB_NODE_CONNECTION_NAME *path = (USB_NODE_CONNECTION_NAME *)buf;
	*path = (USB_NODE_CONNECTION_NAME){
		.ConnectionIndex = portno,
	};
	
	if (!(DeviceIoControl(hubh, IOCTL_USB_GET_NODE_CONNECTION_NAME, path, bufsz, path, bufsz, &rsz, NULL) && rsz >= sizeof(*path)))
		applogfailinfor(NULL, LOG_ERR, "ioctl (2)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	
	return ucs2tochar_dup(path->NodeName, path->ActualLength);
}

static
char *windows_usb_get_string(HANDLE hubh, const int portno, const uint8_t descid)
{
	if (!descid)
		return NULL;
	
	const size_t descsz_max = sizeof(USB_STRING_DESCRIPTOR) + MAXIMUM_USB_STRING_LENGTH;
	const size_t reqsz = sizeof(USB_DESCRIPTOR_REQUEST) + descsz_max;
	uint8_t buf[reqsz];
	
	USB_DESCRIPTOR_REQUEST * const req = (USB_DESCRIPTOR_REQUEST *)buf;
	USB_STRING_DESCRIPTOR * const desc = (USB_STRING_DESCRIPTOR *)&req[1];
	*req = (USB_DESCRIPTOR_REQUEST){
		.ConnectionIndex = portno,
		.SetupPacket = {
			.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8) | descid,
			.wIndex = 0,
			.wLength = descsz_max,
		},
	};
	// Need to explicitly zero the output memory
	memset(desc, '\0', descsz_max);
	
	ULONG descsz;
	if (!DeviceIoControl(hubh, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, req, reqsz, req, reqsz, &descsz, NULL))
		applogfailinfor(NULL, LOG_DEBUG, "ioctl", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	
	if (descsz < 2 || desc->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE || desc->bLength > descsz - sizeof(USB_DESCRIPTOR_REQUEST) || desc->bLength % 2)
		applogfailr(NULL, LOG_ERR, "sanity check");
	
	return ucs2tochar_dup(desc->bString, desc->bLength);
}

static void _vcom_devinfo_scan_windows__hub(struct lowlevel_device_info **, const char *);

static
void _vcom_devinfo_scan_windows__hubport(struct lowlevel_device_info ** const devinfo_list, HANDLE hubh, const int portno)
{
	struct lowlevel_device_info *devinfo;
	const size_t conninfosz = sizeof(USB_NODE_CONNECTION_INFORMATION) + (sizeof(USB_PIPE_INFO) * 30);
	uint8_t buf[conninfosz];
	USB_NODE_CONNECTION_INFORMATION * const conninfo = (USB_NODE_CONNECTION_INFORMATION *)buf;
	
	conninfo->ConnectionIndex = portno;
	
	ULONG respsz;
	if (!DeviceIoControl(hubh, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION, conninfo, conninfosz, conninfo, conninfosz, &respsz, NULL))
		applogfailinfor(, LOG_ERR, "ioctl", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	
	if (conninfo->ConnectionStatus != DeviceConnected)
		return;
	
	if (conninfo->DeviceIsHub)
	{
		const char * const hubpath = windows_usb_get_port_path(hubh, portno);
		if (hubpath)
			_vcom_devinfo_scan_windows__hub(devinfo_list, hubpath);
		return;
	}
	
	const USB_DEVICE_DESCRIPTOR * const devdesc = &conninfo->DeviceDescriptor;
	char * const serial = windows_usb_get_string(hubh, portno, devdesc->iSerialNumber);
	if (!serial)
	{
out:
		free(serial);
		return;
	}
	const size_t slen = strlen(serial);
	char subkey[52 + slen + 18 + 1];
	sprintf(subkey, "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%04x&PID_%04x\\%s\\Device Parameters",
	        (unsigned)devdesc->idVendor, (unsigned)devdesc->idProduct, serial);
	HKEY hkey;
	int e;
	if (ERROR_SUCCESS != (e = RegOpenKey(HKEY_LOCAL_MACHINE, subkey, &hkey)))
	{
		applogfailinfo(LOG_ERR, "open Device Parameters registry key", "%s", bfg_strerror(e, BST_SYSTEM));
		goto out;
	}
	char devpath[0x10] = "\\\\.\\";
	DWORD type, sz = sizeof(devpath) - 4;
	if (ERROR_SUCCESS != (e = RegQueryValueExA(hkey, "PortName", NULL, &type, (LPBYTE)&devpath[4], &sz)))
	{
		applogfailinfo(LOG_ERR, "get PortName registry key value", "%s", bfg_strerror(e, BST_SYSTEM));
		RegCloseKey(hkey);
		goto out;
	}
	RegCloseKey(hkey);
	if (type != REG_SZ)
	{
		applogfailinfor(, LOG_ERR, "get expected type for PortName registry key value", "%ld", (long)type);
		goto out;
	}
	
	devinfo = _vcom_devinfo_findorcreate(devinfo_list, devpath);
	BFGINIT(devinfo->manufacturer, windows_usb_get_string(hubh, portno, devdesc->iManufacturer));
	BFGINIT(devinfo->product, windows_usb_get_string(hubh, portno, devdesc->iProduct));
	if (devinfo->serial)
		free(serial);
	else
		devinfo->serial = serial;
}

static
void _vcom_devinfo_scan_windows__hub(struct lowlevel_device_info ** const devinfo_list, const char * const hubpath)
{
	HANDLE hubh;
	USB_NODE_INFORMATION nodeinfo;
	
	{
		char deviceName[4 + strlen(hubpath) + 1];
		sprintf(deviceName, "\\\\.\\%s", hubpath);
		hubh = CreateFile(deviceName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hubh == INVALID_HANDLE_VALUE)
			applogr(, LOG_ERR, "Error opening USB hub device %s for autodetect: %s", deviceName, bfg_strerror(GetLastError(), BST_SYSTEM));
	}
	
	ULONG nBytes;
	if (!DeviceIoControl(hubh, IOCTL_USB_GET_NODE_INFORMATION, &nodeinfo, sizeof(nodeinfo), &nodeinfo, sizeof(nodeinfo), &nBytes, NULL))
		applogfailinfor(, LOG_ERR, "ioctl", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	
	const int portcount = nodeinfo.u.HubInformation.HubDescriptor.bNumberOfPorts;
	for (int i = 1; i <= portcount; ++i)
		_vcom_devinfo_scan_windows__hubport(devinfo_list, hubh, i);
	
	CloseHandle(hubh);
}

static
char *windows_usb_get_root_hub_path(HANDLE hcntlrh)
{
	size_t namesz;
	ULONG rsz;
	
	{
		USB_ROOT_HUB_NAME pathinfo;
		if (!(DeviceIoControl(hcntlrh, IOCTL_USB_GET_ROOT_HUB_NAME, 0, 0, &pathinfo, sizeof(pathinfo), &rsz, NULL) && rsz >= sizeof(pathinfo)))
			applogfailinfor(NULL, LOG_ERR, "ioctl (1)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
		namesz = pathinfo.ActualLength;
	}
	
	const size_t bufsz = sizeof(USB_ROOT_HUB_NAME) + namesz;
	uint8_t buf[bufsz];
	USB_ROOT_HUB_NAME *hubpath = (USB_ROOT_HUB_NAME *)buf;
	
	if (!(DeviceIoControl(hcntlrh, IOCTL_USB_GET_ROOT_HUB_NAME, NULL, 0, hubpath, bufsz, &rsz, NULL) && rsz >= sizeof(*hubpath)))
		applogfailinfor(NULL, LOG_ERR, "ioctl (2)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	
	return ucs2tochar_dup(hubpath->RootHubName, hubpath->ActualLength);
}

static
void _vcom_devinfo_scan_windows__hcntlr(struct lowlevel_device_info ** const devinfo_list, HDEVINFO *devinfo, const int i)
{
	SP_DEVICE_INTERFACE_DATA devifacedata = {
		.cbSize = sizeof(devifacedata),
	};
	if (!SetupDiEnumDeviceInterfaces(*devinfo, 0, (LPGUID)&WIN_GUID_DEVINTERFACE_USB_HOST_CONTROLLER, i, &devifacedata))
		applogfailinfor(, LOG_ERR, "SetupDiEnumDeviceInterfaces", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	DWORD detailsz;
	if (!(!SetupDiGetDeviceInterfaceDetail(*devinfo, &devifacedata, NULL, 0, &detailsz, NULL) && GetLastError() == ERROR_INSUFFICIENT_BUFFER))
		applogfailinfor(, LOG_ERR, "SetupDiEnumDeviceInterfaceDetail (1)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	PSP_DEVICE_INTERFACE_DETAIL_DATA detail = alloca(detailsz);
	detail->cbSize = sizeof(*detail);
	if (!SetupDiGetDeviceInterfaceDetail(*devinfo, &devifacedata, detail, detailsz, &detailsz, NULL))
		applogfailinfor(, LOG_ERR, "SetupDiEnumDeviceInterfaceDetail (2)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	HANDLE hcntlrh = CreateFile(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hcntlrh == INVALID_HANDLE_VALUE)
		applogfailinfor(, LOG_DEBUG, "open USB host controller device", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	char * const hubpath = windows_usb_get_root_hub_path(hcntlrh);
	CloseHandle(hcntlrh);
	_vcom_devinfo_scan_windows__hub(devinfo_list, hubpath);
	free(hubpath);
}

static
void _vcom_devinfo_scan_windows(struct lowlevel_device_info ** const devinfo_list)
{
	HDEVINFO devinfo;
	devinfo = SetupDiGetClassDevs(&WIN_GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	SP_DEVINFO_DATA devinfodata = {
		.cbSize = sizeof(devinfodata),
	};
	
	for (int i = 0; SetupDiEnumDeviceInfo(devinfo, i, &devinfodata); ++i)
		_vcom_devinfo_scan_windows__hcntlr(devinfo_list, &devinfo, i);
	SetupDiDestroyDeviceInfoList(devinfo);
}

#endif


#ifdef WIN32
#define LOAD_SYM(sym)  do { \
	if (!(sym = dlsym(dll, #sym))) {  \
		applog(LOG_DEBUG, "Failed to load " #sym ", not using FTDI autodetect");  \
		goto out;  \
	}  \
} while(0)

static
char *_ftdi_get_string(char *buf, int i, DWORD flags)
{
	if (FT_OK != FT_ListDevices((PVOID)i, buf, FT_LIST_BY_INDEX | flags))
		return NULL;
	return buf[0] ? buf : NULL;
}

static
void _vcom_devinfo_scan_ftdi(struct lowlevel_device_info ** const devinfo_list)
{
	char devpath[] = "\\\\.\\COMnnnnn";
	char *devpathnum = &devpath[7];
	char **bufptrs;
	char *buf;
	char serial[64];
	struct lowlevel_device_info *devinfo;
	DWORD i;

	FT_STATUS ftStatus;
	DWORD numDevs;
	HMODULE dll = LoadLibrary("FTD2XX.DLL");
	if (!dll) {
		applog(LOG_DEBUG, "FTD2XX.DLL failed to load, not using FTDI autodetect");
		return;
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
		
		FT_HANDLE ftHandle;
		if (FT_OK != FT_Open(i, &ftHandle))
			continue;
		LONG lComPortNumber;
		ftStatus = FT_GetComPortNumber(ftHandle, &lComPortNumber);
		FT_Close(ftHandle);
		if (FT_OK != ftStatus || lComPortNumber < 0)
			continue;
		
		applog(LOG_ERR, "FT_GetComPortNumber(%p (%ld), %ld)", ftHandle, (long)i, (long)lComPortNumber);
		sprintf(devpathnum, "%d", (int)lComPortNumber);
		
		devinfo = _vcom_devinfo_findorcreate(devinfo_list, devpath);
		BFGINIT(devinfo->product, (bufptrs[i] && bufptrs[i][0]) ? strdup(bufptrs[i]) : NULL);
		BFGINIT(devinfo->serial, maybe_strdup(_ftdi_get_string(serial, i, FT_OPEN_BY_SERIAL_NUMBER)));
	}

out:
	dlclose(dll);
}
#endif

#ifdef WIN32
extern void _vcom_devinfo_scan_querydosdevice(struct lowlevel_device_info **);
#else
extern void _vcom_devinfo_scan_lsdev(struct lowlevel_device_info **);
#endif

bool _serial_autodetect_found_cb(struct lowlevel_device_info * const devinfo, void *userp)
{
	detectone_func_t detectone = userp;
	if (bfg_claim_any(NULL, devinfo->path, devinfo->devid))
	{
		applog(LOG_DEBUG, "%s (%s) is already claimed, skipping probe", devinfo->path, devinfo->devid);
		return false;
	}
	if (devinfo->lowl != &lowl_vcom)
	{
		if (devinfo->lowl != &lowl_usb)
			applog(LOG_WARNING, "Non-VCOM %s (%s) matched", devinfo->path, devinfo->devid);
		return false;
	}
	detectone_meta_info = (struct detectone_meta_info_t){
		.manufacturer = devinfo->manufacturer,
		.product = devinfo->product,
		.serial = devinfo->serial,
	};
	const bool rv = detectone(devinfo->path);
	clear_detectone_meta_info();
	return rv;
}

int _serial_autodetect(detectone_func_t detectone, ...)
{
	va_list needles;
	char *needles_array[0x10];
	int needlecount = 0;
	
	va_start(needles, detectone);
	while ( (needles_array[needlecount++] = va_arg(needles, void *)) )
	{}
	va_end(needles);
	
	return _lowlevel_detect(_serial_autodetect_found_cb, NULL, (const char **)needles_array, detectone);
}

static
struct lowlevel_device_info *vcom_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_hash = NULL;
	struct lowlevel_device_info *devinfo_list = NULL;
	struct lowlevel_device_info *devinfo, *tmp;
	
	// All 3 USB Strings available:
#ifndef WIN32
	_vcom_devinfo_scan_sysfs(&devinfo_hash);
#endif
#ifdef HAVE_WIN_DDKUSB
	_vcom_devinfo_scan_windows(&devinfo_hash);
#endif
#ifdef HAVE_LIBUDEV
	_vcom_devinfo_scan_udev(&devinfo_hash);
#endif
	// Missing Manufacturer:
#ifdef WIN32
	_vcom_devinfo_scan_ftdi(&devinfo_hash);
#endif
	// All blobbed together:
#ifndef WIN32
	_vcom_devinfo_scan_devserial(&devinfo_hash);
#endif
	// No info:
#ifdef WIN32
	_vcom_devinfo_scan_querydosdevice(&devinfo_hash);
#else
	_vcom_devinfo_scan_lsdev(&devinfo_hash);
#endif
	
	// Convert hash to simple list
	HASH_ITER(hh, devinfo_hash, devinfo, tmp)
	{
		LL_PREPEND(devinfo_list, devinfo);
	}
	
	return devinfo_list;
}


struct _device_claim {
	struct device_drv *drv;
	char *devpath;
	UT_hash_handle hh;
};

struct device_drv *bfg_claim_any(struct device_drv * const api, const char *verbose, const char * const devpath)
{
	static struct _device_claim *claims = NULL;
	struct _device_claim *c;
	
	HASH_FIND_STR(claims, devpath, c);
	if (c)
	{
		if (verbose && opt_debug)
		{
			char logbuf[LOGBUFSIZ];
			logbuf[0] = '\0';
			if (api)
				tailsprintf(logbuf, sizeof(logbuf), "%s device ", api->dname);
			if (verbose[0])
				tailsprintf(logbuf, sizeof(logbuf), "%s (%s)", verbose, devpath);
			else
				tailsprintf(logbuf, sizeof(logbuf), "%s", devpath);
			tailsprintf(logbuf, sizeof(logbuf), " already claimed by ");
			if (api)
				tailsprintf(logbuf, sizeof(logbuf), "other ");
			tailsprintf(logbuf, sizeof(logbuf), "driver: %s", c->drv->dname);
			_applog(LOG_DEBUG, logbuf);
		}
		return c->drv;
	}
	
	if (!api)
		return NULL;
	
	c = malloc(sizeof(*c));
	c->devpath = strdup(devpath);
	c->drv = api;
	HASH_ADD_KEYPTR(hh, claims, c->devpath, strlen(devpath), c);
	return NULL;
}

struct device_drv *bfg_claim_any2(struct device_drv * const api, const char * const verbose, const char * const llname, const char * const path)
{
	const size_t llnamesz = strlen(llname);
	const size_t pathsz = strlen(path);
	char devpath[llnamesz + 1 + pathsz + 1];
	memcpy(devpath, llname, llnamesz);
	devpath[llnamesz] = ':';
	memcpy(&devpath[llnamesz+1], path, pathsz + 1);
	return bfg_claim_any(api, verbose, devpath);
}

static
char *_vcom_unique_id(const char * const devpath)
{
#ifndef WIN32
	char *devs = malloc(6 + (sizeof(dev_t) * 2) + 1);
	{
		struct stat my_stat;
		if (stat(devpath, &my_stat))
			return NULL;
		memcpy(devs, "dev_t:", 6);
		bin2hex(&devs[6], &my_stat.st_rdev, sizeof(dev_t));
	}
#else
		char *p = strstr(devpath, "COM"), *p2;
		if (!p)
			return NULL;
		const int com = strtol(&p[3], &p2, 10);
		if (p2 == p)
			return NULL;
	char dummy;
	const int sz = snprintf(&dummy, 1, "%d", com);
	char *devs = malloc(4 + sz + 1);
	sprintf(devs, "com:%d", com);
#endif
	return devs;
}

struct device_drv *bfg_claim_serial(struct device_drv * const api, const bool verbose, const char * const devpath)
{
	char * const devs = _vcom_unique_id(devpath);
	if (!devs)
		return false;
	struct device_drv * const rv = bfg_claim_any(api, (verbose ? devpath : NULL), devs);
	free(devs);
	return rv;
}

char *bfg_make_devid_usb(const uint8_t usbbus, const uint8_t usbaddr)
{
	char * const devpath = malloc(12);
	sprintf(devpath, "usb:%03u:%03u", (unsigned)usbbus, (unsigned)usbaddr);
	return devpath;
}

struct device_drv *bfg_claim_usb(struct device_drv * const api, const bool verbose, const uint8_t usbbus, const uint8_t usbaddr)
{
	char * const devpath = bfg_make_devid_usb(usbbus, usbaddr);
	struct device_drv * const rv = bfg_claim_any(api, verbose ? "" : NULL, devpath);
	free(devpath);
	return rv;
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

int serial_close(const int fd)
{
#if defined(LOCK_EX) && defined(LOCK_NB) && defined(LOCK_UN)
	flock(fd, LOCK_UN);
#endif
	return close(fd);
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
		if (!fgets(buf, sizeof(buf), F))
			goto ihxerr;
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

struct lowlevel_driver lowl_vcom = {
	.dname = "vcom",
	.devinfo_scan = vcom_devinfo_scan,
};
