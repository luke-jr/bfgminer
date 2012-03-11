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

#include "miner.h"
#include "libztex.h"

#define GOLDEN_BACKLOG 5

struct device_api ztex_api;

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

static bool ztex_prepare(struct thr_info *thr)
{
  struct timeval now;
  struct cgpu_info *ztex = thr->cgpu;

  gettimeofday(&now, NULL);
  get_datestamp(ztex->init, &now);

  if (libztex_configureFpga(ztex->device) != 0) {
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
  libztex_sendHashData(ztex, sendbuf);
  applog(LOG_DEBUG, "sent hashdata");

  for (i=0; i<ztex->numNonces; i++) {
    lastnonce[i] = 0;
  }
  overflow = false;
  while (!(overflow || work_restart[thr->id].restart)) {
    libztex_readHashData(ztex, &hdata[0]);

    for (i=0; i<ztex->numNonces; i++) {
      nonce = hdata[i].nonce;
      if (nonce > noncecnt)
        noncecnt = nonce;
      if ((nonce >> 4) < (lastnonce[i] >> 4))
        overflow = true;
      else
        lastnonce[i] = nonce;
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
#ifdef __BIG_ENDIAN__
          nonce = swab32(nonce);
#endif
	  work->blk.nonce = 0xffffffff;
	  rv = submit_nonce(thr, work, nonce);
          applog(LOG_DEBUG, "submitted %0.8X %d", nonce, rv);
        }
      }
    }

  }

  applog(LOG_DEBUG, "exit %0.8X", noncecnt);
  return noncecnt;
}

static void ztex_shutdown(struct thr_info *thr)
{
  if (thr->cgpu) {
    libztex_destroy_device(thr->cgpu->device);
    thr->cgpu = NULL;
  } 
}

struct device_api ztex_api = {
	.name = "ZTX",
	.api_detect = ztex_detect,
	.thread_prepare = ztex_prepare,
	.scanhash = ztex_scanhash,
	.thread_shutdown = ztex_shutdown,
};

