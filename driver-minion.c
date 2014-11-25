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
#include <uthash.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

static const uint8_t minion_max_chipid = 0x1f;
static const uint8_t minion_chip_signature[] = {0x44, 0x8a, 0xac, 0xb1};
static const unsigned minion_max_queued = 0x10;
static const unsigned minion_poll_us = 10000;

enum minion_register {
	MRA_SIGNATURE        = 0x00,
	MRA_STATUS           = 0x01,
	MRA_MISC_CTL         = 0x06,
	MRA_FIFO_STATUS      = 0x0b,
	
	MRA_RESULT           = 0x20,
	
	MRA_TASK             = 0x30,
	
	MRA_NONCE_START      = 0x70,
	MRA_NONCE_INC        = 0x71,
};

struct minion_chip {
	uint8_t chipid;
	uint8_t core_count;
	uint16_t next_taskid;
	struct cgpu_info *first_proc;
};

struct minion_bus {
	struct spi_port *spi;
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

static
bool minion_init(struct thr_info * const thr)
{
	struct cgpu_info * const dev = thr->cgpu, *proc = dev;
	struct minion_bus * const mbus = dev->device_data;
	struct spi_port * const spi = mbus->spi;
	uint8_t buf[max(4, sizeof(minion_chip_signature))];
	
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
		};
		minion_set(spi, chipid, MRA_NONCE_START, "\0\0\0\0", 4);
		pk_u32le(buf, 0, 0xffffffff / chip->core_count);
		minion_set(spi, chipid, MRA_NONCE_INC, buf, 4);
		
		minion_get(spi, chipid, MRA_MISC_CTL, buf, 4);
		buf[0] |= 1 << 2;  // Enable "no nonce" result reports
		minion_set(spi, chipid, MRA_MISC_CTL, buf, 4);
		
		timer_set_delay_from_now(&proc->thr[0]->tv_poll, minion_poll_us);
		
		for (unsigned i = 0; i < chip->core_count; ++i)
		{
			struct thr_info * const thr = proc->thr[0];
			
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
	
	const bool full = (HASH_COUNT(thr->work) >= minion_max_queued);
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
	
	pk_u16be(taskdata, 0, work->device_id);
	memset(&taskdata[2], 0, 2);
	memcpy(&taskdata[4], work->midstate, 0x20);
	memcpy(&taskdata[0x24], &work->data[0x40], 0xc);
	
	minion_set(spi, chipid, MRA_TASK, taskdata, sizeof(taskdata));
	
	HASH_ADD(hh, thr->work, device_id, sizeof(work->device_id), work);
	
	minion_queue_full(chip);
	return true;
}

static
void minion_queue_flush(struct thr_info * const thr)
{
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
			HASH_FIND(hh, chip_thr->work, &taskid, sizeof(taskid), work);
			if (unlikely(!work))
			{
				inc_hw_errors_only(core_thr);
				applog(LOG_ERR, "%"PRIpreprv": Unknown task %"PRIwdi, proc->proc_repr, taskid);
				continue;
			}
			
			if (have_nonce)
			{
				const uint32_t nonce = upk_u32le(resbuf_i, 4);
				
				submit_nonce(core_thr, work, nonce);
			}
			
			// Delete the previous work
			uint16_t taskid_truncated = taskid;
			--taskid_truncated;
			taskid = taskid_truncated;
			HASH_FIND(hh, chip_thr->work, &taskid, sizeof(taskid), work);
			if (work)
			{
				HASH_DEL(chip_thr->work, work);
				free_work(work);
			}
		}
		minion_queue_full(chip);
	}
	
	timer_set_delay_from_now(&chip_thr->tv_poll, minion_poll_us);
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
	spi->txrx = linux_spi_txrx2;
	if (spi_open(spi, devpath) < 0)
	{
		free(spi);
		applogr(false, LOG_ERR, "%s: Failed to open %s", minion_drv.dname, devpath);
	}
	
	spi->repr = minion_drv.dname;
	spi->logprio = LOG_WARNING;
	const unsigned total_core_count = minion_count_cores(spi);
	
	struct minion_bus * const mbus = malloc(sizeof(*mbus));
	*mbus = (struct minion_bus){
		.spi = spi,
	};
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &minion_drv,
		.device_path = strdup(devpath),
		.device_data = mbus,
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
	
	.queue_append = minion_queue_append,
	.queue_flush = minion_queue_flush,
	.poll = minion_poll,
};
