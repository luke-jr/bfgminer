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
	uint8_t freqM;
	uint8_t freqMaxM;
	uint8_t freqMDefault;

	uint8_t minGoodSamples;

	double errorCount[256];
	double errorWeight[256];
	double errorRate[256];
	double maxErrorRate[256];
};

typedef bool (*dclk_change_clock_func_t)(struct thr_info *, int multiplier);

extern void dclk_msg_freqchange(const char *, int oldFreq, int newFreq, const char *tail);

// Called to initialize dclk_data at startup
extern void dclk_prepare(struct dclk_data *data);
// Called for every quarter of a second to age error rate info
extern void dclk_gotNonces(struct dclk_data *);
// Called for errors (1.0 "portion" is a quarter second)
extern void dclk_errorCount(struct dclk_data *, double portion);
// Called after a nonce range is completed to update actual error rate
extern void dclk_preUpdate(struct dclk_data *data);
// Called after a nonce range is completed, and error rate updated, to make actual clock adjustments
extern bool dclk_updateFreq(struct dclk_data *, dclk_change_clock_func_t changeclock, struct thr_info *);

#endif
