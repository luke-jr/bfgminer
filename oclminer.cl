typedef uint z;

#define BITALIGN

#ifdef BITALIGN
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#define rotr(a, b) amd_bitalign((z)a, (z)a, (z)b)
#define Ch(a, b, c) amd_bytealign(a, b, c)
#define Ma(a, b, c) amd_bytealign((b), (a | c), (c & a))
#else
#define rotr(a, b) rotate((z)a, (z)(32 - b))
#define Ch(a, b, c) (c ^ (a & (b ^ c)))
#define Ma(a, b, c) ((b & c) | (a & (b | c)))
#endif

#define WGS __attribute__((reqd_work_group_size(128, 1, 1)))

__constant uint K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

typedef struct {
    uint ctx_a;
uint ctx_b;
uint ctx_c;
uint ctx_d;
    uint ctx_e;
uint ctx_f;
uint ctx_g;
uint ctx_h;
    uint cty_a;
uint cty_b;
uint cty_c;
uint cty_d;
    uint cty_e;
uint cty_f;
uint cty_g;
uint cty_h;
    uint merkle;
uint ntime;
uint nbits;
uint nonce;
    uint fW0;
uint fW1;
uint fW2;
uint fW3;
uint fW15;
    uint fW01r;
uint fcty_e;
uint fcty_e2;
} dev_blk_ctx;

__kernel __attribute__((vec_type_hint(uint))) WGS void oclminer(
	__constant dev_blk_ctx *ctx, __global uint *output)
{
  const uint fW0 = ctx->fW0;
  const uint fW1 = ctx->fW1;
  const uint fW2 = ctx->fW2;
  const uint fW3 = ctx->fW3;
  const uint fW15 = ctx->fW15;
  const uint fW01r = ctx->fW01r;
  const uint fcty_e = ctx->fcty_e;
  const uint fcty_e2 = ctx->fcty_e2;
  const uint state0 = ctx->ctx_a;
  const uint state1 = ctx->ctx_b;
  const uint state2 = ctx->ctx_c;
  const uint state3 = ctx->ctx_d;
  const uint state4 = ctx->ctx_e;
  const uint state5 = ctx->ctx_f;
  const uint state6 = ctx->ctx_g;
  const uint state7 = ctx->ctx_h;
  const uint B1 = ctx->cty_b;
  const uint C1 = ctx->cty_c;
  const uint D1 = ctx->cty_d;
  const uint F1 = ctx->cty_f;
  const uint G1 = ctx->cty_g;
  const uint H1 = ctx->cty_h;

  uint A, B, C, D, E, F, G, H;
  uint W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15;
  uint it;
  const uint myid = get_global_id(0);

  const uint tnonce = ctx->nonce + myid;

    W3 = 0 ^ tnonce;
    E = fcty_e +  W3;
A = state0 + E;
E = E + fcty_e2;
    D = D1 + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B1, C1) + K[ 4] +  0x80000000;
H = H1 + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F1) | (G1 & (E | F1)));
    C = C1 + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B1) + K[ 5];
G = G1 + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F1 & (D | E)));
    B = B1 + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[ 6];
F = F1 + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[ 7];
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[ 8];
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[ 9];
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[10];
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[11];
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[12];
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[13];
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[14];
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[15] + 0x00000280;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[16] + fW0;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[17] + fW1;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W2 = (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3)) + fW2;
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[18] +  W2;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W3 = W3 + fW3;
    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[19] +  W3;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W4 = (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10)) + 0x80000000;

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[20] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W5 = (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[21] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W6 = (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10)) + 0x00000280;

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[22] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W7 = (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10)) + fW0;

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[23] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W8 = (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10)) + fW1;

    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[24] +  W8;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W9 = W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10));

    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[25] +  W9;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W10 = W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10));

    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[26] + W10;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W11 = W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[27] + W11;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W12 = W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[28] + W12;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W13 = W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[29] + W13;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W14 = 0x00a00055 + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[30] + W14;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W15 = fW15 + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[31] + W15;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W0 = fW01r + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[32] +  W0;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W1 = fW1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[33] +  W1;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[34] +  W2;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[35] +  W3;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[36] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[37] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[38] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[39] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10));

    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[40] +  W8;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10));

    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[41] +  W9;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10));

    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[42] + W10;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[43] + W11;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[44] + W12;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W13 = W13 + (rotr(W14, 7) ^ rotr(W14, 18) ^ (W14 >> 3)) + W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[45] + W13;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W14 = W14 + (rotr(W15, 7) ^ rotr(W15, 18) ^ (W15 >> 3)) + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[46] + W14;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W15 = W15 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[47] + W15;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3)) + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[48] +  W0;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[49] +  W1;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[50] +  W2;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[51] +  W3;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[52] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[53] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[54] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[55] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10));

    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[56] +  W8;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10));

    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[57] +  W9;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10));

    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[58] + W10;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[59] + W11;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[60] + W12;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W13 = W13 + (rotr(W14, 7) ^ rotr(W14, 18) ^ (W14 >> 3)) + W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[61] + W13;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W14 = W14 + (rotr(W15, 7) ^ rotr(W15, 18) ^ (W15 >> 3)) + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[62] + W14;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W15 = W15 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[63] + W15;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    
    W0 = A + state0;
W1 = B + state1;
    W2 = C + state2;
W3 = D + state3;
    W4 = E + state4;
W5 = F + state5;
    W6 = G + state6;
W7 = H + state7;
    H = 0xb0edbdd0 + K[ 0] +  W0;
D = 0xa54ff53a + H;
H = H + 0x08909ae5;
    G = 0x1f83d9ab + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + (0x9b05688c ^ (D & 0xca0b3af3)) + K[ 1] +  W1;
C = 0x3c6ef372 + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & 0x6a09e667) | (0xbb67ae85 & (H | 0x6a09e667)));
    F = 0x9b05688c + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, 0x510e527f) + K[ 2] +  W2;
B = 0xbb67ae85 + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (0x6a09e667 & (G | H)));
    E = 0x510e527f + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[ 3] +  W3;
A = 0x6a09e667 + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[ 4] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[ 5] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[ 6] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[ 7] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[ 8] +  0x80000000;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[ 9];
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[10];
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[11];
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[12];
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[13];
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[14];
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[15] + 0x00000100;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[16] +  W0;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3)) + 0x00a00000;
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[17] +  W1;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3)) + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[18] +  W2;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3)) + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[19] +  W3;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3)) + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[20] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3)) + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[21] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3)) + 0x00000100 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[22] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W7 = W7 + 0x11002000 + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[23] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W8 = 0x80000000 + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10));

    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[24] +  W8;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W9 = W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10));

    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[25] +  W9;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W10 = W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10));

    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[26] + W10;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W11 = W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[27] + W11;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W12 = W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[28] + W12;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W13 = W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[29] + W13;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W14 = 0x00400022 + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[30] + W14;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W15 = 0x00000100 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[31] + W15;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3)) + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[32] +  W0;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[33] +  W1;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[34] +  W2;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[35] +  W3;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[36] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[37] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[38] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[39] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10));

    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[40] +  W8;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10));

    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[41] +  W9;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10));

    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[42] + W10;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[43] + W11;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[44] + W12;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W13 = W13 + (rotr(W14, 7) ^ rotr(W14, 18) ^ (W14 >> 3)) + W6 + (rotr(W11, 17) ^ rotr(W11, 19) ^ (W11 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[45] + W13;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W14 = W14 + (rotr(W15, 7) ^ rotr(W15, 18) ^ (W15 >> 3)) + W7 + (rotr(W12, 17) ^ rotr(W12, 19) ^ (W12 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[46] + W14;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W15 = W15 + (rotr(W0, 7) ^ rotr(W0, 18) ^ (W0 >> 3)) + W8 + (rotr(W13, 17) ^ rotr(W13, 19) ^ (W13 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[47] + W15;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W0 = W0 + (rotr(W1, 7) ^ rotr(W1, 18) ^ (W1 >> 3)) + W9 + (rotr(W14, 17) ^ rotr(W14, 19) ^ (W14 >> 10));
    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[48] +  W0;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W1 = W1 + (rotr(W2, 7) ^ rotr(W2, 18) ^ (W2 >> 3)) + W10 + (rotr(W15, 17) ^ rotr(W15, 19) ^ (W15 >> 10));
    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[49] +  W1;
C = C + G;
G = G + (rotr(H, 2) ^ rotr(H, 13) ^ rotr(H, 22)) + ((H & A) | (B & (H | A)));
    W2 = W2 + (rotr(W3, 7) ^ rotr(W3, 18) ^ (W3 >> 3)) + W11 + (rotr(W0, 17) ^ rotr(W0, 19) ^ (W0 >> 10));
    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[50] +  W2;
B = B + F;
F = F + (rotr(G, 2) ^ rotr(G, 13) ^ rotr(G, 22)) + ((G & H) | (A & (G | H)));
    W3 = W3 + (rotr(W4, 7) ^ rotr(W4, 18) ^ (W4 >> 3)) + W12 + (rotr(W1, 17) ^ rotr(W1, 19) ^ (W1 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[51] +  W3;
A = A + E;
E = E + (rotr(F, 2) ^ rotr(F, 13) ^ rotr(F, 22)) + ((F & G) | (H & (F | G)));
    W4 = W4 + (rotr(W5, 7) ^ rotr(W5, 18) ^ (W5 >> 3)) + W13 + (rotr(W2, 17) ^ rotr(W2, 19) ^ (W2 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[52] +  W4;
H = H + D;
D = D + (rotr(E, 2) ^ rotr(E, 13) ^ rotr(E, 22)) + ((E & F) | (G & (E | F)));
    W5 = W5 + (rotr(W6, 7) ^ rotr(W6, 18) ^ (W6 >> 3)) + W14 + (rotr(W3, 17) ^ rotr(W3, 19) ^ (W3 >> 10));

    C = C + (rotr(H, 6) ^ rotr(H, 11) ^ rotr(H, 25)) + Ch(H, A, B) + K[53] +  W5;
G = G + C;
C = C + (rotr(D, 2) ^ rotr(D, 13) ^ rotr(D, 22)) + ((D & E) | (F & (D | E)));
    W6 = W6 + (rotr(W7, 7) ^ rotr(W7, 18) ^ (W7 >> 3)) + W15 + (rotr(W4, 17) ^ rotr(W4, 19) ^ (W4 >> 10));

    B = B + (rotr(G, 6) ^ rotr(G, 11) ^ rotr(G, 25)) + Ch(G, H, A) + K[54] +  W6;
F = F + B;
B = B + (rotr(C, 2) ^ rotr(C, 13) ^ rotr(C, 22)) + ((C & D) | (E & (C | D)));
    W7 = W7 + (rotr(W8, 7) ^ rotr(W8, 18) ^ (W8 >> 3)) + W0 + (rotr(W5, 17) ^ rotr(W5, 19) ^ (W5 >> 10));

    A = A + (rotr(F, 6) ^ rotr(F, 11) ^ rotr(F, 25)) + Ch(F, G, H) + K[55] +  W7;
E = E + A;
A = A + (rotr(B, 2) ^ rotr(B, 13) ^ rotr(B, 22)) + ((B & C) | (D & (B | C)));
    W8 = W8 + (rotr(W9, 7) ^ rotr(W9, 18) ^ (W9 >> 3)) + W1 + (rotr(W6, 17) ^ rotr(W6, 19) ^ (W6 >> 10));

    H = H + (rotr(E, 6) ^ rotr(E, 11) ^ rotr(E, 25)) + Ch(E, F, G) + K[56] +  W8;
D = D + H;
H = H + (rotr(A, 2) ^ rotr(A, 13) ^ rotr(A, 22)) + ((A & B) | (C & (A | B)));
    W9 = W9 + (rotr(W10, 7) ^ rotr(W10, 18) ^ (W10 >> 3)) + W2 + (rotr(W7, 17) ^ rotr(W7, 19) ^ (W7 >> 10));

    G = G + (rotr(D, 6) ^ rotr(D, 11) ^ rotr(D, 25)) + Ch(D, E, F) + K[57] +  W9;
C = C + G;
    W10 = W10 + (rotr(W11, 7) ^ rotr(W11, 18) ^ (W11 >> 3)) + W3 + (rotr(W8, 17) ^ rotr(W8, 19) ^ (W8 >> 10));

    F = F + (rotr(C, 6) ^ rotr(C, 11) ^ rotr(C, 25)) + Ch(C, D, E) + K[58] + W10;
B = B + F;
    W11 = W11 + (rotr(W12, 7) ^ rotr(W12, 18) ^ (W12 >> 3)) + W4 + (rotr(W9, 17) ^ rotr(W9, 19) ^ (W9 >> 10));

    E = E + (rotr(B, 6) ^ rotr(B, 11) ^ rotr(B, 25)) + Ch(B, C, D) + K[59] + W11;
A = A + E;
    W12 = W12 + (rotr(W13, 7) ^ rotr(W13, 18) ^ (W13 >> 3)) + W5 + (rotr(W10, 17) ^ rotr(W10, 19) ^ (W10 >> 10));

    D = D + (rotr(A, 6) ^ rotr(A, 11) ^ rotr(A, 25)) + Ch(A, B, C) + K[60] + W12;
H = H + D;

	if (H==0xa41f32e7) {
		for (it = 0; it != 128; it++) {
			if (!output[it]) {
				output[it] = tnonce;
				break;
			}
		}
	}
}
