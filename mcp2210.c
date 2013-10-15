/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <hidapi/hidapi.h> /* FIXME */
#include <utlist.h>

#include "logging.h"
#include "lowlevel.h"

#define MCP2210_IDVENDOR   0x04d8
#define MCP2210_IDPRODUCT  0x00de

#ifdef WIN32
#define HID_API_EXPORT __declspec(dllexport)
#else
#define HID_API_EXPORT /* */
#endif
struct hid_device_info HID_API_EXPORT *(*dlsym_hid_enumerate)(unsigned short, unsigned short);
#define hid_enumerate dlsym_hid_enumerate
void HID_API_EXPORT (*dlsym_hid_free_enumeration)(struct hid_device_info *);
#define hid_free_enumeration dlsym_hid_free_enumeration

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
	void *dlh;
	
	dlh = dlopen(dlname, RTLD_NOW);
	if (!dlh)
	{
		applog(LOG_DEBUG, "%s: Couldn't load %s: %s", __func__, dlname, dlerror());
		return false;
	}
	
	LOAD_SYM(hid_enumerate);
	
	hid_enum = hid_enumerate(0, 0);
	if (!hid_enum)
	{
		applog(LOG_DEBUG, "%s: Loaded %s, but no devices enumerated; trying other libraries", __func__, dlname);
		goto fail;
	}
	
	LOAD_SYM(hid_free_enumeration);
	hid_free_enumeration(hid_enum);
	
	applog(LOG_DEBUG, "%s: Successfully loaded %s", __func__, dlname);
	
	return true;

fail:
	dlclose(dlh);
	return false;
}

static
bool hidapi_load_library()
{
	if (dlsym_hid_free_enumeration)
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
void mcp2210_devinfo_free(struct lowlevel_device_info * const info)
{
	free(info->lowl_data);
}

static
char *wcs2str_dup(wchar_t *ws)
{
	char tmp, *rv;
	int clen;
	
	clen = snprintf(&tmp, 1, "%ls", ws);
	++clen;
	rv = malloc(clen);
	snprintf(rv, clen, "%ls", ws);
	return rv;
}

static
struct lowlevel_device_info *mcp2210_devinfo_scan()
{
	if (!hidapi_load_library())
	{
		applog(LOG_DEBUG, "%s: Failed to load any hidapi library", __func__);
		return NULL;
	}
	
	struct hid_device_info *hid_enum, *hid_item;
	struct lowlevel_device_info *info, *devinfo_list = NULL;
	
	hid_enum = hid_enumerate(MCP2210_IDVENDOR, MCP2210_IDPRODUCT);
	if (!hid_enum)
	{
		applog(LOG_DEBUG, "%s: No MCP2210 devices found", __func__);
		return NULL;
	}
	
	LL_FOREACH(hid_enum, hid_item)
	{
		info = malloc(sizeof(struct lowlevel_device_info));
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_mcp2210,
			.lowl_data = strdup(hid_item->path),
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

struct lowlevel_driver lowl_mcp2210 = {
	.devinfo_scan = mcp2210_devinfo_scan,
	.devinfo_free = mcp2210_devinfo_free,
};
