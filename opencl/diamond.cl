/*
 * GroestlCoin kernel implementation.
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

#ifndef GROESTLCOIN_CL
#define GROESTLCOIN_CL

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

#ifndef SPH_LUFFA_PARALLEL
  #define SPH_LUFFA_PARALLEL 0
#endif

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


  // groestl
  {
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

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    g[u] = m[u] ^ H[u];
  m[10] = 0x80; g[10] = m[10] ^ H[10];
  m[11] = 0; g[11] = m[11] ^ H[11];
  m[12] = 0; g[12] = m[12] ^ H[12];
  m[13] = 0; g[13] = m[13] ^ H[13];
  m[14] = 0; g[14] = m[14] ^ H[14];
  m[15] = 0x100000000000000; g[15] = m[15] ^ H[15];
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

//#pragma unroll 15
	for (unsigned int u = 0; u < 15; u ++)
    H[u] = 0;
  #if USE_LE
    H[15] = ((sph_u64)(512 & 0xFF) << 56) | ((sph_u64)(512 & 0xFF00) << 40);
  #else
    H[15] = (sph_u64)512;
  #endif

  m[0] = hash.h8[0];
  m[1] = hash.h8[1];
  m[2] = hash.h8[2];
  m[3] = hash.h8[3];
  m[4] = hash.h8[4];
  m[5] = hash.h8[5];
  m[6] = hash.h8[6];
  m[7] = hash.h8[7];

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    g[u] = m[u] ^ H[u];
  m[8] = 0x80; g[8] = m[8] ^ H[8];
  m[9] = 0; g[9] = m[9] ^ H[9];
  m[10] = 0; g[10] = m[10] ^ H[10];
  m[11] = 0; g[11] = m[11] ^ H[11];
  m[12] = 0; g[12] = m[12] ^ H[12];
  m[13] = 0; g[13] = m[13] ^ H[13];
  m[14] = 0; g[14] = m[14] ^ H[14];
  m[15] = 0x100000000000000; g[15] = m[15] ^ H[15];
  PERM_BIG_P(g);
  PERM_BIG_Q(m);

//#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    H[u] ^= g[u] ^ m[u];

 //#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    xH[u] = H[u];
  PERM_BIG_P(xH);

 //#pragma unroll 16
  for (unsigned int u = 0; u < 16; u ++)
    H[u] ^= xH[u];

//#pragma unroll 8
    for (unsigned int u = 0; u < 8; u ++)
    hash.h8[u] = H[u + 8];

  barrier(CLK_GLOBAL_MEM_FENCE);
  }

  bool result = (hash.h8[3] <= target);
  if (result)
    output[output[0xFF]++] = SWAP4(gid);
}

#endif // GROESTLCOIN_CL
