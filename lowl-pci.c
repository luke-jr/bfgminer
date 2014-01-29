/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include <utlist.h>

#include "lowlevel.h"
#include "util.h"

static
struct lowlevel_device_info *pci_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_list = NULL, *info;
	struct dirent *de;
	char filename[0x100] = "/sys/bus/pci/devices", buf[0x10];
	DIR * const D = opendir(filename);
	if (!D)
		return 0;
	char * const p = &filename[strlen(filename)], *devid;
	const size_t psz = sizeof(filename) - (p - filename);
	uint32_t vid, pid;
	size_t d_name_len;
	while ( (de = readdir(D)) )
	{
		d_name_len = strlen(de->d_name);
		snprintf(p, psz, "/%s/vendor", de->d_name);
		if (!bfg_slurp_file(buf, sizeof(buf), filename))
			continue;
		vid = strtoll(buf, NULL, 0);
		snprintf(p, psz, "/%s/device", de->d_name);
		if (!bfg_slurp_file(buf, sizeof(buf), filename))
			continue;
		pid = strtoll(buf, NULL, 0);
		devid = malloc(4 + d_name_len + 1);
		sprintf(devid, "pci:%s", de->d_name);
		
		info = malloc(sizeof(struct lowlevel_device_info));
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_pci,
			.devid = devid,
			.path = strdup(de->d_name),
			.vid = vid,
			.pid = pid,
		};
		
		LL_PREPEND(devinfo_list, info);
	}
	closedir(D);
	return devinfo_list;
}

struct lowlevel_driver lowl_pci = {
	.dname = "pci",
	.devinfo_scan = pci_devinfo_scan,
};
