/*
 * TalkCoin kernel implementation.
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

#ifndef TALKCOIN_MOD_CL
#define TALKCOIN_MOD_CL

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
#define SPH_T32(x)    (as_uint(x))
#define SPH_ROTL32(x, n)   rotate(as_uint(x), as_uint(n))
#define SPH_ROTR32(x, n)   SPH_ROTL32(x, (32 - (n)))

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x)    (as_ulong(x))
#define SPH_ROTL64(x, n)   rotate(as_ulong(x), (n) & 0xFFFFFFFFFFFFFFFFUL)
#define SPH_ROTR64(x, n)   SPH_ROTL64(x, (64 - (n)))

#define SPH_GROESTL_BIG_ENDIAN 0
#define SPH_SMALL_FOOTPRINT_GROESTL 0
#define SPH_JH_64 1
#define SPH_KECCAK_64 1
#define SPH_KECCAK_NOCOPY 0
#define SPH_COMPACT_BLAKE_64 0
#ifndef SPH_KECCAK_UNROLL
  #define SPH_KECCAK_UNROLL   0
#endif

#include "blake.cl"
#include "groestl.cl"
#include "jh.cl"
#include "keccak.cl"
#include "skein.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
#endif


typedef union {
  unsigned char h1[64];
  uint h4[16];
  ulong h8[8];
} hash_t;

// blake
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, __global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  sph_u64 H0 = SPH_C64(0x6A09E667F3BCC908), H1 = SPH_C64(0xBB67AE8584CAA73B);
  sph_u64 H2 = SPH_C64(0x3C6EF372FE94F82B), H3 = SPH_C64(0xA54FF53A5F1D36F1);
  sph_u64 H4 = SPH_C64(0x510E527FADE682D1), H5 = SPH_C64(0x9B05688C2B3E6C1F);
  sph_u64 H6 = SPH_C64(0x1F83D9ABFB41BD6B), H7 = SPH_C64(0x5BE0CD19137E2179);
  sph_u64 S0 = 0, S1 = 0, S2 = 0, S3 = 0;
  sph_u64 T0 = SPH_C64(0xFFFFFFFFFFFFFC00) + (80 << 3), T1 = 0xFFFFFFFFFFFFFFFF;;

  if ((T0 = SPH_T64(T0 + 1024)) < 1024)
  {
  T1 = SPH_T64(T1 + 1);
  }
  sph_u64 M0, M1, M2, M3, M4, M5, M6, M7;
  sph_u64 M8, M9, MA, MB, MC, MD, ME, MF;
  sph_u64 V0, V1, V2, V3, V4, V5, V6, V7;
  sph_u64 V8, V9, VA, VB, VC, VD, VE, VF;
  M0 = DEC64BE(block + 0);
  M1 = DEC64BE(block + 8);
  M2 = DEC64BE(block + 16);
  M3 = DEC64BE(block + 24);
  M4 = DEC64BE(block + 32);
  M5 = DEC64BE(block + 40);
  M6 = DEC64BE(block + 48);
  M7 = DEC64BE(block + 56);
  M8 = DEC64BE(block + 64);
  M9 = DEC64BE(block + 72);
  M9 &= 0xFFFFFFFF00000000;
  M9 ^= SWAP4(gid);
  MA = 0x8000000000000000;
  MB = 0;
  MC = 0;
  MD = 1;
  ME = 0;
  MF = 0x280;

  COMPRESS64;

  hash->h8[0] = H0;
  hash->h8[1] = H1;
  hash->h8[2] = H2;
  hash->h8[3] = H3;
  hash->h8[4] = H4;
  hash->h8[5] = H5;
  hash->h8[6] = H6;
  hash->h8[7] = H7;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

// groestl
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search1(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

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
  m[0] = DEC64E(hash->h8[0]);
  m[1] = DEC64E(hash->h8[1]);
  m[2] = DEC64E(hash->h8[2]);
  m[3] = DEC64E(hash->h8[3]);
  m[4] = DEC64E(hash->h8[4]);
  m[5] = DEC64E(hash->h8[5]);
  m[6] = DEC64E(hash->h8[6]);
  m[7] = DEC64E(hash->h8[7]);

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
    hash->h8[u] = DEC64E(H[u + 8]);
    barrier(CLK_GLOBAL_MEM_FENCE);

}

// jh
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search2(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  sph_u64 h0h = C64e(0x6fd14b963e00aa17), h0l = C64e(0x636a2e057a15d543), h1h = C64e(0x8a225e8d0c97ef0b), h1l = C64e(0xe9341259f2b3c361), h2h = C64e(0x891da0c1536f801e), h2l = C64e(0x2aa9056bea2b6d80), h3h = C64e(0x588eccdb2075baa6), h3l = C64e(0xa90f3a76baf83bf7);
  sph_u64 h4h = C64e(0x0169e60541e34a69), h4l = C64e(0x46b58a8e2e6fe65a), h5h = C64e(0x1047a7d0c1843c24), h5l = C64e(0x3b6e71b12d5ac199), h6h = C64e(0xcf57f6ec9db1f856), h6l = C64e(0xa706887c5716b156), h7h = C64e(0xe3c2fcdfe68517fb), h7l = C64e(0x545a4678cc8cdd4b);
  sph_u64 tmp;

  for(int i = 0; i < 2; i++)
  {
  if (i == 0)
  {
    h0h ^= DEC64E(hash->h8[0]);
    h0l ^= DEC64E(hash->h8[1]);
    h1h ^= DEC64E(hash->h8[2]);
    h1l ^= DEC64E(hash->h8[3]);
    h2h ^= DEC64E(hash->h8[4]);
    h2l ^= DEC64E(hash->h8[5]);
    h3h ^= DEC64E(hash->h8[6]);
    h3l ^= DEC64E(hash->h8[7]);
  }
  else if(i == 1)
  {
    h4h ^= DEC64E(hash->h8[0]);
    h4l ^= DEC64E(hash->h8[1]);
    h5h ^= DEC64E(hash->h8[2]);
    h5l ^= DEC64E(hash->h8[3]);
    h6h ^= DEC64E(hash->h8[4]);
    h6l ^= DEC64E(hash->h8[5]);
    h7h ^= DEC64E(hash->h8[6]);
    h7l ^= DEC64E(hash->h8[7]);

    h0h ^= 0x80;
    h3l ^= 0x2000000000000;
  }
  E8;
  }

  h4h ^= 0x80;
  h7l ^= 0x2000000000000;

  hash->h8[0] = DEC64E(h4h);
  hash->h8[1] = DEC64E(h4l);
  hash->h8[2] = DEC64E(h5h);
  hash->h8[3] = DEC64E(h5l);
  hash->h8[4] = DEC64E(h6h);
  hash->h8[5] = DEC64E(h6l);
  hash->h8[6] = DEC64E(h7h);
  hash->h8[7] = DEC64E(h7l);

  barrier(CLK_GLOBAL_MEM_FENCE);
}


// keccak
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search3(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // keccak

  sph_u64 a00 = 0, a01 = 0, a02 = 0, a03 = 0, a04 = 0;
  sph_u64 a10 = 0, a11 = 0, a12 = 0, a13 = 0, a14 = 0;
  sph_u64 a20 = 0, a21 = 0, a22 = 0, a23 = 0, a24 = 0;
  sph_u64 a30 = 0, a31 = 0, a32 = 0, a33 = 0, a34 = 0;
  sph_u64 a40 = 0, a41 = 0, a42 = 0, a43 = 0, a44 = 0;

  a10 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a20 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a31 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a22 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a23 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a04 = SPH_C64(0xFFFFFFFFFFFFFFFF);

  a00 ^= SWAP8(hash->h8[0]);
  a10 ^= SWAP8(hash->h8[1]);
  a20 ^= SWAP8(hash->h8[2]);
  a30 ^= SWAP8(hash->h8[3]);
  a40 ^= SWAP8(hash->h8[4]);
  a01 ^= SWAP8(hash->h8[5]);
  a11 ^= SWAP8(hash->h8[6]);
  a21 ^= SWAP8(hash->h8[7]);
  a31 ^= 0x8000000000000001;
  KECCAK_F_1600;

  // Finalize the "lane complement"
  a10 = ~a10;
  a20 = ~a20;

  hash->h8[0] = SWAP8(a00);
  hash->h8[1] = SWAP8(a10);
  hash->h8[2] = SWAP8(a20);
  hash->h8[3] = SWAP8(a30);
  hash->h8[4] = SWAP8(a40);
  hash->h8[5] = SWAP8(a01);
  hash->h8[6] = SWAP8(a11);
  hash->h8[7] = SWAP8(a21);

  barrier(CLK_GLOBAL_MEM_FENCE);
}


// skein
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search4(__global hash_t* hashes, __global uint* output, const ulong target)
{
  uint gid = get_global_id(0);
  uint offset = get_global_offset(0);
  hash_t hash;
  __global hash_t *hashp = &(hashes[gid-offset]);

  for (int i = 0; i < 8; i++)
  hash.h8[i] = hashes[gid-offset].h8[i];

  sph_u64 h0 = SPH_C64(0x4903ADFF749C51CE), h1 = SPH_C64(0x0D95DE399746DF03), h2 = SPH_C64(0x8FD1934127C79BCE), h3 = SPH_C64(0x9A255629FF352CB1), h4 = SPH_C64(0x5DB62599DF6CA7B0), h5 = SPH_C64(0xEABE394CA9D5C3F4), h6 = SPH_C64(0x991112C71A75B523), h7 = SPH_C64(0xAE18A40B660FCC33);
  sph_u64 m0, m1, m2, m3, m4, m5, m6, m7;
  sph_u64 bcount = 0;

  m0 = SWAP8(hash.h8[0]);
  m1 = SWAP8(hash.h8[1]);
  m2 = SWAP8(hash.h8[2]);
  m3 = SWAP8(hash.h8[3]);
  m4 = SWAP8(hash.h8[4]);
  m5 = SWAP8(hash.h8[5]);
  m6 = SWAP8(hash.h8[6]);
  m7 = SWAP8(hash.h8[7]);

  UBI_BIG(480, 64);

  bcount = 0;
  m0 = m1 = m2 = m3 = m4 = m5 = m6 = m7 = 0;

  UBI_BIG(510, 8);

  hash.h8[0] = SWAP8(h0);
  hash.h8[1] = SWAP8(h1);
  hash.h8[2] = SWAP8(h2);
  hash.h8[3] = SWAP8(h3);
  hash.h8[4] = SWAP8(h4);
  hash.h8[5] = SWAP8(h5);
  hash.h8[6] = SWAP8(h6);
  hash.h8[7] = SWAP8(h7);

  bool result = (SWAP8(hash.h8[3]) <= target);
  if (result)
  output[atomic_inc(output+0xFF)] = SWAP4(gid);
}

#endif // TALKCOIN_MOD_CL