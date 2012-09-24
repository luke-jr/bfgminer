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

	double errorCount[256];
	double errorWeight[256];
	double errorRate[256];
	double maxErrorRate[256];
};

typedef bool (*dclk_change_clock_func_t)(struct thr_info *, int multiplier);

extern void dclk_msg_freqchange(const char *, int oldFreq, int newFreq, const char *tail);

extern void dclk_prepare(struct dclk_data *data);
extern void dclk_gotNonces(struct dclk_data *);
extern void dclk_errorCount(struct dclk_data *, double portion);
extern void dclk_preUpdate(struct dclk_data *data);
extern bool dclk_updateFreq(struct dclk_data *, dclk_change_clock_func_t changeclock, struct thr_info *);

#endif
