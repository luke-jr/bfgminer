/*
 * Copyright 2013-2015 Luke Dashjr
 * Copyright 2013-2014 Nate Woolls
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
// ANTMINER_HASH_TIME is for U1/U2 only
#define ANTMINER_HASH_TIME 0.0000000004761

#define ANTMINER_STATUS_LEN 5

#define ANTMINER_COMMAND_PREFIX 128
#define ANTMINER_COMMAND_LED 1
#define ANTMINER_COMMAND_ON 1
#define ANTMINER_COMMAND_OFFSET 32

BFG_REGISTER_DRIVER(antminer_drv)
BFG_REGISTER_DRIVER(compac_drv)
static
const struct bfg_set_device_definition antminer_set_device_funcs[];

static
bool antminer_detect_one_with_drv(const char * const devpath, struct device_drv * const drv)
{
	struct ICARUS_INFO *info = calloc(1, sizeof(struct ICARUS_INFO));
	if (unlikely(!info))
		quit(1, "Failed to malloc ICARUS_INFO");
	
	*info = (struct ICARUS_INFO){
		.baud = ANTMINER_IO_SPEED,
		.Hs = ANTMINER_HASH_TIME,
		.timing_mode = MODE_LONG,
		.do_icarus_timing = true,
		.read_size = 5,
		.reopen_mode = IRM_NEVER,
	};
	
	struct cgpu_info * const dev = icarus_detect_custom(devpath, drv, info);
	if (!dev)
	{
		free(info);
		return false;
	}
	
	dev->set_device_funcs = antminer_set_device_funcs;
	info->read_timeout_ms = 75;
	
	return true;
}

static bool antminer_detect_one(const char * const devpath)
{
	return antminer_detect_one_with_drv(devpath, &antminer_drv);
}

static
bool antminer_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_vcom, "Antminer");
}

static
bool antminer_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, antminer_detect_one);
}

// Not used for anything, and needs to read a result for every chip
#if 0
static
char *antminer_get_clock(struct cgpu_info *cgpu, char *replybuf)
{
	uint8_t rdreg_buf[4] = {0};
	unsigned char rebuf[ANTMINER_STATUS_LEN] = {0};
	
	struct timeval tv_now;
	struct timeval tv_timeout, tv_finish;
	
	rdreg_buf[0] = 4;
	rdreg_buf[0] |= 0x80;
	rdreg_buf[1] = 0;    //16-23
	rdreg_buf[2] = 0x04; // 8-15
	rdreg_buf[3] = crc5usb(rdreg_buf, 27);
	
	applog(LOG_DEBUG, "%"PRIpreprv": Get clock: %02x%02x%02x%02x", cgpu->proc_repr, rdreg_buf[0], rdreg_buf[1], rdreg_buf[2], rdreg_buf[3]);
	
	timer_set_now(&tv_now);
	int err = icarus_write(cgpu->proc_repr, cgpu->device_fd, rdreg_buf, sizeof(rdreg_buf));
	
	if (err != 0)
	{
		sprintf(replybuf, "invalid send get clock: comms error (err=%d)", err);
		return replybuf;
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Get clock: OK", cgpu->proc_repr);
	
	memset(rebuf, 0, sizeof(rebuf));
	timer_set_delay(&tv_timeout, &tv_now, 1000000);
	err = icarus_read(cgpu->proc_repr, rebuf, cgpu->device_fd, &tv_finish, NULL, &tv_timeout, &tv_now, ANTMINER_STATUS_LEN);
	
	// Timeout is ok - checking specifically for an error here
	if (err == ICA_GETS_ERROR)
	{
		sprintf(replybuf, "invalid recv get clock: comms error (err=%d)", err);
		return replybuf;
	}
		
	applog(LOG_DEBUG, "%"PRIpreprv": Get clock: %02x%02x%02x%02x%02x", cgpu->proc_repr, rebuf[0], rebuf[1], rebuf[2], rebuf[3], rebuf[4]);
	
	return NULL;
}
#endif

static
const char *antminer_set_clock(struct cgpu_info * const cgpu, const char * const optname, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const out_success)
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
	const char * const hex_setting = &setting[1];

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
	
	int err = icarus_write(cgpu->proc_repr, cgpu->device_fd, cmd_buf, sizeof(cmd_buf));
		
	if (err != 0)
	{
		sprintf(replybuf, "invalid send clock: '%s' comms error (err=%d)", setting, err);
		return replybuf;
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Set clock: OK", cgpu->proc_repr);
	
	// This is confirmed required in order for the clock change to "take"
	cgsleep_ms(500);
	
	return NULL;
}

static
const char *antminer_set_voltage(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	if (!(newvalue && *newvalue))
		return "Missing voltage value";
	
	// For now we only allow hex values that use BITMAINtech's lookup table
	// This means values should be prefixed with an x so that later we can
	// accept and distinguish decimal values
	if (newvalue[0] != 'x' || strlen(newvalue) != 4)
invalid_voltage:
		return "Only raw voltage configurations are currently supported using 'x' followed by 3 hexadecimal digits";
	
	char voltagecfg_hex[5];
	voltagecfg_hex[0] = '0';
	memcpy(&voltagecfg_hex[1], &newvalue[1], 3);
	voltagecfg_hex[4] = '\0';
	
	uint8_t cmd[4];
	if (!hex2bin(&cmd[1], voltagecfg_hex, 2))
		goto invalid_voltage;
	cmd[0] = 0xaa;
	cmd[1] |= 0xb0;
	cmd[3] = 0;
	cmd[3] = crc5usb(cmd, (4 * 8) - 5);
	cmd[3] |= 0xc0;
	
	if (opt_debug)
	{
		char hex[(4 * 2) + 1];
		bin2hex(hex, cmd, 4);
		applog(LOG_DEBUG, "%"PRIpreprv": Set voltage: %s", proc->proc_repr, hex);
	}
	
	const int err = icarus_write(proc->proc_repr, proc->device_fd, cmd, sizeof(cmd));
	
	if (err)
	{
		sprintf(replybuf, "Error sending set voltage (err=%d)", err);
		return replybuf;
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Set voltage: OK", proc->proc_repr);
	
	return NULL;
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
	icarus_write(antminer->proc_repr, fd, (char *)(&cmd_buf), sizeof(cmd_buf));
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
const struct bfg_set_device_definition antminer_set_device_funcs[] = {
	{"baud"         , icarus_set_baud         , "serial baud rate"},
	{"work_division", icarus_set_work_division, "number of pieces work is split into"},
	{"reopen"       , icarus_set_reopen       , "how often to reopen device: never, timeout, cycle, (or now for a one-shot reopen)"},
	{"timing"       , icarus_set_timing       , "timing of device; see README.FPGA"},
	{"clock", antminer_set_clock, "clock frequency"},
	{"voltage", antminer_set_voltage, "voltage ('x' followed by 3 digit hex code)"},
	{NULL},
};

static
bool compac_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_vcom, "Compac", "Bitcoin");
}

static bool compac_detect_one(const char * const devpath)
{
	return antminer_detect_one_with_drv(devpath, &compac_drv);
}

static
bool compac_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, compac_detect_one);
}

static
void antminer_drv_init()
{
	antminer_drv = icarus_drv;
	antminer_drv.dname = "antminer";
	antminer_drv.name = "AMU";
	antminer_drv.lowl_match = antminer_lowl_match;
	antminer_drv.lowl_probe = antminer_lowl_probe;
	antminer_drv.identify_device = antminer_identify;
	++antminer_drv.probe_priority;
	
	compac_drv = antminer_drv;
	compac_drv.name = "CBM";
	compac_drv.lowl_match = compac_lowl_match;
	compac_drv.lowl_probe = compac_lowl_probe;
	++compac_drv.probe_priority;
}

struct device_drv antminer_drv = {
	.drv_init = antminer_drv_init,
};

struct device_drv compac_drv = {
	.drv_init = antminer_drv_init,
};
