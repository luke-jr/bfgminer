/*
 * Copyright 2014 Luke Dashjr
 * Copyright 2013 Zefir Kurtisi
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

#include "deviceapi.h"
#include "driver-aan.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

#define AAN_DEFAULT_NONCE_PDIFF  8

#define AAN_PROBE_TIMEOUT_US  3750000
#define AAN_INIT_TIMEOUT_US   5000000
#define AAN_READ_INTERVAL_US   100000

#define AAN_REGISTER_SIZE  6

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
	
	uint8_t * const rx = spi_getrxbuf(spi);
	while (true)
	{
		spi_emit_nop(spi, 2);
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

bool aan_read_reg_direct(struct spi_port * const spi, const uint8_t chip, void * const out_buf, const struct timeval * const tvp_timeout)
{
	if (!aan_spi_cmd_send(spi, AAN_READ_REG, chip, NULL, 0))
		return false;
	if (!aan_spi_cmd_resp(spi, AAN_READ_REG_RESP, chip, tvp_timeout))
		return false;
	
	spi_emit_nop(spi, AAN_REGISTER_SIZE);
	if (!spi_txrx(spi))
		applogr(false, LOG_DEBUG, "%s: %s failed", __func__, "spi_txrx");
	
	uint8_t * const rx = spi_getrxbuf(spi);
	memcpy(out_buf, rx, AAN_REGISTER_SIZE);
	
	return true;
}

static inline
bool aan_read_reg(struct spi_port * const spi, const uint8_t chip, void * const out_buf, const struct timeval * const tvp_timeout)
{
	const struct aan_hooks * const hooks = spi->userp;
	return hooks->read_reg(spi, chip, out_buf, tvp_timeout);
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
		out_chipcount[i] = -1;
	}
	
	do {
		for (int i = 0; i < spi_n; ++i)
		{
			if (state[i] == -1)
				continue;
			struct spi_port * const spi = spi_a[i];
			spi_emit_nop(spi, 2);
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
				spi_clear_buf(spi);
				continue;
			}
			aan_spi_parse_rx(spi);
		}
	} while (completed < spi_n && likely(!timer_passed(&tv_timeout, NULL)));
	
	applog(LOG_DEBUG, "%s completed for %d out of %d SPI ports", __func__, completed, spi_n);
	
	return completed;
}

bool aan_init(struct thr_info * const master_thr)
{
	struct cgpu_info * const master_dev = master_thr->cgpu, *dev = NULL;
	struct aan_board_data *board;
	struct timeval tv_timeout, tv_now;
	int chipid;
	for_each_managed_proc(proc, master_dev)
	{
		struct spi_port * const spi = proc->device_data;
		struct thr_info * const thr = proc->thr[0];
		
		if (dev != proc->device)
		{
			dev = proc->device;
			chipid = 0;
			timer_set_now(&tv_now);
			board = malloc(sizeof(*board));
			*board = (struct aan_board_data){
				.master_dev = master_dev,
				.spi = spi,
				.tv_next_poll = tv_now,
			};
			spi->cgpu = dev;
			
			while (true)
			{
				timer_set_delay(&tv_timeout, &tv_now, AAN_INIT_TIMEOUT_US);
				if (aan_spi_cmd(spi, AAN_BIST_FIX, AAN_ALL_CHIPS, NULL, 0, &tv_timeout))
					break;
				applog(LOG_ERR, "%s: Failed to %s", proc->dev_repr, "BIST_FIX");
			}
		}
		
		proc->device_data = board;
		struct aan_chip_data * const chip = malloc(sizeof(*chip));
		thr->cgpu_data = chip;
		thr->queue_full = true;
		*chip = (struct aan_chip_data){
			.chipid = ++chipid,
			.desired_nonce_pdiff = AAN_DEFAULT_NONCE_PDIFF,
		};
	}
	master_thr->tv_poll = tv_now;
	
	return true;
}

static
bool aan_spi_send_work(struct spi_port * const spi, const uint8_t chipid, const uint8_t jobid, const struct work * const work)
{
	uint8_t buf[0x38];
	
	swab256(&buf[0], work->midstate);
	swap32yes(&buf[0x20], &work->data[0x40], 3);
	memset(&buf[0x2c], 0, 4);         // start nonce
	uint32_t compressed_target = (uint32_t)(0x10000 / work->nonce_diff) | (/*exponent*/ 0x1d << 24);
	pk_u32le(buf, 0x30, compressed_target);
	memset(&buf[0x34], 0xff, 4);      // end nonce
	
	return aan_spi_cmd_send(spi, AAN_WRITE_JOB | (jobid << 4), chipid, buf, sizeof(buf));
}

static bool set_work(struct cgpu_info *, uint8_t, struct work *);

bool aan_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info *proc = thr->cgpu;
	struct aan_chip_data * const chip = thr->cgpu_data;
	struct cgpu_info *dev = proc->device;
	struct aan_board_data *board = dev->device_data;
	struct cgpu_info * const master_dev = board->master_dev;
	struct aan_board_data * const master_board = master_dev->device_data;
	
	applog(LOG_DEBUG, "%s: queue_append queues_empty=%d", proc->proc_repr, master_board->queues_empty-1);
	
	work->nonce_diff = work->work_difficulty;
	if (work->nonce_diff > chip->desired_nonce_pdiff)
		work->nonce_diff = chip->desired_nonce_pdiff;
	chip->current_nonce_pdiff = work->nonce_diff;
	
	if (set_work(dev, proc->proc_id + 1, work))
		hashes_done2(thr, 0x100000000, NULL);
	
	thr->queue_full = true;
	if (!--master_board->queues_empty)
	{
		struct thr_info * const master_thr = master_dev->thr[0];
		
		// Reactivate polling
		dev = NULL;
		for_each_managed_proc(proc, master_dev)
		{
			if (dev == proc->device)
				continue;
			dev = proc->device;
			board = dev->device_data;
			
			reduce_timeout_to(&master_thr->tv_poll, &board->tv_next_poll);
		}
	}
	return true;
}

void aan_queue_flush(struct thr_info * const thr)
{
	// TODO
}

struct cgpu_info *aan_proc_for_chipid(struct cgpu_info * const dev, const int chipid)
{
	struct cgpu_info *proc = dev;
	for (int i = 1; i < chipid; ++i)
	{
		proc = proc->next_proc;
		if (unlikely((!proc) || proc->device != dev))
		{
badchipid:
			inc_hw_errors_only(dev->thr[0]);
			applogr(NULL, LOG_ERR, "%s: Chip number %d out of range", dev->dev_repr, chipid);
		}
	}
	if (unlikely(!chipid))
		goto badchipid;
	return proc;
}

static
void aan_spi_parse_rx(struct spi_port * const spi)
{
	spi_clear_buf(spi);
}

#define MAX_POLL_NUM   20

/* set work for given chip, returns true if a nonce range was finished */
static
bool set_work(struct cgpu_info * const dev, const uint8_t chip_id, struct work * const work)
{
	struct aan_board_data * const board = dev->device_data;
	struct spi_port * const spi = board->spi;
	
	struct cgpu_info * const proc = aan_proc_for_chipid(dev, chip_id);
	struct thr_info * const thr = proc->thr[0];
	struct aan_chip_data * const chip = thr->cgpu_data;
	bool retval = false;
	
	++chip->last_jobid;
	chip->last_jobid &= 3;

	if (chip->works[chip->last_jobid] != NULL)
	{
		free_work(chip->works[chip->last_jobid]);
		chip->works[chip->last_jobid] = NULL;
		retval = true;
	}
	
	if (!aan_spi_send_work(spi, chip_id, chip->last_jobid + 1, work))
	{
		free_work(work);
		applog(LOG_ERR, "%"PRIpreprv": Failed to set work %d", proc->proc_repr, chip->last_jobid + 1);
	}
	else
		chip->works[chip->last_jobid] = work;
	spi_clear_buf(spi);
	
	return retval;
}

/* check for pending results in a chain, returns false if output queue empty */
static
bool get_nonce(struct cgpu_info * const dev, uint8_t * const nonce, uint8_t * const chip, uint8_t * const job_id)
{
	struct aan_board_data * const board = dev->device_data;
	struct spi_port * const spi = board->spi;
	
	int pollLen = MAX_POLL_NUM * dev->procs;
	if (pollLen <= 0)
		pollLen = MAX_POLL_NUM;
	
	if (!aan_spi_cmd_send(spi, AAN_READ_RESULT, AAN_ALL_CHIPS, NULL, 0))
		return false;
	
	for (int i = 0; i < pollLen; ++i)
	{
		spi_clear_buf(spi);
		spi_emit_nop(spi, 2);
		if (!spi_txrx(spi))
			applogr(false, LOG_ERR, "%s: SPI error in get_nonce", dev->dev_repr);
		uint8_t * const spi_rx = spi_getrxbuf(spi);
		if (spi_rx[0] == AAN_READ_RESULT && spi_rx[1] == 0x00)
			applogr(false, LOG_DEBUG, "%s: Output queue empty", dev->dev_repr);
		if ((spi_rx[0] & 0x0f) == AAN_READ_RESULT && spi_rx[1] != 0)
		{
			*job_id = spi_rx[0] >> 4;
			*chip = spi_rx[1];
			
			spi_emit_nop(spi, 2);
			if (!spi_txrx(spi))
				applogr(false, LOG_ERR, "SPI Err(%s):get_nonce", dev->dev_repr);
			memcpy(nonce, spi_rx, 4);
			
			applog(LOG_DEBUG, "%s: Got nonce for chip %d / job_id %d", dev->dev_repr, *chip, *job_id);
			
			return true;
		}
	}
	
	return false;
}

static
void aan_scanwork(struct cgpu_info * const dev, struct thr_info * const master_thr)
{
	struct aan_board_data * const board = dev->device_data;
	struct spi_port * const spi = board->spi;
	
	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	bool work_updated = false;
	
	if (!timer_passed(&board->tv_next_poll, NULL))
		goto out;
	
	while (get_nonce(dev, (uint8_t*)&nonce, &chip_id, &job_id))
	{
		nonce = bswap_32(nonce);
		work_updated = true;
		struct cgpu_info * const proc = aan_proc_for_chipid(dev, chip_id);
		if (!proc)
			continue;
		struct thr_info * const thr = proc->thr[0];
		struct aan_chip_data * const chip = thr->cgpu_data;
		if (job_id < 1 || job_id > 4)
		{
badjob:
			inc_hw_errors3(thr, NULL, &nonce, chip->current_nonce_pdiff);
			continue;
		}
		struct work * const work = chip->works[job_id - 1];
		if (!work)
			goto badjob;
		submit_nonce(thr, work, nonce);
	}
	
	/* check for completed works */
	for_each_logical_proc(proc, dev)
	{
		struct thr_info * const thr = proc->thr[0];
		const int i = proc->proc_id;
		uint8_t reg[AAN_REGISTER_SIZE];
		
		if (!aan_read_reg(spi, i + 1, reg, NULL))
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to read reg", proc->proc_repr);
			continue;
		}
		if ((reg[3] & 2) != 2)
		{
			struct cgpu_info * const master_dev = board->master_dev;
			struct aan_board_data * const master_board = master_dev->device_data;
			
			work_updated = true;
			thr->queue_full = false;
			++master_board->queues_empty;
			applog(LOG_DEBUG, "%s: queue_full=false queues_empty=%d", proc->proc_repr, master_board->queues_empty);
		}
	}
	
	if (!work_updated)
		timer_set_delay_from_now(&board->tv_next_poll, AAN_READ_INTERVAL_US);

out:
	reduce_timeout_to(&master_thr->tv_poll, &board->tv_next_poll);
}

void aan_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const master_dev = master_thr->cgpu, *dev = NULL;
	struct aan_board_data * const master_board = master_dev->device_data;
	
	timer_unset(&master_thr->tv_poll);
	
	for_each_managed_proc(proc, master_dev)
	{
		if (dev == proc->device)
			continue;
		dev = proc->device;
		
		aan_scanwork(dev, master_thr);
	}
	
	if (master_board->queues_empty)
		// Avoid polling when we have queues to fill
		timer_unset(&master_thr->tv_poll);
}

const char *aan_set_diff(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct thr_info * const thr = proc->thr[0];
	struct aan_chip_data * const chip = thr->cgpu_data;
	
	const double nv = atof(newvalue);
	if (nv <= 0)
		return "Invalid difficulty";
	
	chip->desired_nonce_pdiff = nv;
	
	return NULL;
}

const struct bfg_set_device_definition aan_set_device_funcs[] = {
	{"diff", aan_set_diff, "desired nonce difficulty"},
	{NULL},
};
