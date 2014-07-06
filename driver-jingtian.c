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
	.read_reg = aan_read_reg_direct,
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
	struct spi_port *spi;
	int * const chipselect_current = malloc(sizeof(*spi->chipselect_current));
	*chipselect_current = -1;
	
	int devpath_len = strlen(devpath);
	
	int chipcount[jingtian_max_cs];
	struct spi_port *spi_a[jingtian_max_cs];
	for (int i = 0; i < jingtian_max_cs; ++i)
	{
		spi = spi_a[i] = calloc(sizeof(*spi), 1);
		spi->repr = malloc(devpath_len + 0x10);
		sprintf((void*)spi->repr, "%s(cs%d)", devpath, i);
		spi->txrx = jingtian_spi_txrx;
		spi->userp = &jingtian_hooks;
		spi->fd = fd;
		spi->speed = 4000000;
		spi->delay = 0;
		spi->mode = 1;
		spi->bits = 8;
		spi->chipselect = i;
		spi->chipselect_current = chipselect_current;
	}
	
	aan_detect_spi(chipcount, spi_a, jingtian_max_cs);
	
	for (int i = 0; i < jingtian_max_cs; ++i)
	{
		chips = chipcount[i];
		free((void*)spi_a[i]->repr);
		spi_a[i]->repr = NULL;
		if (chips <= 0)
		{
			free(spi_a[i]);
			continue;
		}
		cgpu = malloc(sizeof(*cgpu));
		*cgpu = (struct cgpu_info){
			.drv = &jingtian_drv,
			.procs = chips,
			.threads = prev_cgpu ? 0 : 1,
			.device_data = spi_a[i],
			.device_path = strdup(devpath),
			.set_device_funcs = aan_set_device_funcs,
		};
		add_cgpu_slave(cgpu, prev_cgpu);
		prev_cgpu = cgpu;
		found += chips;
	}
	
	close(fd);
	if (!found)
		free(chipselect_current);
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

static
bool jingtian_init(struct thr_info * const master_thr)
{
	struct cgpu_info * const master_dev = master_thr->cgpu;
	const char * const devpath = master_dev->device_path;
	const int fd = open(devpath, O_RDWR);
	if (fd < 0)
		applogr(false, LOG_ERR, "%s: Failed to open %s", master_dev->dev_repr, devpath);
	
	for_each_managed_proc(proc, master_dev)
	{
		struct spi_port * const spi = proc->device_data;
		spi->fd = fd;
	}
	
	return aan_init(master_thr);
}

struct device_drv jingtian_drv = {
	.dname = "jingtian",
	.name = "JTN",
	.drv_detect = jingtian_detect,
	
	.thread_init = jingtian_init,
	
	.minerloop = minerloop_queue,
	.queue_append = aan_queue_append,
	.queue_flush = aan_queue_flush,
	.poll = aan_poll,
};
