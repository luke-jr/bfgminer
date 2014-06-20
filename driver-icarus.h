/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 Xiangfu
 * Copyright 2012 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFG_DRIVER_ICARUS_H
#define BFG_DRIVER_ICARUS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "dynclock.h"
#include "miner.h"

// Fraction of a second, USB timeout is measured in
// i.e. 10 means 1/10 of a second
// Right now, it MUST be 10 due to other assumptions.
#define TIME_FACTOR 10
// It's 10 per second, thus value = 10/TIME_FACTOR =
#define ICARUS_READ_FAULT_DECISECONDS 1

#define NANOSEC 1000000000.0

// Default value for ICARUS_INFO->read_size
#define ICARUS_DEFAULT_READ_SIZE 4

#define ICA_GETS_ERROR -1
#define ICA_GETS_OK 0
#define ICA_GETS_RESTART 1
#define ICA_GETS_TIMEOUT 2

// Store the last INFO_HISTORY data sets
// [0] = current data, not yet ready to be included as an estimate
// Each new data set throws the last old set off the end thus
// keeping a ongoing average of recent data
#define INFO_HISTORY 10

extern struct device_drv icarus_drv;

struct ICARUS_HISTORY {
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
enum icarus_reopen_mode {
	IRM_NEVER,
	IRM_TIMEOUT,
	IRM_CYCLE,
};
enum icarus_user_settings {
	IUS_WORK_DIVISION = 1,
	IUS_FPGA_COUNT    = 2,
};

struct ICARUS_INFO {
	// time to calculate the golden_ob
	struct timeval golden_tv;

	// History structures for calculating read_count
	// when info->do_icarus_timing is true
	struct ICARUS_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	// Timeout scanning for a nonce (deciseconds)
	int read_count;
	// Timeout scanning for a golden nonce (deciseconds)
	int probe_read_count;

	// ds limit for (short=/long=) read_count
	int read_count_limit;

	enum timing_mode timing_mode;
	bool do_icarus_timing;
	int do_default_detection;

	double fullnonce;
	int count;
	double W;
	uint32_t values;
	uint64_t hash_count_range;

	// Determine the cost of history processing
	// (which will only affect W)
	uint64_t history_count;
	struct timeval history_time;

	// icarus-options
	int baud;

	// Used to calculate / display hash count
	// when no nonce is found
	// seconds per Hash
	double Hs;

	// Used to calculate / display hash count
	// when a nonce is found
	int work_division;
	int fpga_count;
	uint32_t nonce_mask;
	// Calculate hashes based on info->Hs rather than nonce_mask
	// Using nonce_mask doesn't scale when work division
	// is not a power of 2
	bool ignore_nonce_mask;

	enum icarus_reopen_mode reopen_mode;
	bool reopen_now;
	uint8_t user_set;
	bool continue_search;

	dclk_change_clock_func_t dclk_change_clock_func;
	struct dclk_data dclk;
	
	// Bytes to read from Icarus for nonce
	int read_size;

	// Settings used when probing / detecting
	size_t ob_size;
	const char *golden_ob;
	const char *golden_nonce;
	bool nonce_littleendian;
	// Don't check the golden nonce returned when probing
	bool ignore_golden_nonce;

	// Custom driver functions
	bool (*detect_init_func)(const char *devpath, int fd, struct ICARUS_INFO *);
	bool (*job_start_func)(struct thr_info *);

	// Hardware information, doesn't affect anything directly
	// Storage for Icarus-based drivers since fpga_count directly affects nonce_mask
	uint16_t freq;
	uint8_t chips;
	uint8_t cores;
};

struct icarus_state {
	bool firstrun;
	struct timeval tv_workstart;
	struct timeval tv_workfinish;
	struct work *last_work;
	struct work *last2_work;
	bool changework;
	bool identify;
	
	uint8_t *ob_bin;
};

bool icarus_detect_custom(const char *devpath, struct device_drv *, struct ICARUS_INFO *);
extern int icarus_gets(unsigned char *, int fd, struct timeval *tv_finish, struct thr_info *, int read_count, int read_size);
extern int icarus_write(int fd, const void *buf, size_t bufLen);
extern bool icarus_init(struct thr_info *);
extern void do_icarus_close(struct thr_info *thr);
extern bool icarus_job_start(struct thr_info *);

#endif
