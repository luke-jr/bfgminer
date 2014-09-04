;/*
; * Copyright 2011 Neil Kettle
; * Copyright 2013 James Z.M. Gao
; * Copyright 2012-2013 Luke Dashjr
; *
; * This program is free software; you can redistribute it and/or modify it
; * under the terms of the GNU General Public License as published by the Free
; * Software Foundation; either version 3 of the License, or (at your option)
; * any later version.  See COPYING for more details.
; */

; %rbp, %rbx, and %r12-%r15 - callee save

ALIGN 32
BITS 64

%ifidn __OUTPUT_FORMAT__,win64
%define hash  rcx
%define hash1 rdx
%define data  r8
%define init  r9
%else
%define hash  rdi
%define hash1 rsi
%define data  rdx
%define init  rcx
%endif

; 0 = (1024 - 256) (mod (LAB_CALC_UNROLL*LAB_CALC_PARA*16))
%define SHA_CALC_W_PARA         2
%define SHA_CALC_W_UNROLL       8

%define SHA_ROUND_LOOP_UNROLL   16

extern sha256_consts_m128i
extern sha256_init_sse2

global sha256_sse2_64_new

%define sr1   xmm6
%define sr2   xmm1
%define sr3   xmm2
%define sr4   xmm13

%define rA    xmm7
%define rB    xmm5
%define rC    xmm4
%define rD    xmm3
%define rE    xmm0
%define rF    xmm8
%define rG    xmm9
%define rH    xmm10

%macro  sha_round_blk 0
    movdqa    sr1, [data+rax]                   ; T1  =                                             w;
    ;movdqa    sr1, xmm11
    movdqa    sr2, rE                           ; sr2 = rE

    pandn     sr2, rG                           ; sr2 = ~rE & rG
    movdqa    sr3, rF                           ; sr3 = rF

    paddd     sr1, rH                           ; T1  = h                + sha256_consts_m128i[i] + w;
    movdqa    rH, rG                            ; rH  = rG

    pand      sr3, rE                           ; sr3 = rE & rF
    movdqa    rG, rF                            ; rG  = rF

%ifidn __YASM_OBJFMT__, macho64
    paddd     sr1, [rcx+rax]
%else
    paddd     sr1, sha256_consts_m128i[rax]     ; T1  =                    sha256_consts_m128i[i] + w;
%endif
    pxor      sr2, sr3                          ; sr2 = (rE & rF) ^ (~rE & rG) = Ch (e, f, g)

    movdqa    rF, rE                            ; rF  = rE
    paddd     sr1, sr2                          ; T1  = h + Ch (e, f, g) + sha256_consts_m128i[i] + w;

    movdqa    sr2, rE                           ; sr2 = rE
    psrld     rE, 6                 ; e >> 6

    movdqa    sr3, rE               ; e >> 6
    pslld     sr2, 7                ; e << 7

    psrld     sr3, 5                ; e >> 11
    pxor      rE, sr2               ; e >> 6 ^ e << 7

    pslld     sr2, 14               ; e << 21
    pxor      rE, sr3               ; e >> 6 ^ e << 7 ^ e >> 11

    psrld     sr3, 14               ; e >> 25
    pxor      rE, sr2               ; e >> 6 ^ e << 7 ^ e >> 11 ^ e << 21

    pslld     sr2, 5                ; e << 26
    pxor      rE, sr3               ; e >> 6 ^ e << 7 ^ e >> 11 ^ e << 21 ^ e >> 25

    pxor      rE, sr2               ; e >> 6 ^ e << 7 ^ e >> 11 ^ e << 21 ^ e >> 25 ^ e << 26
    movdqa    sr2, rB                           ; sr2 = rB

    paddd     sr1, rE                           ; sr1 = h + BIGSIGMA1_256(e) + Ch (e, f, g) + sha256_consts_m128i[i] + w;
    movdqa    rE, rD                            ; rE  = rD

    movdqa    rD, rC                            ; rD  = rC
    paddd     rE, sr1                           ; rE  = rD + T1

    movdqa    sr3, rC                           ; sr3 = rC
    pand      rC, rA                            ; rC  = rC & rA

    pand      sr3, rB                           ; sr3 = rB & rC
    pand      sr2, rA                           ; sr2 = rB & rA

    pxor      sr2, rC                           ; sr2 = (rB & rA) ^ (rC & rA)
    movdqa    rC, rB                            ; rC  = rB

    pxor      sr2, sr3                          ; sr2 = (rB & rA) ^ (rC & rA) ^ (rB & rC)
    movdqa    rB, rA                            ; rB  = rA

    paddd     sr1, sr2                          ; sr1 = T1 + (rB & rA) ^ (rC & rA) ^ (rB & rC)
    lea       rax, [rax+16]

    movdqa    sr3, rA                           ; sr3 = rA
    psrld     rA, 2                 ; a >> 2

    pslld     sr3, 10               ; a << 10
    movdqa    sr2, rA               ; a >> 2

    pxor      rA, sr3               ; a >> 2 ^ a << 10
    psrld     sr2, 11               ; a >> 13

    pxor      rA, sr2               ; a >> 2 ^ a << 10 ^ a >> 13
    pslld     sr3, 9                ; a << 19

    pxor      rA, sr3               ; a >> 2 ^ a << 10 ^ a >> 13 ^ a << 19
    psrld     sr2, 9                ; a >> 21

    pxor      rA, sr2               ; a >> 2 ^ a << 10 ^ a >> 13 ^ a << 19 ^ a >> 21
    pslld     sr3, 11               ; a << 30

    pxor      rA, sr3               ; a >> 2 ^ a << 10 ^ a >> 13 ^ a << 19 ^ a >> 21 ^ a << 30
    paddd     rA, sr1                           ; T1 + BIGSIGMA0_256(a) + Maj(a, b, c);
%endmacro

%macro  sha_calc_w_blk 1
    movdqa	xmm0, [r11-(15-%1)*16]				; xmm0 = W[I-15]
    movdqa	xmm4, [r11-(15-(%1+1))*16]			; xmm4 = W[I-15+1]
    movdqa	xmm2, xmm0					; xmm2 = W[I-15]
    movdqa	xmm6, xmm4					; xmm6 = W[I-15+1]
    psrld	xmm0, 3						; xmm0 = W[I-15] >> 3
    psrld	xmm4, 3						; xmm4 = W[I-15+1] >> 3
    movdqa	xmm1, xmm0					; xmm1 = W[I-15] >> 3
    movdqa	xmm5, xmm4					; xmm5 = W[I-15+1] >> 3
    pslld	xmm2, 14					; xmm2 = W[I-15] << 14
    pslld	xmm6, 14					; xmm6 = W[I-15+1] << 14
    psrld	xmm1, 4						; xmm1 = W[I-15] >> 7
    psrld	xmm5, 4						; xmm5 = W[I-15+1] >> 7
    pxor	xmm0, xmm1					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7)
    pxor	xmm4, xmm5					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7)
    psrld	xmm1, 11					; xmm1 = W[I-15] >> 18
    psrld	xmm5, 11					; xmm5 = W[I-15+1] >> 18
    pxor	xmm0, xmm2					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7) ^ (W[I-15] << 14)
    pxor	xmm4, xmm6					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7) ^ (W[I-15+1] << 14)
    pslld	xmm2, 11					; xmm2 = W[I-15] << 25
    pslld	xmm6, 11					; xmm6 = W[I-15+1] << 25
    pxor	xmm0, xmm1					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7) ^ (W[I-15] << 14) ^ (W[I-15] >> 18)
    pxor	xmm4, xmm5					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7) ^ (W[I-15+1] << 14) ^ (W[I-15+1] >> 18)
    pxor	xmm0, xmm2					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7) ^ (W[I-15] << 14) ^ (W[I-15] >> 18) ^ (W[I-15] << 25)
    pxor	xmm4, xmm6					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7) ^ (W[I-15+1] << 14) ^ (W[I-15+1] >> 18) ^ (W[I-15+1] << 25)

    movdqa	xmm3, [r11-(2-%1)*16]				; xmm3 = W[I-2]
    movdqa	xmm7, [r11-(2-(%1+1))*16]			; xmm7 = W[I-2+1]

    paddd	xmm0, [r11-(16-%1)*16]				; xmm0 = s0(W[I-15]) + W[I-16]
    paddd	xmm4, [r11-(16-(%1+1))*16]			; xmm4 = s0(W[I-15+1]) + W[I-16+1]

;;;;;;;;;;;;;;;;;;

    movdqa	xmm2, xmm3					; xmm2 = W[I-2]
    movdqa	xmm6, xmm7					; xmm6 = W[I-2+1]
    psrld	xmm3, 10					; xmm3 = W[I-2] >> 10
    psrld	xmm7, 10					; xmm7 = W[I-2+1] >> 10
    movdqa	xmm1, xmm3					; xmm1 = W[I-2] >> 10
    movdqa	xmm5, xmm7					; xmm5 = W[I-2+1] >> 10

    paddd	xmm0, [r11-(7-%1)*16]				; xmm0 = s0(W[I-15]) + W[I-16] + W[I-7]

    pslld	xmm2, 13					; xmm2 = W[I-2] << 13
    pslld	xmm6, 13					; xmm6 = W[I-2+1] << 13
    psrld	xmm1, 7						; xmm1 = W[I-2] >> 17
    psrld	xmm5, 7						; xmm5 = W[I-2+1] >> 17

    paddd	xmm4, [r11-(7-(%1+1))*16]			; xmm4 = s0(W[I-15+1]) + W[I-16+1] + W[I-7+1]

    pxor	xmm3, xmm1					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17)
    pxor	xmm7, xmm5					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17)
    psrld	xmm1, 2						; xmm1 = W[I-2] >> 19
    psrld	xmm5, 2						; xmm5 = W[I-2+1] >> 19
    pxor	xmm3, xmm2					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17) ^ (W[I-2] << 13)
    pxor	xmm7, xmm6					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17) ^ (W[I-2+1] << 13)
    pslld	xmm2, 2						; xmm2 = W[I-2] << 15
    pslld	xmm6, 2						; xmm6 = W[I-2+1] << 15
    pxor	xmm3, xmm1					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17) ^ (W[I-2] << 13) ^ (W[I-2] >> 19)
    pxor	xmm7, xmm5					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17) ^ (W[I-2+1] << 13) ^ (W[I-2+1] >> 19)
    pxor	xmm3, xmm2					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17) ^ (W[I-2] << 13) ^ (W[I-2] >> 19) ^ (W[I-2] << 15)
    pxor	xmm7, xmm6					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17) ^ (W[I-2+1] << 13) ^ (W[I-2+1] >> 19) ^ (W[I-2+1] << 15)

    paddd	xmm0, xmm3					; xmm0 = s0(W[I-15]) + W[I-16] + s1(W[I-2]) + W[I-7]
    paddd	xmm4, xmm7					; xmm4 = s0(W[I-15+1]) + W[I-16+1] + s1(W[I-2+1]) + W[I-7+1]
    movdqa	[r11+(%1*16)], xmm0
    movdqa	[r11+((%1+1)*16)], xmm4
%endmacro

; _sha256_sse2_64_new hash(rdi), hash1(rsi), data(rdx), init(rcx),

sha256_sse2_64_new:

    push        rbx
%ifidn __OUTPUT_FORMAT__,win64
    sub         rsp, 16 * 6
    movdqa      [rsp + 16*0], xmm6
    movdqa      [rsp + 16*1], xmm7
    movdqa      [rsp + 16*2], xmm8
    movdqa      [rsp + 16*3], xmm9
    movdqa      [rsp + 16*4], xmm10
    movdqa      [rsp + 16*5], xmm13
%endif

%macro  SHA_256  0
    mov         rbx, 64*4   ; rbx is # of SHA-2 rounds
    mov         rax, 16*4   ; rax is where we expand to

    push        rbx
    lea         rbx, qword [data+rbx*4]
    lea         r11, qword [data+rax*4]

%%SHA_CALC_W:
%assign i 0
%rep    SHA_CALC_W_UNROLL
        sha_calc_w_blk i
%assign i i+SHA_CALC_W_PARA
%endrep
    add       r11, SHA_CALC_W_UNROLL*SHA_CALC_W_PARA*16
    cmp       r11, rbx
    jb        %%SHA_CALC_W

    pop       rbx
    mov       rax, 0
    lea       rbx, [rbx*4]

    movdqa    rA, [init]
    pshufd    rB, rA, 0x55          ; rB == B
    pshufd    rC, rA, 0xAA          ; rC == C
    pshufd    rD, rA, 0xFF          ; rD == D
    pshufd    rA, rA, 0             ; rA == A

    movdqa    rE, [init+4*4]
    pshufd    rF, rE, 0x55          ; rF == F
    pshufd    rG, rE, 0xAA          ; rG == G
    pshufd    rH, rE, 0xFF          ; rH == H
    pshufd    rE, rE, 0             ; rE == E

%ifidn __YASM_OBJFMT__, macho64
    lea       rcx, [sha256_consts_m128i wrt rip]
%endif

%%SHAROUND_LOOP:
%assign i 0
%rep    SHA_ROUND_LOOP_UNROLL
        sha_round_blk
%assign i i+1
%endrep
    cmp   rax, rbx
    jb    %%SHAROUND_LOOP

; Finished the 64 rounds, calculate hash and save

    movdqa    sr1, [init]
    pshufd    sr2, sr1, 0x55
    pshufd    sr3, sr1, 0xAA
    pshufd    sr4, sr1, 0xFF
    pshufd    sr1, sr1, 0

    paddd     rB, sr2
    paddd     rC, sr3
    paddd     rD, sr4
    paddd     rA, sr1

    movdqa    sr1, [init+4*4]
    pshufd    sr2, sr1, 0x55
    pshufd    sr3, sr1, 0xAA
    pshufd    sr4, sr1, 0xFF
    pshufd    sr1, sr1, 0

    paddd     rF, sr2
    paddd     rG, sr3
    paddd     rH, sr4
    paddd     rE, sr1
%endmacro

    SHA_256
    movdqa    [hash1+0*16], rA
    movdqa    [hash1+1*16], rB
    movdqa    [hash1+2*16], rC
    movdqa    [hash1+3*16], rD
    movdqa    [hash1+4*16], rE
    movdqa    [hash1+5*16], rF
    movdqa    [hash1+6*16], rG
    movdqa    [hash1+7*16], rH

    mov       data, hash1
    mov       init, qword sha256_init_sse2

    SHA_256

    movdqa    [hash+7*16], rH

LAB_RET:
%ifidn __OUTPUT_FORMAT__,win64
    movdqa    xmm6, [rsp + 16*0]
    movdqa    xmm7, [rsp + 16*1]
    movdqa    xmm8, [rsp + 16*2]
    movdqa    xmm9, [rsp + 16*3]
    movdqa    xmm10, [rsp + 16*4]
    movdqa    xmm13, [rsp + 16*5]
    add       rsp, 16 * 6
%endif
    pop       rbx
    ret

%ifidn __OUTPUT_FORMAT__,elf
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
%ifidn __OUTPUT_FORMAT__,elf64
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
