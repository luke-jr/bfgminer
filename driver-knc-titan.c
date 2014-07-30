/*
 * Copyright 2014 Vitalii Demianets
 * Copyright 2014 KnCMiner
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <fcntl.h>

#include "deviceapi.h"

#define	KNC_TITAN_MAX_ASICS		6
#define	KNC_TITAN_DIES_PER_ASIC		4
#define KNC_TITAN_CORES_PER_DIE		48
#define	KNC_TITAN_CORES_PER_ASIC	(KNC_TITAN_CORES_PER_DIE * KNC_TITAN_DIES_PER_ASIC)

#define	KNC_TITAN_DEFAULT_FREQUENCY	600

BFG_REGISTER_DRIVER(knc_titan_drv)

static const struct bfg_set_device_definition knc_titan_set_device_funcs[];

struct knc_titan_info {
	int freq;
};

static bool knc_titan_detect_one(const char *devpath)
{
	static struct cgpu_info *prev_cgpu = NULL;
	struct cgpu_info *cgpu;
	int i;
	const int fd = open(devpath, O_RDWR);
	uint8_t buf[0x20];

	if (unlikely(-1 == fd)) {
		applog(LOG_DEBUG, "%s: Failed to open %s", __func__, devpath);
		return false;
	}

	close(fd);

	cgpu = malloc(sizeof(*cgpu));
	*cgpu = (struct cgpu_info) {
		.drv = &knc_titan_drv,
		.device_path = strdup(devpath),
		.set_device_funcs = knc_titan_set_device_funcs,
		.deven = DEV_ENABLED,
		.procs = KNC_TITAN_CORES_PER_ASIC,
		.threads = prev_cgpu ? 0 : 1,
	};
	const bool rv = add_cgpu_slave(cgpu, prev_cgpu);
	prev_cgpu = cgpu;
	return rv;
}

static int knc_titan_detect_auto(void)
{
	const int first = 3, last = 3 + KNC_TITAN_MAX_ASICS - 1;
	char devpath[256];
	int found = 0, i;
	
	for (i = first; i <= last; ++i)
	{
		sprintf(devpath, "/dev/i2c-%d", i);
		if (knc_titan_detect_one(devpath))
			++found;
	}
	
	return found;
}

static void knc_titan_detect(void)
{
	generic_detect(&knc_titan_drv, knc_titan_detect_one, knc_titan_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

static struct knc_titan_info *knc_titan_alloc_info(void)
{
	struct knc_titan_info *info = calloc(1, sizeof(struct knc_titan_info));
	if (unlikely(!info))
		quit(1, "Failed to malloc knc_titan_info");
	
	info->freq = KNC_TITAN_DEFAULT_FREQUENCY;
	
	return info;
}

static bool knc_titan_init(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct knc_titan_info *knc;

	knc = malloc(sizeof(*knc));

	for (proc = cgpu; proc; proc = proc->next_proc)
	{
		if (proc->device != proc)
		{
			applog(LOG_WARNING, "%"PRIpreprv": Extra processor?", proc->proc_repr);
			proc = proc->next_proc;
			continue;
		}
		knc = knc_titan_alloc_info();
		proc->device_data = knc;
	}

	cgpu_set_defaults(cgpu);

	return true;
}

static int64_t knc_titan_scanhash(struct thr_info *thr, struct work *work, int64_t __maybe_unused max_nonce)
{
	return 0;
}

/*
 * specify settings / options via RPC or command line
 */

/* support for --set-device
 * must be set before probing the device
 */
static void knc_titan_set_clock_freq(struct cgpu_info * const device, int const val)
{
	struct knc_titan_info * const info = device->device_data;
	info->freq = val;
}

static const char *knc_titan_set_clock(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	knc_titan_set_clock_freq(device, atoi(setting));
	return NULL;
}

static const struct bfg_set_device_definition knc_titan_set_device_funcs[] = {
	{ "clock", knc_titan_set_clock, NULL },
	{ NULL },
};

/*
 * specify settings / options via TUI
 */

#ifdef HAVE_CURSES
static void knc_titan_tui_wlogprint_choices(struct cgpu_info * const proc)
{
	wlogprint("[C]lock speed ");
}

static const char *knc_titan_tui_handle_choice(struct cgpu_info * const proc, const int input)
{
	static char buf[0x100];  /* Static for replies */

	switch (input)
	{
		case 'c': case 'C':
		{
			sprintf(buf, "Set clock speed");
			char * const setting = curses_input(buf);

			knc_titan_set_clock_freq(proc->device, atoi(setting));

			return "Clock speed changed\n";
		}
	}
	return NULL;
}

static void knc_titan_wlogprint_status(struct cgpu_info * const proc)
{
	wlogprint("Clock speed: N/A\n");
}
#endif

struct device_drv knc_titan_drv =
{
	/* metadata */
	.dname = "knc-titan",
	.name = "KNC",
	.supported_algos = POW_SCRYPT,
	.drv_detect = knc_titan_detect,

	.thread_init = knc_titan_init,

	/* specify mining type - scanhash */
	.minerloop = minerloop_scanhash,

	/* scanhash mining hooks */
	.scanhash = knc_titan_scanhash,

	/* TUI support - e.g. setting clock via UI */
#ifdef HAVE_CURSES
	.proc_wlogprint_status = knc_titan_wlogprint_status,
	.proc_tui_wlogprint_choices = knc_titan_tui_wlogprint_choices,
	.proc_tui_handle_choice = knc_titan_tui_handle_choice,
#endif
};
