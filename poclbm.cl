// This file is taken and modified from the public-domain poclbm project, and
// we have therefore decided to keep it public-domain in Phoenix.

#define VECTORS

#ifdef VECTORS
	typedef uint4 u;
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

#define BITALIGN

#ifdef BITALIGN
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#define rotr(x, y) amd_bitalign((u)x, (u)x, (u)y)
#else
	#define rotr(x, y) rotate((u)x, (u)(32-y))
#endif

// This part is not from the stock poclbm kernel. It's part of an optimization
// added in the Phoenix Miner.

// Some AMD devices have a BFI_INT opcode, which behaves exactly like the
// SHA-256 Ch function, but provides it in exactly one instruction. If
// detected, use it for Ch. Otherwise, construct Ch out of simpler logical
// primitives.

#define BFI_INT

#ifdef BFI_INT
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

// AMD's KernelAnalyzer throws errors compiling the kernel if we use 
// amd_bytealign on constants with vectors enabled, so we use this to avoid 
// problems. (this is used 4 times, and likely optimized out by the compiler.)
#define Ma2(x, y, z) ((y & z) | (x & (y | z)))

__kernel void search(	const uint state0, const uint state1, const uint state2, const uint state3,
						const uint state4, const uint state5, const uint state6, const uint state7,
						const uint B1, const uint C1, const uint D1,
						const uint F1, const uint G1, const uint H1,
						const uint base,
						const uint fW0, const uint fW1, const uint fW2, const uint fW3, const uint fW15, const uint fW01r, const uint fcty_e, const uint fcty_e2,
						__global uint * output)
{
	u W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15;
	u A,B,C,D,E,F,G,H;
	u nonce;
	uint it;

#ifdef VECTORS 
	nonce = ((base >> 2) + (get_global_id(0))<<2) + (uint4)(0, 1, 2, 3);
#else
	nonce = base + get_global_id(0);
#endif

	W3 = nonce + fW3;
	E = fcty_e +  nonce; A = state0 + E; E = E + fcty_e2;
	D = D1 + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B1, C1) + K[ 4] +  0x80000000; H = H1 + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma2(G1, E, F1);
	C = C1 + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B1) + K[ 5]; G = G1 + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma2(F1, D, E);
	B = B1 + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[ 6]; F = F1 + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[ 7]; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[ 8]; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[ 9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[15] + 0x00000280U; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[16] + fW0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[17] + fW1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W2 = (rotr(nonce, 7) ^ rotr(nonce, 18) ^ (nonce >> 3U)) + fW2;
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[18] +  W2; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[19] +  W3; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W4 = (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10U)) + 0x80000000; 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[20] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W5 = (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[21] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W6 = (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10U)) + 0x00000280U; 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[22] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W7 = (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10U)) + fW0; 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[23] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W8 = (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10U)) + fW1; 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[24] +  W8; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W9 = W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[25] +  W9; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W10 = W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[26] + W10; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W11 = W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[27] + W11; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W12 = W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[28] + W12; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W13 = W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[29] + W13; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W14 = 0x00a00055U + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[30] + W14; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W15 = fW15 + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[31] + W15; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W0 = fW01r + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[32] +  W0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W1 = fW1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3U)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[33] +  W1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3U)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[34] +  W2; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3U)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[35] +  W3; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3U)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[36] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3U)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[37] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3U)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[38] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3U)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[39] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3U)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[40] +  W8; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3U)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[41] +  W9; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3U)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[42] + W10; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3U)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[43] + W11; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3U)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[44] + W12; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W13 = W13 + (rotr(W14, 7) ^ rotr(W14, 18) ^ (W14 >> 3U)) + W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[45] + W13; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W14 = W14 + (rotr(W15, 7) ^ rotr(W15, 18) ^ (W15 >> 3U)) + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[46] + W14; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W15 = W15 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3U)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[47] + W15; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3U)) + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[48] +  W0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3U)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[49] +  W1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3U)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[50] +  W2; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3U)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[51] +  W3; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3U)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[52] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3U)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[53] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3U)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[54] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3U)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[55] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3U)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[56] +  W8; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3U)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[57] +  W9; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3U)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[58] + W10; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3U)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[59] + W11; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3U)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[60] + W12; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W13 = W13 + (rotr(W14, 7) ^ rotr(W14, 18) ^ (W14 >> 3U)) + W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[61] + W13; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W14 = W14 + (rotr(W15, 7) ^ rotr(W15, 18) ^ (W15 >> 3U)) + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[62] + W14; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W15 = W15 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3U)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[63] + W15; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);

	W0 = A + state0; W1 = B + state1;
	W2 = C + state2; W3 = D + state3;
	W4 = E + state4; W5 = F + state5;
	W6 = G + state6; W7 = H + state7;

	H = 0xb0edbdd0 + K[ 0] +  W0; D = 0xa54ff53a + H; H = H + 0x08909ae5U;
	G = 0x1f83d9abU + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + (0x9b05688cU ^ (D & 0xca0b3af3U)) + K[ 1] +  W1; C = 0x3c6ef372U + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) +  Ma2(0xbb67ae85U, H, 0x6a09e667U);
	F = 0x9b05688cU + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, 0x510e527fU) + K[ 2] +  W2; B = 0xbb67ae85U + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma2(0x6a09e667U, G, H);
	E = 0x510e527fU + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[ 3] +  W3; A = 0x6a09e667U + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[ 4] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[ 5] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[ 6] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[ 7] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[ 8] +  0x80000000; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[ 9]; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[10]; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[11]; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[12]; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[13]; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[14]; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[15] + 0x00000100U; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[16] +  W0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3U)) + 0x00a00000U;
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[17] +  W1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3U)) + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[18] +  W2; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3U)) + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[19] +  W3; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3U)) + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[20] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3U)) + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[21] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3U)) + 0x00000100U + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[22] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W7 = W7 + 0x11002000U + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[23] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W8 = 0x80000000 + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[24] +  W8; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W9 = W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[25] +  W9; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W10 = W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[26] + W10; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W11 = W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[27] + W11; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W12 = W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[28] + W12; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W13 = W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[29] + W13; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W14 = 0x00400022U + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[30] + W14; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W15 = 0x00000100U + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3U)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[31] + W15; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3U)) + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[32] +  W0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3U)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[33] +  W1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3U)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[34] +  W2; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3U)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[35] +  W3; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3U)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[36] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3U)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[37] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3U)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[38] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3U)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[39] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3U)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[40] +  W8; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3U)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[41] +  W9; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3U)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[42] + W10; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3U)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[43] + W11; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3U)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[44] + W12; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W13 = W13 + (rotr(W14, 7) ^ rotr(W14, 18) ^ (W14 >> 3U)) + W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[45] + W13; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W14 = W14 + (rotr(W15, 7) ^ rotr(W15, 18) ^ (W15 >> 3U)) + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[46] + W14; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W15 = W15 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3U)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[47] + W15; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3U)) + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10U));
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[48] +  W0; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3U)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10U));
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[49] +  W1; C = C + G; G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + Ma(B, H, A);
	W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3U)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10U));
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[50] +  W2; B = B + F; F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + Ma(A, G, H);
	W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3U)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[51] +  W3; A = A + E; E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + Ma(H, F, G);
	W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3U)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10U)); 
	D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[52] +  W4; H = H + D; D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + Ma(G, E, F);
	W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3U)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10U)); 
	C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[53] +  W5; G = G + C; C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + Ma(F, D, E);
	W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3U)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10U)); 
	B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[54] +  W6; F = F + B; B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + Ma(E, C, D);
	W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3U)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10U)); 
	A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[55] +  W7; E = E + A; A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + Ma(D, B, C);
	W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3U)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10U)); 
	H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[56] +  W8; D = D + H; H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + Ma(C, A, B);
	W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3U)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10U)); 
	G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[57] +  W9; C = C + G;
	W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3U)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10U)); 
	F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[58] + W10; B = B + F;
	W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3U)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10U)); 
	E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[59] + W11; A = A + E;
	W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3U)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10U)); 
	H = H + D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[60] + W12;

	H+=0x5be0cd19U;

#ifdef VECTORS
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