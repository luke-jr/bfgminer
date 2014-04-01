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


// SHA-2 commands

static
const char *sha2_gating_cmd[] =
{
	"55AAEF0200000000",  // Chip 1 - power down SHA-2 (unless masked w/PLL)
	"55AAEF0300000000",  // Chip 2
	"55AAEF0400000000",  // Chip 3
	"55AAEF0500000000",  // Chip 4
	"55AAEF0600000000",  // Chip 5
	NULL
};

// maps the above SHA chip gating with SHA-2 units
static
const char *sha2_open_cmd[] =
{
	"55AAEF0200000001",
	"55AAEF0200000003",
	"55AAEF0200000007",
	"55AAEF020000000F",
	"55AAEF020000001F",
	"55AAEF020000003F",
	"55AAEF020000007F",
	"55AAEF02000000FF",
	"55AAEF02000001FF",
	"55AAEF02000003FF",
	"55AAEF02000007FF",
	"55AAEF0200000FFF",
	"55AAEF0200001FFF",
	"55AAEF0200003FFF",
	"55AAEF0200007FFF",
	"55AAEF020000FFFF",
	"55AAEF020001FFFF",
	"55AAEF020003FFFF",
	"55AAEF020007FFFF",
	"55AAEF02000FFFFF",
	"55AAEF02001FFFFF",
	"55AAEF02003FFFFF",
	"55AAEF02007FFFFF",
	"55AAEF0200FFFFFF",
	"55AAEF0201FFFFFF",
	"55AAEF0203FFFFFF",
	"55AAEF0207FFFFFF",
	"55AAEF020FFFFFFF",
	"55AAEF021FFFFFFF",
	"55AAEF023FFFFFFF",
	"55AAEF027FFFFFFF",
	"55AAEF02FFFFFFFF",
	"55AAEF0300000001",
	"55AAEF0300000003",
	"55AAEF0300000007",
	"55AAEF030000000F",
	"55AAEF030000001F",
	"55AAEF030000003F",
	"55AAEF030000007F",
	"55AAEF03000000FF",
	"55AAEF03000001FF",
	"55AAEF03000003FF",
	"55AAEF03000007FF",
	"55AAEF0300000FFF",
	"55AAEF0300001FFF",
	"55AAEF0300003FFF",
	"55AAEF0300007FFF",
	"55AAEF030000FFFF",
	"55AAEF030001FFFF",
	"55AAEF030003FFFF",
	"55AAEF030007FFFF",
	"55AAEF03000FFFFF",
	"55AAEF03001FFFFF",
	"55AAEF03003FFFFF",
	"55AAEF03007FFFFF",
	"55AAEF0300FFFFFF",
	"55AAEF0301FFFFFF",
	"55AAEF0303FFFFFF",
	"55AAEF0307FFFFFF",
	"55AAEF030FFFFFFF",
	"55AAEF031FFFFFFF",
	"55AAEF033FFFFFFF",
	"55AAEF037FFFFFFF",
	"55AAEF03FFFFFFFF",
	"55AAEF0400000001",
	"55AAEF0400000003",
	"55AAEF0400000007",
	"55AAEF040000000F",
	"55AAEF040000001F",
	"55AAEF040000003F",
	"55AAEF040000007F",
	"55AAEF04000000FF",
	"55AAEF04000001FF",
	"55AAEF04000003FF",
	"55AAEF04000007FF",
	"55AAEF0400000FFF",
	"55AAEF0400001FFF",
	"55AAEF0400003FFF",
	"55AAEF0400007FFF",
	"55AAEF040000FFFF",
	"55AAEF040001FFFF",
	"55AAEF040003FFFF",
	"55AAEF040007FFFF",
	"55AAEF04000FFFFF",
	"55AAEF04001FFFFF",
	"55AAEF04003FFFFF",
	"55AAEF04007FFFFF",
	"55AAEF0400FFFFFF",
	"55AAEF0401FFFFFF",
	"55AAEF0403FFFFFF",
	"55AAEF0407FFFFFF",
	"55AAEF040FFFFFFF",
	"55AAEF041FFFFFFF",
	"55AAEF043FFFFFFF",
	"55AAEF047FFFFFFF",
	"55AAEF04FFFFFFFF",
	"55AAEF0500000001",
	"55AAEF0500000003",
	"55AAEF0500000007",
	"55AAEF050000000F",
	"55AAEF050000001F",
	"55AAEF050000003F",
	"55AAEF050000007F",
	"55AAEF05000000FF",
	"55AAEF05000001FF",
	"55AAEF05000003FF",
	"55AAEF05000007FF",
	"55AAEF0500000FFF",
	"55AAEF0500001FFF",
	"55AAEF0500003FFF",
	"55AAEF0500007FFF",
	"55AAEF050000FFFF",
	"55AAEF050001FFFF",
	"55AAEF050003FFFF",
	"55AAEF050007FFFF",
	"55AAEF05000FFFFF",
	"55AAEF05001FFFFF",
	"55AAEF05003FFFFF",
	"55AAEF05007FFFFF",
	"55AAEF0500FFFFFF",
	"55AAEF0501FFFFFF",
	"55AAEF0503FFFFFF",
	"55AAEF0507FFFFFF",
	"55AAEF050FFFFFFF",
	"55AAEF051FFFFFFF",
	"55AAEF053FFFFFFF",
	"55AAEF057FFFFFFF",
	"55AAEF05FFFFFFFF",
	"55AAEF0600000001",
	"55AAEF0600000003",
	"55AAEF0600000007",
	"55AAEF060000000F",
	"55AAEF060000001F",
	"55AAEF060000003F",
	"55AAEF060000007F",
	"55AAEF06000000FF",
	"55AAEF06000001FF",
	"55AAEF06000003FF",
	"55AAEF06000007FF",
	"55AAEF0600000FFF",
	"55AAEF0600001FFF",
	"55AAEF0600003FFF",
	"55AAEF0600007FFF",
	"55AAEF060000FFFF",
	"55AAEF060001FFFF",
	"55AAEF060003FFFF",
	"55AAEF060007FFFF",
	"55AAEF06000FFFFF",
	"55AAEF06001FFFFF",
	"55AAEF06003FFFFF",
	"55AAEF06007FFFFF",
	"55AAEF0600FFFFFF",
	"55AAEF0601FFFFFF",
	"55AAEF0603FFFFFF",
	"55AAEF0607FFFFFF",
	"55AAEF060FFFFFFF",
	"55AAEF061FFFFFFF",
	"55AAEF063FFFFFFF",
	"55AAEF067FFFFFFF",
	"55AAEF06FFFFFFFF",
	NULL
};

// called while initializing DualMiner when mining scrypt in scrypt-only (not dual-mode)
static
const char *scrypt_only_init_cmd[] =
{
	"55AAEF0200000000",
	"55AAEF0300000000",
	"55AAEF0400000000",
	"55AAEF0500000000",
	"55AAEF0600000000",
	"55AAEF3040000000",
	"55AA1F2810000000",
	"55AA1F2813000000",
	NULL
};

bool opt_dual_mode = false;

void gc3355_reset_dtr(int fd)
{
	// set data terminal ready (DTR) status
	set_serial_dtr(fd, BGV_HIGH);
	cgsleep_ms(GC3355_COMMAND_DELAY_MS);
	set_serial_dtr(fd, BGV_LOW);
}

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
	cgsleep_ms(GC3355_COMMAND_DELAY_MS);
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
		cgsleep_ms(GC3355_COMMAND_DELAY_MS);
	}
}

void gc3355_scrypt_only_reset(int fd)
{
	const char *initscrypt_ob[] =
	{
		"55AA1F2810000000",
		"55AA1F2813000000",
		NULL
	};

	gc3355_send_cmds(fd, initscrypt_ob);
}

static
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
			cgsleep_ms(GC3355_COMMAND_DELAY_MS);
		}
	}
	else if (unit_count == 0)
		gc3355_send_cmds(fd, sha2_gating_cmd);
}

void gc3355_opt_scrypt_only_init(int fd)
{
	gc3355_send_cmds(fd, scrypt_only_init_cmd);

	gc3355_set_pll_freq(fd, opt_pll_freq);
}

void gc3355_dualminer_init(int fd)
{

	const char *init_ob[] =
	{
#if 1
		"55AAEF0200000000",
		"55AAEF0300000000",
		"55AAEF0400000000",
		"55AAEF0500000000",
		"55AAEF0600000000",
#endif
		"55AAEF3020000000",
		"55AA1F2817000000",
		NULL
	};
	const char *initscrypt_ob[] =
	{
		"55AA1F2814000000",
		"55AA1F2817000000",
		NULL
	};

	if (opt_scrypt)
		gc3355_send_cmds(fd, initscrypt_ob);
	else
		gc3355_send_cmds(fd, init_ob);

	if (!opt_scrypt)
		gc3355_set_pll_freq(fd, opt_pll_freq);
}

void gc3355_init(int fd, int sha2_units, bool is_scrypt_only)
{
	if (opt_scrypt)
	{
		if (is_scrypt_only)
			gc3355_opt_scrypt_only_init(fd);
	}
	else
		gc3355_open_sha2_units(fd, sha2_units);
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
