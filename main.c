
/*
 * Copyright 2011 Con Kolivas
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#ifndef WIN32
#include <sys/resource.h>
#endif
#include <getopt.h>
#include <jansson.h>
#include <curl/curl.h>
#include "compat.h"
#include "miner.h"
#include "findnonce.h"
#include "ocl.h"

#define PROGRAM_NAME		"cgminer"
#define DEF_RPC_URL		"http://127.0.0.1:8332/"
#define DEF_RPC_USERNAME	"rpcuser"
#define DEF_RPC_PASSWORD	"rpcpass"
#define DEF_RPC_USERPASS	DEF_RPC_USERNAME ":" DEF_RPC_PASSWORD

#ifdef __linux /* Linux specific policy and affinity management */
#include <sched.h>
static inline void drop_policy(void)
{
	struct sched_param param;

#ifdef SCHED_IDLE
	if (unlikely(sched_setscheduler(0, SCHED_IDLE, &param) == -1))
#endif
#ifdef SCHED_BATCH
		sched_setscheduler(0, SCHED_BATCH, &param);
#endif
}

static inline void affine_to_cpu(int id, int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	sched_setaffinity(0, sizeof(&set), &set);
	applog(LOG_INFO, "Binding cpu mining thread %d to cpu %d", id, cpu);
}
#else
static inline void drop_policy(void)
{
}

static inline void affine_to_cpu(int id, int cpu)
{
}
#endif
		
enum workio_commands {
	WC_GET_WORK,
	WC_SUBMIT_WORK,
	WC_DIE,
};

struct workio_cmd {
	enum workio_commands	cmd;
	struct thr_info		*thr;
	union {
		struct work	*work;
	} u;
};

enum sha256_algos {
	ALGO_C,			/* plain C */
	ALGO_4WAY,		/* parallel SSE2 */
	ALGO_VIA,		/* VIA padlock */
	ALGO_CRYPTOPP,		/* Crypto++ (C) */
	ALGO_CRYPTOPP_ASM32,	/* Crypto++ 32-bit assembly */
	ALGO_SSE2_64,		/* SSE2 for x86_64 */
};

static const char *algo_names[] = {
	[ALGO_C]		= "c",
#ifdef WANT_SSE2_4WAY
	[ALGO_4WAY]		= "4way",
#endif
#ifdef WANT_VIA_PADLOCK
	[ALGO_VIA]		= "via",
#endif
	[ALGO_CRYPTOPP]		= "cryptopp",
#ifdef WANT_CRYPTOPP_ASM32
	[ALGO_CRYPTOPP_ASM32]	= "cryptopp_asm32",
#endif
#ifdef WANT_X8664_SSE2
	[ALGO_SSE2_64]		= "sse2_64",
#endif
};

bool opt_debug = false;
bool opt_protocol = false;
bool opt_ndevs = false;
bool want_longpoll = true;
bool have_longpoll = false;
bool use_syslog = false;
static bool opt_quiet = false;
static int opt_retries = -1;
static int opt_fail_pause = 5;
static int opt_log_interval = 5;
static int opt_queue = 0;
int opt_vectors;
int opt_worksize;
int opt_scantime = 60;
static json_t *opt_config;
static const bool opt_time = true;
#ifdef WANT_X8664_SSE2
static enum sha256_algos opt_algo = ALGO_SSE2_64;
#else
static enum sha256_algos opt_algo = ALGO_C;
#endif
static int nDevs;
static int opt_g_threads = 2;
static int gpu_threads;
static int opt_n_threads = 1;
static int num_processors;
static int scan_intensity = 4;
static char *rpc_url;
static char *rpc_userpass;
static char *rpc_user, *rpc_pass;
struct thr_info *thr_info;
static int work_thr_id;
int longpoll_thr_id;
struct work_restart *work_restart = NULL;
pthread_mutex_t time_lock;
static pthread_mutex_t hash_lock;
static pthread_mutex_t qd_lock;
static double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;
static int accepted, rejected;
int hw_errors;
static int total_queued;

struct option_help {
	const char	*name;
	const char	*helptext;
};

static struct option_help options_help[] = {
	{ "help",
	  "(-h) Display this help text" },

	{ "algo XXX",
	  "(-a XXX) Specify sha256 implementation:\n"
	  "\tc\t\tLinux kernel sha256, implemented in C (default)"
#ifdef WANT_SSE2_4WAY
	  "\n\t4way\t\ttcatm's 4-way SSE2 implementation"
#endif
#ifdef WANT_VIA_PADLOCK
	  "\n\tvia\t\tVIA padlock implementation"
#endif
	  "\n\tcryptopp\tCrypto++ C/C++ implementation"
#ifdef WANT_CRYPTOPP_ASM32
	  "\n\tcryptopp_asm32\tCrypto++ 32-bit assembler implementation"
#endif
#ifdef WANT_X8664_SSE2
	  "\n\tsse2_64\t\tSSE2 implementation for x86_64 machines"
#endif
	  },

	  { "config FILE",
	  "(-c FILE) JSON-format configuration file (default: none)\n"
	  "See example-cfg.json for an example configuration." },

	{ "cpu-threads N",
	  "(-t N) Number of miner CPU threads (default: number of processors or 0 if GPU mining)" },

	{ "debug",
	  "(-D) Enable debug output (default: off)" },

	{ "gpu-threads N",
	  "(-g N) Number of threads per-GPU (0 - 10, default: 2)" },

	{ "intensity N",
	  "(-I) Intensity of scanning (0 - 14, default 4)" },

	{ "log N",
	  "(-l N) Interval in seconds between log output (default: 5)" },

	{ "ndevs",
	  "(-n) Display number of detected GPUs and exit" },

	{ "no-longpoll",
	  "Disable X-Long-Polling support (default: enabled)" },

	{ "pass PASSWORD",
	  "(-p PASSWORD) Password for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_PASSWORD ")" },

	{ "protocol-dump",
	  "(-P) Verbose dump of protocol-level activities (default: off)" },

	{ "queue N",
	  "(-Q N) Number of extra work items to queue (0 - 10, default 0)" },

	{ "quiet",
	  "(-q) Disable per-thread hashmeter output (default: off)" },

	{ "retries N",
	  "(-r N) Number of times to retry before giving up, if JSON-RPC call fails\n"
	  "\t(default: -1; use -1 for \"never\")" },

	{ "retry-pause N",
	  "(-R N) Number of seconds to pause, between retries\n"
	  "\t(default: 5)" },

	{ "scantime N",
	  "(-s N) Upper bound on time spent scanning current work,\n"
	  "\tin seconds. (default: 60)" },

#ifdef HAVE_SYSLOG_H
	{ "syslog",
	  "Use system log for output messages (default: standard error)" },
#endif

	{ "url URL",
	  "URL for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_URL ")" },

	{ "userpass USERNAME:PASSWORD",
	  "Username:Password pair for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERPASS ")" },

	{ "user USERNAME",
	  "(-u USERNAME) Username for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERNAME ")" },

	{ "vectors N",
	  "(-v N) Override detected optimal vector width (default: detected, 1,2 or 4)" },

	{ "worksize N",
	  "(-w N) Override detected optimal worksize (default: detected)" },

};

static struct option options[] = {
	{ "algo", 1, NULL, 'a' },
	{ "config", 1, NULL, 'c' },
	{ "cpu-threads", 1, NULL, 't' },
	{ "gpu-threads", 1, NULL, 'g' },
	{ "debug", 0, NULL, 'D' },
	{ "help", 0, NULL, 'h' },
	{ "intensity", 1, NULL, 'I' },
	{ "log", 1, NULL, 'l' },
	{ "ndevs", 0, NULL, 'n' },
	{ "no-longpoll", 0, NULL, 1003 },
	{ "pass", 1, NULL, 'p' },
	{ "protocol-dump", 0, NULL, 'P' },
	{ "queue", 1, NULL, 'Q' },
	{ "quiet", 0, NULL, 'q' },
	{ "retries", 1, NULL, 'r' },
	{ "retry-pause", 1, NULL, 'R' },
	{ "scantime", 1, NULL, 's' },
#ifdef HAVE_SYSLOG_H
	{ "syslog", 0, NULL, 1004 },
#endif
	{ "url", 1, NULL, 1001 },
	{ "user", 1, NULL, 'u' },
	{ "vectors", 1, NULL, 'v' },
	{ "worksize", 1, NULL, 'w' },
	{ "userpass", 1, NULL, 1002 },
};

static bool jobj_binary(const json_t *obj, const char *key,
			void *buf, size_t buflen)
{
	const char *hexstr;
	json_t *tmp;

	tmp = json_object_get(obj, key);
	if (unlikely(!tmp)) {
		applog(LOG_ERR, "JSON key '%s' not found", key);
		return false;
	}
	hexstr = json_string_value(tmp);
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "JSON key '%s' is not a string", key);
		return false;
	}
	if (!hex2bin(buf, hexstr, buflen))
		return false;

	return true;
}

static bool work_decode(const json_t *val, struct work *work)
{
	if (unlikely(!jobj_binary(val, "midstate",
			 work->midstate, sizeof(work->midstate)))) {
		applog(LOG_ERR, "JSON inval midstate");
		goto err_out;
	}

	if (unlikely(!jobj_binary(val, "data", work->data, sizeof(work->data)))) {
		applog(LOG_ERR, "JSON inval data");
		goto err_out;
	}

	if (unlikely(!jobj_binary(val, "hash1", work->hash1, sizeof(work->hash1)))) {
		applog(LOG_ERR, "JSON inval hash1");
		goto err_out;
	}

	if (unlikely(!jobj_binary(val, "target", work->target, sizeof(work->target)))) {
		applog(LOG_ERR, "JSON inval target");
		goto err_out;
	}

	memset(work->hash, 0, sizeof(work->hash));

	return true;

err_out:
	return false;
}

static bool submit_upstream_work(const struct work *work)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345];
	bool rc = false;
	struct cgpu_info *cgpu = thr_info[work->thr_id].cgpu;
	CURL *curl = curl_easy_init();

	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return rc;
	}

	/* build hex string */
	hexstr = bin2hex(work->data, sizeof(work->data));
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "submit_upstream_work OOM");
		goto out_nofree;
	}

	/* build JSON-RPC request */
	sprintf(s,
	      "{\"method\": \"getwork\", \"params\": [ \"%s\" ], \"id\":1}\r\n",
		hexstr);

	if (opt_debug)
		applog(LOG_DEBUG, "DBG: sending RPC call: %s", s);

	/* issue JSON-RPC request */
	val = json_rpc_call(curl, rpc_url, rpc_userpass, s, false, false);
	if (unlikely(!val)) {
		applog(LOG_ERR, "submit_upstream_work json_rpc_call failed");
		goto out;
	}

	res = json_object_get(val, "result");

	/* Theoretically threads could race when modifying accepted and
	 * rejected values but the chance of two submits completing at the
	 * same time is zero so there is no point adding extra locking */
	if (json_is_true(res)) {
		cgpu->accepted++;
		accepted++;
		if (opt_debug)
			applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
	} else {
		cgpu->rejected++;
		rejected++;
		if (opt_debug)
			applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
	}
	applog(LOG_INFO, "%sPU: %d  Accepted: %d  Rejected: %d  HW errors: %d",
	       cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, cgpu->accepted, cgpu->rejected, cgpu->hw_errors);

	json_decref(val);

	rc = true;
out:
	free(hexstr);
out_nofree:
	curl_easy_cleanup(curl);
	return rc;
}

static const char *rpc_req =
	"{\"method\": \"getwork\", \"params\": [], \"id\":0}\r\n";

static bool get_upstream_work(struct work *work)
{
	json_t *val;
	bool rc = false;
	CURL *curl = curl_easy_init();

	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return rc;
	}

	val = json_rpc_call(curl, rpc_url, rpc_userpass, rpc_req,
			    want_longpoll, false);
	if (unlikely(!val)) {
		applog(LOG_ERR, "Failed json_rpc_call in get_upstream_work");
		goto out;
	}

	rc = work_decode(json_object_get(val, "result"), work);

	json_decref(val);
out:
	curl_easy_cleanup(curl);

	return rc;
}

static void workio_cmd_free(struct workio_cmd *wc)
{
	if (!wc)
		return;

	switch (wc->cmd) {
	case WC_SUBMIT_WORK:
		free(wc->u.work);
		break;
	default: /* do nothing */
		break;
	}

	memset(wc, 0, sizeof(*wc));	/* poison */
	free(wc);
}

static void kill_work(void)
{
	struct workio_cmd *wc;

	applog(LOG_INFO, "Received kill message");
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to calloc wc in kill_work");
		/* We're just trying to die anyway, so forget graceful */
		exit (1);
	}

	wc->cmd = WC_DIE;
	wc->thr = 0;

	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		applog(LOG_ERR, "Failed to tq_push work in kill_work");
		exit (1);
	}
}

static char current_block[36];

static void *get_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	struct work *ret_work;
	int failures = 0;

	pthread_detach(pthread_self());
	ret_work = calloc(1, sizeof(*ret_work));
	if (unlikely(!ret_work)) {
		applog(LOG_ERR, "Failed to calloc ret_work in workio_get_work");
		kill_work();
		goto out;
	}

	/* obtain new work from bitcoin via JSON-RPC */
	while (!get_upstream_work(ret_work)) {
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			applog(LOG_ERR, "json_rpc_call failed, terminating workio thread");
			free(ret_work);
			kill_work();
			goto out;
		}

		/* pause, then restart work-request loop */
		applog(LOG_ERR, "json_rpc_call failed on get work, retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

	/* send work to requesting thread */
	if (unlikely(!tq_push(wc->thr->q, ret_work))) {
		applog(LOG_ERR, "Failed to tq_push work in workio_get_work");
		kill_work();
		free(ret_work);
	}

out:
	workio_cmd_free(wc);
	return NULL;
}

static bool workio_get_work(struct workio_cmd *wc)
{
	pthread_t get_thread;

	if (unlikely(pthread_create(&get_thread, NULL, get_work_thread, (void *)wc))) {
		applog(LOG_ERR, "Failed to create get_work_thread");
		return false;
	}
	return true;
}

static void *submit_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	int failures = 0;

	pthread_detach(pthread_self());
	if (unlikely(strncmp((const char *)wc->u.work->data, current_block, 36))) {
		applog(LOG_INFO, "Stale work detected, discarding");
		goto out;
	}

	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(wc->u.work)) {
		if (unlikely(strncmp((const char *)wc->u.work->data, current_block, 36))) {
			applog(LOG_INFO, "Stale work detected, discarding");
			goto out;
		}
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			applog(LOG_ERR, "Failed %d retries ...terminating workio thread", opt_retries);
			kill_work();
			goto out;
		}

		/* pause, then restart work-request loop */
		applog(LOG_ERR, "json_rpc_call failed on submit_work, retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

out:
	workio_cmd_free(wc);
	return NULL;
}

static bool workio_submit_work(struct workio_cmd *wc)
{
	pthread_t submit_thread;

	if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, (void *)wc))) {
		applog(LOG_ERR, "Failed to create submit_work_thread");
		return false;
	}
	return true;
}

static void *workio_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	bool ok = true;

	while (ok) {
		struct workio_cmd *wc;

		/* wait for workio_cmd sent to us, on our queue */
		wc = tq_pop(mythr->q, NULL);
		if (unlikely(!wc)) {
			ok = false;
			break;
		}

		/* process workio_cmd */
		switch (wc->cmd) {
		case WC_GET_WORK:
			ok = workio_get_work(wc);
			break;
		case WC_SUBMIT_WORK:
			ok = workio_submit_work(wc);
			break;
		case WC_DIE:
		default:
			ok = false;
			break;
		}
	}

	tq_freeze(mythr->q);

	return NULL;
}

static void hashmeter(int thr_id, struct timeval *diff,
		      unsigned long hashes_done)
{
	struct timeval temp_tv_end, total_diff;
	double khashes, secs;
	double total_secs;
	double local_secs;
	static double local_mhashes_done = 0;
	static double rolling_local = 0;
	double local_mhashes = (double)hashes_done / 1000000.0;

	/* Don't bother calculating anything if we're not displaying it */
	if (opt_quiet || !opt_log_interval)
		return;
	khashes = hashes_done / 1000.0;
	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);
	if (opt_debug)
		applog(LOG_DEBUG, "[thread %d: %lu hashes, %.0f khash/sec]",
			thr_id, hashes_done, hashes_done / secs);

	/* Totals are updated by all threads so can race without locking */
	pthread_mutex_lock(&hash_lock);
	gettimeofday(&temp_tv_end, NULL);
	timeval_subtract(&total_diff, &temp_tv_end, &total_tv_end);
	local_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);

	total_mhashes_done += local_mhashes;
	local_mhashes_done += local_mhashes;
	if (total_diff.tv_sec < opt_log_interval)
		/* Only update the total every opt_log_interval seconds */
		goto out_unlock;
	gettimeofday(&total_tv_end, NULL);

	/* Use a rolling average by faking an exponential decay over 5 * log */
	rolling_local = ((rolling_local * 0.9) + local_mhashes_done) / 1.9;

	timeval_subtract(&total_diff, &total_tv_end, &total_tv_start);
	total_secs = (double)total_diff.tv_sec +
		((double)total_diff.tv_usec / 1000000.0);
	applog(LOG_INFO, "[%.2f | %.2f Mhash/s] [%d Accepted] [%d Rejected] [%d HW errors]",
		rolling_local / local_secs,
		total_mhashes_done / total_secs, accepted, rejected, hw_errors);
	local_mhashes_done = 0;
out_unlock:
	pthread_mutex_unlock(&hash_lock);
}

/* This is overkill, but at least we'll know accurately how much work is
 * queued to prevent ever being left without work */
static void inc_queued(void)
{
	pthread_mutex_lock(&qd_lock);
	total_queued++;
	pthread_mutex_unlock(&qd_lock);
}

static void dec_queued(void)
{
	pthread_mutex_lock(&qd_lock);
	total_queued--;
	pthread_mutex_unlock(&qd_lock);
}

static int requests_queued(void)
{
	int ret;

	pthread_mutex_lock(&qd_lock);
	ret = total_queued;
	pthread_mutex_unlock(&qd_lock);
	return ret;
}

/* All work is queued flagged as being for thread 0 and then the mining thread
 * flags it as its own */
static bool queue_request(void)
{
	struct thr_info *thr = &thr_info[0];
	struct workio_cmd *wc;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc))
		return false;

	wc->cmd = WC_GET_WORK;
	wc->thr = thr;

	/* send work request to workio thread */
	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		workio_cmd_free(wc);
		return false;
	}
	inc_queued();
	return true;
}

static bool discard_request(void)
{
	struct thr_info *thr = &thr_info[0];
	struct work *work_heap;

	/* Just in case we fell in a hole and missed a queue filling */
	if (unlikely(!requests_queued()))
		return true;

	work_heap = tq_pop(thr->q, NULL);
	if (unlikely(!work_heap))
		return false;
	free(work_heap);
	dec_queued();
	return true;
}

static void flush_requests(void)
{
	int i, extra;

	extra = requests_queued();
	for (i = 0; i < extra; i++) {
		/* Queue a whole batch of new requests */
		if (unlikely(!queue_request())) {
			applog(LOG_ERR, "Failed to queue requests in flush_requests");
			kill_work();
			break;
		}
		/* Pop off the old requests. Cancelling the requests would be better
		* but is tricky */
		if (unlikely(!discard_request())) {
			applog(LOG_ERR, "Failed to discard requests in flush_requests");
			kill_work();
			break;
		}
	}
}

static bool get_work(struct work *work, bool queued)
{
	struct thr_info *thr = &thr_info[0];
	struct work *work_heap;
	bool ret = false;

	if (!queued || !requests_queued()) {
		if (unlikely(!queue_request()))
		goto out;
	}

	/* wait for 1st response, or get cached response */
	work_heap = tq_pop(thr->q, NULL);
	if (unlikely(!work_heap))
		goto out;
	dec_queued();

	memcpy(work, work_heap, sizeof(*work));
	memcpy(current_block, work->data, 36);
	ret = true;
	free(work_heap);
out:
	return ret;
}

static bool submit_work_sync(struct thr_info *thr, const struct work *work_in)
{
	struct workio_cmd *wc;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to calloc wc in submit_work_sync");
		return false;
	}

	wc->u.work = malloc(sizeof(*work_in));
	if (unlikely(!wc->u.work)) {
		applog(LOG_ERR, "Failed to calloc work in submit_work_sync");
		goto err_out;
	}

	wc->cmd = WC_SUBMIT_WORK;
	wc->thr = thr;
	memcpy(wc->u.work, work_in, sizeof(*work_in));

	/* send solution to workio thread */
	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		applog(LOG_ERR, "Failed to tq_push work in submit_work_sync");
		goto err_out;
	}

	return true;
err_out:
	workio_cmd_free(wc);
	return false;
}

bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	work->data[64+12+0] = (nonce>>0) & 0xff;
	work->data[64+12+1] = (nonce>>8) & 0xff;
	work->data[64+12+2] = (nonce>>16) & 0xff;
	work->data[64+12+3] = (nonce>>24) & 0xff;
	return submit_work_sync(thr, work);
}

static inline int cpu_from_thr_id(int thr_id)
{
	return (thr_id - gpu_threads) % num_processors;
}

static void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	uint32_t max_nonce = 0xffffff;
	bool needs_work = true;
	/* Try to cycle approximately 5 times before each log update */
	const unsigned long cycle = opt_log_interval / 5 ? : 1;
	/* Request the next work item at 2/3 of the scantime */
	unsigned const int request_interval = opt_scantime * 2 / 3 ? : 1;
	unsigned const long request_nonce = MAXTHREADS / 3 * 2;
	bool requested = false;

	/* Set worker threads to nice 19 and then preferentially to SCHED_IDLE
	 * and if that fails, then SCHED_BATCH. No need for this to be an
	 * error if it fails */
	setpriority(PRIO_PROCESS, 0, 19);
	drop_policy();

	/* Cpu affinity only makes sense if the number of threads is a multiple
	 * of the number of CPUs */
	if (!(opt_n_threads % num_processors))
		affine_to_cpu(thr_id - gpu_threads, cpu_from_thr_id(thr_id));

	while (1) {
		struct work work __attribute__((aligned(128)));
		unsigned long hashes_done;
		struct timeval tv_workstart, tv_start, tv_end, diff;
		uint64_t max64;
		bool rc;

		if (needs_work) {
			if (work_restart[thr_id].restart) {
				if (requested) {
					/* We have one extra request than desired now */
					if (unlikely(!discard_request())) {
						applog(LOG_ERR, "Failed to discard request in uminer thread");
						goto out;
					}
				} else
					requested = true;
			}
			gettimeofday(&tv_workstart, NULL);
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(&work, requested))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"mining thread %d", mythr->id);
				goto out;
			}
			work.thr_id = thr_id;
			needs_work = requested = false;
			work.blk.nonce = 0;
		}
		hashes_done = 0;
		gettimeofday(&tv_start, NULL);

		/* scan nonces for a proof-of-work hash */
		switch (opt_algo) {
		case ALGO_C:
			rc = scanhash_c(thr_id, work.midstate, work.data + 64,
				        work.hash1, work.hash, work.target,
					max_nonce, &hashes_done,
					work.blk.nonce);
			break;

#ifdef WANT_X8664_SSE2
		case ALGO_SSE2_64: {
			unsigned int rc5 =
			        scanhash_sse2_64(thr_id, work.midstate, work.data + 64,
						 work.hash1, work.hash,
						 work.target,
					         max_nonce, &hashes_done,
						 work.blk.nonce);
			rc = (rc5 == -1) ? false : true;
			}
			break;
#endif

#ifdef WANT_SSE2_4WAY
		case ALGO_4WAY: {
			unsigned int rc4 =
				ScanHash_4WaySSE2(thr_id, work.midstate, work.data + 64,
						  work.hash1, work.hash,
						  work.target,
						  max_nonce, &hashes_done,
						  work.blk.nonce);
			rc = (rc4 == -1) ? false : true;
			}
			break;
#endif

#ifdef WANT_VIA_PADLOCK
		case ALGO_VIA:
			rc = scanhash_via(thr_id, work.data, work.target,
					  max_nonce, &hashes_done,
					  work.blk.nonce);
			break;
#endif
		case ALGO_CRYPTOPP:
			rc = scanhash_cryptopp(thr_id, work.midstate, work.data + 64,
				        work.hash1, work.hash, work.target,
					max_nonce, &hashes_done,
					work.blk.nonce);
			break;

#ifdef WANT_CRYPTOPP_ASM32
		case ALGO_CRYPTOPP_ASM32:
			rc = scanhash_asm32(thr_id, work.midstate, work.data + 64,
				        work.hash1, work.hash, work.target,
					max_nonce, &hashes_done,
					work.blk.nonce);
			break;
#endif

		default:
			/* should never happen */
			goto out;
		}

		/* record scanhash elapsed time */
		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);

		hashes_done -= work.blk.nonce;
		hashmeter(thr_id, &diff, hashes_done);
		work.blk.nonce += hashes_done;

		/* adjust max_nonce to meet target cycle time */
		if (diff.tv_usec > 500000)
			diff.tv_sec++;
		if (diff.tv_sec != cycle) {
			max64 = work.blk.nonce +
				((uint64_t)hashes_done * cycle) / diff.tv_sec;
		} else
			max64 = work.blk.nonce + hashes_done;
		if (max64 > 0xfffffffaULL)
			max64 = 0xfffffffaULL;
		max_nonce = max64;

		/* if nonce found, submit work */
		if (unlikely(rc)) {
			if (opt_debug)
				applog(LOG_DEBUG, "CPU %d found something?", cpu_from_thr_id(thr_id));
			if (unlikely(!submit_work_sync(mythr, &work)))
				break;
			work.blk.nonce += 4;
		}

		timeval_subtract(&diff, &tv_end, &tv_workstart);
		if (!requested && (diff.tv_sec > request_interval || work.blk.nonce > request_nonce)) {
			if (unlikely(!queue_request()))
				goto out;
			requested = true;
		}

		if (diff.tv_sec > opt_scantime || work_restart[thr_id].restart ||
			work.blk.nonce >= MAXTHREADS - hashes_done)
				needs_work = true;
	}

out:
	tq_freeze(mythr->q);

	return NULL;
}

enum {
	STAT_SLEEP_INTERVAL		= 1,
	STAT_CTR_INTERVAL		= 10000000,
	FAILURE_INTERVAL		= 30,
};

static _clState *clStates[16];

static inline cl_int queue_kernel_parameters(_clState *clState, dev_blk_ctx *blk)
{
	cl_kernel *kernel = &clState->kernel;
	cl_int status = 0;
	int num = 0;

	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_a);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_b);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_c);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_d);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_e);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_f);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_g);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->ctx_h);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_b);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_c);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_d);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_f);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_g);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_h);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->nonce);

	if (clState->hasBitAlign == true) {
		/* Parameters for phatk kernel */
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W2);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W16);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W17);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->PreVal4);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->T1);
	} else {
		/* Parameters for poclbm kernel */
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW0);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW1);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW2);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW3);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW15);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW01r);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fcty_e);
		status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fcty_e2);
	}
	status |= clSetKernelArg(*kernel, num++, sizeof(clState->outputBuffer),
				 (void *)&clState->outputBuffer);

	return status;
}

static inline int gpu_from_thr_id(int thr_id)
{
	return thr_id % nDevs;
}

static void *gpuminer_thread(void *userdata)
{
	const unsigned long cycle = opt_log_interval / 5 ? : 1;
	struct timeval tv_start, tv_end, diff;
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	uint32_t *res, *blank_res;

	size_t globalThreads[1];
	size_t localThreads[1];

	cl_int status;

	_clState *clState = clStates[thr_id];
	const cl_kernel *kernel = &clState->kernel;

	struct work *work = malloc(sizeof(struct work));
	unsigned const int threads = 1 << (15 + scan_intensity);
	unsigned const int vectors = clState->preferred_vwidth;
	unsigned const int hashes = threads * vectors;
	unsigned int hashes_done = 0;

	/* Request the next work item at 2/3 of the scantime */
	unsigned const int request_interval = opt_scantime * 2 / 3 ? : 1;
	unsigned const long request_nonce = MAXTHREADS / 3 * 2;
	bool requested = false;

	res = calloc(BUFFERSIZE, 1);
	blank_res = calloc(BUFFERSIZE, 1);

	if (!res || !blank_res) {
		applog(LOG_ERR, "Failed to calloc in gpuminer_thread");
		goto out;
	}

	gettimeofday(&tv_start, NULL);
	globalThreads[0] = threads;
	localThreads[0] = clState->work_size;
	diff.tv_sec = ~0UL;
	gettimeofday(&tv_end, NULL);

	while (1) {
		struct timeval tv_workstart;

		/* This finish flushes the readbuffer set with CL_FALSE later */
		clFinish(clState->commandQueue);
		if (diff.tv_sec > opt_scantime  || work->blk.nonce >= MAXTHREADS - hashes || work_restart[thr_id].restart) {
			/* Ignore any reads since we're getting new work and queue a clean buffer */
			status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
					BUFFERSIZE, blank_res, 0, NULL, NULL);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }
			memset(res, 0, BUFFERSIZE);

			gettimeofday(&tv_workstart, NULL);
			if (work_restart[thr_id].restart) {
				if (requested) {
					/* We have one extra request than desired now */
					if (unlikely(!discard_request())) {
						applog(LOG_ERR, "Failed to discard request in gpuminer thread");
						goto out;
					}
				} else
					requested = true;
			}
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(work, requested))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"gpu mining thread %d", mythr->id);
				goto out;
			}
			work->thr_id = thr_id;
			requested = false;

			precalc_hash(&work->blk, (uint32_t *)(work->midstate), (uint32_t *)(work->data + 64));
			work->blk.nonce = 0;
			work_restart[thr_id].restart = 0;

			if (opt_debug)
				applog(LOG_DEBUG, "getwork thread %d", thr_id);
			/* Flushes the writebuffer set with CL_FALSE above */
			clFinish(clState->commandQueue);
		}
		status = queue_kernel_parameters(clState, &work->blk);
		if (unlikely(status != CL_SUCCESS))
			{ applog(LOG_ERR, "Error: clSetKernelArg of all params failed."); goto out; }

		/* MAXBUFFERS entry is used as a flag to say nonces exist */
		if (res[MAXBUFFERS]) {
			/* Clear the buffer again */
			status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
					BUFFERSIZE, blank_res, 0, NULL, NULL);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }
			if (opt_debug)
				applog(LOG_DEBUG, "GPU %d found something?", gpu_from_thr_id(thr_id));
			postcalc_hash_async(mythr, work, res);
			memset(res, 0, BUFFERSIZE);
			clFinish(clState->commandQueue);
		}

		status = clEnqueueNDRangeKernel(clState->commandQueue, *kernel, 1, NULL,
				globalThreads, localThreads, 0,  NULL, NULL);
		if (unlikely(status != CL_SUCCESS))
			{ applog(LOG_ERR, "Error: Enqueueing kernel onto command queue. (clEnqueueNDRangeKernel)"); goto out; }

		status = clEnqueueReadBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
				BUFFERSIZE, res, 0, NULL, NULL);
		if (unlikely(status != CL_SUCCESS))
			{ applog(LOG_ERR, "Error: clEnqueueReadBuffer failed. (clEnqueueReadBuffer)"); goto out;}

		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);
		hashes_done += hashes;
		work->blk.nonce += hashes;
		if (diff.tv_usec > 500000)
			diff.tv_sec++;
		if (diff.tv_sec >= cycle) {
			hashmeter(thr_id, &diff, hashes_done);
			gettimeofday(&tv_start, NULL);
			hashes_done = 0;
		}

		timeval_subtract(&diff, &tv_end, &tv_workstart);
		if (!requested && (diff.tv_sec > request_interval || work->blk.nonce > request_nonce)) {
			if (unlikely(!queue_request()))
				goto out;
			requested = true;
		}
	}
out:
	tq_freeze(mythr->q);

	return NULL;
}

static void restart_threads(void)
{
	int i;

	/* Discard old queued requests and get new ones */
	flush_requests();

	/* Queue extra requests for each worker thread since they'll all need
	 * new work. Each worker will set their "requested" flag to true
	 * should they receive a .restart */
	for (i = 0; i < opt_n_threads + gpu_threads; i++) {
		if (unlikely(!queue_request())) {
			applog(LOG_ERR, "Failed to queue requests in flush_requests");
			kill_work();
			return;
		}
		work_restart[i].restart = 1;
	}
}

static void *longpoll_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	CURL *curl = NULL;
	char *copy_start, *hdr_path, *lp_url = NULL;
	bool need_slash = false;
	int failures = 0;

	hdr_path = tq_pop(mythr->q, NULL);
	if (!hdr_path)
		goto out;

	/* full URL */
	if (strstr(hdr_path, "://")) {
		lp_url = hdr_path;
		hdr_path = NULL;
	}
	
	/* absolute path, on current server */
	else {
		copy_start = (*hdr_path == '/') ? (hdr_path + 1) : hdr_path;
		if (rpc_url[strlen(rpc_url) - 1] != '/')
			need_slash = true;

		lp_url = malloc(strlen(rpc_url) + strlen(copy_start) + 2);
		if (!lp_url)
			goto out;

		sprintf(lp_url, "%s%s%s", rpc_url, need_slash ? "/" : "", copy_start);
	}

	applog(LOG_INFO, "Long-polling activated for %s", lp_url);

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		goto out;
	}

	while (1) {
		json_t *val;

		val = json_rpc_call(curl, lp_url, rpc_userpass, rpc_req,
				    false, true);
		if (likely(val)) {
			failures = 0;
			json_decref(val);

			applog(LOG_INFO, "LONGPOLL detected new block");
			restart_threads();
		} else {
			if (failures++ < 10) {
				sleep(30);
				applog(LOG_ERR,
					"longpoll failed, sleeping for 30s");
			} else {
				applog(LOG_ERR,
					"longpoll failed, ending thread");
				goto out;
			}
		}
	}

out:
	free(hdr_path);
	free(lp_url);
	tq_freeze(mythr->q);
	if (curl)
		curl_easy_cleanup(curl);

	return NULL;
}

static void show_usage(void)
{
	int i;

	printf("cgminer version %s\n\n", VERSION);
	printf("Usage:\tcgminer [options]\n\nSupported options:\n");
	for (i = 0; i < ARRAY_SIZE(options_help); i++) {
		struct option_help *h;

		h = &options_help[i];
		printf("--%s\n%s\n\n", h->name, h->helptext);
	}

	exit(1);
}

static void parse_arg (int key, char *arg)
{
	int v, i;

	switch(key) {
	case 'a':
		for (i = 0; i < ARRAY_SIZE(algo_names); i++) {
			if (algo_names[i] &&
			    !strcmp(arg, algo_names[i])) {
				opt_algo = i;
				break;
			}
		}
		if (i == ARRAY_SIZE(algo_names))
			show_usage();
		break;
	case 'c': {
		json_error_t err;
		if (opt_config)
			json_decref(opt_config);
		opt_config = json_load_file(arg, &err);
		if (!json_is_object(opt_config)) {
			applog(LOG_ERR, "JSON decode of %s failed", arg);
			show_usage();
		}
		break;
	}
	case 'g':
		v = atoi(arg);
		if (v < 0 || v > 10)
			show_usage();

		opt_g_threads = v;
		break;
	case 'D':
		opt_debug = true;
		break;
	case 'I':
		v = atoi(arg);
		if (v < 0 || v > 14) /* sanity check */
			show_usage();
		scan_intensity = v;
		break;
	case 'l':
		v = atoi(arg);
		if (v < 0 || v > 9999)	/* sanity check */
			show_usage();
		opt_log_interval = v;
		break;
	case 'p':
		free(rpc_pass);
		rpc_pass = strdup(arg);
		break;
	case 'P':
		opt_protocol = true;
		break;
	case 'Q':
		v = atoi(arg);
		if (v < 0 || v > 10)
			show_usage();

		opt_queue = v;
		break;
	case 'q':
		opt_quiet = true;
		break;
	case 'r':
		v = atoi(arg);
		if (v < -1 || v > 9999)	/* sanity check */
			show_usage();

		opt_retries = v;
		break;
	case 'R':
		v = atoi(arg);
		if (v < 1 || v > 9999)	/* sanity check */
			show_usage();

		opt_fail_pause = v;
		break;
	case 's':
		v = atoi(arg);
		if (v < 1 || v > 9999)	/* sanity check */
			show_usage();

		opt_scantime = v;
		break;
	case 't':
		v = atoi(arg);
		if (v < 0 || v > 9999)	/* sanity check */
			show_usage();

		opt_n_threads = v;
		break;
	case 'u':
		free(rpc_user);
		rpc_user = strdup(arg);
		break;
	case 'v':
		v = atoi(arg);
		if (v != 1 && v != 2 && v != 4)
			show_usage();

		opt_vectors = v;
		break;
	case 'w':
		v = atoi(arg);
		if (v < 1 || v > 9999)	/* sanity check */
			show_usage();

		opt_worksize = v;
		break;
	case 1001:			/* --url */
		if (strncmp(arg, "http://", 7) &&
		    strncmp(arg, "https://", 8))
			show_usage();

		free(rpc_url);
		rpc_url = strdup(arg);
		break;
	case 1002:			/* --userpass */
		if (!strchr(arg, ':'))
			show_usage();

		free(rpc_userpass);
		rpc_userpass = strdup(arg);
		break;
	case 1003:
		want_longpoll = false;
		break;
	case 1004:
		use_syslog = true;
		break;
	default:
		show_usage();
	}
}

static void parse_config(void)
{
	int i;
	json_t *val;

	if (!json_is_object(opt_config))
		return;

	for (i = 0; i < ARRAY_SIZE(options); i++) {
		if (!options[i].name)
			break;
		if (!strcmp(options[i].name, "config"))
			continue;

		val = json_object_get(opt_config, options[i].name);
		if (!val)
			continue;

		if (options[i].has_arg && json_is_string(val)) {
			char *s = strdup(json_string_value(val));
			if (!s)
				break;
			parse_arg(options[i].val, s);
			free(s);
		} else if (!options[i].has_arg && json_is_true(val))
			parse_arg(options[i].val, "");
		else
			applog(LOG_ERR, "JSON option %s invalid",
				options[i].name);
	}
}

static void parse_cmdline(int argc, char *argv[])
{
	int key;

	while (1) {
		key = getopt_long(argc, argv, "a:c:qDPr:s:t:h?", options, NULL);
		if (key < 0)
			break;

		parse_arg(key, optarg);
	}

	parse_config();
}

int main (int argc, char *argv[])
{
	struct thr_info *thr;
	unsigned int i;
	char name[32];
	struct cgpu_info *gpus = NULL, *cpus = NULL;

#ifdef WIN32
	opt_n_threads = num_processors = 1;
#else
	num_processors = sysconf(_SC_NPROCESSORS_ONLN);
	opt_n_threads = num_processors;
#endif /* !WIN32 */

	nDevs = clDevicesNum();
	if (opt_ndevs) {
		applog(LOG_INFO, "%i", nDevs);
		return nDevs;
	}
	/* Invert the value to determine if we manually set it in cmdline
	 * or disable gpu threads */
	if (nDevs)
		opt_n_threads = - opt_n_threads;

	rpc_url = strdup(DEF_RPC_URL);

	/* parse command line */
	parse_cmdline(argc, argv);

	gpu_threads = nDevs * opt_g_threads;
	if (opt_n_threads < 0) {
		if (gpu_threads)
			opt_n_threads = 0;
		else
			opt_n_threads = -opt_n_threads;
	}

	if (!rpc_userpass) {
		if (!rpc_user || !rpc_pass) {
			applog(LOG_ERR, "No login credentials supplied");
			return 1;
		}
		rpc_userpass = malloc(strlen(rpc_user) + strlen(rpc_pass) + 2);
		if (!rpc_userpass)
			return 1;
		sprintf(rpc_userpass, "%s:%s", rpc_user, rpc_pass);
	}

	if (unlikely(pthread_mutex_init(&time_lock, NULL)))
		return 1;
	if (unlikely(pthread_mutex_init(&hash_lock, NULL)))
		return 1;
	if (unlikely(pthread_mutex_init(&qd_lock, NULL)))
		return 1;

	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		return 1;
#ifdef HAVE_SYSLOG_H
	if (use_syslog)
		openlog("cpuminer", LOG_PID, LOG_USER);
#endif

	work_restart = calloc(opt_n_threads + gpu_threads, sizeof(*work_restart));
	if (!work_restart)
		return 1;

	thr_info = calloc(opt_n_threads + 2 + gpu_threads, sizeof(*thr));
	if (!thr_info)
		return 1;

	/* init workio thread info */
	work_thr_id = opt_n_threads + gpu_threads;
	thr = &thr_info[work_thr_id];
	thr->id = work_thr_id;
	thr->q = tq_new();
	if (!thr->q)
		return 1;

	/* start work I/O thread */
	if (pthread_create(&thr->pth, NULL, workio_thread, thr)) {
		applog(LOG_ERR, "workio thread create failed");
		return 1;
	}

	/* init longpoll thread info */
	if (want_longpoll) {
		longpoll_thr_id = opt_n_threads + gpu_threads + 1;
		thr = &thr_info[longpoll_thr_id];
		thr->id = longpoll_thr_id;
		thr->q = tq_new();
		if (!thr->q)
			return 1;

		/* start longpoll thread */
		if (unlikely(pthread_create(&thr->pth, NULL, longpoll_thread, thr))) {
			applog(LOG_ERR, "longpoll thread create failed");
			return 1;
		}
		pthread_detach(thr->pth);
	} else
		longpoll_thr_id = -1;

	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);

	if (opt_n_threads ) {
		cpus = calloc(num_processors, sizeof(struct cgpu_info));
		if (unlikely(!cpus)) {
			applog(LOG_ERR, "Failed to calloc cpus");
			return 1;
		}
	}
	if (gpu_threads) {
		gpus = calloc(nDevs, sizeof(struct cgpu_info));
		if (unlikely(!gpus)) {
			applog(LOG_ERR, "Failed to calloc gpus");
			return 1;
		}
	}

	/* Put the extra work in the queue */
	for (i = 0; i < opt_queue; i++) {
		if (unlikely(!queue_request())) {
			applog(LOG_ERR, "Failed to queue_request in main");
			return 1;
		}
	}

	/* start GPU mining threads */
	for (i = 0; i < gpu_threads; i++) {
		int gpu = gpu_from_thr_id(i);

		thr = &thr_info[i];
		thr->id = i;
		gpus[gpu].is_gpu = 1;
		gpus[gpu].cpu_gpu = gpu;
		thr->cgpu = &gpus[gpu];

		thr->q = tq_new();
		if (!thr->q) {
			applog(LOG_ERR, "tq_new failed in starting gpu mining threads");
			return 1;
		}

		applog(LOG_INFO, "Init GPU thread %i", i);
		clStates[i] = initCl(gpu, name, sizeof(name));
		if (!clStates[i]) {
			applog(LOG_ERR, "Failed to init GPU thread %d", i);
			continue;
		}
		applog(LOG_INFO, "initCl() finished. Found %s", name);

		if (unlikely(pthread_create(&thr->pth, NULL, gpuminer_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", i);
			return 1;
		}
		pthread_detach(thr->pth);
	}

	applog(LOG_INFO, "%d gpu miner threads started", i);

	/* start CPU mining threads */
	for (i = gpu_threads; i < gpu_threads + opt_n_threads; i++) {
		int cpu = cpu_from_thr_id(i);

		thr = &thr_info[i];

		thr->id = i;
		cpus[cpu].cpu_gpu = cpu;
		thr->cgpu = &cpus[cpu];

		thr->q = tq_new();
		if (!thr->q) {
			applog(LOG_ERR, "tq_new failed in starting cpu mining threads");
			return 1;
		}

		if (unlikely(pthread_create(&thr->pth, NULL, miner_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", i);
			return 1;
		}
		pthread_detach(thr->pth);
	}

	applog(LOG_INFO, "%d cpu miner threads started, "
		"using SHA256 '%s' algorithm.",
		opt_n_threads,
		algo_names[opt_algo]);

	/* Restart count as it will be wrong till all threads are started */
	pthread_mutex_lock(&hash_lock);
	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);
	total_mhashes_done = 0;
	pthread_mutex_unlock(&hash_lock);

	/* main loop - simply wait for workio thread to exit */
	pthread_join(thr_info[work_thr_id].pth, NULL);
	curl_global_cleanup();
	if (gpu_threads)
		free(gpus);
	if (opt_n_threads)
		free(cpus);

	applog(LOG_INFO, "workio thread dead, exiting.");

	return 0;
}

