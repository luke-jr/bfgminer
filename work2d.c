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

#include "miner.h"

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
