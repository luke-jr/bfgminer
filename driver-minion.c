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
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/spi/spidev.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

static const uint8_t minion_max_chipid = 0x1f;
static const uint8_t minion_chip_signature[] = {0x44, 0x8a, 0xac, 0xb1};

enum minion_register {
	MRA_SIGNATURE        = 0x00,
	MRA_STATUS           = 0x01,
};

static
void minion_get(struct spi_port * const spi, const uint8_t chipid, const uint8_t addr, void * const buf, const size_t bufsz)
{
	const uint8_t header[] = {chipid, addr | 0x80, bufsz & 0xff, bufsz >> 8};
	spi_clear_buf(spi);
	spi_emit_buf(spi, header, sizeof(header));
	uint8_t dummy[bufsz];
	memset(dummy, 0xff, bufsz);
	spi_emit_buf(spi, dummy, bufsz);
	spi_txrx(spi);
	
	uint8_t * const rdbuf = spi_getrxbuf(spi);
	memcpy(buf, &rdbuf[sizeof(header)], bufsz);
}

static
unsigned minion_count_cores(struct spi_port * const spi)
{
	uint8_t buf[max(4, sizeof(minion_chip_signature))];
	unsigned total_core_count = 0;
	
	for (unsigned chipid = 0; chipid <= minion_max_chipid; ++chipid)
	{
		minion_get(spi, chipid, MRA_SIGNATURE, buf, sizeof(minion_chip_signature));
		if (memcmp(buf, minion_chip_signature, sizeof(minion_chip_signature)))
		{
			for (unsigned i = 0; i < sizeof(minion_chip_signature); ++i)
			{
				if (buf[i] != 0xff)
				{
					char hex[(sizeof(minion_chip_signature) * 2) + 1];
					bin2hex(hex, buf, sizeof(minion_chip_signature));
					applog(LOG_DEBUG, "%s: chipid %u: Bad signature (%s)", spi->repr, chipid, hex);
					break;
				}
			}
			continue;
		}
		
		minion_get(spi, chipid, MRA_STATUS, buf, 4);
		const uint8_t core_count = buf[2];
		
		applog(LOG_DEBUG, "%s: chipid %u: Found %u cores", spi->repr, chipid, core_count);
		total_core_count += core_count;
	}
	
	return total_core_count;
}

BFG_REGISTER_DRIVER(minion_drv)

static
bool minion_detect_one(const char * const devpath)
{
	spi_init();
	
	struct spi_port *spi = malloc(sizeof(*spi));
	// Be careful, read lowl-spi.h comments for warnings
	memset(spi, 0, sizeof(*spi));
	spi->speed = 50000000;
	spi->mode = SPI_MODE_0;
	spi->bits = 8;
	spi->txrx = linux_spi_txrx;
	if (spi_open(spi, devpath) < 0)
	{
		free(spi);
		applogr(false, LOG_ERR, "%s: Failed to open %s", minion_drv.dname, devpath);
	}
	
	spi->repr = minion_drv.dname;
	spi->logprio = LOG_WARNING;
	const unsigned total_core_count = minion_count_cores(spi);
	
	close(spi->fd);
	free(spi);
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &minion_drv,
		.device_path = strdup(devpath),
		.deven = DEV_ENABLED,
		.procs = total_core_count,
		.threads = 1,
	};
	return add_cgpu(cgpu);
}

static
int minion_detect_auto(void)
{
	return minion_detect_one("/dev/spidev0.0") ? 1 : 0;
}

static
void minion_detect(void)
{
	generic_detect(&minion_drv, minion_detect_one, minion_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

struct device_drv minion_drv = {
	.dname = "minion",
	.name = "MNN",
	.drv_detect = minion_detect,
};
