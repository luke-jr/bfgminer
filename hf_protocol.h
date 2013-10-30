//
// Copyright 2013 HashFast LLC
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version. See COPYING for more details.
//
// Useful data structures and values for interfacing with HashFast products
//
// Version 1.0
//

#ifndef _HF_PROTOCOL_H_
#define _HF_PROTOCOL_H_

#define HF_PROTOCOL_VERSION     ((0<<8)|1)

#define HF_PREAMBLE             (uint8_t) 0xaa
#define HF_BROADCAST_ADDRESS    (uint8_t) 0xff
#define HF_GWQ_ADDRESS          (uint8_t) 254

// Serial protocol operation codes (Second header byte)
#define OP_NULL         0
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

// Conversions for the ADC readings from GN on-chip sensors
#define GN_CORE_VOLTAGE(a)              ((float)(a)/256*1.2)
#define GN_DIE_TEMPERATURE(a)           ((((float)(a)*240)/4096.0)-61.5)

// The sequence distance between a sent and received sequence number.
#define HF_SEQUENCE_DISTANCE(tx,rx)        ((tx)>=(rx)?((tx)-(rx)):(info->num_sequence+(tx)-(rx)))

// Values the protocol field in the above structure may take
#define PROTOCOL_USB_MAPPED_SERIAL      0
#define PROTOCOL_GLOBAL_WORK_QUEUE      1

// Conversions for the board/module level sensors
#define M_VOLTAGE(a)                    ((float)(a)*19.0734e-6)
#define M_PHASE_CURRENT(a)              ((float)(a)*0.794728597e-3)

// Values info->device_type can take
#define HFD_G1                            1         // HashFast G-1 GN ASIC
#define HFD_VC709                       128
#define HFD_ExpressAGX                  129

// USB interface specific operation codes
#define OP_USB_INIT                     128         // Initialize USB interface details
#define OP_GET_TRACE                    129         // Send back the trace buffer if present
#define OP_LOOPBACK_USB                 130
#define OP_LOOPBACK_UART                131
#define OP_DFU                          132         // Jump into the boot loader
#define OP_USB_SHUTDOWN                 133         // Initialize USB interface details
#define OP_DIE_STATUS                   134         // Die status. There are 4 die per ASIC
#define OP_GWQ_STATUS                   135         // Global Work Queue protocol status
#define OP_WORK_RESTART                 136         // Stratum work restart regime
#define OP_USB_STATS1                   137         // Statistics class 1
#define OP_USB_GWQSTATS                 138         // GWQ protocol statistics
#define OP_USB_NOTICE                   139         // Asynchronous notification event
#define OP_USB_DEBUG                    255

// HashFast vendor and product ID's
#define HF_USB_VENDOR_ID                0x297c
#define HF_USB_PRODUCT_ID_G1            0x0001

// If this bit is set, search forward for other nonce(s)
#define HF_NTIME_MASK                   0xfff       // Mask for for ntime
#define HF_NONCE_SEARCH                 0x1000      // Search bit in candidate_nonce -> ntime

//
// Fault codes that can be returned in struct hf_usb_init_base.operation_status
//
#define E_RESET_TIMEOUT                 1
#define E_ADDRESS_TIMEOUT               2
#define E_CLOCKGATE_TIMEOUT             3
#define E_CONFIG_TIMEOUT                4
#define E_EXCESS_CORE_FAILURES          5

#define U32SIZE(x)                      (sizeof(x)/sizeof(uint32_t))


// Structure definitions, LE platforms

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#include "hf_protocol_be.h"
#else
// Generic header
struct hf_header {
	uint8_t  preamble;                      // Always 0xaa
	uint8_t  operation_code;
	uint8_t  chip_address;
	uint8_t  core_address;
	uint16_t hdata;                         // Header specific data
	uint8_t  data_length;                   // .. of data frame to follow, in 4 byte blocks, 0=no data
	uint8_t  crc8;                          // Computed across bytes 1-6 inclusive
} __attribute__((packed,aligned(4)));           // 8 bytes total

// Header specific to OP_PLL_CONFIG
struct hf_pll_config {
	uint8_t  preamble;
	uint8_t  operation_code;
	uint8_t  chip_address;

	uint8_t  pll_divr:6;
	uint8_t  pll_bypass:1;
	uint8_t  pll_reset:1;

	uint8_t  pll_divf;

	uint8_t  spare1:1;                      // Must always be 0
	uint8_t  pll_divq:3;
	uint8_t  pll_range:3;
	uint8_t  pll_fse:1;                     // Must always be 1

	uint8_t  data_length;                   // Always 0
	uint8_t  crc8;                          // Computed across bytes 1-6 inclusive
} __attribute__((packed,aligned(4)));           // 8 bytes total

// OP_HASH serial data
struct hf_hash_serial {
	uint8_t  midstate[32];                  // Computed from first half of block header
	uint8_t  merkle_residual[4];            // From block header
	uint32_t timestamp;                     // From block header
	uint32_t bits;                          // Actual difficulty target for block header
	uint32_t starting_nonce;                // Usually set to 0
	uint32_t nonce_loops;                   // How many nonces to search, or 0 for 2^32
	uint16_t ntime_loops;                   // How many times to roll timestamp, or 0
	uint8_t  search_difficulty;             // Search difficulty to use, # of '0' digits required
	uint8_t  option;
	uint8_t  group;
	uint8_t  spare3[3];
} __attribute__((packed,aligned(4)));

// OP_HASH usb data - header+data = 64 bytes
struct hf_hash_usb {
	uint8_t  midstate[32];                  // Computed from first half of block header
	uint8_t  merkle_residual[4];            // From block header
	uint32_t timestamp;                     // From block header
	uint32_t bits;                          // Actual difficulty target for block header
	uint32_t starting_nonce;                // Usually set to 0
	uint32_t nonce_loops;                   // How many nonces to search, or 0 for 2^32
	uint16_t ntime_loops;                   // How many times to roll timestamp, or 0
	uint8_t  search_difficulty;             // Search difficulty to use, # of '0' digits required
	uint8_t  group;                         // Non-zero for valid group
} __attribute__((packed,aligned(4)));

// OP_NONCE data
struct hf_candidate_nonce {
	uint32_t nonce;                         // Candidate nonce
	uint16_t sequence;                      // Sequence number from corresponding OP_HASH
	uint16_t ntime;                         // ntime offset, if ntime roll occurred, in LS 12 bits
						// If b12 set, search forward next 128 nonces to find solution(s)
} __attribute__((packed,aligned(4)));

// OP_CONFIG data
struct hf_config_data {
	uint16_t status_period:11;                  // Periodic status time, msec
	uint16_t enable_periodic_status:1;          // Send periodic status
	uint16_t send_status_on_core_idle:1;        // Schedule status whenever core goes idle
	uint16_t send_status_on_pending_empty:1;    // Schedule status whenever core pending goes idle
	uint16_t pwm_active_level:1;                // Active level of PWM outputs, if used
	uint16_t forward_all_privileged_packets:1;  // Forward priv pkts -- diagnostic
	uint8_t  status_batch_delay;                // Batching delay, time to wait before sending status
	uint8_t  watchdog:7;                        // Watchdog timeout, seconds
	uint8_t  disable_sensors:1;                 // Diagnostic

	uint8_t  rx_header_timeout:7;               // Header timeout in char times
	uint8_t  rx_ignore_header_crc:1;            // Ignore rx header crc's (diagnostic)
	uint8_t  rx_data_timeout:7;                 // Data timeout in char times / 16
	uint8_t  rx_ignore_data_crc:1;              // Ignore rx data crc's (diagnostic)
	uint8_t  stats_interval:7;                  // Minimum interval to report statistics (seconds)
	uint8_t  stat_diagnostic:1;                 // Never set this
	uint8_t  measure_interval;                  // Die temperature measurement interval (msec)

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

// OP_GROUP data
struct hf_group_data {
	uint16_t nonce_msoffset;                    // This value << 16 added to starting nonce
	uint16_t ntime_offset;                      // This value added to timestamp
} __attribute__((packed,aligned(4)));

// Structure of the monitor fields for G-1, returned in OP_STATUS, core bitmap follows this
struct hf_g1_monitor { 
	uint16_t die_temperature;                   // Die temperature ADC count
	uint8_t  core_voltage[6];                   // Core voltage
						// [0] = main sensor
						// [1]-[5] = other positions
} __attribute__((packed,aligned(4)));

// What comes back in the body of an OP_STATISTICS frame (On die statistics)
struct hf_statistics {
	uint8_t rx_header_crc;                      // Header CRC error's
	uint8_t rx_body_crc;                        // Data CRC error's
	uint8_t rx_header_timeouts;                 // Header timeouts
	uint8_t rx_body_timeouts;                   // Data timeouts
	uint8_t core_nonce_fifo_full;               // Core nonce Q overrun events
	uint8_t array_nonce_fifo_full;              // System nonce Q overrun events
	uint8_t stats_overrun;                      // Overrun in statistics reporting
	uint8_t spare;
} __attribute__((packed,aligned(4)));


////////////////////////////////////////////////////////////////////////////////
// USB protocol data structures
////////////////////////////////////////////////////////////////////////////////

// Convenience header specific to OP_USB_INIT
struct hf_usb_init_header {
	uint8_t  preamble;                      // Always 0xaa
	uint8_t  operation_code;
	uint8_t  spare1;

	uint8_t  protocol:3;                    // Which protocol to use
	uint8_t  user_configuration:1;          // Use the following configuration data
	uint8_t  pll_bypass:1;                  // Force PLL bypass, hash clock = ref clock
	uint8_t  no_asic_initialization:1;      // Do not perform automatic ASIC initialization
	uint8_t  do_atspeed_core_tests:1;       // Do core tests at speed, return second bitmap
	uint8_t  leave_powered_down:1;          // Init USB only, leave device powered down

	uint16_t hash_clock;                    // Requested hash clock frequency

	uint8_t  data_length;                   // .. of data frame to follow, in 4 byte blocks
	uint8_t  crc8;                          // Computed across bytes 1-6 inclusive
} __attribute__((packed,aligned(4)));           // 8 bytes total

// Options (only if present) that may be appended to the above header
// Each option involving a numerical value will only be in effect if the value is non-zero
// This allows the user to select only those options desired for modification. Do not
// use this facility unless you are an expert - loading inconsistent settings will not work.
struct hf_usb_init_options {
	uint16_t group_ntime_roll;                  // Total ntime roll amount per group
	uint16_t core_ntime_roll;                   // Total core ntime roll amount
	uint8_t  low_operating_temp_limit;          // Lowest normal operating limit
	uint8_t  high_operating_temp_limit;         // Highest normal operating limit
	uint16_t spare;
} __attribute__((packed,aligned(4)));

// Base item returned from device for OP_USB_INIT
struct hf_usb_init_base { 
	uint16_t firmware_rev;                      // Firmware revision #
	uint16_t hardware_rev;                      // Hardware revision #
	uint32_t serial_number;                     // Board serial number
	uint8_t  operation_status;                  // Reply status for OP_USB_INIT (0 = success)
	uint8_t  extra_status_1;                    // Extra reply status information, code specific
	uint16_t sequence_modulus;                  // Sequence numbers are to be modulo this
	uint16_t hash_clockrate;                    // Actual hash clock rate used (nearest Mhz)
	uint16_t inflight_target;                   // Target inflight amount for GWQ protocol
} __attribute__((packed,aligned(4)));

// The above base item (16 bytes) is followed by the struct hf_config_data (16 bytes) actually
// used internally (so users may modify non-critical fields by doing subsequent
// OP_CONFIG operations). This is followed by a device specific "core good" bitmap (unless the
// user disabled initialization), and optionally by an at-speed "core good" bitmap.


// Information in an OP_DIE_STATUS frame. This is for one die - there are four per ASIC.
// Board level phase current and voltage sensors are likely to disappear in later production models.
struct hf_g1_die_data {
	struct hf_g1_monitor die;                   // Die sensors - 8 bytes
	uint16_t phase_currents[4];                 // Phase currents (0 if unavailable)
	uint16_t voltage;                           // Voltage at device boundary (0 if unavailable)
	uint16_t temperature;                       // Regulator temp sensor
	uint16_t tacho;                             // See documentation
	uint16_t spare;
} __attribute__((packed,aligned(4)));               // 24 bytes total


// Information for an OP_GWQ_STATUS frame
// If sequence_head == sequence_tail, then there is no active work and sequence_head is invalid
struct hf_gwq_data {
	uint64_t hash_count;                        // Add this to host's cumulative hash count
	uint16_t sequence_head;                     // The latest, internal, active sequence #
	uint16_t sequence_tail;                     // The latest, internal, inactive sequence #
	uint16_t shed_count;                        // # of cores have been shedded for thermal control
	uint16_t spare;
} __attribute__((packed,aligned(4)));


// Information for an OP_USB_STATS1 frame - Communication statistics
struct hf_usb_stats1 {
	// USB incoming
	uint16_t usb_rx_preambles;
	uint16_t usb_rx_receive_byte_errors;
	uint16_t usb_rx_bad_hcrc;

	// USB outgoing
	uint16_t usb_tx_attempts;
	uint16_t usb_tx_packets;
	uint16_t usb_tx_timeouts;
	uint16_t usb_tx_incompletes;
	uint16_t usb_tx_endpointstalled;
	uint16_t usb_tx_disconnected;
	uint16_t usb_tx_suspended;

	// Internal UART transmit
	uint16_t uart_tx_queue_dma;
	uint16_t uart_tx_interrupts;

	// Internal UART receive
	uint16_t uart_rx_preamble_ints;
	uint16_t uart_rx_missed_preamble_ints;
	uint16_t uart_rx_header_done;
	uint16_t uart_rx_data_done;
	uint16_t uart_rx_bad_hcrc;
	//uint16_t uart_rx_bad_crc32;
	uint16_t uart_rx_bad_dma;
	uint16_t uart_rx_short_dma;
	uint16_t uart_rx_buffers_full;

	uint8_t  max_tx_buffers;                        // Maximum # of send buffers ever used
	uint8_t  max_rx_buffers;                        // Maximum # of receive buffers ever used
} __attribute__((packed,aligned(4)));

// Information for an OP_USB_NOTICE frame
struct hf_usb_notice_data {
	uint32_t extra_data;                        // Depends on notification code
	char     message[];                         // NULL terminated, little endian byte order
};
#endif

#endif
