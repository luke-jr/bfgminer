/*
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2012 nelisky
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include "dynclock.h"
#include "miner.h"

void dclk_prepare(struct dclk_data *data)
{
	*data = (struct dclk_data){
		// after 275 sample periods
		.minGoodSamples = 150.,
		.freqMinM = 1,
	};
}

void dclk_msg_freqchange(const char *repr, int oldFreq, int newFreq, const char *tail)
{
	applog(LOG_NOTICE, "%"PRIpreprv": Frequency %s from %u to %u MHz%s",
	       repr,
	       (oldFreq > newFreq ? "dropped" : "raised "),
	       oldFreq, newFreq,
	       tail ?: ""
	);
}

bool dclk_updateFreq(struct dclk_data *data, dclk_change_clock_func_t changeclock, struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	uint8_t freqMDefault = data->freqMDefault;
	int i, maxM, bestM;
	double bestR, r;
	bool rv = true;

	if (freqMDefault > data->freqMaxM)
		// This occurs when the device in question adjusts its MaxM down due to temperature or similar reasons
		freqMDefault = data->freqMaxM;

	for (i = 0; i < data->freqMaxM; i++)
		if (data->maxErrorRate[i + 1] * i < data->maxErrorRate[i] * (i + 20))
			data->maxErrorRate[i + 1] = data->maxErrorRate[i] * (1.0 + 20.0 / i);

	maxM = 0;
	// Use max mulitplier up to the default as far as possible without hitting the max error rate
	while (maxM < freqMDefault && data->maxErrorRate[maxM + 1] < DCLK_MAXMAXERRORRATE)
		maxM++;
	// Use max mulitplier beyond the default if it's never hit the max error rate, and our current max has collected sufficient samples
	while (maxM < data->freqMaxM && data->maxErrorRate[maxM + 1] < DCLK_MAXMAXERRORRATE && data->errorWeight[maxM] >= data->minGoodSamples)
		maxM++;

	// Find the multiplier that gives the best hashrate
	bestM = data->freqMinM;
	bestR = 0;
	for (i = bestM; i <= maxM; i++) {
		// Hashrate is weighed on a linear scale
		r = (i + 1);
		
		// The currently selected frequency gets a small "bonus" in comparison, as hysteresis
		if (i == data->freqM)
			r += DCLK_ERRORHYSTERESIS;
		
		// Adjust for measured error rate
		r *= (1 - data->maxErrorRate[i]);
		
		// If it beats the current best, update best*
		if (r > bestR) {
			bestM = i;
			bestR = r;
		}
	}

	// Actually change the clock if the best multiplier is not currently selected
	if (bestM != data->freqM) {
		rv = changeclock(thr, bestM);
	}

	// Find the highest multiplier that we've taken a reasonable sampling of
	maxM = freqMDefault;
	while (maxM < data->freqMaxM && data->errorWeight[maxM + 1] > 100)
		maxM++;
	// If the new multiplier is some fraction of the highest we've used long enough to get a good sample, assume there is something wrong and instruct the driver to shut it off
	if ((bestM < (1.0 - DCLK_OVERHEATTHRESHOLD) * maxM) && bestM < maxM - 1) {
		applog(LOG_ERR, "%"PRIpreprv": frequency drop of %.1f%% detect. This may be caused by overheating. FPGA is shut down to prevent damage.",
		       cgpu->proc_repr,
		       (1.0 - 1.0 * bestM / maxM) * 100);
		return false;
	}
	return rv;
}

void dclk_gotNonces(struct dclk_data *data)
{
	data->errorCount[data->freqM] *= 0.995;
	data->errorWeight[data->freqM] = data->errorWeight[data->freqM] * 0.995 + 1.0;
}

void dclk_errorCount(struct dclk_data *data, double portion)
{
	data->errorCount[data->freqM] += portion;
}

void dclk_preUpdate(struct dclk_data *data)
{
	data->errorRate[data->freqM] = data->errorCount[data->freqM] / data->errorWeight[data->freqM];
	// errorWeight 100 begins after sample period 137; before then, we minimize the effect of measured errorRate
	if (data->errorWeight[data->freqM] < 100)
		data->errorRate[data->freqM] /= 100;
	
	if (data->errorRate[data->freqM] > data->maxErrorRate[data->freqM])
		data->maxErrorRate[data->freqM] = data->errorRate[data->freqM];
}
