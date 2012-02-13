// -ck modified kernel taken from Phoenix taken from poclbm, with aspects of
// phatk and others.
// Modified version copyright 2011-2012 Con Kolivas

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

#ifdef BITALIGN
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#define rotr(x, y) amd_bitalign((u)x, (u)x, (u)y)
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
 #else // BFI_INT
	// Later SDKs optimise this to BFI INT without patching and GCN
	// actually fails if manually patched with BFI_INT

	#define ch(x, y, z) bitselect((u)z, (u)y, (u)x)
	#define Ma(x, y, z) bitselect((u)x, (u)y, (u)z ^ (u)x)
#endif
#else // BITALIGN
	#define ch(x, y, z) (z ^ (x & (y ^ z)))
	#define Ma(x, y, z) ((x & z) | (y & (x | z)))
	#define rotr(x, y) rotate((u)x, (u)(32 - y))
#endif

// AMD's KernelAnalyzer throws errors compiling the kernel if we use 
// amd_bytealign on constants with vectors enabled, so we use this to avoid 
// problems. (this is used 4 times, and likely optimized out by the compiler.)
#define Ma2(x, y, z) ((y & z) | (x & (y | z)))

__kernel void search(const uint state0, const uint state1, const uint state2, const uint state3,
						const uint state4, const uint state5, const uint state6, const uint state7,
						const uint b1, const uint c1, const uint d1,
						const uint f1, const uint g1, const uint h1,
						const u base,
						const uint fw0, const uint fw1, const uint fw2, const uint fw3, const uint fw15, const uint fw01r, const uint fcty_e, const uint fcty_e2,
						__global uint * output)
{
	u W[24];
	//u Vals[8]; Now put at W[16] to be in same array
	u nonce;

#ifdef VECTORS4
	nonce = base + (uint)(get_local_id(0)) * 4u + (uint)(get_group_id(0)) * (WORKSIZE * 4u);
#elif defined VECTORS2
	nonce = base + (uint)(get_local_id(0)) * 2u + (uint)(get_group_id(0)) * (WORKSIZE * 2u);
#else
	nonce = base + get_local_id(0) + get_group_id(0) * (WORKSIZE);
#endif

	W[20] = fcty_e +  nonce;
	W[16] = state0 + W[20];
	W[19] = d1 + (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], b1, c1) + K[ 4] +  0x80000000;
	W[23] = h1 + W[19];
	W[20] += fcty_e2;
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma2(g1, W[20], f1);
	W[18] = c1 + (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], b1) + K[ 5];
	W[22] = g1 + W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma2(f1, W[19], W[20]);
	W[17] = b1 + (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[ 6];
	W[21] = f1 + W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[ 7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[ 8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[ 9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[15] + 0x00000280U;
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[16] + fw0;
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[17] + fw1;
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[2] = (rotr(nonce, 7) ^ rotr(nonce, 18) ^ (nonce >> 3U)) + fw2;

	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[18] +  W[2];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[3] = nonce + fw3;

	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[19] +  W[3];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[4] = (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)) + 0x80000000;
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[20] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[5] = (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[21] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[6] = (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)) + 0x00000280U;
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[22] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[7] = (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)) + fw0;
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[23] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[8] = (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)) + fw1;
	
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[24] +  W[8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[9] = W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));

	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[25] +  W[9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[10] = W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[26] + W[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[11] = W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[27] + W[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[12] = W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[28] + W[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[13] = W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[29] + W[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[14] = 0x00a00055U + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[30] + W[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[15] = fw15 + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[31] + W[15];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[0] = fw01r + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));

	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[32] +  W[0];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[1] = fw1 + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));

	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[33] +  W[1];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[2] += (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));

	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[34] +  W[2];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[3] += (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[35] +  W[3];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[4] += (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[36] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[5] += (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[37] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[6] += (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[38] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[7] += (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[39] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[8] += (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[40] +  W[8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[9] += (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[41] +  W[9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[10] += (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[42] + W[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[11] += (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[43] + W[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[12] += (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[44] + W[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[13] += (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[45] + W[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[14] += (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[46] + W[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[15] += (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[47] + W[15];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[0] += (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));

	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[48] +  W[0];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[1] += (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));

	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[49] +  W[1];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[2] += (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));

	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[50] +  W[2];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[3] += (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[51] +  W[3];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[4] += (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[52] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[5] += (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[53] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[6] += (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[54] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[7] += (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[55] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[8] += (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[56] +  W[8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[9] += (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[57] +  W[9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[10] += (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[58] + W[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[11] += (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[59] + W[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[12] += (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[60] + W[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[13] += (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[61] + W[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[14] += (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[62] + W[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[15] += (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[63] + W[15];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);

	W[0] = W[16] + state0;
	W[7] = W[23] + state7;
	W[23] = 0xb0edbdd0 + K[ 0] +  W[0];

	W[3] = W[19] + state3;
	W[19] = 0xa54ff53a + W[23];
	W[23] += 0x08909ae5U;

	W[1] = W[17] + state1;
	W[6] = W[22] + state6;
	W[22] = 0x1f83d9abU + (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + (0x9b05688cU ^ (W[19] & 0xca0b3af3U)) + K[ 1] +  W[1];

	W[2] = W[18] + state2;
	W[18] = 0x3c6ef372U + W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) +  Ma2(0xbb67ae85U, W[23], 0x6a09e667U);

	W[5] = W[21] + state5;
	W[21] = 0x9b05688cU + (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], 0x510e527fU) + K[ 2] +  W[2];
	W[17] = 0xbb67ae85U + W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma2(0x6a09e667U, W[22], W[23]);

	W[4] = W[20] + state4;
	W[20] = 0x510e527fU + (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[ 3] +  W[3];
	W[16] = 0x6a09e667U + W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[ 4] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[ 5] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[ 6] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[ 7] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[ 8] +  0x80000000;
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[ 9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[15] + 0x00000100U;
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[0] += (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U));

	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[16] +  W[0];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[1] += (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + 0x00a00000U;

	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[17] +  W[1];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[2] += (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));

	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[18] +  W[2];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[3] += (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[19] +  W[3];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[4] += (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[20] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[5] += (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[21] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[6] += (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + 0x00000100U + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[22] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[7] += 0x11002000U + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[23] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[8] = 0x80000000 + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[24] +  W[8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[9] = W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[25] +  W[9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[10] = W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[26] + W[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[11] = W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[27] + W[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[12] = W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[28] + W[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[13] = W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[29] + W[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[14] = 0x00400022U + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[30] + W[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[15] = 0x00000100U + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[31] + W[15];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[0] += (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));

	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[32] +  W[0];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[1] += (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));

	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[33] +  W[1];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[2] += (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));

	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[34] +  W[2];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[3] += (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[35] +  W[3];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[4] += (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[36] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[5] += (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[37] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[6] += (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[38] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[7] += (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[39] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[8] += (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[40] +  W[8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[9] += (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[41] +  W[9];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[10] += (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[42] + W[10];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[11] += (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[43] + W[11];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[12] += (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[44] + W[12];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[13] += (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[45] + W[13];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[14] += (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[46] + W[14];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[15] += (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[47] + W[15];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[0] += (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));

	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[48] +  W[0];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[1] += (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));

	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[49] +  W[1];
	W[18] += W[22];
	W[22] += (rotr(W[23], 2) ^ rotr(W[23], 13) ^ rotr(W[23], 22)) + Ma(W[17], W[23], W[16]);
	W[2] += (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));

	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[50] +  W[2];
	W[17] += W[21];
	W[21] += (rotr(W[22], 2) ^ rotr(W[22], 13) ^ rotr(W[22], 22)) + Ma(W[16], W[22], W[23]);
	W[3] += (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[51] +  W[3];
	W[16] += W[20];
	W[20] += (rotr(W[21], 2) ^ rotr(W[21], 13) ^ rotr(W[21], 22)) + Ma(W[23], W[21], W[22]);
	W[4] += (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U));
	
	W[19] += (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[52] +  W[4];
	W[23] += W[19];
	W[19] += (rotr(W[20], 2) ^ rotr(W[20], 13) ^ rotr(W[20], 22)) + Ma(W[22], W[20], W[21]);
	W[5] += (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U));
	
	W[18] += (rotr(W[23], 6) ^ rotr(W[23], 11) ^ rotr(W[23], 25)) + ch(W[23], W[16], W[17]) + K[53] +  W[5];
	W[22] += W[18];
	W[18] += (rotr(W[19], 2) ^ rotr(W[19], 13) ^ rotr(W[19], 22)) + Ma(W[21], W[19], W[20]);
	W[6] += (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U));
	
	W[17] += (rotr(W[22], 6) ^ rotr(W[22], 11) ^ rotr(W[22], 25)) + ch(W[22], W[23], W[16]) + K[54] +  W[6];
	W[21] += W[17];
	W[17] += (rotr(W[18], 2) ^ rotr(W[18], 13) ^ rotr(W[18], 22)) + Ma(W[20], W[18], W[19]);
	W[7] += (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U));
	
	W[16] += (rotr(W[21], 6) ^ rotr(W[21], 11) ^ rotr(W[21], 25)) + ch(W[21], W[22], W[23]) + K[55] +  W[7];
	W[20] += W[16];
	W[16] += (rotr(W[17], 2) ^ rotr(W[17], 13) ^ rotr(W[17], 22)) + Ma(W[19], W[17], W[18]);
	W[8] += (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U));
	
	W[23] += (rotr(W[20], 6) ^ rotr(W[20], 11) ^ rotr(W[20], 25)) + ch(W[20], W[21], W[22]) + K[56] +  W[8];
	W[19] += W[23];
	W[23] += (rotr(W[16], 2) ^ rotr(W[16], 13) ^ rotr(W[16], 22)) + Ma(W[18], W[16], W[17]);
	W[9] += (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	
	W[22] += (rotr(W[19], 6) ^ rotr(W[19], 11) ^ rotr(W[19], 25)) + ch(W[19], W[20], W[21]) + K[57] +  W[9];
	W[18] += W[22];
	W[10] += (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U));
	
	W[21] += (rotr(W[18], 6) ^ rotr(W[18], 11) ^ rotr(W[18], 25)) + ch(W[18], W[19], W[20]) + K[58] + W[10];
	W[17] += W[21];
	W[11] += (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U));
	
	W[20] += (rotr(W[17], 6) ^ rotr(W[17], 11) ^ rotr(W[17], 25)) + ch(W[17], W[18], W[19]) + K[59] + W[11];
	W[16] += W[20];
	W[12] += (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U));
	
	W[23] += W[19] + (rotr(W[16], 6) ^ rotr(W[16], 11) ^ rotr(W[16], 25)) + ch(W[16], W[17], W[18]) + K[60] + W[12];

#define FOUND (0x80)
#define NFLAG (0x7F)

#if defined(VECTORS4)
	W[23] ^= -0x5be0cd19U;
	bool result = W[23].x & W[23].y & W[23].z & W[23].w;
	if (!result) {
		if (!W[23].x)
			output[FOUND] = output[NFLAG & nonce.x] =  nonce.x;
		if (!W[23].y)
			output[FOUND] = output[NFLAG & nonce.y] =  nonce.y;
		if (!W[23].z)
			output[FOUND] = output[NFLAG & nonce.z] =  nonce.z;
		if (!W[23].w)
			output[FOUND] = output[NFLAG & nonce.w] =  nonce.w;
	}
#elif defined(VECTORS2)
	W[23] ^= -0x5be0cd19U;
	bool result = W[23].x & W[23].y;
	if (!result) {
		if (!W[23].x)
			output[FOUND] = output[NFLAG & nonce.x] =  nonce.x;
		if (!W[23].y)
			output[FOUND] = output[NFLAG & nonce.y] =  nonce.y;
	}
#else
	if (W[23] == -0x5be0cd19U)
		output[FOUND] = output[NFLAG & nonce] =  nonce;
#endif
}
