/*
 * Copyright 2013 Andrew Smith
 * Copyright 2013 Con Kolivas
 * Copyright 2013 Chris Savery
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

#include "config.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include "deviceapi.h"
#include "lowlevel.h"
#include "miner.h"

#define K1 "K1"
#define K16 "K16"
#define K64 "K64"

#define MIDSTATE_BYTES 32
#define MERKLE_OFFSET 64
#define MERKLE_BYTES 12

#define REPLY_SIZE		15	// adequate for all types of replies
#define REPLY_BUFSIZE 		16	// reply + 1 byte to mark used
#define MAX_REPLY_COUNT		32	// more unhandled replies than this will result in data loss
#define REPLY_WAIT_TIME		100 	// poll interval for a cmd waiting it's reply
#define CMD_REPLY_RETRIES	8	// how many retries for cmds
#define MAX_WORK_COUNT		4	// for now, must be binary multiple and match firmware
#define TACH_FACTOR		87890	// fan rpm divisor

BFG_REGISTER_DRIVER(klondike_drv)

typedef struct klondike_id {
	uint8_t version;
	uint8_t product[7];
	uint32_t serial;
} IDENTITY;

typedef struct klondike_status {
	uint8_t state;
	uint8_t chipcount;
	uint8_t slavecount;
	uint8_t workqc;
	uint8_t workid;
	uint8_t temp;
	uint8_t fanspeed;
	uint8_t errorcount;
	uint16_t hashcount;
	uint16_t maxcount;
} WORKSTATUS;

typedef struct _worktask {
	uint16_t pad1;
	uint8_t pad2;
	uint8_t workid;
	uint32_t midstate[8];
	uint32_t merkle[3];
} WORKTASK;

typedef struct _workresult {
	uint16_t pad;
	uint8_t device;
	uint8_t workid;
	uint32_t nonce;
} WORKRESULT;

typedef struct kondike_cfg {
	uint16_t hashclock;
	uint8_t temptarget;
	uint8_t tempcritical;
	uint8_t fantarget;
	uint8_t pad;
} WORKCFG;

typedef struct device_info {
	uint32_t noncecount;
	uint32_t nextworkid;
	uint16_t lasthashcount;
	uint64_t totalhashcount;
	uint32_t rangesize;
	uint32_t *chipstats;
} DEVINFO;

struct klondike_info {
	bool shutdown;
	pthread_rwlock_t stat_lock;
	struct thr_info replies_thr;
	WORKSTATUS *status; 
	DEVINFO *devinfo;
	WORKCFG *cfg;
	char *replies;
	int nextreply;
	int noncecount;
	uint64_t hashcount;
	
	pthread_mutex_t devlock;
	struct libusb_device_handle *usbdev_handle;
	
	// TODO:
	bool usbinfo_nodev;
};

IDENTITY KlondikeID;

static
int usb_init(struct cgpu_info * const klncgpu, struct libusb_device * const dev)
{
	struct klondike_info * const klninfo = klncgpu->device_data;
	int e;
	if (libusb_open(dev, &klninfo->usbdev_handle) != LIBUSB_SUCCESS)
		return 0;
	if (LIBUSB_SUCCESS != (e = libusb_set_configuration(klninfo->usbdev_handle, 1)))
	{
		applog(LOG_DEBUG, "%s: Failed to set configuration 1: %s",
		       klondike_drv.dname, bfg_strerror(e, BST_LIBUSB));
fail:
		libusb_close(klninfo->usbdev_handle);
		return 0;
	}
	if (LIBUSB_SUCCESS != (e = libusb_claim_interface(klninfo->usbdev_handle, 0)))
	{
		applog(LOG_DEBUG, "%s: Failed to claim interface 0: %s",
		       klondike_drv.dname, bfg_strerror(e, BST_LIBUSB));
		goto fail;
	}
	return 1;
}

static
int _usb_rw(struct cgpu_info * const klncgpu, void * const buf, const size_t bufsiz, int * const processed, int ep)
{
	struct klondike_info * const klninfo = klncgpu->device_data;
	const unsigned int timeout = 999;
	unsigned char *cbuf = buf;
	int err, sent;
	
	*processed = 0;
	
	while (*processed < bufsiz)
	{
		mutex_lock(&klninfo->devlock);
		err = libusb_bulk_transfer(klninfo->usbdev_handle, ep, cbuf, bufsiz, &sent, timeout);
		mutex_unlock(&klninfo->devlock);
		if (unlikely(err))
			return err;
		*processed += sent;
	}
	
	return LIBUSB_SUCCESS;
}
#define usb_read( klncgpu, buf, bufsiz, processed) _usb_rw(klncgpu, buf, bufsiz, processed, 1 | LIBUSB_ENDPOINT_IN)
#define usb_write(klncgpu, buf, bufsiz, processed) _usb_rw(klncgpu, buf, bufsiz, processed, 1 | LIBUSB_ENDPOINT_OUT)

static
void usb_uninit(struct cgpu_info * const klncgpu)
{
	struct klondike_info * const klninfo = klncgpu->device_data;
	libusb_release_interface(klninfo->usbdev_handle, 0);
	libusb_close(klninfo->usbdev_handle);
}

static double cvtKlnToC(uint8_t temp)
{
	return (double)1/((double)1/(25+273.15) + log((double)temp*1000/(256-temp)/2200)/3987) - 273.15;
}

static int cvtCToKln(double deg)
{
	double R = exp((1/(deg+273.15)-1/(273.15+25))*3987)*2200;
	return 256*R/(R+1000);
}

static char *SendCmdGetReply(struct cgpu_info *klncgpu, char Cmd, int device, int datalen, void *data)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	char outbuf[64];
	int retries = CMD_REPLY_RETRIES;
	int chkreply = klninfo->nextreply;
	int sent, err;
	
	if (klninfo->usbinfo_nodev)
		return NULL;

	outbuf[0] = Cmd;
	outbuf[1] = device;
	memcpy(outbuf+2, data, datalen);
	err = usb_write(klncgpu, outbuf, 2+datalen, &sent);
	if (err < 0 || sent != 2+datalen) {
		applog(LOG_ERR, "%s (%s) Cmd:%c Dev:%d, write failed (%d:%d)", klncgpu->drv->dname, klncgpu->device_path, Cmd, device, sent, err);
	}
	while (retries-- > 0 && klninfo->shutdown == false) {
		cgsleep_ms(REPLY_WAIT_TIME);
		while (*(klninfo->replies + chkreply*REPLY_BUFSIZE) != Cmd || *(klninfo->replies + chkreply*REPLY_BUFSIZE + 2) != device) {
			if (++chkreply == MAX_REPLY_COUNT)
				chkreply = 0;
			if (chkreply == klninfo->nextreply)
				break;
		}
		if (chkreply == klninfo->nextreply)
			continue;
		*(klninfo->replies + chkreply*REPLY_BUFSIZE) = '!';  // mark to prevent re-use
		return klninfo->replies + chkreply*REPLY_BUFSIZE + 1;
	}
	return NULL;
}
			
static bool klondike_get_stats(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	int slaves, dev;

	if (klninfo->usbinfo_nodev || klninfo->status == NULL)
		return false;

	applog(LOG_DEBUG, "Klondike getting status");
	slaves = klninfo->status[0].slavecount;
	
	// loop thru devices and get status for each
	wr_lock(&(klninfo->stat_lock));
	for (dev = 0; dev <= slaves; dev++) {
		char *reply = SendCmdGetReply(klncgpu, 'S', dev, 0, NULL);
		if (reply != NULL)
			memcpy((void *)(&(klninfo->status[dev])), reply+2, sizeof(klninfo->status[dev]));
	}
	wr_unlock(&(klninfo->stat_lock));
	
	// todo: detect slavecount change and realloc space
		
	return true;
}

static bool klondike_init(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	int slaves, dev;
	
	char *reply = SendCmdGetReply(klncgpu, 'S', 0, 0, NULL);
	if (reply == NULL)
		return false;
		
	slaves = ((WORKSTATUS *)(reply+2))->slavecount;
	if (klninfo->status == NULL) {
		applog(LOG_DEBUG, "Klondike initializing data");
		
		// alloc space for status, devinfo and cfg for master and slaves
		klninfo->status = calloc(slaves+1, sizeof(WORKSTATUS));
		if (unlikely(!klninfo->status))
			quit(1, "Failed to calloc status array in klondke_get_stats");
		klninfo->devinfo = calloc(slaves+1, sizeof(DEVINFO));
		if (unlikely(!klninfo->devinfo))
			quit(1, "Failed to calloc devinfo array in klondke_get_stats");
		klninfo->cfg = calloc(slaves+1, sizeof(WORKCFG));
		if (unlikely(!klninfo->cfg))
			quit(1, "Failed to calloc cfg array in klondke_get_stats");
	}
		
	WORKCFG cfgset = { 0,0,0,0,0 }; // zero init triggers read back only
	double temp1, temp2;
	int size = 2;
	
	if (opt_klondike_options != NULL) {  // boundaries are checked by device, with valid values returned
		sscanf(opt_klondike_options, "%hu:%lf:%lf:%hhu", &cfgset.hashclock, &temp1, &temp2, &cfgset.fantarget);
		cfgset.temptarget = cvtCToKln(temp1);
		cfgset.tempcritical = cvtCToKln(temp2);
		cfgset.fantarget = (int)255*cfgset.fantarget/100;
		size = sizeof(cfgset); 
	}
	
	for (dev = 0; dev <= slaves; dev++) {
		char *reply = SendCmdGetReply(klncgpu, 'C', dev, size, &cfgset);
		if (reply != NULL) {
			klninfo->cfg[dev] = *(WORKCFG *)(reply+2);
			applog(LOG_NOTICE, "Klondike config (%d: Clk: %d, T:%.0lf, C:%.0lf, F:%d)", 
				dev, klninfo->cfg[dev].hashclock, 
				cvtKlnToC(klninfo->cfg[dev].temptarget), 
				cvtKlnToC(klninfo->cfg[dev].tempcritical), 
				(int)100*klninfo->cfg[dev].fantarget/256);
		}
	}
	klondike_get_stats(klncgpu);
	for (dev = 0; dev <= slaves; dev++) {
		klninfo->devinfo[dev].rangesize = ((uint64_t)1<<32) / klninfo->status[dev].chipcount;
		klninfo->devinfo[dev].chipstats = calloc(klninfo->status[dev].chipcount*2 , sizeof(uint32_t));
	}
		
	SendCmdGetReply(klncgpu, 'E', 0, 1, "1");
		
	return true;
}

static
bool klondike_foundlowl(struct lowlevel_device_info * const info, __maybe_unused void * const userp)
{
	if (unlikely(info->lowl != &lowl_usb))
	{
		applog(LOG_WARNING, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not usb!",
		       __func__, info->product, info->serial);
		return false;
	}
	struct libusb_device * const dev = info->lowl_data;
	
// static bool klondike_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
	struct cgpu_info * const klncgpu = malloc(sizeof(*klncgpu));
	struct klondike_info *klninfo = NULL;

	if (unlikely(!klncgpu))
		quit(1, "Failed to calloc klncgpu in klondike_detect_one");
	
	*klncgpu = (struct cgpu_info){
		.drv = &klondike_drv,
		.deven = DEV_ENABLED,
		.threads = 1,
	};
		
	klninfo = calloc(1, sizeof(*klninfo));
	if (unlikely(!klninfo))
		quit(1, "Failed to calloc klninfo in klondke_detect_one");
	klncgpu->device_data = (FILE *)klninfo;
	mutex_init(&klninfo->devlock);
	
	klninfo->replies = calloc(MAX_REPLY_COUNT, REPLY_BUFSIZE);
	if (unlikely(!klninfo->replies))
		quit(1, "Failed to calloc replies buffer in klondke_detect_one");
	klninfo->nextreply = 0;
	
	if (usb_init(klncgpu, dev)) {
		int attempts = 0;		
		while (attempts++ < 3) {
			char reply[REPLY_SIZE];
			const char * const devpath = info->devid;
			int sent, recd, err;
			
			err = usb_write(klncgpu, "I", 2, &sent);
			if (err < 0 || sent != 2) {
				applog(LOG_ERR, "%s (%s) detect write failed (%d:%d)", klncgpu->drv->dname, devpath, sent, err);
			}
			cgsleep_ms(REPLY_WAIT_TIME*10);
			err = usb_read(klncgpu, reply, REPLY_SIZE, &recd);
			if (err < 0) {
				applog(LOG_ERR, "%s (%s) detect read failed (%d:%d)", klncgpu->drv->dname, devpath, recd, err);
			} else if (recd < 1) {
				applog(LOG_ERR, "%s (%s) detect empty reply (%d)",	klncgpu->drv->dname, devpath, recd);
			} else if (reply[0] == 'I' && reply[1] == 0) {

				applog(LOG_DEBUG, "%s (%s) detect successful", klncgpu->drv->dname, devpath);
				KlondikeID = *(IDENTITY *)(&reply[2]);
				klncgpu->device_path = strdup(devpath);
				if (!add_cgpu(klncgpu))
					break;
				applog(LOG_DEBUG, "Klondike cgpu added");
				return true;
			}
		}
		usb_uninit(klncgpu);
	}
	free(klninfo->replies);
	free(klncgpu);
	return false;
}

static
bool klondike_detect_one(const char *serial)
{
	return lowlevel_detect_serial(klondike_foundlowl, serial);
}

static
int klondike_autodetect()
{
	return lowlevel_detect(klondike_foundlowl, "K16");
}

static
void klondike_detect()
{
	generic_detect(&klondike_drv, klondike_detect_one, klondike_autodetect, 0);
}

static
bool klondike_identify(__maybe_unused struct cgpu_info * const klncgpu)
{
	//SendCmdGetReply(klncgpu, 'I', 0, 0, NULL);
	return false;
}

static void klondike_check_nonce(struct cgpu_info *klncgpu, WORKRESULT *result)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct work *work, *tmp;
	
	applog(LOG_DEBUG, "Klondike FOUND NONCE (%02x:%08x)", result->workid, result->nonce);

	HASH_ITER(hh, klncgpu->queued_work, work, tmp) {
		if (work->queued && (work->subid == (result->device*256 + result->workid))) {
			
			wr_lock(&(klninfo->stat_lock));
			klninfo->devinfo[result->device].noncecount++;
			klninfo->noncecount++;
			wr_unlock(&(klninfo->stat_lock));
			
			result->nonce = le32toh(result->nonce - 0xC0);
			applog(LOG_DEBUG, "Klondike SUBMIT NONCE (%02x:%08x)", result->workid, result->nonce);
			bool ok = submit_nonce(klncgpu->thr[0], work, result->nonce);
			
			applog(LOG_DEBUG, "Klondike chip stats %d, %08x, %d, %d", result->device, result->nonce, klninfo->devinfo[result->device].rangesize, klninfo->status[result->device].chipcount);
			klninfo->devinfo[result->device].chipstats[(result->nonce / klninfo->devinfo[result->device].rangesize) + (ok ? 0 : klninfo->status[result->device].chipcount)]++;
			return;
		}
	}
	
	applog(LOG_ERR, "%s%i:%d unknown work (%02x:%08x) - ignored",	
		klncgpu->drv->name, klncgpu->device_id, result->device, result->workid, result->nonce);
	//inc_hw_errors(klncgpu->thr[0]);
}

// thread to keep looking for replies
static void *klondike_get_replies(void *userdata)
{
	struct cgpu_info *klncgpu = (struct cgpu_info *)userdata;
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	char *replybuf;
	int err, recd;

	applog(LOG_DEBUG, "Klondike listening for replies");	
	
	while (klninfo->shutdown == false) {
		if (klninfo->usbinfo_nodev)
			return NULL;
		
		replybuf = klninfo->replies + klninfo->nextreply * REPLY_BUFSIZE;
		replybuf[0] = 0;
		
		err = usb_read(klncgpu, replybuf+1, REPLY_SIZE, &recd);
		if (!err && recd == REPLY_SIZE) {
			if (opt_log_level <= LOG_DEBUG) {
				char hexdata[(recd * 2) + 1];
				bin2hex(hexdata, &replybuf[1], recd);
				applog(LOG_DEBUG, "%s (%s) reply [%s:%s]", klncgpu->drv->dname, klncgpu->device_path, replybuf+1, hexdata);
			}
			if (++klninfo->nextreply == MAX_REPLY_COUNT)
				klninfo->nextreply = 0;
				
			replybuf[0] = replybuf[1];
			if (replybuf[0] == '=')
				klondike_check_nonce(klncgpu, (WORKRESULT *)replybuf);
		}
	}
	return NULL;
}

static void klondike_flush_work(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	int dev;
			
	applog(LOG_DEBUG, "Klondike flushing work");
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) {
		char *reply = SendCmdGetReply(klncgpu, 'A', dev, 0, NULL);
		if (reply != NULL) {
			wr_lock(&(klninfo->stat_lock));
			klninfo->status[dev] = *(WORKSTATUS *)(reply+2);
			wr_unlock(&(klninfo->stat_lock));
		}
	}
}

static bool klondike_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	
	if (thr_info_create(&(klninfo->replies_thr), NULL, klondike_get_replies, (void *)klncgpu)) {
		applog(LOG_ERR, "%s%i: thread create failed", klncgpu->drv->name, klncgpu->device_id);
		return false;
	}
	pthread_detach(klninfo->replies_thr.pth);
	
	// let the listening get started
	cgsleep_ms(100);
	
	return klondike_init(klncgpu);
}

static bool klondike_thread_init(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info * const klninfo = klncgpu->device_data;
	
	notifier_init(thr->work_restart_notifier);

	if (klninfo->usbinfo_nodev)
		return false;

	klondike_flush_work(klncgpu);
	
	return true;
}

static void klondike_shutdown(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	int dev;
	
	applog(LOG_DEBUG, "Klondike shutting down work");
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) {
		SendCmdGetReply(klncgpu, 'E', dev, 1, "0");
	}
	klncgpu->shutdown = klninfo->shutdown = true;
}

static void klondike_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info * const klninfo = klncgpu->device_data;

	if (klninfo->usbinfo_nodev)
		return;
		
	//SendCmdGetReply(klncgpu, 'E', 0, 1, "0");

}

static bool klondike_send_work(struct cgpu_info *klncgpu, int dev, struct work *work)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct work *tmp;
	WORKTASK data;
	
	if (klninfo->usbinfo_nodev)
		return false;
			
	memcpy(data.midstate, work->midstate, MIDSTATE_BYTES);
	memcpy(data.merkle, work->data + MERKLE_OFFSET, MERKLE_BYTES);
	data.workid = (uint8_t)(klninfo->devinfo[dev].nextworkid++ & 0xFF);
	work->subid = dev*256 + data.workid;
	
	if (opt_log_level <= LOG_DEBUG) {
		const size_t sz = sizeof(data) - 3;
		char hexdata[(sz * 2) + 1];
		bin2hex(hexdata, &data.workid, sz);
		applog(LOG_DEBUG, "WORKDATA: %s", hexdata);
	}
	
	applog(LOG_DEBUG, "Klondike sending work (%d:%02x)", dev, data.workid);
	char *reply = SendCmdGetReply(klncgpu, 'W', dev, sizeof(data)-3, &data.workid);
	if (reply != NULL) {
		wr_lock(&(klninfo->stat_lock));
		klninfo->status[dev] = *(WORKSTATUS *)(reply+2);
		wr_unlock(&(klninfo->stat_lock));
		
		// remove old work 
		HASH_ITER(hh, klncgpu->queued_work, work, tmp) {
		if (work->queued && (work->subid == (int)(dev*256 + ((klninfo->devinfo[dev].nextworkid-2*MAX_WORK_COUNT) & 0xFF))))
			work_completed(klncgpu, work);
		}
		return true;
	}
	return false;
}

static bool klondike_queue_full(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct work *work = NULL;
	int dev, queued;
	
	for (queued = 0; queued < MAX_WORK_COUNT-1; queued++)
		for (dev = 0; dev <= klninfo->status->slavecount; dev++)
			if (klninfo->status[dev].workqc <= queued) {
				if (!work)
					work = get_queued(klncgpu);
				if (unlikely(!work))
					return false;
				if (klondike_send_work(klncgpu, dev, work)) {
					work = NULL;
					break;
				}
			}
			
	return true;
}

static int64_t klondike_scanwork(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	int64_t newhashcount = 0;
	int dev;
	
	if (klninfo->usbinfo_nodev)
		return -1;
		
	restart_wait(thr, 200);
	if (klninfo->status != NULL) {
		rd_lock(&(klninfo->stat_lock));
		for (dev = 0; dev <= klninfo->status->slavecount; dev++) {
			uint64_t newhashdev = 0;
			if (klninfo->devinfo[dev].lasthashcount > klninfo->status[dev].hashcount) // todo: chg this to check workid for wrapped instead
				newhashdev += klninfo->status[dev].maxcount; // hash counter wrapped
			newhashdev += klninfo->status[dev].hashcount - klninfo->devinfo[dev].lasthashcount;
			klninfo->devinfo[dev].lasthashcount = klninfo->status[dev].hashcount;
			klninfo->hashcount += (newhashdev << 32) / klninfo->status[dev].maxcount;
			newhashcount += 0xffffffffull * (uint64_t)klninfo->noncecount;
			
			// todo: check stats for critical conditions
		}
		rd_unlock(&(klninfo->stat_lock));
	}
	return newhashcount;
}


static void get_klondike_statline_before(char *buf, size_t siz, struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	uint8_t temp = 0xFF;
	uint16_t fan = 0;
	int dev;
	
	if (klninfo->status == NULL)
		return;

	rd_lock(&(klninfo->stat_lock));
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) { 
		if (klninfo->status[dev].temp < temp)
			temp = klninfo->status[dev].temp;
		fan += klninfo->cfg[dev].fantarget;
	}
	fan /= klninfo->status->slavecount+1;
	rd_unlock(&(klninfo->stat_lock));

	tailsprintf(buf, siz, "     %3.0fC %3d%% | ", cvtKlnToC(temp), fan*100/255);
}

static struct api_data *klondike_api_stats(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct api_data *root = NULL;
	char buf[32];
	int dev;
	
	if (klninfo->status == NULL)
		return NULL;
	
	rd_lock(&(klninfo->stat_lock));
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) { 

		float fTemp = cvtKlnToC(klninfo->status[dev].temp);
		sprintf(buf, "Temp %d", dev);
		root = api_add_temp(root, buf, &fTemp, true);
	
		double dClk = (double)klninfo->cfg[dev].hashclock;
		sprintf(buf, "Clock %d", dev);
		root = api_add_freq(root, buf, &dClk, true);
		
		unsigned int iFan = (unsigned int)100 * klninfo->cfg[dev].fantarget / 255;
		sprintf(buf, "Fan Percent %d", dev);
		root = api_add_int(root, buf, (int *)(&iFan), true);

		iFan = 0;
		if (klninfo->status[dev].fanspeed > 0)
			iFan = (unsigned int)TACH_FACTOR / klninfo->status[dev].fanspeed;
		sprintf(buf, "Fan RPM %d", dev);
		root = api_add_int(root, buf, (int *)(&iFan), true);
		
		if (klninfo->devinfo[dev].chipstats != NULL) {
			char data[128];
			int n;
			sprintf(buf, "Nonces / Chip %d", dev);
			for (n = 0; n < klninfo->status[dev].chipcount; n++)
				sprintf(data+n*8, "%07d ", klninfo->devinfo[dev].chipstats[n]);
			data[127] = 0;
			root = api_add_string(root, buf, data, true);
		
			sprintf(buf, "Errors / Chip %d", dev);
			for (n = 0; n < klninfo->status[dev].chipcount; n++)
				sprintf(data+n*8, "%07d ", klninfo->devinfo[dev].chipstats[n + klninfo->status[dev].chipcount]);
			data[127] = 0;
			root = api_add_string(root, buf, data, true);
		}
	}

	root = api_add_uint64(root, "Hash Count", &(klninfo->hashcount), true);

	rd_unlock(&(klninfo->stat_lock));
	
	return root;
}

struct device_drv klondike_drv = {
	.dname = "Klondike",
	.name = "KLN",
	.drv_detect = klondike_detect,
	.get_api_stats = klondike_api_stats,
// 	.get_statline_before = get_klondike_statline_before,
	.get_stats = klondike_get_stats,
	.identify_device = klondike_identify,
	.thread_prepare = klondike_thread_prepare,
	.thread_init = klondike_thread_init,
	.minerloop = hash_queued_work,
	.scanwork = klondike_scanwork,
	.queue_full = klondike_queue_full,
	.flush_work = klondike_flush_work,
	.thread_shutdown = klondike_shutdown,
	.thread_enable = klondike_thread_enable
};
