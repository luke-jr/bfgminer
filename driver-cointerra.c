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
#define COINTERRA_USB_POLL_TIMEOUT  1
#define COINTERRA_PACKET_SIZE  0x40
#define COINTERRA_START_SEQ  0x5a,0x5a
#define COINTERRA_MSG_SIZE  (COINTERRA_PACKET_SIZE - sizeof(cointerra_startseq))
#define COINTERRA_MSGBODY_SIZE  (COINTERRA_MSG_SIZE - 1)

BFG_REGISTER_DRIVER(cointerra_drv)

enum cointerra_msg_type_out {
	CMTO_RESET     = 1,
	CMTO_WORK      = 2,
	CMTO_REQUEST   = 4,
	CMTO_HWERR     = 5,
	CMTO_LEDCTL    = 6,
	CMTO_HASHRATE  = 7,
	CMTO_GET_INFO  = 0x21,
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

enum cointerra_reset_level {
	CRL_WORK_UPDATE = 1,
	CRL_NEW_BLOCK   = 2,
	CRL_INIT        = 3,
};

struct cointerra_dev_state {
	libusb_device_handle *usbh;
	struct lowl_usb_endpoint *ep;
	unsigned pipes_per_asic;
	unsigned pipes_per_die;
	int works_requested;
	int next_work_id;
};

static const uint8_t cointerra_startseq[] = {COINTERRA_START_SEQ};

static
bool cointerra_open(const struct lowlevel_device_info * const info, const char * const repr, libusb_device_handle ** const usbh_p, struct lowl_usb_endpoint ** const ep_p)
{
	if (libusb_open(info->lowl_data, usbh_p))
		applogr(false, LOG_DEBUG, "%s: USB open failed on %s",
		        repr, info->devid);
	*ep_p = usb_open_ep_pair(*usbh_p, COINTERRA_EP_R, 64, COINTERRA_EP_W, 64);
	usb_ep_set_timeouts_ms(*ep_p, COINTERRA_USB_TIMEOUT, COINTERRA_USB_TIMEOUT);
	if (!*ep_p)
	{
		applog(LOG_DEBUG, "%s: Endpoint open failed on %s",
		       repr, info->devid);
		libusb_close(*usbh_p);
		*usbh_p = NULL;
		return false;
	}
	return true;
}

static
bool cointerra_write_msg(struct lowl_usb_endpoint * const ep, const char * const repr, const uint8_t msgtype, const void * const msgbody)
{
	uint8_t buf[COINTERRA_PACKET_SIZE], *p;
	memcpy(buf, cointerra_startseq, sizeof(cointerra_startseq));
	p = &buf[sizeof(cointerra_startseq)];
	pk_u8(p, 0, msgtype);
	memcpy(&p[1], msgbody, COINTERRA_MSGBODY_SIZE);
	
	if (usb_write(ep, buf, sizeof(buf)) != sizeof(buf))
		return false;
	
	return true;
}

static
bool cointerra_read_msg(uint8_t * const out_msgtype, uint8_t * const out, struct lowl_usb_endpoint * const ep, const char * const repr)
{
	uint8_t ss[] = {COINTERRA_START_SEQ};
	uint8_t buf[COINTERRA_PACKET_SIZE];
	usb_search(ep, ss, sizeof(ss), NULL);
	const int xfer = usb_read(ep, buf, sizeof(buf));
	if (!xfer)
		return false;
	if (xfer != sizeof(buf))
		applogr(false, LOG_ERR, "%s: Packet size mismatch (actual=%d expected=%d)",
		        repr, xfer, (int)sizeof(buf));
	uint8_t * const bufp = &buf[sizeof(ss)];
	*out_msgtype = upk_u8(bufp, 0);
	memcpy(out, &bufp[1], COINTERRA_MSGBODY_SIZE);
	return true;
}

static
bool cointerra_request(struct lowl_usb_endpoint * const ep, const uint8_t msgtype, uint16_t interval_cs)
{
	uint8_t buf[COINTERRA_MSGBODY_SIZE] = {0};
	pk_u16le(buf, 0, msgtype);
	pk_u16le(buf, 2, interval_cs);
	return cointerra_write_msg(ep, cointerra_drv.dname, CMTO_REQUEST, buf);
}

static
bool cointerra_reset(struct lowl_usb_endpoint * const ep, const enum cointerra_reset_level crl)
{
	uint8_t buf[COINTERRA_MSGBODY_SIZE] = { crl };
	return cointerra_write_msg(ep, cointerra_drv.dname, CMTO_RESET, buf);
}

static
void cointerra_set_queue_full(struct cgpu_info * const dev, const bool nv)
{
	if (dev->thr[0]->queue_full == nv)
		return;
	set_on_all_procs(dev, thr[0]->queue_full, nv);
}

static
bool cointerra_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_usb, "GoldStrike");
}

static
bool cointerra_lowl_probe(const struct lowlevel_device_info * const info)
{
	bool rv = false;
	
	if (info->lowl != &lowl_usb)
		applogr(false, LOG_DEBUG, "%s: Matched \"%s\" %s, but lowlevel driver is not usb_generic!",
		        __func__, info->product, info->devid);
	
	libusb_device_handle *usbh;
	struct lowl_usb_endpoint *ep;
	if (!cointerra_open(info, cointerra_drv.dname, &usbh, &ep))
		return false;
	
	unsigned pipes;
	{
		{
			uint8_t buf[COINTERRA_MSGBODY_SIZE] = {0};
			if (!cointerra_write_msg(ep, cointerra_drv.dname, CMTO_GET_INFO, buf))
				goto err;
		}
		
		uint8_t msgtype;
		uint8_t buf[COINTERRA_MSG_SIZE];
		while (true)
		{
			if (!cointerra_read_msg(&msgtype, buf, ep, cointerra_drv.dname))
				goto err;
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
	rv = add_cgpu(cgpu);

err:
	usb_close_ep(ep);
	libusb_close(usbh);
	return rv;
}

static
bool cointerra_init(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	struct lowlevel_device_info * const info = dev->device_data;
	struct cointerra_dev_state * const devstate = malloc(sizeof(*devstate));
	
	dev->device_data = devstate;
	*devstate = (struct cointerra_dev_state){
		.pipes_per_die = 120,
		.pipes_per_asic = 240,
	};
	if (!cointerra_open(info, dev->dev_repr, &devstate->usbh, &devstate->ep))
		return false;
	struct lowl_usb_endpoint * const ep = devstate->ep;
	
	// Request regular status updates
	cointerra_request(ep, CMTI_STATUS, 0x83d);
	
	cointerra_reset(ep, CRL_INIT);
	
	// Queue is full until device asks for work
	cointerra_set_queue_full(dev, true);
	
	timer_set_delay_from_now(&master_thr->tv_poll, 100000);
	
	return true;
}

static
bool cointerra_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info * const dev = thr->cgpu->device;
	struct thr_info * const master_thr = dev->thr[0];
	struct cointerra_dev_state * const devstate = dev->device_data;
	uint8_t buf[COINTERRA_MSGBODY_SIZE] = {0};
	
	if (unlikely(!devstate->works_requested))
	{
		applog(LOG_DEBUG, "%s: Attempt to queue work while none requested; rejecting", dev->dev_repr);
		cointerra_set_queue_full(dev, true);
		return false;
	}
	
	work->device_id = devstate->next_work_id;
	
	pk_u16be(buf, 0, work->device_id);
	swap32yes(&buf[   6],  work->midstate  , 0x20 / 4);
	swap32yes(&buf[0x26], &work->data[0x40],  0xc / 4);
	pk_u16le(buf, 50, 0);  // ntime roll limit
	pk_u16le(buf, 52, 0x20);  // number of zero bits in results
	if (!cointerra_write_msg(devstate->ep, cointerra_drv.dname, CMTO_WORK, buf))
		return false;
	
	HASH_ADD_INT(master_thr->work, device_id, work);
	++devstate->next_work_id;
	if (!--devstate->works_requested)
	{
		applog(LOG_DEBUG, "%s: Sent all requested works, queue full", dev->dev_repr);
		cointerra_set_queue_full(dev, true);
	}
	
	return true;
}

static
void cointerra_queue_flush(struct thr_info * const thr)
{
}

static
bool cointerra_poll_msg(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu, *proc;
	struct thr_info *mythr;
	struct cointerra_dev_state * const devstate = dev->device_data;
	uint8_t msgtype;
	uint8_t buf[COINTERRA_MSGBODY_SIZE];
	
	if (!cointerra_read_msg(&msgtype, buf, devstate->ep, dev->dev_repr))
		return false;
	
	switch (msgtype)
	{
		case CMTI_WORKREQ:
		{
			devstate->works_requested = upk_u16le(buf, 0);
			const bool qf = !devstate->works_requested;
			applog(LOG_DEBUG, "%s: %u works requested",
			       dev->dev_repr, devstate->works_requested);
			cointerra_set_queue_full(dev, qf);
			break;
		}
		case CMTI_MATCH:
		{
			struct work *work;
			const int workid = upk_u16be(buf, 0);
			const int die = buf[2], asic = buf[3], pipeno = buf[5];
			const unsigned procno = (asic * devstate->pipes_per_asic) + (die * devstate->pipes_per_die) + pipeno;
			const uint32_t timeoff = upk_u32le(buf, 42);
			const uint32_t nonce = upk_u32le(buf, 57);
			
			proc = get_proc_by_id(dev, procno) ?: dev;
			mythr = proc->thr[0];
			
			HASH_FIND_INT(master_thr->work, &workid, work);
			if (unlikely(!work))
			{
				applog(LOG_WARNING, "%"PRIpreprv": Got %s message about unknown work 0x%x",
				       proc->proc_repr, "nonce found", workid);
				inc_hw_errors3(mythr, NULL, &nonce, 1.);
				break;
			}
			
			submit_noffset_nonce(mythr, work, nonce, timeoff);
			
			// hashes_done must be counted by matches because cointerra devices do not provide a way to know which pipe completed matchless work
			hashes_done2(mythr, 0x100000000, NULL);
			
			break;
		}
		case CMTI_WORKDONE:
		{
			const int workid = upk_u16be(buf, 0);
			struct work *work;
			HASH_FIND_INT(master_thr->work, &workid, work);
			if (unlikely(!work))
			{
				applog(LOG_WARNING, "%s: Got %s message about unknown work 0x%x",
				       dev->dev_repr, "work done", workid);
				inc_hw_errors_only(master_thr);
				break;
			}
			HASH_DEL(master_thr->work, work);
			free_work(work);
			break;
		}
		case CMTI_STATUS:
		{
			proc = dev;
			for (int i = 0; i < 0x10; i += 2)
			{
				const float celcius = upk_u16le(buf, i) * 0.01;
				for (int j = 0; j < devstate->pipes_per_die; ++j)
				{
					proc->temp = celcius;
					proc = proc->next_proc;
					if (unlikely(!proc))
						goto die_temps_done;
				}
			}
die_temps_done:
			// TODO: ambient temps, fan, voltage, etc
			;
		}
		case CMTI_SETTINGS:
		case CMTI_INFO:
			break;
		case CMTI_LOGMSG:
			applog(LOG_NOTICE, "%s: Devlog: %.*s", dev->dev_repr, (int)COINTERRA_MSGBODY_SIZE, buf);
			break;
		case CMTI_RESETDONE:
		case CMTI_ERRINFO:
			break;
	}
	
	return true;
}

static
void cointerra_poll(struct thr_info * const master_thr)
{
	struct cgpu_info * const dev = master_thr->cgpu;
	struct timeval tv_timeout;
	timer_set_delay_from_now(&tv_timeout, 10000);
	while (true)
	{
		if (!cointerra_poll_msg(master_thr))
		{
			applog(LOG_DEBUG, "%s poll: No more messages", dev->dev_repr);
			break;
		}
		if (timer_passed(&tv_timeout, NULL))
		{
			applog(LOG_DEBUG, "%s poll: 10ms timeout met", dev->dev_repr);
			break;
		}
	}
	
	timer_set_delay_from_now(&master_thr->tv_poll, 100000);
}

struct device_drv cointerra_drv = {
	.dname = "cointerra",
	.name = "CTA",
	
	.lowl_match = cointerra_lowl_match,
	.lowl_probe = cointerra_lowl_probe,
	
	.minerloop = minerloop_queue,
	.thread_init = cointerra_init,
	.queue_append = cointerra_queue_append,
	.queue_flush = cointerra_queue_flush,
	.poll = cointerra_poll,
};
