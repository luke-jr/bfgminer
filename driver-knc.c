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
bool _check_value(const char * const devpath, const int i, const int32_t r, const uint8_t reg, const int32_t expected)
{
	if (expected == r)
		return true;
	
	if (r == -1)
		applog(LOG_DEBUG, "%s: %s: Error when reading slave %d register 0x%2x: %s",
		       knc_drv.dname, devpath, i, reg, bfg_strerror(errno, BST_ERRNO));
	else
		applog(LOG_DEBUG, "%s: %s: Slave %d register 0x%2x is 0x%x, not 0x%x as expected",
		       knc_drv.dname, devpath, i, reg, r, expected);
	
	return false;
}

static
bool knc_detect_one(const char *devpath)
{
	struct cgpu_info *cgpu;
	const int first_slave = 16, last_slave = 23;
	int procs;
	int i;
	int32_t r;
	const int fd = open(devpath, O_RDWR);
	
	if (unlikely(fd == -1))
	{
		applog(LOG_DEBUG, "%s: Failed to open %s", knc_drv.dname, devpath);
		return false;
	}
	
	for (i = first_slave; i <= last_slave; ++i)
	{
		if (ioctl(fd, I2C_SLAVE, i))
		{
			applog(LOG_DEBUG, "%s: %s: Failed to select I2C_SLAVE %d",
			       knc_drv.dname, devpath, i);
			continue;
		}
		r = i2c_smbus_read_byte_data(fd, 0x98);
		if (!_check_value(devpath, i, r, 0x98, 0x11))
			continue;
		r = i2c_smbus_read_word_data(fd, 0xd0);
		if (!_check_value(devpath, i, r, 0xd0, 0x10))
			continue;
		
		applog(LOG_DEBUG, "%s: %s: Found i2c slave %d",
		       knc_drv.dname, devpath, i);
		
		procs += 6;
	}
	
	if (!procs)
		return false;
	
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &knc_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.procs = procs,
		.threads = 1,
	};
	return add_cgpu(cgpu);
}

static int knc_detect_auto(void)
{
	const int first_bus = 3, last_bus = 8;
	char devpath[] = "/dev/i2c-N";
	int found = 0, i;
	
	for (i = first_bus; i <= last_bus; ++i)
	{
		devpath[9] = '0' + i;
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
