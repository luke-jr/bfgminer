// This file is taken and modified from the public-domain poclbm project, and
// we have therefore decided to keep it public-domain in Phoenix.

// The X is a placeholder for patching to suit hardware
#define VECTORSX

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
// SHA-256 Ch function, but provides it in exactly one instruction. If
// detected, use it for Ch. Otherwise, construct Ch out of simpler logical
// primitives.

#define BFI_INTX

#ifdef BFI_INT

#define BITALIGN
	// Well, slight problem... It turns out BFI_INT isn't actually exposed to
	// OpenCL (or CAL IL for that matter) in any way. However, there is 
	// a similar instruction, BYTE_ALIGN_INT, which is exposed to OpenCL via
	// amd_bytealign, takes the same inputs, and provides the same output. 
	// We can use that as a placeholder for BFI_INT and have the application 
	// patch it after compilation.
	
	// This is the BFI_INT function
	#define Ch(x, y, z) amd_bytealign(x, y, z)
	
	// Ma can also be implemented in terms of BFI_INT...
	#define Ma(x, y, z) amd_bytealign((y), (x | z), (z & x))
#else
	#define Ch(x, y, z) (z ^ (x & (y ^ z)))
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
	u W[26];
	u A,B,C,D,E,F,G,H;
	u nonce;
	uint it;

#ifdef VECTORS4
	nonce = ((base >> 2) + (get_global_id(0))<<2) + (uint4)(0, 1, 2, 3);
#elif defined VECTORS2
	nonce = ((base >> 1) + (get_global_id(0))<<1) + (uint2)(0, 1);
#else
	nonce = base + get_global_id(0);
#endif

	W[3] = nonce + fw3;
	E = fcty_e +  nonce; A = state0 + E; E = E + fcty_e2;
	D = d1 + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, b1, c1) + K[ 4] +  0x80000000; H = h1 + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma2(g1, E, f1);
	C = c1 + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, b1) + K[ 5]; G = g1 + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma2(f1, D, E);
	B = b1 + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[ 6]; F = f1 + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[ 7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[ 8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[ 9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[15] + 0x00000280U; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[16] + fw0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[17] + fw1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[2] = (rotr(nonce, 7) ^ rotr(nonce, 18) ^ (nonce >> 3U)) + fw2;
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[18] +  W[2]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[19] +  W[3]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[4] = (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)) + 0x80000000; 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[20] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[5] = (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[21] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[6] = (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)) + 0x00000280U; 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[22] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[7] = (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)) + fw0; 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[23] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[8] = (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)) + fw1; 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[24] +  W[8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[9] = W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[25] +  W[9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[10] = W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[26] + W[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[11] = W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[27] + W[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[12] = W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[28] + W[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[13] = W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[29] + W[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[14] = 0x00a00055U + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[30] + W[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[15] = fw15 + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[31] + W[15]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[0] = fw01r + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[32] +  W[0]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[1] = fw1 + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[33] +  W[1]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[34] +  W[2]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[35] +  W[3]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[36] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[37] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[38] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[39] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[40] +  W[8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[41] +  W[9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[42] + W[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[43] + W[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[44] + W[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[13] = W[13] + (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[45] + W[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[14] = W[14] + (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[46] + W[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[15] = W[15] + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[47] + W[15]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[48] +  W[0]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[49] +  W[1]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[50] +  W[2]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[51] +  W[3]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[52] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[53] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[54] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[55] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[56] +  W[8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[57] +  W[9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[58] + W[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[59] + W[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[60] + W[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[13] = W[13] + (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[61] + W[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[14] = W[14] + (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[62] + W[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[15] = W[15] + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[63] + W[15]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);

	W[0] = A + state0; W[1] = B + state1;
	W[2] = C + state2; W[3] = D + state3;
	W[4] = E + state4; W[5] = F + state5;
	W[6] = G + state6; W[7] = H + state7;

	H = 0xb0edbdd0 + K[ 0] +  W[0]; D = 0xa54ff53a + H; H = H + 0x08909ae5U;
	G = 0x1f83d9abU + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + (0x9b05688cU ^ (D & 0xca0b3af3U)) + K[ 1] +  W[1]; C = 0x3c6ef372U + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) +  Ma2(0xbb67ae85U, H, 0x6a09e667U);
	F = 0x9b05688cU + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, 0x510e527fU) + K[ 2] +  W[2]; B = 0xbb67ae85U + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma2(0x6a09e667U, G, H);
	E = 0x510e527fU + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[ 3] +  W[3]; A = 0x6a09e667U + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[ 4] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[ 5] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[ 6] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[ 7] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[ 8] +  0x80000000; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[ 9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[15] + 0x00000100U; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[16] +  W[0]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + 0x00a00000U;
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[17] +  W[1]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[18] +  W[2]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[19] +  W[3]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[20] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[21] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + 0x00000100U + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[22] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[7] = W[7] + 0x11002000U + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[23] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[8] = 0x80000000 + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[24] +  W[8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[9] = W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[25] +  W[9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[10] = W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[26] + W[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[11] = W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[27] + W[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[12] = W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[28] + W[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[13] = W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[29] + W[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[14] = 0x00400022U + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[30] + W[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[15] = 0x00000100U + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[31] + W[15]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[32] +  W[0]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[33] +  W[1]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[34] +  W[2]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[35] +  W[3]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[36] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[37] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[38] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[39] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[40] +  W[8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[41] +  W[9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[42] + W[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[43] + W[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[44] + W[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[13] = W[13] + (rotr(W[14], 7) ^ rotr(W[14], 18) ^ (W[14] >> 3U)) + W[6] + (rotr(W[11], 17) ^ rotr(W[11], 19) ^ (W[11] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[45] + W[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[14] = W[14] + (rotr(W[15], 7) ^ rotr(W[15], 18) ^ (W[15] >> 3U)) + W[7] + (rotr(W[12], 17) ^ rotr(W[12], 19) ^ (W[12] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[46] + W[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[15] = W[15] + (rotr(W[0], 7) ^ rotr(W[0], 18) ^ (W[0] >> 3U)) + W[8] + (rotr(W[13], 17) ^ rotr(W[13], 19) ^ (W[13] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[47] + W[15]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[0] = W[0] + (rotr(W[1], 7) ^ rotr(W[1], 18) ^ (W[1] >> 3U)) + W[9] + (rotr(W[14], 17) ^ rotr(W[14], 19) ^ (W[14] >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[48] +  W[0]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[1] = W[1] + (rotr(W[2], 7) ^ rotr(W[2], 18) ^ (W[2] >> 3U)) + W[10] + (rotr(W[15], 17) ^ rotr(W[15], 19) ^ (W[15] >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[49] +  W[1]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W[2] = W[2] + (rotr(W[3], 7) ^ rotr(W[3], 18) ^ (W[3] >> 3U)) + W[11] + (rotr(W[0], 17) ^ rotr(W[0], 19) ^ (W[0] >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[50] +  W[2]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W[3] = W[3] + (rotr(W[4], 7) ^ rotr(W[4], 18) ^ (W[4] >> 3U)) + W[12] + (rotr(W[1], 17) ^ rotr(W[1], 19) ^ (W[1] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[51] +  W[3]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W[4] = W[4] + (rotr(W[5], 7) ^ rotr(W[5], 18) ^ (W[5] >> 3U)) + W[13] + (rotr(W[2], 17) ^ rotr(W[2], 19) ^ (W[2] >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[52] +  W[4]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W[5] = W[5] + (rotr(W[6], 7) ^ rotr(W[6], 18) ^ (W[6] >> 3U)) + W[14] + (rotr(W[3], 17) ^ rotr(W[3], 19) ^ (W[3] >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[53] +  W[5]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W[6] = W[6] + (rotr(W[7], 7) ^ rotr(W[7], 18) ^ (W[7] >> 3U)) + W[15] + (rotr(W[4], 17) ^ rotr(W[4], 19) ^ (W[4] >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[54] +  W[6]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W[7] = W[7] + (rotr(W[8], 7) ^ rotr(W[8], 18) ^ (W[8] >> 3U)) + W[0] + (rotr(W[5], 17) ^ rotr(W[5], 19) ^ (W[5] >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[55] +  W[7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W[8] = W[8] + (rotr(W[9], 7) ^ rotr(W[9], 18) ^ (W[9] >> 3U)) + W[1] + (rotr(W[6], 17) ^ rotr(W[6], 19) ^ (W[6] >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[56] +  W[8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W[9] = W[9] + (rotr(W[10], 7) ^ rotr(W[10], 18) ^ (W[10] >> 3U)) + W[2] + (rotr(W[7], 17) ^ rotr(W[7], 19) ^ (W[7] >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[57] +  W[9]; C = C + G;
	W[10] = W[10] + (rotr(W[11], 7) ^ rotr(W[11], 18) ^ (W[11] >> 3U)) + W[3] + (rotr(W[8], 17) ^ rotr(W[8], 19) ^ (W[8] >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[58] + W[10]; B = B + F;
	W[11] = W[11] + (rotr(W[12], 7) ^ rotr(W[12], 18) ^ (W[12] >> 3U)) + W[4] + (rotr(W[9], 17) ^ rotr(W[9], 19) ^ (W[9] >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[59] + W[11]; A = A + E;
	W[12] = W[12] + (rotr(W[13], 7) ^ rotr(W[13], 18) ^ (W[13] >> 3U)) + W[5] + (rotr(W[10], 17) ^ rotr(W[10], 19) ^ (W[10] >> 10U)); 
	H = H + D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[60] + W[12];

	H+=0x5be0cd19U;

#if defined(VECTORS4) || defined(VECTORS2)
	if (H.x == 0)
	{
		for (it = 0; it != 127; it++) {
			if (!output[it]) {
				output[it] = nonce.x;
				output[127] = 1;
				break;
			}
		}
	}
	if (H.y == 0)
	{
		for (it = 0; it != 127; it++) {
			if (!output[it]) {
				output[it] = nonce.y;
				output[127] = 1;
				break;
			}
		}
	}
#ifdef VECTORS4
	if (H.z == 0)
	{
		for (it = 0; it != 127; it++) {
			if (!output[it]) {
				output[it] = nonce.z;
				output[127] = 1;
				break;
			}
		}
	}
	if (H.w == 0)
	{
		for (it = 0; it != 127; it++) {
			if (!output[it]) {
				output[it] = nonce.w;
				output[127] = 1;
				break;
			}
		}
	}
#endif
#else
	if (H == 0)
	{
		for (it = 0; it != 127; it++) {
			if (!output[it]) {
				output[it] = nonce;
				output[127] = 1;
				break;
			}
		}
	}
#endif
}