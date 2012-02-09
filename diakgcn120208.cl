// DiaKGCN 09-02-2012 - OpenCL kernel by Diapolo
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

#ifdef BFI_INT
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#define Ch(x, y, z) amd_bytealign(x, y, z)
	#define Ma(x, y, z) amd_bytealign(z ^ x, y, x)
#else
	#define Ch(x, y, z) bitselect(z, y, x)
	#if defined(VECTORS2) || defined(VECTORS4) || defined(VECTORS8)
		// GCN - VEC2 or VEC4
		#define Ma(z, x, y) bitselect(z, y, z ^ x)
	#else
		// GCN - no VEC
		#define Ma(z, x, y) Ch(z ^ x, y, x)
	#endif
#endif

#ifdef GOFFSET
	// make sure kernel parameter "base" is not used, if GOFFSET is defined
	#define BASE
#else
	// make sure kernel parameter "base" is used, if GOFFSET is not defined
	#define BASE const u base,
#endif

#define ch(n) Ch(V[(4 + 128 - n) % 8], V[(5 + 128 - n) % 8], V[(6 + 128 - n) % 8])
#define ma(n) Ma(V[(1 + 128 - n) % 8], V[(2 + 128 - n) % 8], V[(0 + 128 - n) % 8])

#define rotr15(n) (rotate(n, 15U) ^ rotate(n, 13U) ^ (n >> 10U))
#define rotr25(n) (rotate(n, 25U) ^ rotate(n, 14U) ^ (n >> 3U))
#define rotr26(n) (rotate(n, 26U) ^ rotate(n, 21U) ^ rotate(n, 7U))
#define rotr30(n) (rotate(n, 30U) ^ rotate(n, 19U) ^ rotate(n, 10U))

__kernel
	__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
	void search(	BASE
			const uint PreVal4,
			const uint H1, const uint D1, const uint PreVal0, const uint B1, const uint C1,
			const uint F1, const uint G1, const uint C1addK5, const uint B1addK6, const uint PreVal0addK7,
			const uint W16addK16, const uint W17addK17,
			const uint PreW18, const uint PreW19,
			const uint W16, const uint W17,
			const uint PreW31, const uint PreW32,
			const uint state0, const uint state1, const uint state2, const uint state3,
			const uint state4, const uint state5, const uint state6, const uint state7,
			const uint state0A, const uint state0B,
			__global int * output)
{
	u W[17];
	u V[8];

#ifdef VECTORS8
	#ifdef GOFFSET
		u nonce = ((uint)get_global_id(0) << 3) + (u)(0, 1, 2, 3, 4, 5, 6, 7);
	#else
		u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0) << 3) + ((uint)get_local_id(0) << 3) + base;
	#endif
#elif defined VECTORS4
	#ifdef GOFFSET
		u nonce = ((uint)get_global_id(0) << 2) + (u)(0, 1, 2, 3);
	#else
		u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0) << 2) + ((uint)get_local_id(0) << 2) + base;
	#endif
#elif defined VECTORS2
	#ifdef GOFFSET
		u nonce = ((uint)get_global_id(0) << 1) + (u)(0, 1);
	#else
		u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0) << 1) + ((uint)get_local_id(0) << 1) + base;
	#endif
#else
	#ifdef GOFFSET
		u nonce = (uint)get_global_id(0);
	#else
		u nonce = ((uint)get_group_id(0) * (uint)get_local_size(0)) + (uint)get_local_id(0) + base;
	#endif
#endif

	V[4] = PreVal4 + nonce;

	V[7] = H1 + (V[3] = D1 + Ch((PreVal0 + nonce), B1, C1) + rotr26(PreVal0 + nonce));
	V[3] += rotr30(V[4]) + Ma(F1, G1, V[4]);

	V[6] = G1 + (V[2] = C1addK5 + Ch(V[7], (PreVal0 + nonce), B1) + rotr26(V[7]));
	V[2] += rotr30(V[3]) + Ma(V[4], F1, V[3]);

	V[5] = F1 + (V[1] = B1addK6 + Ch(V[6], V[7], (PreVal0 + nonce)) + rotr26(V[6]));
	V[1] += rotr30(V[2]) + Ma(V[3], V[4], V[2]);

	V[4] += nonce + PreVal0addK7 + Ch(V[5], V[6], V[7]) + rotr26(V[5]);
	V[0] =  nonce + PreVal0addK7 + Ch(V[5], V[6], V[7]) + rotr26(V[5]) +rotr30(V[1]) + Ma(V[2], V[3], V[1]);

	V[3] += 0xd807aa98 + V[7] + Ch(V[4], V[5], V[6]) + rotr26(V[4]);
	V[7] =  0xd807aa98 + V[7] + Ch(V[4], V[5], V[6]) + rotr26(V[4]) + rotr30(V[0]) + Ma(V[1], V[2], V[0]);

	V[2] += 0x12835b01 + V[6] + Ch(V[3], V[4], V[5]) + rotr26(V[3]);
	V[6] =  0x12835b01 + V[6] + Ch(V[3], V[4], V[5]) + rotr26(V[3]) + rotr30(V[7]) + Ma(V[0], V[1], V[7]);

	V[1] += 0x243185be + V[5] + Ch(V[2], V[3], V[4]) + rotr26(V[2]);
	V[5] =  0x243185be + V[5] + Ch(V[2], V[3], V[4]) + rotr26(V[2]) + rotr30(V[6]) + Ma(V[7], V[0], V[6]);

	V[0] += 0x550c7dc3 + V[4] + Ch(V[1], V[2], V[3]) + rotr26(V[1]);
	V[4] =  0x550c7dc3 + V[4] + Ch(V[1], V[2], V[3]) + rotr26(V[1]) + rotr30(V[5]) + Ma(V[6], V[7], V[5]);

//--------------- ch() + ma() replaced above ---------------

	V[7] += 0x72be5d74 + V[3] + ch(12) + rotr26(V[0]);
	V[3] =  0x72be5d74 + V[3] + ch(12) + rotr26(V[0]) + rotr30(V[4]) + ma(12);

	V[6] += 0x80deb1fe + V[2] + ch(13) + rotr26(V[7]);
	V[2] =  0x80deb1fe + V[2] + ch(13) + rotr26(V[7]) + rotr30(V[3]) + ma(13);

	V[5] += 0x9bdc06a7 + V[1] + ch(14) + rotr26(V[6]);
	V[1] =  0x9bdc06a7 + V[1] + ch(14) + rotr26(V[6]) + rotr30(V[2]) + ma(14);

	V[4] += 0xc19bf3f4 + V[0] + ch(15) + rotr26(V[5]);
	V[0] =  0xc19bf3f4 + V[0] + ch(15) + rotr26(V[5]) + rotr30(V[1]) + ma(15);

	V[3] += W16addK16 + V[7] + ch(16) + rotr26(V[4]);
	V[7] =  W16addK16 + V[7] + ch(16) + rotr26(V[4]) + rotr30(V[0]) + ma(16);

	V[2] += W17addK17 + V[6] + ch(17) + rotr26(V[3]);
	V[6] =  W17addK17 + V[6] + ch(17) + rotr26(V[3]) + rotr30(V[7]) + ma(17);

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
	W[12] = 0x00a00055 + W[5] + rotr15(W[10]);
	W[13] = PreW31 + W[6] + rotr15(W[11]);
	W[14] = PreW32 + W[7] + rotr15(W[12]);
	W[15] = W17 + W[8] + rotr15(W[13]) + rotr25(W[0]);
	W[16] = W[0] + W[9] + rotr15(W[14]) + rotr25(W[1]);

	V[1] += 0x0fc19dc6 + V[5] + W[0] + ch(18) + rotr26(V[2]);
	V[5] =  0x0fc19dc6 + V[5] + W[0] + ch(18) + rotr26(V[2]) + rotr30(V[6]) + ma(18);

	V[0] += 0x240ca1cc + V[4] + W[1] + ch(19) + rotr26(V[1]);
	V[4] =  0x240ca1cc + V[4] + W[1] + ch(19) + rotr26(V[1]) + rotr30(V[5]) + ma(19);

	V[7] += 0x2de92c6f + V[3] + W[2] + ch(20) + rotr26(V[0]);
	V[3] =  0x2de92c6f + V[3] + W[2] + ch(20) + rotr26(V[0]) + rotr30(V[4]) + ma(20);

	V[6] += 0x4a7484aa + V[2] + W[3] + ch(21) + rotr26(V[7]);
	V[2] =  0x4a7484aa + V[2] + W[3] + ch(21) + rotr26(V[7]) + rotr30(V[3]) + ma(21);

	V[5] += 0x5cb0a9dc + V[1] + W[4] + ch(22) + rotr26(V[6]);
	V[1] =  0x5cb0a9dc + V[1] + W[4] + ch(22) + rotr26(V[6]) + rotr30(V[2]) + ma(22);

	V[4] += 0x76f988da + V[0] + W[5] + ch(23) + rotr26(V[5]);
	V[0] =  0x76f988da + V[0] + W[5] + ch(23) + rotr26(V[5]) + rotr30(V[1]) + ma(23);

	V[3] += 0x983e5152 + V[7] + W[6] + ch(24) + rotr26(V[4]);
	V[7] =  0x983e5152 + V[7] + W[6] + ch(24) + rotr26(V[4]) + rotr30(V[0]) + ma(24);

	V[2] += 0xa831c66d + V[6] + W[7] + ch(25) + rotr26(V[3]);
	V[6] =  0xa831c66d + V[6] + W[7] + ch(25) + rotr26(V[3]) + rotr30(V[7]) + ma(25);

	V[1] += 0xb00327c8 + V[5] + W[8] + ch(26) + rotr26(V[2]);
	V[5] =  0xb00327c8 + V[5] + W[8] + ch(26) + rotr26(V[2]) + rotr30(V[6]) + ma(26);

	V[0] += 0xbf597fc7 + V[4] + W[9] + ch(27) + rotr26(V[1]);
	V[4] =  0xbf597fc7 + V[4] + W[9] + ch(27) + rotr26(V[1]) + rotr30(V[5]) + ma(27);

	V[7] += 0xc6e00bf3 + V[3] + W[10] + ch(28) + rotr26(V[0]);
	V[3] =  0xc6e00bf3 + V[3] + W[10] + ch(28) + rotr26(V[0]) + rotr30(V[4]) + ma(28);

	V[6] += 0xd5a79147 + V[2] + W[11] + ch(29) + rotr26(V[7]);
	V[2] =  0xd5a79147 + V[2] + W[11] + ch(29) + rotr26(V[7]) + rotr30(V[3]) + ma(29);

	V[5] += 0x06ca6351 + V[1] + W[12] + ch(30) + rotr26(V[6]);
	V[1] =  0x06ca6351 + V[1] + W[12] + ch(30) + rotr26(V[6]) + rotr30(V[2]) + ma(30);

	V[4] += 0x14292967 + V[0] + W[13] + ch(31) + rotr26(V[5]);
	V[0] =  0x14292967 + V[0] + W[13] + ch(31) + rotr26(V[5]) + rotr30(V[1]) + ma(31);

	V[3] += 0x27b70a85 + V[7] + W[14] + ch(32) + rotr26(V[4]);
	V[7] =  0x27b70a85 + V[7] + W[14] + ch(32) + rotr26(V[4]) + rotr30(V[0]) + ma(32);

	V[2] += 0x2e1b2138 + V[6] + W[15] + ch(33) + rotr26(V[3]);
	V[6] =  0x2e1b2138 + V[6] + W[15] + ch(33) + rotr26(V[3]) + rotr30(V[7]) + ma(33);

	V[1] += 0x4d2c6dfc + V[5] + W[16] + ch(34) + rotr26(V[2]);
	V[5] =  0x4d2c6dfc + V[5] + W[16] + ch(34) + rotr26(V[2]) + rotr30(V[6]) + ma(34);

//----------------------------------------------------------------------------------

	 W[0] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[1] =  W[2] + W[11] + rotr15(W[16]) + rotr25( W[3]);
	 W[2] =  W[3] + W[12] + rotr15( W[0]) + rotr25( W[4]);
	 W[3] =  W[4] + W[13] + rotr15( W[1]) + rotr25( W[5]);
	 W[4] =  W[5] + W[14] + rotr15( W[2]) + rotr25( W[6]);
	 W[5] =  W[6] + W[15] + rotr15( W[3]) + rotr25( W[7]);
	 W[6] =  W[7] + W[16] + rotr15( W[4]) + rotr25( W[8]);
	 W[7] =  W[8] +  W[0] + rotr15( W[5]) + rotr25( W[9]);
	 W[8] =  W[9] +  W[1] + rotr15( W[6]) + rotr25(W[10]);
	 W[9] = W[10] +  W[2] + rotr15( W[7]) + rotr25(W[11]);
	W[10] = W[11] +  W[3] + rotr15( W[8]) + rotr25(W[12]);
	W[11] = W[12] +  W[4] + rotr15( W[9]) + rotr25(W[13]);
	W[12] = W[13] +  W[5] + rotr15(W[10]) + rotr25(W[14]);
	W[13] = W[14] +  W[6] + rotr15(W[11]) + rotr25(W[15]);
	W[14] = W[15] +  W[7] + rotr15(W[12]) + rotr25(W[16]);
	W[15] = W[16] +  W[8] + rotr15(W[13]) + rotr25( W[0]);
	W[16] =  W[0] +  W[9] + rotr15(W[14]) + rotr25( W[1]);

	V[0] += 0x53380d13 + V[4] + W[0] + ch(35) + rotr26(V[1]);
	V[4] =  0x53380d13 + V[4] + W[0] + ch(35) + rotr26(V[1]) + rotr30(V[5]) + ma(35);

	V[7] += 0x650a7354 + V[3] + W[1] + ch(36) + rotr26(V[0]);
	V[3] =  0x650a7354 + V[3] + W[1] + ch(36) + rotr26(V[0]) + rotr30(V[4]) + ma(36);

	V[6] += 0x766a0abb + V[2] + W[2] + ch(37) + rotr26(V[7]);
	V[2] =  0x766a0abb + V[2] + W[2] + ch(37) + rotr26(V[7]) + rotr30(V[3]) + ma(37);

	V[5] += 0x81c2c92e + V[1] + W[3] + ch(38) + rotr26(V[6]);
	V[1] =  0x81c2c92e + V[1] + W[3] + ch(38) + rotr26(V[6]) + rotr30(V[2]) + ma(38);

	V[4] += 0x92722c85 + V[0] + W[4] + ch(39) + rotr26(V[5]);
	V[0] =  0x92722c85 + V[0] + W[4] + ch(39) + rotr26(V[5]) + rotr30(V[1]) + ma(39);

	V[3] += 0xa2bfe8a1 + V[7] + W[5] + ch(40) + rotr26(V[4]);
	V[7] =  0xa2bfe8a1 + V[7] + W[5] + ch(40) + rotr26(V[4]) + rotr30(V[0]) + ma(40);

	V[2] += 0xa81a664b + V[6] + W[6] + ch(41) + rotr26(V[3]);
	V[6] =  0xa81a664b + V[6] + W[6] + ch(41) + rotr26(V[3]) + rotr30(V[7]) + ma(41);

	V[1] += 0xc24b8b70 + V[5] + W[7] + ch(42) + rotr26(V[2]);
	V[5] =  0xc24b8b70 + V[5] + W[7] + ch(42) + rotr26(V[2]) + rotr30(V[6]) + ma(42);

	V[0] += 0xc76c51a3 + V[4] + W[8] + ch(43) + rotr26(V[1]);
	V[4] =  0xc76c51a3 + V[4] + W[8] + ch(43) + rotr26(V[1]) + rotr30(V[5]) + ma(43);

	V[7] += 0xd192e819 + V[3] + W[9] + ch(44) + rotr26(V[0]);
	V[3] =  0xd192e819 + V[3] + W[9] + ch(44) + rotr26(V[0]) + rotr30(V[4]) + ma(44);

	V[6] += 0xd6990624 + V[2] + W[10] + ch(45) + rotr26(V[7]);
	V[2] =  0xd6990624 + V[2] + W[10] + ch(45) + rotr26(V[7]) + rotr30(V[3]) + ma(45);

	V[5] += 0xf40e3585 + V[1] + W[11] + ch(46) + rotr26(V[6]);
	V[1] =  0xf40e3585 + V[1] + W[11] + ch(46) + rotr26(V[6]) + rotr30(V[2]) + ma(46);

	V[4] += 0x106aa070 + V[0] + W[12] + ch(47) + rotr26(V[5]);
	V[0] =  0x106aa070 + V[0] + W[12] + ch(47) + rotr26(V[5]) + rotr30(V[1]) + ma(47);

	V[3] += 0x19a4c116 + V[7] + W[13] + ch(48) + rotr26(V[4]);
	V[7] =  0x19a4c116 + V[7] + W[13] + ch(48) + rotr26(V[4]) + rotr30(V[0]) + ma(48);

	V[2] += 0x1e376c08 + V[6] + W[14] + ch(49) + rotr26(V[3]);
	V[6] =  0x1e376c08 + V[6] + W[14] + ch(49) + rotr26(V[3]) + rotr30(V[7]) + ma(49);

	V[1] += 0x2748774c + V[5] + W[15] + ch(50) + rotr26(V[2]);
	V[5] =  0x2748774c + V[5] + W[15] + ch(50) + rotr26(V[2]) + rotr30(V[6]) + ma(50);

	V[0] += 0x34b0bcb5 + V[4] + W[16] + ch(51) + rotr26(V[1]);
	V[4] =  0x34b0bcb5 + V[4] + W[16] + ch(51) + rotr26(V[1]) + rotr30(V[5]) + ma(51);

//----------------------------------------------------------------------------------

	 W[0] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[1] =  W[2] + W[11] + rotr15(W[16]) + rotr25( W[3]);
	 W[2] =  W[3] + W[12] + rotr15( W[0]) + rotr25( W[4]);
	 W[3] =  W[4] + W[13] + rotr15( W[1]) + rotr25( W[5]);
	 W[4] =  W[5] + W[14] + rotr15( W[2]) + rotr25( W[6]);
	 W[5] =  W[6] + W[15] + rotr15( W[3]) + rotr25( W[7]);
	 W[6] =  W[7] + W[16] + rotr15( W[4]) + rotr25( W[8]);
	 W[7] =  W[8] +  W[0] + rotr15( W[5]) + rotr25( W[9]);
	 W[8] =  W[9] +  W[1] + rotr15( W[6]) + rotr25(W[10]);
	 W[9] = W[10] +  W[2] + rotr15( W[7]) + rotr25(W[11]);
	W[10] = W[11] +  W[3] + rotr15( W[8]) + rotr25(W[12]);
	W[11] = W[12] +  W[4] + rotr15( W[9]) + rotr25(W[13]);

	V[7] += 0x391c0cb3 + V[3] + W[0] + ch(52) + rotr26(V[0]);
	V[3] =  0x391c0cb3 + V[3] + W[0] + ch(52) + rotr26(V[0]) + rotr30(V[4]) + ma(52);

	V[6] += 0x4ed8aa4a + V[2] + W[1] + ch(53) + rotr26(V[7]);
	V[2] =  0x4ed8aa4a + V[2] + W[1] + ch(53) + rotr26(V[7]) + rotr30(V[3]) + ma(53);

	V[5] += 0x5b9cca4f + V[1] + W[2] + ch(54) + rotr26(V[6]);
	V[1] =  0x5b9cca4f + V[1] + W[2] + ch(54) + rotr26(V[6]) + rotr30(V[2]) + ma(54);

	V[4] += 0x682e6ff3 + V[0] + W[3] + ch(55) + rotr26(V[5]);
	V[0] =  0x682e6ff3 + V[0] + W[3] + ch(55) + rotr26(V[5]) + rotr30(V[1]) + ma(55);

	V[3] += 0x748f82ee + V[7] + W[4] + ch(56) + rotr26(V[4]);
	V[7] =  0x748f82ee + V[7] + W[4] + ch(56) + rotr26(V[4]) + rotr30(V[0]) + ma(56);

	V[2] += 0x78a5636f + V[6] + W[5] + ch(57) + rotr26(V[3]);
	V[6] =  0x78a5636f + V[6] + W[5] + ch(57) + rotr26(V[3]) + rotr30(V[7]) + ma(57);

	V[1] += 0x84c87814 + V[5] + W[6] + ch(58) + rotr26(V[2]);
	V[5] =  0x84c87814 + V[5] + W[6] + ch(58) + rotr26(V[2]) + rotr30(V[6]) + ma(58);

	V[0] += 0x8cc70208 + V[4] + W[7] + ch(59) + rotr26(V[1]);
	V[4] =  0x8cc70208 + V[4] + W[7] + ch(59) + rotr26(V[1]) + rotr30(V[5]) + ma(59);

	V[7] += 0x90befffa + V[3] + W[8] + ch(60) + rotr26(V[0]);
	V[3] =  0x90befffa + V[3] + W[8] + ch(60) + rotr26(V[0]) + rotr30(V[4]) + ma(60);

	V[6] += 0xa4506ceb + V[2] + W[9] + ch(61) + rotr26(V[7]);
	V[2] =  0xa4506ceb + V[2] + W[9] + ch(61) + rotr26(V[7]) + rotr30(V[3]) + ma(61);

	V[5] += 0xbef9a3f7 + V[1] + W[10] + ch(62) + rotr26(V[6]);
	V[1] =  0xbef9a3f7 + V[1] + W[10] + ch(62) + rotr26(V[6]) + rotr30(V[2]) + ma(62);

	V[4] += 0xc67178f2 + V[0] + W[11] + ch(63) + rotr26(V[5]);
	V[0] =  0xc67178f2 + V[0] + W[11] + ch(63) + rotr26(V[5]) + rotr30(V[1]) + ma(63);

//----------------------------------------------------------------------------------

	W[0] = state0 + V[0];
	W[1] = state1 + V[1];
	W[2] = state2 + V[2];
	W[3] = state3 + V[3];
	W[4] = state4 + V[4];
	W[5] = state5 + V[5];
	W[6] = state6 + V[6];
	W[7] = state7 + V[7];

	// 0x98c7e2a2 + W[0]
	u state0AaddV0 = state0A + V[0];
	// 0xfc08884d + W[0]
	u state0BaddV0 = state0B + V[0];

	V[2] = 0x3c6ef372 + (V[6] = 0x90bb1e3c + W[1] + Ch(state0AaddV0, 0x510e527fU, 0x9b05688cU) + rotr26(state0AaddV0));
	V[6] += rotr30(state0BaddV0) + Ma(0x6a09e667U, 0xbb67ae85U, state0BaddV0);
		
	V[1] = 0xbb67ae85 + (V[5] = 0x50c6645b + W[2] + Ch(V[2], state0AaddV0, 0x510e527fU) + rotr26(V[2]));
	V[5] += rotr30(V[6]) + Ma(state0BaddV0, 0x6a09e667U, V[6]);

	V[0] = 0x6a09e667 + (V[4] = 0x3ac42e24 + W[3] + Ch(V[1], V[2], state0AaddV0) + rotr26(V[1]));
	V[4] += rotr30(V[5]) + Ma(V[6], state0BaddV0, V[5]);

	V[7] = (state0BaddV0) + (V[3] = 0x3956c25b + state0AaddV0 + W[4] + Ch(V[0], V[1], V[2]) + rotr26(V[0]));
	V[3] += rotr30(V[4]) + Ma(V[5], V[6], V[4]);

//--------------- ch() + ma() replaced above ---------------

	V[6] += 0x59f111f1 + V[2] + W[5] + ch(69) + rotr26(V[7]);
	V[2] =  0x59f111f1 + V[2] + W[5] + ch(69) + rotr26(V[7]) + rotr30(V[3]) + ma(69);

	V[5] += 0x923f82a4 + V[1] + W[6] + ch(70) + rotr26(V[6]);
	V[1] =  0x923f82a4 + V[1] + W[6] + ch(70) + rotr26(V[6]) + rotr30(V[2]) + ma(70);

	V[4] += 0xab1c5ed5 + V[0] + W[7] + ch(71) + rotr26(V[5]);
	V[0] =  0xab1c5ed5 + V[0] + W[7] + ch(71) + rotr26(V[5]) + rotr30(V[1]) + ma(71);

	V[3] += 0x5807aa98 + V[7] + ch(72) + rotr26(V[4]);
	V[7] =  0x5807aa98 + V[7] + ch(72) + rotr26(V[4]) + rotr30(V[0]) + ma(72);

	V[2] += 0x12835b01 + V[6] + ch(73) + rotr26(V[3]);
	V[6] =  0x12835b01 + V[6] + ch(73) + rotr26(V[3]) + rotr30(V[7]) + ma(73);

	V[1] += 0x243185be + V[5] + ch(74) + rotr26(V[2]);
	V[5] =  0x243185be + V[5] + ch(74) + rotr26(V[2]) + rotr30(V[6]) + ma(74);

	V[0] += 0x550c7dc3 + V[4] + ch(75) + rotr26(V[1]);
	V[4] =  0x550c7dc3 + V[4] + ch(75) + rotr26(V[1]) + rotr30(V[5]) + ma(75);

	V[7] += 0x72be5d74 + V[3] + ch(76) + rotr26(V[0]);
	V[3] =  0x72be5d74 + V[3] + ch(76) + rotr26(V[0]) + rotr30(V[4]) + ma(76);

	V[6] += 0x80deb1fe + V[2] + ch(77) + rotr26(V[7]);
	V[2] =  0x80deb1fe + V[2] + ch(77) + rotr26(V[7]) + rotr30(V[3]) + ma(77);

	V[5] += 0x9bdc06a7 + V[1] + ch(78) + rotr26(V[6]);
	V[1] =  0x9bdc06a7 + V[1] + ch(78) + rotr26(V[6]) + rotr30(V[2]) + ma(78);

	V[4] += 0xc19bf274 + V[0] + ch(79) + rotr26(V[5]);
	V[0] =  0xc19bf274 + V[0] + ch(79) + rotr26(V[5]) + rotr30(V[1]) + ma(79);

//----------------------------------------------------------------------------------

	 W[0] = W[0] + rotr25(W[1]);
	 W[1] = 0x00a00000 + W[1] + rotr25(W[2]);
	 W[2] = W[2] + rotr15(W[0]) + rotr25(W[3]);
	 W[3] = W[3] + rotr15(W[1]) + rotr25(W[4]);
	 W[4] = W[4] + rotr15(W[2]) + rotr25(W[5]);
	 W[5] = W[5] + rotr15(W[3]) + rotr25(W[6]);
	 W[6] = 0x00000100 + W[6] + rotr15(W[4]) + rotr25(W[7]);	
	 W[7] = 0x11002000 + W[7] + W[0] + rotr15(W[5]);
	 W[8] = 0x80000000 + W[1] + rotr15(W[6]);	
	 W[9] = W[2] + rotr15(W[7]);
	W[10] = W[3] + rotr15(W[8]);
	W[11] = W[4] + rotr15(W[9]);
	W[12] = W[5] + rotr15(W[10]);
	W[13] = W[6] + rotr15(W[11]);
	W[14] = 0x00400022 + W[7] + rotr15( W[12]);
	W[15] = 0x00000100 + W[8] + rotr15( W[13]) + rotr25(W[0]);
	W[16] = W[0] + W[9] + rotr15( W[14]) + rotr25(W[1]);

	V[3] += 0xe49b69c1 + V[7] + W[0] + ch(80) + rotr26(V[4]);
	V[7] =  0xe49b69c1 + V[7] + W[0] + ch(80) + rotr26(V[4]) + rotr30(V[0]) + ma(80);

	V[2] += 0xefbe4786 + V[6] + W[1] + ch(81) + rotr26(V[3]);
	V[6] =  0xefbe4786 + V[6] + W[1] + ch(81) + rotr26(V[3]) + rotr30(V[7]) + ma(81);

	V[1] += 0x0fc19dc6 + V[5] + W[2] + ch(82) + rotr26(V[2]);
	V[5] =  0x0fc19dc6 + V[5] + W[2] + ch(82) + rotr26(V[2]) + rotr30(V[6]) + ma(82);

	V[0] += 0x240ca1cc + V[4] + W[3] + ch(83) + rotr26(V[1]);
	V[4] =  0x240ca1cc + V[4] + W[3] + ch(83) + rotr26(V[1]) + rotr30(V[5]) + ma(83);

	V[7] += 0x2de92c6f + V[3] + W[4] + ch(84) + rotr26(V[0]);
	V[3] =  0x2de92c6f + V[3] + W[4] + ch(84) + rotr26(V[0]) + rotr30(V[4]) + ma(84);

	V[6] += 0x4a7484aa + V[2] + W[5] + ch(85) + rotr26(V[7]);
	V[2] =  0x4a7484aa + V[2] + W[5] + ch(85) + rotr26(V[7]) + rotr30(V[3]) + ma(85);

	V[5] += 0x5cb0a9dc + V[1] + W[6] + ch(86) + rotr26(V[6]);
	V[1] =  0x5cb0a9dc + V[1] + W[6] + ch(86) + rotr26(V[6]) + rotr30(V[2]) + ma(86);

	V[4] += 0x76f988da + V[0] + W[7] + ch(87) + rotr26(V[5]);
	V[0] =  0x76f988da + V[0] + W[7] + ch(87) + rotr26(V[5]) + rotr30(V[1]) + ma(87);

	V[3] += 0x983e5152 + V[7] + W[8] + ch(88) + rotr26(V[4]);
	V[7] =  0x983e5152 + V[7] + W[8] + ch(88) + rotr26(V[4]) + rotr30(V[0]) + ma(88);

	V[2] += 0xa831c66d + V[6] + W[9] + ch(89) + rotr26(V[3]);
	V[6] =  0xa831c66d + V[6] + W[9] + ch(89) + rotr26(V[3]) + rotr30(V[7]) + ma(89);

	V[1] += 0xb00327c8 + V[5] + W[10] + ch(90) + rotr26(V[2]);
	V[5] =  0xb00327c8 + V[5] + W[10] + ch(90) + rotr26(V[2]) + rotr30(V[6]) + ma(90);

	V[0] += 0xbf597fc7 + V[4] + W[11] + ch(91) + rotr26(V[1]);
	V[4] =  0xbf597fc7 + V[4] + W[11] + ch(91) + rotr26(V[1]) + rotr30(V[5]) + ma(91);

	V[7] += 0xc6e00bf3 + V[3] + W[12] + ch(92) + rotr26(V[0]);
	V[3] =  0xc6e00bf3 + V[3] + W[12] + ch(92) + rotr26(V[0]) + rotr30(V[4]) + ma(92);

	V[6] += 0xd5a79147 + V[2] + W[13] + ch(93) + rotr26(V[7]);
	V[2] =  0xd5a79147 + V[2] + W[13] + ch(93) + rotr26(V[7]) + rotr30(V[3]) + ma(93);

	V[5] += 0x06ca6351 + V[1] + W[14] + ch(94) + rotr26(V[6]);
	V[1] =  0x06ca6351 + V[1] + W[14] + ch(94) + rotr26(V[6]) + rotr30(V[2]) + ma(94);

	V[4] += 0x14292967 + V[0] + W[15] + ch(95) + rotr26(V[5]);
	V[0] =  0x14292967 + V[0] + W[15] + ch(95) + rotr26(V[5]) + rotr30(V[1]) + ma(95);

	V[3] += 0x27b70a85 + V[7] + W[16] + ch(96) + rotr26(V[4]);
	V[7] =  0x27b70a85 + V[7] + W[16] + ch(96) + rotr26(V[4]) + rotr30(V[0]) + ma(96);

//----------------------------------------------------------------------------------

	 W[0] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[1] =  W[2] + W[11] + rotr15(W[16]) + rotr25( W[3]);
	 W[2] =  W[3] + W[12] + rotr15( W[0]) + rotr25( W[4]);
	 W[3] =  W[4] + W[13] + rotr15( W[1]) + rotr25( W[5]);
	 W[4] =  W[5] + W[14] + rotr15( W[2]) + rotr25( W[6]);
	 W[5] =  W[6] + W[15] + rotr15( W[3]) + rotr25( W[7]);
	 W[6] =  W[7] + W[16] + rotr15( W[4]) + rotr25( W[8]);
	 W[7] =  W[8] +  W[0] + rotr15( W[5]) + rotr25( W[9]);
	 W[8] =  W[9] +  W[1] + rotr15( W[6]) + rotr25(W[10]);
	 W[9] = W[10] +  W[2] + rotr15( W[7]) + rotr25(W[11]);
	W[10] = W[11] +  W[3] + rotr15( W[8]) + rotr25(W[12]);
	W[11] = W[12] +  W[4] + rotr15( W[9]) + rotr25(W[13]);
	W[12] = W[13] +  W[5] + rotr15(W[10]) + rotr25(W[14]);
	W[13] = W[14] +  W[6] + rotr15(W[11]) + rotr25(W[15]);
	W[14] = W[15] +  W[7] + rotr15(W[12]) + rotr25(W[16]);
	W[15] = W[16] +  W[8] + rotr15(W[13]) + rotr25( W[0]);
	W[16] =  W[0] +  W[9] + rotr15(W[14]) + rotr25( W[1]);

	V[2] += 0x2e1b2138 + V[6] + W[0] + ch(97) + rotr26(V[3]);
	V[6] =  0x2e1b2138 + V[6] + W[0] + ch(97) + rotr26(V[3]) + rotr30(V[7]) + ma(97);

	V[1] += 0x4d2c6dfc + V[5] + W[1] + ch(98) + rotr26(V[2]);
	V[5] =  0x4d2c6dfc + V[5] + W[1] + ch(98) + rotr26(V[2]) + rotr30(V[6]) + ma(98);

	V[0] += 0x53380d13 + V[4] + W[2] + ch(99) + rotr26(V[1]);
	V[4] =  0x53380d13 + V[4] + W[2] + ch(99) + rotr26(V[1]) + rotr30(V[5]) + ma(99);

	V[7] += 0x650a7354 + V[3] + W[3] + ch(100) + rotr26(V[0]);
	V[3] =  0x650a7354 + V[3] + W[3] + ch(100) + rotr26(V[0]) + rotr30(V[4]) + ma(100);

	V[6] += 0x766a0abb + V[2] + W[4] + ch(101) + rotr26(V[7]);
	V[2] =  0x766a0abb + V[2] + W[4] + ch(101) + rotr26(V[7]) + rotr30(V[3]) + ma(101);

	V[5] += 0x81c2c92e + V[1] + W[5] + ch(102) + rotr26(V[6]);
	V[1] =  0x81c2c92e + V[1] + W[5] + ch(102) + rotr26(V[6]) + rotr30(V[2]) + ma(102);

	V[4] += 0x92722c85 + V[0] + W[6] + ch(103) + rotr26(V[5]);
	V[0] =  0x92722c85 + V[0] + W[6] + ch(103) + rotr26(V[5]) + rotr30(V[1]) + ma(103);

	V[3] += 0xa2bfe8a1 + V[7] + W[7] + ch(104) + rotr26(V[4]);
	V[7] =  0xa2bfe8a1 + V[7] + W[7] + ch(104) + rotr26(V[4]) + rotr30(V[0]) + ma(104);

	V[2] += 0xa81a664b + V[6] + W[8] + ch(105) + rotr26(V[3]);
	V[6] =  0xa81a664b + V[6] + W[8] + ch(105) + rotr26(V[3]) + rotr30(V[7]) + ma(105);

	V[1] += 0xc24b8b70 + V[5] + W[9] + ch(106) + rotr26(V[2]);
	V[5] =  0xc24b8b70 + V[5] + W[9] + ch(106) + rotr26(V[2]) + rotr30(V[6]) + ma(106);

	V[0] += 0xc76c51a3 + V[4] + W[10] + ch(107) + rotr26(V[1]);
	V[4] =  0xc76c51a3 + V[4] + W[10] + ch(107) + rotr26(V[1]) + rotr30(V[5]) + ma(107);

	V[7] += 0xd192e819 + V[3] + W[11] + ch(108) + rotr26(V[0]);
	V[3] =  0xd192e819 + V[3] + W[11] + ch(108) + rotr26(V[0]) + rotr30(V[4]) + ma(108);

	V[6] += 0xd6990624 + V[2] + W[12] + ch(109) + rotr26(V[7]);
	V[2] =  0xd6990624 + V[2] + W[12] + ch(109) + rotr26(V[7]) + rotr30(V[3]) + ma(109);

	V[5] += 0xf40e3585 + V[1] + W[13] + ch(110) + rotr26(V[6]);
	V[1] =  0xf40e3585 + V[1] + W[13] + ch(110) + rotr26(V[6]) + rotr30(V[2]) + ma(110);

	V[4] += 0x106aa070 + V[0] + W[14] + ch(111) + rotr26(V[5]);
	V[0] =  0x106aa070 + V[0] + W[14] + ch(111) + rotr26(V[5]) + rotr30(V[1]) + ma(111);

	V[3] += 0x19a4c116 + V[7] + W[15] + ch(112) + rotr26(V[4]);
	V[7] =  0x19a4c116 + V[7] + W[15] + ch(112) + rotr26(V[4]) + rotr30(V[0]) + ma(112);

	V[2] += 0x1e376c08 + V[6] + W[16] + ch(113) + rotr26(V[3]);
	V[6] =  0x1e376c08 + V[6] + W[16] + ch(113) + rotr26(V[3]) + rotr30(V[7]) + ma(113);

//----------------------------------------------------------------------------------

	 W[0] =  W[1] + W[10] + rotr15(W[15]) + rotr25( W[2]);
	 W[1] =  W[2] + W[11] + rotr15(W[16]) + rotr25( W[3]);
	 W[2] =  W[3] + W[12] + rotr15( W[0]) + rotr25( W[4]);
	 W[3] =  W[4] + W[13] + rotr15( W[1]) + rotr25( W[5]);
	 W[4] =  W[5] + W[14] + rotr15( W[2]) + rotr25( W[6]);
	 W[5] =  W[6] + W[15] + rotr15( W[3]) + rotr25( W[7]);
	 W[6] =  W[7] + W[16] + rotr15( W[4]) + rotr25( W[8]);
	 W[7] =  W[8] +  W[0] + rotr15( W[5]) + rotr25( W[9]);
	 W[8] =  W[9] +  W[1] + rotr15( W[6]) + rotr25(W[10]);
	 W[9] = W[10] +  W[2] + rotr15( W[7]) + rotr25(W[11]);
	W[10] = W[11] +  W[3] + rotr15( W[8]) + rotr25(W[12]);

	V[1] += 0x2748774c + V[5] + W[0] + ch(114) + rotr26(V[2]);
	V[5] =  0x2748774c + V[5] + W[0] + ch(114) + rotr26(V[2]) + rotr30(V[6]) + ma(114);

	V[0] += 0x34b0bcb5 + V[4] + W[1] + ch(115) + rotr26(V[1]);
	V[4] =  0x34b0bcb5 + V[4] + W[1] + ch(115) + rotr26(V[1]) + rotr30(V[5]) + ma(115);

	V[7] += 0x391c0cb3 + V[3] + W[2] + ch(116) + rotr26(V[0]);
	V[3] =  0x391c0cb3 + V[3] + W[2] + ch(116) + rotr26(V[0]) + rotr30(V[4]) + ma(116);

	V[6] += 0x4ed8aa4a + V[2] + W[3] + ch(117) + rotr26(V[7]);
	V[2] =  0x4ed8aa4a + V[2] + W[3] + ch(117) + rotr26(V[7]) + rotr30(V[3]) + ma(117);

	V[5] += 0x5b9cca4f + V[1] + W[4] + ch(118) + rotr26(V[6]);
	V[1] =  0x5b9cca4f + V[1] + W[4] + ch(118) + rotr26(V[6]) + rotr30(V[2]) + ma(118);

	V[4] += 0x682e6ff3 + V[0] + W[5] + ch(119) + rotr26(V[5]);
	V[0] =  0x682e6ff3 + V[0] + W[5] + ch(119) + rotr26(V[5]) + rotr30(V[1]) + ma(119);

	V[3] += 0x748f82ee + V[7] + W[6] + ch(120) + rotr26(V[4]);
	V[7] =  0x748f82ee + V[7] + W[6] + ch(120) + rotr26(V[4]) + rotr30(V[0]) + ma(120);

	V[2] += 0x78a5636f + V[6] + W[7] + ch(121) + rotr26(V[3]);

	V[1] += 0x84c87814 + V[5] + W[8] + ch(122) + rotr26(V[2]);

	V[0] += 0x8cc70208 + V[4] + W[9] + ch(123) + rotr26(V[1]);

	V[7] += V[3] + W[10] + ch(124) + rotr26(V[0]);


#define FOUND (0x80)
#define NFLAG (0x7F)

#ifdef VECTORS4
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
#else
	#ifdef VECTORS2
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
#endif
}
