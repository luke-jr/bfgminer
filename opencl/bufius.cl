/*-
 * Copyright 2009 Colin Percival, 2011 ArtForz, 2011 pooler, 2012 mtrlt,
 * 2012-2013 Con Kolivas, 2014 Bufius.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

/* N (nfactor), CPU/Memory cost parameter */
__constant uint N[] = {
  0x00000001U,  /* never used, padding */
  0x00000002U,
  0x00000004U,
  0x00000008U,
  0x00000010U,
  0x00000020U,
  0x00000040U,
  0x00000080U,
  0x00000100U,
  0x00000200U,
  0x00000400U,  /* 2^10 == 1024, Litecoin scrypt default */
  0x00000800U,
  0x00001000U,
  0x00002000U,
  0x00004000U,
  0x00008000U,
  0x00010000U,
  0x00020000U,
  0x00040000U,
  0x00080000U,
  0x00100000U
};

/* Backwards compatibility, if NFACTOR not defined, default to 10 for scrypt */
#ifndef NFACTOR
#define NFACTOR 10
#endif

__constant uint K[] = {
  0x428a2f98U,
  0x71374491U,
  0xb5c0fbcfU,
  0xe9b5dba5U,
  0x3956c25bU,
  0x59f111f1U,
  0x923f82a4U,
  0xab1c5ed5U,
  0xd807aa98U,
  0x12835b01U,
  0x243185beU, // 10
  0x550c7dc3U,
  0x72be5d74U,
  0x80deb1feU,
  0x9bdc06a7U,
  0xe49b69c1U,
  0xefbe4786U,
  0x0fc19dc6U,
  0x240ca1ccU,
  0x2de92c6fU,
  0x4a7484aaU, // 20
  0x5cb0a9dcU,
  0x76f988daU,
  0x983e5152U,
  0xa831c66dU,
  0xb00327c8U,
  0xbf597fc7U,
  0xc6e00bf3U,
  0xd5a79147U,
  0x06ca6351U,
  0x14292967U, // 30
  0x27b70a85U,
  0x2e1b2138U,
  0x4d2c6dfcU,
  0x53380d13U,
  0x650a7354U,
  0x766a0abbU,
  0x81c2c92eU,
  0x92722c85U,
  0xa2bfe8a1U,
  0xa81a664bU, // 40
  0xc24b8b70U,
  0xc76c51a3U,
  0xd192e819U,
  0xd6990624U,
  0xf40e3585U,
  0x106aa070U,
  0x19a4c116U,
  0x1e376c08U,
  0x2748774cU,
  0x34b0bcb5U, // 50
  0x391c0cb3U,
  0x4ed8aa4aU,
  0x5b9cca4fU,
  0x682e6ff3U,
  0x748f82eeU,
  0x78a5636fU,
  0x84c87814U,
  0x8cc70208U,
  0x90befffaU,
  0xa4506cebU, // 60
  0xbef9a3f7U,
  0xc67178f2U,
  0x98c7e2a2U,
  0xfc08884dU,
  0xcd2a11aeU,
  0x510e527fU,
  0x9b05688cU,
  0xC3910C8EU,
  0xfb6feee7U,
  0x2a01a605U, // 70
  0x0c2e12e0U,
  0x4498517BU,
  0x6a09e667U,
  0xa4ce148bU,
  0x95F61999U,
  0xc19bf174U,
  0xBB67AE85U,
  0x3C6EF372U,
  0xA54FF53AU,
  0x1F83D9ABU, // 80
  0x5BE0CD19U,
  0x5C5C5C5CU,
  0x36363636U,
  0x80000000U,
  0x000007FFU,
  0x00000280U,
  0x000004a0U,
  0x00000300U
};

__constant uint ES[2] = { 0x00FF00FF, 0xFF00FF00 };

#define rotl(x,y)  rotate(x,y)
#define Ch(x,y,z)  bitselect(z,y,x)
#define Maj(x,y,z) Ch((x^z),y,z)
#define mod2(x,y)  (x&(y-1U))
#define mod4(x)    (x&3U)

#define EndianSwap(n) (rotl(n & ES[0], 24U)|rotl(n & ES[1], 8U))

#define Tr2(x)    (rotl(x, 30U) ^ rotl(x, 19U) ^ rotl(x, 10U))
#define Tr1(x)    (rotl(x, 26U) ^ rotl(x, 21U) ^ rotl(x, 7U))
#define Wr2(x)    (rotl(x, 25U) ^ rotl(x, 14U) ^ (x>>3U))
#define Wr1(x)    (rotl(x, 15U) ^ rotl(x, 13U) ^ (x>>10U))

#define RND(a, b, c, d, e, f, g, h, k)      \
  h += Tr1(e) + Ch(e, f, g) + k;    \
  d += h;          \
  h += Tr2(a) + Maj(a, b, c);

#define WUpdate(i) { \
    uint4 tmp1 = (uint4) (W[i].y, W[i].z, W[i].w, W[mod4(i+1)].x);  \
    uint4 tmp2 = (uint4) (W[mod4(i+2)].y, W[mod4(i+2)].z, W[mod4(i+2)].w, W[mod4(i+3)].x);  \
    uint4 tmp3 = (uint4) (W[mod4(i+3)].z, W[mod4(i+3)].w, 0, 0);        \
    W[i] += tmp2 + Wr2(tmp1) + Wr1(tmp3);          \
    W[i] += Wr1((uint4) (0, 0, W[i].x, W[i].y));              \
  }


void SHA256(uint4*restrict state0, uint4*restrict state1, const uint4 block0, const uint4 block1, const uint4 block2, const uint4 block3)
{
  uint4 W[4] = {block0, block1, block2, block3};
  uint4 S0 = *state0;
  uint4 S1 = *state1;

#define A S0.x
#define B S0.y
#define C S0.z
#define D S0.w
#define E S1.x
#define F S1.y
#define G S1.z
#define H S1.w


  RND(A,B,C,D,E,F,G,H, W[0].x+ K[0]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[1]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[2]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[3]);

  RND(E,F,G,H,A,B,C,D, W[1].x+ K[4]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[5]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[6]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[7]);

  RND(A,B,C,D,E,F,G,H, W[2].x+ K[8]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[9]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[10]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[11]);

  RND(E,F,G,H,A,B,C,D, W[3].x+ K[12]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[13]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[14]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[76]);

  WUpdate (0);
  RND(A,B,C,D,E,F,G,H, W[0].x+ K[15]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[16]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[17]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[18]);

  WUpdate (1);
  RND(E,F,G,H,A,B,C,D, W[1].x+ K[19]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[20]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[21]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[22]);

  WUpdate (2);
  RND(A,B,C,D,E,F,G,H, W[2].x+ K[23]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[24]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[25]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[26]);

  WUpdate (3);
  RND(E,F,G,H,A,B,C,D, W[3].x+ K[27]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[28]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[29]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[30]);

  WUpdate (0);
  RND(A,B,C,D,E,F,G,H, W[0].x+ K[31]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[32]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[33]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[34]);

  WUpdate (1);
  RND(E,F,G,H,A,B,C,D, W[1].x+ K[35]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[36]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[37]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[38]);

  WUpdate (2);
  RND(A,B,C,D,E,F,G,H, W[2].x+ K[39]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[40]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[41]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[42]);

  WUpdate (3);
  RND(E,F,G,H,A,B,C,D, W[3].x+ K[43]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[44]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[45]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[46]);

  WUpdate (0);
  RND(A,B,C,D,E,F,G,H, W[0].x+ K[47]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[48]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[49]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[50]);

  WUpdate (1);
  RND(E,F,G,H,A,B,C,D, W[1].x+ K[51]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[52]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[53]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[54]);

  WUpdate (2);
  RND(A,B,C,D,E,F,G,H, W[2].x+ K[55]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[56]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[57]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[58]);

  WUpdate (3);
  RND(E,F,G,H,A,B,C,D, W[3].x+ K[59]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[60]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[61]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[62]);


#undef A
#undef B
#undef C
#undef D
#undef E
#undef F
#undef G
#undef H

  *state0 += S0;
  *state1 += S1;
}

void SHA256_fresh(uint4*restrict state0,uint4*restrict state1, const uint4 block0, const uint4 block1, const uint4 block2, const uint4 block3)
{
#define A (*state0).x
#define B (*state0).y
#define C (*state0).z
#define D (*state0).w
#define E (*state1).x
#define F (*state1).y
#define G (*state1).z
#define H (*state1).w

  uint4 W[4] = {block0, block1, block2, block3};

  D = K[63] +W[0].x;
  H = K[64] +W[0].x;

  C = K[65] +Tr1(D)+Ch(D, K[66], K[67])+W[0].y;
  G = K[68] +C+Tr2(H)+Ch(H, K[69], K[70]);

  B = K[71] +Tr1(C)+Ch(C,D,K[66])+W[0].z;
  F = K[72] +B+Tr2(G)+Maj(G, H, K[73]);

  A = K[74] +Tr1(B)+Ch(B, C, D)+W[0].w;
  E = K[75] +A+Tr2(F)+Maj(F, G, H);

  RND(E,F,G,H,A,B,C,D, W[1].x+ K[4]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[5]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[6]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[7]);

  RND(A,B,C,D,E,F,G,H, W[2].x+ K[8]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[9]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[10]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[11]);

  RND(E,F,G,H,A,B,C,D, W[3].x+ K[12]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[13]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[14]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[76]);

  WUpdate (0);
  RND(A,B,C,D,E,F,G,H, W[0].x+ K[15]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[16]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[17]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[18]);

  WUpdate (1);
  RND(E,F,G,H,A,B,C,D, W[1].x+ K[19]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[20]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[21]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[22]);

  WUpdate (2);
  RND(A,B,C,D,E,F,G,H, W[2].x+ K[23]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[24]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[25]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[26]);

  WUpdate (3);
  RND(E,F,G,H,A,B,C,D, W[3].x+ K[27]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[28]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[29]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[30]);

  WUpdate (0);
  RND(A,B,C,D,E,F,G,H, W[0].x+ K[31]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[32]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[33]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[34]);

  WUpdate (1);
  RND(E,F,G,H,A,B,C,D, W[1].x+ K[35]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[36]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[37]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[38]);

  WUpdate (2);
  RND(A,B,C,D,E,F,G,H, W[2].x+ K[39]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[40]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[41]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[42]);

  WUpdate (3);
  RND(E,F,G,H,A,B,C,D, W[3].x+ K[43]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[44]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[45]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[46]);

  WUpdate (0);
  RND(A,B,C,D,E,F,G,H, W[0].x+ K[47]);
  RND(H,A,B,C,D,E,F,G, W[0].y+ K[48]);
  RND(G,H,A,B,C,D,E,F, W[0].z+ K[49]);
  RND(F,G,H,A,B,C,D,E, W[0].w+ K[50]);

  WUpdate (1);
  RND(E,F,G,H,A,B,C,D, W[1].x+ K[51]);
  RND(D,E,F,G,H,A,B,C, W[1].y+ K[52]);
  RND(C,D,E,F,G,H,A,B, W[1].z+ K[53]);
  RND(B,C,D,E,F,G,H,A, W[1].w+ K[54]);

  WUpdate (2);
  RND(A,B,C,D,E,F,G,H, W[2].x+ K[55]);
  RND(H,A,B,C,D,E,F,G, W[2].y+ K[56]);
  RND(G,H,A,B,C,D,E,F, W[2].z+ K[57]);
  RND(F,G,H,A,B,C,D,E, W[2].w+ K[58]);

  WUpdate (3);
  RND(E,F,G,H,A,B,C,D, W[3].x+ K[59]);
  RND(D,E,F,G,H,A,B,C, W[3].y+ K[60]);
  RND(C,D,E,F,G,H,A,B, W[3].z+ K[61]);
  RND(B,C,D,E,F,G,H,A, W[3].w+ K[62]);

#undef A
#undef B
#undef C
#undef D
#undef E
#undef F
#undef G
#undef H

  *state0 += (uint4)(K[73], K[77], K[78], K[79]);
  *state1 += (uint4)(K[66], K[67], K[80], K[81]);
}

__constant uint fixedW[64] =
{
  0x428a2f99,0xf1374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf794,
  0xf59b89c2,0x73924787,0x23c6886e,0xa42ca65c,0x15ed3627,0x4d6edcbf,0xe28217fc,0xef02488f,
  0xb707775c,0x0468c23f,0xe7e72b4c,0x49e1f1a2,0x4b99c816,0x926d1570,0xaa0fc072,0xadb36e2c,
  0xad87a3ea,0xbcb1d3a3,0x7b993186,0x562b9420,0xbff3ca0c,0xda4b0c23,0x6cd8711a,0x8f337caa,
  0xc91b1417,0xc359dce1,0xa83253a7,0x3b13c12d,0x9d3d725d,0xd9031a84,0xb1a03340,0x16f58012,
  0xe64fb6a2,0xe84d923a,0xe93a5730,0x09837686,0x078ff753,0x29833341,0xd5de0b7e,0x6948ccf4,
  0xe0a1adbe,0x7c728e11,0x511c78e4,0x315b45bd,0xfca71413,0xea28f96a,0x79703128,0x4e1ef848,
};

void SHA256_fixed(uint4*restrict state0,uint4*restrict state1)
{
  uint4 S0 = *state0;
  uint4 S1 = *state1;

#define A S0.x
#define B S0.y
#define C S0.z
#define D S0.w
#define E S1.x
#define F S1.y
#define G S1.z
#define H S1.w

  RND(A,B,C,D,E,F,G,H, fixedW[0]);
  RND(H,A,B,C,D,E,F,G, fixedW[1]);
  RND(G,H,A,B,C,D,E,F, fixedW[2]);
  RND(F,G,H,A,B,C,D,E, fixedW[3]);
  RND(E,F,G,H,A,B,C,D, fixedW[4]);
  RND(D,E,F,G,H,A,B,C, fixedW[5]);
  RND(C,D,E,F,G,H,A,B, fixedW[6]);
  RND(B,C,D,E,F,G,H,A, fixedW[7]);
  RND(A,B,C,D,E,F,G,H, fixedW[8]);
  RND(H,A,B,C,D,E,F,G, fixedW[9]);
  RND(G,H,A,B,C,D,E,F, fixedW[10]);
  RND(F,G,H,A,B,C,D,E, fixedW[11]);
  RND(E,F,G,H,A,B,C,D, fixedW[12]);
  RND(D,E,F,G,H,A,B,C, fixedW[13]);
  RND(C,D,E,F,G,H,A,B, fixedW[14]);
  RND(B,C,D,E,F,G,H,A, fixedW[15]);
  RND(A,B,C,D,E,F,G,H, fixedW[16]);
  RND(H,A,B,C,D,E,F,G, fixedW[17]);
  RND(G,H,A,B,C,D,E,F, fixedW[18]);
  RND(F,G,H,A,B,C,D,E, fixedW[19]);
  RND(E,F,G,H,A,B,C,D, fixedW[20]);
  RND(D,E,F,G,H,A,B,C, fixedW[21]);
  RND(C,D,E,F,G,H,A,B, fixedW[22]);
  RND(B,C,D,E,F,G,H,A, fixedW[23]);
  RND(A,B,C,D,E,F,G,H, fixedW[24]);
  RND(H,A,B,C,D,E,F,G, fixedW[25]);
  RND(G,H,A,B,C,D,E,F, fixedW[26]);
  RND(F,G,H,A,B,C,D,E, fixedW[27]);
  RND(E,F,G,H,A,B,C,D, fixedW[28]);
  RND(D,E,F,G,H,A,B,C, fixedW[29]);
  RND(C,D,E,F,G,H,A,B, fixedW[30]);
  RND(B,C,D,E,F,G,H,A, fixedW[31]);
  RND(A,B,C,D,E,F,G,H, fixedW[32]);
  RND(H,A,B,C,D,E,F,G, fixedW[33]);
  RND(G,H,A,B,C,D,E,F, fixedW[34]);
  RND(F,G,H,A,B,C,D,E, fixedW[35]);
  RND(E,F,G,H,A,B,C,D, fixedW[36]);
  RND(D,E,F,G,H,A,B,C, fixedW[37]);
  RND(C,D,E,F,G,H,A,B, fixedW[38]);
  RND(B,C,D,E,F,G,H,A, fixedW[39]);
  RND(A,B,C,D,E,F,G,H, fixedW[40]);
  RND(H,A,B,C,D,E,F,G, fixedW[41]);
  RND(G,H,A,B,C,D,E,F, fixedW[42]);
  RND(F,G,H,A,B,C,D,E, fixedW[43]);
  RND(E,F,G,H,A,B,C,D, fixedW[44]);
  RND(D,E,F,G,H,A,B,C, fixedW[45]);
  RND(C,D,E,F,G,H,A,B, fixedW[46]);
  RND(B,C,D,E,F,G,H,A, fixedW[47]);
  RND(A,B,C,D,E,F,G,H, fixedW[48]);
  RND(H,A,B,C,D,E,F,G, fixedW[49]);
  RND(G,H,A,B,C,D,E,F, fixedW[50]);
  RND(F,G,H,A,B,C,D,E, fixedW[51]);
  RND(E,F,G,H,A,B,C,D, fixedW[52]);
  RND(D,E,F,G,H,A,B,C, fixedW[53]);
  RND(C,D,E,F,G,H,A,B, fixedW[54]);
  RND(B,C,D,E,F,G,H,A, fixedW[55]);
  RND(A,B,C,D,E,F,G,H, fixedW[56]);
  RND(H,A,B,C,D,E,F,G, fixedW[57]);
  RND(G,H,A,B,C,D,E,F, fixedW[58]);
  RND(F,G,H,A,B,C,D,E, fixedW[59]);
  RND(E,F,G,H,A,B,C,D, fixedW[60]);
  RND(D,E,F,G,H,A,B,C, fixedW[61]);
  RND(C,D,E,F,G,H,A,B, fixedW[62]);
  RND(B,C,D,E,F,G,H,A, fixedW[63]);

#undef A
#undef B
#undef C
#undef D
#undef E
#undef F
#undef G
#undef H

  *state0 += S0;
  *state1 += S1;
}

void shittify(uint4 B[8])
{
  uint4 tmp[8];
  tmp[0] = (uint4)(B[1].x,B[2].y,B[3].z,B[0].w);
  tmp[1] = (uint4)(B[2].x,B[3].y,B[0].z,B[1].w);
  tmp[2] = (uint4)(B[3].x,B[0].y,B[1].z,B[2].w);
  tmp[3] = (uint4)(B[0].x,B[1].y,B[2].z,B[3].w);
  tmp[4] = (uint4)(B[5].x,B[6].y,B[7].z,B[4].w);
  tmp[5] = (uint4)(B[6].x,B[7].y,B[4].z,B[5].w);
  tmp[6] = (uint4)(B[7].x,B[4].y,B[5].z,B[6].w);
  tmp[7] = (uint4)(B[4].x,B[5].y,B[6].z,B[7].w);

#pragma unroll 8
  for(uint i=0; i<8; ++i)
    B[i] = EndianSwap(tmp[i]);
}

void unshittify(uint4 B[8])
{
  uint4 tmp[8];
  tmp[0] = (uint4)(B[3].x,B[2].y,B[1].z,B[0].w);
  tmp[1] = (uint4)(B[0].x,B[3].y,B[2].z,B[1].w);
  tmp[2] = (uint4)(B[1].x,B[0].y,B[3].z,B[2].w);
  tmp[3] = (uint4)(B[2].x,B[1].y,B[0].z,B[3].w);
  tmp[4] = (uint4)(B[7].x,B[6].y,B[5].z,B[4].w);
  tmp[5] = (uint4)(B[4].x,B[7].y,B[6].z,B[5].w);
  tmp[6] = (uint4)(B[5].x,B[4].y,B[7].z,B[6].w);
  tmp[7] = (uint4)(B[6].x,B[5].y,B[4].z,B[7].w);

#pragma unroll 8
  for(uint i=0; i<8; ++i)
    B[i] = EndianSwap(tmp[i]);
}

void salsa(uint4 B[8])
{
  uint i;
  uint4 w[4];

#pragma unroll 4
  for(i=0; i<4; ++i)
    w[i] = (B[i]^=B[i+4]);

#pragma unroll 4
  for(i=0; i<4; ++i)
  {
    w[0] ^= rotl(w[3]     +w[2]     , 7U);
    w[1] ^= rotl(w[0]     +w[3]     , 9U);
    w[2] ^= rotl(w[1]     +w[0]     ,13U);
    w[3] ^= rotl(w[2]     +w[1]     ,18U);
    w[2] ^= rotl(w[3].wxyz+w[0].zwxy, 7U);
    w[1] ^= rotl(w[2].wxyz+w[3].zwxy, 9U);
    w[0] ^= rotl(w[1].wxyz+w[2].zwxy,13U);
    w[3] ^= rotl(w[0].wxyz+w[1].zwxy,18U);
  }

#pragma unroll 4
  for(i=0; i<4; ++i)
    w[i] = (B[i+4]^=(B[i]+=w[i]));

#pragma unroll 4
  for(i=0; i<4; ++i)
  {
    w[0] ^= rotl(w[3]     +w[2]     , 7U);
    w[1] ^= rotl(w[0]     +w[3]     , 9U);
    w[2] ^= rotl(w[1]     +w[0]     ,13U);
    w[3] ^= rotl(w[2]     +w[1]     ,18U);
    w[2] ^= rotl(w[3].wxyz+w[0].zwxy, 7U);
    w[1] ^= rotl(w[2].wxyz+w[3].zwxy, 9U);
    w[0] ^= rotl(w[1].wxyz+w[2].zwxy,13U);
    w[3] ^= rotl(w[0].wxyz+w[1].zwxy,18U);
  }

#pragma unroll 4
  for(i=0; i<4; ++i)
    B[i+4] += w[i];
}


__constant uint COy=CONCURRENT_THREADS*8;
void scrypt_core(uint4 X[8], __global uint4* const restrict lookup)
{
  const uint lookup_bits = popcount((uint)(LOOKUP_GAP-1U));
  const uint write_loop  = N[NFACTOR-lookup_bits];
  const uint COx         = rotl((uint)(get_global_id(0)%CONCURRENT_THREADS), 3U);
  uint CO                = COx;
  uint i, j, z, additional_salsa;
  uint4 V[8];

  shittify(X);

  // write lookup table to memory
#pragma unroll 1
  for (i=0; i<write_loop; ++i) {

#pragma unroll 8
    for(z=0; z<8; ++z)
      lookup[CO+z] = X[z];

#pragma unroll 2
    for (j=0; j<LOOKUP_GAP; ++j)
      salsa(X);

    CO += COy;
  }

  // read lookup table from memory and compute
#pragma unroll 1
  for (i=0; i<N[NFACTOR]; ++i) {
    j = mul24((X[7].x & (N[NFACTOR]-LOOKUP_GAP)), (uint)(CONCURRENT_THREADS));
    CO = COx + rotl(j, 3U-lookup_bits);
    additional_salsa = mod2(X[7].x, LOOKUP_GAP);

#pragma unroll 8
    for(z=0; z<8; ++z)
      V[z] = lookup[CO+z];

#pragma unroll 1
    for (j=0; j<additional_salsa; ++j)
      salsa(V);

#pragma unroll 8
    for(z=0; z<8; ++z)
      X[z] ^= V[z];

    salsa(X);
  }

  unshittify(X);
}


#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable
#define SCRYPT_FOUND (0xFF)
#define SETFOUND(Xnonce) output[atomic_add(&output[SCRYPT_FOUND], 1)] = Xnonce;

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__attribute__((max_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global const uint4 * const restrict input,
           volatile __global uint* const restrict output,
           __global uint4* const restrict padcache,
           const uint4 midstate0, const uint4 midstate16,
           const uint target)
{
  uint4 X[8];
  uint4 tstate0, tstate1, ostate0, ostate1, tmp0, tmp1;
  uint4 data = (uint4)(input[4].x, input[4].y, input[4].z, get_global_id(0));
  uint4 pad0 = midstate0, pad1 = midstate16;

  SHA256(&pad0, &pad1, data, (uint4)(K[84], 0U, 0U, 0U), (uint4)(0U, 0U, 0U, 0U), (uint4)(0U, 0U, 0U, K[86]));
  SHA256_fresh(&ostate0, &ostate1, pad0^ K[82], pad1^ K[82], K[82], K[82]);
  SHA256_fresh(&tstate0, &tstate1, pad0^ K[83], pad1^ K[83], K[83], K[83]);

  tmp0 = tstate0;
  tmp1 = tstate1;
  SHA256(&tstate0, &tstate1, input[0], input[1], input[2], input[3]);

#pragma unroll 4
  for (uint i=0; i<4; ++i)
  {
    pad0 = tstate0;
    pad1 = tstate1;
    X[rotl(i, 1U)  ] = ostate0;
    X[rotl(i, 1U)+1] = ostate1;

    SHA256(&pad0, &pad1, data, (uint4)(i+1,K[84],0,0), (uint4)(0,0,0,0), (uint4)(0,0,0, K[87]));
    SHA256(&X[rotl(i, 1U)], &X[rotl(i, 1U)+1], pad0, pad1, (uint4)(K[84], 0U, 0U, 0U), (uint4)(0U, 0U, 0U, K[88]));
  }

  scrypt_core(X, padcache);

  SHA256(&tmp0,&tmp1, X[0], X[1], X[2], X[3]);
  SHA256(&tmp0,&tmp1, X[4], X[5], X[6], X[7]);
  SHA256_fixed(&tmp0, &tmp1);
  SHA256(&ostate0, &ostate1, tmp0, tmp1, (uint4)(K[84], 0U, 0U, 0U), (uint4)(0U, 0U, 0U, K[88]));

  bool found = EndianSwap(ostate1.w) <= target;
  barrier(CLK_GLOBAL_MEM_FENCE);
  if (found)
    SETFOUND(get_global_id(0));
}

