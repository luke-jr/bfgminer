/*
 * Copyright 2012 nelisky
 * Copyright 2012-2013 Luke Dashjr
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

#include <unistd.h>
#include <sha2.h>

#include "deviceapi.h"
#include "dynclock.h"
#include "fpgautils.h"
#include "miner.h"
#include "libztex.h"

#define GOLDEN_BACKLOG 5

struct device_api ztex_api;

// Forward declarations
static void ztex_disable(struct thr_info* thr);
static bool ztex_prepare(struct thr_info *thr);

static void ztex_selectFpga(struct libztex_device* ztex)
{
	if (ztex->root->numberOfFpgas > 1) {
		if (ztex->root->selectedFpga != ztex->fpgaNum)
			mutex_lock(&ztex->root->mutex);
		libztex_selectFpga(ztex);
	}
}

static void ztex_releaseFpga(struct libztex_device* ztex)
{
	if (ztex->root->numberOfFpgas > 1) {
		ztex->root->selectedFpga = -1;
		mutex_unlock(&ztex->root->mutex);
	}
}

static struct cgpu_info *ztex_setup(struct libztex_device *dev, int j, int fpgacount)
{
	struct cgpu_info *ztex;
	char *fpganame = (char*)dev->snString;

	ztex = calloc(1, sizeof(struct cgpu_info));
	ztex->api = &ztex_api;
	ztex->device_ztex = dev;
	ztex->procs = fpgacount;
	ztex->threads = fpgacount;
	dev->fpgaNum = j;
	ztex->name = fpganame;
	add_cgpu(ztex);
	strcpy(ztex->device_ztex->repr, ztex->proc_repr);
	applog(LOG_INFO, "%"PRIpreprv": Found Ztex (ZTEX %s)", ztex->dev_repr, fpganame);

	return ztex;
}

static int ztex_autodetect(void)
{
	int cnt;
	int i;
	int fpgacount;
	int totaldevs = 0;
	struct libztex_dev_list **ztex_devices;
	struct libztex_device *ztex_master;
	struct cgpu_info *ztex;

	cnt = libztex_scanDevices(&ztex_devices);
	if (cnt > 0)
		applog(LOG_INFO, "Found %d ztex board%s", cnt, cnt > 1 ? "s" : "");

	for (i = 0; i < cnt; i++) {
		ztex_master = ztex_devices[i]->dev;
		ztex_master->root = ztex_master;
		fpgacount = libztex_numberOfFpgas(ztex_master);
		ztex = ztex_setup(ztex_master, 0, fpgacount);

		totaldevs += fpgacount;

		if (fpgacount > 1)
			pthread_mutex_init(&ztex->device_ztex->mutex, NULL);
	}

	if (cnt > 0)
		libztex_freeDevList(ztex_devices);

	return totaldevs;
}

static void ztex_detect()
{
	// This wrapper ensures users can specify -S ztex:noauto to disable it
	noserial_detect(&ztex_api, ztex_autodetect);
}

static bool ztex_change_clock_func(struct thr_info *thr, int bestM)
{
	struct libztex_device *ztex = thr->cgpu->device_ztex;

	ztex_selectFpga(ztex);
	libztex_setFreq(ztex, bestM);
	ztex_releaseFpga(ztex);

	return true;
}

static bool ztex_updateFreq(struct thr_info *thr)
{
	struct libztex_device *ztex = thr->cgpu->device_ztex;
	bool rv = dclk_updateFreq(&ztex->dclk, ztex_change_clock_func, thr);
	if (unlikely(!rv)) {
		ztex_selectFpga(ztex);
		libztex_resetFpga(ztex);
		ztex_releaseFpga(ztex);
	}
	return rv;
}

static bool ztex_checkNonce(struct libztex_device *ztex,
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

	sha2(swap, 80, hash1);
	sha2(hash1, 32, hash2);

	if (be32toh(hash2_32[7]) != ((hdata->hash7 + 0x5be0cd19) & 0xFFFFFFFF)) {
		applog(LOG_DEBUG, "%s: checkNonce failed for %08x", ztex->repr, hdata->nonce);
		return false;
	}
	return true;
}

static int64_t ztex_scanhash(struct thr_info *thr, struct work *work,
                              __maybe_unused int64_t max_nonce)
{
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

	ztex_selectFpga(ztex);
	i = libztex_sendHashData(ztex, sendbuf);
	if (i < 0) {
		// Something wrong happened in send
		applog(LOG_ERR, "%s: Failed to send hash data with err %d, retrying", ztex->repr, i);
		nmsleep(500);
		i = libztex_sendHashData(ztex, sendbuf);
		if (i < 0) {
			// And there's nothing we can do about it
			ztex_disable(thr);
			applog(LOG_ERR, "%s: Failed to send hash data with err %d, giving up", ztex->repr, i);
			ztex_releaseFpga(ztex);
			return -1;
		}
	}
	ztex_releaseFpga(ztex);

	applog(LOG_DEBUG, "%s: sent hashdata", ztex->repr);

	lastnonce = calloc(1, sizeof(uint32_t)*ztex->numNonces);
	if (lastnonce == NULL) {
		applog(LOG_ERR, "%s: failed to allocate lastnonce[%d]", ztex->repr, ztex->numNonces);
		return -1;
	}

	/* Add an extra slot for detecting dupes that lie around */
	backlog_max = ztex->numNonces * (2 + ztex->extraSolutions);
	backlog = calloc(1, sizeof(uint32_t) * backlog_max);
	if (backlog == NULL) {
		applog(LOG_ERR, "%s: failed to allocate backlog[%d]", ztex->repr, backlog_max);
		free(lastnonce);
		return -1;
	}

	overflow = false;
	int count = 0;

	applog(LOG_DEBUG, "%s: entering poll loop", ztex->repr);
	while (!(overflow || thr->work_restart)) {
		count++;
		if (!restart_wait(thr, 250))
		{
			applog(LOG_DEBUG, "%s: New work detected", ztex->repr);
			break;
		}
		ztex_selectFpga(ztex);
		i = libztex_readHashData(ztex, &hdata[0]);
		if (i < 0) {
			// Something wrong happened in read
			applog(LOG_ERR, "%s: Failed to read hash data with err %d, retrying", ztex->repr, i);
			nmsleep(500);
			i = libztex_readHashData(ztex, &hdata[0]);
			if (i < 0) {
				// And there's nothing we can do about it
				ztex_disable(thr);
				applog(LOG_ERR, "%s: Failed to read hash data with err %d, giving up", ztex->repr, i);
				free(lastnonce);
				free(backlog);
				ztex_releaseFpga(ztex);
				return -1;
			}
		}
		ztex_releaseFpga(ztex);

		if (thr->work_restart) {
			applog(LOG_DEBUG, "%s: New work detected", ztex->repr);
			break;
		}

		dclk_gotNonces(&ztex->dclk);

		for (i = 0; i < ztex->numNonces; i++) {
			nonce = hdata[i].nonce;
			if (nonce > noncecnt)
				noncecnt = nonce;
			if (((0xffffffff - nonce) < (nonce - lastnonce[i])) || nonce < lastnonce[i]) {
				applog(LOG_DEBUG, "%s: overflow nonce=%08x lastnonce=%08x", ztex->repr, nonce, lastnonce[i]);
				overflow = true;
			} else
				lastnonce[i] = nonce;

			if (!ztex_checkNonce(ztex, work, &hdata[i])) {
				// do not count errors in the first 500ms after sendHashData (2x250 wait time)
				if (count > 2)
					dclk_errorCount(&ztex->dclk, 1.0 / ztex->numNonces);

				thr->cgpu->hw_errors++;
				++hw_errors;
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
					applog(LOG_DEBUG, "%s: submitted %08x (from N%dE%d)", ztex->repr, nonce, i, j);
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

	applog(LOG_DEBUG, "%s: exit %1.8X", ztex->repr, noncecnt);

	work->blk.nonce = 0xffffffff;

	free(lastnonce);
	free(backlog);

	return noncecnt;
}

static void ztex_statline_before(char *buf, struct cgpu_info *cgpu)
{
	char before[] = "               ";
	if (cgpu->device_ztex) {
		const char *snString = (char*)cgpu->device_ztex->snString;
		size_t snStringLen = strlen(snString);
		if (snStringLen > 14)
			snStringLen = 14;
		memcpy(before, snString, snStringLen);
	}
	tailsprintf(buf, "%s| ", &before[0]);
}

static struct api_data*
get_ztex_api_extra_device_status(struct cgpu_info *ztex)
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
	struct timeval now;
	struct cgpu_info *cgpu = thr->cgpu;
	struct libztex_device *ztex = cgpu->device_ztex;

	gettimeofday(&now, NULL);
	get_datestamp(cgpu->init, &now);

	if (cgpu->proc_id)
	{
		struct libztex_device *ztex_master = cgpu->device->device_ztex;
		ztex = malloc(sizeof(struct libztex_device));
		memcpy(ztex, ztex_master, sizeof(*ztex));
		cgpu->device_ztex = ztex;
		ztex->root = ztex_master;
		ztex->fpgaNum = cgpu->proc_id;
		strcpy(ztex->repr, cgpu->proc_repr);
	}
	
	{
		char *fpganame = malloc(LIBZTEX_SNSTRING_LEN+3+1);
		sprintf(fpganame, "%s-%u", ztex->snString, cgpu->proc_id+1);
		cgpu->name = fpganame;
	}

	ztex_selectFpga(ztex);
	if (libztex_configureFpga(ztex) != 0) {
		libztex_resetFpga(ztex);
		ztex_releaseFpga(ztex);
		applog(LOG_ERR, "%s: Disabling!", thr->cgpu->device_ztex->repr);
		thr->cgpu->deven = DEV_DISABLED;
		return true;
	}
	ztex->dclk.freqM = ztex->dclk.freqMaxM+1;;
	//ztex_updateFreq(thr);
	libztex_setFreq(ztex, ztex->dclk.freqMDefault);
	ztex_releaseFpga(ztex);
	notifier_init(thr->work_restart_notifier);
	applog(LOG_DEBUG, "%s: prepare", ztex->repr);
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
	applog(LOG_DEBUG, "%s: shutdown", ztex->repr);
}

static void ztex_disable(struct thr_info *thr)
{
	applog(LOG_ERR, "%s: Disabling!", thr->cgpu->device_ztex->repr);
	devices[thr->cgpu->device_id]->deven = DEV_DISABLED;
	ztex_shutdown(thr);
}

struct device_api ztex_api = {
	.dname = "ztex",
	.name = "ZTX",
	.api_detect = ztex_detect,
	.get_statline_before = ztex_statline_before,
	.get_api_extra_device_status = get_ztex_api_extra_device_status,
	.thread_init = ztex_prepare,
	.scanhash = ztex_scanhash,
	.thread_shutdown = ztex_shutdown,
};
