#ifndef __FINDNONCE_H__
#define __FINDNONCE_H__
#include "miner.h"
#include "config.h"

#define MAXTHREADS (0xFFFFFFFEULL)
/* Maximum worksize 4k to match page size */
#define MAXBUFFERS (0xFF)
#define BUFFERSIZE (sizeof(uint32_t) * MAXBUFFERS)
#define FOUND (0x80)
/* #define NFLAG (0x7F) Just for reference */

#ifdef HAVE_OPENCL
extern void precalc_hash(dev_blk_ctx *blk, uint32_t *state, uint32_t *data);
extern void postcalc_hash_async(struct thr_info *thr, struct work *work, uint32_t *res);
#endif /* HAVE_OPENCL */
#endif /*__FINDNONCE_H__*/
