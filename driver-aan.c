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
#include <string.h>

#include "driver-aan.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

#define AAN_PROBE_TIMEOUT_US  3750000

enum aan_cmd {
	AAN_BIST_START           = 0x01,
	AAN_BIST_FIX             = 0x03,
	AAN_RESET                = 0x04,
	AAN_WRITE_JOB            = 0x07,
	AAN_READ_RESULT          = 0x08,
	AAN_WRITE_REG            = 0x09,
	AAN_READ_REG             = 0x0a,
	AAN_READ_REG_RESP        = 0x1a,
};

static
bool aan_spi_cmd(struct spi_port * const spi, const uint8_t cmd, const uint8_t chip, const struct timeval * const tvp_timeout)
{
	const struct aan_hooks * const hooks = spi->userp;
	const uint8_t cmdbuf[2] = {cmd, chip};
	hooks->precmd(spi);
	spi_emit_buf(spi, cmdbuf, sizeof(cmdbuf));
	if (unlikely(!spi_txrx(spi)))
		return false;
	
	spi_clear_buf(spi);
	spi_emit_nop(spi, 2);
	
	uint8_t * const rx = spi_getrxbuf(spi);
	while (true)
	{
		if (unlikely(!spi_txrx(spi)))
			return false;
		if (!memcmp(rx, cmdbuf, 2))
			break;
		if (unlikely(tvp_timeout && timer_passed(tvp_timeout, NULL)))
			return false;
	}
	spi_clear_buf(spi);
	
	return true;
}

static
int aan_spi_autoaddress(struct spi_port * const spi, const struct timeval * const tvp_timeout)
{
	if (!aan_spi_cmd(spi, AAN_BIST_START, AAN_ALL_CHIPS, tvp_timeout))
		applogr(-1, LOG_DEBUG, "%s: %s failed", __func__, "AAN_BIST_START");
	spi_emit_nop(spi, 2);
	if (!spi_txrx(spi))
		applogr(-1, LOG_DEBUG, "%s: %s failed", __func__, "spi_txrx");
	uint8_t * const rx = spi_getrxbuf(spi);
	return rx[1];
}

int aan_detect_spi(struct spi_port * const spi)
{
	struct timeval tv_timeout;
	timer_set_delay_from_now(&tv_timeout, AAN_PROBE_TIMEOUT_US);
	if (!aan_spi_cmd(spi, AAN_RESET, AAN_ALL_CHIPS, &tv_timeout))
		return -1;
	return aan_spi_autoaddress(spi, &tv_timeout);
}
