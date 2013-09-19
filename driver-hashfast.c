/*
 * Copyright 2013 Con Kolivas <kernel@kolivas.org>
 * Copyright 2013 Hashfast Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>

#include "usbutils.h"
#include "fpgautils.h"

#include "driver-hashfast.h"

static hf_info_t **hashfast_infos;
struct device_drv hashfast_drv;

static void hashfast_detect(void)
{
}

static bool hashfast_prepare(struct thr_info __maybe_unused *thr)
{
	return true;
}

static bool hashfast_fill(struct cgpu_info __maybe_unused *hashfast)
{
	return true;
}

static int64_t hashfast_scanhash(struct thr_info __maybe_unused *thr)
{
	return 0;
}

static struct api_data *hashfast_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void hashfast_init(struct cgpu_info __maybe_unused *hashfast)
{
}

static void hashfast_shutdown(struct thr_info __maybe_unused *thr)
{
}

struct device_drv hashfast_drv = {
	.drv_id = DRIVER_HASHFAST,
	.dname = "Hashfast",
	.name = "HFA",
	.drv_detect = hashfast_detect,
	.thread_prepare = hashfast_prepare,
	.hash_work = hash_queued_work,
	.queue_full = hashfast_fill,
	.scanwork = hashfast_scanhash,
	.get_api_stats = hashfast_api_stats,
	.reinit_device = hashfast_init,
	.thread_shutdown = hashfast_shutdown,
};
