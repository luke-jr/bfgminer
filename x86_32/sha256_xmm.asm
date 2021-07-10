;/*
; * Copyright 2011 Ufasoft
; * Copyright 2012 Guido Ascioti <guido.ascioti@gmail.com>
; * Copyright 2012 Luke Dashjr
; *
; * This program is free software; you can redistribute it and/or modify it
; * under the terms of the GNU General Public License as published by the Free
; * Software Foundation; either version 3 of the License, or (at your option)
; * any later version.  See COPYING for more details.
; */

ALIGN 32
BITS 32

%define hash ecx
%define data edx
%define init esi

; 0 = (1024 - 256) (mod (LAB_CALC_UNROLL*LAB_CALC_PARA*16))
%define LAB_CALC_PARA	2
%define LAB_CALC_UNROLL	24

%define LAB_LOOP_UNROLL 64

extern sha256_consts_m128i

global CalcSha256_x86
;	CalcSha256	hash(ecx), data(edx), init([esp+4])
CalcSha256_x86:
	push	esi
	push	edi
	mov	init, [esp+12]

LAB_SHA:
	lea	edi, qword [data+256]				; + 256

LAB_CALC:
%macro	lab_calc_blk 1
	movdqa	xmm0, [edi-(15-%1)*16]				; xmm0 = W[I-15]
	movdqa	xmm4, [edi-(15-(%1+1))*16]			; xmm4 = W[I-15+1]
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

	movdqa	xmm3, [edi-(2-%1)*16]				; xmm3 = W[I-2]
	movdqa	xmm7, [edi-(2-(%1+1))*16]			; xmm7 = W[I-2+1]

	paddd	xmm0, [edi-(16-%1)*16]				; xmm0 = s0(W[I-15]) + W[I-16]
	paddd	xmm4, [edi-(16-(%1+1))*16]			; xmm4 = s0(W[I-15+1]) + W[I-16+1]

;;;;;;;;;;;;;;;;;;

	movdqa	xmm2, xmm3					; xmm2 = W[I-2]
	movdqa	xmm6, xmm7					; xmm6 = W[I-2+1]
	psrld	xmm3, 10					; xmm3 = W[I-2] >> 10
	psrld	xmm7, 10					; xmm7 = W[I-2+1] >> 10
	movdqa	xmm1, xmm3					; xmm1 = W[I-2] >> 10
	movdqa	xmm5, xmm7					; xmm5 = W[I-2+1] >> 10

	paddd	xmm0, [edi-(7-%1)*16]				; xmm0 = s0(W[I-15]) + W[I-16] + W[I-7]

	pslld	xmm2, 13					; xmm2 = W[I-2] << 13
	pslld	xmm6, 13					; xmm6 = W[I-2+1] << 13
	psrld	xmm1, 7						; xmm1 = W[I-2] >> 17
	psrld	xmm5, 7						; xmm5 = W[I-2+1] >> 17

	paddd	xmm4, [edi-(7-(%1+1))*16]			; xmm4 = s0(W[I-15+1]) + W[I-16+1] + W[I-7+1]

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
	movdqa	[edi+(%1*16)], xmm0
	movdqa	[edi+((%1+1)*16)], xmm4
%endmacro

%assign i 0
%rep    LAB_CALC_UNROLL
        lab_calc_blk i
%assign i i+LAB_CALC_PARA
%endrep

; Load the init values of the message into the hash.

	movdqa	xmm7, [init]
	pshufd	xmm5, xmm7, 0x55		; xmm5 == b
	pshufd	xmm4, xmm7, 0xAA		; xmm4 == c
	pshufd	xmm3, xmm7, 0xFF		; xmm3 == d
	pshufd	xmm7, xmm7, 0			; xmm7 == a

	movdqa	xmm0, [init+4*4]
	pshufd	xmm1, xmm0, 0x55		; [hash+0*16] == f
	movdqa	[hash+0*16], xmm1

	pshufd	xmm1, xmm0, 0xAA		; [hash+1*16] == g
	movdqa	[hash+1*16], xmm1

	pshufd	xmm1, xmm0, 0xFF		; [hash+2*16] == h
	movdqa	[hash+2*16], xmm1

	pshufd	xmm0, xmm0, 0			; xmm0 == e


LAB_LOOP:

;; T t1 = h + (Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25)) + ((e & f) ^ AndNot(e, g)) + Expand32<T>(g_sha256_k[j]) + w[j]

%macro	lab_loop_blk 1
	movdqa	xmm6, [data+%1]
	paddd	xmm6, sha256_consts_m128i[%1]

	paddd	xmm6, [hash+2*16]		; +h

	movdqa	xmm1, xmm0
	movdqa	xmm2, [hash+1*16]
	pandn	xmm1, xmm2	; ~e & g

	movdqa	[hash+2*16], xmm2		; h = g
	movdqa	xmm2, [hash+0*16]		; f
	movdqa	[hash+1*16], xmm2		; g = f


	pand	xmm2, xmm0	; e & f
	pxor	xmm1, xmm2	; (e & f) ^ (~e & g)
	movdqa	[hash+0*16], xmm0		; f = e

	paddd	xmm6, xmm1	; Ch + h + w[i] + k[i]

	movdqa	xmm1, xmm0
	psrld	xmm0, 6
	movdqa	xmm2, xmm0
	pslld	xmm1, 7
	psrld	xmm2, 5
	pxor	xmm0, xmm1
	pxor	xmm0, xmm2
	pslld	xmm1, 14
	psrld	xmm2, 14
	pxor	xmm0, xmm1
	pxor	xmm0, xmm2
	pslld	xmm1, 5
	pxor	xmm0, xmm1	; Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25)
	paddd	xmm6, xmm0	; xmm6 = t1

	movdqa	xmm0, xmm3	; d
	paddd	xmm0, xmm6	; e = d+t1

	movdqa	xmm1, xmm5	; =b
	movdqa	xmm3, xmm4	; d = c
	movdqa	xmm2, xmm4	; c
	pand	xmm2, xmm5	; b & c
	pand	xmm4, xmm7	; a & c
	pand	xmm1, xmm7	; a & b
	pxor	xmm1, xmm4
	movdqa	xmm4, xmm5	; c = b
	movdqa	xmm5, xmm7	; b = a
	pxor	xmm1, xmm2	; (a & c) ^ (a & d) ^ (c & d)
	paddd	xmm6, xmm1	; t1 + ((a & c) ^ (a & d) ^ (c & d))

	movdqa	xmm2, xmm7
	psrld	xmm7, 2
	movdqa	xmm1, xmm7
	pslld	xmm2, 10
	psrld	xmm1, 11
	pxor	xmm7, xmm2
	pxor	xmm7, xmm1
	pslld	xmm2, 9
	psrld	xmm1, 9
	pxor	xmm7, xmm2
	pxor	xmm7, xmm1
	pslld	xmm2, 11
	pxor	xmm7, xmm2
	paddd	xmm7, xmm6	; a = t1 + (Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22)) + ((a & c) ^ (a & d) ^ (c & d));
%endmacro

%assign i 0
%rep    LAB_LOOP_UNROLL
        lab_loop_blk i
%assign i i+16
%endrep

; Finished the 64 rounds, calculate hash and save

	movdqa	xmm1, [init+16]

	pshufd	xmm2, xmm1, 0xFF
	movdqa  xmm6, [hash+2*16]
	paddd   xmm2, xmm6
	movdqa  [hash+7*16], xmm2

	pshufd	xmm2, xmm1, 0xAA
	movdqa  xmm6, [hash+1*16]
	paddd   xmm2, xmm6
	movdqa  [hash+6*16], xmm2

	pshufd  xmm2, xmm1, 0x55
	movdqa  xmm6, [hash+0*16]
	paddd   xmm2, xmm6
	movdqa  [hash+5*16], xmm2

	pshufd	xmm1, xmm1, 0
	paddd	xmm0, xmm1
	movdqa  [hash+4*16], xmm0

	movdqa  xmm1, [init]

	pshufd  xmm2, xmm1, 0xFF
	paddd   xmm3, xmm2
	movdqa  [hash+3*16], xmm3

	pshufd  xmm2, xmm1, 0xAA
	paddd   xmm4, xmm2
	movdqa  [hash+2*16], xmm4

        pshufd  xmm2, xmm1, 0x55
        paddd   xmm5, xmm2
        movdqa  [hash+1*16], xmm5

	pshufd  xmm1, xmm1, 0
	paddd   xmm7, xmm1
	movdqa	[hash+0*16], xmm7

LAB_RET:
	pop	edi
	pop	esi
	retn	4

%ifidn __OUTPUT_FORMAT__,elf
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
%ifidn __OUTPUT_FORMAT__,elf32
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
