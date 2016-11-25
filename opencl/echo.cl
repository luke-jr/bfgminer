/* $Id: echo.c 227 2010-06-16 17:28:38Z tp $ */
/*
 * ECHO implementation.
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

#define T32   SPH_T32
#define C32   SPH_C32
#if SPH_64
#define C64   SPH_C64
#endif

#define AES_BIG_ENDIAN   0
#include "aes_helper.cl"

#define ECHO_DECL_STATE_BIG   \
  sph_u64 W00, W01, W10, W11, W20, W21, W30, W31, W40, W41, W50, W51, W60, W61, W70, W71, W80, W81, W90, W91, WA0, WA1, WB0, WB1, WC0, WC1, WD0, WD1, WE0, WE1, WF0, WF1;

#define AES_2ROUNDS(XX, XY)   do { \
    sph_u32 X0 = (sph_u32)(XX); \
    sph_u32 X1 = (sph_u32)(XX >> 32); \
    sph_u32 X2 = (sph_u32)(XY); \
    sph_u32 X3 = (sph_u32)(XY >> 32); \
    sph_u32 Y0, Y1, Y2, Y3; \
    AES_ROUND_LE(X0, X1, X2, X3, K0, K1, K2, K3, Y0, Y1, Y2, Y3); \
    AES_ROUND_NOKEY_LE(Y0, Y1, Y2, Y3, X0, X1, X2, X3); \
    XX = (sph_u64)X0 | ((sph_u64)X1 << 32); \
    XY = (sph_u64)X2 | ((sph_u64)X3 << 32); \
    if ((K0 = T32(K0 + 1)) == 0) { \
      if ((K1 = T32(K1 + 1)) == 0) \
        if ((K2 = T32(K2 + 1)) == 0) \
          K3 = T32(K3 + 1); \
    } \
  } while (0)

#define BIG_SUB_WORDS   do { \
    AES_2ROUNDS(W00, W01); \
    AES_2ROUNDS(W10, W11); \
    AES_2ROUNDS(W20, W21); \
    AES_2ROUNDS(W30, W31); \
    AES_2ROUNDS(W40, W41); \
    AES_2ROUNDS(W50, W51); \
    AES_2ROUNDS(W60, W61); \
    AES_2ROUNDS(W70, W71); \
    AES_2ROUNDS(W80, W81); \
    AES_2ROUNDS(W90, W91); \
    AES_2ROUNDS(WA0, WA1); \
    AES_2ROUNDS(WB0, WB1); \
    AES_2ROUNDS(WC0, WC1); \
    AES_2ROUNDS(WD0, WD1); \
    AES_2ROUNDS(WE0, WE1); \
    AES_2ROUNDS(WF0, WF1); \
  } while (0)

#define SHIFT_ROW1(a, b, c, d)   do { \
    sph_u64 tmp; \
    tmp = W ## a ## 0; \
    W ## a ## 0 = W ## b ## 0; \
    W ## b ## 0 = W ## c ## 0; \
    W ## c ## 0 = W ## d ## 0; \
    W ## d ## 0 = tmp; \
    tmp = W ## a ## 1; \
    W ## a ## 1 = W ## b ## 1; \
    W ## b ## 1 = W ## c ## 1; \
    W ## c ## 1 = W ## d ## 1; \
    W ## d ## 1 = tmp; \
  } while (0)

#define SHIFT_ROW2(a, b, c, d)   do { \
    sph_u64 tmp; \
    tmp = W ## a ## 0; \
    W ## a ## 0 = W ## c ## 0; \
    W ## c ## 0 = tmp; \
    tmp = W ## b ## 0; \
    W ## b ## 0 = W ## d ## 0; \
    W ## d ## 0 = tmp; \
    tmp = W ## a ## 1; \
    W ## a ## 1 = W ## c ## 1; \
    W ## c ## 1 = tmp; \
    tmp = W ## b ## 1; \
    W ## b ## 1 = W ## d ## 1; \
    W ## d ## 1 = tmp; \
  } while (0)

#define SHIFT_ROW3(a, b, c, d)   SHIFT_ROW1(d, c, b, a)

#define BIG_SHIFT_ROWS   do { \
    SHIFT_ROW1(1, 5, 9, D); \
    SHIFT_ROW2(2, 6, A, E); \
    SHIFT_ROW3(3, 7, B, F); \
  } while (0)

#define MIX_COLUMN1(ia, ib, ic, id, n)   do { \
    sph_u64 a = W ## ia ## n; \
    sph_u64 b = W ## ib ## n; \
    sph_u64 c = W ## ic ## n; \
    sph_u64 d = W ## id ## n; \
    sph_u64 ab = a ^ b; \
    sph_u64 bc = b ^ c; \
    sph_u64 cd = c ^ d; \
    sph_u64 abx = ((ab & C64(0x8080808080808080)) >> 7) * 27U \
      ^ ((ab & C64(0x7F7F7F7F7F7F7F7F)) << 1); \
    sph_u64 bcx = ((bc & C64(0x8080808080808080)) >> 7) * 27U \
      ^ ((bc & C64(0x7F7F7F7F7F7F7F7F)) << 1); \
    sph_u64 cdx = ((cd & C64(0x8080808080808080)) >> 7) * 27U \
      ^ ((cd & C64(0x7F7F7F7F7F7F7F7F)) << 1); \
    W ## ia ## n = abx ^ bc ^ d; \
    W ## ib ## n = bcx ^ a ^ cd; \
    W ## ic ## n = cdx ^ ab ^ d; \
    W ## id ## n = abx ^ bcx ^ cdx ^ ab ^ c; \
  } while (0)

#define MIX_COLUMN(a, b, c, d)   do { \
    MIX_COLUMN1(a, b, c, d, 0); \
    MIX_COLUMN1(a, b, c, d, 1); \
  } while (0)

#define BIG_MIX_COLUMNS   do { \
    MIX_COLUMN(0, 1, 2, 3); \
    MIX_COLUMN(4, 5, 6, 7); \
    MIX_COLUMN(8, 9, A, B); \
    MIX_COLUMN(C, D, E, F); \
  } while (0)

#define BIG_ROUND   do { \
    BIG_SUB_WORDS; \
    BIG_SHIFT_ROWS; \
    BIG_MIX_COLUMNS; \
  } while (0)

#define ECHO_COMPRESS_BIG(sc)   do { \
    sph_u32 K0 = sc->C0; \
    sph_u32 K1 = sc->C1; \
    sph_u32 K2 = sc->C2; \
    sph_u32 K3 = sc->C3; \
    unsigned u; \
    INPUT_BLOCK_BIG(sc); \
    for (u = 0; u < 10; u ++) { \
      BIG_ROUND; \
    } \
    ECHO_FINAL_BIG; \
  } while (0)

