/*
 * Copyright 2013 Xiangfu <xiangfu@openmobilefree.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef AVALON_H
#define AVALON_H

struct avalon_task {
	uint8_t reset		:1;
	uint8_t flush_fifo	:1;
	uint8_t fan_eft		:1;
	uint8_t timer_eft	:1;
	uint8_t chip_num	:4;
	uint8_t fan_pwm_data;
	uint8_t timeout_data;
	uint8_t miner_num;	// Word[0]

	uint8_t nonce_elf		:1;
	uint32_t pad0_miner_ctrl	:31;
	uint32_t pad1_miner_ctrl;	//Word[2:1]

	uint32_t midstate[8];
	uint32_t data[3];

	// nonce_range: Word[??:14]
} __attribute__((packed));

struct avalon_result {
	uint32_t nonce;
	uint32_t data[3];
	uint32_t midstate[8];
	uint32_t reserved[2];
} __attribute__((packed));

struct AVALON_HISTORY {
	struct timeval finish;
	double sumXiTi;
	double sumXi;
	double sumTi;
	double sumXi2;
	uint32_t values;
	uint32_t hash_count_min;
	uint32_t hash_count_max;
};

enum timing_mode { MODE_DEFAULT, MODE_SHORT, MODE_LONG, MODE_VALUE };

#define INFO_HISTORY 10

struct AVALON_INFO {
	// time to calculate the golden_ob
	uint64_t golden_hashes;
	struct timeval golden_tv;

	struct AVALON_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	// seconds per Hash
	double Hs;
	int read_count;

	enum timing_mode timing_mode;
	bool do_avalon_timing;

	double fullnonce;
	int count;
	double W;
	uint32_t values;
	uint64_t hash_count_range;

	// Determine the cost of history processing
	// (which will only affect W)
	uint64_t history_count;
	struct timeval history_time;

	// avalon-options
	int baud;
	int work_division;
	int asic_count;
	uint32_t nonce_mask;
};

#define AVALON_MINER_THREADS 1
#define AVALON_GET_WORK_COUNT 20

#define AVALON_IO_SPEED 115200
#define AVALON_SEND_WORK_PITCH 8000
#define AVALON_WRITE_SIZE (sizeof(struct avalon_task))
#define AVALON_READ_SIZE (sizeof(struct avalon_result))

// Ensure the sizes are correct for the Serial read
#define ASSERT1(condition) __maybe_unused static char sizeof_uint32_t_must_be_4[(condition)?1:-1]
ASSERT1(sizeof(uint32_t) == 4);

#define AVALON_READ_TIME(baud) ((double)AVALON_READ_SIZE * (double)8.0 / (double)(baud))

// Fraction of a second, USB timeout is measured in
// i.e. 10 means 1/10 of a second
#define TIME_FACTOR 10

// It's 10 per second, thus value = 10/TIME_FACTOR =
#define AVALON_RESET_FAULT_DECISECONDS 1

// In timing mode: Default starting value until an estimate can be obtained
// 5 seconds allows for up to a ~840MH/s device
#define AVALON_READ_COUNT_TIMING	(5 * TIME_FACTOR)

// For a standard Avalon REV3 (to 5 places)
// Since this rounds up a the last digit - it is a slight overestimate
// Thus the hash rate will be a VERY slight underestimate
// (by a lot less than the displayed accuracy)
#define AVALON_REV3_HASH_TIME 0.0000000026316
#define NANOSEC 1000000000.0

#define HISTORY_SEC 60
// Minimum how many points a single AVALON_HISTORY should have
#define MIN_DATA_COUNT 5
// The value above used is doubled each history until it exceeds:
#define MAX_MIN_DATA_COUNT 100

#define END_CONDITION 0x0000ffff

#define AVA_GETS_ERROR -1
#define AVA_GETS_OK 0
#define AVA_GETS_RESTART 1
#define AVA_GETS_TIMEOUT 2
#define AVA_GETS_DONE 3

#define AVA_SEND_ERROR -1
#define AVA_SEND_OK 0
#define AVA_SEND_FULL 1

#define avalon_open2(devpath, baud, purge)  serial_open(devpath, baud, AVALON_RESET_FAULT_DECISECONDS, purge)
#define avalon_open(devpath, baud)  avalon_open2(devpath, baud, false)

#define avalon_init_default_task(at) avalon_init_task(at, 0, 0, 0, 0, 0, 0)
#define avalon_close(fd) close(fd)

#define AVA_BUFFER_FULL 0
#define AVA_BUFFER_EMPTY 1
#define avalon_buffer_full(fd)	get_serial_cts(fd)

static inline void rev(uint8_t *s, size_t l)
{
	size_t i, j;
	uint8_t t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

#endif	/* AVALON_H */
