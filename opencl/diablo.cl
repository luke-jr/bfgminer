/*
 *  DiabloMiner - OpenCL miner for BitCoin
 *  Copyright (C) 2012, 2013 Con Kolivas <kernel@kolivas.org>
 *  Copyright (C) 2010, 2011, 2012 Patrick McFarland <diablod3@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more detail).
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// kernel-interface: diablo SHA256d

#ifdef VECTORS4
	typedef uint4 z;
#elif defined(VECTORS2)
	typedef uint2 z;
#else
	typedef uint z;
#endif

#ifdef BITALIGN
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#define Zrotr(a, b) amd_bitalign((z)a, (z)a, (z)(32 - b))
#else
#define Zrotr(a, b) rotate((z)a, (z)b)
#endif

#ifdef BFI_INT
#define ZCh(a, b, c) amd_bytealign(a, b, c)
#define ZMa(a, b, c) amd_bytealign((c ^ a), (b), (a))
#else
#define ZCh(a, b, c) bitselect((z)c, (z)b, (z)a)
#define ZMa(a, b, c) bitselect((z)a, (z)b, (z)c ^ (z)a)
#endif

/* These constants are not the classic SHA256 constants but the order that
 * constants are used in this kernel.
 */
__constant uint K[] = {
	0xd807aa98U,
	0x12835b01U,
	0x243185beU,
	0x550c7dc3U,
	0x72be5d74U,
	0x80deb1feU,
	0x9bdc06a7U,
	0xc19bf3f4U,
	0x0fc19dc6U,
	0x240ca1ccU,
	0x80000000U, // 10
	0x2de92c6fU,
	0x4a7484aaU,
	0x00000280U,
	0x5cb0a9dcU,
	0x76f988daU,
	0x983e5152U,
	0xa831c66dU,
	0xb00327c8U,
	0xbf597fc7U,
	0xc6e00bf3U, // 20
	0x00A00055U,
	0xd5a79147U,
	0x06ca6351U,
	0x14292967U,
	0x27b70a85U,
	0x2e1b2138U,
	0x4d2c6dfcU,
	0x53380d13U,
	0x650a7354U,
	0x766a0abbU, // 30
	0x81c2c92eU,
	0x92722c85U,
	0xa2bfe8a1U,
	0xa81a664bU,
	0xc24b8b70U,
	0xc76c51a3U,
	0xd192e819U,
	0xd6990624U,
	0xf40e3585U,
	0x106aa070U, // 40
	0x19a4c116U,
	0x1e376c08U,
	0x2748774cU,
	0x34b0bcb5U,
	0x391c0cb3U,
	0x4ed8aa4aU,
	0x5b9cca4fU,
	0x682e6ff3U,
	0x748f82eeU,
	0x78a5636fU, // 50
	0x84c87814U,
	0x8cc70208U,
	0x90befffaU,
	0xa4506cebU,
	0xbef9a3f7U,
	0xc67178f2U,
	0x98c7e2a2U,
	0x90bb1e3cU,
	0x510e527fU,
	0x9b05688cU, // 60
	0xfc08884dU,
	0x3c6ef372U,
	0x50c6645bU,
	0x6a09e667U,
	0xbb67ae85U,
	0x3ac42e24U,
	0xd21ea4fdU,
	0x59f111f1U,
	0x923f82a4U,
	0xab1c5ed5U, // 70
	0x5807aa98U,
	0xc19bf274U,
	0xe49b69c1U,
	0x00a00000U,
	0xefbe4786U,
	0x00000100U,
	0x11002000U,
	0x00400022U,
	0x136032EDU
};

#define ZR25(n) ((Zrotr((n), 25) ^ Zrotr((n), 14) ^ ((n) >> 3U)))
#define ZR15(n) ((Zrotr((n), 15) ^ Zrotr((n), 13) ^ ((n) >> 10U)))
#define ZR26(n) ((Zrotr((n), 26) ^ Zrotr((n), 21) ^ Zrotr((n), 7)))
#define ZR30(n) ((Zrotr((n), 30) ^ Zrotr((n), 19) ^ Zrotr((n), 10)))

__kernel
__attribute__((vec_type_hint(z)))
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
void search(
#ifndef GOFFSET
    const z base,
#endif
    const uint PreVal4_state0, const uint PreVal4_state0_k7,
    const uint PreVal4_T1,
    const uint W18, const uint W19,
    const uint W16, const uint W17,
    const uint W16_plus_K16, const uint W17_plus_K17,
    const uint W31, const uint W32,
    const uint d1, const uint b1, const uint c1,
    const uint h1, const uint f1, const uint g1,
    const uint c1_plus_k5, const uint b1_plus_k6,
    const uint state0, const uint state1, const uint state2, const uint state3,
    const uint state4, const uint state5, const uint state6, const uint state7,
    volatile __global uint * output)
{

  z ZA[930];

#ifdef GOFFSET
	const z Znonce = (uint)(get_global_id(0));
#else
	const z Znonce = base + (uint)(get_global_id(0));
#endif

    ZA[15] = Znonce + PreVal4_state0;
    
    ZA[16] = (ZCh(ZA[15], b1, c1) + d1) + ZR26(ZA[15]);
    ZA[26] = Znonce + PreVal4_T1;
    
    ZA[27] = ZMa(f1, g1, ZA[26]) + ZR30(ZA[26]);
    ZA[17] = ZA[16] + h1;
    
    ZA[19] = (ZCh(ZA[17], ZA[15], b1) + c1_plus_k5) + ZR26(ZA[17]);
    ZA[28] = ZA[27] + ZA[16];
    
    ZA[548] = ZMa(ZA[26], f1, ZA[28]) + ZR30(ZA[28]);
    ZA[20] = ZA[19] + g1;
    
    ZA[22] = (ZCh(ZA[20], ZA[17], ZA[15]) + b1_plus_k6) + ZR26(ZA[20]);
    ZA[29] = ZA[548] + ZA[19];
    
    ZA[549] = ZMa(ZA[28], ZA[26], ZA[29]) + ZR30(ZA[29]);
    ZA[23] = ZA[22] + f1;
    
    ZA[24] = ZCh(ZA[23], ZA[20], ZA[17]) + ZR26(ZA[23]);
    ZA[180] = Znonce + PreVal4_state0_k7;
    ZA[30] = ZA[549] + ZA[22];
    
    ZA[31] = ZMa(ZA[29], ZA[28], ZA[30]) + ZR30(ZA[30]);
    ZA[181] = ZA[180] + ZA[24];
    
    ZA[182] = ZA[181] + ZA[26];
    ZA[183] = ZA[181] + ZA[31];
    ZA[18] = ZA[17] + K[0];
    
    ZA[186] = (ZCh(ZA[182], ZA[23], ZA[20]) + ZA[18]) + ZR26(ZA[182]);
    ZA[184] = ZMa(ZA[30], ZA[29], ZA[183]) + ZR30(ZA[183]);
    
    ZA[187] = ZA[186] + ZA[28];
    ZA[188] = ZA[186] + ZA[184];
    ZA[21] = ZA[20] + K[1];
    
    ZA[191] = (ZCh(ZA[187], ZA[182], ZA[23]) + ZA[21]) + ZR26(ZA[187]);
    ZA[189] = ZMa(ZA[183], ZA[30], ZA[188]) + ZR30(ZA[188]);
    
    ZA[192] = ZA[191] + ZA[29];
    ZA[193] = ZA[191] + ZA[189];
    ZA[25] = ZA[23] + K[2];
    
    ZA[196] = (ZCh(ZA[192], ZA[187], ZA[182]) + ZA[25]) + ZR26(ZA[192]);
    ZA[194] = ZMa(ZA[188], ZA[183], ZA[193]) + ZR30(ZA[193]);
    
    ZA[197] = ZA[196] + ZA[30];
    ZA[198] = ZA[196] + ZA[194];
    ZA[185] = ZA[182] + K[3];
    
    ZA[201] = (ZCh(ZA[197], ZA[192], ZA[187]) + ZA[185]) + ZR26(ZA[197]);
    ZA[199] = ZMa(ZA[193], ZA[188], ZA[198]) + ZR30(ZA[198]);
    
    ZA[202] = ZA[201] + ZA[183];
    ZA[203] = ZA[201] + ZA[199];
    ZA[190] = ZA[187] + K[4];
    
    ZA[206] = (ZCh(ZA[202], ZA[197], ZA[192]) + ZA[190]) + ZR26(ZA[202]);
    ZA[204] = ZMa(ZA[198], ZA[193], ZA[203]) + ZR30(ZA[203]);
    
    ZA[207] = ZA[206] + ZA[188];
    ZA[208] = ZA[206] + ZA[204];
    ZA[195] = ZA[192] + K[5];
    
    ZA[211] = (ZCh(ZA[207], ZA[202], ZA[197]) + ZA[195]) + ZR26(ZA[207]);
    ZA[209] = ZMa(ZA[203], ZA[198], ZA[208]) + ZR30(ZA[208]);
    
    ZA[212] = ZA[193] + ZA[211];
    ZA[213] = ZA[211] + ZA[209];
    ZA[200] = ZA[197] + K[6];
    
    ZA[216] = (ZCh(ZA[212], ZA[207], ZA[202]) + ZA[200]) + ZR26(ZA[212]);
    ZA[214] = ZMa(ZA[208], ZA[203], ZA[213]) + ZR30(ZA[213]);
    
    ZA[217] = ZA[198] + ZA[216];
    ZA[218] = ZA[216] + ZA[214];
    ZA[205] = ZA[202] + K[7];
    
    ZA[220] = (ZCh(ZA[217], ZA[212], ZA[207]) + ZA[205]) + ZR26(ZA[217]);
    ZA[219] = ZMa(ZA[213], ZA[208], ZA[218]) + ZR30(ZA[218]);
    
    ZA[222] = ZA[203] + ZA[220];
    ZA[223] = ZA[220] + ZA[219];
    ZA[210] = ZA[207] + W16_plus_K16;
    
    ZA[226] = (ZCh(ZA[222], ZA[217], ZA[212]) + ZA[210]) + ZR26(ZA[222]);
    ZA[225] = ZMa(ZA[218], ZA[213], ZA[223]) + ZR30(ZA[223]);
    
    ZA[0] = ZR25(Znonce) + W18;
    ZA[228] = ZA[226] + ZA[225];
    ZA[227] = ZA[208] + ZA[226];
    ZA[215] = ZA[212] + W17_plus_K17;
    
    ZA[231] = (ZCh(ZA[227], ZA[222], ZA[217]) + ZA[215]) + ZR26(ZA[227]);
    ZA[229] = ZMa(ZA[223], ZA[218], ZA[228]) + ZR30(ZA[228]);
    ZA[1] = ZA[0] + K[8];
    
    ZA[232] = ZA[213] + ZA[231];
    ZA[233] = ZA[231] + ZA[229];
    ZA[221] = ZA[217] + ZA[1];
    ZA[32] = Znonce + W19;
    
    ZA[236] = (ZCh(ZA[232], ZA[227], ZA[222]) + ZA[221]) + ZR26(ZA[232]);
    ZA[234] = ZMa(ZA[228], ZA[223], ZA[233]) + ZR30(ZA[233]);
    ZA[33] = ZA[32] + K[9];
    
    ZA[3] = ZR15(ZA[0]) + K[10];
    ZA[238] = ZA[236] + ZA[234];
    ZA[237] = ZA[218] + ZA[236];
    ZA[224] = ZA[222] + ZA[33];
    
    ZA[241] = (ZCh(ZA[237], ZA[232], ZA[227]) + ZA[224]) + ZR26(ZA[237]);
    ZA[239] = ZMa(ZA[233], ZA[228], ZA[238]) + ZR30(ZA[238]);
    ZA[4] = ZA[3] + K[11];
    
    ZA[35] = ZR15(ZA[32]);
    ZA[243] = ZA[241] + ZA[239];
    ZA[242] = ZA[223] + ZA[241];
    ZA[230] = ZA[227] + ZA[4];
    
    ZA[246] = (ZCh(ZA[242], ZA[237], ZA[232]) + ZA[230]) + ZR26(ZA[242]);
    ZA[244] = ZMa(ZA[238], ZA[233], ZA[243]) + ZR30(ZA[243]);
    ZA[36] = ZA[35] + K[12];
    
    ZA[7] = ZR15(ZA[3]) + K[13];
    ZA[248] = ZA[246] + ZA[244];
    ZA[247] = ZA[228] + ZA[246];
    ZA[235] = ZA[232] + ZA[36];
    
    ZA[251] = (ZCh(ZA[247], ZA[242], ZA[237]) + ZA[235]) + ZR26(ZA[247]);
    ZA[249] = ZMa(ZA[243], ZA[238], ZA[248]) + ZR30(ZA[248]);
    ZA[8] = ZA[7] + K[14];
    
    ZA[38] = ZR15(ZA[35]) + W16;
    ZA[253] = ZA[251] + ZA[249];
    ZA[252] = ZA[233] + ZA[251];
    ZA[240] = ZA[237] + ZA[8];
    
    ZA[256] = (ZCh(ZA[252], ZA[247], ZA[242]) + ZA[240]) + ZR26(ZA[252]);
    ZA[254] = ZMa(ZA[248], ZA[243], ZA[253]) + ZR30(ZA[253]);
    ZA[40] = ZA[38] + K[15];
    
    ZA[10] = ZR15(ZA[7]) + W17;
    ZA[258] = ZA[256] + ZA[254];
    ZA[257] = ZA[238] + ZA[256];
    ZA[245] = ZA[242] + ZA[40];
    
    ZA[261] = (ZCh(ZA[257], ZA[252], ZA[247]) + ZA[245]) + ZR26(ZA[257]);
    ZA[259] = ZMa(ZA[253], ZA[248], ZA[258]) + ZR30(ZA[258]);
    ZA[13] = ZA[10] + K[16];
    
    ZA[43] = ZR15(ZA[38]) + ZA[0];
    ZA[263] = ZA[261] + ZA[259];
    ZA[262] = ZA[243] + ZA[261];
    ZA[250] = ZA[247] + ZA[13];
    
    ZA[266] = (ZCh(ZA[262], ZA[257], ZA[252]) + ZA[250]) + ZR26(ZA[262]);
    ZA[264] = ZMa(ZA[258], ZA[253], ZA[263]) + ZR30(ZA[263]);
    ZA[11] = ZR15(ZA[10]);
    ZA[45] = ZA[43] + K[17];
    
    ZA[52] = ZA[11] + ZA[32];
    ZA[267] = ZA[248] + ZA[266];
    ZA[255] = ZA[252] + ZA[45];
    ZA[268] = ZA[266] + ZA[264];
    
    ZA[271] = (ZCh(ZA[267], ZA[262], ZA[257]) + ZA[255]) + ZR26(ZA[267]);
    ZA[269] = ZMa(ZA[263], ZA[258], ZA[268]) + ZR30(ZA[268]);
    ZA[54] = ZA[52] + K[18];
    
    ZA[48] = ZR15(ZA[43]) + ZA[3];
    ZA[273] = ZA[271] + ZA[269];
    ZA[272] = ZA[253] + ZA[271];
    ZA[260] = ZA[257] + ZA[54];
    
    ZA[276] = (ZCh(ZA[272], ZA[267], ZA[262]) + ZA[260]) + ZR26(ZA[272]);
    ZA[274] = ZMa(ZA[268], ZA[263], ZA[273]) + ZR30(ZA[273]);
    ZA[49] = ZA[48] + K[19];
    
    ZA[61] = ZR15(ZA[52]) + ZA[35];
    ZA[278] = ZA[276] + ZA[274];
    ZA[277] = ZA[258] + ZA[276];
    ZA[265] = ZA[262] + ZA[49];
    
    ZA[281] = (ZCh(ZA[277], ZA[272], ZA[267]) + ZA[265]) + ZR26(ZA[277]);
    ZA[279] = ZMa(ZA[273], ZA[268], ZA[278]) + ZR30(ZA[278]);
    ZA[62] = ZA[61] + K[20];
    
    ZA[53] = ZR15(ZA[48]) + ZA[7];
    ZA[283] = ZA[281] + ZA[279];
    ZA[282] = ZA[263] + ZA[281];
    ZA[270] = ZA[267] + ZA[62];
    
    ZA[286] = (ZCh(ZA[282], ZA[277], ZA[272]) + ZA[270]) + ZR26(ZA[282]);
    ZA[284] = ZMa(ZA[278], ZA[273], ZA[283]) + ZR30(ZA[283]);
    ZA[39] = ZA[38] + K[21];
    ZA[55] = ZA[53] + K[22];
    
    ZA[66] = ZR15(ZA[61]) + ZA[39];
    ZA[288] = ZA[286] + ZA[284];
    ZA[287] = ZA[268] + ZA[286];
    ZA[275] = ZA[272] + ZA[55];
    
    ZA[291] = (ZCh(ZA[287], ZA[282], ZA[277]) + ZA[275]) + ZR26(ZA[287]);
    ZA[289] = ZMa(ZA[283], ZA[278], ZA[288]) + ZR30(ZA[288]);
    ZA[12] = ZA[10] + W31;
    ZA[68] = ZA[66] + K[23];
    
    ZA[67] = ZR15(ZA[53]) + ZA[12];
    ZA[293] = ZA[291] + ZA[289];
    ZA[292] = ZA[273] + ZA[291];
    ZA[280] = ZA[277] + ZA[68];
    
    ZA[296] = (ZCh(ZA[292], ZA[287], ZA[282]) + ZA[280]) + ZR26(ZA[292]);
    ZA[294] = ZMa(ZA[288], ZA[283], ZA[293]) + ZR30(ZA[293]);
    ZA[2] = ZR25(ZA[0]);
    ZA[69] = ZA[67] + K[24];
    ZA[44] = ZA[43] + W32;
    
    ZA[75] = ZR15(ZA[66]) + ZA[44];
    ZA[298] = ZA[296] + ZA[294];
    ZA[297] = ZA[278] + ZA[296];
    ZA[285] = ZA[282] + ZA[69];
    ZA[5] = ZA[2] + W17;
    
    ZA[301] = (ZCh(ZA[297], ZA[292], ZA[287]) + ZA[285]) + ZR26(ZA[297]);
    ZA[299] = ZMa(ZA[293], ZA[288], ZA[298]) + ZR30(ZA[298]);
    ZA[56] = ZA[52] + ZA[5];
    ZA[76] = ZA[75] + K[25];
    
    ZA[34] = ZR25(ZA[32]) + ZA[0];
    ZA[70] = ZR15(ZA[67]) + ZA[56];
    ZA[302] = ZA[283] + ZA[301];
    ZA[303] = ZA[301] + ZA[299];
    ZA[290] = ZA[287] + ZA[76];
    
    ZA[306] = (ZCh(ZA[302], ZA[297], ZA[292]) + ZA[290]) + ZR26(ZA[302]);
    ZA[304] = ZMa(ZA[298], ZA[293], ZA[303]) + ZR30(ZA[303]);
    ZA[6] = ZR25(ZA[3]);
    ZA[77] = ZA[70] + K[26];
    ZA[50] = ZA[34] + ZA[48];
    
    ZA[78] = ZR15(ZA[75]) + ZA[50];
    ZA[308] = ZA[306] + ZA[304];
    ZA[307] = ZA[288] + ZA[306];
    ZA[295] = ZA[292] + ZA[77];
    ZA[41] = ZA[32] + ZA[6];
    
    ZA[311] = (ZCh(ZA[307], ZA[302], ZA[297]) + ZA[295]) + ZR26(ZA[307]);
    ZA[309] = ZMa(ZA[303], ZA[298], ZA[308]) + ZR30(ZA[308]);
    ZA[63] = ZA[41] + ZA[61];
    ZA[85] = ZA[78] + K[27];
    
    ZA[37] = ZR25(ZA[35]) + ZA[3];
    ZA[79] = ZR15(ZA[70]) + ZA[63];
    ZA[312] = ZA[293] + ZA[311];
    ZA[313] = ZA[311] + ZA[309];
    ZA[300] = ZA[297] + ZA[85];
    
    ZA[316] = (ZCh(ZA[312], ZA[307], ZA[302]) + ZA[300]) + ZR26(ZA[312]);
    ZA[314] = ZMa(ZA[308], ZA[303], ZA[313]) + ZR30(ZA[313]);
    ZA[9] = ZR25(ZA[7]);
    ZA[86] = ZA[79] + K[28];
    ZA[57] = ZA[37] + ZA[53];
    
    ZA[87] = ZR15(ZA[78]) + ZA[57];
    ZA[318] = ZA[316] + ZA[314];
    ZA[317] = ZA[298] + ZA[316];
    ZA[305] = ZA[302] + ZA[86];
    ZA[46] = ZA[35] + ZA[9];
    
    ZA[321] = (ZCh(ZA[317], ZA[312], ZA[307]) + ZA[305]) + ZR26(ZA[317]);
    ZA[319] = ZMa(ZA[313], ZA[308], ZA[318]) + ZR30(ZA[318]);
    ZA[71] = ZA[46] + ZA[66];
    ZA[92] = ZA[87] + K[29];
    
    ZA[42] = ZR25(ZA[38]) + ZA[7];
    ZA[88] = ZR15(ZA[79]) + ZA[71];
    ZA[322] = ZA[303] + ZA[321];
    ZA[323] = ZA[321] + ZA[319];
    ZA[310] = ZA[307] + ZA[92];
    
    ZA[326] = (ZCh(ZA[322], ZA[317], ZA[312]) + ZA[310]) + ZR26(ZA[322]);
    ZA[324] = ZMa(ZA[318], ZA[313], ZA[323]) + ZR30(ZA[323]);
    ZA[14] = ZR25(ZA[10]);
    ZA[93] = ZA[88] + K[30];
    ZA[72] = ZA[42] + ZA[67];
    
    ZA[94] = ZR15(ZA[87]) + ZA[72];
    ZA[328] = ZA[326] + ZA[324];
    ZA[327] = ZA[308] + ZA[326];
    ZA[315] = ZA[312] + ZA[93];
    ZA[51] = ZA[38] + ZA[14];
    
    ZA[331] = (ZCh(ZA[327], ZA[322], ZA[317]) + ZA[315]) + ZR26(ZA[327]);
    ZA[329] = ZMa(ZA[323], ZA[318], ZA[328]) + ZR30(ZA[328]);
    ZA[80] = ZA[51] + ZA[75];
    ZA[100] = ZA[94] + K[31];
    
    ZA[47] = ZR25(ZA[43]) + ZA[10];
    ZA[95] = ZR15(ZA[88]) + ZA[80];
    ZA[332] = ZA[313] + ZA[331];
    ZA[333] = ZA[331] + ZA[329];
    ZA[320] = ZA[317] + ZA[100];
    
    ZA[336] = (ZCh(ZA[332], ZA[327], ZA[322]) + ZA[320]) + ZR26(ZA[332]);
    ZA[334] = ZMa(ZA[328], ZA[323], ZA[333]) + ZR30(ZA[333]);
    ZA[81] = ZA[47] + ZA[70];
    ZA[101] = ZA[95] + K[32];
    
    ZA[58] = ZR25(ZA[52]) + ZA[43];
    ZA[102] = ZR15(ZA[94]) + ZA[81];
    ZA[337] = ZA[318] + ZA[336];
    ZA[338] = ZA[336] + ZA[334];
    ZA[325] = ZA[322] + ZA[101];
    
    ZA[341] = (ZCh(ZA[337], ZA[332], ZA[327]) + ZA[325]) + ZR26(ZA[337]);
    ZA[339] = ZMa(ZA[333], ZA[328], ZA[338]) + ZR30(ZA[338]);
    ZA[89] = ZA[58] + ZA[78];
    ZA[108] = ZA[102] + K[33];
    
    ZA[59] = ZR25(ZA[48]) + ZA[52];
    ZA[103] = ZR15(ZA[95]) + ZA[89];
    ZA[342] = ZA[323] + ZA[341];
    ZA[343] = ZA[341] + ZA[339];
    ZA[330] = ZA[327] + ZA[108];
    
    ZA[346] = (ZCh(ZA[342], ZA[337], ZA[332]) + ZA[330]) + ZR26(ZA[342]);
    ZA[344] = ZMa(ZA[338], ZA[333], ZA[343]) + ZR30(ZA[343]);
    ZA[90] = ZA[59] + ZA[79];
    ZA[109] = ZA[103] + K[34];
    
    ZA[64] = ZR25(ZA[61]) + ZA[48];
    ZA[110] = ZR15(ZA[102]) + ZA[90];
    ZA[347] = ZA[328] + ZA[346];
    ZA[348] = ZA[346] + ZA[344];
    ZA[335] = ZA[332] + ZA[109];
    
    ZA[351] = (ZCh(ZA[347], ZA[342], ZA[337]) + ZA[335]) + ZR26(ZA[347]);
    ZA[349] = ZMa(ZA[343], ZA[338], ZA[348]) + ZR30(ZA[348]);
    ZA[60] = ZR25(ZA[53]);
    ZA[116] = ZA[110] + K[35];
    ZA[96] = ZA[87] + ZA[64];
    
    ZA[111] = ZR15(ZA[103]) + ZA[96];
    ZA[353] = ZA[351] + ZA[349];
    ZA[352] = ZA[333] + ZA[351];
    ZA[340] = ZA[337] + ZA[116];
    ZA[65] = ZA[60] + ZA[61];
    
    ZA[356] = (ZCh(ZA[352], ZA[347], ZA[342]) + ZA[340]) + ZR26(ZA[352]);
    ZA[354] = ZMa(ZA[348], ZA[343], ZA[353]) + ZR30(ZA[353]);
    ZA[97] = ZA[88] + ZA[65];
    ZA[117] = ZA[111] + K[36];
    
    ZA[73] = ZR25(ZA[66]) + ZA[53];
    ZA[118] = ZR15(ZA[110]) + ZA[97];
    ZA[357] = ZA[338] + ZA[356];
    ZA[358] = ZA[356] + ZA[354];
    ZA[345] = ZA[342] + ZA[117];
    
    ZA[361] = (ZCh(ZA[357], ZA[352], ZA[347]) + ZA[345]) + ZR26(ZA[357]);
    ZA[359] = ZMa(ZA[353], ZA[348], ZA[358]) + ZR30(ZA[358]);
    ZA[104] = ZA[73] + ZA[94];
    ZA[124] = ZA[118] + K[37];
    
    ZA[74] = ZR25(ZA[67]) + ZA[66];
    ZA[119] = ZR15(ZA[111]) + ZA[104];
    ZA[362] = ZA[343] + ZA[361];
    ZA[363] = ZA[361] + ZA[359];
    ZA[350] = ZA[347] + ZA[124];
    
    ZA[366] = (ZCh(ZA[362], ZA[357], ZA[352]) + ZA[350]) + ZR26(ZA[362]);
    ZA[364] = ZMa(ZA[358], ZA[353], ZA[363]) + ZR30(ZA[363]);
    ZA[105] = ZA[74] + ZA[95];
    ZA[125] = ZA[119] + K[38];
    
    ZA[82] = ZR25(ZA[75]) + ZA[67];
    ZA[126] = ZR15(ZA[118]) + ZA[105];
    ZA[367] = ZA[348] + ZA[366];
    ZA[368] = ZA[366] + ZA[364];
    ZA[355] = ZA[352] + ZA[125];
    
    ZA[371] = (ZCh(ZA[367], ZA[362], ZA[357]) + ZA[355]) + ZR26(ZA[367]);
    ZA[369] = ZMa(ZA[363], ZA[358], ZA[368]) + ZR30(ZA[368]);
    ZA[112] = ZA[102] + ZA[82];
    ZA[132] = ZA[126] + K[39];
    
    ZA[83] = ZR25(ZA[70]) + ZA[75];
    ZA[127] = ZR15(ZA[119]) + ZA[112];
    ZA[372] = ZA[353] + ZA[371];
    ZA[373] = ZA[371] + ZA[369];
    ZA[360] = ZA[357] + ZA[132];
    
    ZA[376] = (ZCh(ZA[372], ZA[367], ZA[362]) + ZA[360]) + ZR26(ZA[372]);
    ZA[374] = ZMa(ZA[368], ZA[363], ZA[373]) + ZR30(ZA[373]);
    ZA[113] = ZA[103] + ZA[83];
    ZA[133] = ZA[127] + K[40];
    
    ZA[84] = ZR25(ZA[78]) + ZA[70];
    ZA[134] = ZR15(ZA[126]) + ZA[113];
    ZA[377] = ZA[358] + ZA[376];
    ZA[378] = ZA[376] + ZA[374];
    ZA[365] = ZA[362] + ZA[133];
    
    ZA[381] = (ZCh(ZA[377], ZA[372], ZA[367]) + ZA[365]) + ZR26(ZA[377]);
    ZA[379] = ZMa(ZA[373], ZA[368], ZA[378]) + ZR30(ZA[378]);
    ZA[120] = ZA[110] + ZA[84];
    ZA[140] = ZA[134] + K[41];
    
    ZA[91] = ZR25(ZA[79]) + ZA[78];
    ZA[135] = ZR15(ZA[127]) + ZA[120];
    ZA[382] = ZA[363] + ZA[381];
    ZA[383] = ZA[381] + ZA[379];
    ZA[370] = ZA[367] + ZA[140];
    
    ZA[386] = (ZCh(ZA[382], ZA[377], ZA[372]) + ZA[370]) + ZR26(ZA[382]);
    ZA[384] = ZMa(ZA[378], ZA[373], ZA[383]) + ZR30(ZA[383]);
    ZA[121] = ZA[111] + ZA[91];
    ZA[141] = ZA[135] + K[42];
    
    ZA[98] = ZR25(ZA[87]) + ZA[79];
    ZA[142] = ZR15(ZA[134]) + ZA[121];
    ZA[387] = ZA[368] + ZA[386];
    ZA[388] = ZA[386] + ZA[384];
    ZA[375] = ZA[372] + ZA[141];
    
    ZA[391] = (ZCh(ZA[387], ZA[382], ZA[377]) + ZA[375]) + ZR26(ZA[387]);
    ZA[389] = ZMa(ZA[383], ZA[378], ZA[388]) + ZR30(ZA[388]);
    ZA[128] = ZA[118] + ZA[98];
    ZA[147] = ZA[142] + K[43];
    
    ZA[99] = ZR25(ZA[88]) + ZA[87];
    ZA[143] = ZR15(ZA[135]) + ZA[128];
    ZA[392] = ZA[373] + ZA[391];
    ZA[393] = ZA[391] + ZA[389];
    ZA[380] = ZA[377] + ZA[147];
    
    ZA[396] = (ZCh(ZA[392], ZA[387], ZA[382]) + ZA[380]) + ZR26(ZA[392]);
    ZA[394] = ZMa(ZA[388], ZA[383], ZA[393]) + ZR30(ZA[393]);
    ZA[129] = ZA[119] + ZA[99];
    ZA[148] = ZA[143] + K[44];
    
    ZA[106] = ZR25(ZA[94]) + ZA[88];
    ZA[149] = ZR15(ZA[142]) + ZA[129];
    ZA[397] = ZA[378] + ZA[396];
    ZA[398] = ZA[396] + ZA[394];
    ZA[385] = ZA[382] + ZA[148];
    
    ZA[401] = (ZCh(ZA[397], ZA[392], ZA[387]) + ZA[385]) + ZR26(ZA[397]);
    ZA[399] = ZMa(ZA[393], ZA[388], ZA[398]) + ZR30(ZA[398]);
    ZA[136] = ZA[126] + ZA[106];
    ZA[153] = ZA[149] + K[45];
    
    ZA[107] = ZR25(ZA[95]) + ZA[94];
    ZA[150] = ZR15(ZA[143]) + ZA[136];
    ZA[402] = ZA[383] + ZA[401];
    ZA[403] = ZA[401] + ZA[399];
    ZA[390] = ZA[387] + ZA[153];
    
    ZA[406] = (ZCh(ZA[402], ZA[397], ZA[392]) + ZA[390]) + ZR26(ZA[402]);
    ZA[404] = ZMa(ZA[398], ZA[393], ZA[403]) + ZR30(ZA[403]);
    ZA[137] = ZA[127] + ZA[107];
    ZA[154] = ZA[150] + K[46];
    
    ZA[114] = ZR25(ZA[102]) + ZA[95];
    ZA[155] = ZR15(ZA[149]) + ZA[137];
    ZA[407] = ZA[388] + ZA[406];
    ZA[408] = ZA[406] + ZA[404];
    ZA[395] = ZA[392] + ZA[154];
    
    ZA[411] = (ZCh(ZA[407], ZA[402], ZA[397]) + ZA[395]) + ZR26(ZA[407]);
    ZA[409] = ZMa(ZA[403], ZA[398], ZA[408]) + ZR30(ZA[408]);
    ZA[144] = ZA[134] + ZA[114];
    ZA[159] = ZA[155] + K[47];
    
    ZA[115] = ZR25(ZA[103]) + ZA[102];
    ZA[156] = ZR15(ZA[150]) + ZA[144];
    ZA[412] = ZA[393] + ZA[411];
    ZA[413] = ZA[411] + ZA[409];
    ZA[400] = ZA[397] + ZA[159];
    
    ZA[416] = (ZCh(ZA[412], ZA[407], ZA[402]) + ZA[400]) + ZR26(ZA[412]);
    ZA[414] = ZMa(ZA[408], ZA[403], ZA[413]) + ZR30(ZA[413]);
    ZA[145] = ZA[135] + ZA[115];
    ZA[160] = ZA[156] + K[48];
    
    ZA[122] = ZR25(ZA[110]) + ZA[103];
    ZA[161] = ZR15(ZA[155]) + ZA[145];
    ZA[417] = ZA[398] + ZA[416];
    ZA[418] = ZA[416] + ZA[414];
    ZA[405] = ZA[402] + ZA[160];
    
    ZA[421] = (ZCh(ZA[417], ZA[412], ZA[407]) + ZA[405]) + ZR26(ZA[417]);
    ZA[419] = ZMa(ZA[413], ZA[408], ZA[418]) + ZR30(ZA[418]);
    ZA[151] = ZA[142] + ZA[122];
    ZA[165] = ZA[161] + K[49];
    
    ZA[123] = ZR25(ZA[111]) + ZA[110];
    ZA[162] = ZR15(ZA[156]) + ZA[151];
    ZA[422] = ZA[403] + ZA[421];
    ZA[423] = ZA[421] + ZA[419];
    ZA[410] = ZA[407] + ZA[165];
    
    ZA[426] = (ZCh(ZA[422], ZA[417], ZA[412]) + ZA[410]) + ZR26(ZA[422]);
    ZA[424] = ZMa(ZA[418], ZA[413], ZA[423]) + ZR30(ZA[423]);
    ZA[152] = ZA[143] + ZA[123];
    ZA[166] = ZA[162] + K[50];
    
    ZA[130] = ZR25(ZA[118]) + ZA[111];
    ZA[167] = ZR15(ZA[161]) + ZA[152];
    ZA[427] = ZA[408] + ZA[426];
    ZA[428] = ZA[426] + ZA[424];
    ZA[415] = ZA[412] + ZA[166];
    
    ZA[431] = (ZCh(ZA[427], ZA[422], ZA[417]) + ZA[415]) + ZR26(ZA[427]);
    ZA[429] = ZMa(ZA[423], ZA[418], ZA[428]) + ZR30(ZA[428]);
    ZA[157] = ZA[149] + ZA[130];
    ZA[170] = ZA[167] + K[51];
    
    ZA[131] = ZR25(ZA[119]) + ZA[118];
    ZA[168] = ZR15(ZA[162]) + ZA[157];
    ZA[432] = ZA[413] + ZA[431];
    ZA[433] = ZA[431] + ZA[429];
    ZA[420] = ZA[417] + ZA[170];
    
    ZA[436] = (ZCh(ZA[432], ZA[427], ZA[422]) + ZA[420]) + ZR26(ZA[432]);
    ZA[434] = ZMa(ZA[428], ZA[423], ZA[433]) + ZR30(ZA[433]);
    ZA[158] = ZA[150] + ZA[131];
    ZA[171] = ZA[168] + K[52];
    
    ZA[138] = ZR25(ZA[126]) + ZA[119];
    ZA[172] = ZR15(ZA[167]) + ZA[158];
    ZA[437] = ZA[418] + ZA[436];
    ZA[438] = ZA[436] + ZA[434];
    ZA[425] = ZA[422] + ZA[171];
    
    ZA[441] = (ZCh(ZA[437], ZA[432], ZA[427]) + ZA[425]) + ZR26(ZA[437]);
    ZA[439] = ZMa(ZA[433], ZA[428], ZA[438]) + ZR30(ZA[438]);
    ZA[163] = ZA[155] + ZA[138];
    ZA[174] = ZA[172] + K[53];
    
    ZA[139] = ZR25(ZA[127]) + ZA[126];
    ZA[173] = ZR15(ZA[168]) + ZA[163];
    ZA[442] = ZA[423] + ZA[441];
    ZA[443] = ZA[441] + ZA[439];
    ZA[430] = ZA[427] + ZA[174];
    
    ZA[445] = (ZCh(ZA[442], ZA[437], ZA[432]) + ZA[430]) + ZR26(ZA[442]);
    ZA[444] = ZMa(ZA[438], ZA[433], ZA[443]) + ZR30(ZA[443]);
    ZA[164] = ZA[156] + ZA[139];
    ZA[175] = ZA[173] + K[54];
    
    ZA[146] = ZR25(ZA[134]) + ZA[127];
    ZA[176] = ZR15(ZA[172]) + ZA[164];
    ZA[446] = ZA[428] + ZA[445];
    ZA[447] = ZA[445] + ZA[444];
    ZA[435] = ZA[432] + ZA[175];
    
    ZA[449] = (ZCh(ZA[446], ZA[442], ZA[437]) + ZA[435]) + ZR26(ZA[446]);
    ZA[448] = ZMa(ZA[443], ZA[438], ZA[447]) + ZR30(ZA[447]);
    ZA[169] = ZA[161] + ZA[146];
    ZA[178] = ZA[176] + K[55];
    
    ZA[177] = ZR15(ZA[173]) + ZA[169];
    ZA[451] = ZA[449] + ZA[448];
    ZA[450] = ZA[433] + ZA[449];
    ZA[440] = ZA[437] + ZA[178];
    
    ZA[453] = (ZCh(ZA[450], ZA[446], ZA[442]) + ZA[440]) + ZR26(ZA[450]);
    ZA[452] = ZMa(ZA[447], ZA[443], ZA[451]) + ZR30(ZA[451]);
    ZA[179] = ZA[177] + K[56];
    
    ZA[454] = ZA[438] + ZA[453];
    ZA[494] = ZA[442] + ZA[179];
    ZA[455] = ZA[453] + ZA[452];
    
    ZA[457] = (ZCh(ZA[454], ZA[450], ZA[446]) + ZA[494]) + ZR26(ZA[454]);
    ZA[456] = ZMa(ZA[451], ZA[447], ZA[455]) + ZR30(ZA[455]);
    
    ZA[459] = ZA[457] + ZA[456];
    
    ZA[461] = ZA[455] + state1;
    ZA[460] = ZA[459] + state0;
    
    ZA[495] = ZA[460] + K[57];
    ZA[469] = ZA[461] + K[58];
    
    ZA[498] = (ZCh(ZA[495], K[59], K[60]) + ZA[469]) + ZR26(ZA[495]);
    ZA[462] = ZA[451] + state2;
    
    ZA[496] = ZA[460] + K[61];
    ZA[506] = ZA[498] + K[62];
    ZA[470] = ZA[462] + K[63];
    
    ZA[507] = (ZCh(ZA[506], ZA[495], K[59]) + ZA[470]) + ZR26(ZA[506]);
    ZA[500] = ZMa(K[64], K[65], ZA[496]) + ZR30(ZA[496]);
    ZA[463] = ZA[447] + state3;
    
    ZA[458] = ZA[443] + ZA[457];
    ZA[499] = ZA[498] + ZA[500];
    ZA[508] = ZA[507] + K[65];
    ZA[473] = ZA[463] + K[66];
    
    ZA[510] = (ZCh(ZA[508], ZA[506], ZA[495]) + ZA[473]) + ZR26(ZA[508]);
    ZA[928] = ZMa(ZA[496], K[64], ZA[499]) + ZR30(ZA[499]);
    ZA[464] = ZA[458] + state4;
    
    ZA[476] = ZA[464] + ZA[460] + K[67];
    ZA[511] = ZA[510] + K[64];
    ZA[509] = ZA[928] + ZA[507];
    ZA[465] = ZA[454] + state5;
    
    ZA[514] = (ZCh(ZA[511], ZA[508], ZA[506]) + ZA[476]) + ZR26(ZA[511]);
    ZA[512] = ZMa(ZA[499], ZA[496], ZA[509]) + ZR30(ZA[509]);
    ZA[478] = ZA[465] + K[68];
    
    ZA[519] = ZA[506] + ZA[478];
    ZA[516] = ZA[496] + ZA[514];
    ZA[513] = ZA[510] + ZA[512];
    ZA[466] = ZA[450] + state6;
    
    ZA[520] = (ZCh(ZA[516], ZA[511], ZA[508]) + ZA[519]) + ZR26(ZA[516]);
    ZA[515] = ZMa(ZA[509], ZA[499], ZA[513]) + ZR30(ZA[513]);
    ZA[480] = ZA[466] + K[69];
    
    ZA[524] = ZA[508] + ZA[480];
    ZA[521] = ZA[499] + ZA[520];
    ZA[517] = ZA[514] + ZA[515];
    ZA[467] = ZA[446] + state7;
    
    ZA[525] = (ZCh(ZA[521], ZA[516], ZA[511]) + ZA[524]) + ZR26(ZA[521]);
    ZA[522] = ZMa(ZA[513], ZA[509], ZA[517]) + ZR30(ZA[517]);
    ZA[484] = ZA[467] + K[70];
    
    ZA[529] = ZA[511] + ZA[484];
    ZA[526] = ZA[509] + ZA[525];
    ZA[523] = ZA[520] + ZA[522];
    
    ZA[530] = (ZCh(ZA[526], ZA[521], ZA[516]) + ZA[529]) + ZR26(ZA[526]);
    ZA[550] = ZMa(ZA[517], ZA[513], ZA[523]) + ZR30(ZA[523]);
    
    ZA[531] = ZA[513] + ZA[530];
    ZA[533] = ZA[516] + K[71];
    ZA[527] = ZA[550] + ZA[525];
    
    ZA[534] = (ZCh(ZA[531], ZA[526], ZA[521]) + ZA[533]) + ZR26(ZA[531]);
    ZA[551] = ZMa(ZA[523], ZA[517], ZA[527]) + ZR30(ZA[527]);
    
    ZA[535] = ZA[517] + ZA[534];
    ZA[538] = ZA[521] + K[1];
    ZA[532] = ZA[551] + ZA[530];
    
    ZA[539] = (ZCh(ZA[535], ZA[531], ZA[526]) + ZA[538]) + ZR26(ZA[535]);
    ZA[552] = ZMa(ZA[527], ZA[523], ZA[532]) + ZR30(ZA[532]);
    
    ZA[540] = ZA[523] + ZA[539];
    ZA[542] = ZA[526] + K[2];
    ZA[536] = ZA[552] + ZA[534];
    
    ZA[543] = (ZCh(ZA[540], ZA[535], ZA[531]) + ZA[542]) + ZR26(ZA[540]);
    ZA[553] = ZMa(ZA[532], ZA[527], ZA[536]) + ZR30(ZA[536]);
    
    ZA[544] = ZA[527] + ZA[543];
    ZA[555] = ZA[531] + K[3];
    ZA[541] = ZA[553] + ZA[539];
    
    ZA[558] = (ZCh(ZA[544], ZA[540], ZA[535]) + ZA[555]) + ZR26(ZA[544]);
    ZA[547] = ZMa(ZA[536], ZA[532], ZA[541]) + ZR30(ZA[541]);
    
    ZA[559] = ZA[532] + ZA[558];
    ZA[556] = ZA[535] + K[4];
    ZA[545] = ZA[547] + ZA[543];
    
    ZA[562] = (ZCh(ZA[559], ZA[544], ZA[540]) + ZA[556]) + ZR26(ZA[559]);
    ZA[561] = ZMa(ZA[541], ZA[536], ZA[545]) + ZR30(ZA[545]);
    
    ZA[563] = ZA[536] + ZA[562];
    ZA[560] = ZA[561] + ZA[558];
    ZA[557] = ZA[540] + K[5];
    
    ZA[568] = (ZCh(ZA[563], ZA[559], ZA[544]) + ZA[557]) + ZR26(ZA[563]);
    ZA[564] = ZMa(ZA[545], ZA[541], ZA[560]) + ZR30(ZA[560]);
    
    ZA[569] = ZA[541] + ZA[568];
    ZA[572] = ZA[544] + K[6];
    ZA[565] = ZA[562] + ZA[564];
    
    ZA[574] = (ZCh(ZA[569], ZA[563], ZA[559]) + ZA[572]) + ZR26(ZA[569]);
    ZA[570] = ZMa(ZA[560], ZA[545], ZA[565]) + ZR30(ZA[565]);
    ZA[468] = ZR25(ZA[461]);
    
    ZA[497] = ZA[468] + ZA[460];
    ZA[575] = ZA[545] + ZA[574];
    ZA[571] = ZA[568] + ZA[570];
    ZA[573] = ZA[559] + K[72];
    
    ZA[578] = (ZCh(ZA[575], ZA[569], ZA[563]) + ZA[573]) + ZR26(ZA[575]);
    ZA[576] = ZMa(ZA[565], ZA[560], ZA[571]) + ZR30(ZA[571]);
    ZA[929] = ZR25(ZA[462]);
    ZA[503] = ZA[497] + 0xe49b69c1U;
    
    ZA[471] = ZA[929] + ZA[461] + K[74];
    ZA[582] = ZA[563] + ZA[503];
    ZA[579] = ZA[560] + ZA[578];
    ZA[577] = ZA[574] + ZA[576];
    
    ZA[583] = (ZCh(ZA[579], ZA[575], ZA[569]) + ZA[582]) + ZR26(ZA[579]);
    ZA[580] = ZMa(ZA[571], ZA[565], ZA[577]) + ZR30(ZA[577]);
    ZA[488] = ZA[471] + K[75];
    
    ZA[472] = ZR25(ZA[463]) + ZA[462];
    ZA[587] = ZA[569] + ZA[488];
    ZA[584] = ZA[565] + ZA[583];
    ZA[581] = ZA[578] + ZA[580];
    
    ZA[588] = (ZCh(ZA[584], ZA[579], ZA[575]) + ZA[587]) + ZR26(ZA[584]);
    ZA[586] = ZMa(ZA[577], ZA[571], ZA[581]) + ZR30(ZA[581]);
    ZA[501] = ZR15(ZA[497]) + ZA[472];
    ZA[475] = ZR15(ZA[471]);
    ZA[926] = ZA[575] + K[8];
    
    ZA[474] = ZA[475] + ZA[463] + ZR25(ZA[464]);
    ZA[927] = ZA[926] + ZA[501];
    ZA[589] = ZA[571] + ZA[588];
    ZA[585] = ZA[583] + ZA[586];
    
    ZA[592] = (ZCh(ZA[589], ZA[584], ZA[579]) + ZA[927]) + ZR26(ZA[589]);
    ZA[590] = ZMa(ZA[581], ZA[577], ZA[585]) + ZR30(ZA[585]);
    ZA[477] = ZR25(ZA[465]) + ZA[464];
    ZA[489] = ZA[474] + K[9];
    
    ZA[518] = ZR15(ZA[501]) + ZA[477];
    ZA[479] = ZR25(ZA[466]);
    ZA[596] = ZA[579] + ZA[489];
    ZA[593] = ZA[577] + ZA[592];
    ZA[591] = ZA[588] + ZA[590];
    
    ZA[597] = (ZCh(ZA[593], ZA[589], ZA[584]) + ZA[596]) + ZR26(ZA[593]);
    ZA[594] = ZMa(ZA[585], ZA[581], ZA[591]) + ZR30(ZA[591]);
    ZA[481] = ZA[479] + ZA[465];
    ZA[601] = ZA[518] + K[11];
    
    ZA[482] = ZR15(ZA[474]) + ZA[481];
    ZA[602] = ZA[584] + ZA[601];
    ZA[598] = ZA[581] + ZA[597];
    ZA[595] = ZA[592] + ZA[594];
    
    ZA[632] = (ZCh(ZA[598], ZA[593], ZA[589]) + ZA[602]) + ZR26(ZA[598]);
    ZA[599] = ZMa(ZA[591], ZA[585], ZA[595]) + ZR30(ZA[595]);
    ZA[483] = ZA[466] + K[76] + ZR25(ZA[467]);
    ZA[490] = ZA[482] + K[12];
    
    ZA[528] = ZR15(ZA[518]) + ZA[483];
    ZA[736] = ZA[585] + ZA[632];
    ZA[605] = ZA[589] + ZA[490];
    ZA[600] = ZA[597] + ZA[599];
    ZA[485] = ZA[467] + K[77];
    
    ZA[738] = (ZCh(ZA[736], ZA[598], ZA[593]) + ZA[605]) + ZR26(ZA[736]);
    ZA[744] = ZMa(ZA[595], ZA[591], ZA[600]) + ZR30(ZA[600]);
    ZA[487] = ZR15(ZA[482]) + ZA[485];
    ZA[603] = ZA[528] + K[14];
    
    ZA[502] = ZA[497] + ZA[487];
    ZA[739] = ZA[591] + ZA[738];
    ZA[604] = ZA[593] + ZA[603];
    ZA[737] = ZA[744] + ZA[632];
    
    ZA[741] = (ZCh(ZA[739], ZA[736], ZA[598]) + ZA[604]) + ZR26(ZA[739]);
    ZA[745] = ZMa(ZA[600], ZA[595], ZA[737]) + ZR30(ZA[737]);
    ZA[486] = ZA[471] + K[10];
    ZA[606] = ZA[502] + K[15];
    
    ZA[537] = ZR15(ZA[528]) + ZA[486];
    ZA[742] = ZA[595] + ZA[741];
    ZA[613] = ZA[598] + ZA[606];
    ZA[740] = ZA[745] + ZA[738];
    
    ZA[747] = (ZCh(ZA[742], ZA[739], ZA[736]) + ZA[613]) + ZR26(ZA[742]);
    ZA[746] = ZMa(ZA[737], ZA[600], ZA[740]) + ZR30(ZA[740]);
    ZA[607] = ZA[537] + K[16];
    
    ZA[546] = ZR15(ZA[502]) + ZA[501];
    ZA[751] = ZA[736] + ZA[607];
    ZA[748] = ZA[600] + ZA[747];
    ZA[743] = ZA[746] + ZA[741];
    
    ZA[752] = (ZCh(ZA[748], ZA[742], ZA[739]) + ZA[751]) + ZR26(ZA[748]);
    ZA[749] = ZMa(ZA[740], ZA[737], ZA[743]) + ZR30(ZA[743]);
    ZA[608] = ZA[546] + K[17];
    
    ZA[554] = ZR15(ZA[537]) + ZA[474];
    ZA[756] = ZA[739] + ZA[608];
    ZA[753] = ZA[737] + ZA[752];
    ZA[750] = ZA[747] + ZA[749];
    
    ZA[757] = (ZCh(ZA[753], ZA[748], ZA[742]) + ZA[756]) + ZR26(ZA[753]);
    ZA[754] = ZMa(ZA[743], ZA[740], ZA[750]) + ZR30(ZA[750]);
    ZA[609] = ZA[554] + K[18];
    
    ZA[566] = ZR15(ZA[546]) + ZA[518];
    ZA[761] = ZA[742] + ZA[609];
    ZA[758] = ZA[740] + ZA[757];
    ZA[755] = ZA[752] + ZA[754];
    
    ZA[762] = (ZCh(ZA[758], ZA[753], ZA[748]) + ZA[761]) + ZR26(ZA[758]);
    ZA[759] = ZMa(ZA[750], ZA[743], ZA[755]) + ZR30(ZA[755]);
    ZA[610] = ZA[566] + K[19];
    
    ZA[567] = ZR15(ZA[554]) + ZA[482];
    ZA[766] = ZA[748] + ZA[610];
    ZA[763] = ZA[743] + ZA[762];
    ZA[760] = ZA[757] + ZA[759];
    
    ZA[767] = (ZCh(ZA[763], ZA[758], ZA[753]) + ZA[766]) + ZR26(ZA[763]);
    ZA[764] = ZMa(ZA[755], ZA[750], ZA[760]) + ZR30(ZA[760]);
    ZA[611] = ZA[567] + K[20];
    
    ZA[614] = ZR15(ZA[566]) + ZA[528];
    ZA[771] = ZA[753] + ZA[611];
    ZA[768] = ZA[750] + ZA[767];
    ZA[765] = ZA[762] + ZA[764];
    
    ZA[772] = (ZCh(ZA[768], ZA[763], ZA[758]) + ZA[771]) + ZR26(ZA[768]);
    ZA[769] = ZMa(ZA[760], ZA[755], ZA[765]) + ZR30(ZA[765]);
    ZA[612] = ZA[502] + K[78];
    ZA[615] = ZA[614] + K[22];
    
    ZA[616] = ZR15(ZA[567]) + ZA[612];
    ZA[504] = ZR25(ZA[497]) + K[76];
    ZA[776] = ZA[758] + ZA[615];
    ZA[773] = ZA[755] + ZA[772];
    ZA[770] = ZA[767] + ZA[769];
    
    ZA[777] = (ZCh(ZA[773], ZA[768], ZA[763]) + ZA[776]) + ZR26(ZA[773]);
    ZA[774] = ZMa(ZA[765], ZA[760], ZA[770]) + ZR30(ZA[770]);
    ZA[492] = ZR25(ZA[471]);
    ZA[618] = ZA[537] + ZA[504];
    ZA[617] = ZA[616] + K[23];
    
    ZA[619] = ZR15(ZA[614]) + ZA[618];
    ZA[781] = ZA[763] + ZA[617];
    ZA[778] = ZA[760] + ZA[777];
    ZA[775] = ZA[772] + ZA[774];
    ZA[505] = ZA[492] + ZA[497];
    
    ZA[782] = (ZCh(ZA[778], ZA[773], ZA[768]) + ZA[781]) + ZR26(ZA[778]);
    ZA[779] = ZMa(ZA[770], ZA[765], ZA[775]) + ZR30(ZA[775]);
    ZA[621] = ZA[505] + ZA[546];
    ZA[620] = ZA[619] + K[24];
    
    ZA[622] = ZR15(ZA[616]) + ZA[621];
    ZA[625] = ZR25(ZA[501]);
    ZA[786] = ZA[768] + ZA[620];
    ZA[783] = ZA[765] + ZA[782];
    ZA[624] = ZA[554] + ZA[471];
    ZA[780] = ZA[777] + ZA[779];
    
    ZA[787] = (ZCh(ZA[783], ZA[778], ZA[773]) + ZA[786]) + ZR26(ZA[783]);
    ZA[784] = ZMa(ZA[775], ZA[770], ZA[780]) + ZR30(ZA[780]);
    ZA[493] = ZR25(ZA[474]);
    ZA[626] = ZA[625] + ZA[624];
    ZA[623] = ZA[622] + K[25];
    
    ZA[627] = ZR15(ZA[619]) + ZA[626];
    ZA[791] = ZA[773] + ZA[623];
    ZA[788] = ZA[770] + ZA[787];
    ZA[785] = ZA[782] + ZA[784];
    ZA[629] = ZA[493] + ZA[501];
    
    ZA[792] = (ZCh(ZA[788], ZA[783], ZA[778]) + ZA[791]) + ZR26(ZA[788]);
    ZA[789] = ZMa(ZA[780], ZA[775], ZA[785]) + ZR30(ZA[785]);
    ZA[630] = ZA[566] + ZA[629];
    ZA[628] = ZA[627] + K[26];
    
    ZA[634] = ZR25(ZA[518]) + ZA[474];
    ZA[631] = ZR15(ZA[622]) + ZA[630];
    ZA[796] = ZA[778] + ZA[628];
    ZA[793] = ZA[775] + ZA[792];
    ZA[790] = ZA[787] + ZA[789];
    
    ZA[797] = (ZCh(ZA[793], ZA[788], ZA[783]) + ZA[796]) + ZR26(ZA[793]);
    ZA[794] = ZMa(ZA[785], ZA[780], ZA[790]) + ZR30(ZA[790]);
    ZA[491] = ZR25(ZA[482]);
    ZA[635] = ZA[567] + ZA[634];
    ZA[633] = ZA[631] + K[27];
    
    ZA[636] = ZR15(ZA[627]) + ZA[635];
    ZA[801] = ZA[783] + ZA[633];
    ZA[798] = ZA[780] + ZA[797];
    ZA[795] = ZA[792] + ZA[794];
    ZA[638] = ZA[491] + ZA[518];
    
    ZA[802] = (ZCh(ZA[798], ZA[793], ZA[788]) + ZA[801]) + ZR26(ZA[798]);
    ZA[799] = ZMa(ZA[790], ZA[785], ZA[795]) + ZR30(ZA[795]);
    ZA[639] = ZA[638] + ZA[614];
    ZA[637] = ZA[636] + K[28];
    
    ZA[642] = ZR25(ZA[528]) + ZA[482];
    ZA[640] = ZR15(ZA[631]) + ZA[639];
    ZA[806] = ZA[788] + ZA[637];
    ZA[803] = ZA[785] + ZA[802];
    ZA[800] = ZA[797] + ZA[799];
    
    ZA[807] = (ZCh(ZA[803], ZA[798], ZA[793]) + ZA[806]) + ZR26(ZA[803]);
    ZA[804] = ZMa(ZA[795], ZA[790], ZA[800]) + ZR30(ZA[800]);
    ZA[643] = ZA[616] + ZA[642];
    ZA[641] = ZA[640] + K[29];
    
    ZA[646] = ZR25(ZA[502]) + ZA[528];
    ZA[644] = ZR15(ZA[636]) + ZA[643];
    ZA[811] = ZA[793] + ZA[641];
    ZA[808] = ZA[790] + ZA[807];
    ZA[805] = ZA[802] + ZA[804];
    
    ZA[812] = (ZCh(ZA[808], ZA[803], ZA[798]) + ZA[811]) + ZR26(ZA[808]);
    ZA[809] = ZMa(ZA[800], ZA[795], ZA[805]) + ZR30(ZA[805]);
    ZA[647] = ZA[619] + ZA[646];
    ZA[645] = ZA[644] + K[30];
    
    ZA[650] = ZR25(ZA[537]) + ZA[502];
    ZA[648] = ZR15(ZA[640]) + ZA[647];
    ZA[816] = ZA[798] + ZA[645];
    ZA[813] = ZA[795] + ZA[812];
    ZA[810] = ZA[807] + ZA[809];
    
    ZA[817] = (ZCh(ZA[813], ZA[808], ZA[803]) + ZA[816]) + ZR26(ZA[813]);
    ZA[814] = ZMa(ZA[805], ZA[800], ZA[810]) + ZR30(ZA[810]);
    ZA[925] = ZA[622] + ZA[650];
    ZA[649] = ZA[648] + K[31];
    
    ZA[653] = ZR25(ZA[546]) + ZA[537];
    ZA[651] = ZR15(ZA[644]) + ZA[925];
    ZA[821] = ZA[803] + ZA[649];
    ZA[818] = ZA[800] + ZA[817];
    ZA[815] = ZA[812] + ZA[814];
    
    ZA[822] = (ZCh(ZA[818], ZA[813], ZA[808]) + ZA[821]) + ZR26(ZA[818]);
    ZA[819] = ZMa(ZA[810], ZA[805], ZA[815]) + ZR30(ZA[815]);
    ZA[654] = ZA[627] + ZA[653];
    ZA[652] = ZA[651] + K[32];
    
    ZA[657] = ZR25(ZA[554]) + ZA[546];
    ZA[655] = ZR15(ZA[648]) + ZA[654];
    ZA[826] = ZA[808] + ZA[652];
    ZA[823] = ZA[805] + ZA[822];
    ZA[820] = ZA[817] + ZA[819];
    
    ZA[827] = (ZCh(ZA[823], ZA[818], ZA[813]) + ZA[826]) + ZR26(ZA[823]);
    ZA[824] = ZMa(ZA[815], ZA[810], ZA[820]) + ZR30(ZA[820]);
    ZA[658] = ZA[631] + ZA[657];
    ZA[656] = ZA[655] + K[33];
    
    ZA[661] = ZR25(ZA[566]) + ZA[554];
    ZA[659] = ZR15(ZA[651]) + ZA[658];
    ZA[831] = ZA[813] + ZA[656];
    ZA[828] = ZA[810] + ZA[827];
    ZA[825] = ZA[822] + ZA[824];
    
    ZA[832] = (ZCh(ZA[828], ZA[823], ZA[818]) + ZA[831]) + ZR26(ZA[828]);
    ZA[829] = ZMa(ZA[820], ZA[815], ZA[825]) + ZR30(ZA[825]);
    ZA[662] = ZA[636] + ZA[661];
    ZA[660] = ZA[659] + K[34];
    
    ZA[665] = ZR25(ZA[567]) + ZA[566];
    ZA[663] = ZR15(ZA[655]) + ZA[662];
    ZA[836] = ZA[818] + ZA[660];
    ZA[833] = ZA[815] + ZA[832];
    ZA[830] = ZA[827] + ZA[829];
    
    ZA[837] = (ZCh(ZA[833], ZA[828], ZA[823]) + ZA[836]) + ZR26(ZA[833]);
    ZA[834] = ZMa(ZA[825], ZA[820], ZA[830]) + ZR30(ZA[830]);
    ZA[666] = ZA[640] + ZA[665];
    ZA[664] = ZA[663] + K[35];
    
    ZA[669] = ZR25(ZA[614]) + ZA[567];
    ZA[667] = ZR15(ZA[659]) + ZA[666];
    ZA[841] = ZA[823] + ZA[664];
    ZA[838] = ZA[820] + ZA[837];
    ZA[835] = ZA[832] + ZA[834];
    
    ZA[842] = (ZCh(ZA[838], ZA[833], ZA[828]) + ZA[841]) + ZR26(ZA[838]);
    ZA[839] = ZMa(ZA[830], ZA[825], ZA[835]) + ZR30(ZA[835]);
    ZA[670] = ZA[644] + ZA[669];
    ZA[668] = ZA[667] + K[36];
    
    ZA[677] = ZR25(ZA[616]) + ZA[614];
    ZA[671] = ZR15(ZA[663]) + ZA[670];
    ZA[846] = ZA[828] + ZA[668];
    ZA[843] = ZA[825] + ZA[842];
    ZA[840] = ZA[837] + ZA[839];
    
    ZA[847] = (ZCh(ZA[843], ZA[838], ZA[833]) + ZA[846]) + ZR26(ZA[843]);
    ZA[844] = ZMa(ZA[835], ZA[830], ZA[840]) + ZR30(ZA[840]);
    ZA[678] = ZA[648] + ZA[677];
    ZA[676] = ZA[671] + K[37];
    
    ZA[682] = ZR25(ZA[619]) + ZA[616];
    ZA[679] = ZR15(ZA[667]) + ZA[678];
    ZA[851] = ZA[833] + ZA[676];
    ZA[848] = ZA[830] + ZA[847];
    ZA[845] = ZA[842] + ZA[844];
    
    ZA[852] = (ZCh(ZA[848], ZA[843], ZA[838]) + ZA[851]) + ZR26(ZA[848]);
    ZA[849] = ZMa(ZA[840], ZA[835], ZA[845]) + ZR30(ZA[845]);
    ZA[683] = ZA[651] + ZA[682];
    ZA[680] = ZA[679] + K[38];
    
    ZA[686] = ZR25(ZA[622]) + ZA[619];
    ZA[684] = ZR15(ZA[671]) + ZA[683];
    ZA[856] = ZA[838] + ZA[680];
    ZA[853] = ZA[835] + ZA[852];
    ZA[850] = ZA[847] + ZA[849];
    
    ZA[857] = (ZCh(ZA[853], ZA[848], ZA[843]) + ZA[856]) + ZR26(ZA[853]);
    ZA[854] = ZMa(ZA[845], ZA[840], ZA[850]) + ZR30(ZA[850]);
    ZA[687] = ZA[655] + ZA[686];
    ZA[685] = ZA[684] + K[39];
    
    ZA[690] = ZR25(ZA[627]) + ZA[622];
    ZA[688] = ZR15(ZA[679]) + ZA[687];
    ZA[861] = ZA[843] + ZA[685];
    ZA[858] = ZA[840] + ZA[857];
    ZA[855] = ZA[852] + ZA[854];
    
    ZA[862] = (ZCh(ZA[858], ZA[853], ZA[848]) + ZA[861]) + ZR26(ZA[858]);
    ZA[859] = ZMa(ZA[850], ZA[845], ZA[855]) + ZR30(ZA[855]);
    ZA[691] = ZA[659] + ZA[690];
    ZA[689] = ZA[688] + K[40];
    
    ZA[694] = ZR25(ZA[631]) + ZA[627];
    ZA[692] = ZR15(ZA[684]) + ZA[691];
    ZA[866] = ZA[848] + ZA[689];
    ZA[863] = ZA[845] + ZA[862];
    ZA[860] = ZA[857] + ZA[859];
    
    ZA[867] = (ZCh(ZA[863], ZA[858], ZA[853]) + ZA[866]) + ZR26(ZA[863]);
    ZA[864] = ZMa(ZA[855], ZA[850], ZA[860]) + ZR30(ZA[860]);
    ZA[695] = ZA[663] + ZA[694];
    ZA[693] = ZA[692] + K[41];
    
    ZA[698] = ZR25(ZA[636]) + ZA[631];
    ZA[696] = ZR15(ZA[688]) + ZA[695];
    ZA[871] = ZA[853] + ZA[693];
    ZA[868] = ZA[850] + ZA[867];
    ZA[865] = ZA[862] + ZA[864];
    
    ZA[873] = (ZCh(ZA[868], ZA[863], ZA[858]) + ZA[871]) + ZR26(ZA[868]);
    ZA[869] = ZMa(ZA[860], ZA[855], ZA[865]) + ZR30(ZA[865]);
    ZA[699] = ZA[667] + ZA[698];
    ZA[697] = ZA[696] + K[42];
    
    ZA[702] = ZR25(ZA[640]) + ZA[636];
    ZA[700] = ZR15(ZA[692]) + ZA[699];
    ZA[877] = ZA[858] + ZA[697];
    ZA[874] = ZA[855] + ZA[873];
    ZA[870] = ZA[867] + ZA[869];
    
    ZA[878] = (ZCh(ZA[874], ZA[868], ZA[863]) + ZA[877]) + ZR26(ZA[874]);
    ZA[875] = ZMa(ZA[865], ZA[860], ZA[870]) + ZR30(ZA[870]);
    ZA[703] = ZA[671] + ZA[702];
    ZA[701] = ZA[700] + K[43];
    
    ZA[706] = ZR25(ZA[644]) + ZA[640];
    ZA[704] = ZR15(ZA[696]) + ZA[703];
    ZA[882] = ZA[863] + ZA[701];
    ZA[879] = ZA[860] + ZA[878];
    ZA[876] = ZA[873] + ZA[875];
    
    ZA[883] = (ZCh(ZA[879], ZA[874], ZA[868]) + ZA[882]) + ZR26(ZA[879]);
    ZA[880] = ZMa(ZA[870], ZA[865], ZA[876]) + ZR30(ZA[876]);
    ZA[707] = ZA[679] + ZA[706];
    ZA[705] = ZA[704] + K[44];
    
    ZA[710] = ZR25(ZA[648]) + ZA[644];
    ZA[708] = ZR15(ZA[700]) + ZA[707];
    ZA[887] = ZA[868] + ZA[705];
    ZA[884] = ZA[865] + ZA[883];
    ZA[881] = ZA[878] + ZA[880];
    
    ZA[888] = (ZCh(ZA[884], ZA[879], ZA[874]) + ZA[887]) + ZR26(ZA[884]);
    ZA[885] = ZMa(ZA[876], ZA[870], ZA[881]) + ZR30(ZA[881]);
    ZA[711] = ZA[684] + ZA[710];
    ZA[709] = ZA[708] + K[45];
    
    ZA[714] = ZR25(ZA[651]) + ZA[648];
    ZA[712] = ZR15(ZA[704]) + ZA[711];
    ZA[892] = ZA[874] + ZA[709];
    ZA[889] = ZA[870] + ZA[888];
    ZA[886] = ZA[883] + ZA[885];
    
    ZA[893] = (ZCh(ZA[889], ZA[884], ZA[879]) + ZA[892]) + ZR26(ZA[889]);
    ZA[890] = ZMa(ZA[881], ZA[876], ZA[886]) + ZR30(ZA[886]);
    ZA[715] = ZA[688] + ZA[714];
    ZA[713] = ZA[712] + K[46];
    
    ZA[718] = ZR25(ZA[655]) + ZA[651];
    ZA[716] = ZR15(ZA[708]) + ZA[715];
    ZA[897] = ZA[879] + ZA[713];
    ZA[894] = ZA[876] + ZA[893];
    ZA[891] = ZA[888] + ZA[890];
    
    ZA[898] = (ZCh(ZA[894], ZA[889], ZA[884]) + ZA[897]) + ZR26(ZA[894]);
    ZA[895] = ZMa(ZA[886], ZA[881], ZA[891]) + ZR30(ZA[891]);
    ZA[719] = ZA[692] + ZA[718];
    ZA[717] = ZA[716] + K[47];
    
    ZA[722] = ZR25(ZA[659]) + ZA[655];
    ZA[720] = ZR15(ZA[712]) + ZA[719];
    ZA[902] = ZA[884] + ZA[717];
    ZA[899] = ZA[881] + ZA[898];
    ZA[896] = ZA[893] + ZA[895];
    
    ZA[903] = (ZCh(ZA[899], ZA[894], ZA[889]) + ZA[902]) + ZR26(ZA[899]);
    ZA[900] = ZMa(ZA[891], ZA[886], ZA[896]) + ZR30(ZA[896]);
    ZA[723] = ZA[696] + ZA[722];
    ZA[721] = ZA[720] + K[48];
    
    ZA[672] = ZR25(ZA[663]) + ZA[659];
    ZA[724] = ZR15(ZA[716]) + ZA[723];
    ZA[907] = ZA[889] + ZA[721];
    ZA[904] = ZA[886] + ZA[903];
    ZA[901] = ZA[898] + ZA[900];
    
    ZA[908] = (ZCh(ZA[904], ZA[899], ZA[894]) + ZA[907]) + ZR26(ZA[904]);
    ZA[905] = ZMa(ZA[896], ZA[891], ZA[901]) + ZR30(ZA[901]);
    ZA[673] = ZR25(ZA[667]) + ZA[663];
    ZA[726] = ZA[700] + ZA[672];
    ZA[725] = ZA[724] + K[49];
    
    ZA[727] = ZR15(ZA[720]) + ZA[726];
    ZA[912] = ZA[894] + ZA[725];
    ZA[909] = ZA[891] + ZA[908];
    ZA[906] = ZA[903] + ZA[905];
    ZA[675] = ZA[667] + K[52];
    ZA[729] = ZA[704] + ZA[673];
    
    ZA[913] = (ZCh(ZA[909], ZA[904], ZA[899]) + ZA[912]) + ZR26(ZA[909]);
    ZA[910] = ZMa(ZA[901], ZA[896], ZA[906]) + ZR30(ZA[906]);
    ZA[674] = ZR25(ZA[671]) + ZA[675];
    ZA[730] = ZR15(ZA[724]) + ZA[729];
    ZA[728] = ZA[727] + K[50];
    
    ZA[681] = ZR25(ZA[679]) + ZA[671];
    ZA[917] = ZA[899] + ZA[901] + ZA[728];
    ZA[914] = ZA[896] + ZA[913];
    ZA[911] = ZA[908] + ZA[910];
    ZA[732] = ZA[708] + ZA[674];
    ZA[731] = ZA[730] + K[51];
    
    ZA[918] = (ZCh(ZA[914], ZA[909], ZA[904]) + ZA[917]) + ZR26(ZA[914]);
    ZA[915] = ZMa(ZA[906], ZA[901], ZA[911]) + ZR30(ZA[911]);
    ZA[733] = ZR15(ZA[727]) + ZA[732];
    ZA[919] = ZA[906] + ZA[904] + ZA[731];
    ZA[734] = ZA[712] + ZA[681];
    
    ZA[920] = (ZCh(ZA[918], ZA[914], ZA[909]) + ZA[919]) + ZR26(ZA[918]);
    ZA[735] = ZR15(ZA[730]) + ZA[734];
    ZA[921] = ZA[911] + ZA[909] + ZA[733];
    ZA[916] = ZA[913] + ZA[915];
    
    ZA[922] = (ZCh(ZA[920], ZA[918], ZA[914]) + ZA[921]) + ZR26(ZA[920]);
    ZA[923] = ZA[916] + ZA[914] + ZA[735];
    
    ZA[924] = (ZCh(ZA[922], ZA[920], ZA[918]) + ZA[923]) + ZR26(ZA[922]);
    
#define FOUND (0x0F)
#define SETFOUND(Xnonce) output[output[FOUND]++] = Xnonce

#if defined(VECTORS4)
	bool result = any(ZA[924] == K[79]);

	if (result) {
		if (ZA[924].x == K[79])
			SETFOUND(Znonce.x);
		if (ZA[924].y == K[79])
			SETFOUND(Znonce.y);
		if (ZA[924].z == K[79])
			SETFOUND(Znonce.z);
		if (ZA[924].w == K[79])
			SETFOUND(Znonce.w);
	}
#elif defined(VECTORS2)
	bool result = any(ZA[924] == K[79]);

	if (result) {
		if (ZA[924].x == K[79])
			SETFOUND(Znonce.x);
		if (ZA[924].y == K[79])
			SETFOUND(Znonce.y);
	}
#else
	if (ZA[924] == K[79])
		SETFOUND(Znonce);
#endif
}
