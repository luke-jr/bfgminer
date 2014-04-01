/*
 * Copyright 2014 Nate Woolls
 * Copyright 2013 Luke Dashjr
 * Copyright 2014 GridSeed Team
 * Copyright 2014 Dualminer Team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "gc3355.h"
#include "gc3355-commands.h"

#include <stdint.h>
#include <string.h>

#include "miner.h"
#include "driver-icarus.h"
#include "logging.h"
#include "lowl-vcom.h"

#ifndef WIN32
  #include <sys/ioctl.h>
#else
  #include <io.h>
#endif

// options configurable by the end-user

int opt_sha2_units = -1;
int opt_pll_freq = 0; // default is set in gc3355_set_pll_freq

#define GC3355_CHIP_NAME			"gc3355"

#define DEFAULT_DELAY_TIME 2000

static
void gc3355_set_register(uint8_t * const buf, const uint8_t clusaddr, const uint8_t chipaddr, const uint8_t regaddr, const uint32_t val)
{
	buf[0] = 0x55;
	buf[1] = 0xaa;
	buf[2] = (clusaddr << 4) | chipaddr;
	buf[3] = regaddr;
	buf[4] = (val >>    0) & 0xff;
	buf[5] = (val >>    8) & 0xff;
	buf[6] = (val >> 0x10) & 0xff;
	buf[7] = (val >> 0x18) & 0xff;
}

static
void gc3355_config_cpm(uint8_t * const buf, const uint8_t chipaddr, const float mhz)
{
	// See https://github.com/gridseed/gc3355-doc/blob/master/GC3355_Register_Spec.pdf
	const uint8_t pll_bypass = 1;
	const uint8_t pll_bandselect = 0;
	const uint8_t pll_outdiv = 0;
	
	uint8_t freq_div, freq_mult, last_freq_mult;  // mhz = (25 / freq_div * freq_mult)
	float actual_mhz, last_actual_mhz = -1;
	for (freq_div = 1; freq_div <= 32; ++freq_div)
	{
		freq_mult = mhz * freq_div / 25;
		if (freq_mult > 0x80)
			freq_mult = 0x80;
		actual_mhz = 25. / freq_div * freq_mult;
		if (last_actual_mhz > actual_mhz)
		{
			--freq_div;
			freq_mult = last_freq_mult;
			if (opt_debug)
				actual_mhz = 25. / freq_div * freq_mult;
			break;
		}
		if (actual_mhz > mhz - .5)
			break;
		last_actual_mhz = actual_mhz;
		last_freq_mult = freq_mult;
	}
	const uint8_t pll_F = freq_mult - 1;
	const uint8_t pll_R = freq_div - 1;
	
	const uint8_t core_clk_out1_diven = 0;
	const uint8_t core_clk_sel1 = 0;
	const uint8_t core_clk_sel0 = 0;
	const uint8_t pll_clk_gate = 0;
	const uint8_t pll_recfg = 1;
	const uint8_t cfg_cpm = 1;
	const uint32_t cfg = (pll_bypass << 31) | (pll_bandselect << 30) | (pll_outdiv << 28) | (pll_F << 21) | (pll_R << 16) | (core_clk_out1_diven << 6) | (core_clk_sel1 << 5) | (core_clk_sel0 << 4) | (pll_clk_gate << 3) | (pll_recfg << 2) | (cfg_cpm << 0);
	gc3355_set_register(buf, 0xe, chipaddr, 0, cfg);
}

// NOTE: MHz must match CPM config
static
void gc3355_config_sha256d(uint8_t * const buf, const uint8_t chipaddr, const float mhz, const uint32_t baud)
{
	// See https://github.com/gridseed/gc3355-doc/blob/master/GC3355_Register_Spec.pdf
	const uint8_t force_start = 1;
	const uint8_t uart_enable = 1;
	const uint8_t uart_debug = 0;
	const uint8_t byte_order = 0;
	const uint16_t rpt_cycle = (mhz * 1000000 / baud);
	const uint32_t cfg = (force_start << 31) | (uart_enable << 30) | (uart_debug << 29) | (byte_order << 28) | rpt_cycle;
	gc3355_set_register(buf, 0, chipaddr, 0xff, cfg);
}

static
int gc3355_write(const int fd, const void * const buf, const size_t bufsz)
{
	const int rv = icarus_write(fd, buf, bufsz);
	usleep(DEFAULT_DELAY_TIME);
	return rv;
}

static
void gc3355_send_cmds(int fd, const char *cmds[])
{
	int i = 0;
	unsigned char ob_bin[32];
	for(i = 0 ;; i++)
	{
		memset(ob_bin, 0, sizeof(ob_bin));

		if (cmds[i] == NULL)
			break;

		hex2bin(ob_bin, cmds[i], strlen(cmds[i]) / 2);
		icarus_write(fd, ob_bin, 8);
		usleep(GC3355_COMMAND_DELAY);
	}
}

static
void gc3355_open_sha2_units(int fd, int sha2_units)
{
	int unit_count = 0;
	unsigned char ob_bin[8];
	int i;

	// should be 0 - 160
	unit_count = sha2_units < 0 ? 0 : sha2_units > 160 ? 160 : sha2_units;

	if (unit_count > 0)
	{
		for(i = 0; i <= unit_count; i++)
		{
			hex2bin(ob_bin, sha2_open_cmd[i], sizeof(ob_bin));
			gc3355_write(fd, ob_bin, 8);
			usleep(GC3355_COMMAND_DELAY);
		}
	}
	else if (unit_count == 0)
		gc3355_send_cmds(fd, sha2_gating_cmd);
}

void gc3355_set_pll_freq(int fd, int pll_freq)
{
	const uint8_t chipaddr = 0xf;
	const uint32_t baud = 115200;  // FIXME: Make this configurable
	uint8_t buf[8];
	
	if (!pll_freq)
	{
		if (gc3355_get_cts_status(fd) == 1)
			//1.2v - Scrypt mode
			pll_freq = 850;
		else
			//0.9v - Scrypt + SHA mode
			pll_freq = 550;
	}
	
	gc3355_config_cpm(buf, chipaddr, pll_freq);
	gc3355_write(fd, buf, sizeof(buf));
	
	gc3355_config_sha256d(buf, chipaddr, pll_freq, baud);
	gc3355_write(fd, buf, sizeof(buf));
}

static
void gc3355_sha2_init(int fd)
{
	gc3355_send_cmds(fd, sha2_gating_cmd);
	gc3355_send_cmds(fd, sha2_init_cmd);
}

static
void gc3355_reset_chips(int fd)
{
	// reset chips
	gc3355_send_cmds(fd, gcp_chip_reset_cmd);
	gc3355_send_cmds(fd, btc_chip_reset_cmd);
}

static
void gc3355_reset_dtr(int fd)
{
	// set data terminal ready (DTR) status
	gc3355_set_dtr_status(fd, DTR_HIGH);
	usleep(GC3355_COMMAND_DELAY);
	gc3355_set_dtr_status(fd, DTR_LOW);
}

static
void gc3355_scrypt_only_init(int fd);

void gc3355_init_usbstick(int fd, int pll_freq, bool scrypt_only, bool detect_only)
{
	gc3355_reset_chips(fd);

	gc3355_reset_dtr(fd);


	// initialize units
	if (opt_scrypt && scrypt_only)
		gc3355_scrypt_only_init(fd);
	else
	{
		gc3355_sha2_init(fd);
		gc3355_scrypt_init(fd);
	}

	//set freq
	gc3355_set_pll_freq(fd, pll_freq);

	// zzz
	usleep(GC3355_COMMAND_DELAY);

	if (!detect_only)
	{
		if (!opt_scrypt)
		{
			// open sha2 units
			gc3355_open_sha2_units(fd, opt_sha2_units);
		}

		// set request to send (RTS) status
		gc3355_set_rts_status(fd, RTS_HIGH);
	}
}

void gc3355_scrypt_init(int fd)
{
	gc3355_send_cmds(fd, scrypt_init_cmd);
}

void gc3355_scrypt_only_reset(int fd);

static
void gc3355_scrypt_only_init(int fd)
{
	gc3355_send_cmds(fd, sha2_gating_cmd);
	gc3355_send_cmds(fd, scrypt_only_init_cmd);
	gc3355_scrypt_only_reset(fd);
}

void gc3355_scrypt_reset(int fd)
{
	gc3355_send_cmds(fd, scrypt_reset_cmd);
}

void gc3355_scrypt_only_reset(int fd)
{
	gc3355_send_cmds(fd, scrypt_only_reset_cmd);
}

void gc3355_scrypt_prepare_work(unsigned char cmd[156], struct work *work)
{
	// command header
	cmd[0] = 0x55;
	cmd[1] = 0xaa;
	cmd[2] = 0x1f;
	cmd[3] = 0x00;

	// task data
	memcpy(cmd + 4, work->target, 32);
	memcpy(cmd + 36, work->midstate, 32);
	memcpy(cmd + 68, work->data, 80);

	// nonce_max
	cmd[148] = 0xff;
	cmd[149] = 0xff;
	cmd[150] = 0xff;
	cmd[151] = 0xff;

	// taskid
	int workid = work->id;
	memcpy(cmd + 152, &(workid), 4);
}

void gc3355_sha2_prepare_work(unsigned char cmd[52], struct work *work, bool simple)
{
	if (simple)
	{
		// command header
		cmd[0] = 0x55;
		cmd[1] = 0xaa;
		cmd[2] = 0x0f;
		cmd[3] = 0x01; // SHA header sig

		memcpy(cmd + 4, work->midstate, 32);
		memcpy(cmd + 36, work->data + 64, 12);

		// taskid
		int workid = work->id;
		memcpy(cmd + 48, &(workid), 4);
	}
	else
	{
		// command header
		cmd[0] = 0x55;
		cmd[1] = 0xaa;
		cmd[2] = 0x0f;
		cmd[3] = 0x00; // Scrypt header sig - used by DualMiner in Dual Mode

		uint8_t temp_bin[64];
		memset(temp_bin, 0, 64);

		memcpy(temp_bin, work->midstate, 32);
		memcpy(temp_bin + 52, work->data + 64, 12);

		memcpy(cmd + 8, work->midstate, 32);
		memcpy(cmd + 40, temp_bin + 52, 12);
	}
}
