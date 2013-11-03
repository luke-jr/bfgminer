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
#define BF1MSGSIZE 7
#define BF1INFOSIZE 14

static void bitfury_empty_buffer(struct cgpu_info *bitfury)
{
	char buf[512];
	int amount;

	do {
		usb_read_once(bitfury, buf, 512, &amount, C_BF1_FLUSH);
	} while (amount);
}

static int bitfury_open(struct cgpu_info *bitfury)
{
	uint32_t buf[2];
	int err;

	bitfury_empty_buffer(bitfury);
	/* Magic sequence to reset device only really needed for windows but
	 * harmless on linux. */
	buf[0] = 0x80250000;
	buf[1] = 0x00000800;
	err = usb_transfer(bitfury, 0, 9, 1, 0, C_ATMEL_RESET);
	if (!err)
		err = usb_transfer(bitfury, 0x21, 0x22, 0, 0, C_ATMEL_OPEN);
	if (!err) {
		err = usb_transfer_data(bitfury, 0x21, 0x20, 0x0000, 0, buf,
					BF1MSGSIZE, C_ATMEL_INIT);
	}

	if (err < 0) {
		applog(LOG_INFO, "%s %d: Failed to open with error %s", bitfury->drv->name,
		       bitfury->device_id, libusb_error_name(err));
	}
	return (err == BF1MSGSIZE);
}

static void bitfury_close(struct cgpu_info *bitfury)
{
	bitfury_empty_buffer(bitfury);
}

static void bitfury_identify(struct cgpu_info *bitfury)
{
	int amount;

	usb_write(bitfury, "L", 1, &amount, C_BF1_IDENTIFY);
}

static bool bitfury_getinfo(struct cgpu_info *bitfury, struct bitfury_info *info)
{
	int amount, err;
	char buf[16];

	err = usb_write(bitfury, "I", 1, &amount, C_BF1_REQINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to write REQINFO",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	err = usb_read(bitfury, buf, BF1INFOSIZE, &amount, C_BF1_GETINFO);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to read GETINFO",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	if (amount != BF1INFOSIZE) {
		applog(LOG_INFO, "%s %d: Getinfo received %d bytes instead of %d",
		       bitfury->drv->name, bitfury->device_id, amount, BF1INFOSIZE);
		return false;
	}
	info->version = buf[1];
	memcpy(&info->product, buf + 2, 8);
	memcpy(&info->serial, buf + 10, 4);

	applog(LOG_INFO, "%s %d: Getinfo returned version %d, product %s serial %08x", bitfury->drv->name,
	       bitfury->device_id, info->version, info->product, info->serial);
	bitfury_empty_buffer(bitfury);
	return true;
}

static bool bitfury_reset(struct cgpu_info *bitfury)
{
	int amount, err;
	char buf[16];

	err = usb_write(bitfury, "R", 1, &amount, C_BF1_REQRESET);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to write REQRESET",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	err = usb_read_timeout(bitfury, buf, BF1MSGSIZE, &amount, BF1WAIT,
			       C_BF1_GETRESET);
	if (err) {
		applog(LOG_INFO, "%s %d: Failed to read GETRESET",
		       bitfury->drv->name, bitfury->device_id);
		return false;
	}
	if (amount != BF1MSGSIZE) {
		applog(LOG_INFO, "%s %d: Getreset received %d bytes instead of %d",
		       bitfury->drv->name, bitfury->device_id, amount, BF1MSGSIZE);
		return false;
	}
	applog(LOG_DEBUG, "%s %d: Getreset returned %s", bitfury->drv->name,
	       bitfury->device_id, buf);
	bitfury_empty_buffer(bitfury);
	return true;
}

static bool bitfury_detect_one(struct libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *bitfury;
	struct bitfury_info *info;

	bitfury = usb_alloc_cgpu(&bitfury_drv, 1);

	if (!usb_init(bitfury, dev, found))
		goto out;
	applog(LOG_INFO, "%s %d: Found at %s", bitfury->drv->name,
	       bitfury->device_id, bitfury->device_path);

	info = calloc(sizeof(struct bitfury_info), 1);
	if (!info)
		quit(1, "Failed to calloc info in bitfury_detect_one");
	bitfury->device_data = info;
	/* This does not artificially raise hashrate, it simply allows the
	 * hashrate to adapt quickly on starting. */
	info->total_nonces = 1;

	if (!bitfury_open(bitfury))
		goto out_close;

	/* Send getinfo request */
	if (!bitfury_getinfo(bitfury, info))
		goto out_close;

	/* Send reset request */
	if (!bitfury_reset(bitfury))
		goto out_close;

	bitfury_identify(bitfury);
	bitfury_empty_buffer(bitfury);

	if (!add_cgpu(bitfury))
		quit(1, "Failed to add_cgpu in bitfury_detect_one");

	update_usb_stats(bitfury);
	applog(LOG_INFO, "%s %d: Successfully initialised %s",
	       bitfury->drv->name, bitfury->device_id, bitfury->device_path);
	return true;
out_close:
	bitfury_close(bitfury);
	usb_uninit(bitfury);
out:
	bitfury = usb_free_cgpu(bitfury);
	return false;
}

static void bitfury_detect(bool __maybe_unused hotplug)
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

#define BT_OFFSETS 3
const uint32_t bf_offsets[] = {-0x800000, 0, -0x400000};

static bool bitfury_checkresults(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	int i;

	for (i = 0; i < BT_OFFSETS; i++) {
		if (test_nonce(work, nonce + bf_offsets[i])) {
			submit_tested_work(thr, work);
			return true;
		}
	}
	return false;
}

static int64_t bitfury_scanwork(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;
	struct bitfury_info *info = bitfury->device_data;
	struct timeval tv_now;
	struct work *work;
	double nonce_rate;
	int64_t ret = 0;
	int amount, i;
	char buf[45];
	int ms_diff;

	work = get_work(thr, thr->id);
	if (unlikely(thr->work_restart)) {
		free_work(work);
		return 0;
	}

	buf[0] = 'W';
	memcpy(buf + 1, work->midstate, 32);
	memcpy(buf + 33, work->data + 64, 12);

	/* New results may spill out from the latest work, making us drop out
	 * too early so read whatever we get for the first half nonce and then
	 * look for the results to prev work. */
	cgtime(&tv_now);
	ms_diff = 600 - ms_tdiff(&tv_now, &info->tv_start);
	if (ms_diff > 0) {
		usb_read_timeout_cancellable(bitfury, info->buf, 512, &amount, ms_diff, C_BF1_GETRES);
		info->tot += amount;
	}

	if (unlikely(thr->work_restart))
		goto cascade;

	/* Now look for the bulk of the previous work results, they will come
	 * in a batch following the first data. */
	cgtime(&tv_now);
	ms_diff = BF1WAIT - ms_tdiff(&tv_now, &info->tv_start);
	if (unlikely(ms_diff < 10))
		ms_diff = 10;
	usb_read_once_timeout_cancellable(bitfury, info->buf + info->tot, BF1MSGSIZE,
					  &amount, ms_diff, C_BF1_GETRES);
	info->tot += amount;
	while (amount) {
		usb_read_once_timeout(bitfury, info->buf + info->tot, 512, &amount, 10, C_BF1_GETRES);
		info->tot += amount;
	};

	if (unlikely(thr->work_restart))
		goto cascade;

	/* Send work */
	usb_write(bitfury, buf, 45, &amount, C_BF1_REQWORK);
	cgtime(&info->tv_start);
	/* Get response acknowledging work */
	usb_read(bitfury, buf, BF1MSGSIZE, &amount, C_BF1_GETWORK);

	/* Only happens on startup */
	if (unlikely(!info->prevwork[BF1ARRAY_SIZE]))
		goto cascade;

	/* Search for what work the nonce matches in order of likelihood. Last
	 * entry is end of result marker. */
	for (i = 0; i < info->tot - BF1MSGSIZE; i += BF1MSGSIZE) {
		uint32_t nonce;
		int j;

		/* Ignore state & switched data in results for now. */
		memcpy(&nonce, info->buf + i + 3, 4);
		nonce = decnonce(nonce);
		for (j = 0; j < BF1ARRAY_SIZE; j++) {
			if (bitfury_checkresults(thr, info->prevwork[j], nonce)) {
				info->nonces++;
				break;
			}
		}
	}

	info->tot = 0;
	free_work(info->prevwork[BF1ARRAY_SIZE]);
cascade:
	for (i = BF1ARRAY_SIZE; i > 0; i--)
		info->prevwork[i] = info->prevwork[i - 1];
	info->prevwork[0] = work;

	info->cycles++;
	info->total_nonces += info->nonces;
	info->saved_nonces += info->nonces;
	info->nonces = 0;
	nonce_rate = (double)info->total_nonces / (double)info->cycles;
	if (info->saved_nonces >= nonce_rate) {
		info->saved_nonces -= nonce_rate;
		ret = (double)0xffffffff * nonce_rate;
	}

	if (unlikely(bitfury->usbinfo.nodev)) {
		applog(LOG_WARNING, "%s %d: Device disappeared, disabling thread",
		       bitfury->drv->name, bitfury->device_id);
		ret = -1;
	}
	return ret;
}

static struct api_data *bitfury_api_stats(struct cgpu_info *cgpu)
{
	struct bitfury_info *info = cgpu->device_data;
	struct api_data *root = NULL;
	double nonce_rate;
	char serial[16];
	int version;

	version = info->version;
	root = api_add_int(root, "Version", &version, true);
	root = api_add_string(root, "Product", info->product, false);
	sprintf(serial, "%08x", info->serial);
	root = api_add_string(root, "Serial", serial, true);
	nonce_rate = (double)info->total_nonces / (double)info->cycles;
	root = api_add_double(root, "NonceRate", &nonce_rate, true);

	return root;
}

static void bitfury_init(struct cgpu_info  *bitfury)
{
	bitfury_close(bitfury);
	bitfury_open(bitfury);
	bitfury_reset(bitfury);
}

static void bitfury_shutdown(struct thr_info *thr)
{
	struct cgpu_info *bitfury = thr->cgpu;

	bitfury_close(bitfury);
}

/* Currently hardcoded to BF1 devices */
struct device_drv bitfury_drv = {
	.drv_id = DRIVER_bitfury,
	.dname = "bitfury",
	.name = "BF1",
	.drv_detect = bitfury_detect,
	.hash_work = &hash_driver_work,
	.scanwork = bitfury_scanwork,
	.get_api_stats = bitfury_api_stats,
	.reinit_device = bitfury_init,
	.thread_shutdown = bitfury_shutdown,
	.identify_device = bitfury_identify
};
