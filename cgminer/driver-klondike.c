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

#include "config.h"

#ifdef WIN32
#include <windows.h>
#endif

#include "compat.h"
#include "miner.h"
#include "usbutils.h"

#define K1 "K1"
#define K16 "K16"
#define K64 "K64"

#define REPLY_BUFSIZE 64

struct device_drv klondike_drv;

struct klondike_id {
	char id[8];
	uint8_t version;
	uint32_t serial;
	char product[16];
};

struct klondike_status {
	
};

struct kondike_cfg {
	
};


static bool klondike_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *klninfo = calloc(1, sizeof(*klninfo));
	char replybuf[REPLY_BUFSIZE];
	char devpath[20];
	int attempts = 0;
	int sent, recd, err;

	if (unlikely(!klninfo))
		quit(1, "Failed to calloc klninfo in klondike_detect_one");
		
	klninfo->drv = &klondike_drv;
	klninfo->deven = DEV_ENABLED;
	klninfo->threads = 1;
	
	if (usb_init(klninfo, dev, found)) {
		
		sprintf(devpath, "%d:%d", (int)(klninfo->usbinfo.bus_number), (int)(klninfo->usbinfo.device_address));
		while(attempts++ < 2) {
			
			err = usb_write(klninfo, "I", 2, &sent, C_REQUESTIDENTIFY);
			if (err < 0 || sent != 2) {
				applog(LOG_ERR, "%s detect (%s) send identify request failed (%d:%d)", klninfo->drv->dname, devpath, sent, err);
				break;
			}
				
			err = usb_read(klninfo, replybuf, sizeof(replybuf), &recd, C_GETIDENTIFY);
			if (err < 0) {
				applog(LOG_ERR, "%s detect (%s) error identify reply (%d:%d)", klninfo->drv->dname, devpath, recd, err);
			} else if (recd < 1) {
				applog(LOG_ERR, "%s detect (%s) empty identify reply (%d)",	klninfo->drv->dname, devpath, recd);
			} else if (replybuf[0] == 'I' && replybuf[1] == 0) {
				applog(LOG_DEBUG, "%s (%s) identified as: '%s'", klninfo->drv->dname, devpath, klninfo->drv->name);
				
				// do something with id info
				
				update_usb_stats(klninfo);
				return true;
			}
		}
		usb_uninit(klninfo);
	}
	free(klninfo);
	return false;
}

static void klondike_detect(void)
{
	usb_detect(&klondike_drv, klondike_detect_one);
}


struct device_drv klondike_drv = {
	.drv_id = DRIVER_KLONDIKE,
	.dname = "Klondike",
	.name = K16,
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
