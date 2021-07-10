// Copyright 2010 Satoshi Nakamoto
// Copyright 2011 Gilles Risch
// Copyright 2012-2013 Luke Dashjr
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.


// 4-way 128-bit Altivec SHA-256,
// based on tcatm's 4-way 128-bit SSE2 SHA-256
//


#include "config.h"

#include "driver-cpu.h"

#ifdef WANT_ALTIVEC_4WAY

#include <stdbool.h>
#include <string.h>
#include <assert.h>

//#include <altivec.h>
#include <stdint.h>
#include <stdio.h>

#define NPAR 32

static void DoubleBlockSHA256(const void* pin, void* pout, const void* pinit, unsigned int hash[8][NPAR], const void* init2);

static const unsigned int sha256_consts[] = {
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


static inline vector unsigned int Ch(const vector unsigned int b, const vector unsigned int c, const vector unsigned int d) {
    return vec_sel(d,c,b);
}

static inline vector unsigned int Maj(const vector unsigned int b, const vector unsigned int c, const vector unsigned int d) {
    return vec_sel(b,c, vec_xor(b,d));
}

/* RotateRight(x, n) := RotateLeft(x, 32-n) */
/* SHA256 Functions */
#define BIGSIGMA0_256(x)    (vec_xor(vec_xor(vec_rl((x), (vector unsigned int)(32-2)),vec_rl((x), (vector unsigned int)(32-13))),vec_rl((x), (vector unsigned int)(32-22))))
#define BIGSIGMA1_256(x)    (vec_xor(vec_xor(vec_rl((x), (vector unsigned int)(32-6)),vec_rl((x), (vector unsigned int)(32-11))),vec_rl((x), (vector unsigned int)(32-25))))

#define SIGMA0_256(x)       (vec_xor(vec_xor(vec_rl((x), (vector unsigned int)(32- 7)),vec_rl((x), (vector unsigned int)(32-18))), vec_sr((x), (vector unsigned int)(3 ))))
#define SIGMA1_256(x)       (vec_xor(vec_xor(vec_rl((x), (vector unsigned int)(32-17)),vec_rl((x), (vector unsigned int)(32-19))), vec_sr((x), (vector unsigned int)(10))))

#define add4(x0, x1, x2, x3) vec_add(vec_add(x0, x1),vec_add( x2,x3))
#define add5(x0, x1, x2, x3, x4) vec_add(add4(x0, x1, x2, x3), x4)

#define SHA256ROUND(a, b, c, d, e, f, g, h, i, w)                       \
    T1 = add5(h, BIGSIGMA1_256(e), Ch(e, f, g), (vector unsigned int)(sha256_consts[i],sha256_consts[i],sha256_consts[i],sha256_consts[i]), w);   \
    d = vec_add(d, T1);                                           \
    h = vec_add(T1, vec_add(BIGSIGMA0_256(a), Maj(a, b, c)));


static const unsigned int pSHA256InitState[8] =
{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};


bool ScanHash_altivec_4way(struct thr_info * const thr, struct work * const work,
	uint32_t max_nonce, uint32_t *last_nonce,
	uint32_t nonce)
{
	const uint8_t * const pmidstate = work->midstate;
	uint8_t *pdata = work->data;
	uint8_t hash1[0x40];
	memcpy(hash1, hash1_init, sizeof(hash1));
	uint8_t * const phash = work->hash;
	
	uint32_t *hash32 = (uint32_t *)phash;
    unsigned int *nNonce_p = (unsigned int*)(pdata + 76);

	pdata += 64;

    for (;;)
    {
        unsigned int thash[9][NPAR] __attribute__((aligned(128)));
	int j;

	*nNonce_p = nonce;

        DoubleBlockSHA256(pdata, phash1, pmidstate, thash, pSHA256InitState);

        for (j = 0; j < NPAR; j++)
        {
            if (unlikely(thash[7][j] == 0))
            {
		int i;

                for (i = 0; i < 32/4; i++)
                    ((unsigned int*)phash)[i] = thash[i][j];

				if (unlikely(hash32[7] == 0))
				{
					nonce += j;
					*last_nonce = nonce;
					*nNonce_p = nonce;
					return true;
				}
            }
        }

        if ((nonce >= max_nonce) || thr->work_restart)
        {
            *last_nonce = nonce;
            return false;
        }

        nonce += NPAR;
    }
}


static void DoubleBlockSHA256(const void* pin, void* pad, const void *pre, unsigned int thash[9][NPAR], const void *init)
{
    unsigned int* In = (unsigned int*)pin;
    unsigned int* Pad = (unsigned int*)pad;
    unsigned int* hPre = (unsigned int*)pre;
    unsigned int* hInit = (unsigned int*)init;
    unsigned int /* i, j, */ k;

    /* vectors used in calculation */
    vector unsigned int w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;
    vector unsigned int T1;
    vector unsigned int a, b, c, d, e, f, g, h;
    vector unsigned int nonce, preNonce;

    /* nonce offset for vector */
    vector unsigned int offset = (vector unsigned int)(0, 1, 2, 3);

    preNonce = vec_add((vector unsigned int)(In[3],In[3],In[3],In[3]), offset);

   for(k = 0; k<NPAR; k+=4)
   {
        w0 = (vector unsigned int)(In[0],In[0],In[0],In[0]);
        w1 = (vector unsigned int)(In[1],In[1],In[1],In[1]);
        w2 = (vector unsigned int)(In[2],In[2],In[2],In[2]);
        //w3 = (vector unsigned int)(In[3],In[3],In[3],In[3]); nonce will be later hacked into the hash
        w4 = (vector unsigned int)(In[4],In[4],In[4],In[4]);
        w5 = (vector unsigned int)(In[5],In[5],In[5],In[5]);
        w6 = (vector unsigned int)(In[6],In[6],In[6],In[6]);
        w7 = (vector unsigned int)(In[7],In[7],In[7],In[7]);
        w8 = (vector unsigned int)(In[8],In[8],In[8],In[8]);
        w9 = (vector unsigned int)(In[9],In[9],In[9],In[9]);
        w10 = (vector unsigned int)(In[10],In[10],In[10],In[10]);
        w11 = (vector unsigned int)(In[11],In[11],In[11],In[11]);
        w12 = (vector unsigned int)(In[12],In[12],In[12],In[12]);
        w13 = (vector unsigned int)(In[13],In[13],In[13],In[13]);
        w14 = (vector unsigned int)(In[14],In[14],In[14],In[14]);
        w15 = (vector unsigned int)(In[15],In[15],In[15],In[15]);

        /* hack nonce into lowest byte of w3 */
	nonce = vec_add(preNonce, (vector unsigned int)(k,k,k,k));

        w3 = nonce;
        //printf ("W3: %08vlx\n", w3);

        a = (vector unsigned int)(hPre[0],hPre[0],hPre[0],hPre[0]);
        b = (vector unsigned int)(hPre[1],hPre[1],hPre[1],hPre[1]);
        c = (vector unsigned int)(hPre[2],hPre[2],hPre[2],hPre[2]);
        d = (vector unsigned int)(hPre[3],hPre[3],hPre[3],hPre[3]);
        e = (vector unsigned int)(hPre[4],hPre[4],hPre[4],hPre[4]);
        f = (vector unsigned int)(hPre[5],hPre[5],hPre[5],hPre[5]);
        g = (vector unsigned int)(hPre[6],hPre[6],hPre[6],hPre[6]);
        h = (vector unsigned int)(hPre[7],hPre[7],hPre[7],hPre[7]);

        SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
        SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
        SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
        SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
        SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
        SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
        SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
        SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
        SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
        SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
        SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
        SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
        SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
        SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
        SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
        SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

        w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
        w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
        w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
        w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
        w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
        w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
        w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
        w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
        w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
        w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
        w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
        w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
        w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
        w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
        w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
        w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

        w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
        w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
        w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
        w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
        w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
        w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
        w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
        w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
        w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
        w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
        w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
        w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
        w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
        w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
        w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
        w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

        w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
        w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
        w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
        w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
        w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
        w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
        w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
        w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
        w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
        w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
        w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
        w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
        w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);
        w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
        w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
        w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);

#define store_load(x, i, dest) \
        T1 = (vector unsigned int)((hPre)[i],(hPre)[i],(hPre)[i],(hPre)[i]); \
        dest = vec_add(T1, x);

        store_load(a, 0, w0);
        store_load(b, 1, w1);
        store_load(c, 2, w2);
        store_load(d, 3, w3);
        store_load(e, 4, w4);
        store_load(f, 5, w5);
        store_load(g, 6, w6);
        store_load(h, 7, w7);

        /* end of first SHA256 round */

        w8 = (vector unsigned int)(Pad[8],Pad[8],Pad[8],Pad[8]);
        w9 = (vector unsigned int)(Pad[9],Pad[9],Pad[9],Pad[9]);
        w10 = (vector unsigned int)(Pad[10],Pad[10],Pad[10],Pad[10]);
        w11 = (vector unsigned int)(Pad[11],Pad[11],Pad[11],Pad[11]);
        w12 = (vector unsigned int)(Pad[12],Pad[12],Pad[12],Pad[12]);
        w13 = (vector unsigned int)(Pad[13],Pad[13],Pad[13],Pad[13]);
        w14 = (vector unsigned int)(Pad[14],Pad[14],Pad[14],Pad[14]);
        w15 = (vector unsigned int)(Pad[15],Pad[15],Pad[15],Pad[15]);

        a = (vector unsigned int)(hInit[0],hInit[0],hInit[0],hInit[0]);
        b = (vector unsigned int)(hInit[1],hInit[1],hInit[1],hInit[1]);
        c = (vector unsigned int)(hInit[2],hInit[2],hInit[2],hInit[2]);
        d = (vector unsigned int)(hInit[3],hInit[3],hInit[3],hInit[3]);
        e = (vector unsigned int)(hInit[4],hInit[4],hInit[4],hInit[4]);
        f = (vector unsigned int)(hInit[5],hInit[5],hInit[5],hInit[5]);
        g = (vector unsigned int)(hInit[6],hInit[6],hInit[6],hInit[6]);
        h = (vector unsigned int)(hInit[7],hInit[7],hInit[7],hInit[7]);

        SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
        SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
        SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
        SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
        SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
        SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
        SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
        SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
        SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
        SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
        SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
        SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
        SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
        SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
        SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
        SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

        w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
        w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
        w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
        w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
        w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
        w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
        w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
        w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
        w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
        w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
        w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
        w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
        w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
        w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
        w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
        w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

        w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
        w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
        w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
        w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
        w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
        w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
        w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
        w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
        w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
        w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
        w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
        w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
        w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
        w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
        w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
        w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

        w0 = add4(SIGMA1_256(w14), w9, SIGMA0_256(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
        w1 = add4(SIGMA1_256(w15), w10, SIGMA0_256(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
        w2 = add4(SIGMA1_256(w0), w11, SIGMA0_256(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
        w3 = add4(SIGMA1_256(w1), w12, SIGMA0_256(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
        w4 = add4(SIGMA1_256(w2), w13, SIGMA0_256(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
        w5 = add4(SIGMA1_256(w3), w14, SIGMA0_256(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
        w6 = add4(SIGMA1_256(w4), w15, SIGMA0_256(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
        w7 = add4(SIGMA1_256(w5), w0, SIGMA0_256(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
        w8 = add4(SIGMA1_256(w6), w1, SIGMA0_256(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
        w9 = add4(SIGMA1_256(w7), w2, SIGMA0_256(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
        w10 = add4(SIGMA1_256(w8), w3, SIGMA0_256(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
        w11 = add4(SIGMA1_256(w9), w4, SIGMA0_256(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
        w12 = add4(SIGMA1_256(w10), w5, SIGMA0_256(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);

	/* Skip last 3-rounds; not necessary for H==0 */
/*#if 0
        w13 = add4(SIGMA1_256(w11), w6, SIGMA0_256(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
        w14 = add4(SIGMA1_256(w12), w7, SIGMA0_256(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
        w15 = add4(SIGMA1_256(w13), w8, SIGMA0_256(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);
#endif*/

        /* store resulsts directly in thash */
#define store_2(x,i)  \
        w0 = (vector unsigned int)(hInit[i],hInit[i],hInit[i],hInit[i]); \
        vec_st(vec_add(w0, x), 0 ,&thash[i][k]);

        store_2(a, 0);
        store_2(b, 1);
        store_2(c, 2);
        store_2(d, 3);
        store_2(e, 4);
        store_2(f, 5);
        store_2(g, 6);
        store_2(h, 7);

        vec_st(nonce, 0 ,&thash[8][k]);
        /* writing the results into the array is time intensive */
        /* -> try if it´s faster to compare the results with the target inside this function */
    }

}

#endif /* WANT_ALTIVEC_4WAY */

