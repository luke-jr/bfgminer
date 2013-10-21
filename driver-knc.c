/*
 * Copyright 2013 Luke Dashjr
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

#ifdef HAVE_LINUX_I2C_DEV_USER_H
#include <linux/i2c-dev-user.h>
#else
#include <linux/i2c-dev.h>
#endif
#include <linux/spi/spidev.h>

#include <uthash.h>

#include "deviceapi.h"
#include "logging.h"
#include "miner.h"
#include "spidevc.h"

#define KNC_POLL_INTERVAL_US 10000
#define KNC_SPI_SPEED 3000000
#define KNC_SPI_DELAY 0
#define KNC_SPI_MODE  (SPI_CPHA | SPI_CPOL | SPI_CS_HIGH)
#define KNC_SPI_BITS  8

static const char * const i2cpath = "/dev/i2c-2";

enum knc_request_cmd {
	KNC_REQ_SUBMIT_WORK = 2,
	KNC_REQ_FLUSH_QUEUE = 3,
};

enum knc_reply_type {
	KNC_REPLY_NONCE_FOUND = 1,
	KNC_REPLY_WORK_DONE   = 2,
};

struct device_drv knc_drv;

struct knc_device {
	int i2c;
	struct spi_port *spi;
	
	struct work *workqueue;
	int workqueue_size;
	int workqueue_max;
	
	struct work *devicework;
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
		.deven = DEV_ENABLED,
		.procs = 192,
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

static
bool knc_spi_txrx(struct spi_port * const spi)
{
	const void * const wrbuf = spi_gettxbuf(spi);
	void * const rdbuf = spi_getrxbuf(spi);
	const size_t bufsz = spi_getbufsz(spi);
	const int fd = spi->fd;
	struct spi_ioc_transfer xf = {
		.tx_buf = (uintptr_t) wrbuf,
		.rx_buf = (uintptr_t) rdbuf,
		.len = bufsz,
		.delay_usecs = spi->delay,
		.speed_hz = spi->speed,
		.bits_per_word = spi->bits,
	};
	return (ioctl(fd, SPI_IOC_MESSAGE(1), &xf) > 0);
}

static
bool knc_init(struct thr_info * const thr)
{
	const int max_cores = 192;
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct knc_device *knc;
	struct spi_port *spi;
	const int i2c = open(i2cpath, O_RDWR);
	int i2cslave, i, j;
	uint8_t buf[0x20];
	
	if (unlikely(i2c == -1))
	{
		applog(LOG_DEBUG, "%s: Failed to open %s", __func__, i2cpath);
		return false;
	}
	
	for (proc = cgpu; proc; )
	{
		if (proc->device != proc)
		{
			applog(LOG_WARNING, "%"PRIpreprv": Extra processor?", proc->proc_repr);
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
				if (buf[j] != 3)
					proc->deven = DEV_DISABLED;
				proc = proc->next_proc;
				if ((!proc) || proc->device == proc)
					goto nomorecores;
			}
		}
nomorecores: ;
	}
	
	cgpu->device_data = knc = malloc(sizeof(*knc));
	spi = malloc(sizeof(*spi));
	*knc = (struct knc_device){
		.i2c = i2c,
		.spi = spi,
		.workqueue_max = 1,
	};
	*spi = (struct spi_port){
		.txrx = knc_spi_txrx,
		.cgpu = cgpu,
		.repr = knc_drv.dname,
		.logprio = LOG_ERR,
		.speed = KNC_SPI_SPEED,
		.delay = KNC_SPI_DELAY,
		.mode  = KNC_SPI_MODE,
		.bits  = KNC_SPI_BITS,
	};
	
	if (!knc_spi_open(cgpu->dev_repr, spi))
		return false;
	
	timer_set_now(&thr->tv_poll);
	
	return true;
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
	thr->queue_full = (knc->workqueue_size >= knc->workqueue_max);
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
	
	thr->queue_full = (knc->workqueue_size >= knc->workqueue_max);
	if (thr->queue_full)
		knc_prune_local_queue(thr);
	
	return true;
}

static
void knc_queue_flush(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_device * const knc = cgpu->device_data;
	struct work *work, *tmp;
	
	DL_FOREACH_SAFE(knc->workqueue, work, tmp)
	{
		knc_remove_local_queue(knc, work);
	}
	thr->queue_full = false;
}

static inline
uint16_t get_u16be(const void * const p)
{
	const uint8_t * const b = p;
	return (((uint16_t)b[0]) << 8) | b[1];
}

static inline
uint16_t get_u32be(const void * const p)
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
	struct work *work, *tmp;
	uint8_t buf[0x30], *rxbuf;
	int works_sent = 0, asicno, i;
	uint16_t workaccept;
	uint32_t nonce, coreno;
	size_t spi_req_sz = 0x1000;
	unsigned long delay_usecs = KNC_POLL_INTERVAL_US;
	
	knc_prune_local_queue(thr);
	
	spi_clear_buf(spi);
	DL_FOREACH(knc->workqueue, work)
	{
		buf[0] = KNC_REQ_SUBMIT_WORK << 4;
		buf[1] = 0;
		buf[2] = work->id >> 8;
		buf[3] = work->id & 0xff;
		
		memcpy(&buf[4], work->midstate, 0x20);
		memcpy(&buf[0x24], &work->data[64], 0xc);
		
		spi_emit_buf(spi, buf, sizeof(buf));
		
		++works_sent;
	}
	spi_emit_nop(spi, spi_req_sz - spi_getbufsz(spi));
	spi_txrx(spi);
	
	rxbuf = spi_getrxbuf(spi);
	if (rxbuf[3] & 1)
		applog(LOG_DEBUG, "%s: Receive buffer overflow reported", knc_drv.dname);
	workaccept = get_u16be(&rxbuf[6]);
	applog(LOG_DEBUG, "%s: %lu/%d jobs accepted to queue (max=%d)",
	       knc_drv.dname, (unsigned long)workaccept, works_sent, knc->workqueue_max);
	if (workaccept)
	{
		if (workaccept >= knc->workqueue_max)
		{
			knc->workqueue_max = workaccept;
			delay_usecs = 0;
		}
		DL_FOREACH_SAFE(knc->workqueue, work, tmp)
		{
			DL_DELETE(knc->workqueue, work);
			HASH_ADD_INT(knc->devicework, id, work);
			if (!--workaccept)
				break;
		}
		thr->queue_full = (knc->workqueue_size >= knc->workqueue_max);
	}
	
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
		for (i = 0; i < asicno; )
		{
			proc = proc->next_proc;
			if (proc == proc->device)
				++i;
		}
		for (i = 0; i < coreno; ++i)
			proc = proc->next_proc;
		mythr = proc->thr[0];
		
		i = get_u16be(&rxbuf[2]);
		HASH_FIND_INT(knc->devicework, &i, work);
		if (!work)
		{
			const char * const msgtype = (rtype == KNC_REPLY_NONCE_FOUND) ? "nonce found" : "work done";
			applog(LOG_WARNING, "%"PRIpreprv": Got %s message about unknown work 0x%04x",
			       proc->proc_repr, msgtype, i);
			continue;
		}
		
		switch (rtype)
		{
			case KNC_REPLY_NONCE_FOUND:
				nonce = get_u32be(&rxbuf[4]);
				submit_nonce(mythr, work, nonce);
				break;
			case KNC_REPLY_WORK_DONE:
				HASH_DEL(knc->devicework, work);
				free_work(work);
				hashes_done2(mythr, 0x100000000, NULL);
				break;
		}
	}
	
	timer_set_delay_from_now(&thr->tv_poll, delay_usecs);
}

struct device_drv knc_drv = {
	.dname = "knc",
	.name = "KNC",
	.drv_detect = knc_detect,
	
	.thread_init = knc_init,
	
	.minerloop = minerloop_queue,
	.queue_append = knc_queue_append,
	.queue_flush = knc_queue_flush,
	.poll = knc_poll,
};
