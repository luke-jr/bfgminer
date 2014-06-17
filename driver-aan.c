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

static void aan_spi_parse_rx(struct spi_port *);

static
void aan_spi_cmd_queue(struct spi_port * const spi, const uint8_t cmd, const uint8_t chip, const void * const data, const size_t datalen)
{
	const struct aan_hooks * const hooks = spi->userp;
	const uint8_t cmdbuf[2] = {cmd, chip};
	hooks->precmd(spi);
	spi_emit_buf(spi, cmdbuf, sizeof(cmdbuf));
	if (datalen)
		spi_emit_buf(spi, data, datalen);
}

static
bool aan_spi_txrx(struct spi_port * const spi)
{
	if (unlikely(!spi_txrx(spi)))
		return false;
	
	aan_spi_parse_rx(spi);
	spi_clear_buf(spi);
	return true;
}

static
bool aan_spi_cmd_send(struct spi_port * const spi, const uint8_t cmd, const uint8_t chip, const void * const data, const size_t datalen)
{
	aan_spi_cmd_queue(spi, cmd, chip, data, datalen);
	return aan_spi_txrx(spi);
}

static
bool aan_spi_cmd_resp(struct spi_port * const spi, const uint8_t cmd, const uint8_t chip, const struct timeval * const tvp_timeout)
{
	const uint8_t cmdbuf[2] = {cmd, chip};
	
	spi_emit_nop(spi, 2);
	
	uint8_t * const rx = spi_getrxbuf(spi);
	while (true)
	{
		if (unlikely(!spi_txrx(spi)))
			return false;
		if (!memcmp(rx, cmdbuf, 2))
			break;
		aan_spi_parse_rx(spi);
		if (unlikely(tvp_timeout && timer_passed(tvp_timeout, NULL)))
			return false;
	}
	spi_clear_buf(spi);
	
	return true;
}

static
bool aan_spi_cmd(struct spi_port * const spi, const uint8_t cmd, const uint8_t chip, const void * const data, const size_t datalen, const struct timeval * const tvp_timeout)
{
	if (!aan_spi_cmd_send(spi, cmd, chip, data, datalen))
		return false;
	if (!aan_spi_cmd_resp(spi, cmd, chip, tvp_timeout))
		return false;
	return true;
}

int aan_detect_spi(int * const out_chipcount, struct spi_port * const * const spi_a, const int spi_n)
{
	struct timeval tv_timeout;
	timer_set_delay_from_now(&tv_timeout, AAN_PROBE_TIMEOUT_US);
	
	int state[spi_n];
	int completed = 0;
	
	for (int i = 0; i < spi_n; ++i)
	{
		struct spi_port * const spi = spi_a[i];
		aan_spi_cmd_send(spi, state[i] = AAN_RESET, AAN_ALL_CHIPS, NULL, 0);
		spi_emit_nop(spi, 2);
		out_chipcount[i] = -1;
	}
	
	do {
		for (int i = 0; i < spi_n; ++i)
		{
			if (state[i] == -1)
				continue;
			struct spi_port * const spi = spi_a[i];
			if (unlikely(!spi_txrx(spi)))
			{
spifail:
				state[i] = -1;
				continue;
			}
			uint8_t * const rx = spi_getrxbuf(spi);
			if (rx[0] == state[i] && rx[1] == AAN_ALL_CHIPS)
			{
				switch (state[i])
				{
					case AAN_RESET:
						applog(LOG_DEBUG, "%s: Reset complete", spi->repr);
						spi_clear_buf(spi);
						aan_spi_cmd_send(spi, state[i] = AAN_BIST_START, AAN_ALL_CHIPS, NULL, 0);
						spi_emit_nop(spi, 2);
						break;
					case AAN_BIST_START:
						if (unlikely(!spi_txrx(spi)))
							goto spifail;
						out_chipcount[i] = rx[1];
						state[i] = -1;
						++completed;
						applog(LOG_DEBUG, "%s: BIST_START complete (%d chips)", spi->repr, rx[1]);
						break;
				}
				continue;
			}
			aan_spi_parse_rx(spi);
		}
	} while (completed < spi_n && likely(!timer_passed(&tv_timeout, NULL)));
	
	for (int i = 0; i < spi_n; ++i)
	{
		struct spi_port * const spi = spi_a[i];
		spi_clear_buf(spi);
	}
	
	applog(LOG_DEBUG, "%s completed for %d out of %d SPI ports", __func__, completed, spi_n);
	
	return completed;
}

static
void aan_spi_parse_rx(struct spi_port * const spi)
{
	// TODO
}
