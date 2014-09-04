;/*
; * Copyright 2011 Neil Kettle
; * Copyright 2011 Ufasoft
; * Copyright 2013 James Z.M. Gao
; * Copyright 2012-2013 Luke Dashjr
; *
; * This program is free software; you can redistribute it and/or modify it
; * under the terms of the GNU General Public License as published by the Free
; * Software Foundation; either version 3 of the License, or (at your option)
; * any later version.  See COPYING for more details.
; */

ALIGN 32
BITS 64

%ifidn __OUTPUT_FORMAT__,win64
%define hash rcx
%define data rdx
%define init r8
%define temp r9
%else
%define hash rdi
%define data rsi
%define init rdx
%define temp rcx
%endif

; 0 = (1024 - 256) (mod (LAB_CALC_UNROLL*LAB_CALC_PARA*16))
%define LAB_CALC_PARA	2
%define LAB_CALC_UNROLL	8

%define LAB_LOOP_UNROLL 8

extern g_4sha256_k

global CalcSha256_x64_sse4
;	CalcSha256	hash(rdi), data(rsi), init(rdx)
;	CalcSha256	hash(rcx), data(rdx), init(r8)
CalcSha256_x64_sse4:

	push	rbx
%ifidn __OUTPUT_FORMAT__,win64
	sub	rsp, 16 * 6
	movdqa	[rsp + 16*0], xmm6
	movdqa	[rsp + 16*1], xmm7
	movdqa	[rsp + 16*2], xmm8
	movdqa	[rsp + 16*3], xmm9
	movdqa	[rsp + 16*4], xmm10
	movdqa	[rsp + 16*5], xmm11
%endif

LAB_NEXT_NONCE:

	mov	temp, 64*4					; 256 - temp is # of SHA-2 rounds
	mov	rax, 16*4					; 64 - rax is where we expand to

LAB_SHA:
	push	temp
	lea	temp, qword [data+temp*4]			; + 1024
	lea	r11, qword [data+rax*4]				; + 256

LAB_CALC:
%macro	lab_calc_blk 1

	movntdqa	xmm0, [r11-(15-%1)*16]				; xmm0 = W[I-15]
	movdqa	xmm2, xmm0					; xmm2 = W[I-15]	
	movntdqa	xmm4, [r11-(15-(%1+1))*16]			; xmm4 = W[I-15+1]
	movdqa	xmm6, xmm4					; xmm6 = W[I-15+1]	

	psrld	xmm0, 3						; xmm0 = W[I-15] >> 3
	movdqa	xmm1, xmm0					; xmm1 = W[I-15] >> 3	
	pslld	xmm2, 14					; xmm2 = W[I-15] << 14			
	psrld	xmm4, 3						; xmm4 = W[I-15+1] >> 3
	movdqa	xmm5, xmm4					; xmm5 = W[I-15+1] >> 3
	psrld	xmm5, 4						; xmm5 = W[I-15+1] >> 7	
	pxor	xmm4, xmm5					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7)	
	pslld	xmm6, 14					; xmm6 = W[I-15+1] << 14
	psrld	xmm1, 4						; xmm1 = W[I-15] >> 7
	pxor	xmm0, xmm1					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7)
	pxor	xmm0, xmm2					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7) ^ (W[I-15] << 14)
	psrld	xmm1, 11					; xmm1 = W[I-15] >> 18
	psrld	xmm5, 11					; xmm5 = W[I-15+1] >> 18
	pxor	xmm4, xmm6					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7) ^ (W[I-15+1] << 14)
	pxor	xmm4, xmm5					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7) ^ (W[I-15+1] << 14) ^ (W[I-15+1] >> 18)	
	pslld	xmm2, 11					; xmm2 = W[I-15] << 25
	pslld	xmm6, 11					; xmm6 = W[I-15+1] << 25
	pxor	xmm4, xmm6					; xmm4 = (W[I-15+1] >> 3) ^ (W[I-15+1] >> 7) ^ (W[I-15+1] << 14) ^ (W[I-15+1] >> 18) ^ (W[I-15+1] << 25)
	pxor	xmm0, xmm1					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7) ^ (W[I-15] << 14) ^ (W[I-15] >> 18)
	pxor	xmm0, xmm2					; xmm0 = (W[I-15] >> 3) ^ (W[I-15] >> 7) ^ (W[I-15] << 14) ^ (W[I-15] >> 18) ^ (W[I-15] << 25)
	paddd	xmm0, [r11-(16-%1)*16]				; xmm0 = s0(W[I-15]) + W[I-16]
	paddd	xmm4, [r11-(16-(%1+1))*16]			; xmm4 = s0(W[I-15+1]) + W[I-16+1]
	movntdqa	xmm3, [r11-(2-%1)*16]				; xmm3 = W[I-2]
	movntdqa	xmm7, [r11-(2-(%1+1))*16]			; xmm7 = W[I-2+1]

;;;;;;;;;;;;;;;;;;

	movdqa	xmm2, xmm3					; xmm2 = W[I-2]
	psrld	xmm3, 10					; xmm3 = W[I-2] >> 10
	movdqa	xmm1, xmm3					; xmm1 = W[I-2] >> 10
	movdqa	xmm6, xmm7					; xmm6 = W[I-2+1]
	psrld	xmm7, 10					; xmm7 = W[I-2+1] >> 10
	movdqa	xmm5, xmm7					; xmm5 = W[I-2+1] >> 10

	paddd	xmm0, [r11-(7-%1)*16]				; xmm0 = s0(W[I-15]) + W[I-16] + W[I-7]
	paddd	xmm4, [r11-(7-(%1+1))*16]			; xmm4 = s0(W[I-15+1]) + W[I-16+1] + W[I-7+1]
	
	pslld	xmm2, 13					; xmm2 = W[I-2] << 13
	pslld	xmm6, 13					; xmm6 = W[I-2+1] << 13
	psrld	xmm1, 7						; xmm1 = W[I-2] >> 17
	psrld	xmm5, 7						; xmm5 = W[I-2+1] >> 17



	pxor	xmm3, xmm1					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17)
	psrld	xmm1, 2						; xmm1 = W[I-2] >> 19
	pxor	xmm3, xmm2					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17) ^ (W[I-2] << 13)
	pslld	xmm2, 2						; xmm2 = W[I-2] << 15
	pxor	xmm7, xmm5					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17)
	psrld	xmm5, 2						; xmm5 = W[I-2+1] >> 19	
	pxor	xmm7, xmm6					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17) ^ (W[I-2+1] << 13)
	pslld	xmm6, 2						; xmm6 = W[I-2+1] << 15



	pxor	xmm3, xmm1					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17) ^ (W[I-2] << 13) ^ (W[I-2] >> 19)
	pxor	xmm3, xmm2					; xmm3 = (W[I-2] >> 10) ^ (W[I-2] >> 17) ^ (W[I-2] << 13) ^ (W[I-2] >> 19) ^ (W[I-2] << 15)
	paddd	xmm0, xmm3					; xmm0 = s0(W[I-15]) + W[I-16] + s1(W[I-2]) + W[I-7]
	pxor	xmm7, xmm5					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17) ^ (W[I-2+1] << 13) ^ (W[I-2+1] >> 19)	
	pxor	xmm7, xmm6					; xmm7 = (W[I-2+1] >> 10) ^ (W[I-2+1] >> 17) ^ (W[I-2+1] << 13) ^ (W[I-2+1] >> 19) ^ (W[I-2+1] << 15)
	paddd	xmm4, xmm7					; xmm4 = s0(W[I-15+1]) + W[I-16+1] + s1(W[I-2+1]) + W[I-7+1]

	movdqa	[r11+(%1*16)], xmm0
	movdqa	[r11+((%1+1)*16)], xmm4
%endmacro

%assign i 0
%rep    LAB_CALC_UNROLL
        lab_calc_blk i
%assign i i+LAB_CALC_PARA
%endrep

	add	r11, LAB_CALC_UNROLL*LAB_CALC_PARA*16
	cmp	r11, temp
	jb	LAB_CALC

	pop	temp
	mov	rax, 0

; Load the init values of the message into the hash.

	movntdqa	xmm7, [init]
	pshufd	xmm5, xmm7, 0x55		; xmm5 == b
	pshufd	xmm4, xmm7, 0xAA		; xmm4 == c
	pshufd	xmm3, xmm7, 0xFF		; xmm3 == d
	pshufd	xmm7, xmm7, 0			; xmm7 == a

	movntdqa	xmm0, [init+4*4]
	pshufd	xmm8, xmm0, 0x55		; xmm8 == f
	pshufd	xmm9, xmm0, 0xAA		; xmm9 == g
	pshufd	xmm10, xmm0, 0xFF		; xmm10 == h
	pshufd	xmm0, xmm0, 0			; xmm0 == e

LAB_LOOP:

;; T t1 = h + (Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25)) + ((e & f) ^ AndNot(e, g)) + Expand32<T>(g_sha256_k[j]) + w[j]

%macro	lab_loop_blk 0
	movntdqa	xmm6, [data+rax*4]
	paddd	xmm6, g_4sha256_k[rax*4]
	add	rax, 4

	paddd	xmm6, xmm10	; +h

	movdqa	xmm1, xmm0
	movdqa	xmm2, xmm9
	pandn	xmm1, xmm2	; ~e & g

	movdqa	xmm10, xmm2	; h = g
	movdqa	xmm2, xmm8	; f
	movdqa	xmm9, xmm2	; g = f

	pand	xmm2, xmm0	; e & f
	pxor	xmm1, xmm2	; (e & f) ^ (~e & g)
	movdqa	xmm8, xmm0	; f = e

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
        lab_loop_blk
%assign i i+1
%endrep

	cmp	rax, temp
	jb	LAB_LOOP

; Finished the 64 rounds, calculate hash and save

	movntdqa	xmm1, [init]
	pshufd	xmm2, xmm1, 0x55
	paddd	xmm5, xmm2
	pshufd	xmm6, xmm1, 0xAA
	paddd	xmm4, xmm6
	pshufd	xmm11, xmm1, 0xFF
	paddd	xmm3, xmm11
	pshufd	xmm1, xmm1, 0
	paddd	xmm7, xmm1

	movntdqa	xmm1, [init+4*4]
	pshufd	xmm2, xmm1, 0x55
	paddd	xmm8, xmm2
	pshufd	xmm6, xmm1, 0xAA
	paddd	xmm9, xmm6
	pshufd	xmm11, xmm1, 0xFF
	paddd	xmm10, xmm11
	pshufd	xmm1, xmm1, 0
	paddd	xmm0, xmm1

	movdqa	[hash+0*16], xmm7
	movdqa	[hash+1*16], xmm5
	movdqa	[hash+2*16], xmm4
	movdqa	[hash+3*16], xmm3
	movdqa	[hash+4*16], xmm0
	movdqa	[hash+5*16], xmm8
	movdqa	[hash+6*16], xmm9
	movdqa	[hash+7*16], xmm10

LAB_RET:
%ifidn __OUTPUT_FORMAT__,win64
	movdqa	xmm6, [rsp + 16*0]
	movdqa	xmm7, [rsp + 16*1]
	movdqa	xmm8, [rsp + 16*2]
	movdqa	xmm9, [rsp + 16*3]
	movdqa	xmm10, [rsp + 16*4]
	movdqa	xmm11, [rsp + 16*5]
	add	rsp, 16 * 6
%endif
	pop	rbx
	ret

%ifidn __OUTPUT_FORMAT__,elf
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
%ifidn __OUTPUT_FORMAT__,elf64
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
