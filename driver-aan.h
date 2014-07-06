#ifndef BFG_DRIVER_AAN
#define BFG_DRIVER_AAN

#include <stdbool.h>
#include <stdint.h>

#include "lowl-spi.h"
#include "miner.h"

#define AAN_ALL_CHIPS  0
#define AAN_MAX_JOBID  4

struct aan_hooks {
	void (*precmd)(struct spi_port *);
};

struct aan_board_data {
	struct spi_port *spi;
	struct timeval tv_next_poll;
	struct cgpu_info *master_dev;
	
	// Master board only
	int queues_empty;
};

struct aan_chip_data {
	uint8_t chipid;
	int8_t last_jobid;
	struct work *works[AAN_MAX_JOBID];
	float desired_nonce_pdiff;
	float current_nonce_pdiff;
};

extern int aan_detect_spi(int *out_chipcount, struct spi_port * const *spi_a, int spi_n);
extern bool aan_init(struct thr_info *);
extern bool aan_queue_append(struct thr_info *, struct work *);
extern void aan_queue_flush(struct thr_info *);
extern void aan_poll(struct thr_info *);

#endif
