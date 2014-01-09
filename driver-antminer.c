/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2013 Nate Woolls
 * Copyright 2013 Lingchao Xu <lingchao.xu@bitmaintech.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>

#include "miner.h"
#include "icarus-common.h"
#include "lowlevel.h"
#include "lowl-vcom.h"

#define ANTMINER_IO_SPEED 115200
#define ANTMINER_HASH_TIME 0.0000000029761

#define ANTMINER_STATUS_LEN 8192

BFG_REGISTER_DRIVER(antminer_drv)

unsigned char antminer_crc5(unsigned char *ptr, unsigned char len)
{
    unsigned char i, j, k;
    unsigned char crc = 0x1f;

    unsigned char crcin[5] = {1, 1, 1, 1, 1};
    unsigned char crcout[5] = {1, 1, 1, 1, 1};
    unsigned char din = 0;

    j = 0x80;
    k = 0;
    for (i = 0; i < len; i++)
    {
    	if (*ptr & j) {
    		din = 1;
    	} else {
    		din = 0;
    	}
    	crcout[0] = crcin[4] ^ din;
    	crcout[1] = crcin[0];
    	crcout[2] = crcin[1] ^ crcin[4] ^ din;
    	crcout[3] = crcin[2];
    	crcout[4] = crcin[3];

        j = j >> 1;
        k++;
        if (k == 8)
        {
            j = 0x80;
            k = 0;
            ptr++;
        }
        memcpy(crcin, crcout, 5);
    }
    crc = 0;
    if(crcin[4]) {
    	crc |= 0x10;
    }
    if(crcin[3]) {
    	crc |= 0x08;
    }
    if(crcin[2]) {
    	crc |= 0x04;
    }
    if(crcin[1]) {
    	crc |= 0x02;
    }
    if(crcin[0]) {
    	crc |= 0x01;
    }
    return crc;
}

static bool antminer_detect_one(const char *devpath)
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
		.scans_full_nonce = true,
	};
	
	if (!icarus_detect_custom(devpath, drv, info)) {
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
char *antminer_get_frequency(struct cgpu_info *cgpu, char *setting, char *replybuf)
{	
	unsigned char rdreg_buf[4] = {0};

	applog(LOG_DEBUG, "!!!test22222!!!");
	
	rdreg_buf[0] = 4;
	rdreg_buf[0] |= 0x80;
	rdreg_buf[1] = 0; //16-23
	rdreg_buf[2] = 0x04;  //8-15
	rdreg_buf[3] = antminer_crc5(rdreg_buf, 27);	

	unsigned char rebuf[ANTMINER_STATUS_LEN] = {0};
	
	struct timeval tv_now;
	//reading out any junk?? a guess...
	memset(rebuf, 0, sizeof(rebuf));
	int err = icarus_gets(rebuf, cgpu->device_fd, &tv_now, NULL, 10, ANTMINER_STATUS_LEN);
		
	if (err != 0) {
		applog(LOG_DEBUG, "!!!error22222!!!");
		sprintf(replybuf, "invalid get freq: '%s' comms1 error", setting);
		return replybuf;
	}
	
	//write the request for status
	err = icarus_write(cgpu->device_fd, rdreg_buf, sizeof(rdreg_buf));
		
	if (err != 0) {
		applog(LOG_DEBUG, "!!!error22222!!!");
		sprintf(replybuf, "invalid get freq: '%s' comms2 error", setting);
		return replybuf;
	}
	
	memset(rebuf, 0, sizeof(rebuf));
	err = icarus_gets(rebuf, cgpu->device_fd, &tv_now, NULL, 10, ANTMINER_STATUS_LEN);
		
	if (err != 0) {
		applog(LOG_DEBUG, "!!!error22222!!!");
		sprintf(replybuf, "invalid get freq: '%s' comms3 error", setting);
		return replybuf;
	}
	
	char msg[10240] = {0};
	for (int i = 0; i < ANTMINER_STATUS_LEN; i++) {
		if (rebuf[i] != 0)
			sprintf(msg + i * 2, "%02x", rebuf[i]);
		else
			break;		
	}
	applog(LOG_DEBUG, "!!!YES %s!!!", msg);
	sprintf(replybuf, "valid get freq: '%s'", msg);
	return replybuf;
}

static
char *antminer_set_frequency(struct cgpu_info *cgpu, char *setting, char *replybuf)
{
	unsigned char reg_data[4] = {0};
	unsigned char cmd_buf[4] = {0};

	applog(LOG_DEBUG, "!!!test!!!");
	
	memset(reg_data, 0, 4);
	if (!hex2bin(reg_data, setting, strlen(setting) / 2)) {
		applog(LOG_DEBUG, "!!!error!!!");
		sprintf(replybuf, "invalid set freq: '%s' hex2bin error", setting);
		return replybuf;
	}
	cmd_buf[0] = 2;
	cmd_buf[0] |= 0x80;
	cmd_buf[1] = reg_data[0]; //16-23
	cmd_buf[2] = reg_data[1];  //8-15
	cmd_buf[3] = antminer_crc5(cmd_buf, 27);

	cgsleep_ms(500);

	applog(LOG_DEBUG, "Send frequency %02x%02x%02x%02x", cmd_buf[0], cmd_buf[1], cmd_buf[2], cmd_buf[3]);
	
	int err = icarus_write(cgpu->device_fd, cmd_buf, sizeof(cmd_buf));
		
	if (err != 0) {
		applog(LOG_DEBUG, "!!!error!!!");
		sprintf(replybuf, "invalid set freq: '%s' comms error", setting);
		return replybuf;
	}

	cgsleep_ms(500);
	
	sprintf(replybuf, antminer_get_frequency(cgpu, setting, replybuf));
	return replybuf;
}

static
char *antminer_set_device(struct cgpu_info *cgpu, char *option, char *setting, char *replybuf)
{		
	if (strcasecmp(option, "freq") == 0) {
		
		if (!setting || !*setting)
			return "missing freq setting";
		
		if (strlen(setting) > 8 || strlen(setting) % 2 != 0 || strlen(setting) / 2 == 0) {
			sprintf(replybuf, "invalid freq: '%s' data must be hex", setting);
			return replybuf;
		}
                		
		return antminer_set_frequency(cgpu, setting, replybuf);
	}

	sprintf(replybuf, "Unknown option: %s", option);
	return replybuf;
}

static void antminer_drv_init()
{
	antminer_drv = icarus_drv;
	antminer_drv.dname = "antminer";
	antminer_drv.name = "ANT";
	antminer_drv.lowl_probe = antminer_lowl_probe;
	antminer_drv.set_device = antminer_set_device,
	++antminer_drv.probe_priority;
}

struct device_drv antminer_drv = {
	.drv_init = antminer_drv_init,
};