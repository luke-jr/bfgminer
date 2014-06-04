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

#ifndef BFG_DRIVER_ZEUS_H
#define BFG_DRIVER_ZEUS_H

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
#define ZEUS_READ_FAULT_DECISECONDS 1

#define NANOSEC 1000000000.0

// Default value for ZEUS_INFO->read_size
#define ZEUS_DEFAULT_READ_SIZE 4

#define ICA_GETS_ERROR -1
#define ICA_GETS_OK 0
#define ICA_GETS_RESTART 1
#define ICA_GETS_TIMEOUT 2

// Store the last INFO_HISTORY data sets
// [0] = current data, not yet ready to be included as an estimate
// Each new data set throws the last old set off the end thus
// keeping a ongoing average of recent data
#define INFO_HISTORY 10

static int option_offset = -1;
extern struct device_drv zeus_drv;

struct ZEUS_HISTORY {
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
enum zeus_reopen_mode {
	IRM_NEVER,
	IRM_TIMEOUT,
	IRM_CYCLE,
};
enum zeus_user_settings {
	IUS_WORK_DIVISION = 1,
	IUS_FPGA_COUNT    = 2,
};

struct ZEUS_INFO {
	// time to calculate the golden_ob
	struct timeval golden_tv;

	struct ZEUS_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	// seconds per Hash
	double Hs;
	uint32_t read_count;
	uint64_t golden_speed_percore;// speed pre core per sec
	int probe_read_count;
	// ds limit for (short=/long=) read_count
	int read_count_limit;

	enum timing_mode timing_mode;
	bool do_zeus_timing;
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

	// zeus-options
	int check_num;
	int baud;
	int cores_perchip;
	int chips_count_max;
	int chips_count;
	int chip_clk;
	uint32_t clk_header;
	int chips_bit_num;//log2(chips_count_max)

	char core_hash[10];
	char chip_hash[10];
	char board_hash[10];
	int work_division;
	int fpga_count;
	uint32_t nonce_mask;
	enum zeus_reopen_mode reopen_mode;
	bool reopen_now;
	uint8_t user_set;
	bool continue_search;

	dclk_change_clock_func_t dclk_change_clock_func;
	struct dclk_data dclk;
	
	// Bytes to read from Zeus for nonce
	int read_size;
	
	size_t ob_size;
	const char *golden_ob;
	const char *golden_nonce;
	bool nonce_littleendian;
	
	// Custom driver functions
	bool (*detect_init_func)(const char *devpath, int fd, struct ZEUS_INFO *);
	bool (*job_start_func)(struct thr_info *);
};

// One for each possible device
static struct ZEUS_INFO **zeus_info;

struct zeus_state {
	bool firstrun;
	struct timeval tv_workstart;
	struct timeval tv_workfinish;
	struct work *last_work;
	struct work *last2_work;
	bool changework;
	bool identify;
	
	uint8_t *ob_bin;
};

//bool zeus_detect_custom(const char *devpath, struct device_drv *, struct ZEUS_INFO *);
extern void suffix_string(uint64_t val, char *buf, int sigdigits);
extern int zeus_gets(unsigned char *, int fd, struct timeval *tv_finish, struct thr_info *, int read_count, int read_size, uint32_t *);
extern int zeus_write(int fd, const void *buf, size_t bufLen);
extern bool zeus_init(struct thr_info *);
extern void do_zeus_close(struct thr_info *thr);
extern bool zeus_job_start(struct thr_info *);

#endif
