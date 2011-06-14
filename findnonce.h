#define MAXTHREADS 2000000

#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#include "miner.h"

typedef struct {
    cl_uint ctx_a; cl_uint ctx_b; cl_uint ctx_c; cl_uint ctx_d;
    cl_uint ctx_e; cl_uint ctx_f; cl_uint ctx_g; cl_uint ctx_h;
    cl_uint cty_a; cl_uint cty_b; cl_uint cty_c; cl_uint cty_d;
    cl_uint cty_e; cl_uint cty_f; cl_uint cty_g; cl_uint cty_h;
    cl_uint merkle; cl_uint ntime; cl_uint nbits; cl_uint nonce;
	cl_uint fW0; cl_uint fW1; cl_uint fW2; cl_uint fW3; cl_uint fW15;
	cl_uint fW01r; cl_uint fcty_e; cl_uint fcty_e2;
} dev_blk_ctx;

extern void precalc_hash(dev_blk_ctx *blk, uint32_t *state, uint32_t *data);
extern uint32_t postcalc_hash(struct thr_info *thr, dev_blk_ctx *blk,
			      struct work *work, uint32_t start, uint32_t end,
			      uint32_t *best_nonce, unsigned int *h0count);
