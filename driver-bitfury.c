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

static bool bitfury_prepare(struct thr_info __maybe_unused *thr)
{
	return true;
}

static bool bitfury_fill(struct cgpu_info __maybe_unused *bitfury)
{
	return true;
}

static bool rehash(unsigned char *midstate, unsigned m7, unsigned ntime, unsigned nbits, unsigned nnonce)
{
	uint8_t   in[16];
	uint32_t *in32 = (uint32_t *)in;
	uint32_t *mid32 = (uint32_t *)midstate;
	uint32_t  out32[8];
	uint8_t  *out = (uint8_t *) out32;
	sha256_ctx ctx;

	memset( &ctx, 0, sizeof(sha256_ctx));
	memcpy(ctx.h, mid32, 8*4);
	ctx.tot_len = 64;

	nnonce = bswap_32(nnonce);
	in32[0] = bswap_32(m7);
	in32[1] = bswap_32(ntime);
	in32[2] = bswap_32(nbits);
	in32[3] = nnonce;

	sha256_update(&ctx, in, 16);
	sha256_final(&ctx, out);
	sha256(out, 32, out);

	if (out32[7] == 0)
		return true;
	return false;
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
	nonceoff = nonce + 0x2800000;
	if (test_nonce(work, nonceoff)) {
		submit_nonce(thr, work, nonceoff);
		return true;
	}
	nonceoff = nonce + 0x2C800000;
	if (test_nonce(work, nonceoff)) {
		submit_nonce(thr, work, nonceoff);
		return true;
	}
	nonceoff = nonce + 0x400000;
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
	char sendbuf[45], buf[512];
	int64_t hashes = 0;
	int amount, i, tot;

	sendbuf[0] = 'W';
	memcpy(sendbuf + 1, work->midstate, 32);
	memcpy(sendbuf + 33, work->data + 64, 12);
	usb_write(bitfury, sendbuf, 45, &amount, C_PING);
	usb_read(bitfury, buf, 7, &amount, C_PING);

	if (unlikely(!info->prevwork)) {
		info->prevwork = copy_work(work);
		return 0;
	}

	usb_read_once_timeout(bitfury, buf, 7, &amount, BF1WAIT, C_PING);
	tot = amount;
	while (amount) {
		usb_read_once_timeout(bitfury, buf + tot, 512, &amount, 10, C_PING);
		tot += amount;
	}

	for (i = 0; i < tot; i += 7) {
		uint32_t nonce;

		/* Ignore state & switched data in results for now. */
		memcpy(&nonce, buf + i + 3, 4);
		nonce = decnonce(nonce);
		if (bitfury_checkresults(thr, work, nonce)) {
			hashes += 0xffffffff;
			continue;
		}
		if (bitfury_checkresults(thr, info->prevwork, nonce))
			hashes += 0xffffffff;
	}

	free_work(info->prevwork);
	info->prevwork = copy_work(work);
	work->blk.nonce = 0xffffffff;
	return hashes;
}

static void bitfury_flush_work(struct cgpu_info __maybe_unused *bitfury)
{
}

static struct api_data *bitfury_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void get_bitfury_statline_before(char __maybe_unused *buf, size_t __maybe_unused bufsiz,
					struct cgpu_info __maybe_unused *bitfury)
{
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
	.flush_work = bitfury_flush_work,
	.get_api_stats = bitfury_api_stats,
	.thread_prepare = bitfury_prepare,
	.get_statline_before = get_bitfury_statline_before,
	.reinit_device = bitfury_init,
	.thread_shutdown = bitfury_shutdown,
	.identify_device = bitfury_identify
};
