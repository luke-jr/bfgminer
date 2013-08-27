/**
 *   bitfury.c - cgminer worker for bitfury chip/board
 *
 *   Copyright (c) 2013 bitfury
 *   Copyright (c) 2013 legkodymov
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

#include "miner.h"
#include <unistd.h>
#include <sha2.h>
#include "libbitfury.h"
#include "util.h"

#define GOLDEN_BACKLOG 5

struct device_drv bitfury_drv;

// Forward declarations
static void bitfury_disable(struct thr_info* thr);
static bool bitfury_prepare(struct thr_info *thr);

static void bitfury_detect(void)
{
	struct cgpu_info *bitfury_info;
	applog(LOG_INFO, "INFO: bitfury_detect");
	libbitfury_detectChips();

	bitfury_info = calloc(1, sizeof(struct cgpu_info));
	bitfury_info->drv = &bitfury_drv;
	bitfury_info->threads = 1;
	add_cgpu(bitfury_info);
}


static uint32_t bitfury_checkNonce(struct work *work, uint32_t nonce)
{
	applog(LOG_INFO, "INFO: bitfury_checkNonce");
}

static int64_t bitfury_scanHash(struct thr_info *thr)
{
	int i;
	unsigned char sendbuf[44];
	unsigned int res[16];
	unsigned char flipped_data[81];
	unsigned char work_hex[161];
	unsigned char *mid_hex;
	unsigned hashMerkleRoot7;
	unsigned ntime, nbits, nnonce;
	static struct work *owork; //old work
	struct work *work;
	int j;

	work = get_queued(thr->cgpu);
	if (work == NULL) {
		return 0;
	}

	flipped_data[80]= '\0';
	swap32yes(flipped_data, work->data, 80 / 4);
	bin2hex(work_hex, flipped_data, 80);

	hashMerkleRoot7 = *(unsigned *)(flipped_data + 64);
	ntime = *(unsigned *)(flipped_data + 68);
	nbits = *(unsigned *)(flipped_data + 72);
	nnonce = *(unsigned *)(flipped_data + 76);
	applog(LOG_INFO, "INFO bitfury_scanHash MS0: %08x, ", ((unsigned int *)work->midstate)[0]);
	applog(LOG_INFO, "INFO merkle[7]: %08x, ntime: %08x, nbits: %08x, nnonce: %08x",
		  hashMerkleRoot7, ntime, nbits, nnonce);

	libbitfury_sendHashData(work->midstate, hashMerkleRoot7, ntime, nbits, nnonce);

	i = libbitfury_readHashData(res);
	for (j = i - 1; j >= 0; j--) {
		if (owork) {
			submit_nonce(thr, owork, bswap_32(res[j]));
		}
	}

	if (owork)
		work_completed(thr->cgpu, owork);

	owork = work;
	return 0xffffffffull * i;
}

static bool bitfury_prepare(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;

	get_now_datestamp(cgpu->init, sizeof(cgpu->init));

	applog(LOG_INFO, "INFO bitfury_prepare");
	return true;
}

static void bitfury_shutdown(struct thr_info *thr)
{
	applog(LOG_INFO, "INFO bitfury_shutdown");
}

static void bitfury_disable(struct thr_info *thr)
{
	applog(LOG_INFO, "INFO bitfury_disable");
}

struct device_drv bitfury_drv = {
	.dname = "bitfury",
	.name = "BITFURY",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.scanwork = bitfury_scanHash,
	.thread_shutdown = bitfury_shutdown,
	.minerloop = hash_queued_work,
};

