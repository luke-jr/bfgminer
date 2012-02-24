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
						const uint b1, const uint c1,
						const uint f1, const uint g1, const uint h1,
						const u base,
						const uint fw0, const uint fw1, const uint fw2, const uint fw3, const uint fw15, const uint fw01r,
						const uint fcty_e2,
						const uint D1A, const uint C1addK5, const uint B1addK6,
						const uint W16addK16, const uint W17addK17,
						const uint PreVal4addT1, const uint Preval0,
						__global uint * output)
{
	u W[24];
	u *Vals = &W[16]; // Now put at W[16] to be in same array

#ifdef VECTORS4
	const u nonce = base + (uint)(get_local_id(0)) * 4u + (uint)(get_group_id(0)) * (WORKSIZE * 4u);
#elif defined VECTORS2
	const u nonce = base + (uint)(get_local_id(0)) * 2u + (uint)(get_group_id(0)) * (WORKSIZE * 2u);
#else
	const u nonce = base + get_local_id(0) + get_group_id(0) * (WORKSIZE);
#endif


Vals[0]=Preval0+nonce;

Vals[3]=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],b1,c1);
Vals[3]+=D1A;

Vals[7]=Vals[3];
Vals[7]+=h1;
Vals[4]=PreVal4addT1+nonce;
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));

Vals[2]=C1addK5;
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],b1);

Vals[6]=Vals[2];
Vals[6]+=g1;
Vals[3]+=Ma2(g1,Vals[4],f1);
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

Vals[1]=B1addK6;
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);

Vals[5]=Vals[1];
Vals[5]+=f1;
Vals[2]+=Ma2(f1,Vals[3],Vals[4]);
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[7];
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[8];
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[9];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[10];
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[11];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[12];
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[13];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[14];
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=0xC19BF3F4;
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=W16addK16;
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=W17addK17;
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[2]=(rotr(nonce,7)^rotr(nonce,18)^(nonce>>3U));
W[2]+=fw2;
Vals[5]+=W[2];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[18];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);

W[3]=nonce;
W[3]+=fw3;
Vals[4]+=W[3];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[19];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));

W[4]=(rotr(W[2],17)^rotr(W[2],19)^(W[2]>>10U));
W[4]+=0x80000000;
Vals[3]+=W[4];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[20];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);

W[5]=(rotr(W[3],17)^rotr(W[3],19)^(W[3]>>10U));
Vals[2]+=W[5];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[21];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

W[6]=(rotr(W[4],17)^rotr(W[4],19)^(W[4]>>10U));
W[6]+=0x00000280U;
Vals[1]+=W[6];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[22];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);

W[7]=(rotr(W[5],17)^rotr(W[5],19)^(W[5]>>10U));
W[7]+=fw0;
Vals[0]+=W[7];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[23];

Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[8]=(rotr(W[6],17)^rotr(W[6],19)^(W[6]>>10U));
W[8]+=fw1;
Vals[7]+=W[8];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[24];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[9]=W[2];
W[9]+=(rotr(W[7],17)^rotr(W[7],19)^(W[7]>>10U));
Vals[6]+=W[9];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[25];

Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[10]=W[3];
W[10]+=(rotr(W[8],17)^rotr(W[8],19)^(W[8]>>10U));
Vals[5]+=W[10];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[26];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);

W[11]=W[4];
W[11]+=(rotr(W[9],17)^rotr(W[9],19)^(W[9]>>10U));
Vals[4]+=W[11];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[27];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));

W[12]=W[5];
W[12]+=(rotr(W[10],17)^rotr(W[10],19)^(W[10]>>10U));
Vals[3]+=W[12];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[28];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);

W[13]=W[6];
W[13]+=(rotr(W[11],17)^rotr(W[11],19)^(W[11]>>10U));
Vals[2]+=W[13];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[29];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

W[14]=0x00a00055U;
W[14]+=W[7];
W[14]+=(rotr(W[12],17)^rotr(W[12],19)^(W[12]>>10U));
Vals[1]+=W[14];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[30];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);

W[15]=fw15;
W[15]+=W[8];
W[15]+=(rotr(W[13],17)^rotr(W[13],19)^(W[13]>>10U));
Vals[0]+=W[15];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[31];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[0]=fw01r;
W[0]+=W[9];
W[0]+=(rotr(W[14],17)^rotr(W[14],19)^(W[14]>>10U));
Vals[7]+=W[0];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[32];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[1]=fw1;
W[1]+=(rotr(W[2],7)^rotr(W[2],18)^(W[2]>>3U));
W[1]+=W[10];
W[1]+=(rotr(W[15],17)^rotr(W[15],19)^(W[15]>>10U));
Vals[6]+=W[1];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[33];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
W[2]+=(rotr(W[3],7)^rotr(W[3],18)^(W[3]>>3U));
W[2]+=W[11];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
W[2]+=(rotr(W[0],17)^rotr(W[0],19)^(W[0]>>10U));
Vals[5]+=K[34];
Vals[5]+=W[2];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
W[3]+=(rotr(W[4],7)^rotr(W[4],18)^(W[4]>>3U));
W[3]+=W[12];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[35];
W[3]+=(rotr(W[1],17)^rotr(W[1],19)^(W[1]>>10U));
Vals[4]+=W[3];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
W[4]+=(rotr(W[5],7)^rotr(W[5],18)^(W[5]>>3U));
W[4]+=W[13];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
W[4]+=(rotr(W[2],17)^rotr(W[2],19)^(W[2]>>10U));
Vals[3]+=K[36];
Vals[3]+=W[4];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
W[5]+=(rotr(W[6],7)^rotr(W[6],18)^(W[6]>>3U));
W[5]+=W[14];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[37];
W[5]+=(rotr(W[3],17)^rotr(W[3],19)^(W[3]>>10U));
Vals[2]+=W[5];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
W[6]+=(rotr(W[7],7)^rotr(W[7],18)^(W[7]>>3U));
W[6]+=W[15];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
W[6]+=(rotr(W[4],17)^rotr(W[4],19)^(W[4]>>10U));
Vals[1]+=K[38];
Vals[1]+=W[6];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
W[7]+=(rotr(W[8],7)^rotr(W[8],18)^(W[8]>>3U));
W[7]+=W[0];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[39];
W[7]+=(rotr(W[5],17)^rotr(W[5],19)^(W[5]>>10U));
Vals[0]+=W[7];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
W[8]+=(rotr(W[9],7)^rotr(W[9],18)^(W[9]>>3U));
W[8]+=W[1];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
W[8]+=(rotr(W[6],17)^rotr(W[6],19)^(W[6]>>10U));
Vals[7]+=K[40];
Vals[7]+=W[8];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
W[9]+=(rotr(W[10],7)^rotr(W[10],18)^(W[10]>>3U));
W[9]+=W[2];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[41];
W[9]+=(rotr(W[7],17)^rotr(W[7],19)^(W[7]>>10U));
Vals[6]+=W[9];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
W[10]+=(rotr(W[11],7)^rotr(W[11],18)^(W[11]>>3U));
W[10]+=W[3];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
W[10]+=(rotr(W[8],17)^rotr(W[8],19)^(W[8]>>10U));
Vals[5]+=K[42];
Vals[5]+=W[10];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
W[11]+=(rotr(W[12],7)^rotr(W[12],18)^(W[12]>>3U));
W[11]+=W[4];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[43];
W[11]+=(rotr(W[9],17)^rotr(W[9],19)^(W[9]>>10U));
Vals[4]+=W[11];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
W[12]+=(rotr(W[13],7)^rotr(W[13],18)^(W[13]>>3U));
W[12]+=W[5];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
W[12]+=(rotr(W[10],17)^rotr(W[10],19)^(W[10]>>10U));
Vals[3]+=K[44];
Vals[3]+=W[12];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
W[13]+=(rotr(W[14],7)^rotr(W[14],18)^(W[14]>>3U));
W[13]+=W[6];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[45];
W[13]+=(rotr(W[11],17)^rotr(W[11],19)^(W[11]>>10U));
Vals[2]+=W[13];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
W[14]+=(rotr(W[15],7)^rotr(W[15],18)^(W[15]>>3U));
W[14]+=W[7];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
W[14]+=(rotr(W[12],17)^rotr(W[12],19)^(W[12]>>10U));
Vals[1]+=K[46];
Vals[1]+=W[14];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
W[15]+=(rotr(W[0],7)^rotr(W[0],18)^(W[0]>>3U));
W[15]+=W[8];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[47];
W[15]+=(rotr(W[13],17)^rotr(W[13],19)^(W[13]>>10U));
Vals[0]+=W[15];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
W[0]+=(rotr(W[1],7)^rotr(W[1],18)^(W[1]>>3U));
W[0]+=W[9];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
W[0]+=(rotr(W[14],17)^rotr(W[14],19)^(W[14]>>10U));
Vals[7]+=K[48];
Vals[7]+=W[0];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
W[1]+=(rotr(W[2],7)^rotr(W[2],18)^(W[2]>>3U));
W[1]+=W[10];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[49];
W[1]+=(rotr(W[15],17)^rotr(W[15],19)^(W[15]>>10U));
Vals[6]+=W[1];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
W[2]+=(rotr(W[3],7)^rotr(W[3],18)^(W[3]>>3U));
W[2]+=W[11];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
W[2]+=(rotr(W[0],17)^rotr(W[0],19)^(W[0]>>10U));
Vals[5]+=K[50];
Vals[5]+=W[2];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
W[3]+=(rotr(W[4],7)^rotr(W[4],18)^(W[4]>>3U));
W[3]+=W[12];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[51];
W[3]+=(rotr(W[1],17)^rotr(W[1],19)^(W[1]>>10U));
Vals[4]+=W[3];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
W[4]+=(rotr(W[5],7)^rotr(W[5],18)^(W[5]>>3U));
W[4]+=W[13];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
W[4]+=(rotr(W[2],17)^rotr(W[2],19)^(W[2]>>10U));
Vals[3]+=K[52];
Vals[3]+=W[4];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
W[5]+=(rotr(W[6],7)^rotr(W[6],18)^(W[6]>>3U));
W[5]+=W[14];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[53];
W[5]+=(rotr(W[3],17)^rotr(W[3],19)^(W[3]>>10U));
Vals[2]+=W[5];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
W[6]+=(rotr(W[7],7)^rotr(W[7],18)^(W[7]>>3U));
W[6]+=W[15];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
W[6]+=(rotr(W[4],17)^rotr(W[4],19)^(W[4]>>10U));
Vals[1]+=K[54];
Vals[1]+=W[6];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
W[7]+=(rotr(W[8],7)^rotr(W[8],18)^(W[8]>>3U));
W[7]+=W[0];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[55];
W[7]+=(rotr(W[5],17)^rotr(W[5],19)^(W[5]>>10U));
Vals[0]+=W[7];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
W[8]+=(rotr(W[9],7)^rotr(W[9],18)^(W[9]>>3U));
W[8]+=W[1];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
W[8]+=(rotr(W[6],17)^rotr(W[6],19)^(W[6]>>10U));
Vals[7]+=K[56];
Vals[7]+=W[8];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
W[9]+=(rotr(W[10],7)^rotr(W[10],18)^(W[10]>>3U));
W[9]+=W[2];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[57];
W[9]+=(rotr(W[7],17)^rotr(W[7],19)^(W[7]>>10U));
Vals[6]+=W[9];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
W[10]+=(rotr(W[11],7)^rotr(W[11],18)^(W[11]>>3U));
W[10]+=W[3];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
W[10]+=(rotr(W[8],17)^rotr(W[8],19)^(W[8]>>10U));
Vals[5]+=K[58];
Vals[5]+=W[10];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
W[11]+=(rotr(W[12],7)^rotr(W[12],18)^(W[12]>>3U));
W[11]+=W[4];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[59];
W[11]+=(rotr(W[9],17)^rotr(W[9],19)^(W[9]>>10U));
Vals[4]+=W[11];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
W[12]+=(rotr(W[13],7)^rotr(W[13],18)^(W[13]>>3U));
W[12]+=W[5];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
W[12]+=(rotr(W[10],17)^rotr(W[10],19)^(W[10]>>10U));
Vals[3]+=K[60];
Vals[3]+=W[12];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
W[13]+=(rotr(W[14],7)^rotr(W[14],18)^(W[14]>>3U));
W[13]+=W[6];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[61];
W[13]+=(rotr(W[11],17)^rotr(W[11],19)^(W[11]>>10U));
Vals[2]+=W[13];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
W[14]+=(rotr(W[15],7)^rotr(W[15],18)^(W[15]>>3U));
W[14]+=W[7];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
W[14]+=(rotr(W[12],17)^rotr(W[12],19)^(W[12]>>10U));
Vals[1]+=K[62];
Vals[1]+=W[14];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
W[15]+=(rotr(W[0],7)^rotr(W[0],18)^(W[0]>>3U));
W[15]+=W[8];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[63];
W[15]+=(rotr(W[13],17)^rotr(W[13],19)^(W[13]>>10U));
Vals[0]+=W[15];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);

W[0]=Vals[0];

W[7]=state7;
W[7]+=Vals[7];

Vals[7]=0xF377ED68;
W[0]+=state0;
Vals[7]+=W[0];

W[3]=state3;
W[3]+=Vals[3];

Vals[3]=0xa54ff53a;
Vals[3]+=Vals[7];

W[1]=Vals[1];
W[1]+=state1;

W[6]=state6;
W[6]+=Vals[6];

Vals[6]=0x90BB1E3C;
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=(0x9b05688cU^(Vals[3]&0xca0b3af3U));

W[2]=state2;
W[2]+=Vals[2];

Vals[2]=0x3c6ef372U;
Vals[6]+=W[1];
Vals[2]+=Vals[6];
Vals[7]+=0x08909ae5U;
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[5]=state5;
W[5]+=Vals[5];

Vals[5]=0x150C6645B;
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],0x510e527fU);
Vals[5]+=W[2];

Vals[1]=0xbb67ae85U;
Vals[1]+=Vals[5];
Vals[6]+=Ma2(0xbb67ae85U,Vals[7],0x6a09e667U);
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));

W[4]=state4;
W[4]+=Vals[4];

Vals[4]=0x13AC42E24;
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=W[3];

Vals[0]=Vals[4];
Vals[0]+=0x6a09e667U;
Vals[5]+=Ma2(0x6a09e667U,Vals[6],Vals[7]);
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[4];
Vals[3]+=W[4];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[5];
Vals[2]+=W[5];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[6];
Vals[1]+=W[6];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[7];
Vals[0]+=W[7];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=0x15807AA98;
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[9];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[10];
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[11];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[12];
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[13];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[14];
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=0xC19BF274;
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
W[0]+=(rotr(W[1],7)^rotr(W[1],18)^(W[1]>>3U));
Vals[7]+=K[16];
Vals[7]+=W[0];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);
W[1]+=(rotr(W[2],7)^rotr(W[2],18)^(W[2]>>3U));
W[1]+=0x00a00000U;
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[17];
Vals[6]+=W[1];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));
W[2]+=(rotr(W[3],7)^rotr(W[3],18)^(W[3]>>3U));
W[2]+=(rotr(W[0],17)^rotr(W[0],19)^(W[0]>>10U));
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[18];
Vals[5]+=W[2];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);
W[3]+=(rotr(W[4],7)^rotr(W[4],18)^(W[4]>>3U));
W[3]+=(rotr(W[1],17)^rotr(W[1],19)^(W[1]>>10U));
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[19];
Vals[4]+=W[3];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));
W[4]+=(rotr(W[5],7)^rotr(W[5],18)^(W[5]>>3U));
W[4]+=(rotr(W[2],17)^rotr(W[2],19)^(W[2]>>10U));
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[20];
Vals[3]+=W[4];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);
W[5]+=(rotr(W[6],7)^rotr(W[6],18)^(W[6]>>3U));
W[5]+=(rotr(W[3],17)^rotr(W[3],19)^(W[3]>>10U));
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[21];
Vals[2]+=W[5];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));
W[6]+=(rotr(W[7],7)^rotr(W[7],18)^(W[7]>>3U));
W[6]+=0x00000100U;
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
W[6]+=(rotr(W[4],17)^rotr(W[4],19)^(W[4]>>10U));
Vals[1]+=K[22];
Vals[1]+=W[6];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);
W[7]+=0x11002000U;
W[7]+=W[0];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[23];
W[7]+=(rotr(W[5],17)^rotr(W[5],19)^(W[5]>>10U));
Vals[0]+=W[7];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[8]=0x80000000;
W[8]+=W[1];
W[8]+=(rotr(W[6],17)^rotr(W[6],19)^(W[6]>>10U));
Vals[7]+=W[8];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[24];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[9]=W[2];
W[9]+=(rotr(W[7],17)^rotr(W[7],19)^(W[7]>>10U));
Vals[6]+=W[9];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[25];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[10]=W[3];
W[10]+=(rotr(W[8],17)^rotr(W[8],19)^(W[8]>>10U));
Vals[5]+=W[10];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[26];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);

W[11]=W[4];
W[11]+=(rotr(W[9],17)^rotr(W[9],19)^(W[9]>>10U));
Vals[4]+=W[11];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[27];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));

W[12]=W[5];
W[12]+=(rotr(W[10],17)^rotr(W[10],19)^(W[10]>>10U));
Vals[3]+=W[12];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[28];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);

W[13]=W[6];
W[13]+=(rotr(W[11],17)^rotr(W[11],19)^(W[11]>>10U));
Vals[2]+=W[13];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[29];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

W[14]=0x00400022U;
W[14]+=W[7];
W[14]+=(rotr(W[12],17)^rotr(W[12],19)^(W[12]>>10U));
Vals[1]+=W[14];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[30];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);

W[15]=0x00000100U;
W[15]+=(rotr(W[0],7)^rotr(W[0],18)^(W[0]>>3U));
W[15]+=W[8];
W[15]+=(rotr(W[13],17)^rotr(W[13],19)^(W[13]>>10U));
Vals[0]+=W[15];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[31];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[0]+=(rotr(W[1],7)^rotr(W[1],18)^(W[1]>>3U));
W[0]+=W[9];
W[0]+=(rotr(W[14],17)^rotr(W[14],19)^(W[14]>>10U));
Vals[7]+=W[0];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[32];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[1]+=(rotr(W[2],7)^rotr(W[2],18)^(W[2]>>3U));
W[1]+=W[10];
W[1]+=(rotr(W[15],17)^rotr(W[15],19)^(W[15]>>10U));
Vals[6]+=W[1];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[33];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[2]+=(rotr(W[3],7)^rotr(W[3],18)^(W[3]>>3U));
W[2]+=W[11];
W[2]+=(rotr(W[0],17)^rotr(W[0],19)^(W[0]>>10U));
Vals[5]+=W[2];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[34];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);

W[3]+=(rotr(W[4],7)^rotr(W[4],18)^(W[4]>>3U));
W[3]+=W[12];
W[3]+=(rotr(W[1],17)^rotr(W[1],19)^(W[1]>>10U));
Vals[4]+=W[3];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[35];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));

W[4]+=(rotr(W[5],7)^rotr(W[5],18)^(W[5]>>3U));
W[4]+=W[13];
W[4]+=(rotr(W[2],17)^rotr(W[2],19)^(W[2]>>10U));
Vals[3]+=W[4];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[36];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);

W[5]+=(rotr(W[6],7)^rotr(W[6],18)^(W[6]>>3U));
W[5]+=W[14];
W[5]+=(rotr(W[3],17)^rotr(W[3],19)^(W[3]>>10U));
Vals[2]+=W[5];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[37];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

W[6]+=(rotr(W[7],7)^rotr(W[7],18)^(W[7]>>3U));
W[6]+=W[15];
W[6]+=(rotr(W[4],17)^rotr(W[4],19)^(W[4]>>10U));
Vals[1]+=W[6];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[38];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);

W[7]+=(rotr(W[8],7)^rotr(W[8],18)^(W[8]>>3U));
W[7]+=W[0];
W[7]+=(rotr(W[5],17)^rotr(W[5],19)^(W[5]>>10U));
Vals[0]+=W[7];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[39];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[8]+=(rotr(W[9],7)^rotr(W[9],18)^(W[9]>>3U));
W[8]+=W[1];
W[8]+=(rotr(W[6],17)^rotr(W[6],19)^(W[6]>>10U));
Vals[7]+=W[8];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[40];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[9]+=(rotr(W[10],7)^rotr(W[10],18)^(W[10]>>3U));
W[9]+=W[2];
W[9]+=(rotr(W[7],17)^rotr(W[7],19)^(W[7]>>10U));
Vals[6]+=W[9];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[41];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[10]+=(rotr(W[11],7)^rotr(W[11],18)^(W[11]>>3U));
W[10]+=W[3];
W[10]+=(rotr(W[8],17)^rotr(W[8],19)^(W[8]>>10U));
Vals[5]+=W[10];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[42];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);

W[11]+=(rotr(W[12],7)^rotr(W[12],18)^(W[12]>>3U));
W[11]+=W[4];
W[11]+=(rotr(W[9],17)^rotr(W[9],19)^(W[9]>>10U));
Vals[4]+=W[11];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[43];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));

W[12]+=(rotr(W[13],7)^rotr(W[13],18)^(W[13]>>3U));
W[12]+=W[5];
W[12]+=(rotr(W[10],17)^rotr(W[10],19)^(W[10]>>10U));
Vals[3]+=W[12];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[44];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);

W[13]+=(rotr(W[14],7)^rotr(W[14],18)^(W[14]>>3U));
W[13]+=W[6];
W[13]+=(rotr(W[11],17)^rotr(W[11],19)^(W[11]>>10U));
Vals[2]+=W[13];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[45];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

W[14]+=(rotr(W[15],7)^rotr(W[15],18)^(W[15]>>3U));
W[14]+=W[7];
W[14]+=(rotr(W[12],17)^rotr(W[12],19)^(W[12]>>10U));
Vals[1]+=W[14];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[46];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);

W[15]+=(rotr(W[0],7)^rotr(W[0],18)^(W[0]>>3U));
W[15]+=W[8];
W[15]+=(rotr(W[13],17)^rotr(W[13],19)^(W[13]>>10U));
Vals[0]+=W[15];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[47];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[0]+=(rotr(W[1],7)^rotr(W[1],18)^(W[1]>>3U));
W[0]+=W[9];
W[0]+=(rotr(W[14],17)^rotr(W[14],19)^(W[14]>>10U));
Vals[7]+=W[0];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[48];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[1]+=(rotr(W[2],7)^rotr(W[2],18)^(W[2]>>3U));
W[1]+=W[10];
W[1]+=(rotr(W[15],17)^rotr(W[15],19)^(W[15]>>10U));
Vals[6]+=W[1];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[49];
Vals[2]+=Vals[6];
Vals[6]+=(rotr(Vals[7],2)^rotr(Vals[7],13)^rotr(Vals[7],22));

W[2]+=(rotr(W[3],7)^rotr(W[3],18)^(W[3]>>3U));
W[2]+=W[11];
W[2]+=(rotr(W[0],17)^rotr(W[0],19)^(W[0]>>10U));
Vals[5]+=W[2];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[50];
Vals[6]+=Ma(Vals[1],Vals[7],Vals[0]);
Vals[1]+=Vals[5];
Vals[5]+=(rotr(Vals[6],2)^rotr(Vals[6],13)^rotr(Vals[6],22));
Vals[5]+=Ma(Vals[0],Vals[6],Vals[7]);

W[3]+=(rotr(W[4],7)^rotr(W[4],18)^(W[4]>>3U));
W[3]+=W[12];
W[3]+=(rotr(W[1],17)^rotr(W[1],19)^(W[1]>>10U));
Vals[4]+=W[3];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[51];
Vals[0]+=Vals[4];
Vals[4]+=(rotr(Vals[5],2)^rotr(Vals[5],13)^rotr(Vals[5],22));

W[4]+=(rotr(W[5],7)^rotr(W[5],18)^(W[5]>>3U));
W[4]+=W[13];
W[4]+=(rotr(W[2],17)^rotr(W[2],19)^(W[2]>>10U));
Vals[3]+=W[4];
Vals[3]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[3]+=ch(Vals[0],Vals[1],Vals[2]);
Vals[3]+=K[52];
Vals[4]+=Ma(Vals[7],Vals[5],Vals[6]);
Vals[7]+=Vals[3];
Vals[3]+=(rotr(Vals[4],2)^rotr(Vals[4],13)^rotr(Vals[4],22));
Vals[3]+=Ma(Vals[6],Vals[4],Vals[5]);

W[5]+=(rotr(W[6],7)^rotr(W[6],18)^(W[6]>>3U));
W[5]+=W[14];
W[5]+=(rotr(W[3],17)^rotr(W[3],19)^(W[3]>>10U));
Vals[2]+=W[5];
Vals[2]+=(rotr(Vals[7],6)^rotr(Vals[7],11)^rotr(Vals[7],25));
Vals[2]+=ch(Vals[7],Vals[0],Vals[1]);
Vals[2]+=K[53];
Vals[6]+=Vals[2];
Vals[2]+=(rotr(Vals[3],2)^rotr(Vals[3],13)^rotr(Vals[3],22));

W[6]+=(rotr(W[7],7)^rotr(W[7],18)^(W[7]>>3U));
W[6]+=W[15];
W[6]+=(rotr(W[4],17)^rotr(W[4],19)^(W[4]>>10U));
Vals[1]+=W[6];
Vals[1]+=(rotr(Vals[6],6)^rotr(Vals[6],11)^rotr(Vals[6],25));
Vals[1]+=ch(Vals[6],Vals[7],Vals[0]);
Vals[1]+=K[54];
Vals[2]+=Ma(Vals[5],Vals[3],Vals[4]);
Vals[5]+=Vals[1];
Vals[1]+=(rotr(Vals[2],2)^rotr(Vals[2],13)^rotr(Vals[2],22));
Vals[1]+=Ma(Vals[4],Vals[2],Vals[3]);

W[7]+=(rotr(W[8],7)^rotr(W[8],18)^(W[8]>>3U));
W[7]+=W[0];
W[7]+=(rotr(W[5],17)^rotr(W[5],19)^(W[5]>>10U));
Vals[0]+=W[7];
Vals[0]+=(rotr(Vals[5],6)^rotr(Vals[5],11)^rotr(Vals[5],25));
Vals[0]+=ch(Vals[5],Vals[6],Vals[7]);
Vals[0]+=K[55];
Vals[4]+=Vals[0];
Vals[0]+=(rotr(Vals[1],2)^rotr(Vals[1],13)^rotr(Vals[1],22));

W[8]+=(rotr(W[9],7)^rotr(W[9],18)^(W[9]>>3U));
W[8]+=W[1];
W[8]+=(rotr(W[6],17)^rotr(W[6],19)^(W[6]>>10U));
Vals[7]+=W[8];
Vals[7]+=(rotr(Vals[4],6)^rotr(Vals[4],11)^rotr(Vals[4],25));
Vals[7]+=ch(Vals[4],Vals[5],Vals[6]);
Vals[7]+=K[56];
Vals[0]+=Ma(Vals[3],Vals[1],Vals[2]);
Vals[3]+=Vals[7];
Vals[7]+=(rotr(Vals[0],2)^rotr(Vals[0],13)^rotr(Vals[0],22));
Vals[7]+=Ma(Vals[2],Vals[0],Vals[1]);

W[9]+=(rotr(W[10],7)^rotr(W[10],18)^(W[10]>>3U));
W[9]+=W[2];
W[9]+=(rotr(W[7],17)^rotr(W[7],19)^(W[7]>>10U));
Vals[6]+=W[9];
Vals[6]+=(rotr(Vals[3],6)^rotr(Vals[3],11)^rotr(Vals[3],25));
Vals[6]+=ch(Vals[3],Vals[4],Vals[5]);
Vals[6]+=K[57];

W[10]+=(rotr(W[11],7)^rotr(W[11],18)^(W[11]>>3U));
W[10]+=W[3];
W[10]+=(rotr(W[8],17)^rotr(W[8],19)^(W[8]>>10U));
Vals[5]+=W[10];
Vals[2]+=Vals[6];
Vals[5]+=(rotr(Vals[2],6)^rotr(Vals[2],11)^rotr(Vals[2],25));
Vals[5]+=ch(Vals[2],Vals[3],Vals[4]);
Vals[5]+=K[58];

W[11]+=(rotr(W[12],7)^rotr(W[12],18)^(W[12]>>3U));
W[11]+=W[4];
W[11]+=(rotr(W[9],17)^rotr(W[9],19)^(W[9]>>10U));
Vals[4]+=W[11];
Vals[1]+=Vals[5];
Vals[4]+=(rotr(Vals[1],6)^rotr(Vals[1],11)^rotr(Vals[1],25));
Vals[4]+=ch(Vals[1],Vals[2],Vals[3]);
Vals[4]+=K[59];

W[12]+=(rotr(W[13],7)^rotr(W[13],18)^(W[13]>>3U));
W[12]+=W[5];
W[12]+=(rotr(W[10],17)^rotr(W[10],19)^(W[10]>>10U));
Vals[7]+=W[12];
Vals[0]+=Vals[4];
Vals[7]+=Vals[3];
Vals[7]+=(rotr(Vals[0],6)^rotr(Vals[0],11)^rotr(Vals[0],25));
Vals[7]+=ch(Vals[0],Vals[1],Vals[2]);
//Vals[7]+=K[60]; diffed from 0xA41F32E7

#define FOUND (0x80)
#define NFLAG (0x7F)

#if defined(VECTORS4)
	Vals[7] ^= 0x136032edU;

	bool result = Vals[7].x & Vals[7].y & Vals[7].z & Vals[7].w;

	if (!result) {
		output[FOUND] = FOUND;
		if (!Vals[7].x)
			output[NFLAG & nonce.x] = nonce.x;
		if (!Vals[7].y)
			output[NFLAG & nonce.y] = nonce.y;
		if (!Vals[7].z)
			output[NFLAG & nonce.z] = nonce.z;
		if (!Vals[7].w)
			output[NFLAG & nonce.w] = nonce.w;
	}
#elif defined VECTORS2
	Vals[7] ^= 0x136032edU;

	bool result = Vals[7].x & Vals[7].y;

	if (!result) {
		output[FOUND] = FOUND;
		if (!Vals[7].x)
			output[FOUND] = output[NFLAG & nonce.x] = nonce.x;
		if (!Vals[7].y)
			output[FOUND] = output[NFLAG & nonce.y] = nonce.y;
	}
#else
	if (Vals[7] == 0x136032EDU)
		output[FOUND] = output[NFLAG & nonce] =  nonce;
#endif
}
