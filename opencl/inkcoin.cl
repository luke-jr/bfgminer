/*
 * InkCoin kernel implementation.
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

#ifndef INKCOIN_CL
#define INKCOIN_CL

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

#include "shavite.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
  #define DEC32LE(x) SWAP4(*(const __global sph_u32 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
  #define DEC32LE(x) (*(const __global sph_u32 *) (x));
#endif

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

#define CONST_EXP2  q[i+0] + SPH_ROTL64(q[i+1], 5)  + q[i+2] + SPH_ROTL64(q[i+3], 11) + \
                    q[i+4] + SPH_ROTL64(q[i+5], 27) + q[i+6] + SPH_ROTL64(q[i+7], 32) + \
                    q[i+8] + SPH_ROTL64(q[i+9], 37) + q[i+10] + SPH_ROTL64(q[i+11], 43) + \
                    q[i+12] + SPH_ROTL64(q[i+13], 53) + (SHR(q[i+14],1) ^ q[i+14]) + (SHR(q[i+15],2) ^ q[i+15])

// __attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, volatile __global uint* output, const ulong target)
{
  uint gid = get_global_id(0);
  union {
    unsigned char h1[64];
    uint h4[16];
    ulong h8[8];
  } hash;

  __local sph_u32 AES0[256], AES1[256], AES2[256], AES3[256];
  int init = get_local_id(0);
  int step = get_local_size(0);
  for (int i = init; i < 256; i += step)
  {
    AES0[i] = AES0_C[i];
    AES1[i] = AES1_C[i];
    AES2[i] = AES2_C[i];
    AES3[i] = AES3_C[i];
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  // shavite
  {
    // IV
    sph_u32 h0 = SPH_C32(0x72FCCDD8), h1 = SPH_C32(0x79CA4727), h2 = SPH_C32(0x128A077B), h3 = SPH_C32(0x40D55AEC);
    sph_u32 h4 = SPH_C32(0xD1901A06), h5 = SPH_C32(0x430AE307), h6 = SPH_C32(0xB29F5CD1), h7 = SPH_C32(0xDF07FBFC);
    sph_u32 h8 = SPH_C32(0x8E45D73D), h9 = SPH_C32(0x681AB538), hA = SPH_C32(0xBDE86578), hB = SPH_C32(0xDD577E47);
    sph_u32 hC = SPH_C32(0xE275EADE), hD = SPH_C32(0x502D9FCD), hE = SPH_C32(0xB9357178), hF = SPH_C32(0x022A4B9A);

    // state
    sph_u32 rk00, rk01, rk02, rk03, rk04, rk05, rk06, rk07;
    sph_u32 rk08, rk09, rk0A, rk0B, rk0C, rk0D, rk0E, rk0F;
    sph_u32 rk10, rk11, rk12, rk13, rk14, rk15, rk16, rk17;
    sph_u32 rk18, rk19, rk1A, rk1B, rk1C, rk1D, rk1E, rk1F;

    sph_u32 sc_count0 = (80 << 3), sc_count1 = 0, sc_count2 = 0, sc_count3 = 0;

    rk00 = DEC32LE(block + 0 * 4);;
    rk01 = DEC32LE(block + 1 * 4);;
    rk02 = DEC32LE(block + 2 * 4);;
    rk03 = DEC32LE(block + 3 * 4);;
    rk04 = DEC32LE(block + 4 * 4);;
    rk05 = DEC32LE(block + 5 * 4);;
    rk06 = DEC32LE(block + 6 * 4);;
    rk07 = DEC32LE(block + 7 * 4);;
    rk08 = DEC32LE(block + 8 * 4);;
    rk09 = DEC32LE(block + 9 * 4);;
    rk0A = DEC32LE(block + 10 * 4);;
    rk0B = DEC32LE(block + 11 * 4);;
    rk0C = DEC32LE(block + 12 * 4);;
    rk0D = DEC32LE(block + 13 * 4);;
    rk0E = DEC32LE(block + 14 * 4);;
    rk0F = DEC32LE(block + 15 * 4);;
    rk10 = DEC32LE(block + 16 * 4);;
    rk11 = DEC32LE(block + 17 * 4);;
    rk12 = DEC32LE(block + 18 * 4);;
    rk13 = gid;
    rk14 = 0x80;
    rk15 = rk16 = rk17 = rk18 = rk19 = rk1A = 0;
    rk1B = 0x2800000;
    rk1C = rk1D = rk1E = 0;
    rk1F = 0x2000000;

    c512(buf);

    hash.h4[0] = h0;
    hash.h4[1] = h1;
    hash.h4[2] = h2;
    hash.h4[3] = h3;
    hash.h4[4] = h4;
    hash.h4[5] = h5;
    hash.h4[6] = h6;
    hash.h4[7] = h7;
    hash.h4[8] = h8;
    hash.h4[9] = h9;
    hash.h4[10] = hA;
    hash.h4[11] = hB;
    hash.h4[12] = hC;
    hash.h4[13] = hD;
    hash.h4[14] = hE;
    hash.h4[15] = hF;
  }

  // shavite
  {
    // IV
    sph_u32 h0 = SPH_C32(0x72FCCDD8), h1 = SPH_C32(0x79CA4727), h2 = SPH_C32(0x128A077B), h3 = SPH_C32(0x40D55AEC);
    sph_u32 h4 = SPH_C32(0xD1901A06), h5 = SPH_C32(0x430AE307), h6 = SPH_C32(0xB29F5CD1), h7 = SPH_C32(0xDF07FBFC);
    sph_u32 h8 = SPH_C32(0x8E45D73D), h9 = SPH_C32(0x681AB538), hA = SPH_C32(0xBDE86578), hB = SPH_C32(0xDD577E47);
    sph_u32 hC = SPH_C32(0xE275EADE), hD = SPH_C32(0x502D9FCD), hE = SPH_C32(0xB9357178), hF = SPH_C32(0x022A4B9A);

    // state
    sph_u32 rk00, rk01, rk02, rk03, rk04, rk05, rk06, rk07;
    sph_u32 rk08, rk09, rk0A, rk0B, rk0C, rk0D, rk0E, rk0F;
    sph_u32 rk10, rk11, rk12, rk13, rk14, rk15, rk16, rk17;
    sph_u32 rk18, rk19, rk1A, rk1B, rk1C, rk1D, rk1E, rk1F;

    sph_u32 sc_count0 = (64 << 3), sc_count1 = 0, sc_count2 = 0, sc_count3 = 0;

    rk00 = hash.h4[0];
    rk01 = hash.h4[1];
    rk02 = hash.h4[2];
    rk03 = hash.h4[3];
    rk04 = hash.h4[4];
    rk05 = hash.h4[5];
    rk06 = hash.h4[6];
    rk07 = hash.h4[7];
    rk08 = hash.h4[8];
    rk09 = hash.h4[9];
    rk0A = hash.h4[10];
    rk0B = hash.h4[11];
    rk0C = hash.h4[12];
    rk0D = hash.h4[13];
    rk0E = hash.h4[14];
    rk0F = hash.h4[15];
    rk10 = 0x80;
    rk11 = rk12 = rk13 = rk14 = rk15 = rk16 = rk17 = rk18 = rk19 = rk1A = 0;
    rk1B = 0x2000000;
    rk1C = rk1D = rk1E = 0;
    rk1F = 0x2000000;

    c512(buf);

    hash.h4[0] = h0;
    hash.h4[1] = h1;
    hash.h4[2] = h2;
    hash.h4[3] = h3;
    hash.h4[4] = h4;
    hash.h4[5] = h5;
    hash.h4[6] = h6;
    hash.h4[7] = h7;
    hash.h4[8] = h8;
    hash.h4[9] = h9;
    hash.h4[10] = hA;
    hash.h4[11] = hB;
    hash.h4[12] = hC;
    hash.h4[13] = hD;
    hash.h4[14] = hE;
    hash.h4[15] = hF;
  }

  bool result = (hash.h8[3] <= target);
  if (result)
    output[output[0xFF]++] = SWAP4(gid);
}

#endif // INKCOIN_CL