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
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

typedef uint z;

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
    const uint base,
    const uint PreVal4_plus_state0, const uint PreVal4_plus_T1,
    const uint W18, const uint W19,
    const uint W16, const uint W17,
    const uint W31, const uint W32,
    const uint d1, const uint b1, const uint c1,
    const uint h1, const uint f1, const uint g1,
    const uint c1_plus_k5, const uint b1_plus_k6,
    const uint state0, const uint state1, const uint state2, const uint state3,
    const uint state4, const uint state5, const uint state6, const uint state7,
    __global uint * output)
{
  z ZA[4];
  z ZB[4];
  z ZC[4];
  z ZD[4];
  z ZE[4];
  z ZF[4];
  z ZG[4];
  z ZH[4];

  z Znonce = base + get_global_id(0);

    ZA[0] = PreVal4_plus_state0 + Znonce;
    ZB[0] = PreVal4_plus_T1 + Znonce;

    ZC[0] = W18 + ZR25(Znonce);
    ZD[0] = W19 + Znonce;
    ZE[0] = 0x80000000U + ZR15(ZC[0]);
    ZF[0] = ZR15(ZD[0]);
    ZG[0] = 0x00000280U + ZR15(ZE[0]);
    ZH[0] = ZR15(ZF[0]) + W16;
    ZA[1] = ZR15(ZG[0]) + W17;
    ZB[1] = ZR15(ZH[0]) + ZC[0];
    ZC[1] = ZR15(ZA[1]) + ZD[0];
    ZD[1] = ZR15(ZB[1]) + ZE[0];
    ZE[1] = ZR15(ZC[1]) + ZF[0];
    ZF[1] = ZR15(ZD[1]) + ZG[0];
    ZG[1] = 0x00A00055U + ZR15(ZE[1]) + ZH[0];
    ZH[1] = W31 + ZR15(ZF[1]) + ZA[1];
    ZA[2] = W32 + ZR15(ZG[1]) + ZB[1];

    ZB[2] = d1 + ZCh(ZA[0], b1, c1) + ZR26(ZA[0]);
    ZC[2] = h1 + ZB[2];
    ZD[2] = ZB[2] + ZR30(ZB[0]) + ZMa(f1, g1, ZB[0]);
    ZE[2] = c1_plus_k5 + ZCh(ZC[2], ZA[0], b1) + ZR26(ZC[2]);
    ZF[2] = g1 + ZE[2];
    ZG[2] = ZE[2] + ZR30(ZD[2]) + ZMa(ZB[0], f1, ZD[2]);
    ZH[2] = b1_plus_k6 + ZCh(ZF[2], ZC[2], ZA[0]) + ZR26(ZF[2]);
    ZA[3] = f1 + ZH[2];
    ZB[3] = ZH[2] + ZR30(ZG[2]) + ZMa(ZD[2], ZB[0], ZG[2]);
    ZC[3] = ZA[0] + 0xab1c5ed5U + ZCh(ZA[3], ZF[2], ZC[2]) + ZR26(ZA[3]);
    ZD[3] = ZB[0] + ZC[3];
    ZE[3] = ZC[3] + ZR30(ZB[3]) + ZMa(ZG[2], ZD[2], ZB[3]);
    ZF[3] = ZC[2] + 0xd807aa98U + ZCh(ZD[3], ZA[3], ZF[2]) + ZR26(ZD[3]);
    ZG[3] = ZD[2] + ZF[3];
    ZH[3] = ZF[3] + ZR30(ZE[3]) + ZMa(ZB[3], ZG[2], ZE[3]);
    ZA[0] = ZF[2] + 0x12835b01U + ZCh(ZG[3], ZD[3], ZA[3]) + ZR26(ZG[3]);
    ZB[0] = ZG[2] + ZA[0];
    ZB[2] = ZA[0] + ZR30(ZH[3]) + ZMa(ZE[3], ZB[3], ZH[3]);
    ZC[2] = ZA[3] + 0x243185beU + ZCh(ZB[0], ZG[3], ZD[3]) + ZR26(ZB[0]);
    ZD[2] = ZB[3] + ZC[2];
    ZE[2] = ZC[2] + ZR30(ZB[2]) + ZMa(ZH[3], ZE[3], ZB[2]);
    ZF[2] = ZD[3] + 0x550c7dc3U + ZCh(ZD[2], ZB[0], ZG[3]) + ZR26(ZD[2]);
    ZG[2] = ZE[3] + ZF[2];
    ZH[2] = ZF[2] + ZR30(ZE[2]) + ZMa(ZB[2], ZH[3], ZE[2]);
    ZA[3] = ZG[3] + 0x72be5d74U + ZCh(ZG[2], ZD[2], ZB[0]) + ZR26(ZG[2]);
    ZB[3] = ZH[3] + ZA[3];
    ZC[3] = ZA[3] + ZR30(ZH[2]) + ZMa(ZE[2], ZB[2], ZH[2]);
    ZD[3] = ZB[0] + 0x80deb1feU + ZCh(ZB[3], ZG[2], ZD[2]) + ZR26(ZB[3]);
    ZE[3] = ZB[2] + ZD[3];
    ZF[3] = ZD[3] + ZR30(ZC[3]) + ZMa(ZH[2], ZE[2], ZC[3]);
    ZG[3] = ZD[2] + 0x9bdc06a7U + ZCh(ZE[3], ZB[3], ZG[2]) + ZR26(ZE[3]);
    ZH[3] = ZE[2] + ZG[3];
    ZA[0] = ZG[3] + ZR30(ZF[3]) + ZMa(ZC[3], ZH[2], ZF[3]);
    ZB[0] = ZG[2] + 0xc19bf3f4U + ZCh(ZH[3], ZE[3], ZB[3]) + ZR26(ZH[3]);
    ZB[2] = ZH[2] + ZB[0];
    ZC[2] = ZB[0] + ZR30(ZA[0]) + ZMa(ZF[3], ZC[3], ZA[0]);
    ZD[2] = ZB[3] + 0xe49b69c1U + W16 + ZCh(ZB[2], ZH[3], ZE[3]) + ZR26(ZB[2]);
    ZE[2] = ZC[3] + ZD[2];
    ZF[2] = ZD[2] + ZR30(ZC[2]) + ZMa(ZA[0], ZF[3], ZC[2]);
    ZG[2] = ZE[3] + 0xefbe4786U + W17 + ZCh(ZE[2], ZB[2], ZH[3]) + ZR26(ZE[2]);
    ZH[2] = ZF[3] + ZG[2];
    ZA[3] = ZG[2] + ZR30(ZF[2]) + ZMa(ZC[2], ZA[0], ZF[2]);
    ZB[3] = ZH[3] + 0x0fc19dc6U + ZC[0] + ZCh(ZH[2], ZE[2], ZB[2]) + ZR26(ZH[2]);
    ZC[3] = ZA[0] + ZB[3];
    ZD[3] = ZB[3] + ZR30(ZA[3]) + ZMa(ZF[2], ZC[2], ZA[3]);
    ZE[3] = ZB[2] + 0x240ca1ccU + ZD[0] + ZCh(ZC[3], ZH[2], ZE[2]) + ZR26(ZC[3]);
    ZF[3] = ZC[2] + ZE[3];
    ZG[3] = ZE[3] + ZR30(ZD[3]) + ZMa(ZA[3], ZF[2], ZD[3]);
    ZH[3] = ZE[2] + 0x2de92c6fU + ZE[0] + ZCh(ZF[3], ZC[3], ZH[2]) + ZR26(ZF[3]);
    ZA[0] = ZF[2] + ZH[3];
    ZB[0] = ZH[3] + ZR30(ZG[3]) + ZMa(ZD[3], ZA[3], ZG[3]);
    ZB[2] = ZH[2] + 0x4a7484aaU + ZF[0] + ZCh(ZA[0], ZF[3], ZC[3]) + ZR26(ZA[0]);
    ZC[2] = ZA[3] + ZB[2];
    ZD[2] = ZB[2] + ZR30(ZB[0]) + ZMa(ZG[3], ZD[3], ZB[0]);
    ZE[2] = ZC[3] + 0x5cb0a9dcU + ZG[0] + ZCh(ZC[2], ZA[0], ZF[3]) + ZR26(ZC[2]);
    ZF[2] = ZD[3] + ZE[2];
    ZG[2] = ZE[2] + ZR30(ZD[2]) + ZMa(ZB[0], ZG[3], ZD[2]);
    ZH[2] = ZF[3] + 0x76f988daU + ZH[0] + ZCh(ZF[2], ZC[2], ZA[0]) + ZR26(ZF[2]);
    ZA[3] = ZG[3] + ZH[2];
    ZB[3] = ZH[2] + ZR30(ZG[2]) + ZMa(ZD[2], ZB[0], ZG[2]);
    ZC[3] = ZA[0] + 0x983e5152U + ZA[1] + ZCh(ZA[3], ZF[2], ZC[2]) + ZR26(ZA[3]);
    ZD[3] = ZB[0] + ZC[3];
    ZE[3] = ZC[3] + ZR30(ZB[3]) + ZMa(ZG[2], ZD[2], ZB[3]);
    ZF[3] = ZC[2] + 0xa831c66dU + ZB[1] + ZCh(ZD[3], ZA[3], ZF[2]) + ZR26(ZD[3]);
    ZG[3] = ZD[2] + ZF[3];
    ZH[3] = ZF[3] + ZR30(ZE[3]) + ZMa(ZB[3], ZG[2], ZE[3]);
    ZA[0] = ZF[2] + 0xb00327c8U + ZC[1] + ZCh(ZG[3], ZD[3], ZA[3]) + ZR26(ZG[3]);
    ZB[0] = ZG[2] + ZA[0];
    ZB[2] = ZA[0] + ZR30(ZH[3]) + ZMa(ZE[3], ZB[3], ZH[3]);
    ZC[2] = ZA[3] + 0xbf597fc7U + ZD[1] + ZCh(ZB[0], ZG[3], ZD[3]) + ZR26(ZB[0]);
    ZD[2] = ZB[3] + ZC[2];
    ZE[2] = ZC[2] + ZR30(ZB[2]) + ZMa(ZH[3], ZE[3], ZB[2]);
    ZF[2] = ZD[3] + 0xc6e00bf3U + ZE[1] + ZCh(ZD[2], ZB[0], ZG[3]) + ZR26(ZD[2]);
    ZG[2] = ZE[3] + ZF[2];
    ZH[2] = ZF[2] + ZR30(ZE[2]) + ZMa(ZB[2], ZH[3], ZE[2]);
    ZA[3] = ZG[3] + 0xd5a79147U + ZF[1] + ZCh(ZG[2], ZD[2], ZB[0]) + ZR26(ZG[2]);
    ZB[3] = ZH[3] + ZA[3];
    ZC[3] = ZA[3] + ZR30(ZH[2]) + ZMa(ZE[2], ZB[2], ZH[2]);
    ZD[3] = ZB[0] + 0x06ca6351U + ZG[1] + ZCh(ZB[3], ZG[2], ZD[2]) + ZR26(ZB[3]);
    ZE[3] = ZB[2] + ZD[3];
    ZF[3] = ZD[3] + ZR30(ZC[3]) + ZMa(ZH[2], ZE[2], ZC[3]);
    ZG[3] = ZD[2] + 0x14292967U + ZH[1] + ZCh(ZE[3], ZB[3], ZG[2]) + ZR26(ZE[3]);
    ZH[3] = ZE[2] + ZG[3];
    ZA[0] = ZG[3] + ZR30(ZF[3]) + ZMa(ZC[3], ZH[2], ZF[3]);
    ZB[0] = ZG[2] + 0x27b70a85U + ZA[2] + ZCh(ZH[3], ZE[3], ZB[3]) + ZR26(ZH[3]);
    ZB[2] = ZH[2] + ZB[0];
    ZC[2] = ZB[0] + ZR30(ZA[0]) + ZMa(ZF[3], ZC[3], ZA[0]);
    ZD[2] = ZR15(ZH[1]) + ZR25(ZC[0]) + ZC[1] + W17;
    ZE[2] = ZB[3] + 0x2e1b2138U + ZD[2] + ZCh(ZB[2], ZH[3], ZE[3]) + ZR26(ZB[2]);
    ZF[2] = ZC[3] + ZE[2];
    ZG[2] = ZE[2] + ZR30(ZC[2]) + ZMa(ZA[0], ZF[3], ZC[2]);
    ZH[2] = ZR15(ZA[2]) + ZR25(ZD[0]) + ZD[1] + ZC[0];
    ZA[3] = ZE[3] + 0x4d2c6dfcU + ZH[2] + ZCh(ZF[2], ZB[2], ZH[3]) + ZR26(ZF[2]);
    ZB[3] = ZF[3] + ZA[3];
    ZC[3] = ZA[3] + ZR30(ZG[2]) + ZMa(ZC[2], ZA[0], ZG[2]);
    ZD[3] = ZR15(ZD[2]) + ZR25(ZE[0]) + ZE[1] + ZD[0];
    ZE[3] = ZH[3] + 0x53380d13U + ZD[3] + ZCh(ZB[3], ZF[2], ZB[2]) + ZR26(ZB[3]);
    ZF[3] = ZA[0] + ZE[3];
    ZG[3] = ZE[3] + ZR30(ZC[3]) + ZMa(ZG[2], ZC[2], ZC[3]);
    ZH[3] = ZR15(ZH[2]) + ZR25(ZF[0]) + ZF[1] + ZE[0];
    ZA[0] = ZB[2] + 0x650a7354U + ZH[3] + ZCh(ZF[3], ZB[3], ZF[2]) + ZR26(ZF[3]);
    ZB[0] = ZC[2] + ZA[0];
    ZC[0] = ZA[0] + ZR30(ZG[3]) + ZMa(ZC[3], ZG[2], ZG[3]);
    ZD[0] = ZR15(ZD[3]) + ZR25(ZG[0]) + ZG[1] + ZF[0];
    ZE[0] = ZF[2] + 0x766a0abbU + ZD[0] + ZCh(ZB[0], ZF[3], ZB[3]) + ZR26(ZB[0]);
    ZF[0] = ZG[2] + ZE[0];
    ZB[2] = ZE[0] + ZR30(ZC[0]) + ZMa(ZG[3], ZC[3], ZC[0]);
    ZC[2] = ZR15(ZH[3]) + ZR25(ZH[0]) + ZH[1] + ZG[0];
    ZE[2] = ZB[3] + 0x81c2c92eU + ZC[2] + ZCh(ZF[0], ZB[0], ZF[3]) + ZR26(ZF[0]);
    ZF[2] = ZC[3] + ZE[2];
    ZG[2] = ZE[2] + ZR30(ZB[2]) + ZMa(ZC[0], ZG[3], ZB[2]);
    ZA[3] = ZR15(ZD[0]) + ZR25(ZA[1]) + ZA[2] + ZH[0];
    ZB[3] = ZF[3] + 0x92722c85U + ZA[3] + ZCh(ZF[2], ZF[0], ZB[0]) + ZR26(ZF[2]);
    ZC[3] = ZG[3] + ZB[3];
    ZE[3] = ZB[3] + ZR30(ZG[2]) + ZMa(ZB[2], ZC[0], ZG[2]);
    ZF[3] = ZR15(ZC[2]) + ZR25(ZB[1]) + ZD[2] + ZA[1];
    ZG[3] = ZB[0] + 0xa2bfe8a1U + ZF[3] + ZCh(ZC[3], ZF[2], ZF[0]) + ZR26(ZC[3]);
    ZA[0] = ZC[0] + ZG[3];
    ZB[0] = ZG[3] + ZR30(ZE[3]) + ZMa(ZG[2], ZB[2], ZE[3]);
    ZC[0] = ZR15(ZA[3]) + ZR25(ZC[1]) + ZH[2] + ZB[1];
    ZE[0] = ZF[0] + 0xa81a664bU + ZC[0] + ZCh(ZA[0], ZC[3], ZF[2]) + ZR26(ZA[0]);
    ZF[0] = ZB[2] + ZE[0];
    ZG[0] = ZE[0] + ZR30(ZB[0]) + ZMa(ZE[3], ZG[2], ZB[0]);
    ZH[0] = ZR15(ZF[3]) + ZR25(ZD[1]) + ZD[3] + ZC[1];
    ZA[1] = ZF[2] + 0xc24b8b70U + ZH[0] + ZCh(ZF[0], ZA[0], ZC[3]) + ZR26(ZF[0]);
    ZB[1] = ZG[2] + ZA[1];
    ZC[1] = ZA[1] + ZR30(ZG[0]) + ZMa(ZB[0], ZE[3], ZG[0]);
    ZB[2] = ZR15(ZC[0]) + ZR25(ZE[1]) + ZH[3] + ZD[1];
    ZE[2] = ZC[3] + 0xc76c51a3U + ZB[2] + ZCh(ZB[1], ZF[0], ZA[0]) + ZR26(ZB[1]);
    ZF[2] = ZE[3] + ZE[2];
    ZG[2] = ZE[2] + ZR30(ZC[1]) + ZMa(ZG[0], ZB[0], ZC[1]);
    ZB[3] = ZR15(ZH[0]) + ZR25(ZF[1]) + ZD[0] + ZE[1];
    ZC[3] = ZA[0] + 0xd192e819U + ZB[3] + ZCh(ZF[2], ZB[1], ZF[0]) + ZR26(ZF[2]);
    ZE[3] = ZB[0] + ZC[3];
    ZG[3] = ZC[3] + ZR30(ZG[2]) + ZMa(ZC[1], ZG[0], ZG[2]);
    ZA[0] = ZR15(ZB[2]) + ZR25(ZG[1]) + ZC[2] + ZF[1];
    ZB[0] = ZF[0] + 0xd6990624U + ZA[0] + ZCh(ZE[3], ZF[2], ZB[1]) + ZR26(ZE[3]);
    ZE[0] = ZG[0] + ZB[0];
    ZF[0] = ZB[0] + ZR30(ZG[3]) + ZMa(ZG[2], ZC[1], ZG[3]);
    ZG[0] = ZR15(ZB[3]) + ZR25(ZH[1]) + ZA[3] + ZG[1];
    ZA[1] = ZB[1] + 0xf40e3585U + ZG[0] + ZCh(ZE[0], ZE[3], ZF[2]) + ZR26(ZE[0]);
    ZB[1] = ZC[1] + ZA[1];
    ZC[1] = ZA[1] + ZR30(ZF[0]) + ZMa(ZG[3], ZG[2], ZF[0]);
    ZD[1] = ZR15(ZA[0]) + ZF[3] + ZR25(ZA[2]) + ZH[1];
    ZE[1] = ZF[2] + 0x106aa070U + ZD[1] + ZCh(ZB[1], ZE[0], ZE[3]) + ZR26(ZB[1]);
    ZF[1] = ZG[2] + ZE[1];
    ZG[1] = ZE[1] + ZR30(ZC[1]) + ZMa(ZF[0], ZG[3], ZC[1]);
    ZH[1] = ZR15(ZG[0]) + ZC[0] + ZR25(ZD[2]) + ZA[2];
    ZA[2] = ZE[3] + 0x19a4c116U + ZH[1] + ZCh(ZF[1], ZB[1], ZE[0]) + ZR26(ZF[1]);
    ZE[2] = ZG[3] + ZA[2];
    ZF[2] = ZA[2] + ZR30(ZG[1]) + ZMa(ZC[1], ZF[0], ZG[1]);
    ZG[2] = ZR15(ZD[1]) + ZH[0] + ZR25(ZH[2]) + ZD[2];
    ZC[3] = ZE[0] + 0x1e376c08U + ZG[2] + ZCh(ZE[2], ZF[1], ZB[1]) + ZR26(ZE[2]);
    ZE[3] = ZF[0] + ZC[3];
    ZG[3] = ZC[3] + ZR30(ZF[2]) + ZMa(ZG[1], ZC[1], ZF[2]);
    ZB[0] = ZR15(ZH[1]) + ZB[2] + ZR25(ZD[3]) + ZH[2];
    ZE[0] = ZB[1] + 0x2748774cU + ZB[0] + ZCh(ZE[3], ZE[2], ZF[1]) + ZR26(ZE[3]);
    ZF[0] = ZC[1] + ZE[0];
    ZA[1] = ZE[0] + ZR30(ZG[3]) + ZMa(ZF[2], ZG[1], ZG[3]);
    ZB[1] = ZR15(ZG[2]) + ZB[3] + ZR25(ZH[3]) + ZD[3];
    ZC[1] = ZF[1] + 0x34b0bcb5U + ZB[1] + ZCh(ZF[0], ZE[3], ZE[2]) + ZR26(ZF[0]);
    ZE[1] = ZG[1] + ZC[1];
    ZF[1] = ZC[1] + ZR30(ZA[1]) + ZMa(ZG[3], ZF[2], ZA[1]);
    ZG[1] = ZR15(ZB[0]) + ZA[0] + ZR25(ZD[0]) + ZH[3];
    ZA[2] = ZE[2] + 0x391c0cb3U + ZG[1] + ZCh(ZE[1], ZF[0], ZE[3]) + ZR26(ZE[1]);
    ZD[2] = ZF[2] + ZA[2];
    ZE[2] = ZA[2] + ZR30(ZF[1]) + ZMa(ZA[1], ZG[3], ZF[1]);
    ZF[2] = ZR15(ZB[1]) + ZG[0] + ZR25(ZC[2]) + ZD[0];
    ZH[2] = ZE[3] + 0x4ed8aa4aU + ZF[2] + ZCh(ZD[2], ZE[1], ZF[0]) + ZR26(ZD[2]);
    ZC[3] = ZG[3] + ZH[2];
    ZD[3] = ZH[2] + ZR30(ZE[2]) + ZMa(ZF[1], ZA[1], ZE[2]);
    ZE[3] = ZR15(ZG[1]) + ZD[1] + ZR25(ZA[3]) + ZC[2];
    ZG[3] = ZF[0] + 0x5b9cca4fU + ZE[3] + ZCh(ZC[3], ZD[2], ZE[1]) + ZR26(ZC[3]);
    ZH[3] = ZA[1] + ZG[3];
    ZD[0] = ZG[3] + ZR30(ZD[3]) + ZMa(ZE[2], ZF[1], ZD[3]);
    ZE[0] = ZR15(ZF[2]) + ZH[1] + ZR25(ZF[3]) + ZA[3];
    ZF[0] = ZE[1] + 0x682e6ff3U + ZE[0] + ZCh(ZH[3], ZC[3], ZD[2]) + ZR26(ZH[3]);
    ZA[1] = ZF[1] + ZF[0];
    ZC[1] = ZF[0] + ZR30(ZD[0]) + ZMa(ZD[3], ZE[2], ZD[0]);
    ZE[1] = ZR15(ZE[3]) + ZG[2] + ZR25(ZC[0]) + ZF[3];
    ZF[1] = ZD[2] + 0x748f82eeU + ZE[1] + ZCh(ZA[1], ZH[3], ZC[3]) + ZR26(ZA[1]);
    ZA[2] = ZE[2] + ZF[1];
    ZC[2] = ZF[1] + ZR30(ZC[1]) + ZMa(ZD[0], ZD[3], ZC[1]);
    ZD[2] = ZR15(ZE[0]) + ZB[0] + ZR25(ZH[0]) + ZC[0];
    ZE[2] = ZC[3] + 0x78a5636fU + ZD[2] + ZCh(ZA[2], ZA[1], ZH[3]) + ZR26(ZA[2]);
    ZG[2] = ZD[3] + ZE[2];
    ZH[2] = ZE[2] + ZR30(ZC[2]) + ZMa(ZC[1], ZD[0], ZC[2]);
    ZA[3] = ZR15(ZE[1]) + ZB[1] + ZR25(ZB[2]) + ZH[0];
    ZC[3] = ZH[3] + 0x84c87814U + ZA[3] + ZCh(ZG[2], ZA[2], ZA[1]) + ZR26(ZG[2]);
    ZD[3] = ZD[0] + ZC[3];
    ZF[3] = ZC[3] + ZR30(ZH[2]) + ZMa(ZC[2], ZC[1], ZH[2]);
    ZG[3] = ZR15(ZD[2]) + ZG[1] + ZR25(ZB[3]) + ZB[2];
    ZH[3] = ZA[1] + 0x8cc70208U + ZG[3] + ZCh(ZD[3], ZG[2], ZA[2]) + ZR26(ZD[3]);
    ZB[0] = ZC[1] + ZH[3];
    ZC[0] = ZH[3] + ZR30(ZF[3]) + ZMa(ZH[2], ZC[2], ZF[3]);
    ZD[0] = ZR15(ZA[3]) + ZF[2] + ZR25(ZA[0]) + ZB[3];
    ZF[0] = ZA[2] + 0x90befffaU + ZD[0] + ZCh(ZB[0], ZD[3], ZG[2]) + ZR26(ZB[0]);
    ZH[0] = ZC[2] + ZF[0];
    ZA[1] = ZF[0] + ZR30(ZC[0]) + ZMa(ZF[3], ZH[2], ZC[0]);
    ZB[1] = ZR15(ZG[3]) + ZE[3] + ZR25(ZG[0]) + ZA[0];
    ZC[1] = ZG[2] + 0xa4506cebU + ZB[1] + ZCh(ZH[0], ZB[0], ZD[3]) + ZR26(ZH[0]);
    ZF[1] = ZH[2] + ZC[1];
    ZG[1] = ZC[1] + ZR30(ZA[1]) + ZMa(ZC[0], ZF[3], ZA[1]);
    ZA[2] = ZR15(ZD[0]) + ZR25(ZD[1]) + ZE[0] + ZG[0];
    ZB[2] = ZD[3] + 0xbef9a3f7U + ZA[2] + ZCh(ZF[1], ZH[0], ZB[0]) + ZR26(ZF[1]);
    ZC[2] = ZF[3] + ZB[2];
    ZD[2] = ZB[2] + ZR30(ZG[1]) + ZMa(ZA[1], ZC[0], ZG[1]);
    ZE[2] = ZR15(ZB[1]) + ZR25(ZH[1]) + ZE[1] + ZD[1];
    ZF[2] = ZB[0] + 0xc67178f2U + ZE[2] + ZCh(ZC[2], ZF[1], ZH[0]) + ZR26(ZC[2]);
    ZG[2] = ZC[0] + ZF[2];
    ZH[2] = ZF[2] + ZR30(ZD[2]) + ZMa(ZG[1], ZA[1], ZD[2]);

    ZA[3] = state0 + ZH[2];
    ZB[3] = state1 + ZD[2];
    ZC[3] = state2 + ZG[1];
    ZD[3] = state3 + ZA[1];
    ZE[3] = state4 + ZG[2];
    ZF[3] = state5 + ZC[2];
    ZG[3] = state6 + ZF[1];
    ZH[3] = state7 + ZH[0];

    ZD[0] = 0x98c7e2a2U + ZA[3];
    ZH[0] = 0xfc08884dU + ZA[3];

    ZA[1] = ZR25(ZB[3]) + ZA[3];

    ZB[1] = 0x90bb1e3cU + ZB[3] + ZCh(ZD[0], 0x510e527fU, 0x9b05688cU) + ZR26(ZD[0]);
    ZC[1] = 0x3c6ef372U + ZB[1];
    ZD[1] = ZB[1] + ZR30(ZH[0]) + ZMa(0x6a09e667U, 0xbb67ae85U, ZH[0]);
    ZE[1] = 0x50c6645bU + ZC[3] + ZCh(ZC[1], ZD[0], 0x510e527fU) + ZR26(ZC[1]);
    ZF[1] = 0xbb67ae85U + ZE[1];
    ZG[1] = ZE[1] + ZR30(ZD[1]) + ZMa(ZH[0], 0x6a09e667U, ZD[1]);
    ZH[1] = 0x00a00000U + ZR25(ZC[3]) + ZB[3];
    ZA[2] = ZR15(ZA[1]) + ZR25(ZD[3]) + ZC[3];
    ZB[2] = 0x3ac42e24U + ZD[3] + ZCh(ZF[1], ZC[1], ZD[0]) + ZR26(ZF[1]);
    ZC[2] = 0x6a09e667U + ZB[2];
    ZD[2] = ZB[2] + ZR30(ZG[1]) + ZMa(ZD[1], ZH[0], ZG[1]);
    ZE[2] = ZR15(ZH[1]) + ZR25(ZE[3]) + ZD[3];
    ZF[2] = ZA[3] + 0xd21ea4fdU + ZE[3] + ZCh(ZC[2], ZF[1], ZC[1]) + ZR26(ZC[2]);
    ZG[2] = ZH[0] + ZF[2];
    ZH[2] = ZF[2] + ZR30(ZD[2]) + ZMa(ZG[1], ZD[1], ZD[2]);
    ZA[3] = ZR15(ZA[2]) + ZR25(ZF[3]) + ZE[3];
    ZB[3] = ZC[1] + 0x59f111f1U + ZF[3] + ZCh(ZG[2], ZC[2], ZF[1]) + ZR26(ZG[2]);
    ZC[3] = ZD[1] + ZB[3];
    ZD[3] = ZB[3] + ZR30(ZH[2]) + ZMa(ZD[2], ZG[1], ZH[2]);
    ZE[3] = ZR15(ZE[2]) + ZR25(ZG[3]) + ZF[3];
    ZF[3] = ZF[1] + 0x923f82a4U + ZG[3] + ZCh(ZC[3], ZG[2], ZC[2]) + ZR26(ZC[3]);

    ZA[0] = ZG[1] + ZF[3];
    ZB[0] = ZF[3] + ZR30(ZD[3]) + ZMa(ZH[2], ZD[2], ZD[3]);
    ZC[0] = ZR15(ZA[3]) + 0x00000100U + ZR25(ZH[3]) + ZG[3];
    ZD[0] = ZC[2] + 0xab1c5ed5U + ZH[3] + ZCh(ZA[0], ZC[3], ZG[2]) + ZR26(ZA[0]);
    ZE[0] = ZD[2] + ZD[0];
    ZF[0] = ZD[0] + ZR30(ZB[0]) + ZMa(ZD[3], ZH[2], ZB[0]);
    ZG[0] = ZG[2] + 0x5807aa98U + ZCh(ZE[0], ZA[0], ZC[3]) + ZR26(ZE[0]);
    ZH[0] = ZH[2] + ZG[0];
    ZB[1] = ZG[0] + ZR30(ZF[0]) + ZMa(ZB[0], ZD[3], ZF[0]);
    ZC[1] = ZR15(ZE[3]) + ZA[1] + 0x11002000U + ZH[3];
    ZD[1] = ZR15(ZC[0]) + ZH[1] + 0x80000000U;
    ZE[1] = ZC[3] + 0x12835b01U + ZCh(ZH[0], ZE[0], ZA[0]) + ZR26(ZH[0]);
    ZF[1] = ZD[3] + ZE[1];
    ZG[1] = ZE[1] + ZR30(ZB[1]) + ZMa(ZF[0], ZB[0], ZB[1]);
    ZB[2] = ZA[0] + 0x243185beU + ZCh(ZF[1], ZH[0], ZE[0]) + ZR26(ZF[1]);
    ZC[2] = ZB[0] + ZB[2];
    ZD[2] = ZB[2] + ZR30(ZG[1]) + ZMa(ZB[1], ZF[0], ZG[1]);
    ZF[2] = ZR15(ZC[1]) + ZA[2];
    ZG[2] = ZR15(ZD[1]) + ZE[2];
    ZH[2] = ZE[0] + 0x550c7dc3U + ZCh(ZC[2], ZF[1], ZH[0]) + ZR26(ZC[2]);
    ZB[3] = ZF[0] + ZH[2];
    ZC[3] = ZH[2] + ZR30(ZD[2]) + ZMa(ZG[1], ZB[1], ZD[2]);
    ZD[3] = ZH[0] + 0x72be5d74U + ZCh(ZB[3], ZC[2], ZF[1]) + ZR26(ZB[3]);
    ZF[3] = ZB[1] + ZD[3];
    ZG[3] = ZD[3] + ZR30(ZC[3]) + ZMa(ZD[2], ZG[1], ZC[3]);
    ZH[3] = ZR15(ZF[2]) + ZA[3];
    ZA[0] = ZR15(ZG[2]) + ZE[3];
    ZB[0] = ZF[1] + 0x80deb1feU + ZCh(ZF[3], ZB[3], ZC[2]) + ZR26(ZF[3]);
    ZD[0] = ZG[1] + ZB[0];
    ZE[0] = ZB[0] + ZR30(ZG[3]) + ZMa(ZC[3], ZD[2], ZG[3]);
    ZF[0] = ZC[2] + 0x9bdc06a7U + ZCh(ZD[0], ZF[3], ZB[3]) + ZR26(ZD[0]);
    ZG[0] = ZD[2] + ZF[0];
    ZH[0] = ZF[0] + ZR30(ZE[0]) + ZMa(ZG[3], ZC[3], ZE[0]);
    ZB[1] = ZB[3] + 0xc19bf274 + ZCh(ZG[0], ZD[0], ZF[3]) + ZR26(ZG[0]);
    ZE[1] = ZC[3] + ZB[1];
    ZF[1] = ZB[1] + ZR30(ZH[0]) + ZMa(ZE[0], ZG[3], ZH[0]);
    ZG[1] = ZF[3] + 0xe49b69c1U + ZA[1] + ZCh(ZE[1], ZG[0], ZD[0]) + ZR26(ZE[1]);
    ZB[2] = ZG[3] + ZG[1];
    ZC[2] = ZG[1] + ZR30(ZF[1]) + ZMa(ZH[0], ZE[0], ZF[1]);
    ZD[2] = ZD[0] + 0xefbe4786U + ZH[1] + ZCh(ZB[2], ZE[1], ZG[0]) + ZR26(ZB[2]);
    ZH[2] = ZE[0] + ZD[2];
    ZB[3] = ZD[2] + ZR30(ZC[2]) + ZMa(ZF[1], ZH[0], ZC[2]);
    ZC[3] = ZG[0] + 0x0fc19dc6U + ZA[2] + ZCh(ZH[2], ZB[2], ZE[1]) + ZR26(ZH[2]);
    ZD[3] = ZH[0] + ZC[3];
    ZF[3] = ZC[3] + ZR30(ZB[3]) + ZMa(ZC[2], ZF[1], ZB[3]);
    ZG[3] = ZE[1] + 0x240ca1ccU + ZE[2] + ZCh(ZD[3], ZH[2], ZB[2]) + ZR26(ZD[3]);
    ZB[0] = ZF[1] + ZG[3];
    ZD[0] = ZG[3] + ZR30(ZF[3]) + ZMa(ZB[3], ZC[2], ZF[3]);
    ZE[0] = ZB[2] + 0x2de92c6fU + ZA[3] + ZCh(ZB[0], ZD[3], ZH[2]) + ZR26(ZB[0]);
    ZF[0] = ZC[2] + ZE[0];
    ZG[0] = ZE[0] + ZR30(ZD[0]) + ZMa(ZF[3], ZB[3], ZD[0]);
    ZH[0] = ZH[2] + 0x4a7484aaU + ZE[3] + ZCh(ZF[0], ZB[0], ZD[3]) + ZR26(ZF[0]);
    ZB[1] = ZB[3] + ZH[0];
    ZE[1] = ZH[0] + ZR30(ZG[0]) + ZMa(ZD[0], ZF[3], ZG[0]);
    ZF[1] = ZD[3] + 0x5cb0a9dcU + ZC[0] + ZCh(ZB[1], ZF[0], ZB[0]) + ZR26(ZB[1]);
    ZG[1] = ZF[3] + ZF[1];
    ZB[2] = ZF[1] + ZR30(ZE[1]) + ZMa(ZG[0], ZD[0], ZE[1]);
    ZC[2] = ZB[0] + 0x76f988daU + ZC[1] + ZCh(ZG[1], ZB[1], ZF[0]) + ZR26(ZG[1]);
    ZD[2] = ZD[0] + ZC[2];
    ZH[2] = ZC[2] + ZR30(ZB[2]) + ZMa(ZE[1], ZG[0], ZB[2]);
    ZB[3] = ZF[0] + 0x983e5152U + ZD[1] + ZCh(ZD[2], ZG[1], ZB[1]) + ZR26(ZD[2]);
    ZC[3] = ZG[0] + ZB[3];
    ZD[3] = ZB[3] + ZR30(ZH[2]) + ZMa(ZB[2], ZE[1], ZH[2]);
    ZF[3] = ZB[1] + 0xa831c66dU + ZF[2] + ZCh(ZC[3], ZD[2], ZG[1]) + ZR26(ZC[3]);
    ZG[3] = ZE[1] + ZF[3];
    ZB[0] = ZF[3] + ZR30(ZD[3]) + ZMa(ZH[2], ZB[2], ZD[3]);
    ZD[0] = ZG[1] + 0xb00327c8U + ZG[2] + ZCh(ZG[3], ZC[3], ZD[2]) + ZR26(ZG[3]);
    ZE[0] = ZB[2] + ZD[0];
    ZF[0] = ZD[0] + ZR30(ZB[0]) + ZMa(ZD[3], ZH[2], ZB[0]);
    ZG[0] = ZD[2] + 0xbf597fc7U + ZH[3] + ZCh(ZE[0], ZG[3], ZC[3]) + ZR26(ZE[0]);
    ZH[0] = ZH[2] + ZG[0];
    ZB[1] = ZG[0] + ZR30(ZF[0]) + ZMa(ZB[0], ZD[3], ZF[0]);
    ZE[1] = ZC[3] + 0xc6e00bf3U + ZA[0] + ZCh(ZH[0], ZE[0], ZG[3]) + ZR26(ZH[0]);
    ZF[1] = ZD[3] + ZE[1];
    ZG[1] = ZE[1] + ZR30(ZB[1]) + ZMa(ZF[0], ZB[0], ZB[1]);
    ZB[2] = ZR15(ZH[3]) + ZC[0];
    ZC[2] = ZG[3] + 0xd5a79147U + ZB[2] + ZCh(ZF[1], ZH[0], ZE[0]) + ZR26(ZF[1]);
    ZD[2] = ZB[0] + ZC[2];
    ZH[2] = ZC[2] + ZR30(ZG[1]) + ZMa(ZB[1], ZF[0], ZG[1]);
    ZB[3] = ZR15(ZA[0]) + 0x00400022U + ZC[1];
    ZC[3] = ZE[0] + 0x06ca6351U + ZB[3] + ZCh(ZD[2], ZF[1], ZH[0]) + ZR26(ZD[2]);
    ZD[3] = ZF[0] + ZC[3];
    ZF[3] = ZC[3] + ZR30(ZH[2]) + ZMa(ZG[1], ZB[1], ZH[2]);
    ZG[3] = ZR15(ZB[2]) + ZR25(ZA[1]) + ZD[1] + 0x00000100U;
    ZB[0] = ZH[0] + 0x14292967U + ZG[3] + ZCh(ZD[3], ZD[2], ZF[1]) + ZR26(ZD[3]);
    ZD[0] = ZB[1] + ZB[0];
    ZE[0] = ZB[0] + ZR30(ZF[3]) + ZMa(ZH[2], ZG[1], ZF[3]);
    ZF[0] = ZR15(ZB[3]) + ZR25(ZH[1]) + ZF[2] + ZA[1];
    ZG[0] = ZF[1] + 0x27b70a85U + ZF[0] + ZCh(ZD[0], ZD[3], ZD[2]) + ZR26(ZD[0]);
    ZH[0] = ZG[1] + ZG[0];
    ZA[1] = ZG[0] + ZR30(ZE[0]) + ZMa(ZF[3], ZH[2], ZE[0]);
    ZB[1] = ZR15(ZG[3]) + ZR25(ZA[2]) + ZG[2] + ZH[1];
    ZE[1] = ZD[2] + 0x2e1b2138U + ZB[1] + ZCh(ZH[0], ZD[0], ZD[3]) + ZR26(ZH[0]);
    ZF[1] = ZH[2] + ZE[1];
    ZG[1] = ZE[1] + ZR30(ZA[1]) + ZMa(ZE[0], ZF[3], ZA[1]);
    ZH[1] = ZR15(ZF[0]) + ZR25(ZE[2]) + ZH[3] + ZA[2];
    ZA[2] = ZD[3] + 0x4d2c6dfcU + ZH[1] + ZCh(ZF[1], ZH[0], ZD[0]) + ZR26(ZF[1]);
    ZC[2] = ZF[3] + ZA[2];
    ZD[2] = ZA[2] + ZR30(ZG[1]) + ZMa(ZA[1], ZE[0], ZG[1]);
    ZH[2] = ZR15(ZB[1]) + ZR25(ZA[3]) + ZA[0] + ZE[2];
    ZC[3] = ZD[0] + 0x53380d13U + ZH[2] + ZCh(ZC[2], ZF[1], ZH[0]) + ZR26(ZC[2]);
    ZD[3] = ZE[0] + ZC[3];
    ZF[3] = ZC[3] + ZR30(ZD[2]) + ZMa(ZG[1], ZA[1], ZD[2]);
    ZB[0] = ZR15(ZH[1]) + ZR25(ZE[3]) + ZB[2] + ZA[3];
    ZD[0] = ZH[0] + 0x650a7354U + ZB[0] + ZCh(ZD[3], ZC[2], ZF[1]) + ZR26(ZD[3]);
    ZE[0] = ZA[1] + ZD[0];
    ZG[0] = ZD[0] + ZR30(ZF[3]) + ZMa(ZD[2], ZG[1], ZF[3]);
    ZH[0] = ZR15(ZH[2]) + ZR25(ZC[0]) + ZB[3] + ZE[3];
    ZA[1] = ZF[1] + 0x766a0abbU + ZH[0] + ZCh(ZE[0], ZD[3], ZC[2]) + ZR26(ZE[0]);
    ZE[1] = ZG[1] + ZA[1];
    ZF[1] = ZA[1] + ZR30(ZG[0]) + ZMa(ZF[3], ZD[2], ZG[0]);
    ZG[1] = ZR15(ZB[0]) + ZR25(ZC[1]) + ZG[3] + ZC[0];
    ZA[2] = ZC[2] + 0x81c2c92eU + ZG[1] + ZCh(ZE[1], ZE[0], ZD[3]) + ZR26(ZE[1]);
    ZC[2] = ZD[2] + ZA[2];
    ZD[2] = ZA[2] + ZR30(ZF[1]) + ZMa(ZG[0], ZF[3], ZF[1]);
    ZE[2] = ZR15(ZH[0]) + ZR25(ZD[1]) + ZF[0] + ZC[1];
    ZA[3] = ZD[3] + 0x92722c85U + ZE[2] + ZCh(ZC[2], ZE[1], ZE[0]) + ZR26(ZC[2]);
    ZC[3] = ZF[3] + ZA[3];
    ZD[3] = ZA[3] + ZR30(ZD[2]) + ZMa(ZF[1], ZG[0], ZD[2]);
    ZE[3] = ZR15(ZG[1]) + ZR25(ZF[2]) + ZB[1] + ZD[1];
    ZF[3] = ZE[0] + 0xa2bfe8a1U + ZE[3] + ZCh(ZC[3], ZC[2], ZE[1]) + ZR26(ZC[3]);
    ZC[0] = ZG[0] + ZF[3];
    ZD[0] = ZF[3] + ZR30(ZD[3]) + ZMa(ZD[2], ZF[1], ZD[3]);
    ZE[0] = ZR15(ZE[2]) + ZR25(ZG[2]) + ZH[1] + ZF[2];
    ZG[0] = ZE[1] + 0xa81a664bU + ZE[0] + ZCh(ZC[0], ZC[3], ZC[2]) + ZR26(ZC[0]);
    ZA[1] = ZF[1] + ZG[0];
    ZC[1] = ZG[0] + ZR30(ZD[0]) + ZMa(ZD[3], ZD[2], ZD[0]);
    ZD[1] = ZR15(ZE[3]) + ZR25(ZH[3]) + ZH[2] + ZG[2];
    ZE[1] = ZC[2] + 0xc24b8b70U + ZD[1] + ZCh(ZA[1], ZC[0], ZC[3]) + ZR26(ZA[1]);
    ZF[1] = ZD[2] + ZE[1];
    ZA[2] = ZE[1] + ZR30(ZC[1]) + ZMa(ZD[0], ZD[3], ZC[1]);
    ZC[2] = ZR15(ZE[0]) + ZR25(ZA[0]) + ZB[0] + ZH[3];
    ZD[2] = ZC[3] + 0xc76c51a3U + ZC[2] + ZCh(ZF[1], ZA[1], ZC[0]) + ZR26(ZF[1]);
    ZF[2] = ZD[3] + ZD[2];
    ZG[2] = ZD[2] + ZR30(ZA[2]) + ZMa(ZC[1], ZD[0], ZA[2]);
    ZA[3] = ZR15(ZD[1]) + ZR25(ZB[2]) + ZH[0] + ZA[0];
    ZC[3] = ZC[0] + 0xd192e819U + ZA[3] + ZCh(ZF[2], ZF[1], ZA[1]) + ZR26(ZF[2]);
    ZD[3] = ZD[0] + ZC[3];
    ZF[3] = ZC[3] + ZR30(ZG[2]) + ZMa(ZA[2], ZC[1], ZG[2]);
    ZH[3] = ZR15(ZC[2]) + ZR25(ZB[3]) + ZG[1] + ZB[2];
    ZA[0] = ZA[1] + 0xd6990624U + ZH[3] + ZCh(ZD[3], ZF[2], ZF[1]) + ZR26(ZD[3]);
    ZC[0] = ZC[1] + ZA[0];
    ZD[0] = ZA[0] + ZR30(ZF[3]) + ZMa(ZG[2], ZA[2], ZF[3]);
    ZG[0] = ZR15(ZA[3]) + ZR25(ZG[3]) + ZE[2] + ZB[3];
    ZA[1] = ZF[1] + 0xf40e3585U + ZG[0] + ZCh(ZC[0], ZD[3], ZF[2]) + ZR26(ZC[0]);
    ZC[1] = ZA[2] + ZA[1];
    ZE[1] = ZA[1] + ZR30(ZD[0]) + ZMa(ZF[3], ZG[2], ZD[0]);
    ZF[1] = ZR15(ZH[3]) + ZR25(ZF[0]) + ZE[3] + ZG[3];
    ZA[2] = ZF[2] + 0x106aa070U + ZF[1] + ZCh(ZC[1], ZC[0], ZD[3]) + ZR26(ZC[1]);
    ZB[2] = ZG[2] + ZA[2];
    ZD[2] = ZA[2] + ZR30(ZE[1]) + ZMa(ZD[0], ZF[3], ZE[1]);
    ZF[2] = ZR15(ZG[0]) + ZR25(ZB[1]) + ZE[0] + ZF[0];
    ZG[2] = ZD[3] + 0x19a4c116U + ZF[2] + ZCh(ZB[2], ZC[1], ZC[0]) + ZR26(ZB[2]);
    ZB[3] = ZF[3] + ZG[2];
    ZC[3] = ZG[2] + ZR30(ZD[2]) + ZMa(ZE[1], ZD[0], ZD[2]);
    ZD[3] = ZR15(ZF[1]) + ZR25(ZH[1]) + ZD[1] + ZB[1];
    ZF[3] = ZC[0] + 0x1e376c08U + ZD[3] + ZCh(ZB[3], ZB[2], ZC[1]) + ZR26(ZB[3]);
    ZG[3] = ZD[0] + ZF[3];
    ZA[0] = ZF[3] + ZR30(ZC[3]) + ZMa(ZD[2], ZE[1], ZC[3]);
    ZC[0] = ZR15(ZF[2]) + ZC[2] + ZR25(ZH[2]) + ZH[1];
    ZD[0] = ZC[1] + 0x2748774cU + ZC[0] + ZCh(ZG[3], ZB[3], ZB[2]) + ZR26(ZG[3]);
    ZF[0] = ZE[1] + ZD[0];
    ZA[1] = ZD[0] + ZR30(ZA[0]) + ZMa(ZC[3], ZD[2], ZA[0]);
    ZB[1] = ZR15(ZD[3]) + ZA[3] + ZR25(ZB[0]) + ZH[2];
    ZC[1] = ZB[2] + 0x34b0bcb5U + ZB[1] + ZCh(ZF[0], ZG[3], ZB[3]) + ZR26(ZF[0]);
    ZE[1] = ZD[2] + ZC[1];
    ZH[1] = ZC[1] + ZR30(ZA[1]) + ZMa(ZA[0], ZC[3], ZA[1]);
    ZA[2] = ZR15(ZC[0]) + ZH[3] + ZR25(ZH[0]) + ZB[0];
    ZB[2] = ZB[3] + 0x391c0cb3U + ZA[2] + ZCh(ZE[1], ZF[0], ZG[3]) + ZR26(ZE[1]);
    ZD[2] = ZC[3] + ZB[2];
    ZG[2] = ZB[2] + ZR30(ZH[1]) + ZMa(ZA[1], ZA[0], ZH[1]);
    ZH[2] = ZR15(ZB[1]) + ZG[0] + ZR25(ZG[1]) + ZH[0];
    ZB[3] = ZG[3] + 0x4ed8aa4aU + ZH[2] + ZCh(ZD[2], ZE[1], ZF[0]) + ZR26(ZD[2]);
    ZC[3] = ZA[0] + ZB[3];
    ZF[3] = ZB[3] + ZR30(ZG[2]) + ZMa(ZH[1], ZA[1], ZG[2]);
    ZG[3] = ZR15(ZA[2]) + ZF[1] + ZR25(ZE[2]) + ZG[1];
    ZA[0] = ZF[0] + 0x5b9cca4fU + ZG[3] + ZCh(ZC[3], ZD[2], ZE[1]) + ZR26(ZC[3]);
    ZB[0] = ZA[1] + ZA[0];
    ZD[0] = ZA[0] + ZR30(ZF[3]) + ZMa(ZG[2], ZH[1], ZF[3]);
    ZF[0] = ZR15(ZH[2]) + ZF[2] + ZR25(ZE[3]) + ZE[2];
    ZG[0] = ZE[1] + 0x682e6ff3U + ZF[0] + ZCh(ZB[0], ZC[3], ZD[2]) + ZR26(ZB[0]);
    ZH[0] = ZH[1] + ZG[0];
    ZA[1] = ZG[0] + ZR30(ZD[0]) + ZMa(ZF[3], ZG[2], ZD[0]);
    ZC[1] = ZR15(ZG[3]) + ZR25(ZE[0]) + ZD[3] + ZE[3];
    ZE[1] = ZD[2] + 0x748f82eeU + ZC[1] + ZCh(ZH[0], ZB[0], ZC[3]) + ZR26(ZH[0]);
    ZF[1] = ZG[2] + ZE[1];
    ZG[1] = ZE[1] + ZR30(ZA[1]) + ZMa(ZD[0], ZF[3], ZA[1]);
    ZH[1] = ZR15(ZF[0]) + ZR25(ZD[1]) + ZC[0] + ZE[0];
    ZB[2] = ZF[3] + ZC[3] + 0x78a5636fU + ZH[1] + ZCh(ZF[1], ZH[0], ZB[0]) + ZR26(ZF[1]);
    ZD[2] = ZR15(ZC[1]) + ZR25(ZC[2]) + ZB[1] + ZD[1];
    ZE[2] = ZD[0] + ZB[0] + 0x84c87814U + ZD[2] + ZCh(ZB[2], ZF[1], ZH[0]) + ZR26(ZB[2]);
    ZF[2] = ZA[1] + ZH[0] + 0x8cc70208U + ZR15(ZH[1]) + ZR25(ZA[3]) + ZA[2] + ZC[2] + ZCh(ZE[2], ZB[2], ZF[1]) + ZR26(ZE[2]);
    ZG[2] = ZG[1] + ZF[1] + ZR26(ZF[2]) + ZCh(ZF[2], ZE[2], ZB[2]) + ZR15(ZD[2]) + ZH[2] + ZR25(ZH[3]) + ZA[3];

#define FOUND (0x80)
#define NFLAG (0x7F)

#if defined(VECTORS4)
	ZG[2] ^= 0x136032EDU;
	bool result = ZG[2].x & ZG[2].y & ZG[2].z & ZG[2].w;
	if (!result) {
		if (!ZG[2].x)
			output[FOUND] = output[NFLAG & Znonce.x] =  Znonce.x;
		if (!ZG[2].y)
			output[FOUND] = output[NFLAG & Znonce.y] =  Znonce.y;
		if (!ZG[2].z)
			output[FOUND] = output[NFLAG & Znonce.z] =  Znonce.z;
		if (!ZG[2].w)
			output[FOUND] = output[NFLAG & Znonce.w] =  Znonce.w;
	}
#elif defined(VECTORS2)
	ZG[2] ^= 0x136032EDU;
	bool result = ZG[2].x & ZG[2].y;
	if (!result) {
		if (!ZG[2].x)
			output[FOUND] = output[NFLAG & Znonce.x] =  Znonce.x;
		if (!ZG[2].y)
			output[FOUND] = output[NFLAG & Znonce.y] =  Znonce.y;
	}
#else
	if (ZG[2] == 0x136032EDU)
		output[FOUND] = output[NFLAG & Znonce] =  Znonce;
#endif
}
