/*
 * Copyright 2011-2013 Luke Dashjr
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "compat.h"
#include "deviceapi.h"
#include "logging.h"
#include "miner.h"
#include "util.h"

bool hashes_done(struct thr_info *thr, int64_t hashes, struct timeval *tvp_hashes, uint32_t *max_nonce)
{
	struct cgpu_info *cgpu = thr->cgpu;
	const long cycle = opt_log_interval / 5 ? : 1;
	
	if (unlikely(hashes == -1)) {
		time_t now = time(NULL);
		if (difftime(now, cgpu->device_last_not_well) > 1.)
			dev_error(cgpu, REASON_THREAD_ZERO_HASH);
		
		if (thr->scanhash_working && opt_restart) {
			applog(LOG_ERR, "%"PRIpreprv" failure, attempting to reinitialize", cgpu->proc_repr);
			thr->scanhash_working = false;
			cgpu->reinit_backoff = 5.2734375;
			hashes = 0;
		} else {
			applog(LOG_ERR, "%"PRIpreprv" failure, disabling!", cgpu->proc_repr);
			cgpu->deven = DEV_RECOVER_ERR;
			return false;
		}
	}
	else
		thr->scanhash_working = true;
	
	thr->hashes_done += hashes;
	if (hashes > cgpu->max_hashes)
		cgpu->max_hashes = hashes;
	
	timeradd(&thr->tv_hashes_done, tvp_hashes, &thr->tv_hashes_done);
	
	// max_nonce management (optional)
	if (unlikely((long)thr->tv_hashes_done.tv_sec < cycle)) {
		int mult;
		
		if (likely(!max_nonce || *max_nonce == 0xffffffff))
			return true;
		
		mult = 1000000 / ((thr->tv_hashes_done.tv_usec + 0x400) / 0x400) + 0x10;
		mult *= cycle;
		if (*max_nonce > (0xffffffff * 0x400) / mult)
			*max_nonce = 0xffffffff;
		else
			*max_nonce = (*max_nonce * mult) / 0x400;
	} else if (unlikely(thr->tv_hashes_done.tv_sec > cycle) && max_nonce)
		*max_nonce = *max_nonce * cycle / thr->tv_hashes_done.tv_sec;
	else if (unlikely(thr->tv_hashes_done.tv_usec > 100000) && max_nonce)
		*max_nonce = *max_nonce * 0x400 / (((cycle * 1000000) + thr->tv_hashes_done.tv_usec) / (cycle * 1000000 / 0x400));
	
	hashmeter2(thr);
	
	return true;
}

/* A generic wait function for threads that poll that will wait a specified
 * time tdiff waiting on a work restart request. Returns zero if the condition
 * was met (work restart requested) or ETIMEDOUT if not.
 */
int restart_wait(struct thr_info *thr, unsigned int mstime)
{
	struct timeval tv_timer, tv_now, tv_timeout;
	fd_set rfds;
	SOCKETTYPE wrn = thr->work_restart_notifier[0];
	int rv;
	
	if (unlikely(thr->work_restart_notifier[1] == INVSOCK))
	{
		// This is a bug!
		applog(LOG_ERR, "%"PRIpreprv": restart_wait called without a work_restart_notifier", thr->cgpu->proc_repr);
		nmsleep(mstime);
		return (thr->work_restart ? 0 : ETIMEDOUT);
	}
	
	gettimeofday(&tv_now, NULL);
	timer_set_delay(&tv_timer, &tv_now, mstime * 1000);
	while (true)
	{
		FD_ZERO(&rfds);
		FD_SET(wrn, &rfds);
		tv_timeout = tv_timer;
		rv = select(wrn + 1, &rfds, NULL, NULL, select_timeout(&tv_timeout, &tv_now));
		if (rv == 0)
			return ETIMEDOUT;
		if (rv > 0)
		{
			if (thr->work_restart)
				return 0;
			notifier_read(thr->work_restart_notifier);
		}
		gettimeofday(&tv_now, NULL);
	}
}

static
struct work *get_and_prepare_work(struct thr_info *thr)
{
	struct cgpu_info *proc = thr->cgpu;
	const struct device_api *api = proc->api;
	struct work *work;
	
	work = get_work(thr);
	if (!work)
		return NULL;
	if (api->prepare_work && !api->prepare_work(thr, work)) {
		free_work(work);
		applog(LOG_ERR, "%"PRIpreprv": Work prepare failed, disabling!", proc->proc_repr);
		proc->deven = DEV_RECOVER_ERR;
		return NULL;
	}
	return work;
}

// Miner loop to manage a single processor (with possibly multiple threads per processor)
void minerloop_scanhash(struct thr_info *mythr)
{
	struct cgpu_info *cgpu = mythr->cgpu;
	const struct device_api *api = cgpu->api;
	struct timeval tv_start, tv_end;
	struct timeval tv_hashes, tv_worktime;
	uint32_t max_nonce = api->can_limit_work ? api->can_limit_work(mythr) : 0xffffffff;
	int64_t hashes;
	struct work *work;
	const bool primary = (!mythr->device_thread) || mythr->primary_thread;
	
	while (1) {
		mythr->work_restart = false;
		request_work(mythr);
		work = get_and_prepare_work(mythr);
		if (!work)
			break;
		gettimeofday(&(work->tv_work_start), NULL);
		
		do {
			thread_reportin(mythr);
			gettimeofday(&tv_start, NULL);
			hashes = api->scanhash(mythr, work, work->blk.nonce + max_nonce);
			gettimeofday(&tv_end, NULL);
			thread_reportin(mythr);
			
			timersub(&tv_end, &tv_start, &tv_hashes);
			if (!hashes_done(mythr, hashes, &tv_hashes, api->can_limit_work ? &max_nonce : NULL))
				goto disabled;
			
			if (unlikely(mythr->work_restart)) {
				/* Apart from device_thread 0, we stagger the
				 * starting of every next thread to try and get
				 * all devices busy before worrying about
				 * getting work for their extra threads */
				if (!primary) {
					struct timespec rgtp;

					rgtp.tv_sec = 0;
					rgtp.tv_nsec = 250 * mythr->device_thread * 1000000;
					nanosleep(&rgtp, NULL);
				}
				break;
			}
			
			if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
disabled:
				mt_disable(mythr);
			
			timersub(&tv_end, &work->tv_work_start, &tv_worktime);
		} while (!abandon_work(work, &tv_worktime, cgpu->max_hashes));
		free_work(work);
	}
}

bool do_job_prepare(struct thr_info *mythr, struct timeval *tvp_now)
{
	struct cgpu_info *proc = mythr->cgpu;
	const struct device_api *api = proc->api;
	struct timeval tv_worktime;
	
	mythr->tv_morework.tv_sec = -1;
	mythr->_job_transition_in_progress = true;
	if (mythr->work)
		timersub(tvp_now, &mythr->work->tv_work_start, &tv_worktime);
	if ((!mythr->work) || abandon_work(mythr->work, &tv_worktime, proc->max_hashes))
	{
		mythr->work_restart = false;
		request_work(mythr);
		// FIXME: Allow get_work to return NULL to retry on notification
		if (mythr->next_work)
			free_work(mythr->next_work);
		mythr->next_work = get_and_prepare_work(mythr);
		if (!mythr->next_work)
			return false;
		mythr->starting_next_work = true;
		api->job_prepare(mythr, mythr->next_work, mythr->_max_nonce);
	}
	else
	{
		mythr->starting_next_work = false;
		api->job_prepare(mythr, mythr->work, mythr->_max_nonce);
	}
	job_prepare_complete(mythr);
	return true;
}

void job_prepare_complete(struct thr_info *mythr)
{
	if (unlikely(mythr->busy_state == TBS_GETTING_RESULTS))
		return;
	if (mythr->work)
	{
		if (true /* TODO: job is near complete */ || unlikely(mythr->work_restart))
			do_get_results(mythr, true);
		else
		{}  // TODO: Set a timer to call do_get_results when job is near complete
	}
	else  // no job currently running
		do_job_start(mythr);
}

void do_get_results(struct thr_info *mythr, bool proceed_with_new_job)
{
	struct cgpu_info *proc = mythr->cgpu;
	const struct device_api *api = proc->api;
	struct work *work = mythr->work;
	
	mythr->_job_transition_in_progress = true;
	mythr->tv_results_jobstart = mythr->tv_jobstart;
	mythr->_proceed_with_new_job = proceed_with_new_job;
	if (api->job_get_results)
		api->job_get_results(mythr, work);
	else
		job_results_fetched(mythr);
}

void job_results_fetched(struct thr_info *mythr)
{
	if (mythr->_proceed_with_new_job)
		do_job_start(mythr);
	else
	{
		struct timeval tv_now;
		
		gettimeofday(&tv_now, NULL);
		
		do_process_results(mythr, &tv_now, mythr->prev_work, true);
	}
}

void do_job_start(struct thr_info *mythr)
{
	struct cgpu_info *proc = mythr->cgpu;
	const struct device_api *api = proc->api;
	
	thread_reportin(mythr);
	api->job_start(mythr);
}

void mt_job_transition(struct thr_info *mythr)
{
	struct timeval tv_now;
	
	gettimeofday(&tv_now, NULL);
	
	if (mythr->starting_next_work)
	{
		mythr->next_work->tv_work_start = tv_now;
		if (mythr->prev_work)
			free_work(mythr->prev_work);
		mythr->prev_work = mythr->work;
		mythr->work = mythr->next_work;
		mythr->next_work = NULL;
	}
	mythr->tv_jobstart = tv_now;
	mythr->_job_transition_in_progress = false;
}

void job_start_complete(struct thr_info *mythr)
{
	struct timeval tv_now;
	
	gettimeofday(&tv_now, NULL);
	
	do_process_results(mythr, &tv_now, mythr->prev_work, false);
}

void job_start_abort(struct thr_info *mythr, bool failure)
{
	struct cgpu_info *proc = mythr->cgpu;
	
	if (failure)
		proc->deven = DEV_RECOVER_ERR;
	mythr->work = NULL;
	mythr->_job_transition_in_progress = false;
}

bool do_process_results(struct thr_info *mythr, struct timeval *tvp_now, struct work *work, bool stopping)
{
	struct cgpu_info *proc = mythr->cgpu;
	const struct device_api *api = proc->api;
	struct timeval tv_hashes;
	int64_t hashes = 0;
	
	if (api->job_process_results)
		hashes = api->job_process_results(mythr, work, stopping);
	thread_reportin(mythr);
	
	if (hashes)
	{
		timersub(tvp_now, &mythr->tv_results_jobstart, &tv_hashes);
		if (!hashes_done(mythr, hashes, &tv_hashes, api->can_limit_work ? &mythr->_max_nonce : NULL))
			return false;
	}
	
	return true;
}

static
void do_notifier_select(struct thr_info *thr, struct timeval *tvp_timeout)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct timeval tv_now;
	int maxfd;
	fd_set rfds;
	
	gettimeofday(&tv_now, NULL);
	FD_ZERO(&rfds);
	FD_SET(thr->notifier[0], &rfds);
	maxfd = thr->notifier[0];
	FD_SET(thr->work_restart_notifier[0], &rfds);
	set_maxfd(&maxfd, thr->work_restart_notifier[0]);
	if (thr->mutex_request[1] != INVSOCK)
	{
		FD_SET(thr->mutex_request[0], &rfds);
		set_maxfd(&maxfd, thr->mutex_request[0]);
	}
	if (select(maxfd + 1, &rfds, NULL, NULL, select_timeout(tvp_timeout, &tv_now)) < 0)
		return;
	if (thr->mutex_request[1] != INVSOCK && FD_ISSET(thr->mutex_request[0], &rfds))
	{
		// FIXME: This can only handle one request at a time!
		pthread_mutex_t *mutexp = &cgpu->device_mutex;
		notifier_read(thr->mutex_request);
		mutex_lock(mutexp);
		pthread_cond_signal(&cgpu->device_cond);
		pthread_cond_wait(&cgpu->device_cond, mutexp);
		mutex_unlock(mutexp);
	}
	if (FD_ISSET(thr->notifier[0], &rfds)) {
		notifier_read(thr->notifier);
	}
	if (FD_ISSET(thr->work_restart_notifier[0], &rfds))
		notifier_read(thr->work_restart_notifier);
}

void minerloop_async(struct thr_info *mythr)
{
	struct thr_info *thr = mythr;
	struct cgpu_info *cgpu = mythr->cgpu;
	const struct device_api *api = cgpu->api;
	struct timeval tv_now;
	struct timeval tv_timeout;
	struct cgpu_info *proc;
	bool is_running, should_be_running;
	
	if (mythr->work_restart_notifier[1] == -1)
		notifier_init(mythr->work_restart_notifier);
	
	while (1) {
		tv_timeout.tv_sec = -1;
		gettimeofday(&tv_now, NULL);
		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			mythr = proc->thr[0];
			
			// Nothing should happen while we're starting a job
			if (unlikely(mythr->busy_state == TBS_STARTING_JOB))
				goto defer_events;
			
			is_running = mythr->work;
			should_be_running = (proc->deven == DEV_ENABLED && !mythr->pause);
			
			if (should_be_running)
			{
				if (unlikely(!(is_running || mythr->_job_transition_in_progress)))
				{
					mt_disable_finish(mythr);
					goto djp;
				}
				if (unlikely(mythr->work_restart))
					goto djp;
			}
			else  // ! should_be_running
			{
				if (unlikely(is_running && !mythr->_job_transition_in_progress))
				{
disabled: ;
					mythr->tv_morework.tv_sec = -1;
					if (mythr->busy_state != TBS_GETTING_RESULTS)
						do_get_results(mythr, false);
					else
						// Avoid starting job when pending result fetch completes
						mythr->_proceed_with_new_job = false;
				}
			}
			
			if (timer_passed(&mythr->tv_morework, &tv_now))
			{
djp: ;
				if (!do_job_prepare(mythr, &tv_now))
					goto disabled;
			}
			
defer_events:
			if (timer_passed(&mythr->tv_poll, &tv_now))
				api->poll(mythr);
			
			reduce_timeout_to(&tv_timeout, &mythr->tv_morework);
			reduce_timeout_to(&tv_timeout, &mythr->tv_poll);
		}
		
		do_notifier_select(thr, &tv_timeout);
	}
}

static
void do_queue_flush(struct thr_info *mythr)
{
	struct cgpu_info *proc = mythr->cgpu;
	const struct device_api *api = proc->api;
	
	api->queue_flush(mythr);
	if (mythr->next_work)
	{
		free_work(mythr->next_work);
		mythr->next_work = NULL;
	}
}

void minerloop_queue(struct thr_info *thr)
{
	struct thr_info *mythr;
	struct cgpu_info *cgpu = thr->cgpu;
	const struct device_api *api = cgpu->api;
	struct timeval tv_now;
	struct timeval tv_timeout;
	struct cgpu_info *proc;
	bool should_be_running;
	struct work *work;
	
	if (thr->work_restart_notifier[1] == -1)
		notifier_init(thr->work_restart_notifier);
	
	while (1) {
		tv_timeout.tv_sec = -1;
		gettimeofday(&tv_now, NULL);
		for (proc = cgpu; proc; proc = proc->next_proc)
		{
			mythr = proc->thr[0];
			
			should_be_running = (proc->deven == DEV_ENABLED && !mythr->pause);
redo:
			if (should_be_running)
			{
				if (unlikely(!mythr->_last_sbr_state))
				{
					mt_disable_finish(mythr);
					mythr->_last_sbr_state = should_be_running;
				}
				
				if (unlikely(mythr->work_restart))
				{
					mythr->work_restart = false;
					do_queue_flush(mythr);
				}
				
				while (!mythr->queue_full)
				{
					if (mythr->next_work)
					{
						work = mythr->next_work;
						mythr->next_work = NULL;
					}
					else
					{
						request_work(mythr);
						// FIXME: Allow get_work to return NULL to retry on notification
						work = get_and_prepare_work(mythr);
					}
					if (!work)
						break;
					if (!api->queue_append(mythr, work))
						mythr->next_work = work;
				}
			}
			else
			if (unlikely(mythr->_last_sbr_state))
			{
				mythr->_last_sbr_state = should_be_running;
				do_queue_flush(mythr);
			}
			
			if (timer_passed(&mythr->tv_poll, &tv_now))
				api->poll(mythr);
			
			should_be_running = (proc->deven == DEV_ENABLED && !mythr->pause);
			if (should_be_running && !mythr->queue_full)
				goto redo;
			
			reduce_timeout_to(&tv_timeout, &mythr->tv_poll);
		}
		
		do_notifier_select(thr, &tv_timeout);
	}
}

void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	const struct device_api *api = cgpu->api;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	char threadname[20];
	snprintf(threadname, 20, "miner_%s", cgpu->proc_repr_ns);
	RenameThread(threadname);

	if (api->thread_init && !api->thread_init(mythr)) {
		dev_error(cgpu, REASON_THREAD_FAIL_INIT);
		for (struct cgpu_info *slave = cgpu->next_proc; slave && !slave->threads; slave = slave->next_proc)
			dev_error(slave, REASON_THREAD_FAIL_INIT);
		goto out;
	}

	thread_reportout(mythr);
	applog(LOG_DEBUG, "Popping ping in miner thread");
	notifier_read(mythr->notifier);  // Wait for a notification to start

	if (api->minerloop)
		api->minerloop(mythr);
	else
		minerloop_scanhash(mythr);

out:
	if (api->thread_shutdown)
		api->thread_shutdown(mythr);

	thread_reportin(mythr);
	applog(LOG_ERR, "Thread %d failure, exiting", thr_id);
	notifier_destroy(mythr->notifier);

	return NULL;
}

bool add_cgpu(struct cgpu_info*cgpu)
{
	int lpcount;
	
	renumber_cgpu(cgpu);
	if (!cgpu->procs)
		cgpu->procs = 1;
	lpcount = cgpu->procs;
	cgpu->device = cgpu;
	
	cgpu->dev_repr = malloc(6);
	sprintf(cgpu->dev_repr, "%s%2u", cgpu->api->name, cgpu->device_id % 100);
	cgpu->dev_repr_ns = malloc(6);
	sprintf(cgpu->dev_repr_ns, "%s%u", cgpu->api->name, cgpu->device_id % 100);
	strcpy(cgpu->proc_repr, cgpu->dev_repr);
	sprintf(cgpu->proc_repr_ns, "%s%u", cgpu->api->name, cgpu->device_id);
	
	devices = realloc(devices, sizeof(struct cgpu_info *) * (total_devices + lpcount + 1));
	devices[total_devices++] = cgpu;
	
	if (lpcount > 1)
	{
		int ns;
		int tpp = cgpu->threads / lpcount;
		struct cgpu_info **nlp_p, *slave;
		
		// Note, strcpy instead of assigning a byte to get the \0 too
		strcpy(&cgpu->proc_repr[5], "a");
		ns = strlen(cgpu->proc_repr_ns);
		strcpy(&cgpu->proc_repr_ns[ns], "a");
		
		nlp_p = &cgpu->next_proc;
		for (int i = 1; i < lpcount; ++i)
		{
			slave = malloc(sizeof(*slave));
			*slave = *cgpu;
			slave->proc_id = i;
			slave->proc_repr[5] += i;
			slave->proc_repr_ns[ns] += i;
			slave->threads = tpp;
			devices[total_devices++] = slave;
			*nlp_p = slave;
			nlp_p = &slave->next_proc;
		}
		*nlp_p = NULL;
		cgpu->proc_id = 0;
		cgpu->threads -= (tpp * (lpcount - 1));
	}
	return true;
}
