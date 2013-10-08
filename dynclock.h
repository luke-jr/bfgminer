/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 nelisky
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef DYNCLOCK_H
#define DYNCLOCK_H

#include <stdbool.h>
#include <stdint.h>

struct thr_info;

#define DCLK_MAXMAXERRORRATE 0.05
#define DCLK_ERRORHYSTERESIS 0.1
#define DCLK_OVERHEATTHRESHOLD 0.4

struct dclk_data {
	// Current frequency multiplier
	uint8_t freqM;
	
	// Minimum frequency multiplier to consider (set by driver)
	uint8_t freqMinM;
	
	// Maximum frequency multiplier to consider (set by driver)
	uint8_t freqMaxM;
	
	// "Default" frequency multiplier to work with (set by driver)
	uint8_t freqMDefault;

	// Threshold before errorWeight is considered reasonably constant
	// NOTE: This is not a mere number of sampling periods (but related)
	uint8_t minGoodSamples;

	// Numerator of errorWeight after dclk_errorCount
	double errorCount[256];
	
	// Approaches 200
	double errorWeight[256];
	
	// Error rate (0.0 - 1.0) as of end of last sampling period
	double errorRate[256];
	
	// Highest error rate (0.0 - 1.0) encountered
	double maxErrorRate[256];
};

typedef bool (*dclk_change_clock_func_t)(struct thr_info *, int multiplier);

// Standard applog message called by driver frequency-change functions
extern void dclk_msg_freqchange(const char *, int oldFreq, int newFreq, const char *tail);

// Called to initialize dclk_data at startup
extern void dclk_prepare(struct dclk_data *data);

// Called to start a sampling period
extern void dclk_gotNonces(struct dclk_data *);

// Called to increment the current sampling period's error rate (1.0 "portion" is 100% errors)
extern void dclk_errorCount(struct dclk_data *, double portion);

// Called after a sampling period is completed to update actual error rate
extern void dclk_preUpdate(struct dclk_data *data);

// Called after a sampling period is completed, and error rate updated, to make actual clock adjustments
extern bool dclk_updateFreq(struct dclk_data *, dclk_change_clock_func_t changeclock, struct thr_info *);

#endif
