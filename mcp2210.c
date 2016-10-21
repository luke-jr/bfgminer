/*
 * Copyright 2012-2013 Luke Dashjr
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

#include <utlist.h>

#include "logging.h"
#include "lowlevel.h"
#include "lowl-hid.h"
#include "miner.h"

#include "mcp2210.h"

#define MCP2210_IDVENDOR   0x04d8
#define MCP2210_IDPRODUCT  0x00de

static
bool _mcp2210_devinfo_scan_cb(struct lowlevel_device_info * const usbinfo, void * const userp)
{
	struct lowlevel_device_info **devinfo_list_p = userp, *info;
	
	info = malloc(sizeof(*info));
	*info = (struct lowlevel_device_info){
		.lowl = &lowl_mcp2210,
	};
	lowlevel_devinfo_semicpy(info, usbinfo);
	LL_PREPEND(*devinfo_list_p, info);
	
	// Never *consume* the lowl_usb entry - especially since this is during the scan!
	return false;
}

static
struct lowlevel_device_info *mcp2210_devinfo_scan()
{
	struct lowlevel_device_info *devinfo_list = NULL;
	
	lowlevel_detect_id(_mcp2210_devinfo_scan_cb, &devinfo_list, &lowl_hid, MCP2210_IDVENDOR, MCP2210_IDPRODUCT);
	
	return devinfo_list;
}

struct mcp2210_device {
	hid_device *hid;
	
	// http://ww1.microchip.com/downloads/en/DeviceDoc/22288A.pdf pg 34
	uint8_t cfg_spi[0x11];
	// http://ww1.microchip.com/downloads/en/DeviceDoc/22288A.pdf pg 40
	uint8_t cfg_gpio[0xf];
};

static
bool mcp2210_io(hid_device * const hid, uint8_t * const cmd, uint8_t * const buf)
{
	char hexcmd[(0x41 * 2) + 1];
	if (opt_dev_protocol)
		bin2hex(hexcmd, cmd, 0x41);
	const bool rv = likely(
		0x41 == hid_write(hid, cmd, 0x41) &&
		64 == hid_read(hid, buf, 64)
	);
	if (opt_dev_protocol)
	{
		char hexbuf[(0x40 * 2) + 1];
		bin2hex(hexbuf, buf, 0x40);
		applog(LOG_DEBUG, "mcp2210_io(%p, %s, %s)", hid, hexcmd, hexbuf);
	}
	return rv;
}

static
bool mcp2210_get_configs(struct mcp2210_device * const h)
{
	hid_device * const hid = h->hid;
	uint8_t cmd[0x41] = {0,0x41}, buf[0x40];
	
	if (!mcp2210_io(hid, cmd, buf))
	{
		applog(LOG_ERR, "%s: Failed to get current %s config", __func__, "SPI");
		return false;
	}
	memcpy(h->cfg_spi, &buf[4], sizeof(h->cfg_spi));
	
	cmd[1] = 0x20;
	if (!mcp2210_io(hid, cmd, buf))
	{
		applog(LOG_ERR, "%s: Failed to get current %s config", __func__, "GPIO");
		return false;
	}
	memcpy(h->cfg_gpio, &buf[4], sizeof(h->cfg_gpio));
	
	return true;
}

struct mcp2210_device *mcp2210_open(const struct lowlevel_device_info * const info)
{
	struct mcp2210_device *h;
	char * const path = info->path;
	hid_device * const hid = hid_open_path(path);
	
	if (unlikely(!hid))
		return NULL;
	
	h = malloc(sizeof(*h));
	h->hid = hid;
	
	if (!mcp2210_get_configs(h))
		goto fail;
	
	return h;

fail:
	free(h);
	return NULL;
}

void mcp2210_close(struct mcp2210_device * const h)
{
	hid_close(h->hid);
	free(h);
}

static
bool mcp2210_set_cfg_spi(struct mcp2210_device * const h)
{
	hid_device * const hid = h->hid;
	uint8_t cmd[0x41] = {0,0x40}, buf[0x40];
	memcpy(&cmd[5], h->cfg_spi, sizeof(h->cfg_spi));
	if (!mcp2210_io(hid, cmd, buf))
	{
		applog(LOG_ERR, "%s: Failed to set current %s config", __func__, "SPI");
		return false;
	}
	
	if (buf[1] != 0)
	{
		applog(LOG_ERR, "%s: Error setting current %s config (%d)", __func__, "SPI", buf[1]);
		return false;
	}
	
	return true;
}

bool mcp2210_configure_spi(struct mcp2210_device * const h, const uint32_t bitrate, const uint16_t idlechipsel, const uint16_t activechipsel, const uint16_t chipseltodatadelay, const uint16_t lastbytetocsdelay, const uint16_t midbytedelay)
{
	uint8_t * const cfg = h->cfg_spi;
	
	cfg[0] = (bitrate >> 0x00) & 0xff;
	cfg[1] = (bitrate >> 0x08) & 0xff;
	cfg[2] = (bitrate >> 0x10) & 0xff;
	cfg[3] = (bitrate >> 0x18) & 0xff;
	
	cfg[4] = (  idlechipsel >> 0) & 0xff;
	cfg[5] = (  idlechipsel >> 8) & 0xff;
	
	cfg[6] = (activechipsel >> 0) & 0xff;
	cfg[7] = (activechipsel >> 8) & 0xff;
	
	cfg[8] = (chipseltodatadelay >> 0) & 0xff;
	cfg[9] = (chipseltodatadelay >> 8) & 0xff;
	
	cfg[0xa] = (lastbytetocsdelay >> 0) & 0xff;
	cfg[0xb] = (lastbytetocsdelay >> 8) & 0xff;
	
	cfg[0xc] = (midbytedelay >> 0) & 0xff;
	cfg[0xd] = (midbytedelay >> 8) & 0xff;
	
	return mcp2210_set_cfg_spi(h);
}

bool mcp2210_set_spimode(struct mcp2210_device * const h, const uint8_t spimode)
{
	uint8_t * const cfg = h->cfg_spi;
	cfg[0x10] = spimode;
	return mcp2210_set_cfg_spi(h);
}

bool mcp2210_spi_transfer(struct mcp2210_device * const h, const void * const tx, void * const rx, uint8_t sz)
{
	hid_device * const hid = h->hid;
	uint8_t * const cfg = h->cfg_spi;
	uint8_t cmd[0x41] = {0,0x42}, buf[0x40];
	uint8_t *p = rx;
	
	if (unlikely(sz > 60))
	{
		applog(LOG_ERR, "%s: SPI transfer too long (%d bytes)", __func__, sz);
		return false;
	}
	
	cfg[0xe] = sz;
	cfg[0xf] = 0;
	if (!mcp2210_set_cfg_spi(h))
		return false;
	
	cmd[2] = sz;
	memcpy(&cmd[5], tx, sz);
	if (unlikely(!mcp2210_io(hid, cmd, buf)))
	{
		applog(LOG_ERR, "%s: Failed to issue SPI transfer", __func__);
		return false;
	}
	
	while (true)
	{
		switch (buf[1])
		{
			case 0:     // accepted
				cmd[2] = 0;
				break;
			case 0xf8:  // transfer in progress
				if (opt_dev_protocol)
					applog(LOG_DEBUG, "%s: SPI transfer rejected temporarily (%d bytes remaining)", __func__, sz);
				cgsleep_ms(20);
				goto retry;
			default:
				applog(LOG_ERR, "%s: SPI transfer error (%d) (%d bytes remaining)", __func__, buf[1], sz);
				return false;
		}
		if (buf[2] >= sz)
		{
			if (buf[2] > sz)
				applog(LOG_WARNING, "%s: Received %d extra bytes in SPI transfer", __func__, sz - buf[2]);
			memcpy(p, &buf[4], sz);
			return true;
		}
		memcpy(p, &buf[4], buf[2]);
		p += buf[2];
		sz -= buf[2];
retry:
		if (unlikely(!mcp2210_io(hid, cmd, buf)))
		{
			applog(LOG_ERR, "%s: Failed to continue SPI transfer (%d bytes remaining)", __func__, sz);
			return false;
		}
	}
}

bool mcp2210_spi_cancel(struct mcp2210_device * const h)
{
	hid_device * const hid = h->hid;
	uint8_t cmd[0x41] = {0,0x11}, buf[0x40];
	
	if (!mcp2210_io(hid, cmd, buf))
		return false;
	
	return (buf[1] == 0);
}

static
bool mcp2210_set_cfg_gpio(struct mcp2210_device * const h)
{
	hid_device * const hid = h->hid;
	uint8_t cmd[0x41] = {0,0x21}, buf[0x40];
	
	// NOTE: NVRAM chip params access control is not set here
	memcpy(&cmd[5], h->cfg_gpio, 0xe);
	if (!mcp2210_io(hid, cmd, buf))
	{
		applog(LOG_ERR, "%s: Failed to set current %s config", __func__, "GPIO");
		return false;
	}
	
	if (buf[1] != 0)
	{
		applog(LOG_ERR, "%s: Error setting current %s config (%d)", __func__, "GPIO", buf[1]);
		return false;
	}
	
	return true;
}

bool mcp2210_set_gpio_output(struct mcp2210_device * const h, const int pin, const enum bfg_gpio_value d)
{
	const int bit = 1 << (pin % 8);
	const int byte = (pin / 8);
	
	// Set pin to GPIO mode
	h->cfg_gpio[pin] = 0;
	
	// Set GPIO to output mode
	h->cfg_gpio[byte + 0xb] &= ~bit;
	
	// Set value for GPIO output
	if (d == BGV_HIGH)
		h->cfg_gpio[byte + 9] |= bit;
	else
		h->cfg_gpio[byte + 9] &= ~bit;
	
	return mcp2210_set_cfg_gpio(h);
}

enum bfg_gpio_value mcp2210_get_gpio_input(struct mcp2210_device * const h, const int pin)
{
	hid_device * const hid = h->hid;
	uint8_t cmd[0x41] = {0,0x31}, buf[0x40];
	const int bit = 1 << (pin % 8);
	const int byte = (pin / 8);
	
	// Set pin to GPIO mode
	h->cfg_gpio[pin] = 0;
	
	// Set GPIO to input mode
	h->cfg_gpio[byte + 0xb] |= bit;
	
	if (!mcp2210_set_cfg_gpio(h))
		return BGV_ERROR;
	
	if (!mcp2210_io(hid, cmd, buf))
	{
		applog(LOG_ERR, "%s: Failed to get current GPIO input values", __func__);
		return BGV_ERROR;
	}
	
	if (buf[byte + 4] & bit)
		return BGV_HIGH;
	else
		return BGV_LOW;
}

struct lowlevel_driver lowl_mcp2210 = {
	.dname = "mcp2210",
	.devinfo_scan = mcp2210_devinfo_scan,
};
