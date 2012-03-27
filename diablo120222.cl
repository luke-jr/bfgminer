/*
 *  DiabloMiner - OpenCL miner for BitCoin
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
    __global uint * output)
{

  z ZA[25];

#ifdef GOFFSET
	const z Znonce = (uint)(get_global_id(0));
#else
	const z Znonce = base + (uint)(get_global_id(0));
#endif

ZA[2]=Znonce;
ZA[2]+=PreVal4_state0;

ZA[3]=ZCh(ZA[2],b1,c1);
ZA[3]+=d1;
ZA[3]+=ZR26(ZA[2]);

ZA[8]=Znonce;
ZA[8]+=PreVal4_T1;

ZA[4]=ZA[3];
ZA[4]+=h1;

ZA[5]=ZCh(ZA[4],ZA[2],b1);
ZA[5]+=c1_plus_k5;
ZA[5]+=ZR26(ZA[4]);
ZA[3]+=ZMa(f1,g1,ZA[8]);
ZA[3]+=ZR30(ZA[8]);

ZA[6]=ZA[5];
ZA[6]+=g1;

ZA[2]=ZCh(ZA[6],ZA[4],ZA[2]);
ZA[2]+=ZR26(ZA[6]);
ZA[2]+=b1_plus_k6;
ZA[5]+=ZMa(ZA[8],f1,ZA[3]);
ZA[5]+=ZR30(ZA[3]);

ZA[7]=ZA[2];
ZA[7]+=f1;
ZA[2]+=ZMa(ZA[3],ZA[8],ZA[5]);
ZA[2]+=ZR30(ZA[5]);

ZA[10]=Znonce;
ZA[10]+=PreVal4_state0_k7;
ZA[10]+=ZCh(ZA[7],ZA[6],ZA[4]);
ZA[10]+=ZR26(ZA[7]);
ZA[8]+=ZA[10];
ZA[10]+=ZMa(ZA[5],ZA[3],ZA[2]);
ZA[10]+=ZR30(ZA[2]);
ZA[4]+=ZCh(ZA[8],ZA[7],ZA[6]);
ZA[4]+=0xd807aa98U;
ZA[4]+=ZR26(ZA[8]);
ZA[3]+=ZA[4];
ZA[4]+=ZMa(ZA[2],ZA[5],ZA[10]);
ZA[4]+=ZR30(ZA[10]);
ZA[6]+=ZCh(ZA[3],ZA[8],ZA[7]);
ZA[6]+=0x12835b01U;
ZA[6]+=ZR26(ZA[3]);
ZA[5]+=ZA[6];
ZA[6]+=ZMa(ZA[10],ZA[2],ZA[4]);
ZA[6]+=ZR30(ZA[4]);
ZA[7]+=ZCh(ZA[5],ZA[3],ZA[8]);
ZA[7]+=0x243185beU;
ZA[7]+=ZR26(ZA[5]);
ZA[2]+=ZA[7];
ZA[7]+=ZMa(ZA[4],ZA[10],ZA[6]);
ZA[7]+=ZR30(ZA[6]);
ZA[8]+=ZCh(ZA[2],ZA[5],ZA[3]);
ZA[8]+=0x550c7dc3U;
ZA[8]+=ZR26(ZA[2]);
ZA[10]+=ZA[8];
ZA[8]+=ZMa(ZA[6],ZA[4],ZA[7]);
ZA[8]+=ZR30(ZA[7]);
ZA[3]+=ZCh(ZA[10],ZA[2],ZA[5]);
ZA[3]+=0x72be5d74U;
ZA[3]+=ZR26(ZA[10]);
ZA[4]+=ZA[3];
ZA[3]+=ZMa(ZA[7],ZA[6],ZA[8]);
ZA[3]+=ZR30(ZA[8]);
ZA[5]+=ZCh(ZA[4],ZA[10],ZA[2]);
ZA[5]+=0x80deb1feU;
ZA[5]+=ZR26(ZA[4]);
ZA[6]+=ZA[5];
ZA[5]+=ZMa(ZA[8],ZA[7],ZA[3]);
ZA[5]+=ZR30(ZA[3]);
ZA[2]+=ZCh(ZA[6],ZA[4],ZA[10]);
ZA[2]+=0x9bdc06a7U;
ZA[2]+=ZR26(ZA[6]);
ZA[7]+=ZA[2];
ZA[2]+=ZMa(ZA[3],ZA[8],ZA[5]);
ZA[2]+=ZR30(ZA[5]);
ZA[10]+=ZCh(ZA[7],ZA[6],ZA[4]);
ZA[10]+=0xc19bf3f4U;
ZA[10]+=ZR26(ZA[7]);
ZA[8]+=ZA[10];
ZA[10]+=ZMa(ZA[5],ZA[3],ZA[2]);
ZA[10]+=ZR30(ZA[2]);
ZA[4]+=ZCh(ZA[8],ZA[7],ZA[6]);
ZA[4]+=W16_plus_K16;
ZA[4]+=ZR26(ZA[8]);

ZA[0]=ZR25(Znonce);
ZA[0]+=W18;

ZA[11]=ZMa(ZA[2],ZA[5],ZA[10]);
ZA[11]+=ZR30(ZA[10]);
ZA[11]+=ZA[4];
ZA[4]+=ZA[3];
ZA[6]+=ZCh(ZA[4],ZA[8],ZA[7]);
ZA[6]+=W17_plus_K17;
ZA[6]+=ZR26(ZA[4]);
ZA[5]+=ZA[6];
ZA[6]+=ZMa(ZA[10],ZA[2],ZA[11]);
ZA[6]+=ZR30(ZA[11]);

ZA[3]=Znonce;
ZA[3]+=W19;
ZA[7]+=ZCh(ZA[5],ZA[4],ZA[8]);
ZA[7]+=ZA[0];
ZA[7]+=0x0fc19dc6U;
ZA[7]+=ZR26(ZA[5]);

ZA[1]=ZR15(ZA[0]);
ZA[1]+=0x80000000U;

ZA[12]=ZMa(ZA[11],ZA[10],ZA[6]);
ZA[12]+=ZR30(ZA[6]);
ZA[12]+=ZA[7];
ZA[7]+=ZA[2];
ZA[8]+=ZCh(ZA[7],ZA[5],ZA[4]);
ZA[8]+=ZA[3];
ZA[8]+=0x240ca1ccU;
ZA[8]+=ZR26(ZA[7]);

ZA[2]=ZR15(ZA[3]);

ZA[10]+=ZA[8];
ZA[8]+=ZMa(ZA[6],ZA[11],ZA[12]);
ZA[8]+=ZR30(ZA[12]);
ZA[4]+=ZCh(ZA[10],ZA[7],ZA[5]);
ZA[4]+=ZA[1];
ZA[4]+=0x2de92c6fU;
ZA[4]+=ZR26(ZA[10]);

ZA[13]=ZR15(ZA[1]);
ZA[13]+=0x00000280U;

ZA[14]=ZMa(ZA[12],ZA[6],ZA[8]);
ZA[14]+=ZR30(ZA[8]);
ZA[14]+=ZA[4];
ZA[4]+=ZA[11];
ZA[5]+=ZCh(ZA[4],ZA[10],ZA[7]);
ZA[5]+=ZA[2];
ZA[5]+=0x4a7484aaU;
ZA[5]+=ZR26(ZA[4]);

ZA[11]=ZR15(ZA[2]);
ZA[11]+=W16;

ZA[15]=ZMa(ZA[8],ZA[12],ZA[14]);
ZA[15]+=ZR30(ZA[14]);
ZA[15]+=ZA[5];
ZA[5]+=ZA[6];
ZA[7]+=ZCh(ZA[5],ZA[4],ZA[10]);
ZA[7]+=ZA[13];
ZA[7]+=0x5cb0a9dcU;
ZA[7]+=ZR26(ZA[5]);

ZA[6]=ZR15(ZA[13]);
ZA[6]+=W17;

ZA[16]=ZMa(ZA[14],ZA[8],ZA[15]);
ZA[16]+=ZR30(ZA[15]);
ZA[16]+=ZA[7];
ZA[7]+=ZA[12];
ZA[10]+=ZCh(ZA[7],ZA[5],ZA[4]);
ZA[10]+=ZA[11];
ZA[10]+=0x76f988daU;
ZA[10]+=ZR26(ZA[7]);

ZA[12]=ZR15(ZA[11]);
ZA[12]+=ZA[0];

ZA[17]=ZA[10];
ZA[17]+=ZMa(ZA[15],ZA[14],ZA[16]);
ZA[17]+=ZR30(ZA[16]);
ZA[10]+=ZA[8];
ZA[4]+=ZCh(ZA[10],ZA[7],ZA[5]);
ZA[4]+=ZA[6];
ZA[4]+=0x983e5152U;
ZA[4]+=ZR26(ZA[10]);

ZA[8]=ZR15(ZA[6]);
ZA[8]+=ZA[3];
ZA[14]+=ZA[4];
ZA[4]+=ZMa(ZA[16],ZA[15],ZA[17]);
ZA[4]+=ZR30(ZA[17]);
ZA[5]+=ZCh(ZA[14],ZA[10],ZA[7]);
ZA[5]+=ZA[12];
ZA[5]+=0xa831c66dU;
ZA[5]+=ZR26(ZA[14]);

ZA[9]=ZR15(ZA[12]);
ZA[9]+=ZA[1];

ZA[18]=ZMa(ZA[17],ZA[16],ZA[4]);
ZA[18]+=ZR30(ZA[4]);
ZA[18]+=ZA[5];
ZA[5]+=ZA[15];
ZA[7]+=ZCh(ZA[5],ZA[14],ZA[10]);
ZA[7]+=ZA[8];
ZA[7]+=0xb00327c8U;
ZA[7]+=ZR26(ZA[5]);

ZA[15]=ZR15(ZA[8]);
ZA[15]+=ZA[2];

ZA[19]=ZA[7];
ZA[19]+=ZMa(ZA[4],ZA[17],ZA[18]);
ZA[19]+=ZR30(ZA[18]);
ZA[7]+=ZA[16];
ZA[10]+=ZCh(ZA[7],ZA[5],ZA[14]);
ZA[10]+=ZA[9];
ZA[10]+=0xbf597fc7U;
ZA[10]+=ZR26(ZA[7]);

ZA[16]=ZR15(ZA[9]);
ZA[16]+=ZA[13];

ZA[20]=ZMa(ZA[18],ZA[4],ZA[19]);
ZA[20]+=ZR30(ZA[19]);
ZA[20]+=ZA[10];
ZA[10]+=ZA[17];
ZA[14]+=ZCh(ZA[10],ZA[7],ZA[5]);
ZA[14]+=ZA[15];
ZA[14]+=0xc6e00bf3U;
ZA[14]+=ZR26(ZA[10]);

ZA[17]=ZR15(ZA[15]);
ZA[17]+=ZA[11];
ZA[17]+=0x00A00055U;

ZA[21]=ZMa(ZA[19],ZA[18],ZA[20]);
ZA[21]+=ZR30(ZA[20]);
ZA[21]+=ZA[14];
ZA[14]+=ZA[4];
ZA[5]+=ZCh(ZA[14],ZA[10],ZA[7]);
ZA[5]+=ZA[16];
ZA[5]+=0xd5a79147U;
ZA[5]+=ZR26(ZA[14]);

ZA[4]=ZR15(ZA[16]);
ZA[4]+=ZA[6];
ZA[4]+=W31;

ZA[22]=ZA[5];
ZA[22]+=ZMa(ZA[20],ZA[19],ZA[21]);
ZA[22]+=ZR30(ZA[21]);
ZA[5]+=ZA[18];
ZA[7]+=ZCh(ZA[5],ZA[14],ZA[10]);
ZA[7]+=ZA[17];
ZA[7]+=0x06ca6351U;
ZA[7]+=ZR26(ZA[5]);

ZA[18]=ZR15(ZA[17]);
ZA[18]+=ZA[12];
ZA[18]+=W32;

ZA[23]=ZA[7];
ZA[23]+=ZMa(ZA[21],ZA[20],ZA[22]);
ZA[23]+=ZR30(ZA[22]);
ZA[7]+=ZA[19];
ZA[10]+=ZCh(ZA[7],ZA[5],ZA[14]);
ZA[10]+=ZA[4];
ZA[10]+=0x14292967U;
ZA[10]+=ZR26(ZA[7]);

ZA[19]=ZR15(ZA[4]);
ZA[19]+=ZA[8];
ZA[19]+=ZR25(ZA[0]);
ZA[19]+=W17;
ZA[20]+=ZA[10];
ZA[10]+=ZMa(ZA[22],ZA[21],ZA[23]);
ZA[10]+=ZR30(ZA[23]);
ZA[14]+=ZCh(ZA[20],ZA[7],ZA[5]);
ZA[14]+=ZA[18];
ZA[14]+=0x27b70a85U;
ZA[14]+=ZR26(ZA[20]);
ZA[0]+=ZR15(ZA[18]);
ZA[0]+=ZR25(ZA[3]);
ZA[0]+=ZA[9];

ZA[24]=ZA[14];
ZA[24]+=ZMa(ZA[23],ZA[22],ZA[10]);
ZA[24]+=ZR30(ZA[10]);
ZA[14]+=ZA[21];
ZA[5]+=ZCh(ZA[14],ZA[20],ZA[7]);
ZA[5]+=ZA[19];
ZA[5]+=0x2e1b2138U;
ZA[5]+=ZR26(ZA[14]);
ZA[3]+=ZR15(ZA[19]);
ZA[3]+=ZR25(ZA[1]);
ZA[3]+=ZA[15];
ZA[22]+=ZA[5];
ZA[5]+=ZMa(ZA[10],ZA[23],ZA[24]);
ZA[5]+=ZR30(ZA[24]);

ZA[7]+=ZCh(ZA[22],ZA[14],ZA[20]);
ZA[7]+=ZA[0];
ZA[7]+=0x4d2c6dfcU;
ZA[7]+=ZR26(ZA[22]);
ZA[1]+=ZR15(ZA[0]);
ZA[1]+=ZR25(ZA[2]);
ZA[1]+=ZA[16];

ZA[21]=ZA[7];
ZA[21]+=ZMa(ZA[24],ZA[10],ZA[5]);
ZA[21]+=ZR30(ZA[5]);
ZA[7]+=ZA[23];
ZA[20]+=ZCh(ZA[7],ZA[22],ZA[14]);
ZA[20]+=ZA[3];
ZA[20]+=0x53380d13U;
ZA[20]+=ZR26(ZA[7]);
ZA[2]+=ZR15(ZA[3]);
ZA[2]+=ZR25(ZA[13]);
ZA[2]+=ZA[17];

ZA[10]+=ZA[20];
ZA[20]+=ZMa(ZA[5],ZA[24],ZA[21]);
ZA[20]+=ZR30(ZA[21]);

ZA[14]+=ZCh(ZA[10],ZA[7],ZA[22]);
ZA[14]+=ZA[1];
ZA[14]+=0x650a7354U;
ZA[14]+=ZR26(ZA[10]);
ZA[13]+=ZR15(ZA[1]);
ZA[13]+=ZR25(ZA[11]);
ZA[13]+=ZA[4];

ZA[23]=ZA[14];
ZA[23]+=ZMa(ZA[21],ZA[5],ZA[20]);
ZA[23]+=ZR30(ZA[20]);
ZA[14]+=ZA[24];
ZA[22]+=ZCh(ZA[14],ZA[10],ZA[7]);
ZA[22]+=ZA[2];
ZA[22]+=0x766a0abbU;
ZA[22]+=ZR26(ZA[14]);
ZA[11]+=ZR15(ZA[2]);
ZA[11]+=ZR25(ZA[6]);
ZA[11]+=ZA[18];

ZA[5]+=ZA[22];
ZA[22]+=ZMa(ZA[20],ZA[21],ZA[23]);
ZA[22]+=ZR30(ZA[23]);

ZA[7]+=ZCh(ZA[5],ZA[14],ZA[10]);
ZA[7]+=ZA[13];
ZA[7]+=0x81c2c92eU;
ZA[7]+=ZR26(ZA[5]);
ZA[6]+=ZR15(ZA[13]);
ZA[6]+=ZR25(ZA[12]);
ZA[6]+=ZA[19];
ZA[21]+=ZA[7];
ZA[7]+=ZMa(ZA[23],ZA[20],ZA[22]);
ZA[7]+=ZR30(ZA[22]);
ZA[10]+=ZCh(ZA[21],ZA[5],ZA[14]);
ZA[10]+=ZA[11];
ZA[10]+=0x92722c85U;
ZA[10]+=ZR26(ZA[21]);
ZA[12]+=ZR15(ZA[11]);
ZA[12]+=ZR25(ZA[8]);
ZA[12]+=ZA[0];
ZA[20]+=ZA[10];
ZA[10]+=ZMa(ZA[22],ZA[23],ZA[7]);
ZA[10]+=ZR30(ZA[7]);

ZA[14]+=ZCh(ZA[20],ZA[21],ZA[5]);
ZA[14]+=ZA[6];
ZA[14]+=0xa2bfe8a1U;
ZA[14]+=ZR26(ZA[20]);
ZA[8]+=ZR15(ZA[6]);
ZA[8]+=ZR25(ZA[9]);
ZA[8]+=ZA[3];
ZA[23]+=ZA[14];
ZA[14]+=ZMa(ZA[7],ZA[22],ZA[10]);
ZA[14]+=ZR30(ZA[10]);
ZA[5]+=ZCh(ZA[23],ZA[20],ZA[21]);
ZA[5]+=ZA[12];
ZA[5]+=0xa81a664bU;
ZA[5]+=ZR26(ZA[23]);
ZA[9]+=ZR15(ZA[12]);
ZA[9]+=ZA[1];
ZA[9]+=ZR25(ZA[15]);

ZA[24]=ZA[5];
ZA[24]+=ZMa(ZA[10],ZA[7],ZA[14]);
ZA[24]+=ZR30(ZA[14]);
ZA[22]+=ZA[5];
ZA[21]+=ZCh(ZA[22],ZA[23],ZA[20]);
ZA[21]+=ZA[8];
ZA[21]+=0xc24b8b70U;
ZA[21]+=ZR26(ZA[22]);
ZA[15]+=ZR15(ZA[8]);
ZA[15]+=ZA[2];
ZA[15]+=ZR25(ZA[16]);

ZA[7]+=ZA[21];
ZA[21]+=ZMa(ZA[14],ZA[10],ZA[24]);
ZA[21]+=ZR30(ZA[24]);
ZA[20]+=ZCh(ZA[7],ZA[22],ZA[23]);
ZA[20]+=ZA[9];
ZA[20]+=0xc76c51a3U;
ZA[20]+=ZR26(ZA[7]);
ZA[16]+=ZR15(ZA[9]);
ZA[16]+=ZR25(ZA[17]);
ZA[16]+=ZA[13];
ZA[10]+=ZA[20];
ZA[20]+=ZMa(ZA[24],ZA[14],ZA[21]);
ZA[20]+=ZR30(ZA[21]);
ZA[23]+=ZCh(ZA[10],ZA[7],ZA[22]);
ZA[23]+=ZA[15];
ZA[23]+=0xd192e819U;
ZA[23]+=ZR26(ZA[10]);
ZA[17]+=ZR15(ZA[15]);
ZA[17]+=ZR25(ZA[4]);
ZA[17]+=ZA[11];

ZA[14]+=ZA[23];
ZA[23]+=ZMa(ZA[21],ZA[24],ZA[20]);
ZA[23]+=ZR30(ZA[20]);
ZA[22]+=ZCh(ZA[14],ZA[10],ZA[7]);
ZA[22]+=ZA[16];
ZA[22]+=0xd6990624U;
ZA[22]+=ZR26(ZA[14]);
ZA[4]+=ZR15(ZA[16]);
ZA[4]+=ZA[6];
ZA[4]+=ZR25(ZA[18]);
ZA[24]+=ZA[22];
ZA[22]+=ZMa(ZA[20],ZA[21],ZA[23]);
ZA[22]+=ZR30(ZA[23]);
ZA[7]+=ZCh(ZA[24],ZA[14],ZA[10]);
ZA[7]+=ZA[17];
ZA[7]+=0xf40e3585U;
ZA[7]+=ZR26(ZA[24]);
ZA[18]+=ZR15(ZA[17]);
ZA[18]+=ZA[12];
ZA[18]+=ZR25(ZA[19]);
ZA[21]+=ZA[7];
ZA[7]+=ZMa(ZA[23],ZA[20],ZA[22]);
ZA[7]+=ZR30(ZA[22]);
ZA[10]+=ZCh(ZA[21],ZA[24],ZA[14]);
ZA[10]+=ZA[4];
ZA[10]+=0x106aa070U;
ZA[10]+=ZR26(ZA[21]);
ZA[19]+=ZR15(ZA[4]);
ZA[19]+=ZA[8];
ZA[19]+=ZR25(ZA[0]);
ZA[20]+=ZA[10];
ZA[10]+=ZMa(ZA[22],ZA[23],ZA[7]);
ZA[10]+=ZR30(ZA[7]);
ZA[14]+=ZCh(ZA[20],ZA[21],ZA[24]);
ZA[14]+=ZA[18];
ZA[14]+=0x19a4c116U;
ZA[14]+=ZR26(ZA[20]);
ZA[0]+=ZR15(ZA[18]);
ZA[0]+=ZA[9];
ZA[0]+=ZR25(ZA[3]);
ZA[23]+=ZA[14];
ZA[14]+=ZMa(ZA[7],ZA[22],ZA[10]);
ZA[14]+=ZR30(ZA[10]);
ZA[24]+=ZCh(ZA[23],ZA[20],ZA[21]);
ZA[24]+=ZA[19];
ZA[24]+=0x1e376c08U;
ZA[24]+=ZR26(ZA[23]);
ZA[3]+=ZR15(ZA[19]);
ZA[3]+=ZA[15];
ZA[3]+=ZR25(ZA[1]);
ZA[22]+=ZA[24];
ZA[24]+=ZMa(ZA[10],ZA[7],ZA[14]);
ZA[24]+=ZR30(ZA[14]);
ZA[21]+=ZCh(ZA[22],ZA[23],ZA[20]);
ZA[21]+=ZA[0];
ZA[21]+=0x2748774cU;
ZA[21]+=ZR26(ZA[22]);
ZA[1]+=ZR15(ZA[0]);
ZA[1]+=ZA[16];
ZA[1]+=ZR25(ZA[2]);
ZA[7]+=ZA[21];
ZA[21]+=ZMa(ZA[14],ZA[10],ZA[24]);
ZA[21]+=ZR30(ZA[24]);
ZA[20]+=ZCh(ZA[7],ZA[22],ZA[23]);
ZA[20]+=ZA[3];
ZA[20]+=0x34b0bcb5U;
ZA[20]+=ZR26(ZA[7]);
ZA[2]+=ZR15(ZA[3]);
ZA[2]+=ZA[17];
ZA[2]+=ZR25(ZA[13]);
ZA[10]+=ZA[20];
ZA[20]+=ZMa(ZA[24],ZA[14],ZA[21]);
ZA[20]+=ZR30(ZA[21]);
ZA[23]+=ZCh(ZA[10],ZA[7],ZA[22]);
ZA[23]+=ZA[1];
ZA[23]+=0x391c0cb3U;
ZA[23]+=ZR26(ZA[10]);
ZA[13]+=ZR15(ZA[1]);
ZA[13]+=ZA[4];
ZA[13]+=ZR25(ZA[11]);
ZA[14]+=ZA[23];
ZA[23]+=ZMa(ZA[21],ZA[24],ZA[20]);
ZA[23]+=ZR30(ZA[20]);
ZA[22]+=ZCh(ZA[14],ZA[10],ZA[7]);
ZA[22]+=ZA[2];
ZA[22]+=0x4ed8aa4aU;
ZA[22]+=ZR26(ZA[14]);
ZA[11]+=ZR15(ZA[2]);
ZA[11]+=ZA[18];
ZA[11]+=ZR25(ZA[6]);
ZA[24]+=ZA[22];
ZA[22]+=ZMa(ZA[20],ZA[21],ZA[23]);
ZA[22]+=ZR30(ZA[23]);
ZA[7]+=ZCh(ZA[24],ZA[14],ZA[10]);
ZA[7]+=ZA[13];
ZA[7]+=0x5b9cca4fU;
ZA[7]+=ZR26(ZA[24]);
ZA[6]+=ZR15(ZA[13]);
ZA[6]+=ZA[19];
ZA[6]+=ZR25(ZA[12]);
ZA[21]+=ZA[7];
ZA[7]+=ZMa(ZA[23],ZA[20],ZA[22]);
ZA[7]+=ZR30(ZA[22]);
ZA[10]+=ZCh(ZA[21],ZA[24],ZA[14]);
ZA[10]+=ZA[11];
ZA[10]+=0x682e6ff3U;
ZA[10]+=ZR26(ZA[21]);
ZA[0]+=ZR15(ZA[11]);
ZA[0]+=ZR25(ZA[8]);
ZA[0]+=ZA[12];

ZA[20]+=ZA[10];
ZA[10]+=ZMa(ZA[22],ZA[23],ZA[7]);
ZA[10]+=ZR30(ZA[7]);

ZA[14]+=ZCh(ZA[20],ZA[21],ZA[24]);
ZA[14]+=ZA[6];
ZA[14]+=0x748f82eeU;
ZA[14]+=ZR26(ZA[20]);
ZA[3]+=ZR15(ZA[6]);
ZA[3]+=ZR25(ZA[9]);
ZA[3]+=ZA[8];
ZA[23]+=ZA[14];
ZA[14]+=ZMa(ZA[7],ZA[22],ZA[10]);
ZA[14]+=ZR30(ZA[10]);
ZA[24]+=ZCh(ZA[23],ZA[20],ZA[21]);
ZA[24]+=ZA[0];
ZA[24]+=0x78a5636fU;
ZA[24]+=ZR26(ZA[23]);
ZA[1]+=ZR15(ZA[0]);
ZA[1]+=ZR25(ZA[15]);
ZA[1]+=ZA[9];

ZA[22]+=ZA[24];
ZA[24]+=ZMa(ZA[10],ZA[7],ZA[14]);
ZA[24]+=ZR30(ZA[14]);

ZA[21]+=ZCh(ZA[22],ZA[23],ZA[20]);
ZA[21]+=ZA[3];
ZA[21]+=0x84c87814U;
ZA[21]+=ZR26(ZA[22]);
ZA[2]+=ZR15(ZA[3]);
ZA[2]+=ZR25(ZA[16]);
ZA[2]+=ZA[15];

ZA[7]+=ZA[21];
ZA[21]+=ZMa(ZA[14],ZA[10],ZA[24]);
ZA[21]+=ZR30(ZA[24]);
ZA[20]+=ZCh(ZA[7],ZA[22],ZA[23]);
ZA[20]+=ZA[1];
ZA[20]+=0x8cc70208U;
ZA[20]+=ZR26(ZA[7]);
ZA[13]+=ZR15(ZA[1]);
ZA[13]+=ZR25(ZA[17]);
ZA[13]+=ZA[16];
ZA[10]+=ZA[20];
ZA[20]+=ZMa(ZA[24],ZA[14],ZA[21]);
ZA[20]+=ZR30(ZA[21]);

ZA[23]+=ZCh(ZA[10],ZA[7],ZA[22]);
ZA[23]+=ZA[2];
ZA[23]+=0x90befffaU;
ZA[23]+=ZR26(ZA[10]);
ZA[14]+=ZA[23];
ZA[23]+=ZMa(ZA[21],ZA[24],ZA[20]);
ZA[23]+=ZR30(ZA[20]);
ZA[22]+=ZCh(ZA[14],ZA[10],ZA[7]);
ZA[22]+=ZA[13];
ZA[22]+=0xa4506cebU;
ZA[22]+=ZR26(ZA[14]);
ZA[16]=ZA[22];
ZA[16]+=ZMa(ZA[20],ZA[21],ZA[23]);
ZA[16]+=ZR30(ZA[23]);
ZA[24]+=ZA[22];
ZA[7]+=ZCh(ZA[24],ZA[14],ZA[10]);
ZA[7]+=ZR15(ZA[2]);
ZA[7]+=ZA[11];
ZA[7]+=ZR25(ZA[4]);
ZA[7]+=0xbef9a3f7U;
ZA[7]+=ZR26(ZA[24]);
ZA[7]+=ZA[17];
ZA[21]+=ZA[7];
ZA[7]+=ZMa(ZA[23],ZA[20],ZA[16]);
ZA[7]+=ZR30(ZA[16]);
ZA[6]+=ZCh(ZA[21],ZA[24],ZA[14]);
ZA[6]+=ZA[10];
ZA[6]+=ZR15(ZA[13]);
ZA[6]+=ZR25(ZA[18]);
ZA[6]+=ZA[4];
ZA[6]+=0xc67178f2U;
ZA[6]+=ZR26(ZA[21]);

ZA[4]=state1;
ZA[4]+=ZA[7];

ZA[7]=ZMa(ZA[16],ZA[23],ZA[7])+ZR30(ZA[7]);
ZA[7]+=ZA[6];
ZA[7]+=state0;

ZA[18]=ZA[7];
ZA[18]+=0x98c7e2a2U;

ZA[17]=ZCh(ZA[18],0x510e527fU,0x9b05688cU);
ZA[17]+=ZA[4];
ZA[17]+=0x90bb1e3cU;
ZA[17]+=ZR26(ZA[18]);
ZA[16]+=state2;

ZA[2]=ZA[7];
ZA[2]+=0xfc08884dU;

ZA[13]=ZA[17];
ZA[13]+=0x3c6ef372U;

ZA[11]=ZCh(ZA[13],ZA[18],0x510e527fU);
ZA[11]+=ZA[16];
ZA[11]+=0x50c6645bU;
ZA[11]+=ZR26(ZA[13]);
ZA[23]+=state3;
ZA[17]+=ZMa(0x6a09e667U,0xbb67ae85U,ZA[2]);
ZA[17]+=ZR30(ZA[2]);

ZA[22]=ZA[11];
ZA[22]+=0xbb67ae85U;

ZA[18]=ZCh(ZA[22],ZA[13],ZA[18]);
ZA[18]+=ZR26(ZA[22]);
ZA[18]+=ZA[23];
ZA[18]+=0x3ac42e24U;
ZA[20]+=state4;
ZA[20]+=ZA[6];

ZA[6]=ZA[18];
ZA[6]+=0x6a09e667U;
ZA[11]+=ZMa(ZA[2],0x6a09e667U,ZA[17]);
ZA[11]+=ZR30(ZA[17]);
ZA[21]+=state5;

ZA[10]=ZCh(ZA[6],ZA[22],ZA[13]);
ZA[10]+=ZA[20];
ZA[10]+=ZA[7];
ZA[10]+=0xd21ea4fdU;
ZA[10]+=ZR26(ZA[6]);

ZA[12]=ZA[10];
ZA[12]+=ZA[2];
ZA[18]+=ZMa(ZA[17],ZA[2],ZA[11]);
ZA[18]+=ZR30(ZA[11]);
ZA[24]+=state6;
ZA[13]+=ZCh(ZA[12],ZA[6],ZA[22]);
ZA[13]+=ZA[21];
ZA[13]+=0x59f111f1U;
ZA[13]+=ZR26(ZA[12]);

ZA[2]=ZA[17];
ZA[2]+=ZA[13];
ZA[10]+=ZMa(ZA[11],ZA[17],ZA[18]);
ZA[10]+=ZR30(ZA[18]);
ZA[14]+=state7;
ZA[22]+=ZCh(ZA[2],ZA[12],ZA[6]);
ZA[22]+=ZA[24];
ZA[22]+=0x923f82a4U;
ZA[22]+=ZR26(ZA[2]);

ZA[17]=ZA[11];
ZA[17]+=ZA[22];
ZA[13]+=ZMa(ZA[18],ZA[11],ZA[10]);
ZA[13]+=ZR30(ZA[10]);
ZA[6]+=ZCh(ZA[17],ZA[2],ZA[12]);
ZA[6]+=ZA[14];
ZA[6]+=0xab1c5ed5U;
ZA[6]+=ZR26(ZA[17]);

ZA[11]=ZA[6];
ZA[11]+=ZA[18];
ZA[22]+=ZMa(ZA[10],ZA[18],ZA[13]);
ZA[22]+=ZR30(ZA[13]);
ZA[12]+=ZCh(ZA[11],ZA[17],ZA[2]);
ZA[12]+=0x5807aa98U;
ZA[12]+=ZR26(ZA[11]);

ZA[18]=ZA[10];
ZA[18]+=ZA[12];
ZA[6]+=ZMa(ZA[13],ZA[10],ZA[22]);
ZA[6]+=ZR30(ZA[22]);
ZA[2]+=ZCh(ZA[18],ZA[11],ZA[17]);
ZA[2]+=0x12835b01U;
ZA[2]+=ZR26(ZA[18]);

ZA[10]=ZA[2];
ZA[10]+=ZA[13];
ZA[12]+=ZMa(ZA[22],ZA[13],ZA[6]);
ZA[12]+=ZR30(ZA[6]);
ZA[17]+=ZCh(ZA[10],ZA[18],ZA[11]);
ZA[17]+=0x243185beU;
ZA[17]+=ZR26(ZA[10]);

ZA[5]=ZA[17];
ZA[5]+=ZA[22];
ZA[2]+=ZMa(ZA[6],ZA[22],ZA[12]);
ZA[2]+=ZR30(ZA[12]);
ZA[11]+=ZCh(ZA[5],ZA[10],ZA[18]);
ZA[11]+=0x550c7dc3U;
ZA[11]+=ZR26(ZA[5]);

ZA[22]=ZA[11];
ZA[22]+=ZA[6];
ZA[17]+=ZMa(ZA[12],ZA[6],ZA[2]);
ZA[17]+=ZR30(ZA[2]);
ZA[18]+=ZCh(ZA[22],ZA[5],ZA[10]);
ZA[18]+=0x72be5d74U;
ZA[18]+=ZR26(ZA[22]);

ZA[6]=ZA[18];
ZA[6]+=ZA[12];
ZA[11]+=ZMa(ZA[2],ZA[12],ZA[17]);
ZA[11]+=ZR30(ZA[17]);
ZA[10]+=ZCh(ZA[6],ZA[22],ZA[5]);
ZA[10]+=0x80deb1feU;
ZA[10]+=ZR26(ZA[6]);

ZA[12]=ZA[10];
ZA[12]+=ZA[2];
ZA[18]+=ZMa(ZA[17],ZA[2],ZA[11]);
ZA[18]+=ZR30(ZA[11]);
ZA[5]+=ZCh(ZA[12],ZA[6],ZA[22]);
ZA[5]+=0x9bdc06a7U;
ZA[5]+=ZR26(ZA[12]);
ZA[7]+=ZR25(ZA[4]);

ZA[2]=ZA[5];
ZA[2]+=ZA[17];
ZA[10]+=ZMa(ZA[11],ZA[17],ZA[18]);
ZA[10]+=ZR30(ZA[18]);
ZA[22]+=ZCh(ZA[2],ZA[12],ZA[6]);
ZA[22]+=0xc19bf274U;
ZA[22]+=ZR26(ZA[2]);
ZA[4]+=ZR25(ZA[16]);
ZA[4]+=0x00a00000U;

ZA[17]=ZA[22];
ZA[17]+=ZA[11];
ZA[5]+=ZMa(ZA[18],ZA[11],ZA[10]);
ZA[5]+=ZR30(ZA[10]);
ZA[6]+=ZCh(ZA[17],ZA[2],ZA[12]);
ZA[6]+=ZA[7];
ZA[6]+=0xe49b69c1U;
ZA[6]+=ZR26(ZA[17]);

ZA[0]=ZA[6];
ZA[0]+=ZA[18];
ZA[22]+=ZMa(ZA[10],ZA[18],ZA[5]);
ZA[22]+=ZR30(ZA[5]);
ZA[12]+=ZCh(ZA[0],ZA[17],ZA[2]);
ZA[12]+=ZA[4];
ZA[12]+=0xefbe4786U;
ZA[12]+=ZR26(ZA[0]);
ZA[16]+=ZR15(ZA[7]);
ZA[16]+=ZR25(ZA[23]);
ZA[23]+=ZR15(ZA[4]);
ZA[23]+=ZR25(ZA[20]);

ZA[6]+=ZMa(ZA[5],ZA[10],ZA[22]);
ZA[6]+=ZR30(ZA[22]);

ZA[10]+=ZA[12];
ZA[2]+=ZCh(ZA[10],ZA[0],ZA[17]);
ZA[2]+=0x0fc19dc6U;
ZA[2]+=ZA[16];
ZA[2]+=ZR26(ZA[10]);

ZA[20]+=ZR15(ZA[16]);
ZA[20]+=ZR25(ZA[21]);

ZA[12]+=ZMa(ZA[22],ZA[5],ZA[6]);
ZA[12]+=ZR30(ZA[6]);

ZA[5]+=ZA[2];
ZA[17]+=ZCh(ZA[5],ZA[10],ZA[0]);
ZA[17]+=ZA[23];
ZA[17]+=0x240ca1ccU;
ZA[17]+=ZR26(ZA[5]);
ZA[21]+=ZR15(ZA[23]);
ZA[21]+=ZR25(ZA[24]);

ZA[2]+=ZMa(ZA[6],ZA[22],ZA[12]);
ZA[2]+=ZR30(ZA[12]);

ZA[22]+=ZA[17];
ZA[0]+=ZCh(ZA[22],ZA[5],ZA[10]);
ZA[0]+=ZA[20];
ZA[0]+=0x2de92c6fU;
ZA[0]+=ZR26(ZA[22]);
ZA[24]+=ZR15(ZA[20]);
ZA[24]+=0x00000100U;
ZA[24]+=ZR25(ZA[14]);

ZA[17]+=ZMa(ZA[12],ZA[6],ZA[2]);
ZA[17]+=ZR30(ZA[2]);

ZA[6]+=ZA[0];
ZA[10]+=ZCh(ZA[6],ZA[22],ZA[5]);
ZA[10]+=ZA[21];
ZA[10]+=0x4a7484aaU;
ZA[10]+=ZR26(ZA[6]);
ZA[14]+=ZA[7];
ZA[14]+=ZR15(ZA[21]);
ZA[14]+=0x11002000U;

ZA[0]+=ZMa(ZA[2],ZA[12],ZA[17]);
ZA[0]+=ZR30(ZA[17]);

ZA[12]+=ZA[10];
ZA[5]+=ZCh(ZA[12],ZA[6],ZA[22]);
ZA[5]+=ZA[24];
ZA[5]+=0x5cb0a9dcU;
ZA[5]+=ZR26(ZA[12]);

ZA[19]=ZR15(ZA[24]);
ZA[19]+=ZA[4];
ZA[19]+=0x80000000U;

ZA[9]=ZA[5];
ZA[9]+=ZA[2];
ZA[10]+=ZMa(ZA[17],ZA[2],ZA[0]);
ZA[10]+=ZR30(ZA[0]);
ZA[22]+=ZCh(ZA[9],ZA[12],ZA[6]);
ZA[22]+=ZA[14];
ZA[22]+=0x76f988daU;
ZA[22]+=ZR26(ZA[9]);

ZA[2]=ZR15(ZA[14]);
ZA[2]+=ZA[16];

ZA[1]=ZA[22];
ZA[1]+=ZA[17];
ZA[5]+=ZMa(ZA[0],ZA[17],ZA[10]);
ZA[5]+=ZR30(ZA[10]);
ZA[6]+=ZCh(ZA[1],ZA[9],ZA[12]);
ZA[6]+=ZA[19];
ZA[6]+=0x983e5152U;
ZA[6]+=ZR26(ZA[1]);

ZA[17]=ZR15(ZA[19]);
ZA[17]+=ZA[23];

ZA[15]=ZA[6];
ZA[15]+=ZA[0];
ZA[22]+=ZMa(ZA[10],ZA[0],ZA[5]);
ZA[22]+=ZR30(ZA[5]);
ZA[12]+=ZCh(ZA[15],ZA[1],ZA[9]);
ZA[12]+=ZA[2];
ZA[12]+=0xa831c66dU;
ZA[12]+=ZR26(ZA[15]);

ZA[0]=ZR15(ZA[2]);
ZA[0]+=ZA[20];

ZA[13]=ZA[12];
ZA[13]+=ZA[10];
ZA[10]=ZMa(ZA[5],ZA[10],ZA[22]);
ZA[10]+=ZA[6];
ZA[10]+=ZR30(ZA[22]);
ZA[9]+=ZCh(ZA[13],ZA[15],ZA[1]);
ZA[9]+=ZA[17];
ZA[9]+=0xb00327c8U;
ZA[9]+=ZR26(ZA[13]);

ZA[6]=ZR15(ZA[17]);
ZA[6]+=ZA[21];

ZA[8]=ZA[9];
ZA[8]+=ZA[5];
ZA[12]+=ZMa(ZA[22],ZA[5],ZA[10]);
ZA[12]+=ZR30(ZA[10]);
ZA[1]+=ZCh(ZA[8],ZA[13],ZA[15]);
ZA[1]+=ZA[0];
ZA[1]+=0xbf597fc7U;
ZA[1]+=ZR26(ZA[8]);

ZA[11]=ZR15(ZA[0]);
ZA[11]+=ZA[24];

ZA[3]=ZA[1];
ZA[3]+=ZA[22];
ZA[9]+=ZMa(ZA[10],ZA[22],ZA[12]);
ZA[9]+=ZR30(ZA[12]);
ZA[15]+=ZCh(ZA[3],ZA[8],ZA[13]);
ZA[15]+=ZA[6];
ZA[15]+=0xc6e00bf3U;
ZA[15]+=ZR26(ZA[3]);

ZA[5]=ZR15(ZA[6]);
ZA[5]+=ZA[14];
ZA[5]+=0x00400022U;

ZA[1]+=ZMa(ZA[12],ZA[10],ZA[9]);
ZA[1]+=ZR30(ZA[9]);

ZA[10]+=ZA[15];
ZA[13]+=ZCh(ZA[10],ZA[3],ZA[8]);
ZA[13]+=ZA[11];
ZA[13]+=0xd5a79147U;
ZA[13]+=ZR26(ZA[10]);

ZA[22]=ZR15(ZA[11]);
ZA[22]+=ZA[19];
ZA[22]+=ZR25(ZA[7]);
ZA[22]+=0x00000100U;

ZA[15]+=ZMa(ZA[9],ZA[12],ZA[1]);
ZA[15]+=ZR30(ZA[1]);

ZA[12]+=ZA[13];
ZA[8]+=ZCh(ZA[12],ZA[10],ZA[3]);
ZA[8]+=ZA[5];
ZA[8]+=0x06ca6351U;
ZA[8]+=ZR26(ZA[12]);
ZA[7]+=ZR15(ZA[5]);
ZA[7]+=ZR25(ZA[4]);
ZA[7]+=ZA[2];

ZA[13]+=ZMa(ZA[1],ZA[9],ZA[15]);
ZA[13]+=ZR30(ZA[15]);

ZA[9]+=ZA[8];
ZA[3]+=ZCh(ZA[9],ZA[12],ZA[10]);
ZA[3]+=ZA[22];
ZA[3]+=0x14292967U;
ZA[3]+=ZR26(ZA[9]);
ZA[4]+=ZR15(ZA[22]);
ZA[4]+=ZR25(ZA[16]);
ZA[4]+=ZA[17];

ZA[8]+=ZMa(ZA[15],ZA[1],ZA[13]);
ZA[8]+=ZR30(ZA[13]);

ZA[1]+=ZA[3];
ZA[10]+=ZCh(ZA[1],ZA[9],ZA[12]);
ZA[10]+=ZA[7];
ZA[10]+=0x27b70a85U;
ZA[10]+=ZR26(ZA[1]);
ZA[16]+=ZR15(ZA[7]);
ZA[16]+=ZA[0];
ZA[16]+=ZR25(ZA[23]);

ZA[3]+=ZMa(ZA[13],ZA[15],ZA[8]);
ZA[3]+=ZR30(ZA[8]);

ZA[15]+=ZA[10];
ZA[12]+=ZCh(ZA[15],ZA[1],ZA[9]);
ZA[12]+=ZA[4];
ZA[12]+=0x2e1b2138U;
ZA[12]+=ZR26(ZA[15]);
ZA[23]+=ZR15(ZA[4]);
ZA[23]+=ZA[6];
ZA[23]+=ZR25(ZA[20]);

ZA[10]+=ZMa(ZA[8],ZA[13],ZA[3]);
ZA[10]+=ZR30(ZA[3]);

ZA[13]+=ZA[12];
ZA[9]+=ZCh(ZA[13],ZA[15],ZA[1]);
ZA[9]+=ZA[16];
ZA[9]+=0x4d2c6dfcU;
ZA[9]+=ZR26(ZA[13]);
ZA[20]+=ZR15(ZA[16]);
ZA[20]+=ZR25(ZA[21]);
ZA[20]+=ZA[11];

ZA[12]+=ZMa(ZA[3],ZA[8],ZA[10]);
ZA[12]+=ZR30(ZA[10]);

ZA[8]+=ZA[9];
ZA[1]+=ZCh(ZA[8],ZA[13],ZA[15]);
ZA[1]+=ZA[23];
ZA[1]+=0x53380d13U;
ZA[1]+=ZR26(ZA[8]);
ZA[21]+=ZR15(ZA[23]);
ZA[21]+=ZA[5];
ZA[21]+=ZR25(ZA[24]);

ZA[9]+=ZMa(ZA[10],ZA[3],ZA[12]);
ZA[9]+=ZR30(ZA[12]);

ZA[3]+=ZA[1];
ZA[15]+=ZCh(ZA[3],ZA[8],ZA[13]);
ZA[15]+=ZA[20];
ZA[15]+=0x650a7354U;
ZA[15]+=ZR26(ZA[3]);
ZA[24]+=ZR15(ZA[20]);
ZA[24]+=ZA[22];
ZA[24]+=ZR25(ZA[14]);

ZA[1]+=ZMa(ZA[12],ZA[10],ZA[9]);
ZA[1]+=ZR30(ZA[9]);

ZA[10]+=ZA[15];
ZA[13]+=ZCh(ZA[10],ZA[3],ZA[8]);
ZA[13]+=ZA[21];
ZA[13]+=0x766a0abbU;
ZA[13]+=ZR26(ZA[10]);
ZA[14]+=ZR15(ZA[21]);
ZA[14]+=ZA[7];
ZA[14]+=ZR25(ZA[19]);

ZA[15]+=ZMa(ZA[9],ZA[12],ZA[1]);
ZA[15]+=ZR30(ZA[1]);

ZA[12]+=ZA[13];
ZA[8]+=ZCh(ZA[12],ZA[10],ZA[3]);
ZA[8]+=ZA[24];
ZA[8]+=0x81c2c92eU;
ZA[8]+=ZR26(ZA[12]);
ZA[19]+=ZR15(ZA[24]);
ZA[19]+=ZA[4];
ZA[19]+=ZR25(ZA[2]);

ZA[13]+=ZMa(ZA[1],ZA[9],ZA[15]);
ZA[13]+=ZR30(ZA[15]);

ZA[9]+=ZA[8];
ZA[3]+=ZCh(ZA[9],ZA[12],ZA[10]);
ZA[3]+=ZA[14];
ZA[3]+=0x92722c85U;
ZA[3]+=ZR26(ZA[9]);
ZA[2]+=ZR15(ZA[14]);
ZA[2]+=ZA[16];
ZA[2]+=ZR25(ZA[17]);

ZA[8]+=ZMa(ZA[15],ZA[1],ZA[13]);
ZA[8]+=ZR30(ZA[13]);

ZA[1]+=ZA[3];
ZA[10]+=ZCh(ZA[1],ZA[9],ZA[12]);
ZA[10]+=ZA[19];
ZA[10]+=0xa2bfe8a1U;
ZA[10]+=ZR26(ZA[1]);
ZA[17]+=ZR15(ZA[19]);
ZA[17]+=ZA[23];
ZA[17]+=ZR25(ZA[0]);

ZA[3]+=ZMa(ZA[13],ZA[15],ZA[8]);
ZA[3]+=ZR30(ZA[8]);

ZA[15]+=ZA[10];
ZA[12]+=ZCh(ZA[15],ZA[1],ZA[9]);
ZA[12]+=ZA[2];
ZA[12]+=0xa81a664bU;
ZA[12]+=ZR26(ZA[15]);
ZA[0]+=ZR15(ZA[2]);
ZA[0]+=ZA[20];
ZA[0]+=ZR25(ZA[6]);

ZA[10]+=ZMa(ZA[8],ZA[13],ZA[3]);
ZA[10]+=ZR30(ZA[3]);

ZA[13]+=ZA[12];
ZA[9]+=ZCh(ZA[13],ZA[15],ZA[1]);
ZA[9]+=ZA[17];
ZA[9]+=0xc24b8b70U;
ZA[9]+=ZR26(ZA[13]);
ZA[6]+=ZR15(ZA[17]);
ZA[6]+=ZA[21];
ZA[6]+=ZR25(ZA[11]);

ZA[12]+=ZMa(ZA[3],ZA[8],ZA[10]);
ZA[12]+=ZR30(ZA[10]);

ZA[8]+=ZA[9];
ZA[1]+=ZCh(ZA[8],ZA[13],ZA[15]);
ZA[1]+=ZA[0];
ZA[1]+=0xc76c51a3U;
ZA[1]+=ZR26(ZA[8]);
ZA[11]+=ZR15(ZA[0]);
ZA[11]+=ZA[24];
ZA[11]+=ZR25(ZA[5]);

ZA[9]+=ZMa(ZA[10],ZA[3],ZA[12]);
ZA[9]+=ZR30(ZA[12]);

ZA[3]+=ZA[1];
ZA[15]+=ZCh(ZA[3],ZA[8],ZA[13]);
ZA[15]+=ZA[6];
ZA[15]+=0xd192e819U;
ZA[15]+=ZR26(ZA[3]);
ZA[5]+=ZR15(ZA[6]);
ZA[5]+=ZA[14];
ZA[5]+=ZR25(ZA[22]);

ZA[1]+=ZMa(ZA[12],ZA[10],ZA[9]);
ZA[1]+=ZR30(ZA[9]);

ZA[10]+=ZA[15];
ZA[13]+=ZCh(ZA[10],ZA[3],ZA[8]);
ZA[13]+=ZA[11];
ZA[13]+=0xd6990624U;
ZA[13]+=ZR26(ZA[10]);
ZA[22]+=ZR15(ZA[11]);
ZA[22]+=ZA[19];
ZA[22]+=ZR25(ZA[7]);

ZA[15]+=ZMa(ZA[9],ZA[12],ZA[1]);
ZA[15]+=ZR30(ZA[1]);

ZA[12]+=ZA[13];
ZA[8]+=ZCh(ZA[12],ZA[10],ZA[3]);
ZA[8]+=ZA[5];
ZA[8]+=0xf40e3585U;
ZA[8]+=ZR26(ZA[12]);
ZA[7]+=ZR15(ZA[5]);
ZA[7]+=ZA[2];
ZA[7]+=ZR25(ZA[4]);

ZA[13]+=ZMa(ZA[1],ZA[9],ZA[15]);
ZA[13]+=ZR30(ZA[15]);

ZA[9]+=ZA[8];
ZA[3]+=ZCh(ZA[9],ZA[12],ZA[10]);
ZA[3]+=ZA[22];
ZA[3]+=0x106aa070U;
ZA[3]+=ZR26(ZA[9]);
ZA[4]+=ZR15(ZA[22]);
ZA[4]+=ZA[17];
ZA[4]+=ZR25(ZA[16]);

ZA[8]+=ZMa(ZA[15],ZA[1],ZA[13]);
ZA[8]+=ZR30(ZA[13]);

ZA[1]+=ZA[3];
ZA[10]+=ZCh(ZA[1],ZA[9],ZA[12]);
ZA[10]+=ZA[7];
ZA[10]+=0x19a4c116U;
ZA[10]+=ZR26(ZA[1]);
ZA[16]+=ZR15(ZA[7]);
ZA[16]+=ZA[0];
ZA[16]+=ZR25(ZA[23]);

ZA[3]+=ZMa(ZA[13],ZA[15],ZA[8]);
ZA[3]+=ZR30(ZA[8]);

ZA[15]+=ZA[10];
ZA[12]+=ZCh(ZA[15],ZA[1],ZA[9]);
ZA[12]+=ZA[4];
ZA[12]+=0x1e376c08U;
ZA[12]+=ZR26(ZA[15]);
ZA[23]+=ZR15(ZA[4]);
ZA[23]+=ZA[6];
ZA[23]+=ZR25(ZA[20]);

ZA[10]+=ZMa(ZA[8],ZA[13],ZA[3]);
ZA[10]+=ZR30(ZA[3]);

ZA[13]+=ZA[12];
ZA[9]+=ZCh(ZA[13],ZA[15],ZA[1]);
ZA[9]+=ZA[16];
ZA[9]+=0x2748774cU;
ZA[9]+=ZR26(ZA[13]);
ZA[20]+=ZR15(ZA[16]);
ZA[20]+=ZA[11];
ZA[20]+=ZR25(ZA[21]);

ZA[12]+=ZMa(ZA[3],ZA[8],ZA[10]);
ZA[12]+=ZR30(ZA[10]);

ZA[8]+=ZA[9];
ZA[1]+=ZCh(ZA[8],ZA[13],ZA[15]);
ZA[1]+=ZA[23];
ZA[1]+=0x34b0bcb5U;
ZA[1]+=ZR26(ZA[8]);
ZA[21]+=ZR15(ZA[23]);
ZA[21]+=ZA[5];
ZA[21]+=ZR25(ZA[24]);

ZA[9]+=ZMa(ZA[10],ZA[3],ZA[12]);
ZA[9]+=ZR30(ZA[12]);

ZA[3]+=ZA[1];
ZA[15]+=ZCh(ZA[3],ZA[8],ZA[13]);
ZA[15]+=ZA[20];
ZA[15]+=0x391c0cb3U;
ZA[15]+=ZR26(ZA[3]);
ZA[24]+=ZR15(ZA[20]);
ZA[24]+=ZA[22];
ZA[24]+=ZR25(ZA[14]);

ZA[1]+=ZMa(ZA[12],ZA[10],ZA[9]);
ZA[1]+=ZR30(ZA[9]);

ZA[10]+=ZA[15];
ZA[13]+=ZCh(ZA[10],ZA[3],ZA[8]);
ZA[13]+=ZA[21];
ZA[13]+=0x4ed8aa4aU;
ZA[13]+=ZR26(ZA[10]);
ZA[7]+=ZR15(ZA[21]);
ZA[7]+=ZR25(ZA[19]);
ZA[7]+=ZA[14];

ZA[15]+=ZMa(ZA[9],ZA[12],ZA[1]);
ZA[15]+=ZR30(ZA[1]);

ZA[12]+=ZA[13];
ZA[8]+=ZCh(ZA[12],ZA[10],ZA[3]);
ZA[8]+=ZA[24];
ZA[8]+=0x5b9cca4fU;
ZA[8]+=ZR26(ZA[12]);
ZA[19]+=ZR15(ZA[24]);
ZA[19]+=ZA[4];
ZA[19]+=ZR25(ZA[2]);

ZA[13]+=ZMa(ZA[1],ZA[9],ZA[15]);
ZA[13]+=ZR30(ZA[15]);

ZA[9]+=ZA[8];
ZA[3]+=ZCh(ZA[9],ZA[12],ZA[10]);
ZA[3]+=ZA[7];
ZA[3]+=0x682e6ff3U;
ZA[3]+=ZR26(ZA[9]);
ZA[16]+=ZR15(ZA[7]);
ZA[16]+=ZR25(ZA[17]);
ZA[16]+=ZA[2];

ZA[8]+=ZMa(ZA[15],ZA[1],ZA[13]);
ZA[8]+=ZR30(ZA[13]);

ZA[1]+=ZA[3];
ZA[10]+=ZCh(ZA[1],ZA[9],ZA[12]);
ZA[10]+=ZA[19];
ZA[10]+=0x748f82eeU;
ZA[10]+=ZR26(ZA[1]);

ZA[17]+=ZR15(ZA[19]);
ZA[17]+=ZA[23];
ZA[17]+=ZR25(ZA[0]);

ZA[3]+=ZR30(ZA[8]);
ZA[3]+=ZMa(ZA[13],ZA[15],ZA[8]);
ZA[15]+=ZA[10];
ZA[12]+=ZCh(ZA[15],ZA[1],ZA[9]);
ZA[12]+=ZA[13];
ZA[12]+=ZA[16];
ZA[12]+=0x78a5636fU;
ZA[12]+=ZR26(ZA[15]);
ZA[9]+=ZR26(ZA[12]);
ZA[9]+=ZCh(ZA[12],ZA[15],ZA[1]);
ZA[9]+=ZA[8];
ZA[9]+=ZA[17];
ZA[9]+=0x84c87814U;
ZA[1]+=ZR26(ZA[9]);
ZA[1]+=ZCh(ZA[9],ZA[12],ZA[15]);
ZA[1]+=ZA[3];
ZA[1]+=ZR15(ZA[16]);
ZA[1]+=ZA[20];
ZA[1]+=ZR25(ZA[6]);
ZA[1]+=ZA[0];
ZA[1]+=0x8cc70208U;

#define FOUND (0x80)
#define NFLAG (0x7F)

#if defined(VECTORS2) || defined(VECTORS4)
ZA[15]+=ZR26(ZA[1]);
ZA[15]+=ZCh(ZA[1],ZA[9],ZA[12]);
ZA[15]+=ZA[10];
ZA[15]+=ZMa(ZA[8],ZA[13],ZA[3]);
ZA[15]+=ZR30(ZA[3]);
ZA[15]+=ZR15(ZA[17]);
ZA[15]+=ZA[21];
ZA[15]+=ZR25(ZA[11]);
ZA[15]+=ZA[6];

	if (any(ZA[15] == 0x136032EDU)) {
		if (ZA[15].x == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.x] =  Znonce.x;
		if (ZA[15].y == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.y] =  Znonce.y;
#if defined(VECTORS4)
		if (ZA[15].z == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.z] =  Znonce.z;
		if (ZA[15].w == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.w] =  Znonce.w;
#endif
	}
#else
	if (ZA[15]+(ZCh(ZA[1],ZA[9],ZA[12])+ZA[10]+ZMa(ZA[8],ZA[13],ZA[3])+
		ZR30(ZA[3])+ZR15(ZA[17])+ZA[21]+ZR25(ZA[11])+ZA[6])+ZR26(ZA[1]) == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce] =  Znonce;
#endif
}
