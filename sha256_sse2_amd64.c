/*
 * Copyright 2011 Mark Crichton
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include "config.h"

#include "driver-cpu.h"

#ifdef WANT_X8664_SSE2

#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <xmmintrin.h>
#include <stdint.h>
#include <stdio.h>

extern void sha256_sse2_64_new (__m128i *res, __m128i *res1, __m128i *data, const uint32_t init[8])__asm__("sha256_sse2_64_new");

static uint32_t g_sha256_k[]__attribute__((aligned(0x100))) = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, /*  0 */
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, /*  8 */
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, /* 16 */
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, /* 24 */
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, /* 32 */
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, /* 40 */
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, /* 48 */
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, /* 56 */
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


const uint32_t sha256_init_sse2[8]__asm__("sha256_init_sse2")__attribute__((aligned(0x100))) =
{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

__m128i g_4sha256_k[64];
__m128i sha256_consts_m128i[64]__asm__("sha256_consts_m128i")__attribute__((aligned(0x1000)));

bool scanhash_sse2_64(struct thr_info * const thr, struct work * const work,
	uint32_t max_nonce, uint32_t *last_nonce,
	uint32_t nonce)
{
	const uint8_t * const pmidstate = work->midstate;
	uint8_t *pdata = work->data;
	const uint32_t * const phash1 = hash1_init;
	uint8_t * const phash = work->hash;
	
	uint32_t *hash32 = (uint32_t *)phash;
    uint32_t *nNonce_p = (uint32_t *)(pdata + 76);
    uint32_t m_midstate[8], m_w[16], m_w1[16];
    __m128i m_4w[64] __attribute__ ((aligned (0x100)));
    __m128i m_4hash[64] __attribute__ ((aligned (0x100)));
    __m128i m_4hash1[64] __attribute__ ((aligned (0x100)));
    __m128i offset;
    int i;

	pdata += 64;

    /* For debugging */
    union {
        __m128i m;
        uint32_t i[4];
    } mi;

    /* Message expansion */
    memcpy(m_midstate, pmidstate, sizeof(m_midstate));
    memcpy(m_w, pdata, sizeof(m_w)); /* The 2nd half of the data */
    memcpy(m_w1, phash1, sizeof(m_w1));
    memset(m_4hash, 0, sizeof(m_4hash));

    /* Transmongrify */
    for (i = 0; i < 16; i++)
        m_4w[i] = _mm_set1_epi32(m_w[i]);

    for (i = 0; i < 16; i++)
        m_4hash1[i] = _mm_set1_epi32(m_w1[i]);

    for (i = 0; i < 64; i++)
	sha256_consts_m128i[i] = _mm_set1_epi32(g_sha256_k[i]);

    offset = _mm_set_epi32(0x3, 0x2, 0x1, 0x0);

    for (;;)
    {
	int j;

	m_4w[3] = _mm_add_epi32(offset, _mm_set1_epi32(nonce));

	sha256_sse2_64_new (m_4hash, m_4hash1, m_4w, m_midstate);

	for (j = 0; j < 4; j++) {
	    mi.m = m_4hash[7];
	    if (unlikely(mi.i[j] == 0))
		break;
        }

	/* If j = true, we found a hit...so check it */
	/* Use the C version for a check... */
	if (unlikely(j != 4)) {
		for (i = 0; i < 8; i++) {
		    mi.m = m_4hash[i];
		    *(uint32_t *)&(phash)[i*4] = mi.i[j];
		}

		if (unlikely(hash32[7] == 0))
		{
		     nonce += j;
		     *last_nonce = nonce + 1;
		     *nNonce_p = nonce;
		     return true;
		}
	}

        if (unlikely((nonce >= max_nonce) || thr->work_restart))
        {
			*last_nonce = nonce;
			return false;
	}

	nonce += 4;
   }
}

#endif /* WANT_X8664_SSE2 */

