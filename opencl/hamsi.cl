/* $Id: hamsi.c 251 2010-10-19 14:31:51Z tp $ */
/*
 * Hamsi implementation.
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

#if SPH_SMALL_FOOTPRINT && !defined SPH_SMALL_FOOTPRINT_HAMSI
#define SPH_SMALL_FOOTPRINT_HAMSI   1
#endif

/*
 * The SPH_HAMSI_EXPAND_* define how many input bits we handle in one
 * table lookup during message expansion (1 to 8, inclusive). If we note
 * w the number of bits per message word (w=32 for Hamsi-224/256, w=64
 * for Hamsi-384/512), r the size of a "row" in 32-bit words (r=8 for
 * Hamsi-224/256, r=16 for Hamsi-384/512), and n the expansion level,
 * then we will get t tables (where t=ceil(w/n)) of individual size
 * 2^n*r*4 (in bytes). The last table may be shorter (e.g. with w=32 and
 * n=5, there are 7 tables, but the last one uses only two bits on
 * input, not five).
 *
 * Also, we read t rows of r words from RAM. Words in a given row are
 * concatenated in RAM in that order, so most of the cost is about
 * reading the first row word; comparatively, cache misses are thus
 * less expensive with Hamsi-512 (r=16) than with Hamsi-256 (r=8).
 *
 * When n=1, tables are "special" in that we omit the first entry of
 * each table (which always contains 0), so that total table size is
 * halved.
 *
 * We thus have the following (size1 is the cumulative table size of
 * Hamsi-224/256; size2 is for Hamsi-384/512; similarly, t1 and t2
 * are for Hamsi-224/256 and Hamsi-384/512, respectively).
 *
 *   n      size1      size2    t1    t2
 * ---------------------------------------
 *   1       1024       4096    32    64
 *   2       2048       8192    16    32
 *   3       2688      10880    11    22
 *   4       4096      16384     8    16
 *   5       6272      25600     7    13
 *   6      10368      41984     6    11
 *   7      16896      73856     5    10
 *   8      32768     131072     4     8
 *
 * So there is a trade-off: a lower n makes the tables fit better in
 * L1 cache, but increases the number of memory accesses. The optimal
 * value depends on the amount of available L1 cache and the relative
 * impact of a cache miss.
 *
 * Experimentally, in ideal benchmark conditions (which are not necessarily
 * realistic with regards to L1 cache contention), it seems that n=8 is
 * the best value on "big" architectures (those with 32 kB or more of L1
 * cache), while n=4 is better on "small" architectures. This was tested
 * on an Intel Core2 Q6600 (both 32-bit and 64-bit mode), a PowerPC G3
 * (32 kB L1 cache, hence "big"), and a MIPS-compatible Broadcom BCM3302
 * (8 kB L1 cache).
 *
 * Note: with n=1, the 32 tables (actually implemented as one big table)
 * are read entirely and sequentially, regardless of the input data,
 * thus avoiding any data-dependent table access pattern.
 */

#if !defined SPH_HAMSI_EXPAND_SMALL
  #if SPH_SMALL_FOOTPRINT_HAMSI
    #define SPH_HAMSI_EXPAND_SMALL  4
  #else
    #define SPH_HAMSI_EXPAND_SMALL  8
  #endif
#endif

#if !defined SPH_HAMSI_EXPAND_BIG
  #define SPH_HAMSI_EXPAND_BIG    8
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4146)
#endif

//temp fix for shortened implementation of X15
#ifdef SPH_HAMSI_SHORT
  #if SPH_HAMSI_SHORT == 1 && SPH_HAMSI_EXPAND_BIG == 1
    #include "hamsi_helper_big.cl"
  #else
    #include "hamsi_helper.cl"
  #endif
#else
  #include "hamsi_helper.cl"
#endif

__constant static const sph_u32 HAMSI_IV224[] = {
  SPH_C32(0xc3967a67), SPH_C32(0xc3bc6c20), SPH_C32(0x4bc3bcc3),
  SPH_C32(0xa7c3bc6b), SPH_C32(0x2c204b61), SPH_C32(0x74686f6c),
  SPH_C32(0x69656b65), SPH_C32(0x20556e69)
};

/*
 * This version is the one used in the Hamsi submission package for
 * round 2 of the SHA-3 competition; the UTF-8 encoding is wrong and
 * shall soon be corrected in the official Hamsi specification.
 *
__constant static const sph_u32 HAMSI_IV224[] = {
  SPH_C32(0x3c967a67), SPH_C32(0x3cbc6c20), SPH_C32(0xb4c343c3),
  SPH_C32(0xa73cbc6b), SPH_C32(0x2c204b61), SPH_C32(0x74686f6c),
  SPH_C32(0x69656b65), SPH_C32(0x20556e69)
};
 */

__constant static const sph_u32 HAMSI_IV256[] = {
  SPH_C32(0x76657273), SPH_C32(0x69746569), SPH_C32(0x74204c65),
  SPH_C32(0x7576656e), SPH_C32(0x2c204465), SPH_C32(0x70617274),
  SPH_C32(0x656d656e), SPH_C32(0x7420456c)
};

__constant static const sph_u32 HAMSI_IV384[] = {
  SPH_C32(0x656b7472), SPH_C32(0x6f746563), SPH_C32(0x686e6965),
  SPH_C32(0x6b2c2043), SPH_C32(0x6f6d7075), SPH_C32(0x74657220),
  SPH_C32(0x53656375), SPH_C32(0x72697479), SPH_C32(0x20616e64),
  SPH_C32(0x20496e64), SPH_C32(0x75737472), SPH_C32(0x69616c20),
  SPH_C32(0x43727970), SPH_C32(0x746f6772), SPH_C32(0x61706879),
  SPH_C32(0x2c204b61)
};

__constant static const sph_u32 HAMSI_IV512[] = {
  SPH_C32(0x73746565), SPH_C32(0x6c706172), SPH_C32(0x6b204172),
  SPH_C32(0x656e6265), SPH_C32(0x72672031), SPH_C32(0x302c2062),
  SPH_C32(0x75732032), SPH_C32(0x3434362c), SPH_C32(0x20422d33),
  SPH_C32(0x30303120), SPH_C32(0x4c657576), SPH_C32(0x656e2d48),
  SPH_C32(0x65766572), SPH_C32(0x6c65652c), SPH_C32(0x2042656c),
  SPH_C32(0x6769756d)
};

__constant static const sph_u32 alpha_n[] = {
  SPH_C32(0xff00f0f0), SPH_C32(0xccccaaaa), SPH_C32(0xf0f0cccc),
  SPH_C32(0xff00aaaa), SPH_C32(0xccccaaaa), SPH_C32(0xf0f0ff00),
  SPH_C32(0xaaaacccc), SPH_C32(0xf0f0ff00), SPH_C32(0xf0f0cccc),
  SPH_C32(0xaaaaff00), SPH_C32(0xccccff00), SPH_C32(0xaaaaf0f0),
  SPH_C32(0xaaaaf0f0), SPH_C32(0xff00cccc), SPH_C32(0xccccf0f0),
  SPH_C32(0xff00aaaa), SPH_C32(0xccccaaaa), SPH_C32(0xff00f0f0),
  SPH_C32(0xff00aaaa), SPH_C32(0xf0f0cccc), SPH_C32(0xf0f0ff00),
  SPH_C32(0xccccaaaa), SPH_C32(0xf0f0ff00), SPH_C32(0xaaaacccc),
  SPH_C32(0xaaaaff00), SPH_C32(0xf0f0cccc), SPH_C32(0xaaaaf0f0),
  SPH_C32(0xccccff00), SPH_C32(0xff00cccc), SPH_C32(0xaaaaf0f0),
  SPH_C32(0xff00aaaa), SPH_C32(0xccccf0f0)
};

__constant static const sph_u32 alpha_f[] = {
  SPH_C32(0xcaf9639c), SPH_C32(0x0ff0f9c0), SPH_C32(0x639c0ff0),
  SPH_C32(0xcaf9f9c0), SPH_C32(0x0ff0f9c0), SPH_C32(0x639ccaf9),
  SPH_C32(0xf9c00ff0), SPH_C32(0x639ccaf9), SPH_C32(0x639c0ff0),
  SPH_C32(0xf9c0caf9), SPH_C32(0x0ff0caf9), SPH_C32(0xf9c0639c),
  SPH_C32(0xf9c0639c), SPH_C32(0xcaf90ff0), SPH_C32(0x0ff0639c),
  SPH_C32(0xcaf9f9c0), SPH_C32(0x0ff0f9c0), SPH_C32(0xcaf9639c),
  SPH_C32(0xcaf9f9c0), SPH_C32(0x639c0ff0), SPH_C32(0x639ccaf9),
  SPH_C32(0x0ff0f9c0), SPH_C32(0x639ccaf9), SPH_C32(0xf9c00ff0),
  SPH_C32(0xf9c0caf9), SPH_C32(0x639c0ff0), SPH_C32(0xf9c0639c),
  SPH_C32(0x0ff0caf9), SPH_C32(0xcaf90ff0), SPH_C32(0xf9c0639c),
  SPH_C32(0xcaf9f9c0), SPH_C32(0x0ff0639c)
};

#define HAMSI_DECL_STATE_SMALL \
  sph_u32 c0, c1, c2, c3, c4, c5, c6, c7;

#define HAMSI_READ_STATE_SMALL(sc)   do { \
    c0 = h[0x0]; \
    c1 = h[0x1]; \
    c2 = h[0x2]; \
    c3 = h[0x3]; \
    c4 = h[0x4]; \
    c5 = h[0x5]; \
    c6 = h[0x6]; \
    c7 = h[0x7]; \
  } while (0)

#define HAMSI_WRITE_STATE_SMALL(sc)   do { \
    h[0x0] = c0; \
    h[0x1] = c1; \
    h[0x2] = c2; \
    h[0x3] = c3; \
    h[0x4] = c4; \
    h[0x5] = c5; \
    h[0x6] = c6; \
    h[0x7] = c7; \
  } while (0)

#define hamsi_s0   m0
#define hamsi_s1   m1
#define hamsi_s2   c0
#define hamsi_s3   c1
#define hamsi_s4   c2
#define hamsi_s5   c3
#define hamsi_s6   m2
#define hamsi_s7   m3
#define hamsi_s8   m4
#define hamsi_s9   m5
#define hamsi_sA   c4
#define hamsi_sB   c5
#define hamsi_sC   c6
#define hamsi_sD   c7
#define hamsi_sE   m6
#define hamsi_sF   m7

#define SBOX(a, b, c, d)   do { \
    sph_u32 t; \
    t = (a); \
    (a) &= (c); \
    (a) ^= (d); \
    (c) ^= (b); \
    (c) ^= (a); \
    (d) |= t; \
    (d) ^= (b); \
    t ^= (c); \
    (b) = (d); \
    (d) |= t; \
    (d) ^= (a); \
    (a) &= (b); \
    t ^= (a); \
    (b) ^= (d); \
    (b) ^= t; \
    (a) = (c); \
    (c) = (b); \
    (b) = (d); \
    (d) = SPH_T32(~t); \
  } while (0)

#define HAMSI_L(a, b, c, d)   do { \
    (a) = SPH_ROTL32(a, 13); \
    (c) = SPH_ROTL32(c, 3); \
    (b) ^= (a) ^ (c); \
    (d) ^= (c) ^ SPH_T32((a) << 3); \
    (b) = SPH_ROTL32(b, 1); \
    (d) = SPH_ROTL32(d, 7); \
    (a) ^= (b) ^ (d); \
    (c) ^= (d) ^ SPH_T32((b) << 7); \
    (a) = SPH_ROTL32(a, 5); \
    (c) = SPH_ROTL32(c, 22); \
  } while (0)

#define ROUND_SMALL(rc, alpha)   do { \
    hamsi_s0 ^= alpha[0x00]; \
    hamsi_s1 ^= alpha[0x01] ^ (sph_u32)(rc); \
    hamsi_s2 ^= alpha[0x02]; \
    hamsi_s3 ^= alpha[0x03]; \
    hamsi_s4 ^= alpha[0x08]; \
    hamsi_s5 ^= alpha[0x09]; \
    hamsi_s6 ^= alpha[0x0A]; \
    hamsi_s7 ^= alpha[0x0B]; \
    hamsi_s8 ^= alpha[0x10]; \
    hamsi_s9 ^= alpha[0x11]; \
    hamsi_sA ^= alpha[0x12]; \
    hamsi_sB ^= alpha[0x13]; \
    hamsi_sC ^= alpha[0x18]; \
    hamsi_sD ^= alpha[0x19]; \
    hamsi_sE ^= alpha[0x1A]; \
    hamsi_sF ^= alpha[0x1B]; \
    SBOX(hamsi_s0, hamsi_s4, hamsi_s8, hamsi_sC); \
    SBOX(hamsi_s1, hamsi_s5, hamsi_s9, hamsi_sD); \
    SBOX(hamsi_s2, hamsi_s6, hamsi_sA, hamsi_sE); \
    SBOX(hamsi_s3, hamsi_s7, hamsi_sB, hamsi_sF); \
    HAMSI_L(hamsi_s0, hamsi_s5, hamsi_sA, hamsi_sF); \
    HAMSI_L(hamsi_s1, hamsi_s6, hamsi_sB, hamsi_sC); \
    HAMSI_L(hamsi_s2, hamsi_s7, hamsi_s8, hamsi_sD); \
    HAMSI_L(hamsi_s3, hamsi_s4, hamsi_s9, hamsi_sE); \
  } while (0)

#define P_SMALL   do { \
    ROUND_SMALL(0, alpha_n); \
    ROUND_SMALL(1, alpha_n); \
    ROUND_SMALL(2, alpha_n); \
  } while (0)

#define PF_SMALL   do { \
    ROUND_SMALL(0, alpha_f); \
    ROUND_SMALL(1, alpha_f); \
    ROUND_SMALL(2, alpha_f); \
    ROUND_SMALL(3, alpha_f); \
    ROUND_SMALL(4, alpha_f); \
    ROUND_SMALL(5, alpha_f); \
  } while (0)

#define T_SMALL   do { \
    /* order is important */ \
    c7 = (h[7] ^= hamsi_sB); \
    c6 = (h[6] ^= hamsi_sA); \
    c5 = (h[5] ^= hamsi_s9); \
    c4 = (h[4] ^= hamsi_s8); \
    c3 = (h[3] ^= hamsi_s3); \
    c2 = (h[2] ^= hamsi_s2); \
    c1 = (h[1] ^= hamsi_s1); \
    c0 = (h[0] ^= hamsi_s0); \
  } while (0)

#define hamsi_s00   m0
#define hamsi_s01   m1
#define hamsi_s02   c0
#define hamsi_s03   c1
#define hamsi_s04   m2
#define hamsi_s05   m3
#define hamsi_s06   c2
#define hamsi_s07   c3
#define hamsi_s08   c4
#define hamsi_s09   c5
#define hamsi_s0A   m4
#define hamsi_s0B   m5
#define hamsi_s0C   c6
#define hamsi_s0D   c7
#define hamsi_s0E   m6
#define hamsi_s0F   m7
#define hamsi_s10   m8
#define hamsi_s11   m9
#define hamsi_s12   c8
#define hamsi_s13   c9
#define hamsi_s14   mA
#define hamsi_s15   mB
#define hamsi_s16   cA
#define hamsi_s17   cB
#define hamsi_s18   cC
#define hamsi_s19   cD
#define hamsi_s1A   mC
#define hamsi_s1B   mD
#define hamsi_s1C   cE
#define hamsi_s1D   cF
#define hamsi_s1E   mE
#define hamsi_s1F   mF

#define ROUND_BIG(rc, alpha)   do { \
    hamsi_s00 ^= alpha[0x00]; \
    hamsi_s01 ^= alpha[0x01] ^ (sph_u32)(rc); \
    hamsi_s02 ^= alpha[0x02]; \
    hamsi_s03 ^= alpha[0x03]; \
    hamsi_s04 ^= alpha[0x04]; \
    hamsi_s05 ^= alpha[0x05]; \
    hamsi_s06 ^= alpha[0x06]; \
    hamsi_s07 ^= alpha[0x07]; \
    hamsi_s08 ^= alpha[0x08]; \
    hamsi_s09 ^= alpha[0x09]; \
    hamsi_s0A ^= alpha[0x0A]; \
    hamsi_s0B ^= alpha[0x0B]; \
    hamsi_s0C ^= alpha[0x0C]; \
    hamsi_s0D ^= alpha[0x0D]; \
    hamsi_s0E ^= alpha[0x0E]; \
    hamsi_s0F ^= alpha[0x0F]; \
    hamsi_s10 ^= alpha[0x10]; \
    hamsi_s11 ^= alpha[0x11]; \
    hamsi_s12 ^= alpha[0x12]; \
    hamsi_s13 ^= alpha[0x13]; \
    hamsi_s14 ^= alpha[0x14]; \
    hamsi_s15 ^= alpha[0x15]; \
    hamsi_s16 ^= alpha[0x16]; \
    hamsi_s17 ^= alpha[0x17]; \
    hamsi_s18 ^= alpha[0x18]; \
    hamsi_s19 ^= alpha[0x19]; \
    hamsi_s1A ^= alpha[0x1A]; \
    hamsi_s1B ^= alpha[0x1B]; \
    hamsi_s1C ^= alpha[0x1C]; \
    hamsi_s1D ^= alpha[0x1D]; \
    hamsi_s1E ^= alpha[0x1E]; \
    hamsi_s1F ^= alpha[0x1F]; \
    SBOX(hamsi_s00, hamsi_s08, hamsi_s10, hamsi_s18); \
    SBOX(hamsi_s01, hamsi_s09, hamsi_s11, hamsi_s19); \
    SBOX(hamsi_s02, hamsi_s0A, hamsi_s12, hamsi_s1A); \
    SBOX(hamsi_s03, hamsi_s0B, hamsi_s13, hamsi_s1B); \
    SBOX(hamsi_s04, hamsi_s0C, hamsi_s14, hamsi_s1C); \
    SBOX(hamsi_s05, hamsi_s0D, hamsi_s15, hamsi_s1D); \
    SBOX(hamsi_s06, hamsi_s0E, hamsi_s16, hamsi_s1E); \
    SBOX(hamsi_s07, hamsi_s0F, hamsi_s17, hamsi_s1F); \
    HAMSI_L(hamsi_s00, hamsi_s09, hamsi_s12, hamsi_s1B); \
    HAMSI_L(hamsi_s01, hamsi_s0A, hamsi_s13, hamsi_s1C); \
    HAMSI_L(hamsi_s02, hamsi_s0B, hamsi_s14, hamsi_s1D); \
    HAMSI_L(hamsi_s03, hamsi_s0C, hamsi_s15, hamsi_s1E); \
    HAMSI_L(hamsi_s04, hamsi_s0D, hamsi_s16, hamsi_s1F); \
    HAMSI_L(hamsi_s05, hamsi_s0E, hamsi_s17, hamsi_s18); \
    HAMSI_L(hamsi_s06, hamsi_s0F, hamsi_s10, hamsi_s19); \
    HAMSI_L(hamsi_s07, hamsi_s08, hamsi_s11, hamsi_s1A); \
    HAMSI_L(hamsi_s00, hamsi_s02, hamsi_s05, hamsi_s07); \
    HAMSI_L(hamsi_s10, hamsi_s13, hamsi_s15, hamsi_s16); \
    HAMSI_L(hamsi_s09, hamsi_s0B, hamsi_s0C, hamsi_s0E); \
    HAMSI_L(hamsi_s19, hamsi_s1A, hamsi_s1C, hamsi_s1F); \
  } while (0)


#define P_BIG   do { \
    ROUND_BIG(0, alpha_n); \
    ROUND_BIG(1, alpha_n); \
    ROUND_BIG(2, alpha_n); \
    ROUND_BIG(3, alpha_n); \
    ROUND_BIG(4, alpha_n); \
    ROUND_BIG(5, alpha_n); \
  } while (0)

#define PF_BIG   do { \
    ROUND_BIG(0, alpha_f); \
    ROUND_BIG(1, alpha_f); \
    ROUND_BIG(2, alpha_f); \
    ROUND_BIG(3, alpha_f); \
    ROUND_BIG(4, alpha_f); \
    ROUND_BIG(5, alpha_f); \
    ROUND_BIG(6, alpha_f); \
    ROUND_BIG(7, alpha_f); \
    ROUND_BIG(8, alpha_f); \
    ROUND_BIG(9, alpha_f); \
    ROUND_BIG(10, alpha_f); \
    ROUND_BIG(11, alpha_f); \
  } while (0)

#define T_BIG   do { \
    /* order is important */ \
    cF = (h[0xF] ^= hamsi_s17); \
    cE = (h[0xE] ^= hamsi_s16); \
    cD = (h[0xD] ^= hamsi_s15); \
    cC = (h[0xC] ^= hamsi_s14); \
    cB = (h[0xB] ^= hamsi_s13); \
    cA = (h[0xA] ^= hamsi_s12); \
    c9 = (h[0x9] ^= hamsi_s11); \
    c8 = (h[0x8] ^= hamsi_s10); \
    c7 = (h[0x7] ^= hamsi_s07); \
    c6 = (h[0x6] ^= hamsi_s06); \
    c5 = (h[0x5] ^= hamsi_s05); \
    c4 = (h[0x4] ^= hamsi_s04); \
    c3 = (h[0x3] ^= hamsi_s03); \
    c2 = (h[0x2] ^= hamsi_s02); \
    c1 = (h[0x1] ^= hamsi_s01); \
    c0 = (h[0x0] ^= hamsi_s00); \
  } while (0)


