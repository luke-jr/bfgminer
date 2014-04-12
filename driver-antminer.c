/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 Nate Woolls
 * Copyright 2013 Lingchao Xu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "miner.h"
#include "driver-icarus.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "deviceapi.h"
#include "logging.h"
#include "util.h"

#define ANTMINER_IO_SPEED 115200
#define ANTMINER_HASH_TIME 0.0000000004761

#define ANTMINER_STATUS_LEN 5

#define ANTMINER_COMMAND_PREFIX 128
#define ANTMINER_COMMAND_LED 1
#define ANTMINER_COMMAND_ON 1
#define ANTMINER_COMMAND_OFFSET 32

BFG_REGISTER_DRIVER(antminer_drv)

static
bool antminer_detect_one(const char *devpath)
{
	struct device_drv *drv = &antminer_drv;
	
	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");
	
	*info = (struct ICARUS_INFO){
		.baud = ANTMINER_IO_SPEED,
		.Hs = ANTMINER_HASH_TIME,
		.timing_mode = MODE_DEFAULT,
		.read_size = 5,
	};
	
	if (!icarus_detect_custom(devpath, drv, info))
	{
		free(info);
		return false;
	}
	
	info->read_count = 15;
	
	return true;
}

static
bool antminer_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, antminer_detect_one);
}

static
char *antminer_get_clock(struct cgpu_info *cgpu, char *replybuf)
{
	uint8_t rdreg_buf[4] = {0};
	unsigned char rebuf[ANTMINER_STATUS_LEN] = {0};
	
	struct timeval tv_now;
	
	rdreg_buf[0] = 4;
	rdreg_buf[0] |= 0x80;
	rdreg_buf[1] = 0;    //16-23
	rdreg_buf[2] = 0x04; // 8-15
	rdreg_buf[3] = crc5usb(rdreg_buf, 27);
	
	applog(LOG_DEBUG, "%"PRIpreprv": Get clock: %02x%02x%02x%02x", cgpu->proc_repr, rdreg_buf[0], rdreg_buf[1], rdreg_buf[2], rdreg_buf[3]);
	
	timer_set_now(&tv_now);
	int err = icarus_write(cgpu->device_fd, rdreg_buf, sizeof(rdreg_buf));
	
	if (err != 0)
	{
		sprintf(replybuf, "invalid send get clock: comms error (err=%d)", err);
		return replybuf;
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Get clock: OK", cgpu->proc_repr);
	
	memset(rebuf, 0, sizeof(rebuf));
	err = icarus_gets(rebuf, cgpu->device_fd, &tv_now, NULL, 10, ANTMINER_STATUS_LEN);
	
	// Timeout is ok - checking specifically for an error here
	if (err == ICA_GETS_ERROR)
	{
		sprintf(replybuf, "invalid recv get clock: comms error (err=%d)", err);
		return replybuf;
	}
		
	applog(LOG_DEBUG, "%"PRIpreprv": Get clock: %02x%02x%02x%02x%02x", cgpu->proc_repr, rebuf[0], rebuf[1], rebuf[2], rebuf[3], rebuf[4]);
	
	return NULL;
}

static
char *antminer_set_clock(struct cgpu_info *cgpu, char *setting, char *replybuf)
{
	if (!setting || !*setting)
		return "missing clock setting";
	
	// For now we only allow hex values that use BITMAINtech's lookup table
	// This means values should be prefixed with an x so that later we can
	// accept and distinguish decimal values
	if (setting[0] != 'x')
	{
		sprintf(replybuf, "invalid clock: '%s' data must be prefixed with an x", setting);
		return replybuf;
	}
	
	//remove leading character
	char *hex_setting = setting + 1;

	uint8_t reg_data[4] = {0};
	
	if (!hex2bin(reg_data, hex_setting, strlen(hex_setting) / 2))
	{
		sprintf(replybuf, "invalid clock: '%s' data must be a hexadecimal value", hex_setting);
		return replybuf;
	}
	
	uint8_t cmd_buf[4] = {0};
	
	cmd_buf[0] = 2;
	cmd_buf[0] |= 0x80;
	cmd_buf[1] = reg_data[0]; //16-23
	cmd_buf[2] = reg_data[1]; // 8-15
	cmd_buf[3] = crc5usb(cmd_buf, 27);
	
	applog(LOG_DEBUG, "%"PRIpreprv": Set clock: %02x%02x%02x%02x", cgpu->proc_repr, cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
	
	int err = icarus_write(cgpu->device_fd, cmd_buf, sizeof(cmd_buf));
		
	if (err != 0)
	{
		sprintf(replybuf, "invalid send clock: '%s' comms error (err=%d)", setting, err);
		return replybuf;
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Set clock: OK", cgpu->proc_repr);
	
	// This is confirmed required in order for the clock change to "take"
	cgsleep_ms(500);
		
	return antminer_get_clock(cgpu, replybuf);
}

static
char *antminer_set_device(struct cgpu_info *cgpu, char *option, char *setting, char *replybuf)
{		
	if (strcasecmp(option, "clock") == 0)
	{
		return antminer_set_clock(cgpu, setting, replybuf);
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static
void antminer_flash_led(const struct cgpu_info *antminer)
{
	const int offset = ANTMINER_COMMAND_OFFSET;

	uint8_t cmd_buf[4 + offset];
	memset(cmd_buf, 0, sizeof(cmd_buf));

	cmd_buf[offset + 0] = ANTMINER_COMMAND_PREFIX;
	cmd_buf[offset + 1] = ANTMINER_COMMAND_LED;
	cmd_buf[offset + 2] = ANTMINER_COMMAND_ON;
	cmd_buf[offset + 3] = crc5usb(cmd_buf, sizeof(cmd_buf));

	const int fd = antminer->device_fd;
	icarus_write(fd, (char *)(&cmd_buf), sizeof(cmd_buf));
}

static
bool antminer_identify(struct cgpu_info *antminer)
{
	for (int i = 0; i < 10; i++)
	{
		antminer_flash_led(antminer);
		cgsleep_ms(250);
	}

	return true;
}

static
void antminer_drv_init()
{
	antminer_drv = icarus_drv;
	antminer_drv.dname = "antminer";
	antminer_drv.name = "AMU";
	antminer_drv.lowl_probe = antminer_lowl_probe;
	antminer_drv.set_device = antminer_set_device,
	antminer_drv.identify_device = antminer_identify;
	++antminer_drv.probe_priority;
}

struct device_drv antminer_drv = {
	.drv_init = antminer_drv_init,
};
