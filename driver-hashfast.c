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

static void hf_init_crc8(void)
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

struct hf_cmd {
	int cmd;
	char *cmd_name;
	enum usb_cmds usb_cmd;
};

/* Entries in this array need to align with the actual op values specified
 * in hf_protocol.h */
#define C_NULL C_MAX
static const struct hf_cmd hf_cmds[] = {
	{OP_NULL, "OP_NULL", C_NULL},				// 0
	{OP_ROOT, "OP_ROOT", C_NULL},
	{OP_RESET, "OP_RESET", C_HF_RESET},
	{OP_PLL_CONFIG, "OP_PLL_CONFIG", C_HF_PLL_CONFIG},
	{OP_ADDRESS, "OP_ADDRESS", C_HF_ADDRESS},
	{OP_READDRESS, "OP_READDRESS", C_NULL},
	{OP_HIGHEST, "OP_HIGHEST", C_NULL},
	{OP_BAUD, "OP_BAUD", C_HF_BAUD},
	{OP_UNROOT, "OP_UNROOT", C_NULL},			// 8
	{OP_HASH, "OP_HASH", C_HF_HASH},
	{OP_NONCE, "OP_NONCE", C_HF_NONCE},
	{OP_ABORT, "OP_ABORT", C_HF_ABORT},
	{OP_STATUS, "OP_STATUS", C_HF_STATUS},
	{OP_GPIO, "OP_GPIO", C_NULL},
	{OP_CONFIG, "OP_CONFIG", C_HF_CONFIG},
	{OP_STATISTICS, "OP_STATISTICS", C_HF_STATISTICS},
	{OP_GROUP, "OP_GROUP", C_NULL},				// 16
	{OP_CLOCKGATE, "OP_CLOCKGATE", C_HF_CLOCKGATE},

	{OP_USB_INIT, "OP_USB_INIT", C_HF_USB_INIT},		// 18
	{OP_GET_TRACE, "OP_GET_TRACE", C_NULL},
	{OP_LOOPBACK_USB, "OP_LOOPBACK_USB", C_NULL},
	{OP_LOOPBACK_UART, "OP_LOOPBACK_UART", C_NULL},
	{OP_DFU, "OP_DFU", C_NULL},
	{OP_USB_SHUTDOWN, "OP_USB_SHUTDOWN", C_NULL},
	{OP_DIE_STATUS, "OP_DIE_STATUS", C_HF_DIE_STATUS},	// 24
	{OP_GWQ_STATUS, "OP_GWQ_STATUS", C_HF_GWQ_STATUS},
	{OP_WORK_RESTART, "OP_WORK_RESTART", C_HF_WORK_RESTART},
	{OP_USB_STATS1, "OP_USB_STATS1", C_NULL},
	{OP_USB_GWQSTATS, "OP_USB_GWQSTATS", C_HF_GWQSTATS}
};

#define HF_USB_CMD_OFFSET (128 - 18)
#define HF_USB_CMD(X) (X - HF_USB_CMD_OFFSET)

/* Send an arbitrary frame, consisting of an 8 byte header and an optional
 * packet body. */

static bool hashfast_send_frame(struct cgpu_info *hashfast, uint8_t opcode,
				uint16_t hdata, uint8_t *data, int len)
{
	int tx_length, ret, amount, id = hashfast->device_id;
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;

	p->preamble = HF_PREAMBLE;
	p->operation_code = opcode;
	p->chip_address = HF_GWQ_ADDRESS;
	p->core_address = 0;
	p->hdata = htole16(hdata);
	p->data_length = len / 4;
	p->crc8 = hf_crc8(packet);

	if (len)
		memcpy(&packet[sizeof(struct hf_header)], data, len);
	tx_length = sizeof(struct hf_header) + len;

	ret = usb_write(hashfast, (char *)packet, tx_length, &amount,
			hf_cmds[opcode].usb_cmd);
	if (unlikely(ret < 0 || amount != tx_length)) {
		applog(LOG_ERR, "HFA %d: hashfast_send_frame: USB Send error, ret %d amount %d vs. tx_length %d",
		       id, ret, amount, tx_length);
		return false;
	}
	return true;
}

static bool hashfast_send_header(struct cgpu_info *hashfast, struct hf_header *h,
				 int cmd)
{
	int amount, ret, len;

	len = sizeof(*h);
	ret = usb_write(hashfast, (char *)h, len, &amount, hf_cmds[cmd].usb_cmd);
	if (ret < 0 || amount != len) {
		applog(LOG_WARNING, "HFA%d: send_header: %s USB Send error, ret %d amount %d vs. length %d",
		       hashfast->device_id, hf_cmds[cmd].cmd_name, ret, amount, len);
		return false;
	}
	return true;
}

static bool hashfast_get_header(struct cgpu_info *hashfast, struct hf_header *h,
				uint8_t *computed_crc)
{
	int amount, ret, orig_len, len, ofs = 0, reads = 0;
	char buf[512];
	char *header;

	/* Read for up to 200ms till we find the first occurrence of HF_PREAMBLE
	 * though it should be the first byte unless we get woefully out of
	 * sync. */
	orig_len = len = sizeof(*h);
	do {

		if (++reads > 20)
			return false;

		ret = usb_read_timeout(hashfast, buf + ofs, len, &amount, 10, C_HF_GETHEADER);
		if (unlikely(ret && ret != LIBUSB_ERROR_TIMEOUT))
			return false;
		ofs += amount;
		header = memchr(buf, HF_PREAMBLE, ofs);
		if (header)
			len -= ofs - (header - buf);
	} while (len);

	memcpy(h, header, orig_len);
	*computed_crc = hf_crc8((uint8_t *)h);

	return true;
}

static bool hashfast_get_data(struct cgpu_info *hashfast, char *buf, int len4)
{
	int amount, ret, len = len4 * 4;

	ret = usb_read(hashfast, buf, len, &amount, C_HF_GETDATA);
	if (ret)
		return false;
	if (amount != len) {
		applog(LOG_WARNING, "HFA %d: get_data: Strange amount returned %d vs. expected %d",
		       hashfast->device_id, amount, len);
		return false;
	}
	return true;
}

static bool hashfast_reset(struct cgpu_info *hashfast, struct hashfast_info *info)
{
	struct hf_usb_init_header usb_init, *hu = &usb_init;
	struct hf_usb_init_base *db;
	char buf[1024];
	struct hf_header *h = (struct hf_header *)buf;
	uint8_t hcrc;
	bool ret;
	int i;

	info->hash_clock_rate = 550;                        // Hash clock rate in Mhz
	// Assemble the USB_INIT request
	memset(hu, 0, sizeof(*hu));
	hu->preamble = HF_PREAMBLE;
	hu->operation_code = OP_USB_INIT;
	hu->protocol = PROTOCOL_GLOBAL_WORK_QUEUE;          // Protocol to use
	hu->hash_clock = info->hash_clock_rate;             // Hash clock rate in Mhz
	hu->crc8 = hf_crc8((uint8_t *)hu);
	applog(LOG_WARNING, "HFA%d: Sending OP_USB_INIT with GWQ protocol specified",
	       hashfast->device_id);

	if (!hashfast_send_header(hashfast, (struct hf_header *)hu, HF_USB_CMD(OP_USB_INIT)))
		return false;

	// Check for the correct response.
	// We extend the normal timeout - a complete device initialization, including
	// bringing power supplies up from standby, etc., can take over a second.
	for (i = 0; i < 30; i++) {
		ret = hashfast_get_header(hashfast, h, &hcrc);
		if (ret)
			break;
	}
	if (!ret) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed!", hashfast->device_id);
		return false;
	}
	if (h->crc8 != hcrc) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed! CRC mismatch", hashfast->device_id);
		return false;
	}
	if (h->operation_code != OP_USB_INIT) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT: Tossing packet, valid but unexpected type", hashfast->device_id);
		hashfast_get_data(hashfast, buf, h->data_length);
		return false;
	}

	applog(LOG_DEBUG, "HFA %d: Good reply to OP_USB_INIT", hashfast->device_id);
	applog(LOG_DEBUG, "HFA %d: OP_USB_INIT: %d die in chain, %d cores, device_type %d, refclk %d Mhz",
	       hashfast->device_id, h->chip_address, h->core_address, h->hdata & 0xff, (h->hdata >> 8) & 0xff);

	// Save device configuration
	info->asic_count = h->chip_address;
	info->core_count = h->core_address;
	info->device_type = (uint8_t)h->hdata;
	info->ref_frequency = (uint8_t)(h->hdata>>8);
	info->hash_sequence = 0;
	info->hash_sequence_tail = 0;
	info->device_sequence_tail = 0;

	// Size in bytes of the core bitmap in bytes
	info->core_bitmap_size = (((info->asic_count * info->core_count) + 31) / 32) * 4;

	// Get the usb_init_base structure
	if (!hashfast_get_data(hashfast, (char *)&info->usb_init_base, U32SIZE(info->usb_init_base))) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed! Failure to get usb_init_base data",
		       hashfast->device_id);
		return false;
	}
	db = &info->usb_init_base;
	applog(LOG_INFO, "HFA %d:      firmware_rev:    %d.%d", hashfast->device_id,
	       (db->firmware_rev >> 8) & 0xff, db->firmware_rev & 0xff);
	applog(LOG_INFO, "HFA %d:      hardware_rev:    %d.%d", hashfast->device_id,
	       (db->hardware_rev >> 8) & 0xff, db->hardware_rev & 0xff);
	applog(LOG_INFO, "HFA %d:      serial number:   %d", hashfast->device_id,
	       db->serial_number);
	applog(LOG_INFO, "HFA %d:      hash clockrate:  %d Mhz", hashfast->device_id,
	       db->hash_clockrate);
	applog(LOG_INFO, "HFA %d:      inflight_target: %d", hashfast->device_id,
	       db->inflight_target);

	// Now a copy of the config data used
	if (!hashfast_get_data(hashfast, (char *)&info->config_data, U32SIZE(info->config_data))) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed! Failure to get config_data",
		       hashfast->device_id);
		return false;
	}

	// Now the core bitmap
	info->core_bitmap = malloc(info->core_bitmap_size);
	if (!info->core_bitmap)
		quit(1, "Failed to malloc info core bitmap in hashfast_reset");
	if (!hashfast_get_data(hashfast, (char *)info->core_bitmap, info->core_bitmap_size / 4)) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed! Failure to get core_bitmap", hashfast->device_id);
		return false;
	}

	return true;
}

static bool hashfast_detect_common(struct cgpu_info *hashfast)
{
	struct hashfast_info *info;
	bool ret;

	info = calloc(sizeof(struct hashfast_info), 1);
	if (!info)
		quit(1, "Failed to calloc hashfast_info in hashfast_detect_common");
	hashfast->device_data = info;
	/* hashfast_reset should fill in details for info */
	ret = hashfast_reset(hashfast, info);
	if (!ret) {
		free(info);
		hashfast->device_data = NULL;
		return false;
	}

	// The per-die status array
	info->die_status = calloc(info->asic_count, sizeof(struct hf_g1_die_data));
	if (unlikely(!(info->die_status)))
		quit(1, "Failed to calloc die_status");

	// The per-die statistics array
	info->die_statistics = calloc(info->asic_count, sizeof(struct hf_long_statistics));
	if (unlikely(!(info->die_statistics)))
		quit(1, "Failed to calloc die_statistics");

	info->works = calloc(sizeof(struct work *), HF_NUM_SEQUENCE);
	if (!info->works)
		quit(1, "Failed to calloc info works in hashfast_detect_common");

	return true;
}

static void hashfast_initialise(struct cgpu_info *hashfast)
{
	if (hashfast->usbinfo.nodev)
		return;
	// FIXME Do necessary initialising here
}

static bool hashfast_detect_one_usb(libusb_device *dev, struct usb_find_devices *found)
{
	struct cgpu_info *hashfast;

	hashfast = usb_alloc_cgpu(&hashfast_drv, HASHFAST_MINER_THREADS);
	if (!hashfast)
		quit(1, "Failed to usb_alloc_cgpu hashfast");

	if (!usb_init(hashfast, dev, found)) {
		hashfast = usb_free_cgpu(hashfast);
		return false;
	}

	hashfast->usbdev->usb_type = USB_TYPE_STD;

	hashfast_initialise(hashfast);

	add_cgpu(hashfast);

	return hashfast_detect_common(hashfast);
}

static void hashfast_detect(bool hotplug)
{
	/* Set up the CRC tables only once. */
	if (!hotplug)
		hf_init_crc8();
	usb_detect(&hashfast_drv, hashfast_detect_one_usb);
}

static void *hfa_read(void *arg)
{
	struct thr_info *thr = (struct thr_info *)arg;
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	char threadname[24];

	snprintf(threadname, 24, "hfa_read/%d", hashfast->device_id);
	RenameThread(threadname);

	while (likely(!hashfast->shutdown)) {
	}

	return NULL;
}

static bool hashfast_prepare(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	struct timeval now;

	mutex_init(&info->lock);
	if (pthread_create(&info->read_thr, NULL, hfa_read, (void *)thr))
		quit(1, "Failed to pthread_create read thr in hashfast_prepare");

	cgtime(&now);
	get_datestamp(hashfast->init, sizeof(hashfast->init), &now);

	return true;
}

/* Figure out how many jobs to send. */
static int __hashfast_jobs(struct hashfast_info *info)
{
	return info->usb_init_base.inflight_target - GWQ_SEQUENCE_DISTANCE(info->hash_sequence, info->device_sequence_tail);
}

static int hashfast_jobs(struct hashfast_info *info)
{
	int ret;

	mutex_lock(&info->lock);
	ret = __hashfast_jobs(info);
	mutex_unlock(&info->lock);

	return ret;
}

static int64_t hashfast_scanwork(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	int64_t hashes;
	int jobs, ret;

	if (unlikely(thr->work_restart)) {
restart:
		ret = hashfast_send_frame(hashfast, OP_WORK_RESTART, 0, (uint8_t *)NULL, 0);
		if (unlikely(!ret)) {
			ret = hashfast_reset(hashfast, info);
			if (unlikely(!ret)) {
				applog(LOG_ERR, "HFA %d: Failed to reset after write failure, disabling",
				hashfast->device_id);
				return -1;
			}
		}
	}

	jobs = hashfast_jobs(info);

	if (!jobs) {
		ret = restart_wait(thr, 100);
		if (unlikely(!ret))
			goto restart;
		jobs = hashfast_jobs(info);
	}

	while (jobs > 0) {
		struct hf_hash_usb op_hash_data;
		struct work *work;
		uint64_t intdiff;
		int i, sequence;
		uint32_t *p;

		/* This is a blocking function if there's no work */
		work = get_work(thr, thr->id);

		/* Assemble the data frame and send the OP_HASH packet */
		memcpy(op_hash_data.midstate, work->midstate, sizeof(op_hash_data.midstate));
		memcpy(op_hash_data.merkle_residual, work->data + 64, 4);
		p = (uint32_t *)(work->data + 64 + 4);
		op_hash_data.timestamp = *p++;
		op_hash_data.bits = *p++;
		op_hash_data.nonce_loops = 0;

		/* Set the number of leading zeroes to look for based on diff.
		 * Diff 1 = 32, Diff 2 = 33, Diff 4 = 34 etc. */
		intdiff = (uint64_t)work->device_diff;
		for (i = 31; intdiff; i++, intdiff >>= 1);
		op_hash_data.search_difficulty = i;
		if ((sequence = info->hash_sequence + 1) >= HF_NUM_SEQUENCE)
			sequence = 0;
		ret = hashfast_send_frame(hashfast, OP_HASH, sequence, (uint8_t *)&op_hash_data, sizeof(op_hash_data));
		if (unlikely(!ret)) {
			ret = hashfast_reset(hashfast, info);
			if (unlikely(!ret)) {
				applog(LOG_ERR, "HFA %d: Failed to reset after write failure, disabling",
				       hashfast->device_id);
				return -1;
			}
		}

		mutex_lock(&info->lock);
		info->hash_sequence = sequence;
		*(info->works + info->hash_sequence) = work;
		jobs = __hashfast_jobs(info);
		mutex_unlock(&info->lock);

		applog(LOG_DEBUG, "HFA %d: OP_HASH sequence %d search_difficulty %d work_difficulty %g",
		       hashfast->device_id, info->hash_sequence, op_hash_data.search_difficulty, work->work_difficulty);
	}

	mutex_lock(&info->lock);
	hashes = info->hash_count;
	info->hash_count = 0;
	mutex_unlock(&info->lock);

	return hashes;
}

static struct api_data *hashfast_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void hashfast_init(struct cgpu_info *hashfast)
{
	usb_buffer_enable(hashfast);
}

static void hashfast_shutdown(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;

	pthread_join(info->read_thr, NULL);
}

struct device_drv hashfast_drv = {
	.drv_id = DRIVER_hashfast,
	.dname = "Hashfast",
	.name = "HFA",
	.max_diff = 256.0, // Limit max diff to get some nonces back regardless
	.drv_detect = hashfast_detect,
	.thread_prepare = hashfast_prepare,
	.hash_work = &hash_driver_work,
	.scanwork = hashfast_scanwork,
	.get_api_stats = hashfast_api_stats,
	.reinit_device = hashfast_init,
	.thread_shutdown = hashfast_shutdown,
};
