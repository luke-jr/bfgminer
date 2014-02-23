/*
 * Copyright 2011-2013 Luke Dashjr
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2011 Mark Crichton
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFG_DRIVER_CPU_H
#define BFG_DRIVER_CPU_H

#include "miner.h"

#include "config.h"
#include <stdbool.h>

#ifndef OPT_SHOW_LEN
#define OPT_SHOW_LEN 80
#endif

#if defined(__i386__) && defined(HAVE_SSE2)
#define WANT_SSE2_4WAY 1
#endif

#ifdef __ALTIVEC__
#define WANT_ALTIVEC_4WAY 1
#endif

#if defined(__i386__) && defined(HAVE_YASM) && defined(HAVE_SSE2)
#define WANT_X8632_SSE2 1
#endif

#ifdef __i386__
#define WANT_VIA_PADLOCK 1
#endif

#if defined(__x86_64__) && defined(HAVE_YASM)
#define WANT_X8664_SSE2 1
#endif

#if defined(__x86_64__) && defined(HAVE_YASM)
#define WANT_X8664_SSE4 1
#endif

#ifdef USE_SCRYPT
#define WANT_SCRYPT
#endif

enum sha256_algos {
	ALGO_C,			/* plain C */
	ALGO_4WAY,		/* parallel SSE2 */
	ALGO_VIA,		/* VIA padlock */
	ALGO_CRYPTOPP,		/* Crypto++ (C) */
	ALGO_CRYPTOPP_ASM32,	/* Crypto++ 32-bit assembly */
	ALGO_SSE2_32,		/* SSE2 for x86_32 */
	ALGO_SSE2_64,		/* SSE2 for x86_64 */
	ALGO_SSE4_64,		/* SSE4 for x86_64 */
	ALGO_ALTIVEC_4WAY,	/* parallel Altivec */
	ALGO_SCRYPT,		/* scrypt */
	
	ALGO_FASTAUTO,		/* fast autodetect */
	ALGO_AUTO		/* autodetect */
};

extern const char *algo_names[];
extern struct device_drv cpu_drv;

extern char *set_algo(const char *arg, enum sha256_algos *algo);
extern void show_algo(char buf[OPT_SHOW_LEN], const enum sha256_algos *algo);
extern char *force_nthreads_int(const char *arg, int *i);
extern void init_max_name_len();
extern double bench_algo_stage3(enum sha256_algos algo);
extern void set_scrypt_algo(enum sha256_algos *algo);

#endif /* __DEVICE_CPU_H__ */
