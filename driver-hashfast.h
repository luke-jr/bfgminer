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

#define HASHFAST_MINER_THREADS      1

// Some serial protocol definitions
#define DEFAULT_BAUD_RATE	    115200

#define HF_PREAMBLE             (uint8_t) 0xaa
#define HF_BROADCAST_ADDRESS    (uint8_t) 0xff

// Operation codes (Second header byte)
#define OP_NULL		0
#define OP_ROOT         1
#define OP_RESET        2
#define OP_PLL_CONFIG   3
#define OP_ADDRESS      4
#define OP_READDRESS    5
#define OP_HIGHEST      6
#define OP_BAUD         7
#define OP_UNROOT       8

#define OP_HASH         9
#define OP_NONCE        10
#define OP_ABORT        11
#define OP_STATUS       12
#define OP_GPIO         13
#define OP_CONFIG       14
#define OP_STATISTICS   15
#define OP_GROUP        16
#define OP_CLOCKGATE    17

// All packets begin with a standard 8 byte header
struct hf_header {
	uint8_t  preamble;                      // Always 0xaa
	uint8_t  operation_code;
	uint8_t  chip_address;
	uint8_t  core_address;
	uint16_t hdata;                         // Header specific data
	uint8_t  data_length;                   // .. of data frame to follow, in 4 byte blocks, 0=no data
	uint8_t  crc8;                          // Computed across bytes 1-6 inclusive
} __attribute__((packed,aligned(4)));   	// 8 bytes total

// Body of packet for an OP_HASH operation
struct hf_hash {
	uint8_t  midstate[32];                  // Computed from first half of block header
	uint8_t  merkle_residual[4];            // From block header
	uint32_t timestamp;                     // From block header
	uint32_t bits;                          // Actual difficulty target for block header
	uint32_t starting_nonce;                // Usually set to 0
	uint32_t nonce_loops;                   // How many nonces to search, or 0 for 2^32
	uint16_t ntime_loops:12;                // How many times to roll timestamp, or 0
	uint16_t spare1:4;
	uint8_t  search_difficulty;             // Search difficulty to use, number of leading '0' bits required
	uint8_t  spare2;
	uint32_t spare3;
	uint32_t crc32;                         // Computed across all preceding data fields
} __attribute__((packed,aligned(4)));           // 64 bytes total, including CRC

// How nonces are returned in OP_NONCE packets
struct hf_candidate_nonce {
	uint32_t nonce;                         // Candidate nonce
	uint16_t sequence;                      // Sequence number from corresponding OP_HASH
	uint16_t ntime:12;                      // ntime offset, if ntime roll occurred
	uint16_t search:1;                      // Search forward next 128 nonces to find solution
	uint16_t spare:3;
} __attribute__((packed,aligned(4)));

// Body of packet for an OP_CONFIG operation
struct hf_config_data {
	uint16_t status_period:11;                  // Periodic status time, msec
	uint16_t enable_periodic_status:1;          // Send periodic status
	uint16_t send_status_on_core_idle:1;        // Schedule status whenever core goes idle
	uint16_t send_status_on_pending_empty:1;    // Schedule status whenever core pending goes idle
	uint16_t pwm_active_level:1;                // Active level of PWM outputs, if used
	uint16_t forward_all_privileged_packets:1;  // Forward priv pkts -- diagnostic
	uint8_t  status_batch_delay;                // Batching delay, time to wait before actually sending status
	uint8_t  watchdog:7;                        // Watchdog timeout, seconds
	uint8_t  disable_sensors:1;                 // Diagnostic

        uint8_t  rx_header_timeout:7;               // Header timeout in char times
	uint8_t  rx_ignore_header_crc:1;            // Ignore rx header crc's (diagnostic)
	uint8_t  rx_data_timeout:7;                 // Data timeout in char times / 16
	uint8_t  rx_ignore_data_crc:1;              // Ignore rx data crc's (diagnostic)
	uint8_t  stats_interval:7;                  // Minimum interval to report statistics (seconds)
	uint8_t  stat_diagnostic:1;                 // Never set this
	uint8_t  measure_interval;                  // Die temperature measurment interval (msec)

        uint32_t one_usec:12;                       // How many LF clocks per usec.
	uint32_t max_nonces_per_frame:4;            // Maximum # of nonces to combine in a single frame
	uint32_t voltage_sample_points:8;           // Bit mask for sample points (up to 5 bits set)
	uint32_t pwm_phases:2;                      // phases - 1
	uint32_t trim:4;                            // Trim value for temperature measurements
	uint32_t clock_diagnostic:1;                // Never set this
	uint32_t forward_all_packets:1;             // Forward everything - diagnostic.

        uint16_t pwm_period;                        // Period of PWM outputs, in reference clock cycles
	uint16_t pwm_pulse_period;                  // Initial count, phase 0
} __attribute__((packed,aligned(4)));

// What comes back in the body of an OP_STATISTICS frame
struct hf_statistics {
	uint8_t rx_header_crc;                      // Header CRC's
	uint8_t rx_body_crc;                        // Data CRC's
	uint8_t rx_header_timeouts;                 // Header timeouts
	uint8_t rx_body_timeouts;                   // Data timeouts
	uint8_t core_nonce_fifo_full;               // Core nonce Q overrun events
	uint8_t array_nonce_fifo_full;              // System nonce Q overrun events
	uint8_t stats_overrun;                      // Overrun in statistics reporting
	uint8_t spare;
} __attribute__((packed,aligned(4)));


// Not really necessary (could just link directly to CGMiner's work structures), but these are
// here as an internal place to stage split jobs in the future, e.g. ntime rolling across
// multiple cores.
typedef struct hf_work_t {
	struct work     *work;                      // Finally out to cgminer's work

	uint8_t         data[128];                  // XXX These are only replicated here to help de-couple the
	uint8_t         midstate[32];               // XXX driver code from cgminer's specifics, since this is
	uint8_t         target[32];                 // XXX a sample driver. There's no other reason.

	int             split_count;                // How many cores this is split between
} hf_work_t;

// Internal representation of a "job". Each core should normally have one active job and one pending job queued to it.
// This is where the ALL IMPORTANT sequence number is kept. When jobs are created, this structure is put in the "active"
// list (unique to the asic/core), and an incrementing sequence number is assigned to the job. Only when a sequence number
// that matches or exceeds (modulo <max sequence>) this number in a returned OP_STATUS, do we know that the "busy" bits
// associated with this same core represent the job status, i.e. the associated OP_HASH is no longer in flight.
typedef struct hf_job_t {
	struct list_head l;

	uint8_t chip;                               // Chip address
	uint8_t core;                               // Core address
	uint16_t sequence;                          // Copy of the active sequence number

	hf_work_t *work;                            // Pointer to the work block

} hf_job_t;

// Per-core structure
typedef struct hf_core_t {
	hf_job_t *active;                           // Active job on this core, NULL if none
	hf_job_t *pending;                          // Pending job on this core, NULL if none
	uint8_t enabled;                            // 1 = enabled, 0 = disabled
	uint8_t inflight;                           // How many jobs are "inflight": 0, 1 or 2
	uint8_t seen_allbusy;                       // We've seen both active and pending queues busy
} hf_core_t;                                        // since the last OP_HASH was queued

// Per device structure. This is found by looking up an array, which is indexed
// by CGMiner's cgpu_info.device_id field.
typedef struct hf_info_t {
	int miner_count;
	int timeout;
	int baud_rate;                          // Baud rate, if applicable
	int ref_frequency;                      // Reference clock rate
	int asic_count;                         // # of chips in the chain
	int core_count;                         // # of cores per chip
	int device_type;                        // What sort of device this is
	int max_search_difficulty;              // # of bits set to 0 in hash
	int inflight_target;                    // Set to chips * cores * 2 (1 active, 1 pending each core)
	int hash_sequence;                      // The last hash sequence #
	int num_sequence;                       // A power of 2. What the sequence number range is.
	int num_work;                           // Number of "work" entries in work queue
	int max_work;                           // Target maximum number of "work" entries in work queue
						// If work is split between cores, then max_work < inflight_target.
	int last_log;                           // Last OP_STATUS log time in seconds

	float thermal_trip_temperature;         // Thermal trip temperature in degrees C
	int thermal_trip_limit;                 // Thermal trip limit in raw device adc counts (derived from above)
	int tacho_enable;                       // Set if there is a tacho to be read

	uint64_t hash_loops;                    // XXX Temp. How many nonces to cycle through (range limited for FPGA emulation)

	int no_matching_work;

	struct list_head active;                  // Double linked list through all ACTIVE in-flight jobs (hf_job_t's)
	struct list_head inactive;                // Double linked list through all INACTIVE job blocks (hf_job_t's)

	int active_count;                       // How many active hf_job_t's are out there
	int inactive_count;                     // How many inactive hf_job_t's are out there

	hf_core_t **cores;                      // Points to array of chips, which each point to array of cores
	hf_work_t *work;                        // Points to array of work
} hf_info_t;

// The sequence distance between a sent and received sequence number.
#define SEQUENCE_DISTANCE(tx,rx)        ((tx)>=(rx)?((tx)-(rx)):(info->num_sequence+(tx)-(rx)))

// Values info->device_type can take, comes from a completed OP_ADDRESS cycle
#define HFD_G1                          1       /* A real G-1 ASIC */
#define HFD_VC709                       128     /* FPGA Emulation */
#define HFD_ExpressAGX                  129     /* FPGA Emulation */

// Some USB defines
#define HASHFAST_USB_PACKETSIZE         512     /* XXX Fix this. */

// Some low level serial defines
#define HASHFAST_READ_TIME(baud) ((double)HASHFAST_READ_SIZE * (double)8.0 / (double)(baud))

#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

hf_info_t **hashfast_info;

void hf_init_crc8(void);
void hf_init_crc32(void);

#endif /* USE_HASHFAST */
#endif	/* HASHFAST_H */
