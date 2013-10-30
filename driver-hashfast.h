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

// Matching fields for hf_statistics, but large #s for local accumulation, per-die
struct hf_long_statistics {
	uint64_t rx_header_crc;                     // Header CRCs
	uint64_t rx_body_crc;                       // Data CRCs
	uint64_t rx_header_timeouts;                // Header timeouts
	uint64_t rx_body_timeouts;                  // Data timeouts
	uint64_t core_nonce_fifo_full;              // Core nonce Q overrun events
	uint64_t array_nonce_fifo_full;             // System nonce Q overrun events
	uint64_t stats_overrun;                     // Overrun in statistics reporting
};

// Matching fields for hf_usb_stats1, but large #s for local accumulation, per device
struct hf_long_usb_stats1 {
	// USB incoming
	uint64_t usb_rx_preambles;
	uint64_t usb_rx_receive_byte_errors;
	uint64_t usb_rx_bad_hcrc;

	// USB outgoing
	uint64_t usb_tx_attempts;
	uint64_t usb_tx_packets;
	uint64_t usb_tx_timeouts;
	uint64_t usb_tx_incompletes;
	uint64_t usb_tx_endpointstalled;
	uint64_t usb_tx_disconnected;
	uint64_t usb_tx_suspended;
#if 0
	/* We don't care about UART stats */
	// UART transmit
	uint64_t uart_tx_queue_dma;
	uint64_t uart_tx_interrupts;

	// UART receive
	uint64_t uart_rx_preamble_ints;
	uint64_t uart_rx_missed_preamble_ints;
	uint64_t uart_rx_header_done;
	uint64_t uart_rx_data_done;
	uint64_t uart_rx_bad_hcrc;
	uint64_t uart_rx_bad_dma;
	uint64_t uart_rx_short_dma;
	uint64_t uart_rx_buffers_full;
#endif

	uint8_t  max_tx_buffers;
	uint8_t  max_rx_buffers;
};

struct hashfast_info {
	int asic_count;                             // # of chips in the chain
	int core_count;                             // # of cores per chip
	int device_type;                            // What sort of device this is
	int num_sequence;                           // A power of 2. What the sequence number range is.
	int ref_frequency;                          // Reference clock rate
	struct hf_g1_die_data *die_status;          // Array of per-die voltage, current, temperature sensor data
	struct hf_long_statistics *die_statistics;  // Array of per-die error counters
	struct hf_long_usb_stats1 stats1;
	int hash_clock_rate;                        // Hash clock rate to use, in Mhz
	struct hf_usb_init_base usb_init_base;      // USB Base information from USB_INIT
	struct hf_config_data config_data;          // Configuration data used from USB_INIT
	int core_bitmap_size;                       // in bytes
	uint32_t *core_bitmap;                      // Core OK bitmap test results, run with PLL Bypassed

	pthread_mutex_t lock;
	struct work **works;
	uint16_t hash_sequence_head;                // HOST:   The next hash sequence # to be sent
	uint16_t hash_sequence_tail;                // HOST:   Follows device_sequence_tail around to free work
	uint16_t device_sequence_head;              // DEVICE: The most recent sequence number the device dispatched
	uint16_t device_sequence_tail;              // DEVICE: The most recently completed job in the device
	int64_t hash_count;
	uint16_t shed_count;                        // Dynamic copy of #cores device has shed for thermal control
	int no_matching_work;

	pthread_t read_thr;
};

#endif /* USE_HASHFAST */
#endif	/* HASHFAST_H */
