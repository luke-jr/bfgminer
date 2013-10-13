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

struct hashfast_info {
	int asic_count;                             // # of chips in the chain
	struct hf_g1_die_data *die_status;          // Array of per-die voltage, current, temperature sensor data
	struct hf_long_statistics *die_statistics;  // Array of per-die error counters
	int hash_clock_rate;                        // Hash clock rate to use, in Mhz
	struct hf_usb_init_base usb_init_base;      // USB Base information from USB_INIT
	struct hf_config_data config_data;          // Configuration data used from USB_INIT

	struct work **works;
	uint16_t device_sequence_head;              // The most recent sequence number the device dispatched
	uint16_t device_sequence_tail;              // The most recently completed job in the device
	uint16_t hash_sequence_tail;                // Follows device_sequence_tail around to free work
};

#endif /* USE_HASHFAST */
#endif	/* HASHFAST_H */
