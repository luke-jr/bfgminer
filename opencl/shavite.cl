/* $Id: shavite.c 227 2010-06-16 17:28:38Z tp $ */
/*
 * SHAvite-3 implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2007-2010  Projet RNRT SAPHIR
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   Thomas Pornin <thomas.pornin@cryptolog.com>
 */

/*
 * As of round 2 of the SHA-3 competition, the published reference
 * implementation and test vectors are wrong, because they use
 * big-endian AES tables while the internal decoding uses little-endian.
 * The code below follows the specification. To turn it into a code
 * which follows the reference implementation (the one called "BugFix"
 * on the SHAvite-3 web site, published on Nov 23rd, 2009), comment out
 * the code below (from the '#define AES_BIG_ENDIAN...' to the definition
 * of the AES_ROUND_NOKEY macro) and replace it with the version which
 * is commented out afterwards.
 */

#define AES_BIG_ENDIAN   0
#include "aes_helper.cl"

#define AES_ROUND_NOKEY(x0, x1, x2, x3)   do { \
    sph_u32 t0 = (x0); \
    sph_u32 t1 = (x1); \
    sph_u32 t2 = (x2); \
    sph_u32 t3 = (x3); \
    AES_ROUND_NOKEY_LE(t0, t1, t2, t3, x0, x1, x2, x3); \
  } while (0)

#define KEY_EXPAND_ELT(k0, k1, k2, k3)   do { \
    sph_u32 kt; \
    AES_ROUND_NOKEY(k1, k2, k3, k0); \
    kt = (k0); \
    (k0) = (k1); \
    (k1) = (k2); \
    (k2) = (k3); \
    (k3) = kt; \
  } while (0)

/*
 * This function assumes that "msg" is aligned for 32-bit access.
 */
#define c512(msg)  do { \
  sph_u32 p0, p1, p2, p3, p4, p5, p6, p7; \
  sph_u32 p8, p9, pA, pB, pC, pD, pE, pF; \
  sph_u32 x0, x1, x2, x3; \
  int r; \
 \
  p0 = h0; \
  p1 = h1; \
  p2 = h2; \
  p3 = h3; \
  p4 = h4; \
  p5 = h5; \
  p6 = h6; \
  p7 = h7; \
  p8 = h8; \
  p9 = h9; \
  pA = hA; \
  pB = hB; \
  pC = hC; \
  pD = hD; \
  pE = hE; \
  pF = hF; \
  /* round 0 */ \
  x0 = p4 ^ rk00; \
  x1 = p5 ^ rk01; \
  x2 = p6 ^ rk02; \
  x3 = p7 ^ rk03; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk04; \
  x1 ^= rk05; \
  x2 ^= rk06; \
  x3 ^= rk07; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk08; \
  x1 ^= rk09; \
  x2 ^= rk0A; \
  x3 ^= rk0B; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk0C; \
  x1 ^= rk0D; \
  x2 ^= rk0E; \
  x3 ^= rk0F; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  x0 = pC ^ rk10; \
  x1 = pD ^ rk11; \
  x2 = pE ^ rk12; \
  x3 = pF ^ rk13; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk14; \
  x1 ^= rk15; \
  x2 ^= rk16; \
  x3 ^= rk17; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk18; \
  x1 ^= rk19; \
  x2 ^= rk1A; \
  x3 ^= rk1B; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk1C; \
  x1 ^= rk1D; \
  x2 ^= rk1E; \
  x3 ^= rk1F; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p8 ^= x0; \
  p9 ^= x1; \
  pA ^= x2; \
  pB ^= x3; \
 \
  for (r = 0; r < 3; r ++) { \
    /* round 1, 5, 9 */ \
    KEY_EXPAND_ELT(rk00, rk01, rk02, rk03); \
    rk00 ^= rk1C; \
    rk01 ^= rk1D; \
    rk02 ^= rk1E; \
    rk03 ^= rk1F; \
    if (r == 0) { \
      rk00 ^= sc_count0; \
      rk01 ^= sc_count1; \
      rk02 ^= sc_count2; \
      rk03 ^= SPH_T32(~sc_count3); \
    } \
    x0 = p0 ^ rk00; \
    x1 = p1 ^ rk01; \
    x2 = p2 ^ rk02; \
    x3 = p3 ^ rk03; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk04, rk05, rk06, rk07); \
    rk04 ^= rk00; \
    rk05 ^= rk01; \
    rk06 ^= rk02; \
    rk07 ^= rk03; \
    if (r == 1) { \
      rk04 ^= sc_count3; \
      rk05 ^= sc_count2; \
      rk06 ^= sc_count1; \
      rk07 ^= SPH_T32(~sc_count0); \
    } \
    x0 ^= rk04; \
    x1 ^= rk05; \
    x2 ^= rk06; \
    x3 ^= rk07; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk08, rk09, rk0A, rk0B); \
    rk08 ^= rk04; \
    rk09 ^= rk05; \
    rk0A ^= rk06; \
    rk0B ^= rk07; \
    x0 ^= rk08; \
    x1 ^= rk09; \
    x2 ^= rk0A; \
    x3 ^= rk0B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk0C, rk0D, rk0E, rk0F); \
    rk0C ^= rk08; \
    rk0D ^= rk09; \
    rk0E ^= rk0A; \
    rk0F ^= rk0B; \
    x0 ^= rk0C; \
    x1 ^= rk0D; \
    x2 ^= rk0E; \
    x3 ^= rk0F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    pC ^= x0; \
    pD ^= x1; \
    pE ^= x2; \
    pF ^= x3; \
    KEY_EXPAND_ELT(rk10, rk11, rk12, rk13); \
    rk10 ^= rk0C; \
    rk11 ^= rk0D; \
    rk12 ^= rk0E; \
    rk13 ^= rk0F; \
    x0 = p8 ^ rk10; \
    x1 = p9 ^ rk11; \
    x2 = pA ^ rk12; \
    x3 = pB ^ rk13; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk14, rk15, rk16, rk17); \
    rk14 ^= rk10; \
    rk15 ^= rk11; \
    rk16 ^= rk12; \
    rk17 ^= rk13; \
    x0 ^= rk14; \
    x1 ^= rk15; \
    x2 ^= rk16; \
    x3 ^= rk17; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk18, rk19, rk1A, rk1B); \
    rk18 ^= rk14; \
    rk19 ^= rk15; \
    rk1A ^= rk16; \
    rk1B ^= rk17; \
    x0 ^= rk18; \
    x1 ^= rk19; \
    x2 ^= rk1A; \
    x3 ^= rk1B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk1C, rk1D, rk1E, rk1F); \
    rk1C ^= rk18; \
    rk1D ^= rk19; \
    rk1E ^= rk1A; \
    rk1F ^= rk1B; \
    if (r == 2) { \
      rk1C ^= sc_count2; \
      rk1D ^= sc_count3; \
      rk1E ^= sc_count0; \
      rk1F ^= SPH_T32(~sc_count1); \
    } \
    x0 ^= rk1C; \
    x1 ^= rk1D; \
    x2 ^= rk1E; \
    x3 ^= rk1F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    p4 ^= x0; \
    p5 ^= x1; \
    p6 ^= x2; \
    p7 ^= x3; \
    /* round 2, 6, 10 */ \
    rk00 ^= rk19; \
    x0 = pC ^ rk00; \
    rk01 ^= rk1A; \
    x1 = pD ^ rk01; \
    rk02 ^= rk1B; \
    x2 = pE ^ rk02; \
    rk03 ^= rk1C; \
    x3 = pF ^ rk03; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk04 ^= rk1D; \
    x0 ^= rk04; \
    rk05 ^= rk1E; \
    x1 ^= rk05; \
    rk06 ^= rk1F; \
    x2 ^= rk06; \
    rk07 ^= rk00; \
    x3 ^= rk07; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk08 ^= rk01; \
    x0 ^= rk08; \
    rk09 ^= rk02; \
    x1 ^= rk09; \
    rk0A ^= rk03; \
    x2 ^= rk0A; \
    rk0B ^= rk04; \
    x3 ^= rk0B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk0C ^= rk05; \
    x0 ^= rk0C; \
    rk0D ^= rk06; \
    x1 ^= rk0D; \
    rk0E ^= rk07; \
    x2 ^= rk0E; \
    rk0F ^= rk08; \
    x3 ^= rk0F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    p8 ^= x0; \
    p9 ^= x1; \
    pA ^= x2; \
    pB ^= x3; \
    rk10 ^= rk09; \
    x0 = p4 ^ rk10; \
    rk11 ^= rk0A; \
    x1 = p5 ^ rk11; \
    rk12 ^= rk0B; \
    x2 = p6 ^ rk12; \
    rk13 ^= rk0C; \
    x3 = p7 ^ rk13; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk14 ^= rk0D; \
    x0 ^= rk14; \
    rk15 ^= rk0E; \
    x1 ^= rk15; \
    rk16 ^= rk0F; \
    x2 ^= rk16; \
    rk17 ^= rk10; \
    x3 ^= rk17; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk18 ^= rk11; \
    x0 ^= rk18; \
    rk19 ^= rk12; \
    x1 ^= rk19; \
    rk1A ^= rk13; \
    x2 ^= rk1A; \
    rk1B ^= rk14; \
    x3 ^= rk1B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk1C ^= rk15; \
    x0 ^= rk1C; \
    rk1D ^= rk16; \
    x1 ^= rk1D; \
    rk1E ^= rk17; \
    x2 ^= rk1E; \
    rk1F ^= rk18; \
    x3 ^= rk1F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    p0 ^= x0; \
    p1 ^= x1; \
    p2 ^= x2; \
    p3 ^= x3; \
    /* round 3, 7, 11 */ \
    KEY_EXPAND_ELT(rk00, rk01, rk02, rk03); \
    rk00 ^= rk1C; \
    rk01 ^= rk1D; \
    rk02 ^= rk1E; \
    rk03 ^= rk1F; \
    x0 = p8 ^ rk00; \
    x1 = p9 ^ rk01; \
    x2 = pA ^ rk02; \
    x3 = pB ^ rk03; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk04, rk05, rk06, rk07); \
    rk04 ^= rk00; \
    rk05 ^= rk01; \
    rk06 ^= rk02; \
    rk07 ^= rk03; \
    x0 ^= rk04; \
    x1 ^= rk05; \
    x2 ^= rk06; \
    x3 ^= rk07; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk08, rk09, rk0A, rk0B); \
    rk08 ^= rk04; \
    rk09 ^= rk05; \
    rk0A ^= rk06; \
    rk0B ^= rk07; \
    x0 ^= rk08; \
    x1 ^= rk09; \
    x2 ^= rk0A; \
    x3 ^= rk0B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk0C, rk0D, rk0E, rk0F); \
    rk0C ^= rk08; \
    rk0D ^= rk09; \
    rk0E ^= rk0A; \
    rk0F ^= rk0B; \
    x0 ^= rk0C; \
    x1 ^= rk0D; \
    x2 ^= rk0E; \
    x3 ^= rk0F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    p4 ^= x0; \
    p5 ^= x1; \
    p6 ^= x2; \
    p7 ^= x3; \
    KEY_EXPAND_ELT(rk10, rk11, rk12, rk13); \
    rk10 ^= rk0C; \
    rk11 ^= rk0D; \
    rk12 ^= rk0E; \
    rk13 ^= rk0F; \
    x0 = p0 ^ rk10; \
    x1 = p1 ^ rk11; \
    x2 = p2 ^ rk12; \
    x3 = p3 ^ rk13; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk14, rk15, rk16, rk17); \
    rk14 ^= rk10; \
    rk15 ^= rk11; \
    rk16 ^= rk12; \
    rk17 ^= rk13; \
    x0 ^= rk14; \
    x1 ^= rk15; \
    x2 ^= rk16; \
    x3 ^= rk17; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk18, rk19, rk1A, rk1B); \
    rk18 ^= rk14; \
    rk19 ^= rk15; \
    rk1A ^= rk16; \
    rk1B ^= rk17; \
    x0 ^= rk18; \
    x1 ^= rk19; \
    x2 ^= rk1A; \
    x3 ^= rk1B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    KEY_EXPAND_ELT(rk1C, rk1D, rk1E, rk1F); \
    rk1C ^= rk18; \
    rk1D ^= rk19; \
    rk1E ^= rk1A; \
    rk1F ^= rk1B; \
    x0 ^= rk1C; \
    x1 ^= rk1D; \
    x2 ^= rk1E; \
    x3 ^= rk1F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    pC ^= x0; \
    pD ^= x1; \
    pE ^= x2; \
    pF ^= x3; \
    /* round 4, 8, 12 */ \
    rk00 ^= rk19; \
    x0 = p4 ^ rk00; \
    rk01 ^= rk1A; \
    x1 = p5 ^ rk01; \
    rk02 ^= rk1B; \
    x2 = p6 ^ rk02; \
    rk03 ^= rk1C; \
    x3 = p7 ^ rk03; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk04 ^= rk1D; \
    x0 ^= rk04; \
    rk05 ^= rk1E; \
    x1 ^= rk05; \
    rk06 ^= rk1F; \
    x2 ^= rk06; \
    rk07 ^= rk00; \
    x3 ^= rk07; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk08 ^= rk01; \
    x0 ^= rk08; \
    rk09 ^= rk02; \
    x1 ^= rk09; \
    rk0A ^= rk03; \
    x2 ^= rk0A; \
    rk0B ^= rk04; \
    x3 ^= rk0B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk0C ^= rk05; \
    x0 ^= rk0C; \
    rk0D ^= rk06; \
    x1 ^= rk0D; \
    rk0E ^= rk07; \
    x2 ^= rk0E; \
    rk0F ^= rk08; \
    x3 ^= rk0F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    p0 ^= x0; \
    p1 ^= x1; \
    p2 ^= x2; \
    p3 ^= x3; \
    rk10 ^= rk09; \
    x0 = pC ^ rk10; \
    rk11 ^= rk0A; \
    x1 = pD ^ rk11; \
    rk12 ^= rk0B; \
    x2 = pE ^ rk12; \
    rk13 ^= rk0C; \
    x3 = pF ^ rk13; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk14 ^= rk0D; \
    x0 ^= rk14; \
    rk15 ^= rk0E; \
    x1 ^= rk15; \
    rk16 ^= rk0F; \
    x2 ^= rk16; \
    rk17 ^= rk10; \
    x3 ^= rk17; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk18 ^= rk11; \
    x0 ^= rk18; \
    rk19 ^= rk12; \
    x1 ^= rk19; \
    rk1A ^= rk13; \
    x2 ^= rk1A; \
    rk1B ^= rk14; \
    x3 ^= rk1B; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    rk1C ^= rk15; \
    x0 ^= rk1C; \
    rk1D ^= rk16; \
    x1 ^= rk1D; \
    rk1E ^= rk17; \
    x2 ^= rk1E; \
    rk1F ^= rk18; \
    x3 ^= rk1F; \
    AES_ROUND_NOKEY(x0, x1, x2, x3); \
    p8 ^= x0; \
    p9 ^= x1; \
    pA ^= x2; \
    pB ^= x3; \
  } \
  /* round 13 */ \
  KEY_EXPAND_ELT(rk00, rk01, rk02, rk03); \
  rk00 ^= rk1C; \
  rk01 ^= rk1D; \
  rk02 ^= rk1E; \
  rk03 ^= rk1F; \
  x0 = p0 ^ rk00; \
  x1 = p1 ^ rk01; \
  x2 = p2 ^ rk02; \
  x3 = p3 ^ rk03; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk04, rk05, rk06, rk07); \
  rk04 ^= rk00; \
  rk05 ^= rk01; \
  rk06 ^= rk02; \
  rk07 ^= rk03; \
  x0 ^= rk04; \
  x1 ^= rk05; \
  x2 ^= rk06; \
  x3 ^= rk07; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk08, rk09, rk0A, rk0B); \
  rk08 ^= rk04; \
  rk09 ^= rk05; \
  rk0A ^= rk06; \
  rk0B ^= rk07; \
  x0 ^= rk08; \
  x1 ^= rk09; \
  x2 ^= rk0A; \
  x3 ^= rk0B; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk0C, rk0D, rk0E, rk0F); \
  rk0C ^= rk08; \
  rk0D ^= rk09; \
  rk0E ^= rk0A; \
  rk0F ^= rk0B; \
  x0 ^= rk0C; \
  x1 ^= rk0D; \
  x2 ^= rk0E; \
  x3 ^= rk0F; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  pC ^= x0; \
  pD ^= x1; \
  pE ^= x2; \
  pF ^= x3; \
  KEY_EXPAND_ELT(rk10, rk11, rk12, rk13); \
  rk10 ^= rk0C; \
  rk11 ^= rk0D; \
  rk12 ^= rk0E; \
  rk13 ^= rk0F; \
  x0 = p8 ^ rk10; \
  x1 = p9 ^ rk11; \
  x2 = pA ^ rk12; \
  x3 = pB ^ rk13; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk14, rk15, rk16, rk17); \
  rk14 ^= rk10; \
  rk15 ^= rk11; \
  rk16 ^= rk12; \
  rk17 ^= rk13; \
  x0 ^= rk14; \
  x1 ^= rk15; \
  x2 ^= rk16; \
  x3 ^= rk17; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk18, rk19, rk1A, rk1B); \
  rk18 ^= rk14 ^ sc_count1; \
  rk19 ^= rk15 ^ sc_count0; \
  rk1A ^= rk16 ^ sc_count3; \
  rk1B ^= rk17 ^ SPH_T32(~sc_count2); \
  x0 ^= rk18; \
  x1 ^= rk19; \
  x2 ^= rk1A; \
  x3 ^= rk1B; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk1C, rk1D, rk1E, rk1F); \
  rk1C ^= rk18; \
  rk1D ^= rk19; \
  rk1E ^= rk1A; \
  rk1F ^= rk1B; \
  x0 ^= rk1C; \
  x1 ^= rk1D; \
  x2 ^= rk1E; \
  x3 ^= rk1F; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  h0 ^= p8; \
  h1 ^= p9; \
  h2 ^= pA; \
  h3 ^= pB; \
  h4 ^= pC; \
  h5 ^= pD; \
  h6 ^= pE; \
  h7 ^= pF; \
  h8 ^= p0; \
  h9 ^= p1; \
  hA ^= p2; \
  hB ^= p3; \
  hC ^= p4; \
  hD ^= p5; \
  hE ^= p6; \
  hF ^= p7; \
  } while (0)

#define c256(msg)    do { \
  sph_u32 p0, p1, p2, p3, p4, p5, p6, p7; \
  sph_u32 x0, x1, x2, x3; \
    \
  p0 = h[0x0]; \
  p1 = h[0x1]; \
  p2 = h[0x2]; \
  p3 = h[0x3]; \
  p4 = h[0x4]; \
  p5 = h[0x5]; \
  p6 = h[0x6]; \
  p7 = h[0x7]; \
  /* round 0 */ \
  x0 = p4 ^ rk0; \
  x1 = p5 ^ rk1; \
  x2 = p6 ^ rk2; \
  x3 = p7 ^ rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk4; \
  x1 ^= rk5; \
  x2 ^= rk6; \
  x3 ^= rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  x0 ^= rk8; \
  x1 ^= rk9; \
  x2 ^= rkA; \
  x3 ^= rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  /* round 1 */ \
  x0 = p0 ^ rkC; \
  x1 = p1 ^ rkD; \
  x2 = p2 ^ rkE; \
  x3 = p3 ^ rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk0, rk1, rk2, rk3); \
  rk0 ^= rkC ^ count0; \
  rk1 ^= rkD ^ SPH_T32(~count1); \
  rk2 ^= rkE; \
  rk3 ^= rkF; \
  x0 ^= rk0; \
  x1 ^= rk1; \
  x2 ^= rk2; \
  x3 ^= rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk4, rk5, rk6, rk7); \
  rk4 ^= rk0; \
  rk5 ^= rk1; \
  rk6 ^= rk2; \
  rk7 ^= rk3; \
  x0 ^= rk4; \
  x1 ^= rk5; \
  x2 ^= rk6; \
  x3 ^= rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  /* round 2 */ \
  KEY_EXPAND_ELT(rk8, rk9, rkA, rkB); \
  rk8 ^= rk4; \
  rk9 ^= rk5; \
  rkA ^= rk6; \
  rkB ^= rk7; \
  x0 = p4 ^ rk8; \
  x1 = p5 ^ rk9; \
  x2 = p6 ^ rkA; \
  x3 = p7 ^ rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rkC, rkD, rkE, rkF); \
  rkC ^= rk8; \
  rkD ^= rk9; \
  rkE ^= rkA; \
  rkF ^= rkB; \
  x0 ^= rkC; \
  x1 ^= rkD; \
  x2 ^= rkE; \
  x3 ^= rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk0 ^= rkD; \
  x0 ^= rk0; \
  rk1 ^= rkE; \
  x1 ^= rk1; \
  rk2 ^= rkF; \
  x2 ^= rk2; \
  rk3 ^= rk0; \
  x3 ^= rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  /* round 3 */ \
  rk4 ^= rk1; \
  x0 = p0 ^ rk4; \
  rk5 ^= rk2; \
  x1 = p1 ^ rk5; \
  rk6 ^= rk3; \
  x2 = p2 ^ rk6; \
  rk7 ^= rk4; \
  x3 = p3 ^ rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk8 ^= rk5; \
  x0 ^= rk8; \
  rk9 ^= rk6; \
  x1 ^= rk9; \
  rkA ^= rk7; \
  x2 ^= rkA; \
  rkB ^= rk8; \
  x3 ^= rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rkC ^= rk9; \
  x0 ^= rkC; \
  rkD ^= rkA; \
  x1 ^= rkD; \
  rkE ^= rkB; \
  x2 ^= rkE; \
  rkF ^= rkC; \
  x3 ^= rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  /* round 4 */ \
  KEY_EXPAND_ELT(rk0, rk1, rk2, rk3); \
  rk0 ^= rkC; \
  rk1 ^= rkD; \
  rk2 ^= rkE; \
  rk3 ^= rkF; \
  x0 = p4 ^ rk0; \
  x1 = p5 ^ rk1; \
  x2 = p6 ^ rk2; \
  x3 = p7 ^ rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk4, rk5, rk6, rk7); \
  rk4 ^= rk0; \
  rk5 ^= rk1; \
  rk6 ^= rk2; \
  rk7 ^= rk3; \
  x0 ^= rk4; \
  x1 ^= rk5; \
  x2 ^= rk6; \
  x3 ^= rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk8, rk9, rkA, rkB); \
  rk8 ^= rk4; \
  rk9 ^= rk5 ^ count1; \
  rkA ^= rk6 ^ SPH_T32(~count0); \
  rkB ^= rk7; \
  x0 ^= rk8; \
  x1 ^= rk9; \
  x2 ^= rkA; \
  x3 ^= rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  /* round 5 */ \
  KEY_EXPAND_ELT(rkC, rkD, rkE, rkF); \
  rkC ^= rk8; \
  rkD ^= rk9; \
  rkE ^= rkA; \
  rkF ^= rkB; \
  x0 = p0 ^ rkC; \
  x1 = p1 ^ rkD; \
  x2 = p2 ^ rkE; \
  x3 = p3 ^ rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk0 ^= rkD; \
  x0 ^= rk0; \
  rk1 ^= rkE; \
  x1 ^= rk1; \
  rk2 ^= rkF; \
  x2 ^= rk2; \
  rk3 ^= rk0; \
  x3 ^= rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk4 ^= rk1; \
  x0 ^= rk4; \
  rk5 ^= rk2; \
  x1 ^= rk5; \
  rk6 ^= rk3; \
  x2 ^= rk6; \
  rk7 ^= rk4; \
  x3 ^= rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  /* round 6 */ \
  rk8 ^= rk5; \
  x0 = p4 ^ rk8; \
  rk9 ^= rk6; \
  x1 = p5 ^ rk9; \
  rkA ^= rk7; \
  x2 = p6 ^ rkA; \
  rkB ^= rk8; \
  x3 = p7 ^ rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rkC ^= rk9; \
  x0 ^= rkC; \
  rkD ^= rkA; \
  x1 ^= rkD; \
  rkE ^= rkB; \
  x2 ^= rkE; \
  rkF ^= rkC; \
  x3 ^= rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk0, rk1, rk2, rk3); \
  rk0 ^= rkC; \
  rk1 ^= rkD; \
  rk2 ^= rkE; \
  rk3 ^= rkF; \
  x0 ^= rk0; \
  x1 ^= rk1; \
  x2 ^= rk2; \
  x3 ^= rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  /* round 7 */ \
  KEY_EXPAND_ELT(rk4, rk5, rk6, rk7); \
  rk4 ^= rk0; \
  rk5 ^= rk1; \
  rk6 ^= rk2 ^ count1; \
  rk7 ^= rk3 ^ SPH_T32(~count0); \
  x0 = p0 ^ rk4; \
  x1 = p1 ^ rk5; \
  x2 = p2 ^ rk6; \
  x3 = p3 ^ rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk8, rk9, rkA, rkB); \
  rk8 ^= rk4; \
  rk9 ^= rk5; \
  rkA ^= rk6; \
  rkB ^= rk7; \
  x0 ^= rk8; \
  x1 ^= rk9; \
  x2 ^= rkA; \
  x3 ^= rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rkC, rkD, rkE, rkF); \
  rkC ^= rk8; \
  rkD ^= rk9; \
  rkE ^= rkA; \
  rkF ^= rkB; \
  x0 ^= rkC; \
  x1 ^= rkD; \
  x2 ^= rkE; \
  x3 ^= rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  /* round 8 */ \
  rk0 ^= rkD; \
  x0 = p4 ^ rk0; \
  rk1 ^= rkE; \
  x1 = p5 ^ rk1; \
  rk2 ^= rkF; \
  x2 = p6 ^ rk2; \
  rk3 ^= rk0; \
  x3 = p7 ^ rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk4 ^= rk1; \
  x0 ^= rk4; \
  rk5 ^= rk2; \
  x1 ^= rk5; \
  rk6 ^= rk3; \
  x2 ^= rk6; \
  rk7 ^= rk4; \
  x3 ^= rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk8 ^= rk5; \
  x0 ^= rk8; \
  rk9 ^= rk6; \
  x1 ^= rk9; \
  rkA ^= rk7; \
  x2 ^= rkA; \
  rkB ^= rk8; \
  x3 ^= rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  /* round 9 */ \
  rkC ^= rk9; \
  x0 = p0 ^ rkC; \
  rkD ^= rkA; \
  x1 = p1 ^ rkD; \
  rkE ^= rkB; \
  x2 = p2 ^ rkE; \
  rkF ^= rkC; \
  x3 = p3 ^ rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk0, rk1, rk2, rk3); \
  rk0 ^= rkC; \
  rk1 ^= rkD; \
  rk2 ^= rkE; \
  rk3 ^= rkF; \
  x0 ^= rk0; \
  x1 ^= rk1; \
  x2 ^= rk2; \
  x3 ^= rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rk4, rk5, rk6, rk7); \
  rk4 ^= rk0; \
  rk5 ^= rk1; \
  rk6 ^= rk2; \
  rk7 ^= rk3; \
  x0 ^= rk4; \
  x1 ^= rk5; \
  x2 ^= rk6; \
  x3 ^= rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  /* round 10 */ \
  KEY_EXPAND_ELT(rk8, rk9, rkA, rkB); \
  rk8 ^= rk4; \
  rk9 ^= rk5; \
  rkA ^= rk6; \
  rkB ^= rk7; \
  x0 = p4 ^ rk8; \
  x1 = p5 ^ rk9; \
  x2 = p6 ^ rkA; \
  x3 = p7 ^ rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  KEY_EXPAND_ELT(rkC, rkD, rkE, rkF); \
  rkC ^= rk8 ^ count0; \
  rkD ^= rk9; \
  rkE ^= rkA; \
  rkF ^= rkB ^ SPH_T32(~count1); \
  x0 ^= rkC; \
  x1 ^= rkD; \
  x2 ^= rkE; \
  x3 ^= rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk0 ^= rkD; \
  x0 ^= rk0; \
  rk1 ^= rkE; \
  x1 ^= rk1; \
  rk2 ^= rkF; \
  x2 ^= rk2; \
  rk3 ^= rk0; \
  x3 ^= rk3; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p0 ^= x0; \
  p1 ^= x1; \
  p2 ^= x2; \
  p3 ^= x3; \
  /* round 11 */ \
  rk4 ^= rk1; \
  x0 = p0 ^ rk4; \
  rk5 ^= rk2; \
  x1 = p1 ^ rk5; \
  rk6 ^= rk3; \
  x2 = p2 ^ rk6; \
  rk7 ^= rk4; \
  x3 = p3 ^ rk7; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rk8 ^= rk5; \
  x0 ^= rk8; \
  rk9 ^= rk6; \
  x1 ^= rk9; \
  rkA ^= rk7; \
  x2 ^= rkA; \
  rkB ^= rk8; \
  x3 ^= rkB; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  rkC ^= rk9; \
  x0 ^= rkC; \
  rkD ^= rkA; \
  x1 ^= rkD; \
  rkE ^= rkB; \
  x2 ^= rkE; \
  rkF ^= rkC; \
  x3 ^= rkF; \
  AES_ROUND_NOKEY(x0, x1, x2, x3); \
  p4 ^= x0; \
  p5 ^= x1; \
  p6 ^= x2; \
  p7 ^= x3; \
  h[0x0] ^= p0; \
  h[0x1] ^= p1; \
  h[0x2] ^= p2; \
  h[0x3] ^= p3; \
  h[0x4] ^= p4; \
  h[0x5] ^= p5; \
  h[0x6] ^= p6; \
  h[0x7] ^= p7; \
    } while(0)

