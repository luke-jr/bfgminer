/* $Id: luffa.c 219 2010-06-08 17:24:41Z tp $ */
/*
 * Luffa implementation.
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

#ifdef __cplusplus
extern "C"{
#endif

#if SPH_64_TRUE && !defined SPH_LUFFA_PARALLEL
#define SPH_LUFFA_PARALLEL   1
#endif

__constant static const sph_u32 V_INIT[5][8] = {
  {
    SPH_C32(0x6d251e69), SPH_C32(0x44b051e0),
    SPH_C32(0x4eaa6fb4), SPH_C32(0xdbf78465),
    SPH_C32(0x6e292011), SPH_C32(0x90152df4),
    SPH_C32(0xee058139), SPH_C32(0xdef610bb)
  }, {
    SPH_C32(0xc3b44b95), SPH_C32(0xd9d2f256),
    SPH_C32(0x70eee9a0), SPH_C32(0xde099fa3),
    SPH_C32(0x5d9b0557), SPH_C32(0x8fc944b3),
    SPH_C32(0xcf1ccf0e), SPH_C32(0x746cd581)
  }, {
    SPH_C32(0xf7efc89d), SPH_C32(0x5dba5781),
    SPH_C32(0x04016ce5), SPH_C32(0xad659c05),
    SPH_C32(0x0306194f), SPH_C32(0x666d1836),
    SPH_C32(0x24aa230a), SPH_C32(0x8b264ae7)
  }, {
    SPH_C32(0x858075d5), SPH_C32(0x36d79cce),
    SPH_C32(0xe571f7d7), SPH_C32(0x204b1f67),
    SPH_C32(0x35870c6a), SPH_C32(0x57e9e923),
    SPH_C32(0x14bcb808), SPH_C32(0x7cde72ce)
  }, {
    SPH_C32(0x6c68e9be), SPH_C32(0x5ec41e22),
    SPH_C32(0xc825b7c7), SPH_C32(0xaffb4363),
    SPH_C32(0xf5df3999), SPH_C32(0x0fc688f1),
    SPH_C32(0xb07224cc), SPH_C32(0x03e86cea)
  }
};

__constant static const sph_u32 RC00[8] = {
  SPH_C32(0x303994a6), SPH_C32(0xc0e65299),
  SPH_C32(0x6cc33a12), SPH_C32(0xdc56983e),
  SPH_C32(0x1e00108f), SPH_C32(0x7800423d),
  SPH_C32(0x8f5b7882), SPH_C32(0x96e1db12)
};

__constant static const sph_u32 RC04[8] = {
  SPH_C32(0xe0337818), SPH_C32(0x441ba90d),
  SPH_C32(0x7f34d442), SPH_C32(0x9389217f),
  SPH_C32(0xe5a8bce6), SPH_C32(0x5274baf4),
  SPH_C32(0x26889ba7), SPH_C32(0x9a226e9d)
};

__constant static const sph_u32 RC10[8] = {
  SPH_C32(0xb6de10ed), SPH_C32(0x70f47aae),
  SPH_C32(0x0707a3d4), SPH_C32(0x1c1e8f51),
  SPH_C32(0x707a3d45), SPH_C32(0xaeb28562),
  SPH_C32(0xbaca1589), SPH_C32(0x40a46f3e)
};

__constant static const sph_u32 RC14[8] = {
  SPH_C32(0x01685f3d), SPH_C32(0x05a17cf4),
  SPH_C32(0xbd09caca), SPH_C32(0xf4272b28),
  SPH_C32(0x144ae5cc), SPH_C32(0xfaa7ae2b),
  SPH_C32(0x2e48f1c1), SPH_C32(0xb923c704)
};

#if SPH_LUFFA_PARALLEL

__constant static const sph_u64 RCW010[8] = {
  SPH_C64(0xb6de10ed303994a6), SPH_C64(0x70f47aaec0e65299),
  SPH_C64(0x0707a3d46cc33a12), SPH_C64(0x1c1e8f51dc56983e),
  SPH_C64(0x707a3d451e00108f), SPH_C64(0xaeb285627800423d),
  SPH_C64(0xbaca15898f5b7882), SPH_C64(0x40a46f3e96e1db12)
};

__constant static const sph_u64 RCW014[8] = {
  SPH_C64(0x01685f3de0337818), SPH_C64(0x05a17cf4441ba90d),
  SPH_C64(0xbd09caca7f34d442), SPH_C64(0xf4272b289389217f),
  SPH_C64(0x144ae5cce5a8bce6), SPH_C64(0xfaa7ae2b5274baf4),
  SPH_C64(0x2e48f1c126889ba7), SPH_C64(0xb923c7049a226e9d)
};

#endif

__constant static const sph_u32 RC20[8] = {
  SPH_C32(0xfc20d9d2), SPH_C32(0x34552e25),
  SPH_C32(0x7ad8818f), SPH_C32(0x8438764a),
  SPH_C32(0xbb6de032), SPH_C32(0xedb780c8),
  SPH_C32(0xd9847356), SPH_C32(0xa2c78434)
};

__constant static const sph_u32 RC24[8] = {
  SPH_C32(0xe25e72c1), SPH_C32(0xe623bb72),
  SPH_C32(0x5c58a4a4), SPH_C32(0x1e38e2e7),
  SPH_C32(0x78e38b9d), SPH_C32(0x27586719),
  SPH_C32(0x36eda57f), SPH_C32(0x703aace7)
};

__constant static const sph_u32 RC30[8] = {
  SPH_C32(0xb213afa5), SPH_C32(0xc84ebe95),
  SPH_C32(0x4e608a22), SPH_C32(0x56d858fe),
  SPH_C32(0x343b138f), SPH_C32(0xd0ec4e3d),
  SPH_C32(0x2ceb4882), SPH_C32(0xb3ad2208)
};

__constant static const sph_u32 RC34[8] = {
  SPH_C32(0xe028c9bf), SPH_C32(0x44756f91),
  SPH_C32(0x7e8fce32), SPH_C32(0x956548be),
  SPH_C32(0xfe191be2), SPH_C32(0x3cb226e5),
  SPH_C32(0x5944a28e), SPH_C32(0xa1c4c355)
};

#if SPH_LUFFA_PARALLEL

__constant static const sph_u64 RCW230[8] = {
  SPH_C64(0xb213afa5fc20d9d2), SPH_C64(0xc84ebe9534552e25),
  SPH_C64(0x4e608a227ad8818f), SPH_C64(0x56d858fe8438764a),
  SPH_C64(0x343b138fbb6de032), SPH_C64(0xd0ec4e3dedb780c8),
  SPH_C64(0x2ceb4882d9847356), SPH_C64(0xb3ad2208a2c78434)
};


__constant static const sph_u64 RCW234[8] = {
  SPH_C64(0xe028c9bfe25e72c1), SPH_C64(0x44756f91e623bb72),
  SPH_C64(0x7e8fce325c58a4a4), SPH_C64(0x956548be1e38e2e7),
  SPH_C64(0xfe191be278e38b9d), SPH_C64(0x3cb226e527586719),
  SPH_C64(0x5944a28e36eda57f), SPH_C64(0xa1c4c355703aace7)
};

#endif

__constant static const sph_u32 RC40[8] = {
  SPH_C32(0xf0d2e9e3), SPH_C32(0xac11d7fa),
  SPH_C32(0x1bcb66f2), SPH_C32(0x6f2d9bc9),
  SPH_C32(0x78602649), SPH_C32(0x8edae952),
  SPH_C32(0x3b6ba548), SPH_C32(0xedae9520)
};

__constant static const sph_u32 RC44[8] = {
  SPH_C32(0x5090d577), SPH_C32(0x2d1925ab),
  SPH_C32(0xb46496ac), SPH_C32(0xd1925ab0),
  SPH_C32(0x29131ab6), SPH_C32(0x0fc053c3),
  SPH_C32(0x3f014f0c), SPH_C32(0xfc053c31)
};

#define DECL_TMP8(w) \
  sph_u32 w ## 0, w ## 1, w ## 2, w ## 3, w ## 4, w ## 5, w ## 6, w ## 7;

#define M2(d, s)   do { \
    sph_u32 tmp = s ## 7; \
    d ## 7 = s ## 6; \
    d ## 6 = s ## 5; \
    d ## 5 = s ## 4; \
    d ## 4 = s ## 3 ^ tmp; \
    d ## 3 = s ## 2 ^ tmp; \
    d ## 2 = s ## 1; \
    d ## 1 = s ## 0 ^ tmp; \
    d ## 0 = tmp; \
  } while (0)

#define XOR(d, s1, s2)   do { \
    d ## 0 = s1 ## 0 ^ s2 ## 0; \
    d ## 1 = s1 ## 1 ^ s2 ## 1; \
    d ## 2 = s1 ## 2 ^ s2 ## 2; \
    d ## 3 = s1 ## 3 ^ s2 ## 3; \
    d ## 4 = s1 ## 4 ^ s2 ## 4; \
    d ## 5 = s1 ## 5 ^ s2 ## 5; \
    d ## 6 = s1 ## 6 ^ s2 ## 6; \
    d ## 7 = s1 ## 7 ^ s2 ## 7; \
  } while (0)

#if SPH_LUFFA_PARALLEL

#define SUB_CRUMB_GEN(a0, a1, a2, a3, width)   do { \
    sph_u ## width tmp; \
    tmp = (a0); \
    (a0) |= (a1); \
    (a2) ^= (a3); \
    (a1) = SPH_T ## width(~(a1)); \
    (a0) ^= (a3); \
    (a3) &= tmp; \
    (a1) ^= (a3); \
    (a3) ^= (a2); \
    (a2) &= (a0); \
    (a0) = SPH_T ## width(~(a0)); \
    (a2) ^= (a1); \
    (a1) |= (a3); \
    tmp ^= (a1); \
    (a3) ^= (a2); \
    (a2) &= (a1); \
    (a1) ^= (a0); \
    (a0) = tmp; \
  } while (0)

#define SUB_CRUMB(a0, a1, a2, a3)    SUB_CRUMB_GEN(a0, a1, a2, a3, 32)
#define SUB_CRUMBW(a0, a1, a2, a3)   SUB_CRUMB_GEN(a0, a1, a2, a3, 64)

#define MIX_WORDW(u, v)   do { \
    sph_u32 ul, uh, vl, vh; \
    (v) ^= (u); \
    ul = SPH_T32((sph_u32)(u)); \
    uh = SPH_T32((sph_u32)((u) >> 32)); \
    vl = SPH_T32((sph_u32)(v)); \
    vh = SPH_T32((sph_u32)((v) >> 32)); \
    ul = SPH_ROTL32(ul, 2) ^ vl; \
    vl = SPH_ROTL32(vl, 14) ^ ul; \
    ul = SPH_ROTL32(ul, 10) ^ vl; \
    vl = SPH_ROTL32(vl, 1); \
    uh = SPH_ROTL32(uh, 2) ^ vh; \
    vh = SPH_ROTL32(vh, 14) ^ uh; \
    uh = SPH_ROTL32(uh, 10) ^ vh; \
    vh = SPH_ROTL32(vh, 1); \
    (u) = (sph_u64)ul | ((sph_u64)uh << 32); \
    (v) = (sph_u64)vl | ((sph_u64)vh << 32); \
  } while (0)

#else

#define SUB_CRUMB(a0, a1, a2, a3)   do { \
    sph_u32 tmp; \
    tmp = (a0); \
    (a0) |= (a1); \
    (a2) ^= (a3); \
    (a1) = SPH_T32(~(a1)); \
    (a0) ^= (a3); \
    (a3) &= tmp; \
    (a1) ^= (a3); \
    (a3) ^= (a2); \
    (a2) &= (a0); \
    (a0) = SPH_T32(~(a0)); \
    (a2) ^= (a1); \
    (a1) |= (a3); \
    tmp ^= (a1); \
    (a3) ^= (a2); \
    (a2) &= (a1); \
    (a1) ^= (a0); \
    (a0) = tmp; \
  } while (0)

#endif

#define MIX_WORD(u, v)   do { \
    (v) ^= (u); \
    (u) = SPH_ROTL32((u), 2) ^ (v); \
    (v) = SPH_ROTL32((v), 14) ^ (u); \
    (u) = SPH_ROTL32((u), 10) ^ (v); \
    (v) = SPH_ROTL32((v), 1); \
  } while (0)

#define MI5   do { \
    DECL_TMP8(a) \
    DECL_TMP8(b) \
    XOR(a, V0, V1); \
    XOR(b, V2, V3); \
    XOR(a, a, b); \
    XOR(a, a, V4); \
    M2(a, a); \
    XOR(V0, a, V0); \
    XOR(V1, a, V1); \
    XOR(V2, a, V2); \
    XOR(V3, a, V3); \
    XOR(V4, a, V4); \
    M2(b, V0); \
    XOR(b, b, V1); \
    M2(V1, V1); \
    XOR(V1, V1, V2); \
    M2(V2, V2); \
    XOR(V2, V2, V3); \
    M2(V3, V3); \
    XOR(V3, V3, V4); \
    M2(V4, V4); \
    XOR(V4, V4, V0); \
    M2(V0, b); \
    XOR(V0, V0, V4); \
    M2(V4, V4); \
    XOR(V4, V4, V3); \
    M2(V3, V3); \
    XOR(V3, V3, V2); \
    M2(V2, V2); \
    XOR(V2, V2, V1); \
    M2(V1, V1); \
    XOR(V1, V1, b); \
    XOR(V0, V0, M); \
    M2(M, M); \
    XOR(V1, V1, M); \
    M2(M, M); \
    XOR(V2, V2, M); \
    M2(M, M); \
    XOR(V3, V3, M); \
    M2(M, M); \
    XOR(V4, V4, M); \
  } while (0)

#define TWEAK5   do { \
    V14 = SPH_ROTL32(V14, 1); \
    V15 = SPH_ROTL32(V15, 1); \
    V16 = SPH_ROTL32(V16, 1); \
    V17 = SPH_ROTL32(V17, 1); \
    V24 = SPH_ROTL32(V24, 2); \
    V25 = SPH_ROTL32(V25, 2); \
    V26 = SPH_ROTL32(V26, 2); \
    V27 = SPH_ROTL32(V27, 2); \
    V34 = SPH_ROTL32(V34, 3); \
    V35 = SPH_ROTL32(V35, 3); \
    V36 = SPH_ROTL32(V36, 3); \
    V37 = SPH_ROTL32(V37, 3); \
    V44 = SPH_ROTL32(V44, 4); \
    V45 = SPH_ROTL32(V45, 4); \
    V46 = SPH_ROTL32(V46, 4); \
    V47 = SPH_ROTL32(V47, 4); \
  } while (0)

#if SPH_LUFFA_PARALLEL

#define LUFFA_P5   do { \
    int r; \
    sph_u64 W0, W1, W2, W3, W4, W5, W6, W7; \
    TWEAK5; \
    W0 = (sph_u64)V00 | ((sph_u64)V10 << 32); \
    W1 = (sph_u64)V01 | ((sph_u64)V11 << 32); \
    W2 = (sph_u64)V02 | ((sph_u64)V12 << 32); \
    W3 = (sph_u64)V03 | ((sph_u64)V13 << 32); \
    W4 = (sph_u64)V04 | ((sph_u64)V14 << 32); \
    W5 = (sph_u64)V05 | ((sph_u64)V15 << 32); \
    W6 = (sph_u64)V06 | ((sph_u64)V16 << 32); \
    W7 = (sph_u64)V07 | ((sph_u64)V17 << 32); \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMBW(W0, W1, W2, W3); \
      SUB_CRUMBW(W5, W6, W7, W4); \
      MIX_WORDW(W0, W4); \
      MIX_WORDW(W1, W5); \
      MIX_WORDW(W2, W6); \
      MIX_WORDW(W3, W7); \
      W0 ^= RCW010[r]; \
      W4 ^= RCW014[r]; \
    } \
    V00 = SPH_T32((sph_u32)W0); \
    V10 = SPH_T32((sph_u32)(W0 >> 32)); \
    V01 = SPH_T32((sph_u32)W1); \
    V11 = SPH_T32((sph_u32)(W1 >> 32)); \
    V02 = SPH_T32((sph_u32)W2); \
    V12 = SPH_T32((sph_u32)(W2 >> 32)); \
    V03 = SPH_T32((sph_u32)W3); \
    V13 = SPH_T32((sph_u32)(W3 >> 32)); \
    V04 = SPH_T32((sph_u32)W4); \
    V14 = SPH_T32((sph_u32)(W4 >> 32)); \
    V05 = SPH_T32((sph_u32)W5); \
    V15 = SPH_T32((sph_u32)(W5 >> 32)); \
    V06 = SPH_T32((sph_u32)W6); \
    V16 = SPH_T32((sph_u32)(W6 >> 32)); \
    V07 = SPH_T32((sph_u32)W7); \
    V17 = SPH_T32((sph_u32)(W7 >> 32)); \
    W0 = (sph_u64)V20 | ((sph_u64)V30 << 32); \
    W1 = (sph_u64)V21 | ((sph_u64)V31 << 32); \
    W2 = (sph_u64)V22 | ((sph_u64)V32 << 32); \
    W3 = (sph_u64)V23 | ((sph_u64)V33 << 32); \
    W4 = (sph_u64)V24 | ((sph_u64)V34 << 32); \
    W5 = (sph_u64)V25 | ((sph_u64)V35 << 32); \
    W6 = (sph_u64)V26 | ((sph_u64)V36 << 32); \
    W7 = (sph_u64)V27 | ((sph_u64)V37 << 32); \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMBW(W0, W1, W2, W3); \
      SUB_CRUMBW(W5, W6, W7, W4); \
      MIX_WORDW(W0, W4); \
      MIX_WORDW(W1, W5); \
      MIX_WORDW(W2, W6); \
      MIX_WORDW(W3, W7); \
      W0 ^= RCW230[r]; \
      W4 ^= RCW234[r]; \
    } \
    V20 = SPH_T32((sph_u32)W0); \
    V30 = SPH_T32((sph_u32)(W0 >> 32)); \
    V21 = SPH_T32((sph_u32)W1); \
    V31 = SPH_T32((sph_u32)(W1 >> 32)); \
    V22 = SPH_T32((sph_u32)W2); \
    V32 = SPH_T32((sph_u32)(W2 >> 32)); \
    V23 = SPH_T32((sph_u32)W3); \
    V33 = SPH_T32((sph_u32)(W3 >> 32)); \
    V24 = SPH_T32((sph_u32)W4); \
    V34 = SPH_T32((sph_u32)(W4 >> 32)); \
    V25 = SPH_T32((sph_u32)W5); \
    V35 = SPH_T32((sph_u32)(W5 >> 32)); \
    V26 = SPH_T32((sph_u32)W6); \
    V36 = SPH_T32((sph_u32)(W6 >> 32)); \
    V27 = SPH_T32((sph_u32)W7); \
    V37 = SPH_T32((sph_u32)(W7 >> 32)); \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMB(V40, V41, V42, V43); \
      SUB_CRUMB(V45, V46, V47, V44); \
      MIX_WORD(V40, V44); \
      MIX_WORD(V41, V45); \
      MIX_WORD(V42, V46); \
      MIX_WORD(V43, V47); \
      V40 ^= RC40[r]; \
      V44 ^= RC44[r]; \
    } \
  } while (0)

#else

#define LUFFA_P5   do { \
    int r; \
    TWEAK5; \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMB(V00, V01, V02, V03); \
      SUB_CRUMB(V05, V06, V07, V04); \
      MIX_WORD(V00, V04); \
      MIX_WORD(V01, V05); \
      MIX_WORD(V02, V06); \
      MIX_WORD(V03, V07); \
      V00 ^= RC00[r]; \
      V04 ^= RC04[r]; \
    } \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMB(V10, V11, V12, V13); \
      SUB_CRUMB(V15, V16, V17, V14); \
      MIX_WORD(V10, V14); \
      MIX_WORD(V11, V15); \
      MIX_WORD(V12, V16); \
      MIX_WORD(V13, V17); \
      V10 ^= RC10[r]; \
      V14 ^= RC14[r]; \
    } \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMB(V20, V21, V22, V23); \
      SUB_CRUMB(V25, V26, V27, V24); \
      MIX_WORD(V20, V24); \
      MIX_WORD(V21, V25); \
      MIX_WORD(V22, V26); \
      MIX_WORD(V23, V27); \
      V20 ^= RC20[r]; \
      V24 ^= RC24[r]; \
    } \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMB(V30, V31, V32, V33); \
      SUB_CRUMB(V35, V36, V37, V34); \
      MIX_WORD(V30, V34); \
      MIX_WORD(V31, V35); \
      MIX_WORD(V32, V36); \
      MIX_WORD(V33, V37); \
      V30 ^= RC30[r]; \
      V34 ^= RC34[r]; \
    } \
    for (r = 0; r < 8; r ++) { \
      SUB_CRUMB(V40, V41, V42, V43); \
      SUB_CRUMB(V45, V46, V47, V44); \
      MIX_WORD(V40, V44); \
      MIX_WORD(V41, V45); \
      MIX_WORD(V42, V46); \
      MIX_WORD(V43, V47); \
      V40 ^= RC40[r]; \
      V44 ^= RC44[r]; \
    } \
  } while (0)

#endif

