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
#include <utlist.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

static const uint8_t minion_max_chipid = 0x1f;
static const uint8_t minion_chip_signature[] = {0x44, 0x8a, 0xac, 0xb1};
static const unsigned minion_max_queued = 0x10;
static const unsigned minion_poll_us = 10000;
static const unsigned minion_min_clock =  800;
static const unsigned minion_max_clock = 1999;
static const unsigned long minion_temp_interval_us = 5273437;

enum minion_register {
	MRA_SIGNATURE        = 0x00,
	MRA_STATUS           = 0x01,
	MRA_TEMP_CFG         = 0x03,
	MRA_PLL_CFG          = 0x04,
	MRA_MISC_CTL         = 0x06,
	MRA_RESET            = 0x07,
	MRA_FIFO_STATUS      = 0x0b,
	
	MRA_CORE_EN_         = 0x10,
	
	MRA_RESULT           = 0x20,
	
	MRA_TASK             = 0x30,
	
	MRA_NONCE_START      = 0x70,
	MRA_NONCE_INC        = 0x71,
};

struct minion_chip {
	uint8_t chipid;
	uint8_t core_count;
	uint8_t core_enabled_count;
	uint16_t next_taskid;
	struct cgpu_info *first_proc;
	unsigned queue_count;
	uint32_t core_nonce_inc;
	uint32_t pllcfg_asserted;
	uint32_t pllcfg_desired;
	struct timeval tv_read_temp;
	unsigned long timeout_us;
	struct timeval tv_timeout;
};

struct minion_bus {
	struct spi_port *spi;
};

static const uint8_t minion_crystal_mhz = 12;

static
uint32_t minion_freq_to_pllcfg(unsigned freq)
{
	uint32_t rv;
	uint8_t * const pllcfg = (void*)&rv;
	uint8_t best_rem = 12, pll_dm = 1;
	for (uint8_t try_dm = 1; try_dm <= 8; ++try_dm)
	{
		const unsigned x = freq * try_dm;
		if (x > 0x100 * minion_crystal_mhz)
			// We'd overflow pll_dn to continue
			break;
		const uint8_t rem = x % minion_crystal_mhz;
		if (rem > best_rem)
			continue;
		best_rem = rem;
		pll_dm = try_dm;
		if (!rem)
			break;
	}
	const unsigned pll_dn = freq * pll_dm / minion_crystal_mhz;
	freq = pll_dn * minion_crystal_mhz / pll_dm;
	const uint8_t pll_cont = ((freq - 800) / 300);  // 2 bits
	static const uint8_t pll_dp   = 0;  // 3 bits
	static const uint8_t pll_byp  = 0;  // 1 bit
	static const uint8_t pll_div2 = 0;  // 1 bit
	static const uint8_t sys_div  = 1;  // 3 bits
	pllcfg[0] = pll_dn - 1;
	pllcfg[1] = (pll_dm - 1) | (pll_dp << 4);
	pllcfg[2] = pll_cont | (pll_byp << 2) | (pll_div2 << 4) | (sys_div << 5);
	pllcfg[3] = 0;
	return rv;
}

static
unsigned minion_pllcfg_to_freq(const uint32_t in_pllcfg)
{
	const uint8_t * const pllcfg = (void*)&in_pllcfg;
	const unsigned pll_dn = (unsigned)pllcfg[0] + 1;
	const uint8_t pll_dm = (pllcfg[1] & 0xf) + 1;
	const unsigned freq = pll_dn * minion_crystal_mhz / pll_dm;
	// FIXME: How to interpret the rest of the pll cfg?
	if (minion_freq_to_pllcfg(freq) != in_pllcfg)
		return 0;
	return freq;
}

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
void minion_set(struct spi_port * const spi, const uint8_t chipid, const uint8_t addr, const void * const buf, const size_t bufsz)
{
	const uint8_t header[] = {chipid, addr, bufsz & 0xff, bufsz >> 8};
	spi_clear_buf(spi);
	spi_emit_buf(spi, header, sizeof(header));
	spi_emit_buf(spi, buf, bufsz);
	spi_txrx(spi);
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

static inline
void minion_config_pll(struct spi_port * const spi, struct minion_chip * const chip)
{
	if (chip->pllcfg_asserted == chip->pllcfg_desired)
		return;
	const uint8_t chipid = chip->chipid;
	minion_set(spi, chipid, MRA_PLL_CFG, &chip->pllcfg_desired, 4);
	chip->pllcfg_asserted = chip->pllcfg_desired;
	// NOTE: This assumes we only ever assert pllcfgs we can decode!
	chip->timeout_us = 0xffffffff / minion_pllcfg_to_freq(chip->pllcfg_asserted);
	timer_set_delay_from_now(&chip->tv_timeout, chip->timeout_us);
}

static inline
void minion_core_enable_register_position(const uint8_t coreid, uint8_t * const corereg, uint8_t * const corebyte, uint8_t * const corebit)
{
	*corereg = MRA_CORE_EN_ + (coreid >> 5);
	*corebyte = (coreid >> 3) % 4;
	*corebit = 1 << (coreid % 8);
}

static
void minion_reinit(struct cgpu_info * const first_proc, struct minion_chip * const chip, const struct timeval * const tvp_now)
{
	struct thr_info * const thr = first_proc->thr[0];
	struct minion_bus * const mbus = first_proc->device_data;
	struct spi_port * const spi = mbus->spi;
	const uint8_t chipid = chip->chipid;
	uint8_t buf[4];
	
	static const uint8_t resetcmd[4] = {0xff, 0xff, 0xa5, 0xf5};
	minion_set(spi, chipid, MRA_RESET, resetcmd, sizeof(resetcmd));
	
	minion_set(spi, chipid, MRA_NONCE_START, "\0\0\0\0", 4);
	chip->core_nonce_inc = 0xffffffff / chip->core_count;
	pk_u32le(buf, 0, chip->core_nonce_inc);
	minion_set(spi, chipid, MRA_NONCE_INC, buf, 4);
	
	minion_get(spi, chipid, MRA_TEMP_CFG, buf, 4);
	buf[0] &= ~(1 << 5);  // Enable temperature sensor
	buf[0] &= ~(1 << 4);  // 20 C precision (alternative is 40 C)
	minion_set(spi, chipid, MRA_TEMP_CFG, buf, 4);
	
	minion_get(spi, chipid, MRA_PLL_CFG, &chip->pllcfg_asserted, 4);
	
	minion_get(spi, chipid, MRA_MISC_CTL, buf, 4);
	buf[0] &= ~(1 << 4);  // Unpause cores
	buf[0] &= ~(1 << 3);  // Unpause queue
	buf[0] |= 1 << 2;  // Enable "no nonce" result reports
	buf[0] &= ~(1 << 1);  // Disable test mode
	minion_set(spi, chipid, MRA_MISC_CTL, buf, 4);
	
	thr->tv_poll = *tvp_now;
	chip->tv_read_temp = *tvp_now;
}

static
void minion_reenable_cores(struct cgpu_info * const first_proc, struct minion_chip * const chip)
{
	struct minion_bus * const mbus = first_proc->device_data;
	struct spi_port * const spi = mbus->spi;
	const uint8_t chipid = chip->chipid;
	uint8_t buf[4] = {0,0,0,0};
	struct cgpu_info *proc = first_proc;
	for (unsigned coreid = 0; coreid < chip->core_count; (proc = proc->next_proc), ++coreid)
	{
		uint8_t corereg, corebyte, corebit;
		minion_core_enable_register_position(coreid, &corereg, &corebyte, &corebit);
		if (proc->deven == DEV_ENABLED)
			buf[corebyte] |= corebit;
		if (coreid % 0x20 == 0x1f || coreid == chip->core_count - 1)
			minion_set(spi, chipid, corereg, buf, 4);
	}
}

static
bool minion_init(struct thr_info * const thr)
{
	struct cgpu_info * const dev = thr->cgpu, *proc = dev;
	struct minion_bus * const mbus = dev->device_data;
	struct spi_port * const spi = mbus->spi;
	uint8_t buf[max(4, sizeof(minion_chip_signature))];
	struct timeval tv_now;
	
	timer_set_now(&tv_now);
	
	struct minion_chip * const chips = malloc(sizeof(*chips) * ((size_t)minion_max_chipid + 1));
	for (unsigned chipid = 0; proc; ++chipid)
	{
		struct minion_chip * const chip = &chips[chipid];
		spi->repr = proc->proc_repr;
		
		minion_get(spi, chipid, MRA_SIGNATURE, buf, sizeof(minion_chip_signature));
		if (memcmp(buf, minion_chip_signature, sizeof(minion_chip_signature)))
			continue;
		
		minion_get(spi, chipid, MRA_STATUS, buf, 4);
		if (!buf[2])
			continue;
		
		*chip = (struct minion_chip){
			.chipid = chipid,
			.core_count = buf[2],
			.first_proc = proc,
			.pllcfg_desired = minion_freq_to_pllcfg(900),
		};
		minion_reinit(proc, chip, &tv_now);
		
		for (unsigned coreid = 0; coreid < chip->core_count; ++coreid)
		{
			struct thr_info * const thr = proc->thr[0];
			
			uint8_t corereg, corebyte, corebit;
			minion_core_enable_register_position(coreid, &corereg, &corebyte, &corebit);
			if (coreid % 0x20 == 0)
			{
				spi->repr = proc->proc_repr;
				minion_get(spi, chipid, corereg, buf, 4);
			}
			if (buf[corebyte] & corebit)
				++chip->core_enabled_count;
			else
				proc->deven = DEV_DISABLED;
			
			thr->cgpu_data = chip;
			
			proc = proc->next_proc;
		}
	}
	
	return true;
}

static
bool minion_queue_full(struct minion_chip * const chip)
{
	struct cgpu_info *proc = chip->first_proc;
	struct thr_info *thr = proc->thr[0];
	
	const bool full = (chip->queue_count >= minion_max_queued);
	if (full != thr->queue_full)
	{
		for (unsigned i = 0; i < chip->core_count; (proc = proc->next_proc), ++i)
		{
			thr = proc->thr[0];
			
			thr->queue_full = full;
		}
	}
	
	return full;
}

static
void minion_core_enabledisable(struct thr_info * const thr, const bool enable)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct minion_bus * const mbus = proc->device_data;
	struct minion_chip * const chip = thr->cgpu_data;
	struct spi_port * const spi = mbus->spi;
	const uint8_t chipid = chip->chipid;
	
	uint8_t coreid = 0;
	for (struct cgpu_info *p = chip->first_proc; p != proc; p = p->next_proc)
		++coreid;
	
	uint8_t corereg, corebyte, corebit;
	minion_core_enable_register_position(coreid, &corereg, &corebyte, &corebit);
	
	uint8_t buf[4];
	minion_get(spi, chipid, corereg, buf, 4);
	const uint8_t oldbyte = buf[corebyte];
	if (enable)
		buf[corebyte] |= corebit;
	else
		buf[corebyte] &= ~corebit;
	if (buf[corebyte] != oldbyte)
	{
		minion_set(spi, chipid, corereg, buf, 4);
		chip->core_enabled_count += enable ? 1 : -1;
	}
}

static
void minion_core_disable(struct thr_info * const thr)
{
	minion_core_enabledisable(thr, false);
}

static
void minion_core_enable(struct thr_info * const thr)
{
	minion_core_enabledisable(thr, true);
}

static
bool minion_queue_append(struct thr_info *thr, struct work * const work)
{
	struct cgpu_info *proc = thr->cgpu;
	struct minion_bus * const mbus = proc->device_data;
	struct minion_chip * const chip = thr->cgpu_data;
	proc = chip->first_proc;
	thr = proc->thr[0];
	
	if (minion_queue_full(chip))
		return false;
	
	struct spi_port * const spi = mbus->spi;
	const uint8_t chipid = chip->chipid;
	uint8_t taskdata[0x30];
	spi->repr = proc->proc_repr;
	
	work->device_id = ++chip->next_taskid;
	work->tv_stamp.tv_sec = 1;
	work->blk.nonce = 0;
	
	pk_u16be(taskdata, 0, work->device_id);
	memset(&taskdata[2], 0, 2);
	memcpy(&taskdata[4], work->midstate, 0x20);
	memcpy(&taskdata[0x24], &work->data[0x40], 0xc);
	
	minion_config_pll(spi, chip);
	minion_set(spi, chipid, MRA_TASK, taskdata, sizeof(taskdata));
	
	DL_APPEND(thr->work_list, work);
	++chip->queue_count;
	
	minion_queue_full(chip);
	return true;
}

static void minion_refill_queue(struct thr_info *);

static
void minion_queue_flush(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct minion_bus * const mbus = proc->device_data;
	struct minion_chip * const chip = thr->cgpu_data;
	if (proc != chip->first_proc)
		// Redundant, all queues flush at the same time
		return;
	const uint8_t chipid = chip->chipid;
	struct spi_port * const spi = mbus->spi;
	
	static const uint8_t flushcmd[4] = {0xfb, 0xff, 0xff, 0xff};
	minion_set(spi, chipid, MRA_RESET, flushcmd, sizeof(flushcmd));
	
	minion_refill_queue(thr);
}

static
void minion_refill_queue(struct thr_info * const thr)
{
	struct minion_chip * const chip = thr->cgpu_data;
	struct work *work;
	DL_FOREACH(thr->work_list, work)
	{
		work->tv_stamp.tv_sec = 0;
	}
	chip->queue_count = 0;
	minion_queue_full(chip);
}

static
void minion_hashes_done(struct cgpu_info *proc, const uint8_t core_count, const uint64_t hashes)
{
	for (int j = 0; j < core_count; (proc = proc->next_proc), ++j)
	{
		if (proc->deven != DEV_ENABLED)
			continue;
		struct thr_info * const thr = proc->thr[0];
		hashes_done2(thr, hashes, NULL);
	}
}

static
void minion_poll(struct thr_info * const chip_thr)
{
	struct cgpu_info * const first_proc = chip_thr->cgpu;
	struct minion_bus * const mbus = first_proc->device_data;
	struct minion_chip * const chip = chip_thr->cgpu_data;
	struct spi_port * const spi = mbus->spi;
	const uint8_t chipid = chip->chipid;
	spi->repr = first_proc->proc_repr;
	
	uint8_t buf[4];
	minion_get(spi, chipid, MRA_FIFO_STATUS, buf, 4);
	
	const uint8_t res_fifo_len = buf[0];
	if (res_fifo_len)
	{
		static const size_t resbuf_i_len = 8;
		const size_t resbuf_len = (size_t)res_fifo_len * resbuf_i_len;
		uint8_t resbuf[resbuf_len], *resbuf_i = resbuf;
		minion_get(spi, chipid, MRA_RESULT, resbuf, resbuf_len);
		
		for (unsigned i = 0; i < res_fifo_len; (resbuf_i += resbuf_i_len), ++i)
		{
			const uint8_t coreid = resbuf_i[2];
			work_device_id_t taskid = upk_u16be(resbuf_i, 0);
			const bool have_nonce = !(resbuf_i[3] & 0x80);
			struct cgpu_info *proc;
			struct thr_info *core_thr;
			bool clean = false;
			
			if (likely(coreid < chip->core_count))
			{
				proc = first_proc;
				for (int j = 0; j < coreid; ++j)
					proc = proc->next_proc;
				core_thr = proc->thr[0];
			}
			else
			{
				proc = first_proc;
				core_thr = proc->thr[0];
				inc_hw_errors_only(core_thr);
				applog(LOG_ERR, "%"PRIpreprv": Core id out of range (%u >= %u)", proc->proc_repr, coreid, chip->core_count);
			}
			
			struct work *work;
			DL_SEARCH_SCALAR(chip_thr->work_list, work, device_id, taskid);
			if (unlikely(!work))
			{
				inc_hw_errors_only(core_thr);
				applog(LOG_ERR, "%"PRIpreprv": Unknown task %"PRIwdi, proc->proc_repr, taskid);
				continue;
			}
			
			if (have_nonce)
			{
				const uint32_t nonce = upk_u32le(resbuf_i, 4);
				
				if (submit_nonce(core_thr, work, nonce))
				{
					clean = (coreid < chip->core_count);
					
					// It's only 0xffffffff if we prematurely considered it complete
					if (likely(work->blk.nonce != 0xffffffff))
					{
						uint32_t hashes = (nonce % chip->core_nonce_inc);
						if (hashes > work->blk.nonce)
						{
							hashes -= work->blk.nonce - 1;
							minion_hashes_done(first_proc, chip->core_count, hashes);
							work->blk.nonce = hashes + 1;
						}
					}
				}
			}
			else
			{
				const uint32_t hashes = chip->core_nonce_inc - work->blk.nonce;
				minion_hashes_done(first_proc, chip->core_count, hashes);
				work->blk.nonce = 0xffffffff;
			}
			
			// Flag previous work(s) as done, and delete them when we are sure
			struct work *work_tmp;
			DL_FOREACH_SAFE(chip_thr->work_list, work, work_tmp)
			{
				if (work->device_id == taskid)
					break;
				
				if (work->blk.nonce && work->blk.nonce != 0xffffffff)
				{
					// At least one nonce was found, assume the job completed
					const uint32_t hashes = chip->core_nonce_inc - work->blk.nonce;
					minion_hashes_done(first_proc, chip->core_count, hashes);
					work->blk.nonce = 0xffffffff;
				}
				if (work->tv_stamp.tv_sec)
				{
					--chip->queue_count;
					work->tv_stamp.tv_sec = 0;
				}
				if (clean)
				{
					DL_DELETE(chip_thr->work_list, work);
					free_work(work);
				}
			}
		}
		minion_queue_full(chip);
	}
	
	struct timeval tv_now;
	timer_set_now(&tv_now);
	
	if (timer_passed(&chip->tv_read_temp, &tv_now))
	{
		minion_get(spi, chipid, MRA_STATUS, buf, 4);
		const float temp = buf[3] * 20.;
		struct cgpu_info *proc = first_proc;
		for (int j = 0; j < chip->core_count; (proc = proc->next_proc), ++j)
			proc->temp = temp;
		timer_set_delay(&chip_thr->tv_poll, &tv_now, minion_temp_interval_us);
	}
	
	if (res_fifo_len)
		timer_set_delay(&chip->tv_timeout, &tv_now, chip->timeout_us);
	else
	if (timer_passed(&chip->tv_timeout, &tv_now))
	{
		applog(LOG_WARNING, "%"PRIpreprv": Chip timeout, reinitialising", first_proc->proc_repr);
		minion_reinit(first_proc, chip, &tv_now);
		minion_reenable_cores(first_proc, chip);
		minion_refill_queue(chip_thr);
	}
	
	minion_config_pll(spi, chip);
	
	timer_set_delay(&chip_thr->tv_poll, &tv_now, minion_poll_us);
}

static
struct api_data *minion_get_api_extra_device_status(struct cgpu_info * const proc)
{
	struct thr_info * const thr = proc->thr[0];
	struct minion_chip * const chip = thr->cgpu_data;
	struct api_data *root = NULL;
	double d;
	
	d = minion_pllcfg_to_freq(chip->pllcfg_asserted);
	if (d > 0)
		root = api_add_freq(root, "Frequency", &d, true);
	
	return root;
}

static
const char *minion_set_clock(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct thr_info * const thr = proc->thr[0];
	struct minion_chip * const chip = thr->cgpu_data;
	
	const int nv = atoi(newvalue);
	if (nv < minion_min_clock || nv > minion_max_clock)
	{
		sprintf(replybuf, "Clock frequency must be within range of %u-%u MHz", minion_min_clock, minion_max_clock);
		return replybuf;
	}
	
	const uint32_t pllcfg = minion_freq_to_pllcfg(nv);
	chip->pllcfg_desired = pllcfg;
	
	return NULL;
}

static const struct bfg_set_device_definition minion_set_device_funcs[] = {
	{"clock", minion_set_clock, "clock frequency"},
	{NULL},
};

#ifdef HAVE_CURSES
static
void minion_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock speed ");
}

static
const char *minion_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	struct thr_info * const thr = proc->thr[0];
	struct minion_chip * const chip = thr->cgpu_data;
	char buf[0x100];
	
	switch (input)
	{
		case 'c': case 'C':
		{
			sprintf(buf, "Set clock speed (range %d-%d)", minion_min_clock, minion_max_clock);
			const int nv = curses_int(buf);
			if (nv < minion_min_clock || nv > minion_max_clock)
				return "Invalid clock speed\n";
			
			const uint32_t pllcfg = minion_freq_to_pllcfg(nv);
			chip->pllcfg_desired = pllcfg;
			
			return "Clock speed changed\n";
		}
	}
	
	return NULL;
}

static
void minion_wlogprint_status(struct cgpu_info * const proc)
{
	struct thr_info * const thr = proc->thr[0];
	struct minion_chip * const chip = thr->cgpu_data;
	
	const unsigned freq = minion_pllcfg_to_freq(chip->pllcfg_asserted);
	if (freq)
		wlogprint("Clock speed: %u\n", freq);
}
#endif

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
	spi->txrx = linux_spi_txrx2;
	if (spi_open(spi, devpath) < 0)
	{
		free(spi);
		applogr(false, LOG_ERR, "%s: Failed to open %s", minion_drv.dname, devpath);
	}
	
	spi->repr = minion_drv.dname;
	spi->logprio = LOG_WARNING;
	const unsigned total_core_count = minion_count_cores(spi);
	
	if (!total_core_count)
	{
		free(spi);
		applogr(false, LOG_WARNING, "%s: Zero chips detected on %s", minion_drv.dname, devpath);
	}
	
	struct minion_bus * const mbus = malloc(sizeof(*mbus));
	*mbus = (struct minion_bus){
		.spi = spi,
	};
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &minion_drv,
		.device_path = strdup(devpath),
		.device_data = mbus,
		.set_device_funcs = minion_set_device_funcs,
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
	
	.thread_init = minion_init,
	.minerloop = minerloop_queue,
	
	.thread_disable = minion_core_disable,
	.thread_enable = minion_core_enable,
	
	.queue_append = minion_queue_append,
	.queue_flush = minion_queue_flush,
	.poll = minion_poll,
	
	.get_api_extra_device_status = minion_get_api_extra_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = minion_wlogprint_status,
	.proc_tui_wlogprint_choices = minion_tui_wlogprint_choices,
	.proc_tui_handle_choice = minion_tui_handle_choice,
#endif
};
