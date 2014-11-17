// This file is taken and modified from the public-domain poclbm project, and
// I have therefore decided to keep it public-domain.
// Modified version copyright 2011-2012 Con Kolivas

// kernel-interface: phatk SHA256d

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

__constant uint ConstW[128] = {
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x80000000U, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000280U,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,

0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x80000000U, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000100U,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
};

__constant uint H[8] = { 
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};


#ifdef BITALIGN
	#pragma OPENCL EXTENSION cl_amd_media_ops : enable
	#define rot(x, y) amd_bitalign(x, x, (uint)(32 - y))

// This part is not from the stock poclbm kernel. It's part of an optimization
// added in the Phoenix Miner.

// Some AMD devices have Vals[0] BFI_INT opcode, which behaves exactly like the
// SHA-256 Ch function, but provides it in exactly one instruction. If
// detected, use it for Ch. Otherwise, construct Ch out of simpler logical
// primitives.

 #ifdef BFI_INT
	// Well, slight problem... It turns out BFI_INT isn't actually exposed to
	// OpenCL (or CAL IL for that matter) in any way. However, there is 
	// a similar instruction, BYTE_ALIGN_INT, which is exposed to OpenCL via
	// amd_bytealign, takes the same inputs, and provides the same output. 
	// We can use that as a placeholder for BFI_INT and have the application 
	// patch it after compilation.
	
	// This is the BFI_INT function
	#define Ch(x, y, z) amd_bytealign(x,y,z)
	// Ma can also be implemented in terms of BFI_INT...
	#define Ma(z, x, y) amd_bytealign(z^x,y,x)
 #else // BFI_INT
	// Later SDKs optimise this to BFI INT without patching and GCN
	// actually fails if manually patched with BFI_INT

	#define Ch(x, y, z) bitselect((u)z, (u)y, (u)x)
	#define Ma(x, y, z) bitselect((u)x, (u)y, (u)z ^ (u)x)
	#define rotr(x, y) amd_bitalign((u)x, (u)x, (u)y)
 #endif
#else // BITALIGN
	#define Ch(x, y, z) (z ^ (x & (y ^ z)))
	#define Ma(x, y, z) ((x & z) | (y & (x | z)))
	#define rot(x, y) rotate(x, y)
	#define rotr(x, y) rotate((u)x, (u)(32-y))
#endif



//Various intermediate calculations for each SHA round
#define s0(n) (S0(Vals[(0 + 128 - (n)) % 8]))
#define S0(n) (rot(n, 30u)^rot(n, 19u)^rot(n,10u))

#define s1(n) (S1(Vals[(4 + 128 - (n)) % 8]))
#define S1(n) (rot(n, 26u)^rot(n, 21u)^rot(n, 7u))

#define ch(n) Ch(Vals[(4 + 128 - (n)) % 8],Vals[(5 + 128 - (n)) % 8],Vals[(6 + 128 - (n)) % 8])
#define maj(n) Ma(Vals[(1 + 128 - (n)) % 8],Vals[(2 + 128 - (n)) % 8],Vals[(0 + 128 - (n)) % 8])

//t1 calc when W is already calculated
#define t1(n) K[(n) % 64] + Vals[(7 + 128 - (n)) % 8] +  W[(n)] + s1(n) + ch(n) 

//t1 calc which calculates W
#define t1W(n) K[(n) % 64] + Vals[(7 + 128 - (n)) % 8] +  W(n) + s1(n) + ch(n)

//Used for constant W Values (the compiler optimizes out zeros)
#define t1C(n) (K[(n) % 64]+ ConstW[(n)]) + Vals[(7 + 128 - (n)) % 8] + s1(n) + ch(n)

//t2 Calc
#define t2(n)  maj(n) + s0(n)

#define rotC(x,n) (x<<n | x >> (32-n))

//W calculation used for SHA round
#define W(n) (W[n] = P4(n) + P3(n) + P2(n) + P1(n))



//Partial W calculations (used for the begining where only some values are nonzero)
#define P1(n) ((rot(W[(n)-2],15u)^rot(W[(n)-2],13u)^((W[(n)-2])>>10U)))
#define P2(n) ((rot(W[(n)-15],25u)^rot(W[(n)-15],14u)^((W[(n)-15])>>3U)))


#define p1(x) ((rot(x,15u)^rot(x,13u)^((x)>>10U)))
#define p2(x) ((rot(x,25u)^rot(x,14u)^((x)>>3U)))


#define P3(n)  W[n-7]
#define P4(n)  W[n-16]


//Partial Calcs for constant W values
#define P1C(n) ((rotC(ConstW[(n)-2],15)^rotC(ConstW[(n)-2],13)^((ConstW[(n)-2])>>10U)))
#define P2C(n) ((rotC(ConstW[(n)-15],25)^rotC(ConstW[(n)-15],14)^((ConstW[(n)-15])>>3U)))
#define P3C(x)  ConstW[x-7]
#define P4C(x)  ConstW[x-16]

//SHA round with built in W calc
#define sharoundW(n) Barrier1(n);  Vals[(3 + 128 - (n)) % 8] += t1W(n); Vals[(7 + 128 - (n)) % 8] = t1W(n) + t2(n);  

//SHA round without W calc
#define sharound(n)  Barrier2(n); Vals[(3 + 128 - (n)) % 8] += t1(n); Vals[(7 + 128 - (n)) % 8] = t1(n) + t2(n);

//SHA round for constant W values
#define sharoundC(n)  Barrier3(n); Vals[(3 + 128 - (n)) % 8] += t1C(n); Vals[(7 + 128 - (n)) % 8] = t1C(n) + t2(n);

//The compiler is stupid... I put this in there only to stop the compiler from (de)optimizing the order
#define Barrier1(n) t1 = t1C((n+1))
#define Barrier2(n) t1 = t1C((n))
#define Barrier3(n) t1 = t1C((n))

//#define WORKSIZE 256
#define MAXBUFFERS (4095)

__kernel 
 __attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
void search(	const uint state0, const uint state1, const uint state2, const uint state3,
						const uint state4, const uint state5, const uint state6, const uint state7,
						const uint B1, const uint C1, const uint D1,
						const uint F1, const uint G1, const uint H1,
						const u base,
						const uint W16, const uint W17,
						const uint PreVal4, const uint PreVal0,
						const uint PreW18, const uint PreW19,
						const uint PreW31, const uint PreW32,
						
						volatile __global uint * output)
{


	u W[124];
	u Vals[8];

//Dummy Variable to prevent compiler from reordering between rounds
	u t1;

	//Vals[0]=state0;
	Vals[1]=B1;
	Vals[2]=C1;
	Vals[3]=D1;
	//Vals[4]=PreVal4;
	Vals[5]=F1;
	Vals[6]=G1;
	Vals[7]=H1;

	W[16] = W16;
	W[17] = W17;

#ifdef VECTORS4
	//Less dependencies to get both the local id and group id and then add them
	W[3] = base + (uint)(get_local_id(0)) * 4u + (uint)(get_group_id(0)) * (WORKSIZE * 4u);
	uint r = rot(W[3].x,25u)^rot(W[3].x,14u)^((W[3].x)>>3U);
	//Since only the 2 LSB is opposite between the nonces, we can save an instruction by flipping the 4 bits in W18 rather than the 1 bit in W3
	W[18] = PreW18 + (u){r, r ^ 0x2004000U, r ^ 0x4008000U, r ^ 0x600C000U};
#elif defined VECTORS2
	W[3] = base + (uint)(get_local_id(0)) * 2u + (uint)(get_group_id(0)) * (WORKSIZE * 2u);
	uint r = rot(W[3].x,25u)^rot(W[3].x,14u)^((W[3].x)>>3U);
	W[18] = PreW18 + (u){r, r ^ 0x2004000U};
#else
	W[3] = base + get_local_id(0) + get_group_id(0) * (WORKSIZE);
	u r = rot(W[3],25u)^rot(W[3],14u)^((W[3])>>3U);
	W[18] = PreW18 + r;
#endif
	//the order of the W calcs and Rounds is like this because the compiler needs help finding how to order the instructions



	Vals[4] = PreVal4 + W[3];
	Vals[0] = PreVal0 + W[3];

	sharoundC(4);
	W[19] = PreW19 + W[3];
	sharoundC(5);
	W[20] = P4C(20) + P1(20);
	sharoundC(6);
	W[21] = P1(21);
	sharoundC(7);
	W[22] = P3C(22) + P1(22);
	sharoundC(8);
	W[23] = W[16] + P1(23);
	sharoundC(9);
	W[24] = W[17] + P1(24);
	sharoundC(10);
	W[25] = P1(25) + P3(25);
	W[26] = P1(26) + P3(26);
	sharoundC(11);
	W[27] = P1(27) + P3(27);
	W[28] = P1(28) + P3(28);
	sharoundC(12);
	W[29] = P1(29) + P3(29);
	sharoundC(13);
	W[30] = P1(30) + P2C(30) + P3(30);
	W[31] = PreW31 + (P1(31) + P3(31));
	sharoundC(14);
	W[32] = PreW32 + (P1(32) + P3(32));
	sharoundC(15);
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
	sharound(31);
	sharound(32);
	sharoundW(33);
	sharoundW(34);
	sharoundW(35);
	sharoundW(36);
	sharoundW(37);	
	sharoundW(38);
	sharoundW(39);
	sharoundW(40);
	sharoundW(41);
	sharoundW(42);
	sharoundW(43);
	sharoundW(44);
	sharoundW(45);
	sharoundW(46);
	sharoundW(47);
	sharoundW(48);
	sharoundW(49);
	sharoundW(50);
	sharoundW(51);
	sharoundW(52);
	sharoundW(53);
	sharoundW(54);
	sharoundW(55);
	sharoundW(56);
	sharoundW(57);
	sharoundW(58);
	sharoundW(59);
	sharoundW(60);
	sharoundW(61);
	sharoundW(62);
	sharoundW(63);

	W[64]=state0+Vals[0];
	W[65]=state1+Vals[1];
	W[66]=state2+Vals[2];
	W[67]=state3+Vals[3];
	W[68]=state4+Vals[4];
	W[69]=state5+Vals[5];
	W[70]=state6+Vals[6];
	W[71]=state7+Vals[7];

	Vals[0]=H[0];
	Vals[1]=H[1];
	Vals[2]=H[2];
	Vals[3]=H[3];
	Vals[4]=H[4];
	Vals[5]=H[5];
	Vals[6]=H[6];
	Vals[7]=H[7];

	//sharound(64 + 0);
	const u Temp = (0xb0edbdd0U + K[0]) +  W[64];
	Vals[7] = Temp + 0x08909ae5U;
	Vals[3] = 0xa54ff53aU + Temp;
	
#define P124(n) P2(n) + P1(n) + P4(n)


	W[64 + 16] = + P2(64 + 16) + P4(64 + 16);
	sharound(64 + 1);
	W[64 + 17] = P1C(64 + 17) + P2(64 + 17) + P4(64 + 17);
	sharound(64 + 2);
	W[64 + 18] = P124(64 + 18);
	sharound(64 + 3);
	W[64 + 19] = P124(64 + 19);
	sharound(64 + 4);
	W[64 + 20] = P124(64 + 20);
	sharound(64 + 5);
	W[64 + 21] = P124(64 + 21);
	sharound(64 + 6);
	W[64 + 22] = P4(64 + 22) + P3C(64 + 22) + P2(64 + 22) + P1(64 + 22);
	sharound(64 + 7);
	W[64 + 23] = P4(64 + 23) + P3(64 + 23) + P2C(64 + 23) + P1(64 + 23);
	sharoundC(64 + 8);
	W[64 + 24] =   P1(64 + 24) + P4C(64 + 24) + P3(64 + 24);
	sharoundC(64 + 9);
	W[64 + 25] = P3(64 + 25) + P1(64 + 25);
	sharoundC(64 + 10);
	W[64 + 26] = P3(64 + 26) + P1(64 + 26);
	sharoundC(64 + 11);
	W[64 + 27] = P3(64 + 27) + P1(64 + 27);
	sharoundC(64 + 12);
	W[64 + 28] = P3(64 + 28) + P1(64 + 28);
	sharoundC(64 + 13);
	W[64 + 29] = P1(64 + 29) + P3(64 + 29);
	W[64 + 30] = P3(64 + 30) + P2C(64 + 30) + P1(64 + 30);
	sharoundC(64 + 14);
	W[64 + 31] = P4C(64 + 31) + P3(64 + 31) + P2(64 + 31) + P1(64 + 31);
	sharoundC(64 + 15);
	sharound(64 + 16);
	sharound(64 + 17);
	sharound(64 + 18);
	sharound(64 + 19);
	sharound(64 + 20);
	sharound(64 + 21);
	sharound(64 + 22);
	sharound(64 + 23);
	sharound(64 + 24);
	sharound(64 + 25);
	sharound(64 + 26);
	sharound(64 + 27);
	sharound(64 + 28);
	sharound(64 + 29);
	sharound(64 + 30);
	sharound(64 + 31);
	sharoundW(64 + 32);
	sharoundW(64 + 33);
	sharoundW(64 + 34);
	sharoundW(64 + 35);
	sharoundW(64 + 36);
	sharoundW(64 + 37);
	sharoundW(64 + 38);
	sharoundW(64 + 39);
	sharoundW(64 + 40);
	sharoundW(64 + 41);
	sharoundW(64 + 42);
	sharoundW(64 + 43);
	sharoundW(64 + 44);
	sharoundW(64 + 45);
	sharoundW(64 + 46);
	sharoundW(64 + 47);
	sharoundW(64 + 48);
	sharoundW(64 + 49);
	sharoundW(64 + 50);
	sharoundW(64 + 51);
	sharoundW(64 + 52);
	sharoundW(64 + 53);
	sharoundW(64 + 54);
	sharoundW(64 + 55);
	sharoundW(64 + 56);
	sharoundW(64 + 57);
	sharoundW(64 + 58);

	W[117] += W[108] + Vals[3] + Vals[7] + P2(124) + P1(124) + Ch((Vals[0] + Vals[4]) + (K[59] + W(59+64)) + s1(64+59)+ ch(59+64),Vals[1],Vals[2]) -
		(-(K[60] + H[7]) - S1((Vals[0] + Vals[4]) + (K[59] + W(59+64))  + s1(64+59)+ ch(59+64)));

#define FOUND (0x0F)
#define SETFOUND(Xnonce) output[output[FOUND]++] = Xnonce

#ifdef VECTORS4
	bool result = W[117].x & W[117].y & W[117].z & W[117].w;
	if (!result) {
		if (!W[117].x)
			SETFOUND(W[3].x);
		if (!W[117].y)
			SETFOUND(W[3].y);
		if (!W[117].z)
			SETFOUND(W[3].z);
		if (!W[117].w)
			SETFOUND(W[3].w);
	}
#elif defined VECTORS2
	bool result = W[117].x & W[117].y;
	if (!result) {
		if (!W[117].x)
			SETFOUND(W[3].x);
		if (!W[117].y)
			SETFOUND(W[3].y);
	}
#else
	if (!W[117])
		SETFOUND(W[3]);
#endif
}
