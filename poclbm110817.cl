// -ck modified kernel taken from Phoenix taken from poclbm, with aspects of
// phatk and others.
// Modified version copyright 2011 Con Kolivas

// This file is taken and modified from the public-domain poclbm project, and
// we have therefore decided to keep it public-domain in Phoenix.

#ifdef VECTORS4
	typedef uint4 u;
#elif defined VECTORS2
	typedef uint2 u;
#else
	typedef uint u;
#endif

__constant uint K[64] = { 
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


// This part is not from the stock poclbm kernel. It's part of an optimization
// added in the Phoenix Miner.

// Some AMD devices have a BFI_INT opcode, which behaves exactly like the
// SHA-256 ch function, but provides it in exactly one instruction. If
// detected, use it for ch. Otherwise, construct ch out of simpler logical
// primitives.

#ifdef BFI_INT
	// Well, slight problem... It turns out BFI_INT isn't actually exposed to
	// OpenCL (or CAL IL for that matter) in any way. However, there is 
	// a similar instruction, BYTE_ALIGN_INT, which is exposed to OpenCL via
	// amd_bytealign, takes the same inputs, and provides the same output. 
	// We can use that as a placeholder for BFI_INT and have the application 
	// patch it after compilation.
	
	// This is the BFI_INT function
	#define ch(x, y, z) amd_bytealign(x, y, z)
	
	// Ma can also be implemented in terms of BFI_INT...
	#define Ma(x, y, z) amd_bytealign( (z^x), (y), (x) )
#else
	#define ch(x, y, z) (z ^ (x & (y ^ z)))
	#define Ma(x, y, z) ((x & z) | (y & (x | z)))
#endif

#ifdef BITALIGN
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#define rotr(x, y) amd_bitalign((u)x, (u)x, (u)y)
#else
	#define rotr(x, y) rotate((u)x, (u)(32-y))
#endif

// AMD's KernelAnalyzer throws errors compiling the kernel if we use 
// amd_bytealign on constants with vectors enabled, so we use this to avoid 
// problems. (this is used 4 times, and likely optimized out by the compiler.)
#define Ma2(x, y, z) ((y & z) | (x & (y | z)))

__kernel void search(	const uint state0, const uint state1, const uint state2, const uint state3,
						const uint state4, const uint state5, const uint state6, const uint state7,
						const uint b1, const uint c1, const uint d1,
						const uint f1, const uint g1, const uint h1,
						const uint base,
						const uint fw0, const uint fw1, const uint fw2, const uint fw3, const uint fw15, const uint fw01r, const uint fcty_e, const uint fcty_e2,
						__global uint * output)
{
	u W[24];
	u Vals[8];
	u nonce;

#ifdef VECTORS4
	nonce = base + (get_global_id(0)<<2) + (uint4)(0, 1, 2, 3);
#elif defined VECTORS2
	nonce = base + (get_global_id(0)<<1) + (uint2)(0, 1);
#else
	nonce = base + get_global_id(0);
#endif

	W[3] = nonce + fw3;
	Vals[4] = fcty_e +  nonce;
	Vals[0] = state0 + Vals[4];
	Vals[4] = Vals[4] + fcty_e2;
	Vals[3] = d1 + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], b1, c1) + K[ 4] +  0x80000000;
	Vals[7] = h1 + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma2(g1, Vals[4], f1);
	Vals[2] = c1 + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], b1) + K[ 5];
	Vals[6] = g1 + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma2(f1, Vals[3], Vals[4]);
	Vals[1] = b1 + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[ 6];
	Vals[5] = f1 + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[ 7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[ 8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[ 9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[15] + 0x00000280U;
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[16] + fw0;
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[17] + fw1;
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[2] = (rotr(nonce, 7) ^ rotr(nonce, 18) ^ (nonce >> 3U)) + fw2;
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[18] +  W[2];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[19] +  W[3];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[4] = (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)) + 0x80000000;
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[20] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[5] = (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[21] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[6] = (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)) + 0x00000280U;
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[22] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[7] = (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)) + fw0;
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[23] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[8] = (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)) + fw1;
	
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[24] +  W[8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[9] = W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[25] +  W[9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[10] = W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[26] + W[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[11] = W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[27] + W[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[12] = W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[28] + W[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[13] = W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[29] + W[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[14] = 0x00a00055U + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[30] + W[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[15] = fw15 + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[31] + W[15];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[0] = fw01r + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[32] +  W[0];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[1] = fw1 + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[33] +  W[1];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[34] +  W[2];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[35] +  W[3];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[36] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[37] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[38] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[39] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[40] +  W[8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[41] +  W[9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[42] + W[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[43] + W[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[44] + W[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[13] = W[13] + (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[45] + W[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[14] = W[14] + (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[46] + W[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[15] = W[15] + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[47] + W[15];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[48] +  W[0];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[49] +  W[1];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[50] +  W[2];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[51] +  W[3];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[52] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[53] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[54] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[55] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[56] +  W[8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[57] +  W[9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[58] + W[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[59] + W[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[60] + W[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[13] = W[13] + (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[61] + W[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[14] = W[14] + (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[62] + W[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[15] = W[15] + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[63] + W[15];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);

	W[0] = Vals[0] + state0;
	W[1] = Vals[1] + state1;
	W[2] = Vals[2] + state2;
	W[3] = Vals[3] + state3;
	W[4] = Vals[4] + state4;
	W[5] = Vals[5] + state5;
	W[6] = Vals[6] + state6;
	W[7] = Vals[7] + state7;

	Vals[7] = 0xb0edbdd0 + K[ 0] +  W[0];
	Vals[3] = 0xa54ff53a + Vals[7];
	Vals[7] = Vals[7] + 0x08909ae5U;
	Vals[6] = 0x1f83d9abU + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + (0x9b05688cU ^ (Vals[3] & 0xca0b3af3U)) + K[ 1] +  W[1];
	Vals[2] = 0x3c6ef372U + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) +  Ma2(0xbb67ae85U, Vals[7], 0x6a09e667U);
	Vals[5] = 0x9b05688cU + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], 0x510e527fU) + K[ 2] +  W[2];
	Vals[1] = 0xbb67ae85U + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma2(0x6a09e667U, Vals[6], Vals[7]);
	Vals[4] = 0x510e527fU + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[ 3] +  W[3];
	Vals[0] = 0x6a09e667U + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[ 4] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[ 5] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[ 6] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[ 7] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[ 8] +  0x80000000;
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[ 9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[15] + 0x00000100U;
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U));
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[16] +  W[0];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + 0x00a00000U;
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[17] +  W[1];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[18] +  W[2];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[19] +  W[3];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[20] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[21] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + 0x00000100U + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[22] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[7] = W[7] + 0x11002000U + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[23] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[8] = 0x80000000 + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[24] +  W[8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[9] = W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[25] +  W[9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[10] = W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[26] + W[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[11] = W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[27] + W[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[12] = W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[28] + W[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[13] = W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[29] + W[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[14] = 0x00400022U + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[30] + W[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[15] = 0x00000100U + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[31] + W[15];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[32] +  W[0];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[33] +  W[1];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[34] +  W[2];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[35] +  W[3];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[36] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[37] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[38] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[39] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[40] +  W[8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[41] +  W[9];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[42] + W[10];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[43] + W[11];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[44] + W[12];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[13] = W[13] + (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[45] + W[13];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[14] = W[14] + (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[46] + W[14];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[15] = W[15] + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[47] + W[15];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[48] +  W[0];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[49] +  W[1];
	Vals[2] = Vals[2] + Vals[6];
	Vals[6] = Vals[6] + (rotr(Vals[7], 2) ^ rotr(Vals[7], 13) ^ rotr(Vals[7], 22)) + Ma(Vals[1], Vals[7], Vals[0]);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[50] +  W[2];
	Vals[1] = Vals[1] + Vals[5];
	Vals[5] = Vals[5] + (rotr(Vals[6], 2) ^ rotr(Vals[6], 13) ^ rotr(Vals[6], 22)) + Ma(Vals[0], Vals[6], Vals[7]);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[51] +  W[3];
	Vals[0] = Vals[0] + Vals[4];
	Vals[4] = Vals[4] + (rotr(Vals[5], 2) ^ rotr(Vals[5], 13) ^ rotr(Vals[5], 22)) + Ma(Vals[7], Vals[5], Vals[6]);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	Vals[3] = Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[52] +  W[4];
	Vals[7] = Vals[7] + Vals[3];
	Vals[3] = Vals[3] + (rotr(Vals[4], 2) ^ rotr(Vals[4], 13) ^ rotr(Vals[4], 22)) + Ma(Vals[6], Vals[4], Vals[5]);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	Vals[2] = Vals[2] + (rotr(Vals[7], 6) ^ rotr(Vals[7], 11) ^ rotr(Vals[7], 25)) + ch(Vals[7], Vals[0], Vals[1]) + K[53] +  W[5];
	Vals[6] = Vals[6] + Vals[2];
	Vals[2] = Vals[2] + (rotr(Vals[3], 2) ^ rotr(Vals[3], 13) ^ rotr(Vals[3], 22)) + Ma(Vals[5], Vals[3], Vals[4]);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	Vals[1] = Vals[1] + (rotr(Vals[6], 6) ^ rotr(Vals[6], 11) ^ rotr(Vals[6], 25)) + ch(Vals[6], Vals[7], Vals[0]) + K[54] +  W[6];
	Vals[5] = Vals[5] + Vals[1];
	Vals[1] = Vals[1] + (rotr(Vals[2], 2) ^ rotr(Vals[2], 13) ^ rotr(Vals[2], 22)) + Ma(Vals[4], Vals[2], Vals[3]);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	Vals[0] = Vals[0] + (rotr(Vals[5], 6) ^ rotr(Vals[5], 11) ^ rotr(Vals[5], 25)) + ch(Vals[5], Vals[6], Vals[7]) + K[55] +  W[7];
	Vals[4] = Vals[4] + Vals[0];
	Vals[0] = Vals[0] + (rotr(Vals[1], 2) ^ rotr(Vals[1], 13) ^ rotr(Vals[1], 22)) + Ma(Vals[3], Vals[1], Vals[2]);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	Vals[7] = Vals[7] + (rotr(Vals[4], 6) ^ rotr(Vals[4], 11) ^ rotr(Vals[4], 25)) + ch(Vals[4], Vals[5], Vals[6]) + K[56] +  W[8];
	Vals[3] = Vals[3] + Vals[7];
	Vals[7] = Vals[7] + (rotr(Vals[0], 2) ^ rotr(Vals[0], 13) ^ rotr(Vals[0], 22)) + Ma(Vals[2], Vals[0], Vals[1]);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	Vals[6] = Vals[6] + (rotr(Vals[3], 6) ^ rotr(Vals[3], 11) ^ rotr(Vals[3], 25)) + ch(Vals[3], Vals[4], Vals[5]) + K[57] +  W[9];
	Vals[2] = Vals[2] + Vals[6];
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	Vals[5] = Vals[5] + (rotr(Vals[2], 6) ^ rotr(Vals[2], 11) ^ rotr(Vals[2], 25)) + ch(Vals[2], Vals[3], Vals[4]) + K[58] + W[10];
	Vals[1] = Vals[1] + Vals[5];
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	Vals[4] = Vals[4] + (rotr(Vals[1], 6) ^ rotr(Vals[1], 11) ^ rotr(Vals[1], 25)) + ch(Vals[1], Vals[2], Vals[3]) + K[59] + W[11];
	Vals[0] = Vals[0] + Vals[4];
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	Vals[7] = Vals[7] + Vals[3] + (rotr(Vals[0], 6) ^ rotr(Vals[0], 11) ^ rotr(Vals[0], 25)) + ch(Vals[0], Vals[1], Vals[2]) + K[60] + W[12];
	Vals[7] ^= -0x5be0cd19U;

#define FOUND (0x80)
#define NFLAG (0x7F)

#if defined(VECTORS4)
	bool result = Vals[7].x & Vals[7].y & Vals[7].z & Vals[7].w;
	if (!result) {
		if (!Vals[7].x)
			output[FOUND] = output[NFLAG & nonce.x] =  nonce.x;
		if (!Vals[7].y)
			output[FOUND] = output[NFLAG & nonce.y] =  nonce.y;
		if (!Vals[7].z)
			output[FOUND] = output[NFLAG & nonce.z] =  nonce.z;
		if (!Vals[7].w)
			output[FOUND] = output[NFLAG & nonce.w] =  nonce.w;
	}
#elif defined(VECTORS2)
	bool result = Vals[7].x & Vals[7].y;
	if (!result) {
		if (!Vals[7].x)
			output[FOUND] = output[NFLAG & nonce.x] =  nonce.x;
		if (!Vals[7].y)
			output[FOUND] = output[NFLAG & nonce.y] =  nonce.y;
	}
#else
	if (!Vals[7])
		output[FOUND] = output[NFLAG & nonce] =  nonce;
#endif
}
