/*
 * Copyright 2013-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "miner.h"
#include "util.h"

#define MAX_DIVISIONS 255

static bool work2d_reserved[MAX_DIVISIONS + 1] = { true };
int work2d_xnonce1sz;
int work2d_xnonce2sz;

void work2d_init()
{
	RUNONCE();
	
	for (uint64_t n = MAX_DIVISIONS; n; n >>= 8)
		++work2d_xnonce1sz;
	work2d_xnonce2sz = 2;
}

bool reserve_work2d_(uint32_t * const xnonce1_p)
{
	uint32_t xnonce1;
	for (xnonce1 = MAX_DIVISIONS; work2d_reserved[xnonce1]; --xnonce1)
		if (!xnonce1)
			return false;
	work2d_reserved[xnonce1] = true;
	*xnonce1_p = htole32(xnonce1);
	return true;
}

void release_work2d_(uint32_t xnonce1)
{
	xnonce1 = le32toh(xnonce1);
	work2d_reserved[xnonce1] = false;
}

void work2d_gen_dummy_work(struct work * const work, struct stratum_work * const swork, const struct timeval * const tvp_prepared, const void * const xnonce2, const uint32_t xnonce1)
{
	uint8_t *p, *s;
	
	*work = (struct work){
		.pool = swork->pool,
		.work_restart_id = swork->work_restart_id,
		.tv_staged = *tvp_prepared,
	};
	bytes_resize(&work->nonce2, swork->n2size);
	s = bytes_buf(&work->nonce2);
	p = &s[swork->n2size - work2d_xnonce2sz];
	if (xnonce2)
		memcpy(p, xnonce2, work2d_xnonce2sz);
#ifndef __OPTIMIZE__
	else
		memset(p, '\0', work2d_xnonce2sz);
#endif
	p -= work2d_xnonce1sz;
	memcpy(p, &xnonce1, work2d_xnonce1sz);
	if (p != s)
		memset(s, '\xbb', p - s);
	gen_stratum_work2(work, swork);
}

bool work2d_submit_nonce(struct thr_info * const thr, struct stratum_work * const swork, const struct timeval * const tvp_prepared, const void * const xnonce2, const uint32_t xnonce1, const uint32_t nonce, const uint32_t ntime, bool * const out_is_stale)
{
	struct work _work, *work;
	bool rv;
	
	// Generate dummy work
	work = &_work;
	work2d_gen_dummy_work(work, swork, tvp_prepared, xnonce2, xnonce1);
	*(uint32_t *)&work->data[68] = htobe32(ntime);
	
	// Check if it's stale, if desired
	if (out_is_stale)
		*out_is_stale = stale_work(work, true);
	
	// Submit nonce
	rv = submit_nonce(thr, work, nonce);
	
	clean_work(work);
	
	return rv;
}
