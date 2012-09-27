#ifndef ICARUS_COMMON_H
#define ICARUS_COMMON_H

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


// Store the last INFO_HISTORY data sets
// [0] = current data, not yet ready to be included as an estimate
// Each new data set throws the last old set off the end thus
// keeping a ongoing average of recent data
#define INFO_HISTORY 10

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

struct ICARUS_INFO {
	// time to calculate the golden_ob
	uint64_t golden_hashes;
	struct timeval golden_tv;

	struct ICARUS_HISTORY history[INFO_HISTORY+1];
	uint32_t min_data_count;

	// seconds per Hash
	double Hs;
	int read_count;

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
	int work_division;
	int fpga_count;
	uint32_t nonce_mask;
	bool quirk_reopen;

	dclk_change_clock_func_t dclk_change_clock_func;
	struct dclk_data dclk;
};

struct icarus_state {
	bool firstrun;
	struct timeval tv_workstart;
	struct timeval tv_workfinish;
	struct work last_work;
	bool changework;
};

bool icarus_detect_custom(const char *devpath, struct device_api *, struct ICARUS_INFO *);
extern int icarus_gets(unsigned char *, int fd, struct timeval *tv_finish, struct thr_info *, int read_count);

#endif
