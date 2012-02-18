// DiaKGCN 18-02-2012 - OpenCL kernel by Diapolo
//
// Parts and / or ideas for this kernel are based upon the public-domain poclbm project, the phatk kernel by Phateus and the DiabloMiner kernel by DiabloD3.
// The kernel was rewritten by me (Diapolo) and is still public-domain!

#ifdef VECTORS8
	typedef uint8 u;
#elif defined VECTORS4
	typedef uint4 u;
#elif defined VECTORS2
	typedef uint2 u;
#else
	typedef uint u;
#endif

#ifdef BITALIGN
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#ifdef BFI_INT
		#define ch(x, y, z) amd_bytealign(x, y, z)
		#define ma(x, y, z) amd_bytealign(z ^ x, y, x)
	#else
		#define ch(x, y, z) bitselect(z, y, x)
		#if defined(VECTORS2) || defined(VECTORS4) || defined(VECTORS8)
			// GCN - VEC2 or VEC4 or VEC8
			#define ma(z, x, y) bitselect(z, y, z ^ x)
		#else
			// GCN - no VEC
			#define ma(z, x, y) ch(z ^ x, y, x)
		#endif
	#endif
#else //BITALIGN
	#define ch(x, y, z) (z ^ (x & (y ^ z)))
	#define ma(x, y, z) ((x & z) | (y & (x | z)))
#endif

#define rotr15(n) (rotate(n, 15U) ^ rotate(n, 13U) ^ (n >> 10U))
#define rotr25(n) (rotate(n, 25U) ^ rotate(n, 14U) ^ (n >> 3U))
#define rotr26(n) (rotate(n, 26U) ^ rotate(n, 21U) ^ rotate(n, 7U))
#define rotr30(n) (rotate(n, 30U) ^ rotate(n, 19U) ^ rotate(n, 10U))

__kernel
	__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
	void search(	
			#ifndef GOFFSET
			const u base,
			#endif
			const uint PreVal0, const uint PreVal4,
			const uint H1, const uint D1A, const uint B1, const uint C1,
			const uint F1, const uint G1, const uint C1addK5, const uint B1addK6, const uint PreVal0addK7,
			const uint W16addK16, const uint W17addK17,
			const uint PreW18, const uint PreW19,
			const uint W16, const uint W17,
			const uint PreW31, const uint PreW32,
			const uint state0, const uint state1, const uint state2, const uint state3,
			const uint state4, const uint state5, const uint state6, const uint state7,
			const uint state0A, const uint state0B,
			const uint state1A, const uint state2A, const uint state3A, const uint state4A,
			const uint state5A, const uint state6A, const uint state7A,
			__global uint * output)
{
	u W[16];
	u V[8];

#ifdef VECTORS8
	#ifdef GOFFSET
		const u nonce = ((uint)get_global_id(0) << 3) + (u)(0, 1, 2, 3, 4, 5, 6, 7);
	#else
		const u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0) << 3) + ((uint)get_local_id(0) << 3) + base;
	#endif
#elif defined VECTORS4
	#ifdef GOFFSET
		const u nonce = ((uint)get_global_id(0) << 2) + (u)(0, 1, 2, 3);
	#else
		const u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0) << 2) + ((uint)get_local_id(0) << 2) + base;
	#endif
#elif defined VECTORS2
	#ifdef GOFFSET
		const u nonce = ((uint)get_global_id(0) << 1) + (u)(0, 1);
	#else
		const u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0) << 1) + ((uint)get_local_id(0) << 1) + base;
	#endif
#else
	#ifdef GOFFSET
		const u nonce = (uint)get_global_id(0);
	#else
		const u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0)) + (uint)get_local_id(0) + base;
	#endif
#endif

	V[0] = PreVal0 + nonce;
	V[1] = B1;
	V[2] = C1;
	V[3] = D1A;
	V[4] = PreVal4 + nonce;
	V[5] = F1;
	V[6] = G1;
	V[7] = H1;

	V[7] += V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += C1addK5 + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  C1addK5 + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += B1addK6 + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  B1addK6 + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += PreVal0addK7 + nonce + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  PreVal0addK7 + nonce + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0xd807aa98 + V[7] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0xd807aa98 + V[7] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x12835b01 + V[6] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x12835b01 + V[6] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0x243185be + V[5] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x243185be + V[5] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x550c7dc3 + V[4] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x550c7dc3 + V[4] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x72be5d74 + V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x72be5d74 + V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x80deb1fe + V[2] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x80deb1fe + V[2] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x9bdc06a7 + V[1] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x9bdc06a7 + V[1] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0xc19bf3f4 + V[0] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0xc19bf3f4 + V[0] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += W16addK16 + V[7] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  W16addK16 + V[7] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += W17addK17 + V[6] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  W17addK17 + V[6] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

//----------------------------------------------------------------------------------

#ifdef VECTORS8
	 W[0] = PreW18 + (u)(	rotr25(nonce.s0),             rotr25(nonce.s0) ^ 0x2004000, rotr25(nonce.s0) ^ 0x4008000, rotr25(nonce.s0) ^ 0x600C000,
				rotr25(nonce.s0) ^ 0x8010000, rotr25(nonce.s0) ^ 0xa014000, rotr25(nonce.s0) ^ 0xc018000, rotr25(nonce.s0) ^ 0xe01c000);
#elif defined VECTORS4
	 W[0] = PreW18 + (u)(rotr25(nonce.x), rotr25(nonce.x) ^ 0x2004000, rotr25(nonce.x) ^ 0x4008000, rotr25(nonce.x) ^ 0x600C000);
#elif defined VECTORS2
	 W[0] = PreW18 + (u)(rotr25(nonce.x), rotr25(nonce.x) ^ 0x2004000);
#else
	 W[0] = PreW18 + rotr25(nonce);
#endif
	 W[1] = PreW19 + nonce;
	 W[2] = 0x80000000 + rotr15(W[0]);
	 W[3] = rotr15(W[1]);
	 W[4] = 0x00000280 + rotr15(W[2]);
	 W[5] = W16 + rotr15(W[3]);
	 W[6] = W17 + rotr15(W[4]);
	 W[7] = W[0] + rotr15(W[5]);
	 W[8] = W[1] + rotr15(W[6]);
	 W[9] = W[2] + rotr15(W[7]);
	W[10] = W[3] + rotr15(W[8]);
	W[11] = W[4] + rotr15(W[9]);
	W[12] = W[5] + 0x00a00055 + rotr15(W[10]);
	W[13] = W[6] + PreW31 + rotr15(W[11]);
	W[14] = W[7] + PreW32 + rotr15(W[12]);
	W[15] = W[8] + W17 + rotr15(W[13]) + rotr25(W[0]);

	V[1] += 0x0fc19dc6 + V[5] + W[0] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x0fc19dc6 + V[5] + W[0] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x240ca1cc + V[4] + W[1] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x240ca1cc + V[4] + W[1] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x2de92c6f + V[3] + W[2] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x2de92c6f + V[3] + W[2] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x4a7484aa + V[2] + W[3] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x4a7484aa + V[2] + W[3] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x5cb0a9dc + V[1] + W[4] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x5cb0a9dc + V[1] + W[4] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x76f988da + V[0] + W[5] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x76f988da + V[0] + W[5] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x983e5152 + V[7] + W[6] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x983e5152 + V[7] + W[6] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0xa831c66d + V[6] + W[7] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0xa831c66d + V[6] + W[7] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0xb00327c8 + V[5] + W[8] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0xb00327c8 + V[5] + W[8] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0xbf597fc7 + V[4] + W[9] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0xbf597fc7 + V[4] + W[9] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0xc6e00bf3 + V[3] + W[10] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0xc6e00bf3 + V[3] + W[10] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0xd5a79147 + V[2] + W[11] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0xd5a79147 + V[2] + W[11] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x06ca6351 + V[1] + W[12] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x06ca6351 + V[1] + W[12] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x14292967 + V[0] + W[13] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x14292967 + V[0] + W[13] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x27b70a85 + V[7] + W[14] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x27b70a85 + V[7] + W[14] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x2e1b2138 + V[6] + W[15] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x2e1b2138 + V[6] + W[15] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

//----------------------------------------------------------------------------------

	 W[0] =  W[0] +  W[9] + rotr15(W[14]) + rotr25( W[1]);
	 W[1] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[2] =  W[2] + W[11] + rotr15( W[0]) + rotr25( W[3]);
	 W[3] =  W[3] + W[12] + rotr15( W[1]) + rotr25( W[4]);
	 W[4] =  W[4] + W[13] + rotr15( W[2]) + rotr25( W[5]);
	 W[5] =  W[5] + W[14] + rotr15( W[3]) + rotr25( W[6]);
	 W[6] =  W[6] + W[15] + rotr15( W[4]) + rotr25( W[7]);
	 W[7] =  W[7] +  W[0] + rotr15( W[5]) + rotr25( W[8]);
	 W[8] =  W[8] +  W[1] + rotr15( W[6]) + rotr25( W[9]);
	 W[9] =  W[9] +  W[2] + rotr15( W[7]) + rotr25(W[10]);
	W[10] = W[10] +  W[3] + rotr15( W[8]) + rotr25(W[11]);
	W[11] = W[11] +  W[4] + rotr15( W[9]) + rotr25(W[12]);
	W[12] = W[12] +  W[5] + rotr15(W[10]) + rotr25(W[13]);
	W[13] = W[13] +  W[6] + rotr15(W[11]) + rotr25(W[14]);
	W[14] = W[14] +  W[7] + rotr15(W[12]) + rotr25(W[15]);
	W[15] = W[15] +  W[8] + rotr15(W[13]) + rotr25( W[0]);

	V[1] += 0x4d2c6dfc + V[5] + W[0] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x4d2c6dfc + V[5] + W[0] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x53380d13 + V[4] + W[1] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x53380d13 + V[4] + W[1] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x650a7354 + V[3] + W[2] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x650a7354 + V[3] + W[2] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x766a0abb + V[2] + W[3] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x766a0abb + V[2] + W[3] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x81c2c92e + V[1] + W[4] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x81c2c92e + V[1] + W[4] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x92722c85 + V[0] + W[5] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x92722c85 + V[0] + W[5] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0xa2bfe8a1 + V[7] + W[6] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0xa2bfe8a1 + V[7] + W[6] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0xa81a664b + V[6] + W[7] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0xa81a664b + V[6] + W[7] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0xc24b8b70 + V[5] + W[8] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0xc24b8b70 + V[5] + W[8] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0xc76c51a3 + V[4] + W[9] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0xc76c51a3 + V[4] + W[9] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0xd192e819 + V[3] + W[10] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0xd192e819 + V[3] + W[10] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0xd6990624 + V[2] + W[11] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0xd6990624 + V[2] + W[11] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0xf40e3585 + V[1] + W[12] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0xf40e3585 + V[1] + W[12] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x106aa070 + V[0] + W[13] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x106aa070 + V[0] + W[13] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x19a4c116 + V[7] + W[14] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x19a4c116 + V[7] + W[14] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x1e376c08 + V[6] + W[15] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x1e376c08 + V[6] + W[15] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

//----------------------------------------------------------------------------------

	 W[0] =  W[0] +  W[9] + rotr15(W[14]) + rotr25( W[1]);
	 W[1] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[2] =  W[2] + W[11] + rotr15( W[0]) + rotr25( W[3]);
	 W[3] =  W[3] + W[12] + rotr15( W[1]) + rotr25( W[4]);
	 W[4] =  W[4] + W[13] + rotr15( W[2]) + rotr25( W[5]);
	 W[5] =  W[5] + W[14] + rotr15( W[3]) + rotr25( W[6]);
	 W[6] =  W[6] + W[15] + rotr15( W[4]) + rotr25( W[7]);
	 W[7] =  W[7] +  W[0] + rotr15( W[5]) + rotr25( W[8]);
	 W[8] =  W[8] +  W[1] + rotr15( W[6]) + rotr25( W[9]);
	 W[9] =  W[9] +  W[2] + rotr15( W[7]) + rotr25(W[10]);
	W[10] = W[10] +  W[3] + rotr15( W[8]) + rotr25(W[11]);
	W[11] = W[11] +  W[4] + rotr15( W[9]) + rotr25(W[12]);
	W[12] = W[12] +  W[5] + rotr15(W[10]) + rotr25(W[13]);
	W[13] = W[13] +  W[6] + rotr15(W[11]) + rotr25(W[14]);

	V[1] += 0x2748774c + V[5] + W[0] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x2748774c + V[5] + W[0] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x34b0bcb5 + V[4] + W[1] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x34b0bcb5 + V[4] + W[1] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x391c0cb3 + V[3] + W[2] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x391c0cb3 + V[3] + W[2] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x4ed8aa4a + V[2] + W[3] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x4ed8aa4a + V[2] + W[3] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x5b9cca4f + V[1] + W[4] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x5b9cca4f + V[1] + W[4] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x682e6ff3 + V[0] + W[5] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x682e6ff3 + V[0] + W[5] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x748f82ee + V[7] + W[6] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x748f82ee + V[7] + W[6] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x78a5636f + V[6] + W[7] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x78a5636f + V[6] + W[7] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0x84c87814 + V[5] + W[8] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x84c87814 + V[5] + W[8] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x8cc70208 + V[4] + W[9] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x8cc70208 + V[4] + W[9] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x90befffa + V[3] + W[10] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x90befffa + V[3] + W[10] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0xa4506ceb + V[2] + W[11] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0xa4506ceb + V[2] + W[11] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0xbef9a3f7 + V[1] + W[12] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0xbef9a3f7 + V[1] + W[12] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0xc67178f2 + V[0] + W[13] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0xc67178f2 + V[0] + W[13] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

//----------------------------------------------------------------------------------

	W[0] = state0 + V[0];
	W[1] = state1 + V[1];
	W[2] = state2 + V[2];
	W[3] = state3 + V[3];
	W[4] = state4 + V[4];
	W[5] = state5 + V[5];
	W[6] = state6 + V[6];
	W[7] = state7 + V[7];

	// 0x71374491 + 0x1f83d9ab + state1
	const u state1AaddV1 = state1A + V[1];
	// 0xb5c0fbcf + 0x9b05688c + state2
	const u state2AaddV2 = state2A + V[2];
	// 0x510e527f + 0xe9b5dba5 + state3
	const u state3AaddV3 = state3A + V[3];
	// 0x3956c25b + state4
	const u state4AaddV4 = state4A + V[4];
	// 0x59f111f1 + state5
	const u state5AaddV5 = state5A + V[5];
	// 0x923f82a4 + state6
	const u state6AaddV6 = state6A + V[6];
	// 0xab1c5ed5 + state7
	const u state7AaddV7 = state7A + V[7];

	// 0x98c7e2a2 + state0	
	V[3] = state0A + V[0];
	// 0xfc08884d + state0
	V[7] = state0B + V[0];
	V[0] = 0x6a09e667;
	V[1] = 0xbb67ae85;
	V[2] = 0x3c6ef372;
	V[4] = 0x510e527f;
	V[5] = 0x9b05688c;
	V[6] = 0x1f83d9ab;

	V[2] += state1AaddV1 + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  state1AaddV1 + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += state2AaddV2 + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  state2AaddV2 + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += state3AaddV3 + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  state3AaddV3 + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += state4AaddV4 + V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  state4AaddV4 + V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += state5AaddV5 + V[2] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  state5AaddV5 + V[2] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += state6AaddV6 + V[1] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  state6AaddV6 + V[1] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += state7AaddV7 + V[0] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  state7AaddV7 + V[0] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x5807aa98 + V[7] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x5807aa98 + V[7] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x12835b01 + V[6] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x12835b01 + V[6] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0x243185be + V[5] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x243185be + V[5] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x550c7dc3 + V[4] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x550c7dc3 + V[4] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x72be5d74 + V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x72be5d74 + V[3] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x80deb1fe + V[2] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x80deb1fe + V[2] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x9bdc06a7 + V[1] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x9bdc06a7 + V[1] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0xc19bf274 + V[0] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0xc19bf274 + V[0] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

//----------------------------------------------------------------------------------

	 W[0] = W[0] + rotr25(W[1]);
	 W[1] = W[1] + 0x00a00000 + rotr25(W[2]);
	 W[2] = W[2] + rotr15(W[0]) + rotr25(W[3]);
	 W[3] = W[3] + rotr15(W[1]) + rotr25(W[4]);
	 W[4] = W[4] + rotr15(W[2]) + rotr25(W[5]);
	 W[5] = W[5] + rotr15(W[3]) + rotr25(W[6]);
	 W[6] = W[6] + 0x00000100 + rotr15(W[4]) + rotr25(W[7]);	
	 W[7] = W[7] + W[0] + 0x11002000 + rotr15(W[5]);
	 W[8] = W[1] + 0x80000000 + rotr15(W[6]);	
	 W[9] = W[2] + rotr15(W[7]);
	W[10] = W[3] + rotr15(W[8]);
	W[11] = W[4] + rotr15(W[9]);
	W[12] = W[5] + rotr15(W[10]);
	W[13] = W[6] + rotr15(W[11]);
	W[14] = W[7] + 0x00400022 + rotr15(W[12]);
	W[15] = W[8] + 0x00000100 + rotr15(W[13]) + rotr25(W[0]);

	V[3] += 0xe49b69c1 + V[7] + W[0] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0xe49b69c1 + V[7] + W[0] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0xefbe4786 + V[6] + W[1] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0xefbe4786 + V[6] + W[1] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0x0fc19dc6 + V[5] + W[2] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x0fc19dc6 + V[5] + W[2] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x240ca1cc + V[4] + W[3] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x240ca1cc + V[4] + W[3] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x2de92c6f + V[3] + W[4] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x2de92c6f + V[3] + W[4] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x4a7484aa + V[2] + W[5] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x4a7484aa + V[2] + W[5] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x5cb0a9dc + V[1] + W[6] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x5cb0a9dc + V[1] + W[6] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x76f988da + V[0] + W[7] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x76f988da + V[0] + W[7] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x983e5152 + V[7] + W[8] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x983e5152 + V[7] + W[8] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0xa831c66d + V[6] + W[9] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0xa831c66d + V[6] + W[9] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0xb00327c8 + V[5] + W[10] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0xb00327c8 + V[5] + W[10] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0xbf597fc7 + V[4] + W[11] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0xbf597fc7 + V[4] + W[11] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0xc6e00bf3 + V[3] + W[12] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0xc6e00bf3 + V[3] + W[12] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0xd5a79147 + V[2] + W[13] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0xd5a79147 + V[2] + W[13] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x06ca6351 + V[1] + W[14] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x06ca6351 + V[1] + W[14] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x14292967 + V[0] + W[15] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x14292967 + V[0] + W[15] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

//----------------------------------------------------------------------------------

	 W[0] =  W[0] +  W[9] + rotr15(W[14]) + rotr25( W[1]);
	 W[1] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[2] =  W[2] + W[11] + rotr15( W[0]) + rotr25( W[3]);
	 W[3] =  W[3] + W[12] + rotr15( W[1]) + rotr25( W[4]);
	 W[4] =  W[4] + W[13] + rotr15( W[2]) + rotr25( W[5]);
	 W[5] =  W[5] + W[14] + rotr15( W[3]) + rotr25( W[6]);
	 W[6] =  W[6] + W[15] + rotr15( W[4]) + rotr25( W[7]);
	 W[7] =  W[7] +  W[0] + rotr15( W[5]) + rotr25( W[8]);
	 W[8] =  W[8] +  W[1] + rotr15( W[6]) + rotr25( W[9]);
	 W[9] =  W[9] +  W[2] + rotr15( W[7]) + rotr25(W[10]);
	W[10] = W[10] +  W[3] + rotr15( W[8]) + rotr25(W[11]);
	W[11] = W[11] +  W[4] + rotr15( W[9]) + rotr25(W[12]);
	W[12] = W[12] +  W[5] + rotr15(W[10]) + rotr25(W[13]);
	W[13] = W[13] +  W[6] + rotr15(W[11]) + rotr25(W[14]);
	W[14] = W[14] +  W[7] + rotr15(W[12]) + rotr25(W[15]);
	W[15] = W[15] +  W[8] + rotr15(W[13]) + rotr25( W[0]);

	V[3] += 0x27b70a85 + V[7] + W[0] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x27b70a85 + V[7] + W[0] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x2e1b2138 + V[6] + W[1] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x2e1b2138 + V[6] + W[1] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0x4d2c6dfc + V[5] + W[2] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x4d2c6dfc + V[5] + W[2] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x53380d13 + V[4] + W[3] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x53380d13 + V[4] + W[3] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x650a7354 + V[3] + W[4] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x650a7354 + V[3] + W[4] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x766a0abb + V[2] + W[5] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x766a0abb + V[2] + W[5] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x81c2c92e + V[1] + W[6] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x81c2c92e + V[1] + W[6] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x92722c85 + V[0] + W[7] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x92722c85 + V[0] + W[7] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0xa2bfe8a1 + V[7] + W[8] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0xa2bfe8a1 + V[7] + W[8] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0xa81a664b + V[6] + W[9] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0xa81a664b + V[6] + W[9] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0xc24b8b70 + V[5] + W[10] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0xc24b8b70 + V[5] + W[10] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0xc76c51a3 + V[4] + W[11] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0xc76c51a3 + V[4] + W[11] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0xd192e819 + V[3] + W[12] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0xd192e819 + V[3] + W[12] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0xd6990624 + V[2] + W[13] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0xd6990624 + V[2] + W[13] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0xf40e3585 + V[1] + W[14] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0xf40e3585 + V[1] + W[14] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x106aa070 + V[0] + W[15] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x106aa070 + V[0] + W[15] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

//----------------------------------------------------------------------------------

	 W[0] =  W[0] +  W[9] + rotr15(W[14]) + rotr25( W[1]);
	 W[1] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[2] =  W[2] + W[11] + rotr15( W[0]) + rotr25( W[3]);
	 W[3] =  W[3] + W[12] + rotr15( W[1]) + rotr25( W[4]);
	 W[4] =  W[4] + W[13] + rotr15( W[2]) + rotr25( W[5]);
	 W[5] =  W[5] + W[14] + rotr15( W[3]) + rotr25( W[6]);
	 W[6] =  W[6] + W[15] + rotr15( W[4]) + rotr25( W[7]);
	 W[7] =  W[7] +  W[0] + rotr15( W[5]) + rotr25( W[8]);
	 W[8] =  W[8] +  W[1] + rotr15( W[6]) + rotr25( W[9]);
	 W[9] =  W[9] +  W[2] + rotr15( W[7]) + rotr25(W[10]);
	W[10] = W[10] +  W[3] + rotr15( W[8]) + rotr25(W[11]);
	W[11] = W[11] +  W[4] + rotr15( W[9]) + rotr25(W[12]);
	W[12] = W[12] +  W[5] + rotr15(W[10]) + rotr25(W[13]);

	V[3] += 0x19a4c116 + V[7] + W[0] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x19a4c116 + V[7] + W[0] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x1e376c08 + V[6] + W[1] + ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x1e376c08 + V[6] + W[1] + ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + ma(V[0], V[1], V[7]);

	V[1] += 0x2748774c + V[5] + W[2] + ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x2748774c + V[5] + W[2] + ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + ma(V[7], V[0], V[6]);

	V[0] += 0x34b0bcb5 + V[4] + W[3] + ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x34b0bcb5 + V[4] + W[3] + ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + ma(V[6], V[7], V[5]);

	V[7] += 0x391c0cb3 + V[3] + W[4] + ch(V[0], V[1], V[2]) + rotr26(V[0]);
	V[3] =  0x391c0cb3 + V[3] + W[4] + ch(V[0], V[1], V[2]) + rotr26(V[0]) + rotr30(V[4]) + ma(V[5], V[6], V[4]);

	V[6] += 0x4ed8aa4a + V[2] + W[5] + ch(V[7], V[0], V[1]) + rotr26(V[7]);
	V[2] =  0x4ed8aa4a + V[2] + W[5] + ch(V[7], V[0], V[1]) + rotr26(V[7]) + rotr30(V[3]) + ma(V[4], V[5], V[3]);

	V[5] += 0x5b9cca4f + V[1] + W[6] + ch(V[6], V[7], V[0]) + rotr26(V[6]);
	V[1] =  0x5b9cca4f + V[1] + W[6] + ch(V[6], V[7], V[0]) + rotr26(V[6]) + rotr30(V[2]) + ma(V[3], V[4], V[2]);

	V[4] += 0x682e6ff3 + V[0] + W[7] + ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  0x682e6ff3 + V[0] + W[7] + ch(V[5], V[6], V[7]) + rotr26(V[5]) + rotr30(V[1]) + ma(V[2], V[3], V[1]);

	V[3] += 0x748f82ee + V[7] + W[8] + ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0x748f82ee + V[7] + W[8] + ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + ma(V[1], V[2], V[0]);

	V[2] += 0x78a5636f + V[6] + W[9] + ch(V[3], V[4], V[5]) + rotr26(V[3]);

	V[1] += 0x84c87814 + V[5] + W[10] + ch(V[2], V[3], V[4]) + rotr26(V[2]);

	V[0] += 0x8cc70208 + V[4] + W[11] + ch(V[1], V[2], V[3]) + rotr26(V[1]);

	V[7] += V[3] + W[12] + ch(V[0], V[1], V[2]) + rotr26(V[0]);


#define FOUND (0x80)
#define NFLAG (0x7F)

#ifdef VECTORS8
	V[7] ^= 0x136032ed;

	bool result = V[7].s0 & V[7].s1 & V[7].s2 & V[7].s3 & V[7].s4 & V[7].s5 & V[7].s6 & V[7].s7;

	if (!result) {
		if (!V[7].s0)
			output[FOUND] = output[NFLAG & nonce.s0] = nonce.s0;
		if (!V[7].s1)
			output[FOUND] = output[NFLAG & nonce.s1] = nonce.s1;
		if (!V[7].s2)
			output[FOUND] = output[NFLAG & nonce.s2] = nonce.s2;
		if (!V[7].s3)
			output[FOUND] = output[NFLAG & nonce.s3] = nonce.s3;
		if (!V[7].s4)
			output[FOUND] = output[NFLAG & nonce.s4] = nonce.s4;
		if (!V[7].s5)
			output[FOUND] = output[NFLAG & nonce.s5] = nonce.s5;
		if (!V[7].s6)
			output[FOUND] = output[NFLAG & nonce.s6] = nonce.s6;
		if (!V[7].s7)
			output[FOUND] = output[NFLAG & nonce.s7] = nonce.s7;
	}
#elif defined VECTORS4
	V[7] ^= 0x136032ed;

	bool result = V[7].x & V[7].y & V[7].z & V[7].w;

	if (!result) {
		if (!V[7].x)
			output[FOUND] = output[NFLAG & nonce.x] = nonce.x;
		if (!V[7].y)
			output[FOUND] = output[NFLAG & nonce.y] = nonce.y;
		if (!V[7].z)
			output[FOUND] = output[NFLAG & nonce.z] = nonce.z;
		if (!V[7].w)
			output[FOUND] = output[NFLAG & nonce.w] = nonce.w;
	}
#elif defined VECTORS2
	V[7] ^= 0x136032ed;

	bool result = V[7].x & V[7].y;

	if (!result) {
		if (!V[7].x)
			output[FOUND] = output[NFLAG & nonce.x] = nonce.x;
		if (!V[7].y)
			output[FOUND] = output[NFLAG & nonce.y] = nonce.y;
	}
#else
	if (V[7] == 0x136032ed)
		output[FOUND] = output[NFLAG & nonce] = nonce;
#endif
}
