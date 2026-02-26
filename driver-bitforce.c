/*
 * Copyright 2012-2014 Luke Dashjr
 * Copyright 2012-2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "compat.h"
#include "deviceapi.h"
#include "miner.h"
#include "lowlevel.h"
#ifdef NEED_BFG_LOWL_MSWIN
#include "lowl-mswin.h"
#endif
#include "lowl-pci.h"
#include "lowl-vcom.h"
#include "util.h"

#define BFL_PCI_VENDOR_ID 0x1cf9

#define BITFORCE_SLEEP_MS 500
#define BITFORCE_VCOM_TIMEOUT_DSEC     250
#define BITFORCE_VCOM_TIMEOUT_DSEC_ZCX  10
#define BITFORCE_TIMEOUT_S 7
#define BITFORCE_TIMEOUT_MS (BITFORCE_TIMEOUT_S * 1000)
#define BITFORCE_LONG_TIMEOUT_S 25
#define BITFORCE_LONG_TIMEOUT_MS (BITFORCE_LONG_TIMEOUT_S * 1000)
#define BITFORCE_CHECK_INTERVAL_MS 10
#define WORK_CHECK_INTERVAL_MS 50
#define tv_to_ms(tval) ((unsigned long)(tval.tv_sec * 1000 + tval.tv_usec / 1000))
#define TIME_AVG_CONSTANT 8
#define BITFORCE_QRESULT_LINE_LEN 165
#define BITFORCE_MAX_QUEUED_MAX 40
#define BITFORCE_MIN_QUEUED_MAX 10
#define BITFORCE_MAX_QRESULTS 16
#define BITFORCE_GOAL_QRESULTS 5
#define BITFORCE_MIN_QRESULT_WAIT BITFORCE_CHECK_INTERVAL_MS
#define BITFORCE_MAX_QRESULT_WAIT 1000
#define BITFORCE_MAX_BQUEUE_AT_ONCE_65NM 5
#define BITFORCE_MAX_BQUEUE_AT_ONCE_28NM 20

enum bitforce_proto {
	BFP_WORK   = 0,
	BFP_RANGE  = 1,
	BFP_BQUEUE = 3,
	BFP_PQUEUE = 4,
};

static const char *protonames[] = {
	"full work",
	"nonce range",
	NULL,
	"bulk queue",
	"parallel queue",
};

BFG_REGISTER_DRIVER(bitforce_drv)
BFG_REGISTER_DRIVER(bitforce_queue_api)
static const struct bfg_set_device_definition bitforce_set_device_funcs[];

enum bitforce_style {
	BFS_FPGA,
	BFS_65NM,
	BFS_28NM,
};

struct bitforce_lowl_interface {
	bool (*open)(struct cgpu_info *);
	void (*close)(struct cgpu_info *);
	ssize_t (*read)(void *, size_t, struct cgpu_info *);
	void (*gets)(char *, size_t, struct cgpu_info *);
	ssize_t (*write)(struct cgpu_info *, const void *, ssize_t);
	bool (*set_timeout)(struct cgpu_info* , uint8_t);
};

struct bitforce_data {
	struct bitforce_lowl_interface *lowlif;
	bool is_open;
	struct lowl_pci_handle *lph;
	uint8_t lasttag;
	uint8_t lasttag_read;
	bytes_t getsbuf;
	int xlink_id;
	unsigned char next_work_ob[70];  // Data aligned for 32-bit access
	unsigned char *next_work_obs;    // Start of data to send
	unsigned char next_work_obsz;
	const char *next_work_cmd;
	char noncebuf[14 + ((BITFORCE_MAX_QRESULTS+1) * BITFORCE_QRESULT_LINE_LEN)];
	int poll_func;
	enum bitforce_proto proto;
	enum bitforce_style style;
	int queued;
	int queued_max;
	int parallel;
	bool parallel_protocol;
	bool missing_zwx;
	bool already_have_results;
	bool just_flushed;
	int max_queue_at_once;
	int ready_to_queue;
	bool want_to_send_queue;
	unsigned result_busy_polled;
	unsigned sleep_ms_default;
	struct timeval tv_hashmeter_start;
	float temp[2];
	long *volts;
	int volts_count;
	unsigned max_queueid;
	
	bool probed;
	bool supports_fanspeed;
};

// Code must deal with a timeout
static
bool bitforce_vcom_open(struct cgpu_info * const dev)
{
	struct bitforce_data * const devdata = dev->device_data;
	const char * const devpath = dev->device_path;
	dev->device_fd = serial_open(devpath, 0, BITFORCE_VCOM_TIMEOUT_DSEC, true);
	devdata->is_open = (dev->device_fd != -1);
	return devdata->is_open;
}

static
void bitforce_vcom_close(struct cgpu_info * const dev)
{
	struct bitforce_data * const devdata = dev->device_data;
	if (devdata->is_open)
	{
		serial_close(dev->device_fd);
		dev->device_fd = -1;
		devdata->is_open = false;
	}
}

static
ssize_t bitforce_vcom_read(void * const buf_p, size_t bufLen, struct cgpu_info * const dev)
{
	uint8_t *buf = buf_p;
	const int fd = dev->device_fd;
	ssize_t rv, ret = 0;
	while (bufLen > 0)
	{
		rv = read(fd, buf, bufLen);
		if (rv <= 0)
		{
			if (ret > 0)
				return ret;
			return rv;
		}
		buf += rv;
		bufLen -= rv;
		ret += rv;
	}
	return ret;
}

static
void bitforce_vcom_gets(char *buf, size_t bufLen, struct cgpu_info * const dev)
{
	const int fd = dev->device_fd;
	while (likely(bufLen > 1 && read(fd, buf, 1) == 1 && (buf++)[0] != '\n'))
	{}

	buf[0] = '\0';
}

static
ssize_t bitforce_vcom_write(struct cgpu_info * const dev, const void *buf, ssize_t bufLen)
{
	const int fd = dev->device_fd;
	if ((bufLen) != write(fd, buf, bufLen))
		return 0;
	else
		return bufLen;
}

static
bool bitforce_vcom_set_timeout(struct cgpu_info * const dev, const uint8_t timeout)
{
	const int fd = dev->device_fd;
	return vcom_set_timeout(fd, timeout);
}

static struct bitforce_lowl_interface bfllif_vcom = {
	.open = bitforce_vcom_open,
	.close = bitforce_vcom_close,
	.read = bitforce_vcom_read,
	.gets = bitforce_vcom_gets,
	.write = bitforce_vcom_write,
	.set_timeout = bitforce_vcom_set_timeout,
};

#ifdef NEED_BFG_LOWL_PCI
static
bool bitforce_pci_open(struct cgpu_info * const dev)
{
	const char * const devpath = dev->device_path;
	struct bitforce_data * const devdata = dev->device_data;
	devdata->lph = lowl_pci_open(devpath, LP_BARINFO(
		LP_BAR(0, 0x1000, O_WRONLY),
		LP_BAR(1, 0x1000, O_RDONLY),
		LP_BAR(2,   0x80, O_RDWR),
	));
	if (!devdata->lph)
		return false;
	devdata->lasttag = (lowl_pci_get_word(devdata->lph, 2, 2) >> 16) & 0xff;
	devdata->lasttag_read = 0;
	bytes_reset(&devdata->getsbuf);
	devdata->is_open = true;
	return devdata->is_open;
}

static
void bitforce_pci_close(struct cgpu_info * const dev)
{
	struct bitforce_data * const devdata = dev->device_data;
	if (devdata->is_open)
	{
		lowl_pci_close(devdata->lph);
		devdata->is_open = false;
	}
}

static
void _bitforce_pci_read(struct cgpu_info * const dev)
{
	struct bitforce_data * const devdata = dev->device_data;
	const uint32_t looking_for = (uint32_t)devdata->lasttag << 0x10;
	uint32_t resp;
	bytes_t *b = &devdata->getsbuf;
	
	if (devdata->lasttag != devdata->lasttag_read)
	{
		if (unlikely(bytes_len(b)))
		{
			applog(LOG_WARNING, "%s: %ld bytes remaining in read buffer at new command", dev->dev_repr, (long)bytes_len(&devdata->getsbuf));
			bytes_reset(b);
		}
		
		while (((resp = lowl_pci_get_word(devdata->lph, 2, 2)) & 0xff0000) != looking_for)
			cgsleep_ms(1);
		
		resp &= 0xffff;
		if (unlikely(resp > 0x1000))
			resp = 0x1000;
		
		void * const buf = bytes_preappend(b, resp + LOWL_PCI_GET_DATA_PADDING);
		if (lowl_pci_read_data(devdata->lph, buf, resp, 1, 0))
			bytes_postappend(b, resp);
		
		devdata->lasttag_read = devdata->lasttag;
	}
}

static
ssize_t bitforce_pci_read(void * const buf, const size_t bufLen, struct cgpu_info * const dev)
{
	struct bitforce_data * const devdata = dev->device_data;
	bytes_t *b = &devdata->getsbuf;
	
	_bitforce_pci_read(dev);
	ssize_t datalen = bytes_len(b);
	if (datalen <= 0)
		return datalen;
	
	if (datalen > bufLen)
		datalen = bufLen;
	
	memcpy(buf, bytes_buf(b), datalen);
	bytes_shift(b, datalen);
	
	return datalen;
}

static
void bitforce_pci_gets(char * const buf, size_t bufLen, struct cgpu_info * const dev)
{
	struct bitforce_data * const devdata = dev->device_data;
	bytes_t *b = &devdata->getsbuf;
	
	_bitforce_pci_read(dev);
	ssize_t linelen = (bytes_find(b, '\n') + 1) ?: bytes_len(b);
	if (linelen > --bufLen)
		linelen = bufLen;
	
	memcpy(buf, bytes_buf(b), linelen);
	bytes_shift(b, linelen);
	buf[linelen] = '\0';
}

static
ssize_t bitforce_pci_write(struct cgpu_info * const dev, const void * const bufp, ssize_t bufLen)
{
	const uint8_t *buf = bufp;
	struct bitforce_data * const devdata = dev->device_data;
	
	if (unlikely(bufLen > 0x1000))
		return 0;
	
	if (!lowl_pci_set_data(devdata->lph, buf, bufLen, 0, 0))
		return 0;
	if (++devdata->lasttag == 0)
		++devdata->lasttag;
	if (!lowl_pci_set_word(devdata->lph, 2, 0, ((uint32_t)devdata->lasttag << 0x10) | bufLen))
		return 0;
	
	return bufLen;
}

static struct bitforce_lowl_interface bfllif_pci = {
	.open = bitforce_pci_open,
	.close = bitforce_pci_close,
	.read = bitforce_pci_read,
	.gets = bitforce_pci_gets,
	.write = bitforce_pci_write,
};
#endif

static
void bitforce_close(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct bitforce_data * const devdata = dev->device_data;
	
	if (devdata->is_open)
		devdata->lowlif->close(dev);
}

static
bool bitforce_open(struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct bitforce_data * const devdata = dev->device_data;
	
	bitforce_close(proc);
	return devdata->lowlif->open(dev);
}

static
ssize_t bitforce_read(struct cgpu_info * const proc, void * const buf, const size_t bufLen)
{
	struct cgpu_info * const dev = proc->device;
	struct bitforce_data * const devdata = dev->device_data;
	ssize_t rv;
	
	if (likely(devdata->is_open))
	{
		if (bufLen == 0)
			rv = 0;
		else
			rv = devdata->lowlif->read(buf, bufLen, dev);
	}
	else
		rv = -1;
	
	if (unlikely(opt_dev_protocol))
	{
		size_t datalen = (rv > 0) ? rv : 0;
		char hex[(rv * 2) + 1];
		bin2hex(hex, buf, datalen);
		applog(LOG_DEBUG, "DEVPROTO: %s: READ(%lu): %s",
		       dev->dev_repr, (unsigned long)bufLen, hex);
	}
	
	return rv;
}

static
void bitforce_gets(char * const buf, const size_t bufLen, struct cgpu_info * const proc)
{
	struct cgpu_info * const dev = proc->device;
	struct bitforce_data * const devdata = dev->device_data;
	
	if (likely(devdata->is_open))
		devdata->lowlif->gets(buf, bufLen, dev);
	else
		buf[0] = '\0';
	
	if (unlikely(opt_dev_protocol))
		applog(LOG_DEBUG, "DEVPROTO: %s: GETS: %s", dev->dev_repr, buf);
}

static
ssize_t bitforce_write(struct cgpu_info * const proc, const void * const buf, const ssize_t bufLen)
{
	struct cgpu_info * const dev = proc->device;
	struct bitforce_data * const devdata = dev->device_data;
	
	if (unlikely(!devdata->is_open))
		return 0;
	
	return devdata->lowlif->write(dev, buf, bufLen);
}

static ssize_t bitforce_send(struct cgpu_info * const proc, const void *buf, ssize_t bufLen)
{
	struct bitforce_data * const data = proc->device_data;
	const int procid = data->xlink_id;
	if (!procid)
		return bitforce_write(proc, buf, bufLen);
	
	if (bufLen > 255)
		return -1;
	
	size_t bufLeft = bufLen + 3;
	char realbuf[bufLeft], *bufp;
	ssize_t rv;
	memcpy(&realbuf[3], buf, bufLen);
	realbuf[0] = '@';
	realbuf[1] = bufLen;
	realbuf[2] = procid;
	bufp = realbuf;
	do
	{
		rv = bitforce_write(proc, bufp, bufLeft);
		if (rv <= 0)
			return rv;
		bufLeft -= rv;
	}
	while (bufLeft > 0);
	return bufLen;
}

static
void bitforce_cmd1b(struct cgpu_info * const proc, void *buf, size_t bufsz, const char *cmd, size_t cmdsz)
{
	if (unlikely(opt_dev_protocol))
		applog(LOG_DEBUG, "DEVPROTO: %"PRIpreprv": CMD1: %s",
		       proc->proc_repr, cmd);
	
	bitforce_send(proc, cmd, cmdsz);
	bitforce_gets(buf, bufsz, proc);
}

static
void bitforce_cmd1c(struct cgpu_info * const proc, void *buf, size_t bufsz, void *cmd, size_t cmdsz)
{
	if (unlikely(opt_dev_protocol))
	{
		char hex[(cmdsz * 2) + 1];
		bin2hex(hex, cmd, cmdsz);
		applog(LOG_DEBUG, "DEVPROTO: %"PRIpreprv": CMD1 HEX: %s",
		       proc->proc_repr, hex);
	}
	
	bitforce_send(proc, cmd, cmdsz);
	bitforce_gets(buf, bufsz, proc);
}

static
void bitforce_cmd2(struct cgpu_info * const proc, void *buf, size_t bufsz, const char *cmd, void *data, size_t datasz)
{
	bitforce_cmd1b(proc, buf, bufsz, cmd, 3);
	if (strncasecmp(buf, "OK", 2))
		return;
	
	if (unlikely(opt_dev_protocol))
	{
		char hex[(datasz * 2) + 1];
		bin2hex(hex, data, datasz);
		applog(LOG_DEBUG, "DEVPROTO: %"PRIpreprv": CMD2: %s",
		       proc->proc_repr, hex);
	}
	
	bitforce_send(proc, data, datasz);
	bitforce_gets(buf, bufsz, proc);
}

static
void bitforce_zgx(struct cgpu_info * const proc, void *buf, size_t bufsz)
{
	struct cgpu_info * const dev = proc->device;
	struct bitforce_data * const devdata = dev->device_data;
	
	if (devdata->is_open && devdata->lowlif->set_timeout)
	{
		devdata->lowlif->set_timeout(dev, BITFORCE_VCOM_TIMEOUT_DSEC_ZCX);
		bitforce_cmd1b(proc, buf, bufsz, "ZGX", 3);
		devdata->lowlif->set_timeout(dev, BITFORCE_VCOM_TIMEOUT_DSEC);
	}
	else
		bitforce_cmd1b(proc, buf, bufsz, "ZGX", 3);
}

struct bitforce_init_data {
	struct bitforce_lowl_interface *lowlif;
	enum bitforce_style style;
	long devmask;
	int *parallels;
	unsigned queue_depth;
	unsigned long scan_interval_ms;
	unsigned max_queueid;
};

static
int bitforce_chips_to_plan_for(int parallel, int chipcount) {
	if (parallel < 1)
		return parallel;
	return upper_power_of_two_u32(chipcount);
}

static
bool bitforce_lowl_match(const struct lowlevel_device_info * const info)
{
#ifdef NEED_BFG_LOWL_PCI
	if (info->lowl == &lowl_pci)
		return info->vid == BFL_PCI_VENDOR_ID;
#endif
#ifdef NEED_BFG_LOWL_MSWIN
	if (lowl_mswin_match_guid(info, &WIN_GUID_DEVINTERFACE_MonarchKMDF))
		return true;
#endif
	return lowlevel_match_product(info, "BitFORCE", "SHA256");
}

char *bitforce_copy_name(char * const pdevbuf)
{
	char *s;
	if (likely((!memcmp(pdevbuf, ">>>ID: ", 7)) && (s = strstr(pdevbuf + 3, ">>>"))))
	{
		s[0] = '\0';
		s = strdup(&pdevbuf[7]);
	}
	else
	{
		for (s = &pdevbuf[strlen(pdevbuf)]; isspace(*--s); )
			*s = '\0';
		s = strdup(pdevbuf);
	}
	return s;
}

static
bool bitforce_detect_oneof(const char * const devpath, struct bitforce_lowl_interface * const lowlif)
{
	struct cgpu_info *bitforce;
	char pdevbuf[0x100];
	size_t pdevbuf_len;
	char *s = NULL;
	int procs = 1, parallel = -1;
	long maxchipno = 0;
	struct bitforce_init_data *initdata;
	char *manuf = NULL;
	struct bitforce_data dummy_bfdata = {
		.lowlif = lowlif,
		.xlink_id = 0,
	};
	struct cgpu_info dummy_cgpu = {
		.device = &dummy_cgpu,
		.dev_repr = "BFL",
		.proc_repr = "BFL",
		.device_path = devpath,
		.device_data = &dummy_bfdata,
	};
	dummy_cgpu.device_fd = -1;

	applog(LOG_DEBUG, "BFL: Attempting to open %s", devpath);
	bitforce_open(&dummy_cgpu);

	if (unlikely(!dummy_bfdata.is_open)) {
		applog(LOG_DEBUG, "BFL: Failed to open %s", devpath);
		return false;
	}

	bitforce_zgx(&dummy_cgpu, pdevbuf, sizeof(pdevbuf));
	if (unlikely(!pdevbuf[0])) {
		applog(LOG_DEBUG, "BFL: Error reading/timeout (ZGX)");
		bitforce_close(&dummy_cgpu);
		return 0;
	}

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		applog(LOG_DEBUG, "BFL: Didn't recognise BitForce on %s", devpath);
		bitforce_close(&dummy_cgpu);
		return false;
	}

	if (serial_claim_v(devpath, &bitforce_drv))
	{
		bitforce_close(&dummy_cgpu);
		return false;
	}
	
	applog(LOG_DEBUG, "Found BitForce device on %s", devpath);
	
	s = bitforce_copy_name(pdevbuf);
	
	initdata = malloc(sizeof(*initdata));
	*initdata = (struct bitforce_init_data){
		.lowlif = lowlif,
		.style = BFS_FPGA,
		.queue_depth = BITFORCE_MAX_QUEUED_MAX,
	};
	bitforce_cmd1b(&dummy_cgpu, pdevbuf, sizeof(pdevbuf), "ZCX", 3);
	for (int i = 0; (!pdevbuf[0]) && i < 4; ++i)
		bitforce_gets(pdevbuf, sizeof(pdevbuf), &dummy_cgpu);
	for ( ;
	      strncasecmp(pdevbuf, "OK", 2);
	      bitforce_gets(pdevbuf, sizeof(pdevbuf), &dummy_cgpu) )
	{
		pdevbuf_len = strlen(pdevbuf);
		if (unlikely(!pdevbuf_len))
			continue;
		pdevbuf[pdevbuf_len-1] = '\0';  // trim newline
		
		applog(LOG_DEBUG, "  %s", pdevbuf);
		
		if (!strncasecmp(pdevbuf, "PROCESSOR ", 10))
			maxchipno = max(maxchipno, atoi(&pdevbuf[10]));
		else
		if (!strncasecmp(pdevbuf, "CHANNEL", 7))
			maxchipno = max(maxchipno, atoi(&pdevbuf[7]));
		else
		if (!strncasecmp(pdevbuf, "CORTEX-", 7))
			maxchipno = max(maxchipno, strtol(&pdevbuf[7], NULL, 0x10));
		else
		if (!strncasecmp(pdevbuf, "DEVICES IN CHAIN:", 17))
			procs = atoi(&pdevbuf[17]);
		else
		if (!strncasecmp(pdevbuf, "CHAIN PRESENCE MASK:", 20))
			initdata->devmask = strtol(&pdevbuf[20], NULL, 16);
		else
		if (!strncasecmp(pdevbuf, "DEVICE:", 7) && strstr(pdevbuf, "SC") && initdata->style == BFS_FPGA)
			initdata->style = BFS_65NM;
		else
		if (!strncasecmp(pdevbuf, "CHIP PARALLELIZATION: YES @", 27))
			parallel = atoi(&pdevbuf[27]);
		else
		if (!strncasecmp(pdevbuf, "ASIC CHANNELS:", 14))
		{
			parallel = atoi(&pdevbuf[14]);
			initdata->style = BFS_28NM;
		}
		else
		if (!strncasecmp(pdevbuf, "Queue Depth:", 12))
			initdata->queue_depth = atoi(&pdevbuf[12]);
		else
		if (!strncasecmp(pdevbuf, "Scan Interval:", 14))
			initdata->scan_interval_ms = atoi(&pdevbuf[14]);
		else
		if (!strncasecmp(pdevbuf, "Max Queue ID:", 13))
			initdata->max_queueid = strtol(&pdevbuf[13], NULL, 0x10);
		else
		if (!strncasecmp(pdevbuf, "MANUFACTURER:", 13))
		{
			manuf = &pdevbuf[13];
			while (manuf[0] && isspace(manuf[0]))
				++manuf;
			if (manuf[0])
				manuf = strdup(manuf);
			else
				manuf = NULL;
		}
	}
	parallel = bitforce_chips_to_plan_for(parallel, maxchipno);
	initdata->parallels = malloc(sizeof(initdata->parallels[0]) * procs);
	initdata->parallels[0] = parallel;
	parallel = abs(parallel);
	for (int proc = 1; proc < procs; ++proc)
	{
		applog(LOG_DEBUG, "Slave board %d:", proc);
		initdata->parallels[proc] = -1;
		maxchipno = 0;
		bitforce_cmd1b(&dummy_cgpu, pdevbuf, sizeof(pdevbuf), "ZCX", 3);
		for (int i = 0; (!pdevbuf[0]) && i < 4; ++i)
			bitforce_gets(pdevbuf, sizeof(pdevbuf), &dummy_cgpu);
		for ( ;
		      strncasecmp(pdevbuf, "OK", 2);
		      bitforce_gets(pdevbuf, sizeof(pdevbuf), &dummy_cgpu) )
		{
			pdevbuf_len = strlen(pdevbuf);
			if (unlikely(!pdevbuf_len))
				continue;
			pdevbuf[pdevbuf_len-1] = '\0';  // trim newline
			
			applog(LOG_DEBUG, "  %s", pdevbuf);
			
			if (!strncasecmp(pdevbuf, "PROCESSOR ", 10))
				maxchipno = max(maxchipno, atoi(&pdevbuf[10]));
			else
			if (!strncasecmp(pdevbuf, "CHIP PARALLELIZATION: YES @", 27))
				initdata->parallels[proc] = atoi(&pdevbuf[27]);
		}
		initdata->parallels[proc] = bitforce_chips_to_plan_for(initdata->parallels[proc], maxchipno);
		parallel += abs(initdata->parallels[proc]);
	}
	bitforce_close(&dummy_cgpu);
	
	if (unlikely((procs != 1 || parallel != 1) && initdata->style == BFS_FPGA))
	{
		// Only bitforce_queue supports parallelization and XLINK, so force SC mode and hope for the best
		applog(LOG_WARNING, "SC features detected with non-SC device; this is not supported!");
		initdata->style = BFS_65NM;
	}
	
	// We have a real BitForce!
	bitforce = calloc(1, sizeof(*bitforce));
	bitforce->drv = &bitforce_drv;
	if (initdata->style != BFS_FPGA)
		bitforce->drv = &bitforce_queue_api;
	bitforce->device_path = strdup(devpath);
	if (manuf)
		bitforce->dev_manufacturer = manuf;
	bitforce->deven = DEV_ENABLED;
	bitforce->procs = parallel;
	bitforce->threads = 1;
	if (initdata->style != BFS_FPGA)
		bitforce->cutofftemp = 85;

	if (s)
		bitforce->name = s;
	bitforce->device_data = initdata;
	
	// Skip fanspeed until we probe support for it
	bitforce->set_device_funcs = &bitforce_set_device_funcs[1];

	mutex_init(&bitforce->device_mutex);

	return add_cgpu(bitforce);
}

static
bool bitforce_detect_one(const char * const devpath)
{
	return bitforce_detect_oneof(devpath, &bfllif_vcom);
}

static
bool bitforce_lowl_probe(const struct lowlevel_device_info * const info)
{
#ifdef NEED_BFG_LOWL_PCI
	if (info->lowl == &lowl_pci)
		return bitforce_detect_oneof(info->path, &bfllif_pci);
#endif
#ifdef NEED_BFG_LOWL_MSWIN
	if (lowl_mswin_match_guid(info, &WIN_GUID_DEVINTERFACE_MonarchKMDF))
		return bitforce_detect_oneof(info->path, &bfllif_vcom);
#endif
	return vcom_lowl_probe_wrapper(info, bitforce_detect_one);
}

struct bitforce_proc_data {
	struct cgpu_info *cgpu;
	bool handles_board;  // The first processor handles the queue for the entire board
};

static void bitforce_clear_buffer(struct cgpu_info *);

static
void bitforce_comm_error(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	
	data->noncebuf[0] = '\0';
	applog(LOG_ERR, "%"PRIpreprv": Comms error", bitforce->proc_repr);
	dev_error(bitforce, REASON_DEV_COMMS_ERROR);
	inc_hw_errors_only(thr);
	if (!bitforce_open(bitforce))
	{
		applog(LOG_ERR, "%s: Error reopening %s", bitforce->dev_repr, bitforce->device_path);
		return;
	}
	/* empty read buffer */
	bitforce_clear_buffer(bitforce);
}

static
void __bitforce_clear_buffer(struct cgpu_info * const dev)
{
	char pdevbuf[0x100];
	int count = 0;

	do {
		pdevbuf[0] = '\0';
		bitforce_gets(pdevbuf, sizeof(pdevbuf), dev);
	} while (pdevbuf[0] && (++count < 10));
}

static void bitforce_clear_buffer(struct cgpu_info *bitforce)
{
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	
	mutex_lock(mutexp);
	if (devdata->is_open)
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Clearing read buffer", bitforce->proc_repr);
		__bitforce_clear_buffer(bitforce);
	}
	mutex_unlock(mutexp);
}

void work_list_del(struct work **head, struct work *);

void bitforce_reinit(struct cgpu_info *bitforce)
{
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	struct bitforce_data *data = bitforce->device_data;
	struct thr_info *thr = bitforce->thr[0];
	struct bitforce_proc_data *procdata = thr->cgpu_data;
	const char *devpath = bitforce->device_path;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	int retries = 0;
	char pdevbuf[0x100];
	
	if (!procdata->handles_board)
		return;

	mutex_lock(mutexp);
	
	applog(LOG_WARNING, "%"PRIpreprv": Re-initialising", bitforce->proc_repr);

	if (devdata->is_open)
	{
		bitforce_close(bitforce);
		cgsleep_ms(5000);
	}

	bitforce_open(bitforce);
	if (unlikely(!devdata->is_open)) {
		mutex_unlock(mutexp);
		applog(LOG_ERR, "%s: Failed to open %s", bitforce->dev_repr, devpath);
		return;
	}

	__bitforce_clear_buffer(bitforce);
	
	do {
		bitforce_zgx(bitforce, pdevbuf, sizeof(pdevbuf));
		if (unlikely(!pdevbuf[0])) {
			mutex_unlock(mutexp);
			bitforce_close(bitforce);
			applog(LOG_ERR, "%s: Error reading/timeout (ZGX)", bitforce->dev_repr);
			return;
		}

		if (retries++)
			cgsleep_ms(10);
	} while (strstr(pdevbuf, "BUSY") && (retries * 10 < BITFORCE_TIMEOUT_MS));

	if (unlikely(!strstr(pdevbuf, "SHA256"))) {
		mutex_unlock(mutexp);
		bitforce_close(bitforce);
		applog(LOG_ERR, "%s: Didn't recognise BitForce on %s returned: %s", bitforce->dev_repr, devpath, pdevbuf);
		return;
	}
	
	free((void*)bitforce->name);
	bitforce->name = bitforce_copy_name(pdevbuf);

	bitforce->sleep_ms = data->sleep_ms_default;
	
	if (bitforce->drv == &bitforce_queue_api)
	{
		struct work *work, *tmp;
		
		timer_set_delay_from_now(&thr->tv_poll, 0);
		notifier_wake(thr->notifier);
		
		bitforce_cmd1b(bitforce, pdevbuf, sizeof(pdevbuf), "ZQX", 3);
		DL_FOREACH_SAFE(thr->work_list, work, tmp)
			work_list_del(&thr->work_list, work);
		data->queued = 0;
		data->ready_to_queue = 0;
		data->already_have_results = false;
		data->just_flushed = true;
		thr->queue_full = false;
	}

	mutex_unlock(mutexp);

}

static void bitforce_flash_led(struct cgpu_info *bitforce)
{
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;

	if (unlikely(!devdata->is_open))
		return;

	/* Do not try to flash the led if we're polling for a result to
	 * minimise the chance of interleaved results */
	if (bitforce->polling)
		return;

	/* It is not critical flashing the led so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(mutexp))
		return;

	char pdevbuf[0x100];
	bitforce_cmd1b(bitforce, pdevbuf, sizeof(pdevbuf), "ZMX", 3);

	/* Once we've tried - don't do it until told to again */
	bitforce->flash_led = false;

	/* However, this stops anything else getting a reply
	 * So best to delay any other access to the BFL */
	cgsleep_ms(4000);

	mutex_unlock(mutexp);

	return; // nothing is returned by the BFL
}

static
float my_strtof(const char *nptr, char **endptr)
{
	float f = strtof(nptr, endptr);
	
	/* Cope with older software  that breaks and reads nonsense
	 * values */
	if (f > 100)
		f = strtod(nptr, endptr);
	
	return f;
}

static
void set_float_if_gt_zero(float *var, float value)
{
	if (value > 0)
		*var = value;
}

static bool bitforce_get_temp(struct cgpu_info *bitforce)
{
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	char pdevbuf[0x40];
	char voltbuf[0x40];
	char *s;
	struct cgpu_info *chip_cgpu;

	if (unlikely(!devdata->is_open))
		return false;

	/* Do not try to get the temperature if we're polling for a result to
	 * minimise the chance of interleaved results */
	if (bitforce->polling)
		return true;

	// Flash instead of Temp - doing both can be too slow
	if (bitforce->flash_led) {
		bitforce_flash_led(bitforce);
 		return true;
	}

	/* It is not critical getting temperature so don't get stuck if we
	 * can't grab the mutex here */
	if (mutex_trylock(mutexp))
		return false;

	if (data->style != BFS_FPGA)
	{
		if (unlikely(!data->probed))
		{
			bitforce_cmd1b(bitforce, voltbuf, sizeof(voltbuf), "Z9X", 3);
			if (strncasecmp(voltbuf, "ERR", 3))
			{
				data->supports_fanspeed = true;
				bitforce->set_device_funcs = bitforce_set_device_funcs;
			}
			data->probed = true;
		}
		bitforce_cmd1b(bitforce, voltbuf, sizeof(voltbuf), "ZTX", 3);
	}
	bitforce_cmd1b(bitforce, pdevbuf, sizeof(pdevbuf), "ZLX", 3);
	mutex_unlock(mutexp);
	
	if (data->style != BFS_FPGA && likely(voltbuf[0]))
	{
		// Process voltage info
		// "NNNxxx,NNNxxx,NNNxxx"
		int n = 1;
		for (char *p = voltbuf; p[0]; ++p)
			if (p[0] == ',')
				++n;
		
		long *out = malloc(sizeof(long) * n);
		if (!out)
			goto skipvolts;
		
		n = 0;
		char *saveptr, *v;
		for (v = strtok_r(voltbuf, ",", &saveptr); v; v = strtok_r(NULL, ",", &saveptr))
			out[n++] = strtol(v, NULL, 10);
		
		data->volts_count = 0;
		free(data->volts);
		data->volts = out;
		data->volts_count = n;
	}
	
skipvolts:
	if (unlikely(!pdevbuf[0])) {
		struct thr_info *thr = bitforce->thr[0];
		applog(LOG_ERR, "%"PRIpreprv": Error: Get temp returned empty string/timed out", bitforce->proc_repr);
		inc_hw_errors_only(thr);
		return false;
	}

	if ((!strncasecmp(pdevbuf, "TEMP", 4)) && (s = strchr(pdevbuf + 4, ':'))) {
		float temp = my_strtof(s + 1, &s);
		
		set_float_if_gt_zero(&data->temp[0], temp);
		for ( ; s[0]; ++s)
		{
			if (!strncasecmp(s, "TEMP", 4) && (s = strchr(&s[4], ':')))
			{
				float temp2 = my_strtof(s + 1, &s);
				set_float_if_gt_zero(&data->temp[1], temp2);
				if (temp2 > temp)
					temp = temp2;
			}
		}

		if (temp > 0)
		{
			chip_cgpu = bitforce;
			for (int i = 0; i < data->parallel; ++i, (chip_cgpu = chip_cgpu->next_proc))
				chip_cgpu->temp = temp;
		}
	} else {
		struct thr_info *thr = bitforce->thr[0];
		/* Use the temperature monitor as a kind of watchdog for when
		 * our responses are out of sync and flush the buffer to
		 * hopefully recover */
		applog(LOG_WARNING, "%"PRIpreprv": Garbled response probably throttling, clearing buffer", bitforce->proc_repr);
		dev_error(bitforce, REASON_DEV_THROTTLE);
		/* Count throttling episodes as hardware errors */
		inc_hw_errors_only(thr);
		bitforce_clear_buffer(bitforce);
		return false;
	}

	return true;
}

static inline
void dbg_block_data(struct cgpu_info *bitforce)
{
	if (!opt_debug)
		return;
	
	struct bitforce_data *data = bitforce->device_data;
	char s[89];
	bin2hex(s, &data->next_work_ob[8], 44);
	applog(LOG_DEBUG, "%"PRIpreprv": block data: %s", bitforce->proc_repr, s);
}

static void bitforce_change_mode(struct cgpu_info *, enum bitforce_proto);

static
bool bitforce_job_prepare(struct thr_info *thr, struct work *work, __maybe_unused uint64_t max_nonce)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	unsigned char *ob_ms = &data->next_work_ob[8];
	unsigned char *ob_dt = &ob_ms[32];
	
	// If polling job_start, cancel it
	if (data->poll_func == 1)
	{
		thr->tv_poll.tv_sec = -1;
		data->poll_func = 0;
	}
	
	memcpy(ob_ms, work->midstate, 32);
	memcpy(ob_dt, work->data + 64, 12);
	switch (data->proto)
	{
		case BFP_BQUEUE:
			quithere(1, "%"PRIpreprv": Impossible BFP_BQUEUE", bitforce->proc_repr);
		case BFP_PQUEUE:
			quithere(1, "%"PRIpreprv": Impossible BFP_PQUEUE", bitforce->proc_repr);
		case BFP_RANGE:
		{
			uint32_t *ob_nonce = (uint32_t*)&(ob_dt[32]);
			ob_nonce[0] = htobe32(work->blk.nonce);
			ob_nonce[1] = htobe32(work->blk.nonce + bitforce->nonces);
			// FIXME: if nonce range fails... we didn't increment enough
			work->blk.nonce += bitforce->nonces + 1;
			break;
		}
		case BFP_WORK:
			work->blk.nonce = 0xffffffff;
	}
	
	return true;
}

static
void bitforce_change_mode(struct cgpu_info *bitforce, enum bitforce_proto proto)
{
	struct bitforce_data *data = bitforce->device_data;
	
	if (data->proto == proto)
		return;
	if (data->proto == BFP_RANGE)
	{
		bitforce->nonces = 0xffffffff;
		bitforce->sleep_ms *= 5;
		data->sleep_ms_default *= 5;
		switch (proto)
		{
			case BFP_WORK:
				data->next_work_cmd = "ZDX";
			default:
				;
		}
		if (data->style != BFS_FPGA)
		{
			// "S|---------- MidState ----------||-DataTail-|E"
			data->next_work_ob[7] = 45;
			data->next_work_ob[8+32+12] = '\xAA';
			data->next_work_obsz = 46;
		}
		else
		{
			// ">>>>>>>>|---------- MidState ----------||-DataTail-|>>>>>>>>"
			memset(&data->next_work_ob[8+32+12], '>', 8);
			data->next_work_obsz = 60;
		}
	}
	else
	if (proto == BFP_RANGE)
	{
		/* Split work up into 1/5th nonce ranges */
		bitforce->nonces = 0x33333332;
		bitforce->sleep_ms /= 5;
		data->sleep_ms_default /= 5;
		data->next_work_cmd = "ZPX";
		if (data->style != BFS_FPGA)
		{
			data->next_work_ob[7] = 53;
			data->next_work_obsz = 54;
		}
		else
			data->next_work_obsz = 68;
	}
	data->proto = proto;
	bitforce->kname = protonames[proto];
}

static
void bitforce_job_start(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	unsigned char *ob = data->next_work_obs;
	char pdevbuf[0x100];
	struct timeval tv_now;

	data->result_busy_polled = 0;
	
	if (data->queued)
	{
		uint32_t delay;
		
		// get_results collected more accurate job start time
		mt_job_transition(thr);
		job_start_complete(thr);
		data->queued = 0;
		delay = (uint32_t)bitforce->sleep_ms * 1000;
		if (unlikely(data->already_have_results))
			delay = 0;
		timer_set_delay(&thr->tv_morework, &bitforce->work_start_tv, delay);
		return;
	}

	if (unlikely(!devdata->is_open))
		goto commerr;
re_send:
	mutex_lock(mutexp);
	bitforce_cmd2(bitforce, pdevbuf, sizeof(pdevbuf), data->next_work_cmd, ob, data->next_work_obsz);
	if (!pdevbuf[0] || !strncasecmp(pdevbuf, "B", 1)) {
		mutex_unlock(mutexp);
		cgtime(&tv_now);
		timer_set_delay(&thr->tv_poll, &tv_now, WORK_CHECK_INTERVAL_MS * 1000);
		data->poll_func = 1;
		return;
	} else if (unlikely(strncasecmp(pdevbuf, "OK", 2))) {
		mutex_unlock(mutexp);
		switch (data->proto)
		{
			case BFP_RANGE:
				applog(LOG_WARNING, "%"PRIpreprv": Does not support nonce range, disabling", bitforce->proc_repr);
				bitforce_change_mode(bitforce, BFP_WORK);
				goto re_send;
			default:
				;
		}
		applog(LOG_ERR, "%"PRIpreprv": Error: Send work reports: %s", bitforce->proc_repr, pdevbuf);
		goto commerr;
	}

	mt_job_transition(thr);
	mutex_unlock(mutexp);

	dbg_block_data(bitforce);

	cgtime(&tv_now);
	bitforce->work_start_tv = tv_now;
	
	timer_set_delay(&thr->tv_morework, &tv_now, bitforce->sleep_ms * 1000);
	
	job_start_complete(thr);
	return;

commerr:
	bitforce_comm_error(thr);
	job_start_abort(thr, true);
}

static char _discardedbuf[0x10];

static
int bitforce_zox(struct thr_info *thr, const char *cmd, int * const out_inprog)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	char *pdevbuf = &data->noncebuf[0];
	int count;
	
	mutex_lock(mutexp);
	bitforce_cmd1b(bitforce, pdevbuf, sizeof(data->noncebuf), cmd, 3);
	if (!strncasecmp(pdevbuf, "INPROCESS:", 10))
	{
		if (out_inprog)
			*out_inprog = atoi(&pdevbuf[10]);
		bitforce_gets(pdevbuf, sizeof(data->noncebuf), bitforce);
	}
	else
	if (out_inprog)
		*out_inprog = -1;
	if (!strncasecmp(pdevbuf, "COUNT:", 6))
	{
		count = atoi(&pdevbuf[6]);
		size_t cls = strlen(pdevbuf);
		char *pmorebuf = &pdevbuf[cls];
		size_t szleft = sizeof(data->noncebuf) - cls, sz;
		
		if (count && data->queued)
			cgtime(&bitforce->work_start_tv);
		
		while (true)
		{
			bitforce_gets(pmorebuf, szleft, bitforce);
			if (!strncasecmp(pmorebuf, "OK", 2))
			{
				pmorebuf[0] = '\0';  // process expects only results
				break;
			}
			sz = strlen(pmorebuf);
			if (!sz)
			{
				applog(LOG_ERR, "%"PRIpreprv": Timeout during %s", bitforce->proc_repr, cmd);
				break;
			}
			szleft -= sz;
			pmorebuf += sz;
			if (unlikely(szleft < BITFORCE_QRESULT_LINE_LEN))
			{
				// Out of buffer space somehow :(
				applog(LOG_ERR, "%"PRIpreprv": Ran out of buffer space for results, discarding extra data", bitforce->proc_repr);
				pmorebuf = _discardedbuf;
				szleft = sizeof(_discardedbuf);
			}
		}
	}
	else
		count = -1;
	mutex_unlock(mutexp);
	
	return count;
}

static inline char *next_line(char *);

static
void bitforce_job_get_results(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	struct bitforce_data *data = bitforce->device_data;
	unsigned int delay_time_ms;
	struct timeval elapsed;
	struct timeval now;
	char *pdevbuf = &data->noncebuf[0];
	bool stale;
	int count;

	cgtime(&now);
	timersub(&now, &bitforce->work_start_tv, &elapsed);
	bitforce->wait_ms = tv_to_ms(elapsed);
	bitforce->polling = true;
	
	if (unlikely(!devdata->is_open))
		goto commerr;

	stale = stale_work(work, true);
	
	if (unlikely(bitforce->wait_ms < bitforce->sleep_ms))
	{
		// We're likely here because of a work restart
		// Since Bitforce cannot stop a work without losing results, only do it if the current job is finding stale shares
		if (!stale)
		{
			delay_time_ms = bitforce->sleep_ms - bitforce->wait_ms;
			timer_set_delay(&thr->tv_poll, &now, delay_time_ms * 1000);
			data->poll_func = 2;
			return;
		}
	}

	while (1) {
		if (data->already_have_results)
		{
			data->already_have_results = false;
			strcpy(pdevbuf, "COUNT:0");
			count = 1;
			break;
		}
		
		const char * const cmd = "ZFX";
		count = bitforce_zox(thr, cmd, NULL);

		cgtime(&now);
		timersub(&now, &bitforce->work_start_tv, &elapsed);

		if (elapsed.tv_sec >= BITFORCE_LONG_TIMEOUT_S) {
			applog(LOG_ERR, "%"PRIpreprv": took %lums - longer than %lums", bitforce->proc_repr,
				tv_to_ms(elapsed), (unsigned long)BITFORCE_LONG_TIMEOUT_MS);
			goto out;
		}

		if (count > 0)
		{
			// Check that queue results match the current work
			// Also, if there are results from the next work, short-circuit this wait
			unsigned char midstate[32], datatail[12];
			char *p;
			int i;
			
			p = pdevbuf;
			for (i = 0; i < count; ++i)
			{
				p = next_line(p);
				hex2bin(midstate, p, 32);
				hex2bin(datatail, &p[65], 12);
				if (!(memcmp(work->midstate, midstate, 32) || memcmp(&work->data[64], datatail, 12)))
					break;
			}
			if (i == count)
			{
				// Didn't find the one we're waiting on
				// Must be extra stuff in the queue results
				char xmid[65];
				char xdt[25];
				bin2hex(xmid, work->midstate, 32);
				bin2hex(xdt, &work->data[64], 12);
				applog(LOG_WARNING, "%"PRIpreprv": Found extra garbage in queue results: %s",
				       bitforce->proc_repr, pdevbuf);
				applog(LOG_WARNING, "%"PRIpreprv": ...while waiting on: %s,%s",
				       bitforce->proc_repr, xmid, xdt);
				count = 0;
			}
			else
			if (i == count - 1)
				// Last one found is what we're looking for
			{}
			else
				// We finished the next job too!
				data->already_have_results = true;
		}
		
		if (!count)
			goto noqr;
		if (pdevbuf[0] && strncasecmp(pdevbuf, "B", 1)) /* BFL does not respond during throttling */
			break;

		data->result_busy_polled = bitforce->wait_ms;
		
		if (stale)
		{
			applog(LOG_NOTICE, "%"PRIpreprv": Abandoning stale search to restart",
			       bitforce->proc_repr);
			goto out;
		}

noqr:
		data->result_busy_polled = bitforce->wait_ms;
		
		/* if BFL is throttling, no point checking so quickly */
		delay_time_ms = (pdevbuf[0] ? BITFORCE_CHECK_INTERVAL_MS : 2 * WORK_CHECK_INTERVAL_MS);
		timer_set_delay(&thr->tv_poll, &now, delay_time_ms * 1000);
		data->poll_func = 2;
		return;
	}

	if (count < 0 && pdevbuf[0] == 'N')
		count = strncasecmp(pdevbuf, "NONCE-FOUND", 11) ? 1 : 0;
	// At this point, 'count' is:
	//   negative, in case of some kind of error
	//   zero, if NO-NONCE (FPGA either completed with no results, or rebooted)
	//   positive, if at least one job completed successfully

	if (elapsed.tv_sec > BITFORCE_TIMEOUT_S) {
		applog(LOG_ERR, "%"PRIpreprv": took %lums - longer than %lums", bitforce->proc_repr,
			tv_to_ms(elapsed), (unsigned long)BITFORCE_TIMEOUT_MS);
		dev_error(bitforce, REASON_DEV_OVER_HEAT);
		inc_hw_errors_only(thr);

		/* If the device truly throttled, it didn't process the job and there
		 * are no results. But check first, just in case we're wrong about it
		 * throttling.
		 */
		if (count > 0)
			goto out;
	} else if (count >= 0) {/* Hashing complete (NONCE-FOUND or NO-NONCE) */
		/* Simple timing adjustment. Allow a few polls to cope with
		 * OS timer delays being variably reliable. wait_ms will
		 * always equal sleep_ms when we've waited greater than or
		 * equal to the result return time.*/
		delay_time_ms = bitforce->sleep_ms;

		if (!data->result_busy_polled)
		{
			// No busy polls before results received
			if (bitforce->wait_ms > delay_time_ms + (WORK_CHECK_INTERVAL_MS * 8))
				// ... due to poll being rather late; ignore it as an anomaly
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on first poll after %ums, later than scheduled %ums (ignoring)",
				       bitforce->proc_repr, bitforce->wait_ms, delay_time_ms);
			else
			if (bitforce->sleep_ms > data->sleep_ms_default + (BITFORCE_CHECK_INTERVAL_MS * 0x20))
			{
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on first poll after %ums, on delayed schedule %ums; Wait time changed to: %ums (default sch)",
				       bitforce->proc_repr, bitforce->wait_ms, delay_time_ms, data->sleep_ms_default);
				bitforce->sleep_ms = data->sleep_ms_default;
			}
			else
			{
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on first poll after %ums, on default schedule %ums; Wait time changed to: %ums (check interval)",
				       bitforce->proc_repr, bitforce->wait_ms, delay_time_ms, BITFORCE_CHECK_INTERVAL_MS);
				bitforce->sleep_ms = BITFORCE_CHECK_INTERVAL_MS;
			}
		}
		else
		{
			if (data->result_busy_polled - bitforce->sleep_ms > WORK_CHECK_INTERVAL_MS)
			{
				bitforce->sleep_ms = data->result_busy_polled - (WORK_CHECK_INTERVAL_MS / 2);
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on Nth poll after %ums (busy poll at %ums, sch'd %ums); Wait time changed to: %ums",
				       bitforce->proc_repr, bitforce->wait_ms, data->result_busy_polled, delay_time_ms, bitforce->sleep_ms);
			}
			else
				applog(LOG_DEBUG, "%"PRIpreprv": Got results on Nth poll after %ums (busy poll at %ums, sch'd %ums); Wait time unchanged",
				       bitforce->proc_repr, bitforce->wait_ms, data->result_busy_polled, delay_time_ms);
		}

		/* Work out the average time taken. Float for calculation, uint for display */
		bitforce->avg_wait_f += (tv_to_ms(elapsed) - bitforce->avg_wait_f) / TIME_AVG_CONSTANT;
		bitforce->avg_wait_d = (unsigned int) (bitforce->avg_wait_f + 0.5);
	}

	applog(LOG_DEBUG, "%"PRIpreprv": waited %dms until %s", bitforce->proc_repr, bitforce->wait_ms, pdevbuf);
	if (count < 0 && strncasecmp(pdevbuf, "I", 1)) {
		inc_hw_errors_only(thr);
		applog(LOG_WARNING, "%"PRIpreprv": Error: Get result reports: %s", bitforce->proc_repr, pdevbuf);
		bitforce_clear_buffer(bitforce);
	}
out:
	bitforce->polling = false;
	job_results_fetched(thr);
	return;

commerr:
	bitforce_comm_error(thr);
	goto out;
}

static
void bitforce_process_result_nonces(struct thr_info *thr, struct work *work, char *pnoncebuf)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	uint32_t nonce;
	
	while (1) {
		hex2bin((void*)&nonce, pnoncebuf, 4);
		nonce = be32toh(nonce);
		if (unlikely(data->proto == BFP_RANGE && (nonce >= work->blk.nonce ||
			/* FIXME: blk.nonce is probably moved on quite a bit now! */
			(work->blk.nonce > 0 && nonce < work->blk.nonce - bitforce->nonces - 1)))) {
				applog(LOG_WARNING, "%"PRIpreprv": Disabling broken nonce range support", bitforce->proc_repr);
				bitforce_change_mode(bitforce, BFP_WORK);
		}
			
		submit_nonce(thr, work, nonce);
		if (strncmp(&pnoncebuf[8], ",", 1))
			break;
		pnoncebuf += 9;
	}
}

static
bool bitforce_process_qresult_line_i(struct thr_info *thr, char *midstate, char *datatail, char *buf, struct work *work)
{
	if (!work)
		return false;
	if (memcmp(work->midstate, midstate, 32))
		return false;
	if (memcmp(&work->data[64], datatail, 12))
		return false;
	
	char *end;
	if (strtol(&buf[90], &end, 10))
		bitforce_process_result_nonces(thr, work, &end[1]);
	
	return true;
}

static
void bitforce_process_qresult_line(struct thr_info *thr, char *buf, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	char midstate[32], datatail[12];
	
	hex2bin((void*)midstate, buf, 32);
	hex2bin((void*)datatail, &buf[65], 12);
	
	if (!( bitforce_process_qresult_line_i(thr, midstate, datatail, buf, work)
	    || bitforce_process_qresult_line_i(thr, midstate, datatail, buf, thr->work)
	    || bitforce_process_qresult_line_i(thr, midstate, datatail, buf, thr->prev_work)
	    || bitforce_process_qresult_line_i(thr, midstate, datatail, buf, thr->next_work) ))
	{
		applog(LOG_ERR, "%"PRIpreprv": Failed to find work for queued results", bitforce->proc_repr);
		inc_hw_errors_only(thr);
	}
}

static inline
char *next_line(char *in)
{
	while (in[0] && (in++)[0] != '\n')
	{}
	return in;
}

static
int64_t bitforce_job_process_results(struct thr_info *thr, struct work *work, __maybe_unused bool stopping)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	char *pnoncebuf = &data->noncebuf[0];
	int count;
	
	if (!strncasecmp(pnoncebuf, "NO-", 3))
		return bitforce->nonces;   /* No valid nonce found */
	
	if (!strncasecmp(pnoncebuf, "NONCE-FOUND", 11))
	{
		bitforce_process_result_nonces(thr, work, &pnoncebuf[12]);
		count = 1;
	}
	else
	if (!strncasecmp(pnoncebuf, "COUNT:", 6))
	{
		count = 0;
		pnoncebuf = next_line(pnoncebuf);
		while (pnoncebuf[0])
		{
			bitforce_process_qresult_line(thr, pnoncebuf, work);
			++count;
			pnoncebuf = next_line(pnoncebuf);
		}
	}
	else
		return 0;

	// FIXME: This might have changed in the meantime (new job start, or broken)
	return bitforce->nonces * count;
}

static void bitforce_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	bitforce_close(bitforce);
}

static void biforce_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;

	bitforce_reinit(bitforce);
}

static bool bitforce_get_stats(struct cgpu_info *bitforce)
{
	struct bitforce_proc_data *procdata = bitforce->thr[0]->cgpu_data;
	
	if (!procdata->handles_board)
		return true;
	return bitforce_get_temp(bitforce);
}

static bool bitforce_identify(struct cgpu_info *bitforce)
{
	bitforce->flash_led = true;
	return true;
}

static
void bitforce_zero_stats(struct cgpu_info * const proc)
{
	struct bitforce_data *data = proc->device_data;
	
	// These don't get cleared when not-read, so we clear them here
	data->volts_count = 0;
	data->temp[0] = data->temp[1] = 0;
	free(data->volts);
	data->volts = NULL;
	
	proc->avg_wait_f = 0;
}

static bool bitforce_thread_init(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data;
	struct bitforce_proc_data *procdata;
	struct bitforce_init_data *initdata = bitforce->device_data;
	const enum bitforce_style style = initdata->style;
	int xlink_id = 0, boardno = 0;
	struct bitforce_proc_data *first_on_this_board;
	char buf[100];
	
	for ( ; bitforce; bitforce = bitforce->next_proc)
	{
		thr = bitforce->thr[0];
		
		if (unlikely(xlink_id > 30))
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to find XLINK address", bitforce->proc_repr);
			dev_error(bitforce, REASON_THREAD_FAIL_INIT);
			bitforce->reinit_backoff = 1e10;
			continue;
		}
		
		bitforce->sleep_ms = BITFORCE_SLEEP_MS;
		bitforce->device_data = data = malloc(sizeof(*data));
		*data = (struct bitforce_data){
			.lowlif = initdata->lowlif,
			.xlink_id = xlink_id,
			.next_work_ob = ">>>>>>>>|---------- MidState ----------||-DataTail-||Nonces|>>>>>>>>",
			.proto = BFP_RANGE,
			.style = style,
			.sleep_ms_default = BITFORCE_SLEEP_MS,
			.parallel = abs(initdata->parallels[boardno]),
			.parallel_protocol = (initdata->parallels[boardno] != -1),
			.max_queueid = initdata->max_queueid,
		};
		thr->cgpu_data = procdata = malloc(sizeof(*procdata));
		*procdata = (struct bitforce_proc_data){
			.handles_board = true,
			.cgpu = bitforce,
		};
		if (style != BFS_FPGA)
		{
			// ".......S|---------- MidState ----------||-DataTail-||Nonces|E"
			data->next_work_ob[8+32+12+8] = '\xAA';
			data->next_work_obs = &data->next_work_ob[7];
			
			switch (style)
			{
				case BFS_FPGA:  // impossible
				case BFS_65NM:
					data->max_queue_at_once = BITFORCE_MAX_BQUEUE_AT_ONCE_65NM;
					break;
				case BFS_28NM:
					data->max_queue_at_once = BITFORCE_MAX_BQUEUE_AT_ONCE_28NM;
			}
			
			if (bitforce->drv == &bitforce_queue_api)
			{
				bitforce_change_mode(bitforce, data->parallel_protocol ? BFP_PQUEUE : BFP_BQUEUE);
				bitforce->sleep_ms = data->sleep_ms_default = 100;
				timer_set_delay_from_now(&thr->tv_poll, 0);
				data->queued_max = data->parallel * 2;
				if (data->queued_max < BITFORCE_MIN_QUEUED_MAX)
					data->queued_max = BITFORCE_MIN_QUEUED_MAX;
				if (data->queued_max > initdata->queue_depth)
					data->queued_max = initdata->queue_depth;
			}
			else
				bitforce_change_mode(bitforce, BFP_WORK);
		}
		else
		{
			data->next_work_obs = &data->next_work_ob[0];
			
			// Unconditionally change away from cold-initialized BFP_RANGE, to allow for setting up other variables
			bitforce_change_mode(bitforce, BFP_WORK);
			/* Initially enable support for nonce range and disable it later if it
			 * fails */
			if (opt_bfl_noncerange)
				bitforce_change_mode(bitforce, BFP_RANGE);
		}
		
		if (initdata->scan_interval_ms)
			bitforce->sleep_ms = initdata->scan_interval_ms;
		
		bitforce->status = LIFE_INIT2;
		
		first_on_this_board = procdata;
		for (int proc = 1; proc < data->parallel; ++proc)
		{
			bitforce = bitforce->next_proc;
			assert(bitforce);
			thr = bitforce->thr[0];
			
			thr->queue_full = true;
			thr->cgpu_data = procdata = malloc(sizeof(*procdata));
			*procdata = *first_on_this_board;
			procdata->handles_board = false;
			procdata->cgpu = bitforce;
			bitforce->device_data = data;
			bitforce->status = LIFE_INIT2;
			bitforce->kname = first_on_this_board->cgpu->kname;
		}
		applog(LOG_DEBUG, "%s: Board %d: %"PRIpreprv"-%"PRIpreprv, bitforce->dev_repr, boardno, first_on_this_board->cgpu->proc_repr, bitforce->proc_repr);
		
		++boardno;
		while (xlink_id < 31 && !(initdata->devmask & (1 << ++xlink_id)))
		{}
	}
	
	bitforce = thr->cgpu->device;

	free(initdata->parallels);
	free(initdata);

	if (unlikely(!bitforce_open(bitforce)))
	{
		applog(LOG_ERR, "%s: Failed to open %s", bitforce->dev_repr, bitforce->device_path);
		return false;
	}
	applog(LOG_INFO, "%s: Opened %s", bitforce->dev_repr, bitforce->device_path);
	
	if (style != BFS_FPGA)
	{
		// Clear job and results queue, to start fresh; ignore response
		for (int pass = 0, inprog = 1, tmp_inprog; pass < 500 && inprog; ++pass)
		{
			if (pass)
				cgsleep_ms(10);
			applog(LOG_DEBUG, "%s: Flushing job and results queue... pass %d", bitforce->dev_repr, pass);
			
			int last_xlink_id = -1;
			inprog = 0;
			for ( ; bitforce; bitforce = bitforce->next_proc)
			{
				struct bitforce_data * const data = bitforce->device_data;
				if (data->xlink_id == last_xlink_id)
					continue;
				last_xlink_id = data->xlink_id;
				thr = bitforce->thr[0];
				if (!pass)
					bitforce_cmd1b(bitforce, buf, sizeof(buf), "ZQX", 3);
				bitforce_zox(thr, "ZOX", &tmp_inprog);
				if (tmp_inprog == 1)
					inprog = 1;
			}
			bitforce = thr->cgpu->device;
		}
		applog(LOG_DEBUG, "%s: Flushing job and results queue DONE", bitforce->dev_repr);
	}
	
	return true;
}

static
const char *bitforce_set_voltage(struct cgpu_info *proc, const char *optname, const char *newvalue, char *replybuf, enum bfg_set_device_replytype *out_success)
{
	pthread_mutex_t *mutexp = &proc->device->device_mutex;
	char cmd[3] = "VFX";
	
	const unsigned mV = round(atof(newvalue) * 1000);
	static const unsigned n[] = {750, 730, 720, 700, 680, 670, 662, 650, 643, 630, 620, 600, 580, 560, 550, 540};
	for (int i = 0; ; ++i)
	{
		if (i >= 0x10)
			return "Voltage must be from 0.54 to 0.75";
		if (mV >= n[i])
		{
			cmd[1] -= i;
			break;
		}
	}
	
	mutex_lock(mutexp);
	bitforce_cmd1b(proc, replybuf, 8000, cmd, 3);
	mutex_unlock(mutexp);
	
	if (!strncasecmp(replybuf, "OK", 2))
		return NULL;
	
	return replybuf;
}

#ifdef HAVE_CURSES
static
void bitforce_tui_wlogprint_choices(struct cgpu_info *cgpu)
{
	struct bitforce_data *data = cgpu->device_data;
	if (data->supports_fanspeed)
		wlogprint("[F]an control ");
	wlogprint("[V]oltage ");
}

static
const char *bitforce_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	struct bitforce_data *data = cgpu->device_data;
	pthread_mutex_t *mutexp;
	static char replybuf[0x100];
	
	if (!data->supports_fanspeed)
		return NULL;
	switch (input)
	{
		case 'f': case 'F':
		{
			int fanspeed;
			char *intvar;

			intvar = curses_input("Set fan speed (range 0-5 for low to fast or 9 for auto)");
			if (!intvar)
				return "Invalid fan speed\n";
			fanspeed = atoi(intvar);
			free(intvar);
			if ((fanspeed < 0 || fanspeed > 5) && fanspeed != 9)
				return "Invalid fan speed\n";
			
			char cmd[4] = "Z0X";
			cmd[1] += fanspeed;
			mutexp = &cgpu->device->device_mutex;
			mutex_lock(mutexp);
			bitforce_cmd1b(cgpu, replybuf, sizeof(replybuf), cmd, 3);
			mutex_unlock(mutexp);
			return replybuf;
		}
		case 'v': case 'V':
			return proc_set_device_tui_wrapper(cgpu, NULL, bitforce_set_voltage, "Set voltage (0.54-0.75 V)", "Requested voltage change");
	}
	return NULL;
}

static
void bitforce_wlogprint_status(struct cgpu_info *cgpu)
{
	struct bitforce_data *data = cgpu->device_data;
	if (data->temp[0] > 0 && data->temp[1] > 0)
		wlogprint("Temperatures: %4.1fC %4.1fC\n", data->temp[0], data->temp[1]);
	if (data->volts_count)
	{
		// -> "NNN.xxx / NNN.xxx / NNN.xxx"
		size_t sz = (data->volts_count * 10) + 1;
		char buf[sz];
		char *s = buf;
		int rv = 0;
		for (int i = 0; i < data->volts_count; ++i)
		{
			long v = data->volts[i];
			_SNP("%ld.%03d / ", v / 1000, (int)(v % 1000));
		}
		if (rv >= 3 && s[-2] == '/')
			s[-3] = '\0';
		wlogprint("Voltages: %s\n", buf);
	}
}
#endif

static struct api_data *bitforce_drv_stats(struct cgpu_info *cgpu)
{
	struct bitforce_data *data = cgpu->device_data;
	struct api_data *root = NULL;

	// Warning, access to these is not locked - but we don't really
	// care since hashing performance is way more important than
	// locking access to displaying API debug 'stats'
	// If locking becomes an issue for any of them, use copy_data=true also
	root = api_add_uint(root, "Sleep Time", &(cgpu->sleep_ms), false);
	if (data->proto != BFP_BQUEUE && data->proto != BFP_PQUEUE)
		root = api_add_uint(root, "Avg Wait", &(cgpu->avg_wait_d), false);
	if (data->temp[0] > 0 && data->temp[1] > 0)
	{
		root = api_add_temp(root, "Temperature0", &(data->temp[0]), false);
		root = api_add_temp(root, "Temperature1", &(data->temp[1]), false);
	}
	
	for (int i = 0; i < data->volts_count; ++i)
	{
		float voltage = data->volts[i];
		char key[] = "VoltageNN";
		snprintf(&key[7], 3, "%d", i);
		voltage /= 1e3;
		root = api_add_volts(root, key, &voltage, true);
	}

	return root;
}

void bitforce_poll(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	int poll = data->poll_func;
	thr->tv_poll.tv_sec = -1;
	data->poll_func = 0;
	switch (poll)
	{
		case 1:
			bitforce_job_start(thr);
			break;
		case 2:
			bitforce_job_get_results(thr, thr->work);
			break;
		default:
			applog(LOG_ERR, "%"PRIpreprv": Unexpected poll from device API!", thr->cgpu->proc_repr);
	}
}

static
const char *bitforce_set_fanmode(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct bitforce_data *data = proc->device_data;
	pthread_mutex_t *mutexp = &proc->device->device_mutex;
	
	{
		if (!data->supports_fanspeed)
		{
			sprintf(replybuf, "fanmode not supported");
			return replybuf;
		}
		if (!setting || !*setting)
		{
			sprintf(replybuf, "missing fanmode setting");
			return replybuf;
		}
		if (setting[1] || ((setting[0] < '0' || setting[0] > '5') && setting[0] != '9'))
		{
			sprintf(replybuf, "invalid fanmode setting");
			return replybuf;
		}
		
		char cmd[4] = "Z5X";
		cmd[1] = setting[0];
		mutex_lock(mutexp);
		bitforce_cmd1b(proc, replybuf, 256, cmd, 3);
		mutex_unlock(mutexp);
		return replybuf;
	}
}

static
const char *bitforce_rpc_send_cmd1(struct cgpu_info * const proc, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	pthread_mutex_t *mutexp = &proc->device->device_mutex;
	
	{
		mutex_lock(mutexp);
		bitforce_cmd1b(proc, replybuf, 8000, setting, strlen(setting));
		mutex_unlock(mutexp);
		*success = SDR_OK;
		return replybuf;
	}
}

static const struct bfg_set_device_definition bitforce_set_device_funcs[] = {
	{"fanmode", bitforce_set_fanmode, "range 0-5 (low to fast) or 9 (auto)"},
	{"voltage", bitforce_set_voltage, "range 0.54-0.75 V"},
	{"_cmd1", bitforce_rpc_send_cmd1, NULL},
	{NULL},
};

struct device_drv bitforce_drv = {
	.dname = "bitforce",
	.name = "BFL",
	.lowl_match = bitforce_lowl_match,
	.lowl_probe = bitforce_lowl_probe,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitforce_wlogprint_status,
	.proc_tui_wlogprint_choices = bitforce_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitforce_tui_handle_choice,
#endif
	.get_api_stats = bitforce_drv_stats,
	.minerloop = minerloop_async,
	.reinit_device = bitforce_reinit,
	.get_stats = bitforce_get_stats,
	.identify_device = bitforce_identify,
	.thread_init = bitforce_thread_init,
	.job_prepare = bitforce_job_prepare,
	.job_start = bitforce_job_start,
	.job_get_results = bitforce_job_get_results,
	.poll = bitforce_poll,
	.job_process_results = bitforce_job_process_results,
	.thread_shutdown = bitforce_shutdown,
	.thread_enable = biforce_thread_enable
};


static inline
void bitforce_set_queue_full(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	
	thr->queue_full = (data->queued + data->ready_to_queue >= data->queued_max) || (data->ready_to_queue >= data->max_queue_at_once);
}

static
bool bitforce_send_queue(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	struct bitforce_data *data = bitforce->device_data;
	pthread_mutex_t *mutexp = &bitforce->device->device_mutex;
	struct work *work;
	
	if (unlikely(!(devdata->is_open && data->ready_to_queue)))
		return false;
	
	char buf[0x100];
	int queued_ok;
	size_t qjs_sz = (32 + 12 + 1);
	if (data->style == BFS_65NM)
		++qjs_sz;
	size_t qjp_sz = 7 + (qjs_sz * data->ready_to_queue);
	if (data->style == BFS_65NM)
		qjp_sz -= 3;
	uint8_t qjp[qjp_sz], *qjs;
	qjs = &qjp[qjp_sz];
	// NOTE: qjp is build backwards here
	
	*(--qjs) = 0xfe;
	
	work = thr->work_list->prev;
	for (int i = data->ready_to_queue; i > 0; --i, work = work->prev)
	{
		*(--qjs) = 0xaa;
		memcpy(qjs -= 12, work->data + 64, 12);
		memcpy(qjs -= 32, work->midstate, 32);
		if (data->style == BFS_65NM)
			*(--qjs) = 45;
	}
	
	*(--qjs) = data->ready_to_queue;
	*(--qjs) = 0xc1;
	if (data->style == BFS_65NM)
		*(--qjs) = qjp_sz - 1;
	else
	{
		*(--qjs) = qjp_sz >> 8;
		*(--qjs) = qjp_sz & 0xff;
		*(--qjs) = 'X';
		*(--qjs) = 'W';
	}
	
retry:
	mutex_lock(mutexp);
	if (data->style != BFS_65NM)
		bitforce_cmd1c(bitforce, buf, sizeof(buf), qjp, qjp_sz);
	else
	if (data->missing_zwx)
		bitforce_cmd2(bitforce, buf, sizeof(buf), "ZNX", &qjp[3], qjp_sz - 4);
	else
		bitforce_cmd2(bitforce, buf, sizeof(buf), "ZWX", qjp, qjp_sz);
	mutex_unlock(mutexp);
	
	if (!strncasecmp(buf, "ERR:QUEUE", 9))
	{
		// Queue full :(
		applog(LOG_DEBUG, "%"PRIpreprv": Device queue full while attempting to append %d jobs (queued<=%d)",
	           bitforce->proc_repr,
	           data->ready_to_queue, data->queued);
		thr->queue_full = true;
		return false;
	}
	if (strncasecmp(buf, "OK:QUEUED", 9))
	{
		if ((!strncasecmp(buf, "ERROR: UNKNOWN", 11)) && !data->missing_zwx)
		{
			applog(LOG_DEBUG, "%"PRIpreprv": Missing ZWX command, trying ZNX",
			       bitforce->proc_repr);
			data->missing_zwx = true;
			goto retry;
		}
		applog(LOG_DEBUG, "%"PRIpreprv": Unexpected error attempting to append %d jobs (queued<=%d): %s",
	           bitforce->proc_repr,
	           data->ready_to_queue, data->queued, buf);
		return false;
	}
	
	if (!data->queued)
		cgtime(&data->tv_hashmeter_start);
	
	if (data->missing_zwx)
		queued_ok = 1;
	else
	{
		char *p;
		queued_ok = strtol(&buf[9], &p, 0);
		if (data->max_queueid)
		{
			if (unlikely(p[0] != ':'))
				applog(LOG_ERR, "%"PRIpreprv": Successfully queued %d/%d jobs, but no queue ids returned (queued<=%d)", bitforce->proc_repr, queued_ok, data->ready_to_queue, data->queued + queued_ok);
			else
			{
				// NOTE: work is set to just-before the first item from the build-command loop earlier
				// NOTE: This ugly statement ends up with the first work item queued
				work = work ? (work->next ?: work) : thr->work_list;
				for (int i = data->ready_to_queue; i > 0; --i, (work = work->next))
				{
					work->device_id = strtol(&p[1], &p, 0x10);
					if (unlikely(!p[0]))
						--p;
				}
			}
		}
	}
	data->queued += queued_ok;
	applog(LOG_DEBUG, "%"PRIpreprv": Successfully queued %d/%d jobs on device (queued<=%d)",
	       bitforce->proc_repr,
	       queued_ok, data->ready_to_queue, data->queued);
	data->ready_to_queue -= queued_ok;
	if (!data->missing_zwx)
		thr->queue_full = data->ready_to_queue;
	data->just_flushed = false;
	data->want_to_send_queue = false;
	
	return true;
}

void work_list_del(struct work **head, struct work *work)
{
	DL_DELETE(*head, work);
	free_work(work);
}

static
bool bitforce_queue_do_results(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct cgpu_info * const dev = bitforce->device;
	struct bitforce_data * const devdata = dev->device_data;
	struct bitforce_data *data = bitforce->device_data;
	int count;
	int fcount;
	char *noncebuf, *buf, *end;
	unsigned char midstate[32], datatail[12];
	struct work *work, *tmpwork, *thiswork;
	struct timeval tv_now, tv_elapsed;
	long chipno = 0;  // Initialized value is used for non-parallelized boards
	struct cgpu_info *chip_cgpu;
	struct thr_info *chip_thr;
	int counts[data->parallel];
	bool ret = true;
	
	if (unlikely(!devdata->is_open))
		return false;
	
	fcount = 0;
	for (int i = 0; i < data->parallel; ++i)
		counts[i] = 0;
	
again:
	noncebuf = &data->noncebuf[0];
	count = bitforce_zox(thr, "ZOX", NULL);
	
	if (unlikely(count < 0))
	{
		inc_hw_errors_only(thr);
		return_via_applog(out, ret = false, LOG_ERR, "%"PRIpreprv": Received unexpected queue result response: %s", bitforce->proc_repr, noncebuf);
	}
	
	applog(LOG_DEBUG, "%"PRIpreprv": Received %d queue results on poll (max=%d)", bitforce->proc_repr, count, (int)BITFORCE_MAX_QRESULTS);
	if (!count)
		goto done;
	
	noncebuf = next_line(noncebuf);
	while ((buf = noncebuf)[0])
	{
		if ( (noncebuf = next_line(buf)) )
			noncebuf[-1] = '\0';
		
		if (data->max_queueid)
		{
			const work_device_id_t queueid = strtol(buf, &end, 0x10);
			if (unlikely(!end[0]))
				goto gibberish;
			DL_SEARCH_SCALAR(thr->work_list, thiswork, device_id, queueid);
		}
		else
		{
			if (strlen(buf) <= 90)
			{
gibberish:
				applog(LOG_ERR, "%"PRIpreprv": Gibberish within queue results: %s", bitforce->proc_repr, buf);
				continue;
			}
			
			hex2bin(midstate, buf, 32);
			hex2bin(datatail, &buf[65], 12);
			
			thiswork = NULL;
			DL_FOREACH(thr->work_list, work)
			{
				if (unlikely(memcmp(work->midstate, midstate, 32)))
					continue;
				if (unlikely(memcmp(&work->data[64], datatail, 12)))
					continue;
				thiswork = work;
				break;
			}
			
			end = &buf[89];
		}
		
		chip_cgpu = bitforce;
		if (data->parallel_protocol)
		{
			chipno = strtol(&end[1], &end, 16);
			if (chipno >= data->parallel)
			{
				applog(LOG_ERR, "%"PRIpreprv": Chip number out of range for queue result: %s", chip_cgpu->proc_repr, buf);
				chipno = 0;
			}
			for (int i = 0; i < chipno; ++i)
				chip_cgpu = chip_cgpu->next_proc;
		}
		chip_thr = chip_cgpu->thr[0];
		
		applog(LOG_DEBUG, "%"PRIpreprv": Queue result: %s", chip_cgpu->proc_repr, buf);
		
		if (unlikely(!thiswork))
		{
			applog(LOG_ERR, "%"PRIpreprv": Failed to find work for queue results: %s", chip_cgpu->proc_repr, buf);
			inc_hw_errors_only(chip_thr);
			goto next_qline;
		}
		
		if (unlikely(!end[0]))
		{
			applog(LOG_ERR, "%"PRIpreprv": Missing nonce count in queue results: %s", chip_cgpu->proc_repr, buf);
			goto finishresult;
		}
		if (strtol(&end[1], &end, 10))
		{
			if (unlikely(!end[0]))
			{
				applog(LOG_ERR, "%"PRIpreprv": Missing nonces in queue results: %s", chip_cgpu->proc_repr, buf);
				goto finishresult;
			}
			bitforce_process_result_nonces(chip_thr, thiswork, &end[1]);
		}
		++fcount;
		++counts[chipno];
		
finishresult:
		if (data->parallel == 1)
		{

		// Queue results are in order, so anything queued prior this is lost
		// Delete all queued work up to, and including, this one
		DL_FOREACH_SAFE(thr->work_list, work, tmpwork)
		{
			work_list_del(&thr->work_list, work);
			--data->queued;
			if (work == thiswork)
				break;
		}

		}
		else
		{
			// Parallel processors means the results might not be in order
			// This could leak if jobs get lost, hence the sanity checks using "ZqX"
			work_list_del(&thr->work_list, thiswork);
			--data->queued;
		}
next_qline: (void)0;
	}
	
	bitforce_set_queue_full(thr);
	
	if (count >= BITFORCE_MAX_QRESULTS)
		goto again;
	
done:
	if (data->parallel == 1 && (
	        (fcount < BITFORCE_GOAL_QRESULTS && bitforce->sleep_ms < BITFORCE_MAX_QRESULT_WAIT && data->queued > 1)
	     || (fcount > BITFORCE_GOAL_QRESULTS && bitforce->sleep_ms > BITFORCE_MIN_QRESULT_WAIT)  ))
	{
		unsigned int old_sleep_ms = bitforce->sleep_ms;
		bitforce->sleep_ms = (uint32_t)bitforce->sleep_ms * BITFORCE_GOAL_QRESULTS / (fcount ?: 1);
		if (bitforce->sleep_ms > BITFORCE_MAX_QRESULT_WAIT)
			bitforce->sleep_ms = BITFORCE_MAX_QRESULT_WAIT;
		if (bitforce->sleep_ms < BITFORCE_MIN_QRESULT_WAIT)
			bitforce->sleep_ms = BITFORCE_MIN_QRESULT_WAIT;
		applog(LOG_DEBUG, "%"PRIpreprv": Received %d queue results after %ums; Wait time changed to: %ums (queued<=%d)",
		       bitforce->proc_repr, fcount, old_sleep_ms, bitforce->sleep_ms, data->queued);
	}
	else
		applog(LOG_DEBUG, "%"PRIpreprv": Received %d queue results after %ums; Wait time unchanged (queued<=%d)",
		       bitforce->proc_repr, fcount, bitforce->sleep_ms, data->queued);
	
out:
	cgtime(&tv_now);
	timersub(&tv_now, &data->tv_hashmeter_start, &tv_elapsed);
	chip_cgpu = bitforce;
	for (int i = 0; i < data->parallel; ++i, (chip_cgpu = chip_cgpu->next_proc))
	{
		chip_thr = chip_cgpu->thr[0];
		hashes_done(chip_thr, (uint64_t)bitforce->nonces * counts[i], &tv_elapsed, NULL);
	}
	data->tv_hashmeter_start = tv_now;
	
	return ret;
}

static
bool bitforce_queue_append(struct thr_info *thr, struct work *work)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	bool rv, ndq;
	
	bitforce_set_queue_full(thr);
	rv = !thr->queue_full;
	if (rv)
	{
		DL_APPEND(thr->work_list, work);
		++data->ready_to_queue;
		applog(LOG_DEBUG, "%"PRIpreprv": Appending to driver queue (max=%u, ready=%d, queued<=%d)",
		       bitforce->proc_repr,
		       (unsigned)data->queued_max, data->ready_to_queue, data->queued);
		bitforce_set_queue_full(thr);
	}
	else
	if (!data->ready_to_queue)
		return rv;
	
	ndq = !data->queued;
	if ((ndq)              // Device is idle
	 || (data->ready_to_queue >= data->max_queue_at_once)  // ...or 5 items ready to go
	 || (thr->queue_full)            // ...or done filling queue
	 || (data->just_flushed)         // ...or queue was just flushed (only remaining job is partly done already)
	 || (data->missing_zwx)          // ...or device can only queue one at a time
	)
	{
		if (!bitforce_send_queue(thr))
		{
			// Problem sending queue, retry again in a few seconds
			applog(LOG_ERR, "%"PRIpreprv": Failed to send queue", bitforce->proc_repr);
			inc_hw_errors_only(thr);
			data->want_to_send_queue = true;
		}
	}
	
	return rv;
}

struct _jobinfo {
	uint8_t key[32+12];
	int instances;
	int flushed_instances;
	UT_hash_handle hh;
};

static
void _bitforce_queue_flush_add_to_processing(struct _jobinfo ** const processing_p, struct _jobinfo * const this, const size_t keysz, const bool was_flushed)
{
	struct _jobinfo *item;
	HASH_FIND(hh, *processing_p, &this->key[0], keysz, item);
	if (likely(!item))
	{
		item = this;
		this->flushed_instances = this->instances = 0;
		HASH_ADD(hh, *processing_p, key, keysz, this);
	}
	else
	{
		// This should really only happen in testing/benchmarking...
		free(this);
	}
	if (was_flushed)
		++item->flushed_instances;
	else
		++item->instances;
}

static
void bitforce_delete_last_n_work(struct thr_info * const thr, int n)
{
	while (n--)
		work_list_del(&thr->work_list, thr->work_list->prev);
}

static
void bitforce_queue_flush_sanity_check(struct thr_info * const thr, struct _jobinfo ** const processing_p, const size_t keysz, const bool ignore_race)
{
	struct cgpu_info * const bitforce = thr->cgpu;
	struct bitforce_data * const data = bitforce->device_data;
	struct work *work, *tmp;
	struct _jobinfo *item, *this;
	uint8_t key[keysz];
	char hex[(keysz * 2) + 1];
	
	// Iterate over the work_list and delete anything not in the hash
	DL_FOREACH_SAFE(thr->work_list, work, tmp)
	{
		if (data->max_queueid)
		{
			memcpy(&key[0], &work->device_id, sizeof(work->device_id));
			snprintf(hex, sizeof(hex), "%04x", work->device_id);
		}
		else
		{
			memcpy(&key[ 0],  work->midstate, 32);
			memcpy(&key[32], &work->data[64], 12);
			bin2hex(hex, key, keysz);
		}
		HASH_FIND(hh, *processing_p, &key[0], keysz, item);
		if (unlikely(!item))
		{
			applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device is missing queued job! %s", bitforce->proc_repr, hex);
			work_list_del(&thr->work_list, work);
			--data->queued;
			continue;
		}
		if (item->instances)
		{
			applog(LOG_DEBUG, "%"PRIpreprv": Queue flush: %s inprogress", bitforce->proc_repr, hex);
			--item->instances;
		}
		else
		{
			--item->flushed_instances;
			work_list_del(&thr->work_list, work);
			// NOTE: data->queued is decremented later via bitforce_finish_flush
			applog(LOG_DEBUG, "%"PRIpreprv": Queue flush: %s flushed", bitforce->proc_repr, hex);
		}
		if (likely(!(item->instances + item->flushed_instances)))
		{
			HASH_DEL(*processing_p, item);
			free(item);
		}
	}
	if (unlikely(*processing_p))
	{
		HASH_ITER(hh, *processing_p, item, this)
		{
			bin2hex(hex, &item->key[0], keysz);
			if (item->instances && !ignore_race)
				applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device %s unknown work %s (%d)", bitforce->proc_repr, "is processing", hex, item->instances);
			if (item->flushed_instances)
				applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device %s unknown work %s (%d)", bitforce->proc_repr, "flushed", hex, item->flushed_instances);
			
			HASH_DEL(*processing_p, item);
			free(item);
		}
	}
}

static
void bitforce_finish_flush(struct thr_info * const thr, const int flushed)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data * const data = bitforce->device_data;
	
	data->queued -= flushed;
	
	applog(LOG_DEBUG, "%"PRIpreprv": Flushed %u jobs from device and %d from driver (queued<=%d)",
	       bitforce->proc_repr, flushed, data->ready_to_queue, data->queued);
	
	bitforce_set_queue_full(thr);
	data->just_flushed = true;
	data->want_to_send_queue = false;
	data->ready_to_queue = 0;
}

static
void bitforce_process_flb_result(struct thr_info * const thr, int inproc, int flushed)
{
	struct cgpu_info *bitforce = thr->cgpu;
	
	size_t total = inproc + flushed, readsz;
	uint16_t buf[total];
	readsz = bitforce_read(bitforce, buf, total * 2) / 2;
	if (unlikely(readsz != total))
	{
		applog(LOG_ERR, "%"PRIpreprv": Short read for FLB result", bitforce->proc_repr);
		if (readsz < inproc)
		{
			inproc = readsz;
			flushed = 0;
		}
		else
			flushed = readsz - inproc;
	}
	
	const int keysz = sizeof(work_device_id_t);
	struct _jobinfo *processing = NULL, *this;
	for (int i = inproc + flushed; i--; )
	{
		this = malloc(sizeof(*this));
		const work_device_id_t queueid = be16toh(buf[i]);
		memcpy(&this->key[0], &queueid, sizeof(queueid));
		_bitforce_queue_flush_add_to_processing(&processing, this, keysz, !(i < inproc));
	}
	
	bitforce_queue_flush_sanity_check(thr, &processing, keysz, false);
	
	bitforce_finish_flush(thr, flushed);
}

static
void bitforce_queue_flush(struct thr_info *thr)
{
	struct bitforce_proc_data *procdata = thr->cgpu_data;
	if (!procdata->handles_board)
		return;
	
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	char *buf = &data->noncebuf[0], *buf2 = NULL;
	const char *cmd = "ZqX";
	int inproc = -1;
	unsigned flushed;
	struct _jobinfo *processing = NULL, *this;
	
	// First, eliminate all unsent works
	bitforce_delete_last_n_work(thr, data->ready_to_queue);
	
	if (data->parallel == 1)
		// Pre-parallelization neither needs nor supports "ZqX"
		cmd = "ZQX";
	else
	if (data->max_queueid)
		cmd = "FLB";
	bitforce_zox(thr, cmd, NULL);
	if (!strncasecmp(buf, "OK:FLUSHED", 10))
		flushed = atoi(&buf[10]);
	else
	if ((!strncasecmp(buf, "COUNT:", 6)) && (buf2 = strstr(buf, "FLUSHED:")) )
	{
		inproc = atoi(&buf[6]);
		flushed = atoi(&buf2[8]);
		buf2 = next_line(buf2);
	}
	else
	if ((!strncasecmp(buf, "BIN-InP:", 8)) && (buf2 = strstr(buf, "FLUSHED:")) )
	{
		inproc = atoi(&buf[8]);
		flushed = atoi(&buf2[8]);
		if (unlikely(data->queued != inproc + flushed))
			applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device work count mismatch (dev inproc=%d, dev flushed=%u, queued=%d)", bitforce->proc_repr, inproc, flushed, data->queued);
		bitforce_process_flb_result(thr, inproc, flushed);
		goto final;
	}
	else
	if (!strncasecmp(buf, "OK", 2))
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Didn't report flush count", bitforce->proc_repr);
		thr->queue_full = false;
		flushed = 0;
	}
	else
	{
		applog(LOG_DEBUG, "%"PRIpreprv": Failed to flush device queue: %s", bitforce->proc_repr, buf);
		flushed = 0;
	}
	
	if (flushed > data->queued)
	{
		applog(LOG_WARNING, "%"PRIpreprv": Flushed %u jobs from device, but only %u were queued",
		       bitforce->proc_repr, flushed, data->queued);
		inc_hw_errors_only(thr);
		// We need to avoid trying to delete more items than we've sent, or a segfault is upcoming...
		flushed = data->queued;
	}
	
	bitforce_delete_last_n_work(thr, flushed);
	bitforce_finish_flush(thr, flushed);
	
	// "ZqX" returns jobs in progress, allowing us to sanity check
	// NOTE: Must process buffer into hash table BEFORE calling bitforce_queue_do_results, which clobbers it
	// NOTE: Must do actual sanity check AFTER calling bitforce_queue_do_results, to ensure we don't delete completed jobs
	
	const size_t keysz = data->max_queueid ? sizeof(work_device_id_t) : sizeof(this->key);
	
	if (buf2)
	{
		// First, turn buf2 into a hash
		for ( ; buf2[0]; buf2 = next_line(buf2))
		{
			this = malloc(sizeof(*this));
			if (data->max_queueid)
			{
				const work_device_id_t queueid = strtol(buf2, NULL, 0x10);
				memcpy(&this->key[0], &queueid, sizeof(queueid));
			}
			else
			{
				hex2bin(&this->key[ 0], &buf2[ 0], 32);
				hex2bin(&this->key[32], &buf2[65], 12);
			}
			_bitforce_queue_flush_add_to_processing(&processing, this, keysz, false);
		}
	}
	
	bitforce_queue_do_results(thr);
	
	if (buf2)
		// There is a race condition where the flush may have reported a job as in progress even though we completed and processed its results just now - so we just silence the sanity check
		bitforce_queue_flush_sanity_check(thr, &processing, keysz, true);
	
final: ;
	if (data->style == BFS_28NM)
	{
		if (unlikely(inproc != -1 && inproc != data->queued))
		{
			applog(LOG_WARNING, "%"PRIpreprv": Sanity check: Device work inprogress count mismatch (dev inproc=%d, queued=%d)", bitforce->proc_repr, inproc, data->queued);
			data->queued = inproc;
		}
	}
}

static
void bitforce_queue_poll(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu;
	struct bitforce_data *data = bitforce->device_data;
	unsigned long sleep_us;
	
	if (data->queued)
		bitforce_queue_do_results(thr);
	sleep_us = (unsigned long)bitforce->sleep_ms * 1000;
	
	if (data->want_to_send_queue)
		if (!bitforce_send_queue(thr))
			if (!data->queued)
			{
				applog(LOG_ERR, "%"PRIpreprv": Failed to send queue, and queue empty; retrying after 1 second", bitforce->proc_repr);
				inc_hw_errors_only(thr);
				sleep_us = 1000000;
			}
	
	timer_set_delay_from_now(&thr->tv_poll, sleep_us);
}

static void bitforce_queue_thread_deven(struct thr_info *thr)
{
	struct cgpu_info *bitforce = thr->cgpu, *thisbf;
	struct bitforce_data *data = bitforce->device_data;
	struct thr_info *thisthr;
	
	for (thisbf = bitforce->device; thisbf && thisbf->device_data != data; thisbf = thisbf->next_proc)
	{}
	for ( ; thisbf && thisbf->device_data == data; thisbf = thisbf->next_proc)
	{
		thisthr = bitforce->thr[0];
		
		thisthr->pause = thr->pause;
		thisbf->deven = bitforce->deven;
	}
}

static void bitforce_queue_thread_disable(struct thr_info *thr)
{
	// Disable other threads sharing the same queue
	bitforce_queue_thread_deven(thr);
}

static void bitforce_queue_thread_enable(struct thr_info *thr)
{
	// TODO: Maybe reinit?
	
	// Enable other threads sharing the same queue
	bitforce_queue_thread_deven(thr);
}

struct device_drv bitforce_queue_api = {
	.dname = "bitforce_queue",
	.name = "BFL",
	.lowl_probe_by_name_only = true,
	.lowl_match = bitforce_lowl_match,
	.lowl_probe = bitforce_lowl_probe,
	.minerloop = minerloop_queue,
	.reinit_device = bitforce_reinit,
	.zero_stats = bitforce_zero_stats,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = bitforce_wlogprint_status,
	.proc_tui_wlogprint_choices = bitforce_tui_wlogprint_choices,
	.proc_tui_handle_choice = bitforce_tui_handle_choice,
#endif
	.get_api_stats = bitforce_drv_stats,
	.get_stats = bitforce_get_stats,
	.identify_device = bitforce_identify,
	.thread_init = bitforce_thread_init,
	.queue_append = bitforce_queue_append,
	.queue_flush = bitforce_queue_flush,
	.poll = bitforce_queue_poll,
	.thread_shutdown = bitforce_shutdown,
	.thread_disable = bitforce_queue_thread_disable,
	.thread_enable = bitforce_queue_thread_enable,
};
