/*
 * Copyright 2012-2014 Luke Dashjr
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

#include "compat.h"
#include "logging.h"
#include "lowlevel.h"
#include "miner.h"

static struct lowlevel_device_info *devinfo_list;

#if defined(HAVE_LIBUSB) || defined(NEED_BFG_LOWL_HID)
char *bfg_make_devid_usb(const uint8_t usbbus, const uint8_t usbaddr)
{
	char * const devpath = malloc(12);
	sprintf(devpath, "usb:%03u:%03u", (unsigned)usbbus, (unsigned)usbaddr);
	return devpath;
}
#endif

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
	if (info->ref--)
		return;
	if (info->lowl->devinfo_free)
		info->lowl->devinfo_free(info);
	free(info->manufacturer);
	free(info->product);
	free(info->serial);
	free(info->path);
	free(info->devid);
	free(info);
}

struct lowlevel_device_info *lowlevel_ref(const struct lowlevel_device_info * const cinfo)
{
	struct lowlevel_device_info * const info = (void*)cinfo;
	++info->ref;
	return info;
}

void lowlevel_scan_free()
{
	if (!devinfo_list)
		return;
	
	struct lowlevel_device_info *info, *tmp;
	struct lowlevel_device_info *info2, *tmp2;
	
	LL_FOREACH_SAFE(devinfo_list, info, tmp)
	{
		LL_DELETE(devinfo_list, info);
		LL_FOREACH_SAFE2(info, info2, tmp2, same_devid_next)
		{
			LL_DELETE2(info, info2, same_devid_next);
			lowlevel_devinfo_free(info2);
		}
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
	
#ifdef NEED_BFG_LOWL_FTDI
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
	
#ifdef NEED_BFG_LOWL_MSWIN
	devinfo_mid_list = lowl_mswin.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
#ifdef NEED_BFG_LOWL_PCI
	devinfo_mid_list = lowl_pci.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
#ifdef NEED_BFG_LOWL_VCOM
	devinfo_mid_list = lowl_vcom.devinfo_scan();
	LL_CONCAT(devinfo_list, devinfo_mid_list);
#endif
	
	struct lowlevel_device_info *devinfo_same_prev_ht = NULL, *devinfo_same_list;
	LL_FOREACH(devinfo_list, devinfo_mid_list)
	{
		// Check for devid overlapping, and build a secondary linked list for them, only including the devid in the main list once (high level to low level)
		HASH_FIND_STR(devinfo_same_prev_ht, devinfo_mid_list->devid, devinfo_same_list);
		if (devinfo_same_list)
		{
			HASH_DEL(devinfo_same_prev_ht, devinfo_same_list);
			LL_DELETE(devinfo_list, devinfo_same_list);
		}
		LL_PREPEND2(devinfo_same_list, devinfo_mid_list, same_devid_next);
		HASH_ADD_KEYPTR(hh, devinfo_same_prev_ht, devinfo_mid_list->devid, strlen(devinfo_mid_list->devid), devinfo_same_list);
		
		applog(LOG_DEBUG, "%s: Found %s device at %s (path=%s, vid=%04x, pid=%04x, manuf=%s, prod=%s, serial=%s)",
		       __func__,
		       devinfo_mid_list->lowl->dname,
		       devinfo_mid_list->devid,
		       devinfo_mid_list->path,
		       (unsigned)devinfo_mid_list->vid, (unsigned)devinfo_mid_list->pid,
		       devinfo_mid_list->manufacturer, devinfo_mid_list->product, devinfo_mid_list->serial);
	}
	HASH_CLEAR(hh, devinfo_same_prev_ht);
	
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


struct _device_claim {
	struct device_drv *drv;
	char *devpath;
	UT_hash_handle hh;
};

struct device_drv *bfg_claim_any(struct device_drv * const api, const char *verbose, const char * const devpath)
{
	static struct _device_claim *claims = NULL;
	static pthread_mutex_t claims_lock = PTHREAD_MUTEX_INITIALIZER;
	struct _device_claim *c;
	
	mutex_lock(&claims_lock);
	HASH_FIND_STR(claims, devpath, c);
	if (c)
	{
		mutex_unlock(&claims_lock);
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
	{
		mutex_unlock(&claims_lock);
		return NULL;
	}
	
	c = malloc(sizeof(*c));
	c->devpath = strdup(devpath);
	c->drv = api;
	HASH_ADD_KEYPTR(hh, claims, c->devpath, strlen(devpath), c);
	mutex_unlock(&claims_lock);
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
