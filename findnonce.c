/*
 * Copyright 2011 Con Kolivas
 * Copyright 2011 Nils Schneider
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.  See COPYING for more details.
 */

#include "config.h"
#ifdef HAVE_OPENCL

#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>
#include <string.h>

#include "ocl.h"
#include "findnonce.h"
#include "miner.h"

const uint32_t SHA256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t ByteReverse(uint32_t value)
{
	__asm__ ("bswap %0" : "=r" (value) : "0" (value));
	return value;
}

#define rotate(x,y) ((x<<y) | (x>>(sizeof(x)*8-y)))
#define rotr(x,y) ((x>>y) | (x<<(sizeof(x)*8-y)))

#define R(a, b, c, d, e, f, g, h, w, k) \
	h = h + (rotate(e, 26) ^ rotate(e, 21) ^ rotate(e, 7)) + (g ^ (e & (f ^ g))) + k + w; \
	d = d + h; \
	h = h + (rotate(a, 30) ^ rotate(a, 19) ^ rotate(a, 10)) + ((a & b) | (c & (a | b)))

void precalc_hash(dev_blk_ctx *blk, uint32_t *state, uint32_t *data) {
	cl_uint A, B, C, D, E, F, G, H;
	
	A = state[0];
	B = state[1];
	C = state[2];
	D = state[3];
	E = state[4];
	F = state[5];
	G = state[6];
	H = state[7];

	R(A, B, C, D, E, F, G, H, data[0], SHA256_K[0]); 
	R(H, A, B, C, D, E, F, G, data[1], SHA256_K[1]);
	R(G, H, A, B, C, D, E, F, data[2], SHA256_K[2]);

	blk->cty_a = A;
	blk->cty_b = B;
	blk->cty_c = C;

	blk->C1addK5 = C + 0x59f111f1;

	blk->cty_d = D;

	blk->D1A = D + 0xb956c25b;

	blk->cty_e = E;
	blk->cty_f = F;
	blk->cty_g = G;
	blk->cty_h = H;

	blk->ctx_a = state[0];
	blk->ctx_b = state[1];
	blk->ctx_c = state[2];
	blk->ctx_d = state[3];
	blk->ctx_e = state[4];
	blk->ctx_f = state[5];
	blk->ctx_g = state[6];
	blk->ctx_h = state[7];

	blk->merkle = data[0];
	blk->ntime = data[1];
	blk->nbits = data[2];
	
	blk->W16 = blk->fW0 = data[0] + (rotr(data[1], 7) ^ rotr(data[1], 18) ^ (data[1] >> 3));
	blk->W17 = blk->fW1 = data[1] + (rotr(data[2], 7) ^ rotr(data[2], 18) ^ (data[2] >> 3)) + 0x01100000;
	blk->PreVal4 = blk->fcty_e = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + (D ^ (B & (C ^ D))) + 0xe9b5dba5;
	blk->T1 = blk->fcty_e2 = (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
	blk->PreVal4_2 = blk->PreVal4 + blk->T1;
	blk->PreVal0 = blk->PreVal4 + state[0];
	blk->PreW31 = 0x00000280 + (rotr(blk->W16,  7) ^ rotr(blk->W16, 18) ^ (blk->W16 >> 3));
	blk->PreW32 = blk->W16 + ((rotr(blk->W17, 7) ^ rotr(blk->W17, 18) ^ (blk->W17 >> 3)));
	blk->PreW18 = data[2] + (rotr(blk->W16, 17) ^ rotr(blk->W16, 19) ^ (blk->W16 >> 10));
	blk->PreW19 = 0x11002000 + (rotr(blk->W17, 17) ^ rotr(blk->W17, 19) ^ (blk->W17 >> 10));
	
	
	blk->W2 = data[2];

	blk->W2A = blk->W2 + (rotr(blk->W16, 19) ^ rotr(blk->W16, 17) ^ (blk->W16 >> 10));
	blk->W17_2 = 0x11002000 + (rotr(blk->W17, 19) ^ rotr(blk->W17, 17) ^ (blk->W17 >> 10));

	blk->fW2 = data[2] + (rotr(blk->fW0, 17) ^ rotr(blk->fW0, 19) ^ (blk->fW0 >> 10));
	blk->fW3 = 0x11002000 + (rotr(blk->fW1, 17) ^ rotr(blk->fW1, 19) ^ (blk->fW1 >> 10));
	blk->fW15 = 0x00000280 + (rotr(blk->fW0, 7) ^ rotr(blk->fW0, 18) ^ (blk->fW0 >> 3));
	blk->fW01r = blk->fW0 + (rotr(blk->fW1, 7) ^ rotr(blk->fW1, 18) ^ (blk->fW1 >> 3));

	
	blk->PreVal4addT1 = blk->PreVal4 + blk->T1;
	blk->T1substate0 = state[0] - blk->T1;
}

#define P(t) (W[(t)&0xF] = W[(t-16)&0xF] + (rotate(W[(t-15)&0xF], 25) ^ rotate(W[(t-15)&0xF], 14) ^ (W[(t-15)&0xF] >> 3)) + W[(t-7)&0xF] + (rotate(W[(t-2)&0xF], 15) ^ rotate(W[(t-2)&0xF], 13) ^ (W[(t-2)&0xF] >> 10)))

#define IR(u) \
  R(A, B, C, D, E, F, G, H, W[u+0], SHA256_K[u+0]); \
  R(H, A, B, C, D, E, F, G, W[u+1], SHA256_K[u+1]); \
  R(G, H, A, B, C, D, E, F, W[u+2], SHA256_K[u+2]); \
  R(F, G, H, A, B, C, D, E, W[u+3], SHA256_K[u+3]); \
  R(E, F, G, H, A, B, C, D, W[u+4], SHA256_K[u+4]); \
  R(D, E, F, G, H, A, B, C, W[u+5], SHA256_K[u+5]); \
  R(C, D, E, F, G, H, A, B, W[u+6], SHA256_K[u+6]); \
  R(B, C, D, E, F, G, H, A, W[u+7], SHA256_K[u+7])
#define FR(u) \
  R(A, B, C, D, E, F, G, H, P(u+0), SHA256_K[u+0]); \
  R(H, A, B, C, D, E, F, G, P(u+1), SHA256_K[u+1]); \
  R(G, H, A, B, C, D, E, F, P(u+2), SHA256_K[u+2]); \
  R(F, G, H, A, B, C, D, E, P(u+3), SHA256_K[u+3]); \
  R(E, F, G, H, A, B, C, D, P(u+4), SHA256_K[u+4]); \
  R(D, E, F, G, H, A, B, C, P(u+5), SHA256_K[u+5]); \
  R(C, D, E, F, G, H, A, B, P(u+6), SHA256_K[u+6]); \
  R(B, C, D, E, F, G, H, A, P(u+7), SHA256_K[u+7])

#define PIR(u) \
  R(F, G, H, A, B, C, D, E, W[u+3], SHA256_K[u+3]); \
  R(E, F, G, H, A, B, C, D, W[u+4], SHA256_K[u+4]); \
  R(D, E, F, G, H, A, B, C, W[u+5], SHA256_K[u+5]); \
  R(C, D, E, F, G, H, A, B, W[u+6], SHA256_K[u+6]); \
  R(B, C, D, E, F, G, H, A, W[u+7], SHA256_K[u+7])

#define PFR(u) \
  R(A, B, C, D, E, F, G, H, P(u+0), SHA256_K[u+0]); \
  R(H, A, B, C, D, E, F, G, P(u+1), SHA256_K[u+1]); \
  R(G, H, A, B, C, D, E, F, P(u+2), SHA256_K[u+2]); \
  R(F, G, H, A, B, C, D, E, P(u+3), SHA256_K[u+3]); \
  R(E, F, G, H, A, B, C, D, P(u+4), SHA256_K[u+4]); \
  R(D, E, F, G, H, A, B, C, P(u+5), SHA256_K[u+5])

struct pc_data {
	struct thr_info *thr;
	struct work *work;
	uint32_t res[MAXBUFFERS];
	pthread_t pth;
};

static void *postcalc_hash(void *userdata)
{
	struct pc_data *pcd = (struct pc_data *)userdata;
	struct thr_info *thr = pcd->thr;
	dev_blk_ctx *blk = &pcd->work->blk;
	struct work *work = pcd->work;

	cl_uint A, B, C, D, E, F, G, H;
	cl_uint W[16];
	cl_uint nonce = 0;
	int entry = 0;

	pthread_detach(pthread_self());
cycle:
	while (entry < MAXBUFFERS) {
		if (pcd->res[entry]) {
			nonce = pcd->res[entry++];
			break;
		}
		entry++;
	}
	if (entry == MAXBUFFERS)
		goto out;

	A = blk->cty_a; B = blk->cty_b;
	C = blk->cty_c; D = blk->cty_d;
	E = blk->cty_e; F = blk->cty_f;
	G = blk->cty_g; H = blk->cty_h;
	W[0] = blk->merkle; W[1] = blk->ntime;
	W[2] = blk->nbits; W[3] = nonce;;
	W[4] = 0x80000000; W[5] = 0x00000000; W[6] = 0x00000000; W[7] = 0x00000000;
	W[8] = 0x00000000; W[9] = 0x00000000; W[10] = 0x00000000; W[11] = 0x00000000;
	W[12] = 0x00000000; W[13] = 0x00000000; W[14] = 0x00000000; W[15] = 0x00000280;
	PIR(0); IR(8);
	FR(16); FR(24);
	FR(32); FR(40);
	FR(48); FR(56);

	W[0] = A + blk->ctx_a; W[1] = B + blk->ctx_b;
	W[2] = C + blk->ctx_c; W[3] = D + blk->ctx_d;
	W[4] = E + blk->ctx_e; W[5] = F + blk->ctx_f;
	W[6] = G + blk->ctx_g; W[7] = H + blk->ctx_h;
	W[8] = 0x80000000; W[9] = 0x00000000; W[10] = 0x00000000; W[11] = 0x00000000;
	W[12] = 0x00000000; W[13] = 0x00000000; W[14] = 0x00000000; W[15] = 0x00000100;
	A = 0x6a09e667; B = 0xbb67ae85;
	C = 0x3c6ef372; D = 0xa54ff53a;
	E = 0x510e527f; F = 0x9b05688c;
	G = 0x1f83d9ab; H = 0x5be0cd19;
	IR(0); IR(8);
	FR(16); FR(24);
	FR(32); FR(40);
	FR(48); PFR(56);

	if (likely(H == 0xA41F32E7)) {
		if (unlikely(submit_nonce(thr, work, nonce) == false)) {
			applog(LOG_ERR, "Failed to submit work, exiting");
			goto out;
		}
	} else {
		if (opt_debug)
			applog(LOG_DEBUG, "No best_g found! Error in OpenCL code?");
		hw_errors++;
		thr->cgpu->hw_errors++;
	}
	if (entry < MAXBUFFERS)
		goto cycle;
out:
	free(pcd);
	return NULL;
}

void postcalc_hash_async(struct thr_info *thr, struct work *work, uint32_t *res)
{
	struct pc_data *pcd = malloc(sizeof(struct pc_data));
	if (unlikely(!pcd)) {
		applog(LOG_ERR, "Failed to malloc pc_data in postcalc_hash_async");
		return;
	}
	pcd->work = calloc(1, sizeof(struct work));
	if (unlikely(!pcd->work)) {
		applog(LOG_ERR, "Failed to malloc work in postcalc_hash_async");
		return;
	}

	pcd->thr = thr;
	memcpy(pcd->work, work, sizeof(struct work));
	memcpy(&pcd->res, res, BUFFERSIZE);

	if (pthread_create(&pcd->pth, NULL, postcalc_hash, (void *)pcd)) {
		applog(LOG_ERR, "Failed to create postcalc_hash thread");
		return;
	}
}
#endif /* HAVE_OPENCL */
