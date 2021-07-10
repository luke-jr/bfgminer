/*-
 * Copyright 2009 Colin Percival
 * Copyright 2011 ArtForz
 * Copyright 2011 pooler
 * Copyright 2012 mtrlt
 * Copyright 2012-2013 Con Kolivas
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

// kernel-interface: scrypt scrypt

__constant uint ES[2] = { 0x00FF00FF, 0xFF00FF00 };
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
	0x000003FFU,
	0x00000280U,
	0x000004a0U,
	0x00000300U
};

#define rotl(x,y) rotate(x,y)
#define Ch(x,y,z) bitselect(z,y,x)
#define Maj(x,y,z) Ch((x^z),y,z)

#define EndianSwap(n) (rotl(n & ES[0], 24U)|rotl(n & ES[1], 8U))

#define Tr2(x)		(rotl(x, 30U) ^ rotl(x, 19U) ^ rotl(x, 10U))
#define Tr1(x)		(rotl(x, 26U) ^ rotl(x, 21U) ^ rotl(x, 7U))
#define Wr2(x)		(rotl(x, 25U) ^ rotl(x, 14U) ^ (x>>3U))
#define Wr1(x)		(rotl(x, 15U) ^ rotl(x, 13U) ^ (x>>10U))

#define RND(a, b, c, d, e, f, g, h, k)	\
	h += Tr1(e); 			\
	h += Ch(e, f, g); 		\
	h += k;				\
	d += h;				\
	h += Tr2(a); 			\
	h += Maj(a, b, c);

void SHA256(uint4*restrict state0,uint4*restrict state1, const uint4 block0, const uint4 block1, const uint4 block2, const uint4 block3)
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

	uint4 W[4];

	W[ 0].x = block0.x;
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[0]);
	W[ 0].y = block0.y;
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[1]);
	W[ 0].z = block0.z;
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[2]);
	W[ 0].w = block0.w;
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[3]);

	W[ 1].x = block1.x;
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[4]);
	W[ 1].y = block1.y;
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[5]);
	W[ 1].z = block1.z;
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[6]);
	W[ 1].w = block1.w;
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[7]);

	W[ 2].x = block2.x;
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[8]);
	W[ 2].y = block2.y;
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[9]);
	W[ 2].z = block2.z;
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[10]);
	W[ 2].w = block2.w;
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[11]);

	W[ 3].x = block3.x;
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[12]);
	W[ 3].y = block3.y;
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[13]);
	W[ 3].z = block3.z;
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[14]);
	W[ 3].w = block3.w;
	RND(B,C,D,E,F,G,H,A, W[3].w+ K[76]);

	W[ 0].x += Wr1(W[ 3].z) + W[ 2].y + Wr2(W[ 0].y);
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[15]);

	W[ 0].y += Wr1(W[ 3].w) + W[ 2].z + Wr2(W[ 0].z);
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[16]);

	W[ 0].z += Wr1(W[ 0].x) + W[ 2].w + Wr2(W[ 0].w);
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[17]);

	W[ 0].w += Wr1(W[ 0].y) + W[ 3].x + Wr2(W[ 1].x);
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[18]);

	W[ 1].x += Wr1(W[ 0].z) + W[ 3].y + Wr2(W[ 1].y);
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[19]);

	W[ 1].y += Wr1(W[ 0].w) + W[ 3].z + Wr2(W[ 1].z);
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[20]);

	W[ 1].z += Wr1(W[ 1].x) + W[ 3].w + Wr2(W[ 1].w);
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[21]);

	W[ 1].w += Wr1(W[ 1].y) + W[ 0].x + Wr2(W[ 2].x);
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[22]);

	W[ 2].x += Wr1(W[ 1].z) + W[ 0].y + Wr2(W[ 2].y);
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[23]);

	W[ 2].y += Wr1(W[ 1].w) + W[ 0].z + Wr2(W[ 2].z);
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[24]);

	W[ 2].z += Wr1(W[ 2].x) + W[ 0].w + Wr2(W[ 2].w);
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[25]);

	W[ 2].w += Wr1(W[ 2].y) + W[ 1].x + Wr2(W[ 3].x);
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[26]);

	W[ 3].x += Wr1(W[ 2].z) + W[ 1].y + Wr2(W[ 3].y);
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[27]);

	W[ 3].y += Wr1(W[ 2].w) + W[ 1].z + Wr2(W[ 3].z);
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[28]);

	W[ 3].z += Wr1(W[ 3].x) + W[ 1].w + Wr2(W[ 3].w);
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[29]);

	W[ 3].w += Wr1(W[ 3].y) + W[ 2].x + Wr2(W[ 0].x);
	RND(B,C,D,E,F,G,H,A, W[3].w+ K[30]);

	W[ 0].x += Wr1(W[ 3].z) + W[ 2].y + Wr2(W[ 0].y);
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[31]);

	W[ 0].y += Wr1(W[ 3].w) + W[ 2].z + Wr2(W[ 0].z);
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[32]);

	W[ 0].z += Wr1(W[ 0].x) + W[ 2].w + Wr2(W[ 0].w);
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[33]);

	W[ 0].w += Wr1(W[ 0].y) + W[ 3].x + Wr2(W[ 1].x);
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[34]);

	W[ 1].x += Wr1(W[ 0].z) + W[ 3].y + Wr2(W[ 1].y);
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[35]);

	W[ 1].y += Wr1(W[ 0].w) + W[ 3].z + Wr2(W[ 1].z);
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[36]);

	W[ 1].z += Wr1(W[ 1].x) + W[ 3].w + Wr2(W[ 1].w);
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[37]);

	W[ 1].w += Wr1(W[ 1].y) + W[ 0].x + Wr2(W[ 2].x);
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[38]);

	W[ 2].x += Wr1(W[ 1].z) + W[ 0].y + Wr2(W[ 2].y);
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[39]);

	W[ 2].y += Wr1(W[ 1].w) + W[ 0].z + Wr2(W[ 2].z);
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[40]);

	W[ 2].z += Wr1(W[ 2].x) + W[ 0].w + Wr2(W[ 2].w);
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[41]);

	W[ 2].w += Wr1(W[ 2].y) + W[ 1].x + Wr2(W[ 3].x);
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[42]);

	W[ 3].x += Wr1(W[ 2].z) + W[ 1].y + Wr2(W[ 3].y);
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[43]);

	W[ 3].y += Wr1(W[ 2].w) + W[ 1].z + Wr2(W[ 3].z);
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[44]);

	W[ 3].z += Wr1(W[ 3].x) + W[ 1].w + Wr2(W[ 3].w);
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[45]);

	W[ 3].w += Wr1(W[ 3].y) + W[ 2].x + Wr2(W[ 0].x);
	RND(B,C,D,E,F,G,H,A, W[3].w+ K[46]);

	W[ 0].x += Wr1(W[ 3].z) + W[ 2].y + Wr2(W[ 0].y);
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[47]);

	W[ 0].y += Wr1(W[ 3].w) + W[ 2].z + Wr2(W[ 0].z);
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[48]);

	W[ 0].z += Wr1(W[ 0].x) + W[ 2].w + Wr2(W[ 0].w);
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[49]);

	W[ 0].w += Wr1(W[ 0].y) + W[ 3].x + Wr2(W[ 1].x);
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[50]);

	W[ 1].x += Wr1(W[ 0].z) + W[ 3].y + Wr2(W[ 1].y);
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[51]);

	W[ 1].y += Wr1(W[ 0].w) + W[ 3].z + Wr2(W[ 1].z);
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[52]);

	W[ 1].z += Wr1(W[ 1].x) + W[ 3].w + Wr2(W[ 1].w);
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[53]);

	W[ 1].w += Wr1(W[ 1].y) + W[ 0].x + Wr2(W[ 2].x);
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[54]);

	W[ 2].x += Wr1(W[ 1].z) + W[ 0].y + Wr2(W[ 2].y);
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[55]);

	W[ 2].y += Wr1(W[ 1].w) + W[ 0].z + Wr2(W[ 2].z);
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[56]);

	W[ 2].z += Wr1(W[ 2].x) + W[ 0].w + Wr2(W[ 2].w);
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[57]);

	W[ 2].w += Wr1(W[ 2].y) + W[ 1].x + Wr2(W[ 3].x);
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[58]);

	W[ 3].x += Wr1(W[ 2].z) + W[ 1].y + Wr2(W[ 3].y);
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[59]);

	W[ 3].y += Wr1(W[ 2].w) + W[ 1].z + Wr2(W[ 3].z);
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[60]);

	W[ 3].z += Wr1(W[ 3].x) + W[ 1].w + Wr2(W[ 3].w);
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[61]);

	W[ 3].w += Wr1(W[ 3].y) + W[ 2].x + Wr2(W[ 0].x);
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

	uint4 W[4];

	W[0].x = block0.x;
	D= K[63] +W[0].x;
	H= K[64] +W[0].x;

	W[0].y = block0.y;
	C= K[65] +Tr1(D)+Ch(D, K[66], K[67])+W[0].y;
	G= K[68] +C+Tr2(H)+Ch(H, K[69] ,K[70]);

	W[0].z = block0.z;
	B= K[71] +Tr1(C)+Ch(C,D,K[66])+W[0].z;
	F= K[72] +B+Tr2(G)+Maj(G,H, K[73]);

	W[0].w = block0.w;
	A= K[74] +Tr1(B)+Ch(B,C,D)+W[0].w;
	E= K[75] +A+Tr2(F)+Maj(F,G,H);

	W[1].x = block1.x;
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[4]);
	W[1].y = block1.y;
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[5]);
	W[1].z = block1.z;
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[6]);
	W[1].w = block1.w;
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[7]);
	
	W[2].x = block2.x;
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[8]);
	W[2].y = block2.y;
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[9]);
	W[2].z = block2.z;
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[10]);
	W[2].w = block2.w;
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[11]);
	
	W[3].x = block3.x;
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[12]);
	W[3].y = block3.y;
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[13]);
	W[3].z = block3.z;
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[14]);
	W[3].w = block3.w;
	RND(B,C,D,E,F,G,H,A, W[3].w+ K[76]);

	W[0].x += Wr1(W[3].z) + W[2].y + Wr2(W[0].y);
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[15]);

	W[0].y += Wr1(W[3].w) + W[2].z + Wr2(W[0].z);
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[16]);

	W[0].z += Wr1(W[0].x) + W[2].w + Wr2(W[0].w);
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[17]);

	W[0].w += Wr1(W[0].y) + W[3].x + Wr2(W[1].x);
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[18]);

	W[1].x += Wr1(W[0].z) + W[3].y + Wr2(W[1].y);
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[19]);

	W[1].y += Wr1(W[0].w) + W[3].z + Wr2(W[1].z);
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[20]);

	W[1].z += Wr1(W[1].x) + W[3].w + Wr2(W[1].w);
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[21]);

	W[1].w += Wr1(W[1].y) + W[0].x + Wr2(W[2].x);
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[22]);

	W[2].x += Wr1(W[1].z) + W[0].y + Wr2(W[2].y);
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[23]);

	W[2].y += Wr1(W[1].w) + W[0].z + Wr2(W[2].z);
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[24]);

	W[2].z += Wr1(W[2].x) + W[0].w + Wr2(W[2].w);
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[25]);

	W[2].w += Wr1(W[2].y) + W[1].x + Wr2(W[3].x);
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[26]);

	W[3].x += Wr1(W[2].z) + W[1].y + Wr2(W[3].y);
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[27]);

	W[3].y += Wr1(W[2].w) + W[1].z + Wr2(W[3].z);
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[28]);

	W[3].z += Wr1(W[3].x) + W[1].w + Wr2(W[3].w);
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[29]);

	W[3].w += Wr1(W[3].y) + W[2].x + Wr2(W[0].x);
	RND(B,C,D,E,F,G,H,A, W[3].w+ K[30]);

	W[0].x += Wr1(W[3].z) + W[2].y + Wr2(W[0].y);
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[31]);

	W[0].y += Wr1(W[3].w) + W[2].z + Wr2(W[0].z);
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[32]);

	W[0].z += Wr1(W[0].x) + W[2].w + Wr2(W[0].w);
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[33]);

	W[0].w += Wr1(W[0].y) + W[3].x + Wr2(W[1].x);
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[34]);

	W[1].x += Wr1(W[0].z) + W[3].y + Wr2(W[1].y);
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[35]);

	W[1].y += Wr1(W[0].w) + W[3].z + Wr2(W[1].z);
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[36]);

	W[1].z += Wr1(W[1].x) + W[3].w + Wr2(W[1].w);
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[37]);

	W[1].w += Wr1(W[1].y) + W[0].x + Wr2(W[2].x);
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[38]);

	W[2].x += Wr1(W[1].z) + W[0].y + Wr2(W[2].y);
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[39]);

	W[2].y += Wr1(W[1].w) + W[0].z + Wr2(W[2].z);
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[40]);

	W[2].z += Wr1(W[2].x) + W[0].w + Wr2(W[2].w);
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[41]);

	W[2].w += Wr1(W[2].y) + W[1].x + Wr2(W[3].x);
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[42]);

	W[3].x += Wr1(W[2].z) + W[1].y + Wr2(W[3].y);
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[43]);

	W[3].y += Wr1(W[2].w) + W[1].z + Wr2(W[3].z);
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[44]);

	W[3].z += Wr1(W[3].x) + W[1].w + Wr2(W[3].w);
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[45]);

	W[3].w += Wr1(W[3].y) + W[2].x + Wr2(W[0].x);
	RND(B,C,D,E,F,G,H,A, W[3].w+ K[46]);

	W[0].x += Wr1(W[3].z) + W[2].y + Wr2(W[0].y);
	RND(A,B,C,D,E,F,G,H, W[0].x+ K[47]);

	W[0].y += Wr1(W[3].w) + W[2].z + Wr2(W[0].z);
	RND(H,A,B,C,D,E,F,G, W[0].y+ K[48]);

	W[0].z += Wr1(W[0].x) + W[2].w + Wr2(W[0].w);
	RND(G,H,A,B,C,D,E,F, W[0].z+ K[49]);

	W[0].w += Wr1(W[0].y) + W[3].x + Wr2(W[1].x);
	RND(F,G,H,A,B,C,D,E, W[0].w+ K[50]);

	W[1].x += Wr1(W[0].z) + W[3].y + Wr2(W[1].y);
	RND(E,F,G,H,A,B,C,D, W[1].x+ K[51]);

	W[1].y += Wr1(W[0].w) + W[3].z + Wr2(W[1].z);
	RND(D,E,F,G,H,A,B,C, W[1].y+ K[52]);

	W[1].z += Wr1(W[1].x) + W[3].w + Wr2(W[1].w);
	RND(C,D,E,F,G,H,A,B, W[1].z+ K[53]);

	W[1].w += Wr1(W[1].y) + W[0].x + Wr2(W[2].x);
	RND(B,C,D,E,F,G,H,A, W[1].w+ K[54]);

	W[2].x += Wr1(W[1].z) + W[0].y + Wr2(W[2].y);
	RND(A,B,C,D,E,F,G,H, W[2].x+ K[55]);

	W[2].y += Wr1(W[1].w) + W[0].z + Wr2(W[2].z);
	RND(H,A,B,C,D,E,F,G, W[2].y+ K[56]);

	W[2].z += Wr1(W[2].x) + W[0].w + Wr2(W[2].w);
	RND(G,H,A,B,C,D,E,F, W[2].z+ K[57]);

	W[2].w += Wr1(W[2].y) + W[1].x + Wr2(W[3].x);
	RND(F,G,H,A,B,C,D,E, W[2].w+ K[58]);

	W[3].x += Wr1(W[2].z) + W[1].y + Wr2(W[3].y);
	RND(E,F,G,H,A,B,C,D, W[3].x+ K[59]);

	W[3].y += Wr1(W[2].w) + W[1].z + Wr2(W[3].z);
	RND(D,E,F,G,H,A,B,C, W[3].y+ K[60]);

	W[3].z += Wr1(W[3].x) + W[1].w + Wr2(W[3].w);
	RND(C,D,E,F,G,H,A,B, W[3].z+ K[61]);

	W[3].w += Wr1(W[3].y) + W[2].x + Wr2(W[0].x);
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
	uint4 tmp[4];
	tmp[0] = (uint4)(B[1].x,B[2].y,B[3].z,B[0].w);
	tmp[1] = (uint4)(B[2].x,B[3].y,B[0].z,B[1].w);
	tmp[2] = (uint4)(B[3].x,B[0].y,B[1].z,B[2].w);
	tmp[3] = (uint4)(B[0].x,B[1].y,B[2].z,B[3].w);
	
#pragma unroll
	for(uint i=0; i<4; ++i)
		B[i] = EndianSwap(tmp[i]);

	tmp[0] = (uint4)(B[5].x,B[6].y,B[7].z,B[4].w);
	tmp[1] = (uint4)(B[6].x,B[7].y,B[4].z,B[5].w);
	tmp[2] = (uint4)(B[7].x,B[4].y,B[5].z,B[6].w);
	tmp[3] = (uint4)(B[4].x,B[5].y,B[6].z,B[7].w);
	
#pragma unroll
	for(uint i=0; i<4; ++i)
		B[i+4] = EndianSwap(tmp[i]);
}

void unshittify(uint4 B[8])
{
	uint4 tmp[4];
	tmp[0] = (uint4)(B[3].x,B[2].y,B[1].z,B[0].w);
	tmp[1] = (uint4)(B[0].x,B[3].y,B[2].z,B[1].w);
	tmp[2] = (uint4)(B[1].x,B[0].y,B[3].z,B[2].w);
	tmp[3] = (uint4)(B[2].x,B[1].y,B[0].z,B[3].w);
	
#pragma unroll
	for(uint i=0; i<4; ++i)
		B[i] = EndianSwap(tmp[i]);

	tmp[0] = (uint4)(B[7].x,B[6].y,B[5].z,B[4].w);
	tmp[1] = (uint4)(B[4].x,B[7].y,B[6].z,B[5].w);
	tmp[2] = (uint4)(B[5].x,B[4].y,B[7].z,B[6].w);
	tmp[3] = (uint4)(B[6].x,B[5].y,B[4].z,B[7].w);
	
#pragma unroll
	for(uint i=0; i<4; ++i)
		B[i+4] = EndianSwap(tmp[i]);
}

void salsa(uint4 B[8])
{
	uint4 w[4];

#pragma unroll
	for(uint i=0; i<4; ++i)
		w[i] = (B[i]^=B[i+4]);

#pragma unroll
	for(uint i=0; i<4; ++i)
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

#pragma unroll
	for(uint i=0; i<4; ++i)
		w[i] = (B[i+4]^=(B[i]+=w[i]));

#pragma unroll
	for(uint i=0; i<4; ++i)
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

#pragma unroll
	for(uint i=0; i<4; ++i)
		B[i+4] += w[i];
}

#define Coord(x,y,z) x+y*(x ## SIZE)+z*(y ## SIZE)*(x ## SIZE)
#define CO Coord(z,x,y)

void scrypt_core(const uint gid, uint4 X[8], __global uint4 * restrict lookup)
{
	shittify(X);
	const uint zSIZE = 8;
	const uint ySIZE = (1024/LOOKUP_GAP+(1024%LOOKUP_GAP>0));
	const uint xSIZE = CONCURRENT_THREADS;
	const uint x = gid % xSIZE;

	for(uint y=0; y<1024/LOOKUP_GAP; ++y)
	{
#pragma unroll
		for(uint z=0; z<zSIZE; ++z)
			lookup[CO] = X[z];
		for(uint i=0; i<LOOKUP_GAP; ++i) 
			salsa(X);
	}
#if (LOOKUP_GAP != 1) && (LOOKUP_GAP != 2) && (LOOKUP_GAP != 4) && (LOOKUP_GAP != 8)
	{
		uint y = (1024/LOOKUP_GAP);
#pragma unroll
		for(uint z=0; z<zSIZE; ++z)
			lookup[CO] = X[z];
		for(uint i=0; i<1024%LOOKUP_GAP; ++i)
			salsa(X); 
	}
#endif
	for (uint i=0; i<1024; ++i) 
	{
		uint4 V[8];
		uint j = X[7].x & K[85];
		uint y = (j/LOOKUP_GAP);
#pragma unroll
		for(uint z=0; z<zSIZE; ++z)
			V[z] = lookup[CO];

#if (LOOKUP_GAP == 1)
#elif (LOOKUP_GAP == 2)
		if (j&1)
			salsa(V);
#else
		uint val = j%LOOKUP_GAP;
		for (uint z=0; z<val; ++z) 
			salsa(V);
#endif

#pragma unroll
		for(uint z=0; z<zSIZE; ++z)
			X[z] ^= V[z];
		salsa(X);
	}
	unshittify(X);
}

#define SCRYPT_FOUND (0xFF)
#define SETFOUND(Xnonce) output[output[SCRYPT_FOUND]++] = Xnonce

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(
#ifndef GOFFSET
	const uint base,
#endif
	__global const uint4 * restrict input,
volatile __global uint*restrict output, __global uint4*restrict padcache,
const uint4 midstate0, const uint4 midstate16, const uint target)
{
	const uint gid = get_global_id(0)
#ifndef GOFFSET
		+ base
#endif
	;
	uint4 X[8];
	uint4 tstate0, tstate1, ostate0, ostate1, tmp0, tmp1;
	uint4 data = (uint4)(input[4].x,input[4].y,input[4].z,gid);
	uint4 pad0 = midstate0, pad1 = midstate16;

	SHA256(&pad0,&pad1, data, (uint4)(K[84],0,0,0), (uint4)(0,0,0,0), (uint4)(0,0,0, K[86]));
	SHA256_fresh(&ostate0,&ostate1, pad0^ K[82], pad1^ K[82], K[82], K[82]);
	SHA256_fresh(&tstate0,&tstate1, pad0^ K[83], pad1^ K[83], K[83], K[83]);

	tmp0 = tstate0;
	tmp1 = tstate1;
	SHA256(&tstate0, &tstate1, input[0],input[1],input[2],input[3]);

#pragma unroll
	for (uint i=0; i<4; i++) 
	{
		pad0 = tstate0;
		pad1 = tstate1;
		X[i*2 ] = ostate0;
		X[i*2+1] = ostate1;

		SHA256(&pad0,&pad1, data, (uint4)(i+1,K[84],0,0), (uint4)(0,0,0,0), (uint4)(0,0,0, K[87]));
		SHA256(X+i*2,X+i*2+1, pad0, pad1, (uint4)(K[84], 0U, 0U, 0U), (uint4)(0U, 0U, 0U, K[88]));
	}
	scrypt_core(gid, X, padcache);
	SHA256(&tmp0,&tmp1, X[0], X[1], X[2], X[3]);
	SHA256(&tmp0,&tmp1, X[4], X[5], X[6], X[7]);
	SHA256_fixed(&tmp0,&tmp1);
	SHA256(&ostate0,&ostate1, tmp0, tmp1, (uint4)(K[84], 0U, 0U, 0U), (uint4)(0U, 0U, 0U, K[88]));

	bool result = (EndianSwap(ostate1.w) <= target);
	if (result)
		SETFOUND(gid);
}
