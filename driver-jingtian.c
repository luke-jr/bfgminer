/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "deviceapi.h"
#include "driver-aan.h"
#include "logging.h"
#include "lowl-spi.h"
#include "util.h"

static const int jingtian_cs_gpio[] = {14, 15, 18};
static const int jingtian_max_cs = 1 << (sizeof(jingtian_cs_gpio) / sizeof(*jingtian_cs_gpio));
static const uint8_t jingtian_pre_header[] = {0xb5, 0xb5};

BFG_REGISTER_DRIVER(jingtian_drv)

static
bool jingtian_spi_txrx(struct spi_port * const port)
{
	if (*port->chipselect_current != port->chipselect)
	{
		unsigned cs_set_low = 0, cs_set_high = 0, cur_cs_bit;
		bool bit_desired;
		for (int i = 0; i < sizeof(jingtian_cs_gpio) / sizeof(*jingtian_cs_gpio); ++i)
		{
			cur_cs_bit = (1 << i);
			bit_desired = (port->chipselect & cur_cs_bit);
			if (bit_desired == (bool)(*port->chipselect_current & cur_cs_bit))
				// No change needed
				continue;
			if (bit_desired)
				cs_set_high |= (1 << jingtian_cs_gpio[i]);
			else
				cs_set_low  |= (1 << jingtian_cs_gpio[i]);
		}
		if (cs_set_low)
			bfg_gpio_set_low(cs_set_low);
		if (cs_set_high)
			bfg_gpio_set_high(cs_set_high);
		if (opt_dev_protocol)
			applog(LOG_DEBUG, "%s(%p): CS %d", __func__, port, port->chipselect);
		*port->chipselect_current = port->chipselect;
	}
	if (opt_dev_protocol)
	{
		char x[(spi_getbufsz(port) * 2) + 1];
		bin2hex(x, spi_gettxbuf(port), spi_getbufsz(port));
		applog(LOG_DEBUG, "%s(%p): %cX %s", __func__, port, 'T', x);
	}
	bool rv = linux_spi_txrx(port);
	if (opt_dev_protocol)
	{
		char x[(spi_getbufsz(port) * 2) + 1];
		bin2hex(x, spi_getrxbuf(port), spi_getbufsz(port));
		applog(LOG_DEBUG, "%s(%p): %cX %s", __func__, port, 'R', x);
	}
	return rv;
}

static
void jingtian_precmd(struct spi_port * const spi)
{
	spi_emit_buf(spi, jingtian_pre_header, sizeof(jingtian_pre_header));
}

static
struct aan_hooks jingtian_hooks = {
	.precmd = jingtian_precmd,
};

static
void jingtian_common_init(void)
{
	RUNONCE();
	spi_init();
	for (int i = 0; i < sizeof(jingtian_cs_gpio) / sizeof(*jingtian_cs_gpio); ++i)
		bfg_gpio_setpin_output(jingtian_cs_gpio[i]);
}

static
bool jingtian_detect_one(const char * const devpath)
{
	int found = 0, chips;
	
	jingtian_common_init();
	
	const int fd = open(devpath, O_RDWR);
	if (fd < 0)
		applogr(false, LOG_DEBUG, "%s: Failed to open %s", jingtian_drv.dname, devpath);
	
	struct cgpu_info *cgpu, *prev_cgpu = NULL;
	struct spi_port * const spi = calloc(sizeof(*spi), 1), *spicopy;
	spi->txrx = jingtian_spi_txrx;
	spi->userp = &jingtian_hooks;
	spi->fd = fd;
	spi->speed = 4000000;
	spi->delay = 0;
	spi->mode = 1;
	spi->bits = 8;
	spi->chipselect_current = malloc(sizeof(*spi->chipselect_current));
	*spi->chipselect_current = -1;
	for (int i = 0; i < jingtian_max_cs; ++i)
	{
		spi->chipselect = i;
		chips = aan_detect_spi(spi);
		applog(LOG_DEBUG, "%s: %d chips found on %s CS %d",
		       jingtian_drv.dname, chips, devpath, i);
		if (chips <= 0)
			continue;
		spicopy = malloc(sizeof(*spicopy));
		memcpy(spicopy, spi, sizeof(*spicopy));
		cgpu = malloc(sizeof(*cgpu));
		*cgpu = (struct cgpu_info){
			.drv = &jingtian_drv,
			.procs = chips,
			.device_data = spicopy,
		};
		add_cgpu_slave(cgpu, prev_cgpu);
		prev_cgpu = cgpu;
		found += chips;
	}
	close(fd);
	if (!found)
		free(spi->chipselect_current);
	free(spi);
	return found;
}

static
int jingtian_detect_auto(void)
{
	return jingtian_detect_one("/dev/spidev0.0") ? 1 : 0;
}

static
void jingtian_detect(void)
{
	generic_detect(&jingtian_drv, jingtian_detect_one, jingtian_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

struct device_drv jingtian_drv = {
	.dname = "jingtian",
	.name = "JTN",
	.drv_detect = jingtian_detect,
};
