/*
* "pluck" kernel implementation.
*
* ==========================(LICENSE BEGIN)============================
*
* Copyright (c) 2015  djm34
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
* @author   djm34
*/
#if !defined(cl_khr_byte_addressable_store)
#error "Device does not support unaligned stores"
#endif
#define ROL32(x, n)  rotate(x, (uint) n)
//#define ROL32(x, n)   (((x) << (n)) | ((x) >> (32 - (n))))
#define HASH_MEMORY 4096


#define SALSA(a,b,c,d) do { \
    t =a+d; b^=rotate(t,  7U);    \
    t =b+a; c^=rotate(t,  9U);    \
    t =c+b; d^=rotate(t, 13U);    \
    t =d+c; a^=rotate(t, 18U);     \
} while(0)


#define SALSA_CORE(state) do { \
\
SALSA(state.s0,state.s4,state.s8,state.sc); \
SALSA(state.s5,state.s9,state.sd,state.s1); \
SALSA(state.sa,state.se,state.s2,state.s6); \
SALSA(state.sf,state.s3,state.s7,state.sb); \
SALSA(state.s0,state.s1,state.s2,state.s3); \
SALSA(state.s5,state.s6,state.s7,state.s4); \
SALSA(state.sa,state.sb,state.s8,state.s9); \
SALSA(state.sf,state.sc,state.sd,state.se); \
	} while(0)

/*
#define SALSA_CORE(state)	do { \
	state.s4 ^= rotate(state.s0 + state.sc, 7U); state.s8 ^= rotate(state.s4 + state.s0, 9U); state.sc ^= rotate(state.s8 + state.s4, 13U); state.s0 ^= rotate(state.sc + state.s8, 18U); \
	state.s9 ^= rotate(state.s5 + state.s1, 7U); state.sd ^= rotate(state.s9 + state.s5, 9U); state.s1 ^= rotate(state.sd + state.s9, 13U); state.s5 ^= rotate(state.s1 + state.sd, 18U); \
	state.se ^= rotate(state.sa + state.s6, 7U); state.s2 ^= rotate(state.se + state.sa, 9U); state.s6 ^= rotate(state.s2 + state.se, 13U); state.sa ^= rotate(state.s6 + state.s2, 18U); \
	state.s3 ^= rotate(state.sf + state.sb, 7U); state.s7 ^= rotate(state.s3 + state.sf, 9U); state.sb ^= rotate(state.s7 + state.s3, 13U); state.sf ^= rotate(state.sb + state.s7, 18U); \
	state.s1 ^= rotate(state.s0 + state.s3, 7U); state.s2 ^= rotate(state.s1 + state.s0, 9U); state.s3 ^= rotate(state.s2 + state.s1, 13U); state.s0 ^= rotate(state.s3 + state.s2, 18U); \
	state.s6 ^= rotate(state.s5 + state.s4, 7U); state.s7 ^= rotate(state.s6 + state.s5, 9U); state.s4 ^= rotate(state.s7 + state.s6, 13U); state.s5 ^= rotate(state.s4 + state.s7, 18U); \
	state.sb ^= rotate(state.sa + state.s9, 7U); state.s8 ^= rotate(state.sb + state.sa, 9U); state.s9 ^= rotate(state.s8 + state.sb, 13U); state.sa ^= rotate(state.s9 + state.s8, 18U); \
	state.sc ^= rotate(state.sf + state.se, 7U); state.sd ^= rotate(state.sc + state.sf, 9U); state.se ^= rotate(state.sd + state.sc, 13U); state.sf ^= rotate(state.se + state.sd, 18U); \
} while(0)
*/
uint16 xor_salsa8(uint16 Bx)
{
uint t;
	uint16 st = Bx;
	SALSA_CORE(st);
	SALSA_CORE(st);
	SALSA_CORE(st);
	SALSA_CORE(st);
	return(st + Bx);
}



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

inline uint8 sha256_round1(uint16 data)
{
	uint temp1;
    uint8 res;
	uint W0 = SWAP32(data.s0);
	uint W1 = SWAP32(data.s1);
	uint W2 = SWAP32(data.s2);
	uint W3 = SWAP32(data.s3);
	uint W4 = SWAP32(data.s4);
	uint W5 = SWAP32(data.s5);
	uint W6 = SWAP32(data.s6);
	uint W7 = SWAP32(data.s7);
	uint W8 = SWAP32(data.s8);
	uint W9 = SWAP32(data.s9);
	uint W10 = SWAP32(data.sA);
	uint W11 = SWAP32(data.sB);
	uint W12 = SWAP32(data.sC);
	uint W13 = SWAP32(data.sD);
	uint W14 = SWAP32(data.sE);
	uint W15 = SWAP32(data.sF);

	uint v0 = 0x6A09E667;
	uint v1 = 0xBB67AE85;
	uint v2 = 0x3C6EF372;
	uint v3 = 0xA54FF53A;
	uint v4 = 0x510E527F;
	uint v5 = 0x9B05688C;
	uint v6 = 0x1F83D9AB;
	uint v7 = 0x5BE0CD19;

	P(v0, v1, v2, v3, v4, v5, v6, v7, W0, 0x428A2F98);
	P(v7, v0, v1, v2, v3, v4, v5, v6, W1, 0x71374491);
	P(v6, v7, v0, v1, v2, v3, v4, v5, W2, 0xB5C0FBCF);
	P(v5, v6, v7, v0, v1, v2, v3, v4, W3, 0xE9B5DBA5);
	P(v4, v5, v6, v7, v0, v1, v2, v3, W4, 0x3956C25B);
	P(v3, v4, v5, v6, v7, v0, v1, v2, W5, 0x59F111F1);
	P(v2, v3, v4, v5, v6, v7, v0, v1, W6, 0x923F82A4);
	P(v1, v2, v3, v4, v5, v6, v7, v0, W7, 0xAB1C5ED5);
	P(v0, v1, v2, v3, v4, v5, v6, v7, W8, 0xD807AA98);
	P(v7, v0, v1, v2, v3, v4, v5, v6, W9, 0x12835B01);
	P(v6, v7, v0, v1, v2, v3, v4, v5, W10, 0x243185BE);
	P(v5, v6, v7, v0, v1, v2, v3, v4, W11, 0x550C7DC3);
	P(v4, v5, v6, v7, v0, v1, v2, v3, W12, 0x72BE5D74);
	P(v3, v4, v5, v6, v7, v0, v1, v2, W13, 0x80DEB1FE);
	P(v2, v3, v4, v5, v6, v7, v0, v1, W14, 0x9BDC06A7);
	P(v1, v2, v3, v4, v5, v6, v7, v0, W15, 0xC19BF174);

	P(v0, v1, v2, v3, v4, v5, v6, v7, R0, 0xE49B69C1);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R1, 0xEFBE4786);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x0FC19DC6);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x240CA1CC);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x2DE92C6F);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x4A7484AA);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x5CB0A9DC);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x76F988DA);
	P(v0, v1, v2, v3, v4, v5, v6, v7, R8, 0x983E5152);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R9, 0xA831C66D);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R10, 0xB00327C8);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R11, 0xBF597FC7);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R12, 0xC6E00BF3);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xD5A79147);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R14, 0x06CA6351);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R15, 0x14292967);

	P(v0, v1, v2, v3, v4, v5, v6, v7, R0, 0x27B70A85);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R1, 0x2E1B2138);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x4D2C6DFC);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x53380D13);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x650A7354);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x766A0ABB);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x81C2C92E);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x92722C85);
	P(v0, v1, v2, v3, v4, v5, v6, v7, R8, 0xA2BFE8A1);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R9, 0xA81A664B);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R10, 0xC24B8B70);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R11, 0xC76C51A3);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R12, 0xD192E819);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xD6990624);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R14, 0xF40E3585);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R15, 0x106AA070);

	P(v0, v1, v2, v3, v4, v5, v6, v7, R0, 0x19A4C116);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R1, 0x1E376C08);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x2748774C);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x34B0BCB5);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x391C0CB3);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x4ED8AA4A);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x5B9CCA4F);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x682E6FF3);
	P(v0, v1, v2, v3, v4, v5, v6, v7, R8, 0x748F82EE);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R9, 0x78A5636F);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R10, 0x84C87814);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R11, 0x8CC70208);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R12, 0x90BEFFFA);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xA4506CEB);
	P(v2, v3, v4, v5, v6, v7, v0, v1, RD14, 0xBEF9A3F7);
	P(v1, v2, v3, v4, v5, v6, v7, v0, RD15, 0xC67178F2);

	res.s0 = v0 + 0x6A09E667;
	res.s1 = v1 + 0xBB67AE85;
	res.s2 = v2 + 0x3C6EF372;
	res.s3 = v3 + 0xA54FF53A;
	res.s4 = v4 + 0x510E527F;
	res.s5 = v5 + 0x9B05688C;
	res.s6 = v6 + 0x1F83D9AB;
	res.s7 = v7 + 0x5BE0CD19;
	return (res);
}


inline uint8 sha256_round2(uint16 data,uint8 buf)
{
	uint temp1;
	uint8 res;
	uint W0 = data.s0;
	uint W1 = data.s1;
	uint W2 = data.s2;
	uint W3 = data.s3;
	uint W4 = data.s4;
	uint W5 = data.s5;
	uint W6 = data.s6;
	uint W7 = data.s7;
	uint W8 = data.s8;
	uint W9 = data.s9;
	uint W10 = data.sA;
	uint W11 = data.sB;
	uint W12 = data.sC;
	uint W13 = data.sD;
	uint W14 = data.sE;
	uint W15 = data.sF;

	uint v0 = buf.s0;
	uint v1 = buf.s1;
	uint v2 = buf.s2;
	uint v3 = buf.s3;
	uint v4 = buf.s4;
	uint v5 = buf.s5;
	uint v6 = buf.s6;
	uint v7 = buf.s7;

	P(v0, v1, v2, v3, v4, v5, v6, v7, W0, 0x428A2F98);
	P(v7, v0, v1, v2, v3, v4, v5, v6, W1, 0x71374491);
	P(v6, v7, v0, v1, v2, v3, v4, v5, W2, 0xB5C0FBCF);
	P(v5, v6, v7, v0, v1, v2, v3, v4, W3, 0xE9B5DBA5);
	P(v4, v5, v6, v7, v0, v1, v2, v3, W4, 0x3956C25B);
	P(v3, v4, v5, v6, v7, v0, v1, v2, W5, 0x59F111F1);
	P(v2, v3, v4, v5, v6, v7, v0, v1, W6, 0x923F82A4);
	P(v1, v2, v3, v4, v5, v6, v7, v0, W7, 0xAB1C5ED5);
	P(v0, v1, v2, v3, v4, v5, v6, v7, W8, 0xD807AA98);
	P(v7, v0, v1, v2, v3, v4, v5, v6, W9, 0x12835B01);
	P(v6, v7, v0, v1, v2, v3, v4, v5, W10, 0x243185BE);
	P(v5, v6, v7, v0, v1, v2, v3, v4, W11, 0x550C7DC3);
	P(v4, v5, v6, v7, v0, v1, v2, v3, W12, 0x72BE5D74);
	P(v3, v4, v5, v6, v7, v0, v1, v2, W13, 0x80DEB1FE);
	P(v2, v3, v4, v5, v6, v7, v0, v1, W14, 0x9BDC06A7);
	P(v1, v2, v3, v4, v5, v6, v7, v0, W15, 0xC19BF174);

	P(v0, v1, v2, v3, v4, v5, v6, v7, R0, 0xE49B69C1);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R1, 0xEFBE4786);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x0FC19DC6);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x240CA1CC);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x2DE92C6F);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x4A7484AA);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x5CB0A9DC);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x76F988DA);
	P(v0, v1, v2, v3, v4, v5, v6, v7, R8, 0x983E5152);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R9, 0xA831C66D);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R10, 0xB00327C8);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R11, 0xBF597FC7);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R12, 0xC6E00BF3);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xD5A79147);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R14, 0x06CA6351);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R15, 0x14292967);

	P(v0, v1, v2, v3, v4, v5, v6, v7, R0, 0x27B70A85);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R1, 0x2E1B2138);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x4D2C6DFC);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x53380D13);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x650A7354);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x766A0ABB);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x81C2C92E);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x92722C85);
	P(v0, v1, v2, v3, v4, v5, v6, v7, R8, 0xA2BFE8A1);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R9, 0xA81A664B);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R10, 0xC24B8B70);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R11, 0xC76C51A3);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R12, 0xD192E819);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xD6990624);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R14, 0xF40E3585);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R15, 0x106AA070);

	P(v0, v1, v2, v3, v4, v5, v6, v7, R0, 0x19A4C116);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R1, 0x1E376C08);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R2, 0x2748774C);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R3, 0x34B0BCB5);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R4, 0x391C0CB3);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R5, 0x4ED8AA4A);
	P(v2, v3, v4, v5, v6, v7, v0, v1, R6, 0x5B9CCA4F);
	P(v1, v2, v3, v4, v5, v6, v7, v0, R7, 0x682E6FF3);
	P(v0, v1, v2, v3, v4, v5, v6, v7, R8, 0x748F82EE);
	P(v7, v0, v1, v2, v3, v4, v5, v6, R9, 0x78A5636F);
	P(v6, v7, v0, v1, v2, v3, v4, v5, R10, 0x84C87814);
	P(v5, v6, v7, v0, v1, v2, v3, v4, R11, 0x8CC70208);
	P(v4, v5, v6, v7, v0, v1, v2, v3, R12, 0x90BEFFFA);
	P(v3, v4, v5, v6, v7, v0, v1, v2, R13, 0xA4506CEB);
	P(v2, v3, v4, v5, v6, v7, v0, v1, RD14, 0xBEF9A3F7);
	P(v1, v2, v3, v4, v5, v6, v7, v0, RD15, 0xC67178F2);

	res.s0 = SWAP32(v0 + buf.s0);
	res.s1 = SWAP32(v1 + buf.s1);
	res.s2 = SWAP32(v2 + buf.s2);
	res.s3 = SWAP32(v3 + buf.s3);
	res.s4 = SWAP32(v4 + buf.s4);
	res.s5 = SWAP32(v5 + buf.s5);
	res.s6 = SWAP32(v6 + buf.s6);
	res.s7 = SWAP32(v7 + buf.s7);
	return (res);
}

inline uint8 sha256_80(uint* data,uint nonce)
{

uint8 buf = sha256_round1( ((uint16*)data)[0]);
uint in[16];
for (int i = 0; i<3; i++) { in[i] = SWAP32(data[i + 16]); }
in[3] = SWAP32(nonce);
in[4] = 0x80000000;
in[15] = 0x280;
for (int i = 5; i<15; i++) { in[i] = 0; }

return(sha256_round2(((uint16*)in)[0], buf));
}

inline uint8 sha256_64(uint* data)
{
	
uint8 buf=sha256_round1(((uint16*)data)[0]);
uint in[16];
for (int i = 1; i<15; i++) { in[i] = 0; }
in[0] = 0x80000000;
in[15] = 0x200;

	return(sha256_round2(((uint16*)in)[0],buf));
}


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global const uchar* restrict input, __global uint* restrict output, __global uchar *padcache, const uint target)
{

	__global uchar *hashbuffer = (__global uchar *)(padcache + (1024*128 * (get_global_id(0) % MAX_GLOBAL_THREADS)));

    uint  data[20];

	((uint16 *)data)[0] = ((__global const uint16 *)input)[0];
	((uint4 *)data)[4] = ((__global const uint4 *)input)[4];
     
        ((__global uint8*)hashbuffer)[0] = sha256_80(data,get_global_id(0));
		((__global uint8*)hashbuffer)[1] = 0;
     		
	for (int i = 2; i < 4096 - 1; i++)
	{
		uint randmax = i * 32 - 4;
		uint randseed[16];
		uint randbuffer[16];
		uint joint[16];
        
		((uint8*)randseed)[0] = ((__global uint8*)hashbuffer)[i - 2];
		((uint8*)randseed)[1] = ((__global uint8*)hashbuffer)[i - 1];
        
		if (i>4)
		{

			((uint8*)randseed)[0] ^= ((__global uint8*)hashbuffer)[i - 4];
			((uint8*)randseed)[1] ^= ((__global uint8*)hashbuffer)[i - 3];
		}

		((uint16*)randbuffer)[0] = xor_salsa8(((uint16*)randseed)[0]);

		

       ((uint8*)joint)[0] = ((__global uint8*)hashbuffer)[i - 1];
		for (int j = 0; j < 8; j++)
		{
			uint rand = randbuffer[j] % (randmax - 32);

	     ((uchar4*)joint)[(j + 8)].x =((__global uchar*)(hashbuffer))[0+rand];
	     ((uchar4*)joint)[(j + 8)].y =((__global uchar*)(hashbuffer))[1+rand];
	     ((uchar4*)joint)[(j + 8)].z =((__global uchar*)(hashbuffer))[2+rand];
	     ((uchar4*)joint)[(j + 8)].w =((__global uchar*)(hashbuffer))[3+rand];
}
		((__global uint8*)(hashbuffer))[i] = sha256_64(joint);



		(( uint8*)randseed)[0] = ((__global uint8*)(hashbuffer))[i - 1];
		(( uint8*)randseed)[1] = ((__global uint8*)(hashbuffer))[i];


		if (i>4)
		{

			((uint8*)randseed)[0] ^= ((__global uint8*)(hashbuffer))[i - 4];
			((uint8*)randseed)[1] ^= ((__global uint8*)(hashbuffer))[i - 3];

		}

		((uint16*)randbuffer)[0] = xor_salsa8(((uint16*)randseed)[0]);

		for (int j = 0; j < 32; j += 2)
		{
			uint rand = randbuffer[j / 2] % randmax;
            uchar4 Tohere;

			Tohere.x =  ((__global uchar*)(hashbuffer))[randmax + j];
			Tohere.y =  ((__global uchar*)(hashbuffer))[randmax + j + 1];
			Tohere.z =  ((__global uchar*)(hashbuffer))[randmax + j + 2];
			Tohere.w =  ((__global uchar*)(hashbuffer))[randmax + j + 3];
			((__global uchar*)(hashbuffer))[rand] =  Tohere.x;
			((__global uchar*)(hashbuffer))[rand+1] =  Tohere.y;
			((__global uchar*)(hashbuffer))[rand+2] =  Tohere.z;
			((__global uchar*)(hashbuffer))[rand+3] =  Tohere.w;

		}

	} // main loop

	
	if( ((__global uint *)hashbuffer)[7] <= (target)) {output[atomic_inc(output + 0xFF)] = SWAP32(get_global_id(0));
//printf("gpu hashbuffer %08x nonce %08x\n",((__global uint *)hashbuffer)[7] ,SWAP32(get_global_id(0)));
}



/////////////////////////////////////////////////////////////////

}