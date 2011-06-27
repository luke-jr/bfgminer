#ifndef __FINDNONCE_H__
#define __FINDNONCE_H__
#include "miner.h"

#define MAXTHREADS (0xFFFFFFFF)
#define BUFFERSIZE (sizeof(uint32_t) * 128)

extern void precalc_hash(dev_blk_ctx *blk, uint32_t *state, uint32_t *data);
extern void postcalc_hash_async(struct thr_info *thr, struct work *work, uint32_t start);
#endif /*__FINDNONCE_H__*/
