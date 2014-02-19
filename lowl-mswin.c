/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <objbase.h>
#include <rpc.h>
#include <setupapi.h>
#include <utlist.h>

#include "logging.h"
#include "lowlevel.h"
#include "lowl-mswin.h"
#include "util.h"

static
struct lowlevel_device_info *mswin_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_list = NULL, *info;
	
	HDEVINFO devinfo = SetupDiGetClassDevs(NULL, NULL, NULL, (DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE | DIGCF_PRESENT));
	if (INVALID_HANDLE_VALUE == devinfo)
		applogfailinfor(NULL, LOG_DEBUG, "SetupDiGetClassDevs", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
	
	SP_DEVINFO_DATA devinfodata = {
		.cbSize = sizeof(devinfodata),
	};
	SP_DEVICE_INTERFACE_DATA devifacedata = {
		.cbSize = sizeof(devifacedata),
	};
	for (int i = 0; SetupDiEnumDeviceInfo(devinfo, i, &devinfodata); ++i)
	{
		// FIXME: Figure out a way to get all GUIDs here
		if (!SetupDiEnumDeviceInterfaces(devinfo, &devinfodata, &WIN_GUID_DEVINTERFACE_MonarchKMDF, 0, &devifacedata))
		{
			applogfailinfo(LOG_DEBUG, "SetupDiEnumDeviceInterfaces", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
			continue;
		}
		DWORD detailsz;
		if (!(!SetupDiGetDeviceInterfaceDetail(devinfo, &devifacedata, NULL, 0, &detailsz, NULL) && GetLastError() == ERROR_INSUFFICIENT_BUFFER))
		{
			applogfailinfo(LOG_ERR, "SetupDiEnumDeviceInterfaceDetail (1)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
			continue;
		}
		PSP_DEVICE_INTERFACE_DETAIL_DATA detail = alloca(detailsz);
		detail->cbSize = sizeof(*detail);
		if (!SetupDiGetDeviceInterfaceDetail(devinfo, &devifacedata, detail, detailsz, &detailsz, NULL))
		{
			applogfailinfo(LOG_ERR, "SetupDiEnumDeviceInterfaceDetail (2)", "%s", bfg_strerror(GetLastError(), BST_SYSTEM));
			continue;
		}
		
		char *devid = malloc(6 + strlen(detail->DevicePath) + 1);
		sprintf(devid, "mswin:%s", detail->DevicePath);
		
		info = malloc(sizeof(struct lowlevel_device_info));
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_mswin,
			.devid = devid,
			.path = strdup(detail->DevicePath),
			.lowl_data = (void *)&WIN_GUID_DEVINTERFACE_MonarchKMDF,
		};
		LL_PREPEND(devinfo_list, info);
	}
	
	SetupDiDestroyDeviceInfoList(devinfo);
	
	return devinfo_list;
}

bool lowl_mswin_match_guid(const struct lowlevel_device_info * const info, const GUID * const guid)
{
	if (info->lowl != &lowl_mswin)
		return false;
	return IsEqualGUID(info->lowl_data, guid);
}

struct lowlevel_driver lowl_mswin = {
	.dname = "mswin",
	.devinfo_scan = mswin_devinfo_scan,
};
