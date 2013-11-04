/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifndef WIN32
#include <dlfcn.h>
typedef void *dlh_t;
#else
#include <winsock2.h>
#include <windows.h>
#define dlopen(lib, flags) LoadLibrary(lib)
#define dlsym(h, sym)  ((void*)GetProcAddress(h, sym))
#define dlerror()  "unknown"
#define dlclose(h)  FreeLibrary(h)
typedef HMODULE dlh_t;
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hidapi.h>
#include <utlist.h>

#include "logging.h"
#include "lowlevel.h"
#include "miner.h"

#include "mcp2210.h"

#define MCP2210_IDVENDOR   0x04d8
#define MCP2210_IDPRODUCT  0x00de

#ifdef WIN32
#define HID_API_EXPORT __declspec(dllexport)
#else
#define HID_API_EXPORT /* */
#endif
struct hid_device_info HID_API_EXPORT *(*dlsym_hid_enumerate)(unsigned short, unsigned short);
void HID_API_EXPORT (*dlsym_hid_free_enumeration)(struct hid_device_info *);
hid_device * HID_API_EXPORT (*dlsym_hid_open_path)(const char *);
void HID_API_EXPORT (*dlsym_hid_close)(hid_device *);
int HID_API_EXPORT (*dlsym_hid_read)(hid_device *, unsigned char *, size_t);
int HID_API_EXPORT (*dlsym_hid_write)(hid_device *, const unsigned char *, size_t);

#define LOAD_SYM(sym)  do { \
	if (!(dlsym_ ## sym = dlsym(dlh, #sym))) {  \
		applog(LOG_DEBUG, "%s: Failed to load %s in %s", __func__, #sym, dlname);  \
		goto fail;  \
	}  \
} while(0)

static
bool hidapi_try_lib(const char * const dlname)
{
	struct hid_device_info *hid_enum;
	dlh_t dlh;
	
	dlh = dlopen(dlname, RTLD_NOW);
	if (!dlh)
	{
		applog(LOG_DEBUG, "%s: Couldn't load %s: %s", __func__, dlname, dlerror());
		return false;
	}
	
	LOAD_SYM(hid_enumerate);
	LOAD_SYM(hid_free_enumeration);
	
	hid_enum = dlsym_hid_enumerate(0, 0);
	if (!hid_enum)
	{
		applog(LOG_DEBUG, "%s: Loaded %s, but no devices enumerated; trying other libraries", __func__, dlname);
		goto fail;
	}
	dlsym_hid_free_enumeration(hid_enum);
	
	LOAD_SYM(hid_open_path);
	LOAD_SYM(hid_close);
	LOAD_SYM(hid_read);
	LOAD_SYM(hid_write);
	
	applog(LOG_DEBUG, "%s: Successfully loaded %s", __func__, dlname);
	
	return true;

fail:
	dlclose(dlh);
	return false;
}

#define hid_enumerate dlsym_hid_enumerate
#define hid_free_enumeration dlsym_hid_free_enumeration
#define hid_open_path dlsym_hid_open_path
#define hid_close dlsym_hid_close
#define hid_read dlsym_hid_read
#define hid_write dlsym_hid_write

static
bool hidapi_load_library()
{
	if (dlsym_hid_write)
		return true;
	
	const char **p;
	char dlname[23] = "libhidapi";
	const char *dltry[] = {
		"",
		"-0",
		"-hidraw",
		"-libusb",
		NULL
	};
	for (p = &dltry[0]; *p; ++p)
	{
		sprintf(&dlname[9], "%s.%s", *p,
#ifdef WIN32
		        "dll"
#else
		        "so"
#endif
		);
		if (hidapi_try_lib(dlname))
			return true;
	}
	
	return false;
}

static
char *wcs2str_dup(wchar_t *ws)
{
	if (!ws)
		return NULL;
	
	char *rv;
	int clen, i;
	
	clen = wcslen(ws);
	++clen;
	rv = malloc(clen);
	for (i = 0; i < clen; ++i)
		rv[i] = ws[i];
	
	return rv;
}

static
struct lowlevel_device_info *mcp2210_devinfo_scan()
{
	if (!hidapi_load_library())
	{
		applog(LOG_DEBUG, "%s: Failed to load any hidapi library", __func__);
		return NULL;
	}
	
	struct hid_device_info *hid_enum, *hid_item;
	struct lowlevel_device_info *info, *devinfo_list = NULL;
	
	hid_enum = hid_enumerate(MCP2210_IDVENDOR, MCP2210_IDPRODUCT);
	if (!hid_enum)
	{
		applog(LOG_DEBUG, "%s: No MCP2210 devices found", __func__);
		return NULL;
	}
	
	LL_FOREACH(hid_enum, hid_item)
	{
		info = malloc(sizeof(struct lowlevel_device_info));
		char * const devid = malloc(4 + strlen(hid_item->path) + 1);
		sprintf(devid, "hid:%s", hid_item->path);
		*info = (struct lowlevel_device_info){
			.lowl = &lowl_mcp2210,
			.path = strdup(hid_item->path),
			.devid = devid,
			.manufacturer = wcs2str_dup(hid_item->manufacturer_string),
			.product = wcs2str_dup(hid_item->product_string),
			.serial  = wcs2str_dup(hid_item->serial_number),
		};
		LL_PREPEND(devinfo_list, info);

		applog(LOG_DEBUG, "%s: Found \"%s\" serial \"%s\"",
		       __func__, info->product, info->serial);
	}
	
	hid_free_enumeration(hid_enum);
	
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
	return likely(
		0x41 == hid_write(hid, cmd, 0x41) &&
		64 == hid_read(hid, buf, 64)
	);
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

struct mcp2210_device *mcp2210_open(struct lowlevel_device_info *info)
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

bool mcp2210_set_gpio_output(struct mcp2210_device * const h, const int pin, const enum mcp2210_gpio_value d)
{
	const int bit = 1 << (pin % 8);
	const int byte = (pin / 8);
	
	// Set pin to GPIO mode
	h->cfg_gpio[pin] = 0;
	
	// Set GPIO to output mode
	h->cfg_gpio[byte + 0xb] &= ~bit;
	
	// Set value for GPIO output
	if (d == MGV_HIGH)
		h->cfg_gpio[byte + 9] |= bit;
	else
		h->cfg_gpio[byte + 9] &= ~bit;
	
	return mcp2210_set_cfg_gpio(h);
}

enum mcp2210_gpio_value mcp2210_get_gpio_input(struct mcp2210_device * const h, const int pin)
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
		return MGV_ERROR;
	
	if (!mcp2210_io(hid, cmd, buf))
	{
		applog(LOG_ERR, "%s: Failed to get current GPIO input values", __func__);
		return MGV_ERROR;
	}
	
	if (buf[byte + 4] & bit)
		return MGV_HIGH;
	else
		return MGV_LOW;
}

struct lowlevel_driver lowl_mcp2210 = {
	.dname = "mcp2210",
	.devinfo_scan = mcp2210_devinfo_scan,
};
