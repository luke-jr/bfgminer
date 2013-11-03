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

#include "miner.h"
#include "usbutils.h"

#include "driver-hashfast.h"

////////////////////////////////////////////////////////////////////////////////
// Support for the CRC's used in header (CRC-8) and packet body (CRC-32)
////////////////////////////////////////////////////////////////////////////////

#define GP8  0x107   /* x^8 + x^2 + x + 1 */
#define DI8  0x07

static unsigned char crc8_table[256];	/* CRC-8 table */

static void hfa_init_crc8(void)
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

static unsigned char hfa_crc8(unsigned char *h)
{
	int i;
	unsigned char crc;

	h++;	// Preamble not included
	for (i = 1, crc = 0xff; i < 7; i++)
		crc = crc8_table[crc ^ *h++];

	return crc;
}

struct hfa_cmd {
	uint8_t cmd;
	char *cmd_name;
	enum usb_cmds usb_cmd;
};

/* Entries in this array need to align with the actual op values specified
 * in hf_protocol.h */
#define C_NULL C_MAX
static const struct hfa_cmd hfa_cmds[] = {
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

static bool hfa_send_frame(struct cgpu_info *hashfast, uint8_t opcode, uint16_t hdata,
			   uint8_t *data, int len)
{
	int tx_length, ret, amount, id = hashfast->device_id;
	uint8_t packet[256];
	struct hf_header *p = (struct hf_header *)packet;

	p->preamble = HF_PREAMBLE;
	p->operation_code = hfa_cmds[opcode].cmd;
	p->chip_address = HF_GWQ_ADDRESS;
	p->core_address = 0;
	p->hdata = htole16(hdata);
	p->data_length = len / 4;
	p->crc8 = hfa_crc8(packet);

	if (len)
		memcpy(&packet[sizeof(struct hf_header)], data, len);
	tx_length = sizeof(struct hf_header) + len;

	ret = usb_write(hashfast, (char *)packet, tx_length, &amount,
			hfa_cmds[opcode].usb_cmd);
	if (unlikely(ret < 0 || amount != tx_length)) {
		applog(LOG_ERR, "HFA %d: hfa_send_frame: USB Send error, ret %d amount %d vs. tx_length %d",
		       id, ret, amount, tx_length);
		return false;
	}
	return true;
}

static bool hfa_send_header(struct cgpu_info *hashfast, struct hf_header *h, int cmd)
{
	int amount, ret, len;

	len = sizeof(*h);
	ret = usb_write(hashfast, (char *)h, len, &amount, hfa_cmds[cmd].usb_cmd);
	if (ret < 0 || amount != len) {
		applog(LOG_WARNING, "HFA%d: send_header: %s USB Send error, ret %d amount %d vs. length %d",
		       hashfast->device_id, hfa_cmds[cmd].cmd_name, ret, amount, len);
		return false;
	}
	return true;
}

static bool hfa_get_header(struct cgpu_info *hashfast, struct hf_header *h, uint8_t *computed_crc)
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
	*computed_crc = hfa_crc8((uint8_t *)h);

	return true;
}

static bool hfa_get_data(struct cgpu_info *hashfast, char *buf, int len4)
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

static bool hfa_reset(struct cgpu_info *hashfast, struct hashfast_info *info)
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
	hu->crc8 = hfa_crc8((uint8_t *)hu);
	applog(LOG_INFO, "HFA%d: Sending OP_USB_INIT with GWQ protocol specified",
	       hashfast->device_id);

	if (!hfa_send_header(hashfast, (struct hf_header *)hu, HF_USB_CMD(OP_USB_INIT)))
		return false;

	// Check for the correct response.
	// We extend the normal timeout - a complete device initialization, including
	// bringing power supplies up from standby, etc., can take over a second.
	for (i = 0; i < 30; i++) {
		ret = hfa_get_header(hashfast, h, &hcrc);
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
		hfa_get_data(hashfast, buf, h->data_length);
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
	info->hash_sequence_head = 0;
	info->hash_sequence_tail = 0;
	info->device_sequence_tail = 0;

	// Size in bytes of the core bitmap in bytes
	info->core_bitmap_size = (((info->asic_count * info->core_count) + 31) / 32) * 4;

	// Get the usb_init_base structure
	if (!hfa_get_data(hashfast, (char *)&info->usb_init_base, U32SIZE(info->usb_init_base))) {
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
	applog(LOG_INFO, "HFA %d:      sequence_modulus: %d", hashfast->device_id,
	       db->sequence_modulus);
	info->num_sequence = db->sequence_modulus;

	// Now a copy of the config data used
	if (!hfa_get_data(hashfast, (char *)&info->config_data, U32SIZE(info->config_data))) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed! Failure to get config_data",
		       hashfast->device_id);
		return false;
	}

	// Now the core bitmap
	info->core_bitmap = malloc(info->core_bitmap_size);
	if (!info->core_bitmap)
		quit(1, "Failed to malloc info core bitmap in hfa_reset");
	if (!hfa_get_data(hashfast, (char *)info->core_bitmap, info->core_bitmap_size / 4)) {
		applog(LOG_WARNING, "HFA %d: OP_USB_INIT failed! Failure to get core_bitmap", hashfast->device_id);
		return false;
	}

	return true;
}

static void hfa_send_shutdown(struct cgpu_info *hashfast)
{
	hfa_send_frame(hashfast, HF_USB_CMD(OP_USB_SHUTDOWN), 0, NULL, 0);
}

static void hfa_clear_readbuf(struct cgpu_info *hashfast)
{
	int amount, ret;
	char buf[512];

	do {
		ret = usb_read(hashfast, buf, 512, &amount, C_HF_CLEAR_READ);
	} while (!ret || amount);
}

static bool hfa_detect_common(struct cgpu_info *hashfast)
{
	struct hashfast_info *info;
	bool ret;

	info = calloc(sizeof(struct hashfast_info), 1);
	if (!info)
		quit(1, "Failed to calloc hashfast_info in hfa_detect_common");
	hashfast->device_data = info;
	/* hashfast_reset should fill in details for info */
	ret = hfa_reset(hashfast, info);
	if (!ret) {
		hfa_send_shutdown(hashfast);
		hfa_clear_readbuf(hashfast);
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

	info->works = calloc(sizeof(struct work *), info->num_sequence);
	if (!info->works)
		quit(1, "Failed to calloc info works in hfa_detect_common");

	return true;
}

static bool hfa_initialise(struct cgpu_info *hashfast)
{
	int err;

	if (hashfast->usbinfo.nodev)
		return false;

	hfa_clear_readbuf(hashfast);

	err = usb_transfer(hashfast, 0, 9, 1, 0, C_ATMEL_RESET);
	if (!err)
		err = usb_transfer(hashfast, 0x21, 0x22, 0, 0, C_ATMEL_OPEN);
	if (!err) {
		uint32_t buf[2];

		/* Magic sequence to reset device only really needed for windows
		 * but harmless on linux. */
		buf[0] = 0x80250000;
		buf[1] = 0x00000800;
		err = usb_transfer_data(hashfast, 0x21, 0x20, 0x0000, 0, buf,
					7, C_ATMEL_INIT);
	}
	if (err < 0) {
		applog(LOG_INFO, "HFA %d: Failed to open with error %s",
		       hashfast->device_id, libusb_error_name(err));
	}
	/* Must have transmitted init sequence sized buffer */
	return (err == 7);
}

static bool hfa_detect_one_usb(libusb_device *dev, struct usb_find_devices *found)
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

	if (!hfa_initialise(hashfast)) {
		hashfast = usb_free_cgpu(hashfast);
		return false;
	}

	add_cgpu(hashfast);

	return hfa_detect_common(hashfast);
}

static void hfa_detect(bool hotplug)
{
	/* Set up the CRC tables only once. */
	if (!hotplug)
		hfa_init_crc8();
	usb_detect(&hashfast_drv, hfa_detect_one_usb);
}

static bool hfa_get_packet(struct cgpu_info *hashfast, struct hf_header *h)
{
	uint8_t hcrc;
	bool ret;

	ret = hfa_get_header(hashfast, h, &hcrc);
	if (unlikely(!ret))
		goto out;
	if (unlikely(h->crc8 != hcrc)) {
		applog(LOG_WARNING, "HFA %d: Bad CRC %d vs %d, attempting to process anyway",
		       hashfast->device_id, h->crc8, hcrc);
	}
	if (h->data_length > 0)
		ret = hfa_get_data(hashfast, (char *)(h + 1), h->data_length);
	if (unlikely(!ret)) {
		applog(LOG_WARNING, "HFA %d: Failed to get data associated with header",
		       hashfast->device_id);
	}

out:
	return ret;
}

static void hfa_parse_gwq_status(struct cgpu_info *hashfast, struct hashfast_info *info,
				 struct hf_header *h)
{
	struct hf_gwq_data *g = (struct hf_gwq_data *)(h + 1);
	struct work *work;

	applog(LOG_DEBUG, "HFA %d: OP_GWQ_STATUS, device_head %4d tail %4d my tail %4d shed %3d inflight %4d",
		hashfast->device_id, g->sequence_head, g->sequence_tail, info->hash_sequence_tail,
		g->shed_count, HF_SEQUENCE_DISTANCE(info->hash_sequence_head,g->sequence_tail));

	mutex_lock(&info->lock);
	info->hash_count += g->hash_count;
	info->device_sequence_head = g->sequence_head;
	info->device_sequence_tail = g->sequence_tail;
	info->shed_count = g->shed_count;
	/* Free any work that is no longer required */
	while (info->device_sequence_tail != info->hash_sequence_tail) {
		if (++info->hash_sequence_tail >= info->num_sequence)
			info->hash_sequence_tail = 0;
		if (unlikely(!(work = info->works[info->hash_sequence_tail]))) {
			applog(LOG_ERR, "HFA %d: Bad work sequence tail",
			       hashfast->device_id);
			hashfast->shutdown = true;
			break;
		}
		applog(LOG_DEBUG, "HFA %d: Completing work on hash_sequence_tail %d",
		       hashfast->device_id, info->hash_sequence_tail);
		free_work(work);
		info->works[info->hash_sequence_tail] = NULL;
	}
	mutex_unlock(&info->lock);
}

static void hfa_update_die_status(struct cgpu_info *hashfast, struct hashfast_info *info,
				  struct hf_header *h)
{
	struct hf_g1_die_data *d = (struct hf_g1_die_data *)(h + 1), *ds;
	int num_included = (h->data_length * 4) / sizeof(struct hf_g1_die_data);
	int i, j;

	float die_temperature;
	float core_voltage[6];

	if (info->device_type == HFD_G1) {
		// Copy in the data. They're numbered sequentially from the starting point
		ds = info->die_status + h->chip_address;
		for (i = 0; i < num_included; i++)
			memcpy(ds++, d++, sizeof(struct hf_g1_die_data));

		for (i = 0, d = &info->die_status[h->chip_address]; i < num_included; i++, d++) {
			die_temperature = GN_DIE_TEMPERATURE(d->die.die_temperature);
			for (j = 0; j < 6; j++)
				core_voltage[j] = GN_CORE_VOLTAGE(d->die.core_voltage[j]);

			applog(LOG_DEBUG, "HFA %d: die %2d: OP_DIE_STATUS Die temp %.2fC vdd's %.2f %.2f %.2f %.2f %.2f %.2f",
			       hashfast->device_id, h->chip_address + i, die_temperature,
			       core_voltage[0], core_voltage[1], core_voltage[2],
			       core_voltage[3], core_voltage[4], core_voltage[5]);
			// XXX Convert board phase currents, voltage, temperature
		}
	}
}

static void search_for_extra_nonce(struct thr_info *thr, struct work *work,
				   struct hf_candidate_nonce *n)
{
	uint32_t nonce = n->nonce;
	int i;

	/* No function to test with ntime offsets yet */
	if (n->ntime & HF_NTIME_MASK)
		return;
	for (i = 0; i < 128; i++, nonce++) {
		/* We could break out of this early if nonce wraps or if we
		 * find one correct nonce since the chance of more is extremely
		 * low but this function will be hit so infrequently we may as
		 * well test the entire range with the least code. */
		if (test_nonce(work, nonce))
			submit_tested_work(thr, work);
	}
}

static void hfa_parse_nonce(struct thr_info *thr, struct cgpu_info *hashfast,
			    struct hashfast_info *info, struct hf_header *h)
{
	struct hf_candidate_nonce *n = (struct hf_candidate_nonce *)(h + 1);
	int i, num_nonces = h->data_length / U32SIZE(sizeof(struct hf_candidate_nonce));

	applog(LOG_DEBUG, "HFA %d: OP_NONCE: %2d:, num_nonces %d hdata 0x%04x",
	       hashfast->device_id, h->chip_address, num_nonces, h->hdata);
	for (i = 0; i < num_nonces; i++, n++) {
		struct work *work;

		applog(LOG_DEBUG, "HFA %d: OP_NONCE: %2d: %2d: ntime %2d sequence %4d nonce 0x%08x",
		       hashfast->device_id, h->chip_address, i, n->ntime & HF_NTIME_MASK, n->sequence, n->nonce);

		// Find the job from the sequence number
		mutex_lock(&info->lock);
		work = info->works[n->sequence];
		mutex_unlock(&info->lock);

		if (unlikely(!work)) {
			info->no_matching_work++;
			applog(LOG_INFO, "HFA %d: No matching work!", hashfast->device_id);
		} else {
			applog(LOG_DEBUG, "HFA %d: OP_NONCE: sequence %d: submitting nonce 0x%08x ntime %d",
			       hashfast->device_id, n->sequence, n->nonce, n->ntime & HF_NTIME_MASK);
			if ((n->nonce & 0xffff0000) == 0x42420000)		// XXX REMOVE THIS
				break;						// XXX PHONEY EMULATOR NONCE
			submit_noffset_nonce(thr, work, n->nonce, n->ntime & HF_NTIME_MASK);	// XXX Return value from submit_nonce is error if set
			if (unlikely(n->ntime & HF_NONCE_SEARCH)) {
				/* This tells us there is another share in the
				 * next 128 nonces */
				applog(LOG_DEBUG, "HFA %d: OP_NONCE: SEARCH PROXIMITY EVENT FOUND",
				       hashfast->device_id);
				search_for_extra_nonce(thr, work, n);
			}
		}
	}
}

static void hfa_update_die_statistics(struct hashfast_info *info, struct hf_header *h)
{
	struct hf_statistics *s = (struct hf_statistics *)(h + 1);
	struct hf_long_statistics *l;

	// Accumulate the data
	l = info->die_statistics + h->chip_address;

	l->rx_header_crc += s->rx_header_crc;
	l->rx_body_crc += s->rx_body_crc;
	l->rx_header_timeouts += s->rx_header_timeouts;
	l->rx_body_timeouts += s->rx_body_timeouts;
	l->core_nonce_fifo_full += s->core_nonce_fifo_full;
	l->array_nonce_fifo_full += s->array_nonce_fifo_full;
	l->stats_overrun += s->stats_overrun;
}

static void hfa_update_stats1(struct cgpu_info *hashfast, struct hashfast_info *info,
			      struct hf_header *h)
{
	struct hf_long_usb_stats1 *s1 = &info->stats1;
	struct hf_usb_stats1 *sd = (struct hf_usb_stats1 *)(h + 1);

	s1->usb_rx_preambles += sd->usb_rx_preambles;
	s1->usb_rx_receive_byte_errors += sd->usb_rx_receive_byte_errors;
	s1->usb_rx_bad_hcrc += sd->usb_rx_bad_hcrc;

	s1->usb_tx_attempts += sd->usb_tx_attempts;
	s1->usb_tx_packets += sd->usb_tx_packets;
	s1->usb_tx_timeouts += sd->usb_tx_timeouts;
	s1->usb_tx_incompletes += sd->usb_tx_incompletes;
	s1->usb_tx_endpointstalled += sd->usb_tx_endpointstalled;
	s1->usb_tx_disconnected += sd->usb_tx_disconnected;
	s1->usb_tx_suspended += sd->usb_tx_suspended;
#if 0
	/* We don't care about UART stats so they're not in our struct */
	s1->uart_tx_queue_dma += sd->uart_tx_queue_dma;
	s1->uart_tx_interrupts += sd->uart_tx_interrupts;

	s1->uart_rx_preamble_ints += sd->uart_rx_preamble_ints;
	s1->uart_rx_missed_preamble_ints += sd->uart_rx_missed_preamble_ints;
	s1->uart_rx_header_done += sd->uart_rx_header_done;
	s1->uart_rx_data_done += sd->uart_rx_data_done;
	s1->uart_rx_bad_hcrc += sd->uart_rx_bad_hcrc;
	s1->uart_rx_bad_dma += sd->uart_rx_bad_dma;
	s1->uart_rx_short_dma += sd->uart_rx_short_dma;
	s1->uart_rx_buffers_full += sd->uart_rx_buffers_full;
#endif
	if (sd->max_tx_buffers >  s1->max_tx_buffers)
		s1->max_tx_buffers = sd->max_tx_buffers;
	if (sd->max_rx_buffers >  s1->max_rx_buffers)
		s1->max_rx_buffers = sd->max_rx_buffers;

	applog(LOG_DEBUG, "HFA %d: OP_USB_STATS1:", hashfast->device_id);
	applog(LOG_DEBUG, "      usb_rx_preambles:             %6d", sd->usb_rx_preambles);
	applog(LOG_DEBUG, "      usb_rx_receive_byte_errors:   %6d", sd->usb_rx_receive_byte_errors);
	applog(LOG_DEBUG, "      usb_rx_bad_hcrc:              %6d", sd->usb_rx_bad_hcrc);

	applog(LOG_DEBUG, "      usb_tx_attempts:              %6d", sd->usb_tx_attempts);
	applog(LOG_DEBUG, "      usb_tx_packets:               %6d", sd->usb_tx_packets);
	applog(LOG_DEBUG, "      usb_tx_timeouts:              %6d", sd->usb_tx_timeouts);
	applog(LOG_DEBUG, "      usb_tx_incompletes:           %6d", sd->usb_tx_incompletes);
	applog(LOG_DEBUG, "      usb_tx_endpointstalled:       %6d", sd->usb_tx_endpointstalled);
	applog(LOG_DEBUG, "      usb_tx_disconnected:          %6d", sd->usb_tx_disconnected);
	applog(LOG_DEBUG, "      usb_tx_suspended:             %6d", sd->usb_tx_suspended);
#if 0
	applog(LOG_DEBUG, "      uart_tx_queue_dma:            %6d", sd->uart_tx_queue_dma);
	applog(LOG_DEBUG, "      uart_tx_interrupts:           %6d", sd->uart_tx_interrupts);

	applog(LOG_DEBUG, "      uart_rx_preamble_ints:        %6d", sd->uart_rx_preamble_ints);
	applog(LOG_DEBUG, "      uart_rx_missed_preamble_ints: %6d", sd->uart_rx_missed_preamble_ints);
	applog(LOG_DEBUG, "      uart_rx_header_done:          %6d", sd->uart_rx_header_done);
	applog(LOG_DEBUG, "      uart_rx_data_done:            %6d", sd->uart_rx_data_done);
	applog(LOG_DEBUG, "      uart_rx_bad_hcrc:             %6d", sd->uart_rx_bad_hcrc);
	applog(LOG_DEBUG, "      uart_rx_bad_dma:              %6d", sd->uart_rx_bad_dma);
	applog(LOG_DEBUG, "      uart_rx_short_dma:            %6d", sd->uart_rx_short_dma);
	applog(LOG_DEBUG, "      uart_rx_buffers_full:         %6d", sd->uart_rx_buffers_full);
#endif
	applog(LOG_DEBUG, "      max_tx_buffers:               %6d", sd->max_tx_buffers);
	applog(LOG_DEBUG, "      max_rx_buffers:               %6d", sd->max_rx_buffers);

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
		char buf[512];
		struct hf_header *h = (struct hf_header *)buf;
		bool ret = hfa_get_packet(hashfast, h);

		if (unlikely(!ret))
			continue;

		switch (h->operation_code) {
			case OP_GWQ_STATUS:
				hfa_parse_gwq_status(hashfast, info, h);
				break;
			case OP_DIE_STATUS:
				hfa_update_die_status(hashfast, info, h);
				break;
			case OP_NONCE:
				hfa_parse_nonce(thr, hashfast, info, h);
				break;
			case OP_STATISTICS:
				hfa_update_die_statistics(info, h);
				break;
			case OP_USB_STATS1:
				hfa_update_stats1(hashfast, info, h);
				break;
			default:
				applog(LOG_WARNING, "HFA %d: Unhandled operation code %d",
				       hashfast->device_id, h->operation_code);
				break;
		}
	}

	return NULL;
}

static bool hfa_prepare(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	struct timeval now;

	mutex_init(&info->lock);
	if (pthread_create(&info->read_thr, NULL, hfa_read, (void *)thr))
		quit(1, "Failed to pthread_create read thr in hfa_prepare");

	cgtime(&now);
	get_datestamp(hashfast->init, sizeof(hashfast->init), &now);

	return true;
}

/* Figure out how many jobs to send. */
static int hfa_jobs(struct hashfast_info *info)
{
	int ret;

	mutex_lock(&info->lock);
	ret = info->usb_init_base.inflight_target - HF_SEQUENCE_DISTANCE(info->hash_sequence_head, info->device_sequence_tail);
	/* Place an upper limit on how many jobs to queue to prevent sending
	 * more  work than the device can use after a period of outage. */
	if (ret > info->usb_init_base.inflight_target)
		ret = info->usb_init_base.inflight_target;
	mutex_unlock(&info->lock);

	return ret;
}

static int64_t hfa_scanwork(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;
	int64_t hashes;
	int jobs, ret;

	if (unlikely(hashfast->usbinfo.nodev)) {
		applog(LOG_WARNING, "HFA %d: device disappeared, disabling",
		       hashfast->device_id);
		return -1;
	}

	if (unlikely(thr->work_restart)) {
restart:
		ret = hfa_send_frame(hashfast, HF_USB_CMD(OP_WORK_RESTART), 0, (uint8_t *)NULL, 0);
		if (unlikely(!ret)) {
			ret = hfa_reset(hashfast, info);
			if (unlikely(!ret)) {
				applog(LOG_ERR, "HFA %d: Failed to reset after write failure, disabling",
				hashfast->device_id);
				return -1;
			}
		}
	}

	jobs = hfa_jobs(info);

	if (!jobs) {
		ret = restart_wait(thr, 100);
		if (unlikely(!ret))
			goto restart;
		jobs = hfa_jobs(info);
	}

	while (jobs-- > 0) {
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
		if ((sequence = info->hash_sequence_head + 1) >= info->num_sequence)
			sequence = 0;
		ret = hfa_send_frame(hashfast, OP_HASH, sequence, (uint8_t *)&op_hash_data, sizeof(op_hash_data));
		if (unlikely(!ret)) {
			ret = hfa_reset(hashfast, info);
			if (unlikely(!ret)) {
				applog(LOG_ERR, "HFA %d: Failed to reset after write failure, disabling",
				       hashfast->device_id);
				return -1;
			}
		}

		mutex_lock(&info->lock);
		info->hash_sequence_head = sequence;
		info->works[info->hash_sequence_head] = work;
		mutex_unlock(&info->lock);

		applog(LOG_DEBUG, "HFA %d: OP_HASH sequence %d search_difficulty %d work_difficulty %g",
		       hashfast->device_id, info->hash_sequence_head, op_hash_data.search_difficulty, work->work_difficulty);
	}

	mutex_lock(&info->lock);
	hashes = info->hash_count;
	info->hash_count = 0;
	mutex_unlock(&info->lock);

	return hashes;
}

static struct api_data *hfa_api_stats(struct cgpu_info *cgpu)
{
	struct hashfast_info *info = cgpu->device_data;
	struct hf_long_usb_stats1 *s1;
	struct api_data *root = NULL;
	struct hf_usb_init_base *db;
	int varint, i;
	char buf[64];

	root = api_add_int(root, "asic count", &info->asic_count, false);
	root = api_add_int(root, "core count", &info->core_count, false);

	db = &info->usb_init_base;
	sprintf(buf, "%d.%d", (db->firmware_rev >> 8) & 0xff, db->firmware_rev & 0xff);
	root = api_add_string(root, "firmware rev", buf, true);
	sprintf(buf, "%d.%d", (db->hardware_rev >> 8) & 0xff, db->hardware_rev & 0xff);
	root = api_add_string(root, "hardware rev", buf, true);
	varint = db->serial_number;
	root = api_add_int(root, "serial number", &varint, true);
	varint = db->hash_clockrate;
	root = api_add_int(root, "hash clockrate", &varint, true);
	varint = db->inflight_target;
	root = api_add_int(root, "inflight target", &varint, true);
	varint = db->sequence_modulus;
	root = api_add_int(root, "sequence modules", &varint, true);

	s1 = &info->stats1;
	root = api_add_uint64(root, "rx preambles", &s1->usb_rx_preambles, false);
	root = api_add_uint64(root, "rx rcv byte err", &s1->usb_rx_receive_byte_errors, false);
	root = api_add_uint64(root, "rx bad hcrc", &s1->usb_rx_bad_hcrc, false);
	root = api_add_uint64(root, "tx attempts", &s1->usb_tx_attempts, false);
	root = api_add_uint64(root, "tx packets", &s1->usb_tx_packets, false);
	root = api_add_uint64(root, "tx incompletes", &s1->usb_tx_incompletes, false);
	root = api_add_uint64(root, "tx ep stalled", &s1->usb_tx_endpointstalled, false);
	root = api_add_uint64(root, "tx disconnect", &s1->usb_tx_disconnected, false);
	root = api_add_uint64(root, "tx suspend", &s1->usb_tx_suspended, false);
	varint = s1->max_tx_buffers;
	root = api_add_int(root, "max tx buf", &varint, true);
	varint = s1->max_rx_buffers;
	root = api_add_int(root, "max rx buf", &varint, true);

	for (i = 0; i < info->asic_count; i++) {
		struct hf_long_statistics *l = &info->die_statistics[i];
		struct hf_g1_die_data *d = &info->die_status[i];
		double die_temp, core_voltage;
		int j;

		root = api_add_int(root, "Core", &i, true);
		die_temp = GN_DIE_TEMPERATURE(d->die.die_temperature);
		root = api_add_double(root, "die temperature", &die_temp, true);
		for (j = 0; j < 6; j++) {
			core_voltage = GN_CORE_VOLTAGE(d->die.core_voltage[j]);
			sprintf(buf, "%d: %.2f", j, core_voltage);
			root = api_add_string(root, "core voltage", buf, true);
		}
		root = api_add_uint64(root, "rx header crc", &l->rx_header_crc, false);
		root = api_add_uint64(root, "rx body crc", &l->rx_body_crc, false);
		root = api_add_uint64(root, "rx header to", &l->rx_header_timeouts, false);
		root = api_add_uint64(root, "rx body to", &l->rx_body_timeouts, false);
		root = api_add_uint64(root, "cn fifo full", &l->core_nonce_fifo_full, false);
		root = api_add_uint64(root, "an fifo full", &l->array_nonce_fifo_full, false);
		root = api_add_uint64(root, "stats overrun", &l->stats_overrun, false);
	}

	return root;
}

static void hfa_statline_before(char *buf, size_t bufsiz, struct cgpu_info *hashfast)
{
	struct hashfast_info *info = hashfast->device_data;
	double max_temp, max_volt;
	struct hf_g1_die_data *d;
	int i;

	max_temp = max_volt = 0.0;

	for (i = 0; i < info->asic_count; i++) {
		double temp;
		int j;

		d = &info->die_status[i];
		temp = GN_DIE_TEMPERATURE(d->die.die_temperature);
		if (temp > max_temp)
			max_temp = temp;
		for (j = 0; j < 6; j++) {
			double volt = GN_CORE_VOLTAGE(d->die.core_voltage[j]);

			if (volt > max_volt)
				max_volt = volt;
		}
	}

	tailsprintf(buf, bufsiz, " max%3.0fC %3.2fV | ", max_temp, max_volt);
}

static void hfa_init(struct cgpu_info __maybe_unused *hashfast)
{
}

static void hfa_free_all_work(struct hashfast_info *info)
{
	while (info->device_sequence_tail != info->hash_sequence_head) {
		struct work *work;

		if (++info->hash_sequence_tail >= info->num_sequence)
			info->hash_sequence_tail = 0;
		if (unlikely(!(work = info->works[info->hash_sequence_tail])))
			break;
		free_work(work);
		info->works[info->hash_sequence_tail] = NULL;
	}
}

static void hfa_shutdown(struct thr_info *thr)
{
	struct cgpu_info *hashfast = thr->cgpu;
	struct hashfast_info *info = hashfast->device_data;

	hfa_send_shutdown(hashfast);
	pthread_join(info->read_thr, NULL);
	hfa_free_all_work(info);
	hfa_clear_readbuf(hashfast);
	free(info->works);
	free(info->die_statistics);
	free(info->die_status);
	free(info);
}

struct device_drv hashfast_drv = {
	.drv_id = DRIVER_hashfast,
	.dname = "Hashfast",
	.name = "HFA",
	.max_diff = 256.0, // Limit max diff to get some nonces back regardless
	.drv_detect = hfa_detect,
	.thread_prepare = hfa_prepare,
	.hash_work = &hash_driver_work,
	.scanwork = hfa_scanwork,
	.get_api_stats = hfa_api_stats,
	.get_statline_before = hfa_statline_before,
	.reinit_device = hfa_init,
	.thread_shutdown = hfa_shutdown,
};
