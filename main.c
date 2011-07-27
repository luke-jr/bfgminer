
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
	bool			lagging;
};

enum sha256_algos {
	ALGO_C,			/* plain C */
	ALGO_4WAY,		/* parallel SSE2 */
	ALGO_VIA,		/* VIA padlock */
	ALGO_CRYPTOPP,		/* Crypto++ (C) */
	ALGO_CRYPTOPP_ASM32,	/* Crypto++ 32-bit assembly */
	ALGO_SSE2_64,		/* SSE2 for x86_64 */
	ALGO_SSE4_64,		/* SSE4 for x86_64 */
};

enum pool_strategy {
	POOL_FAILOVER,
	POOL_ROUNDROBIN,
	POOL_ROTATE,
	POOL_LOADBALANCE,
};

#define TOP_STRATEGY (POOL_LOADBALANCE)

struct strategies {
	const char *s;
} strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
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
#ifdef WANT_X8664_SSE4
	[ALGO_SSE4_64]		= "sse4_64",
#endif
};

bool opt_debug = false;
bool opt_protocol = false;
bool want_longpoll = true;
bool have_longpoll = false;
bool use_syslog = false;
static bool opt_quiet = false;
static bool opt_loginput = false;
static int opt_retries = -1;
static int opt_fail_pause = 5;
static int opt_log_interval = 5;
bool opt_log_output = false;
static bool opt_dynamic = true;
static int opt_queue;
int opt_vectors;
int opt_worksize;
int opt_scantime = 60;
static const bool opt_time = true;
#if defined(WANT_X8664_SSE4) && defined(__SSE4_1__)
static enum sha256_algos opt_algo = ALGO_SSE4_64;
#elif defined(WANT_X8664_SSE2) && defined(__SSE2__)
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

struct thr_info *thr_info;
static int work_thr_id;
int longpoll_thr_id;
static int stage_thr_id;
static int watchdog_thr_id;
static int input_thr_id;
static int total_threads;

struct work_restart *work_restart = NULL;

static pthread_mutex_t hash_lock;
static pthread_mutex_t qd_lock;
static pthread_mutex_t stgd_lock;
static pthread_mutex_t curses_lock;
static double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;

pthread_mutex_t control_lock;

int hw_errors;
static int total_accepted, total_rejected;
static int total_getworks, total_stale, total_discarded;
static int total_queued, total_staged, lp_staged;
static unsigned int new_blocks;

enum block_change {
	BLOCK_NONE,
	BLOCK_LP,
	BLOCK_DETECT,
	BLOCK_FIRST,
};

static enum block_change block_changed = BLOCK_FIRST;
static unsigned int local_work;
static unsigned int total_lo, total_ro;

#define MAX_POOLS (32)

static struct pool *pools[MAX_POOLS];
static struct pool *currentpool = NULL;
static int total_pools;
static enum pool_strategy pool_strategy = POOL_FAILOVER;
static int opt_rotate_period;
static int total_urls, total_users, total_passes, total_userpasses;

static bool curses_active = false;

static char current_block[37];
static char datestamp[40];
static char blockdate[40];

static char *opt_kernel = NULL;

enum cl_kernel chosen_kernel;

static bool ping = true;

struct sigaction termhandler, inthandler;

struct thread_q *getq;

void get_datestamp(char *f, struct timeval *tv)
{
	struct tm tm;

	localtime_r(&tv->tv_sec, &tm);
	sprintf(f, "[%d-%02d-%02d %02d:%02d:%02d]",
		tm.tm_year + 1900,
		tm.tm_mon + 1,
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec);
}

static void applog_and_exit(const char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	vapplog(LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
}

static void add_pool(void)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool) {
		applog(LOG_ERR, "Failed to malloc pool in add_pool");
		exit (1);
	}
	pool->pool_no = pool->prio = total_pools;
	pools[total_pools++] = pool;
	if (unlikely(pthread_mutex_init(&pool->pool_lock, NULL))) {
		applog(LOG_ERR, "Failed to pthread_mutex_init in add_pool");
		exit (1);
	}
	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;
}

/* Pool variant of test and set */
static bool pool_tset(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = true;
	mutex_unlock(&pool->pool_lock);
	return ret;
}

static bool pool_tclear(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = false;
	mutex_unlock(&pool->pool_lock);
	return ret;
}

static struct pool *current_pool(void)
{
	struct pool *pool;

	mutex_lock(&control_lock);
	pool = currentpool;
	mutex_unlock(&control_lock);
	return pool;
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

static char *forced_int_1010(const char *arg, int *i)
{
	opt_dynamic = false;
	return set_int_range(arg, i, -10, 10);
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

static char *set_loadbalance(enum pool_strategy *strategy)
{
	*strategy = POOL_LOADBALANCE;
	return NULL;
}

static char *set_rotate(const char *arg, int *i)
{
	pool_strategy = POOL_ROTATE;
	return set_int_range(arg, i, 0, 9999);
}

static char *set_rr(enum pool_strategy *strategy)
{
	*strategy = POOL_ROUNDROBIN;
	return NULL;
}

static char *set_url(const char *arg, char **p)
{
	struct pool *pool;

	total_urls++;
	if (total_urls > total_pools)
		add_pool();
	pool = pools[total_urls - 1];

	opt_set_charp(arg, &pool->rpc_url);
	if (strncmp(arg, "http://", 7) &&
	    strncmp(arg, "https://", 8))
		return "URL must start with http:// or https://";

	return NULL;
}

static char *set_user(const char *arg, char **p)
{
	struct pool *pool;

	if (total_userpasses)
		return "Use only user + pass or userpass, but not both";
	total_users++;
	if (total_users > total_pools)
		add_pool();

	pool = pools[total_users - 1];
	opt_set_charp(arg, &pool->rpc_user);

	return NULL;
}

static char *set_pass(const char *arg, char **p)
{
	struct pool *pool;

	if (total_userpasses)
		return "Use only user + pass or userpass, but not both";
	total_passes++;
	if (total_passes > total_pools)
		add_pool();

	pool = pools[total_passes - 1];
	opt_set_charp(arg, &pool->rpc_pass);

	return NULL;
}

static char *set_userpass(const char *arg, char **p)
{
	struct pool *pool;

	if (total_users || total_passes)
		return "Use only user + pass or userpass, but not both";
	total_userpasses++;
	if (total_userpasses > total_pools)
		add_pool();

	pool = pools[total_userpasses - 1];
	opt_set_charp(arg, &pool->rpc_userpass);

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

static char *trpc_url;
static char *trpc_userpass;
static char *trpc_user, *trpc_pass;

/* These options are available from config file or commandline */
static struct opt_table opt_config_table[] = {
	OPT_WITH_ARG("--algo|-a",
		     set_algo, show_algo, &opt_algo,
		     "Specify sha256 implementation for CPU mining:\n"
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
#ifdef WANT_X8664_SSE4
		     "\n\tsse4_64\t\tSSE4 implementation for x86_64 machines"
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
		     forced_int_1010, opt_show_intval, &scan_intensity,
		     "Intensity of GPU scanning (-10 -> 10, default: dynamic to maintain desktop interactivity)"),
	OPT_WITH_ARG("--kernel|-k",
		     opt_set_charp, NULL, &opt_kernel,
		     "Select kernel to use (poclbm or phatk - default: auto)"),
#endif
	OPT_WITHOUT_ARG("--load-balance",
		     set_loadbalance, &pool_strategy,
		     "Change multipool strategy from failover to even load balance"),
	OPT_WITH_ARG("--log|-l",
		     set_int_0_to_9999, opt_show_intval, &opt_log_interval,
		     "Interval in seconds between log output"),
	OPT_WITHOUT_ARG("--no-longpoll",
			opt_set_invbool, &want_longpoll,
			"Disable X-Long-Polling support"),
	OPT_WITH_ARG("--pass|-p",
		     set_pass, NULL, &trpc_pass,
		     "Password for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--protocol-dump|-P",
			opt_set_bool, &opt_protocol,
			"Verbose dump of protocol-level activities"),
	OPT_WITH_ARG("--queue|-Q",
		     set_int_0_to_10, opt_show_intval, &opt_queue,
		     "Number of extra work items to queue (0 - 10)"),
	OPT_WITHOUT_ARG("--quiet|-q",
			opt_set_bool, &opt_quiet,
			"Disable per-thread hashmeter output"),
	OPT_WITH_ARG("--retries|-r",
		     opt_set_intval, opt_show_intval, &opt_retries,
		     "Number of times to retry before giving up, if JSON-RPC call fails (-1 means never)"),
	OPT_WITH_ARG("--retry-pause|-R",
		     set_int_0_to_9999, opt_show_intval, &opt_fail_pause,
		     "Number of seconds to pause, between retries"),
	OPT_WITH_ARG("--rotate",
		     set_rotate, opt_show_intval, &opt_rotate_period,
		     "Change multipool strategy from failover to regularly rotate at N minutes"),
	OPT_WITHOUT_ARG("--round-robin",
		     set_rr, &pool_strategy,
		     "Change multipool strategy from failover to round robin on failure"),
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
		     set_url, opt_show_charp, &trpc_url,
		     "URL for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--user|-u",
		     set_user, NULL, &trpc_user,
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
		     set_userpass, NULL, &trpc_userpass,
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

#ifdef HAVE_OPENCL
static char *print_ndevs_and_exit(int *ndevs)
{
	printf("%i GPU devices detected\n", *ndevs);
	exit(*ndevs);
}
#endif

/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
	OPT_WITH_ARG("--config|-c",
		     load_config, NULL, NULL,
		     "Load a JSON-format configuration file\n"
		     "See example-cfg.json for an example configuration."),
	OPT_WITHOUT_ARG("--help|-h",
			opt_usage_and_exit,
#ifdef HAVE_OPENCL
			"\nBuilt with CPU and GPU mining support.\n",
#else
			"\nBuilt with CPU mining support only.\n",
#endif
			"Print this message"),
#ifdef HAVE_OPENCL
	OPT_WITHOUT_ARG("--ndevs|-n",
			print_ndevs_and_exit, &nDevs,
			"Enumerate number of detected GPUs and exit"),
#endif
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

static inline int dev_from_id(int thr_id)
{
	return thr_info[thr_id].cgpu->cpu_gpu;
}

/* Simulate a rolling average by faking an exponential decay over 5 * log */
static inline void decay_time(double *f, double fadd)
{
	*f = (fadd + *f * 0.9) / 1.9;
}

static WINDOW *mainwin, *statuswin, *logwin;
static double total_secs = 0.1;
static char statusline[256];
static int cpucursor, gpucursor, logstart, logcursor;
static struct cgpu_info *gpus, *cpus;

static void text_print_status(int thr_id)
{
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;

	printf(" %sPU %d: [%.1f / %.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]\n",
	       cgpu->is_gpu ? "G" : "C", cgpu->cpu_gpu, cgpu->rolling,
			cgpu->total_mhashes / total_secs, cgpu->getworks,
			cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->efficiency, cgpu->utility);
}

/* Must be called with curses mutex lock held and curses_active */
static void curses_print_status(int thr_id)
{
	struct pool *pool = current_pool();

	wmove(statuswin, 0, 0);
	wattron(statuswin, A_BOLD);
	wprintw(statuswin, " " PROGRAM_NAME " version " VERSION " - Started: %s", datestamp);
	wattroff(statuswin, A_BOLD);
	wmove(statuswin, 1, 0);
	whline(statuswin, '-', 80);
	wmove(statuswin, 2,0);
	wprintw(statuswin, " %s", statusline);
	wclrtoeol(statuswin);
	wmove(statuswin, 3,0);
	wprintw(statuswin, " TQ: %d  ST: %d  LS: %d  SS: %d  DW: %d  NB: %d  LW: %d  LO: %d  RF: %d  I: %d",
		total_queued, total_staged, lp_staged, total_stale, total_discarded, new_blocks,
		local_work, total_lo, total_ro, scan_intensity);
	wclrtoeol(statuswin);
	wmove(statuswin, 4, 0);
	if (pool_strategy == POOL_LOADBALANCE && total_pools > 1)
		wprintw(statuswin, " Connected to multiple pools");
	else
		wprintw(statuswin, " Connected to %s as user %s", pool->rpc_url, pool->rpc_user);
	wclrtoeol(statuswin);
	wmove(statuswin, 5, 0);
	wprintw(statuswin, " Block %s  started: %s", current_block + 4, blockdate);
	wmove(statuswin, 6, 0);
	whline(statuswin, '-', 80);
	wmove(statuswin, logstart - 1, 0);
	whline(statuswin, '-', 80);
	mvwprintw(statuswin, gpucursor - 1, 1, "[P]ool management %s[S]ettings [D]isplay options [Q]uit",
		opt_g_threads ? "[G]PU management " : "");

	if (thr_id >= 0 && thr_id < gpu_threads) {
		int gpu = dev_from_id(thr_id);
		struct cgpu_info *cgpu = &gpus[gpu];

		wmove(statuswin, gpucursor + gpu, 0);
		if (!gpu_devices[gpu] || !cgpu->alive)
			wattron(logwin, A_DIM);
		wprintw(statuswin, " GPU %d: [%.1f / %.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]",
			gpu, cgpu->rolling, cgpu->total_mhashes / total_secs,
			cgpu->getworks, cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->efficiency, cgpu->utility);
		wattroff(logwin, A_DIM);
		wclrtoeol(statuswin);
	} else if (thr_id >= gpu_threads) {
		int cpu = dev_from_id(thr_id);
		struct cgpu_info *cgpu = &cpus[cpu];

		wmove(statuswin, cpucursor + cpu, 0);
		wprintw(statuswin, " CPU %d: [%.1f / %.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]",
			cpu, cgpu->rolling, cgpu->total_mhashes / total_secs,
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
		mutex_lock(&curses_lock);
		curses_print_status(thr_id);
		mutex_unlock(&curses_lock);
	}
}

/* Check for window resize. Called with curses mutex locked */
static inline void check_logwinsize(void)
{
	int x, y, logx, logy;

	getmaxyx(mainwin, y, x);
	getmaxyx(logwin, logy, logx);
	y -= logcursor;
	/* Detect screen size change */
	if ((x != logx || y != logy) && x >= 80 && y >= 25)
		wresize(logwin, y, x);
}

/* For mandatory printing when mutex is already locked */
static void wlog(const char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	vw_printw(logwin, f, ap);
	va_end(ap);
}

/* Mandatory printing */
static void wlogprint(const char *f, ...)
{
	va_list ap;

	mutex_lock(&curses_lock);

	va_start(ap, f);
	vw_printw(logwin, f, ap);
	va_end(ap);
	wrefresh(logwin);

	mutex_unlock(&curses_lock);
}

void log_curses(const char *f, va_list ap)
{
	if (curses_active) {
		if (!opt_loginput) {
			mutex_lock(&curses_lock);
			vw_printw(logwin, f, ap);
			wrefresh(logwin);
			mutex_unlock(&curses_lock);
		}
	} else
		vprintf(f, ap);
}

static void clear_logwin(void)
{
	mutex_lock(&curses_lock);
	wclear(logwin);
	wrefresh(logwin);
	mutex_unlock(&curses_lock);
}

static bool submit_upstream_work(const struct work *work)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345];
	bool rc = false;
	int thr_id = work->thr_id;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	CURL *curl = curl_easy_init();
	struct pool *pool = work->pool;

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
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, s, false, false, pool);
	if (unlikely(!val)) {
		applog(LOG_INFO, "submit_upstream_work json_rpc_call failed");
		if (!pool_tset(pool, &pool->submit_fail)) {
			total_ro++;
			pool->remotefail_occasions++;
			applog(LOG_WARNING, "Pool %d communication failure, caching submissions", pool->pool_no);
		}
		goto out;
	} else if (pool_tclear(pool, &pool->submit_fail))
		applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);

	res = json_object_get(val, "result");

	/* Theoretically threads could race when modifying accepted and
	 * rejected values but the chance of two submits completing at the
	 * same time is zero so there is no point adding extra locking */
	if (json_is_true(res)) {
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		if (opt_debug)
			applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
		if (!opt_quiet) {
			if (total_pools > 1)
				applog(LOG_WARNING, "Accepted %.8s %sPU %d thread %d pool %d",
				       hexstr + 152, cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, thr_id, work->pool->pool_no);
			else
				applog(LOG_WARNING, "Accepted %.8s %sPU %d thread %d",
				       hexstr + 152, cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, thr_id);
		}
	} else {
		cgpu->rejected++;
		total_rejected++;
		pool->rejected++;
		if (opt_debug)
			applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!opt_quiet) {
			if (total_pools > 1)
				applog(LOG_WARNING, "Rejected %.8s %sPU %d thread %d pool %d",
				       hexstr + 152, cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, thr_id, work->pool->pool_no);
			else
				applog(LOG_WARNING, "Rejected %.8s %sPU %d thread %d",
				       hexstr + 152, cgpu->is_gpu? "G" : "C", cgpu->cpu_gpu, thr_id);
		}
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

/* Select any active pool in a rotating fashion when loadbalance is chosen */
static inline struct pool *select_pool(bool lagging)
{
	static int rotating_pool = 0;
	struct pool *pool, *cp;

	cp = current_pool();

	if (pool_strategy != POOL_LOADBALANCE && !lagging)
		pool = cp;
	else
		pool = NULL;

	while (!pool) {
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
		pool = pools[rotating_pool];
		if ((!pool->idle && pool->enabled) || pool == cp)
			break;
		pool = NULL;
	}

	return pool;
}

static bool get_upstream_work(struct work *work, bool lagging)
{
	struct pool *pool;
	json_t *val;
	bool rc = false;
	CURL *curl;

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return rc;
	}

	pool = select_pool(lagging);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, rpc_req,
			    want_longpoll, false, pool);
	if (unlikely(!val)) {
		applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");
		goto out;
	}

	rc = work_decode(json_object_get(val, "result"), work);
	work->pool = pool;
	total_getworks++;
	pool->getwork_requested++;

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
	if (test_and_clear(&curses_active)) {
		leaveok(logwin, false);
		leaveok(statuswin, false);
		leaveok(mainwin, false);
		nocbreak();
		echo();
		delwin(logwin);
		delwin(statuswin);
		delwin(mainwin);
		endwin();
		refresh();		
		
#ifdef WIN32
		// Move the cursor to after curses output.
		HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		COORD coord;
		
		if (GetConsoleScreenBufferInfo(hout, &csbi)) {
			coord.X = 0;
			coord.Y = csbi.dwSize.Y - 1;
			SetConsoleCursorPosition(hout, coord);
		}
#endif
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
	pthread_cancel(*thr->pth);

	/* Stop the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		thr = &thr_info[i];
		if (!thr->pth)
			continue;
		tq_freeze(thr->q);
		/* No need to check if this succeeds or not */
		pthread_cancel(*thr->pth);
	}

	/* Stop the others */
	thr = &thr_info[stage_thr_id];
	pthread_cancel(*thr->pth);
	thr = &thr_info[longpoll_thr_id];
	pthread_cancel(*thr->pth);

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
	/* Restore signal handlers so we can still quit if kill_work fails */
	sigaction(SIGTERM, &termhandler, NULL);
	sigaction(SIGINT, &inthandler, NULL);
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
	while (!get_upstream_work(ret_work, wc->lagging)) {
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

static bool stale_work(struct work *work)
{
	struct timeval now;
	bool ret = false;
	char *hexstr;

	/* Only use the primary pool for determination as the work may
	 * interleave at times of new blocks */
	if (work->pool != current_pool())
		return ret;

	gettimeofday(&now, NULL);
	if ((now.tv_sec - work->tv_staged.tv_sec) > opt_scantime)
		return ret;

	hexstr = bin2hex(work->data, 36);
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "submit_work_thread OOM");
		return ret;
	}

	if (strncmp(hexstr, current_block, 36))
		ret = true;

	free(hexstr);
	return ret;
}

static void *submit_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	struct work *work = wc->u.work;
	struct pool *pool = work->pool;
	int failures = 0;

	pthread_detach(pthread_self());

	if (stale_work(work)) {
		applog(LOG_WARNING, "Stale share detected, discarding");
		total_stale++;
		pool->stale_shares++;
		goto out;
	}

	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(work)) {
		if (stale_work(work)) {
			applog(LOG_WARNING, "Stale share detected, discarding");
			total_stale++;
			pool->stale_shares++;
			break;
		}
		if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
			applog(LOG_ERR, "Failed %d retries ...terminating workio thread", opt_retries);
			kill_work();
			break;
		}

		/* pause, then restart work-request loop */
		applog(LOG_INFO, "json_rpc_call failed on submit_work, retry after %d seconds",
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

static void inc_staged(struct pool *pool, int inc, bool lp)
{
	mutex_lock(&stgd_lock);
	if (lp) {
		lp_staged += inc;
		total_staged += inc;
	} else if (lp_staged)
		--lp_staged;
	else
		total_staged += inc;
	mutex_unlock(&stgd_lock);
}

static void dec_staged(int inc)
{
	mutex_lock(&stgd_lock);
	total_staged -= inc;
	mutex_unlock(&stgd_lock);
}

static int requests_staged(void)
{
	int ret;

	mutex_lock(&stgd_lock);
	ret = total_staged;
	mutex_unlock(&stgd_lock);
	return ret;
}

static int real_staged(void)
{
	int ret;

	mutex_lock(&stgd_lock);
	ret = total_staged - lp_staged;
	mutex_unlock(&stgd_lock);
	return ret;
}

/* Find the pool that currently has the highest priority */
static struct pool *priority_pool(int choice)
{
	struct pool *ret = NULL;
	int i;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->prio == choice) {
			ret = pool;
			break;
		}
	}

	if (unlikely(!ret)) {
		applog(LOG_ERR, "WTF No pool %d found!", choice);
		return pools[choice];
	}
	return ret;
}

static void restart_longpoll(void);

static void switch_pools(struct pool *selected)
{
	struct pool *pool, *last_pool;
	int i, pool_no;

	mutex_lock(&control_lock);
	last_pool = currentpool;
	pool_no = currentpool->pool_no;

	/* Switch selected to pool number 0 and move the rest down */
	if (selected) {
		if (selected->prio != 0) {
			for (i = 0; i < total_pools; i++) {
				pool = pools[i];
				if (pool->prio < selected->prio)
					pool->prio++;
			}
			selected->prio = 0;
		}
	}

	switch (pool_strategy) {
		/* Both of these set to the master pool */
		case POOL_FAILOVER:
		case POOL_LOADBALANCE:
			for (i = 0; i < total_pools; i++) {
				pool = priority_pool(i);
				if (!pool->idle && pool->enabled) {
					pool_no = pool->pool_no;
					break;
				}
			}
			break;
		/* Both of these simply increment and cycle */
		case POOL_ROUNDROBIN:
		case POOL_ROTATE:
			if (selected) {
				pool_no = selected->pool_no;
				break;
			}
			pool_no++;
			if (pool_no >= total_pools)
				pool_no = 0;
			break;
		default:
			break;
	}

	currentpool = pools[pool_no];
	pool = currentpool;
	mutex_unlock(&control_lock);

	if (pool != last_pool) {
		applog(LOG_WARNING, "Switching to %s", pool->rpc_url);
		restart_longpoll();
	}

	/* Reset the queued amount to allow more to be queued for the new pool */
	mutex_lock(&qd_lock);
	total_queued = 0;
	mutex_unlock(&qd_lock);

	inc_staged(pool, 1, true);
}

static void set_curblock(char *hexstr)
{
	struct timeval tv_now;

	memcpy(current_block, hexstr, 36);
	gettimeofday(&tv_now, NULL);
	get_datestamp(blockdate, &tv_now);
}

static void test_work_current(struct work *work)
{
	char *hexstr;

	/* Only use the primary pool for determination */
	if (work->pool != current_pool() || work->cloned || work->rolls || work->clone)
		return;

	hexstr = bin2hex(work->data, 36);
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "stage_thread OOM");
		return;
	}

	/* current_block is blanked out on successful longpoll */
	if (unlikely(strncmp(hexstr, current_block, 36))) {
		if (block_changed != BLOCK_LP && block_changed != BLOCK_FIRST) {
			block_changed = BLOCK_DETECT;
			new_blocks++;
			if (have_longpoll)
				applog(LOG_WARNING, "New block detected on network before longpoll, waiting on fresh work");
			else
				applog(LOG_WARNING, "New block detected on network, waiting on fresh work");
			/* As we can't flush the work from here, signal the
			 * wakeup thread to restart all the threads */
			work_restart[watchdog_thr_id].restart = 1;
		} else
			block_changed = BLOCK_NONE;
		set_curblock(hexstr);
	}

	free(hexstr);
}

static void *stage_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	bool ok = true;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (ok) {
		struct work *work = NULL;

		work = tq_pop(mythr->q, NULL);
		if (unlikely(!work)) {
			applog(LOG_ERR, "Failed to tq_pop in stage_thread");
			ok = false;
			break;
		}

		test_work_current(work);
		if (!work->cloned && !work->clone)
			gettimeofday(&work->tv_staged, NULL);

		if (unlikely(!tq_push(getq, work))) {
			applog(LOG_ERR, "Failed to tq_push work in stage_thread");
			ok = false;
			break;
		}
		inc_staged(work->pool, 1, false);
	}

	tq_freeze(mythr->q);
	return NULL;
}

static char *curses_input(const char *query);

static int curses_int(const char *query)
{
	int ret;
	char *cvar;

	cvar = curses_input(query);
	ret = atoi(cvar);
	free(cvar);
	return ret;
}

static bool input_pool(bool live);

static int active_pools(void)
{
	int ret = 0;
	int i;

	for (i = 0; i < total_pools; i++) {
		if ((pools[i])->enabled)
			ret++;
	}
	return ret;
}

static void display_pool_summary(struct pool *pool)
{
	double efficiency = 0.0;

	mutex_lock(&curses_lock);
	wlog("Pool: %s\n", pool->rpc_url);
	wlog(" Queued work requests: %d\n", pool->getwork_requested);
	wlog(" Share submissions: %d\n", pool->accepted + pool->rejected);
	wlog(" Accepted shares: %d\n", pool->accepted);
	wlog(" Rejected shares: %d\n", pool->rejected);
	if (pool->accepted || pool->rejected)
		wlog(" Reject ratio: %.1f\n", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));
	efficiency = pool->getwork_requested ? pool->accepted * 100.0 / pool->getwork_requested : 0.0;
	wlog(" Efficiency (accepted / queued): %.0f%%\n", efficiency);

	wlog(" Discarded work due to new blocks: %d\n", pool->discarded_work);
	wlog(" Stale submissions discarded due to new blocks: %d\n", pool->stale_shares);
	wlog(" Unable to get work from server occasions: %d\n", pool->localgen_occasions);
	wlog(" Submitting work remotely delay occasions: %d\n\n", pool->remotefail_occasions);
	wrefresh(logwin);
	mutex_unlock(&curses_lock);
}

/* We can't remove the memory used for this struct pool because there may
 * still be work referencing it. We just remove it from the pools list */
static void remove_pool(struct pool *pool)
{
	int i, last_pool = total_pools - 1;
	struct pool *other;

	/* Boost priority of any lower prio than this one */
	for (i = 0; i < total_pools; i++) {
		other = pools[i];
		if (other->prio > pool->prio)
			other->prio--;
	}

	if (pool->pool_no < last_pool) {
		/* Swap the last pool for this one */
		(pools[last_pool])->pool_no = pool->pool_no;
		pools[pool->pool_no] = pools[last_pool];
	}
	/* Give it an invalid number */
	pool->pool_no = total_pools;
	total_pools--;
}

static void display_pools(void)
{
	struct pool *pool;
	int selected, i;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
updated:
	clear_logwin();
	for (i = 0; i < total_pools; i++) {
		pool = pools[i];

		if (pool == current_pool())
			wattron(logwin, A_BOLD);
		if (!pool->enabled)
			wattron(logwin, A_DIM);
		wlogprint("%d: %s %s Priority %d: %s  User:%s\n",
			pool->pool_no,
			pool->enabled? "Enabled" : "Disabled",
			pool->idle? "Dead" : "Alive",
			pool->prio,
			pool->rpc_url, pool->rpc_user);
		wattroff(logwin, A_BOLD | A_DIM);
	}
retry:
	wlogprint("\nCurrent pool management strategy: %s\n",
		strategies[pool_strategy]);
	if (pool_strategy == POOL_ROTATE)
		wlogprint("Set to rotate every %d minutes\n", opt_rotate_period);
	wlogprint("[A]dd pool [R]emove pool [D]isable pool [E]nable pool\n");
	wlogprint("[C]hange management strategy [S]witch pool [I]nformation\n");
	wlogprint("Or press any other key to continue\n");
	input = getch();

	if (!strncasecmp(&input, "a", 1)) {
		input_pool(true);
		goto updated;
	} else if (!strncasecmp(&input, "r", 1)) {
		if (total_pools <= 1) {
			wlogprint("Cannot remove last pool");
			goto retry;
		}
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		if (pool == current_pool())
			switch_pools(NULL);
		if (pool == current_pool()) {
			wlogprint("Unable to remove pool due to activity\n");
			goto retry;
		}
		pool->enabled = false;
		remove_pool(pool);
		goto updated;
	} else if (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		pool->enabled = true;
		switch_pools(pool);
		goto updated;
	} else if (!strncasecmp(&input, "d", 1)) {
		if (active_pools() <= 1) {
			wlogprint("Cannot disable last pool");
			goto retry;
		}
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		pool->enabled = false;
		if (pool == current_pool())
			switch_pools(NULL);
		goto updated;
	} else if (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		pool->enabled = true;
		if (pool->prio < current_pool()->prio)
			switch_pools(pool);
		goto updated;
	} else if (!strncasecmp(&input, "c", 1)) {
		for (i = 0; i <= TOP_STRATEGY; i++)
			wlogprint("%d: %s\n", i, strategies[i]);
		selected = curses_int("Select strategy number type");
		if (selected < 0 || selected > TOP_STRATEGY) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		if (selected == POOL_ROTATE) {
			opt_rotate_period = curses_int("Select interval in minutes");

			if (opt_rotate_period < 0 || opt_rotate_period > 9999) {
				opt_rotate_period = 0;
				wlogprint("Invalid selection\n");
				goto retry;
			}
		}
		pool_strategy = selected;
		switch_pools(NULL);
		goto updated;
	} else if (!strncasecmp(&input, "i", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		display_pool_summary(pool);
		goto retry;
	}

	clear_logwin();
	immedok(logwin, false);
	opt_loginput = false;
}

static void display_options(void)
{
	int selected;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
retry:
	clear_logwin();
	wlogprint("\nToggle: [D]ebug [N]ormal [S]ilent [V]erbose [R]PC debug\n");
	wlogprint("[L]og interval [C]lear\n");
	wlogprint("Select an option or any other key to return\n");
	input = getch();
	if (!strncasecmp(&input, "s", 1)) {
		opt_quiet ^= true;
		applog(LOG_WARNING, "Silent mode %s", opt_quiet ? "enabled" : "disabled");
	} else if (!strncasecmp(&input, "v", 1)) {
		opt_log_output ^= true;
		applog(LOG_WARNING, "Verbose mode %s", opt_log_output ? "enabled" : "disabled");
	} else if (!strncasecmp(&input, "n", 1)) {
		opt_log_output = false;
		opt_debug = false;
		opt_quiet = false;
		opt_protocol = false;
		applog(LOG_WARNING, "Output mode reset to normal");
	} else if (!strncasecmp(&input, "d", 1)) {
		opt_debug ^= true;
		if (opt_debug)
			opt_log_output = true;
		applog(LOG_WARNING, "Debug mode %s", opt_debug ? "enabled" : "disabled");
	} else if (!strncasecmp(&input, "r", 1)) {
		opt_protocol ^= true;
		applog(LOG_WARNING, "RPC protocol debugging %s", opt_protocol ? "enabled" : "disabled");
	} else if (!strncasecmp(&input, "c", 1))
		clear_logwin();
	else if (!strncasecmp(&input, "l", 1)) {
		selected = curses_int("Interval in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_log_interval = selected;
	}

	clear_logwin();
	immedok(logwin, false);
	opt_loginput = false;
}

static void set_options(void)
{
	int selected;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
retry:
	clear_logwin();
	wlogprint("\n[D]ynamic mode: %s\n[L]ongpoll: %s\n",
		opt_dynamic ? "On" : "Off", want_longpoll ? "On" : "Off");
	if (opt_dynamic)
		wlogprint("[I]ntensity: Dynamic\n");
	else
		wlogprint("[I]ntensity: %d\n", scan_intensity);
	wlogprint("[Q]ueue: %d\n[S]cantime: %d\n[R]etries: %d\n[P]ause: %d\n",
		opt_queue, opt_scantime, opt_retries, opt_fail_pause);
	wlogprint("Select an option or any other key to return\n");
	input = getch();

	if (!strncasecmp(&input, "q", 1)) {
		selected = curses_int("Extra work items to queue");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_queue = selected;
		goto retry;
	} else if (!strncasecmp(&input, "d", 1)) {
		opt_dynamic ^= true;
		goto retry;
	} else if (!strncasecmp(&input, "l", 1)) {
		want_longpoll ^= true;
		applog(LOG_WARNING, "Longpoll %s", want_longpoll ? "enabled" : "disabled");
		restart_longpoll();
		goto retry;
	} else if (!strncasecmp(&input, "i", 1)) {
		selected = curses_int("Set GPU scan intensity (-10 -> 10)");
		if (selected < -10 || selected > 10) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_dynamic = false;
		scan_intensity = selected;
		goto retry;
	} else if  (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Set scantime in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_scantime = selected;
		goto retry;
	} else if  (!strncasecmp(&input, "r", 1)) {
		selected = curses_int("Retries before failing (-1 infinite)");
		if (selected < -1 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_retries = selected;
		goto retry;
	} else if  (!strncasecmp(&input, "p", 1)) {
		selected = curses_int("Seconds to pause before network retries");
		if (selected < 1 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_fail_pause = selected;
		goto retry;
	}

	clear_logwin();
	immedok(logwin, false);
	opt_loginput = false;
}

#ifdef HAVE_OPENCL
static void reinit_device(struct cgpu_info *cgpu);

static void manage_gpu(void)
{
	struct thr_info *thr;
	int selected, gpu, i;
	char checkin[40];
	char input;

	if (!opt_g_threads)
		return;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
retry:

	for (gpu = 0; gpu < nDevs; gpu++) {
		struct cgpu_info *cgpu = &gpus[gpu];

		wlog("GPU %d: [%.1f / %.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]\n",
			gpu, cgpu->rolling, cgpu->total_mhashes / total_secs,
			cgpu->getworks, cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->efficiency, cgpu->utility);
		for (i = 0; i < mining_threads; i++) {
			thr = &thr_info[i];
			if (thr->cgpu != cgpu)
				continue;
			get_datestamp(checkin, &thr->last);
			wlog("Thread %d: %.1f Mh/s %s %s reported in %s\n", i,
			     thr->rolling, gpu_devices[gpu] ? "Enabled" : "Disabled",
			     cgpu->alive ? "Alive" : "Dead", checkin);
		}
	}

	wlogprint("[E]nable [D]isable [R]estart GPU\n");
	wlogprint("Or press any other key to continue\n");
	input = getch();

	if (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Select GPU to enable");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		if (!gpus[selected].alive) {
			wlogprint("Device dead, need to attempt to restart before enabling\n");
			goto retry;
		}
		if (gpu_devices[selected]) {
			wlogprint("Device already enabled\n");
			goto retry;
		}
		gpu_devices[selected] = true;
		for (i = 0; i < gpu_threads; i++) {
			if (dev_from_id(i) != selected)
				continue;
			thr = &thr_info[i];
			tq_push(thr->q, &ping);
		}
	} if (!strncasecmp(&input, "d", 1)) {
		selected = curses_int("Select GPU to disable");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		if (!gpu_devices[selected]) {
			wlogprint("Device already disabled\n");
			goto retry;
		}
		gpu_devices[selected] = false;
	} else if (!strncasecmp(&input, "r", 1)) {
		selected = curses_int("Select GPU to attempt to restart");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		wlogprint("Attempting to restart threads of GPU %d\n", selected);
		reinit_device(&gpus[selected]);
	}

	clear_logwin();
	immedok(logwin, false);
	opt_loginput = false;
}
#else
static void manage_gpu(void)
{
}
#endif

static void *input_thread(void *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if (!curses_active)
		return NULL;

	while (1) {
		char input;

		input = getch();
		if (!strncasecmp(&input, "q", 1)) {
			kill_work();
			return NULL;
		} else if (!strncasecmp(&input, "d", 1))
			display_options();
		else if (!strncasecmp(&input, "p", 1))
			display_pools();
		else if (!strncasecmp(&input, "s", 1))
			set_options();
		else if (!strncasecmp(&input, "g", 1))
			manage_gpu();
	}

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

static void thread_reportin(struct thr_info *thr)
{
	gettimeofday(&thr->last, NULL);
	thr->cgpu->alive = true;
	thr->getwork = false;
}

static inline void thread_reportout(struct thr_info *thr)
{
	thr->getwork = true;
}

static void hashmeter(int thr_id, struct timeval *diff,
		      unsigned long hashes_done)
{
	struct timeval temp_tv_end, total_diff;
	double secs;
	double local_secs;
	double utility, efficiency = 0.0;
	static double local_mhashes_done = 0;
	static double rolling = 0;
	double local_mhashes = (double)hashes_done / 1000000.0;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	bool showlog = false;

	/* Update the last time this thread reported in */
	if (thr_id >= 0)
		gettimeofday(&thr_info[thr_id].last, NULL);

	/* Don't bother calculating anything if we're not displaying it */
	if (opt_quiet || !opt_log_interval)
		return;
	
	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

	/* So we can call hashmeter from a non worker thread */
	if (thr_id >= 0) {
		struct thr_info *thr = &thr_info[thr_id];
		double thread_rolling = 0.0;
		int i;

		if (opt_debug)
			applog(LOG_DEBUG, "[thread %d: %lu hashes, %.0f khash/sec]",
				thr_id, hashes_done, hashes_done / secs);

		/* Rolling average for each thread and each device */
		decay_time(&thr->rolling, local_mhashes / secs);
		for (i = 0; i < mining_threads; i++) {
			struct thr_info *th = &thr_info[i];

			if (th->cgpu == cgpu)
				thread_rolling += th->rolling;
		}
		decay_time(&cgpu->rolling, thread_rolling);
		cgpu->total_mhashes += local_mhashes;
	}

	/* Totals are updated by all threads so can race without locking */
	mutex_lock(&hash_lock);
	gettimeofday(&temp_tv_end, NULL);
	timeval_subtract(&total_diff, &temp_tv_end, &total_tv_end);

	total_mhashes_done += local_mhashes;
	local_mhashes_done += local_mhashes;
	if (total_diff.tv_sec < opt_log_interval)
		/* Only update the total every opt_log_interval seconds */
		goto out_unlock;
	showlog = true;
	gettimeofday(&total_tv_end, NULL);

	local_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);
	decay_time(&rolling, local_mhashes_done / local_secs);

	timeval_subtract(&total_diff, &total_tv_end, &total_tv_start);
	total_secs = (double)total_diff.tv_sec +
		((double)total_diff.tv_usec / 1000000.0);

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	efficiency = total_getworks ? total_accepted * 100.0 / total_getworks : 0.0;

	sprintf(statusline, "[(%ds):%.1f  (avg):%.1f Mh/s] [Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.2f/m]",
		opt_log_interval, rolling, total_mhashes_done / total_secs,
		total_getworks, total_accepted, total_rejected, hw_errors, efficiency, utility);
	local_mhashes_done = 0;
out_unlock:
	mutex_unlock(&hash_lock);
	if (showlog) {
		if (!curses_active) {
			printf("%s          \r", statusline);
			fflush(stdout);
		} else
			applog(LOG_INFO, "%s", statusline);
	}
}

/* This is overkill, but at least we'll know accurately how much work is
 * queued to prevent ever being left without work */
static void inc_queued(void)
{
	mutex_lock(&qd_lock);
	total_queued++;
	mutex_unlock(&qd_lock);
}

static void dec_queued(void)
{
	mutex_lock(&qd_lock);
	if (total_queued > 0)
		total_queued--;
	mutex_unlock(&qd_lock);
	dec_staged(1);
}

static int requests_queued(void)
{
	int ret;

	mutex_lock(&qd_lock);
	ret = total_queued;
	mutex_unlock(&qd_lock);
	return ret;
}

static bool pool_active(struct pool *pool)
{
	bool ret = false;
	json_t *val;
	CURL *curl;

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return false;
	}

	applog(LOG_INFO, "Testing pool %s", pool->rpc_url);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, rpc_req,
			true, false, pool);

	if (val) {
		struct work *work = malloc(sizeof(struct work));
		bool rc;

		if (!work) {
			applog(LOG_ERR, "Unable to malloc work in pool_active");
			goto out;
		}
		rc = work_decode(json_object_get(val, "result"), work);
		if (rc) {
			applog(LOG_DEBUG, "Successfully retrieved and deciphered work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			work->pool = pool;
			tq_push(thr_info[stage_thr_id].q, work);
			total_getworks++;
			pool->getwork_requested++;
			inc_queued();
			ret = true;
			gettimeofday(&pool->tv_idle, NULL);
		} else {
			applog(LOG_DEBUG, "Successfully retreived but FAILED to decipher work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			free(work);
		}
		json_decref(val);
	} else {
		applog(LOG_DEBUG, "FAILED to retrieve work from pool %u %s",
		       pool->pool_no, pool->rpc_url);
		applog(LOG_WARNING, "Pool down, URL or credentials invalid");
	}
out:
	curl_easy_cleanup(curl);
	return ret;
}

static void pool_died(struct pool *pool)
{
	applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
	gettimeofday(&pool->tv_idle, NULL);
	switch_pools(NULL);
}

static void pool_resus(struct pool *pool)
{
	applog(LOG_WARNING, "Pool %d %s recovered", pool->pool_no, pool->rpc_url);
	if (pool->prio < current_pool()->prio && pool_strategy == POOL_FAILOVER)
		switch_pools(NULL);
}

static bool queue_request(void)
{
	int maxq = opt_queue + mining_threads;
	struct workio_cmd *wc;
	int rq, rs;

	rq = requests_queued();
	rs = real_staged();

	/* If we've been generating lots of local work we may already have
	 * enough in the queue */
	if (rq >= maxq || rs >= maxq)
		return true;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to tq_pop in queue_request");
		return false;
	}

	wc->cmd = WC_GET_WORK;
	/* The get work does not belong to any thread */
	wc->thr = NULL;

	/* If we've queued more than 2/3 of the maximum and still have no
	 * staged work, consider the system lagging and allow work to be
	 * gathered from another pool if possible */
	if (rq > (maxq * 2 / 3) && !rs)
		wc->lagging = true;

	/* send work request to workio thread */
	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		applog(LOG_ERR, "Failed to tq_push in queue_request");
		workio_cmd_free(wc);
		return false;
	}
	inc_queued();
	return true;
}

static void discard_staged(void)
{
	struct timespec abstime = {};
	struct timeval now;
	struct work *work_heap;
	struct pool *pool;

	/* Just in case we fell in a hole and missed a queue filling */
	if (unlikely(!requests_staged()))
		return;

	gettimeofday(&now, NULL);
	abstime.tv_sec = now.tv_sec + 60;

	work_heap = tq_pop(getq, &abstime);
	if (unlikely(!work_heap))
		return;

	pool = work_heap->pool;
	free(work_heap);
	dec_queued();
	pool->discarded_work++;
	total_discarded++;
}

static void flush_requests(void)
{
	struct pool *pool = current_pool();
	int i, stale;

	/* We should have one fresh work item staged from the block change. */
	stale = requests_staged() - 1;

	/* Temporarily increase the staged count so that get_work thinks there
	 * is work available instead of making threads reuse existing work */
	inc_staged(pool, mining_threads, true);

	for (i = 0; i < stale; i++) {
		/* Queue a whole batch of new requests */
		if (unlikely(!queue_request())) {
			applog(LOG_ERR, "Failed to queue requests in flush_requests");
			kill_work();
			break;
		}
		/* Pop off the old requests. Cancelling the requests would be better
		* but is tricky */
		discard_staged();
	}
}

static inline bool can_roll(struct work *work)
{
	return (work->pool && !stale_work(work) && work->pool->has_rolltime &&
		work->rolls < 11 && !work->clone);
}

static void roll_work(struct work *work)
{
	uint32_t *work_ntime;
	uint32_t ntime;

	work_ntime = (uint32_t *)(work->data + 68);
	ntime = be32toh(*work_ntime);
	ntime++;
	*work_ntime = htobe32(ntime);
	local_work++;
	work->rolls++;
	work->blk.nonce = 0;
	gettimeofday(&work->tv_staged, NULL);
	if (opt_debug)
		applog(LOG_DEBUG, "Successfully rolled work");
}

/* Recycle the work at a higher starting res_nonce if we know the thread we're
 * giving it to will not finish scanning it. We keep the master copy to be
 * recycled more rapidly and discard the clone to avoid repeating work */
static bool divide_work(struct timeval *now, struct work *work, uint32_t hash_div)
{
	uint64_t hash_inc;

	if (hash_div < 3 || work->clone)
		return false;

	hash_inc = MAXTHREADS / hash_div * 2;
	if ((uint64_t)work->blk.nonce + hash_inc < MAXTHREADS) {
		/* Don't keep handing it out if it's getting old, but try to
		 * roll it instead */
		if ((now->tv_sec - work->tv_staged.tv_sec) > opt_scantime * 2 / 3) {
			if (!can_roll(work))
				return false;
			else {
				local_work++;
				roll_work(work);
				return true;
			}
		}
		/* Okay we can divide it up */
		work->blk.nonce += hash_inc;
		work->cloned = true;
		if (opt_debug)
			applog(LOG_DEBUG, "Successfully divided work");
		return true;
	}
	return false;
}

static bool get_work(struct work *work, bool queued, struct thr_info *thr,
		     const int thr_id, uint32_t hash_div)
{
	struct timespec abstime = {};
	struct timeval now;
	struct work *work_heap;
	struct pool *pool;
	bool ret = false;
	int failures = 0;

	/* Tell the watchdog thread this thread is waiting on getwork and
	 * should not be restarted */
	thread_reportout(thr);
retry:
	pool = current_pool();
	if (unlikely(!queued && !queue_request())) {
		applog(LOG_WARNING, "Failed to queue_request in get_work");
		goto out;
	}

	if (!requests_staged() && can_roll(work)) {
		/* Only print this message once each time we shift to localgen */
		if (!pool_tset(pool, &pool->idle)) {
			applog(LOG_WARNING, "Pool %d not providing work fast enough, generating work locally",
				pool->pool_no);
			pool->localgen_occasions++;
			total_lo++;
			gettimeofday(&pool->tv_idle, NULL);
		} else {
			struct timeval tv_now, diff;

			gettimeofday(&tv_now, NULL);
			timeval_subtract(&diff, &tv_now, &pool->tv_idle);
			/* Attempt to switch pools if this one has been unresponsive for >half
				* a block's duration */
			if (diff.tv_sec > 300) {
				pool_died(pool);
				goto retry;
			}
		}

		roll_work(work);
		ret = true;
		goto out;
	}

	gettimeofday(&now, NULL);
	abstime.tv_sec = now.tv_sec + 60;

	/* wait for 1st response, or get cached response */
	work_heap = tq_pop(getq, &abstime);
	if (unlikely(!work_heap)) {
		/* Attempt to switch pools if this one has mandatory work that
		 * has timed out or does not support rolltime */
		pool->localgen_occasions++;
		total_lo++;
		pool_died(pool);
		goto retry;
	}

	pool = work_heap->pool;
	/* If we make it here we have succeeded in getting fresh work */
	if (pool_tclear(pool, &pool->idle))
		pool_resus(pool);
	dec_queued();

	memcpy(work, work_heap, sizeof(*work));

	/* Copy the res nonce back so we know to start at a higher baseline
	 * should we divide the same work up again. Make the work we're
	 * handing out be clone */
	if (divide_work(&now, work_heap, hash_div)) {
		tq_push(thr_info[stage_thr_id].q, work_heap);
		work->clone = true;
	} else
		free(work_heap);

	ret = true;
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

	work->thr_id = thr_id;
	thread_reportin(thr);
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
	/* Do one last check before attempting to submit the work */
	if (!fulltest(work->data + 64, work->target))
		return true;
	return submit_work_sync(thr, work);
}

static void *miner_thread(void *userdata)
{
	struct work work __attribute__((aligned(128)));
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	uint32_t max_nonce = 0xffffff, total_hashes = 0;
	unsigned long hashes_done = max_nonce;
	bool needs_work = true;
	/* Try to cycle approximately 5 times before each log update */
	const unsigned long cycle = opt_log_interval / 5 ? : 1;
	int request_interval;
	bool requested = true;
	uint32_t hash_div = 1;
	double hash_divfloat = 1.0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* Request the next work item just before the end of the scantime. We
	 * don't want the work lying around too long since the CPU will always
	 * spend the full scantime */
	request_interval = opt_scantime - 5;
	if (request_interval < 1)
		request_interval = 1;

	/* Set worker threads to nice 19 and then preferentially to SCHED_IDLE
	 * and if that fails, then SCHED_BATCH. No need for this to be an
	 * error if it fails */
	setpriority(PRIO_PROCESS, 0, 19);
	drop_policy();

	/* Cpu affinity only makes sense if the number of threads is a multiple
	 * of the number of CPUs */
	if (!(opt_n_threads % num_processors))
		affine_to_cpu(thr_id - gpu_threads, dev_from_id(thr_id));

	/* Invalidate pool so it fails can_roll() test */
	work.pool = NULL;

	while (1) {
		struct timeval tv_workstart, tv_start, tv_end, diff;
		uint64_t max64;
		bool rc;

		if (needs_work) {
			gettimeofday(&tv_workstart, NULL);
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(&work, requested, mythr, thr_id, hash_div))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"mining thread %d", thr_id);
				goto out;
			}
			mythr->cgpu->getworks++;
			needs_work = requested = false;
			total_hashes = 0;
			max_nonce = work.blk.nonce + hashes_done;
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

#ifdef WANT_X8664_SSE4
		case ALGO_SSE4_64: {
			unsigned int rc5 =
			        scanhash_sse4_64(thr_id, work.midstate, work.data + 64,
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
		total_hashes += hashes_done;
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
				applog(LOG_DEBUG, "CPU %d found something?", dev_from_id(thr_id));
			if (unlikely(!submit_work_sync(mythr, &work))) {
				applog(LOG_ERR, "Failed to submit_work_sync in miner_thread %d", thr_id);
				break;
			}
			work.blk.nonce += 4;
		}

		timeval_subtract(&diff, &tv_end, &tv_workstart);
		if (!requested && (diff.tv_sec >= request_interval)) {
			if (unlikely(!queue_request())) {
				applog(LOG_ERR, "Failed to queue_request in miner_thread %d", thr_id);
				goto out;
			}
			requested = true;
		}

		if (diff.tv_sec > opt_scantime) {
			decay_time(&hash_divfloat , (double)((MAXTHREADS / total_hashes) ? : 1));
			hash_div = hash_divfloat;
			needs_work = true;
		} else if (work_restart[thr_id].restart || stale_work(&work) ||
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

static cl_int queue_poclbm_kernel(_clState *clState, dev_blk_ctx *blk)
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

	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW0);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW1);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW2);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW3);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW15);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fW01r);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fcty_e);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->fcty_e2);

	status |= clSetKernelArg(*kernel, num++, sizeof(clState->outputBuffer),
				 (void *)&clState->outputBuffer);

	return status;
}

static cl_int queue_phatk_kernel(_clState *clState, dev_blk_ctx *blk)
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
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->C1addK5);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->D1A);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_f);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_g);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->cty_h);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->nonce);

	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W2A);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W16);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W17);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->W17_2);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->PreVal4addT1);
	status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->T1substate0);

	status |= clSetKernelArg(*kernel, num++, sizeof(clState->outputBuffer),
				 (void *)&clState->outputBuffer);

	return status;
}

static void set_threads_hashes(unsigned int vectors, unsigned int *threads,
			       unsigned int *hashes, size_t *globalThreads,
			       unsigned int minthreads)
{
	*threads = 1 << (15 + scan_intensity);
	if (*threads < minthreads)
		*threads = minthreads;
	*globalThreads = *threads;
	*hashes = *threads * vectors;
}

static void *gpuminer_thread(void *userdata)
{
	cl_int (*queue_kernel_parameters)(_clState *, dev_blk_ctx *);

	const unsigned long cycle = opt_log_interval / 5 ? : 1;
	struct timeval tv_start, tv_end, diff, tv_workstart;
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	uint32_t *res, *blank_res;
	double gpu_ms_average = 7;
	int gpu = dev_from_id(thr_id);

	size_t globalThreads[1];
	size_t localThreads[1];

	cl_int status;

	_clState *clState = clStates[thr_id];
	const cl_kernel *kernel = &clState->kernel;

	struct work *work = malloc(sizeof(struct work));
	unsigned int threads;
	unsigned const int vectors = clState->preferred_vwidth;
	unsigned int hashes;
	unsigned int hashes_done = 0;

	/* Request the next work item at 2/3 of the scantime */
	unsigned const int request_interval = opt_scantime * 2 / 3 ? : 1;
	unsigned const long request_nonce = MAXTHREADS / 3 * 2;
	bool requested = true;
	uint32_t total_hashes = 0, hash_div = 1;

	switch (chosen_kernel) {
		case KL_POCLBM:
			queue_kernel_parameters = &queue_poclbm_kernel;
			break;
		case KL_PHATK:
		default:
			queue_kernel_parameters = &queue_phatk_kernel;
			break;
	}

	if (opt_dynamic) {
		/* Minimise impact on desktop if we want dynamic mode */
		setpriority(PRIO_PROCESS, 0, 19);
		drop_policy();
	}

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	res = calloc(BUFFERSIZE, 1);
	blank_res = calloc(BUFFERSIZE, 1);

	if (!res || !blank_res) {
		applog(LOG_ERR, "Failed to calloc in gpuminer_thread");
		goto out;
	}

	gettimeofday(&tv_start, NULL);
	localThreads[0] = clState->work_size;
	set_threads_hashes(vectors, &threads, &hashes, &globalThreads[0],
			   localThreads[0]);

	diff.tv_sec = 0;
	gettimeofday(&tv_end, NULL);

	work->pool = NULL;

	status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0,
			BUFFERSIZE, blank_res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS))
		{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }

	mythr->cgpu->alive = true;
	tq_pop(mythr->q, NULL); /* Wait for a ping to start */
	gettimeofday(&tv_workstart, NULL);
	/* obtain new work from internal workio thread */
	if (unlikely(!get_work(work, requested, mythr, thr_id, hash_div))) {
		applog(LOG_ERR, "work retrieval failed, exiting "
			"gpu mining thread %d", thr_id);
		goto out;
	}
	mythr->cgpu->getworks++;
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
		decay_time(&gpu_ms_average, gpu_us / 1000);
		if (opt_dynamic) {
			/* Try to not let the GPU be out for longer than 6ms, but
			 * increase intensity when the system is idle, unless
			 * dynamic is disabled. */
			if (gpu_ms_average > 7) {
				if (scan_intensity > -10)
					scan_intensity--;
			} else if (gpu_ms_average < 3) {
				if (scan_intensity < 10)
					scan_intensity++;
			}
		}
		set_threads_hashes(vectors, &threads, &hashes, globalThreads, localThreads[0]);

		if (diff.tv_sec > opt_scantime ||
		    work->blk.nonce >= MAXTHREADS - hashes ||
		    work_restart[thr_id].restart ||
		    stale_work(work)) {
			/* Ignore any reads since we're getting new work and queue a clean buffer */
			status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
					BUFFERSIZE, blank_res, 0, NULL, NULL);
			if (unlikely(status != CL_SUCCESS))
				{ applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed."); goto out; }
			memset(res, 0, BUFFERSIZE);

			gettimeofday(&tv_workstart, NULL);
			/* obtain new work from internal workio thread */
			if (unlikely(!get_work(work, requested, mythr, thr_id, hash_div))) {
				applog(LOG_ERR, "work retrieval failed, exiting "
					"gpu mining thread %d", thr_id);
				goto out;
			}
			mythr->cgpu->getworks++;
			requested = false;

			precalc_hash(&work->blk, (uint32_t *)(work->midstate), (uint32_t *)(work->data + 64));
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
				applog(LOG_DEBUG, "GPU %d found something?", gpu);
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
		total_hashes += hashes;
		work->blk.nonce += hashes;
		if (diff.tv_sec >= cycle) {
			hashmeter(thr_id, &diff, hashes_done);
			gettimeofday(&tv_start, NULL);
			hashes_done = 0;
		}

		timeval_subtract(&diff, &tv_end, &tv_workstart);
		if (!requested) {
#if 0
			if (diff.tv_sec > request_interval)
				hash_div = (MAXTHREADS / total_hashes) ? : 1;
#endif
			if (diff.tv_sec > request_interval || work->blk.nonce > request_nonce) {
				if (unlikely(!queue_request())) {
					applog(LOG_ERR, "Failed to queue_request in gpuminer_thread %d", thr_id);
					goto out;
				}
				requested = true;
			}
		}
		if (unlikely(!gpu_devices[gpu])) {
			applog(LOG_WARNING, "Thread %d being disabled\n", thr_id);
			mythr->rolling = mythr->cgpu->rolling = 0;
			tq_pop(mythr->q, NULL); /* Ignore ping that's popped */
			applog(LOG_WARNING, "Thread %d being re-enabled\n", thr_id);
		}
	}
out:
	tq_freeze(mythr->q);

	return NULL;
}
#endif /* HAVE_OPENCL */

static void restart_threads(void)
{
	int i;

	if (block_changed == BLOCK_DETECT)
		block_changed = BLOCK_NONE;

	/* Discard old queued requests and get new ones */
	flush_requests();

	for (i = 0; i < mining_threads; i++)
		work_restart[i].restart = 1;
}

/* Stage another work item from the work returned in a longpoll */
static void convert_to_work(json_t *val)
{
	struct work *work;
	bool rc;

	work = calloc(sizeof(*work), 1);
	if (unlikely(!work)) {
		applog(LOG_ERR, "OOM in convert_to_work");
		return;
	}

	rc= work_decode(json_object_get(val, "result"), work);
	if (unlikely(!rc)) {
		applog(LOG_ERR, "Could not convert longpoll data to work");
		return;
	}
	work->pool = current_pool();

	if (unlikely(!tq_push(thr_info[stage_thr_id].q, work)))
		applog(LOG_ERR, "Could not tq_push work in convert_to_work");
	else if (opt_debug)
		applog(LOG_DEBUG, "Converted longpoll data to work");
}

static void *longpoll_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	CURL *curl = NULL;
	char *copy_start, *hdr_path, *lp_url = NULL;
	bool need_slash = false;
	int failures = 0;
	struct pool *pool = current_pool();

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		goto out;
	}

	hdr_path = tq_pop(mythr->q, NULL);
	if (!hdr_path) {
		applog(LOG_WARNING, "No long-poll found on this server");
		goto out;
	}

	/* full URL */
	if (strstr(hdr_path, "://")) {
		lp_url = hdr_path;
		hdr_path = NULL;
	}
	
	/* absolute path, on current server */
	else {
		copy_start = (*hdr_path == '/') ? (hdr_path + 1) : hdr_path;
		if (pool->rpc_url[strlen(pool->rpc_url) - 1] != '/')
			need_slash = true;

		lp_url = malloc(strlen(pool->rpc_url) + strlen(copy_start) + 2);
		if (!lp_url)
			goto out;

		sprintf(lp_url, "%s%s%s", pool->rpc_url, need_slash ? "/" : "", copy_start);
	}
	free(hdr_path);

	applog(LOG_WARNING, "Long-polling activated for %s", lp_url);

	while (1) {
		struct timeval start, end;
		json_t *val;

		gettimeofday(&start, NULL);
		val = json_rpc_call(curl, lp_url, pool->rpc_userpass, rpc_req,
				    false, true, pool);
		if (likely(val)) {
			/* Keep track of who ordered a restart_threads to make
			 * sure it's only done once per new block */
			if (block_changed != BLOCK_DETECT) {
				block_changed = BLOCK_LP;
				new_blocks++;
				applog(LOG_WARNING, "LONGPOLL detected new block on network, waiting on fresh work");
				restart_threads();
			} else {
				applog(LOG_WARNING, "LONGPOLL received after new block already detected");
				block_changed = BLOCK_NONE;
			}

			convert_to_work(val);
			failures = 0;
			json_decref(val);
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
	}

out:
	free(lp_url);
	tq_freeze(mythr->q);
	if (curl)
		curl_easy_cleanup(curl);

	return NULL;
}

static void stop_longpoll(void)
{
	struct thr_info *thr = &thr_info[longpoll_thr_id];

	tq_freeze(thr->q);
	pthread_cancel(*thr->pth);
	have_longpoll = false;
}

static void start_longpoll(void)
{
	struct thr_info *thr = &thr_info[longpoll_thr_id];

	tq_thaw(thr->q);		
	if (unlikely(thr_info_create(thr, NULL, longpoll_thread, thr)))
		quit(1, "longpoll thread create failed");
	pthread_detach(*thr->pth);
}

static void restart_longpoll(void)
{
	stop_longpoll();
	if (want_longpoll)
		start_longpoll();
}

static void *reinit_cpu(void *userdata)
{
#if 0
	struct cgpu_info *cgpu = (struct cgpu_info *)userdata;
	int cpu = cgpu->cpu_gpu;
	long thr_id = ....(long)userdata;
	struct thr_info *thr = &thr_info[thr_id];
	int cpu = dev_from_id(thr_id);

	cpus[cpu].alive = false;
	thr->rolling = thr->cgpu->rolling = 0;
	tq_freeze(thr->q);
	if (!pthread_cancel(*thr->pth))
		pthread_join(*thr->pth, NULL);
	free(thr->q);
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "Failed to tq_new in reinit_cputhread");

	applog(LOG_INFO, "Reinit CPU thread %d", thr_id);

	if (unlikely(thr_info_create(thr, NULL, miner_thread, thr))) {
		applog(LOG_ERR, "thread %d create failed", thr_id);
		return NULL;
	}
	tq_push(thr->q, &ping);

	applog(LOG_WARNING, "Thread %d restarted", thr_id);
#endif
	return NULL;
}

#ifdef HAVE_OPENCL
static void *reinit_gpu(void *userdata)
{
	struct cgpu_info *cgpu = (struct cgpu_info *)userdata;
	int gpu = cgpu->cpu_gpu;
	struct thr_info *thr;
	char name[256];
	int thr_id;

	gpus[gpu].alive = false;

	for (thr_id = 0; thr_id < gpu_threads; thr_id ++) {
		if (dev_from_id(thr_id) != gpu)
			continue;

		thr = &thr_info[thr_id];
		thr->rolling = thr->cgpu->rolling = 0;
		tq_freeze(thr->q);
		if (!pthread_cancel(*thr->pth)) {
			pthread_join(*thr->pth, NULL);
			free(thr->q);
			free(clStates[thr_id]);
		}

		thr->q = tq_new();
		if (!thr->q)
			quit(1, "Failed to tq_new in reinit_gputhread");

		applog(LOG_INFO, "Reinit GPU thread %d", thr_id);
		clStates[thr_id] = initCl(gpu, name, sizeof(name));
		if (!clStates[thr_id]) {
			applog(LOG_ERR, "Failed to reinit GPU thread %d", thr_id);
			return NULL;
		}
		applog(LOG_INFO, "initCl() finished. Found %s", name);

		if (unlikely(thr_info_create(thr, NULL, gpuminer_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", thr_id);
			return NULL;
		}
		/* Try to re-enable it */
		gpu_devices[gpu] = true;
		tq_push(thr->q, &ping);

		applog(LOG_WARNING, "Thread %d restarted", thr_id);
	}

	return NULL;
}
#else
static void *reinit_gpu(void *userdata)
{
}
#endif

static void reinit_device(struct cgpu_info *cgpu)
{
	pthread_t resus_thread;
	void *reinit;

	if (cgpu->is_gpu)
		reinit = reinit_gpu;
	else
		reinit = reinit_cpu;

	if (unlikely(pthread_create(&resus_thread, NULL, reinit, (void *)cgpu)))
		applog(LOG_ERR, "Failed to create reinit thread");
}

/* Determine which are the first threads belonging to a device and if they're
 * active */
static bool active_device(int thr_id)
{
	if (thr_id < gpu_threads) {
		if (thr_id >= total_devices)
			return false;
		if (!gpu_devices[dev_from_id(thr_id)])
			return false;
	} else if (thr_id > gpu_threads + num_processors)
		return false;
	return true;
}

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
static void *watchdog_thread(void *userdata)
{
	const unsigned int interval = opt_log_interval / 2 ? : 1;
	static struct timeval rotate_tv;
	struct timeval zero_tv;
	bool statwin = false;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	memset(&zero_tv, 0, sizeof(struct timeval));
	gettimeofday(&rotate_tv, NULL);

	while (1) {
		int i;
		struct timeval now;

		sleep(interval);
		if (requests_queued() < opt_queue)
			queue_request();

		hashmeter(-1, &zero_tv, 0);

		if (curses_active) {
			statwin ^= true;
			mutex_lock(&curses_lock);
			for (i = 0; i < mining_threads; i++)
				curses_print_status(i);
			if (statwin)
				redrawwin(statuswin);
			else {
				check_logwinsize();
				redrawwin(logwin);
			}
			mutex_unlock(&curses_lock);
		}

		if (unlikely(work_restart[watchdog_thr_id].restart)) {
			restart_threads();
			work_restart[watchdog_thr_id].restart = 0;
		}

		gettimeofday(&now, NULL);

		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			if (!pool->enabled)
				continue;

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 60) {
				gettimeofday(&pool->tv_idle, NULL);
				if (pool_active(pool) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
			}
		}

		if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
			gettimeofday(&rotate_tv, NULL);
			switch_pools(NULL);
		}

		//for (i = 0; i < mining_threads; i++) {
		for (i = 0; i < gpu_threads; i++) {
			struct thr_info *thr = &thr_info[i];

			/* Thread is waiting on getwork or disabled */
			if (thr->getwork || !gpu_devices[i] || !gpus[i].alive)
				continue;
	
			if (now.tv_sec - thr->last.tv_sec > 60) {
				thr->rolling = thr->cgpu->rolling = 0;
				gpus[i].alive = false;
				applog(LOG_ERR, "Attempting to restart thread %d, idle for more than 60 seconds", i);
				/* Create one mandatory work item */
				inc_staged(current_pool(), 1, true);
				if (unlikely(!queue_request())) {
					applog(LOG_ERR, "Failed to queue_request in watchdog_thread");
					kill_work();
					break;
				}
				reinit_device(thr->cgpu);
				/* Only initialise the device once since there
				 * will be multiple threads on the same device
				 * and it will be declared !alive */
				break;
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

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	efficiency = total_getworks ? total_accepted * 100.0 / total_getworks : 0.0;

	printf("\nSummary of runtime statistics:\n\n");
	printf("Started at %s\n", datestamp);
	printf("Runtime: %d hrs : %d mins : %d secs\n", hours, mins, secs);
	if (total_secs)
		printf("Average hashrate: %.1f Megahash/s\n", total_mhashes_done / total_secs);
	printf("Queued work requests: %d\n", total_getworks);
	printf("Share submissions: %d\n", total_accepted + total_rejected);
	printf("Accepted shares: %d\n", total_accepted);
	printf("Rejected shares: %d\n", total_rejected);
	if (total_accepted || total_rejected)
		printf("Reject ratio: %.1f\n", (double)(total_rejected * 100) / (double)(total_accepted + total_rejected));
	printf("Hardware errors: %d\n", hw_errors);
	printf("Efficiency (accepted / queued): %.0f%%\n", efficiency);
	printf("Utility (accepted shares / min): %.2f/min\n\n", utility);

	printf("Discarded work due to new blocks: %d\n", total_discarded);
	printf("Stale submissions discarded due to new blocks: %d\n", total_stale);
	printf("Unable to get work from server occasions: %d\n", total_lo);
	printf("Work items generated locally: %d\n", local_work);
	printf("Submitting work remotely delay occasions: %d\n", total_ro);
	printf("New blocks detected on network: %d\n\n", new_blocks);

	if (total_pools > 1) {
		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			printf("Pool: %s\n", pool->rpc_url);
			printf(" Queued work requests: %d\n", pool->getwork_requested);
			printf(" Share submissions: %d\n", pool->accepted + pool->rejected);
			printf(" Accepted shares: %d\n", pool->accepted);
			printf(" Rejected shares: %d\n", pool->rejected);
			if (pool->accepted || pool->rejected)
				printf(" Reject ratio: %.1f\n", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));
			efficiency = pool->getwork_requested ? pool->accepted * 100.0 / pool->getwork_requested : 0.0;
			printf(" Efficiency (accepted / queued): %.0f%%\n", efficiency);

			printf(" Discarded work due to new blocks: %d\n", pool->discarded_work);
			printf(" Stale submissions discarded due to new blocks: %d\n", pool->stale_shares);
			printf(" Unable to get work from server occasions: %d\n", pool->localgen_occasions);
			printf(" Submitting work remotely delay occasions: %d\n\n", pool->remotefail_occasions);
		}
	}

	printf("Summary of per device statistics:\n\n");
	for (i = 0; i < mining_threads; i++) {
		if (active_device(i))
			print_status(i);
	}
	printf("\n");
}

void quit(int status, const char *format, ...)
{
	va_list ap;

	disable_curses();
	if (format) {
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
	}
	fprintf(stderr, "\n");
	fflush(stderr);

	exit(status);
}

static char *curses_input(const char *query)
{
	char *input;

	echo();
	input = malloc(255);
	if (!input)
		quit(1, "Failed to malloc input");
	leaveok(logwin, false);
	wlogprint("%s: ", query);
	wgetnstr(logwin, input, 255);
	leaveok(logwin, true);
	noecho();
	return input;
}

static bool input_pool(bool live)
{
	char *url = NULL, *user = NULL, *pass = NULL;
	struct pool *pool = NULL;
	bool ret = false;

	immedok(logwin, true);
	if (total_pools == MAX_POOLS) {
		wlogprint("Reached maximum number of pools.\n");
		goto out;
	}
	wlogprint("Input server details.\n");

	url = curses_input("URL");
	if (strncmp(url, "http://", 7) &&
	    strncmp(url, "https://", 8)) {
		applog(LOG_ERR, "URL must start with http:// or https://");
		goto out;
	}

	user = curses_input("Username");
	if (!user)
		goto out;

	pass = curses_input("Password");
	if (!pass)
		goto out;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to realloc pools in input_pool");
	pool->pool_no = total_pools;
	pool->prio = total_pools;
	if (unlikely(pthread_mutex_init(&pool->pool_lock, NULL)))
		quit (1, "Failed to pthread_mutex_init in input_pool");
	pool->rpc_url = url;
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);

	pool->tv_idle.tv_sec = ~0UL;

	/* Test the pool before we enable it if we're live running, otherwise
	 * it will be tested separately */
	ret = true;
	if (live && pool_active(pool))
		pool->enabled = true;
	pools[total_pools++] = pool;
out:
	immedok(logwin, false);

	if (!ret) {
		if (url)
			free(url);
		if (user)
			free(user);
		if (pass)
			free(pass);
		if (pool)
			free(pool);
	}
	return ret;
}

int main (int argc, char *argv[])
{
	unsigned int i, j = 0, x, y, pools_active = 0;
	struct sigaction handler;
	struct thr_info *thr;
	char name[256];

	/* This dangerous functions tramples random dynamically allocated
	 * variables so do it before anything at all */
	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		quit(1, "Failed to curl_global_init");

	if (unlikely(pthread_mutex_init(&hash_lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");
	if (unlikely(pthread_mutex_init(&qd_lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");
	if (unlikely(pthread_mutex_init(&stgd_lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");
	if (unlikely(pthread_mutex_init(&curses_lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");
	if (unlikely(pthread_mutex_init(&control_lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");

	handler.sa_handler = &sighandler;
	sigaction(SIGTERM, &handler, &termhandler);
	sigaction(SIGINT, &handler, &inthandler);

	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);
	get_datestamp(datestamp, &total_tv_start);

	for (i = 0; i < 36; i++)
		strcat(current_block, "0");

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
		quit(1, "clDevicesNum returned error");
#endif
	if (nDevs)
		opt_n_threads = 0;

	trpc_url = strdup(DEF_RPC_URL);

	/* parse command line */
	opt_register_table(opt_config_table,
			   "Options for both config file and command line");
	opt_register_table(opt_cmdline_table,
			   "Options for command line only");

	opt_parse(&argc, argv, applog_and_exit);
	if (argc != 1)
		quit(1, "Unexpected extra commandline arguments");

	if (opt_kernel) {
		if (strcmp(opt_kernel, "poclbm") && strcmp(opt_kernel, "phatk"))
			quit(1, "Invalid kernel name specified - must be poclbm or phatk");
		if (!strcmp(opt_kernel, "poclbm"))
			chosen_kernel = KL_POCLBM;
		else
			chosen_kernel = KL_PHATK;
	} else
		chosen_kernel = KL_NONE;

	gpu_threads = nDevs * opt_g_threads;
	if (total_devices) {
		if (total_devices > nDevs)
			quit(1, "More devices specified than exist");
		for (i = 0; i < 16; i++)
			if (gpu_devices[i] && i + 1 > nDevs)
				quit (1, "Command line options set a device that doesn't exist");
	} else {
		for (i = 0; i < nDevs; i++)
			gpu_devices[i] = true;
		total_devices = nDevs;
	}

	if (!gpu_threads && !forced_n_threads) {
		/* Maybe they turned GPU off; restore default CPU threads. */
		opt_n_threads = num_processors;
	}

	logcursor = 8;
	gpucursor = logcursor;
	cpucursor = gpucursor + nDevs;
	logstart = cpucursor + (opt_n_threads ? num_processors : 0) + 1;
	logcursor = logstart + 1;

	/* Set up the ncurses interface */
	if (!opt_quiet && use_curses) {
		mainwin = initscr();
		getmaxyx(mainwin, y, x);
		statuswin = newwin(logstart, x, 0, 0);
		leaveok(statuswin, true);
		logwin = newwin(y - logcursor, 0, logcursor, 0);
		idlok(logwin, true);
		scrollok(logwin, true);
		leaveok(logwin, true);
		cbreak();
		noecho();
		test_and_set(&curses_active);
	}

	if (!total_pools) {
		if (curses_active) {
			if (!input_pool(false))
				quit(1, "Pool setup failed");
		} else
			quit(1, "No server specified");
	}

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (!pool->rpc_userpass) {
			if (!pool->rpc_user || !pool->rpc_pass)
				quit(1, "No login credentials supplied for pool %u %s", i, pool->rpc_url);
			pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
			if (!pool->rpc_userpass)
				quit(1, "Failed to malloc userpass");
			sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);
		} else {
			pool->rpc_user = malloc(strlen(pool->rpc_userpass));
			if (!pool->rpc_user)
				quit(1, "Failed to malloc user");
			strcpy(pool->rpc_user, pool->rpc_userpass);
			pool->rpc_user = strtok(pool->rpc_user, ":");
			if (!pool->rpc_user)
				quit(1, "Failed to find colon delimiter in userpass");
		}
	}
	/* Set the currentpool to pool 0 */
	currentpool = pools[0];

#ifdef HAVE_SYSLOG_H
	if (use_syslog)
		openlog("cpuminer", LOG_PID, LOG_USER);
#endif

	mining_threads = opt_n_threads + gpu_threads;

	total_threads = mining_threads + 5;
	work_restart = calloc(total_threads, sizeof(*work_restart));
	if (!work_restart)
		quit(1, "Failed to calloc work_restart");

	thr_info = calloc(total_threads, sizeof(*thr));
	if (!thr_info)
		quit(1, "Failed to calloc thr_info");

	/* init workio thread info */
	work_thr_id = mining_threads;
	thr = &thr_info[work_thr_id];
	thr->id = work_thr_id;
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "Failed to tq_new");

	/* start work I/O thread */
	if (thr_info_create(thr, NULL, workio_thread, thr)) 
		quit(1, "workio thread create failed");

	/* init longpoll thread info */
	longpoll_thr_id = mining_threads + 1;
	thr = &thr_info[longpoll_thr_id];
	thr->id = longpoll_thr_id;
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "Failed to tq_new");

	if (want_longpoll)
		start_longpoll();

	if (opt_n_threads ) {
		cpus = calloc(num_processors, sizeof(struct cgpu_info));
		if (unlikely(!cpus))
			quit(1, "Failed to calloc cpus");
	}
	if (gpu_threads) {
		gpus = calloc(nDevs, sizeof(struct cgpu_info));
		if (unlikely(!gpus))
			quit(1, "Failed to calloc gpus");
	}

	stage_thr_id = mining_threads + 3;
	thr = &thr_info[stage_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "Failed to tq_new");
	/* start stage thread */
	if (thr_info_create(thr, NULL, stage_thread, thr))
		quit(1, "stage thread create failed");
	pthread_detach(*thr->pth);

	/* Create a unique get work queue */
	getq = tq_new();
	if (!getq)
		quit(1, "Failed to create getq");

	/* Test each pool to see if we can retrieve and use work and for what
	 * it supports */
	for (i = 0; i < total_pools; i++) {
		struct pool *pool;

		pool = pools[i];
		if (pool_active(pool)) {
			if (!currentpool)
				currentpool = pool;
			applog(LOG_INFO, "Pool %d %s active", pool->pool_no, pool->rpc_url);
			pools_active++;
			pool->enabled = true;
		} else {
			if (pool == currentpool)
				currentpool = NULL;
			applog(LOG_WARNING, "Unable to get work from pool %d %s", pool->pool_no, pool->rpc_url);
		}
	}

	if (!pools_active)
		quit(0, "No pools active! Exiting.");

#ifdef HAVE_OPENCL
	i = 0;

	/* start GPU mining threads */
	for (j = 0; j < nDevs * opt_g_threads; j++) {
		int gpu = j % nDevs;

		gpus[gpu].is_gpu = 1;
		gpus[gpu].cpu_gpu = gpu;

		thr = &thr_info[i];
		thr->id = i;
		thr->cgpu = &gpus[gpu];

		thr->q = tq_new();
		if (!thr->q)
			quit(1, "tq_new failed in starting gpu mining threads");

		/* Enable threads for devices set not to mine but disable
		 * their queue in case we wish to enable them later*/
		if (gpu_devices[gpu])
			tq_push(thr->q, &ping);

		applog(LOG_INFO, "Init GPU thread %i", i);
		clStates[i] = initCl(gpu, name, sizeof(name));
		if (!clStates[i]) {
			applog(LOG_ERR, "Failed to init GPU thread %d", i);
			gpu_devices[i] = false;
			continue;
		}
		applog(LOG_INFO, "initCl() finished. Found %s", name);

		if (unlikely(thr_info_create(thr, NULL, gpuminer_thread, thr)))
			quit(1, "thread %d create failed", i);

		i++;
	}

	applog(LOG_INFO, "%d gpu miner threads started", gpu_threads);
#else
	opt_g_threads = 0;
#endif

	/* start CPU mining threads */
	for (i = gpu_threads; i < mining_threads; i++) {
		int cpu = (i - gpu_threads) % num_processors;

		thr = &thr_info[i];

		thr->id = i;
		cpus[cpu].cpu_gpu = cpu;
		thr->cgpu = &cpus[cpu];

		thr->q = tq_new();
		if (!thr->q)
			quit(1, "tq_new failed in starting cpu mining threads");

		thread_reportin(thr);

		if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
			quit(1, "thread %d create failed", i);
	}

	applog(LOG_INFO, "%d cpu miner threads started, "
		"using SHA256 '%s' algorithm.",
		opt_n_threads,
		algo_names[opt_algo]);

	watchdog_thr_id = mining_threads + 2;
	thr = &thr_info[watchdog_thr_id];
	/* start wakeup thread */
	if (thr_info_create(thr, NULL, watchdog_thread, NULL))
		quit(1, "wakeup thread create failed");

	/* Now that everything's ready put enough work in the queue */
	for (i = 0; i < mining_threads; i++) {
		if (unlikely(!queue_request()))
			quit(1, "Failed to queue_request in main");
		if (!opt_quiet && active_device(i))
			print_status(i);
	}

	/* Create curses input thread for keyboard input */
	input_thr_id = mining_threads + 4;
	thr = &thr_info[input_thr_id];
	if (thr_info_create(thr, NULL, input_thread, thr))
		quit(1, "input thread create failed");
	pthread_detach(*thr->pth);

	/* main loop - simply wait for workio thread to exit */
	pthread_join(*thr_info[work_thr_id].pth, NULL);
	applog(LOG_INFO, "workio thread dead, exiting.");

	gettimeofday(&total_tv_end, NULL);
	disable_curses();
	if (!opt_quiet && successful_connect)
		print_summary();

	if (gpu_threads)
		free(gpus);
	if (opt_n_threads)
		free(cpus);

	curl_global_cleanup();

	return 0;
}

