/*
 * FugueCoin kernel implementation.
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

#ifndef FUGUECOIN_CL
#define FUGUECOIN_CL

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

#include "fugue.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC32BE(x) (*(const __global sph_u32 *) (x))
#else
  #define DEC32BE(x) SWAP4(*(const __global sph_u32 *) (x))
#endif

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* input, volatile __global uint* output, const ulong target)
{
  uint gid = get_global_id(0);

  //mixtab
  __local sph_u32 mixtab0[256], mixtab1[256], mixtab2[256], mixtab3[256];
  int init = get_local_id(0);
  int step = get_local_size(0);
  for (int i = init; i < 256; i += step)
  {
    mixtab0[i] = mixtab0_c[i];
    mixtab1[i] = mixtab1_c[i];
    mixtab2[i] = mixtab2_c[i];
    mixtab3[i] = mixtab3_c[i];
  }
  barrier(CLK_GLOBAL_MEM_FENCE);

  sph_u32 S00 = 0, S01 = 0, S02 = 0, S03 = 0, S04 = 0, S05 = 0, S06 = 0, S07 = 0, S08 = 0, S09 = 0; \
  sph_u32 S10 = 0, S11 = 0, S12 = 0, S13 = 0, S14 = 0, S15 = 0, S16 = 0, S17 = 0, S18 = 0, S19 = 0; \
  sph_u32 S20 = 0, S21 = 0, S22 = IV256[0], S23 = IV256[1], S24 = IV256[2], S25 = IV256[3], S26 = IV256[4], S27 = IV256[5], S28 = IV256[6], S29 = IV256[7];

  FUGUE256_5(DEC32BE(input + 0x0), DEC32BE(input + 0x4), DEC32BE(input + 0x8), DEC32BE(input + 0xc), DEC32BE(input + 0x10));
  FUGUE256_5(DEC32BE(input + 0x14), DEC32BE(input + 0x18), DEC32BE(input + 0x1c), DEC32BE(input + 0x20), DEC32BE(input + 0x24));
  FUGUE256_5(DEC32BE(input + 0x28), DEC32BE(input + 0x2c), DEC32BE(input + 0x30), DEC32BE(input + 0x34), DEC32BE(input + 0x38));
  FUGUE256_4(DEC32BE(input + 0x3c), DEC32BE(input + 0x40), DEC32BE(input + 0x44), DEC32BE(input + 0x48));

  TIX2(SWAP4(gid), S06, S07, S14, S16, S00);
  CMIX30(S03, S04, S05, S07, S08, S09, S18, S19, S20);
  SMIX(S03, S04, S05, S06);
  CMIX30(S00, S01, S02, S04, S05, S06, S15, S16, S17);
  SMIX(S00, S01, S02, S03);

  TIX2(0, S00, S01, S08, S10, S24);
  CMIX30(S27, S28, S29, S01, S02, S03, S12, S13, S14);
  SMIX(S27, S28, S29, S00);
  CMIX30(S24, S25, S26, S28, S29, S00, S09, S10, S11);
  SMIX(S24, S25, S26, S27);

  TIX2(0x280, S24, S25, S02, S04, S18);
  CMIX30(S21, S22, S23, S25, S26, S27, S06, S07, S08);
  SMIX(S21, S22, S23, S24);
  CMIX30(S18, S19, S20, S22, S23, S24, S03, S04, S05);
  SMIX(S18, S19, S20, S21);

  CMIX30(S15, S16, S17, S19, S20, S21, S00, S01, S02);
  SMIX(S15, S16, S17, S18);
  CMIX30(S12, S13, S14, S16, S17, S18, S27, S28, S29);
  SMIX(S12, S13, S14, S15);
  CMIX30(S09, S10, S11, S13, S14, S15, S24, S25, S26);
  SMIX(S09, S10, S11, S12);
  CMIX30(S06, S07, S08, S10, S11, S12, S21, S22, S23);
  SMIX(S06, S07, S08, S09);
  CMIX30(S03, S04, S05, S07, S08, S09, S18, S19, S20);
  SMIX(S03, S04, S05, S06);
  CMIX30(S00, S01, S02, S04, S05, S06, S15, S16, S17);
  SMIX(S00, S01, S02, S03);
  CMIX30(S27, S28, S29, S01, S02, S03, S12, S13, S14);
  SMIX(S27, S28, S29, S00);
  CMIX30(S24, S25, S26, S28, S29, S00, S09, S10, S11);
  SMIX(S24, S25, S26, S27);
  CMIX30(S21, S22, S23, S25, S26, S27, S06, S07, S08);
  SMIX(S21, S22, S23, S24);
  CMIX30(S18, S19, S20, S22, S23, S24, S03, S04, S05);
  SMIX(S18, S19, S20, S21);
  S22 ^= S18;
  S03 ^= S18;
  SMIX(S03, S04, S05, S06);
  S07 ^= S03;
  S19 ^= S03;
  SMIX(S19, S20, S21, S22);
  S23 ^= S19;
  S04 ^= S19;
  SMIX(S04, S05, S06, S07);
  S08 ^= S04;
  S20 ^= S04;
  SMIX(S20, S21, S22, S23);
  S24 ^= S20;
  S05 ^= S20;
  SMIX(S05, S06, S07, S08);
  S09 ^= S05;
  S21 ^= S05;
  SMIX(S21, S22, S23, S24);
  S25 ^= S21;
  S06 ^= S21;
  SMIX(S06, S07, S08, S09);
  S10 ^= S06;
  S22 ^= S06;
  SMIX(S22, S23, S24, S25);
  S26 ^= S22;
  S07 ^= S22;
  SMIX(S07, S08, S09, S10);
  S11 ^= S07;
  S23 ^= S07;
  SMIX(S23, S24, S25, S26);
  S27 ^= S23;
  S08 ^= S23;
  SMIX(S08, S09, S10, S11);
  S12 ^= S08;
  S24 ^= S08;
  SMIX(S24, S25, S26, S27);
  S28 ^= S24;
  S09 ^= S24;
  SMIX(S09, S10, S11, S12);
  S13 ^= S09;
  S25 ^= S09;
  SMIX(S25, S26, S27, S28);
  S29 ^= S25;
  S10 ^= S25;
  SMIX(S10, S11, S12, S13);
  S14 ^= S10;
  S26 ^= S10;
  SMIX(S26, S27, S28, S29);
  S00 ^= S26;
  S11 ^= S26;
  SMIX(S11, S12, S13, S14);
  S15 ^= S11;
  S27 ^= S11;
  SMIX(S27, S28, S29, S00);
  S01 ^= S27;
  S12 ^= S27;
  SMIX(S12, S13, S14, S15);
  S16 ^= S12;
  S28 ^= S12;
  SMIX(S28, S29, S00, S01);
  S02 ^= S28;
  S13 ^= S28;
  SMIX(S13, S14, S15, S16);
  S17 ^= S13;
  S29 ^= S13;
  SMIX(S29, S00, S01, S02);
  S03 ^= S29;
  S14 ^= S29;
  SMIX(S14, S15, S16, S17);
  S18 ^= S14;
  S00 ^= S14;
  SMIX(S00, S01, S02, S03);
  S04 ^= S00;
  S15 ^= S00;
  SMIX(S15, S16, S17, S18);
  S19 ^= S15;
  S01 ^= S15;
  SMIX(S01, S02, S03, S04);

  S05 ^= S01;
  S16 ^= S01;

  bool result = ((((sph_u64) SWAP4(S19) << 32) | SWAP4(S18)) <= target);
  if (result)
    output[output[0xFF]++] = SWAP4(gid);
}

#endif // FUGUECOIN_CL
