/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifndef WIN32
#include <dlfcn.h>
typedef void *dlh_t;
#else
#include <winsock2.h>
#include <windows.h>
#define dlopen(lib, flags) LoadLibrary(lib)
#define dlsym(h, sym)  ((void*)GetProcAddress(h, sym))
#define dlerror()  "unknown"
#define dlclose(h)  FreeLibrary(h)
typedef HMODULE dlh_t;
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hidapi.h>
#include <utlist.h>

#include "logging.h"
#include "lowlevel.h"
#include "miner.h"

struct hid_device_info HID_API_EXPORT *(*dlsym_hid_enumerate)(unsigned short, unsigned short);
void HID_API_EXPORT (*dlsym_hid_free_enumeration)(struct hid_device_info *);
hid_device * HID_API_EXPORT (*dlsym_hid_open_path)(const char *);
void HID_API_EXPORT (*dlsym_hid_close)(hid_device *);
int HID_API_EXPORT (*dlsym_hid_read)(hid_device *, unsigned char *, size_t);
int HID_API_EXPORT (*dlsym_hid_write)(hid_device *, const unsigned char *, size_t);

#define LOAD_SYM(sym)  do { \
	if (!(dlsym_ ## sym = dlsym(dlh, #sym))) {  \
		applog(LOG_DEBUG, "%s: Failed to load %s in %s", __func__, #sym, dlname);  \
		goto fail;  \
	}  \
} while(0)

static
bool hidapi_try_lib(const char * const dlname)
{
	struct hid_device_info *hid_enum;
	dlh_t dlh;
	
	dlh = dlopen(dlname, RTLD_NOW);
	if (!dlh)
	{
		applog(LOG_DEBUG, "%s: Couldn't load %s: %s", __func__, dlname, dlerror());
		return false;
	}
	
	LOAD_SYM(hid_enumerate);
	LOAD_SYM(hid_free_enumeration);
	
	hid_enum = dlsym_hid_enumerate(0, 0);
	if (!hid_enum)
	{
		applog(LOG_DEBUG, "%s: Loaded %s, but no devices enumerated; trying other libraries", __func__, dlname);
		goto fail;
	}
	dlsym_hid_free_enumeration(hid_enum);
	
	LOAD_SYM(hid_open_path);
	LOAD_SYM(hid_close);
	LOAD_SYM(hid_read);
	LOAD_SYM(hid_write);
	
	applog(LOG_DEBUG, "%s: Successfully loaded %s", __func__, dlname);
	
	return true;

fail:
	dlclose(dlh);
	return false;
}

// #defines hid_* calls, so must be after library loader
#include "lowl-hid.h"

static
bool hidapi_load_library()
{
	if (dlsym_hid_write)
		return true;
	
	const char **p;
	char dlname[23] = "libhidapi";
	const char *dltry[] = {
		"",
		"-0",
		"-hidraw",
		"-libusb",
		NULL
	};
	for (p = &dltry[0]; *p; ++p)
	{
		sprintf(&dlname[9], "%s.%s", *p,
#ifdef WIN32
		        "dll"
#else
		        "so"
#endif
		);
		if (hidapi_try_lib(dlname))
			return true;
	}
	
	return false;
}

static
char *wcs2str_dup(wchar_t *ws)
{
	if (!(ws && ws[0]))
		return NULL;
	
	char *rv;
	int clen, i;
	
	clen = wcslen(ws);
	++clen;
	rv = malloc(clen);
	for (i = 0; i < clen; ++i)
		rv[i] = ws[i];
	
	return rv;
}

static
struct lowlevel_device_info *hid_devinfo_scan()
{
	if (!hidapi_load_library())
	{
		applog(LOG_DEBUG, "%s: Failed to load any hidapi library", __func__);
		return NULL;
	}
	
	struct hid_device_info *hid_enum, *hid_item;
	struct lowlevel_device_info *info, *devinfo_list = NULL;
	
	hid_enum = hid_enumerate(0, 0);
	if (!hid_enum)
	{
		applog(LOG_DEBUG, "%s: No HID devices found", __func__);
		return NULL;
	}
	
	LL_FOREACH(hid_enum, hid_item)
	{
		info = malloc(sizeof(struct lowlevel_device_info));
		char * const devid = malloc(4 + strlen(hid_item->path) + 1);
		sprintf(devid, "hid:%s", hid_item->path);
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_hid,
			.path = strdup(hid_item->path),
			.devid = devid,
			.vid = hid_item->vendor_id,
			.pid = hid_item->product_id,
			.manufacturer = wcs2str_dup(hid_item->manufacturer_string),
			.product = wcs2str_dup(hid_item->product_string),
			.serial  = wcs2str_dup(hid_item->serial_number),
		};
		LL_PREPEND(devinfo_list, info);

		applog(LOG_DEBUG, "%s: Found \"%s\" serial \"%s\"",
		       __func__, info->product, info->serial);
	}
	
	hid_free_enumeration(hid_enum);
	
	return devinfo_list;
}

struct lowlevel_driver lowl_hid = {
	.dname = "hid",
	.devinfo_scan = hid_devinfo_scan,
};
