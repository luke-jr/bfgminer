/*
 * AnimeCoin kernel implementation.
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

#ifndef ANIMECOIN_CL
#define ANIMECOIN_CL

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
#define SPH_T32(x)    ((x) & SPH_C32(0xFFFFFFFF))
#define SPH_ROTL32(x, n)   SPH_T32(((x) << (n)) | ((x) >> (32 - (n))))
#define SPH_ROTR32(x, n)   SPH_ROTL32(x, (32 - (n)))

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x)    ((x) & SPH_C64(0xFFFFFFFFFFFFFFFF))
#define SPH_ROTL64(x, n)   SPH_T64(((x) << (n)) | ((x) >> (64 - (n))))
#define SPH_ROTR64(x, n)   SPH_ROTL64(x, (64 - (n)))

#define SPH_ECHO_64 1
#define SPH_KECCAK_64 1
#define SPH_JH_64 1
#define SPH_SIMD_NOCOPY 0
#define SPH_KECCAK_NOCOPY 0
#define SPH_SMALL_FOOTPRINT_GROESTL 0
#define SPH_GROESTL_BIG_ENDIAN 0
#define SPH_CUBEHASH_UNROLL 0

#ifndef SPH_COMPACT_BLAKE_64
  #define SPH_COMPACT_BLAKE_64 0
#endif
#ifndef SPH_LUFFA_PARALLEL
  #define SPH_LUFFA_PARALLEL 0
#endif
#ifndef SPH_KECCAK_UNROLL
  #define SPH_KECCAK_UNROLL   0
#endif

#include "blake.cl"
#include "bmw.cl"
#include "groestl.cl"
#include "jh.cl"
#include "keccak.cl"
#include "skein.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

#define CONST_EXP2  q[i+0] + SPH_ROTL64(q[i+1], 5)  + q[i+2] + SPH_ROTL64(q[i+3], 11) + \
  q[i+4] + SPH_ROTL64(q[i+5], 27) + q[i+6] + SPH_ROTL64(q[i+7], 32) + \
  q[i+8] + SPH_ROTL64(q[i+9], 37) + q[i+10] + SPH_ROTL64(q[i+11], 43) + \
  q[i+12] + SPH_ROTL64(q[i+13], 53) + (SHR(q[i+14],1) ^ q[i+14]) + (SHR(q[i+15],2) ^ q[i+15])

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
  #define DEC64LE(x) SWAP8(*(const __global sph_u64 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
  #define DEC64LE(x) (*(const __global sph_u64 *) (x));
#endif

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, volatile __global uint* output, const ulong target)
{
  uint gid = get_global_id(0);
  union {
    unsigned char h1[64];
    uint h4[16];
    ulong h8[8];
  } hash;

  // bmw
  {
    sph_u64 BMW_H[16];
    for(unsigned u = 0; u < 16; u++)
      BMW_H[u] = BMW_IV512[u];

    sph_u64 mv[16],q[32];
      sph_u64 tmp;

    mv[0] = DEC64LE(block +   0);
    mv[1] = DEC64LE(block +   8);
    mv[2] = DEC64LE(block +  16);
    mv[3] = DEC64LE(block +  24);
    mv[4] = DEC64LE(block +  32);
    mv[5] = DEC64LE(block +  40);
    mv[6] = DEC64LE(block +  48);
    mv[7] = DEC64LE(block +  56);
    mv[8] = DEC64LE(block +  64);
    mv[9] = DEC64LE(block +  72);
    mv[9] &= 0x00000000FFFFFFFF;
    mv[9] ^= ((sph_u64) gid) << 32;
    mv[10] = 0x80;
    mv[11] = 0;
    mv[12] = 0;
    mv[13] = 0;
    mv[14] = 0;
    mv[15] = 0x280;

  tmp = (mv[5] ^ BMW_H[5]) - (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]) + (mv[14] ^ BMW_H[14]);
  q[0] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[1];
  tmp = (mv[6] ^ BMW_H[6]) - (mv[8] ^ BMW_H[8]) + (mv[11] ^ BMW_H[11]) + (mv[14] ^ BMW_H[14]) - (mv[15] ^ BMW_H[15]);
  q[1] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[2];
  tmp = (mv[0] ^ BMW_H[0]) + (mv[7] ^ BMW_H[7]) + (mv[9] ^ BMW_H[9]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[2] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[3];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[1] ^ BMW_H[1]) + (mv[8] ^ BMW_H[8]) - (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]);
  q[3] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[4];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[2] ^ BMW_H[2]) + (mv[9] ^ BMW_H[9]) - (mv[11] ^ BMW_H[11]) - (mv[14] ^ BMW_H[14]);
  q[4] = (SHR(tmp, 1) ^ tmp) + BMW_H[5];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[2] ^ BMW_H[2]) + (mv[10] ^ BMW_H[10]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[5] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[6];
  tmp = (mv[4] ^ BMW_H[4]) - (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) - (mv[11] ^ BMW_H[11]) + (mv[13] ^ BMW_H[13]);
  q[6] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[7];
  tmp = (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[5] ^ BMW_H[5]) - (mv[12] ^ BMW_H[12]) - (mv[14] ^ BMW_H[14]);
  q[7] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[8];
  tmp = (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) - (mv[6] ^ BMW_H[6]) + (mv[13] ^ BMW_H[13]) - (mv[15] ^ BMW_H[15]);
  q[8] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[9];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) + (mv[6] ^ BMW_H[6]) - (mv[7] ^ BMW_H[7]) + (mv[14] ^ BMW_H[14]);
  q[9] = (SHR(tmp, 1) ^ tmp) + BMW_H[10];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[7] ^ BMW_H[7]) + (mv[15] ^ BMW_H[15]);
  q[10] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[11];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[0] ^ BMW_H[0]) - (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) + (mv[9] ^ BMW_H[9]);
  q[11] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[12];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[3] ^ BMW_H[3]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[10] ^ BMW_H[10]);
  q[12] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[13];
  tmp = (mv[2] ^ BMW_H[2]) + (mv[4] ^ BMW_H[4]) + (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[11] ^ BMW_H[11]);
  q[13] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[14];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[5] ^ BMW_H[5]) + (mv[8] ^ BMW_H[8]) - (mv[11] ^ BMW_H[11]) - (mv[12] ^ BMW_H[12]);
  q[14] = (SHR(tmp, 1) ^ tmp) + BMW_H[15];
  tmp = (mv[12] ^ BMW_H[12]) - (mv[4] ^ BMW_H[4]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[13] ^ BMW_H[13]);
  q[15] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[0];

#pragma unroll 2
  for(int i=0;i<2;i++)
  {
  q[i+16] =
    (SHR(q[i], 1) ^ SHL(q[i], 2) ^ SPH_ROTL64(q[i], 13) ^ SPH_ROTL64(q[i], 43)) +
    (SHR(q[i+1], 2) ^ SHL(q[i+1], 1) ^ SPH_ROTL64(q[i+1], 19) ^ SPH_ROTL64(q[i+1], 53)) +
    (SHR(q[i+2], 2) ^ SHL(q[i+2], 2) ^ SPH_ROTL64(q[i+2], 28) ^ SPH_ROTL64(q[i+2], 59)) +
    (SHR(q[i+3], 1) ^ SHL(q[i+3], 3) ^ SPH_ROTL64(q[i+3], 4) ^ SPH_ROTL64(q[i+3], 37)) +
    (SHR(q[i+4], 1) ^ SHL(q[i+4], 2) ^ SPH_ROTL64(q[i+4], 13) ^ SPH_ROTL64(q[i+4], 43)) +
    (SHR(q[i+5], 2) ^ SHL(q[i+5], 1) ^ SPH_ROTL64(q[i+5], 19) ^ SPH_ROTL64(q[i+5], 53)) +
    (SHR(q[i+6], 2) ^ SHL(q[i+6], 2) ^ SPH_ROTL64(q[i+6], 28) ^ SPH_ROTL64(q[i+6], 59)) +
    (SHR(q[i+7], 1) ^ SHL(q[i+7], 3) ^ SPH_ROTL64(q[i+7], 4) ^ SPH_ROTL64(q[i+7], 37)) +
    (SHR(q[i+8], 1) ^ SHL(q[i+8], 2) ^ SPH_ROTL64(q[i+8], 13) ^ SPH_ROTL64(q[i+8], 43)) +
    (SHR(q[i+9], 2) ^ SHL(q[i+9], 1) ^ SPH_ROTL64(q[i+9], 19) ^ SPH_ROTL64(q[i+9], 53)) +
    (SHR(q[i+10], 2) ^ SHL(q[i+10], 2) ^ SPH_ROTL64(q[i+10], 28) ^ SPH_ROTL64(q[i+10], 59)) +
    (SHR(q[i+11], 1) ^ SHL(q[i+11], 3) ^ SPH_ROTL64(q[i+11], 4) ^ SPH_ROTL64(q[i+11], 37)) +
    (SHR(q[i+12], 1) ^ SHL(q[i+12], 2) ^ SPH_ROTL64(q[i+12], 13) ^ SPH_ROTL64(q[i+12], 43)) +
    (SHR(q[i+13], 2) ^ SHL(q[i+13], 1) ^ SPH_ROTL64(q[i+13], 19) ^ SPH_ROTL64(q[i+13], 53)) +
    (SHR(q[i+14], 2) ^ SHL(q[i+14], 2) ^ SPH_ROTL64(q[i+14], 28) ^ SPH_ROTL64(q[i+14], 59)) +
    (SHR(q[i+15], 1) ^ SHL(q[i+15], 3) ^ SPH_ROTL64(q[i+15], 4) ^ SPH_ROTL64(q[i+15], 37)) +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=2;i<6;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 3
  for(int i=6;i<9;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=9;i<13;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

#pragma unroll 3
  for(int i=13;i<16;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i-13], (i-13)+1) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

  sph_u64 XL64 = q[16]^q[17]^q[18]^q[19]^q[20]^q[21]^q[22]^q[23];
  sph_u64 XH64 = XL64^q[24]^q[25]^q[26]^q[27]^q[28]^q[29]^q[30]^q[31];

  BMW_H[0] = (SHL(XH64, 5) ^ SHR(q[16],5) ^ mv[0]) + ( XL64 ^ q[24] ^ q[0]);
  BMW_H[1] = (SHR(XH64, 7) ^ SHL(q[17],8) ^ mv[1]) + ( XL64 ^ q[25] ^ q[1]);
  BMW_H[2] = (SHR(XH64, 5) ^ SHL(q[18],5) ^ mv[2]) + ( XL64 ^ q[26] ^ q[2]);
  BMW_H[3] = (SHR(XH64, 1) ^ SHL(q[19],5) ^ mv[3]) + ( XL64 ^ q[27] ^ q[3]);
  BMW_H[4] = (SHR(XH64, 3) ^ q[20] ^ mv[4]) + ( XL64 ^ q[28] ^ q[4]);
  BMW_H[5] = (SHL(XH64, 6) ^ SHR(q[21],6) ^ mv[5]) + ( XL64 ^ q[29] ^ q[5]);
  BMW_H[6] = (SHR(XH64, 4) ^ SHL(q[22],6) ^ mv[6]) + ( XL64 ^ q[30] ^ q[6]);
  BMW_H[7] = (SHR(XH64,11) ^ SHL(q[23],2) ^ mv[7]) + ( XL64 ^ q[31] ^ q[7]);

  BMW_H[8] = SPH_ROTL64(BMW_H[4], 9) + ( XH64 ^ q[24] ^ mv[8]) + (SHL(XL64,8) ^ q[23] ^ q[8]);
  BMW_H[9] = SPH_ROTL64(BMW_H[5],10) + ( XH64 ^ q[25] ^ mv[9]) + (SHR(XL64,6) ^ q[16] ^ q[9]);
  BMW_H[10] = SPH_ROTL64(BMW_H[6],11) + ( XH64 ^ q[26] ^ mv[10]) + (SHL(XL64,6) ^ q[17] ^ q[10]);
  BMW_H[11] = SPH_ROTL64(BMW_H[7],12) + ( XH64 ^ q[27] ^ mv[11]) + (SHL(XL64,4) ^ q[18] ^ q[11]);
  BMW_H[12] = SPH_ROTL64(BMW_H[0],13) + ( XH64 ^ q[28] ^ mv[12]) + (SHR(XL64,3) ^ q[19] ^ q[12]);
  BMW_H[13] = SPH_ROTL64(BMW_H[1],14) + ( XH64 ^ q[29] ^ mv[13]) + (SHR(XL64,4) ^ q[20] ^ q[13]);
  BMW_H[14] = SPH_ROTL64(BMW_H[2],15) + ( XH64 ^ q[30] ^ mv[14]) + (SHR(XL64,7) ^ q[21] ^ q[14]);
  BMW_H[15] = SPH_ROTL64(BMW_H[3],16) + ( XH64 ^ q[31] ^ mv[15]) + (SHR(XL64,2) ^ q[22] ^ q[15]);

#pragma unroll 16
  for(int i=0;i<16;i++)
  {
  mv[i] = BMW_H[i];
  BMW_H[i] = 0xaaaaaaaaaaaaaaa0ull + (sph_u64)i;
  }

  tmp = (mv[5] ^ BMW_H[5]) - (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]) + (mv[14] ^ BMW_H[14]);
  q[0] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[1];
  tmp = (mv[6] ^ BMW_H[6]) - (mv[8] ^ BMW_H[8]) + (mv[11] ^ BMW_H[11]) + (mv[14] ^ BMW_H[14]) - (mv[15] ^ BMW_H[15]);
  q[1] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[2];
  tmp = (mv[0] ^ BMW_H[0]) + (mv[7] ^ BMW_H[7]) + (mv[9] ^ BMW_H[9]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[2] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[3];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[1] ^ BMW_H[1]) + (mv[8] ^ BMW_H[8]) - (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]);
  q[3] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[4];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[2] ^ BMW_H[2]) + (mv[9] ^ BMW_H[9]) - (mv[11] ^ BMW_H[11]) - (mv[14] ^ BMW_H[14]);
  q[4] = (SHR(tmp, 1) ^ tmp) + BMW_H[5];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[2] ^ BMW_H[2]) + (mv[10] ^ BMW_H[10]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[5] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[6];
  tmp = (mv[4] ^ BMW_H[4]) - (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) - (mv[11] ^ BMW_H[11]) + (mv[13] ^ BMW_H[13]);
  q[6] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[7];
  tmp = (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[5] ^ BMW_H[5]) - (mv[12] ^ BMW_H[12]) - (mv[14] ^ BMW_H[14]);
  q[7] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[8];
  tmp = (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) - (mv[6] ^ BMW_H[6]) + (mv[13] ^ BMW_H[13]) - (mv[15] ^ BMW_H[15]);
  q[8] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[9];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) + (mv[6] ^ BMW_H[6]) - (mv[7] ^ BMW_H[7]) + (mv[14] ^ BMW_H[14]);
  q[9] = (SHR(tmp, 1) ^ tmp) + BMW_H[10];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[7] ^ BMW_H[7]) + (mv[15] ^ BMW_H[15]);
  q[10] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[11];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[0] ^ BMW_H[0]) - (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) + (mv[9] ^ BMW_H[9]);
  q[11] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[12];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[3] ^ BMW_H[3]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[10] ^ BMW_H[10]);
  q[12] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[13];
  tmp = (mv[2] ^ BMW_H[2]) + (mv[4] ^ BMW_H[4]) + (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[11] ^ BMW_H[11]);
  q[13] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[14];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[5] ^ BMW_H[5]) + (mv[8] ^ BMW_H[8]) - (mv[11] ^ BMW_H[11]) - (mv[12] ^ BMW_H[12]);
  q[14] = (SHR(tmp, 1) ^ tmp) + BMW_H[15];
  tmp = (mv[12] ^ BMW_H[12]) - (mv[4] ^ BMW_H[4]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[13] ^ BMW_H[13]);
  q[15] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[0];

#pragma unroll 2
  for(int i=0;i<2;i++)
  {
  q[i+16] =
    (SHR(q[i], 1) ^ SHL(q[i], 2) ^ SPH_ROTL64(q[i], 13) ^ SPH_ROTL64(q[i], 43)) +
    (SHR(q[i+1], 2) ^ SHL(q[i+1], 1) ^ SPH_ROTL64(q[i+1], 19) ^ SPH_ROTL64(q[i+1], 53)) +
    (SHR(q[i+2], 2) ^ SHL(q[i+2], 2) ^ SPH_ROTL64(q[i+2], 28) ^ SPH_ROTL64(q[i+2], 59)) +
    (SHR(q[i+3], 1) ^ SHL(q[i+3], 3) ^ SPH_ROTL64(q[i+3], 4) ^ SPH_ROTL64(q[i+3], 37)) +
    (SHR(q[i+4], 1) ^ SHL(q[i+4], 2) ^ SPH_ROTL64(q[i+4], 13) ^ SPH_ROTL64(q[i+4], 43)) +
    (SHR(q[i+5], 2) ^ SHL(q[i+5], 1) ^ SPH_ROTL64(q[i+5], 19) ^ SPH_ROTL64(q[i+5], 53)) +
    (SHR(q[i+6], 2) ^ SHL(q[i+6], 2) ^ SPH_ROTL64(q[i+6], 28) ^ SPH_ROTL64(q[i+6], 59)) +
    (SHR(q[i+7], 1) ^ SHL(q[i+7], 3) ^ SPH_ROTL64(q[i+7], 4) ^ SPH_ROTL64(q[i+7], 37)) +
    (SHR(q[i+8], 1) ^ SHL(q[i+8], 2) ^ SPH_ROTL64(q[i+8], 13) ^ SPH_ROTL64(q[i+8], 43)) +
    (SHR(q[i+9], 2) ^ SHL(q[i+9], 1) ^ SPH_ROTL64(q[i+9], 19) ^ SPH_ROTL64(q[i+9], 53)) +
    (SHR(q[i+10], 2) ^ SHL(q[i+10], 2) ^ SPH_ROTL64(q[i+10], 28) ^ SPH_ROTL64(q[i+10], 59)) +
    (SHR(q[i+11], 1) ^ SHL(q[i+11], 3) ^ SPH_ROTL64(q[i+11], 4) ^ SPH_ROTL64(q[i+11], 37)) +
    (SHR(q[i+12], 1) ^ SHL(q[i+12], 2) ^ SPH_ROTL64(q[i+12], 13) ^ SPH_ROTL64(q[i+12], 43)) +
    (SHR(q[i+13], 2) ^ SHL(q[i+13], 1) ^ SPH_ROTL64(q[i+13], 19) ^ SPH_ROTL64(q[i+13], 53)) +
    (SHR(q[i+14], 2) ^ SHL(q[i+14], 2) ^ SPH_ROTL64(q[i+14], 28) ^ SPH_ROTL64(q[i+14], 59)) +
    (SHR(q[i+15], 1) ^ SHL(q[i+15], 3) ^ SPH_ROTL64(q[i+15], 4) ^ SPH_ROTL64(q[i+15], 37)) +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=2;i<6;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 3
  for(int i=6;i<9;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=9;i<13;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

#pragma unroll 3
  for(int i=13;i<16;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i-13], (i-13)+1) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

  XL64 = q[16]^q[17]^q[18]^q[19]^q[20]^q[21]^q[22]^q[23];
  XH64 = XL64^q[24]^q[25]^q[26]^q[27]^q[28]^q[29]^q[30]^q[31];

  BMW_H[0] = (SHL(XH64, 5) ^ SHR(q[16],5) ^ mv[0]) + ( XL64 ^ q[24] ^ q[0]);
  BMW_H[1] = (SHR(XH64, 7) ^ SHL(q[17],8) ^ mv[1]) + ( XL64 ^ q[25] ^ q[1]);
  BMW_H[2] = (SHR(XH64, 5) ^ SHL(q[18],5) ^ mv[2]) + ( XL64 ^ q[26] ^ q[2]);
  BMW_H[3] = (SHR(XH64, 1) ^ SHL(q[19],5) ^ mv[3]) + ( XL64 ^ q[27] ^ q[3]);
  BMW_H[4] = (SHR(XH64, 3) ^ q[20] ^ mv[4]) + ( XL64 ^ q[28] ^ q[4]);
  BMW_H[5] = (SHL(XH64, 6) ^ SHR(q[21],6) ^ mv[5]) + ( XL64 ^ q[29] ^ q[5]);
  BMW_H[6] = (SHR(XH64, 4) ^ SHL(q[22],6) ^ mv[6]) + ( XL64 ^ q[30] ^ q[6]);
  BMW_H[7] = (SHR(XH64,11) ^ SHL(q[23],2) ^ mv[7]) + ( XL64 ^ q[31] ^ q[7]);

  BMW_H[8] = SPH_ROTL64(BMW_H[4], 9) + ( XH64 ^ q[24] ^ mv[8]) + (SHL(XL64,8) ^ q[23] ^ q[8]);
  BMW_H[9] = SPH_ROTL64(BMW_H[5],10) + ( XH64 ^ q[25] ^ mv[9]) + (SHR(XL64,6) ^ q[16] ^ q[9]);
  BMW_H[10] = SPH_ROTL64(BMW_H[6],11) + ( XH64 ^ q[26] ^ mv[10]) + (SHL(XL64,6) ^ q[17] ^ q[10]);
  BMW_H[11] = SPH_ROTL64(BMW_H[7],12) + ( XH64 ^ q[27] ^ mv[11]) + (SHL(XL64,4) ^ q[18] ^ q[11]);
  BMW_H[12] = SPH_ROTL64(BMW_H[0],13) + ( XH64 ^ q[28] ^ mv[12]) + (SHR(XL64,3) ^ q[19] ^ q[12]);
  BMW_H[13] = SPH_ROTL64(BMW_H[1],14) + ( XH64 ^ q[29] ^ mv[13]) + (SHR(XL64,4) ^ q[20] ^ q[13]);
  BMW_H[14] = SPH_ROTL64(BMW_H[2],15) + ( XH64 ^ q[30] ^ mv[14]) + (SHR(XL64,7) ^ q[21] ^ q[14]);
  BMW_H[15] = SPH_ROTL64(BMW_H[3],16) + ( XH64 ^ q[31] ^ mv[15]) + (SHR(XL64,2) ^ q[22] ^ q[15]);

  hash.h8[0] = SWAP8(BMW_H[8]);
  hash.h8[1] = SWAP8(BMW_H[9]);
  hash.h8[2] = SWAP8(BMW_H[10]);
  hash.h8[3] = SWAP8(BMW_H[11]);
  hash.h8[4] = SWAP8(BMW_H[12]);
  hash.h8[5] = SWAP8(BMW_H[13]);
  hash.h8[6] = SWAP8(BMW_H[14]);
  hash.h8[7] = SWAP8(BMW_H[15]);
  }

  // blake
  {
    sph_u64 H0 = SPH_C64(0x6A09E667F3BCC908), H1 = SPH_C64(0xBB67AE8584CAA73B);
    sph_u64 H2 = SPH_C64(0x3C6EF372FE94F82B), H3 = SPH_C64(0xA54FF53A5F1D36F1);
    sph_u64 H4 = SPH_C64(0x510E527FADE682D1), H5 = SPH_C64(0x9B05688C2B3E6C1F);
    sph_u64 H6 = SPH_C64(0x1F83D9ABFB41BD6B), H7 = SPH_C64(0x5BE0CD19137E2179);
    sph_u64 S0 = 0, S1 = 0, S2 = 0, S3 = 0;
    sph_u64 T0 = SPH_C64(0xFFFFFFFFFFFFFC00) + (64 << 3), T1 = 0xFFFFFFFFFFFFFFFF;;

    if ((T0 = SPH_T64(T0 + 1024)) < 1024)
    {
      T1 = SPH_T64(T1 + 1);
    }
    sph_u64 M0, M1, M2, M3, M4, M5, M6, M7;
    sph_u64 M8, M9, MA, MB, MC, MD, ME, MF;
    sph_u64 V0, V1, V2, V3, V4, V5, V6, V7;
    sph_u64 V8, V9, VA, VB, VC, VD, VE, VF;
    M0 = hash.h8[0];
    M1 = hash.h8[1];
    M2 = hash.h8[2];
    M3 = hash.h8[3];
    M4 = hash.h8[4];
    M5 = hash.h8[5];
    M6 = hash.h8[6];
    M7 = hash.h8[7];
    M8 = 0x8000000000000000;
    M9 = 0;
    MA = 0;
    MB = 0;
    MC = 0;
    MD = 1;
    ME = 0;
    MF = 0x200;

    COMPRESS64;

    hash.h8[0] = H0;
    hash.h8[1] = H1;
    hash.h8[2] = H2;
    hash.h8[3] = H3;
    hash.h8[4] = H4;
    hash.h8[5] = H5;
    hash.h8[6] = H6;
    hash.h8[7] = H7;
  }

  bool dec = ((hash.h1[7] & 0x8) != 0);
  {
    // groestl
    sph_u64 H[16];
    for (unsigned int u = 0; u < 15; u ++)
      H[u] = 0;
  #if USE_LE
    H[15] = ((sph_u64)(512 & 0xFF) << 56) | ((sph_u64)(512 & 0xFF00) << 40);
  #else
    H[15] = (sph_u64)512;
  #endif

    sph_u64 g[16], m[16];
    m[0] = DEC64E(hash.h8[0]);
    m[1] = DEC64E(hash.h8[1]);
    m[2] = DEC64E(hash.h8[2]);
    m[3] = DEC64E(hash.h8[3]);
    m[4] = DEC64E(hash.h8[4]);
    m[5] = DEC64E(hash.h8[5]);
    m[6] = DEC64E(hash.h8[6]);
    m[7] = DEC64E(hash.h8[7]);
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
    for (unsigned int u = 0; u < 16; u ++)
      H[u] ^= g[u] ^ m[u];
    sph_u64 xH[16];
    for (unsigned int u = 0; u < 16; u ++)
      xH[u] = H[u];
    PERM_BIG_P(xH);
    for (unsigned int u = 0; u < 16; u ++)
      H[u] ^= xH[u];
    for (unsigned int u = 0; u < 8; u ++)
      hash.h8[u] = (dec ? DEC64E(H[u + 8]) : hash.h8[u]);
  }
  {

    // skein

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
    hash.h8[0] = (!dec ? SWAP8(h0) : hash.h8[0]);
    hash.h8[1] = (!dec ? SWAP8(h1) : hash.h8[1]);
    hash.h8[2] = (!dec ? SWAP8(h2) : hash.h8[2]);
    hash.h8[3] = (!dec ? SWAP8(h3) : hash.h8[3]);
    hash.h8[4] = (!dec ? SWAP8(h4) : hash.h8[4]);
    hash.h8[5] = (!dec ? SWAP8(h5) : hash.h8[5]);
    hash.h8[6] = (!dec ? SWAP8(h6) : hash.h8[6]);
    hash.h8[7] = (!dec ? SWAP8(h7) : hash.h8[7]);
  }

  // groestl
  {
    sph_u64 H[16];
    for (unsigned int u = 0; u < 15; u ++)
      H[u] = 0;
#if USE_LE
    H[15] = ((sph_u64)(512 & 0xFF) << 56) | ((sph_u64)(512 & 0xFF00) << 40);
#else
    H[15] = (sph_u64)512;
#endif

    sph_u64 g[16], m[16];
    m[0] = DEC64E(hash.h8[0]);
    m[1] = DEC64E(hash.h8[1]);
    m[2] = DEC64E(hash.h8[2]);
    m[3] = DEC64E(hash.h8[3]);
    m[4] = DEC64E(hash.h8[4]);
    m[5] = DEC64E(hash.h8[5]);
    m[6] = DEC64E(hash.h8[6]);
    m[7] = DEC64E(hash.h8[7]);
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
    for (unsigned int u = 0; u < 16; u ++)
      H[u] ^= g[u] ^ m[u];
    sph_u64 xH[16];
    for (unsigned int u = 0; u < 16; u ++)
      xH[u] = H[u];
    PERM_BIG_P(xH);
    for (unsigned int u = 0; u < 16; u ++)
      H[u] ^= xH[u];
    for (unsigned int u = 0; u < 8; u ++)
      hash.h8[u] = DEC64E(H[u + 8]);
  }

  // jh
  {
    sph_u64 h0h = C64e(0x6fd14b963e00aa17), h0l = C64e(0x636a2e057a15d543), h1h = C64e(0x8a225e8d0c97ef0b), h1l = C64e(0xe9341259f2b3c361), h2h = C64e(0x891da0c1536f801e), h2l = C64e(0x2aa9056bea2b6d80), h3h = C64e(0x588eccdb2075baa6), h3l = C64e(0xa90f3a76baf83bf7);
    sph_u64 h4h = C64e(0x0169e60541e34a69), h4l = C64e(0x46b58a8e2e6fe65a), h5h = C64e(0x1047a7d0c1843c24), h5l = C64e(0x3b6e71b12d5ac199), h6h = C64e(0xcf57f6ec9db1f856), h6l = C64e(0xa706887c5716b156), h7h = C64e(0xe3c2fcdfe68517fb), h7l = C64e(0x545a4678cc8cdd4b);
    sph_u64 tmp;

    for(int i = 0; i < 2; i++)
    {
      if (i == 0) {
        h0h ^= DEC64E(hash.h8[0]);
        h0l ^= DEC64E(hash.h8[1]);
        h1h ^= DEC64E(hash.h8[2]);
        h1l ^= DEC64E(hash.h8[3]);
        h2h ^= DEC64E(hash.h8[4]);
        h2l ^= DEC64E(hash.h8[5]);
        h3h ^= DEC64E(hash.h8[6]);
        h3l ^= DEC64E(hash.h8[7]);
      } else if(i == 1) {
        h4h ^= DEC64E(hash.h8[0]);
        h4l ^= DEC64E(hash.h8[1]);
        h5h ^= DEC64E(hash.h8[2]);
        h5l ^= DEC64E(hash.h8[3]);
        h6h ^= DEC64E(hash.h8[4]);
        h6l ^= DEC64E(hash.h8[5]);
        h7h ^= DEC64E(hash.h8[6]);
        h7l ^= DEC64E(hash.h8[7]);

        h0h ^= 0x80;
        h3l ^= 0x2000000000000;
      }
      E8;
    }
    h4h ^= 0x80;
    h7l ^= 0x2000000000000;

    hash.h8[0] = DEC64E(h4h);
    hash.h8[1] = DEC64E(h4l);
    hash.h8[2] = DEC64E(h5h);
    hash.h8[3] = DEC64E(h5l);
    hash.h8[4] = DEC64E(h6h);
    hash.h8[5] = DEC64E(h6l);
    hash.h8[6] = DEC64E(h7h);
    hash.h8[7] = DEC64E(h7l);
  }

  dec = ((hash.h1[7] & 0x8) != 0);
  {
    // blake

    sph_u64 H0 = SPH_C64(0x6A09E667F3BCC908), H1 = SPH_C64(0xBB67AE8584CAA73B);
    sph_u64 H2 = SPH_C64(0x3C6EF372FE94F82B), H3 = SPH_C64(0xA54FF53A5F1D36F1);
    sph_u64 H4 = SPH_C64(0x510E527FADE682D1), H5 = SPH_C64(0x9B05688C2B3E6C1F);
    sph_u64 H6 = SPH_C64(0x1F83D9ABFB41BD6B), H7 = SPH_C64(0x5BE0CD19137E2179);
    sph_u64 S0 = 0, S1 = 0, S2 = 0, S3 = 0;
    sph_u64 T0 = SPH_C64(0xFFFFFFFFFFFFFC00) + (64 << 3), T1 = 0xFFFFFFFFFFFFFFFF;;

    if ((T0 = SPH_T64(T0 + 1024)) < 1024)
    {
      T1 = SPH_T64(T1 + 1);
    }
    sph_u64 M0, M1, M2, M3, M4, M5, M6, M7;
    sph_u64 M8, M9, MA, MB, MC, MD, ME, MF;
    sph_u64 V0, V1, V2, V3, V4, V5, V6, V7;
    sph_u64 V8, V9, VA, VB, VC, VD, VE, VF;
    M0 = hash.h8[0];
    M1 = hash.h8[1];
    M2 = hash.h8[2];
    M3 = hash.h8[3];
    M4 = hash.h8[4];
    M5 = hash.h8[5];
    M6 = hash.h8[6];
    M7 = hash.h8[7];
    M8 = 0x8000000000000000;
    M9 = 0;
    MA = 0;
    MB = 0;
    MC = 0;
    MD = 1;
    ME = 0;
    MF = 0x200;

    COMPRESS64;

    hash.h8[0] = (dec ? H0 : hash.h8[0]);
    hash.h8[1] = (dec ? H1 : hash.h8[1]);
    hash.h8[2] = (dec ? H2 : hash.h8[2]);
    hash.h8[3] = (dec ? H3 : hash.h8[3]);
    hash.h8[4] = (dec ? H4 : hash.h8[4]);
    hash.h8[5] = (dec ? H5 : hash.h8[5]);
    hash.h8[6] = (dec ? H6 : hash.h8[6]);
    hash.h8[7] = (dec ? H7 : hash.h8[7]);
  }
  {
    // bmw
    sph_u64 BMW_H[16];
    for(unsigned u = 0; u < 16; u++)
      BMW_H[u] = BMW_IV512[u];

    sph_u64 mv[16],q[32];
      sph_u64 tmp;

    mv[ 0] = SWAP8(hash.h8[0]);
    mv[ 1] = SWAP8(hash.h8[1]);
    mv[ 2] = SWAP8(hash.h8[2]);
    mv[ 3] = SWAP8(hash.h8[3]);
    mv[ 4] = SWAP8(hash.h8[4]);
    mv[ 5] = SWAP8(hash.h8[5]);
    mv[ 6] = SWAP8(hash.h8[6]);
    mv[ 7] = SWAP8(hash.h8[7]);
    mv[ 8] = 0x80;
    mv[ 9] = 0;
    mv[10] = 0;
    mv[11] = 0;
    mv[12] = 0;
    mv[13] = 0;
    mv[14] = 0;
    mv[15] = 0x200;

  tmp = (mv[5] ^ BMW_H[5]) - (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]) + (mv[14] ^ BMW_H[14]);
  q[0] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[1];
  tmp = (mv[6] ^ BMW_H[6]) - (mv[8] ^ BMW_H[8]) + (mv[11] ^ BMW_H[11]) + (mv[14] ^ BMW_H[14]) - (mv[15] ^ BMW_H[15]);
  q[1] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[2];
  tmp = (mv[0] ^ BMW_H[0]) + (mv[7] ^ BMW_H[7]) + (mv[9] ^ BMW_H[9]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[2] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[3];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[1] ^ BMW_H[1]) + (mv[8] ^ BMW_H[8]) - (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]);
  q[3] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[4];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[2] ^ BMW_H[2]) + (mv[9] ^ BMW_H[9]) - (mv[11] ^ BMW_H[11]) - (mv[14] ^ BMW_H[14]);
  q[4] = (SHR(tmp, 1) ^ tmp) + BMW_H[5];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[2] ^ BMW_H[2]) + (mv[10] ^ BMW_H[10]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[5] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[6];
  tmp = (mv[4] ^ BMW_H[4]) - (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) - (mv[11] ^ BMW_H[11]) + (mv[13] ^ BMW_H[13]);
  q[6] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[7];
  tmp = (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[5] ^ BMW_H[5]) - (mv[12] ^ BMW_H[12]) - (mv[14] ^ BMW_H[14]);
  q[7] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[8];
  tmp = (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) - (mv[6] ^ BMW_H[6]) + (mv[13] ^ BMW_H[13]) - (mv[15] ^ BMW_H[15]);
  q[8] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[9];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) + (mv[6] ^ BMW_H[6]) - (mv[7] ^ BMW_H[7]) + (mv[14] ^ BMW_H[14]);
  q[9] = (SHR(tmp, 1) ^ tmp) + BMW_H[10];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[7] ^ BMW_H[7]) + (mv[15] ^ BMW_H[15]);
  q[10] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[11];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[0] ^ BMW_H[0]) - (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) + (mv[9] ^ BMW_H[9]);
  q[11] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[12];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[3] ^ BMW_H[3]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[10] ^ BMW_H[10]);
  q[12] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[13];
  tmp = (mv[2] ^ BMW_H[2]) + (mv[4] ^ BMW_H[4]) + (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[11] ^ BMW_H[11]);
  q[13] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[14];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[5] ^ BMW_H[5]) + (mv[8] ^ BMW_H[8]) - (mv[11] ^ BMW_H[11]) - (mv[12] ^ BMW_H[12]);
  q[14] = (SHR(tmp, 1) ^ tmp) + BMW_H[15];
  tmp = (mv[12] ^ BMW_H[12]) - (mv[4] ^ BMW_H[4]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[13] ^ BMW_H[13]);
  q[15] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[0];

#pragma unroll 2
  for(int i=0;i<2;i++)
  {
  q[i+16] =
    (SHR(q[i], 1) ^ SHL(q[i], 2) ^ SPH_ROTL64(q[i], 13) ^ SPH_ROTL64(q[i], 43)) +
    (SHR(q[i+1], 2) ^ SHL(q[i+1], 1) ^ SPH_ROTL64(q[i+1], 19) ^ SPH_ROTL64(q[i+1], 53)) +
    (SHR(q[i+2], 2) ^ SHL(q[i+2], 2) ^ SPH_ROTL64(q[i+2], 28) ^ SPH_ROTL64(q[i+2], 59)) +
    (SHR(q[i+3], 1) ^ SHL(q[i+3], 3) ^ SPH_ROTL64(q[i+3], 4) ^ SPH_ROTL64(q[i+3], 37)) +
    (SHR(q[i+4], 1) ^ SHL(q[i+4], 2) ^ SPH_ROTL64(q[i+4], 13) ^ SPH_ROTL64(q[i+4], 43)) +
    (SHR(q[i+5], 2) ^ SHL(q[i+5], 1) ^ SPH_ROTL64(q[i+5], 19) ^ SPH_ROTL64(q[i+5], 53)) +
    (SHR(q[i+6], 2) ^ SHL(q[i+6], 2) ^ SPH_ROTL64(q[i+6], 28) ^ SPH_ROTL64(q[i+6], 59)) +
    (SHR(q[i+7], 1) ^ SHL(q[i+7], 3) ^ SPH_ROTL64(q[i+7], 4) ^ SPH_ROTL64(q[i+7], 37)) +
    (SHR(q[i+8], 1) ^ SHL(q[i+8], 2) ^ SPH_ROTL64(q[i+8], 13) ^ SPH_ROTL64(q[i+8], 43)) +
    (SHR(q[i+9], 2) ^ SHL(q[i+9], 1) ^ SPH_ROTL64(q[i+9], 19) ^ SPH_ROTL64(q[i+9], 53)) +
    (SHR(q[i+10], 2) ^ SHL(q[i+10], 2) ^ SPH_ROTL64(q[i+10], 28) ^ SPH_ROTL64(q[i+10], 59)) +
    (SHR(q[i+11], 1) ^ SHL(q[i+11], 3) ^ SPH_ROTL64(q[i+11], 4) ^ SPH_ROTL64(q[i+11], 37)) +
    (SHR(q[i+12], 1) ^ SHL(q[i+12], 2) ^ SPH_ROTL64(q[i+12], 13) ^ SPH_ROTL64(q[i+12], 43)) +
    (SHR(q[i+13], 2) ^ SHL(q[i+13], 1) ^ SPH_ROTL64(q[i+13], 19) ^ SPH_ROTL64(q[i+13], 53)) +
    (SHR(q[i+14], 2) ^ SHL(q[i+14], 2) ^ SPH_ROTL64(q[i+14], 28) ^ SPH_ROTL64(q[i+14], 59)) +
    (SHR(q[i+15], 1) ^ SHL(q[i+15], 3) ^ SPH_ROTL64(q[i+15], 4) ^ SPH_ROTL64(q[i+15], 37)) +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=2;i<6;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 3
  for(int i=6;i<9;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=9;i<13;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

#pragma unroll 3
  for(int i=13;i<16;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i-13], (i-13)+1) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

  sph_u64 XL64 = q[16]^q[17]^q[18]^q[19]^q[20]^q[21]^q[22]^q[23];
  sph_u64 XH64 = XL64^q[24]^q[25]^q[26]^q[27]^q[28]^q[29]^q[30]^q[31];

  BMW_H[0] = (SHL(XH64, 5) ^ SHR(q[16],5) ^ mv[0]) + ( XL64 ^ q[24] ^ q[0]);
  BMW_H[1] = (SHR(XH64, 7) ^ SHL(q[17],8) ^ mv[1]) + ( XL64 ^ q[25] ^ q[1]);
  BMW_H[2] = (SHR(XH64, 5) ^ SHL(q[18],5) ^ mv[2]) + ( XL64 ^ q[26] ^ q[2]);
  BMW_H[3] = (SHR(XH64, 1) ^ SHL(q[19],5) ^ mv[3]) + ( XL64 ^ q[27] ^ q[3]);
  BMW_H[4] = (SHR(XH64, 3) ^ q[20] ^ mv[4]) + ( XL64 ^ q[28] ^ q[4]);
  BMW_H[5] = (SHL(XH64, 6) ^ SHR(q[21],6) ^ mv[5]) + ( XL64 ^ q[29] ^ q[5]);
  BMW_H[6] = (SHR(XH64, 4) ^ SHL(q[22],6) ^ mv[6]) + ( XL64 ^ q[30] ^ q[6]);
  BMW_H[7] = (SHR(XH64,11) ^ SHL(q[23],2) ^ mv[7]) + ( XL64 ^ q[31] ^ q[7]);

  BMW_H[8] = SPH_ROTL64(BMW_H[4], 9) + ( XH64 ^ q[24] ^ mv[8]) + (SHL(XL64,8) ^ q[23] ^ q[8]);
  BMW_H[9] = SPH_ROTL64(BMW_H[5],10) + ( XH64 ^ q[25] ^ mv[9]) + (SHR(XL64,6) ^ q[16] ^ q[9]);
  BMW_H[10] = SPH_ROTL64(BMW_H[6],11) + ( XH64 ^ q[26] ^ mv[10]) + (SHL(XL64,6) ^ q[17] ^ q[10]);
  BMW_H[11] = SPH_ROTL64(BMW_H[7],12) + ( XH64 ^ q[27] ^ mv[11]) + (SHL(XL64,4) ^ q[18] ^ q[11]);
  BMW_H[12] = SPH_ROTL64(BMW_H[0],13) + ( XH64 ^ q[28] ^ mv[12]) + (SHR(XL64,3) ^ q[19] ^ q[12]);
  BMW_H[13] = SPH_ROTL64(BMW_H[1],14) + ( XH64 ^ q[29] ^ mv[13]) + (SHR(XL64,4) ^ q[20] ^ q[13]);
  BMW_H[14] = SPH_ROTL64(BMW_H[2],15) + ( XH64 ^ q[30] ^ mv[14]) + (SHR(XL64,7) ^ q[21] ^ q[14]);
  BMW_H[15] = SPH_ROTL64(BMW_H[3],16) + ( XH64 ^ q[31] ^ mv[15]) + (SHR(XL64,2) ^ q[22] ^ q[15]);

#pragma unroll 16
  for(int i=0;i<16;i++)
  {
  mv[i] = BMW_H[i];
  BMW_H[i] = 0xaaaaaaaaaaaaaaa0ull + (sph_u64)i;
  }

  tmp = (mv[5] ^ BMW_H[5]) - (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]) + (mv[14] ^ BMW_H[14]);
  q[0] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[1];
  tmp = (mv[6] ^ BMW_H[6]) - (mv[8] ^ BMW_H[8]) + (mv[11] ^ BMW_H[11]) + (mv[14] ^ BMW_H[14]) - (mv[15] ^ BMW_H[15]);
  q[1] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[2];
  tmp = (mv[0] ^ BMW_H[0]) + (mv[7] ^ BMW_H[7]) + (mv[9] ^ BMW_H[9]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[2] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[3];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[1] ^ BMW_H[1]) + (mv[8] ^ BMW_H[8]) - (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]);
  q[3] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[4];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[2] ^ BMW_H[2]) + (mv[9] ^ BMW_H[9]) - (mv[11] ^ BMW_H[11]) - (mv[14] ^ BMW_H[14]);
  q[4] = (SHR(tmp, 1) ^ tmp) + BMW_H[5];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[2] ^ BMW_H[2]) + (mv[10] ^ BMW_H[10]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[5] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[6];
  tmp = (mv[4] ^ BMW_H[4]) - (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) - (mv[11] ^ BMW_H[11]) + (mv[13] ^ BMW_H[13]);
  q[6] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[7];
  tmp = (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[5] ^ BMW_H[5]) - (mv[12] ^ BMW_H[12]) - (mv[14] ^ BMW_H[14]);
  q[7] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[8];
  tmp = (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) - (mv[6] ^ BMW_H[6]) + (mv[13] ^ BMW_H[13]) - (mv[15] ^ BMW_H[15]);
  q[8] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[9];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) + (mv[6] ^ BMW_H[6]) - (mv[7] ^ BMW_H[7]) + (mv[14] ^ BMW_H[14]);
  q[9] = (SHR(tmp, 1) ^ tmp) + BMW_H[10];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[7] ^ BMW_H[7]) + (mv[15] ^ BMW_H[15]);
  q[10] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[11];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[0] ^ BMW_H[0]) - (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) + (mv[9] ^ BMW_H[9]);
  q[11] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[12];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[3] ^ BMW_H[3]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[10] ^ BMW_H[10]);
  q[12] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[13];
  tmp = (mv[2] ^ BMW_H[2]) + (mv[4] ^ BMW_H[4]) + (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[11] ^ BMW_H[11]);
  q[13] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[14];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[5] ^ BMW_H[5]) + (mv[8] ^ BMW_H[8]) - (mv[11] ^ BMW_H[11]) - (mv[12] ^ BMW_H[12]);
  q[14] = (SHR(tmp, 1) ^ tmp) + BMW_H[15];
  tmp = (mv[12] ^ BMW_H[12]) - (mv[4] ^ BMW_H[4]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[13] ^ BMW_H[13]);
  q[15] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[0];

#pragma unroll 2
  for(int i=0;i<2;i++)
  {
  q[i+16] =
    (SHR(q[i], 1) ^ SHL(q[i], 2) ^ SPH_ROTL64(q[i], 13) ^ SPH_ROTL64(q[i], 43)) +
    (SHR(q[i+1], 2) ^ SHL(q[i+1], 1) ^ SPH_ROTL64(q[i+1], 19) ^ SPH_ROTL64(q[i+1], 53)) +
    (SHR(q[i+2], 2) ^ SHL(q[i+2], 2) ^ SPH_ROTL64(q[i+2], 28) ^ SPH_ROTL64(q[i+2], 59)) +
    (SHR(q[i+3], 1) ^ SHL(q[i+3], 3) ^ SPH_ROTL64(q[i+3], 4) ^ SPH_ROTL64(q[i+3], 37)) +
    (SHR(q[i+4], 1) ^ SHL(q[i+4], 2) ^ SPH_ROTL64(q[i+4], 13) ^ SPH_ROTL64(q[i+4], 43)) +
    (SHR(q[i+5], 2) ^ SHL(q[i+5], 1) ^ SPH_ROTL64(q[i+5], 19) ^ SPH_ROTL64(q[i+5], 53)) +
    (SHR(q[i+6], 2) ^ SHL(q[i+6], 2) ^ SPH_ROTL64(q[i+6], 28) ^ SPH_ROTL64(q[i+6], 59)) +
    (SHR(q[i+7], 1) ^ SHL(q[i+7], 3) ^ SPH_ROTL64(q[i+7], 4) ^ SPH_ROTL64(q[i+7], 37)) +
    (SHR(q[i+8], 1) ^ SHL(q[i+8], 2) ^ SPH_ROTL64(q[i+8], 13) ^ SPH_ROTL64(q[i+8], 43)) +
    (SHR(q[i+9], 2) ^ SHL(q[i+9], 1) ^ SPH_ROTL64(q[i+9], 19) ^ SPH_ROTL64(q[i+9], 53)) +
    (SHR(q[i+10], 2) ^ SHL(q[i+10], 2) ^ SPH_ROTL64(q[i+10], 28) ^ SPH_ROTL64(q[i+10], 59)) +
    (SHR(q[i+11], 1) ^ SHL(q[i+11], 3) ^ SPH_ROTL64(q[i+11], 4) ^ SPH_ROTL64(q[i+11], 37)) +
    (SHR(q[i+12], 1) ^ SHL(q[i+12], 2) ^ SPH_ROTL64(q[i+12], 13) ^ SPH_ROTL64(q[i+12], 43)) +
    (SHR(q[i+13], 2) ^ SHL(q[i+13], 1) ^ SPH_ROTL64(q[i+13], 19) ^ SPH_ROTL64(q[i+13], 53)) +
    (SHR(q[i+14], 2) ^ SHL(q[i+14], 2) ^ SPH_ROTL64(q[i+14], 28) ^ SPH_ROTL64(q[i+14], 59)) +
    (SHR(q[i+15], 1) ^ SHL(q[i+15], 3) ^ SPH_ROTL64(q[i+15], 4) ^ SPH_ROTL64(q[i+15], 37)) +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=2;i<6;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 3
  for(int i=6;i<9;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=9;i<13;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

#pragma unroll 3
  for(int i=13;i<16;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i-13], (i-13)+1) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

  XL64 = q[16]^q[17]^q[18]^q[19]^q[20]^q[21]^q[22]^q[23];
  XH64 = XL64^q[24]^q[25]^q[26]^q[27]^q[28]^q[29]^q[30]^q[31];

  BMW_H[0] = (SHL(XH64, 5) ^ SHR(q[16],5) ^ mv[0]) + ( XL64 ^ q[24] ^ q[0]);
  BMW_H[1] = (SHR(XH64, 7) ^ SHL(q[17],8) ^ mv[1]) + ( XL64 ^ q[25] ^ q[1]);
  BMW_H[2] = (SHR(XH64, 5) ^ SHL(q[18],5) ^ mv[2]) + ( XL64 ^ q[26] ^ q[2]);
  BMW_H[3] = (SHR(XH64, 1) ^ SHL(q[19],5) ^ mv[3]) + ( XL64 ^ q[27] ^ q[3]);
  BMW_H[4] = (SHR(XH64, 3) ^ q[20] ^ mv[4]) + ( XL64 ^ q[28] ^ q[4]);
  BMW_H[5] = (SHL(XH64, 6) ^ SHR(q[21],6) ^ mv[5]) + ( XL64 ^ q[29] ^ q[5]);
  BMW_H[6] = (SHR(XH64, 4) ^ SHL(q[22],6) ^ mv[6]) + ( XL64 ^ q[30] ^ q[6]);
  BMW_H[7] = (SHR(XH64,11) ^ SHL(q[23],2) ^ mv[7]) + ( XL64 ^ q[31] ^ q[7]);

  BMW_H[8] = SPH_ROTL64(BMW_H[4], 9) + ( XH64 ^ q[24] ^ mv[8]) + (SHL(XL64,8) ^ q[23] ^ q[8]);
  BMW_H[9] = SPH_ROTL64(BMW_H[5],10) + ( XH64 ^ q[25] ^ mv[9]) + (SHR(XL64,6) ^ q[16] ^ q[9]);
  BMW_H[10] = SPH_ROTL64(BMW_H[6],11) + ( XH64 ^ q[26] ^ mv[10]) + (SHL(XL64,6) ^ q[17] ^ q[10]);
  BMW_H[11] = SPH_ROTL64(BMW_H[7],12) + ( XH64 ^ q[27] ^ mv[11]) + (SHL(XL64,4) ^ q[18] ^ q[11]);
  BMW_H[12] = SPH_ROTL64(BMW_H[0],13) + ( XH64 ^ q[28] ^ mv[12]) + (SHR(XL64,3) ^ q[19] ^ q[12]);
  BMW_H[13] = SPH_ROTL64(BMW_H[1],14) + ( XH64 ^ q[29] ^ mv[13]) + (SHR(XL64,4) ^ q[20] ^ q[13]);
  BMW_H[14] = SPH_ROTL64(BMW_H[2],15) + ( XH64 ^ q[30] ^ mv[14]) + (SHR(XL64,7) ^ q[21] ^ q[14]);
  BMW_H[15] = SPH_ROTL64(BMW_H[3],16) + ( XH64 ^ q[31] ^ mv[15]) + (SHR(XL64,2) ^ q[22] ^ q[15]);

    hash.h8[0] = (!dec ? SWAP8(BMW_H[8]) : hash.h8[0]);
    hash.h8[1] = (!dec ? SWAP8(BMW_H[9]) : hash.h8[1]);
    hash.h8[2] = (!dec ? SWAP8(BMW_H[10]) : hash.h8[2]);
    hash.h8[3] = (!dec ? SWAP8(BMW_H[11]) : hash.h8[3]);
    hash.h8[4] = (!dec ? SWAP8(BMW_H[12]) : hash.h8[4]);
    hash.h8[5] = (!dec ? SWAP8(BMW_H[13]) : hash.h8[5]);
    hash.h8[6] = (!dec ? SWAP8(BMW_H[14]) : hash.h8[6]);
    hash.h8[7] = (!dec ? SWAP8(BMW_H[15]) : hash.h8[7]);

  }

  // keccak
  {
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

    a00 ^= SWAP8(hash.h8[0]);
    a10 ^= SWAP8(hash.h8[1]);
    a20 ^= SWAP8(hash.h8[2]);
    a30 ^= SWAP8(hash.h8[3]);
    a40 ^= SWAP8(hash.h8[4]);
    a01 ^= SWAP8(hash.h8[5]);
    a11 ^= SWAP8(hash.h8[6]);
    a21 ^= SWAP8(hash.h8[7]);
    a31 ^= 0x8000000000000001;
    KECCAK_F_1600;
    // Finalize the "lane complement"
    a10 = ~a10;
    a20 = ~a20;

    hash.h8[0] = SWAP8(a00);
    hash.h8[1] = SWAP8(a10);
    hash.h8[2] = SWAP8(a20);
    hash.h8[3] = SWAP8(a30);
    hash.h8[4] = SWAP8(a40);
    hash.h8[5] = SWAP8(a01);
    hash.h8[6] = SWAP8(a11);
    hash.h8[7] = SWAP8(a21);
  }

  // skein
  {
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
  }

  if ((hash.h1[7] & 0x8) != 0)
  {
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

    a00 ^= SWAP8(hash.h8[0]);
    a10 ^= SWAP8(hash.h8[1]);
    a20 ^= SWAP8(hash.h8[2]);
    a30 ^= SWAP8(hash.h8[3]);
    a40 ^= SWAP8(hash.h8[4]);
    a01 ^= SWAP8(hash.h8[5]);
    a11 ^= SWAP8(hash.h8[6]);
    a21 ^= SWAP8(hash.h8[7]);
    a31 ^= 0x8000000000000001;
    KECCAK_F_1600;
    // Finalize the "lane complement"
    a10 = ~a10;
    a20 = ~a20;

    hash.h8[0] = SWAP8(a00);
    hash.h8[1] = SWAP8(a10);
    hash.h8[2] = SWAP8(a20);
    hash.h8[3] = SWAP8(a30);
    hash.h8[4] = SWAP8(a40);
    hash.h8[5] = SWAP8(a01);
    hash.h8[6] = SWAP8(a11);
    hash.h8[7] = SWAP8(a21);
  }
  else
  {

    // jh

    sph_u64 h0h = C64e(0x6fd14b963e00aa17), h0l = C64e(0x636a2e057a15d543), h1h = C64e(0x8a225e8d0c97ef0b), h1l = C64e(0xe9341259f2b3c361), h2h = C64e(0x891da0c1536f801e), h2l = C64e(0x2aa9056bea2b6d80), h3h = C64e(0x588eccdb2075baa6), h3l = C64e(0xa90f3a76baf83bf7);
    sph_u64 h4h = C64e(0x0169e60541e34a69), h4l = C64e(0x46b58a8e2e6fe65a), h5h = C64e(0x1047a7d0c1843c24), h5l = C64e(0x3b6e71b12d5ac199), h6h = C64e(0xcf57f6ec9db1f856), h6l = C64e(0xa706887c5716b156), h7h = C64e(0xe3c2fcdfe68517fb), h7l = C64e(0x545a4678cc8cdd4b);
    sph_u64 tmp;

    for(int i = 0; i < 2; i++)
    {
      if (i == 0) {
        h0h ^= DEC64E(hash.h8[0]);
        h0l ^= DEC64E(hash.h8[1]);
        h1h ^= DEC64E(hash.h8[2]);
        h1l ^= DEC64E(hash.h8[3]);
        h2h ^= DEC64E(hash.h8[4]);
        h2l ^= DEC64E(hash.h8[5]);
        h3h ^= DEC64E(hash.h8[6]);
        h3l ^= DEC64E(hash.h8[7]);
      } else if(i == 1) {
        h4h ^= DEC64E(hash.h8[0]);
        h4l ^= DEC64E(hash.h8[1]);
        h5h ^= DEC64E(hash.h8[2]);
        h5l ^= DEC64E(hash.h8[3]);
        h6h ^= DEC64E(hash.h8[4]);
        h6l ^= DEC64E(hash.h8[5]);
        h7h ^= DEC64E(hash.h8[6]);
        h7l ^= DEC64E(hash.h8[7]);

        h0h ^= 0x80;
        h3l ^= 0x2000000000000;
      }
      E8;
    }
    h4h ^= 0x80;
    h7l ^= 0x2000000000000;

    hash.h8[0] = DEC64E(h4h);
    hash.h8[1] = DEC64E(h4l);
    hash.h8[2] = DEC64E(h5h);
    hash.h8[3] = DEC64E(h5l);
    hash.h8[4] = DEC64E(h6h);
    hash.h8[5] = DEC64E(h6l);
    hash.h8[6] = DEC64E(h7h);
    hash.h8[7] = DEC64E(h7l);
  }

  bool result = (SWAP8(hash.h8[3]) <= target);
  if (result)
    output[output[0xFF]++] = SWAP4(gid);
}

#endif // ANIMECOIN_CL