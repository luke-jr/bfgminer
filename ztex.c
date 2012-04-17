/**
 *   ztex.c - cgminer worker for Ztex 1.15x fpga board
 *
 *   Copyright (c) 2012 nelisky.btc@gmail.com
 *
 *   This work is based upon the Java SDK provided by ztex which is
 *   Copyright (C) 2009-2011 ZTEX GmbH.
 *   http://www.ztex.de
 *
 *   This work is based upon the icarus.c worker which is
 *   Copyright 2012 Luke Dashjr
 *   Copyright 2012 Xiangfu <xiangfu@openmobilefree.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see http://www.gnu.org/licenses/.
**/
#include <unistd.h>
#include <sha2.h>
#include "miner.h"
#include "libztex.h"

#define GOLDEN_BACKLOG 5

struct device_api ztex_api, ztex_hotplug_api;

// Forward declarations
static void ztex_disable (struct thr_info* thr);
static bool ztex_prepare(struct thr_info *thr);

static void ztex_detect()
{
	int cnt;
	int i;
	struct libztex_dev_list **ztex_devices;

	cnt = libztex_scanDevices(&ztex_devices);
	applog(LOG_WARNING, "Found %d ztex board(s)", cnt);

	for (i=0; i<cnt; i++) {
		if (total_devices == MAX_DEVICES)
			break;
		struct cgpu_info *ztex;
		ztex = calloc(1, sizeof(struct cgpu_info));
		ztex->api = &ztex_api;
		ztex->device_id = total_devices;
		ztex->device = ztex_devices[i]->dev;
		ztex->threads = 1;
		devices[total_devices++] = ztex;

		applog(LOG_WARNING,"%s: Found Ztex, mark as %d", ztex->device->repr, ztex->device_id);
	}

	if (cnt > 0) {
		libztex_freeDevList(ztex_devices);
	}

}

static bool ztex_updateFreq (struct libztex_device* ztex)
{
	int i, maxM, bestM;
	double bestR, r;

	for (i=0; i<ztex->freqMaxM; i++) {
		if (ztex->maxErrorRate[i+1]*i < ztex->maxErrorRate[i]*(i+20))
			ztex->maxErrorRate[i+1] = ztex->maxErrorRate[i]*(1.0+20.0/i);
	}

	maxM = 0;
	while (maxM<ztex->freqMDefault && ztex->maxErrorRate[maxM+1]<LIBZTEX_MAXMAXERRORRATE)
		maxM++;
	while (maxM<ztex->freqMaxM && ztex->errorWeight[maxM]>150 && ztex->maxErrorRate[maxM+1]<LIBZTEX_MAXMAXERRORRATE)
		maxM++;

	bestM = 0;
	bestR = 0;
	for (i=0; i<=maxM; i++) {
		r = (i + 1 + ( i == ztex->freqM ? LIBZTEX_ERRORHYSTERESIS : 0))*(1-ztex->maxErrorRate[i]);
		if (r > bestR) {
			bestM = i;
			bestR = r;
		}
	}

	if (bestM != ztex->freqM) {
		libztex_setFreq(ztex, bestM);
	}

	maxM = ztex->freqMDefault;
	while (maxM<ztex->freqMaxM && ztex->errorWeight[maxM+1] > 100)
		maxM++;
	if ((bestM < (1.0-LIBZTEX_OVERHEATTHRESHOLD) * maxM) && bestM < maxM - 1) {
		libztex_resetFpga(ztex);
		applog(LOG_ERR, "%s: frequency drop of %.1f%% detect. This may be caused by overheating. FPGA is shut down to prevent damage.", ztex->repr, (1.0-1.0*bestM/maxM)*100);
		return false;
	}
	return true;
}


static bool ztex_checkNonce (struct libztex_device *ztex,
                             struct work *work,
                             struct libztex_hash_data *hdata)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[128];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	unsigned char hash2[32];
	uint32_t *hash2_32 = (uint32_t *)hash2;
	int i;

#if defined(__BIGENDIAN__) || defined(MIPSEB)
	hdata->nonce = swab32(hdata->nonce);
	hdata->hash7 = swab32(hdata->hash7);
#endif

	work->data[64 + 12 + 0] = (hdata->nonce >> 0) & 0xff;
	work->data[64 + 12 + 1] = (hdata->nonce >> 8) & 0xff;
	work->data[64 + 12 + 2] = (hdata->nonce >> 16) & 0xff;
	work->data[64 + 12 + 3] = (hdata->nonce >> 24) & 0xff;

	for (i = 0; i < 80 / 4; i++)
		swap32[i] = swab32(data32[i]);
	
	sha2(swap, 80, hash1, false);
	sha2(hash1, 32, hash2, false);
#if defined(__BIGENDIAN__) || defined(MIPSEB)
	if (hash2_32[7] != ((hdata->hash7 + 0x5be0cd19) & 0xFFFFFFFF)) {
#else
	if (swab32(hash2_32[7]) != ((hdata->hash7 + 0x5be0cd19) & 0xFFFFFFFF)) {
#endif
		ztex->errorCount[ztex->freqM] += 1.0/ztex->numNonces;
		applog(LOG_DEBUG, "%s: checkNonce failed for %0.8X", ztex->repr, hdata->nonce);
		return false;
	}
	return true;
}

static uint64_t ztex_scanhash(struct thr_info *thr, struct work *work,
                              __maybe_unused uint64_t max_nonce)
{
	struct libztex_device *ztex;
	unsigned char sendbuf[44];
	int i, j;
	uint32_t backlog[GOLDEN_BACKLOG];
	int backlog_p = 0;
	uint32_t lastnonce[GOLDEN_BACKLOG], nonce, noncecnt = 0;
	bool overflow, found, rv;
	struct libztex_hash_data hdata[GOLDEN_BACKLOG];

	ztex = thr->cgpu->device;

	memcpy(sendbuf, work->data + 64, 12);
	memcpy(sendbuf+12, work->midstate, 32);
	memset(backlog, 0, sizeof(backlog));
	i = libztex_sendHashData(ztex, sendbuf);
	if (i < 0) {
		// Something wrong happened in send
		applog(LOG_ERR, "%s: Failed to send hash data with err %d", ztex->repr, i);
		usleep(500000);
		i = libztex_sendHashData(ztex, sendbuf);
		if (i < 0) {
			// And there's nothing we can do about it
			ztex_disable(thr);
			return 0;
		}
	}
	
	applog(LOG_DEBUG, "sent hashdata");

	for (i=0; i<ztex->numNonces; i++) {
		lastnonce[i] = 0;
	}
	overflow = false;

	while (!(overflow || work_restart[thr->id].restart)) {
		usleep(250000);
		if (work_restart[thr->id].restart) {
			break;
		}
		i = libztex_readHashData(ztex, &hdata[0]);
		if (i < 0) {
			// Something wrong happened in read
			applog(LOG_ERR, "%s: Failed to read hash data with err %d", ztex->repr, i);
			usleep(500000);
			i = libztex_readHashData(ztex, &hdata[0]);
			if (i < 0) {
				// And there's nothing we can do about it
				ztex_disable(thr);
				return 0;
			}
		}

		if (work_restart[thr->id].restart) {
			break;
		}

		ztex->errorCount[ztex->freqM] *= 0.995;
		ztex->errorWeight[ztex->freqM] = ztex->errorWeight[ztex->freqM] * 0.995 + 1.0;
 
		for (i=0; i<ztex->numNonces; i++) {
			nonce = swab32(hdata[i].nonce);
			if (nonce > noncecnt)
				noncecnt = nonce;
			if ((nonce >> 4) < (lastnonce[i] >> 4)) {
				overflow = true;
			} else {
				lastnonce[i] = nonce;
			}
			if (!ztex_checkNonce(ztex, work, &hdata[i])) {
				thr->cgpu->hw_errors++;
				continue;
			}
			nonce = hdata[i].goldenNonce;
			if (nonce > 0) {
				found = false;
				for (j=0; j<GOLDEN_BACKLOG; j++) {
					if (backlog[j] == nonce) {
						found = true;
						break;
					}
				}
				if (!found) {
					// new nonce found!
					backlog[backlog_p++] = nonce;
					if (backlog_p >= GOLDEN_BACKLOG) {
						backlog_p = 0;
					}
#if defined(__BIGENDIAN__) || defined(MIPSEB)
					nonce = swab32(nonce);
#endif
		work->blk.nonce = 0xffffffff;
		rv = submit_nonce(thr, work, nonce);
					applog(LOG_DEBUG, "submitted %0.8X %d", nonce, rv);
				}
			}

		}

	}

	ztex->errorRate[ztex->freqM] = ztex->errorCount[ztex->freqM] /	ztex->errorWeight[ztex->freqM] * (ztex->errorWeight[ztex->freqM]<100 ? ztex->errorWeight[ztex->freqM]*0.01 : 1.0);
	if (ztex->errorRate[ztex->freqM] > ztex->maxErrorRate[ztex->freqM]) {
		ztex->maxErrorRate[ztex->freqM] = ztex->errorRate[ztex->freqM];
	}

	if (!ztex_updateFreq(ztex)) {
		// Something really serious happened, so mark this thread as dead!
		return 0;
	}
	applog(LOG_DEBUG, "exit %1.8X", noncecnt);

	work->blk.nonce = 0xffffffff;

	return noncecnt > 0 ? noncecnt : 1;
}

static void ztex_statline_before(char *buf, struct cgpu_info *cgpu)
{
	if (cgpu->deven == DEV_ENABLED) {
		tailsprintf(buf, "%s | ", cgpu->device->snString);
		tailsprintf(buf, "%0.2fMhz | ", cgpu->device->freqM1 * (cgpu->device->freqM + 1));
	}
}

static bool ztex_prepare(struct thr_info *thr)
{
	struct timeval now;
	struct cgpu_info *ztex = thr->cgpu;

	gettimeofday(&now, NULL);
	get_datestamp(ztex->init, &now);

	if (libztex_configureFpga(ztex->device) != 0) {
		return false;
	}
	ztex->device->freqM = -1;
	ztex_updateFreq(ztex->device);

	return true;
}

static void ztex_shutdown(struct thr_info *thr)
{
	if (thr->cgpu->device != NULL) {
		libztex_destroy_device(thr->cgpu->device);
		thr->cgpu->device = NULL;
	}
}

static void ztex_disable (struct thr_info *thr)
{
	applog(LOG_ERR, "%s: Disabling!", thr->cgpu->device->repr);
	devices[thr->cgpu->device_id]->deven = DEV_DISABLED;
	ztex_shutdown(thr);
}

struct device_api ztex_api = {
	.name = "ZTX",
	.api_detect = ztex_detect,
	.get_statline_before = ztex_statline_before,
	.thread_prepare = ztex_prepare,
	.scanhash = ztex_scanhash,
	.thread_shutdown = ztex_shutdown,
};

