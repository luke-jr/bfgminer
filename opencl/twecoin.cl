#if __ENDIAN_LITTLE__
#define SPH_LITTLE_ENDIAN 1
#else
#define SPH_BIG_ENDIAN 1
#endif

#define SPH_UPTR sph_u64

typedef unsigned int sph_u32;
typedef int sph_s32;
#ifndef __OPENCL_VERSION__
typedef unsigned long long sph_u64;
typedef long long sph_s64;
#else
typedef unsigned long sph_u64;
typedef long sph_s64;
#endif

#define SPH_64 1
#define SPH_64_TRUE 1

#define SPH_C32(x)    ((sph_u32)(x ## U))
#define SPH_T32(x) (as_uint(x))
#define SPH_ROTL32(x, n) rotate(as_uint(x), as_uint(n))
#define SPH_ROTR32(x, n)   SPH_ROTL32(x, (32 - (n)))

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x) (as_ulong(x))
#define SPH_ROTL64(x, n) rotate(as_ulong(x), (n) & 0xFFFFFFFFFFFFFFFFUL)
#define SPH_ROTR64(x, n)   SPH_ROTL64(x, (64 - (n)))

#define SPH_HAMSI_EXPAND_SMALL 1

#include "fugue.cl"
#include "shavite.cl"
#include "hamsi.cl"
#include "panama.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC32BE(x) (*(const __global sph_u32 *) (x))
#else
  #define DEC32BE(x) SWAP4(*(const __global sph_u32 *) (x))
#endif

#define sph_bswap32(x) SWAP4(x)

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

#define CONST_EXP2  q[i+0] + SPH_ROTL64(q[i+1], 5)  + q[i+2] + SPH_ROTL64(q[i+3], 11) + \
                    q[i+4] + SPH_ROTL64(q[i+5], 27) + q[i+6] + SPH_ROTL64(q[i+7], 32) + \
                    q[i+8] + SPH_ROTL64(q[i+9], 37) + q[i+10] + SPH_ROTL64(q[i+11], 43) + \
                    q[i+12] + SPH_ROTL64(q[i+13], 53) + (SHR(q[i+14],1) ^ q[i+14]) + (SHR(q[i+15],2) ^ q[i+15])

static void sph_enc32be(void *dst, sph_u32 val)
{
#if defined SPH_UPTR
#if SPH_UNALIGNED
#if SPH_LITTLE_ENDIAN
  val = sph_bswap32(val);
#endif
  *(sph_u32 *)dst = val;
#else
  if (((SPH_UPTR)dst & 3) == 0) {
#if SPH_LITTLE_ENDIAN
    val = sph_bswap32(val);
#endif
    *(sph_u32 *)dst = val;
  } else {
    ((unsigned char *)dst)[0] = (val >> 24);
    ((unsigned char *)dst)[1] = (val >> 16);
    ((unsigned char *)dst)[2] = (val >> 8);
    ((unsigned char *)dst)[3] = val;
  }
#endif
#else
  ((unsigned char *)dst)[0] = (val >> 24);
  ((unsigned char *)dst)[1] = (val >> 16);
  ((unsigned char *)dst)[2] = (val >> 8);
  ((unsigned char *)dst)[3] = val;
#endif
}

static void sph_enc32le(void *dst, sph_u32 val)
{
#if defined SPH_UPTR
#if SPH_UNALIGNED
#if SPH_BIG_ENDIAN
  val = sph_bswap32(val);
#endif
  *(sph_u32 *)dst = val;
#else
  if (((SPH_UPTR)dst & 3) == 0) {
#if SPH_BIG_ENDIAN
    val = sph_bswap32(val);
#endif
    *(sph_u32 *)dst = val;
  } else {
    ((unsigned char *)dst)[0] = val;
    ((unsigned char *)dst)[1] = (val >> 8);
    ((unsigned char *)dst)[2] = (val >> 16);
    ((unsigned char *)dst)[3] = (val >> 24);
  }
#endif
#else
  ((unsigned char *)dst)[0] = val;
  ((unsigned char *)dst)[1] = (val >> 8);
  ((unsigned char *)dst)[2] = (val >> 16);
  ((unsigned char *)dst)[3] = (val >> 24);
#endif
}

static sph_u32 sph_dec32le_aligned(const void *src)
{
#if SPH_LITTLE_ENDIAN
  return *(const sph_u32 *)src;
#elif SPH_BIG_ENDIAN
  return sph_bswap32(*(const sph_u32 *)src);
#else
  return (sph_u32)(((const unsigned char *)src)[0])
      | ((sph_u32)(((const unsigned char *)src)[1]) << 8)
      | ((sph_u32)(((const unsigned char *)src)[2]) << 16)
      | ((sph_u32)(((const unsigned char *)src)[3]) << 24);
#endif
}


__kernel void search(__global unsigned char* block, volatile __global uint* output, const ulong target)
{
  __local sph_u32 AES0[256], AES1[256], AES2[256], AES3[256];
  int init = get_local_id(0);
  int step = get_local_size(0);
  for (int i = init; i < 256; i += step)
  {
    AES0[i] = AES0_C[i];
    AES1[i] = AES1_C[i];
    AES2[i] = AES2_C[i];
    AES3[i] = AES3_C[i];
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  unsigned char hash[64];
  for(unsigned j = 0; j < 64; j++)
    hash[j] = 0;

  sph_u32 gid = get_global_id(0);

  // fugue
  {
    sph_u32 S00 = 0, S01 = 0, S02 = 0, S03 = 0, S04 = 0, S05 = 0, S06 = 0, S07 = 0, S08 = 0, S09 = 0; \
    sph_u32 S10 = 0, S11 = 0, S12 = 0, S13 = 0, S14 = 0, S15 = 0, S16 = 0, S17 = 0, S18 = 0, S19 = 0; \
    sph_u32 S20 = 0, S21 = 0, S22 = IV256[0], S23 = IV256[1], S24 = IV256[2], S25 = IV256[3], S26 = IV256[4], S27 = IV256[5], S28 = IV256[6], S29 = IV256[7];

    FUGUE256_5(DEC32BE(block + 0x0), DEC32BE(block + 0x4), DEC32BE(block + 0x8), DEC32BE(block + 0xc), DEC32BE(block + 0x10));
    FUGUE256_5(DEC32BE(block + 0x14), DEC32BE(block + 0x18), DEC32BE(block + 0x1c), DEC32BE(block + 0x20), DEC32BE(block + 0x24));
    FUGUE256_5(DEC32BE(block + 0x28), DEC32BE(block + 0x2c), DEC32BE(block + 0x30), DEC32BE(block + 0x34), DEC32BE(block + 0x38));
    FUGUE256_4(DEC32BE(block + 0x3c), DEC32BE(block + 0x40), DEC32BE(block + 0x44), DEC32BE(block + 0x48));

    TIX2(SWAP4(gid), S06, S07, S14, S16, S00);
    CMIX30(S03, S04, S05, S07, S08, S09, S18, S19, S20);
    SMIX(S03, S04, S05, S06);
    CMIX30(S00, S01, S02, S04, S05, S06, S15, S16, S17);
    SMIX(S00, S01, S02, S03);

    TIX2(0, S00, S01, S08, S10, S24);
    CMIX30(S27, S28, S29, S01, S02, S03, S12, S13, S14);
    SMIX(S27, S28, S29, S00);
    CMIX30(S24, S25, S26, S28, S29, S00, S09, S10, S11);
    SMIX(S24, S25, S26, S27);

    TIX2(0x280, S24, S25, S02, S04, S18);
    CMIX30(S21, S22, S23, S25, S26, S27, S06, S07, S08);
    SMIX(S21, S22, S23, S24);
    CMIX30(S18, S19, S20, S22, S23, S24, S03, S04, S05);
    SMIX(S18, S19, S20, S21);

    CMIX30(S15, S16, S17, S19, S20, S21, S00, S01, S02);
    SMIX(S15, S16, S17, S18);
    CMIX30(S12, S13, S14, S16, S17, S18, S27, S28, S29);
    SMIX(S12, S13, S14, S15);
    CMIX30(S09, S10, S11, S13, S14, S15, S24, S25, S26);
    SMIX(S09, S10, S11, S12);
    CMIX30(S06, S07, S08, S10, S11, S12, S21, S22, S23);
    SMIX(S06, S07, S08, S09);
    CMIX30(S03, S04, S05, S07, S08, S09, S18, S19, S20);
    SMIX(S03, S04, S05, S06);
    CMIX30(S00, S01, S02, S04, S05, S06, S15, S16, S17);
    SMIX(S00, S01, S02, S03);
    CMIX30(S27, S28, S29, S01, S02, S03, S12, S13, S14);
    SMIX(S27, S28, S29, S00);
    CMIX30(S24, S25, S26, S28, S29, S00, S09, S10, S11);
    SMIX(S24, S25, S26, S27);
    CMIX30(S21, S22, S23, S25, S26, S27, S06, S07, S08);
    SMIX(S21, S22, S23, S24);
    CMIX30(S18, S19, S20, S22, S23, S24, S03, S04, S05);
    SMIX(S18, S19, S20, S21);
    S22 ^= S18;
    S03 ^= S18;
    SMIX(S03, S04, S05, S06);
    S07 ^= S03;
    S19 ^= S03;
    SMIX(S19, S20, S21, S22);
    S23 ^= S19;
    S04 ^= S19;
    SMIX(S04, S05, S06, S07);
    S08 ^= S04;
    S20 ^= S04;
    SMIX(S20, S21, S22, S23);
    S24 ^= S20;
    S05 ^= S20;
    SMIX(S05, S06, S07, S08);
    S09 ^= S05;
    S21 ^= S05;
    SMIX(S21, S22, S23, S24);
    S25 ^= S21;
    S06 ^= S21;
    SMIX(S06, S07, S08, S09);
    S10 ^= S06;
    S22 ^= S06;
    SMIX(S22, S23, S24, S25);
    S26 ^= S22;
    S07 ^= S22;
    SMIX(S07, S08, S09, S10);
    S11 ^= S07;
    S23 ^= S07;
    SMIX(S23, S24, S25, S26);
    S27 ^= S23;
    S08 ^= S23;
    SMIX(S08, S09, S10, S11);
    S12 ^= S08;
    S24 ^= S08;
    SMIX(S24, S25, S26, S27);
    S28 ^= S24;
    S09 ^= S24;
    SMIX(S09, S10, S11, S12);
    S13 ^= S09;
    S25 ^= S09;
    SMIX(S25, S26, S27, S28);
    S29 ^= S25;
    S10 ^= S25;
    SMIX(S10, S11, S12, S13);
    S14 ^= S10;
    S26 ^= S10;
    SMIX(S26, S27, S28, S29);
    S00 ^= S26;
    S11 ^= S26;
    SMIX(S11, S12, S13, S14);
    S15 ^= S11;
    S27 ^= S11;
    SMIX(S27, S28, S29, S00);
    S01 ^= S27;
    S12 ^= S27;
    SMIX(S12, S13, S14, S15);
    S16 ^= S12;
    S28 ^= S12;
    SMIX(S28, S29, S00, S01);
    S02 ^= S28;
    S13 ^= S28;
    SMIX(S13, S14, S15, S16);
    S17 ^= S13;
    S29 ^= S13;
    SMIX(S29, S00, S01, S02);
    S03 ^= S29;
    S14 ^= S29;
    SMIX(S14, S15, S16, S17);
    S18 ^= S14;
    S00 ^= S14;
    SMIX(S00, S01, S02, S03);
    S04 ^= S00;
    S15 ^= S00;
    SMIX(S15, S16, S17, S18);
    S19 ^= S15;
    S01 ^= S15;
    SMIX(S01, S02, S03, S04);

    S05 ^= S01;
    S16 ^= S01;

    sph_enc32be((unsigned char*) &hash +  0, S02);
    sph_enc32be((unsigned char*) &hash +  4, S03);
    sph_enc32be((unsigned char*) &hash +  8, S04);
    sph_enc32be((unsigned char*) &hash + 12, S05);
    sph_enc32be((unsigned char*) &hash + 16, S16);
    sph_enc32be((unsigned char*) &hash + 20, S17);
    sph_enc32be((unsigned char*) &hash + 24, S18);
    sph_enc32be((unsigned char*) &hash + 28, S19);
  }

  // shavite
  {
    sph_u32 h[] = { SPH_C32(0x49BB3E47), SPH_C32(0x2674860D), SPH_C32(0xA8B392AC), SPH_C32(0x021AC4E6), SPH_C32(0x409283CF), SPH_C32(0x620E5D86), SPH_C32(0x6D929DCB), SPH_C32(0x96CC2A8B) };
    sph_u32 rk0, rk1, rk2, rk3, rk4, rk5, rk6, rk7;
    sph_u32 rk8, rk9, rkA, rkB, rkC, rkD, rkE, rkF;
    sph_u32 count0, count1;

    rk0 = sph_dec32le_aligned((const unsigned char *)&hash +  0);
    rk1 = sph_dec32le_aligned((const unsigned char *)&hash +  4);
    rk2 = sph_dec32le_aligned((const unsigned char *)&hash +  8);
    rk3 = sph_dec32le_aligned((const unsigned char *)&hash + 12);
    rk4 = sph_dec32le_aligned((const unsigned char *)&hash + 16);
    rk5 = sph_dec32le_aligned((const unsigned char *)&hash + 20);
    rk6 = sph_dec32le_aligned((const unsigned char *)&hash + 24);
    rk7 = sph_dec32le_aligned((const unsigned char *)&hash + 28);
    rk8 = sph_dec32le_aligned((const unsigned char *)&hash + 32);
    rk9 = sph_dec32le_aligned((const unsigned char *)&hash + 36);
    rkA = sph_dec32le_aligned((const unsigned char *)&hash + 40);
    rkB = sph_dec32le_aligned((const unsigned char *)&hash + 44);
    rkC = sph_dec32le_aligned((const unsigned char *)&hash + 48);
    rkD = sph_dec32le_aligned((const unsigned char *)&hash + 52);
    rkE = sph_dec32le_aligned((const unsigned char *)&hash + 56);
    rkF = sph_dec32le_aligned((const unsigned char *)&hash + 60);
    count0 = 0x200;
    count1 = 0;
    c256(buf);

    rk0 = 0x80;
    rk1 = 0;
    rk2 = 0;
    rk3 = 0;
    rk4 = 0;
    rk5 = 0;
    rk6 = 0;
    rk7 = 0;
    rk8 = 0;
    rk9 = 0;
    rkA = 0;
    rkB = 0;
    rkC = 0;
    rkD = 0x2000000;
    rkE = 0;
    rkF = 0x1000000;
    count0 = 0;
    count1 = 0;
    c256(buf);

    for (unsigned u = 0; u < 8; u ++)
      sph_enc32le((unsigned char *)&hash + (u << 2), h[u]);
  }

  // hamsi
  {
    sph_u32 c0 = HAMSI_IV256[0], c1 = HAMSI_IV256[1], c2 = HAMSI_IV256[2], c3 = HAMSI_IV256[3];
    sph_u32 c4 = HAMSI_IV256[4], c5 = HAMSI_IV256[5], c6 = HAMSI_IV256[6], c7 = HAMSI_IV256[7];
    sph_u32 m0, m1, m2, m3, m4, m5, m6, m7;
    sph_u32 h[8] = { c0, c1, c2, c3, c4, c5, c6, c7 };

#define buf(u) hash[i + u]
    for(int i = 0; i < 64; i += 4) {
      INPUT_SMALL;
      P_SMALL;
      T_SMALL;
    }
#undef buf
#define buf(u) (u == 0 ? 0x80 : 0)
    INPUT_SMALL;
    P_SMALL;
    T_SMALL;
#undef buf
#define buf(u) 0
    INPUT_SMALL;
    P_SMALL;
    T_SMALL;
#undef buf
#define buf(u) (u == 2 ? 2 : 0)
    INPUT_SMALL;
    PF_SMALL;
    T_SMALL;

    for (unsigned u = 0; u < 8; u ++)
      sph_enc32be((unsigned char*) &hash + (u << 2), h[u]);
  }

  // panama
  {
    sph_u32 buffer[32][8];
    sph_u32 state[17];
    int i, j;
    for(i = 0; i < 32; i++)
        for(j = 0; j < 8; j++)
            buffer[i][j] = 0;
    for(i = 0; i < 17; i++)
        state[i] = 0;

    LVARS
    unsigned ptr0 = 0;
#define INW1(i)   sph_dec32le_aligned((unsigned char*) &hash + 4 * (i))
#define INW2(i)   INW1(i)

    M17(RSTATE);
    PANAMA_STEP;

#undef INW1
#undef INW2
#define INW1(i)   sph_dec32le_aligned((unsigned char*) &hash + 32 + 4 * (i))
#define INW2(i)   INW1(i)
    PANAMA_STEP;
    M17(WSTATE);

#undef INW1
#undef INW2

#define INW1(i)   (sph_u32) (i == 0)
#define INW2(i)   INW1(i)

    M17(RSTATE);
    PANAMA_STEP;
    M17(WSTATE);

#undef INW1
#undef INW2

#define INW1(i)     INW_H1(INC ## i)
#define INW_H1(i)   INW_H2(i)
#define INW_H2(i)   a ## i
#define INW2(i)     buffer[ptr4][i]

    M17(RSTATE);
    for(i = 0; i < 32; i++) {
        unsigned ptr4 = (ptr0 + 4) & 31;
        PANAMA_STEP;
    }
    M17(WSTATE);

#undef INW1
#undef INW_H1
#undef INW_H2
#undef INW2

    bool result = ((((sph_u64) state[16] << 32) | state[15]) <= target);
    if (result)
      output[output[0xFF]++] = SWAP4(gid);
  }
}