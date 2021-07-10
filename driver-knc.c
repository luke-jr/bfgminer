/*
 * Copyright 2013-2014 Luke Dashjr
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
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#ifdef HAVE_LINUX_I2C_DEV_USER_H
#include <linux/i2c-dev-user.h>
#else
#include <linux/i2c-dev.h>
#endif
#include <linux/spi/spidev.h>

#include <uthash.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

#define KNC_POLL_INTERVAL_US 10000
#define KNC_SPI_SPEED 3000000
#define KNC_SPI_DELAY 0
#define KNC_SPI_MODE  (SPI_CPHA | SPI_CPOL | SPI_CS_HIGH)
#define KNC_SPI_BITS  8


/*
 The core disable/enable strategy is as follows:

 If a core gets 10 HW errors in a row without doing any proper work
 it is disabled for 10 seconds.

 When a core gets 10 HW errors the next time it checks when it was enabled
 the last time and compare that to when it started to get errors.
 If those times are close (50%) the disabled time is doubled,
 if not it is just disabled for 10s again.

 */

#define KNC_MAX_HWERR_IN_ROW    10
#define KNC_HWERR_DISABLE_SECS (10)
#define KNC_MAX_DISABLE_SECS   (15 * 60)

#define KNC_CORES_PER_DIE  0x30
#define KNC_DIE_PER_CHIP      4
#define KNC_CORES_PER_CHIP  (KNC_CORES_PER_DIE * KNC_DIE_PER_CHIP)


static const char * const i2cpath = "/dev/i2c-2";

#define KNC_I2C_TEMPLATE "/dev/i2c-%d"

enum knc_request_cmd {
	KNC_REQ_SUBMIT_WORK = 2,
	KNC_REQ_FLUSH_QUEUE = 3,
};

enum knc_reply_type {
	KNC_REPLY_NONCE_FOUND = 1,
	KNC_REPLY_WORK_DONE   = 2,
};

enum knc_i2c_core_status {
	KNC_I2CSTATUS_DISABLED = 2,
	KNC_I2CSTATUS_ENABLED  = 3,
};

BFG_REGISTER_DRIVER(knc_drv)
static const struct bfg_set_device_definition knc_set_device_funcs[];

struct knc_device {
	int i2c;
	struct spi_port *spi;
	struct cgpu_info *cgpu;
	
	bool need_flush;
	struct work *workqueue;
	int workqueue_size;
	int workqueue_max;
	int next_id;
	
	struct work *devicework;
};

struct knc_core {
	int asicno;
	int coreno;
	
	bool use_dcdc;
	float volt;
	float current;
	
	int hwerr_in_row;
	int hwerr_disable_time;
	struct timeval enable_at;
	struct timeval first_hwerr;
};

static
bool knc_detect_one(const char *devpath)
{
	static struct cgpu_info *prev_cgpu = NULL;
	struct cgpu_info *cgpu;
	int i;
	const int fd = open(i2cpath, O_RDWR);
	char *leftover = NULL;
	const int i2cslave = strtol(devpath, &leftover, 0);
	uint8_t buf[0x20];
	
	if (leftover && leftover[0])
		return false;
	
	if (unlikely(fd == -1))
	{
		applog(LOG_DEBUG, "%s: Failed to open %s", __func__, i2cpath);
		return false;
	}
	
	if (ioctl(fd, I2C_SLAVE, i2cslave))
	{
		close(fd);
		applog(LOG_DEBUG, "%s: Failed to select i2c slave 0x%x",
		       __func__, i2cslave);
		return false;
	}
	
	i = i2c_smbus_read_i2c_block_data(fd, 0, 0x20, buf);
	close(fd);
	if (-1 == i)
	{
		applog(LOG_DEBUG, "%s: 0x%x: Failed to read i2c block data",
		       __func__, i2cslave);
		return false;
	}
	for (i = 0; ; ++i)
	{
		if (buf[i] == 3)
			break;
		if (i == 0x1f)
			return false;
	}
	
	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &knc_drv,
		.device_path = strdup(devpath),
		.set_device_funcs = knc_set_device_funcs,
		.deven = DEV_ENABLED,
		.procs = KNC_CORES_PER_CHIP,
		.threads = prev_cgpu ? 0 : 1,
	};
	const bool rv = add_cgpu_slave(cgpu, prev_cgpu);
	prev_cgpu = cgpu;
	return rv;
}

static int knc_detect_auto(void)
{
	const int first = 0x20, last = 0x26;
	char devpath[4];
	int found = 0, i;
	
	for (i = first; i <= last; ++i)
	{
		sprintf(devpath, "%d", i);
		if (knc_detect_one(devpath))
			++found;
	}
	
	return found;
}

static void knc_detect(void)
{
	generic_detect(&knc_drv, knc_detect_one, knc_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}


static
bool knc_spi_open(const char *repr, struct spi_port * const spi)
{
	const char * const spipath = "/dev/spidev1.0";
	const int fd = open(spipath, O_RDWR);
	const uint8_t lsbfirst = 0;
	if (fd == -1)
		return false;
	if (ioctl(fd, SPI_IOC_WR_MODE         , &spi->mode )) goto fail;
	if (ioctl(fd, SPI_IOC_WR_LSB_FIRST    , &lsbfirst  )) goto fail;
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &spi->bits )) goto fail;
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ , &spi->speed)) goto fail;
	spi->fd = fd;
	return true;

fail:
	close(fd);
	spi->fd = -1;
	applog(LOG_WARNING, "%s: Failed to open %s", repr, spipath);
	return false;
}

#define knc_spi_txrx  linux_spi_txrx

static
void knc_clean_flush(struct spi_port * const spi)
{
	const uint8_t flushcmd = KNC_REQ_FLUSH_QUEUE << 4;
	const size_t spi_req_sz = 0x1000;
	
	spi_clear_buf(spi);
	spi_emit_buf(spi, &flushcmd, 1);
	spi_emit_nop(spi, spi_req_sz - spi_getbufsz(spi));
	applog(LOG_DEBUG, "%s: Issuing flush command to clear out device queues", knc_drv.dname);
	spi_txrx(spi);
}

static
bool knc_init(struct thr_info * const thr)
{
	const int max_cores = KNC_CORES_PER_CHIP;
	struct thr_info *mythr;
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct knc_device *knc;
	struct knc_core *knccore;
	struct spi_port *spi;
	const int i2c = open(i2cpath, O_RDWR);
	int i2cslave, i, j;
	uint8_t buf[0x20];
	
	if (unlikely(i2c == -1))
	{
		applog(LOG_DEBUG, "%s: Failed to open %s", __func__, i2cpath);
		return false;
	}
	
	knc = malloc(sizeof(*knc));
	
	for (proc = cgpu; proc; )
	{
		if (proc->device != proc)
		{
			applog(LOG_WARNING, "%"PRIpreprv": Extra processor?", proc->proc_repr);
			proc = proc->next_proc;
			continue;
		}
		
		i2cslave = atoi(proc->device_path);
		
		if (ioctl(i2c, I2C_SLAVE, i2cslave))
		{
			applog(LOG_DEBUG, "%s: Failed to select i2c slave 0x%x",
			       __func__, i2cslave);
			return false;
		}
		
		for (i = 0; i < max_cores; i += 0x20)
		{
			i2c_smbus_read_i2c_block_data(i2c, i, 0x20, buf);
			for (j = 0; j < 0x20; ++j)
			{
				mythr = proc->thr[0];
				mythr->cgpu_data = knccore = malloc(sizeof(*knccore));
				*knccore = (struct knc_core){
					.asicno = i2cslave - 0x20,
					.coreno = i + j,
					.hwerr_in_row = 0,
					.hwerr_disable_time = KNC_HWERR_DISABLE_SECS,
					.use_dcdc = true,
				};
				timer_set_now(&knccore->enable_at);
				proc->device_data = knc;
				switch (buf[j])
				{
					case KNC_I2CSTATUS_ENABLED:
						break;
					default:  // permanently disabled
						proc->deven = DEV_DISABLED;
						break;
					case KNC_I2CSTATUS_DISABLED:
						proc->deven = DEV_RECOVER_DRV;
						break;
				}
				
				proc = proc->next_proc;
				if ((!proc) || proc->device == proc)
					goto nomorecores;
			}
		}
nomorecores: ;
	}
	
	spi = malloc(sizeof(*spi));
	*knc = (struct knc_device){
		.i2c = i2c,
		.spi = spi,
		.cgpu = cgpu,
		.workqueue_max = 1,
	};
	
	/* Be careful, read lowl-spi.h comments for warnings */
	memset(spi, 0, sizeof(*spi));
	spi->txrx = knc_spi_txrx;
	spi->cgpu = cgpu;
	spi->repr = knc_drv.dname;
	spi->logprio = LOG_ERR;
	spi->speed = KNC_SPI_SPEED;
	spi->delay = KNC_SPI_DELAY;
	spi->mode = KNC_SPI_MODE;
	spi->bits = KNC_SPI_BITS;
	
	cgpu_set_defaults(cgpu);
	
	if (!knc_spi_open(cgpu->dev_repr, spi))
		return false;
	
	knc_clean_flush(spi);
	
	timer_set_now(&thr->tv_poll);
	
	return true;
}

static
void knc_set_queue_full(struct knc_device * const knc)
{
	const bool full = (knc->workqueue_size >= knc->workqueue_max);
	struct cgpu_info *proc;
	
	for (proc = knc->cgpu; proc; proc = proc->next_proc)
	{
		struct thr_info * const thr = proc->thr[0];
		thr->queue_full = full;
	}
}

static
void knc_remove_local_queue(struct knc_device * const knc, struct work * const work)
{
	DL_DELETE(knc->workqueue, work);
	free_work(work);
	--knc->workqueue_size;
}

static
void knc_prune_local_queue(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_device * const knc = cgpu->device_data;
	struct work *work, *tmp;
	
	DL_FOREACH_SAFE(knc->workqueue, work, tmp)
	{
		if (stale_work(work, false))
			knc_remove_local_queue(knc, work);
	}
	knc_set_queue_full(knc);
}

static
bool knc_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_device * const knc = cgpu->device_data;
	
	if (knc->workqueue_size >= knc->workqueue_max)
	{
		knc_prune_local_queue(thr);
		if (thr->queue_full)
			return false;
	}
	
	DL_APPEND(knc->workqueue, work);
	++knc->workqueue_size;
	
	knc_set_queue_full(knc);
	if (thr->queue_full)
		knc_prune_local_queue(thr);
	
	return true;
}

#define HASH_LAST_ADDED(head, out)  \
	(out = (head) ? (ELMT_FROM_HH((head)->hh.tbl, (head)->hh.tbl->tail)) : NULL)

static
void knc_queue_flush(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_device * const knc = cgpu->device_data;
	struct work *work, *tmp;
	
	if (knc->cgpu != cgpu)
		return;
	
	DL_FOREACH_SAFE(knc->workqueue, work, tmp)
	{
		knc_remove_local_queue(knc, work);
	}
	knc_set_queue_full(knc);
	
	HASH_LAST_ADDED(knc->devicework, work);
	if (work && stale_work(work, true))
	{
		knc->need_flush = true;
		timer_set_now(&thr->tv_poll);
	}
}

static inline
uint16_t get_u16be(const void * const p)
{
	const uint8_t * const b = p;
	return (((uint16_t)b[0]) << 8) | b[1];
}

static inline
uint32_t get_u32be(const void * const p)
{
	const uint8_t * const b = p;
	return (((uint32_t)b[0]) << 0x18)
	     | (((uint32_t)b[1]) << 0x10)
	     | (((uint32_t)b[2]) << 8)
	     |             b[3];
}

static
void knc_poll(struct thr_info * const thr)
{
	struct thr_info *mythr;
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct knc_device * const knc = cgpu->device_data;
	struct spi_port * const spi = knc->spi;
	struct knc_core *knccore;
	struct work *work, *tmp;
	uint8_t buf[0x30], *rxbuf;
	int works_sent = 0, asicno, i;
	uint16_t workaccept;
	int workid = knc->next_id;
	uint32_t nonce, coreno;
	size_t spi_req_sz = 0x1000;
	unsigned long delay_usecs = KNC_POLL_INTERVAL_US;
	
	knc_prune_local_queue(thr);
	
	spi_clear_buf(spi);
	if (knc->need_flush)
	{
		applog(LOG_NOTICE, "%s: Abandoning stale searches to restart", knc_drv.dname);
		buf[0] = KNC_REQ_FLUSH_QUEUE << 4;
		spi_emit_buf(spi, buf, sizeof(buf));
	}
	DL_FOREACH(knc->workqueue, work)
	{
		buf[0] = KNC_REQ_SUBMIT_WORK << 4;
		buf[1] = 0;
		buf[2] = (workid >> 8) & 0x7f;
		buf[3] = workid & 0xff;
		
		for (i = 0; i < 0x20; ++i)
			buf[4 + i] = work->midstate[0x1f - i];
		for (i = 0; i < 0xc; ++i)
			buf[0x24 + i] = work->data[0x4b - i];
		
		spi_emit_buf(spi, buf, sizeof(buf));
		
		++works_sent;
		++workid;
	}
	spi_emit_nop(spi, spi_req_sz - spi_getbufsz(spi));
	spi_txrx(spi);
	
	rxbuf = spi_getrxbuf(spi);
	if (rxbuf[3] & 1)
		applog(LOG_DEBUG, "%s: Receive buffer overflow reported", knc_drv.dname);
	workaccept = get_u16be(&rxbuf[6]);
	applog(LOG_DEBUG, "%s: %lu/%d jobs accepted to queue (max=%d)",
	       knc_drv.dname, (unsigned long)workaccept, works_sent, knc->workqueue_max);
	
	while (true)
	{
		rxbuf += 0xc;
		spi_req_sz -= 0xc;
		if (spi_req_sz < 0xc)
			break;
		
		const int rtype = rxbuf[0] >> 6;
		if (rtype && opt_debug)
		{
			char x[(0xc * 2) + 1];
			bin2hex(x, rxbuf, 0xc);
			applog(LOG_DEBUG, "%s: RECV: %s", knc_drv.dname, x);
		}
		if (rtype != KNC_REPLY_NONCE_FOUND && rtype != KNC_REPLY_WORK_DONE)
			continue;
		
		asicno = (rxbuf[0] & 0x38) >> 3;
		coreno = get_u32be(&rxbuf[8]);
		proc = cgpu;
		while (true)
		{
			knccore = proc->thr[0]->cgpu_data;
			if (knccore->asicno == asicno)
				break;
			do {
				proc = proc->next_proc;
			} while(proc != proc->device);
		}
		for (i = 0; i < coreno; ++i)
			proc = proc->next_proc;
		mythr = proc->thr[0];
		knccore = mythr->cgpu_data;
		
		i = get_u16be(&rxbuf[2]);
		HASH_FIND_INT(knc->devicework, &i, work);
		if (!work)
		{
			const char * const msgtype = (rtype == KNC_REPLY_NONCE_FOUND) ? "nonce found" : "work done";
			applog(LOG_WARNING, "%"PRIpreprv": Got %s message about unknown work 0x%04x",
			       proc->proc_repr, msgtype, i);
			if (KNC_REPLY_NONCE_FOUND == rtype)
			{
				nonce = get_u32be(&rxbuf[4]);
				nonce = le32toh(nonce);
				inc_hw_errors2(mythr, NULL, &nonce);
			}
			else
				inc_hw_errors2(mythr, NULL, NULL);
			continue;
		}
		
		switch (rtype)
		{
			case KNC_REPLY_NONCE_FOUND:
				nonce = get_u32be(&rxbuf[4]);
				nonce = le32toh(nonce);
				if (submit_nonce(mythr, work, nonce))
					knccore->hwerr_in_row = 0;
				break;
			case KNC_REPLY_WORK_DONE:
				HASH_DEL(knc->devicework, work);
				free_work(work);
				hashes_done2(mythr, 0x100000000, NULL);
				break;
		}
	}
	
	if (knc->need_flush)
	{
		knc->need_flush = false;
		HASH_ITER(hh, knc->devicework, work, tmp)
		{
			HASH_DEL(knc->devicework, work);
			free_work(work);
		}
		delay_usecs = 0;
	}
	
	if (workaccept)
	{
		if (workaccept >= knc->workqueue_max)
		{
			knc->workqueue_max = workaccept;
			delay_usecs = 0;
		}
		DL_FOREACH_SAFE(knc->workqueue, work, tmp)
		{
			--knc->workqueue_size;
			DL_DELETE(knc->workqueue, work);
			work->device_id = knc->next_id++ & 0x7fff;
			HASH_ADD(hh, knc->devicework, device_id, sizeof(work->device_id), work);
			if (!--workaccept)
				break;
		}
		knc_set_queue_full(knc);
	}
	
	timer_set_delay_from_now(&thr->tv_poll, delay_usecs);
}

static
bool _knc_core_setstatus(struct thr_info * const thr, uint8_t val)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct knc_device * const knc = proc->device_data;
	struct knc_core * const knccore = thr->cgpu_data;
	const int i2c = knc->i2c;
	const int i2cslave = 0x20 + knccore->asicno;
	
	if (ioctl(i2c, I2C_SLAVE, i2cslave))
	{
		applog(LOG_DEBUG, "%"PRIpreprv": %s: Failed to select i2c slave 0x%x",
		       proc->proc_repr, __func__, i2cslave);
		return false;
	}
	
	return (-1 != i2c_smbus_write_byte_data(i2c, knccore->coreno, val));
}

static
void knc_core_disable(struct thr_info * const thr)
{
	_knc_core_setstatus(thr, 0);
}

static
void knc_core_enable(struct thr_info * const thr)
{
	struct knc_core * const knccore = thr->cgpu_data;
	timer_set_now(&knccore->enable_at);
	_knc_core_setstatus(thr, 1);
}

static
float knc_dcdc_decode_5_11(uint16_t raw)
{
	if (raw == 0)
		return 0.0;
	
	int dcdc_vin_exp = (raw & 0xf800) >> 11;
	float dcdc_vin_man = raw & 0x07ff;
	if (dcdc_vin_exp >= 16)
		dcdc_vin_exp = -32 + dcdc_vin_exp;
	float dcdc_vin = dcdc_vin_man * exp2(dcdc_vin_exp);
	return dcdc_vin;
}

static
void knc_hw_error(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct knc_core * const knccore = thr->cgpu_data;
	
	if(knccore->hwerr_in_row == 0)
		timer_set_now(&knccore->first_hwerr);
	++knccore->hwerr_in_row;
	
	if (knccore->hwerr_in_row >= KNC_MAX_HWERR_IN_ROW && proc->deven == DEV_ENABLED)
	{
		struct timeval now;
		timer_set_now(&now);
		float first_err_dt  = tdiff(&now, &knccore->first_hwerr);
		float enable_dt     = tdiff(&now, &knccore->enable_at);
		
		if(first_err_dt * 1.5 > enable_dt)
		{
			// didn't really do much good
			knccore->hwerr_disable_time *= 2;
			if (knccore->hwerr_disable_time > KNC_MAX_DISABLE_SECS)
				knccore->hwerr_disable_time = KNC_MAX_DISABLE_SECS;
		}
		else
			knccore->hwerr_disable_time  = KNC_HWERR_DISABLE_SECS;
		proc->deven = DEV_RECOVER_DRV;
		applog(LOG_WARNING, "%"PRIpreprv": Disabled. %d hwerr in %.3f / %.3f . disabled %d s",
		       proc->proc_repr, knccore->hwerr_in_row,
		       enable_dt, first_err_dt, knccore->hwerr_disable_time);
		
		timer_set_delay_from_now(&knccore->enable_at, knccore->hwerr_disable_time * 1000000);
	}
}

static
bool knc_get_stats(struct cgpu_info * const cgpu)
{
	if (cgpu->device != cgpu)
		return true;
	
	struct thr_info *thr = cgpu->thr[0];
	struct knc_core *knccore = thr->cgpu_data;
	struct cgpu_info *proc;
	const int i2cdev = knccore->asicno + 3;
	const int i2cslave_temp = 0x48;
	const int i2cslave_dcdc[] = {0x10, 0x12, 0x14, 0x17};
	int die, i;
	int i2c;
	int32_t rawtemp, rawvolt, rawcurrent;
	float temp, volt, current;
	struct timeval tv_now;
	bool rv = false;
	
	char i2cpath[sizeof(KNC_I2C_TEMPLATE)];
	sprintf(i2cpath, KNC_I2C_TEMPLATE, i2cdev);
	i2c = open(i2cpath, O_RDWR);
	if (i2c == -1)
	{
		applog(LOG_DEBUG, "%s: %s: Failed to open %s",
		       cgpu->dev_repr, __func__, i2cpath);
		return false;
	}
	
	if (ioctl(i2c, I2C_SLAVE, i2cslave_temp))
	{
		applog(LOG_DEBUG, "%s: %s: Failed to select i2c slave 0x%x",
		       cgpu->dev_repr, __func__, i2cslave_temp);
		goto out;
	}
	
	rawtemp = i2c_smbus_read_word_data(i2c, 0);
	if (rawtemp == -1)
		goto out;
	temp = ((float)(rawtemp & 0xff));
	if (rawtemp & 0x8000)
		temp += 0.5;
	
	/* DCDC i2c slaves are on 0x10 + [0-7]
	   8 DCDC boards have all populated
	   4 DCDC boards only have 0,2,4,7 populated
	   Only 0,2,4,7 are used
	   Each DCDC powers one die in the chip, each die has 48 cores
	   
	   Datasheet at http://www.lineagepower.com/oem/pdf/MDT040A0X.pdf
	*/

	timer_set_now(&tv_now);
	volt = current = 0;
	for (proc = cgpu, i = 0; proc && proc->device == cgpu; proc = proc->next_proc, ++i)
	{
		thr = proc->thr[0];
		knccore = thr->cgpu_data;
		die = i / KNC_CORES_PER_DIE;
		
		if (0 == i % KNC_CORES_PER_DIE && knccore->use_dcdc)
		{
			if (ioctl(i2c, I2C_SLAVE, i2cslave_dcdc[die]))
			{
				applog(LOG_DEBUG, "%s: %s: Failed to select i2c slave 0x%x",
				       cgpu->dev_repr, __func__, i2cslave_dcdc[die]);
				goto out;
			}
			
			rawvolt = i2c_smbus_read_word_data(i2c, 0x8b);  // VOUT
			if (rawvolt == -1)
				goto out;
			
			rawcurrent = i2c_smbus_read_word_data(i2c, 0x8c);  // IOUT
			if (rawcurrent == -1)
				goto out;
			
			volt    = (float)rawvolt * exp2(-10);
			current = (float)knc_dcdc_decode_5_11(rawcurrent);
			
			applog(LOG_DEBUG, "%s: die %d %6.3fV %5.2fA",
			       cgpu->dev_repr, die, volt, current);
		}
		
		proc->temp = temp;
		knccore->volt = volt;
		knccore->current = current;
		
		// NOTE: We need to check _mt_disable_called because otherwise enabling won't assert it to i2c (it's false when getting stats for eg proc 0 before proc 1+ haven't initialised completely yet)
		if (proc->deven == DEV_RECOVER_DRV && timer_passed(&knccore->enable_at, &tv_now) && thr->_mt_disable_called)
		{
			knccore->hwerr_in_row = 0;
			proc_enable(proc);
		}
	}
	
	rv = true;
out:
	close(i2c);
	return rv;
}

static
struct api_data *knc_api_extra_device_status(struct cgpu_info * const cgpu)
{
	struct api_data *root = NULL;
	struct thr_info * const thr = cgpu->thr[0];
	struct knc_core * const knccore = thr->cgpu_data;
	
	if (knccore->use_dcdc)
	{
		root = api_add_volts(root, "Voltage", &knccore->volt, false);
		root = api_add_volts(root, "DCDC Current", &knccore->current, false);
	}
	
	return root;
}

#ifdef HAVE_CURSES
static
void knc_wlogprint_status(struct cgpu_info * const cgpu)
{
	struct thr_info * const thr = cgpu->thr[0];
	struct knc_core * const knccore = thr->cgpu_data;
	
	if (knccore->use_dcdc)
		wlogprint("Voltage: %.3f  DCDC Current: %.3f\n",
		          knccore->volt, knccore->current);
}
#endif

static
const char *knc_set_use_dcdc(struct cgpu_info *proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	int core_index_on_die = proc->proc_id % KNC_CORES_PER_DIE;
	bool nv;
	char *end;
	
	nv = bfg_strtobool(newvalue, &end, 0);
	if (!(newvalue[0] && !end[0]))
		return "Usage: use_dcdc=yes/no";
	
	if (core_index_on_die)
	{
		const int seek = (proc->proc_id / KNC_CORES_PER_DIE) * KNC_CORES_PER_DIE;
		proc = proc->device;
		for (int i = 0; i < seek; ++i)
			proc = proc->next_proc;
	}
	
	{
		struct thr_info * const mythr = proc->thr[0];
		struct knc_core * const knccore = mythr->cgpu_data;
		
		if (knccore->use_dcdc == nv)
			return NULL;
	}
	
	for (int i = 0; i < KNC_CORES_PER_DIE; (proc = proc->next_proc), ++i)
	{
		struct thr_info * const mythr = proc->thr[0];
		struct knc_core * const knccore = mythr->cgpu_data;
		
		knccore->use_dcdc = nv;
	}
	
	return NULL;
}

static const struct bfg_set_device_definition knc_set_device_funcs[] = {
	{"use_dcdc", knc_set_use_dcdc, "whether to access DCDC module for voltage/current information"},
	{NULL}
};

struct device_drv knc_drv = {
	.dname = "knc",
	.name = "KNC",
	.drv_detect = knc_detect,
	
	.thread_init = knc_init,
	.thread_disable = knc_core_disable,
	.thread_enable  = knc_core_enable,
	
	.minerloop = minerloop_queue,
	.queue_append = knc_queue_append,
	.queue_flush = knc_queue_flush,
	.poll = knc_poll,
	.hw_error = knc_hw_error,
	
	.get_stats = knc_get_stats,
	.get_api_extra_device_status = knc_api_extra_device_status,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = knc_wlogprint_status,
#endif
};
