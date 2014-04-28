/*
 * Copyright 2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowlevel.h"
#include "lowl-usb.h"

#define COINTERRA_EP_R  (LIBUSB_ENDPOINT_IN  | 1)
#define COINTERRA_EP_W  (LIBUSB_ENDPOINT_OUT | 1)
#define COINTERRA_USB_TIMEOUT  100
#define COINTERRA_PACKET_SIZE  0x40
#define COINTERRA_START_SEQ  0x5a,0x5a
#define COINTERRA_MSG_SIZE  (COINTERRA_PACKET_SIZE - sizeof(cointerra_startseq))

BFG_REGISTER_DRIVER(cointerra_drv)

enum cointerra_msg_type_out {
	CMTO_RESET     = 1,
	CMTO_WORK      = 2,
	CMTO_REQUEST   = 4,
	CMTO_HWERR     = 5,
	CMTO_LEDCTL    = 6,
	CMTO_HASHRATE  = 7,
};

enum cointerra_msg_type_in {
	CMTI_WORKREQ   = 1,
	CMTI_MATCH     = 2,
	CMTI_WORKDONE  = 3,
	CMTI_STATUS    = 4,
	CMTI_SETTINGS  = 5,
	CMTI_INFO      = 6,
	CMTI_LOGMSG    = 7,
	CMTI_RESETDONE = 8,
	CMTI_ERRINFO   = 0xa,
};

static const uint8_t cointerra_startseq[] = {COINTERRA_START_SEQ};

static
int cointerra_read_msg(uint8_t * const out_msgtype, uint8_t * const out, libusb_device_handle * const usbh, const char * const repr, const unsigned timeout)
{
	uint8_t ss[] = {COINTERRA_START_SEQ};
	uint8_t buf[COINTERRA_PACKET_SIZE];
	int e, xfer;
	e = libusb_bulk_transfer(usbh, COINTERRA_EP_R, buf, sizeof(buf), &xfer, timeout);
	if (e)
		return e;
	if (xfer != COINTERRA_PACKET_SIZE)
		applogr(LIBUSB_ERROR_OTHER, LOG_ERR, "%s: Packet size mismatch (actual=%d expected=%d)",
		        repr, xfer, (int)COINTERRA_PACKET_SIZE);
	for (int i = sizeof(ss); i--; )
		if (ss[i] != buf[i])
			applogr(LIBUSB_ERROR_OTHER;, LOG_ERR, "%s: Packet start sequence mismatch", repr);
	uint8_t * const bufp = &buf[sizeof(ss)];
	*out_msgtype = upk_u8(bufp, 0);
	memcpy(out, &bufp[1], COINTERRA_MSGBODY_SIZE);
	return 0;
}

static
bool cointerra_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_usb, "GoldStrike");
}

static
bool cointerra_lowl_probe(const struct lowlevel_device_info * const info)
{
	int e;
	
	if (info->lowl != &lowl_usb)
		applogr(false, LOG_DEBUG, "%s: Matched \"%s\" %s, but lowlevel driver is not usb_generic!",
		        __func__, info->product, info->devid);
	
	libusb_device_handle *usbh;
	e = libusb_open(info->lowl_data, &usbh);
	if (e)
		applogr(false, LOG_ERR, "%s: Failed to open %s: %s",
		        cointerra_drv.dname, info->devid, bfg_strerror(e, BST_LIBUSB));
	
	unsigned pipes;
	{
		uint8_t buf[COINTERRA_PACKET_SIZE] = {
			COINTERRA_START_SEQ,
			CMTO_REQUEST,
			CMTI_INFO, 0,
		};
		int xfer;
		uint8_t msgtype;
		
		e = libusb_bulk_transfer(usbh, COINTERRA_EP_W, buf, sizeof(buf), &xfer, COINTERRA_USB_TIMEOUT);
		if (e)
			return e;
		
		while (true)
		{
			e = cointerra_read_msg(&msgtype, buf, usbh, cointerra_drv.dname, COINTERRA_USB_TIMEOUT);
			if (e)
				return e;
			if (msgtype == CMTI_INFO)
				break;
			// FIXME: Timeout if we keep getting packets we don't care about
		}
		
		pipes = upk_u16le(buf, 8);
	}
	
	applog(LOG_DEBUG, "%s: Found %u pipes on %s",
	       __func__, pipes, info->devid);
	
	struct cgpu_info * const cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info){
		.drv = &cointerra_drv,
		.procs = pipes,
		.device_data = lowlevel_ref(info),
		.threads = 1,
		.device_path = strdup(info->devid),
		.dev_manufacturer = maybe_strdup(info->manufacturer),
		.dev_product = maybe_strdup(info->product),
		.dev_serial = maybe_strdup(info->serial),
		.deven = DEV_ENABLED,
	};
	return add_cgpu(cgpu);
}

struct device_drv cointerra_drv = {
	.dname = "cointerra",
	.name = "CTA",
	
	.lowl_match = cointerra_lowl_match,
	.lowl_probe = cointerra_lowl_probe,
};
