#ifndef BFG_FINDNONCE_H
#define BFG_FINDNONCE_H

#include <stdint.h>

#include "driver-opencl.h"
#include "miner.h"
#include "config.h"

#define MAXTHREADS (0xFFFFFFFEULL)
#define MAXBUFFERS (0x10)
#define BUFFERSIZE (sizeof(uint32_t) * MAXBUFFERS)
#define FOUND (0x0F)

#ifdef USE_SCRYPT
#define SCRYPT_MAXBUFFERS (0x100)
#define SCRYPT_BUFFERSIZE (sizeof(uint32_t) * SCRYPT_MAXBUFFERS)
#define SCRYPT_FOUND (0xFF)

#define OPENCL_MAX_BUFFERSIZE  SCRYPT_BUFFERSIZE
#else
#define OPENCL_MAX_BUFFERSIZE  BUFFERSIZE
#endif

#ifdef USE_SHA256D
extern void precalc_hash(struct opencl_work_data *blk, uint32_t *state, uint32_t *data);
#endif
extern void postcalc_hash_async(struct thr_info *thr, struct work *work, uint32_t *res, enum cl_kernels);

#endif /*__FINDNONCE_H__*/
