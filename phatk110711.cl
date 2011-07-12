// This file is taken and modified from the public-domain poclbm project, and
// we have therefore decided to keep it public-domain in Phoenix.

// 2011-07-11: further modified by Diapolo and still public-domain
// -ck version to be compatible with cgminer

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

// H[6] =  0x08909ae5U + 0xb0edbdd0 + K[0] == 0xfc08884d
// H[7] = -0x5be0cd19 - (0x90befffa) K[60] == -0xec9fcd13
__constant uint H[8] = { 
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0xfc08884d, 0xec9fcd13
};

// L = 0xa54ff53a + 0xb0edbdd0 + K[0] == 0x198c7e2a2
__constant ulong L = 0x198c7e2a2;

#define BFI_INTX
#define BITALIGNX

#ifdef BITALIGN
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#define rot(x, y) amd_bitalign(x, x, (u)(32 - y))
#else
	#define rot(x, y) rotate(x, (u)y)
#endif

#ifdef BFI_INT
	#define Ch(x, y, z) amd_bytealign(x, y, z)
#else 
	#define Ch(x, y, z) bitselect(z, y, x)
#endif

// Ma now uses the Ch function, if BFI_INT is enabled, the optimized Ch version is used
#define Ma(x, y, z) Ch((z ^ x), y, x)

// Various intermediate calculations for each SHA round
#define s0(n) (rot(Vals[(128 - n) % 8], 30) ^ rot(Vals[(128 - n) % 8], 19) ^ rot(Vals[(128 - n) % 8], 10))
#define s1(n) (rot(Vals[(132 - n) % 8], 26) ^ rot(Vals[(132 - n) % 8], 21) ^ rot(Vals[(132 - n) % 8], 7))
#define ch(n) (Ch(Vals[(132 - n) % 8], Vals[(133 - n) % 8], Vals[(134 - n) % 8]))
#define ma(n) (Ma(Vals[(129 - n) % 8], Vals[(130 - n) % 8], Vals[(128 - n) % 8]))
#define t1(n) (K[n % 64] + Vals[(135 - n) % 8] + W[n] + s1(n) + ch(n))

// intermediate W calculations
#define P1(x) (rot(W[x - 2], 15) ^ rot(W[x - 2], 13) ^ (W[x - 2] >> 10U))
#define P2(x) (rot(W[x - 15], 25) ^ rot(W[x - 15], 14) ^ (W[x - 15] >> 3U))
#define P3(x) W[x - 7]
#define P4(x) W[x - 16]

// full W calculation
#define W(x) (W[x] = P4(x) + P3(x) + P2(x) + P1(x))

// SHA round without W calc
#define sharound(n) { Vals[(131 - n) % 8] += t1(n); Vals[(135 - n) % 8] = t1(n) + s0(n) + ma(n); }

__kernel void search(	const uint state0, const uint state1, const uint state2, const uint state3,
						const uint state4, const uint state5, const uint state6, const uint state7,
						const uint B1, const uint C1, const uint D1,
						const uint F1, const uint G1, const uint H1,
						const uint base,
						const uint W2,
						const uint W16, const uint W17,
						const uint PreVal4, const uint T1,
						__global uint * output)
{
	u W[124];
	u Vals[8];
	uint it = get_local_id(0);

	Vals[1] = B1;
	Vals[2] = C1;
	Vals[5] = F1;
	Vals[6] = G1;
	
	W[2] = W2;
#ifdef VECTORS4
        Vals[4] = (W[3] = base + (get_global_id(0) << 2) + (uint4)(0, 1, 2, 3)) + PreVal4;
#elif defined VECTORS2
        Vals[4] = (W[3] = base + (get_global_id(0) << 1) + (uint2)(0, 1)) + PreVal4;
#else
        Vals[4] = (W[3] = base + get_global_id(0)) + PreVal4;
#endif
	// used in: P2(19) == 285220864 (0x11002000), P4(20)
	W[4] = 0x80000000U;
	// P1(x) is 0 for x == 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
	// P2(x) is 0 for x == 20, 21, 22, 23, 24, 25, 26, 27, 28, 29
	// P3(x) is 0 for x == 12, 13, 14, 15, 16, 17, 18, 19, 20, 21
	// P4(x) is 0 for x == 21, 22, 23, 24, 25, 26, 27, 28, 29, 30
	// W[x] in sharound(x) is 0 for x == 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
	W[14] = W[13] = W[12] = W[11] = W[10] = W[9] = W[8] = W[7] = W[6] = W[5] = 0x00000000U;
	// used in: P2(30) == 10485845 (0xA00055), P3(22), P4(31)
	// K[15] + W[15] == 0xc19bf174 + 0x00000280U = 0xc19bf3f4
	W[15] = 0x00000280U;

	W[16] = W16;
	W[17] = W17;
	// removed P3(18) from add because it is == 0
	W[18] = P1(18) + P4(18) + P2(18);
	// removed P3(19) from add because it is == 0
	W[19] = (u)0x11002000 + P1(19) + P4(19);
	// removed P2(20), P3(20) from add because it is == 0
	W[20] = P1(20) + P4(20);
	W[21] = P1(21);
	W[22] = P1(22) + P3(22);
	W[23] = P1(23) + P3(23);
	W[24] = P1(24) + P3(24);
	W[25] = P1(25) + P3(25);
	W[26] = P1(26) + P3(26);
	W[27] = P1(27) + P3(27);
	W[28] = P1(28) + P3(28);
	W[29] = P1(29) + P3(29);
	W[30] = (u)0xA00055 + P1(30) + P3(30);
	
	// Round 3
	Vals[0] = state0 + Vals[4];
	Vals[4] += T1;
	
	// Round 4
	// K[4] + W[4] == 0x3956c25b + 0x80000000U = 0xb956c25b
	Vals[7] = (Vals[3] = (u)0xb956c25b + D1 + s1(4) + ch(4)) + H1;
	Vals[3] += s0(4) + ma(4);

	// Round 5
	Vals[2] = K[5] + C1 + s1(5) + ch(5) + s0(5) + ma(5);
	Vals[6] = K[5] + C1 + G1 + s1(5) + ch(5);

	sharound(6);
	sharound(7);
	sharound(8);
	sharound(9);
	sharound(10);
	sharound(11);
	sharound(12);
	sharound(13);
	sharound(14);
	sharound(15);
	sharound(16);
	sharound(17);
	sharound(18);
	sharound(19);
	sharound(20);
	sharound(21);
	sharound(22);
	sharound(23);
	sharound(24);
	sharound(25);
	sharound(26);
	sharound(27);
	sharound(28);
	sharound(29);
	sharound(30);

	W(31);
	sharound(31);
	W(32);
	sharound(32);
	W(33);
	sharound(33);
	W(34);
	sharound(34);
	W(35);
	sharound(35);
	W(36);
	sharound(36);
	W(37);
	sharound(37);
	W(38);
	sharound(38);
	W(39);
	sharound(39);
	W(40);
	sharound(40);
	W(41);
	sharound(41);
	W(42);
	sharound(42);
	W(43);
	sharound(43);
	W(44);
	sharound(44);
	W(45);
	sharound(45);
	W(46);
	sharound(46);
	W(47);
	sharound(47);
	W(48);
	sharound(48);
	W(49);
	sharound(49);
	W(50);
	sharound(50);
	W(51);
	sharound(51);
	W(52);
	sharound(52);
	W(53);
	sharound(53);
	W(54);
	sharound(54);
	W(55);
	sharound(55);
	W(56);
	sharound(56);
	W(57);
	sharound(57);
	W(58);
	sharound(58);
	W(59);
	sharound(59);
	W(60);
	sharound(60);
	W(61);
	sharound(61);
	W(62);
	sharound(62);
	W(63);
	sharound(63);

	W[64] = state0 + Vals[0];
	W[65] = state1 + Vals[1];
	W[66] = state2 + Vals[2];
	W[67] = state3 + Vals[3];
	W[68] = state4 + Vals[4];
	W[69] = state5 + Vals[5];
	W[70] = state6 + Vals[6];
	W[71] = state7 + Vals[7];
	// used in: P2(87) = 285220864 (0x11002000), P4(88)
	// K[72] + W[72] ==
	W[72] = 0x80000000U;
	// P1(x) is 0 for x == 75, 76, 77, 78, 79, 80
	// P2(x) is 0 for x == 88, 89, 90, 91, 92, 93
	// P3(x) is 0 for x == 80, 81, 82, 83, 84, 85
	// P4(x) is 0 for x == 89, 90, 91, 92, 93, 94
	// W[x] in sharound(x) is 0 for x == 73, 74, 75, 76, 77, 78
	W[78] = W[77] = W[76] = W[75] = W[74] = W[73] = 0x00000000U;
	// used in: P1(81) = 10485760 (0xA00000), P2(94) = 4194338 (0x400022), P3(86), P4(95)
	// K[79] + W[79] ==
	W[79] = 0x00000100U;

	Vals[0] = H[0];
	Vals[1] = H[1];
	Vals[2] = H[2];
	Vals[3] = (u)L + W[64];
	Vals[4] = H[3];
	Vals[5] = H[4];
	Vals[6] = H[5];
	Vals[7] = H[6] + W[64];
	
	sharound(65);
	sharound(66);
	sharound(67);
	sharound(68);
	sharound(69);
	sharound(70);
	sharound(71);
	sharound(72);
	sharound(73);
	sharound(74);
	sharound(75);
	sharound(76);
	sharound(77);
	sharound(78);
	sharound(79);
	
	// removed P1(80), P3(80) from add because it is == 0
	W[80] = P2(80) + P4(80);
	W[81] = (u)0xA00000 + P4(81) + P2(81);
	W[82] = P4(82) + P2(82) + P1(82);
	W[83] = P4(83) + P2(83) + P1(83);
	W[84] = P4(84) + P2(84) + P1(84);
	W[85] = P4(85) + P2(85) + P1(85);
	W(86);

	sharound(80);
	sharound(81);	
	sharound(82);
	sharound(83);
	sharound(84);
	sharound(85);
	sharound(86);

	W[87] = (u)0x11002000 + P4(87) + P3(87) + P1(87);
	sharound(87);
	W[88] = P4(88) + P3(88) + P1(88);
	sharound(88);
	W[89] = P3(89) + P1(89);
	sharound(89);
	W[90] = P3(90) + P1(90);
	sharound(90);
	W[91] = P3(91) + P1(91);
	sharound(91);
	W[92] = P3(92) + P1(92);
	sharound(92);
	// removed P2(93), P4(93) from add because it is == 0
	W[93] = P3(93) + P1(93);
	sharound(93);
	// removed P4(94) from add because it is == 0
	W[94] = (u)0x400022 + P3(94) + P1(94);
	sharound(94);
	
	W(95);
	sharound(95);
	W(96);
	sharound(96);
	W(97);
	sharound(97);
	W(98);
	sharound(98);
	W(99);
	sharound(99);
	W(100);
	sharound(100);
	W(101);
	sharound(101);
	W(102);
	sharound(102);
	W(103);
	sharound(103);
	W(104);
	sharound(104);
	W(105);
	sharound(105);
	W(106);
	sharound(106);
	W(107);
	sharound(107);
	W(108);
	sharound(108);
	W(109);
	sharound(109);
	W(110);
	sharound(110);
	W(111);
	sharound(111);
	W(112);
	sharound(112);
	W(113);
	sharound(113);
	W(114);
	sharound(114);
	W(115);
	sharound(115);
	W(116);
	sharound(116);
	W(117);
	sharound(117);
	W(118);
	sharound(118);
	W(119);
	sharound(119);
	W(120);
	sharound(120);
	W(121);
	sharound(121);
	W(122);
	sharound(122);
	W(123);
	sharound(123);

	// Round 124
	Vals[7] += Vals[3] + P4(124) + P3(124) + P2(124) + P1(124) + s1(124) + ch(124);
	
#define MAXBUFFERS (4 * 512)

#if defined(VECTORS4) || defined(VECTORS2)
	if (Vals[7].x == -H[7])
	{
		// Unlikely event there is something here already !
		if (output[it]) {
			for (it = 0; it < MAXBUFFERS; it++) {
				if (!output[it])
					break;
			}
		}
		output[it] = W[3].x;
		output[MAXBUFFERS] = 1;
	}
	if (Vals[7].y == -H[7])
	{
		it += 512;
		if (output[it]) {
			for (it = 0; it < MAXBUFFERS; it++) {
				if (!output[it])
					break;
			}
		}
		output[it] = W[3].y;
		output[MAXBUFFERS] = 1;
	}
#ifdef VECTORS4
	if (Vals[7].z == -H[7])
	{
		it += 1024;
		if (output[it]) {
			for (it = 0; it < MAXBUFFERS; it++) {
				if (!output[it])
					break;
			}
		}
		output[it] = W[3].z;
		output[MAXBUFFERS] = 1;
	}
	if (Vals[7].w == -H[7])
	{
		it += 1536;
		if (output[it]) {
			for (it = 0; it < MAXBUFFERS; it++) {
				if (!output[it])
					break;
			}
		}
		output[it] = W[3].w;
		output[MAXBUFFERS] = 1;
	}
#endif
#else
	if (Vals[7] == -H[7])
	{
		if (output[it]) {
			for (it = 0; it < MAXBUFFERS; it++) {
				if (!output[it])
					break;
			}
		}
		output[it] = W[3];
		output[MAXBUFFERS] = 1;
	}
#endif
}