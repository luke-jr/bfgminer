/*
 * Copyright 2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include "miner.h"
#include "driver-bitfury.h"
#include "sha2.h"

/* Wait longer 1/3 longer than it would take for a full nonce range */
#define BF1WAIT 1600
struct device_drv bitfury_drv;

static void bitfury_open(struct cgpu_info *bitfury)
{
	/* Magic open sequence */
	usb_transfer(bitfury, 0x21, 0x22, 0x0003, 0, C_BFO_OPEN);
}

static void bitfury_close(struct cgpu_info *bitfury)
{
	/* Magic close sequence */
	usb_transfer(bitfury, 0x21, 0x22, 0, 0, C_BFO_CLOSE);
}

static void bitfury_empty_buffer(struct cgpu_info *bitfury)
{
	char buf[512];
	int amount;

	do {
		usb_read(bitfury, buf, 512, &amount, C_PING);
	} while (amount);
}

static void bitfury_identify(struct cgpu_info *bitfury)
{
	int amount;

	usb_write(bitfury, "L", 1, &amount, C_PING);
}

static bool bitfury_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *bitfury;
	struct bitfury_info *info;
	char buf[512];
	int amount;

	bitfury = usb_alloc_cgpu(&bitfury_drv, 1);

	if (!usb_init(bitfury, dev, found)) {
		bitfury = usb_free_cgpu(bitfury);
		goto out;
	}
	applog(LOG_INFO, "%s%d: Found at %s", bitfury->drv->name,
	       bitfury->device_id, bitfury->device_path);

	info = calloc(sizeof(struct bitfury_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in bitfury_detect_one");
	bitfury->device_data = info;

	bitfury_empty_buffer(bitfury);
	usb_buffer_enable(bitfury);

	bitfury_open(bitfury);

	/* Send getinfo request */
	usb_write(bitfury, "I", 1, &amount, C_BFO_REQINFO);
	usb_read(bitfury, buf, 14, &amount, C_BFO_GETINFO);
	if (amount != 14) {
		applog(LOG_WARNING, "%s%d: Getinfo received %d bytes",
		       bitfury->drv->name, bitfury->device_id, amount);
		goto out_close;
	}
	info->version = buf[1];
	memcpy(&info->product, buf + 2, 8);
	memcpy(&info->serial, buf + 10, 4);

	applog(LOG_INFO, "%s%d: Getinfo returned version %d, product %s serial %08x", bitfury->drv->name,
	       bitfury->device_id, info->version, info->product, info->serial);
	bitfury_empty_buffer(bitfury);

	/* Send reset request */
	usb_write(bitfury, "R", 1, &amount, C_BFO_REQRESET);
	usb_read_timeout(bitfury, buf, 7, &amount, BF1WAIT, C_BFO_GETRESET);

	if (amount != 7) {
		applog(LOG_WARNING, "%s%d: Getreset received %d bytes",
		       bitfury->drv->name, bitfury->device_id, amount);
		goto out_close;
	}
	applog(LOG_INFO, "%s%d: Getreset returned %s", bitfury->drv->name,
	       bitfury->device_id, buf);
	bitfury_empty_buffer(bitfury);

	bitfury_identify(bitfury);
	bitfury_empty_buffer(bitfury);

	if (!add_cgpu(bitfury))
		goto out_close;
	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s%d: Found at %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);
	return true;
out_close:
	bitfury_close(bitfury);
out:
	return false;
}

static void bitfury_detect(void)
{
	usb_detect(&bitfury_drv, bitfury_detect_one);
}

static uint32_t decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24; in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2)&0x3FFFFF;

	/* Extraction */
	if (in & 1) out |= (1 << 23);
	if (in & 2) out |= (1 << 22);

	out -= 0x800004;
	return out;
}

static bool bitfury_checkresults(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	uint32_t nonceoff;

	if (test_nonce(work, nonce)) {
		submit_nonce(thr, work, nonce);
		return true;
	}
	nonceoff = nonce - 0x400000;
	if (test_nonce(work, nonceoff)) {
		submit_nonce(thr, work, nonceoff);
		return true;
	}
	nonceoff = nonce - 0x800000;
	if (test_nonce(work, nonceoff)) {
		submit_nonce(thr, work, nonceoff);
		return true;
	}
	return false;
}

static int64_t bitfury_scanhash(struct thr_info *thr, struct work *work,
				int64_t __maybe_unused max_nonce)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury_info *info = bitfury->device_data;
	int amount, i;
	char buf[45];

	buf[0] = 'W';
	memcpy(buf + 1, work->midstate, 32);
	memcpy(buf + 33, work->data + 64, 12);

	/* New results may spill out from the latest work, making us drop out
	 * too early so read whatever we get for the first half nonce and then
	 * look for the results to prev work. */
	usb_read_timeout(bitfury, info->buf, 512, &info->tot, 600, C_BFO_GETRES);

	/* Now look for the bulk of the previous work results, they will come
	 * in a batch following the first data. */
	usb_read_once_timeout(bitfury, info->buf + info->tot, 7, &amount, 1000, C_BFO_GETRES);
	info->tot += amount;
	while (amount) {
		usb_read_once_timeout(bitfury, info->buf + info->tot, 512, &amount, 10, C_BFO_GETRES);
		info->tot += amount;
	};

	/* Send work */
	usb_write(bitfury, buf, 45, &amount, C_BFO_REQWORK);
	/* Get response acknowledging work */
	usb_read(bitfury, buf, 7, &amount, C_BFO_GETWORK);

	/* Only happens on startup */
	if (unlikely(!info->prevwork2))
		goto cascade;

	/* Search for what work the nonce matches in order of likelihood. Last
	 * entry is end of result marker. */
	for (i = 0; i < info->tot - 7; i += 7) {
		uint32_t nonce;

		/* Ignore state & switched data in results for now. */
		memcpy(&nonce, info->buf + i + 3, 4);
		nonce = decnonce(nonce);
		if (bitfury_checkresults(thr, info->prevwork1, nonce)) {
			info->nonces++;
			continue;
		}
		if (bitfury_checkresults(thr, info->prevwork2, nonce)) {
			info->nonces++;
			continue;
		}
		if (bitfury_checkresults(thr, work, nonce)) {
			info->nonces++;
			continue;
		}
	}

	free_work(info->prevwork2);
cascade:
	info->prevwork2 = info->prevwork1;
	info->prevwork1 = copy_work(work);
	work->blk.nonce = 0xffffffff;
	if (info->nonces) {
		info->nonces--;
		return (int64_t)0xffffffff;
	}
	return 0;
}

static struct api_data *bitfury_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void bitfury_init(struct cgpu_info __maybe_unused *bitfury)
{
}

static void bitfury_shutdown(struct thr_info __maybe_unused *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;

	bitfury_close(bitfury);
}

/* Currently hardcoded to BF1 devices */
struct device_drv bitfury_drv = {
	.drv_id = DRIVER_BITFURY,
	.dname = "bitfury",
	.name = "BFO",
	.drv_detect = bitfury_detect,
	.scanhash = bitfury_scanhash,
	.get_api_stats = bitfury_api_stats,
	.reinit_device = bitfury_init,
	.thread_shutdown = bitfury_shutdown,
	.identify_device = bitfury_identify
};
