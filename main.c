
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

#include <curses.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#ifndef WIN32
#include <sys/resource.h>
#endif
#include <ccan/opt/opt.h>
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

#ifdef SCHED_BATCH
#ifdef SCHED_IDLE
	if (unlikely(sched_setscheduler(0, SCHED_IDLE, &param) == -1))
#endif
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
bool want_longpoll = true;
bool have_longpoll = false;
bool use_syslog = false;
static bool opt_quiet = false;
static int opt_retries = -1;
static int opt_fail_pause = 5;
static int opt_log_interval = 5;
bool opt_log_output = false;
static bool opt_dynamic = true;
static int opt_queue = 1;
int opt_vectors;
int opt_worksize;
int opt_scantime = 60;
static const bool opt_time = true;
#ifdef WANT_X8664_SSE2
static enum sha256_algos opt_algo = ALGO_SSE2_64;
#else
static enum sha256_algos opt_algo = ALGO_C;
#endif
static int nDevs;
static int opt_g_threads = 2;
static int opt_device;
static int total_devices;
static bool gpu_devices[16];
static int gpu_threads;
static bool forced_n_threads;
static int opt_n_threads;
static int mining_threads;
static int num_processors;
static int scan_intensity;
static bool use_curses = true;

static char *rpc_url;
static char *rpc_userpass;
static char *rpc_user, *rpc_pass;

struct thr_info *thr_info;
static int work_thr_id;
int longpoll_thr_id;
static int stage_thr_id;
static int watchdog_thr_id;

struct work_restart *work_restart = NULL;

static pthread_mutex_t hash_lock;
static pthread_mutex_t qd_lock;
static pthread_mutex_t stgd_lock;
static pthread_mutex_t curses_lock;
static double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;

static int accepted, rejected;
int hw_errors;
static int total_queued, total_staged, lp_staged;
static bool localgen = false;
static unsigned int getwork_requested;
static unsigned int stale_shares;
static unsigned int discarded_work;
static unsigned int new_blocks;
static unsigned int local_work;
static unsigned int localgen_occasions;
static unsigned int remotefail_occasions;

static char current_block[37];
static char longpoll_block[37];
static char blank[37];
static char datestamp[40];

static void applog_and_exit(const char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	vapplog(LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
}

/* FIXME: Use asprintf for better errors. */
static char *set_algo(const char *arg, enum sha256_algos *algo)
{
	enum sha256_algos i;

	for (i = 0; i < ARRAY_SIZE(algo_names); i++) {
		if (algo_names[i] && !strcmp(arg, algo_names[i])) {
			*algo = i;
			return NULL;
		}
	}
	return "Unknown algorithm";
}

static void show_algo(char buf[OPT_SHOW_LEN], const enum sha256_algos *algo)
{
	strncpy(buf, algo_names[*algo], OPT_SHOW_LEN);
}

static char *set_int_range(const char *arg, int *i, int min, int max)
{
	char *err = opt_set_intval(arg, i);
	if (err)
		return err;

	if (*i < min || *i > max)
		return "Value out of range";

	return NULL;
}

static char *set_int_0_to_9999(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 9999);
}

static char *forced_int_0_to_14(const char *arg, int *i)
{
	opt_dynamic = false;
	return set_int_range(arg, i, 0, 14);
}

static char *force_nthreads_int(const char *arg, int *i)
{
	forced_n_threads = true;
	return set_int_range(arg, i, 0, 9999);
}

static char *set_int_0_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 10);
}

static char *set_int_1_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 10);
}

static char *set_devices(const char *arg, int *i)
{
	char *err = opt_set_intval(arg, i);

	if (err)
		return err;

	if (*i < 0 || *i > 15)
		return "Invalid GPU device number";
	total_devices++;
	gpu_devices[*i] = true;
	return NULL;
}

static char *set_url(const char *arg, char **p)
{
	opt_set_charp(arg, p);
	if (strncmp(arg, "http://", 7) &&
	    strncmp(arg, "https://", 8))
		return "URL must start with http:// or https://";

	return NULL;
}

static char *set_vector(const char *arg, int *i)
{
	char *err = opt_set_intval(arg, i);
	if (err)
		return err;

	if (*i != 1 && *i != 2 && *i != 4)
		return "Valid vectors are 1, 2 or 4";
	return NULL;
}

static char *enable_debug(bool *flag)
{
	*flag = true;
	/* Turn out verbose output, too. */
	opt_log_output = true;
	return NULL;
}


/* These options are available from config file or commandline */
static struct opt_table opt_config_table[] = {
	OPT_WITH_ARG("--algo|-a",
		     set_algo, show_algo, &opt_algo,
		     "Specify sha256 implementation:\n"
		     "\tc\t\tLinux kernel sha256, implemented in C"
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
		),
	OPT_WITH_ARG("--cpu-threads|-t",
		     force_nthreads_int, opt_show_intval, &opt_n_threads,
		     "Number of miner CPU threads"),
	OPT_WITHOUT_ARG("--debug|-D",
		     enable_debug, &opt_debug,
		     "Enable debug output"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--device|-d",
		     set_devices, NULL, &opt_device,
	             "Select device to use, (Use repeat -d for multiple devices, default: all)"),
	OPT_WITH_ARG("--gpu-threads|-g",
		     set_int_0_to_10, opt_show_intval, &opt_g_threads,
		     "Number of threads per GPU (0 - 10)"),
	OPT_WITH_ARG("--intensity|-I",
		     forced_int_0_to_14, opt_show_intval, &scan_intensity,
		     "Intensity of GPU scanning (0 - 14, default: dynamic to maintain desktop interactivity)"),
#endif
	OPT_WITH_ARG("--log|-l",
		     set_int_0_to_9999, opt_show_intval, &opt_log_interval,
		     "Interval in seconds between log output"),
	OPT_WITHOUT_ARG("--no-longpoll",
			opt_set_invbool, &want_longpoll,
			"Disable X-Long-Polling support"),
	OPT_WITH_ARG("--pass|-p",
		     opt_set_charp, NULL, &rpc_pass,
		     "Password for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--protocol-dump|-P",
			opt_set_bool, &opt_protocol,
			"Verbose dump of protocol-level activities"),
	OPT_WITH_ARG("--queue|-Q",
		     set_int_1_to_10, opt_show_intval, &opt_queue,
		     "Number of extra work items to queue (1 - 10)"),
	OPT_WITHOUT_ARG("--quiet|-q",
			opt_set_bool, &opt_quiet,
			"Disable per-thread hashmeter output"),
	OPT_WITH_ARG("--retries|-r",
		     opt_set_intval, opt_show_intval, &opt_retries,
		     "Number of times to retry before giving up, if JSON-RPC call fails (-1 means never)"),
	OPT_WITH_ARG("--retry-pause|-R",
		     set_int_0_to_9999, opt_show_intval, &opt_fail_pause,
		     "Number of seconds to pause, between retries"),
	OPT_WITH_ARG("--scan-time|-s",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     "Upper bound on time spent scanning current work, in seconds"),
#ifdef HAVE_SYSLOG_H
	OPT_WITHOUT_ARG("--syslog",
			opt_set_bool, &use_syslog,
			"Use system log for output messages (default: standard error)"),
#endif
	OPT_WITHOUT_ARG("--text-only|-T",
			opt_set_invbool, &use_curses,
			"Disable ncurses formatted screen output"),
	OPT_WITH_ARG("--url|-o",
		     set_url, opt_show_charp, &rpc_url,
		     "URL for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--user|-u",
		     opt_set_charp, NULL, &rpc_user,
		     "Username for bitcoin JSON-RPC server"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--vectors|-v",
		     set_vector, NULL, &opt_vectors,
		     "Override detected optimal vector width (1, 2 or 4)"),
#endif
	OPT_WITHOUT_ARG("--verbose",
			opt_set_bool, &opt_log_output,
			"Log verbose output to stderr as well as status output"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--worksize|-w",
		     set_int_0_to_9999, opt_show_intval, &opt_worksize,
		     "Override detected optimal worksize"),
#endif
	OPT_WITH_ARG("--userpass|-O",
		     opt_set_charp, NULL, &rpc_userpass,
		     "Username:Password pair for bitcoin JSON-RPC server"),
	OPT_ENDTABLE
};

static char *parse_config(json_t *config)
{
	static char err_buf[200];
	json_t *val;
	struct opt_table *opt;

	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name;

		/* We don't handle subtables. */
		assert(!(opt->type & OPT_SUBTABLE));

		/* Pull apart the option name(s). */
		name = strdup(opt->names);
		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			char *err;
			/* Ignore short options. */
			if (p[1] != '-')
				continue;

			val = json_object_get(config, p+2);
			if (!val)
				continue;

			if ((opt->type & OPT_HASARG) && json_is_string(val)) {
				err = opt->cb_arg(json_string_value(val),
						  opt->u.arg);
			} else if ((opt->type&OPT_NOARG) && json_is_true(val)) {
				err = opt->cb(opt->u.arg);
			} else {
				err = "Invalid value";
			}
			if (err) {
				sprintf(err_buf, "Parsing JSON option %s: %s",
					p, err);
				return err_buf;
			}
		}
		free(name);
	}
	return NULL;
}

static char *load_config(const char *arg, void *unused)
{
	json_error_t err;
	json_t *config;

	config = json_load_file(arg, 0, &err);
	if (!json_is_object(config))
		return "JSON decode of file failed";

	/* Parse the config now, so we can override it.  That can keep pointers
	 * so don't free config object. */
	return parse_config(config);
}

static char *print_ndevs_and_exit(int *ndevs)
{
	printf("%i GPU devices detected\n", *ndevs);
	exit(*ndevs);
}

/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
	OPT_WITH_ARG("--config|-c",
		     load_config, NULL, NULL,
		     "Load a JSON-format configuration file\n"
		     "See example-cfg.json for an example configuration."),
	OPT_WITHOUT_ARG("--help|-h",
			opt_usage_and_exit,
#ifdef HAVE_OPENCL
			"\nBuilt with CPU and GPU mining support.\n\n",
#else
			"\nBuilt with CPU mining support only.\n\n",
#endif
			"Print this message"),
	OPT_WITHOUT_ARG("--ndevs|-n",
			print_ndevs_and_exit, &nDevs,
			"Enumerate number of detected GPUs and exit"),
	OPT_ENDTABLE
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

static inline int gpu_from_thr_id(int thr_id)
{
	return thr_id % nDevs;
}

static inline int cpu_from_thr_id(int thr_id)
{
	return (thr_id - gpu_threads) % num_processors;
}

static WINDOW *mainwin, *statuswin, *logwin;
static double total_secs = 0.1;
static char statusline[256];
static int cpucursor, gpucursor, logstart, logcursor;
static bool curses_active = false;
static struct cgpu_info *gpus, *cpus;

static void text_print_status(int thr_id)
{
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;

	printf(" %sPU %d: [%.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]\n",
	       cgpu->is_gpu ? "G" : "C", cgpu->cpu_gpu, cgpu->total_mhashes / total_secs,
			cgpu->getworks, cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->efficiency, cgpu->utility);
}

/* Must be called with curses mutex lock held and curses_active */
static void curses_print_status(int thr_id)
{
	wmove(statuswin, 0, 0);
	wattron(statuswin, A_BOLD);
	wprintw(statuswin, " " PROGRAM_NAME " version " VERSION " - Started: %s", datestamp);
	wattroff(statuswin, A_BOLD);
	wmove(statuswin, 1, 0);
	whline(statuswin, '-', 80);
	wmove(statuswin, 2,0);
	wprintw(statuswin, " %s", statusline);
	wclrtoeol(statuswin);
	wmove(statuswin, 3, 0);
	whline(statuswin, '-', 80);
	wmove(statuswin, logstart - 1, 0);
	whline(statuswin, '-', 80);

	if (thr_id >= 0 && thr_id < gpu_threads) {
		int gpu = gpu_from_thr_id(thr_id);
		struct cgpu_info *cgpu = &gpus[gpu];

		wmove(statuswin, gpucursor + gpu, 0);
		wprintw(statuswin, " GPU %d: [%.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]",
			gpu, cgpu->total_mhashes / total_secs,
			cgpu->getworks, cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->efficiency, cgpu->utility);
		wclrtoeol(statuswin);
	} else if (thr_id >= gpu_threads) {
		int cpu = cpu_from_thr_id(thr_id);
		struct cgpu_info *cgpu = &cpus[cpu];

		wmove(statuswin, cpucursor + cpu, 0);
		wprintw(statuswin, " CPU %d: [%.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]",
			cpu, cgpu->total_mhashes / total_secs,
			cgpu->getworks, cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->efficiency, cgpu->utility);
		wclrtoeol(statuswin);
	}
	wrefresh(statuswin);
}

static void print_status(int thr_id)
{
	if (!curses_active)
		text_print_status(thr_id);
	else {
		pthread_mutex_lock(&curses_lock);
		curses_print_status(thr_id);
		wrefresh(statuswin);
		pthread_mutex_unlock(&curses_lock);
	}
}

void log_curses(const char *f, va_list ap)
{
	if (curses_active) {
		pthread_mutex_lock(&curses_lock);
		vw_printw(logwin, f, ap);
		wrefresh(logwin);
		pthread_mutex_unlock(&curses_lock);
	} else
		vprintf(f, ap);
}

static bool submit_fail = false;

static bool submit_upstream_work(const struct work *work)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345];
	bool rc = false;
	int thr_id = work->thr_id;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
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
		applog(LOG_INFO, "submit_upstream_work json_rpc_call failed");
		if (!submit_fail) {
			submit_fail = true;
			remotefail_occasions++;
			applog(LOG_WARNING, "Upstream communication failure, caching submissions");
		}
		goto out;
	} else if (submit_fail) {
		submit_fail = false;
		applog(LOG_WARNING, "Upstream communication resumed, submitting work");
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
		if (!opt_quiet)
			applog(LOG_WARNING, "Share accepted from %sPU %d thread %d",
				cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, thr_id);
	} else {
		cgpu->rejected++;
		rejected++;
		if (opt_debug)
			applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!opt_quiet)
			applog(LOG_WARNING, "Share rejected from %sPU %d thread %d",
				cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, thr_id);
	}

	cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;
	cgpu->efficiency = cgpu->getworks ? cgpu->accepted * 100.0 / cgpu->getworks : 0.0;

	if (!opt_quiet)
		print_status(thr_id);
	applog(LOG_INFO, "%sPU %d  Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m",
		cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, cgpu->getworks, cgpu->accepted,
		cgpu->rejected, cgpu->hw_errors, cgpu->efficiency, cgpu->utility);

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
		applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");
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

static void disable_curses(void)
{
	if (curses_active) {
		curses_active = false;
		delwin(logwin);
		delwin(statuswin);
		delwin(mainwin);
		endwin();
		refresh();
	}
}

void kill_work(void)
{
	struct workio_cmd *wc;
	struct thr_info *thr;
	unsigned int i;

	disable_curses();
	applog(LOG_INFO, "Received kill message");

	/* Kill the watchdog thread */
	thr = &thr_info[watchdog_thr_id];
	pthread_cancel(thr->pth);

	/* Stop the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		thr = &thr_info[i];
		tq_freeze(thr->q);
		/* No need to check if this succeeds or not */
		pthread_cancel(thr->pth);
	}

	/* Stop the others */
	thr = &thr_info[stage_thr_id];
	pthread_cancel(thr->pth);
	thr = &thr_info[longpoll_thr_id];
	pthread_cancel(thr->pth);

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

static void sighandler(int sig)
{
	kill_work();
}

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
		applog(LOG_DEBUG, "json_rpc_call failed on get work, retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}

	/* send work to requesting thread */
	if (unlikely(!tq_push(thr_info[stage_thr_id].q, ret_work))) {
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
	char *hexstr;

	pthread_detach(pthread_self());

	hexstr = bin2hex(wc->u.work->data, 36);
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "submit_work_thread OOM");
		goto out;
	}
	if (unlikely(strncmp(hexstr, current_block, 36))) {
		applog(LOG_WARNING, "Stale work detected, discarding");
		stale_shares++;
		goto out_free;
	}

	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(wc->u.work)) {
		if (unlikely(strncmp(hexstr, current_block, 36))) {
			applog(LOG_WARNING, "Stale work detected, discarding");
			stale_shares++;
			goto out_free;
		}
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			applog(LOG_ERR, "Failed %d retries ...terminating workio thread", opt_retries);
			kill_work();
			goto out_free;
		}

		/* pause, then restart work-request loop */
		applog(LOG_INFO, "json_rpc_call failed on submit_work, retry after %d seconds",
			opt_fail_pause);
		sleep(opt_fail_pause);
	}
out_free:
	free(hexstr);
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

static void inc_staged(int inc, bool lp)
{
	pthread_mutex_lock(&stgd_lock);
	if (lp) {
		lp_staged += inc;
		total_staged += inc;
	} else if (lp_staged)
		lp_staged--;
	else
		total_staged += inc;
	pthread_mutex_unlock(&stgd_lock);
}

static void dec_staged(int inc)
{
	pthread_mutex_lock(&stgd_lock);
	if (lp_staged)
		lp_staged -= inc;
	total_staged -= inc;
	pthread_mutex_unlock(&stgd_lock);
}

static int requests_staged(void)
{
	int ret;

	pthread_mutex_lock(&stgd_lock);
	ret = total_staged;
	pthread_mutex_unlock(&stgd_lock);
	return ret;
}

static void *stage_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	bool ok = true;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (ok) {
		struct work *work = NULL;
		char *hexstr;

		work = tq_pop(mythr->q, NULL);
		if (unlikely(!work)) {
			applog(LOG_ERR, "Failed to tq_pop in stage_thread");
			ok = false;
			break;
		}

		hexstr = bin2hex(work->data, 36);
		if (unlikely(!hexstr)) {
			applog(LOG_ERR, "stage_thread OOM");
			break;
		}

		/* current_block is blanked out on successful longpoll */
		if (likely(strncmp(current_block, blank, 36))) {
			if (unlikely(strncmp(hexstr, current_block, 36))) {
				new_blocks++;
				if (want_longpoll)
					applog(LOG_WARNING, "New block detected on network before receiving longpoll, flushing work queue");
				else
					applog(LOG_WARNING, "New block detected on network, flushing work queue");
				/* As we can't flush the work from here, signal
				 * the wakeup thread to restart all the
				 * threads */
				work_restart[stage_thr_id].restart = 1;
			}
		} else
			memcpy(longpoll_block, hexstr, 36);
		memcpy(current_block, hexstr, 36);
		free(hexstr);

		if (unlikely(!tq_push(thr_info[0].q, work))) {
			applog(LOG_ERR, "Failed to tq_push work in stage_thread");
			ok = false;
			break;
		}
		inc_staged(1, false);
	}

	tq_freeze(mythr->q);
	return NULL;
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
			applog(LOG_ERR, "Failed to tq_pop in workio_thread");
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
	double local_secs;
	double utility, efficiency = 0.0;
	static double local_mhashes_done = 0;
	static double rolling_local = 0;
	double local_mhashes = (double)hashes_done / 1000000.0;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;

	/* Update the last time this thread reported in */
	if (thr_id >= 0)
		gettimeofday(&thr_info[thr_id].last, NULL);

	/* Don't bother calculating anything if we're not displaying it */
	if (opt_quiet || !opt_log_interval)
		return;
	
	khashes = hashes_done / 1000.0;
	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

	if (thr_id >= 0 && secs) {
		/* So we can call hashmeter from a non worker thread */
		if (opt_debug)
			applog(LOG_DEBUG, "[thread %d: %lu hashes, %.0f khash/sec]",
				thr_id, hashes_done, hashes_done / secs);
		cgpu->local_mhashes += local_mhashes;
		cgpu->total_mhashes += local_mhashes;
	}

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

	utility = accepted / ( total_secs ? total_secs : 1 ) * 60;
	efficiency = getwork_requested ? accepted * 100.0 / getwork_requested : 0.0;

	sprintf(statusline, "[(%ds):%.1f  (avg):%.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]",
		opt_log_interval, rolling_local / local_secs, total_mhashes_done / total_secs,
		getwork_requested, accepted, rejected, hw_errors, efficiency, utility);
	if (!curses_active) {
		printf("%s          \r", statusline);
		fflush(stdout);
	} else
		applog(LOG_INFO, "%s", statusline);

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
	dec_staged(1);
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
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to tq_pop in queue_request");
		return false;
	}

	wc->cmd = WC_GET_WORK;
	wc->thr = thr;

	/* send work request to workio thread */
	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		applog(LOG_ERR, "Failed to tq_push in queue_request");
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
	if (unlikely(!requests_queued())) {
		applog(LOG_WARNING, "Tried to discard_request with nil queued");
		return true;
	}

	work_heap = tq_pop(thr->q, NULL);
	if (unlikely(!work_heap)) {
		applog(LOG_ERR, "Failed to tq_pop in discard_request");
		return false;
	}
	free(work_heap);
	dec_queued();
	discarded_work++;
	return true;
}

static void flush_requests(bool longpoll)
{
	int i, extra;

	extra = requests_queued();
	/* When flushing from longpoll, we don't know the new work yet. When
	 * not flushing from longpoll, the first work item is valid so do not
	 * discard it */
	if (longpoll)
		memcpy(current_block, blank, 36);
	else
		extra--;

	/* Temporarily increase the staged count so that get_work thinks there
	 * is work available instead of making threads reuse existing work */
	if (extra >= mining_threads)
		inc_staged(mining_threads, true);
	else
		inc_staged(extra, true);

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
	struct timeval now;
	struct timespec abstime = {};
	struct thr_info *thr = &thr_info[0];
	struct work *work_heap;
	bool ret = false;
	int failures = 0;

	getwork_requested++;

retry:
	gettimeofday(&now, NULL);
	abstime.tv_sec = now.tv_sec + 60;

	if (unlikely(!queued && !queue_request())) {
		applog(LOG_WARNING, "Failed to queue_request in get_work");
		goto out;
	}

	if (!requests_staged()) {
		uint32_t *work_ntime;
		uint32_t ntime;

		/* Only print this message once each time we shift to localgen */
		if (!localgen) {
			applog(LOG_WARNING, "Server not providing work fast enough, generating work locally");
			localgen = true;
			localgen_occasions++;
		}
		work_ntime = (uint32_t *)(work->data + 68);
		ntime = be32toh(*work_ntime);
		ntime++;
		*work_ntime = htobe32(ntime);
		ret = true;
		local_work++;
		goto out;
	} else if (localgen) {
		localgen = false;
		applog(LOG_WARNING, "Resumed retrieving work from server");
	}

	/* Wait for 1st response, or get cached response. We really should
	 * never time out on the pop request but something might go amiss :/
	 */
	work_heap = tq_pop(thr->q, &abstime);
	if (unlikely(!work_heap)) {
		applog(LOG_INFO, "Failed to tq_pop in get_work");
		goto out;
	}
	dec_queued();

	memcpy(work, work_heap, sizeof(*work));
	
	ret = true;
	free(work_heap);
out:
	if (unlikely(ret == false)) {
		if ((opt_retries >= 0) && (++failures > opt_retries)) {
			applog(LOG_ERR, "Failed %d times to get_work");
			return ret;
		}
		applog(LOG_DEBUG, "Retrying after %d seconds", opt_fail_pause);
		sleep(opt_fail_pause);
		goto retry;
	}
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

static void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	uint32_t max_nonce = 0xffffff;
	unsigned long hashes_done = max_nonce;
	bool needs_work = true;
	/* Try to cycle approximately 5 times before each log update */
	const unsigned long cycle = opt_log_interval / 5 ? : 1;
	/* Request the next work item at 2/3 of the scantime */
	unsigned const int request_interval = opt_scantime * 2 / 3 ? : 1;
	unsigned const long request_nonce = MAXTHREADS / 3 * 2;
	bool requested = true;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

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
		struct timeval tv_workstart, tv_start, tv_end, diff;
		uint64_t max64;
		bool rc;

		if (needs_work) {
			gettimeofday(&tv_workstart, NULL);
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(&work, requested))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"mining thread %d", thr_id);
				goto out;
			}
			mythr->cgpu->getworks++;
			work.thr_id = thr_id;
			needs_work = requested = false;
			work.blk.nonce = 0;
			max_nonce = hashes_done;
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
		if (diff.tv_sec && diff.tv_sec != cycle) {
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
			if (unlikely(!submit_work_sync(mythr, &work))) {
				applog(LOG_ERR, "Failed to submit_work_sync in miner_thread %d", thr_id);
				break;
			}
			work.blk.nonce += 4;
		}

		timeval_subtract(&diff, &tv_end, &tv_workstart);
		if (!requested && (diff.tv_sec > request_interval || work.blk.nonce > request_nonce)) {
			if (unlikely(!queue_request())) {
				applog(LOG_ERR, "Failed to queue_request in miner_thread %d", thr_id);
				goto out;
			}
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

#ifdef HAVE_OPENCL
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

static void set_threads_hashes(unsigned int vectors, unsigned int *threads,
			       unsigned int *hashes, size_t *globalThreads)
{
	*globalThreads = *threads = 1 << (15 + scan_intensity);
	*hashes = *threads * vectors;
}

static void *gpuminer_thread(void *userdata)
{
	const unsigned long cycle = opt_log_interval / 5 ? : 1;
	struct timeval tv_start, tv_end, diff, tv_workstart;
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	uint32_t *res, *blank_res;
	double gpu_ms_average = 7;

	size_t globalThreads[1];
	size_t localThreads[1];

	cl_int status;

	_clState *clState = clStates[thr_id];
	const cl_kernel *kernel = &clState->kernel;

	struct work *work = malloc(sizeof(struct work));
	unsigned int threads = 1 << (15 + scan_intensity);
	unsigned const int vectors = clState->preferred_vwidth;
	unsigned int hashes = threads * vectors;
	unsigned int hashes_done = 0;

	/* Request the next work item at 2/3 of the scantime */
	unsigned const int request_interval = opt_scantime * 2 / 3 ? : 1;
	unsigned const long request_nonce = MAXTHREADS / 3 * 2;
	bool requested = true;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	res = calloc(BUFFERSIZE, 1);
	blank_res = calloc(BUFFERSIZE, 1);

	if (!res || !blank_res) {
		applog(LOG_ERR, "Failed to calloc in gpuminer_thread");
		goto out;
	}

	gettimeofday(&tv_start, NULL);
	globalThreads[0] = threads;
	localThreads[0] = clState->work_size;
	diff.tv_sec = 0;
	gettimeofday(&tv_end, NULL);

	status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0,
			BUFFERSIZE, blank_res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS))
		{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }
	gettimeofday(&tv_workstart, NULL);
	/* obtain new work from internal workio thread */
	if (unlikely(!get_work(work, requested))) {
		applog(LOG_ERR, "work retrieval failed, exiting "
			"gpu mining thread %d", mythr->id);
		goto out;
	}
	mythr->cgpu->getworks++;
	work->thr_id = thr_id;
	requested = false;
	precalc_hash(&work->blk, (uint32_t *)(work->midstate), (uint32_t *)(work->data + 64));
	work->blk.nonce = 0;

	while (1) {
		struct timeval tv_gpustart, tv_gpuend;
		suseconds_t gpu_us;

		gettimeofday(&tv_gpustart, NULL);
		timeval_subtract(&diff, &tv_gpustart, &tv_gpuend);
		/* This finish flushes the readbuffer set with CL_FALSE later */
		clFinish(clState->commandQueue);
		gettimeofday(&tv_gpuend, NULL);
		timeval_subtract(&diff, &tv_gpuend, &tv_gpustart);
		gpu_us = diff.tv_sec * 1000000 + diff.tv_usec;
		gpu_ms_average = ((gpu_us / 1000) + gpu_ms_average * 0.9) / 1.9;
		if (opt_dynamic) {
			/* Try to not let the GPU be out for longer than 6ms, but
			 * increase intensity when the system is idle, unless
			 * dynamic is disabled. */
			if (gpu_ms_average > 7) {
				if (scan_intensity > 0)
					scan_intensity--;
				set_threads_hashes(vectors, &threads, &hashes, globalThreads);
			} else if (gpu_ms_average < 3) {
				if (scan_intensity < 14)
					scan_intensity++;
				set_threads_hashes(vectors, &threads, &hashes, globalThreads);
			}
		}

		if (diff.tv_sec > opt_scantime  || work->blk.nonce >= MAXTHREADS - hashes || work_restart[thr_id].restart) {
			/* Ignore any reads since we're getting new work and queue a clean buffer */
			status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
					BUFFERSIZE, blank_res, 0, NULL, NULL);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }
			memset(res, 0, BUFFERSIZE);

			gettimeofday(&tv_workstart, NULL);
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(work, requested))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"gpu mining thread %d", mythr->id);
				goto out;
			}
			mythr->cgpu->getworks++;
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
			if (unlikely(!queue_request())) {
				applog(LOG_ERR, "Failed to queue_request in gpuminer_thread %d", thr_id);
				goto out;
			}
			requested = true;
		}
	}
out:
	tq_freeze(mythr->q);

	return NULL;
}
#endif /* HAVE_OPENCL */

static void restart_threads(bool longpoll)
{
	int i;

	/* Discard old queued requests and get new ones */
	flush_requests(longpoll);

	for (i = 0; i < mining_threads; i++)
		work_restart[i].restart = 1;
}

static void *longpoll_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	CURL *curl = NULL;
	char *copy_start, *hdr_path, *lp_url = NULL;
	bool need_slash = false;
	int failures = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

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
		struct timeval start, end;
		json_t *val;

		gettimeofday(&start, NULL);
		val = json_rpc_call(curl, lp_url, rpc_userpass, rpc_req,
				    false, true);
		if (likely(val)) {
			failures = 0;
			json_decref(val);

			/* Keep track of who ordered a restart_threads to make
			 * sure it's only done once per new block */
			if (likely(!strncmp(longpoll_block, blank, 36) ||
				!strncmp(longpoll_block, current_block, 36))) {
					new_blocks++;
					applog(LOG_WARNING, "LONGPOLL detected new block on network, flushing work queue");
					restart_threads(true);
			} else
				applog(LOG_WARNING, "LONGPOLL received after new block already detected");
		} else {
			/* Some pools regularly drop the longpoll request so
			 * only see this as longpoll failure if it happens
			 * immediately and just restart it the rest of the
			 * time. */
			gettimeofday(&end, NULL);
			if (end.tv_sec - start.tv_sec > 30)
				continue;
			if (failures++ < 10) {
				sleep(30);
				applog(LOG_WARNING,
					"longpoll failed, sleeping for 30s");
			} else {
				applog(LOG_ERR,
					"longpoll failed, ending thread");
				goto out;
			}
		}
		memcpy(longpoll_block, current_block, 36);
	}

out:
	free(hdr_path);
	free(lp_url);
	tq_freeze(mythr->q);
	if (curl)
		curl_easy_cleanup(curl);

	return NULL;
}

static void reinit_cputhread(int thr_id)
{
	struct thr_info *thr = &thr_info[thr_id];

	tq_freeze(thr->q);
	if (unlikely(pthread_cancel(thr->pth))) {
		applog(LOG_ERR, "Failed to pthread_cancel in reinit_gputhread");
		goto failed_out;
	}

	if (unlikely(pthread_join(thr->pth, NULL))) {
		applog(LOG_ERR, "Failed to pthread_join in reinit_gputhread");
		goto failed_out;
	}

	applog(LOG_INFO, "Reinit CPU thread %d", thr_id);
	tq_thaw(thr->q);

	gettimeofday(&thr->last, NULL);

	if (unlikely(pthread_create(&thr->pth, NULL, miner_thread, thr))) {
		applog(LOG_ERR, "thread %d create failed", thr_id);
		goto failed_out;
	}
	return;

failed_out:
	kill_work();
}

#ifdef HAVE_OPENCL
static void reinit_gputhread(int thr_id)
{
	int gpu = gpu_from_thr_id(thr_id);
	struct thr_info *thr = &thr_info[thr_id];
	char name[256];

	tq_freeze(thr->q);
	if (unlikely(pthread_cancel(thr->pth))) {
		applog(LOG_ERR, "Failed to pthread_cancel in reinit_gputhread");
		goto failed_out;
	}
	if (unlikely(pthread_join(thr->pth, NULL))) {
		applog(LOG_ERR, "Failed to pthread_join in reinit_gputhread");
		goto failed_out;
	}
	free(clStates[thr_id]);

	applog(LOG_INFO, "Reinit GPU thread %d", thr_id);
	tq_thaw(thr->q);
	clStates[thr_id] = initCl(gpu, name, sizeof(name));
	if (!clStates[thr_id]) {
		applog(LOG_ERR, "Failed to reinit GPU thread %d", thr_id);
		goto failed_out;
	}
	applog(LOG_INFO, "initCl() finished. Found %s", name);

	gettimeofday(&thr->last, NULL);

	if (unlikely(pthread_create(&thr->pth, NULL, gpuminer_thread, thr))) {
		applog(LOG_ERR, "thread %d create failed", thr_id);
		goto failed_out;
	}
	return;

failed_out:
	kill_work();
}

static void reinit_thread(int thr_id)
{
	if (thr_id < gpu_threads)
		reinit_gputhread(thr_id);
	else
		reinit_cputhread(thr_id);
}
#else
static void reinit_thread(int thr_id)
{
	reinit_cputhread(thr_id);
}
#endif

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
static void *watchdog_thread(void *userdata)
{
	const unsigned int interval = opt_log_interval / 2 ? : 1;
	struct timeval zero_tv;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	memset(&zero_tv, 0, sizeof(struct timeval));

	while (1) {
		int x, y, logx, logy, i;
		struct timeval now;

		sleep(interval);
		if (requests_queued() < opt_queue)
			queue_request();

		hashmeter(-1, &zero_tv, 0);

		if (curses_active) {
			pthread_mutex_lock(&curses_lock);
			getmaxyx(mainwin, y, x);
			getmaxyx(logwin, logy, logx);
			y -= logcursor;
			/* Detect screen size change */
			if (x != logx || y != logy)
				wresize(logwin, y, x);
			for (i = 0; i < mining_threads; i++)
				curses_print_status(i);
			redrawwin(logwin);
			redrawwin(statuswin);
			pthread_mutex_unlock(&curses_lock);
		}

		if (unlikely(work_restart[stage_thr_id].restart)) {
			restart_threads(false);
			work_restart[stage_thr_id].restart = 0;
		}

		gettimeofday(&now, NULL);
		for (i = 0; i < mining_threads; i++) {
			struct thr_info *thr = &thr_info[i];

			/* Do not kill threads waiting on longpoll staged work */
			if (now.tv_sec - thr->last.tv_sec > 60 && !lp_staged) {
				applog(LOG_ERR, "Attempting to restart thread %d, idle for more than 60 seconds", i);
				/* Create one mandatory work item */
				inc_staged(1, true);
				if (unlikely(!queue_request())) {
					applog(LOG_ERR, "Failed to queue_request in watchdog_thread");
					kill_work();
					break;
				}
				reinit_thread(i);
				applog(LOG_WARNING, "Thread %d restarted", i);
			}
		}
	}

	return NULL;
}

static void print_summary(void)
{
	struct timeval diff;
	int hours, mins, secs, i;
	double utility, efficiency = 0.0;

	timeval_subtract(&diff, &total_tv_end, &total_tv_start);
	hours = diff.tv_sec / 3600;
	mins = (diff.tv_sec % 3600) / 60;
	secs = diff.tv_sec % 60;

	utility = accepted / ( total_secs ? total_secs : 1 ) * 60;
	efficiency = getwork_requested ? accepted * 100.0 / getwork_requested : 0.0;

	printf("\nSummary of runtime statistics:\n\n");
	printf("Started at %s\n", datestamp);
	printf("Runtime: %d hrs : %d mins : %d secs\n", hours, mins, secs);
	if (total_secs)
		printf("Average hashrate: %.1f Megahash/s\n", total_mhashes_done / total_secs);
	printf("Queued work requests: %d\n", getwork_requested);
	printf("Share submissions: %d\n", accepted + rejected);
	printf("Accepted shares: %d\n", accepted);
	printf("Rejected shares: %d\n", rejected);
	if (accepted || rejected)
		printf("Reject ratio: %.1f\n", (double)(rejected * 100) / (double)(accepted + rejected));
	printf("Hardware errors: %d\n", hw_errors);
	printf("Efficiency (accepted / queued): %.0f%%\n", efficiency);
	printf("Utility (accepted shares / min): %.2f/min\n\n", utility);
	printf("Discarded work due to new blocks: %d\n", discarded_work);
	printf("Stale submissions discarded due to new blocks: %d\n", stale_shares);
	printf("Unable to get work from server occasions: %d\n", localgen_occasions);
	printf("Work items generated locally: %d\n", local_work);
	printf("Submitting work remotely delay occasions: %d\n", remotefail_occasions);
	printf("New blocks detected on network: %d\n\n", new_blocks);

	printf("Summary of per device statistics:\n\n");
	for (i = 0; i < mining_threads; i++) {
		if (i < gpu_threads) {
			if (i < nDevs && gpu_devices[gpu_from_thr_id(i)])
				print_status(i);
		} else if (i < gpu_threads + num_processors)
			print_status(i);
	}
	printf("\n");
}

int main (int argc, char *argv[])
{
	unsigned int i, j = 0, x, y;
	struct sigaction handler;
	struct thr_info *thr;
	char name[256];
	struct tm tm;

	if (unlikely(pthread_mutex_init(&hash_lock, NULL)))
		return 1;
	if (unlikely(pthread_mutex_init(&qd_lock, NULL)))
		return 1;
	if (unlikely(pthread_mutex_init(&stgd_lock, NULL)))
		return 1;
	if (unlikely(pthread_mutex_init(&curses_lock, NULL)))
		return 1;

	handler.sa_handler = &sighandler;
	sigaction(SIGTERM, &handler, 0);
	sigaction(SIGINT, &handler, 0);

	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);
	localtime_r(&total_tv_start.tv_sec, &tm);
	sprintf(datestamp, "[%d-%02d-%02d %02d:%02d:%02d]",
		tm.tm_year + 1900,
		tm.tm_mon + 1,
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec);

	for (i = 0; i < 36; i++) {
		strcat(blank, "0");
		strcat(current_block, "0");
		strcat(longpoll_block, "0");
	}

#ifdef WIN32
	opt_n_threads = num_processors = 1;
#else
	num_processors = sysconf(_SC_NPROCESSORS_ONLN);
	opt_n_threads = num_processors;
#endif /* !WIN32 */

#ifdef HAVE_OPENCL
	for (i = 0; i < 16; i++)
		gpu_devices[i] = false;
	nDevs = clDevicesNum();
	if (nDevs < 0)
		return 1;
#endif
	if (nDevs)
		opt_n_threads = 0;

	rpc_url = strdup(DEF_RPC_URL);

	/* parse command line */
	opt_register_table(opt_config_table,
			   "Options for both config file and command line");
	opt_register_table(opt_cmdline_table,
			   "Options for command line only");

	opt_parse(&argc, argv, applog_and_exit);
	if (argc != 1) {
		applog(LOG_ERR, "Unexpected extra commandline arguments");
		return 1;
	}

	if (total_devices) {
		if (total_devices > nDevs) {
			applog(LOG_ERR, "More devices specified than exist");
			return 1;
		}
		for (i = 0; i < 16; i++)
			if (gpu_devices[i] && i + 1 > nDevs) {
				applog(LOG_ERR, "Command line options set a device that doesn't exist");
				return 1;
			}
		gpu_threads = total_devices * opt_g_threads;
	} else {
		gpu_threads = nDevs * opt_g_threads;
		for (i = 0; i < nDevs; i++)
			gpu_devices[i] = true;
	}

	if (!gpu_threads && !forced_n_threads) {
		/* Maybe they turned GPU off; restore default CPU threads. */
		opt_n_threads = num_processors;
	}

	logcursor = 4;
	mining_threads = opt_n_threads + gpu_threads;
	gpucursor = logcursor;
	cpucursor = gpucursor + nDevs;
	logstart = cpucursor + (opt_n_threads ? num_processors : 0) + 1;
	logcursor = logstart + 1;

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

	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		return 1;
#ifdef HAVE_SYSLOG_H
	if (use_syslog)
		openlog("cpuminer", LOG_PID, LOG_USER);
#endif

	work_restart = calloc(mining_threads + 4, sizeof(*work_restart));
	if (!work_restart)
		return 1;

	thr_info = calloc(mining_threads + 4, sizeof(*thr));
	if (!thr_info)
		return 1;

	/* init workio thread info */
	work_thr_id = mining_threads;
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
		longpoll_thr_id = mining_threads + 1;
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

	stage_thr_id = mining_threads + 3;
	thr = &thr_info[stage_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		return 1;
	/* start stage thread */
	if (pthread_create(&thr->pth, NULL, stage_thread, thr)) {
		applog(LOG_ERR, "stage thread create failed");
		return 1;
	}

	/* Flag the work as ready forcing the mining threads to wait till we
	 * actually put something into the queue */
	inc_staged(mining_threads, true);

#ifdef HAVE_OPENCL
	i = 0;

	/* start GPU mining threads */
	for (j = 0; j < nDevs * opt_g_threads; j++) {
		int gpu = gpu_from_thr_id(j);

		/* Skip devices not set to work */
		if (!gpu_devices[gpu])
			continue;
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

		gettimeofday(&thr->last, NULL);

		if (unlikely(pthread_create(&thr->pth, NULL, gpuminer_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", i);
			return 1;
		}
		i++;
	}

	applog(LOG_INFO, "%d gpu miner threads started", gpu_threads);
#endif

	/* start CPU mining threads */
	for (i = gpu_threads; i < mining_threads; i++) {
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

		gettimeofday(&thr->last, NULL);

		if (unlikely(pthread_create(&thr->pth, NULL, miner_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", i);
			return 1;
		}
	}

	applog(LOG_INFO, "%d cpu miner threads started, "
		"using SHA256 '%s' algorithm.",
		opt_n_threads,
		algo_names[opt_algo]);

	watchdog_thr_id = mining_threads + 2;
	thr = &thr_info[watchdog_thr_id];
	/* start wakeup thread */
	if (pthread_create(&thr->pth, NULL, watchdog_thread, NULL)) {
		applog(LOG_ERR, "wakeup thread create failed");
		return 1;
	}

	/* Restart count as it will be wrong till all threads are started */
	pthread_mutex_lock(&hash_lock);
	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);
	total_mhashes_done = 0;
	pthread_mutex_unlock(&hash_lock);

	/* Set up the ncurses interface */
	if (!opt_quiet && use_curses) {
		mainwin = initscr();
		getmaxyx(mainwin, y, x);
		statuswin = newwin(logstart, x, 0, 0);
		logwin = newwin(y - logcursor, 0, logcursor, 0);
		idlok(logwin, true);
		scrollok(logwin, true);
		leaveok(logwin, true);
		leaveok(statuswin, true);
		curses_active = true;
		for (i = 0; i < mining_threads; i++)
			print_status(i);
	}

	/* Now that everything's ready put enough work in the queue */
	for (i = 0; i < opt_queue + mining_threads; i++) {
		if (unlikely(!queue_request())) {
			applog(LOG_ERR, "Failed to queue_request in main");
			return 1;
		}
	}

	/* main loop - simply wait for workio thread to exit */
	pthread_join(thr_info[work_thr_id].pth, NULL);
	applog(LOG_INFO, "workio thread dead, exiting.");

	gettimeofday(&total_tv_end, NULL);
	curl_global_cleanup();
	disable_curses();
	if (!opt_quiet && successful_connect)
		print_summary();

	if (gpu_threads)
		free(gpus);
	if (opt_n_threads)
		free(cpus);

	return 0;
}

