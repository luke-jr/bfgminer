/*
 * Copyright 2012-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <uthash.h>

#include "logging.h"
#include "miner.h"
#include "ocl.h"
#include "util.h"

static
void hash_data(void *out_hash, const void *data)
{
	unsigned char blkheader[80];
	
	// data is past the first SHA256 step (padding and interpreting as big endian on a little endian platform), so we need to flip each 32-bit chunk around to get the original input block header
	swap32yes(blkheader, data, 80 / 4);
	
	// double-SHA256 to get the block hash
	gen_hash(blkheader, out_hash, 80);
}

#ifdef USE_OPENCL
static
float opencl_oclthreads_to_intensity_sha256d(const unsigned long oclthreads)
{
	return log2f(oclthreads) - 15.;
}

static
unsigned long opencl_intensity_to_oclthreads_sha256d(float intensity)
{
	return powf(2, intensity + 15);
}

static
char *opencl_get_default_kernel_file_sha256d(const struct mining_algorithm * const malgo, struct cgpu_info * const cgpu, struct _clState * const clState)
{
	const char * const vbuff = clState->platform_ver_str;
	
	if (clState->is_mesa)
	{
		applog(LOG_INFO, "Selecting phatk kernel for Mesa");
		return strdup("phatk");
	}
	
	/* Detect all 2.6 SDKs not with Tahiti and use diablo kernel */
	if (!strstr(cgpu->name, "Tahiti") &&
	   (strstr(vbuff, "844.4") ||  // Linux 64 bit ATI 2.6 SDK
	    strstr(vbuff, "851.4") ||  // Windows 64 bit ""
	    strstr(vbuff, "831.4") ||
	    strstr(vbuff, "898.1") ||  // 12.2 driver SDK
	    strstr(vbuff, "923.1") ||  // 12.4
	    strstr(vbuff, "938.2") ||  // SDK 2.7
	    strstr(vbuff, "1113.2")))  // SDK 2.8
	{
		applog(LOG_INFO, "Selecting diablo kernel");
		return strdup("diablo");
	}
	
	/* Detect all 7970s, older ATI and NVIDIA and use poclbm */
	if (strstr(cgpu->name, "Tahiti") || !clState->hasBitAlign)
	{
		applog(LOG_INFO, "Selecting poclbm kernel");
		return strdup("poclbm");
	}
	
	/* Use phatk for the rest R5xxx R6xxx */
	{
		applog(LOG_INFO, "Selecting phatk kernel");
		return strdup("phatk");
	}
}
#endif  /* USE_OPENCL */

struct mining_algorithm malgo_sha256d = {
	.name = "SHA256d",
	.aliases = "SHA256d|SHA256|SHA2",
	
	.algo = POW_SHA256D,
	.ui_skip_hash_bytes = 4,
	.worktime_skip_prevblk_u32 = 1,
	.reasonable_low_nonce_diff = 1.,
	
	.hash_data_f = hash_data,
	
#ifdef USE_OPENCL
	.opencl_nodefault = true,
	.opencl_oclthreads_to_intensity = opencl_oclthreads_to_intensity_sha256d,
	.opencl_intensity_to_oclthreads = opencl_intensity_to_oclthreads_sha256d,
	.opencl_min_oclthreads =       0x20,  // intensity -10
	.opencl_max_oclthreads = 0x20000000,  // intensity  14
	.opencl_min_nonce_diff = 1.,
	.opencl_get_default_kernel_file = opencl_get_default_kernel_file_sha256d,
#endif
};

static
__attribute__((constructor))
void init_sha256d(void)
{
    LL_APPEND(mining_algorithms, (&malgo_sha256d));
}
