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

#include <linux/spi/spidev.h>

#include <zlib.h>

#include "deviceapi.h"
#include "lowl-spi.h"
#include "util.h"

#define NEPTUNE_SPI_SPEED  3000000
#define NEPTUNE_SPI_DELAY  0
#define NEPTUNE_SPI_MODE  (SPI_CPHA | SPI_CPOL | SPI_CS_HIGH)
#define NEPTUNE_SPI_BITS  8
#define NEPTUNE_MAX_CHANNEL  6
#define NEPTUNE_MAX_DIES_PER_CHANNEL  4
#define NEPTUNE_VERSION  0xa002

BFG_REGISTER_DRIVER(neptune_drv)

enum neptune_cmd {
	NEPTUNE_GETINFO = 0x80,
};

static
void neptune_spi_emit_to_channel(struct spi_port * const spi, const uint8_t channel, const uint8_t datasz)
{
	const uint8_t muxheader[2] = {0x80 | channel, datasz};
	spi_emit_buf(spi, muxheader, sizeof(muxheader));
}

void *neptune_spi_emit_cmd(struct spi_port * const spi, const uint8_t channel, const uint8_t die, const uint8_t core, const enum neptune_cmd cmd, const void * const data, const uint8_t datasz, const uint8_t responsesz)
{
	const uint8_t cmdhdr[4] = {cmd, die, core >> 8, core & 0xff};
	neptune_spi_emit_to_channel(spi, channel, sizeof(cmdhdr) + datasz);
	spi_emit_buf(spi, cmdhdr, sizeof(cmdhdr));
	void * const rv = spi_emit_buf(spi, data, datasz);
	if (datasz < responsesz)
		spi_emit_nop(spi, responsesz - datasz);
	uint32_t crc = crc32(0, NULL, 0);
	crc = crc32(crc, cmdhdr, sizeof(cmdhdr));
	if (datasz)
		crc = crc32(crc, data, datasz);
	uint8_t crcb[4];
	pk_u32be(crcb, 0, crc);
	spi_emit_buf(spi, crcb, sizeof(crcb));
	return rv;
}

static
bool neptune_detect_one(const char * const devpath)
{
	struct cgpu_info *prev_cgpu = NULL, *cgpu;
	
	spi_init();
	
	struct spi_port * const spi = malloc(sizeof(*spi));
	memset(spi, 0, sizeof(*spi));
	spi->txrx = linux_spi_txrx;
	spi->repr = neptune_drv.dname;
	spi->logprio = LOG_ERR;
	spi->speed = NEPTUNE_SPI_SPEED;
	spi->delay = NEPTUNE_SPI_DELAY;
	spi->mode = NEPTUNE_SPI_MODE;
	spi->bits = NEPTUNE_SPI_BITS;
	
	const int fd = spi_open(spi, devpath);
	if (unlikely(fd == -1))
	{
		free(spi);
		applogr(false, LOG_DEBUG, "%s: Failed to open %s", neptune_drv.dname, devpath);
	}
	
	for (int channel = 0; channel < NEPTUNE_MAX_CHANNEL; ++channel)
	{
		unsigned total_cores = 0;
		for (int die = 0; die < NEPTUNE_MAX_DIES_PER_CHANNEL; ++die)
		{
			uint8_t *rx = neptune_spi_emit_cmd(spi, channel, die, 0, NEPTUNE_GETINFO, NULL, 0, 12);
			spi_txrx(spi);
			unsigned cores = upk_u16be(rx, 0);
			unsigned version = upk_u16be(rx, 2);
			if (version != NEPTUNE_VERSION)
				continue;
			
			// Read (and ignore) core status
			size_t bytes_to_ignore = cores / 4;
			neptune_spi_emit_to_channel(spi, channel, bytes_to_ignore);
			spi_emit_nop(spi, bytes_to_ignore);
			
			total_cores += cores;
		}
		if (!total_cores)
			continue;
		
		cgpu = malloc(sizeof(*cgpu));
		*cgpu = (struct cgpu_info){
			.drv = &neptune_drv,
			.device_path = strdup(devpath),
			.procs = total_cores,
			.threads = prev_cgpu ? 0 : 1,
		};
		if (!add_cgpu_slave(cgpu, prev_cgpu))
			continue;
		prev_cgpu = cgpu;
	}
	
	return prev_cgpu;
}

static
int neptune_detect_auto(void)
{
	if (neptune_detect_one("/dev/spidev1.0"))
		return 1;
	return 0;
}

static
void neptune_detect(void)
{
	generic_detect(&neptune_drv, neptune_detect_one, neptune_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

struct device_drv neptune_drv = {
	.dname = "neptune",
	.name = "NEP",
	.drv_detect = neptune_detect,
};
