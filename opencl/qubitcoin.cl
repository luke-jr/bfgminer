/*
 * QubitCoin kernel implementation.
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

#ifndef QUBITCOIN_CL
#define QUBITCOIN_CL

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

#include "luffa.cl"
#include "cubehash.cl"
#include "shavite.cl"
#include "simd.cl"
#include "echo.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC32BE(x) (*(const __global sph_u32 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC32BE(x) SWAP4(*(const __global sph_u32 *) (x));
#endif

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

#define CONST_EXP2  q[i+0] + SPH_ROTL64(q[i+1], 5)  + q[i+2] + SPH_ROTL64(q[i+3], 11) + \
                    q[i+4] + SPH_ROTL64(q[i+5], 27) + q[i+6] + SPH_ROTL64(q[i+7], 32) + \
                    q[i+8] + SPH_ROTL64(q[i+9], 37) + q[i+10] + SPH_ROTL64(q[i+11], 43) + \
                    q[i+12] + SPH_ROTL64(q[i+13], 53) + (SHR(q[i+14],1) ^ q[i+14]) + (SHR(q[i+15],2) ^ q[i+15])

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
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

  // luffa
  {
    sph_u32 V00 = SPH_C32(0x6d251e69), V01 = SPH_C32(0x44b051e0), V02 = SPH_C32(0x4eaa6fb4), V03 = SPH_C32(0xdbf78465), V04 = SPH_C32(0x6e292011), V05 = SPH_C32(0x90152df4), V06 = SPH_C32(0xee058139), V07 = SPH_C32(0xdef610bb);
    sph_u32 V10 = SPH_C32(0xc3b44b95), V11 = SPH_C32(0xd9d2f256), V12 = SPH_C32(0x70eee9a0), V13 = SPH_C32(0xde099fa3), V14 = SPH_C32(0x5d9b0557), V15 = SPH_C32(0x8fc944b3), V16 = SPH_C32(0xcf1ccf0e), V17 = SPH_C32(0x746cd581);
    sph_u32 V20 = SPH_C32(0xf7efc89d), V21 = SPH_C32(0x5dba5781), V22 = SPH_C32(0x04016ce5), V23 = SPH_C32(0xad659c05), V24 = SPH_C32(0x0306194f), V25 = SPH_C32(0x666d1836), V26 = SPH_C32(0x24aa230a), V27 = SPH_C32(0x8b264ae7);
    sph_u32 V30 = SPH_C32(0x858075d5), V31 = SPH_C32(0x36d79cce), V32 = SPH_C32(0xe571f7d7), V33 = SPH_C32(0x204b1f67), V34 = SPH_C32(0x35870c6a), V35 = SPH_C32(0x57e9e923), V36 = SPH_C32(0x14bcb808), V37 = SPH_C32(0x7cde72ce);
    sph_u32 V40 = SPH_C32(0x6c68e9be), V41 = SPH_C32(0x5ec41e22), V42 = SPH_C32(0xc825b7c7), V43 = SPH_C32(0xaffb4363), V44 = SPH_C32(0xf5df3999), V45 = SPH_C32(0x0fc688f1), V46 = SPH_C32(0xb07224cc), V47 = SPH_C32(0x03e86cea);

    DECL_TMP8(M);

    M0 = DEC32BE(block + 0);
    M1 = DEC32BE(block + 4);
    M2 = DEC32BE(block + 8);
    M3 = DEC32BE(block + 12);
    M4 = DEC32BE(block + 16);
    M5 = DEC32BE(block + 20);
    M6 = DEC32BE(block + 24);
    M7 = DEC32BE(block + 28);

    for(uint i = 0; i < 5; i++)
    {
      MI5;
      LUFFA_P5;

      if(i == 0) {
        M0 = DEC32BE(block + 32);
        M1 = DEC32BE(block + 36);
        M2 = DEC32BE(block + 40);
        M3 = DEC32BE(block + 44);
        M4 = DEC32BE(block + 48);
        M5 = DEC32BE(block + 52);
        M6 = DEC32BE(block + 56);
        M7 = DEC32BE(block + 60);
      } else if(i == 1) {
        M0 = DEC32BE(block + 64);
        M1 = DEC32BE(block + 68);
        M2 = DEC32BE(block + 72);
        M3 = SWAP4(gid);
        M4 = 0x80000000;
        M5 = M6 = M7 = 0;
      } else if(i == 2) {
        M0 = M1 = M2 = M3 = M4 = M5 = M6 = M7 = 0;
      } else if(i == 3) {
        hash.h4[1] = V00 ^ V10 ^ V20 ^ V30 ^ V40;
        hash.h4[0] = V01 ^ V11 ^ V21 ^ V31 ^ V41;
        hash.h4[3] = V02 ^ V12 ^ V22 ^ V32 ^ V42;
        hash.h4[2] = V03 ^ V13 ^ V23 ^ V33 ^ V43;
        hash.h4[5] = V04 ^ V14 ^ V24 ^ V34 ^ V44;
        hash.h4[4] = V05 ^ V15 ^ V25 ^ V35 ^ V45;
        hash.h4[7] = V06 ^ V16 ^ V26 ^ V36 ^ V46;
        hash.h4[6] = V07 ^ V17 ^ V27 ^ V37 ^ V47;
      }
    }
    hash.h4[9] = V00 ^ V10 ^ V20 ^ V30 ^ V40;
    hash.h4[8] = V01 ^ V11 ^ V21 ^ V31 ^ V41;
    hash.h4[11] = V02 ^ V12 ^ V22 ^ V32 ^ V42;
    hash.h4[10] = V03 ^ V13 ^ V23 ^ V33 ^ V43;
    hash.h4[13] = V04 ^ V14 ^ V24 ^ V34 ^ V44;
    hash.h4[12] = V05 ^ V15 ^ V25 ^ V35 ^ V45;
    hash.h4[15] = V06 ^ V16 ^ V26 ^ V36 ^ V46;
    hash.h4[14] = V07 ^ V17 ^ V27 ^ V37 ^ V47;
  }

  // cubehash.h1
  {
    sph_u32 x0 = SPH_C32(0x2AEA2A61), x1 = SPH_C32(0x50F494D4), x2 = SPH_C32(0x2D538B8B), x3 = SPH_C32(0x4167D83E);
    sph_u32 x4 = SPH_C32(0x3FEE2313), x5 = SPH_C32(0xC701CF8C), x6 = SPH_C32(0xCC39968E), x7 = SPH_C32(0x50AC5695);
    sph_u32 x8 = SPH_C32(0x4D42C787), x9 = SPH_C32(0xA647A8B3), xa = SPH_C32(0x97CF0BEF), xb = SPH_C32(0x825B4537);
    sph_u32 xc = SPH_C32(0xEEF864D2), xd = SPH_C32(0xF22090C4), xe = SPH_C32(0xD0E5CD33), xf = SPH_C32(0xA23911AE);
    sph_u32 xg = SPH_C32(0xFCD398D9), xh = SPH_C32(0x148FE485), xi = SPH_C32(0x1B017BEF), xj = SPH_C32(0xB6444532);
    sph_u32 xk = SPH_C32(0x6A536159), xl = SPH_C32(0x2FF5781C), xm = SPH_C32(0x91FA7934), xn = SPH_C32(0x0DBADEA9);
    sph_u32 xo = SPH_C32(0xD65C8A2B), xp = SPH_C32(0xA5A70E75), xq = SPH_C32(0xB1C62456), xr = SPH_C32(0xBC796576);
    sph_u32 xs = SPH_C32(0x1921C8F7), xt = SPH_C32(0xE7989AF1), xu = SPH_C32(0x7795D246), xv = SPH_C32(0xD43E3B44);

    x0 ^= SWAP4(hash.h4[1]);
    x1 ^= SWAP4(hash.h4[0]);
    x2 ^= SWAP4(hash.h4[3]);
    x3 ^= SWAP4(hash.h4[2]);
    x4 ^= SWAP4(hash.h4[5]);
    x5 ^= SWAP4(hash.h4[4]);
    x6 ^= SWAP4(hash.h4[7]);
    x7 ^= SWAP4(hash.h4[6]);

    for (int i = 0; i < 13; i ++) {
      SIXTEEN_ROUNDS;

      if (i == 0) {
        x0 ^= SWAP4(hash.h4[9]);
        x1 ^= SWAP4(hash.h4[8]);
        x2 ^= SWAP4(hash.h4[11]);
        x3 ^= SWAP4(hash.h4[10]);
        x4 ^= SWAP4(hash.h4[13]);
        x5 ^= SWAP4(hash.h4[12]);
        x6 ^= SWAP4(hash.h4[15]);
        x7 ^= SWAP4(hash.h4[14]);
      } else if(i == 1) {
        x0 ^= 0x80;
      } else if (i == 2) {
        xv ^= SPH_C32(1);
      }
    }

    hash.h4[0] = x0;
    hash.h4[1] = x1;
    hash.h4[2] = x2;
    hash.h4[3] = x3;
    hash.h4[4] = x4;
    hash.h4[5] = x5;
    hash.h4[6] = x6;
    hash.h4[7] = x7;
    hash.h4[8] = x8;
    hash.h4[9] = x9;
    hash.h4[10] = xa;
    hash.h4[11] = xb;
    hash.h4[12] = xc;
    hash.h4[13] = xd;
    hash.h4[14] = xe;
    hash.h4[15] = xf;
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

  // simd
  {
    s32 q[256];
    unsigned char x[128];
    for(unsigned int i = 0; i < 64; i++)
    x[i] = hash.h1[i];
    for(unsigned int i = 64; i < 128; i++)
    x[i] = 0;

    u32 A0 = C32(0x0BA16B95), A1 = C32(0x72F999AD), A2 = C32(0x9FECC2AE), A3 = C32(0xBA3264FC), A4 = C32(0x5E894929), A5 = C32(0x8E9F30E5), A6 = C32(0x2F1DAA37), A7 = C32(0xF0F2C558);
    u32 B0 = C32(0xAC506643), B1 = C32(0xA90635A5), B2 = C32(0xE25B878B), B3 = C32(0xAAB7878F), B4 = C32(0x88817F7A), B5 = C32(0x0A02892B), B6 = C32(0x559A7550), B7 = C32(0x598F657E);
    u32 C0 = C32(0x7EEF60A1), C1 = C32(0x6B70E3E8), C2 = C32(0x9C1714D1), C3 = C32(0xB958E2A8), C4 = C32(0xAB02675E), C5 = C32(0xED1C014F), C6 = C32(0xCD8D65BB), C7 = C32(0xFDB7A257);
    u32 D0 = C32(0x09254899), D1 = C32(0xD699C7BC), D2 = C32(0x9019B6DC), D3 = C32(0x2B9022E4), D4 = C32(0x8FA14956), D5 = C32(0x21BF9BD3), D6 = C32(0xB94D0943), D7 = C32(0x6FFDDC22);

    FFT256(0, 1, 0, ll1);
    for (int i = 0; i < 256; i ++) {
      s32 tq;

      tq = q[i] + yoff_b_n[i];
      tq = REDS2(tq);
      tq = REDS1(tq);
      tq = REDS1(tq);
      q[i] = (tq <= 128 ? tq : tq - 257);
    }

    A0 ^= hash.h4[0];
    A1 ^= hash.h4[1];
    A2 ^= hash.h4[2];
    A3 ^= hash.h4[3];
    A4 ^= hash.h4[4];
    A5 ^= hash.h4[5];
    A6 ^= hash.h4[6];
    A7 ^= hash.h4[7];
    B0 ^= hash.h4[8];
    B1 ^= hash.h4[9];
    B2 ^= hash.h4[10];
    B3 ^= hash.h4[11];
    B4 ^= hash.h4[12];
    B5 ^= hash.h4[13];
    B6 ^= hash.h4[14];
    B7 ^= hash.h4[15];

    ONE_ROUND_BIG(0_, 0,  3, 23, 17, 27);
    ONE_ROUND_BIG(1_, 1, 28, 19, 22,  7);
    ONE_ROUND_BIG(2_, 2, 29,  9, 15,  5);
    ONE_ROUND_BIG(3_, 3,  4, 13, 10, 25);

    STEP_BIG(
      C32(0x0BA16B95), C32(0x72F999AD), C32(0x9FECC2AE), C32(0xBA3264FC),
      C32(0x5E894929), C32(0x8E9F30E5), C32(0x2F1DAA37), C32(0xF0F2C558),
      IF,  4, 13, PP8_4_);
    STEP_BIG(
      C32(0xAC506643), C32(0xA90635A5), C32(0xE25B878B), C32(0xAAB7878F),
      C32(0x88817F7A), C32(0x0A02892B), C32(0x559A7550), C32(0x598F657E),
      IF, 13, 10, PP8_5_);
    STEP_BIG(
      C32(0x7EEF60A1), C32(0x6B70E3E8), C32(0x9C1714D1), C32(0xB958E2A8),
      C32(0xAB02675E), C32(0xED1C014F), C32(0xCD8D65BB), C32(0xFDB7A257),
      IF, 10, 25, PP8_6_);
    STEP_BIG(
      C32(0x09254899), C32(0xD699C7BC), C32(0x9019B6DC), C32(0x2B9022E4),
      C32(0x8FA14956), C32(0x21BF9BD3), C32(0xB94D0943), C32(0x6FFDDC22),
      IF, 25,  4, PP8_0_);

    u32 COPY_A0 = A0, COPY_A1 = A1, COPY_A2 = A2, COPY_A3 = A3, COPY_A4 = A4, COPY_A5 = A5, COPY_A6 = A6, COPY_A7 = A7;
    u32 COPY_B0 = B0, COPY_B1 = B1, COPY_B2 = B2, COPY_B3 = B3, COPY_B4 = B4, COPY_B5 = B5, COPY_B6 = B6, COPY_B7 = B7;
    u32 COPY_C0 = C0, COPY_C1 = C1, COPY_C2 = C2, COPY_C3 = C3, COPY_C4 = C4, COPY_C5 = C5, COPY_C6 = C6, COPY_C7 = C7;
    u32 COPY_D0 = D0, COPY_D1 = D1, COPY_D2 = D2, COPY_D3 = D3, COPY_D4 = D4, COPY_D5 = D5, COPY_D6 = D6, COPY_D7 = D7;

    #define q SIMD_Q

    A0 ^= 0x200;

    ONE_ROUND_BIG(0_, 0,  3, 23, 17, 27);
    ONE_ROUND_BIG(1_, 1, 28, 19, 22,  7);
    ONE_ROUND_BIG(2_, 2, 29,  9, 15,  5);
    ONE_ROUND_BIG(3_, 3,  4, 13, 10, 25);
    STEP_BIG(
      COPY_A0, COPY_A1, COPY_A2, COPY_A3,
      COPY_A4, COPY_A5, COPY_A6, COPY_A7,
      IF,  4, 13, PP8_4_);
    STEP_BIG(
      COPY_B0, COPY_B1, COPY_B2, COPY_B3,
      COPY_B4, COPY_B5, COPY_B6, COPY_B7,
      IF, 13, 10, PP8_5_);
    STEP_BIG(
      COPY_C0, COPY_C1, COPY_C2, COPY_C3,
      COPY_C4, COPY_C5, COPY_C6, COPY_C7,
      IF, 10, 25, PP8_6_);
    STEP_BIG(
      COPY_D0, COPY_D1, COPY_D2, COPY_D3,
      COPY_D4, COPY_D5, COPY_D6, COPY_D7,
      IF, 25,  4, PP8_0_);
    #undef q

    hash.h4[0] = A0;
    hash.h4[1] = A1;
    hash.h4[2] = A2;
    hash.h4[3] = A3;
    hash.h4[4] = A4;
    hash.h4[5] = A5;
    hash.h4[6] = A6;
    hash.h4[7] = A7;
    hash.h4[8] = B0;
    hash.h4[9] = B1;
    hash.h4[10] = B2;
    hash.h4[11] = B3;
    hash.h4[12] = B4;
    hash.h4[13] = B5;
    hash.h4[14] = B6;
    hash.h4[15] = B7;
  }

  // echo
  {
    sph_u64 W00, W01, W10, W11, W20, W21, W30, W31, W40, W41, W50, W51, W60, W61, W70, W71, W80, W81, W90, W91, WA0, WA1, WB0, WB1, WC0, WC1, WD0, WD1, WE0, WE1, WF0, WF1;
    sph_u64 Vb00, Vb01, Vb10, Vb11, Vb20, Vb21, Vb30, Vb31, Vb40, Vb41, Vb50, Vb51, Vb60, Vb61, Vb70, Vb71;
    Vb00 = Vb10 = Vb20 = Vb30 = Vb40 = Vb50 = Vb60 = Vb70 = 512UL;
    Vb01 = Vb11 = Vb21 = Vb31 = Vb41 = Vb51 = Vb61 = Vb71 = 0;

    sph_u32 K0 = 512;
    sph_u32 K1 = 0;
    sph_u32 K2 = 0;
    sph_u32 K3 = 0;

    W00 = Vb00;
    W01 = Vb01;
    W10 = Vb10;
    W11 = Vb11;
    W20 = Vb20;
    W21 = Vb21;
    W30 = Vb30;
    W31 = Vb31;
    W40 = Vb40;
    W41 = Vb41;
    W50 = Vb50;
    W51 = Vb51;
    W60 = Vb60;
    W61 = Vb61;
    W70 = Vb70;
    W71 = Vb71;
    W80 = hash.h8[0];
    W81 = hash.h8[1];
    W90 = hash.h8[2];
    W91 = hash.h8[3];
    WA0 = hash.h8[4];
    WA1 = hash.h8[5];
    WB0 = hash.h8[6];
    WB1 = hash.h8[7];
    WC0 = 0x80;
    WC1 = 0;
    WD0 = 0;
    WD1 = 0;
    WE0 = 0;
    WE1 = 0x200000000000000;
    WF0 = 0x200;
    WF1 = 0;

    for (unsigned u = 0; u < 10; u ++) {
      BIG_ROUND;
    }

    Vb00 ^= hash.h8[0] ^ W00 ^ W80;
    Vb01 ^= hash.h8[1] ^ W01 ^ W81;
    Vb10 ^= hash.h8[2] ^ W10 ^ W90;
    Vb11 ^= hash.h8[3] ^ W11 ^ W91;
    Vb20 ^= hash.h8[4] ^ W20 ^ WA0;
    Vb21 ^= hash.h8[5] ^ W21 ^ WA1;
    Vb30 ^= hash.h8[6] ^ W30 ^ WB0;
    Vb31 ^= hash.h8[7] ^ W31 ^ WB1;

    bool result = (Vb11 <= target);
    if (result)
      output[output[0xFF]++] = SWAP4(gid);
  }
}

#endif // QUBITCOIN_CL