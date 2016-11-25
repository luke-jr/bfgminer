/* $Id: cubehash.c 227 2010-06-16 17:28:38Z tp $ */
/*
 * CubeHash implementation.
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
 * Some tests were conducted on an Intel Core2 Q6600 (32-bit and 64-bit
 * mode), a PowerPC G3, and a MIPS-compatible CPU (Broadcom BCM3302).
 * It appears that the optimal settings are:
 *  -- full unroll, no state copy on the "big" systems (x86, PowerPC)
 *  -- unroll to 4 or 8, state copy on the "small" system (MIPS)
 */

#if !defined SPH_CUBEHASH_UNROLL
#define SPH_CUBEHASH_UNROLL   0
#endif

__constant static const sph_u32 CUBEHASH_IV512[] = {
  SPH_C32(0x2AEA2A61), SPH_C32(0x50F494D4), SPH_C32(0x2D538B8B),
  SPH_C32(0x4167D83E), SPH_C32(0x3FEE2313), SPH_C32(0xC701CF8C),
  SPH_C32(0xCC39968E), SPH_C32(0x50AC5695), SPH_C32(0x4D42C787),
  SPH_C32(0xA647A8B3), SPH_C32(0x97CF0BEF), SPH_C32(0x825B4537),
  SPH_C32(0xEEF864D2), SPH_C32(0xF22090C4), SPH_C32(0xD0E5CD33),
  SPH_C32(0xA23911AE), SPH_C32(0xFCD398D9), SPH_C32(0x148FE485),
  SPH_C32(0x1B017BEF), SPH_C32(0xB6444532), SPH_C32(0x6A536159),
  SPH_C32(0x2FF5781C), SPH_C32(0x91FA7934), SPH_C32(0x0DBADEA9),
  SPH_C32(0xD65C8A2B), SPH_C32(0xA5A70E75), SPH_C32(0xB1C62456),
  SPH_C32(0xBC796576), SPH_C32(0x1921C8F7), SPH_C32(0xE7989AF1),
  SPH_C32(0x7795D246), SPH_C32(0xD43E3B44)
};

#define T32      SPH_T32
#define ROTL32   SPH_ROTL32

#define ROUND_EVEN   do { \
    xg = T32(x0 + xg); \
    x0 = ROTL32(x0, 7); \
    xh = T32(x1 + xh); \
    x1 = ROTL32(x1, 7); \
    xi = T32(x2 + xi); \
    x2 = ROTL32(x2, 7); \
    xj = T32(x3 + xj); \
    x3 = ROTL32(x3, 7); \
    xk = T32(x4 + xk); \
    x4 = ROTL32(x4, 7); \
    xl = T32(x5 + xl); \
    x5 = ROTL32(x5, 7); \
    xm = T32(x6 + xm); \
    x6 = ROTL32(x6, 7); \
    xn = T32(x7 + xn); \
    x7 = ROTL32(x7, 7); \
    xo = T32(x8 + xo); \
    x8 = ROTL32(x8, 7); \
    xp = T32(x9 + xp); \
    x9 = ROTL32(x9, 7); \
    xq = T32(xa + xq); \
    xa = ROTL32(xa, 7); \
    xr = T32(xb + xr); \
    xb = ROTL32(xb, 7); \
    xs = T32(xc + xs); \
    xc = ROTL32(xc, 7); \
    xt = T32(xd + xt); \
    xd = ROTL32(xd, 7); \
    xu = T32(xe + xu); \
    xe = ROTL32(xe, 7); \
    xv = T32(xf + xv); \
    xf = ROTL32(xf, 7); \
    x8 ^= xg; \
    x9 ^= xh; \
    xa ^= xi; \
    xb ^= xj; \
    xc ^= xk; \
    xd ^= xl; \
    xe ^= xm; \
    xf ^= xn; \
    x0 ^= xo; \
    x1 ^= xp; \
    x2 ^= xq; \
    x3 ^= xr; \
    x4 ^= xs; \
    x5 ^= xt; \
    x6 ^= xu; \
    x7 ^= xv; \
    xi = T32(x8 + xi); \
    x8 = ROTL32(x8, 11); \
    xj = T32(x9 + xj); \
    x9 = ROTL32(x9, 11); \
    xg = T32(xa + xg); \
    xa = ROTL32(xa, 11); \
    xh = T32(xb + xh); \
    xb = ROTL32(xb, 11); \
    xm = T32(xc + xm); \
    xc = ROTL32(xc, 11); \
    xn = T32(xd + xn); \
    xd = ROTL32(xd, 11); \
    xk = T32(xe + xk); \
    xe = ROTL32(xe, 11); \
    xl = T32(xf + xl); \
    xf = ROTL32(xf, 11); \
    xq = T32(x0 + xq); \
    x0 = ROTL32(x0, 11); \
    xr = T32(x1 + xr); \
    x1 = ROTL32(x1, 11); \
    xo = T32(x2 + xo); \
    x2 = ROTL32(x2, 11); \
    xp = T32(x3 + xp); \
    x3 = ROTL32(x3, 11); \
    xu = T32(x4 + xu); \
    x4 = ROTL32(x4, 11); \
    xv = T32(x5 + xv); \
    x5 = ROTL32(x5, 11); \
    xs = T32(x6 + xs); \
    x6 = ROTL32(x6, 11); \
    xt = T32(x7 + xt); \
    x7 = ROTL32(x7, 11); \
    xc ^= xi; \
    xd ^= xj; \
    xe ^= xg; \
    xf ^= xh; \
    x8 ^= xm; \
    x9 ^= xn; \
    xa ^= xk; \
    xb ^= xl; \
    x4 ^= xq; \
    x5 ^= xr; \
    x6 ^= xo; \
    x7 ^= xp; \
    x0 ^= xu; \
    x1 ^= xv; \
    x2 ^= xs; \
    x3 ^= xt; \
  } while (0)

#define ROUND_ODD   do { \
    xj = T32(xc + xj); \
    xc = ROTL32(xc, 7); \
    xi = T32(xd + xi); \
    xd = ROTL32(xd, 7); \
    xh = T32(xe + xh); \
    xe = ROTL32(xe, 7); \
    xg = T32(xf + xg); \
    xf = ROTL32(xf, 7); \
    xn = T32(x8 + xn); \
    x8 = ROTL32(x8, 7); \
    xm = T32(x9 + xm); \
    x9 = ROTL32(x9, 7); \
    xl = T32(xa + xl); \
    xa = ROTL32(xa, 7); \
    xk = T32(xb + xk); \
    xb = ROTL32(xb, 7); \
    xr = T32(x4 + xr); \
    x4 = ROTL32(x4, 7); \
    xq = T32(x5 + xq); \
    x5 = ROTL32(x5, 7); \
    xp = T32(x6 + xp); \
    x6 = ROTL32(x6, 7); \
    xo = T32(x7 + xo); \
    x7 = ROTL32(x7, 7); \
    xv = T32(x0 + xv); \
    x0 = ROTL32(x0, 7); \
    xu = T32(x1 + xu); \
    x1 = ROTL32(x1, 7); \
    xt = T32(x2 + xt); \
    x2 = ROTL32(x2, 7); \
    xs = T32(x3 + xs); \
    x3 = ROTL32(x3, 7); \
    x4 ^= xj; \
    x5 ^= xi; \
    x6 ^= xh; \
    x7 ^= xg; \
    x0 ^= xn; \
    x1 ^= xm; \
    x2 ^= xl; \
    x3 ^= xk; \
    xc ^= xr; \
    xd ^= xq; \
    xe ^= xp; \
    xf ^= xo; \
    x8 ^= xv; \
    x9 ^= xu; \
    xa ^= xt; \
    xb ^= xs; \
    xh = T32(x4 + xh); \
    x4 = ROTL32(x4, 11); \
    xg = T32(x5 + xg); \
    x5 = ROTL32(x5, 11); \
    xj = T32(x6 + xj); \
    x6 = ROTL32(x6, 11); \
    xi = T32(x7 + xi); \
    x7 = ROTL32(x7, 11); \
    xl = T32(x0 + xl); \
    x0 = ROTL32(x0, 11); \
    xk = T32(x1 + xk); \
    x1 = ROTL32(x1, 11); \
    xn = T32(x2 + xn); \
    x2 = ROTL32(x2, 11); \
    xm = T32(x3 + xm); \
    x3 = ROTL32(x3, 11); \
    xp = T32(xc + xp); \
    xc = ROTL32(xc, 11); \
    xo = T32(xd + xo); \
    xd = ROTL32(xd, 11); \
    xr = T32(xe + xr); \
    xe = ROTL32(xe, 11); \
    xq = T32(xf + xq); \
    xf = ROTL32(xf, 11); \
    xt = T32(x8 + xt); \
    x8 = ROTL32(x8, 11); \
    xs = T32(x9 + xs); \
    x9 = ROTL32(x9, 11); \
    xv = T32(xa + xv); \
    xa = ROTL32(xa, 11); \
    xu = T32(xb + xu); \
    xb = ROTL32(xb, 11); \
    x0 ^= xh; \
    x1 ^= xg; \
    x2 ^= xj; \
    x3 ^= xi; \
    x4 ^= xl; \
    x5 ^= xk; \
    x6 ^= xn; \
    x7 ^= xm; \
    x8 ^= xp; \
    x9 ^= xo; \
    xa ^= xr; \
    xb ^= xq; \
    xc ^= xt; \
    xd ^= xs; \
    xe ^= xv; \
    xf ^= xu; \
  } while (0)

/*
 * There is no need to unroll all 16 rounds. The word-swapping permutation
 * is an involution, so we need to unroll an even number of rounds. On
 * "big" systems, unrolling 4 rounds yields about 97% of the speed
 * achieved with full unrolling; and it keeps the code more compact
 * for small architectures.
 */

#if SPH_CUBEHASH_UNROLL == 2

#define SIXTEEN_ROUNDS   do { \
    int j; \
    for (j = 0; j < 8; j ++) { \
      ROUND_EVEN; \
      ROUND_ODD; \
    } \
  } while (0)

#elif SPH_CUBEHASH_UNROLL == 4

#define SIXTEEN_ROUNDS   do { \
    int j; \
    for (j = 0; j < 4; j ++) { \
      ROUND_EVEN; \
      ROUND_ODD; \
      ROUND_EVEN; \
      ROUND_ODD; \
    } \
  } while (0)

#elif SPH_CUBEHASH_UNROLL == 8

#define SIXTEEN_ROUNDS   do { \
    int j; \
    for (j = 0; j < 2; j ++) { \
      ROUND_EVEN; \
      ROUND_ODD; \
      ROUND_EVEN; \
      ROUND_ODD; \
      ROUND_EVEN; \
      ROUND_ODD; \
      ROUND_EVEN; \
      ROUND_ODD; \
    } \
  } while (0)

#else

#define SIXTEEN_ROUNDS   do { \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
    ROUND_EVEN; \
    ROUND_ODD; \
  } while (0)

#endif

