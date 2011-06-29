#ifndef __FINDNONCE_H__
#define __FINDNONCE_H__
#include "miner.h"

#define MAXTHREADS (0xFFFFFFFEULL)
/* Maximum worksize 512 * maximum vectors 4 plus one flag entry */
#define MAXBUFFERS (4 * 512)
#define BUFFERSIZE (sizeof(uint32_t) * (MAXBUFFERS + 1))

extern void precalc_hash(dev_blk_ctx *blk, uint32_t *state, uint32_t *data);
extern void postcalc_hash_async(struct thr_info *thr, struct work *work, uint32_t *res);
#endif /*__FINDNONCE_H__*/
