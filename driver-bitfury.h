#ifndef BFG_DRIVER_BITFURY_H
#define BFG_DRIVER_BITFURY_H

#include <stdbool.h>
#include <stdint.h>

#include "miner.h"

extern bool bitfury_prepare(struct thr_info *);
extern bool bitfury_init_oldbuf(struct cgpu_info *);
extern void bitfury_init_dclk(struct cgpu_info *, uint8_t def, uint8_t max);

extern int64_t bitfury_scanHash(struct thr_info *);

extern bool bitfury_job_prepare(struct thr_info *, struct work *, uint64_t max_nonce);
extern void bitfury_do_io(struct thr_info *);
extern int64_t bitfury_job_process_results(struct thr_info *, struct work *, bool stopping);

extern void bitfury_shutdown(struct thr_info *);

#endif
