/*
 * Lyra2RE kernel implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 * Copyright (c) 2014 djm34
 * Copyright (c) 2014 James Lovejoy
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

#pragma OPENCL EXTENSION cl_amd_printf : enable

#ifndef LYRA2RE_CL
#define LYRA2RE_CL

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

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x)    ((x) & SPH_C64(0xFFFFFFFFFFFFFFFF))

//#define SPH_ROTL32(x, n)   (((x) << (n)) | ((x) >> (32 - (n))))
//#define SPH_ROTR32(x, n)   (((x) >> (n)) | ((x) << (32 - (n))))
//#define SPH_ROTL64(x, n)   (((x) << (n)) | ((x) >> (64 - (n))))
//#define SPH_ROTR64(x, n)   (((x) >> (n)) | ((x) << (64 - (n))))

#define SPH_ROTL32(x,n) rotate(x,(uint)n)     //faster with driver 14.6
#define SPH_ROTR32(x,n) rotate(x,(uint)(32-n))
#define SPH_ROTL64(x,n) rotate(x,(ulong)n)
//#define SPH_ROTR64(x,n) rotate(x,(ulong)(64-n))

/*
inline ulong rol64 (ulong l,ulong n) { 
  if (n<=32) {
    uint2 t = rotate(as_uint2(l), (n)); 
    return as_ulong((uint2)(bitselect(t.s0, t.s1, (uint)(1 << (n)) - 1), bitselect(t.s0, t.s1, (uint)(~((1 << (n)) - 1)))));
  } else {
  uint2 t = rotate(as_uint2(l), (n - 32)); 
  return as_ulong((uint2)(bitselect(t.s1, t.s0, (uint)(1 << (n - 32)) - 1), bitselect(t.s1, t.s0, (uint)(~((1 << (n - 32)) - 1))))); 
  }
}
*/

/*
static inline ulong rol64(const ulong vw, unsigned n) {
	uint2 result;
    uint2 v=as_uint2(vw);
    if (n == 32) { return as_ulong((uint2)(v.y, v.x)); }
	if (n < 32) {
		result.y = ( (v.y << (n)) | (v.x >> (32 - n)) );
		result.x = ( (v.x << (n)) | (v.y >> (32 - n)) );
	}
	else {
		result.y = ( (v.x << (n - 32)) | (v.y >> (64 - n)) );
		result.x = ( (v.y << (n - 32)) | (v.x >> (64 - n)) );
	}
	return as_ulong(result);
}
*/

static inline sph_u64 ror64(sph_u64 vw, unsigned a) {
	uint2 result;
	uint2 v = as_uint2(vw);
	unsigned n = (unsigned)(64 - a);
	if (n == 32) { return as_ulong((uint2)(v.y, v.x)); }
	if (n < 32) {
		result.y = ((v.y << (n)) | (v.x >> (32 - n)));
		result.x = ((v.x << (n)) | (v.y >> (32 - n)));
	}	else {
		result.y = ((v.x << (n - 32)) | (v.y >> (64 - n)));
		result.x = ((v.y << (n - 32)) | (v.x >> (64 - n)));
	}
	return as_ulong(result);
}

#define SPH_ROTR64(l,n) ror64(l, n)

#include "blake256.cl"
#include "groestl256.cl"
#include "lyra2.cl"
#include "keccak1600.cl"
#include "skein256.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
  #define DEC64LE(x) SWAP8(*(const __global sph_u64 *) (x));
  #define DEC32LE(x) (*(const __global sph_u32 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
  #define DEC64LE(x) (*(const __global sph_u64 *) (x));
  #define DEC32LE(x) SWAP4(*(const __global sph_u32 *) (x));
#endif

typedef union {
  unsigned char h1[64];
  uint h4[16];
  ulong h8[8];
} hash_t;

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(
	 __global hash_t* hashes,
	// precalc hash from fisrt part of message
	const uint h0,
	const uint h1,
	const uint h2,
	const uint h3,
	const uint h4,
	const uint h5,
	const uint h6,
	const uint h7,
	// last 12 bytes of original message
	const uint in16,
	const uint in17,
	const uint in18
)

{
 uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

    sph_u32 h[8];
    sph_u32 m[16];
    sph_u32 v[16];

h[0]=h0;
h[1]=h1;
h[2]=h2;
h[3]=h3;
h[4]=h4;
h[5]=h5;
h[6]=h6;
h[7]=h7;
// compress 2nd round
 m[0] = in16;
 m[1] = in17;
 m[2] = in18;
 m[3] = SWAP4(gid);

	for (int i = 4; i < 16; i++) {m[i] = c_Padding[i];}

	for (int i = 0; i < 8; i++) {v[i] = h[i];}

	v[8] =  c_u256[0];
	v[9] =  c_u256[1];
	v[10] = c_u256[2];
	v[11] = c_u256[3];
	v[12] = c_u256[4] ^ 640;
	v[13] = c_u256[5] ^ 640;
	v[14] = c_u256[6];
	v[15] = c_u256[7];

	for (int r = 0; r < 14; r++) {
		GS(0, 4, 0x8, 0xC, 0x0);
		GS(1, 5, 0x9, 0xD, 0x2);
		GS(2, 6, 0xA, 0xE, 0x4);
		GS(3, 7, 0xB, 0xF, 0x6);
		GS(0, 5, 0xA, 0xF, 0x8);
		GS(1, 6, 0xB, 0xC, 0xA);
		GS(2, 7, 0x8, 0xD, 0xC);
		GS(3, 4, 0x9, 0xE, 0xE);
	}

	for (int i = 0; i < 16; i++) {
		int j = i & 7;
		h[j] ^= v[i];}

for (int i = 0; i < 8; i++) {hash->h4[i]=SWAP4(h[i]);}

barrier(CLK_LOCAL_MEM_FENCE);

}

// keccak256

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search1(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

 		sph_u64 keccak_gpu_state[25];

		for (int i = 0; i < 25; i++) {
			if (i < 4) { keccak_gpu_state[i] = hash->h8[i];
      } else {
      keccak_gpu_state[i] = 0;
      }
		}
		keccak_gpu_state[4] = 0x0000000000000001;
		keccak_gpu_state[16] = 0x8000000000000000;

		keccak_block(keccak_gpu_state);
		for (int i = 0; i < 4; i++) {hash->h8[i] = keccak_gpu_state[i];}
barrier(CLK_LOCAL_MEM_FENCE);

}

/// lyra2 algo 

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search2(__global hash_t* hashes)
{
 uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  uint2 state[16];

  for (int i = 0; i < 4; i++) { state[i] = as_uint2(hash->h8[i]);} //password
  for (int i = 0; i < 4; i++) { state[i + 4] = state[i]; } //salt 
  for (int i = 0; i < 8; i++) { state[i + 8] = as_uint2(blake2b_IV[i]); }

  //     blake2blyra x2 

  for (int i = 0; i < 24; i++) {round_lyra(state);} //because 12 is not enough

  __private uint2 Matrix[96][8]; // very uncool
  /// reducedSqueezeRow0

  for (int i = 0; i < 8; i++)
  {
	  for (int j = 0; j<12; j++) {Matrix[j + 84 - 12 * i][0] = state[j];}
	  round_lyra(state);
  }

  /// reducedSqueezeRow1

  for (int i = 0; i < 8; i++)
  {
	  for (int j = 0; j < 12; j++) {state[j] ^= Matrix[j + 12 * i][0];}
	  round_lyra(state);
	  for (int j = 0; j < 12; j++) {Matrix[j + 84 - 12 * i][1] = Matrix[j + 12 * i][0] ^ state[j];}
  }
 
  reduceDuplexRowSetup(1, 0, 2);
  reduceDuplexRowSetup(2, 1, 3);
  reduceDuplexRowSetup(3, 0, 4);
  reduceDuplexRowSetup(4, 3, 5);
  reduceDuplexRowSetup(5, 2, 6);
  reduceDuplexRowSetup(6, 1, 7);

  sph_u32 rowa;
  rowa = state[0].x & 7;

  reduceDuplexRow(7, rowa, 0);
  rowa = state[0].x & 7;
  reduceDuplexRow(0, rowa, 3);
  rowa = state[0].x & 7;
  reduceDuplexRow(3, rowa, 6);
  rowa = state[0].x & 7;
  reduceDuplexRow(6, rowa, 1);
  rowa = state[0].x & 7;
  reduceDuplexRow(1, rowa, 4);
  rowa = state[0].x & 7;
  reduceDuplexRow(4, rowa, 7);
  rowa = state[0].x & 7;
  reduceDuplexRow(7, rowa, 2);
  rowa = state[0].x & 7;
  reduceDuplexRow(2, rowa, 5);

  absorbblock(rowa);

  for (int i = 0; i < 4; i++) {hash->h8[i] = as_ulong(state[i]);} 
barrier(CLK_LOCAL_MEM_FENCE);

}

//skein256

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search3(__global hash_t* hashes)
{
 uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

		sph_u64 h[9];
		sph_u64 t[3];
        sph_u64 dt0, dt1, dt2, dt3;
		sph_u64 p0, p1, p2, p3, p4, p5, p6, p7;
        h[8] = skein_ks_parity;

		for (int i = 0; i < 8; i++) {
			h[i] = SKEIN_IV512_256[i];
			h[8] ^= h[i];}

			t[0] = t12[0];
			t[1] = t12[1];
			t[2] = t12[2];

        dt0 = hash->h8[0];
        dt1 = hash->h8[1];
        dt2 = hash->h8[2];
        dt3 = hash->h8[3];

		p0 = h[0] + dt0;
		p1 = h[1] + dt1;
		p2 = h[2] + dt2;
		p3 = h[3] + dt3;
		p4 = h[4];
		p5 = h[5] + t[0];
		p6 = h[6] + t[1];
		p7 = h[7];

        #pragma unroll
		for (int i = 1; i < 19; i+=2) {Round_8_512(p0, p1, p2, p3, p4, p5, p6, p7, i);}
        p0 ^= dt0;
        p1 ^= dt1;
        p2 ^= dt2;
        p3 ^= dt3;

		h[0] = p0;
		h[1] = p1;
		h[2] = p2;
		h[3] = p3;
		h[4] = p4;
		h[5] = p5;
		h[6] = p6;
		h[7] = p7;
		h[8] = skein_ks_parity;

		for (int i = 0; i < 8; i++) {h[8] ^= h[i];}

		t[0] = t12[3];
		t[1] = t12[4];
		t[2] = t12[5];
		p5 += t[0];  //p5 already equal h[5]
		p6 += t[1];

        #pragma unroll
		for (int i = 1; i < 19; i+=2) {Round_8_512(p0, p1, p2, p3, p4, p5, p6, p7, i);}

		hash->h8[0]      = p0;
		hash->h8[1]      = p1;
		hash->h8[2]      = p2;
		hash->h8[3]      = p3;
	barrier(CLK_LOCAL_MEM_FENCE);

}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search4(__global hash_t* hashes, __global uint* output, const ulong target)
{
// __local ulong T0[256], T1[256], T2[256], T3[256], T4[256], T5[256], T6[256], T7[256];
   // uint u   = get_local_id(0);
/*
for (uint u = get_local_id(0); u < 256; u += get_local_size(0)) {

  T0[u] = T0_G[u];
  T1[u] = T1_G[u];
  T2[u] = T2_G[u];
  T3[u] = T3_G[u];
  T4[u] = T4_G[u];
  T5[u] = T5_G[u];
  T6[u] = T6_G[u];
  T7[u] = T7_G[u];
 }
barrier(CLK_LOCAL_MEM_FENCE);

  T1[u] = SPH_ROTL64(T0[u], 8UL);
  T2[u] = SPH_ROTL64(T0[u], 16UL);
  T3[u] = SPH_ROTL64(T0[u], 24UL);
  T4[u] = SPH_ROTL64(T0[u], 32UL);
  T5[u] = SPH_ROTL64(T0[u], 40UL);
  T6[u] = SPH_ROTL64(T0[u], 48UL);
  T7[u] = SPH_ROTL64(T0[u], 56UL);

*/
	uint gid = get_global_id(0);

	__global hash_t *hash = &(hashes[gid - get_global_offset(0)]);

    __private ulong message[8], state[8];
	__private ulong t[8];

	for (int u = 0; u < 4; u++) {message[u] = hash->h8[u];}

	message[4] = 0x80UL;
	message[5] = 0UL;
	message[6] = 0UL;
	message[7] = 0x0100000000000000UL;

	for (int u = 0; u < 8; u++) {state[u] = message[u];}
	state[7] ^= 0x0001000000000000UL;

	for (int r = 0; r < 10; r ++) {ROUND_SMALL_P(state, r);}		

	state[7] ^= 0x0001000000000000UL;

	for (int r = 0; r < 10; r ++) {ROUND_SMALL_Q(message, r);}		

	for (int u = 0; u < 8; u++) {state[u] ^= message[u];}
	message[7] = state[7];

	for (int r = 0; r < 9; r ++) {ROUND_SMALL_P(state, r);}
    uchar8 State;
	State.s0 = as_uchar8(state[7] ^ 0x79).s0;
	State.s1 = as_uchar8(state[0] ^ 0x09).s1;
	State.s2 = as_uchar8(state[1] ^ 0x19).s2;
	State.s3 = as_uchar8(state[2] ^ 0x29).s3;
	State.s4 = as_uchar8(state[3] ^ 0x39).s4;
	State.s5 = as_uchar8(state[4] ^ 0x49).s5;
	State.s6 = as_uchar8(state[5] ^ 0x59).s6;
	State.s7 = as_uchar8(state[6] ^ 0x69).s7;

		state[7] = T0_G[State.s0]
			   ^ R64(T0_G[State.s1],  8)
         ^ R64(T0_G[State.s2], 16)
			   ^ R64(T0_G[State.s3], 24)
			   ^     T4_G[State.s4]
			   ^ R64(T4_G[State.s5],  8)
			   ^ R64(T4_G[State.s6], 16)
			   ^ R64(T4_G[State.s7], 24) ^message[7];

//	t[7] ^= message[7];
	barrier(CLK_LOCAL_MEM_FENCE);

	bool result = ( state[7] <= target);
	if (result) {
		output[atomic_inc(output + 0xFF)] = SWAP4(gid);
	}
}

#endif // LYRA2RE_CL
