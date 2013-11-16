/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utlist.h>

#include "logging.h"
#include "lowlevel.h"

static struct lowlevel_device_info *devinfo_list;

void lowlevel_devinfo_semicpy(struct lowlevel_device_info * const dst, const struct lowlevel_device_info * const src)
{
#define COPYSTR(key)  BFGINIT(dst->key, maybe_strdup(src->key))
	COPYSTR(manufacturer);
	COPYSTR(product);
	COPYSTR(serial);
	COPYSTR(path);
	COPYSTR(devid);
	BFGINIT(dst->vid, src->vid);
	BFGINIT(dst->pid, src->pid);
}

void lowlevel_devinfo_free(struct lowlevel_device_info * const info)
{
	if (info->lowl->devinfo_free)
		info->lowl->devinfo_free(info);
	free(info->manufacturer);
	free(info->product);
	free(info->serial);
	free(info->path);
	free(info->devid);
	free(info);
}

void lowlevel_scan_free()
{
	if (!devinfo_list)
		return;
	
	struct lowlevel_device_info *info, *tmp;
	
	LL_FOREACH_SAFE(devinfo_list, info, tmp)
	{
		LL_DELETE(devinfo_list, info);
		lowlevel_devinfo_free(info);
	}
}

struct lowlevel_device_info *lowlevel_scan()
{
	struct lowlevel_device_info *devinfo_mid_list;
	
	lowlevel_scan_free();
	
#ifdef HAVE_LIBUSB
	devinfo_mid_list = lowl_usb.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
#ifdef USE_X6500
	devinfo_mid_list = lowl_ft232r.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
#ifdef NEED_BFG_LOWL_HID
	devinfo_mid_list = lowl_hid.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
#ifdef USE_NANOFURY
	devinfo_mid_list = lowl_mcp2210.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
#ifdef HAVE_FPGAUTILS
	devinfo_mid_list = lowl_vcom.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
	LL_FOREACH(devinfo_list, devinfo_mid_list)
	{
		applog(LOG_DEBUG, "%s: Found %s device at %s (path=%s, vid=%04x, pid=%04x, manuf=%s, prod=%s, serial=%s)",
		       __func__,
		       devinfo_mid_list->lowl->dname,
		       devinfo_mid_list->devid,
		       devinfo_mid_list->path,
		       (unsigned)devinfo_mid_list->vid, (unsigned)devinfo_mid_list->pid,
		       devinfo_mid_list->manufacturer, devinfo_mid_list->product, devinfo_mid_list->serial);
	}
	
	return devinfo_list;
}

bool _lowlevel_match_product(const struct lowlevel_device_info * const info, const char ** const needles)
{
	if (!info->product)
		return false;
	for (int i = 0; needles[i]; ++i)
		if (!strstr(info->product, needles[i]))
			return false;
	return true;
}

bool lowlevel_match_id(const struct lowlevel_device_info * const info, const struct lowlevel_driver * const lowl, const int32_t vid, const int32_t pid)
{
	if (info->lowl != lowl)
		return false;
	if (vid != -1 && vid != info->vid)
		return false;
	if (pid != -1 && pid != info->pid)
		return false;
	return true;
}

#define DETECT_BEGIN  \
	struct lowlevel_device_info *info, *tmp;  \
	int found = 0;  \
	  \
	LL_FOREACH_SAFE(devinfo_list, info, tmp)  \
	{  \
// END DETECT_BEGIN

#define DETECT_END  \
		if (!cb(info, userp))  \
			continue;  \
		LL_DELETE(devinfo_list, info);  \
		++found;  \
	}  \
	return found;  \
// END DETECT_END

int _lowlevel_detect(lowl_found_devinfo_func_t cb, const char *serial, const char **product_needles, void * const userp)
{
	DETECT_BEGIN
		if (serial && ((!info->serial) || strcmp(serial, info->serial)))
			continue;
		if (product_needles[0] && !_lowlevel_match_product(info, product_needles))
			continue;
	DETECT_END
}

int lowlevel_detect_id(const lowl_found_devinfo_func_t cb, void * const userp, const struct lowlevel_driver * const lowl, const int32_t vid, const int32_t pid)
{
	DETECT_BEGIN
		if (!lowlevel_match_id(info, lowl, vid, pid))
			continue;
	DETECT_END
}
