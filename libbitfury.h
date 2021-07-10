#ifndef BFG_LIBBITFURY_H
#define BFG_LIBBITFURY_H

#include <stdbool.h>
#include <stdint.h>

#include "lowl-spi.h"
#include "miner.h"

struct work;

#define BITFURY_STAT_N 1024

struct bitfury_payload {
	uint8_t midstate[32];
	uint32_t junk[8];
	uint32_t m7;
	uint32_t ntime;
	uint32_t nbits;
	uint32_t nnonce;
};

struct freq_stat {
	double *mh;
	double *s;
	int osc6_min;
	int osc6_max;
	double omh;
	double os;
	int best_osc;
	int best_done;
};

struct bitfury_device {
	struct spi_port *spi;
	uint8_t osc6_bits;
	uint32_t newbuf[17];
	uint32_t oldbuf[17];
	bool oldjob;
	int active;
	int chipgen;
	int chipgen_probe;
	uint32_t atrvec[20];
	struct bitfury_payload payload;
	struct freq_stat chip_stat;
	struct timeval timer1;
	struct timeval tv_stat;
	uint32_t counter1, counter2;
	double mhz;
	int mhz_last;
	int mhz_best;
	uint32_t slot;
	unsigned fasync;
	unsigned strange_counter;
	bool force_reinit;
	int desync_counter;
	int sample_hwe;
	int sample_tot;
};

extern void work_to_bitfury_payload(struct bitfury_payload *, struct work *);
extern void bitfury_payload_to_atrvec(uint32_t *atrvec, struct bitfury_payload *);
extern void bitfury_send_reinit(struct spi_port *, int slot, int chip_n, int n);
extern void bitfury_send_shutdown(struct spi_port *, int slot, int chip_n);
extern void bitfury_send_freq(struct spi_port *, int slot, int chip_n, int bits);
extern int libbitfury_detectChips1(struct spi_port *);
extern uint32_t bitfury_decnonce(uint32_t);
extern bool bitfury_fudge_nonce(const void *midstate, const uint32_t m7, const uint32_t ntime, const uint32_t nbits, uint32_t *nonce_p);

#endif /* __LIBBITFURY_H__ */
