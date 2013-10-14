/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2013 Hashfast
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef HASHFAST_H
#define HASHFAST_H

#ifdef USE_HASHFAST
#include "miner.h"
#include "elist.h"
#include "hf_protocol.h"

#define HASHFAST_MINER_THREADS 1
#define HF_NUM_SEQUENCE 256
#define GWQ_SEQUENCE_DISTANCE(tx,rx)        ((tx)>=(rx)?((tx)-(rx)):(HF_NUM_SEQUENCE+(tx)-(rx)))

// Matching fields for hf_statistics, but large #'s for local accumulation, per-die
struct hf_long_statistics {
	uint64_t rx_header_crc;                     // Header CRC's
	uint64_t rx_body_crc;                       // Data CRC's
	uint64_t rx_header_timeouts;                // Header timeouts
	uint64_t rx_body_timeouts;                  // Data timeouts
	uint64_t core_nonce_fifo_full;              // Core nonce Q overrun events
	uint64_t array_nonce_fifo_full;             // System nonce Q overrun events
	uint64_t stats_overrun;                     // Overrun in statistics reporting
} __attribute__((packed,aligned(4)));

struct hashfast_info {
	int asic_count;                             // # of chips in the chain
	int core_count;                             // # of cores per chip
	int device_type;                            // What sort of device this is
	int ref_frequency;                          // Reference clock rate
	uint16_t hash_sequence;                     // The next hash sequence # to be sent
	struct hf_g1_die_data *die_status;          // Array of per-die voltage, current, temperature sensor data
	struct hf_long_statistics *die_statistics;  // Array of per-die error counters
	int hash_clock_rate;                        // Hash clock rate to use, in Mhz
	struct hf_usb_init_base usb_init_base;      // USB Base information from USB_INIT
	struct hf_config_data config_data;          // Configuration data used from USB_INIT
	int core_bitmap_size;                       // in bytes
	uint32_t *core_bitmap;                      // Core OK bitmap test results, run with PLL Bypassed

	pthread_mutex_t lock;
	struct work **works;
	uint16_t device_sequence_head;              // The most recent sequence number the device dispatched
	uint16_t device_sequence_tail;              // The most recently completed job in the device
	uint16_t hash_sequence_tail;                // Follows device_sequence_tail around to free work
	int64_t hash_count;

	pthread_t read_thr;
};

#endif /* USE_HASHFAST */
#endif	/* HASHFAST_H */
