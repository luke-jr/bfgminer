/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2013 Hashfast Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>

#include "usbutils.h"

#include "driver-hashfast.h"

////////////////////////////////////////////////////////////////////////////////
// Support for the CRC's used in header (CRC-8) and packet body (CRC-32)
////////////////////////////////////////////////////////////////////////////////

#define GP8  0x107   /* x^8 + x^2 + x + 1 */
#define DI8  0x07

static unsigned char crc8_table[256];	/* CRC-8 table */

void hf_init_crc8(void)
{
	int i,j;
	unsigned char crc;

	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 0; j < 8; j++)
			crc = (crc << 1) ^ ((crc & 0x80) ? DI8 : 0);
		crc8_table[i] = crc & 0xFF;
	}
}

static unsigned char __maybe_unused hf_crc8(unsigned char *h)
{
	int i;
	unsigned char crc;

	h++;	// Preamble not included
	for (i = 1, crc = 0xff; i < 7; i++)
		crc = crc8_table[crc ^ *h++];

	return crc;
}

static hf_info_t **hashfast_infos;

struct hf_cmd {
	int cmd;
	char *cmd_name;
	enum usb_cmds usb_cmd;
};

#define C_NULL C_MAX
static const struct hf_cmd hf_cmds[] = {
	{OP_NULL, "OP_NULL", C_NULL},
	{OP_ROOT, "OP_ROOT", C_NULL},
	{OP_RESET, "OP_RESET", C_HF_RESET},
	{OP_PLL_CONFIG, "OP_PLL_CONFIG", C_HF_PLL_CONFIG},
	{OP_ADDRESS, "OP_ADDRESS", C_HF_ADDRESS},
	{OP_READDRESS, "OP_READDRESS", C_NULL},
	{OP_HIGHEST, "OP_HIGHEST", C_NULL},
	{OP_BAUD, "OP_BAUD", C_HF_BAUD},
	{OP_UNROOT, "OP_UNROOT", C_NULL},
	{OP_HASH, "OP_HASH", C_HF_HASH},
	{OP_NONCE, "OP_NONCE", C_HF_NONCE},
	{OP_ABORT, "OP_ABORT", C_HF_ABORT},
	{OP_STATUS, "OP_STATUS", C_HF_STATUS},
	{OP_GPIO, "OP_GPIO", C_NULL},
	{OP_CONFIG, "OP_CONFIG", C_HF_CONFIG},
	{OP_STATISTICS, "OP_STATISTICS", C_HF_STATISTICS},
	{OP_GROUP, "OP_GROUP", C_NULL},
	{OP_CLOCKGATE, "OP_CLOCKGATE", C_HF_CLOCKGATE}
};

/* Send an arbitrary frame, consisting of an 8 byte header and an optional
 * packet body. */

static int __maybe_unused hashfast_send_frame(struct cgpu_info *hashfast, uint8_t opcode,
			       uint8_t chip, uint8_t core, uint16_t hdata,
			       uint8_t *data, int len)
{
	int tx_length, ret, amount, id = hashfast->device_id;
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;

	p->preamble = HF_PREAMBLE;
	p->operation_code = opcode;
	p->chip_address = chip;
	p->core_address = core;
	p->hdata = htole16(hdata);
	p->data_length = len / 4;
	p->crc8 = hf_crc8(packet);

	if (len)
		memcpy(&packet[sizeof(struct hf_header)], data, len);
	tx_length = sizeof(struct hf_header) + len;

	tx_length = sizeof(struct hf_header);

	ret = usb_write(hashfast, (char *)packet, tx_length, &amount,
			hf_cmds[opcode].usb_cmd);
	if (ret < 0 || amount != tx_length) {
		applog(LOG_ERR, "HF%d: hashfast_send_frame: USB Send error, ret %d amount %d vs. tx_length %d",
		       id, ret, amount, tx_length);
		return 1;
	}
	return 0;
}

static int hashfast_reset(struct cgpu_info __maybe_unused *hashfast)
{
	return 0;
}

static bool hashfast_detect_common(struct cgpu_info *hashfast, int baud)
{
	hf_core_t **c, *core;
	hf_info_t *info;
	hf_job_t *j;
	int i, k, ret;

	hashfast_infos = realloc(hashfast_infos, sizeof(hf_info_t *) * (total_devices + 1));
	if (unlikely(!hashfast_infos))
		quit(1, "Failed to realloc hashfast_infos in hashfast_detect_common");
	// Assume success, allocate info ahead of reset, so reset can fill fields in
	info = calloc(sizeof(hf_info_t), 1);
	if (unlikely(!info))
		quit(1, "Failed to calloc info in hashfast_detect_common");
	hashfast_infos[hashfast->device_id] = info;
	info->tacho_enable = 1;
	info->miner_count = 1;
	info->max_search_difficulty = 12;
	info->baud_rate = baud;

	ret = hashfast_reset(hashfast);
	if (unlikely(ret)) {
		free(info);
		hashfast_infos[hashfast->device_id] = NULL;
		return false;
	}

	/* 1 Pending, 1 active for each */
	info->inflight_target = info->asic_count * info->core_count *2;

	switch (info->device_type) {
		default:
		case HFD_G1:
			/* Implies hash_loops = 0 for full nonce range */
			break;
		case HFD_ExpressAGX:
			/* ExpressAGX */
			info->hash_loops = 1 << 26;
			break;
		case HFD_VC709:
			/* Virtex 7
			 * Adjust according to fast or slow configuration */
			if (info->core_count > 5)
				info->hash_loops = 1 << 26;
			else
				info->hash_loops = 1 << 30;
			break;
	}
	applog(LOG_INFO, "Hashfast Detect: chips %d cores %d inflight_target %d entries",
	       info->asic_count, info->core_count, info->inflight_target);

	/* Initialize list heads */
	info->active.next = &info->active;
	info->active.prev = &info->active;
	info->inactive.next = &info->inactive;
	info->inactive.prev = &info->inactive;

	/* Allocate core data structures */
	info->cores = calloc(info->asic_count, sizeof(hf_core_t  *));
	if (unlikely(!info->cores))
		quit(1, "Failed to calloc info cores in hashfast_detect_common");
	c = info->cores;

	for (i = 0; i < info->asic_count; i++) {
		*c = calloc(info->core_count, sizeof(hf_core_t));
		if (unlikely(!*c))
			quit(1, "Failed to calloc hf_core_t in hashfast_detect_common");
		for (k = 0, core = *c; k < info->core_count; k++, core++)
			core->enabled = 1;
		c++;
	}

	/* Now allocate enough structures to hold all the in-flight work
	 * 2 per core - one active and one pending. These go on the inactive
	 * queue, and get used/recycled as required. */
	for (i = 0; i < info->asic_count * info->core_count * 2; i++) {
		j = calloc(sizeof(hf_job_t), 1);
		if (unlikely(!j))
			quit(1, "Failed to calloc hf_job_t in hashfast_detect_common");
		list_add(&info->inactive, &j->l);
	}

	info->inactive_count = info->asic_count * info->core_count * 2;
	applog(LOG_INFO, "Hashfast Detect: Allocated %d job entries",
	       info->inflight_target);

		// Finally, allocate enough space to hold the work array.
	info->max_work = info->inflight_target;
	info->num_sequence = 1024;

	info->work = calloc(info->max_work, sizeof(hf_work_t));
	if (unlikely(!info->work))
		quit(1, "Failed to calloc info work in hashfast_detect_common");
	applog(LOG_INFO, "Hashfast Detect: Allocated space for %d work entries", info->inflight_target);

	return true;
}

static void hashfast_usb_initialise(struct cgpu_info *hashfast)
{
	if (hashfast->usbinfo.nodev)
		return;
	// FIXME Do necessary initialising here
}

static bool hashfast_detect_one_usb(libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *hashfast;
	int baud = DEFAULT_BAUD_RATE;

	hashfast = usb_alloc_cgpu(&hashfast_drv, HASHFAST_MINER_THREADS);
	if (!hashfast)
		return false;

	if (!usb_init(hashfast, dev, found)) {
		free(hashfast->device_data);
		hashfast->device_data = NULL;
		hashfast = usb_free_cgpu(hashfast);
		return false;
	}

	hashfast->usbdev->usb_type = USB_TYPE_STD;
	usb_set_pps(hashfast, HASHFAST_USB_PACKETSIZE);

	hashfast_usb_initialise(hashfast);

	add_cgpu(hashfast);
	return hashfast_detect_common(hashfast, baud);
}

static void hashfast_detect(bool hotplug)
{
	/* Set up the CRC tables only once. */
	if (!hotplug)
		hf_init_crc8();
	usb_detect(&hashfast_drv, hashfast_detect_one_usb);
}

static bool hashfast_prepare(struct thr_info __maybe_unused *thr)
{
	return true;
}

static bool hashfast_fill(struct cgpu_info __maybe_unused *hashfast)
{
	return true;
}

static int64_t hashfast_scanhash(struct thr_info __maybe_unused *thr)
{
	return 0;
}

static struct api_data *hashfast_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void hashfast_init(struct cgpu_info *hashfast)
{
	usb_buffer_enable(hashfast);
}

static void hashfast_shutdown(struct thr_info __maybe_unused *thr)
{
}

struct device_drv hashfast_drv = {
	.drv_id = DRIVER_hashfast,
	.dname = "Hashfast",
	.name = "HFA",
	.drv_detect = hashfast_detect,
	.thread_prepare = hashfast_prepare,
	.hash_work = hash_queued_work,
	.queue_full = hashfast_fill,
	.scanwork = hashfast_scanhash,
	.get_api_stats = hashfast_api_stats,
	.reinit_device = hashfast_init,
	.thread_shutdown = hashfast_shutdown,
};
