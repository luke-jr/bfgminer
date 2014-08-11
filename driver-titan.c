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
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "deviceapi.h"
#include "logging.h"
#include "lowl-spi.h"
#include "miner.h"
#include "util.h"

#include "titan-asic.h"

#define	KNC_TITAN_DEFAULT_FREQUENCY	600

#define KNC_TITAN_HWERR_DISABLE_SECS	10

#define	KNC_TITAN_SPI_SPEED		3000000
#define	KNC_TITAN_SPI_DELAY		0
#define	KNC_TITAN_SPI_MODE		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH)
#define	KNC_TITAN_SPI_BITS		8

BFG_REGISTER_DRIVER(knc_titan_drv)

static const struct bfg_set_device_definition knc_titan_set_device_funcs[];

struct knc_titan_info {
	struct spi_port *spi;
	struct cgpu_info *cgpu;
	int freq;
};

struct knc_titan_core {
	int asicno;
	int coreno;
	int hwerr_in_row;
	int hwerr_disable_time;
	struct timeval enable_at;
	struct timeval first_hwerr;
};

static struct spi_port global_spi;

static bool knc_titan_spi_open(const char *repr, struct spi_port * const spi)
{
	const char * const spipath = "/dev/spidev0.1";
	const int fd = open(spipath, O_RDWR);
	const uint8_t lsbfirst = 0;
	if (0 > fd)
		return false;
	if (ioctl(fd, SPI_IOC_WR_MODE         , &spi->mode )) goto fail;
	if (ioctl(fd, SPI_IOC_WR_LSB_FIRST    , &lsbfirst  )) goto fail;
	if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &spi->bits )) goto fail;
	if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ , &spi->speed)) goto fail;
	spi->fd = fd;
	return true;

fail:
	close(fd);
	spi->fd = -1;
	applog(LOG_WARNING, "%s: Failed to open %s", repr, spipath);
	return false;
}

#define	knc_titan_spi_txrx	linux_spi_txrx

static bool knc_titan_detect_one(const char *devpath)
{
	static struct cgpu_info *prev_cgpu = NULL;
	struct cgpu_info *cgpu;
	struct spi_port *spi = &global_spi;
	int cores = 0, die;
	struct titan_info_response resp;
	char repr[6];

	cgpu = malloc(sizeof(*cgpu));
	if (unlikely(!cgpu))
		quit(1, "Failed to alloc cgpu_info");

	if (!prev_cgpu) {
		/* Be careful, read lowl-spi.h comments for warnings */
		memset(spi, 0, sizeof(*spi));
		spi->txrx = knc_titan_spi_txrx;
		spi->cgpu = cgpu;
		spi->repr = knc_titan_drv.dname;
		spi->logprio = LOG_ERR;
		spi->speed = KNC_TITAN_SPI_SPEED;
		spi->delay = KNC_TITAN_SPI_DELAY;
		spi->mode = KNC_TITAN_SPI_MODE;
		spi->bits = KNC_TITAN_SPI_BITS;

		if (!knc_titan_spi_open(knc_titan_drv.name, spi)) {
			free(cgpu);
			return false;
		}
	}

	snprintf(repr, sizeof(repr), "%s%s", knc_titan_drv.name, devpath);
	for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
		if (!knc_titan_spi_get_info(repr, spi, &resp, die, KNC_TITAN_CORES_PER_DIE))
			continue;
		cores += resp.cores;
	}
	if (0 == cores) {
		free(cgpu);
		return false;
	}

	applog(LOG_NOTICE, "%s: Found ASIC with %d cores", repr, cores);

	*cgpu = (struct cgpu_info) {
		.drv = &knc_titan_drv,
		.device_path = strdup(devpath),
		.set_device_funcs = knc_titan_set_device_funcs,
		.deven = DEV_ENABLED,
		.procs = cores,
		.threads = prev_cgpu ? 0 : 1,
	};
	const bool rv = add_cgpu_slave(cgpu, prev_cgpu);
	prev_cgpu = cgpu;
	return rv;
}

static int knc_titan_detect_auto(void)
{
	const int first = 0, last = KNC_TITAN_MAX_ASICS - 1;
	char devpath[256];
	int found = 0, i;

	for (i = first; i <= last; ++i)
	{
		sprintf(devpath, "%d", i);
		if (knc_titan_detect_one(devpath))
			++found;
	}

	return found;
}

static void knc_titan_detect(void)
{
	generic_detect(&knc_titan_drv, knc_titan_detect_one, knc_titan_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

static bool knc_titan_init(struct thr_info * const thr)
{
	const int max_cores = KNC_TITAN_CORES_PER_ASIC;
	struct thr_info *mythr;
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct knc_titan_info *knc;
	struct knc_titan_core *knccore;
	int i, asic;

	knc = calloc(1, sizeof(*knc));
	if (unlikely(!knc))
		quit(1, "Failed to alloc knc_titan_info");

	for (proc = cgpu; proc; ) {
		if (proc->device != proc) {
			applog(LOG_WARNING, "%"PRIpreprv": Extra processor?", proc->proc_repr);
			proc = proc->next_proc;
			continue;
		}

		asic = atoi(proc->device_path);

		for (i = 0; i < max_cores; ++i) {
			mythr = proc->thr[0];
			mythr->cgpu_data = knccore = malloc(sizeof(*knccore));
			if (unlikely(!knccore))
				quit(1, "Failed to alloc knc_titan_core");
			*knccore = (struct knc_titan_core) {
				.asicno = asic,
				.coreno = i,
				.hwerr_in_row = 0,
				.hwerr_disable_time = KNC_TITAN_HWERR_DISABLE_SECS,
			};
			timer_set_now(&knccore->enable_at);
			proc->device_data = knc;

			proc = proc->next_proc;
			if ((!proc) || proc->device == proc)
				break;
		}
	}

	*knc = (struct knc_titan_info) {
		.spi = &global_spi,
		.cgpu = cgpu,
		.freq = KNC_TITAN_DEFAULT_FREQUENCY,
	};

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
	.dname = "titan",
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
