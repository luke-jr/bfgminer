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

__kernel __attribute__((reqd_work_group_size(WORKSIZE, 1, 1))) void search(
    const z base,
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

	const z Znonce = base + (uint)(get_global_id(0));

ZA[2]=Znonce+PreVal4_state0;
ZA[3]=(ZCh(ZA[2],b1,c1)+d1)+ZR26(ZA[2]);
ZA[8]=Znonce+PreVal4_T1;
ZA[4]=ZA[3]+h1;
ZA[5]=(ZCh(ZA[4],ZA[2],b1)+c1_plus_k5)+ZR26(ZA[4]);
ZA[3]+=ZMa(f1,g1,ZA[8])+ZR30(ZA[8]);
ZA[6]=ZA[5]+g1;
ZA[2]=(ZCh(ZA[6],ZA[4],ZA[2])+b1_plus_k6)+ZR26(ZA[6]);
ZA[5]+=ZMa(ZA[8],f1,ZA[3])+ZR30(ZA[3]);
ZA[7]=ZA[2]+f1;
ZA[2]+=ZMa(ZA[3],ZA[8],ZA[5])+ZR30(ZA[5]);
ZA[10]=Znonce+PreVal4_state0_k7+ZCh(ZA[7],ZA[6],ZA[4])+ZR26(ZA[7]);
ZA[8]+=ZA[10];
ZA[10]+=ZMa(ZA[5],ZA[3],ZA[2])+ZR30(ZA[2]);
ZA[4]+=(ZCh(ZA[8],ZA[7],ZA[6])+0xd807aa98U)+ZR26(ZA[8]);
ZA[3]+=ZA[4];
ZA[4]+=ZMa(ZA[2],ZA[5],ZA[10])+ZR30(ZA[10]);
ZA[6]+=(ZCh(ZA[3],ZA[8],ZA[7])+0x12835b01U)+ZR26(ZA[3]);
ZA[5]+=ZA[6];
ZA[6]+=ZMa(ZA[10],ZA[2],ZA[4])+ZR30(ZA[4]);
ZA[7]+=(ZCh(ZA[5],ZA[3],ZA[8])+0x243185beU)+ZR26(ZA[5]);
ZA[2]+=ZA[7];
ZA[7]+=ZMa(ZA[4],ZA[10],ZA[6])+ZR30(ZA[6]);
ZA[8]+=(ZCh(ZA[2],ZA[5],ZA[3])+0x550c7dc3U)+ZR26(ZA[2]);
ZA[10]+=ZA[8];
ZA[8]+=ZMa(ZA[6],ZA[4],ZA[7])+ZR30(ZA[7]);
ZA[3]+=(ZCh(ZA[10],ZA[2],ZA[5])+0x72be5d74U)+ZR26(ZA[10]);
ZA[4]+=ZA[3];
ZA[3]+=ZMa(ZA[7],ZA[6],ZA[8])+ZR30(ZA[8]);
ZA[5]+=(ZCh(ZA[4],ZA[10],ZA[2])+0x80deb1feU)+ZR26(ZA[4]);
ZA[6]+=ZA[5];
ZA[5]+=ZMa(ZA[8],ZA[7],ZA[3])+ZR30(ZA[3]);
ZA[2]+=(ZCh(ZA[6],ZA[4],ZA[10])+0x9bdc06a7U)+ZR26(ZA[6]);
ZA[7]+=ZA[2];
ZA[2]+=ZMa(ZA[3],ZA[8],ZA[5])+ZR30(ZA[5]);
ZA[10]+=(ZCh(ZA[7],ZA[6],ZA[4])+0xc19bf3f4U)+ZR26(ZA[7]);
ZA[8]+=ZA[10];
ZA[10]+=ZMa(ZA[5],ZA[3],ZA[2])+ZR30(ZA[2]);
ZA[4]+=(ZCh(ZA[8],ZA[7],ZA[6])+W16_plus_K16)+ZR26(ZA[8]);
ZA[0]=ZR25(Znonce)+W18;
ZA[11]=ZA[4]+ZMa(ZA[2],ZA[5],ZA[10])+ZR30(ZA[10]);
ZA[4]+=ZA[3];
ZA[6]+=(ZCh(ZA[4],ZA[8],ZA[7])+W17_plus_K17)+ZR26(ZA[4]);
ZA[5]+=ZA[6];
ZA[6]+=ZMa(ZA[10],ZA[2],ZA[11])+ZR30(ZA[11]);
ZA[3]=Znonce+W19;
ZA[7]+=(ZCh(ZA[5],ZA[4],ZA[8])+ZA[0]+0x0fc19dc6U)+ZR26(ZA[5]);
ZA[1]=ZR15(ZA[0])+0x80000000U;
ZA[12]=ZA[7]+ZMa(ZA[11],ZA[10],ZA[6])+ZR30(ZA[6]);
ZA[7]+=ZA[2];
ZA[8]+=(ZCh(ZA[7],ZA[5],ZA[4])+ZA[3]+0x240ca1ccU)+ZR26(ZA[7]);
ZA[2]=ZR15(ZA[3]);
ZA[13]=ZA[8]+ZMa(ZA[6],ZA[11],ZA[12])+ZR30(ZA[12]);
ZA[10]+=ZA[8];
ZA[4]+=(ZCh(ZA[10],ZA[7],ZA[5])+ZA[1]+0x2de92c6fU)+ZR26(ZA[10]);
ZA[8]=ZR15(ZA[1])+0x00000280U;
ZA[14]=ZA[4]+ZMa(ZA[12],ZA[6],ZA[13])+ZR30(ZA[13]);
ZA[4]+=ZA[11];
ZA[5]+=(ZCh(ZA[4],ZA[10],ZA[7])+ZA[2]+0x4a7484aaU)+ZR26(ZA[4]);
ZA[11]=ZR15(ZA[2])+W16;
ZA[15]=ZA[5]+ZMa(ZA[13],ZA[12],ZA[14])+ZR30(ZA[14]);
ZA[5]+=ZA[6];
ZA[6]=(ZCh(ZA[5],ZA[4],ZA[10])+ZA[7]+ZA[8]+0x5cb0a9dcU)+ZR26(ZA[5]);
ZA[7]=ZR15(ZA[8])+W17;
ZA[16]=ZA[6]+ZMa(ZA[14],ZA[13],ZA[15])+ZR30(ZA[15]);
ZA[6]+=ZA[12];
ZA[10]+=(ZCh(ZA[6],ZA[5],ZA[4])+ZA[11]+0x76f988daU)+ZR26(ZA[6]);
ZA[12]=ZR15(ZA[11])+ZA[0];
ZA[17]=ZA[10]+ZMa(ZA[15],ZA[14],ZA[16])+ZR30(ZA[16]);
ZA[10]+=ZA[13];
ZA[13]=(ZCh(ZA[10],ZA[6],ZA[5])+ZA[4]+ZA[7]+0x983e5152U)+ZR26(ZA[10]);
ZA[4]=ZR15(ZA[7])+ZA[3];
ZA[14]+=ZA[13];
ZA[13]+=ZMa(ZA[16],ZA[15],ZA[17])+ZR30(ZA[17]);
ZA[5]+=(ZCh(ZA[14],ZA[10],ZA[6])+ZA[12]+0xa831c66dU)+ZR26(ZA[14]);
ZA[9]=ZR15(ZA[12])+ZA[1];
ZA[18]=ZA[5]+ZMa(ZA[17],ZA[16],ZA[13])+ZR30(ZA[13]);
ZA[5]+=ZA[15];
ZA[15]=(ZCh(ZA[5],ZA[14],ZA[10])+ZA[6]+ZA[4]+0xb00327c8U)+ZR26(ZA[5]);
ZA[6]=ZR15(ZA[4])+ZA[2];
ZA[19]=ZA[15]+ZMa(ZA[13],ZA[17],ZA[18])+ZR30(ZA[18]);
ZA[15]+=ZA[16];
ZA[16]=(ZCh(ZA[15],ZA[5],ZA[14])+ZA[10]+ZA[9]+0xbf597fc7U)+ZR26(ZA[15]);
ZA[10]=ZR15(ZA[9])+ZA[8];
ZA[20]=ZA[16]+ZMa(ZA[18],ZA[13],ZA[19])+ZR30(ZA[19]);
ZA[16]+=ZA[17];
ZA[14]+=(ZCh(ZA[16],ZA[15],ZA[5])+ZA[6]+0xc6e00bf3U)+ZR26(ZA[16]);
ZA[17]=ZR15(ZA[6])+ZA[11]+0x00A00055U;
ZA[21]=ZA[14]+ZMa(ZA[19],ZA[18],ZA[20])+ZR30(ZA[20]);
ZA[14]+=ZA[13];
ZA[13]=(ZCh(ZA[14],ZA[16],ZA[15])+ZA[5]+ZA[10]+0xd5a79147U)+ZR26(ZA[14]);
ZA[5]=ZR15(ZA[10])+ZA[7]+W31;
ZA[22]=ZA[13]+ZMa(ZA[20],ZA[19],ZA[21])+ZR30(ZA[21]);
ZA[13]+=ZA[18];
ZA[18]=(ZCh(ZA[13],ZA[14],ZA[16])+ZA[15]+ZA[17]+0x06ca6351U)+ZR26(ZA[13]);
ZA[15]=ZR15(ZA[17])+ZA[12]+W32;
ZA[23]=ZA[18]+ZMa(ZA[21],ZA[20],ZA[22])+ZR30(ZA[22]);
ZA[18]+=ZA[19];
ZA[19]=(ZCh(ZA[18],ZA[13],ZA[14])+ZA[16]+ZA[5]+0x14292967U)+ZR26(ZA[18]);
ZA[16]=ZR15(ZA[5])+ZA[4]+ZR25(ZA[0])+W17;
ZA[20]+=ZA[19];
ZA[19]+=ZMa(ZA[22],ZA[21],ZA[23])+ZR30(ZA[23]);
ZA[14]+=(ZCh(ZA[20],ZA[18],ZA[13])+ZA[15]+0x27b70a85U)+ZR26(ZA[20]);
ZA[0]+=ZR15(ZA[15])+ZR25(ZA[3])+ZA[9];
ZA[24]=ZA[14]+ZMa(ZA[23],ZA[22],ZA[19])+ZR30(ZA[19]);
ZA[14]+=ZA[21];
ZA[21]=(ZCh(ZA[14],ZA[20],ZA[18])+ZA[13]+ZA[16]+0x2e1b2138U)+ZR26(ZA[14]);
ZA[3]+=ZR15(ZA[16])+ZR25(ZA[1])+ZA[6];
ZA[22]+=ZA[21];
ZA[21]+=ZMa(ZA[19],ZA[23],ZA[24])+ZR30(ZA[24]);
ZA[13]=(ZCh(ZA[22],ZA[14],ZA[20])+ZA[18]+ZA[0]+0x4d2c6dfcU)+ZR26(ZA[22]);
ZA[1]+=ZR15(ZA[0])+ZR25(ZA[2])+ZA[10];
ZA[18]=ZA[13]+ZMa(ZA[24],ZA[19],ZA[21])+ZR30(ZA[21]);
ZA[13]+=ZA[23];
ZA[20]+=(ZCh(ZA[13],ZA[22],ZA[14])+ZA[3]+0x53380d13U)+ZR26(ZA[13]);
ZA[2]+=ZR15(ZA[3])+ZR25(ZA[8])+ZA[17];
ZA[23]=ZA[19]+ZA[20];
ZA[20]+=ZMa(ZA[21],ZA[24],ZA[18])+ZR30(ZA[18]);
ZA[19]=(ZCh(ZA[23],ZA[13],ZA[22])+ZA[14]+ZA[1]+0x650a7354U)+ZR26(ZA[23]);
ZA[8]+=ZR15(ZA[1])+ZR25(ZA[11])+ZA[5];
ZA[14]=ZA[19]+ZMa(ZA[18],ZA[21],ZA[20])+ZR30(ZA[20]);
ZA[19]+=ZA[24];
ZA[22]+=(ZCh(ZA[19],ZA[23],ZA[13])+ZA[2]+0x766a0abbU)+ZR26(ZA[19]);
ZA[11]+=ZR15(ZA[2])+ZR25(ZA[7])+ZA[15];
ZA[24]=ZA[21]+ZA[22];
ZA[22]+=ZMa(ZA[20],ZA[18],ZA[14])+ZR30(ZA[14]);
ZA[21]=(ZCh(ZA[24],ZA[19],ZA[23])+ZA[13]+ZA[8]+0x81c2c92eU)+ZR26(ZA[24]);
ZA[7]+=ZR15(ZA[8])+ZR25(ZA[12])+ZA[16];
ZA[18]+=ZA[21];
ZA[21]+=ZMa(ZA[14],ZA[20],ZA[22])+ZR30(ZA[22]);
ZA[23]+=(ZCh(ZA[18],ZA[24],ZA[19])+ZA[11]+0x92722c85U)+ZR26(ZA[18]);
ZA[12]+=ZR15(ZA[11])+ZR25(ZA[4])+ZA[0];
ZA[20]+=ZA[23];
ZA[13]=ZA[23]+ZMa(ZA[22],ZA[14],ZA[21])+ZR30(ZA[21]);
ZA[23]=(ZCh(ZA[20],ZA[18],ZA[24])+ZA[19]+ZA[7]+0xa2bfe8a1U)+ZR26(ZA[20]);
ZA[4]+=ZR15(ZA[7])+ZR25(ZA[9])+ZA[3];
ZA[14]+=ZA[23];
ZA[23]+=ZMa(ZA[21],ZA[22],ZA[13])+ZR30(ZA[13]);
ZA[24]+=(ZCh(ZA[14],ZA[20],ZA[18])+ZA[12]+0xa81a664bU)+ZR26(ZA[14]);
ZA[9]+=ZR15(ZA[12])+ZA[1]+ZR25(ZA[6]);
ZA[19]=ZA[24]+ZMa(ZA[13],ZA[21],ZA[23])+ZR30(ZA[23]);
ZA[22]+=ZA[24];
ZA[18]+=(ZCh(ZA[22],ZA[14],ZA[20])+ZA[4]+0xc24b8b70U)+ZR26(ZA[22]);
ZA[6]+=ZR15(ZA[4])+ZA[2]+ZR25(ZA[10]);
ZA[24]=ZA[21]+ZA[18];
ZA[18]+=ZMa(ZA[23],ZA[13],ZA[19])+ZR30(ZA[19]);
ZA[20]+=(ZCh(ZA[24],ZA[22],ZA[14])+ZA[9]+0xc76c51a3U)+ZR26(ZA[24]);
ZA[10]+=ZR15(ZA[9])+ZR25(ZA[17])+ZA[8];
ZA[13]+=ZA[20];
ZA[20]+=ZMa(ZA[19],ZA[23],ZA[18])+ZR30(ZA[18]);
ZA[14]+=(ZCh(ZA[13],ZA[24],ZA[22])+ZA[6]+0xd192e819U)+ZR26(ZA[13]);
ZA[17]+=ZR15(ZA[6])+ZR25(ZA[5])+ZA[11];
ZA[21]=ZA[23]+ZA[14];
ZA[14]+=ZMa(ZA[18],ZA[19],ZA[20])+ZR30(ZA[20]);
ZA[22]+=(ZCh(ZA[21],ZA[13],ZA[24])+ZA[10]+0xd6990624U)+ZR26(ZA[21]);
ZA[5]+=ZR15(ZA[10])+ZA[7]+ZR25(ZA[15]);
ZA[19]+=ZA[22];
ZA[22]+=ZMa(ZA[20],ZA[18],ZA[14])+ZR30(ZA[14]);
ZA[24]+=(ZCh(ZA[19],ZA[21],ZA[13])+ZA[17]+0xf40e3585U)+ZR26(ZA[19]);
ZA[15]+=ZR15(ZA[17])+ZA[12]+ZR25(ZA[16]);
ZA[18]+=ZA[24];
ZA[23]=ZA[24]+ZMa(ZA[14],ZA[20],ZA[22])+ZR30(ZA[22]);
ZA[13]+=(ZCh(ZA[18],ZA[19],ZA[21])+ZA[5]+0x106aa070U)+ZR26(ZA[18]);
ZA[16]+=ZR15(ZA[5])+ZA[4]+ZR25(ZA[0]);
ZA[20]+=ZA[13];
ZA[13]+=ZMa(ZA[22],ZA[14],ZA[23])+ZR30(ZA[23]);
ZA[21]+=(ZCh(ZA[20],ZA[18],ZA[19])+ZA[15]+0x19a4c116U)+ZR26(ZA[20]);
ZA[0]+=ZR15(ZA[15])+ZA[9]+ZR25(ZA[3]);
ZA[14]+=ZA[21];
ZA[24]=ZA[21]+ZMa(ZA[23],ZA[22],ZA[13])+ZR30(ZA[13]);
ZA[19]+=(ZCh(ZA[14],ZA[20],ZA[18])+ZA[16]+0x1e376c08U)+ZR26(ZA[14]);
ZA[3]+=ZR15(ZA[16])+ZA[6]+ZR25(ZA[1]);
ZA[22]+=ZA[19];
ZA[19]+=ZMa(ZA[13],ZA[23],ZA[24])+ZR30(ZA[24]);
ZA[18]+=(ZCh(ZA[22],ZA[14],ZA[20])+ZA[0]+0x2748774cU)+ZR26(ZA[22]);
ZA[1]+=ZR15(ZA[0])+ZA[10]+ZR25(ZA[2]);
ZA[23]+=ZA[18];
ZA[21]=ZA[18]+ZMa(ZA[24],ZA[13],ZA[19])+ZR30(ZA[19]);
ZA[20]+=(ZCh(ZA[23],ZA[22],ZA[14])+ZA[3]+0x34b0bcb5U)+ZR26(ZA[23]);
ZA[2]+=ZR15(ZA[3])+ZA[17]+ZR25(ZA[8]);
ZA[13]+=ZA[20];
ZA[20]+=ZMa(ZA[19],ZA[24],ZA[21])+ZR30(ZA[21]);
ZA[14]+=(ZCh(ZA[13],ZA[23],ZA[22])+ZA[1]+0x391c0cb3U)+ZR26(ZA[13]);
ZA[8]+=ZR15(ZA[1])+ZA[5]+ZR25(ZA[11]);
ZA[24]+=ZA[14];
ZA[18]=ZA[14]+ZMa(ZA[21],ZA[19],ZA[20])+ZR30(ZA[20]);
ZA[22]+=(ZCh(ZA[24],ZA[13],ZA[23])+ZA[2]+0x4ed8aa4aU)+ZR26(ZA[24]);
ZA[11]+=ZR15(ZA[2])+ZA[15]+ZR25(ZA[7]);
ZA[19]+=ZA[22];
ZA[22]+=ZMa(ZA[20],ZA[21],ZA[18])+ZR30(ZA[18]);
ZA[23]+=(ZCh(ZA[19],ZA[24],ZA[13])+ZA[8]+0x5b9cca4fU)+ZR26(ZA[19]);
ZA[7]+=ZR15(ZA[8])+ZA[16]+ZR25(ZA[12]);
ZA[21]+=ZA[23];
ZA[23]+=ZMa(ZA[18],ZA[20],ZA[22])+ZR30(ZA[22]);
ZA[13]+=(ZCh(ZA[21],ZA[19],ZA[24])+ZA[11]+0x682e6ff3U)+ZR26(ZA[21]);
ZA[0]+=ZR15(ZA[11])+ZR25(ZA[4])+ZA[12];
ZA[12]=ZA[20]+ZA[13];
ZA[16]=ZA[13]+ZMa(ZA[22],ZA[18],ZA[23])+ZR30(ZA[23]);
ZA[20]=(ZCh(ZA[12],ZA[21],ZA[19])+ZA[24]+ZA[7]+0x748f82eeU)+ZR26(ZA[12]);
ZA[3]+=ZR15(ZA[7])+ZR25(ZA[9])+ZA[4];
ZA[18]+=ZA[20];
ZA[20]+=ZMa(ZA[23],ZA[22],ZA[16])+ZR30(ZA[16]);
ZA[19]+=(ZCh(ZA[18],ZA[12],ZA[21])+ZA[0]+0x78a5636fU)+ZR26(ZA[18]);
ZA[1]+=ZR15(ZA[0])+ZR25(ZA[6])+ZA[9];
ZA[9]=ZA[22]+ZA[19];
ZA[4]=ZA[19]+ZMa(ZA[16],ZA[23],ZA[20])+ZR30(ZA[20]);
ZA[0]=(ZCh(ZA[9],ZA[18],ZA[12])+ZA[21]+ZA[3]+0x84c87814U)+ZR26(ZA[9]);
ZA[2]+=ZR15(ZA[3])+ZR25(ZA[10])+ZA[6];
ZA[6]=ZA[23]+ZA[0];
ZA[0]+=ZMa(ZA[20],ZA[16],ZA[4])+ZR30(ZA[4]);
ZA[12]+=(ZCh(ZA[6],ZA[9],ZA[18])+ZA[1]+0x8cc70208U)+ZR26(ZA[6]);
ZA[8]+=ZR15(ZA[1])+ZR25(ZA[17])+ZA[10];
ZA[16]+=ZA[12];
ZA[10]=ZA[12]+ZMa(ZA[4],ZA[20],ZA[0])+ZR30(ZA[0]);
ZA[1]=(ZCh(ZA[16],ZA[6],ZA[9])+ZA[18]+ZA[2]+0x90befffaU)+ZR26(ZA[16]);
ZA[3]=ZA[20]+ZA[1];
ZA[1]+=ZMa(ZA[0],ZA[4],ZA[10])+ZR30(ZA[10]);
ZA[9]+=(ZCh(ZA[3],ZA[16],ZA[6])+ZA[8]+0xa4506cebU)+ZR26(ZA[3]);
ZA[12]=ZA[9]+ZMa(ZA[10],ZA[0],ZA[1])+ZR30(ZA[1]);
ZA[4]+=ZA[9];
ZA[6]+=(ZCh(ZA[4],ZA[3],ZA[16])+ZR15(ZA[2])+ZA[11]+ZR25(ZA[5])+ZA[17]+0xbef9a3f7U)+ZR26(ZA[4]);
ZA[17]=ZA[0]+ZA[6];
ZA[9]=ZA[6]+ZMa(ZA[1],ZA[10],ZA[12])+ZR30(ZA[12]);
ZA[7]+=(ZCh(ZA[17],ZA[4],ZA[3])+ZA[16]+ZR15(ZA[8])+ZR25(ZA[15])+ZA[5]+0xc67178f2U)+ZR26(ZA[17]);
ZA[5]=ZA[9]+state1;
ZA[9]=ZA[7]+ZMa(ZA[12],ZA[1],ZA[9])+ZR30(ZA[9])+state0;
ZA[15]=ZA[9]+0x98c7e2a2U;
ZA[0]=(ZCh(ZA[15],0x510e527fU,0x9b05688cU)+ZA[5]+0x90bb1e3cU)+ZR26(ZA[15]);
ZA[12]+=state2;
ZA[2]=ZA[9]+0xfc08884dU;
ZA[8]=ZA[0]+0x3c6ef372U;
ZA[11]=(ZCh(ZA[8],ZA[15],0x510e527fU)+ZA[12]+0x50c6645bU)+ZR26(ZA[8]);
ZA[1]+=state3;
ZA[0]+=ZMa(0x6a09e667U,0xbb67ae85U,ZA[2])+ZR30(ZA[2]);
ZA[6]=ZA[11]+0xbb67ae85U;
ZA[15]=(ZCh(ZA[6],ZA[8],ZA[15])+ZA[1]+0x3ac42e24U)+ZR26(ZA[6]);
ZA[10]+=ZA[7]+state4;
ZA[7]=ZA[15]+0x6a09e667U;
ZA[11]+=ZMa(ZA[2],0x6a09e667U,ZA[0])+ZR30(ZA[0]);
ZA[17]+=state5;
ZA[16]=(ZCh(ZA[7],ZA[6],ZA[8])+ZA[10]+ZA[9]+0xd21ea4fdU)+ZR26(ZA[7]);
ZA[24]=ZA[2]+ZA[16];
ZA[2]=ZA[15]+ZMa(ZA[0],ZA[2],ZA[11])+ZR30(ZA[11]);
ZA[4]+=state6;
ZA[8]+=(ZCh(ZA[24],ZA[7],ZA[6])+ZA[17]+0x59f111f1U)+ZR26(ZA[24]);
ZA[15]=ZA[0]+ZA[8];
ZA[16]+=ZMa(ZA[11],ZA[0],ZA[2])+ZR30(ZA[2]);
ZA[3]+=state7;
ZA[6]+=(ZCh(ZA[15],ZA[24],ZA[7])+ZA[4]+0x923f82a4U)+ZR26(ZA[15]);
ZA[0]=ZA[11]+ZA[6];
ZA[8]+=ZMa(ZA[2],ZA[11],ZA[16])+ZR30(ZA[16]);
ZA[7]+=(ZCh(ZA[0],ZA[15],ZA[24])+ZA[3]+0xab1c5ed5U)+ZR26(ZA[0]);
ZA[11]=ZA[2]+ZA[7];
ZA[2]=ZMa(ZA[16],ZA[2],ZA[8])+ZR30(ZA[8])+ZA[6];
ZA[24]+=(ZCh(ZA[11],ZA[0],ZA[15])+0x5807aa98U)+ZR26(ZA[11]);
ZA[6]=ZA[16]+ZA[24];
ZA[7]+=ZMa(ZA[8],ZA[16],ZA[2])+ZR30(ZA[2]);
ZA[15]+=(ZCh(ZA[6],ZA[11],ZA[0])+0x12835b01U)+ZR26(ZA[6]);
ZA[16]=ZA[8]+ZA[15];
ZA[8]=ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7])+ZA[24];
ZA[0]+=(ZCh(ZA[16],ZA[6],ZA[11])+0x243185beU)+ZR26(ZA[16]);
ZA[14]=ZA[2]+ZA[0];
ZA[2]=ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8])+ZA[15];
ZA[11]+=(ZCh(ZA[14],ZA[16],ZA[6])+0x550c7dc3U)+ZR26(ZA[14]);
ZA[15]=ZA[7]+ZA[11];
ZA[7]=ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2])+ZA[0];
ZA[6]+=(ZCh(ZA[15],ZA[14],ZA[16])+0x72be5d74U)+ZR26(ZA[15]);
ZA[0]=ZA[8]+ZA[6];
ZA[8]=ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7])+ZA[11];
ZA[16]+=(ZCh(ZA[0],ZA[15],ZA[14])+0x80deb1feU)+ZR26(ZA[0]);
ZA[11]=ZA[2]+ZA[16];
ZA[2]=ZA[6]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[14]+=(ZCh(ZA[11],ZA[0],ZA[15])+0x9bdc06a7U)+ZR26(ZA[11]);
ZA[9]+=ZR25(ZA[5]);
ZA[6]=ZA[7]+ZA[14];
ZA[7]=ZA[16]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[15]+=(ZCh(ZA[6],ZA[11],ZA[0])+0xc19bf274U)+ZR26(ZA[6]);
ZA[5]+=ZR25(ZA[12])+0x00a00000U;
ZA[16]=ZA[8]+ZA[15];
ZA[8]=ZA[14]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[0]+=(ZCh(ZA[16],ZA[6],ZA[11])+ZA[9]+0xe49b69c1U)+ZR26(ZA[16]);
ZA[21]=ZA[2]+ZA[0];
ZA[2]=ZA[15]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[11]+=(ZCh(ZA[21],ZA[16],ZA[6])+ZA[5]+0xefbe4786U)+ZR26(ZA[21]);
ZA[12]+=ZR15(ZA[9])+ZR25(ZA[1]);
ZA[1]+=ZR15(ZA[5])+ZR25(ZA[10]);
ZA[15]=ZA[7]+ZA[11];
ZA[7]=ZA[0]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[6]+=(ZCh(ZA[15],ZA[21],ZA[16])+0x0fc19dc6U+ZA[12])+ZR26(ZA[15]);
ZA[0]=ZR15(ZA[12])+ZR25(ZA[17])+ZA[10];
ZA[10]=ZA[8]+ZA[6];
ZA[8]=ZA[11]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[16]+=(ZCh(ZA[10],ZA[15],ZA[21])+ZA[1]+0x240ca1ccU)+ZR26(ZA[10]);
ZA[17]+=ZR15(ZA[1])+ZR25(ZA[4]);
ZA[11]=ZA[2]+ZA[16];
ZA[2]=ZA[6]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[21]+=(ZCh(ZA[11],ZA[10],ZA[15])+ZA[0]+0x2de92c6fU)+ZR26(ZA[11]);
ZA[4]+=ZR15(ZA[0])+0x00000100U+ZR25(ZA[3]);
ZA[6]=ZA[7]+ZA[21];
ZA[7]=ZA[16]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[15]+=(ZCh(ZA[6],ZA[11],ZA[10])+ZA[17]+0x4a7484aaU)+ZR26(ZA[6]);
ZA[3]+=ZA[9]+ZR15(ZA[17])+0x11002000U;
ZA[16]=ZA[8]+ZA[15];
ZA[8]=ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7])+ZA[21];
ZA[10]+=(ZCh(ZA[16],ZA[6],ZA[11])+ZA[4]+0x5cb0a9dcU)+ZR26(ZA[16]);
ZA[13]=ZR15(ZA[4])+ZA[5]+0x80000000U;
ZA[22]=ZA[2]+ZA[10];
ZA[2]=ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8])+ZA[15];
ZA[11]+=(ZCh(ZA[22],ZA[16],ZA[6])+ZA[3]+0x76f988daU)+ZR26(ZA[22]);
ZA[15]=ZR15(ZA[3])+ZA[12];
ZA[18]=ZA[7]+ZA[11];
ZA[7]=ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2])+ZA[10];
ZA[6]+=(ZCh(ZA[18],ZA[22],ZA[16])+ZA[13]+0x983e5152U)+ZR26(ZA[18]);
ZA[10]=ZR15(ZA[13])+ZA[1];
ZA[23]=ZA[8]+ZA[6];
ZA[8]=ZA[11]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[16]+=(ZCh(ZA[23],ZA[18],ZA[22])+ZA[15]+0xa831c66dU)+ZR26(ZA[23]);
ZA[11]=ZR15(ZA[15])+ZA[0];
ZA[24]=ZA[2]+ZA[16];
ZA[2]=ZA[6]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[22]+=(ZCh(ZA[24],ZA[23],ZA[18])+ZA[10]+0xb00327c8U)+ZR26(ZA[24]);
ZA[6]=ZR15(ZA[10])+ZA[17];
ZA[19]=ZA[7]+ZA[22];
ZA[7]=ZA[16]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[18]+=(ZCh(ZA[19],ZA[24],ZA[23])+ZA[11]+0xbf597fc7U)+ZR26(ZA[19]);
ZA[14]=ZR15(ZA[11])+ZA[4];
ZA[20]=ZA[8]+ZA[18];
ZA[8]=ZA[22]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[23]+=(ZCh(ZA[20],ZA[19],ZA[24])+ZA[6]+0xc6e00bf3U)+ZR26(ZA[20]);
ZA[16]=ZR15(ZA[6])+ZA[3]+0x00400022U;
ZA[21]=ZA[2]+ZA[23];
ZA[2]=ZA[18]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[24]+=(ZCh(ZA[21],ZA[20],ZA[19])+ZA[14]+0xd5a79147U)+ZR26(ZA[21]);
ZA[22]=ZR15(ZA[14])+ZA[13]+ZR25(ZA[9])+0x00000100U;
ZA[18]=ZA[7]+ZA[24];
ZA[7]=ZA[23]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[19]+=(ZCh(ZA[18],ZA[21],ZA[20])+ZA[16]+0x06ca6351U)+ZR26(ZA[18]);
ZA[9]+=ZR15(ZA[16])+ZR25(ZA[5])+ZA[15];
ZA[23]=ZA[8]+ZA[19];
ZA[8]=ZA[24]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[20]+=(ZCh(ZA[23],ZA[18],ZA[21])+ZA[22]+0x14292967U)+ZR26(ZA[23]);
ZA[5]+=ZR15(ZA[22])+ZR25(ZA[12])+ZA[10];
ZA[24]=ZA[2]+ZA[20];
ZA[2]=ZA[19]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[21]+=(ZCh(ZA[24],ZA[23],ZA[18])+ZA[9]+0x27b70a85U)+ZR26(ZA[24]);
ZA[12]+=ZR15(ZA[9])+ZA[11]+ZR25(ZA[1]);
ZA[19]=ZA[7]+ZA[21];
ZA[7]=ZA[20]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[18]+=(ZCh(ZA[19],ZA[24],ZA[23])+ZA[5]+0x2e1b2138U)+ZR26(ZA[19]);
ZA[1]+=ZR15(ZA[5])+ZA[6]+ZR25(ZA[0]);
ZA[20]=ZA[8]+ZA[18];
ZA[8]=ZA[21]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[23]+=(ZCh(ZA[20],ZA[19],ZA[24])+ZA[12]+0x4d2c6dfcU)+ZR26(ZA[20]);
ZA[0]+=ZR15(ZA[12])+ZR25(ZA[17])+ZA[14];
ZA[21]=ZA[2]+ZA[23];
ZA[2]=ZA[18]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[24]+=(ZCh(ZA[21],ZA[20],ZA[19])+ZA[1]+0x53380d13U)+ZR26(ZA[21]);
ZA[17]+=ZR15(ZA[1])+ZA[16]+ZR25(ZA[4]);
ZA[18]=ZA[7]+ZA[24];
ZA[7]=ZA[23]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[19]+=(ZCh(ZA[18],ZA[21],ZA[20])+ZA[0]+0x650a7354U)+ZR26(ZA[18]);
ZA[4]+=ZR15(ZA[0])+ZA[22]+ZR25(ZA[3]);
ZA[23]=ZA[8]+ZA[19];
ZA[8]=ZA[24]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[20]+=(ZCh(ZA[23],ZA[18],ZA[21])+ZA[17]+0x766a0abbU)+ZR26(ZA[23]);
ZA[3]+=ZR15(ZA[17])+ZA[9]+ZR25(ZA[13]);
ZA[24]=ZA[2]+ZA[20];
ZA[2]=ZA[19]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[21]+=(ZCh(ZA[24],ZA[23],ZA[18])+ZA[4]+0x81c2c92eU)+ZR26(ZA[24]);
ZA[13]+=ZR15(ZA[4])+ZA[5]+ZR25(ZA[15]);
ZA[19]=ZA[7]+ZA[21];
ZA[7]=ZA[20]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[18]+=(ZCh(ZA[19],ZA[24],ZA[23])+ZA[3]+0x92722c85U)+ZR26(ZA[19]);
ZA[15]+=ZR15(ZA[3])+ZA[12]+ZR25(ZA[10]);
ZA[20]=ZA[8]+ZA[18];
ZA[8]=ZA[21]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[23]+=(ZCh(ZA[20],ZA[19],ZA[24])+ZA[13]+0xa2bfe8a1U)+ZR26(ZA[20]);
ZA[10]+=ZR15(ZA[13])+ZA[1]+ZR25(ZA[11]);
ZA[21]=ZA[2]+ZA[23];
ZA[2]=ZA[18]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[24]+=(ZCh(ZA[21],ZA[20],ZA[19])+ZA[15]+0xa81a664bU)+ZR26(ZA[21]);
ZA[11]+=ZR15(ZA[15])+ZA[0]+ZR25(ZA[6]);
ZA[18]=ZA[7]+ZA[24];
ZA[7]=ZA[23]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[19]+=(ZCh(ZA[18],ZA[21],ZA[20])+ZA[10]+0xc24b8b70U)+ZR26(ZA[18]);
ZA[6]+=ZR15(ZA[10])+ZA[17]+ZR25(ZA[14]);
ZA[23]=ZA[8]+ZA[19];
ZA[8]=ZA[24]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[20]+=(ZCh(ZA[23],ZA[18],ZA[21])+ZA[11]+0xc76c51a3U)+ZR26(ZA[23]);
ZA[14]+=ZR15(ZA[11])+ZA[4]+ZR25(ZA[16]);
ZA[24]=ZA[2]+ZA[20];
ZA[2]=ZA[19]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[21]+=(ZCh(ZA[24],ZA[23],ZA[18])+ZA[6]+0xd192e819U)+ZR26(ZA[24]);
ZA[16]+=ZR15(ZA[6])+ZA[3]+ZR25(ZA[22]);
ZA[19]=ZA[7]+ZA[21];
ZA[7]=ZA[20]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[18]+=(ZCh(ZA[19],ZA[24],ZA[23])+ZA[14]+0xd6990624U)+ZR26(ZA[19]);
ZA[22]+=ZR15(ZA[14])+ZA[13]+ZR25(ZA[9]);
ZA[20]=ZA[8]+ZA[18];
ZA[8]=ZA[21]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[23]+=(ZCh(ZA[20],ZA[19],ZA[24])+ZA[16]+0xf40e3585U)+ZR26(ZA[20]);
ZA[9]+=ZR15(ZA[16])+ZA[15]+ZR25(ZA[5]);
ZA[21]=ZA[2]+ZA[23];
ZA[2]=ZA[18]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[24]+=(ZCh(ZA[21],ZA[20],ZA[19])+ZA[22]+0x106aa070U)+ZR26(ZA[21]);
ZA[5]+=ZR15(ZA[22])+ZA[10]+ZR25(ZA[12]);
ZA[18]=ZA[7]+ZA[24];
ZA[7]=ZA[23]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[19]+=(ZCh(ZA[18],ZA[21],ZA[20])+ZA[9]+0x19a4c116U)+ZR26(ZA[18]);
ZA[12]+=ZR15(ZA[9])+ZA[11]+ZR25(ZA[1]);
ZA[23]=ZA[8]+ZA[19];
ZA[8]=ZA[24]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[20]+=(ZCh(ZA[23],ZA[18],ZA[21])+ZA[5]+0x1e376c08U)+ZR26(ZA[23]);
ZA[1]+=ZR15(ZA[5])+ZA[6]+ZR25(ZA[0]);
ZA[24]=ZA[2]+ZA[20];
ZA[2]=ZA[19]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[21]+=(ZCh(ZA[24],ZA[23],ZA[18])+ZA[12]+0x2748774cU)+ZR26(ZA[24]);
ZA[0]+=ZR15(ZA[12])+ZA[14]+ZR25(ZA[17]);
ZA[19]=ZA[7]+ZA[21];
ZA[7]=ZA[20]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[18]+=(ZCh(ZA[19],ZA[24],ZA[23])+ZA[1]+0x34b0bcb5U)+ZR26(ZA[19]);
ZA[17]+=ZR15(ZA[1])+ZA[16]+ZR25(ZA[4]);
ZA[16]=ZA[8]+ZA[18];
ZA[8]=ZA[21]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[23]+=(ZCh(ZA[16],ZA[19],ZA[24])+ZA[0]+0x391c0cb3U)+ZR26(ZA[16]);
ZA[4]+=ZR15(ZA[0])+ZA[22]+ZR25(ZA[3]);
ZA[22]=ZA[2]+ZA[23];
ZA[2]=ZA[18]+ZMa(ZA[7],ZA[2],ZA[8])+ZR30(ZA[8]);
ZA[24]+=(ZCh(ZA[22],ZA[16],ZA[19])+ZA[17]+0x4ed8aa4aU)+ZR26(ZA[22]);
ZA[9]+=ZR15(ZA[17])+ZR25(ZA[13])+ZA[3];
ZA[3]=ZA[7]+ZA[24];
ZA[7]=ZA[23]+ZMa(ZA[8],ZA[7],ZA[2])+ZR30(ZA[2]);
ZA[19]+=(ZCh(ZA[3],ZA[22],ZA[16])+ZA[4]+0x5b9cca4fU)+ZR26(ZA[3]);
ZA[13]+=ZR15(ZA[4])+ZA[5]+ZR25(ZA[15]);
ZA[4]=ZA[8]+ZA[19];
ZA[5]=ZA[24]+ZMa(ZA[2],ZA[8],ZA[7])+ZR30(ZA[7]);
ZA[16]+=(ZCh(ZA[4],ZA[3],ZA[22])+ZA[9]+0x682e6ff3U)+ZR26(ZA[4]);
ZA[12]+=ZR15(ZA[9])+ZR25(ZA[10])+ZA[15];
ZA[15]=ZA[2]+ZA[16];
ZA[9]=ZA[19]+ZMa(ZA[7],ZA[2],ZA[5])+ZR30(ZA[5]);
ZA[22]+=(ZCh(ZA[15],ZA[4],ZA[3])+ZA[13]+0x748f82eeU)+ZR26(ZA[15]);
ZA[13]=ZR15(ZA[13])+ZA[1]+ZR25(ZA[11])+ZA[10];
ZA[10]=ZA[7]+ZA[22];
ZA[1]=ZA[16]+ZMa(ZA[5],ZA[7],ZA[9])+ZR30(ZA[9]);
ZA[3]+=(ZCh(ZA[10],ZA[15],ZA[4])+ZA[5]+ZA[12]+0x78a5636fU)+ZR26(ZA[10]);
ZA[4]+=(ZCh(ZA[3],ZA[10],ZA[15])+ZA[9]+ZA[13]+0x84c87814U)+ZR26(ZA[3]);
ZA[15]+=(ZCh(ZA[4],ZA[3],ZA[10])+ZA[1]+ZR15(ZA[12])+ZA[0]+ZR25(ZA[6])+ZA[11]+0x8cc70208U)+ZR26(ZA[4]);
ZA[10]+=(ZCh(ZA[15],ZA[4],ZA[3])+ZA[22]+ZMa(ZA[9],ZA[5],ZA[1])+ZR30(ZA[1])+ZR15(ZA[13])+ZA[17]+ZR25(ZA[14])+ZA[6])+ZR26(ZA[15]);
    
#define FOUND (0x80)
#define NFLAG (0x7F)

#if defined(VECTORS4)
	bool result = any(ZA[10] == 0x136032EDU);

	if (result) {
		if (ZA[10].x == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.x] =  Znonce.x;
		if (ZA[10].y == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.y] =  Znonce.y;
		if (ZA[10].z == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.z] =  Znonce.z;
		if (ZA[10].w == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.w] =  Znonce.w;
	}
#elif defined(VECTORS2)
	bool result = any(ZA[10] == 0x136032EDU);

	if (result) {
		if (ZA[10].x == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.x] =  Znonce.x;
		if (ZA[10].y == 0x136032EDU)
			output[FOUND] = output[NFLAG & Znonce.y] =  Znonce.y;
	}
#else
	if (ZA[10] == 0x136032EDU)
		output[FOUND] = output[NFLAG & Znonce] =  Znonce;
#endif
}
