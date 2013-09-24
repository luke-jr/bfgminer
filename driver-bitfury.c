/*
 * Copyright 2013 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include "miner.h"
#include "driver-bitfury.h"

struct device_drv bitfury_drv;

static void bitfury_detect(void)
{
}

static bool bitfury_prepare(struct thr_info __maybe_unused *thr)
{
	return false;
}

static bool bitfury_fill(struct cgpu_info __maybe_unused *bitfury)
{
	return true;
}

static int64_t bitfury_scanhash(struct thr_info __maybe_unused *thr)
{
	return 0;
}

static void bitfury_flush_work(struct cgpu_info __maybe_unused *bitfury)
{
}

static struct api_data *bitfury_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void get_bitfury_statline_before(char __maybe_unused *buf, size_t __maybe_unused bufsiz,
					struct cgpu_info __maybe_unused *bitfury)
{
}

static void bitfury_init(struct cgpu_info __maybe_unused *bitfury)
{
}

static void bitfury_shutdown(struct thr_info __maybe_unused *thr)
{
}

/* Currently hardcoded to BF1 devices */
struct device_drv bitfury_drv = {
	.drv_id = DRIVER_BITFURY,
	.dname = "bitfury",
	.name = "BFO",
	.drv_detect = bitfury_detect,
	.thread_prepare = bitfury_prepare,
	.hash_work = hash_queued_work,
	.queue_full = bitfury_fill,
	.scanwork = bitfury_scanhash,
	.flush_work = bitfury_flush_work,
	.get_api_stats = bitfury_api_stats,
	.get_statline_before = get_bitfury_statline_before,
	.reinit_device = bitfury_init,
	.thread_shutdown = bitfury_shutdown
};
