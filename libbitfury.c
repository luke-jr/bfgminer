/*
 * Copyright 2013 bitfury
 * Copyright 2013 Anatoly Legkodymov
 * Copyright 2013-2014 Luke Dashjr
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "logging.h"
#include "miner.h"
#include "libbitfury.h"
#include "lowl-spi.h"
#include "sha2.h"

#include <time.h>

#define BITFURY_REFRESH_DELAY 100
#define BITFURY_DETECT_TRIES 3000 / BITFURY_REFRESH_DELAY

uint32_t bitfury_decnonce(uint32_t);

/* Configuration registers - control oscillators and such stuff. PROGRAMMED when magic number is matches, UNPROGRAMMED (default) otherwise */
static
void bitfury_config_reg(struct spi_port *port, int cfgreg, int ena)
{
	static const uint8_t enaconf[4] = { 0xc1, 0x6a, 0x59, 0xe3 };
	static const uint8_t disconf[4] = { 0, 0, 0, 0 };
	
	if (ena) spi_emit_data(port, 0x7000+cfgreg*32, enaconf, 4);
	else     spi_emit_data(port, 0x7000+cfgreg*32, disconf, 4);
}

#define FIRST_BASE 61
#define SECOND_BASE 4
static
const int8_t bitfury_counters[16] = { 64, 64,
	SECOND_BASE, SECOND_BASE+4, SECOND_BASE+2, SECOND_BASE+2+16, SECOND_BASE, SECOND_BASE+1,
	(FIRST_BASE)%65,  (FIRST_BASE+1)%65,  (FIRST_BASE+3)%65, (FIRST_BASE+3+16)%65, (FIRST_BASE+4)%65, (FIRST_BASE+4+4)%65, (FIRST_BASE+3+3)%65, (FIRST_BASE+3+1+3)%65};

/* Oscillator setup variants (maybe more), values inside of chip ANDed to not allow by programming errors work it at higher speeds  */
/* WARNING! no chip temperature control limits, etc. It may self-fry and make fried chips with great ease :-) So if trying to overclock */
/* Do not place chip near flammable objects, provide adequate power protection and better wear eye protection ! */
/* Thermal runaway in this case could produce nice flames of chippy fries */

// Thermometer code from left to right - more ones ==> faster clock!

#define rotrFixed(x,y) (((x) >> (y)) | ((x) << (32-(y))))
#define s0(x) (rotrFixed(x,7)^rotrFixed(x,18)^(x>>3))
#define s1(x) (rotrFixed(x,17)^rotrFixed(x,19)^(x>>10))
#define Ch(x,y,z) (z^(x&(y^z)))
#define Maj(x,y,z) (y^((x^y)&(y^z)))
#define S0(x) (rotrFixed(x,2)^rotrFixed(x,13)^rotrFixed(x,22))
#define S1(x) (rotrFixed(x,6)^rotrFixed(x,11)^rotrFixed(x,25))

/* SHA256 CONSTANTS */
static const uint32_t SHA_K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};



static
void libbitfury_ms3_compute(uint32_t *p)
{
	uint32_t cp[8];
	uint32_t a,b,c,d,e,f,g,h, ne, na,  i;

	swap32tole(cp, p, 8);
	a = cp[0]; b = cp[1]; c = cp[2]; d = cp[3]; e = cp[4]; f = cp[5]; g = cp[6]; h = cp[7];

	for (i = 0; i < 3; i++) {
		const uint32_t x = le32toh(p[i+16]);
		ne = x + SHA_K[i] + h + Ch(e,f,g) + S1(e) + d;
		na = x + SHA_K[i] + h + Ch(e,f,g) + S1(e) + S0(a) + Maj(a,b,c);
		d = c; c = b; b = a; a = na;
		h = g; g = f; f = e; e = ne;
	}

	p[15] = a; p[14] = b; p[13] = c; p[12] = d; p[11] = e; p[10] = f; p[9] = g; p[8] = h;
	swap32tole(&p[8], &p[8], 8);
}

static
void bitfury_send_conf(struct spi_port *port) {
	int i;
	for (i = 7; i <= 11; ++i)
		bitfury_config_reg(port, i, 0);
	bitfury_config_reg(port, 6, 0); /* disable OUTSLK */
	bitfury_config_reg(port, 4, 1); /* Enable slow oscillator */
	for (i = 1; i <= 3; ++i)
		bitfury_config_reg(port, i, 0);
	spi_emit_data(port, 0x0100, bitfury_counters, 16); /* Program counters correctly for rounds processing, here baby should start consuming power */
}

static
void bitfury_send_init(struct spi_port *port) {
	/* Prepare internal buffers */
	/* PREPARE BUFFERS (INITIAL PROGRAMMING) */
	{
		uint32_t w[] = {
			0,0,0,0xffffffff,
			0x80000000,0,0,0,
			0,0,0,0,
			0,0,0,0x00000280,
		};
		swap32tole(w, w, sizeof(w)/4);
		spi_emit_data(port, 0x1000, w, 16*4);
		spi_emit_data(port, 0x1400, w,  8*4);
	}
	{
		uint32_t w[] = {
			0x80000000,0,0,0,
			0,0,0,0x100,
		};
		swap32tole(w, w, sizeof(w)/4);
		spi_emit_data(port, 0x1900, &w[0],8*4); /* Prepare MS and W buffers! */
		uint32_t atrvec[] = {
			0xb0e72d8e, 0x1dc5b862, 0xe9e7c4a6, 0x3050f1f5, 0x8a1a6b7e, 0x7ec384e8, 0x42c1c3fc, 0x8ed158a1, /* MIDSTATE */
			0,0,0,0,0,0,0,0,
			0x8a0bb7b7, 0x33af304f, 0x0b290c1a, 0xf0c4e61f, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
		};
		libbitfury_ms3_compute(&atrvec[0]);
		swap32tole(atrvec, atrvec, sizeof(atrvec)/4);
		spi_emit_data(port, 0x3000, &atrvec[0], 19*4);
	}
}

static
void bitfury_set_freq(struct spi_port *port, int bits) {
	uint64_t freq;
	const uint8_t *
	osc6 = (unsigned char *)&freq;
	freq = (1ULL << bits) - 1ULL;
	freq = htole64(freq);

	spi_emit_data(port, 0x6000, osc6, 8); /* Program internal on-die slow oscillator frequency */
	bitfury_config_reg(port, 4, 1); /* Enable slow oscillator */
}

void bitfury_send_reinit(struct spi_port *port, int slot, int chip_n, int n) {
	spi_clear_buf(port);
	spi_emit_break(port);
	spi_emit_fasync(port, chip_n);
	bitfury_set_freq(port, n);
	bitfury_send_conf(port);
	bitfury_send_init(port);
	spi_txrx(port);
}

void bitfury_send_shutdown(struct spi_port *port, int slot, int chip_n) {
	spi_clear_buf(port);
	spi_emit_break(port);
	spi_emit_fasync(port, chip_n);
	bitfury_config_reg(port, 4, 0); /* Disable slow oscillator */
	spi_txrx(port);
}

void bitfury_send_freq(struct spi_port *port, int slot, int chip_n, int bits) {
	spi_clear_buf(port);
	spi_emit_break(port);
	spi_emit_fasync(port, chip_n);
	bitfury_set_freq(port, bits);
	spi_txrx(port);
}

static
uint32_t libbitfury_c_diff(uint32_t ocounter, uint32_t counter) {
	return counter >  ocounter ? counter - ocounter : (0x003FFFFF - ocounter) + counter;
}

static
uint32_t libbitfury_get_counter(uint32_t *newbuf, uint32_t *oldbuf) {
	int j;
	for(j = 0; j < 16; j++) {
		if (newbuf[j] != oldbuf[j]) {
			uint32_t counter = bitfury_decnonce(newbuf[j]);
			if ((counter & 0xFFC00000) == 0xdf800000) {
				counter -= 0xdf800000;
				return counter;
			}
		}
	}
	return 0;
}

static
int libbitfury_detect_chip(struct spi_port *port, int chip_n) {
	/* Test vectors to calculate (using address-translated loads) */
	uint32_t atrvec[] = {
		0xb0e72d8e, 0x1dc5b862, 0xe9e7c4a6, 0x3050f1f5, 0x8a1a6b7e, 0x7ec384e8, 0x42c1c3fc, 0x8ed158a1, /* MIDSTATE */
		0,0,0,0,0,0,0,0,
		0x8a0bb7b7, 0x33af304f, 0x0b290c1a, 0xf0c4e61f, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
		
		0x9c4dfdc0, 0xf055c9e1, 0xe60f079d, 0xeeada6da, 0xd459883d, 0xd8049a9d, 0xd49f9a96, 0x15972fed, /* MIDSTATE */
		0,0,0,0,0,0,0,0,
		0x048b2528, 0x7acb2d4f, 0x0b290c1a, 0xbe00084a, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
		
		0x0317b3ea, 0x1d227d06, 0x3cca281e, 0xa6d0b9da, 0x1a359fe2, 0xa7287e27, 0x8b79c296, 0xc4d88274, /* MIDSTATE */
		0,0,0,0,0,0,0,0,
		0x328bcd4f, 0x75462d4f, 0x0b290c1a, 0x002c6dbc, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
		
		0xac4e38b6, 0xba0e3b3b, 0x649ad6f8, 0xf72e4c02, 0x93be06fb, 0x366d1126, 0xf4aae554, 0x4ff19c5b, /* MIDSTATE */
		0,0,0,0,0,0,0,0,
		0x72698140, 0x3bd62b4f, 0x3fd40c1a, 0x801e43e9, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
		
		0x9dbf91c9, 0x12e5066c, 0xf4184b87, 0x8060bc4d, 0x18f9c115, 0xf589d551, 0x0f7f18ae, 0x885aca59, /* MIDSTATE */
		0,0,0,0,0,0,0,0,
		0x6f3806c3, 0x41f82a4f, 0x3fd40c1a, 0x00334b39, /* WDATA: hashMerleRoot[7], nTime, nBits, nNonce */
	};
	int i;
	uint32_t newbuf[17] = {0}, oldbuf[17] = {0};
	uint32_t ocounter;
	long long odiff = 0;

	memset(newbuf, 0, 17 * 4);
	memset(oldbuf, 0, 17 * 4);

	libbitfury_ms3_compute(&atrvec[0]);
	libbitfury_ms3_compute(&atrvec[20]);
	libbitfury_ms3_compute(&atrvec[40]);
	
	swap32tole(atrvec, atrvec, sizeof(atrvec)/4);


	spi_clear_buf(port);
	spi_emit_break(port); /* First we want to break chain! Otherwise we'll get all of traffic bounced to output */
	spi_emit_fasync(port, chip_n);
	bitfury_set_freq(port, 30);
	bitfury_send_conf(port);
	bitfury_send_init(port);
	spi_txrx(port);

	ocounter = 0;
	for (i = 0; i < BITFURY_DETECT_TRIES; i++) {
		int counter;

		spi_clear_buf(port);
		spi_emit_break(port);
		spi_emit_fasync(port, chip_n);
		spi_emit_data(port, 0x3000, &atrvec[0], 19*4);
		spi_txrx(port);
		swap32tole(newbuf, spi_getrxbuf(port) + 4 + chip_n, 17);

		counter = libbitfury_get_counter(newbuf, oldbuf);
		if (ocounter) {
			long long cdiff = libbitfury_c_diff(ocounter, counter);

			if (llabs(odiff - cdiff) < 5000)
				return 1;
			odiff = cdiff;
		}
		ocounter = counter;
		if (newbuf[16] != 0 && newbuf[16] != 0xFFFFFFFF) {
			return 0;
		}
		cgsleep_ms(BITFURY_REFRESH_DELAY / 10);
		memcpy(oldbuf, newbuf, 17 * 4);
	}
	return 0;
}

int libbitfury_detectChips1(struct spi_port *port) {
	int n;
	for (n = 0; libbitfury_detect_chip(port, n); ++n)
	{}
	return n;
}

// in  = 1f 1e 1d 1c 1b 1a 19 18 17 16 15 14 13 12 11 10  f  e  d  c  b  a  9  8  7  6  5  4  3  2  1  0
uint32_t bitfury_decnonce(uint32_t in)
{
	uint32_t out;

	/* First part load */
	out = (in & 0xFF) << 24; in >>= 8;

	/* Byte reversal */
	in = (((in & 0xaaaaaaaa) >> 1) | ((in & 0x55555555) << 1));
	in = (((in & 0xcccccccc) >> 2) | ((in & 0x33333333) << 2));
	in = (((in & 0xf0f0f0f0) >> 4) | ((in & 0x0f0f0f0f) << 4));

	out |= (in >> 2)&0x3FFFFF;

	/* Extraction */
	if (in & 1) out |= (1 << 23);
	if (in & 2) out |= (1 << 22);
// out =  7  6  5  4  3  2  1  0  f  e 18 19 1a 1b 1c 1d 1e 1f 10 11 12 13 14 15 16 17  8  9  a  b  c  d

	out -= 0x800004;
	return out;
}

static
int libbitfury_rehash(const void *midstate, const uint32_t m7, const uint32_t ntime, const uint32_t nbits, uint32_t nnonce) {
	uint8_t in[16];
	uint32_t *in32 = (uint32_t *)in;
	const uint32_t *mid32 = midstate;
	uint32_t out32[8];
	uint8_t *out = (uint8_t *) out32;
#ifdef BITFURY_REHASH_DEBUG
	static uint32_t history[512];
	static uint32_t history_p;
#endif
	sha256_ctx ctx;


	memset( &ctx, 0, sizeof( sha256_ctx ) );
	memcpy(ctx.h, mid32, 8*4);
	ctx.tot_len = 64;
	ctx.len = 0;

	in32[0] = bswap_32(m7);
	in32[1] = bswap_32(ntime);
	in32[2] = bswap_32(nbits);
	in32[3] = bswap_32(nnonce);

	sha256_update(&ctx, in, 16);
	sha256_final(&ctx, out);
	sha256(out, 32, out);

	if (out32[7] == 0) {
#ifdef BITFURY_REHASH_DEBUG
		char hex[65];
		bin2hex(hex, out, 32);
		applog(LOG_INFO, "! MS0: %08x, m7: %08x, ntime: %08x, nbits: %08x, nnonce: %08x", mid32[0], m7, ntime, nbits, nnonce);
		applog(LOG_INFO, " out: %s", hex);
		history[history_p] = nnonce;
		history_p++; history_p &= 512 - 1;
#endif
		return 1;
	}
	return 0;
}

bool bitfury_fudge_nonce(const void *midstate, const uint32_t m7, const uint32_t ntime, const uint32_t nbits, uint32_t *nonce_p) {
	static const uint32_t offsets[] = {0, 0xffc00000, 0xff800000, 0x02800000, 0x02C00000, 0x00400000};
	uint32_t nonce;
	int i;
	
	for (i = 0; i < 6; ++i)
	{
		nonce = *nonce_p + offsets[i];
		if (libbitfury_rehash(midstate, m7, ntime, nbits, nonce))
		{
			*nonce_p = nonce;
			return true;
		}
	}
	return false;
}

void work_to_bitfury_payload(struct bitfury_payload *p, struct work *w) {
	memset(p, 0, sizeof(struct bitfury_payload));

	memcpy(p->midstate, w->midstate, 32);
	p->m7 = *(uint32_t *)&w->data[0x40];
	p->ntime = *(uint32_t *)&w->data[0x44];
	p->nbits = *(uint32_t *)&w->data[0x48];
}

void bitfury_payload_to_atrvec(uint32_t *atrvec, struct bitfury_payload *p)
{
	/* Programming next value */
	memcpy(atrvec, p, 20*4);
	libbitfury_ms3_compute(atrvec);
}
