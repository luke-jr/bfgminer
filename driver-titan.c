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

#define	KNC_POLL_INTERVAL_US		10000

#define	KNC_TITAN_SPI_SPEED		3000000
#define	KNC_TITAN_SPI_DELAY		0
#define	KNC_TITAN_SPI_MODE		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH)
#define	KNC_TITAN_SPI_BITS		8

BFG_REGISTER_DRIVER(knc_titan_drv)

static const struct bfg_set_device_definition knc_titan_set_device_funcs[];

struct knc_titan_core {
	int asicno;
	int dieno; /* inside asic */
	int coreno; /* inside die */
	struct knc_titan_die *die;
	struct cgpu_info *proc;

	int next_slot;

	int hwerr_in_row;
	int hwerr_disable_time;
	struct timeval enable_at;
	struct timeval first_hwerr;
};

struct knc_titan_die {
	int asicno;
	int dieno; /* inside asic */
	int cores;
	struct cgpu_info *first_proc;

	int freq;
};

struct knc_titan_info {
	struct spi_port *spi;
	struct cgpu_info *cgpu;
	int cores;
	struct knc_titan_die dies[KNC_TITAN_MAX_ASICS][KNC_TITAN_DIES_PER_ASIC];

	bool need_flush;
	struct work *workqueue;
	int workqueue_size;
	int workqueue_max;
	int next_id;

	struct work *devicework;
};

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
	struct spi_port *spi;
	struct knc_titan_info *knc;
	int cores = 0, asic, die;
	struct titan_info_response resp;
	char repr[6];

	cgpu = malloc(sizeof(*cgpu));
	if (unlikely(!cgpu))
		quit(1, "Failed to alloc cgpu_info");

	if (!prev_cgpu) {
		spi = calloc(1, sizeof(*spi));
		if (unlikely(!spi))
			quit(1, "Failed to alloc spi_port");

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

		knc = calloc(1, sizeof(*knc));
		if (unlikely(!knc))
			quit(1, "Failed to alloc knc_titan_info");

		knc->spi = spi;
		knc->cgpu = cgpu;
		knc->workqueue_max = 1;
	} else {
		knc = prev_cgpu->device_data;
		spi = knc->spi;
	}

	snprintf(repr, sizeof(repr), "%s %s", knc_titan_drv.name, devpath);
	asic = atoi(devpath);
	for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
		if (!knc_titan_spi_get_info(repr, spi, &resp, die, KNC_TITAN_CORES_PER_DIE))
			continue;
		if (0 < resp.cores) {
			knc->dies[asic][die] = (struct knc_titan_die) {
				.asicno = asic,
				.dieno = die,
				.cores = resp.cores,
				.first_proc = cgpu,
				.freq = KNC_TITAN_DEFAULT_FREQUENCY,
			};
			cores += resp.cores;
		} else {
			knc->dies[asic][die] = (struct knc_titan_die) {
				.asicno = -INT_MAX,
				.dieno = -INT_MAX,
				.cores = 0,
				.first_proc = NULL,
			};
		}
	}
	if (0 == cores) {
		free(cgpu);
		if (!prev_cgpu) {
			free(knc);
			close(spi->fd);
			free(spi);
		}
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
		.device_data = knc,
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
	struct knc_titan_core *knccore;
	struct knc_titan_info *knc;
	int i, asic, die, core_base;
	int total_cores = 0;

	for (proc = cgpu; proc; ) {
		if (proc->device != proc) {
			applog(LOG_WARNING, "%"PRIpreprv": Extra processor?", proc->proc_repr);
			proc = proc->next_proc;
			continue;
		}

		asic = atoi(proc->device_path);
		knc = proc->device_data;

		die = 0;
		core_base = 0;
		for (i = 0; i < max_cores; ++i) {
			while (i >= (core_base + knc->dies[asic][die].cores)) {
				core_base += knc->dies[asic][die].cores;
				if (++die >= KNC_TITAN_DIES_PER_ASIC)
					break;
			}
			if (die >= KNC_TITAN_DIES_PER_ASIC)
				break;

			mythr = proc->thr[0];
			mythr->cgpu_data = knccore = malloc(sizeof(*knccore));
			if (unlikely(!knccore))
				quit(1, "Failed to alloc knc_titan_core");
			*knccore = (struct knc_titan_core) {
				.asicno = asic,
				.dieno = die,
				.coreno = i - core_base,
				.next_slot = 1,
				.die = &(knc->dies[asic][die]),
				.proc = proc,
				.hwerr_in_row = 0,
				.hwerr_disable_time = KNC_TITAN_HWERR_DISABLE_SECS,
			};
			timer_set_now(&knccore->enable_at);
			proc->device_data = knc;
			++total_cores;
			applog(LOG_DEBUG, "%s Allocated core %d:%d:%d", proc->device->dev_repr, asic, die, (i - core_base));

			proc = proc->next_proc;
			if ((!proc) || proc->device == proc)
				break;
		}

		knc->cores = total_cores;
	}

	cgpu_set_defaults(cgpu);
	if (0 >= total_cores)
		return false;

	/* Init nonce ranges for cores */
	double nonce_step = 4294967296.0 / total_cores;
	double nonce_f = 0.0;
	struct titan_setup_core_params setup_params = {
		.bad_address_mask = {0, 0},
		.bad_address_match = {0x3FF, 0x3FF},
		.difficulty = 0xC,
		.thread_enable = 0xFF,
		.thread_base_address = {0, 1, 2, 3, 4, 5, 6, 7},
		.lookup_gap_mask = {0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7},
		.N_mask = {0, 0, 0, 0, 0, 0, 0, 0},
		.N_shift = {0, 0, 0, 0, 0, 0, 0, 0},
		.nonce_bottom = 0,
		.nonce_top = 0xFFFFFFFF,
	};

	for (proc = cgpu; proc; proc = proc->next_proc) {
		nonce_f += nonce_step;
		setup_params.nonce_bottom = setup_params.nonce_top + 1;
		if (NULL != proc->next_proc)
			setup_params.nonce_top = nonce_f;
		else
			setup_params.nonce_top = 0xFFFFFFFF;
		knc = proc->device_data;
		mythr = proc->thr[0];
		knccore = mythr->cgpu_data;
		applog(LOG_DEBUG, "%s Setup core %d:%d:%d, nonces 0x%08X - 0x%08X", proc->device->dev_repr, knccore->asicno, knccore->dieno, knccore->coreno, setup_params.nonce_bottom, setup_params.nonce_top);
		knc_titan_setup_core(proc->device->dev_repr, knc->spi, &setup_params, knccore->dieno, knccore->coreno);
	}

	timer_set_now(&thr->tv_poll);

	return true;
}

static void knc_titan_set_queue_full(struct knc_titan_info * const knc)
{
	const bool full = (knc->workqueue_size >= knc->workqueue_max);
	struct cgpu_info *proc;

	for (proc = knc->cgpu; proc; proc = proc->next_proc) {
		struct thr_info * const thr = proc->thr[0];
		thr->queue_full = full;
	}
}

static void knc_titan_remove_local_queue(struct knc_titan_info * const knc, struct work * const work)
{
	DL_DELETE(knc->workqueue, work);
	free_work(work);
	--knc->workqueue_size;
}

static void knc_titan_prune_local_queue(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_titan_info * const knc = cgpu->device_data;
	struct work *work, *tmp;

	DL_FOREACH_SAFE(knc->workqueue, work, tmp) {
		if (stale_work(work, false))
			knc_titan_remove_local_queue(knc, work);
	}
	knc_titan_set_queue_full(knc);
}

static bool knc_titan_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_titan_info * const knc = cgpu->device_data;

	if (knc->workqueue_size >= knc->workqueue_max) {
		knc_titan_prune_local_queue(thr);
		if (thr->queue_full)
			return false;
	}

	DL_APPEND(knc->workqueue, work);
	++knc->workqueue_size;

	knc_titan_set_queue_full(knc);
	if (thr->queue_full)
		knc_titan_prune_local_queue(thr);

	return true;
}

#define HASH_LAST_ADDED(head, out)  \
	(out = (head) ? (ELMT_FROM_HH((head)->hh.tbl, (head)->hh.tbl->tail)) : NULL)

static void knc_titan_queue_flush(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_titan_info * const knc = cgpu->device_data;
	struct work *work, *tmp;

	if (knc->cgpu != cgpu)
		return;

	DL_FOREACH_SAFE(knc->workqueue, work, tmp){
		knc_titan_remove_local_queue(knc, work);
	}
	knc_titan_set_queue_full(knc);

	HASH_LAST_ADDED(knc->devicework, work);
	if (work && stale_work(work, true)) {
		knc->need_flush = true;
		timer_set_now(&thr->tv_poll);
	}
}

static void knc_titan_poll(struct thr_info * const thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct knc_titan_info * const knc = cgpu->device_data;
	struct spi_port * const spi = knc->spi;
	struct knc_titan_core *knccore;
	struct work *work, *tmp;
	int workaccept = 0;
	unsigned long delay_usecs = KNC_POLL_INTERVAL_US;
	struct titan_report report;
	bool urgent = false;

	knc_titan_prune_local_queue(thr);

	spi_clear_buf(spi);
	if (knc->need_flush) {
		applog(LOG_NOTICE, "%s: Flushing stale works", knc_titan_drv.dname);
		urgent = true;
	}
	knccore = cgpu->thr[0]->cgpu_data;
	DL_FOREACH(knc->workqueue, work) {
		bool work_accepted;
		if (!knc_titan_set_work(cgpu->dev_repr, knc->spi, &report, 0, 0xFFFF, knccore->next_slot, work, urgent, &work_accepted))
			work_accepted = false;
		if (!work_accepted)
			break;
		if (++(knccore->next_slot) >= 16)
			knccore->next_slot = 1;
		urgent = false;
		++workaccept;
	}

	applog(LOG_DEBUG, "%s: %d jobs accepted to queue (max=%d)", knc_titan_drv.dname, workaccept, knc->workqueue_max);

	while (true) {
		/* TODO: collect reports from processors */
	}

	if (knc->need_flush) {
		knc->need_flush = false;
		HASH_ITER(hh, knc->devicework, work, tmp)
		{
			HASH_DEL(knc->devicework, work);
			free_work(work);
		}
		delay_usecs = 0;
	}

	if (workaccept) {
		if (workaccept >= knc->workqueue_max) {
			knc->workqueue_max = workaccept;
			delay_usecs = 0;
		}
		DL_FOREACH_SAFE(knc->workqueue, work, tmp) {
			--knc->workqueue_size;
			DL_DELETE(knc->workqueue, work);
			work->device_id = knc->next_id++;
			HASH_ADD(hh, knc->devicework, device_id, sizeof(work->device_id), work);
			if (!--workaccept)
				break;
		}
		knc_titan_set_queue_full(knc);
	}

	timer_set_delay_from_now(&thr->tv_poll, delay_usecs);
}

/*
 * specify settings / options via RPC or command line
 */

/* support for --set-device
 * must be set before probing the device
 */
static void knc_titan_set_clock_freq(struct cgpu_info * const device, int const val)
{
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

	/* specify mining type - queue */
	.minerloop = minerloop_queue,
	.queue_append = knc_titan_queue_append,
	.queue_flush = knc_titan_queue_flush,
	.poll = knc_titan_poll,

	/* TUI support - e.g. setting clock via UI */
#ifdef HAVE_CURSES
	.proc_wlogprint_status = knc_titan_wlogprint_status,
	.proc_tui_wlogprint_choices = knc_titan_tui_wlogprint_choices,
	.proc_tui_handle_choice = knc_titan_tui_handle_choice,
#endif
};
