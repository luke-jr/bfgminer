#include "config.h"
#include "miner.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <stdio.h>

#define CL_SET_BLKARG(blkvar) status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->blkvar)
#define CL_SET_ARG(var) status |= clSetKernelArg(*kernel, num++, sizeof(var), (void *)&var)

struct uint256 {
	unsigned char v[32];
};
typedef struct uint256 uint256;

typedef unsigned long long UINT64;

#define ROL(a, offset) ((a << offset) | (a >> (64-offset)))

static const UINT64 KeccakF_RoundConstants[24] = {
	0x0000000000000001ULL,
	0x0000000000008082ULL,
	0x800000000000808aULL,
	0x8000000080008000ULL,
	0x000000000000808bULL,
	0x0000000080000001ULL,
	0x8000000080008081ULL,
	0x8000000000008009ULL,
	0x000000000000008aULL,
	0x0000000000000088ULL,
	0x0000000080008009ULL,
	0x000000008000000aULL,
	0x000000008000808bULL,
	0x800000000000008bULL,
	0x8000000000008089ULL,
	0x8000000000008003ULL,
	0x8000000000008002ULL,
	0x8000000000000080ULL,
	0x000000000000800aULL,
	0x800000008000000aULL,
	0x8000000080008081ULL,
	0x8000000000008080ULL,
	0x0000000080000001ULL,
	0x8000000080008008ULL
};

struct bin32 {
	UINT64 v0;
	UINT64 v1;
	UINT64 v2;
	UINT64 v3;
};

void keccak1(unsigned char *out, const unsigned char *inraw, unsigned inrawlen)
{
	unsigned char temp[136];
	unsigned round;
	
	UINT64 Aba, Abe, Abi, Abo, Abu;
	UINT64 Aga, Age, Agi, Ago, Agu;
	UINT64 Aka, Ake, Aki, Ako, Aku;
	UINT64 Ama, Ame, Ami, Amo, Amu;
	UINT64 Asa, Ase, Asi, Aso, Asu;
	UINT64 BCa, BCe, BCi, BCo, BCu;
	UINT64 Da, De, Di, Do, Du;
	UINT64 Eba, Ebe, Ebi, Ebo, Ebu;
	UINT64 Ega, Ege, Egi, Ego, Egu;
	UINT64 Eka, Eke, Eki, Eko, Eku;
	UINT64 Ema, Eme, Emi, Emo, Emu;
	UINT64 Esa, Ese, Esi, Eso, Esu;
	
	memcpy(temp, inraw, inrawlen);
	temp[inrawlen++] = 1;
	memset( temp+inrawlen, 0, 136 - inrawlen);
	temp[136-1] |= 0x80;
	const UINT64 *in = (const UINT64 *)temp;
	
	// copyFromState(A, state)
	Aba = in[ 0];
	Abe = in[ 1];
	Abi = in[ 2];
	Abo = in[ 3];
	Abu = in[ 4];
	Aga = in[ 5];
	Age = in[ 6];
	Agi = in[ 7];
	Ago = in[ 8];
	Agu = in[ 9];
	Aka = in[10];
	Ake = in[11];
	Aki = in[12];
	Ako = in[13];
	Aku = in[14];
	Ama = in[15];
	Ame = in[16];
	Ami = 0;
	Amo = 0;
	Amu = 0;
	Asa = 0;
	Ase = 0;
	Asi = 0;
	Aso = 0;
	Asu = 0;
	
	for (round = 0; round < 24; round += 2)
	{
		// prepareTheta
		BCa = Aba^Aga^Aka^Ama^Asa;
		BCe = Abe^Age^Ake^Ame^Ase;
		BCi = Abi^Agi^Aki^Ami^Asi;
		BCo = Abo^Ago^Ako^Amo^Aso;
		BCu = Abu^Agu^Aku^Amu^Asu;
		
		// thetaRhoPiChiIotaPrepareTheta(round, A, E)
		Da = BCu^ROL(BCe, 1);
		De = BCa^ROL(BCi, 1);
		Di = BCe^ROL(BCo, 1);
		Do = BCi^ROL(BCu, 1);
		Du = BCo^ROL(BCa, 1);
		
		Aba ^= Da;
		BCa = Aba;
		Age ^= De;
		BCe = ROL(Age, 44);
		Aki ^= Di;
		BCi = ROL(Aki, 43);
		Amo ^= Do;
		BCo = ROL(Amo, 21);
		Asu ^= Du;
		BCu = ROL(Asu, 14);
		Eba = BCa ^((~BCe) & BCi);
		Eba ^= KeccakF_RoundConstants[round];
		Ebe = BCe ^((~BCi) & BCo);
		Ebi = BCi ^((~BCo) & BCu);
		Ebo = BCo ^((~BCu) & BCa);
		Ebu = BCu ^((~BCa) & BCe);
		
		Abo ^= Do;
		BCa = ROL(Abo, 28);
		Agu ^= Du;
		BCe = ROL(Agu, 20);
		Aka ^= Da;
		BCi = ROL(Aka,  3);
		Ame ^= De;
		BCo = ROL(Ame, 45);
		Asi ^= Di;
		BCu = ROL(Asi, 61);
		Ega = BCa ^((~BCe) & BCi);
		Ege = BCe ^((~BCi) & BCo);
		Egi = BCi ^((~BCo) & BCu);
		Ego = BCo ^((~BCu) & BCa);
		Egu = BCu ^((~BCa) & BCe);
		
		Abe ^= De;
		BCa = ROL(Abe,  1);
		Agi ^= Di;
		BCe = ROL(Agi,  6);
		Ako ^= Do;
		BCi = ROL(Ako, 25);
		Amu ^= Du;
		BCo = ROL(Amu,  8);
		Asa ^= Da;
		BCu = ROL(Asa, 18);
		Eka = BCa ^((~BCe) & BCi);
		Eke = BCe ^((~BCi) & BCo);
		Eki = BCi ^((~BCo) & BCu);
		Eko = BCo ^((~BCu) & BCa);
		Eku = BCu ^((~BCa) & BCe);
		
		Abu ^= Du;
		BCa = ROL(Abu, 27);
		Aga ^= Da;
		BCe = ROL(Aga, 36);
		Ake ^= De;
		BCi = ROL(Ake, 10);
		Ami ^= Di;
		BCo = ROL(Ami, 15);
		Aso ^= Do;
		BCu = ROL(Aso, 56);
		Ema = BCa ^((~BCe) & BCi);
		Eme = BCe ^((~BCi) & BCo);
		Emi = BCi ^((~BCo) & BCu);
		Emo = BCo ^((~BCu) & BCa);
		Emu = BCu ^((~BCa) & BCe);
		
		Abi ^= Di;
		BCa = ROL(Abi, 62);
		Ago ^= Do;
		BCe = ROL(Ago, 55);
		Aku ^= Du;
		BCi = ROL(Aku, 39);
		Ama ^= Da;
		BCo = ROL(Ama, 41);
		Ase ^= De;
		BCu = ROL(Ase,  2);
		Esa = BCa ^((~BCe) & BCi);
		Ese = BCe ^((~BCi) & BCo);
		Esi = BCi ^((~BCo) & BCu);
		Eso = BCo ^((~BCu) & BCa);
		Esu = BCu ^((~BCa) & BCe);
		
		// prepareTheta
		BCa = Eba^Ega^Eka^Ema^Esa;
		BCe = Ebe^Ege^Eke^Eme^Ese;
		BCi = Ebi^Egi^Eki^Emi^Esi;
		BCo = Ebo^Ego^Eko^Emo^Eso;
		BCu = Ebu^Egu^Eku^Emu^Esu;
		
		// thetaRhoPiChiIotaPrepareTheta(round+1, E, A)
		Da = BCu^ROL(BCe, 1);
		De = BCa^ROL(BCi, 1);
		Di = BCe^ROL(BCo, 1);
		Do = BCi^ROL(BCu, 1);
		Du = BCo^ROL(BCa, 1);
		
		Eba ^= Da;
		BCa = Eba;
		Ege ^= De;
		BCe = ROL(Ege, 44);
		Eki ^= Di;
		BCi = ROL(Eki, 43);
		Emo ^= Do;
		BCo = ROL(Emo, 21);
		Esu ^= Du;
		BCu = ROL(Esu, 14);
		Aba = BCa ^((~BCe) & BCi);
		Aba ^= KeccakF_RoundConstants[round+1];
		Abe = BCe ^((~BCi) & BCo);
		Abi = BCi ^((~BCo) & BCu);
		Abo = BCo ^((~BCu) & BCa);
		Abu = BCu ^((~BCa) & BCe);
		
		Ebo ^= Do;
		BCa = ROL(Ebo, 28);
		Egu ^= Du;
		BCe = ROL(Egu, 20);
		Eka ^= Da;
		BCi = ROL(Eka, 3);
		Eme ^= De;
		BCo = ROL(Eme, 45);
		Esi ^= Di;
		BCu = ROL(Esi, 61);
		Aga = BCa ^((~BCe) & BCi);
		Age = BCe ^((~BCi) & BCo);
		Agi = BCi ^((~BCo) & BCu);
		Ago = BCo ^((~BCu) & BCa);
		Agu = BCu ^((~BCa) & BCe);
		
		Ebe ^= De;
		BCa = ROL(Ebe, 1);
		Egi ^= Di;
		BCe = ROL(Egi, 6);
		Eko ^= Do;
		BCi = ROL(Eko, 25);
		Emu ^= Du;
		BCo = ROL(Emu, 8);
		Esa ^= Da;
		BCu = ROL(Esa, 18);
		Aka = BCa ^((~BCe) & BCi);
		Ake = BCe ^((~BCi) & BCo);
		Aki = BCi ^((~BCo) & BCu);
		Ako = BCo ^((~BCu) & BCa);
		Aku = BCu ^((~BCa) & BCe);
		
		Ebu ^= Du;
		BCa = ROL(Ebu, 27);
		Ega ^= Da;
		BCe = ROL(Ega, 36);
		Eke ^= De;
		BCi = ROL(Eke, 10);
		Emi ^= Di;
		BCo = ROL(Emi, 15);
		Eso ^= Do;
		BCu = ROL(Eso, 56);
		Ama = BCa ^((~BCe) & BCi);
		Ame = BCe ^((~BCi) & BCo);
		Ami = BCi ^((~BCo) & BCu);
		Amo = BCo ^((~BCu) & BCa);
		Amu = BCu ^((~BCa) & BCe);
		
		Ebi ^= Di;
		BCa = ROL(Ebi, 62);
		Ego ^= Do;
		BCe = ROL(Ego, 55);
		Eku ^= Du;
		BCi = ROL(Eku, 39);
		Ema ^= Da;
		BCo = ROL(Ema, 41);
		Ese ^= De;
		BCu = ROL(Ese, 2);
		Asa = BCa ^((~BCe) & BCi);
		Ase = BCe ^((~BCi) & BCo);
		Asi = BCi ^((~BCo) & BCu);
		Aso = BCo ^((~BCu) & BCa);
		Asu = BCu ^((~BCa) & BCe);
	}
	{
		UINT64 *out64 = (UINT64 *)out;
		out64[ 0] = Aba;
		out64[ 1] = Abe;
		out64[ 2] = Abi;
		out64[ 3] = Abo;
	}
}

void keccak_regenhash(struct work *work)
{
	uint256 result;
	
	unsigned int data[20], datacopy[20]; // aligned for flip80
	memcpy(datacopy, work->data, 80);
	flip80(data, datacopy); 
	keccak1((unsigned char*)&result, (unsigned char*)data, 80);
	
	memcpy(work->hash, &result, 32);
}

bool keccak_prepare_work(struct thr_info __maybe_unused *thr, struct work *work)
{
	unsigned int src[20], dst[20]; // aligned for flip80
	int i;
	memcpy(src, work->data, 80);
	flip80(dst, src);
	memcpy(work->blk.keccak_data, dst, 80);
	return true;
}
