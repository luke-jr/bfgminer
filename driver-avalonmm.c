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
#include <unistd.h>

#include <utlist.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-vcom.h"
#include "miner.h"
#include "util.h"
#include "work2d.h"

#define AVALONMM_MAX_MODULES  4
#define AVALONMM_MAX_COINBASE_SIZE  (6 * 1024)
#define AVALONMM_MAX_MERKLES  20
#define AVALONMM_MAX_NONCE_DIFF  0x20

// Must be a power of two
#define AVALONMM_CACHED_JOBS  2

#define AVALONMM_NONCE_OFFSET  0x180

BFG_REGISTER_DRIVER(avalonmm_drv)
static const struct bfg_set_device_definition avalonmm_set_device_funcs[];

#define AVALONMM_PKT_DATA_SIZE  0x20
#define AVALONMM_PKT_SIZE  (AVALONMM_PKT_DATA_SIZE + 7)

enum avalonmm_cmd {
	AMC_DETECT     = 0x0a,
	AMC_NEW_JOB    = 0x0b,
	AMC_JOB_ID     = 0x0c,
	AMC_COINBASE   = 0x0d,
	AMC_MERKLES    = 0x0e,
	AMC_BLKHDR     = 0x0f,
	AMC_POLL       = 0x10,
	AMC_TARGET     = 0x11,
	AMC_START      = 0x13,
};

enum avalonmm_reply {
	AMR_NONCE      = 0x17,
	AMR_STATUS     = 0x18,
	AMR_DETECT_ACK = 0x19,
};

static
bool avalonmm_write_cmd(const int fd, const enum avalonmm_cmd cmd, const void *data, size_t datasz)
{
	uint8_t packets = ((datasz + AVALONMM_PKT_DATA_SIZE - 1) / AVALONMM_PKT_DATA_SIZE) ?: 1;
	uint8_t pkt[AVALONMM_PKT_SIZE] = {'A', 'V', cmd, 1, packets};
	uint16_t crc;
	ssize_t r;
	while (true)
	{
		size_t copysz = AVALONMM_PKT_DATA_SIZE;
		if (datasz < copysz)
		{
			copysz = datasz;
			memset(&pkt[5 + copysz], '\0', AVALONMM_PKT_DATA_SIZE - copysz);
		}
		if (copysz)
			memcpy(&pkt[5], data, copysz);
		crc = crc16xmodem(&pkt[5], AVALONMM_PKT_DATA_SIZE);
		pk_u16be(pkt, 5 + AVALONMM_PKT_DATA_SIZE, crc);
		r = write(fd, pkt, sizeof(pkt));
		if (opt_dev_protocol)
		{
			char hex[(sizeof(pkt) * 2) + 1];
			bin2hex(hex, pkt, sizeof(pkt));
			applog(LOG_DEBUG, "DEVPROTO fd=%d SEND: %s => %d", fd, hex, (int)r);
		}
		if (sizeof(pkt) != r)
			return false;
		datasz -= copysz;
		if (!datasz)
			break;
		data += copysz;
		++pkt[3];
	}
	return true;
}

static
ssize_t avalonmm_read(const int fd, const int logprio, enum avalonmm_reply *out_reply, void * const bufp, size_t bufsz)
{
	uint8_t *buf = bufp;
	uint8_t pkt[AVALONMM_PKT_SIZE];
	uint8_t packets = 0, got = 0;
	uint16_t good_crc, actual_crc;
	ssize_t r;
	while (true)
	{
		r = serial_read(fd, pkt, sizeof(pkt));
		if (opt_dev_protocol)
		{
			if (r >= 0)
			{
				char hex[(r * 2) + 1];
				bin2hex(hex, pkt, r);
				applog(LOG_DEBUG, "DEVPROTO fd=%d RECV: %s", fd, hex);
			}
			else
				applog(LOG_DEBUG, "DEVPROTO fd=%d RECV (%d)", fd, (int)r);
		}
		if (r != sizeof(pkt))
			return -1;
		if (memcmp(pkt, "AV", 2))
			applogr(-1, logprio, "%s: bad header", __func__);
		good_crc = crc16xmodem(&pkt[5], AVALONMM_PKT_DATA_SIZE);
		actual_crc = upk_u16le(pkt, 5 + AVALONMM_PKT_DATA_SIZE);
		if (good_crc != actual_crc)
			applogr(-1, logprio, "%s: bad CRC (good=%04x actual=%04x)", __func__, good_crc, actual_crc);
		*out_reply = pkt[2];
		if (!got)
		{
			if (pkt[3] != 1)
				applogr(-1, logprio, "%s: first packet is not index 1", __func__);
			++got;
			packets = pkt[4];
		}
		else
		{
			if (pkt[3] != ++got)
				applogr(-1, logprio, "%s: packet %d is not index %d", __func__, got, got);
			if (pkt[4] != packets)
				applogr(-1, logprio, "%s: packet %d total packet count is %d rather than original value of %d", __func__, got, pkt[4], packets);
		}
		if (bufsz)
		{
			if (likely(bufsz > AVALONMM_PKT_DATA_SIZE))
			{
				memcpy(buf, &pkt[5], AVALONMM_PKT_DATA_SIZE);
				bufsz -= AVALONMM_PKT_DATA_SIZE;
				buf += AVALONMM_PKT_DATA_SIZE;
			}
			else
			{
				memcpy(buf, &pkt[5], bufsz);
				bufsz = 0;
			}
		}
		if (got == packets)
			break;
	}
	return (((ssize_t)got) * AVALONMM_PKT_DATA_SIZE);
}

struct avalonmm_init_data {
	int module_id;
	uint32_t mmversion;
};

static
bool avalonmm_detect_one(const char * const devpath)
{
	uint8_t buf[AVALONMM_PKT_DATA_SIZE] = {0};
	enum avalonmm_reply reply;
	const int fd = serial_open(devpath, 115200, 1, true);
	struct cgpu_info *prev_cgpu = NULL;
	if (fd == -1)
		applogr(false, LOG_DEBUG, "%s: Failed to open %s", __func__, devpath);
	
	for (int i = 0; i < AVALONMM_MAX_MODULES; ++i)
	{
		pk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4, i);
		avalonmm_write_cmd(fd, AMC_DETECT, buf, AVALONMM_PKT_DATA_SIZE);
	}
	
	while (avalonmm_read(fd, LOG_DEBUG, &reply, buf, AVALONMM_PKT_DATA_SIZE) > 0)
	{
		if (reply != AMR_DETECT_ACK)
			continue;
		
		int moduleno = upk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4);
		uint32_t mmversion;
		{
			char mmver[5];
			memcpy(mmver, buf, 4);
			mmver[4] = '\0';
			mmversion = atol(mmver);
		}
		
		if (!prev_cgpu)
		{
			if (serial_claim_v(devpath, &avalonmm_drv))
			{
				serial_close(fd);
				return false;
			}
		}
		
		struct avalonmm_init_data * const initdata = malloc(sizeof(*initdata));
		*initdata = (struct avalonmm_init_data){
			.module_id = moduleno,
			.mmversion = mmversion,
		};
		
		struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
		*cgpu = (struct cgpu_info){
			.drv = &avalonmm_drv,
			.device_path = prev_cgpu ? prev_cgpu->device_path : strdup(devpath),
			.device_data = initdata,
			.set_device_funcs = avalonmm_set_device_funcs,
			.deven = DEV_ENABLED,
			.procs = 1,
			.threads = prev_cgpu ? 0 : 1,
		};
		
		add_cgpu_slave(cgpu, prev_cgpu);
		prev_cgpu = cgpu;
	}
	
	serial_close(fd);
	
	return prev_cgpu;
}

static
bool avalonmm_lowl_probe(const struct lowlevel_device_info * const info)
{
	return vcom_lowl_probe_wrapper(info, avalonmm_detect_one);
}

struct avalonmm_job {
	struct stratum_work swork;
	uint32_t jobid;
	struct timeval tv_prepared;
	double nonce_diff;
};

struct avalonmm_chain_state {
	uint32_t xnonce1;
	struct avalonmm_job *jobs[AVALONMM_CACHED_JOBS];
	uint32_t next_jobid;
	
	uint32_t fan_desired;
	uint32_t clock_desired;
	uint32_t voltcfg_desired;
};

struct avalonmm_module_state {
	uint32_t module_id;
	uint32_t mmversion;
	uint16_t temp[2];
	uint16_t fan[2];
	uint32_t clock_actual;
	uint32_t voltcfg_actual;
};

static
uint16_t avalonmm_voltage_config_from_dmvolts(uint32_t dmvolts)
{
	return ((uint16_t)bitflip8((0x78 - dmvolts / 125) << 1 | 1)) << 8;
}

// Potentially lossy!
static
uint32_t avalonmm_dmvolts_from_voltage_config(uint32_t voltcfg)
{
	return (0x78 - (bitflip8(voltcfg >> 8) >> 1)) * 125;
}

static
uint32_t avalonmm_fan_config_from_percent(uint8_t percent)
{
	return (0x3ff - percent * 0x3ff / 100);
}

static
uint8_t avalonmm_fan_percent_from_config(uint32_t cfg)
{
	return (0x3ff - cfg) * 100 / 0x3ff;
}

static struct cgpu_info *avalonmm_dev_for_module_id(struct cgpu_info *, uint32_t);
static bool avalonmm_poll_once(struct cgpu_info *, int64_t *);

static
bool avalonmm_init(struct thr_info * const master_thr)
{
	struct cgpu_info * const master_dev = master_thr->cgpu, *dev = NULL;
	struct avalonmm_init_data * const master_initdata = master_dev->device_data;
	const char * const devpath = master_dev->device_path;
	const int fd = serial_open(devpath, 115200, 1, true);
	uint8_t buf[AVALONMM_PKT_DATA_SIZE] = {0};
	int64_t module_id;
	
	master_dev->device_fd = fd;
	if (unlikely(fd == -1))
		applogr(false, LOG_ERR, "%s: Failed to initialise", master_dev->dev_repr);
	
	struct avalonmm_chain_state * const chain = malloc(sizeof(*chain));
	*chain = (struct avalonmm_chain_state){
		.fan_desired = avalonmm_fan_config_from_percent(90),
	};
	
	switch (master_initdata->mmversion)
	{
		case 2014:
			chain->voltcfg_desired = avalonmm_voltage_config_from_dmvolts(10000);
			break;
		default:
			chain->voltcfg_desired = avalonmm_voltage_config_from_dmvolts(6625);
	}
	
	work2d_init();
	if (!reserve_work2d_(&chain->xnonce1))
	{
		applog(LOG_ERR, "%s: Failed to reserve 2D work", master_dev->dev_repr);
		free(chain);
		serial_close(fd);
		return false;
	}
	
	for_each_managed_proc(proc, master_dev)
	{
		if (dev == proc->device)
			continue;
		dev = proc->device;
		
		struct thr_info * const thr = proc->thr[0];
		struct avalonmm_init_data * const initdata = dev->device_data;
		
		struct avalonmm_module_state * const module = malloc(sizeof(*module));
		*module = (struct avalonmm_module_state){
			.module_id = initdata->module_id,
			.mmversion = initdata->mmversion,
		};
		
		free(initdata);
		proc->device_data = chain;
		thr->cgpu_data = module;
	}
	
	dev = NULL;
	for_each_managed_proc(proc, master_dev)
	{
		cgpu_set_defaults(proc);
		proc->status = LIFE_INIT2;
	}
	
	if (!chain->clock_desired)
	{
		// Get a reasonable default frequency
		dev = master_dev;
		struct thr_info * const thr = dev->thr[0];
		struct avalonmm_module_state * const module = thr->cgpu_data;
		
resend:
		pk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4, module->module_id);
		avalonmm_write_cmd(fd, AMC_POLL, buf, AVALONMM_PKT_DATA_SIZE);
		
		while (avalonmm_poll_once(master_dev, &module_id))
		{
			if (module_id != module->module_id)
				continue;
			
			if (module->clock_actual)
			{
				chain->clock_desired = module->clock_actual;
				break;
			}
			else
				goto resend;
		}
		
		if (!chain->clock_desired)
		{
			switch (module->mmversion)
			{
				case 2014:
					chain->clock_desired = 1500;
					break;
				case 3314:
					chain->clock_desired = 450;
					break;
			}
		}
	}
	
	if (likely(chain->clock_desired))
		applog(LOG_DEBUG, "%s: Frequency is initialised with %d MHz", master_dev->dev_repr, chain->clock_desired);
	else
		applogr(false, LOG_ERR, "%s: No frequency detected, please use --set %s@%s:clock=MHZ", master_dev->dev_repr, master_dev->drv->dname, devpath);
	
	return true;
}

static
bool avalonmm_send_swork(const int fd, struct avalonmm_chain_state * const chain, const struct stratum_work * const swork, uint32_t jobid, double *out_nonce_diff)
{
	uint8_t buf[AVALONMM_PKT_DATA_SIZE];
	bytes_t coinbase = BYTES_INIT;
	
	int coinbase_len = bytes_len(&swork->coinbase);
	if (coinbase_len > AVALONMM_MAX_COINBASE_SIZE)
		return false;
	
	if (swork->merkles > AVALONMM_MAX_MERKLES)
		return false;
	
	pk_u32be(buf,    0, coinbase_len);
	
	const size_t xnonce2_offset = swork->nonce2_offset + work2d_pad_xnonce_size(swork) + work2d_xnonce1sz;
	pk_u32be(buf,    4, xnonce2_offset);
	
	pk_u32be(buf,    8, 4);  // extranonce2 size, but only 4 is supported - smaller sizes are handled by limiting the range
	pk_u32be(buf, 0x0c, 0x24);  // merkle_offset, always 0x24 for Bitcoin
	pk_u32be(buf, 0x10, swork->merkles);
	pk_u32be(buf, 0x14, 1);  // diff? poorly defined
	pk_u32be(buf, 0x18, 0);  // pool number - none of its business
	if (!avalonmm_write_cmd(fd, AMC_NEW_JOB, buf, 0x1c))
		return false;
	
	double nonce_diff = target_diff(swork->target);
	if (nonce_diff >= AVALONMM_MAX_NONCE_DIFF)
		set_target_to_pdiff(buf, nonce_diff = AVALONMM_MAX_NONCE_DIFF);
	else
		memcpy(buf, swork->target, 0x20);
	*out_nonce_diff = nonce_diff;
	if (!avalonmm_write_cmd(fd, AMC_TARGET, buf, 0x20))
		return false;
	
	pk_u32be(buf, 0, jobid);
	if (!avalonmm_write_cmd(fd, AMC_JOB_ID, buf, 4))
		return false;
	
	// Need to add extranonce padding and extranonce2
	bytes_cpy(&coinbase, &swork->coinbase);
	uint8_t *cbp = bytes_buf(&coinbase);
	cbp += swork->nonce2_offset;
	work2d_pad_xnonce(cbp, swork, false);
	cbp += work2d_pad_xnonce_size(swork);
	memcpy(cbp, &chain->xnonce1, work2d_xnonce1sz);
	cbp += work2d_xnonce1sz;
	if (!avalonmm_write_cmd(fd, AMC_COINBASE, bytes_buf(&coinbase), bytes_len(&coinbase)))
		return false;
	
	if (!avalonmm_write_cmd(fd, AMC_MERKLES, bytes_buf(&swork->merkle_bin), bytes_len(&swork->merkle_bin)))
		return false;
	
	uint8_t header_bin[0x80];
	memcpy(&header_bin[   0], swork->header1, 0x24);
	memset(&header_bin[0x24], '\0', 0x20);  // merkle root
	pk_u32be(header_bin, 0x44, swork->ntime);
	memcpy(&header_bin[0x48], swork->diffbits, 4);
	memset(&header_bin[0x4c], '\0', 4);  // nonce
	memcpy(&header_bin[0x50], bfg_workpadding_bin, 0x30);
	if (!avalonmm_write_cmd(fd, AMC_BLKHDR, header_bin, sizeof(header_bin)))
		return false;
	
	// Avalon MM cannot handle xnonce2_size other than 4, and works in big endian, so we use a range to ensure the following bytes match
	const int fixed_mm_xnonce2_bytes = (work2d_xnonce2sz >= 4) ? 0 : (4 - work2d_xnonce2sz);
	uint8_t mm_xnonce2_start[4];
	uint32_t xnonce2_range;
	memset(mm_xnonce2_start, '\0', 4);
	cbp += work2d_xnonce2sz;
	for (int i = 1; i <= fixed_mm_xnonce2_bytes; ++i)
		mm_xnonce2_start[fixed_mm_xnonce2_bytes - i] = cbp++[0];
	if (fixed_mm_xnonce2_bytes > 0)
		xnonce2_range = (1 << (8 * work2d_xnonce2sz)) - 1;
	else
		xnonce2_range = 0xffffffff;
	
	pk_u32be(buf, 0, chain->fan_desired);
	pk_u32be(buf, 4, chain->voltcfg_desired);
	pk_u32be(buf, 8, chain->clock_desired);
	memcpy(&buf[0xc], mm_xnonce2_start, 4);
	pk_u32be(buf, 0x10, xnonce2_range);
	if (!avalonmm_write_cmd(fd, AMC_START, buf, 0x14))
		return false;
	
	return true;
}

static
void avalonmm_free_job(struct avalonmm_job * const mmjob)
{
	stratum_work_clean(&mmjob->swork);
	free(mmjob);
}

static
bool avalonmm_update_swork_from_pool(struct cgpu_info * const master_dev, struct pool * const pool)
{
	struct avalonmm_chain_state * const chain = master_dev->device_data;
	const int fd = master_dev->device_fd;
	struct avalonmm_job *mmjob = malloc(sizeof(*mmjob));
	*mmjob = (struct avalonmm_job){
		.jobid = chain->next_jobid,
	};
	cg_rlock(&pool->data_lock);
	stratum_work_cpy(&mmjob->swork, &pool->swork);
	cg_runlock(&pool->data_lock);
	timer_set_now(&mmjob->tv_prepared);
	mmjob->swork.data_lock_p = NULL;
	if (!avalonmm_send_swork(fd, chain, &mmjob->swork, mmjob->jobid, &mmjob->nonce_diff))
	{
		avalonmm_free_job(mmjob);
		return false;
	}
	applog(LOG_DEBUG, "%s: Upload of job id %08lx complete", master_dev->dev_repr, (unsigned long)mmjob->jobid);
	++chain->next_jobid;
	
	struct avalonmm_job **jobentry = &chain->jobs[mmjob->jobid % AVALONMM_CACHED_JOBS];
	if (*jobentry)
		avalonmm_free_job(*jobentry);
	*jobentry = mmjob;
	
	return true;
}

static
struct cgpu_info *avalonmm_dev_for_module_id(struct cgpu_info * const master_dev, const uint32_t module_id)
{
	struct cgpu_info *dev = NULL;
	for_each_managed_proc(proc, master_dev)
	{
		if (dev == proc->device)
			continue;
		dev = proc->device;
		
		struct thr_info * const thr = dev->thr[0];
		struct avalonmm_module_state * const module = thr->cgpu_data;
		
		if (module->module_id == module_id)
			return dev;
	}
	return NULL;
}

static
bool avalonmm_poll_once(struct cgpu_info * const master_dev, int64_t *out_module_id)
{
	struct avalonmm_chain_state * const chain = master_dev->device_data;
	const int fd = master_dev->device_fd;
	uint8_t buf[AVALONMM_PKT_DATA_SIZE];
	enum avalonmm_reply reply;
	
	*out_module_id = -1;
	
	if (avalonmm_read(fd, LOG_ERR, &reply, buf, sizeof(buf)) < 0)
		return false;
	
	switch (reply)
	{
		case AMR_DETECT_ACK:
			break;
		
		case AMR_STATUS:
		{
			const uint32_t module_id = upk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4);
			struct cgpu_info * const dev = avalonmm_dev_for_module_id(master_dev, module_id);
			
			if (unlikely(!dev))
			{
				struct thr_info * const master_thr = master_dev->thr[0];
				applog(LOG_ERR, "%s: %s for unknown module id %lu", master_dev->dev_repr, "Status", (unsigned long)module_id);
				inc_hw_errors_only(master_thr);
				break;
			}
			
			*out_module_id = module_id;
			
			struct thr_info * const thr = dev->thr[0];
			struct avalonmm_module_state * const module = thr->cgpu_data;
			
			module->temp[0] = upk_u16be(buf,    0);
			module->temp[1] = upk_u16be(buf,    2);
			module->fan [0] = upk_u16be(buf,    4);
			module->fan [1] = upk_u16be(buf,    6);
			module->clock_actual = upk_u32be(buf, 8);
			module->voltcfg_actual = upk_u32be(buf, 0x0c);
			
			dev->temp = max(module->temp[0], module->temp[1]);
			
			break;
		}
		case AMR_NONCE:
		{
			const int fixed_mm_xnonce2_bytes = (work2d_xnonce2sz >= 4) ? 0 : (4 - work2d_xnonce2sz);
			const uint8_t * const backward_xnonce2 = &buf[8 + fixed_mm_xnonce2_bytes];
			const uint32_t nonce = upk_u32be(buf, 0x10) - AVALONMM_NONCE_OFFSET;
			const uint32_t jobid = upk_u32be(buf, 0x14);
			const uint32_t module_id = upk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4);
			struct cgpu_info * const dev = avalonmm_dev_for_module_id(master_dev, module_id);
			
			if (unlikely(!dev))
			{
				struct thr_info * const master_thr = master_dev->thr[0];
				applog(LOG_ERR, "%s: %s for unknown module id %lu", master_dev->dev_repr, "Nonce", (unsigned long)module_id);
				inc_hw_errors_only(master_thr);
				break;
			}
			
			*out_module_id = module_id;
			
			struct thr_info * const thr = dev->thr[0];
			
			bool invalid_jobid = false;
			if (unlikely((uint32_t)(chain->next_jobid - AVALONMM_CACHED_JOBS) > chain->next_jobid))
				// Jobs wrap around
				invalid_jobid = (jobid < chain->next_jobid - AVALONMM_CACHED_JOBS && jobid >= chain->next_jobid);
			else
				invalid_jobid = (jobid < chain->next_jobid - AVALONMM_CACHED_JOBS || jobid >= chain->next_jobid);
			struct avalonmm_job * const mmjob = chain->jobs[jobid % AVALONMM_CACHED_JOBS];
			if (unlikely(invalid_jobid || !mmjob))
			{
				applog(LOG_ERR, "%s: Bad job id %08lx", dev->dev_repr, (unsigned long)jobid);
				inc_hw_errors_only(thr);
				break;
			}
			
			uint8_t xnonce2[work2d_xnonce2sz];
			for (int i = 0; i < work2d_xnonce2sz; ++i)
				xnonce2[i] = backward_xnonce2[(work2d_xnonce2sz - 1) - i];
			
			work2d_submit_nonce(thr, &mmjob->swork, &mmjob->tv_prepared, xnonce2, chain->xnonce1, nonce, mmjob->swork.ntime, NULL, mmjob->nonce_diff);
			hashes_done2(thr, mmjob->nonce_diff * 0x100000000, NULL);
			break;
		}
	}
	
	return true;
}

static
void avalonmm_poll(struct cgpu_info * const master_dev, int n)
{
	int64_t dummy;
	while (n > 0)
	{
		if (avalonmm_poll_once(master_dev, &dummy))
			--n;
	}
}

static
struct thr_info *avalonmm_should_disable(struct cgpu_info * const master_dev)
{
	for_each_managed_proc(proc, master_dev)
	{
		struct thr_info * const thr = proc->thr[0];
		if (thr->pause || proc->deven != DEV_ENABLED)
			return thr;
	}
	return NULL;
}

static
void avalonmm_minerloop(struct thr_info * const master_thr)
{
	struct cgpu_info * const master_dev = master_thr->cgpu;
	const int fd = master_dev->device_fd;
	struct pool *nextpool = current_pool(), *pool = NULL;
	uint8_t buf[AVALONMM_PKT_DATA_SIZE] = {0};
	
	while (likely(!master_dev->shutdown))
	{
		if (avalonmm_should_disable(master_dev))
		{
			struct thr_info *thr;
			while ( (thr = avalonmm_should_disable(master_dev)) )
			{
				if (!thr->_mt_disable_called)
					if (avalonmm_write_cmd(fd, AMC_NEW_JOB, NULL, 0))
					{
						for_each_managed_proc(proc, master_dev)
						{
							struct thr_info * const thr = proc->thr[0];
							mt_disable_start(thr);
						}
					}
				notifier_read(thr->notifier);
			}
			for_each_managed_proc(proc, master_dev)
			{
				struct thr_info * const thr = proc->thr[0];
				mt_disable_finish(thr);
			}
		}
		
		master_thr->work_restart = false;
		if (!pool_has_usable_swork(nextpool))
			; // FIXME
		else
		if (avalonmm_update_swork_from_pool(master_dev, nextpool))
			pool = nextpool;
		
		while (likely(!(master_thr->work_restart || ((nextpool = current_pool()) != pool && pool_has_usable_swork(nextpool)) || avalonmm_should_disable(master_dev))))
		{
			cgsleep_ms(10);
			
			struct cgpu_info *dev = NULL;
			for_each_managed_proc(proc, master_dev)
			{
				if (dev == proc->device)
					continue;
				dev = proc->device;
				
				struct thr_info * const thr = dev->thr[0];
				struct avalonmm_module_state * const module = thr->cgpu_data;
				
				pk_u32be(buf, AVALONMM_PKT_DATA_SIZE - 4, module->module_id);
				
				avalonmm_write_cmd(fd, AMC_POLL, buf, AVALONMM_PKT_DATA_SIZE);
				avalonmm_poll(master_dev, 1);
			}
		}
	}
}

static
const char *avalonmm_set_clock(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct cgpu_info * const dev = proc->device;
	struct avalonmm_chain_state * const chain = dev->device_data;
	
	const int nv = atoi(newvalue);
	if (nv < 0)
		return "Invalid clock";
	
	chain->clock_desired = nv;
	
	return NULL;
}

static
const char *avalonmm_set_fan(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct cgpu_info * const dev = proc->device;
	struct avalonmm_chain_state * const chain = dev->device_data;
	
	const int nv = atoi(newvalue);
	if (nv < 0 || nv > 100)
		return "Invalid fan speed";
	
	chain->fan_desired = avalonmm_fan_config_from_percent(nv);
	
	return NULL;
}

static
const char *avalonmm_set_voltage(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct cgpu_info * const dev = proc->device;
	struct avalonmm_chain_state * const chain = dev->device_data;
	
	const long val = atof(newvalue) * 10000;
	if (val < 0 || val > 15000)
		return "Invalid voltage value";
	
	chain->voltcfg_desired = avalonmm_voltage_config_from_dmvolts(val);
	
	return NULL;
}

static const struct bfg_set_device_definition avalonmm_set_device_funcs[] = {
	{"clock", avalonmm_set_clock, "clock frequency"},
	{"fan", avalonmm_set_fan, "fan speed (0-100 percent)"},
	{"voltage", avalonmm_set_voltage, "voltage (0 to 1.5 volts)"},
	{NULL},
};

static
struct api_data *avalonmm_api_extra_device_detail(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct avalonmm_chain_state * const chain = dev->device_data;
	struct thr_info * const thr = dev->thr[0];
	struct avalonmm_module_state * const module = thr->cgpu_data;
	struct api_data *root = NULL;
	
	root = api_add_uint32(root, "Module Id", &module->module_id, false);
	root = api_add_uint32(root, "ExtraNonce1", &chain->xnonce1, false);
	
	return root;
}

static
struct api_data *avalonmm_api_extra_device_status(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct avalonmm_chain_state * const chain = dev->device_data;
	struct thr_info * const thr = dev->thr[0];
	struct avalonmm_module_state * const module = thr->cgpu_data;
	struct api_data *root = NULL;
	char buf[0x10];
	
	strcpy(buf, "Temperature");
	for (int i = 0; i < 2; ++i)
	{
		if (module->temp[i])
		{
			float temp = module->temp[i];
			buf[0xb] = '0' + i;
			root = api_add_temp(root, buf, &temp, true);
		}
	}
	
	{
		uint8_t fan_percent = avalonmm_fan_percent_from_config(chain->fan_desired);
		root = api_add_uint8(root, "Fan Percent", &fan_percent, true);
	}
	
	strcpy(buf, "Fan RPM ");
	for (int i = 0; i < 2; ++i)
	{
		if (module->fan[i])
		{
			buf[8] = '0' + i;
			root = api_add_uint16(root, buf, &module->fan[i], false);
		}
	}
	
	if (module->clock_actual)
	{
		double freq = module->clock_actual;
		root = api_add_freq(root, "Frequency", &freq, true);
	}
	
	if (module->voltcfg_actual)
	{
		float volts = avalonmm_dmvolts_from_voltage_config(module->voltcfg_actual);
		volts /= 10000;
		root = api_add_volts(root, "Voltage", &volts, true);
	}
	
	return root;
}

#ifdef HAVE_CURSES
static
void avalonmm_wlogprint_status(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct avalonmm_chain_state * const chain = dev->device_data;
	struct thr_info * const thr = dev->thr[0];
	struct avalonmm_module_state * const module = thr->cgpu_data;
	
	wlogprint("ExtraNonce1:%0*lx  ModuleId:%lu\n", work2d_xnonce1sz * 2, (unsigned long)chain->xnonce1, (unsigned long)module->module_id);
	
	if (module->temp[0] && module->temp[1])
	{
		wlogprint("Temperatures: %uC %uC", (unsigned)module->temp[0], (unsigned)module->temp[1]);
		if (module->fan[0] || module->fan[1])
			wlogprint("  ");
	}
	unsigned fan_percent = avalonmm_fan_percent_from_config(chain->fan_desired);
	if (module->fan[0])
	{
		if (module->fan[1])
			wlogprint("Fans: %u RPM, %u RPM (%u%%)", (unsigned)module->fan[0], (unsigned)module->fan[1], fan_percent);
		else
			wlogprint("Fan: %u RPM (%u%%)", (unsigned)module->fan[0], fan_percent);
	}
	else
	if (module->fan[1])
		wlogprint("Fan: %u RPM (%u%%)", (unsigned)module->fan[1], fan_percent);
	else
		wlogprint("Fan: %u%%", fan_percent);
	wlogprint("\n");
	
	if (module->clock_actual)
		wlogprint("Clock speed: %lu\n", (unsigned long)module->clock_actual);
	
	if (module->voltcfg_actual)
	{
		const uint32_t dmvolts = avalonmm_dmvolts_from_voltage_config(module->voltcfg_actual);
		wlogprint("Voltage: %u.%04u V\n", (unsigned)(dmvolts / 10000), (unsigned)(dmvolts % 10000));
	}
}

static
void avalonmm_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock speed ");
	wlogprint("[F]an speed ");
	wlogprint("[V]oltage ");
}

static
const char *avalonmm_tui_wrapper(struct cgpu_info * const proc, bfg_set_device_func_t func, const char * const prompt)
{
	static char replybuf[0x20];
	char * const cvar = curses_input(prompt);
	if (!cvar)
		return "Cancelled\n";
	
	const char *reply = func(proc, NULL, cvar, NULL, NULL);
	free(cvar);
	if (reply)
	{
		snprintf(replybuf, sizeof(replybuf), "%s\n", reply);
		return replybuf;
	}
	
	return "Successful\n";
}

static
const char *avalonmm_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	switch (input)
	{
		case 'c': case 'C':
			return avalonmm_tui_wrapper(proc, avalonmm_set_clock  , "Set clock speed (Avalon2: 1500; Avalon3: 450)");
		
		case 'f': case 'F':
			return avalonmm_tui_wrapper(proc, avalonmm_set_fan    , "Set fan speed (0-100 percent)");
		
		case 'v': case 'V':
			return avalonmm_tui_wrapper(proc, avalonmm_set_voltage, "Set voltage (Avalon2: 1.0; Avalon3: 0.6625)");
	}
	return NULL;
}
#endif

struct device_drv avalonmm_drv = {
	.dname = "avalonmm",
	.name = "AVM",
	
	.lowl_probe = avalonmm_lowl_probe,
	
	.thread_init = avalonmm_init,
	.minerloop = avalonmm_minerloop,
	
	.get_api_extra_device_detail = avalonmm_api_extra_device_detail,
	.get_api_extra_device_status = avalonmm_api_extra_device_status,
	
#ifdef HAVE_CURSES
	.proc_wlogprint_status = avalonmm_wlogprint_status,
	.proc_tui_wlogprint_choices = avalonmm_tui_wlogprint_choices,
	.proc_tui_handle_choice = avalonmm_tui_handle_choice,
#endif
};
