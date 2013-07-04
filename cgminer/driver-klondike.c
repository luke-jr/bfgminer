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
#include "miner.h"
#include "usbutils.h"

#define KLN "KLN"
#define K1 "K1"
#define K16 "K16"
#define K64 "K64"

#define MIDSTATE_BYTES 32
#define MERKLE_OFFSET 64
#define MERKLE_BYTES 12

#define REPLY_SIZE			15	// adequate for all types of replies
#define REPLY_BUFSIZE 		16	// reply + 1 byte to mark used
#define MAX_REPLY_COUNT		32	// more unhandled replies than this will result in data loss
#define REPLY_WAIT_TIME		100 // poll interval for a cmd waiting it's reply
#define MAX_WORK_COUNT      4   // for now, must be binary multiple and match firmware

struct device_drv klondike_drv;

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
};

IDENTITY KlondikeID;

static char *SendCmdGetReply(struct cgpu_info *klncgpu, char Cmd, int device, int datalen, void *data)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	char outbuf[64];
	int retries = 10;
	int chkreply = klninfo->nextreply;
	int sent, err;
	
	if (klncgpu->usbinfo.nodev)
		return NULL;

	outbuf[0] = Cmd;
	outbuf[1] = device;
	memcpy(outbuf+2, data, datalen);
	err = usb_write(klncgpu, outbuf, 2+datalen, &sent, C_REQUESTRESULTS);
	if (err < 0 || sent != 2+datalen) {
		applog(LOG_ERR, "%s (%s) Cmd:%c Dev:%d, write failed (%d:%d)", klncgpu->drv->dname, klncgpu->device_path, Cmd, device, sent, err);
	}
	while(retries-- > 0) {
		nmsleep(REPLY_WAIT_TIME);
		while(*(klninfo->replies + chkreply*REPLY_BUFSIZE) != Cmd || *(klninfo->replies + chkreply*REPLY_BUFSIZE + 2) != device) {
			if(++chkreply == MAX_REPLY_COUNT)
				chkreply = 0;
			if(chkreply == klninfo->nextreply)
				break;
			}
		if(chkreply == klninfo->nextreply)
			continue;
		*(klninfo->replies + chkreply*REPLY_BUFSIZE) = '!';  // mark to prevent re-use
		return klninfo->replies + chkreply*REPLY_BUFSIZE + 1;
	}
	return NULL;
}

static bool klondike_get_stats(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	char replybuf[REPLY_BUFSIZE];
	char devpath[20];
	bool allok = false;
	int sent, recd, err;
	int slaves, dev;

	if (klncgpu->usbinfo.nodev)
		return false;
	
	char *reply = SendCmdGetReply(klncgpu, 'S', 0, 0, NULL);
	if(reply != NULL) {
		
		// todo: detect slavecount change and realloc space
		slaves = ((WORKSTATUS *)(reply+2))->slavecount;
		if(klninfo->status == NULL) {
			applog(LOG_DEBUG, "Klondike initializing status data");
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
				
			// where does saved user cfg info come from?
			// todo: set user cfg to devices
			}
		applog(LOG_DEBUG, "Klondike updating status");
		
		// device 0 is master and must exist
		wr_lock(&(klninfo->stat_lock));
		klninfo->status[0] = *(WORKSTATUS *)(reply+2);
	
		// loop thru slaves and get status for each
		for(dev = 1; dev <= slaves; dev++) {
			char *reply = SendCmdGetReply(klncgpu, 'S', dev, 0, NULL);
			if(reply != NULL)
				klninfo->status[dev] = *(WORKSTATUS *)(reply+2);
			}
		wr_unlock(&(klninfo->stat_lock));
		allok = true;
		}
	return allok;
}

static bool klondike_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *klncgpu = usb_alloc_cgpu(&klondike_drv, 1);
	struct klondike_info *klninfo = NULL;

	if (unlikely(!klncgpu))
		quit(1, "Failed to calloc klncgpu in klondike_detect_one");
		
	klninfo = calloc(1, sizeof(*klninfo));
	if (unlikely(!klninfo))
		quit(1, "Failed to calloc klninfo in klondke_detect_one");
	// TODO: fix ... everywhere ...
	klncgpu->device_data = (FILE *)klninfo;
	
	klninfo->replies = calloc(MAX_REPLY_COUNT, REPLY_BUFSIZE);
	if (unlikely(!klninfo->replies))
		quit(1, "Failed to calloc replies buffer in klondke_detect_one");
	klninfo->nextreply = 0;
	
	if (usb_init(klncgpu, dev, found)) {
		int attempts = 0;		
		while(attempts++ < 3) {
			char devpath[20], reply[REPLY_SIZE];
			int sent, recd, err;
			
			sprintf(devpath, "%d:%d", (int)(klncgpu->usbinfo.bus_number), (int)(klncgpu->usbinfo.device_address));
			err = usb_write(klncgpu, "I", 2, &sent, C_REQUESTRESULTS);
			if (err < 0 || sent != 2) {
				applog(LOG_ERR, "%s (%s) detect write failed (%d:%d)", klncgpu->drv->dname, devpath, sent, err);
			}
			nmsleep(REPLY_WAIT_TIME*10);
			err = usb_read(klncgpu, reply, REPLY_SIZE, &recd, C_GETRESULTS);
			if (err < 0) {
				applog(LOG_ERR, "%s (%s) detect read failed (%d:%d)", klncgpu->drv->dname, devpath, recd, err);
			} else if (recd < 1) {
				applog(LOG_ERR, "%s (%s) detect empty reply (%d)",	klncgpu->drv->dname, devpath, recd);
			} else if(reply[0] == 'I' && reply[1] == 0) {

				applog(LOG_DEBUG, "%s (%s) detect successful", klncgpu->drv->dname, devpath);
				KlondikeID = *(IDENTITY *)(&reply[2]);
				klncgpu->device_path = strdup(devpath);
				update_usb_stats(klncgpu);
				if(!add_cgpu(klncgpu))
					break;
				applog(LOG_DEBUG, "Klondike cgpu added");
				return true;
			}
		}
		usb_uninit(klncgpu);
	}
	free(klncgpu);
	return false;
}

static void klondike_detect(void)
{
	usb_detect(&klondike_drv, klondike_detect_one);
}

static void klondike_identify(struct cgpu_info *klncgpu)
{
	//SendCmdGetReply(klncgpu, 'I', 0, 0, NULL);
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
			wr_unlock(&(klninfo->stat_lock));
			
			result->nonce = le32toh(result->nonce - 0xC0);
			applog(LOG_DEBUG, "Klondike SUBMIT NONCE (%02x:%08x)", result->workid, result->nonce);
			submit_nonce(klncgpu->thr[0], work, result->nonce);
			return;
			}
		}
	
	applog(LOG_ERR, "%s%i:%d failed to find nonce work (%02x:%08x) - can't be processed - ignored",	
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
		if (klncgpu->usbinfo.nodev)
			return NULL;
		
		replybuf = klninfo->replies + klninfo->nextreply * REPLY_BUFSIZE;
		replybuf[0] = 0;
		
		err = usb_read(klncgpu, replybuf+1, REPLY_SIZE, &recd, C_GETRESULTS);
		if (recd == REPLY_SIZE) {
			if(opt_log_level <= LOG_DEBUG) {
				char *hexdata = bin2hex(replybuf+1, recd);
				applog(LOG_DEBUG, "%s (%s) reply [%s:%s]", klncgpu->drv->dname, klncgpu->device_path, replybuf+1, hexdata);
				free(hexdata);
			}
			if(++klninfo->nextreply == MAX_REPLY_COUNT)
				klninfo->nextreply = 0;
				
			replybuf[0] = replybuf[1];
			if(replybuf[0] == '=')
				klondike_check_nonce(klncgpu, (WORKRESULT *)replybuf);
		}
	}
	return NULL;
}

static bool klondike_thread_prepare(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct timeval now;
	
	if (thr_info_create(&(klninfo->replies_thr), NULL, klondike_get_replies, (void *)klncgpu)) {
		applog(LOG_ERR, "%s%i: thread create failed", klncgpu->drv->name, klncgpu->device_id);
		return false;
	}
	pthread_detach(klninfo->replies_thr.pth);
	
	// init status data structures
	nmsleep(500);
	klondike_get_stats(klncgpu);
	
	return true;
}

static void klondike_flush_work(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	int dev;
	
	if(klninfo->status == NULL)
		return;
		
	applog(LOG_DEBUG, "Klondike flushing work work");
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) {
		char *reply = SendCmdGetReply(klncgpu, 'A', dev, 0, NULL);
		if(reply != NULL) {
			wr_lock(&(klninfo->stat_lock));
			klninfo->status[dev] = *(WORKSTATUS *)(reply+2);
			wr_unlock(&(klninfo->stat_lock));
		}
	}
}

static bool klondike_thread_init(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;

	if (klncgpu->usbinfo.nodev)
		return false;

	klondike_flush_work(klncgpu);

	return true;
}

static void klondike_shutdown(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);

	klondike_flush_work(klncgpu);
	klninfo->shutdown = true;
}

static void klondike_thread_enable(struct thr_info *thr)
{
	struct cgpu_info *klncgpu = thr->cgpu;

	if (klncgpu->usbinfo.nodev)
		return;

	klondike_flush_work(klncgpu);
}

static bool klondike_send_work(struct cgpu_info *klncgpu, int dev, struct work *work)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	WORKTASK data;
	
	if (klncgpu->usbinfo.nodev)
		return false;
			
	memcpy(data.midstate, work->midstate, MIDSTATE_BYTES);
	memcpy(data.merkle, work->data + MERKLE_OFFSET, MERKLE_BYTES);
	data.workid = (uint8_t)(klninfo->devinfo[dev].nextworkid++ & 0xFF);
	work->subid = dev*256 + data.workid;
	
	if(opt_log_level <= LOG_DEBUG) {
				char *hexdata = bin2hex(&data.workid, sizeof(data)-3);
				applog(LOG_DEBUG, "WORKDATA: %s", hexdata);
				free(hexdata);
			}
	
	applog(LOG_DEBUG, "Klondike sending work (%d:%02x)", dev, data.workid);
	char *reply = SendCmdGetReply(klncgpu, 'W', dev, sizeof(data)-3, &data.workid);
	if(reply != NULL) {
		wr_lock(&(klninfo->stat_lock));
		klninfo->status[dev] = *(WORKSTATUS *)(reply+2);
		wr_unlock(&(klninfo->stat_lock));
		return true;
	}
	return false;
}

static bool klondike_queue_full(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct work *work = NULL;
	int dev, queued;
	
	for(queued = 0; queued < MAX_WORK_COUNT-1; queued++)
		for(dev = 0; dev <= klninfo->status->slavecount; dev++)
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
	
	if (klncgpu->usbinfo.nodev)
		return -1;
		
	restart_wait(200);
	if (klninfo->status != NULL) {
		rd_lock(&(klninfo->stat_lock));
		for(dev = 0; dev <= klninfo->status->slavecount; dev++) {
			uint64_t newhashdev = 0;
			if(klninfo->devinfo[dev].lasthashcount > klninfo->status[dev].hashcount) // todo: chg this to check workid for wrapped instead
				newhashdev += klninfo->status[dev].maxcount; // hash counter wrapped
			newhashdev += klninfo->status[dev].hashcount - klninfo->devinfo[dev].lasthashcount;
			klninfo->devinfo[dev].lasthashcount = klninfo->status[dev].hashcount;
			newhashcount += (newhashdev << 32) / klninfo->status[dev].maxcount;
			
			// discard old work
			
			//	work_completed(klncgpu, work);
		}
		rd_unlock(&(klninfo->stat_lock));
	}
	return newhashcount;
}

static double convertKlnTemp(uint8_t temp)
{
	return (double)1/((double)1/(25+273.15) + log((double)temp*1000/(256-temp)/2200)/3987) - 273.15;
}

static void get_klondike_statline_before(char *buf, struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	uint8_t temp = 0xFF;
	int dev;

	rd_lock(&(klninfo->stat_lock));
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) { 
		if (klninfo->status[dev].temp < temp)
			temp = klninfo->status[dev].temp;
	}
	rd_unlock(&(klninfo->stat_lock));

	tailsprintf(buf, " %3.0fC 1.2V | ", convertKlnTemp(temp));
}

static struct api_data *klondike_api_stats(struct cgpu_info *klncgpu)
{
	struct klondike_info *klninfo = (struct klondike_info *)(klncgpu->device_data);
	struct api_data *root = NULL;
	char buf[12];
	int dev;
		
	rd_lock(&(klninfo->stat_lock));
	for (dev = 0; dev <= klninfo->status->slavecount; dev++) { 

		float ftemp = convertKlnTemp(klninfo->status[dev].temp);
		sprintf(buf, "Temp%d", dev);
		root = api_add_temp(root, buf, &ftemp, true);
		
		double dclk = (double)klninfo->cfg[dev].hashclock / 2;
		sprintf(buf, "Clock%d", dev);
		root = api_add_freq(root, buf, &dclk, true);
	}
	rd_unlock(&(klninfo->stat_lock));

}

struct device_drv klondike_drv = {
	.drv_id = DRIVER_KLONDIKE,
	.dname = "Klondike",
	.name = KLN,
	.drv_detect = klondike_detect,
	.get_api_stats = klondike_api_stats,
	.get_statline_before = get_klondike_statline_before,
	.get_stats = klondike_get_stats,
	.identify_device = klondike_identify,
	.thread_prepare = klondike_thread_prepare,
	.thread_init = klondike_thread_init,
	.hash_work = hash_queued_work,
	.scanwork = klondike_scanwork,
	.queue_full = klondike_queue_full,
	.flush_work = klondike_flush_work,
	.thread_shutdown = klondike_shutdown,
	.thread_enable = klondike_thread_enable
};
