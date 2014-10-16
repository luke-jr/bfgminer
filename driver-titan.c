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

#include "deviceapi.h"
#include "logging.h"
#include "miner.h"
#include "util.h"

#include "titan-asic.h"

#define	KNC_TITAN_DEFAULT_FREQUENCY	275

#define KNC_TITAN_HWERR_DISABLE_SECS	10

#define	KNC_POLL_INTERVAL_US		10000

#define	DIE_HEALTH_INTERVAL_SEC		20

/* Broadcast address to all cores in a die */
#define	ALL_CORES	0xFFFF

/* Work queue pre-fill level.
 * Must be high enough to supply all ASICs with works after a flush */
#define	WORK_QUEUE_PREFILL			20

/* Specify here minimum number of leading zeroes in hash */
#define	DEFAULT_DIFF_FILTERING_ZEROES	24
#define	DEFAULT_DIFF_FILTERING_FLOAT	(1. / ((double)(0x00000000FFFFFFFF >> DEFAULT_DIFF_FILTERING_ZEROES)))
#define	DEFAULT_DIFF_HASHES_PER_NONCE	(1 << DEFAULT_DIFF_FILTERING_ZEROES)

BFG_REGISTER_DRIVER(knc_titan_drv)

/* 3 - default number of threads per core */
static int opt_knc_threads_per_core = 3;

static const struct bfg_set_device_definition knc_titan_set_device_funcs[];

struct knc_titan_core {
	int asicno;
	int dieno; /* inside asic */
	int coreno; /* inside die */
	struct knc_titan_die *die;
	struct cgpu_info *proc;

	int hwerr_in_row;
	int hwerr_disable_time;
	struct timeval enable_at;
	struct timeval first_hwerr;

	struct nonce_report last_nonce;
};

struct knc_titan_die {
	int asicno;
	int dieno; /* inside asic */
	int cores;
	struct cgpu_info *first_proc;

	bool need_flush;
	int next_slot;
	/* First slot after flush. If next_slot reaches this, then
	 * we need to re-flush all the cores to avoid duplicating slot numbers
	 * for different works */
	int first_slot;

	struct timeval last_share;
	/* Don't use this! DC/DCs don't like broadcast urgent setworks */
	bool broadcast_flushes;

	int freq;
};

struct knc_titan_info {
	void *ctx;
	struct cgpu_info *cgpu;
	int cores;
	struct knc_titan_die dies[KNC_TITAN_MAX_ASICS][KNC_TITAN_DIES_PER_ASIC];

	struct work *workqueue;
	int workqueue_size;
	int workqueue_max;
	int next_id;

	struct work *devicework;
};

static bool knc_titan_detect_one(const char *devpath)
{
	static struct cgpu_info *prev_cgpu = NULL;
	struct cgpu_info *cgpu;
	void *ctx;
	struct knc_titan_info *knc;
	int cores = 0, asic, die;
	struct knc_die_info die_info;
	char repr[6];

	cgpu = malloc(sizeof(*cgpu));
	if (unlikely(!cgpu))
		quit(1, "Failed to alloc cgpu_info");

	if (!prev_cgpu) {
		if (NULL == (ctx = knc_trnsp_new(NULL))) {
			free(cgpu);
			return false;
		}

		knc = calloc(1, sizeof(*knc));
		if (unlikely(!knc))
			quit(1, "Failed to alloc knc_titan_info");

		knc->ctx = ctx;
		knc->cgpu = cgpu;
		knc->workqueue_max = WORK_QUEUE_PREFILL;
	} else {
		knc = prev_cgpu->device_data;
		ctx = knc->ctx;
	}

	snprintf(repr, sizeof(repr), "%s %s", knc_titan_drv.name, devpath);
	asic = atoi(devpath);
	for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
		die_info.cores = KNC_TITAN_CORES_PER_DIE; /* core hint */
		die_info.version = KNC_VERSION_TITAN;
		if (!knc_titan_get_info(repr, ctx, asic, die, &die_info))
			die_info.cores = -1;
		if (0 < die_info.cores) {
			knc->dies[asic][die] = (struct knc_titan_die) {
				.asicno = asic,
				.dieno = die,
				.cores = die_info.cores,
				.first_proc = cgpu,
				.freq = KNC_TITAN_DEFAULT_FREQUENCY,
			};
			cores += die_info.cores;
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
			knc_trnsp_free(ctx);
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
		.extra_work_queue = -1,
		.device_data = knc,
	};
	const bool rv = add_cgpu_slave(cgpu, prev_cgpu);
	if (!prev_cgpu) {
		cgpu->extra_work_queue += WORK_QUEUE_PREFILL - opt_queue;
		if (0 > cgpu->extra_work_queue)
			cgpu->extra_work_queue = 0;
	}
	prev_cgpu = cgpu;
	return rv;
}

static int knc_titan_detect_auto(void)
{
	const int first = 0, last = KNC_TITAN_MAX_ASICS - 1;
	char devpath[256];
	int found = 0, i;

	for (i = first; i <= last; ++i) {
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

static void knc_titan_clean_flush(const char *repr, void * const ctx, int asic, int die, int core)
{
	struct knc_report report;
	bool unused;
	knc_titan_set_work(repr, ctx, asic, die, core, 0, NULL, true, &unused, &report);
}

static uint32_t nonce_tops[KNC_TITAN_DIES_PER_ASIC][KNC_TITAN_CORES_PER_DIE];
static bool nonce_tops_inited = false;

static void get_nonce_range(int dieno, int coreno, uint32_t *nonce_bottom, uint32_t *nonce_top)
{
	if (!nonce_tops_inited) {
		uint32_t top;
		double nonce_f, nonce_step;
		int die, core;

		nonce_f = 0.0;
		nonce_step = 4294967296.0 / KNC_TITAN_CORES_PER_ASIC;

		for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
			for (core = 0; core < KNC_TITAN_CORES_PER_DIE; ++core) {
				nonce_f += nonce_step;
				if ((core < (KNC_TITAN_CORES_PER_DIE - 1)) || (die < (KNC_TITAN_DIES_PER_ASIC - 1)))
					top = nonce_f;
				else
					top = 0xFFFFFFFF;
				nonce_tops[die][core] = top;
			}
		}

		nonce_tops_inited = true;
	}

	*nonce_top = nonce_tops[dieno][coreno];
	if (coreno > 0) {
		*nonce_bottom = nonce_tops[dieno][coreno - 1] + 1;
		return;
	}
	if (dieno > 0) {
		*nonce_bottom = nonce_tops[dieno - 1][KNC_TITAN_CORES_PER_DIE - 1] + 1;
	}
	*nonce_bottom = 0;
}

static bool configure_one_die(struct knc_titan_info *knc, int asic, int die)
{
	struct cgpu_info *proc, *first_proc;
	struct thr_info *mythr;
	struct knc_titan_core *knccore;
	char *repr;
	struct knc_titan_die *die_p;

	if ((0 > asic) || (KNC_TITAN_MAX_ASICS <= asic) || (0 > die) || (KNC_TITAN_DIES_PER_ASIC <= die))
		return false;
	die_p = &(knc->dies[asic][die]);
	if (0 >= die_p->cores)
		return false;

	/* Init nonce ranges for cores */
	struct titan_setup_core_params setup_params = {
		.bad_address_mask = {0, 0},
		.bad_address_match = {0x3FF, 0x3FF},
		.difficulty = DEFAULT_DIFF_FILTERING_ZEROES - 1,
		.thread_enable = 0xFF,
		.thread_base_address = {0, 1, 2, 3, 4, 5, 6, 7},
		.lookup_gap_mask = {0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x7},
		.N_mask = {0, 0, 0, 0, 0, 0, 0, 0},
		.N_shift = {0, 0, 0, 0, 0, 0, 0, 0},
		.nonce_bottom = 0,
		.nonce_top = 0xFFFFFFFF,
	};
	fill_in_thread_params(opt_knc_threads_per_core, &setup_params);

	first_proc = die_p->first_proc;
	repr = first_proc->device->dev_repr;
	bool success = true;
	for (proc = first_proc; proc; proc = proc->next_proc) {
		mythr = proc->thr[0];
		knccore = mythr->cgpu_data;
		if ((asic != knccore->asicno) || (die != knccore->dieno))
			break;
		knc_titan_clean_flush(repr, knc->ctx, knccore->asicno, knccore->dieno, knccore->coreno);
		get_nonce_range(knccore->dieno, knccore->coreno, &setup_params.nonce_bottom, &setup_params.nonce_top);
		applog(LOG_DEBUG, "%s[%d:%d:%d]: Setup core, nonces 0x%08X - 0x%08X", repr, knccore->asicno, knccore->dieno, knccore->coreno, setup_params.nonce_bottom, setup_params.nonce_top);
		if (!knc_titan_setup_core_local(repr, knc->ctx, knccore->asicno, knccore->dieno, knccore->coreno, &setup_params))
			success = false;
	}
	applog(LOG_NOTICE, "%s[%d-%d] Die configur%s", repr, asic, die, success ? "ed successfully" : "ation failed");
	die_p->need_flush = true;
	timer_set_now(&(die_p->last_share));
	die_p->broadcast_flushes = false;

	return true;
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
	int asic_cores[KNC_TITAN_MAX_ASICS] = {0};

	for (proc = cgpu; proc; ) {
		proc->min_nonce_diff = DEFAULT_DIFF_FILTERING_FLOAT;
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
				.die = &(knc->dies[asic][die]),
				.proc = proc,
				.hwerr_in_row = 0,
				.hwerr_disable_time = KNC_TITAN_HWERR_DISABLE_SECS,
			};
			timer_set_now(&knccore->enable_at);
			proc->device_data = knc;
			++total_cores;
			++(asic_cores[asic]);
			applog(LOG_DEBUG, "%s Allocated core %d:%d:%d", proc->device->dev_repr, asic, die, (i - core_base));

			if (0 == knccore->coreno) {
				knc->dies[asic][die].first_proc = proc;
			}

			proc = proc->next_proc;
			if ((!proc) || proc->device == proc)
				break;
		}

		knc->cores = total_cores;
	}

	cgpu_set_defaults(cgpu);
	cgpu_setup_control_requests(cgpu);
	if (0 >= total_cores)
		return false;

	knc = cgpu->device_data;
	for (asic = 0; asic < KNC_TITAN_MAX_ASICS; ++asic) {
		for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
			configure_one_die(knc, asic, die);
			knc->dies[asic][die].next_slot = KNC_TITAN_MIN_WORK_SLOT_NUM;
			knc->dies[asic][die].first_slot = KNC_TITAN_MIN_WORK_SLOT_NUM;
		}
	}
	timer_set_now(&thr->tv_poll);

	return true;
}

static bool die_test_and_add(struct knc_titan_info * const knc, int asic, int die, char * const errbuf)
{
	struct knc_die_info die_info;
	char repr[6];

	snprintf(repr, sizeof(repr), "%s %d", knc_titan_drv.name, asic);
	die_info.cores = KNC_TITAN_CORES_PER_DIE; /* core hint */
	die_info.version = KNC_VERSION_TITAN;
	if (!knc_titan_get_info(repr, knc->ctx, asic, die, &die_info))
		die_info.cores = -1;
	if (0 < die_info.cores) {
		sprintf(errbuf, "Die[%d:%d] not detected", asic, die);
		return false;
	}

	/* TODO: add procs */
	sprintf(errbuf, "Die[%d:%d] has %d cores; was not added (addition not implemented)", asic, die, die_info.cores);

	return false;
}

static bool die_enable(struct knc_titan_info * const knc, int asic, int die, char * const errbuf)
{
	bool res = true;

	cgpu_request_control(knc->cgpu);
	if (0 >= knc->dies[asic][die].cores)
		res = die_test_and_add(knc, asic, die, errbuf);
	if (res) {
		res = configure_one_die(knc, asic, die);
	}
	cgpu_release_control(knc->cgpu);

	return res;
}

static bool die_disable(struct knc_titan_info * const knc, int asic, int die, char * const errbuf)
{
	cgpu_request_control(knc->cgpu);
	/* TODO: delete procs */
	cgpu_release_control(knc->cgpu);
	sprintf(errbuf, "die_disable[%d:%d] not imnplemented", asic, die);
	return false;
}

static bool die_reconfigure(struct knc_titan_info * const knc, int asic, int die, char * const errbuf)
{
	return die_enable(knc, asic, die, errbuf);
}

static bool knc_titan_prepare_work(struct thr_info *thr, struct work *work)
{
	struct cgpu_info * const cgpu = thr->cgpu;

	work->nonce_diff = cgpu->min_nonce_diff;
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
		int asic, die;
		for (asic = 0; asic < KNC_TITAN_MAX_ASICS; ++asic) {
			for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
				knc->dies[asic][die].need_flush = true;
			}
		}
		timer_set_now(&thr->tv_poll);
	}
}

#define	MAKE_WORKID(asic, die, slot)	((((uint32_t)(asic)) << 16) | ((uint32_t)(die) << 8) | ((uint32_t)(slot)))
#define	ASIC_FROM_WORKID(workid)		((((uint32_t)(workid)) >> 16) & 0xFF)
#define	DIE_FROM_WORKID(workid)			((((uint32_t)(workid)) >> 8) & 0xFF)
#define	SLOT_FROM_WORKID(workid)		(((uint32_t)(workid)) & 0xFF)

static bool knc_titan_process_report(struct knc_titan_info * const knc, struct knc_titan_core * const knccore, struct knc_report * const report)
{
	int i, tmp_int;
	struct work *work;
	struct cgpu_info * const proc = knccore->proc;
	bool ret = false;

	for (i = 0; i < KNC_TITAN_NONCES_PER_REPORT; ++i) {
		if ((report->nonce[i].slot == knccore->last_nonce.slot) &&
		    (report->nonce[i].nonce == knccore->last_nonce.nonce))
			break;
		ret = true;
		tmp_int = MAKE_WORKID(knccore->asicno, knccore->dieno, report->nonce[i].slot);
		HASH_FIND_INT(knc->devicework, &tmp_int, work);
		if (!work) {
			applog(LOG_WARNING, "%"PRIpreprv"[%d:%d:%d]: Got nonce for unknown work in slot %u", proc->proc_repr, knccore->asicno, knccore->dieno, knccore->coreno, (unsigned)report->nonce[i].slot);
			continue;
		}
		if (submit_nonce(proc->thr[0], work, report->nonce[i].nonce)) {
			hashes_done2(proc->thr[0], DEFAULT_DIFF_HASHES_PER_NONCE, NULL);
			knccore->hwerr_in_row = 0;
		}
	}
	knccore->last_nonce.slot = report->nonce[0].slot;
	knccore->last_nonce.nonce = report->nonce[0].nonce;
	return ret;
}

static void knc_titan_poll(struct thr_info * const thr)
{
	struct thr_info *mythr;
	struct cgpu_info * const cgpu = thr->cgpu, *proc;
	struct knc_titan_info * const knc = cgpu->device_data;
	struct knc_titan_core *knccore, *core1;
	struct work *work, *tmp;
	int workaccept = 0;
	unsigned long delay_usecs = KNC_POLL_INTERVAL_US;
	struct knc_report report;
	struct knc_die_info die_info;
	int asic;
	int die;
	struct knc_titan_die *die_p;
	struct timeval tv_now, tv_prev;
	bool any_was_flushed = false;

	knc_titan_prune_local_queue(thr);
	timer_set_now(&tv_prev);

	for (asic = 0; asic < KNC_TITAN_MAX_ASICS; ++asic) {
		for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
			die_p = &(knc->dies[asic][die]);
			if (0 >= die_p->cores)
				continue;
			struct cgpu_info *first_proc = die_p->first_proc;
			DL_FOREACH_SAFE(knc->workqueue, work, tmp) {
				bool work_accepted = false;
				bool need_replace;
				if (die_p->first_slot > KNC_TITAN_MIN_WORK_SLOT_NUM)
					need_replace = ((die_p->next_slot + 1) == die_p->first_slot);
				else
					need_replace = (die_p->next_slot == KNC_TITAN_MAX_WORK_SLOT_NUM);
				if (die_p->need_flush || need_replace) {
					bool unused;
					if (die_p->broadcast_flushes) {
						/* Use broadcast */
						if (knc_titan_set_work(first_proc->device->dev_repr, knc->ctx, asic, die, ALL_CORES, die_p->next_slot, work, true, &unused, &report)) {
							work_accepted = true;
						}
					} else {
						/* Use unicasts */
						bool work_acc_arr[die_p->cores];
						struct knc_report reports[die_p->cores];
						for (proc = first_proc; proc; proc = proc->next_proc) {
							mythr = proc->thr[0];
							core1 = mythr->cgpu_data;
							if ((core1->dieno != die) || (core1->asicno != asic))
								break;
							work_acc_arr[core1->coreno] = false;
						}
						if (knc_titan_set_work_multi(first_proc->device->dev_repr, knc->ctx, asic, die, 0, die_p->next_slot, work, true, work_acc_arr, reports, die_p->cores)) {
							for (proc = first_proc; proc; proc = proc->next_proc) {
								mythr = proc->thr[0];
								core1 = mythr->cgpu_data;
								if ((core1->dieno != die) || (core1->asicno != asic))
									break;
								if (work_acc_arr[core1->coreno]) {
									/* Submit stale shares just in case we are working with multi-coin pool
									 * and those shares still might be useful (merged mining case etc) */
									if (knc_titan_process_report(knc, core1, &(reports[core1->coreno])))
										timer_set_now(&(die_p->last_share));
									work_accepted = true;
								}
							}
						}
					}
				} else {
					if (!knc_titan_set_work(first_proc->dev_repr, knc->ctx, asic, die, ALL_CORES, die_p->next_slot, work, false, &work_accepted, &report))
						work_accepted = false;
				}
				knccore = first_proc->thr[0]->cgpu_data;
				if ((!work_accepted) || (NULL == knccore))
					break;
				bool was_flushed = false;
				if (die_p->need_flush || need_replace) {
					struct work *work1, *tmp1;
					applog(LOG_NOTICE, "%s[%d-%d] Flushing stale works (%s)", first_proc->dev_repr, asic, die,
					       die_p->need_flush ? "New work" : "Slot collision");
					die_p->need_flush = false;
					die_p->first_slot = die_p->next_slot;
					HASH_ITER(hh, knc->devicework, work1, tmp1) {
						if ( (asic == ASIC_FROM_WORKID(work1->device_id)) &&
							 (die == DIE_FROM_WORKID(work1->device_id)) ) {
							HASH_DEL(knc->devicework, work1);
							free_work(work1);
						}
					}
					delay_usecs = 0;
					was_flushed = true;
					any_was_flushed = true;
				}
				--knc->workqueue_size;
				DL_DELETE(knc->workqueue, work);
				work->device_id = MAKE_WORKID(asic, die, die_p->next_slot);
				HASH_ADD(hh, knc->devicework, device_id, sizeof(work->device_id), work);
				if (++(die_p->next_slot) > KNC_TITAN_MAX_WORK_SLOT_NUM)
					die_p->next_slot = KNC_TITAN_MIN_WORK_SLOT_NUM;
				++workaccept;
				/* If we know for sure that this work was urgent, then we don't need to hurry up
				 * with filling next slot, we have plenty of time until current work completes.
				 * So, better to proceed with other ASICs/dies. */
				if (was_flushed)
					break;
			}
		}
	}

	applog(LOG_DEBUG, "%s: %d jobs accepted to queue (max=%d)", knc_titan_drv.dname, workaccept, knc->workqueue_max);
	timer_set_now(&tv_now);
	if (any_was_flushed) {
		double diff = ((tv_now.tv_sec - tv_prev.tv_sec) * 1000000.0 + (tv_now.tv_usec - tv_prev.tv_usec)) / 1000000.0;
		applog(LOG_INFO, "%s: Flush took %f secs", knc_titan_drv.dname, diff);
	}

	for (asic = 0; asic < KNC_TITAN_MAX_ASICS; ++asic) {
		for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
			die_p = &(knc->dies[asic][die]);
			if (0 >= die_p->cores)
				continue;
			die_info.cores = die_p->cores; /* core hint */
			die_info.version = KNC_VERSION_TITAN;
			if (!knc_titan_get_info(cgpu->dev_repr, knc->ctx, asic, die, &die_info))
				continue;
			for (proc = die_p->first_proc; proc; proc = proc->next_proc) {
				mythr = proc->thr[0];
				knccore = mythr->cgpu_data;
				if ((knccore->dieno != die) || (knccore->asicno != asic))
					break;
				if (!die_info.has_report[knccore->coreno])
					continue;
				if (!knc_titan_get_report(proc->proc_repr, knc->ctx, asic, die, knccore->coreno, &report))
					continue;
				if (knc_titan_process_report(knc, knccore, &report))
					timer_set_now(&(die_p->last_share));
			}
		}

		/* Check die health */
		for (die = 0; die < KNC_TITAN_DIES_PER_ASIC; ++die) {
			die_p = &(knc->dies[asic][die]);
			if (0 >= die_p->cores)
				continue;
			if (timer_elapsed(&(die_p->last_share), &tv_now) < DIE_HEALTH_INTERVAL_SEC)
				continue;
			/* Reconfigure die */
			configure_one_die(knc, asic, die);
		}
	}

	if (workaccept) {
		if (workaccept >= knc->workqueue_max) {
			knc->workqueue_max = workaccept;
			delay_usecs = 0;
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

static const char *knc_titan_die_ena(struct cgpu_info * const device, const char * const option, const char * const setting, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	int asic, die;
	char str[256];

	/* command format: ASIC:N;DIE:N;MODE:ENABLE|DISABLE|RECONFIGURE */
	if (3 != sscanf(setting, "ASIC:%d;DIE:%d;MODE:%255s", &asic, &die, str)) {
error_bad_params:
		sprintf(replybuf, "Die setup failed, bad parameters");
		return replybuf;
	}
	if (0 == strncasecmp(str, "enable", sizeof(str) - 1)) {
		if (!die_enable(device->device_data, asic, die, replybuf))
			return replybuf;
	} else if (0 == strncasecmp(str, "disable", sizeof(str) - 1)) {
		if (!die_disable(device->device_data, asic, die, replybuf))
			return replybuf;
	} else if (0 == strncasecmp(str, "reconfigure", sizeof(str) - 1)) {
		if (!die_reconfigure(device->device_data, asic, die, replybuf)) {
			/* Do not return error on reconfigure command!
			 * (or the whole bfgminer will be restarted) */
			*success = SDR_OK;
			return replybuf;
		}
	} else
		goto error_bad_params;
	sprintf(replybuf, "Die setup Ok; asic %d die %d cmd %s", asic, die, str);
	*success = SDR_OK;
	return replybuf;
}

static const struct bfg_set_device_definition knc_titan_set_device_funcs[] = {
	{ "clock", knc_titan_set_clock, NULL },
	{ "die", knc_titan_die_ena, NULL },
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
	.prepare_work = knc_titan_prepare_work,

	/* TUI support - e.g. setting clock via UI */
#ifdef HAVE_CURSES
	.proc_wlogprint_status = knc_titan_wlogprint_status,
	.proc_tui_wlogprint_choices = knc_titan_tui_wlogprint_choices,
	.proc_tui_handle_choice = knc_titan_tui_handle_choice,
#endif
};
