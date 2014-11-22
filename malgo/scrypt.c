/*-
 * Copyright 2009 Colin Percival
 * Copyright 2011 ArtForz
 * Copyright 2014 Luke Dashjr
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

#include "config.h"
#include "miner.h"

#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <uthash.h>

typedef struct SHA256Context {
	uint32_t state[8];
	uint32_t buf[16];
} SHA256_CTX;

/*
 * Encode a length len/4 vector of (uint32_t) into a length len vector of
 * (unsigned char) in big-endian form.  Assumes len is a multiple of 4.
 */
static inline void
be32enc_vect(uint32_t *dst, const uint32_t *src, uint32_t len)
{
	uint32_t i;

	for (i = 0; i < len; i++)
		dst[i] = htobe32(src[i]);
}

/* Elementary functions used by SHA256 */
#define Ch(x, y, z)	((x & (y ^ z)) ^ z)
#define Maj(x, y, z)	((x & (y | z)) | (y & z))
#define SHR(x, n)	(x >> n)
#define ROTR(x, n)	((x >> n) | (x << (32 - n)))
#define S0(x)		(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x)		(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x)		(ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define s1(x)		(ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

/* SHA256 round function */
#define RND(a, b, c, d, e, f, g, h, k)			\
	t0 = h + S1(e) + Ch(e, f, g) + k;		\
	t1 = S0(a) + Maj(a, b, c);			\
	d += t0;					\
	h  = t0 + t1;

/* Adjusted round function for rotating state */
#define RNDr(S, W, i, k)			\
	RND(S[(64 - i) % 8], S[(65 - i) % 8],	\
	    S[(66 - i) % 8], S[(67 - i) % 8],	\
	    S[(68 - i) % 8], S[(69 - i) % 8],	\
	    S[(70 - i) % 8], S[(71 - i) % 8],	\
	    W[i] + k)

/*
 * SHA256 block compression function.  The 256-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
static void
SHA256_Transform(uint32_t * state, const uint32_t block[16], int swap)
{
	uint32_t W[64];
	uint32_t S[8];
	uint32_t t0, t1;
	int i;

	/* 1. Prepare message schedule W. */
	if(swap)
		for (i = 0; i < 16; i++)
			W[i] = swab32(block[i]);
	else
		memcpy(W, block, 64);
	for (i = 16; i < 64; i += 2) {
		W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];
		W[i+1] = s1(W[i - 1]) + W[i - 6] + s0(W[i - 14]) + W[i - 15];
	}

	/* 2. Initialize working variables. */
	memcpy(S, state, 32);

	/* 3. Mix. */
	RNDr(S, W, 0, 0x428a2f98);
	RNDr(S, W, 1, 0x71374491);
	RNDr(S, W, 2, 0xb5c0fbcf);
	RNDr(S, W, 3, 0xe9b5dba5);
	RNDr(S, W, 4, 0x3956c25b);
	RNDr(S, W, 5, 0x59f111f1);
	RNDr(S, W, 6, 0x923f82a4);
	RNDr(S, W, 7, 0xab1c5ed5);
	RNDr(S, W, 8, 0xd807aa98);
	RNDr(S, W, 9, 0x12835b01);
	RNDr(S, W, 10, 0x243185be);
	RNDr(S, W, 11, 0x550c7dc3);
	RNDr(S, W, 12, 0x72be5d74);
	RNDr(S, W, 13, 0x80deb1fe);
	RNDr(S, W, 14, 0x9bdc06a7);
	RNDr(S, W, 15, 0xc19bf174);
	RNDr(S, W, 16, 0xe49b69c1);
	RNDr(S, W, 17, 0xefbe4786);
	RNDr(S, W, 18, 0x0fc19dc6);
	RNDr(S, W, 19, 0x240ca1cc);
	RNDr(S, W, 20, 0x2de92c6f);
	RNDr(S, W, 21, 0x4a7484aa);
	RNDr(S, W, 22, 0x5cb0a9dc);
	RNDr(S, W, 23, 0x76f988da);
	RNDr(S, W, 24, 0x983e5152);
	RNDr(S, W, 25, 0xa831c66d);
	RNDr(S, W, 26, 0xb00327c8);
	RNDr(S, W, 27, 0xbf597fc7);
	RNDr(S, W, 28, 0xc6e00bf3);
	RNDr(S, W, 29, 0xd5a79147);
	RNDr(S, W, 30, 0x06ca6351);
	RNDr(S, W, 31, 0x14292967);
	RNDr(S, W, 32, 0x27b70a85);
	RNDr(S, W, 33, 0x2e1b2138);
	RNDr(S, W, 34, 0x4d2c6dfc);
	RNDr(S, W, 35, 0x53380d13);
	RNDr(S, W, 36, 0x650a7354);
	RNDr(S, W, 37, 0x766a0abb);
	RNDr(S, W, 38, 0x81c2c92e);
	RNDr(S, W, 39, 0x92722c85);
	RNDr(S, W, 40, 0xa2bfe8a1);
	RNDr(S, W, 41, 0xa81a664b);
	RNDr(S, W, 42, 0xc24b8b70);
	RNDr(S, W, 43, 0xc76c51a3);
	RNDr(S, W, 44, 0xd192e819);
	RNDr(S, W, 45, 0xd6990624);
	RNDr(S, W, 46, 0xf40e3585);
	RNDr(S, W, 47, 0x106aa070);
	RNDr(S, W, 48, 0x19a4c116);
	RNDr(S, W, 49, 0x1e376c08);
	RNDr(S, W, 50, 0x2748774c);
	RNDr(S, W, 51, 0x34b0bcb5);
	RNDr(S, W, 52, 0x391c0cb3);
	RNDr(S, W, 53, 0x4ed8aa4a);
	RNDr(S, W, 54, 0x5b9cca4f);
	RNDr(S, W, 55, 0x682e6ff3);
	RNDr(S, W, 56, 0x748f82ee);
	RNDr(S, W, 57, 0x78a5636f);
	RNDr(S, W, 58, 0x84c87814);
	RNDr(S, W, 59, 0x8cc70208);
	RNDr(S, W, 60, 0x90befffa);
	RNDr(S, W, 61, 0xa4506ceb);
	RNDr(S, W, 62, 0xbef9a3f7);
	RNDr(S, W, 63, 0xc67178f2);

	/* 4. Mix local working variables into global state */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}

static inline void
SHA256_InitState(uint32_t * state)
{
	/* Magic initialization constants */
	state[0] = 0x6A09E667;
	state[1] = 0xBB67AE85;
	state[2] = 0x3C6EF372;
	state[3] = 0xA54FF53A;
	state[4] = 0x510E527F;
	state[5] = 0x9B05688C;
	state[6] = 0x1F83D9AB;
	state[7] = 0x5BE0CD19;
}

static const uint32_t passwdpad[12] = {0x00000080, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80020000};
static const uint32_t outerpad[8] = {0x80000000, 0, 0, 0, 0, 0, 0, 0x00000300};

/**
 * PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, c, buf, dkLen):
 * Compute PBKDF2(passwd, salt, c, dkLen) using HMAC-SHA256 as the PRF, and
 * write the output to buf.  The value dkLen must be at most 32 * (2^32 - 1).
 */
static inline void
PBKDF2_SHA256_80_128(const uint32_t * passwd, uint32_t * buf)
{
	SHA256_CTX PShictx, PShoctx;
	uint32_t tstate[8];
	uint32_t ihash[8];
	uint32_t i;
	uint32_t pad[16];
	
	static const uint32_t innerpad[11] = {0x00000080, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa0040000};

	/* If Klen > 64, the key is really SHA256(K). */
	SHA256_InitState(tstate);
	SHA256_Transform(tstate, passwd, 1);
	memcpy(pad, passwd+16, 16);
	memcpy(pad+4, passwdpad, 48);
	SHA256_Transform(tstate, pad, 1);
	memcpy(ihash, tstate, 32);

	SHA256_InitState(PShictx.state);
	for (i = 0; i < 8; i++)
		pad[i] = ihash[i] ^ 0x36363636;
	for (; i < 16; i++)
		pad[i] = 0x36363636;
	SHA256_Transform(PShictx.state, pad, 0);
	SHA256_Transform(PShictx.state, passwd, 1);
	be32enc_vect(PShictx.buf, passwd+16, 4);
	be32enc_vect(PShictx.buf+5, innerpad, 11);

	SHA256_InitState(PShoctx.state);
	for (i = 0; i < 8; i++)
		pad[i] = ihash[i] ^ 0x5c5c5c5c;
	for (; i < 16; i++)
		pad[i] = 0x5c5c5c5c;
	SHA256_Transform(PShoctx.state, pad, 0);
	memcpy(PShoctx.buf+8, outerpad, 32);

	/* Iterate through the blocks. */
	swap32tole(PShictx.buf, PShictx.buf, 0x10);
	for (i = 0; i < 4; i++) {
		uint32_t istate[8];
		uint32_t ostate[8];
		
		memcpy(istate, PShictx.state, 32);
		PShictx.buf[4] = i + 1;
		SHA256_Transform(istate, PShictx.buf, 0);
		memcpy(PShoctx.buf, istate, 32);

		memcpy(ostate, PShoctx.state, 32);
		SHA256_Transform(ostate, PShoctx.buf, 0);
		swap32yes(buf+i*8, ostate, 8);
	}
}


static inline void
PBKDF2_SHA256_80_128_32(const uint32_t * passwd, const uint32_t * salt, uint32_t *ostate)
{
	uint32_t tstate[8];
	uint32_t ihash[8];
	uint32_t i;

	/* Compute HMAC state after processing P and S. */
	uint32_t pad[16];
	
	static const uint32_t ihash_finalblk[16] = {0x00000001,0x80000000,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0x00000620};

	/* If Klen > 64, the key is really SHA256(K). */
	SHA256_InitState(tstate);
	SHA256_Transform(tstate, passwd, 1);
	memcpy(pad, passwd+16, 16);
	memcpy(pad+4, passwdpad, 48);
	SHA256_Transform(tstate, pad, 1);
	memcpy(ihash, tstate, 32);

	SHA256_InitState(ostate);
	for (i = 0; i < 8; i++)
		pad[i] = ihash[i] ^ 0x5c5c5c5c;
	for (; i < 16; i++)
		pad[i] = 0x5c5c5c5c;
	SHA256_Transform(ostate, pad, 0);

	SHA256_InitState(tstate);
	for (i = 0; i < 8; i++)
		pad[i] = ihash[i] ^ 0x36363636;
	for (; i < 16; i++)
		pad[i] = 0x36363636;
	SHA256_Transform(tstate, pad, 0);
	SHA256_Transform(tstate, salt, 1);
	SHA256_Transform(tstate, salt+16, 1);
	SHA256_Transform(tstate, ihash_finalblk, 0);
	memcpy(pad, tstate, 32);
	memcpy(pad+8, outerpad, 32);

	/* Feed the inner hash to the outer SHA256 operation. */
	SHA256_Transform(ostate, pad, 0);
}


/**
 * salsa20_8(B):
 * Apply the salsa20/8 core to the provided block.
 */
static inline void
salsa20_8(uint32_t B[16], const uint32_t Bx[16])
{
	uint32_t x00,x01,x02,x03,x04,x05,x06,x07,x08,x09,x10,x11,x12,x13,x14,x15;
	size_t i;

	x00 = (B[ 0] ^= Bx[ 0]);
	x01 = (B[ 1] ^= Bx[ 1]);
	x02 = (B[ 2] ^= Bx[ 2]);
	x03 = (B[ 3] ^= Bx[ 3]);
	x04 = (B[ 4] ^= Bx[ 4]);
	x05 = (B[ 5] ^= Bx[ 5]);
	x06 = (B[ 6] ^= Bx[ 6]);
	x07 = (B[ 7] ^= Bx[ 7]);
	x08 = (B[ 8] ^= Bx[ 8]);
	x09 = (B[ 9] ^= Bx[ 9]);
	x10 = (B[10] ^= Bx[10]);
	x11 = (B[11] ^= Bx[11]);
	x12 = (B[12] ^= Bx[12]);
	x13 = (B[13] ^= Bx[13]);
	x14 = (B[14] ^= Bx[14]);
	x15 = (B[15] ^= Bx[15]);
	for (i = 0; i < 8; i += 2) {
#define R(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
		/* Operate on columns. */
		x04 ^= R(x00+x12, 7);	x09 ^= R(x05+x01, 7);	x14 ^= R(x10+x06, 7);	x03 ^= R(x15+x11, 7);
		x08 ^= R(x04+x00, 9);	x13 ^= R(x09+x05, 9);	x02 ^= R(x14+x10, 9);	x07 ^= R(x03+x15, 9);
		x12 ^= R(x08+x04,13);	x01 ^= R(x13+x09,13);	x06 ^= R(x02+x14,13);	x11 ^= R(x07+x03,13);
		x00 ^= R(x12+x08,18);	x05 ^= R(x01+x13,18);	x10 ^= R(x06+x02,18);	x15 ^= R(x11+x07,18);

		/* Operate on rows. */
		x01 ^= R(x00+x03, 7);	x06 ^= R(x05+x04, 7);	x11 ^= R(x10+x09, 7);	x12 ^= R(x15+x14, 7);
		x02 ^= R(x01+x00, 9);	x07 ^= R(x06+x05, 9);	x08 ^= R(x11+x10, 9);	x13 ^= R(x12+x15, 9);
		x03 ^= R(x02+x01,13);	x04 ^= R(x07+x06,13);	x09 ^= R(x08+x11,13);	x14 ^= R(x13+x12,13);
		x00 ^= R(x03+x02,18);	x05 ^= R(x04+x07,18);	x10 ^= R(x09+x08,18);	x15 ^= R(x14+x13,18);
#undef R
	}
	B[ 0] += x00;
	B[ 1] += x01;
	B[ 2] += x02;
	B[ 3] += x03;
	B[ 4] += x04;
	B[ 5] += x05;
	B[ 6] += x06;
	B[ 7] += x07;
	B[ 8] += x08;
	B[ 9] += x09;
	B[10] += x10;
	B[11] += x11;
	B[12] += x12;
	B[13] += x13;
	B[14] += x14;
	B[15] += x15;
}

/* cpu and memory intensive function to transform a 80 byte buffer into a 32 byte output
   scratchpad size needs to be at least 63 + (128 * r * p) + (256 * r + 64) + (128 * r * N) bytes
 */
static void scrypt_1024_1_1_256_sp(const uint32_t* input, char* scratchpad, uint32_t *ostate)
{
	uint32_t * V;
	uint32_t X[32];
	uint32_t i;
	uint32_t j;
	uint32_t k;
	uint64_t *p1, *p2;

	p1 = (uint64_t *)X;
	V = (uint32_t *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

	PBKDF2_SHA256_80_128(input, X);

	for (i = 0; i < 1024; i += 2) {
		memcpy(&V[i * 32], X, 128);

		salsa20_8(&X[0], &X[16]);
		salsa20_8(&X[16], &X[0]);

		memcpy(&V[(i + 1) * 32], X, 128);

		salsa20_8(&X[0], &X[16]);
		salsa20_8(&X[16], &X[0]);
	}
	for (i = 0; i < 1024; i += 2) {
		j = X[16] & 1023;
		p2 = (uint64_t *)(&V[j * 32]);
		for(k = 0; k < 16; k++)
			p1[k] ^= p2[k];

		salsa20_8(&X[0], &X[16]);
		salsa20_8(&X[16], &X[0]);

		j = X[16] & 1023;
		p2 = (uint64_t *)(&V[j * 32]);
		for(k = 0; k < 16; k++)
			p1[k] ^= p2[k];

		salsa20_8(&X[0], &X[16]);
		salsa20_8(&X[16], &X[0]);
	}

	PBKDF2_SHA256_80_128_32(input, X, ostate);
}

/* 131583 rounded up to 4 byte alignment */
#define SCRATCHBUF_SIZE	(131584)

static
void bin2hex32(char * const out_hex, const uint32_t * const data, const size_t n)
{
	uint32_t dataswap[n];
	swap32tobe(dataswap, data, n);
	bin2hex(out_hex, dataswap, n * 4);
}

void test_scrypt(void)
{
	static const uint32_t input[20] = {0};
	uint32_t X[32];
	char hex[257];
	{
		PBKDF2_SHA256_80_128(input, X);
		static const uint32_t expect_X[] = {
			0x0ea9ea2c, 0x458a4459, 0xac2e8931, 0x227bb8f5,
			0xf2b1fe63, 0x65f4ca78, 0xc13ee80a, 0x9dd6a8b9,
			0x37a70962, 0xce24556e, 0x169081af, 0x73a06c4c,
			0x7feffbbe, 0x90188614, 0x499f4152, 0x174f00cf,
			0x5a2f89a9, 0x9f98d171, 0x2ff50782, 0xc8c551b1,
			0xcf4afba2, 0x089745f0, 0x37553b1f, 0xbca60eec,
			0x193ed225, 0x0d4c2da1, 0x4a670674, 0x4420645c,
			0x432ead7e, 0xa70b8496, 0x1d992334, 0x842b14de,
		};
		if (memcmp(expect_X, X, sizeof(expect_X)))
		{
			++unittest_failures;
			bin2hex32(hex, X, 32);
			applog(LOG_ERR, "%s: %s failed (got %s)", __func__, "PBKDF2_SHA256_80_128", hex);
		}
	}
	{
		for (int i = 0; i < 0x10; ++i)
			X[i] = i;
		salsa20_8(X, input);
		static const uint32_t expect_X[] = {
			0x4fdd18f5, 0xe08388b9, 0xc05479a8, 0x7086ab5c,
			0x0888bb83, 0x75102855, 0x58a08522, 0x166cf522,
			0x0f2a4a9d, 0x232514d2, 0x0bc658d7, 0x681b4136,
			0x0586532d, 0xd271b814, 0x2a045976, 0x5d47fa5a,
		};
		if (memcmp(expect_X, X, sizeof(expect_X)))
		{
			++unittest_failures;
			bin2hex32(hex, X, 16);
			applog(LOG_ERR, "%s; %s failed (got %s)", __func__, "salsa20_8", hex);
		}
	}
	{
		char scratchpad[SCRATCHBUF_SIZE];
		scrypt_1024_1_1_256_sp(input, scratchpad, X);
		static const uint32_t expect_X[] = {
			0x161d0876, 0xf3b93b10, 0x48cda1bd, 0xeaa7332e,
			0xe210f713, 0x1b42013c, 0xb43913a6, 0x553a4b69,
		};
		if (memcmp(expect_X, X, sizeof(expect_X)))
		{
			++unittest_failures;
			bin2hex32(hex, X, 8);
			applog(LOG_ERR, "%s: %s failed (got %s)", __func__, "scrypt_1024_1_1_256_sp", hex);
		}
	}
}

void scrypt_regenhash(struct work *work)
{
	uint32_t data[20];
	char *scratchbuf;
	uint32_t *nonce = (uint32_t *)(work->data + 76);
	uint32_t *ohash = (uint32_t *)(work->hash);

	be32enc_vect(data, (const uint32_t *)work->data, 19);
	data[19] = htobe32(*nonce);
	scratchbuf = alloca(SCRATCHBUF_SIZE);
	scrypt_1024_1_1_256_sp(data, scratchbuf, ohash);
	swap32tobe(ohash, ohash, 8);
}

/* Used by test_nonce functions */
void scrypt_hash_data(void * const out_hash, const void * const pdata)
{
	uint32_t data[20], ohash[8];
	char *scratchbuf;

	be32enc_vect(data, pdata, 20);
	scratchbuf = alloca(SCRATCHBUF_SIZE);
	scrypt_1024_1_1_256_sp(data, scratchbuf, ohash);
	swap32tobe(out_hash, ohash, 32/4);
}

bool scanhash_scrypt(struct thr_info * const thr, struct work * const work,
		     uint32_t max_nonce, uint32_t *last_nonce, uint32_t n)
{
	uint8_t * const pdata = work->data;
	const uint8_t * const ptarget = work->target;
	
	uint32_t *nonce = (uint32_t *)(pdata + 76);
	char *scratchbuf;
	uint32_t data[20];
	uint32_t tmp_hash7;
	uint32_t Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	bool ret = false;

	be32enc_vect(data, (const uint32_t *)pdata, 19);

	scratchbuf = malloc(SCRATCHBUF_SIZE);
	if (unlikely(!scratchbuf)) {
		applog(LOG_ERR, "Failed to malloc scratchbuf in scanhash_scrypt");
		return ret;
	}
	
	while(1) {
		uint32_t ostate[8];

		data[19] = n;
		scrypt_1024_1_1_256_sp(data, scratchbuf, ostate);
		tmp_hash7 = swab32(ostate[7]);

		if (unlikely(tmp_hash7 <= Htarg)) {
			*nonce = htobe32(n);
			ret = true;
			break;
		}

		if (unlikely((n >= max_nonce) || thr->work_restart)) {
			break;
		}
		
		++n;
	}

	*last_nonce = n;
	
	free(scratchbuf);
	return ret;
}

#ifdef USE_OPENCL
static
float opencl_oclthreads_to_intensity_scrypt(const unsigned long oclthreads)
{
	return log2(oclthreads);
}

static
unsigned long opencl_intensity_to_oclthreads_scrypt(float intensity)
{
	return pow(2, intensity);
}

static
char *opencl_get_default_kernel_file_scrypt(const struct mining_algorithm * const malgo, struct cgpu_info * const cgpu, struct _clState * const clState)
{
	return strdup("scrypt");
}
#endif

struct mining_algorithm malgo_scrypt = {
	.name = "scrypt",
	.aliases = "scrypt",
	
	.algo = POW_SCRYPT,
	.ui_skip_hash_bytes = 2,
	.reasonable_low_nonce_diff = 1./0x10000,
	
	.hash_data_f = scrypt_hash_data,
	
#ifdef USE_OPENCL
	.opencl_nodefault = true,
	.opencl_oclthreads_to_intensity = opencl_oclthreads_to_intensity_scrypt,
	.opencl_intensity_to_oclthreads = opencl_intensity_to_oclthreads_scrypt,
	.opencl_min_oclthreads =      0x100,  // intensity   8
	.opencl_max_oclthreads = 0x20000000,  // intensity  31
	.opencl_min_nonce_diff = 1./0x10000,
	.opencl_get_default_kernel_file = opencl_get_default_kernel_file_scrypt,
#endif
};

static
__attribute__((constructor))
void init_scrypt(void)
{
	LL_APPEND(mining_algorithms, (&malgo_scrypt));
}
