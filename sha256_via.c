/*
 * Copyright 2010-2011 Jeff Garzik
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2011-2012 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include "driver-cpu.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include "miner.h"

#ifdef WANT_VIA_PADLOCK

static void via_sha256(void *hash, void *buf, unsigned len)
{
	unsigned stat = 0;
	asm volatile(".byte 0xf3, 0x0f, 0xa6, 0xd0"
		     :"+S"(buf), "+a"(stat)
		     :"c"(len), "D" (hash)
		     :"memory");
}

bool scanhash_via(struct thr_info * const thr, struct work * const work,
		  uint32_t max_nonce, uint32_t *last_nonce,
		  uint32_t n)
{
	uint8_t * const data_inout = work->data;
	
	unsigned char data[128] __attribute__((aligned(128)));
	unsigned char tmp_hash[32] __attribute__((aligned(128)));
	unsigned char tmp_hash1[32] __attribute__((aligned(128)));
	uint32_t *data32 = (uint32_t *) data;
	uint32_t *hash32 = (uint32_t *) tmp_hash;
	uint32_t *nonce = (uint32_t *)(data + 64 + 12);
	uint32_t *nonce_inout = (uint32_t *)(data_inout + 64 + 12);
	unsigned long stat_ctr = 0;

	/* bitcoin gives us big endian input, but via wants LE,
	 * so we reverse the swapping bitcoin has already done (extra work)
	 * in order to permit the hardware to swap everything
	 * back to BE again (extra work).
	 */
	swap32yes(data32, data_inout, 128/4);

	while (1) {
		*nonce = n;

		/* first SHA256 transform */
		memcpy(tmp_hash1, sha256_init_state, 32);
		via_sha256(tmp_hash1, data, 80);	/* or maybe 128? */

		swap32yes(tmp_hash1, tmp_hash1, 32/4);

		/* second SHA256 transform */
		memcpy(tmp_hash, sha256_init_state, 32);
		via_sha256(tmp_hash, tmp_hash1, 32);

		stat_ctr++;

		if (unlikely((hash32[7] == 0)))
		{
			/* swap nonce'd data back into original storage area;
			 */
			*nonce_inout = bswap_32(n);
			*last_nonce = n;
			return true;
		}

		if ((n >= max_nonce) || thr->work_restart) {
			*last_nonce = n;
			return false;
		}

		n++;
	}
}

#endif /* WANT_VIA_PADLOCK */

