/*
 * Copyright 2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_LINUX_I2C_DEV_USER_H
#include <linux/i2c-dev-user.h>
#else
#include <linux/i2c-dev.h>
#endif

#include "deviceapi.h"

struct device_drv knc_drv;

static
bool knc_detect_one(const char *devpath)
{
	struct cgpu_info *cgpu;
	int i;
	const char * const i2cpath = "/dev/i2c-2";
	const int fd = open(i2cpath, O_RDWR);
	char *leftover = NULL;
	const int i2cslave = strtol(devpath, &leftover, 0);
	uint8_t buf[0x20];
	
	if (leftover && leftover[0])
		return false;
	
	if (unlikely(fd == -1))
	{
		applog(LOG_DEBUG, "%s: Failed to open %s", knc_drv.dname, i2cpath);
		return false;
	}
	
	if (ioctl(fd, I2C_SLAVE, i2cslave))
	{
		close(fd);
		applog(LOG_DEBUG, "%s: Failed to select i2c slave 0x%x",
		       knc_drv.dname, i2cslave);
		return false;
	}
	
	i = i2c_smbus_read_i2c_block_data(fd, 0, 0x20, buf);
	close(fd);
	if (-1 == i)
	{
		applog(LOG_DEBUG, "%s: 0x%x: Failed to read i2c block data",
		       knc_drv.dname, i2cslave);
		return false;
	}
	for (i = 0; ; ++i)
	{
		if (buf[i] == 3)
			break;
		if (i == 0x1f)
			return false;
	}
	
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &knc_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.procs = 192,
		.threads = 1,
	};
	return add_cgpu(cgpu);
}

static int knc_detect_auto(void)
{
	const int first = 0x20, last = 0x26;
	char devpath[4];
	int found = 0, i;
	
	for (i = first; i <= last; ++i)
	{
		sprintf(devpath, "%d", i);
		if (knc_detect_one(devpath))
			++found;
	}
	
	return found;
}

static void knc_detect(void)
{
	generic_detect(&knc_drv, knc_detect_one, knc_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

struct device_drv knc_drv = {
	.dname = "knc",
	.name = "KNC",
	.drv_detect = knc_detect,
};
