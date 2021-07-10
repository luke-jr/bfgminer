/*
 * Copyright 2012 nelisky
 * Copyright 2012-2014 Luke Dashjr
 * Copyright 2012-2013 Denis Ahrens
 * Copyright 2012 Xiangfu
 *
 * This work is based upon the Java SDK provided by ztex which is
 * Copyright (C) 2009-2011 ZTEX GmbH.
 * http://www.ztex.de
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include "miner.h"
#include <unistd.h>
#include <sha2.h>

#include "deviceapi.h"
#include "dynclock.h"
#include "libztex.h"
#include "lowlevel.h"
#include "lowl-usb.h"
#include "util.h"

#define GOLDEN_BACKLOG 5

BFG_REGISTER_DRIVER(ztex_drv)

// Forward declarations
static void ztex_disable(struct thr_info* thr);
static bool ztex_prepare(struct thr_info *thr);

static void ztex_selectFpga(struct libztex_device* ztex, int16_t fpgaNum)
{
	if (ztex->root->numberOfFpgas > 1) {
		if (ztex->root->selectedFpga != fpgaNum)
			mutex_lock(&ztex->root->mutex);
		libztex_selectFpga(ztex, fpgaNum);
	}
}

static void ztex_releaseFpga(struct libztex_device* ztex)
{
	if (ztex->root->numberOfFpgas > 1) {
		ztex->root->selectedFpga = -1;
		mutex_unlock(&ztex->root->mutex);
	}
}

static struct cgpu_info *ztex_setup(struct libztex_device *dev, int fpgacount)
{
	struct cgpu_info *ztex;
	char *fpganame = (char*)dev->snString;

	ztex = calloc(1, sizeof(struct cgpu_info));
	ztex->drv = &ztex_drv;
	ztex->device_ztex = dev;
	ztex->procs = fpgacount;
	ztex->threads = fpgacount;
	ztex->dev_manufacturer = dev->dev_manufacturer;
	ztex->dev_product = dev->dev_product;
	ztex->dev_serial = (char*)&dev->snString[0];
	ztex->name = fpganame;
	add_cgpu(ztex);
	strcpy(ztex->device_ztex->repr, ztex->dev_repr);
	applog(LOG_INFO, "%"PRIpreprv": Found Ztex (ZTEX %s)", ztex->dev_repr, fpganame);

	return ztex;
}

static
bool ztex_lowl_match(const struct lowlevel_device_info * const info)
{
	return lowlevel_match_lowlproduct(info, &lowl_usb, "btcminer for ZTEX");
}

static
bool ztex_lowl_probe(const struct lowlevel_device_info * const info)
{
	const char * const product = info->product;
	const char * const serial = info->serial;
	if (info->lowl != &lowl_usb)
	{
		bfg_probe_result_flags = BPR_WRONG_DEVTYPE;
		applog(LOG_DEBUG, "%s: Matched \"%s\" serial \"%s\", but lowlevel driver is not usb!",
		       __func__, product, serial);
		return false;
	}
	
	libusb_device * const usbdev = info->lowl_data;
	
	const enum ztex_check_result err = libztex_checkDevice(usbdev);
	switch (err)
	{
		case CHECK_ERROR:
			applogr(false, LOG_ERR, "%s: Can not check device %s", ztex_drv.dname, info->devid);
		case CHECK_IS_NOT_ZTEX:
			return false;
		case CHECK_OK:
			break;
		case CHECK_RESCAN:
			bfg_need_detect_rescan = true;
			return false;
	}
	
	int fpgacount;
	struct libztex_device *ztex_master;
	struct cgpu_info *ztex;
	
	ztex_master = libztex_prepare_device2(usbdev);
	if (!ztex_master)
		applogr(false, LOG_ERR, "%s: libztex_prepare_device2 failed on %s", ztex_drv.dname, info->devid);
	
	if (bfg_claim_usb(&ztex_drv, true, ztex_master->usbbus, ztex_master->usbaddress))
		return false;
	ztex_master->root = ztex_master;
	fpgacount = libztex_numberOfFpgas(ztex_master);
	ztex_master->handles = fpgacount;
	ztex = ztex_setup(ztex_master, fpgacount);

	if (fpgacount > 1)
		pthread_mutex_init(&ztex->device_ztex->mutex, NULL);
	
	return true;
}

static bool ztex_change_clock_func(struct thr_info *thr, int bestM)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct libztex_device *ztex = thr->cgpu->device_ztex;

	ztex_selectFpga(ztex, cgpu->proc_id);
	libztex_setFreq(ztex, bestM, cgpu->proc_repr);
	ztex_releaseFpga(ztex);

	return true;
}

static bool ztex_updateFreq(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct libztex_device *ztex = thr->cgpu->device_ztex;
	bool rv = dclk_updateFreq(&ztex->dclk, ztex_change_clock_func, thr);
	if (unlikely(!rv)) {
		ztex_selectFpga(ztex, cgpu->proc_id);
		libztex_resetFpga(ztex);
		ztex_releaseFpga(ztex);
	}
	return rv;
}

static bool ztex_checkNonce(struct cgpu_info *cgpu,
                            struct work *work,
                            struct libztex_hash_data *hdata)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[80];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	unsigned char hash2[32];
	uint32_t *hash2_32 = (uint32_t *)hash2;

	swap32[76/4] = htobe32(hdata->nonce);

	swap32yes(swap32, data32, 76 / 4);

	sha256(swap, 80, hash1);
	sha256(hash1, 32, hash2);

	if (be32toh(hash2_32[7]) != ((hdata->hash7 + 0x5be0cd19) & 0xFFFFFFFF)) {
		applog(LOG_DEBUG, "%"PRIpreprv": checkNonce failed for %08x", cgpu->proc_repr, hdata->nonce);
		return false;
	}
	return true;
}

static int64_t ztex_scanhash(struct thr_info *thr, struct work *work,
                              __maybe_unused int64_t max_nonce)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct libztex_device *ztex;
	unsigned char sendbuf[44];
	int i, j, k;
	uint32_t *backlog;
	int backlog_p = 0, backlog_max;
	uint32_t *lastnonce;
	uint32_t nonce, noncecnt = 0;
	bool overflow, found;
	struct libztex_hash_data hdata[GOLDEN_BACKLOG];

	if (thr->cgpu->deven == DEV_DISABLED)
		return -1;

	ztex = thr->cgpu->device_ztex;

	memcpy(sendbuf, work->data + 64, 12);
	memcpy(sendbuf + 12, work->midstate, 32);

	ztex_selectFpga(ztex, cgpu->proc_id);
	i = libztex_sendHashData(ztex, sendbuf);
	if (i < 0) {
		// Something wrong happened in send
		applog(LOG_ERR, "%"PRIpreprv": Failed to send hash data with err %d, retrying", cgpu->proc_repr, i);
		cgsleep_ms(500);
		i = libztex_sendHashData(ztex, sendbuf);
		if (i < 0) {
			// And there's nothing we can do about it
			ztex_disable(thr);
			applog(LOG_ERR, "%"PRIpreprv": Failed to send hash data with err %d, giving up", cgpu->proc_repr, i);
			ztex_releaseFpga(ztex);
			return -1;
		}
	}
	ztex_releaseFpga(ztex);

	applog(LOG_DEBUG, "%"PRIpreprv": sent hashdata", cgpu->proc_repr);

	lastnonce = calloc(1, sizeof(uint32_t)*ztex->numNonces);
	if (lastnonce == NULL) {
		applog(LOG_ERR, "%"PRIpreprv": failed to allocate lastnonce[%d]", cgpu->proc_repr, ztex->numNonces);
		return -1;
	}

	/* Add an extra slot for detecting dupes that lie around */
	backlog_max = ztex->numNonces * (2 + ztex->extraSolutions);
	backlog = calloc(1, sizeof(uint32_t) * backlog_max);
	if (backlog == NULL) {
		applog(LOG_ERR, "%"PRIpreprv": failed to allocate backlog[%d]", cgpu->proc_repr, backlog_max);
		free(lastnonce);
		return -1;
	}

	overflow = false;
	int count = 0;

	applog(LOG_DEBUG, "%"PRIpreprv": entering poll loop", cgpu->proc_repr);
	while (!(overflow || thr->work_restart)) {
		count++;
		if (!restart_wait(thr, 250))
		{
			applog(LOG_DEBUG, "%"PRIpreprv": New work detected", cgpu->proc_repr);
			break;
		}
		ztex_selectFpga(ztex, cgpu->proc_id);
		i = libztex_readHashData(ztex, &hdata[0]);
		if (i < 0) {
			// Something wrong happened in read
			applog(LOG_ERR, "%"PRIpreprv": Failed to read hash data with err %d, retrying", cgpu->proc_repr, i);
			cgsleep_ms(500);
			i = libztex_readHashData(ztex, &hdata[0]);
			if (i < 0) {
				// And there's nothing we can do about it
				ztex_disable(thr);
				applog(LOG_ERR, "%"PRIpreprv": Failed to read hash data with err %d, giving up", cgpu->proc_repr, i);
				free(lastnonce);
				free(backlog);
				ztex_releaseFpga(ztex);
				return -1;
			}
		}
		ztex_releaseFpga(ztex);

		if (thr->work_restart) {
			applog(LOG_DEBUG, "%"PRIpreprv": New work detected", cgpu->proc_repr);
			break;
		}

		dclk_gotNonces(&ztex->dclk);

		for (i = 0; i < ztex->numNonces; i++) {
			nonce = hdata[i].nonce;
			if (nonce > noncecnt)
				noncecnt = nonce;
			if (((0xffffffff - nonce) < (nonce - lastnonce[i])) || nonce < lastnonce[i]) {
				applog(LOG_DEBUG, "%"PRIpreprv": overflow nonce=%08x lastnonce=%08x", cgpu->proc_repr, nonce, lastnonce[i]);
				overflow = true;
			} else
				lastnonce[i] = nonce;

			if (!ztex_checkNonce(cgpu, work, &hdata[i])) {
				// do not count errors in the first 500ms after sendHashData (2x250 wait time)
				if (count > 2)
					dclk_errorCount(&ztex->dclk, 1.0 / ztex->numNonces);

				inc_hw_errors_only(thr);
			}

			for (j=0; j<=ztex->extraSolutions; j++) {
				nonce = hdata[i].goldenNonce[j];

				if (nonce == ztex->offsNonces) {
					continue;
				}

				found = false;
				for (k = 0; k < backlog_max; k++) {
					if (backlog[k] == nonce) {
						found = true;
						break;
					}
				}
				if (!found) {
					backlog[backlog_p++] = nonce;

					if (backlog_p >= backlog_max)
						backlog_p = 0;

					work->blk.nonce = 0xffffffff;
					if (!j || test_nonce(work, nonce, false))
						submit_nonce(thr, work, nonce);
					applog(LOG_DEBUG, "%"PRIpreprv": submitted %08x (from N%dE%d)", cgpu->proc_repr, nonce, i, j);
				}
			}
		}
	}

	dclk_preUpdate(&ztex->dclk);

	if (!ztex_updateFreq(thr)) {
		// Something really serious happened, so mark this thread as dead!
		free(lastnonce);
		free(backlog);
		
		return -1;
	}

	applog(LOG_DEBUG, "%"PRIpreprv": exit %1.8X", cgpu->proc_repr, noncecnt);

	work->blk.nonce = 0xffffffff;

	free(lastnonce);
	free(backlog);

	return noncecnt;
}

static struct api_data*
get_ztex_drv_extra_device_status(struct cgpu_info *ztex)
{
	struct api_data*root = NULL;
	struct libztex_device *ztexr = ztex->device_ztex;

	if (ztexr) {
		double frequency = ztexr->freqM1 * (ztexr->dclk.freqM + 1);
		root = api_add_freq(root, "Frequency", &frequency, true);
	}

	return root;
}

static bool ztex_prepare(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct libztex_device *ztex = cgpu->device_ztex;

	{
		char *fpganame = malloc(LIBZTEX_SNSTRING_LEN+3+1);
		sprintf(fpganame, "%s-%u", ztex->snString, cgpu->proc_id+1);
		cgpu->name = fpganame;
	}

	ztex_selectFpga(ztex, cgpu->proc_id);
	if (libztex_configureFpga(ztex, cgpu->proc_repr) != 0) {
		libztex_resetFpga(ztex);
		ztex_releaseFpga(ztex);
		applog(LOG_ERR, "%"PRIpreprv": Disabling!", cgpu->proc_repr);
		thr->cgpu->deven = DEV_DISABLED;
		return true;
	}
	ztex->dclk.freqM = ztex->dclk.freqMaxM+1;
	//ztex_updateFreq(thr);
	libztex_setFreq(ztex, ztex->dclk.freqMDefault, cgpu->proc_repr);
	ztex_releaseFpga(ztex);
	notifier_init(thr->work_restart_notifier);
	applog(LOG_DEBUG, "%"PRIpreprv": prepare", cgpu->proc_repr);
	cgpu->status = LIFE_INIT2;
	return true;
}

static void ztex_shutdown(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct libztex_device *ztex = cgpu->device_ztex;
	
	if (!ztex)
		return;
	
	cgpu->device_ztex = NULL;
	applog(LOG_DEBUG, "%"PRIpreprv": shutdown", cgpu->proc_repr);
	if (--ztex->handles)
		return;
	applog(LOG_DEBUG, "%s: No handles remaining, destroying libztex device", cgpu->dev_repr);
	if (ztex->root->numberOfFpgas > 1)
		pthread_mutex_destroy(&ztex->mutex);
	libztex_destroy_device(ztex);
}

static void ztex_disable(struct thr_info *thr)
{
	applog(LOG_ERR, "%"PRIpreprv": Disabling!", thr->cgpu->proc_repr);
	thr->cgpu->deven = DEV_DISABLED;
	ztex_shutdown(thr);
}

struct device_drv ztex_drv = {
	.dname = "ztex",
	.name = "ZTX",
	.lowl_match = ztex_lowl_match,
	.lowl_probe = ztex_lowl_probe,
	.get_api_extra_device_status = get_ztex_drv_extra_device_status,
	.thread_init = ztex_prepare,
	.scanhash = ztex_scanhash,
	.thread_shutdown = ztex_shutdown,
};
