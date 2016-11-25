/*
 * MyriadCoin Groestl kernel implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2014  phm
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   phm <phm@inbox.com>
 */

#ifndef MYRIADCOIN_GROESTL_CL
#define MYRIADCOIN_GROESTL_CL

#if __ENDIAN_LITTLE__
#define SPH_LITTLE_ENDIAN 1
#else
#define SPH_BIG_ENDIAN 1
#endif

#define SPH_UPTR sph_u64

typedef unsigned int sph_u32;
typedef int sph_s32;
#ifndef __OPENCL_VERSION__
typedef unsigned long long sph_u64;
typedef long long sph_s64;
#else
typedef unsigned long sph_u64;
typedef long sph_s64;
#endif

#define SPH_64 1
#define SPH_64_TRUE 1

#define SPH_C32(x)    ((sph_u32)(x ## U))
#define SPH_T32(x) (as_uint(x))
#define SPH_ROTL32(x, n) rotate(as_uint(x), as_uint(n))
#define SPH_ROTR32(x, n)   SPH_ROTL32(x, (32 - (n)))

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x) (as_ulong(x))
#define SPH_ROTL64(x, n) rotate(as_ulong(x), (n) & 0xFFFFFFFFFFFFFFFFUL)
#define SPH_ROTR64(x, n)   SPH_ROTL64(x, (64 - (n)))

#define SPH_ECHO_64 1
#define SPH_SIMD_NOCOPY 0
#define SPH_CUBEHASH_UNROLL 0

#include "groestl.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define ENC64E(x) SWAP8(x)
  #define DEC64E(x) SWAP8(*(const __global sph_u64 *) (x));
#else
  #define ENC64E(x) (x)
  #define DEC64E(x) (*(const __global sph_u64 *) (x));
#endif

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

#define CONST_EXP2  q[i+0] + SPH_ROTL64(q[i+1], 5)  + q[i+2] + SPH_ROTL64(q[i+3], 11) + \
                    q[i+4] + SPH_ROTL64(q[i+5], 27) + q[i+6] + SPH_ROTL64(q[i+7], 32) + \
                    q[i+8] + SPH_ROTL64(q[i+9], 37) + q[i+10] + SPH_ROTL64(q[i+11], 43) + \
                    q[i+12] + SPH_ROTL64(q[i+13], 53) + (SHR(q[i+14],1) ^ q[i+14]) + (SHR(q[i+15],2) ^ q[i+15])

#define ROL32(x, n)  rotate(x, (uint) n)
#define SHR(x, n)    ((x) >> n)
#define SWAP32(a)    (as_uint(as_uchar4(a).wzyx))

#define S0(x) (ROL32(x, 25) ^ ROL32(x, 14) ^  SHR(x, 3))
#define S1(x) (ROL32(x, 15) ^ ROL32(x, 13) ^  SHR(x, 10))

#define S2(x) (ROL32(x, 30) ^ ROL32(x, 19) ^ ROL32(x, 10))
#define S3(x) (ROL32(x, 26) ^ ROL32(x, 21) ^ ROL32(x, 7))

#define P(a,b,c,d,e,f,g,h,x,K)                  \
{                                               \
  temp1 = h + S3(e) + F1(e,f,g) + (K + x);      \
  d += temp1; h = temp1 + S2(a) + F0(a,b,c);  \
}

#define PLAST(a,b,c,d,e,f,g,h,x,K)                  \
{                                               \
  d += h + S3(e) + F1(e,f,g) + (x + K);              \
}

#define F0(y, x, z) bitselect(z, y, z ^ x)
#define F1(x, y, z) bitselect(z, y, x)

#define R0 (W0 = S1(W14) + W9 + S0(W1) + W0)
#define R1 (W1 = S1(W15) + W10 + S0(W2) + W1)
#define R2 (W2 = S1(W0) + W11 + S0(W3) + W2)
#define R3 (W3 = S1(W1) + W12 + S0(W4) + W3)
#define R4 (W4 = S1(W2) + W13 + S0(W5) + W4)
#define R5 (W5 = S1(W3) + W14 + S0(W6) + W5)
#define R6 (W6 = S1(W4) + W15 + S0(W7) + W6)
#define R7 (W7 = S1(W5) + W0 + S0(W8) + W7)
#define R8 (W8 = S1(W6) + W1 + S0(W9) + W8)
#define R9 (W9 = S1(W7) + W2 + S0(W10) + W9)
#define R10 (W10 = S1(W8) + W3 + S0(W11) + W10)
#define R11 (W11 = S1(W9) + W4 + S0(W12) + W11)
#define R12 (W12 = S1(W10) + W5 + S0(W13) + W12)
#define R13 (W13 = S1(W11) + W6 + S0(W14) + W13)
#define R14 (W14 = S1(W12) + W7 + S0(W15) + W14)
#define R15 (W15 = S1(W13) + W8 + S0(W0) + W15)

#define RD14 (S1(W12) + W7 + S0(W15) + W14)
#define RD15 (S1(W13) + W8 + S0(W0) + W15)

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, volatile __global uint* output, const ulong target)
{
  uint gid = get_global_id(0);
  union {
    unsigned char h1[64];
    uint h4[16];
    ulong h8[8];
  } hash;

#if !SPH_SMALL_FOOTPRINT_GROESTL
  __local sph_u64 T0_C[256], T1_C[256], T2_C[256], T3_C[256];
  __local sph_u64 T4_C[256], T5_C[256], T6_C[256], T7_C[256];
#else
  __local sph_u64 T0_C[256], T4_C[256];
#endif
  int init = get_local_id(0);
  int step = get_local_size(0);

  for (int i = init; i < 256; i += step)
  {
    T0_C[i] = T0[i];
    T4_C[i] = T4[i];
#if !SPH_SMALL_FOOTPRINT_GROESTL
    T1_C[i] = T1[i];
    T2_C[i] = T2[i];
    T3_C[i] = T3[i];
    T5_C[i] = T5[i];
    T6_C[i] = T6[i];
    T7_C[i] = T7[i];
#endif
  }
  barrier(CLK_LOCAL_MEM_FENCE);    // groestl
#define T0 T0_C
#define T1 T1_C
#define T2 T2_C
#define T3 T3_C
#define T4 T4_C
#define T5 T5_C
#define T6 T6_C
#define T7 T7_C


  sph_u64 H[16];
//#pragma unroll 15
  for (unsigned int u = 0; u < 15; u ++)
    H[u] = 0;
#if USE_LE
  H[15] = ((sph_u64)(512 & 0xFF) << 56) | ((sph_u64)(512 & 0xFF00) << 40);
#else
  H[15] = (sph_u64)512;
#endif

  sph_u64 g[16], m[16];
  m[0] = DEC64E(block + 0 * 8);
  m[1] = DEC64E(block + 1 * 8);
  m[2] = DEC64E(block + 2 * 8);
  m[3] = DEC64E(block + 3 * 8);
  m[4] = DEC64E(block + 4 * 8);
  m[5] = DEC64E(block + 5 * 8);
  m[6] = DEC64E(block + 6 * 8);
  m[7] = DEC64E(block + 7 * 8);
  m[8] = DEC64E(block + 8 * 8);
  m[9] = DEC64E(block + 9 * 8);
  m[9] &= 0x00000000FFFFFFFF;
  m[9] |= ((sph_u64) gid << 32);
  m[10] = 0x80;
  m[11] = 0;
  m[12] = 0;
  m[13] = 0;
  m[14] = 0;
  m[15] = 0x100000000000000;

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    g[u] = m[u] ^ H[u];

  PERM_BIG_P(g);
  PERM_BIG_Q(m);

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    H[u] ^= g[u] ^ m[u];
  sph_u64 xH[16];

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    xH[u] = H[u];
  PERM_BIG_P(xH);

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    H[u] ^= xH[u];

//#pragma unroll 8
  for (unsigned int u = 0; u < 8; u ++)
    hash.h8[u] = ENC64E(H[u + 8]);
    barrier(CLK_GLOBAL_MEM_FENCE);

  uint temp1;
  uint W0 = SWAP32(hash.h4[0x0]);
  uint W1 = SWAP32(hash.h4[0x1]);
  uint W2 = SWAP32(hash.h4[0x2]);
  uint W3 = SWAP32(hash.h4[0x3]);
  uint W4 = SWAP32(hash.h4[0x4]);
  uint W5 = SWAP32(hash.h4[0x5]);
  uint W6 = SWAP32(hash.h4[0x6]);
  uint W7 = SWAP32(hash.h4[0x7]);
  uint W8 = SWAP32(hash.h4[0x8]);
  uint W9 = SWAP32(hash.h4[0x9]);
  uint W10 = SWAP32(hash.h4[0xA]);
  uint W11 = SWAP32(hash.h4[0xB]);
  uint W12 = SWAP32(hash.h4[0xC]);
  uint W13 = SWAP32(hash.h4[0xD]);
  uint W14 = SWAP32(hash.h4[0xE]);
  uint W15 = SWAP32(hash.h4[0xF]);

  uint v0 = 0x6A09E667;
  uint v1 = 0xBB67AE85;
  uint v2 = 0x3C6EF372;
  uint v3 = 0xA54FF53A;
  uint v4 = 0x510E527F;
  uint v5 = 0x9B05688C;
  uint v6 = 0x1F83D9AB;
  uint v7 = 0x5BE0CD19;

  P( v0, v1, v2, v3, v4, v5, v6, v7, W0, 0x428A2F98 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, W1, 0x71374491 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, W2, 0xB5C0FBCF );
  P( v5, v6, v7, v0, v1, v2, v3, v4, W3, 0xE9B5DBA5 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, W4, 0x3956C25B );
  P( v3, v4, v5, v6, v7, v0, v1, v2, W5, 0x59F111F1 );
  P( v2, v3, v4, v5, v6, v7, v0, v1, W6, 0x923F82A4 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, W7, 0xAB1C5ED5 );
  P( v0, v1, v2, v3, v4, v5, v6, v7, W8, 0xD807AA98 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, W9, 0x12835B01 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, W10, 0x243185BE );
  P( v5, v6, v7, v0, v1, v2, v3, v4, W11, 0x550C7DC3 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, W12, 0x72BE5D74 );
  P( v3, v4, v5, v6, v7, v0, v1, v2, W13, 0x80DEB1FE );
  P( v2, v3, v4, v5, v6, v7, v0, v1, W14, 0x9BDC06A7 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, W15, 0xC19BF174 );

  P( v0, v1, v2, v3, v4, v5, v6, v7, R0, 0xE49B69C1 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, R1, 0xEFBE4786 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x0FC19DC6 );
  P( v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x240CA1CC );
  P( v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x2DE92C6F );
  P( v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x4A7484AA );
  P( v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x5CB0A9DC );
  P( v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x76F988DA );
  P( v0, v1, v2, v3, v4, v5, v6, v7, R8, 0x983E5152 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, R9, 0xA831C66D );
  P( v6, v7, v0, v1, v2, v3, v4, v5, R10, 0xB00327C8 );
  P( v5, v6, v7, v0, v1, v2, v3, v4, R11, 0xBF597FC7 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, R12, 0xC6E00BF3 );
  P( v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xD5A79147 );
  P( v2, v3, v4, v5, v6, v7, v0, v1, R14, 0x06CA6351 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, R15, 0x14292967 );

  P( v0, v1, v2, v3, v4, v5, v6, v7, R0,  0x27B70A85 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, R1,  0x2E1B2138 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, R2,  0x4D2C6DFC );
  P( v5, v6, v7, v0, v1, v2, v3, v4, R3,  0x53380D13 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, R4,  0x650A7354 );
  P( v3, v4, v5, v6, v7, v0, v1, v2, R5,  0x766A0ABB );
  P( v2, v3, v4, v5, v6, v7, v0, v1, R6,  0x81C2C92E );
  P( v1, v2, v3, v4, v5, v6, v7, v0, R7,  0x92722C85 );
  P( v0, v1, v2, v3, v4, v5, v6, v7, R8,  0xA2BFE8A1 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, R9,  0xA81A664B );
  P( v6, v7, v0, v1, v2, v3, v4, v5, R10, 0xC24B8B70 );
  P( v5, v6, v7, v0, v1, v2, v3, v4, R11, 0xC76C51A3 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, R12, 0xD192E819 );
  P( v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xD6990624 );
  P( v2, v3, v4, v5, v6, v7, v0, v1, R14, 0xF40E3585 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, R15, 0x106AA070 );

  P( v0, v1, v2, v3, v4, v5, v6, v7, R0,  0x19A4C116 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, R1,  0x1E376C08 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, R2,  0x2748774C );
  P( v5, v6, v7, v0, v1, v2, v3, v4, R3,  0x34B0BCB5 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, R4,  0x391C0CB3 );
  P( v3, v4, v5, v6, v7, v0, v1, v2, R5,  0x4ED8AA4A );
  P( v2, v3, v4, v5, v6, v7, v0, v1, R6,  0x5B9CCA4F );
  P( v1, v2, v3, v4, v5, v6, v7, v0, R7,  0x682E6FF3 );
  P( v0, v1, v2, v3, v4, v5, v6, v7, R8,  0x748F82EE );
  P( v7, v0, v1, v2, v3, v4, v5, v6, R9,  0x78A5636F );
  P( v6, v7, v0, v1, v2, v3, v4, v5, R10, 0x84C87814 );
  P( v5, v6, v7, v0, v1, v2, v3, v4, R11, 0x8CC70208 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, R12, 0x90BEFFFA );
  P( v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xA4506CEB );
  P( v2, v3, v4, v5, v6, v7, v0, v1, RD14, 0xBEF9A3F7 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, RD15, 0xC67178F2 );

  v0 += 0x6A09E667;
  uint s0 = v0;
  v1 += 0xBB67AE85;
  uint s1 = v1;
  v2 += 0x3C6EF372;
  uint s2 = v2;
  v3 += 0xA54FF53A;
  uint s3 = v3;
  v4 += 0x510E527F;
  uint s4 = v4;
  v5 += 0x9B05688C;
  uint s5 = v5;
  v6 += 0x1F83D9AB;
  uint s6 = v6;
  v7 += 0x5BE0CD19;
  uint s7 = v7;

  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x80000000, 0x428A2F98 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0, 0x71374491 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0, 0xB5C0FBCF );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0, 0xE9B5DBA5 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0, 0x3956C25B );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0, 0x59F111F1 );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0, 0x923F82A4 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 0, 0xAB1C5ED5 );
  P( v0, v1, v2, v3, v4, v5, v6, v7, 0, 0xD807AA98 );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0, 0x12835B01 );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0, 0x243185BE );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0, 0x550C7DC3 );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0, 0x72BE5D74 );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0, 0x80DEB1FE );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0, 0x9BDC06A7 );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 512, 0xC19BF174 );

  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x80000000U, 0xE49B69C1U );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0x01400000U, 0xEFBE4786U );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0x00205000U, 0x0FC19DC6U );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0x00005088U, 0x240CA1CCU );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0x22000800U, 0x2DE92C6FU );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0x22550014U, 0x4A7484AAU );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0x05089742U, 0x5CB0A9DCU );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 0xa0000020U, 0x76F988DAU );
  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x5a880000U, 0x983E5152U );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0x005c9400U, 0xA831C66DU );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0x0016d49dU, 0xB00327C8U );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0xfa801f00U, 0xBF597FC7U );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0xd33225d0U, 0xC6E00BF3U );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0x11675959U, 0xD5A79147U );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0xf6e6bfdaU, 0x06CA6351U );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 0xb30c1549U, 0x14292967U );
  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x08b2b050U, 0x27B70A85U );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0x9d7c4c27U, 0x2E1B2138U );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0x0ce2a393U, 0x4D2C6DFCU );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0x88e6e1eaU, 0x53380D13U );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0xa52b4335U, 0x650A7354U );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0x67a16f49U, 0x766A0ABBU );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0xd732016fU, 0x81C2C92EU );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 0x4eeb2e91U, 0x92722C85U );
  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x5dbf55e5U, 0xA2BFE8A1U );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0x8eee2335U, 0xA81A664BU );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0xe2bc5ec2U, 0xC24B8B70U );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0xa83f4394U, 0xC76C51A3U );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0x45ad78f7U, 0xD192E819U );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0x36f3d0cdU, 0xD6990624U );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0xd99c05e8U, 0xF40E3585U );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 0xb0511dc7U, 0x106AA070U );
  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x69bc7ac4U, 0x19A4C116U );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0xbd11375bU, 0x1E376C08U );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0xe3ba71e5U, 0x2748774CU );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0x3b209ff2U, 0x34B0BCB5U );
  P( v4, v5, v6, v7, v0, v1, v2, v3, 0x18feee17U, 0x391C0CB3U );
  P( v3, v4, v5, v6, v7, v0, v1, v2, 0xe25ad9e7U, 0x4ED8AA4AU );
  P( v2, v3, v4, v5, v6, v7, v0, v1, 0x13375046U, 0x5B9CCA4FU );
  P( v1, v2, v3, v4, v5, v6, v7, v0, 0x0515089dU, 0x682E6FF3U );
  P( v0, v1, v2, v3, v4, v5, v6, v7, 0x4f0d0f04U, 0x748F82EEU );
  P( v7, v0, v1, v2, v3, v4, v5, v6, 0x2627484eU, 0x78A5636FU );
  P( v6, v7, v0, v1, v2, v3, v4, v5, 0x310128d2U, 0x84C87814U );
  P( v5, v6, v7, v0, v1, v2, v3, v4, 0xc668b434U, 0x8CC70208U );
  PLAST( v4, v5, v6, v7, v0, v1, v2, v3, 0x420841ccU, 0x90BEFFFAU );

  hash.h4[0] = SWAP4(v0 + s0);
  hash.h4[1] = SWAP4(v1 + s1);
  hash.h4[2] = SWAP4(v2 + s2);
  hash.h4[3] = SWAP4(v3 + s3);
  hash.h4[4] = SWAP4(v4 + s4);
  hash.h4[5] = SWAP4(v5 + s5);
  hash.h4[6] = SWAP4(v6 + s6);
  hash.h4[7] = SWAP4(v7 + s7);

  bool result = (hash.h8[3] <= target);
  if (result)
    output[output[0xFF]++] = SWAP4(gid);
}

#endif // MYRIADCOIN_GROESTL_CL