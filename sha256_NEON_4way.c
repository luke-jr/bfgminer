#include "config.h"
#include "driver-cpu.h"

#ifdef WANT_NEON_4WAY

#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __ARM_NEON__
#include <arm_neon.h>
#else
#error "NEON is desired but not enabled or found"
#endif
//typedef unsigned char byte;
#define NPAR 32


//#define unlikely(expr) __builtin_expect(!!(expr), 0)
//#define likely(expr) __builtin_expect(!!(expr), 1)

static void DoubleBlockSHA256(const void* pin, void* pout, const void* pinit, unsigned int hash[8][NPAR], const void* init2);

static const unsigned int K_base[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, /*  0 */
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, /*  8 */
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, /* 16 */
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, /* 24 */
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, /* 32 */
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, /* 40 */
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, /* 48 */
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, /* 56 */
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const unsigned int H_base[8] =
{  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 
   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};


/*-----------------------------------------------------------------------------------------------*/
   static inline uint32x4_t rotr_neon(uint32x4_t x, const int n)
   {
      return vorrq_u32(vshrq_n_u32(x,n) , vshlq_n_u32(x,32-n));
   }
/*-----------------------------------------------------------------------------------------------*/
   static inline uint32x4_t shr_neon(uint32x4_t x, const int n)
   {
         return vshrq_n_u32(x, n);
   }
/*-----------------------------------------------------------------------------------------------
   static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
{
   return (x & y) | (z & (x | y));
}
-----------------------------------------------------------------------------------------------*/
static inline uint32x4_t maj_neon(uint32x4_t x, uint32x4_t y, uint32x4_t z)
{
   return  vorrq_u32(vandq_u32(x, y), vandq_u32(z, vorrq_u32(x, y)));
}
/*-----------------------------------------------------------------------------------------------
static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
{
   return z ^ (x & (y ^ z));
}
/*-----------------------------------------------------------------------------------------------*/
static inline uint32x4_t ch_neon(uint32x4_t x, uint32x4_t y, uint32x4_t z)
{
 return veorq_u32(z,vandq_u32(x,veorq_u32(y,z)));
}
/*-----------------------------------------------------------------------------------------------
Sha256.Sigma0 = function(x) { return Sha256.ROTR(2,  x) ^ Sha256.ROTR(13, x) ^ Sha256.ROTR(22, x); }
/*-----------------------------------------------------------------------------------------------*/
static inline uint32x4_t E0_neon(uint32x4_t x)
{
   return veorq_u32(veorq_u32(rotr_neon(x,2),rotr_neon(x,13)),rotr_neon(x,22));
}
/*-----------------------------------------------------------------------------------------------
Sha256.Sigma1 = function(x) { return Sha256.ROTR(6,  x) ^ Sha256.ROTR(11, x) ^ Sha256.ROTR(25, x); }
/*-----------------------------------------------------------------------------------------------*/
static inline uint32x4_t E1_neon(uint32x4_t x)
{
   return veorq_u32(veorq_u32(rotr_neon(x,6),rotr_neon(x,12)),rotr_neon(x,25));
}
/*-----------------------------------------------------------------------------------------------
Sha256.sigma0 = function(x) { return Sha256.ROTR(7,  x) ^ Sha256.ROTR(18, x) ^ (x>>>3);  }
/*-----------------------------------------------------------------------------------------------*/
static inline uint32x4_t e0_neon(uint32x4_t x)
{
   return veorq_u32(veorq_u32(rotr_neon(x,7),rotr_neon(x,18)),shr_neon(x,3));
}
/*-----------------------------------------------------------------------------------------------
Sha256.sigma1 = function(x) { return Sha256.ROTR(17, x) ^ Sha256.ROTR(19, x) ^ (x>>>10); }
/*-----------------------------------------------------------------------------------------------*/
static inline uint32x4_t e1_neon(uint32x4_t x)
{
   return veorq_u32(veorq_u32(rotr_neon(x,17),rotr_neon(x,18)),shr_neon(x,10));
}

static inline uint32_t store32( const uint32x4_t x, int i)
{
  union {uint32_t ret[4]; uint32x4_t x;} box;
  box.x= x;
  return box.x[i];
}
static inline void store_epi32(const uint32x4_t x, unsigned int *x0, unsigned int *x1, unsigned int *x2, unsigned int *x3) {
    union { unsigned int ret[4]; uint32x4_t x; } box;
    box.x = x;
    *x0 = box.ret[3]; *x1 = box.ret[2]; *x2 = box.ret[1]; *x3 = box.ret[0];
}

static inline uint32x4_t add4(uint32x4_t x0,uint32x4_t x1,uint32x4_t x2,uint32x4_t x3){return vaddq_u32(vaddq_u32(x0,x1),vaddq_u32(x2,x3));}
static inline uint32x4_t add5(uint32x4_t x0,uint32x4_t x1,uint32x4_t x2,uint32x4_t x3,uint32x4_t x4){return vaddq_u32(add4(x0, x1, x2, x3),x4);}

static inline void SHA256ROUND(uint32x4_t a,uint32x4_t b,uint32x4_t c,uint32x4_t d,uint32x4_t e,uint32x4_t f,uint32x4_t g,uint32x4_t h,uint32_t i,uint32x4_t w){
 uint32x4_t T1 = add5(h,E1_neon(e), ch_neon(e,f,g), vdupq_n_u32(K_base[i]),w);
  d = vaddq_u32(d,T1);
  h = vaddq_u32(T1,vaddq_u32(E0_neon(a),maj_neon(a,b,c)));
}

  static inline void dumpreg(uint32x4_t x, char *msg) {
    union { unsigned int ret[4]; uint32x4_t x; } box;
    box.x = x ;
    printf("%s %08x %08x %08x %08x\n", msg, box.ret[0], box.ret[1], box.ret[2], box.ret[3]);
}

#if 1
#define dumpstate(i) printf("%s: %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", \
        __func__, store32(w0, i), store32(a, i), store32(b, i), store32(c, i), store32(d, i), store32(e, i), store32(f, i), store32(g, i), store32(h, i));
#else
#define dumpstate()
#endif




bool ScanHash_NEON_4way( struct thr_info*thr, const unsigned char *pmidstate,
   unsigned char *pdata,
   unsigned char *phash1, unsigned char *phash,
   const unsigned char *ptarget,
   uint32_t max_nonce, uint32_t *last_nonce,
   uint32_t nonce)
{
   uint32_t *hash32 = (uint32_t *)phash;
   unsigned int *nNoce_p = (unsigned int*)(pdata + 76);

   pdata +=64;

   for (;;)
   {
     uint32_t thash[9][NPAR] __attribute((aligned(128)));

     int j;

     *nNoce_p = nonce;

      DoubleBlockSHA256(pdata,phash1,pmidstate,thash,H_base);
      //32 round loop (nonce validation)
      for (j=0; j < NPAR; j++)
      {
        //run through each bit in the 7th byte is a 0 and therefore atleast a diff1?
        if (unlikely(thash[7][j] == 0))
        {
            int i;

            for (i = 0; i < 32/4; i++)
            {
                ((uint32_t*)phash)[i] = thash[i][j];            
            }

            if (unlikely(hash32[7] ==0 &&  fulltest(phash,ptarget)))
            {
              nonce +=j;
              *last_nonce = nonce;
              *nNoce_p = nonce;
              return true;
            }
        }
      }

      if ((nonce >= max_nonce)|| (thr->work_restart))
      {
       *last_nonce = nonce;
       return false;   
      }
      nonce+=NPAR;
   }

}

static void DoubleBlockSHA256(const void* pin, void* pad, const void *pre, unsigned int thash[9][NPAR], const void *init)
{
    unsigned int* In = (unsigned int*)pin;
    unsigned int* Pad = (unsigned int*)pad;
    unsigned int* hPre = (unsigned int*)pre;
    unsigned int* hInit = (unsigned int*)init;
    unsigned int /* i, j, */ k; 

    /* vectors used in calculation */
    uint32x4_t w0, w1, w2, w3, w4, w5, w6, w7;
    uint32x4_t w8, w9, w10, w11, w12, w13, w14, w15;
    uint32x4_t T1;
    uint32x4_t a, b, c, d, e, f, g, h;
    uint32x4_t nonce, preNonce;

    /* nonce offset for vector */
    uint32x4_t offset = vdupq_n_u32(0x00000000); //_mm_set_epi32(0x00000003, 0x00000002, 0x00000001, 0x00000000);
    offset = vsetq_lane_u32(0x00000003,offset,3);  
    offset = vsetq_lane_u32(0x00000002,offset,2); 
    offset = vsetq_lane_u32(0x00000001,offset,1);

    preNonce = vaddq_u32(vdupq_n_u32(In[3]),offset);    


    for(k = 0; k<NPAR; k+=4) {
        w0  = vdupq_n_u32(In[0]);
        w1  = vdupq_n_u32(In[1]);
        w2  = vdupq_n_u32(In[2]);
      //w3  = vdupq_n_u32(In[3]); nonce will be later hacked into the hash
        w4  = vdupq_n_u32(In[4]);
        w5  = vdupq_n_u32(In[5]);
        w6  = vdupq_n_u32(In[6]);
        w7  = vdupq_n_u32(In[7]);
        w8  = vdupq_n_u32(In[8]);
        w9  = vdupq_n_u32(In[9]);
        w10 = vdupq_n_u32(In[10]);
        w11 = vdupq_n_u32(In[11]);
        w12 = vdupq_n_u32(In[12]);
        w13 = vdupq_n_u32(In[13]);
        w14 = vdupq_n_u32(In[14]);
        w15 = vdupq_n_u32(In[15]);

        /* hack nonce into lowest byte of w3 */
        w3 = nonce = vaddq_u32(preNonce, vdupq_n_u32(k));

        a = vdupq_n_u32(hPre[0]);
        b = vdupq_n_u32(hPre[1]);
        c = vdupq_n_u32(hPre[2]);
        d = vdupq_n_u32(hPre[3]);
        e = vdupq_n_u32(hPre[4]);
        f = vdupq_n_u32(hPre[5]);
        g = vdupq_n_u32(hPre[6]);
        h = vdupq_n_u32(hPre[7]);

        SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
        SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
        SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
        SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
        SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
        SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
        SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
        SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
        SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
        SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
        SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
        SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
        SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
        SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
        SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
        SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

        w0 = add4(e1_neon(w14), w9, e0_neon(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
        w1 = add4(e1_neon(w15), w10, e0_neon(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
        w2 = add4(e1_neon(w0), w11, e0_neon(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
        w3 = add4(e1_neon(w1), w12, e0_neon(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
        w4 = add4(e1_neon(w2), w13, e0_neon(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
        w5 = add4(e1_neon(w3), w14, e0_neon(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
        w6 = add4(e1_neon(w4), w15, e0_neon(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
        w7 = add4(e1_neon(w5), w0, e0_neon(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
        w8 = add4(e1_neon(w6), w1, e0_neon(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
        w9 = add4(e1_neon(w7), w2, e0_neon(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
        w10 = add4(e1_neon(w8), w3, e0_neon(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
        w11 = add4(e1_neon(w9), w4, e0_neon(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
        w12 = add4(e1_neon(w10), w5, e0_neon(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
        w13 = add4(e1_neon(w11), w6, e0_neon(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
        w14 = add4(e1_neon(w12), w7, e0_neon(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
        w15 = add4(e1_neon(w13), w8, e0_neon(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

        w0 = add4(e1_neon(w14), w9, e0_neon(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
        w1 = add4(e1_neon(w15), w10, e0_neon(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
        w2 = add4(e1_neon(w0), w11, e0_neon(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
        w3 = add4(e1_neon(w1), w12, e0_neon(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
        w4 = add4(e1_neon(w2), w13, e0_neon(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
        w5 = add4(e1_neon(w3), w14, e0_neon(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
        w6 = add4(e1_neon(w4), w15, e0_neon(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
        w7 = add4(e1_neon(w5), w0, e0_neon(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
        w8 = add4(e1_neon(w6), w1, e0_neon(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
        w9 = add4(e1_neon(w7), w2, e0_neon(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
        w10 = add4(e1_neon(w8), w3, e0_neon(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
        w11 = add4(e1_neon(w9), w4, e0_neon(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
        w12 = add4(e1_neon(w10), w5, e0_neon(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
        w13 = add4(e1_neon(w11), w6, e0_neon(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
        w14 = add4(e1_neon(w12), w7, e0_neon(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
        w15 = add4(e1_neon(w13), w8, e0_neon(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

        w0 = add4(e1_neon(w14), w9, e0_neon(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
        w1 = add4(e1_neon(w15), w10, e0_neon(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
        w2 = add4(e1_neon(w0), w11, e0_neon(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
        w3 = add4(e1_neon(w1), w12, e0_neon(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
        w4 = add4(e1_neon(w2), w13, e0_neon(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
        w5 = add4(e1_neon(w3), w14, e0_neon(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
        w6 = add4(e1_neon(w4), w15, e0_neon(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
        w7 = add4(e1_neon(w5), w0, e0_neon(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
        w8 = add4(e1_neon(w6), w1, e0_neon(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
        w9 = add4(e1_neon(w7), w2, e0_neon(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
        w10 = add4(e1_neon(w8), w3, e0_neon(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
        w11 = add4(e1_neon(w9), w4, e0_neon(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
        w12 = add4(e1_neon(w10), w5, e0_neon(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);
        w13 = add4(e1_neon(w11), w6, e0_neon(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
        w14 = add4(e1_neon(w12), w7, e0_neon(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
        w15 = add4(e1_neon(w13), w8, e0_neon(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);

#define store_load(x, i, dest) \
        T1 = vdupq_n_u32((hPre)[i]); \
        dest = vaddq_u32(T1, x);

        w8  = vdupq_n_u32(Pad[8]);
        w9  = vdupq_n_u32(Pad[9]);
        w10 = vdupq_n_u32(Pad[10]);
        w11 = vdupq_n_u32(Pad[11]);
        w12 = vdupq_n_u32(Pad[12]);
        w13 = vdupq_n_u32(Pad[13]);
        w14 = vdupq_n_u32(Pad[14]);
        w15 = vdupq_n_u32(Pad[15]);

        a   = vdupq_n_u32(hInit[0]);
        b   = vdupq_n_u32(hInit[1]);
        c   = vdupq_n_u32(hInit[2]);
        d   = vdupq_n_u32(hInit[3]);
        e   = vdupq_n_u32(hInit[4]);
        f   = vdupq_n_u32(hInit[5]);
        g   = vdupq_n_u32(hInit[6]);
        h   = vdupq_n_u32(hInit[7]);

        SHA256ROUND(a, b, c, d, e, f, g, h, 0, w0);
        SHA256ROUND(h, a, b, c, d, e, f, g, 1, w1);
        SHA256ROUND(g, h, a, b, c, d, e, f, 2, w2);
        SHA256ROUND(f, g, h, a, b, c, d, e, 3, w3);
        SHA256ROUND(e, f, g, h, a, b, c, d, 4, w4);
        SHA256ROUND(d, e, f, g, h, a, b, c, 5, w5);
        SHA256ROUND(c, d, e, f, g, h, a, b, 6, w6);
        SHA256ROUND(b, c, d, e, f, g, h, a, 7, w7);
        SHA256ROUND(a, b, c, d, e, f, g, h, 8, w8);
        SHA256ROUND(h, a, b, c, d, e, f, g, 9, w9);
        SHA256ROUND(g, h, a, b, c, d, e, f, 10, w10);
        SHA256ROUND(f, g, h, a, b, c, d, e, 11, w11);
        SHA256ROUND(e, f, g, h, a, b, c, d, 12, w12);
        SHA256ROUND(d, e, f, g, h, a, b, c, 13, w13);
        SHA256ROUND(c, d, e, f, g, h, a, b, 14, w14);
        SHA256ROUND(b, c, d, e, f, g, h, a, 15, w15);

        w0 = add4(e1_neon(w14), w9, e0_neon(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 16, w0);
        w1 = add4(e1_neon(w15), w10, e0_neon(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 17, w1);
        w2 = add4(e1_neon(w0), w11, e0_neon(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 18, w2);
        w3 = add4(e1_neon(w1), w12, e0_neon(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 19, w3);
        w4 = add4(e1_neon(w2), w13, e0_neon(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 20, w4);
        w5 = add4(e1_neon(w3), w14, e0_neon(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 21, w5);
        w6 = add4(e1_neon(w4), w15, e0_neon(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 22, w6);
        w7 = add4(e1_neon(w5), w0, e0_neon(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 23, w7);
        w8 = add4(e1_neon(w6), w1, e0_neon(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 24, w8);
        w9 = add4(e1_neon(w7), w2, e0_neon(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 25, w9);
        w10 = add4(e1_neon(w8), w3, e0_neon(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 26, w10);
        w11 = add4(e1_neon(w9), w4, e0_neon(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 27, w11);
        w12 = add4(e1_neon(w10), w5, e0_neon(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 28, w12);
        w13 = add4(e1_neon(w11), w6, e0_neon(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 29, w13);
        w14 = add4(e1_neon(w12), w7, e0_neon(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 30, w14);
        w15 = add4(e1_neon(w13), w8, e0_neon(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 31, w15);

        w0 = add4(e1_neon(w14), w9, e0_neon(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 32, w0);
        w1 = add4(e1_neon(w15), w10, e0_neon(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 33, w1);
        w2 = add4(e1_neon(w0), w11, e0_neon(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 34, w2);
        w3 = add4(e1_neon(w1), w12, e0_neon(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 35, w3);
        w4 = add4(e1_neon(w2), w13, e0_neon(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 36, w4);
        w5 = add4(e1_neon(w3), w14, e0_neon(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 37, w5);
        w6 = add4(e1_neon(w4), w15, e0_neon(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 38, w6);
        w7 = add4(e1_neon(w5), w0, e0_neon(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 39, w7);
        w8 = add4(e1_neon(w6), w1, e0_neon(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 40, w8);
        w9 = add4(e1_neon(w7), w2, e0_neon(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 41, w9);
        w10 = add4(e1_neon(w8), w3, e0_neon(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 42, w10);
        w11 = add4(e1_neon(w9), w4, e0_neon(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 43, w11);
        w12 = add4(e1_neon(w10), w5, e0_neon(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 44, w12);
        w13 = add4(e1_neon(w11), w6, e0_neon(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 45, w13);
        w14 = add4(e1_neon(w12), w7, e0_neon(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 46, w14);
        w15 = add4(e1_neon(w13), w8, e0_neon(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 47, w15);

        w0 = add4(e1_neon(w14), w9, e0_neon(w1), w0);
        SHA256ROUND(a, b, c, d, e, f, g, h, 48, w0);
        w1 = add4(e1_neon(w15), w10, e0_neon(w2), w1);
        SHA256ROUND(h, a, b, c, d, e, f, g, 49, w1);
        w2 = add4(e1_neon(w0), w11, e0_neon(w3), w2);
        SHA256ROUND(g, h, a, b, c, d, e, f, 50, w2);
        w3 = add4(e1_neon(w1), w12, e0_neon(w4), w3);
        SHA256ROUND(f, g, h, a, b, c, d, e, 51, w3);
        w4 = add4(e1_neon(w2), w13, e0_neon(w5), w4);
        SHA256ROUND(e, f, g, h, a, b, c, d, 52, w4);
        w5 = add4(e1_neon(w3), w14, e0_neon(w6), w5);
        SHA256ROUND(d, e, f, g, h, a, b, c, 53, w5);
        w6 = add4(e1_neon(w4), w15, e0_neon(w7), w6);
        SHA256ROUND(c, d, e, f, g, h, a, b, 54, w6);
        w7 = add4(e1_neon(w5), w0, e0_neon(w8), w7);
        SHA256ROUND(b, c, d, e, f, g, h, a, 55, w7);
        w8 = add4(e1_neon(w6), w1, e0_neon(w9), w8);
        SHA256ROUND(a, b, c, d, e, f, g, h, 56, w8);
        w9 = add4(e1_neon(w7), w2, e0_neon(w10), w9);
        SHA256ROUND(h, a, b, c, d, e, f, g, 57, w9);
        w10 = add4(e1_neon(w8), w3, e0_neon(w11), w10);
        SHA256ROUND(g, h, a, b, c, d, e, f, 58, w10);
        w11 = add4(e1_neon(w9), w4, e0_neon(w12), w11);
        SHA256ROUND(f, g, h, a, b, c, d, e, 59, w11);
        w12 = add4(e1_neon(w10), w5, e0_neon(w13), w12);
        SHA256ROUND(e, f, g, h, a, b, c, d, 60, w12);

  /* Skip last 3-rounds; not necessary for H==0 */
#if 0
        w13 = add4(e1_neon(w11), w6, e0_neon(w14), w13);
        SHA256ROUND(d, e, f, g, h, a, b, c, 61, w13);
        w14 = add4(e1_neon(w12), w7, e0_neon(w15), w14);
        SHA256ROUND(c, d, e, f, g, h, a, b, 62, w14);
        w15 = add4(e1_neon(w13), w8, e0_neon(w0), w15);
        SHA256ROUND(b, c, d, e, f, g, h, a, 63, w15);
#endif


#define store_2(x,i)  \
        w0 = vdupq_n_u32(hInit[i]); \
        *(uint32x4_t *)&(thash)[i][0+k] = vaddq_u32(w0, x);

        store_2(a, 0);
        store_2(b, 1);
        store_2(c, 2);
        store_2(d, 3);
        store_2(e, 4);
        store_2(f, 5);
        store_2(g, 6);
        store_2(h, 7);
        *(uint32x4_t *)&(thash)[8][0+k] = nonce;
      }
    }

/*-----------------------------------------------------------------------------------------------*/
#endif
