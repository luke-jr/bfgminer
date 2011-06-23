
/*
 * Copyright 2011 Con Kolivas
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "cpuminer-config.h"
#define _GNU_SOURCE

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

#define PROGRAM_NAME		"minerd"
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
	applog(LOG_INFO, "Binding thread %d to cpu %d", id, cpu);
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
static int opt_retries = 10;
static int opt_fail_pause = 30;
int opt_scantime = 5;
static json_t *opt_config;
static const bool opt_time = true;
#ifdef WANT_X8664_SSE2
static enum sha256_algos opt_algo = ALGO_SSE2_64;
#else
static enum sha256_algos opt_algo = ALGO_C;
#endif
static int nDevs;
static int opt_n_threads = 1;
static int num_processors;
static int scan_intensity = 5;
static char *rpc_url;
static char *rpc_userpass;
static char *rpc_user, *rpc_pass;
struct thr_info *thr_info;
static int work_thr_id;
int longpoll_thr_id;
struct work_restart *work_restart = NULL;
pthread_mutex_t time_lock;
static pthread_mutex_t hash_lock;
static unsigned long total_hashes_done;
static struct timeval total_tv_start, total_tv_end;
static int accepted, rejected;


struct option_help {
	const char	*name;
	const char	*helptext;
};

static struct option_help options_help[] = {
	{ "help",
	  "(-h) Display this help text" },

	{ "config FILE",
	  "(-c FILE) JSON-format configuration file (default: none)\n"
	  "See example-cfg.json for an example configuration." },

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

	{ "quiet",
	  "(-q) Disable per-thread hashmeter output (default: off)" },

	{ "debug",
	  "(-D) Enable debug output (default: off)" },

	{ "intensity",
	  "(-I) Intensity of scanning (0 - 16, default 5)" },

	{ "ndevs",
	  "(-n) Display number of detected GPUs" },

	{ "no-longpoll",
	  "Disable X-Long-Polling support (default: enabled)" },

	{ "protocol-dump",
	  "(-P) Verbose dump of protocol-level activities (default: off)" },

	{ "retries N",
	  "(-r N) Number of times to retry, if JSON-RPC call fails\n"
	  "\t(default: 10; use -1 for \"never\")" },

	{ "retry-pause N",
	  "(-R N) Number of seconds to pause, between retries\n"
	  "\t(default: 30)" },

	{ "scantime N",
	  "(-s N) Upper bound on time spent scanning current work,\n"
	  "\tin seconds. (default: 5)" },

#ifdef HAVE_SYSLOG_H
	{ "syslog",
	  "Use system log for output messages (default: standard error)" },
#endif

	{ "threads N",
	  "(-t N) Number of miner CPU threads (default: number of processors)" },

	{ "url URL",
	  "URL for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_URL ")" },

	{ "userpass USERNAME:PASSWORD",
	  "Username:Password pair for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERPASS ")" },

	{ "user USERNAME",
	  "(-u USERNAME) Username for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_USERNAME ")" },

	{ "pass PASSWORD",
	  "(-p PASSWORD) Password for bitcoin JSON-RPC server "
	  "(default: " DEF_RPC_PASSWORD ")" },
};

static struct option options[] = {
	{ "algo", 1, NULL, 'a' },
	{ "config", 1, NULL, 'c' },
	{ "debug", 0, NULL, 'D' },
	{ "help", 0, NULL, 'h' },
	{ "intensity", 1, NULL, 'I' },
	{ "ndevs", 0, NULL, 'n' },
	{ "no-longpoll", 0, NULL, 1003 },
	{ "pass", 1, NULL, 'p' },
	{ "protocol-dump", 0, NULL, 'P' },
	{ "quiet", 0, NULL, 'q' },
	{ "threads", 1, NULL, 't' },
	{ "retries", 1, NULL, 'r' },
	{ "retry-pause", 1, NULL, 'R' },
	{ "scantime", 1, NULL, 's' },
#ifdef HAVE_SYSLOG_H
	{ "syslog", 0, NULL, 1004 },
#endif
	{ "url", 1, NULL, 1001 },
	{ "user", 1, NULL, 'u' },
	{ "userpass", 1, NULL, 1002 },
};

struct work {
	unsigned char	data[128];
	unsigned char	hash1[64];
	unsigned char	midstate[32];
	unsigned char	target[32];

	unsigned char	hash[32];

	uint32_t		output[1];
	uint32_t		res_nonce;
	uint32_t		valid;
	dev_blk_ctx		blk;
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

static bool submit_upstream_work(CURL *curl, const struct work *work)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345];
	bool rc = false;

	/* build hex string */
	hexstr = bin2hex(work->data, sizeof(work->data));
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "submit_upstream_work OOM");
		goto out;
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

	if (json_is_true(res)) {
		accepted++;
		applog(LOG_INFO, "PROOF OF WORK RESULT: true (yay!!!)");
	} else {
		rejected++;
		applog(LOG_INFO, "PROOF OF WORK RESULT: false (booooo)");
	}

	json_decref(val);

	rc = true;

out:
	free(hexstr);
	return rc;
}

static const char *rpc_req =
	"{\"method\": \"getwork\", \"params\": [], \"id\":0}\r\n";

static bool get_upstream_work(CURL *curl, struct work *work)
{
	json_t *val;
	bool rc;

	val = json_rpc_call(curl, rpc_url, rpc_userpass, rpc_req,
			    want_longpoll, false);
	if (!val)
		return false;

	rc = work_decode(json_object_get(val, "result"), work);

	json_decref(val);

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

static bool workio_get_work(struct workio_cmd *wc, CURL *curl)
{
	struct work *ret_work;
	int failures = 0;

	ret_work = calloc(1, sizeof(*ret_work));
	if (!ret_work)
		return false;

	/* obtain new work from bitcoin via JSON-RPC */
	while (!get_upstream_work(curl, ret_work)) {
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			applog(LOG_ERR, "json_rpc_call failed, terminating workio thread");
			free(ret_work);
			return false;
		}

		/* pause, then restart work-request loop */
		applog(LOG_ERR, "json_rpc_call failed, retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

	/* send work to requesting thread */
	if (!tq_push(wc->thr->q, ret_work))
		free(ret_work);

	return true;
}

static bool workio_submit_work(struct workio_cmd *wc, CURL *curl)
{
	int failures = 0;

	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(curl, wc->u.work)) {
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			applog(LOG_ERR, "...terminating workio thread");
			return false;
		}

		/* pause, then restart work-request loop */
		applog(LOG_ERR, "...retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

	return true;
}

static void *workio_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	CURL *curl;
	bool ok = true;

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialization failed");
		return NULL;
	}

	while (ok) {
		struct workio_cmd *wc;

		/* wait for workio_cmd sent to us, on our queue */
		wc = tq_pop(mythr->q, NULL);
		if (!wc) {
			ok = false;
			break;
		}

		/* process workio_cmd */
		switch (wc->cmd) {
		case WC_GET_WORK:
			ok = workio_get_work(wc, curl);
			break;
		case WC_SUBMIT_WORK:
			ok = workio_submit_work(wc, curl);
			break;

		default:		/* should never happen */
			ok = false;
			break;
		}

		workio_cmd_free(wc);
	}

	tq_freeze(mythr->q);
	curl_easy_cleanup(curl);

	return NULL;
}

static void hashmeter(int thr_id, struct timeval *diff,
		      unsigned long hashes_done)
{
	struct timeval temp_tv_end, total_diff;
	double khashes, secs;
	double total_mhashes, total_secs;

	/* Don't bother calculating anything if we're not displaying it */
	if (opt_quiet)
		return;
	khashes = hashes_done / 1000.0;
	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);
	if (opt_debug)
		applog(LOG_DEBUG, "[thread %d: %lu hashes, %.0f khash/sec]",
			thr_id, hashes_done, hashes_done / secs);
	gettimeofday(&temp_tv_end, NULL);
	timeval_subtract(&total_diff, &temp_tv_end, &total_tv_end);


	if (opt_n_threads + nDevs > 1) {
		/* Totals are updated by all threads so can race without locking */
		pthread_mutex_lock(&hash_lock);
		total_hashes_done += hashes_done;
		if (total_diff.tv_sec < 5) {
			/* Only update the total every 5 seconds */
			pthread_mutex_unlock(&hash_lock);
			return;
		}
		gettimeofday(&total_tv_end, NULL);
		pthread_mutex_unlock(&hash_lock);
		timeval_subtract(&total_diff, &total_tv_end, &total_tv_start);
		total_mhashes = total_hashes_done / 1000000.0;
		total_secs = (double)total_diff.tv_sec +
			((double)total_diff.tv_usec / 1000000.0);
		applog(LOG_INFO, "[%.2f Mhash/sec] [%d Accepted] [%d Rejected]",
		       total_mhashes / total_secs, accepted, rejected);
	} else {
		total_hashes_done += hashes_done;
		if (total_diff.tv_sec < 5) {
			/* Only update the total every 5 seconds */
			pthread_mutex_unlock(&hash_lock);
			return;
		}
		gettimeofday(&total_tv_end, NULL);
		timeval_subtract(&total_diff, &total_tv_end, &total_tv_start);
		total_mhashes = total_hashes_done / 1000000.0;
		total_secs = (double)total_diff.tv_sec +
			((double)total_diff.tv_usec / 1000000.0);
		applog(LOG_INFO, "[%.2f Mhash/sec] [%d Accepted] [%d Rejected]",
		       total_mhashes / total_secs, accepted, rejected);
	}
}

static bool get_work(struct thr_info *thr, struct work *work)
{
	struct workio_cmd *wc;
	struct work *work_heap;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (!wc)
		return false;

	wc->cmd = WC_GET_WORK;
	wc->thr = thr;

	/* send work request to workio thread */
	if (!tq_push(thr_info[work_thr_id].q, wc)) {
		workio_cmd_free(wc);
		return false;
	}

	/* wait for response, a unit of work */
	work_heap = tq_pop(thr->q, NULL);
	if (!work_heap)
		return false;

	/* copy returned work into storage provided by caller */
	memcpy(work, work_heap, sizeof(*work));
	free(work_heap);

	return true;
}

static bool submit_work(struct thr_info *thr, const struct work *work_in)
{
	struct workio_cmd *wc;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (!wc)
		return false;

	wc->u.work = malloc(sizeof(*work_in));
	if (!wc->u.work)
		goto err_out;

	wc->cmd = WC_SUBMIT_WORK;
	wc->thr = thr;
	memcpy(wc->u.work, work_in, sizeof(*work_in));

	/* send solution to workio thread */
	if (!tq_push(thr_info[work_thr_id].q, wc))
		goto err_out;

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
	return submit_work(thr, work);
}

static inline int cpu_from_thr_id(int thr_id)
{
	return (thr_id - nDevs) % num_processors;
}

static void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	int thr_id = mythr->id;
	uint32_t max_nonce = 0xffffff;

	/* Set worker threads to nice 19 and then preferentially to SCHED_IDLE
	 * and if that fails, then SCHED_BATCH. No need for this to be an
	 * error if it fails */
	setpriority(PRIO_PROCESS, 0, 19);
	drop_policy();

	/* Cpu affinity only makes sense if the number of threads is a multiple
	 * of the number of CPUs */
	if (!(opt_n_threads % num_processors))
		affine_to_cpu(mythr->id, mythr->id % num_processors);

	while (1) {
		struct work work __attribute__((aligned(128)));
		unsigned long hashes_done;
		struct timeval tv_start, tv_end, diff;
		uint64_t max64;
		bool rc;

		/* obtain new work from internal workio thread */
		if (unlikely(!get_work(mythr, &work))) {
			applog(LOG_ERR, "work retrieval failed, exiting "
				"mining thread %d", mythr->id);
			goto out;
		}

		hashes_done = 0;
		gettimeofday(&tv_start, NULL);

		/* scan nonces for a proof-of-work hash */
		switch (opt_algo) {
		case ALGO_C:
			rc = scanhash_c(thr_id, work.midstate, work.data + 64,
				        work.hash1, work.hash, work.target,
					max_nonce, &hashes_done);
			break;

#ifdef WANT_X8664_SSE2
		case ALGO_SSE2_64: {
			unsigned int rc5 =
			        scanhash_sse2_64(thr_id, work.midstate, work.data + 64,
						 work.hash1, work.hash,
						 work.target,
					         max_nonce, &hashes_done);
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
						  max_nonce, &hashes_done);
			rc = (rc4 == -1) ? false : true;
			}
			break;
#endif

#ifdef WANT_VIA_PADLOCK
		case ALGO_VIA:
			rc = scanhash_via(thr_id, work.data, work.target,
					  max_nonce, &hashes_done);
			break;
#endif
		case ALGO_CRYPTOPP:
			rc = scanhash_cryptopp(thr_id, work.midstate, work.data + 64,
				        work.hash1, work.hash, work.target,
					max_nonce, &hashes_done);
			break;

#ifdef WANT_CRYPTOPP_ASM32
		case ALGO_CRYPTOPP_ASM32:
			rc = scanhash_asm32(thr_id, work.midstate, work.data + 64,
				        work.hash1, work.hash, work.target,
					max_nonce, &hashes_done);
			break;
#endif

		default:
			/* should never happen */
			goto out;
		}

		/* record scanhash elapsed time */
		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);

		hashmeter(thr_id, &diff, hashes_done);

		/* adjust max_nonce to meet target scan time */
		if (diff.tv_usec > 500000)
			diff.tv_sec++;
		if (diff.tv_sec > 0) {
			max64 =
			   ((uint64_t)hashes_done * opt_scantime) / diff.tv_sec;
			if (max64 > 0xfffffffaULL)
				max64 = 0xfffffffaULL;
			max_nonce = max64;
		}

		/* if nonce found, submit work */
		if (unlikely(rc)) {
			applog(LOG_INFO, "CPU %d found something?", cpu_from_thr_id(thr_id));
			if (!submit_work(mythr, &work))
				break;
		}
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

static inline cl_int queue_kernel_parameters(dev_blk_ctx *blk, cl_kernel *kernel,
	struct _cl_mem *output)
{
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
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW0);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW1);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW2);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW3);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW15);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW01r);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fcty_e);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fcty_e2);
	status |= clSetKernelArg(*kernel, num++, sizeof(output), (void *)&output);

	return status;
}

static inline int gpu_from_thr_id(int thr_id)
{
	return thr_id;
}

static void *gpuminer_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	struct timeval tv_start;
	int thr_id = mythr->id;
	uint32_t res[128], blank_res[128];
	cl_kernel *kernel;

	memset(blank_res, 0, BUFFERSIZE);

	size_t globalThreads[1];
	size_t localThreads[1];

	cl_int status;

	_clState *clState = clStates[thr_id];
	kernel = &clState->kernel;

	status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0,
			BUFFERSIZE, blank_res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS))
		{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }

	struct work *work = malloc(sizeof(struct work));
	bool need_work = true;
	unsigned int threads = 1 << (15 + scan_intensity);
	unsigned int vectors = 4;
	unsigned int hashes_done = threads * vectors;

	gettimeofday(&tv_start, NULL);
	globalThreads[0] = threads;
	localThreads[0] = 64;

	while (1) {
		struct timeval tv_end, diff;
		unsigned int i;

		clFinish(clState->commandQueue);

		if (need_work) {
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(mythr, work))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"gpu mining thread %d", mythr->id);
				goto out;
			}

			precalc_hash(&work->blk, (uint32_t *)(work->midstate), (uint32_t *)(work->data + 64));
			work->blk.nonce = 0;
			status = queue_kernel_parameters(&work->blk, kernel, clState->outputBuffer);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clSetKernelArg of all params failed."); exit (1); }

			work_restart[thr_id].restart = 0;
			need_work = false;

			if (opt_debug)
				applog(LOG_DEBUG, "getwork");

		} else {
			status = clSetKernelArg(*kernel, 14, sizeof(uint), (void *)&work->blk.nonce);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clSetKernelArg of nonce failed."); goto out; }
		}

		status = clEnqueueNDRangeKernel(clState->commandQueue, *kernel, 1, NULL,
				globalThreads, localThreads, 0,  NULL, NULL);
		if (unlikely(status != CL_SUCCESS))
			{ applog(LOG_ERR, "Error: Enqueueing kernel onto command queue. (clEnqueueNDRangeKernel)"); goto out; }

		/* 127 is used as a flag to say nonces exist */
		if (unlikely(res[127])) {
			/* Clear the buffer again */
			status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
					BUFFERSIZE, blank_res, 0, NULL, NULL);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }
			for (i = 0; i < 127; i++) {
				if (res[i]) {
					applog(LOG_INFO, "GPU %d found something?", gpu_from_thr_id(thr_id));
					postcalc_hash(mythr, &work->blk, work, res[i]);
				} else
					break;
			}
			clFinish(clState->commandQueue);
		}

		status = clEnqueueReadBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
				BUFFERSIZE, res, 0, NULL, NULL);
		if (unlikely(status != CL_SUCCESS))
			{ applog(LOG_ERR, "Error: clEnqueueReadBuffer failed. (clEnqueueReadBuffer)"); goto out;}

		gettimeofday(&tv_end, NULL);
		timeval_subtract(&diff, &tv_end, &tv_start);
		hashmeter(thr_id, &diff, hashes_done);
		gettimeofday(&tv_start, NULL);

		work->blk.nonce += hashes_done;

		if (unlikely(work->blk.nonce > MAXTHREADS - hashes_done) ||
			(work_restart[thr_id].restart))
				need_work = true;
	}
out:
	tq_freeze(mythr->q);

	return NULL;
}

static void restart_threads(void)
{
	int i;

	for (i = 0; i < opt_n_threads + nDevs; i++)
		work_restart[i].restart = 1;
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
		applog(LOG_ERR, "CURL initialization failed");
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

	printf("minerd version %s\n\n", VERSION);
	printf("Usage:\tminerd [options]\n\nSupported options:\n");
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
	case 'q':
		opt_quiet = true;
		break;
	case 'I':
		v = atoi(arg);
		if (v < 0 || v > 16) /* sanity check */
			show_usage();
		scan_intensity = v;
		break;
	case 'D':
		opt_debug = true;
		break;
	case 'p':
		free(rpc_pass);
		rpc_pass = strdup(arg);
		break;
	case 'P':
		opt_protocol = true;
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
	int i;
	char name[32];

#ifdef WIN32
	opt_n_threads = 1;
#else
	num_processors = sysconf(_SC_NPROCESSORS_ONLN);
	opt_n_threads = num_processors;
#endif /* !WIN32 */

	nDevs = clDevicesNum();
	if (opt_ndevs) {
		printf("%i\n", nDevs);
		return nDevs;
	}

	rpc_url = strdup(DEF_RPC_URL);

	/* parse command line */
	parse_cmdline(argc, argv);

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

#ifdef HAVE_SYSLOG_H
	if (use_syslog)
		openlog("cpuminer", LOG_PID, LOG_USER);
#endif

	work_restart = calloc(opt_n_threads + nDevs, sizeof(*work_restart));
	if (!work_restart)
		return 1;

	thr_info = calloc(opt_n_threads + 2 + nDevs, sizeof(*thr));
	if (!thr_info)
		return 1;

	/* init workio thread info */
	work_thr_id = opt_n_threads + nDevs;
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
		longpoll_thr_id = opt_n_threads + nDevs + 1;
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
	} else
		longpoll_thr_id = -1;

	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);

	/* start GPU mining threads */
	for (i = 0; i < nDevs; i++) {
		thr = &thr_info[i];

		thr->id = i;
		thr->q = tq_new();
		if (!thr->q)
			return 1;

		printf("Init GPU %i\n", i);
		clStates[i] = initCl(i, name, sizeof(name));
		printf("initCl() finished. Found %s\n", name);

		if (unlikely(pthread_create(&thr->pth, NULL, gpuminer_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", i);
			return 1;
		}

		sleep(1);	/* don't pound RPC server all at once */
	}

	applog(LOG_INFO, "%d gpu miner threads started", i);

	/* start CPU mining threads */
	for (i = nDevs; i < nDevs + opt_n_threads; i++) {
		thr = &thr_info[i];

		thr->id = i;
		thr->q = tq_new();
		if (!thr->q)
			return 1;

		if (unlikely(pthread_create(&thr->pth, NULL, miner_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", i);
			return 1;
		}

		sleep(1);	/* don't pound RPC server all at once */
	}

	
	applog(LOG_INFO, "%d cpu miner threads started, "
		"using SHA256 '%s' algorithm.",
		opt_n_threads,
		algo_names[opt_algo]);

	/* Restart count as it will be wrong till all threads are started */
	pthread_mutex_lock(&hash_lock);
	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);
	total_hashes_done = 0;
	pthread_mutex_unlock(&hash_lock);

	/* main loop - simply wait for workio thread to exit */
	pthread_join(thr_info[work_thr_id].pth, NULL);

	applog(LOG_INFO, "workio thread dead, exiting.");

	return 0;
}

