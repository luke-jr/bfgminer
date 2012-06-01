/*
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2011-2012 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef HAVE_CURSES
#include <curses.h>
#endif

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

#include <sys/stat.h>
#include <sys/types.h>

#ifndef WIN32
#include <sys/resource.h>
#endif
#include <ccan/opt/opt.h>
#include <jansson.h>
#include <curl/curl.h>
#include <libgen.h>
#include <sha2.h>

#include "compat.h"
#include "miner.h"
#include "findnonce.h"
#include "adl.h"
#include "driver-cpu.h"
#include "driver-opencl.h"
#include "bench_block.h"

#if defined(unix)
	#include <errno.h>
	#include <fcntl.h>
	#include <sys/wait.h>
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
	bool			lagging;
};

struct strategies strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
};

static char packagename[255];

int gpu_threads;

bool opt_protocol;
static bool opt_benchmark;
static bool want_longpoll = true;
static bool have_longpoll;
static bool want_per_device_stats;
bool use_syslog;
static bool opt_quiet;
static bool opt_realquiet;
bool opt_loginput;
const int opt_cutofftemp = 95;
static int opt_retries = -1;
static int opt_fail_pause = 5;
static int fail_pause = 5;
int opt_log_interval = 5;
static int opt_queue = 1;
int opt_scantime = 60;
int opt_expiry = 120;
int opt_bench_algo = -1;
static const bool opt_time = true;

#ifdef HAVE_OPENCL
int opt_dynamic_interval = 7;
#endif
bool opt_restart = true;
static bool opt_nogpu;

struct list_head scan_devices;
int nDevs;
int opt_g_threads = 2;
static signed int devices_enabled;
static bool opt_removedisabled;
int total_devices;
struct cgpu_info *devices[MAX_DEVICES];
bool have_opencl;
int gpu_threads;
int opt_n_threads = -1;
int mining_threads;
int num_processors;
#ifdef HAVE_CURSES
bool use_curses = true;
#else
bool use_curses;
#endif
static bool opt_submit_stale = true;
static int opt_shares;
static bool opt_fail_only;
bool opt_autofan;
bool opt_autoengine;
bool opt_noadl;
char *opt_api_allow = NULL;
char *opt_api_description = PACKAGE_STRING;
int opt_api_port = 4028;
bool opt_api_listen;
bool opt_api_network;
bool opt_delaynet;
bool opt_disable_pool = true;
char *opt_icarus_timing = NULL;

char *opt_kernel_path;
char *cgminer_path;

#define QUIET	(opt_quiet || opt_realquiet)

struct thr_info *thr_info;
static int work_thr_id;
static int stage_thr_id;
static int watchpool_thr_id;
static int watchdog_thr_id;
#ifdef HAVE_CURSES
static int input_thr_id;
#endif
int gpur_thr_id;
static int api_thr_id;
static int total_threads;

static pthread_mutex_t hash_lock;
static pthread_mutex_t qd_lock;
static pthread_mutex_t *stgd_lock;
#ifdef HAVE_CURSES
static pthread_mutex_t curses_lock;
#endif
static pthread_mutex_t ch_lock;
static pthread_rwlock_t blk_lock;

pthread_rwlock_t netacc_lock;

static pthread_mutex_t lp_lock;
static pthread_cond_t lp_cond;

double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;

pthread_mutex_t control_lock;

int hw_errors;
int total_accepted, total_rejected;
int total_getworks, total_stale, total_discarded;
static int total_queued;
unsigned int new_blocks;
static unsigned int work_block;
unsigned int found_blocks;

unsigned int local_work;
unsigned int total_go, total_ro;

struct pool *pools[MAX_POOLS];
static struct pool *currentpool = NULL;

int total_pools;
enum pool_strategy pool_strategy = POOL_FAILOVER;
int opt_rotate_period;
static int total_urls, total_users, total_passes, total_userpasses;

#ifndef HAVE_CURSES
const
#endif
static bool curses_active;

static char current_block[37];
static char *current_hash;
static char datestamp[40];
static char blocktime[30];

struct block {
	char hash[37];
	UT_hash_handle hh;
};

static struct block *blocks = NULL;

char *opt_socks_proxy = NULL;

static const char def_conf[] = "bfgminer.conf";
static bool config_loaded;
static int include_count;
#define JSON_INCLUDE_CONF "include"
#define JSON_LOAD_ERROR "JSON decode of file '%s' failed"
#define JSON_LOAD_ERROR_LEN strlen(JSON_LOAD_ERROR)
#define JSON_MAX_DEPTH 10
#define JSON_MAX_DEPTH_ERR "Too many levels of JSON includes (limit 10) or a loop"

#if defined(unix)
	static char *opt_stderr_cmd = NULL;
	static int forkpid;
#endif // defined(unix)

bool ping = true;

struct sigaction termhandler, inthandler;

struct thread_q *getq;

static int total_work;
struct work *staged_work = NULL;
static int staged_extras;

struct schedtime {
	bool enable;
	struct tm tm;
};

struct schedtime schedstart;
struct schedtime schedstop;
bool sched_paused;

static bool time_before(struct tm *tm1, struct tm *tm2)
{
	if (tm1->tm_hour < tm2->tm_hour)
		return true;
	if (tm1->tm_hour == tm2->tm_hour && tm1->tm_min < tm2->tm_min)
		return true;
	return false;
}

static bool should_run(void)
{
	struct timeval tv;
	struct tm *tm;

	if (!schedstart.enable && !schedstop.enable)
		return true;

	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);
	if (schedstart.enable) {
		if (!schedstop.enable) {
			if (time_before(tm, &schedstart.tm))
				return false;

			/* This is a once off event with no stop time set */
			schedstart.enable = false;
			return true;
		}
		if (time_before(&schedstart.tm, &schedstop.tm)) {
			if (time_before(tm, &schedstop.tm) && !time_before(tm, &schedstart.tm))
				return true;
			return false;
		} /* Times are reversed */
		if (time_before(tm, &schedstart.tm)) {
			if (time_before(tm, &schedstop.tm))
				return true;
			return false;
		}
		return true;
	}
	/* only schedstop.enable == true */
	if (!time_before(tm, &schedstop.tm))
		return false;
	return true;
}

void get_datestamp(char *f, struct timeval *tv)
{
	struct tm *tm;

	tm = localtime(&tv->tv_sec);
	sprintf(f, "[%d-%02d-%02d %02d:%02d:%02d]",
		tm->tm_year + 1900,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

void get_timestamp(char *f, struct timeval *tv)
{
	struct tm *tm;

	tm = localtime(&tv->tv_sec);
	sprintf(f, "[%02d:%02d:%02d]",
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static void applog_and_exit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vapplog(LOG_ERR, fmt, ap);
	va_end(ap);
	exit(1);
}

static pthread_mutex_t sharelog_lock;
static FILE *sharelog_file = NULL;

static void sharelog(const char*disposition, const struct work*work)
{
	if (!sharelog_file)
		return;

	int thr_id = work->thr_id;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	struct pool *pool = work->pool;
	unsigned long int t = (unsigned long int)work->share_found_time;
	char *target = bin2hex(work->target, sizeof(work->target));
	if (unlikely(!target)) {
		applog(LOG_ERR, "sharelog target OOM");
		return;
	}
	char *hash = bin2hex(work->hash, sizeof(work->hash));
	if (unlikely(!hash)) {
		free(target);
		applog(LOG_ERR, "sharelog hash OOM");
		return;
	}
	char *data = bin2hex(work->data, sizeof(work->data));
	if (unlikely(!data)) {
		free(target);
		free(hash);
		applog(LOG_ERR, "sharelog data OOM");
		return;
	}

	// timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
	char s[1024];
	int rv;
	rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s%u,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->api->name, cgpu->device_id, thr_id, hash, data);
	free(target);
	free(hash);
	free(data);
	if (rv >= (int)(sizeof(s)))
		s[sizeof(s) - 1] = '\0';
	else
	if (rv < 0) {
		applog(LOG_ERR, "sharelog printf error");
		return;
	}

	size_t ret;
	mutex_lock(&sharelog_lock);
	ret = fwrite(s, rv, 1, sharelog_file);
	fflush(sharelog_file);
	mutex_unlock(&sharelog_lock);
	if (1 != ret)
		applog(LOG_ERR, "sharelog fwrite error");
}

/* Return value is ignored if not called from add_pool_details */
static struct pool *add_pool(void)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to malloc pool in add_pool");
	pool->pool_no = pool->prio = total_pools;
	pools[total_pools++] = pool;
	if (unlikely(pthread_mutex_init(&pool->pool_lock, NULL)))
		quit(1, "Failed to pthread_mutex_init in add_pool");
	if (unlikely(pthread_cond_init(&pool->cr_cond, NULL)))
		quit(1, "Failed to pthread_cond_init in add_pool");
	INIT_LIST_HEAD(&pool->curlring);

	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;

	return pool;
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

bool pool_tclear(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = false;
	mutex_unlock(&pool->pool_lock);
	return ret;
}

struct pool *current_pool(void)
{
	struct pool *pool;

	mutex_lock(&control_lock);
	pool = currentpool;
	mutex_unlock(&control_lock);
	return pool;
}

char *set_int_range(const char *arg, int *i, int min, int max)
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

static char *set_int_1_to_65535(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 65535);
}

static char *set_int_0_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 10);
}

static char *set_int_1_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 10);
}

#if defined(USE_BITFORCE) || defined(USE_ICARUS)
static char *add_serial(char *arg)
{
	string_elist_add(arg, &scan_devices);
	return NULL;
}
#endif

static char *set_devices(char *arg)
{
	int i = strtol(arg, &arg, 0);

	if (*arg) {
		if (*arg == '?') {
			devices_enabled = -1;
			return NULL;
		}
		return "Invalid device number";
	}

	if (i < 0 || i >= (int)(sizeof(devices_enabled) * 8) - 1)
		return "Invalid device number";
	devices_enabled |= 1 << i;
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

static char *set_url(char *arg)
{
	struct pool *pool;

	total_urls++;
	if (total_urls > total_pools)
		add_pool();
	pool = pools[total_urls - 1];

	opt_set_charp(arg, &pool->rpc_url);
	if (strncmp(arg, "http://", 7) &&
	    strncmp(arg, "https://", 8)) {
		char *httpinput;

		httpinput = malloc(255);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		strcpy(httpinput, "http://");
		strncat(httpinput, arg, 248);
		pool->rpc_url = httpinput;
	}

	return NULL;
}

static char *set_user(const char *arg)
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

static char *set_pass(const char *arg)
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

static char *set_userpass(const char *arg)
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

static char *enable_debug(bool *flag)
{
	*flag = true;
	/* Turn out verbose output, too. */
	opt_log_output = true;
	return NULL;
}

static char *set_schedtime(const char *arg, struct schedtime *st)
{
	if (sscanf(arg, "%d:%d", &st->tm.tm_hour, &st->tm.tm_min) != 2)
		return "Invalid time set, should be HH:MM";
	if (st->tm.tm_hour > 23 || st->tm.tm_min > 59 || st->tm.tm_hour < 0 || st->tm.tm_min < 0)
		return "Invalid time set.";
	st->enable = true;
	return NULL;
}

static char*
set_sharelog(char *arg) {
	char *r = "";
	long int i = strtol(arg, &r, 10);

	if ((!*r) && i >= 0 && i <= INT_MAX) {
		sharelog_file = fdopen((int)i, "a");
		if (!sharelog_file)
			applog(LOG_ERR, "Failed to open fd %u for share log", (unsigned int)i);
	} else if (!strcmp(arg, "-")) {
		sharelog_file = stdout;
		if (!sharelog_file)
			applog(LOG_ERR, "Standard output missing for share log");
	} else {
		sharelog_file = fopen(arg, "a");
		if (!sharelog_file)
			applog(LOG_ERR, "Failed to open %s for share log", arg);
	}

	return NULL;
}

static char *temp_cutoff_str = NULL;

char *set_temp_cutoff(char *arg)
{
	int val;

	if (!(arg && arg[0]))
		return "Invalid parameters for set temp cutoff";
	val = atoi(arg);
	if (val < 0 || val > 200)
		return "Invalid value passed to set temp cutoff";
	temp_cutoff_str = arg;

	return NULL;
}

static void load_temp_cutoffs()
{
	int i, val = 0, device = 0;
	char *nextptr;

	if (temp_cutoff_str) {
		for (device = 0, nextptr = strtok(temp_cutoff_str, ","); nextptr; ++device, nextptr = strtok(NULL, ",")) {
			if (device >= total_devices)
				quit(1, "Too many values passed to set temp cutoff");
			val = atoi(nextptr);
			if (val < 0 || val > 200)
				quit(1, "Invalid value passed to set temp cutoff");

			devices[device]->cutofftemp = val;
		}
	}
	else
		val = opt_cutofftemp;
	if (device <= 1) {
		for (i = device; i < total_devices; ++i)
			devices[i]->cutofftemp = val;
	}
}

static char *set_api_allow(const char *arg)
{
	opt_set_charp(arg, &opt_api_allow);

	return NULL;
}

static char *set_api_description(const char *arg)
{
	opt_set_charp(arg, &opt_api_description);

	return NULL;
}

#ifdef USE_ICARUS
static char *set_icarus_timing(const char *arg)
{
	opt_set_charp(arg, &opt_icarus_timing);

	return NULL;
}
#endif

/* These options are available from config file or commandline */
static struct opt_table opt_config_table[] = {
#ifdef WANT_CPUMINE
	OPT_WITH_ARG("--algo|-a",
		     set_algo, show_algo, &opt_algo,
		     "Specify sha256 implementation for CPU mining:\n"
		     "\tauto\t\tBenchmark at startup and pick fastest algorithm"
		     "\n\tc\t\tLinux kernel sha256, implemented in C"
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
#ifdef WANT_X8632_SSE2
		     "\n\tsse2_32\t\tSSE2 32 bit implementation for i386 machines"
#endif
#ifdef WANT_X8664_SSE2
		     "\n\tsse2_64\t\tSSE2 64 bit implementation for x86_64 machines"
#endif
#ifdef WANT_X8664_SSE4
		     "\n\tsse4_64\t\tSSE4.1 64 bit implementation for x86_64 machines"
#endif
#ifdef WANT_ALTIVEC_4WAY
    "\n\taltivec_4way\tAltivec implementation for PowerPC G4 and G5 machines"
#endif
		),
#endif
	OPT_WITH_ARG("--api-allow",
		     set_api_allow, NULL, NULL,
		     "Allow API access only to the given list of IP[/Prefix] addresses[/subnets]"),
	OPT_WITH_ARG("--api-description",
		     set_api_description, NULL, NULL,
		     "Description placed in the API status header, default: BFGMiner version"),
	OPT_WITHOUT_ARG("--api-listen",
			opt_set_bool, &opt_api_listen,
			"Enable API, default: disabled"),
	OPT_WITHOUT_ARG("--api-network",
			opt_set_bool, &opt_api_network,
			"Allow API (if enabled) to listen on/for any address, default: only 127.0.0.1"),
	OPT_WITH_ARG("--api-port",
		     set_int_1_to_65535, opt_show_intval, &opt_api_port,
		     "Port number of miner API"),
#ifdef HAVE_ADL
	OPT_WITHOUT_ARG("--auto-fan",
			opt_set_bool, &opt_autofan,
			"Automatically adjust all GPU fan speeds to maintain a target temperature"),
	OPT_WITHOUT_ARG("--auto-gpu",
			opt_set_bool, &opt_autoengine,
			"Automatically adjust all GPU engine clock speeds to maintain a target temperature"),
#endif
	OPT_WITHOUT_ARG("--benchmark",
			opt_set_bool, &opt_benchmark,
			"Run BFGMiner in benchmark mode - produces no shares"),
#ifdef WANT_CPUMINE
	OPT_WITH_ARG("--bench-algo|-b",
		     set_int_0_to_9999, opt_show_intval, &opt_bench_algo,
		     opt_hidden),
	OPT_WITH_ARG("--cpu-threads|-t",
		     force_nthreads_int, opt_show_intval, &opt_n_threads,
		     "Number of miner CPU threads"),
#endif
	OPT_WITHOUT_ARG("--debug|-D",
		     enable_debug, &opt_debug,
		     "Enable debug output"),
	OPT_WITH_ARG("--device|-d",
		     set_devices, NULL, NULL,
	             "Select device to use, (Use repeat -d for multiple devices, default: all)"),
	OPT_WITHOUT_ARG("--disable-gpu|-G",
			opt_set_bool, &opt_nogpu,
#ifdef HAVE_OPENCL
			"Disable GPU mining even if suitable devices exist"
#else
			opt_hidden
#endif
	),
#if defined(WANT_CPUMINE) && (defined(HAVE_OPENCL) || defined(USE_BITFORCE) || defined(USE_ICARUS))
	OPT_WITHOUT_ARG("--enable-cpu|-C",
			opt_set_bool, &opt_usecpu,
			"Enable CPU mining with other mining (default: no CPU mining if other devices exist)"),
#endif
	OPT_WITH_ARG("--expiry|-E",
		     set_int_0_to_9999, opt_show_intval, &opt_expiry,
		     "Upper bound on how many seconds after getting work we consider a share from it stale"),
	OPT_WITHOUT_ARG("--failover-only",
			opt_set_bool, &opt_fail_only,
			"Don't leak work to backup pools when primary pool is lagging"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--gpu-dyninterval",
		     set_int_1_to_65535, opt_show_intval, &opt_dynamic_interval,
		     "Set the refresh interval in ms for GPUs using dynamic intensity"),
	OPT_WITH_ARG("--gpu-platform",
		     set_int_0_to_9999, opt_show_intval, &opt_platform_id,
		     "Select OpenCL platform ID to use for GPU mining"),
	OPT_WITH_ARG("--gpu-threads|-g",
		     set_int_1_to_10, opt_show_intval, &opt_g_threads,
		     "Number of threads per GPU (1 - 10)"),
#ifdef HAVE_ADL
	OPT_WITH_ARG("--gpu-engine",
		     set_gpu_engine, NULL, NULL,
		     "GPU engine (over)clock range in Mhz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)"),
	OPT_WITH_ARG("--gpu-fan",
		     set_gpu_fan, NULL, NULL,
		     "GPU fan percentage range - one value, range and/or comma separated list (e.g. 0-85,85,65)"),
	OPT_WITH_ARG("--gpu-map",
		     set_gpu_map, NULL, NULL,
		     "Map OpenCL to ADL device order manually, paired CSV (e.g. 1:0,2:1 maps OpenCL 1 to ADL 0, 2 to 1)"),
	OPT_WITH_ARG("--gpu-memclock",
		     set_gpu_memclock, NULL, NULL,
		     "Set the GPU memory (over)clock in Mhz - one value for all or separate by commas for per card"),
	OPT_WITH_ARG("--gpu-memdiff",
		     set_gpu_memdiff, NULL, NULL,
		     "Set a fixed difference in clock speed between the GPU and memory in auto-gpu mode"),
	OPT_WITH_ARG("--gpu-powertune",
		     set_gpu_powertune, NULL, NULL,
		     "Set the GPU powertune percentage - one value for all or separate by commas for per card"),
	OPT_WITHOUT_ARG("--gpu-reorder",
			opt_set_bool, &opt_reorder,
			"Attempt to reorder GPU devices according to PCI Bus ID"),
	OPT_WITH_ARG("--gpu-vddc",
		     set_gpu_vddc, NULL, NULL,
		     "Set the GPU voltage in Volts - one value for all or separate by commas for per card"),
#endif
	OPT_WITH_ARG("--intensity|-I",
		     set_intensity, NULL, NULL,
		     "Intensity of GPU scanning (d or " _MIN_INTENSITY_STR " -> " _MAX_INTENSITY_STR ", default: d to maintain desktop interactivity)"),
	OPT_WITH_ARG("--kernel-path|-K",
		     opt_set_charp, opt_show_charp, &opt_kernel_path,
	             "Specify a path to where the kernel .cl files are"),
	OPT_WITH_ARG("--kernel|-k",
		     set_kernel, NULL, NULL,
		     "Override kernel to use (diablo, poclbm, phatk or diakgcn) - one value or comma separated"),
#endif
#ifdef USE_ICARUS
	OPT_WITH_ARG("--icarus-timing",
		     set_icarus_timing, NULL, NULL,
		     opt_hidden),
#endif
	OPT_WITHOUT_ARG("--load-balance",
		     set_loadbalance, &pool_strategy,
		     "Change multipool strategy from failover to even load balance"),
	OPT_WITH_ARG("--log|-l",
		     set_int_0_to_9999, opt_show_intval, &opt_log_interval,
		     "Interval in seconds between log output"),
#if defined(unix)
	OPT_WITH_ARG("--monitor|-m",
		     opt_set_charp, NULL, &opt_stderr_cmd,
		     "Use custom pipe cmd for output messages"),
#endif // defined(unix)
	OPT_WITHOUT_ARG("--net-delay",
			opt_set_bool, &opt_delaynet,
			"Impose small delays in networking to not overload slow routers"),
	OPT_WITHOUT_ARG("--no-adl",
			opt_set_bool, &opt_noadl,
#ifdef HAVE_ADL
			"Disable the ATI display library used for monitoring and setting GPU parameters"
#else
			opt_hidden
#endif
	),
	OPT_WITHOUT_ARG("--no-longpoll",
			opt_set_invbool, &want_longpoll,
			"Disable X-Long-Polling support"),
	OPT_WITHOUT_ARG("--no-pool-disable",
			opt_set_invbool, &opt_disable_pool,
			"Do not automatically disable pools that continually reject shares"),
	OPT_WITHOUT_ARG("--no-restart",
			opt_set_invbool, &opt_restart,
#ifdef HAVE_OPENCL
			"Do not attempt to restart GPUs that hang"
#else
			opt_hidden
#endif
	),
	OPT_WITHOUT_ARG("--no-submit-stale",
			opt_set_invbool, &opt_submit_stale,
		        "Don't submit shares if they are detected as stale"),
	OPT_WITH_ARG("--pass|-p",
		     set_pass, NULL, NULL,
		     "Password for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--per-device-stats",
			opt_set_bool, &want_per_device_stats,
			"Force verbose mode and output per-device statistics"),
	OPT_WITHOUT_ARG("--protocol-dump|-P",
			opt_set_bool, &opt_protocol,
			"Verbose dump of protocol-level activities"),
	OPT_WITH_ARG("--queue|-Q",
		     set_int_0_to_9999, opt_show_intval, &opt_queue,
		     "Minimum number of work items to have queued (0+)"),
	OPT_WITHOUT_ARG("--quiet|-q",
			opt_set_bool, &opt_quiet,
			"Disable logging output, display status and errors"),
	OPT_WITHOUT_ARG("--real-quiet",
			opt_set_bool, &opt_realquiet,
			"Disable all output"),
	OPT_WITHOUT_ARG("--remove-disabled",
		     opt_set_bool, &opt_removedisabled,
	         "Remove disabled devices entirely, as if they didn't exist"),
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
#if defined(USE_BITFORCE) || defined(USE_ICARUS)
	OPT_WITH_ARG("--scan-serial|-S",
		     add_serial, NULL, NULL,
		     "Serial port to probe for FPGA Mining device"),
#endif
	OPT_WITH_ARG("--scan-time|-s",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     "Upper bound on time spent scanning current work, in seconds"),
	OPT_WITH_ARG("--sched-start",
		     set_schedtime, NULL, &schedstart,
		     "Set a time of day in HH:MM to start mining (a once off without a stop time)"),
	OPT_WITH_ARG("--sched-stop",
		     set_schedtime, NULL, &schedstop,
		     "Set a time of day in HH:MM to stop mining (will quit without a start time)"),
	OPT_WITH_ARG("--sharelog",
		     set_sharelog, NULL, NULL,
		     "Append share log to file"),
	OPT_WITH_ARG("--shares",
		     opt_set_intval, NULL, &opt_shares,
		     "Quit after mining N shares (default: unlimited)"),
	OPT_WITH_ARG("--socks-proxy",
		     opt_set_charp, NULL, &opt_socks_proxy,
		     "Set socks4 proxy (host:port)"),
	OPT_WITHOUT_ARG("--submit-stale",
			opt_set_bool, &opt_submit_stale,
	                opt_hidden),
#ifdef HAVE_SYSLOG_H
	OPT_WITHOUT_ARG("--syslog",
			opt_set_bool, &use_syslog,
			"Use system log for output messages (default: standard error)"),
#endif
#if defined(HAVE_ADL) || defined(USE_BITFORCE)
	OPT_WITH_ARG("--temp-cutoff",
		     set_temp_cutoff, opt_show_intval, &opt_cutofftemp,
		     "Temperature where a device will be automatically disabled, one value or comma separated list"),
#endif
#ifdef HAVE_ADL
	OPT_WITH_ARG("--temp-hysteresis",
		     set_int_1_to_10, opt_show_intval, &opt_hysteresis,
		     "Set how much the temperature can fluctuate outside limits when automanaging speeds"),
	OPT_WITH_ARG("--temp-overheat",
		     set_temp_overheat, opt_show_intval, &opt_overheattemp,
		     "Overheat temperature when automatically managing fan and GPU speeds, one value or comma separated list"),
	OPT_WITH_ARG("--temp-target",
		     set_temp_target, opt_show_intval, &opt_targettemp,
		     "Target temperature when automatically managing fan and GPU speeds, one value or comma separated list"),
#endif
	OPT_WITHOUT_ARG("--text-only|-T",
			opt_set_invbool, &use_curses,
#ifdef HAVE_CURSES
			"Disable ncurses formatted screen output"
#else
			opt_hidden
#endif
	),
	OPT_WITH_ARG("--url|-o",
		     set_url, NULL, NULL,
		     "URL for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--user|-u",
		     set_user, NULL, NULL,
		     "Username for bitcoin JSON-RPC server"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--vectors|-v",
		     set_vector, NULL, NULL,
		     "Override detected optimal vector (1, 2 or 4) - one value or comma separated list"),
#endif
	OPT_WITHOUT_ARG("--verbose",
			opt_set_bool, &opt_log_output,
			"Log verbose output to stderr as well as status output"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--worksize|-w",
		     set_worksize, NULL, NULL,
		     "Override detected optimal worksize - one value or comma separated list"),
#endif
	OPT_WITH_ARG("--userpass|-O",
		     set_userpass, NULL, NULL,
		     "Username:Password pair for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--pools",
			opt_set_bool, NULL, NULL, opt_hidden),
	OPT_ENDTABLE
};

static char *load_config(const char *arg, void __maybe_unused *unused);

static int fileconf_load;

static char *parse_config(json_t *config, bool fileconf)
{
	static char err_buf[200];
	json_t *val;
	struct opt_table *opt;

	if (fileconf && !fileconf_load)
		fileconf_load = 1;

	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name;

		/* We don't handle subtables. */
		assert(!(opt->type & OPT_SUBTABLE));

		/* Pull apart the option name(s). */
		name = strdup(opt->names);
		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			char *err = NULL;
			/* Ignore short options. */
			if (p[1] != '-')
				continue;

			val = json_object_get(config, p+2);
			if (!val)
				continue;

			if ((opt->type & OPT_HASARG) && json_is_string(val)) {
				err = opt->cb_arg(json_string_value(val),
						  opt->u.arg);
			} else if ((opt->type & OPT_HASARG) && json_is_array(val)) {
				int n, size = json_array_size(val);

				for (n = 0; n < size && !err; n++) {
					if (json_is_string(json_array_get(val, n)))
						err = opt->cb_arg(json_string_value(json_array_get(val, n)), opt->u.arg);
					else if (json_is_object(json_array_get(val, n)))
						err = parse_config(json_array_get(val, n), false);
				}
			} else if ((opt->type & OPT_NOARG) && json_is_true(val))
				err = opt->cb(opt->u.arg);
			else
				err = "Invalid value";

			if (err) {
				/* Allow invalid values to be in configuration
				 * file, just skipping over them provided the
				 * JSON is still valid after that. */
				if (fileconf) {
					applog(LOG_ERR, "Invalid config option %s: %s", p, err);
					fileconf_load = -1;
				} else {
					sprintf(err_buf, "Parsing JSON option %s: %s",
						p, err);
					return err_buf;
				}
			}
		}
		free(name);
	}

	val = json_object_get(config, JSON_INCLUDE_CONF);
	if (val && json_is_string(val))
		return load_config(json_string_value(val), NULL);

	return NULL;
}

char *cnfbuf = NULL;

static char *load_config(const char *arg, void __maybe_unused *unused)
{
	json_error_t err;
	json_t *config;
	char *json_error;

	if (!cnfbuf)
		cnfbuf = strdup(arg);

	if (++include_count > JSON_MAX_DEPTH)
		return JSON_MAX_DEPTH_ERR;

#if JANSSON_MAJOR_VERSION > 1
	config = json_load_file(arg, 0, &err);
#else
	config = json_load_file(arg, &err);
#endif
	if (!json_is_object(config)) {
		json_error = malloc(JSON_LOAD_ERROR_LEN + strlen(arg));
		if (!json_error)
			quit(1, "Malloc failure in json error");

		sprintf(json_error, JSON_LOAD_ERROR, arg);
		return json_error;
	}

	config_loaded = true;

	/* Parse the config now, so we can override it.  That can keep pointers
	 * so don't free config object. */
	return parse_config(config, true);
}

static void load_default_config(void)
{
	cnfbuf = malloc(PATH_MAX);

#if defined(unix)
	if (getenv("HOME") && *getenv("HOME")) {
	        strcpy(cnfbuf, getenv("HOME"));
		strcat(cnfbuf, "/");
	}
	else
		strcpy(cnfbuf, "");
	char *dirp = cnfbuf + strlen(cnfbuf);
	strcpy(dirp, ".bfgminer/");
	strcat(dirp, def_conf);
	if (access(cnfbuf, R_OK))
	{
		// No BFGMiner config, try Cgminer's...
		strcpy(dirp, ".cgminer/");
		strcat(dirp, def_conf);
	}
#else
	strcpy(cnfbuf, "");
#endif
	strcat(cnfbuf, def_conf);
	if (!access(cnfbuf, R_OK))
		load_config(cnfbuf, NULL);
	else {
		free(cnfbuf);
		cnfbuf = NULL;
	}
}

extern const char *opt_argv0;

static char *opt_verusage_and_exit(const char *extra)
{
	printf("%s\nBuilt with "
#ifdef HAVE_OPENCL
		"GPU "
#endif
#ifdef WANT_CPUMINE
		"CPU "
#endif
#ifdef USE_BITFORCE
		"bitforce "
#endif
#ifdef USE_ICARUS
		"icarus "
#endif
#ifdef USE_ZTEX
		"ztex "
#endif
		"mining support.\n"
		, packagename);
	printf("%s", opt_usage(opt_argv0, extra));
	fflush(stdout);
	exit(0);
}

/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
	OPT_WITH_ARG("--config|-c",
		     load_config, NULL, NULL,
		     "Load a JSON-format configuration file\n"
		     "See example.conf for an example configuration."),
	OPT_WITHOUT_ARG("--help|-h",
			opt_verusage_and_exit, NULL,
			"Print this message"),
#ifdef HAVE_OPENCL
	OPT_WITHOUT_ARG("--ndevs|-n",
			print_ndevs_and_exit, &nDevs,
			"Display number of detected GPUs, OpenCL platform information, and exit"),
#endif
	OPT_WITHOUT_ARG("--version|-V",
			opt_version_and_exit, packagename,
			"Display version and exit"),
	OPT_ENDTABLE
};

static bool jobj_binary(const json_t *obj, const char *key,
			void *buf, size_t buflen, bool required)
{
	const char *hexstr;
	json_t *tmp;

	tmp = json_object_get(obj, key);
	if (unlikely(!tmp)) {
		if (unlikely(required))
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
	if (unlikely(!jobj_binary(val, "data", work->data, sizeof(work->data), true))) {
		applog(LOG_ERR, "JSON inval data");
		goto err_out;
	}

	if (likely(!jobj_binary(val, "midstate",
			 work->midstate, sizeof(work->midstate), false))) {
		// Calculate it ourselves
		union {
			unsigned char c[64];
			uint32_t i[16];
		} data;
		int swapcounter;
		for (swapcounter = 0; swapcounter < 16; swapcounter++)
			data.i[swapcounter] = swab32(((uint32_t*) (work->data))[swapcounter]);
		sha2_context ctx;
		sha2_starts( &ctx, 0 );
		sha2_update( &ctx, data.c, 64 );
		memcpy(work->midstate, ctx.state, sizeof(work->midstate));
#if defined(__BIG_ENDIAN__) || defined(MIPSEB)
		int i;
		for (i = 0; i < 8; i++)
			(((uint32_t*) (work->midstate))[i]) = swab32(((uint32_t*) (work->midstate))[i]);
#endif
	}

	if (likely(!jobj_binary(val, "hash1", work->hash1, sizeof(work->hash1), false))) {
		// Always the same anyway
		memcpy(work->hash1, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x80\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\0\0", 64);
	}

	if (unlikely(!jobj_binary(val, "target", work->target, sizeof(work->target), true))) {
		applog(LOG_ERR, "JSON inval target");
		goto err_out;
	}

	memset(work->hash, 0, sizeof(work->hash));

	gettimeofday(&work->tv_staged, NULL);

	return true;

err_out:
	return false;
}

int dev_from_id(int thr_id)
{
	return thr_info[thr_id].cgpu->device_id;
}

/* Make the change in the recent value adjust dynamically when the difference
 * is large, but damp it when the values are closer together. This allows the
 * value to change quickly, but not fluctuate too dramatically when it has
 * stabilised. */
void decay_time(double *f, double fadd)
{
	double ratio = 0;

	if (likely(*f > 0)) {
		ratio = fadd / *f;
		if (ratio > 1)
			ratio = 1 / ratio;
	}

	if (ratio > 0.63)
		*f = (fadd * 0.58 + *f) / 1.58;
	else
		*f = (fadd + *f * 0.58) / 1.58;
}

static int requests_staged(void)
{
	int ret;

	mutex_lock(stgd_lock);
	ret = HASH_COUNT(staged_work);
	mutex_unlock(stgd_lock);
	return ret;
}

#ifdef HAVE_CURSES
WINDOW *mainwin, *statuswin, *logwin;
#endif
double total_secs = 0.1;
static char statusline[256];
/* logstart is where the log window should start */
static int devcursor, logstart, logcursor;
/* statusy is where the status window goes up to in cases where it won't fit at startup */
static int statusy;
struct cgpu_info gpus[MAX_GPUDEVICES]; /* Maximum number apparently possible */
struct cgpu_info *cpus;

#ifdef HAVE_CURSES
static inline void unlock_curses(void)
{
	mutex_unlock(&curses_lock);
}

static inline void lock_curses(void)
{
	mutex_lock(&curses_lock);
}

static bool curses_active_locked(void)
{
	bool ret;

	lock_curses();
	ret = curses_active;
	if (!ret)
		unlock_curses();
	return ret;
}
#endif

void tailsprintf(char *f, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsprintf(f + strlen(f), fmt, ap);
	va_end(ap);
}

static void get_statline(char *buf, struct cgpu_info *cgpu)
{
	sprintf(buf, "%s%d ", cgpu->api->name, cgpu->device_id);
	if (cgpu->api->get_statline_before)
		cgpu->api->get_statline_before(buf, cgpu);
	else
		tailsprintf(buf, "               | ");
	tailsprintf(buf, "(%ds):%.1f (avg):%.1f Mh/s | A:%d R:%d HW:%d U:%.1f/m",
		opt_log_interval,
		cgpu->rolling,
		cgpu->total_mhashes / total_secs,
		cgpu->accepted,
		cgpu->rejected,
		cgpu->hw_errors,
		cgpu->utility);
	if (cgpu->api->get_statline)
		cgpu->api->get_statline(buf, cgpu);
}

static void text_print_status(int thr_id)
{
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	char logline[255];

	if (cgpu) {
		get_statline(logline, cgpu);
		printf("%s\n", logline);
	}
}

#ifdef HAVE_CURSES
/* Must be called with curses mutex lock held and curses_active */
static void curses_print_status(void)
{
	struct pool *pool = current_pool();

	wattron(statuswin, A_BOLD);
	mvwprintw(statuswin, 0, 0, " " PACKAGE " version " VERSION " - Started: %s", datestamp);
#ifdef WANT_CPUMINE
	if (opt_n_threads)
		wprintw(statuswin, " CPU Algo: %s", algo_names[opt_algo]);
#endif
	wattroff(statuswin, A_BOLD);
	mvwhline(statuswin, 1, 0, '-', 80);
	mvwprintw(statuswin, 2, 0, " %s", statusline);
	wclrtoeol(statuswin);
	mvwprintw(statuswin, 3, 0, " TQ: %d  ST: %d  SS: %d  DW: %d  NB: %d  LW: %d  GF: %d  RF: %d",
		total_queued, requests_staged(), total_stale, total_discarded, new_blocks,
		local_work, total_go, total_ro);
	wclrtoeol(statuswin);
	if (pool_strategy == POOL_LOADBALANCE && total_pools > 1)
		mvwprintw(statuswin, 4, 0, " Connected to multiple pools with%s LP",
			have_longpoll ? "": "out");
	else
		mvwprintw(statuswin, 4, 0, " Connected to %s with%s LP as user %s",
			pool->rpc_url, have_longpoll ? "": "out", pool->rpc_user);
	wclrtoeol(statuswin);
	mvwprintw(statuswin, 5, 0, " Block: %s...  Started: %s", current_hash, blocktime);
	mvwhline(statuswin, 6, 0, '-', 80);
	mvwhline(statuswin, statusy - 1, 0, '-', 80);
	mvwprintw(statuswin, devcursor - 1, 1, "[P]ool management %s[S]ettings [D]isplay options [Q]uit",
		have_opencl ? "[G]PU management " : "");
}

static void adj_width(int var, int *length)
{
	if ((int)(log10(var) + 1) > *length)
		(*length)++;
}

static void curses_print_devstatus(int thr_id)
{
	static int awidth = 1, rwidth = 1, hwwidth = 1, uwidth = 1;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	char logline[255];

	cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

	/* Check this isn't out of the window size */
	if (wmove(statuswin,devcursor + cgpu->cgminer_id, 0) == ERR)
		return;
	wprintw(statuswin, " %s %d: ", cgpu->api->name, cgpu->device_id);
	if (cgpu->api->get_statline_before) {
		logline[0] = '\0';
		cgpu->api->get_statline_before(logline, cgpu);
		wprintw(statuswin, "%s", logline);
	}
	else
		wprintw(statuswin, "               | ");

	if (cgpu->status == LIFE_DEAD)
		wprintw(statuswin, "DEAD ");
	else if (cgpu->status == LIFE_SICK)
		wprintw(statuswin, "SICK ");
	else if (cgpu->deven == DEV_DISABLED)
		wprintw(statuswin, "OFF  ");
	else if (cgpu->deven == DEV_RECOVER)
		wprintw(statuswin, "REST  ");
	else
		wprintw(statuswin, "%5.1f", cgpu->rolling);
	adj_width(cgpu->accepted, &awidth);
	adj_width(cgpu->rejected, &rwidth);
	adj_width(cgpu->hw_errors, &hwwidth);
	adj_width(cgpu->utility, &uwidth);
	wprintw(statuswin, "/%5.1fMh/s | A:%*d R:%*d HW:%*d U:%*.2f/m",
			cgpu->total_mhashes / total_secs,
			awidth, cgpu->accepted,
			rwidth, cgpu->rejected,
			hwwidth, cgpu->hw_errors,
		uwidth + 3, cgpu->utility);

	if (cgpu->api->get_statline) {
		logline[0] = '\0';
		cgpu->api->get_statline(logline, cgpu);
		wprintw(statuswin, "%s", logline);
	}

	wclrtoeol(statuswin);
}
#endif

static void print_status(int thr_id)
{
	if (!curses_active)
		text_print_status(thr_id);
}

#ifdef HAVE_CURSES
/* Check for window resize. Called with curses mutex locked */
static inline bool change_logwinsize(void)
{
	int x, y, logx, logy;
	bool ret = false;

	getmaxyx(mainwin, y, x);
	if (x < 80 || y < 25)
		return ret;

	if (y > statusy + 2 && statusy < logstart) {
		if (y - 2 < logstart)
			statusy = y - 2;
		else
			statusy = logstart;
		logcursor = statusy + 1;
		mvwin(logwin, logcursor, 0);
		wresize(statuswin, statusy, x);
		ret = true;
	}

	y -= logcursor;
	getmaxyx(logwin, logy, logx);
	/* Detect screen size change */
	if (x != logx || y != logy) {
		wresize(logwin, y, x);
		ret = true;
	}
	return ret;
}

static void check_winsizes(void)
{
	if (!use_curses)
		return;
	if (curses_active_locked()) {
		int y, x;

		x = getmaxx(statuswin);
		if (logstart > LINES - 2)
			statusy = LINES - 2;
		else
			statusy = logstart;
		logcursor = statusy + 1;
		wresize(statuswin, statusy, x);
		getmaxyx(mainwin, y, x);
		y -= logcursor;
		wresize(logwin, y, x);
		mvwin(logwin, logcursor, 0);
		unlock_curses();
	}
}

/* For mandatory printing when mutex is already locked */
void wlog(const char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	vw_printw(logwin, f, ap);
	va_end(ap);
}

/* Mandatory printing */
void wlogprint(const char *f, ...)
{
	va_list ap;

	if (curses_active_locked()) {
		va_start(ap, f);
		vw_printw(logwin, f, ap);
		va_end(ap);
		unlock_curses();
	}
}
#endif

#ifdef HAVE_CURSES
void log_curses(int prio, const char *f, va_list ap)
{
	bool high_prio;

	if (opt_quiet && prio != LOG_ERR)
		return;

	high_prio = (prio == LOG_WARNING || prio == LOG_ERR);

	if (curses_active_locked()) {
		if (!opt_loginput || high_prio) {
			vw_printw(logwin, f, ap);
			if (high_prio) {
				touchwin(logwin);
				wrefresh(logwin);
			}
		}
		unlock_curses();
	} else
		vprintf(f, ap);
}

void clear_logwin(void)
{
	if (curses_active_locked()) {
		wclear(logwin);
		unlock_curses();
	}
}
#endif

/* regenerate the full work->hash value and also return true if it's a block */
bool regeneratehash(const struct work *work)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[128];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	uint32_t *hash32 = (uint32_t *)(work->hash);
	uint32_t difficulty = 0;
	uint32_t diffbytes = 0;
	uint32_t diffvalue = 0;
	uint32_t diffcmp[8];
	int diffshift = 0;
	int i;

	for (i = 0; i < 80 / 4; i++)
		swap32[i] = swab32(data32[i]);

	sha2(swap, 80, hash1, false);
	sha2(hash1, 32, (unsigned char *)(work->hash), false);

	difficulty = swab32(*((uint32_t *)(work->data + 72)));

	diffbytes = ((difficulty >> 24) & 0xff) - 3;
	diffvalue = difficulty & 0x00ffffff;

	diffshift = (diffbytes % 4) * 8;
	if (diffshift == 0) {
		diffshift = 32;
		diffbytes--;
	}

	memset(diffcmp, 0, 32);
	diffcmp[(diffbytes >> 2) + 1] = diffvalue >> (32 - diffshift);
	diffcmp[diffbytes >> 2] = diffvalue << diffshift;

	for (i = 7; i >= 0; i--) {
		if (hash32[i] > diffcmp[i])
			return false;
		if (hash32[i] < diffcmp[i])
			return true;
	}

	// https://en.bitcoin.it/wiki/Block says: "numerically below"
	// https://en.bitcoin.it/wiki/Target says: "lower than or equal to"
	// code in bitcoind 0.3.24 main.cpp CheckWork() says: if (hash > hashTarget) return false;
	if (hash32[0] == diffcmp[0])
		return true;
	else
		return false;
}

static bool submit_upstream_work(const struct work *work, CURL *curl)
{
	char *hexstr = NULL;
	json_t *val, *res;
	char s[345], sd[345];
	bool rc = false;
	int thr_id = work->thr_id;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	struct pool *pool = work->pool;
	bool rolltime;
	uint32_t *hash32;
	char hashshow[64+1] = "";
	bool isblock;

#ifdef __BIG_ENDIAN__
        int swapcounter = 0;
        for (swapcounter = 0; swapcounter < 32; swapcounter++)
            (((uint32_t*) (work->data))[swapcounter]) = swab32(((uint32_t*) (work->data))[swapcounter]);
#endif

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
	sprintf(sd,
	      "{\"method\": \"getwork\", \"params\": [ \"%s\" ], \"id\":1}",
		hexstr);

	applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, sd);

	/* issue JSON-RPC request */
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, s, false, false, &rolltime, pool, true);
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

	if (!QUIET) {
#ifndef MIPSEB
// This one segfaults on my router for some reason
		isblock = regeneratehash(work);
		if (unlikely(isblock)) {
			pool->solved++;
			found_blocks++;
		}
		hash32 = (uint32_t *)(work->hash);
		sprintf(hashshow, "%08lx.%08lx%s", (unsigned long)(hash32[6]), (unsigned long)(hash32[5]),
			isblock ? " BLOCK!" : "");
#endif
	}

	/* Theoretically threads could race when modifying accepted and
	 * rejected values but the chance of two submits completing at the
	 * same time is zero so there is no point adding extra locking */
	if (json_is_true(res)) {
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		pool->seq_rejects = 0;
		cgpu->last_share_pool = pool->pool_no;
		cgpu->last_share_pool_time = time(NULL);
		pool->last_share_time = cgpu->last_share_pool_time;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
		if (!QUIET) {
			if (total_pools > 1)
				applog(LOG_NOTICE, "Accepted %s %s %d pool %d",
				       hashshow, cgpu->api->name, cgpu->device_id, work->pool->pool_no);
			else
				applog(LOG_NOTICE, "Accepted %s %s %d",
				       hashshow, cgpu->api->name, cgpu->device_id);
		}
		sharelog("accept", work);
		if (opt_shares && total_accepted >= opt_shares) {
			applog(LOG_WARNING, "Successfully mined %d accepted shares as requested and exiting.", opt_shares);
			kill_work();
			goto out;
		}

		/* Detect if a pool that has been temporarily disabled for
		 * continually rejecting shares has started accepting shares.
		 * This will only happen with the work returned from a
		 * longpoll */
		if (unlikely(pool->enabled == POOL_REJECTING)) {
			applog(LOG_WARNING, "Rejecting pool %d now accepting shares, re-enabling!", pool->pool_no);
			pool->enabled = POOL_ENABLED;
			switch_pools(NULL);
		}
	} else {
		cgpu->rejected++;
		total_rejected++;
		pool->rejected++;
		pool->seq_rejects++;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!QUIET) {
			char where[17];
			char disposition[36] = "reject";
			char reason[32];

			if (total_pools > 1)
				sprintf(where, "pool %d", work->pool->pool_no);
			else
				strcpy(where, "");

			res = json_object_get(val, "reject-reason");
			if (res) {
				const char *reasontmp = json_string_value(res);

				size_t reasonLen = strlen(reasontmp);
				if (reasonLen > 28)
					reasonLen = 28;
				reason[0] = ' '; reason[1] = '(';
				memcpy(2 + reason, reasontmp, reasonLen);
				reason[reasonLen + 2] = ')'; reason[reasonLen + 3] = '\0';
				memcpy(disposition + 7, reasontmp, reasonLen);
				disposition[6] = ':'; disposition[reasonLen + 7] = '\0';
			} else
				strcpy(reason, "");

			applog(LOG_NOTICE, "Rejected %s %s %d %s%s",
			       hashshow, cgpu->api->name, cgpu->device_id, where, reason);
			sharelog(disposition, work);
		}

		/* Once we have more than a nominal amount of sequential rejects,
		 * at least 10 and more than 3 mins at the current utility,
		 * disable the pool because some pool error is likely to have
		 * ensued. Do not do this if we know the share just happened to
		 * be stale due to networking delays.
		 */
		if (pool->seq_rejects > 10 && !work->stale && opt_disable_pool && total_pools > 1) {
			double utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;

			if (pool->seq_rejects > utility * 3) {
				applog(LOG_WARNING, "Pool %d rejected %d sequential shares, disabling!",
				       pool->pool_no, pool->seq_rejects);
				pool->enabled = POOL_REJECTING;
				if (pool == current_pool())
					switch_pools(NULL);
				pool->seq_rejects = 0;
			}
		}
	}

	cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

	if (!opt_realquiet)
		print_status(thr_id);
	if (!want_per_device_stats) {
		char logline[255];

		get_statline(logline, cgpu);
		applog(LOG_INFO, "%s", logline);
	}

	json_decref(val);

	rc = true;
out:
	free(hexstr);
out_nofree:
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
		if ((!pool->idle && pool->enabled == POOL_ENABLED) || pool == cp)
			break;
		pool = NULL;
	}

	return pool;
}

static void get_benchmark_work(struct work *work)
{
	// Use a random work block pulled from a pool
	static uint8_t bench_block[] = { CGMINER_BENCHMARK_BLOCK };

	size_t bench_size = sizeof(work);
	size_t work_size = sizeof(bench_block);
	size_t min_size = (work_size < bench_size ? work_size : bench_size);
	memset(work, 0, sizeof(work));
	memcpy(work, &bench_block, min_size);
}

static bool get_upstream_work(struct work *work, CURL *curl)
{
	struct pool *pool = work->pool;
	json_t *val = NULL;
	bool rc = false;
	int retries = 0;
	char *url;

	applog(LOG_DEBUG, "DBG: sending %s get RPC call: %s", pool->rpc_url, rpc_req);

	url = pool->rpc_url;

retry:
	/* A single failure response here might be reported as a dead pool and
	 * there may be temporary denied messages etc. falsely reporting
	 * failure so retry a few times before giving up */
	while (!val && retries++ < 3) {
		val = json_rpc_call(curl, url, pool->rpc_userpass, rpc_req,
			    false, false, &work->rolltime, pool, false);
	}
	if (unlikely(!val)) {
		applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");
		goto out;
	}

	rc = work_decode(json_object_get(val, "result"), work);
	if (!rc && retries < 3)
		goto retry;
	work->pool = pool;
	work->longpoll = false;
	total_getworks++;
	pool->getwork_requested++;

	json_decref(val);
out:

	return rc;
}

static struct work *make_work(void)
{
	struct work *work = calloc(1, sizeof(struct work));

	if (unlikely(!work))
		quit(1, "Failed to calloc work in make_work");
	work->id = total_work++;
	return work;
}

static void free_work(struct work *work)
{
	free(work);
}

static void workio_cmd_free(struct workio_cmd *wc)
{
	if (!wc)
		return;

	switch (wc->cmd) {
	case WC_SUBMIT_WORK:
		free_work(wc->u.work);
		break;
	default: /* do nothing */
		break;
	}

	memset(wc, 0, sizeof(*wc));	/* poison */
	free(wc);
}

#ifdef HAVE_CURSES
static void disable_curses(void)
{
	if (curses_active_locked()) {
		curses_active = false;
		leaveok(logwin, false);
		leaveok(statuswin, false);
		leaveok(mainwin, false);
		nocbreak();
		echo();
		delwin(logwin);
		delwin(statuswin);
		delwin(mainwin);
		endwin();
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
		unlock_curses();
	}
}
#endif

static void print_summary(void);

static void __kill_work(void)
{
	struct thr_info *thr;
	int i;

	applog(LOG_INFO, "Received kill message");

	applog(LOG_DEBUG, "Killing off watchpool thread");
	/* Kill the watchpool thread */
	thr = &thr_info[watchpool_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Killing off watchdog thread");
	/* Kill the watchdog thread */
	thr = &thr_info[watchdog_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Stopping mining threads");
	/* Stop the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		thr = &thr_info[i];
		thr_info_freeze(thr);
		thr->pause = true;
	}

	sleep(1);

	applog(LOG_DEBUG, "Killing off mining threads");
	/* Kill the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		thr = &thr_info[i];
		thr_info_cancel(thr);
	}

	applog(LOG_DEBUG, "Killing off stage thread");
	/* Stop the others */
	thr = &thr_info[stage_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Killing off API thread");
	thr = &thr_info[api_thr_id];
	thr_info_cancel(thr);
}

/* This should be the common exit path */
void kill_work(void)
{
	__kill_work();

	quit(0, "Shutdown signal received.");
}

static
#ifdef WIN32
const
#endif
char **initial_args;

static void clean_up(void);

void app_restart(void)
{
	applog(LOG_WARNING, "Attempting to restart %s", packagename);

	__kill_work();
	clean_up();

#if defined(unix)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif

	execv(initial_args[0], initial_args);
	applog(LOG_WARNING, "Failed to restart application");
}

static void sighandler(int __maybe_unused sig)
{
	/* Restore signal handlers so we can still quit if kill_work fails */
	sigaction(SIGTERM, &termhandler, NULL);
	sigaction(SIGINT, &inthandler, NULL);
	kill_work();
}

static void start_longpoll(void);
static void stop_longpoll(void);

/* Called with pool_lock held. Recruit an extra curl if none are available for
 * this pool. */
static void recruit_curl(struct pool *pool)
{
	struct curl_ent *ce = calloc(sizeof(struct curl_ent), 1);

	ce->curl = curl_easy_init();
	if (unlikely(!ce->curl || !ce))
		quit(1, "Failed to init in recruit_curl");

	list_add(&ce->node, &pool->curlring);
	pool->curls++;
	applog(LOG_DEBUG, "Recruited curl %d for pool %d", pool->curls, pool->pool_no);
}

/* Grab an available curl if there is one. If not, then recruit extra curls
 * unless we are in a submit_fail situation, or we have opt_delaynet enabled
 * and there are already 5 curls in circulation */
static struct curl_ent *pop_curl_entry(struct pool *pool)
{
	struct curl_ent *ce;

	mutex_lock(&pool->pool_lock);
	if (!pool->curls)
		recruit_curl(pool);
	else if (list_empty(&pool->curlring)) {
		if ((pool->submit_fail || opt_delaynet) && pool->curls > 4)
			pthread_cond_wait(&pool->cr_cond, &pool->pool_lock);
		else
			recruit_curl(pool);
	}
	ce = list_entry(pool->curlring.next, struct curl_ent, node);
	list_del(&ce->node);
	mutex_unlock(&pool->pool_lock);

	return ce;
}

static void push_curl_entry(struct curl_ent *ce, struct pool *pool)
{
	mutex_lock(&pool->pool_lock);
	list_add_tail(&ce->node, &pool->curlring);
	gettimeofday(&ce->tv, NULL);
	pthread_cond_signal(&pool->cr_cond);
	mutex_unlock(&pool->pool_lock);
}

/* ce and pool may appear uninitialised at push_curl_entry, but they're always
 * set when we don't have opt_benchmark enabled */
static void *get_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	struct curl_ent * uninitialised_var(ce);
	struct pool * uninitialised_var(pool);
	struct work *ret_work = make_work();
	int failures = 0;

	pthread_detach(pthread_self());

	applog(LOG_DEBUG, "Creating extra get work thread");

	if (wc->thr)
		ret_work->thr = wc->thr;
	else
		ret_work->thr = NULL;

	if (opt_benchmark)
		get_benchmark_work(ret_work);
	else {
		pool = ret_work->pool = select_pool(wc->lagging);
		
		ce = pop_curl_entry(pool);

		/* obtain new work from bitcoin via JSON-RPC */
		while (!get_upstream_work(ret_work, ce->curl)) {
			if (unlikely((opt_retries >= 0) && (++failures > opt_retries))) {
				applog(LOG_ERR, "json_rpc_call failed, terminating workio thread");
				free_work(ret_work);
				kill_work();
				goto out;
			}

			/* pause, then restart work-request loop */
			applog(LOG_DEBUG, "json_rpc_call failed on get work, retry after %d seconds",
				fail_pause);
			sleep(fail_pause);
			fail_pause += opt_fail_pause;
		}
		fail_pause = opt_fail_pause;
	}

	applog(LOG_DEBUG, "Pushing work to requesting thread");

	/* send work to requesting thread */
	if (unlikely(!tq_push(thr_info[stage_thr_id].q, ret_work))) {
		applog(LOG_ERR, "Failed to tq_push work in workio_get_work");
		kill_work();
		free_work(ret_work);
	}

out:
	workio_cmd_free(wc);
	if (!opt_benchmark)
		push_curl_entry(ce, pool);
	return NULL;
}

/* As per the submit work system, we try to reuse the existing curl handles,
 * but start recruiting extra connections if we start accumulating queued
 * requests */
static bool workio_get_work(struct workio_cmd *wc)
{
	pthread_t get_thread;

	if (unlikely(pthread_create(&get_thread, NULL, get_work_thread, (void *)wc))) {
		applog(LOG_ERR, "Failed to create get_work_thread");
		return false;
	}
	return true;
}

static bool stale_work(struct work *work, bool share)
{
	struct timeval now;
	struct pool *pool;

	if (opt_benchmark)
		return false;

	gettimeofday(&now, NULL);
	if (share) {
		if ((now.tv_sec - work->tv_staged.tv_sec) >= opt_expiry)
			return true;
	} else if ((now.tv_sec - work->tv_staged.tv_sec) >= opt_scantime)
		return true;

	if (work->work_block != work_block)
		return true;

	pool = work->pool;
	if (opt_fail_only && !share && pool != current_pool() && pool->enabled != POOL_REJECTING)
		return true;

	return false;
}


static void *submit_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	struct work *work = wc->u.work;
	struct pool *pool = work->pool;
	struct curl_ent *ce;
	int failures = 0;

	pthread_detach(pthread_self());

	applog(LOG_DEBUG, "Creating extra submit work thread");

	if (stale_work(work, true)) {
		if (opt_submit_stale)
			applog(LOG_NOTICE, "Stale share detected, submitting as user requested");
		else if (pool->submit_old)
			applog(LOG_NOTICE, "Stale share detected, submitting as pool requested");
		else {
			applog(LOG_NOTICE, "Stale share detected, discarding");
			sharelog("discard", work);
			total_stale++;
			pool->stale_shares++;
			goto out;
		}
		work->stale = true;
	}

	ce = pop_curl_entry(pool);
	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(work, ce->curl)) {
		if (stale_work(work, true)) {
			applog(LOG_NOTICE, "Share became stale while retrying submit, discarding");
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
			fail_pause);
		sleep(fail_pause);
		fail_pause += opt_fail_pause;
	}
	fail_pause = opt_fail_pause;
	push_curl_entry(ce, pool);
out:
	workio_cmd_free(wc);
	return NULL;
}

/* We try to reuse curl handles as much as possible, but if there is already
 * work queued to be submitted, we start generating extra handles to submit
 * the shares to avoid ever increasing backlogs. This allows us to scale to
 * any size hardware */
static bool workio_submit_work(struct workio_cmd *wc)
{
	pthread_t submit_thread;

	if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, (void *)wc))) {
		applog(LOG_ERR, "Failed to create submit_work_thread");
		return false;
	}
	return true;
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

void switch_pools(struct pool *selected)
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
				if (!pool->idle && pool->enabled == POOL_ENABLED) {
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

	if (pool != last_pool)
		applog(LOG_WARNING, "Switching to %s", pool->rpc_url);

	/* Reset the queued amount to allow more to be queued for the new pool */
	mutex_lock(&qd_lock);
	total_queued = 0;
	mutex_unlock(&qd_lock);

	mutex_lock(&lp_lock);
	pthread_cond_broadcast(&lp_cond);
	mutex_unlock(&lp_lock);
}

static void discard_work(struct work *work)
{
	if (!work->clone && !work->rolls && !work->mined) {
		if (work->pool)
			work->pool->discarded_work++;
		total_discarded++;
		applog(LOG_DEBUG, "Discarded work");
	} else
		applog(LOG_DEBUG, "Discarded cloned or rolled work");
	free_work(work);
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
}

static int requests_queued(void)
{
	int ret;

	mutex_lock(&qd_lock);
	ret = total_queued;
	mutex_unlock(&qd_lock);
	return ret;
}

static int discard_stale(void)
{
	struct work *work, *tmp;
	int i, stale = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (stale_work(work, false)) {
			HASH_DEL(staged_work, work);
			if (work->clone || work->longpoll)
				--staged_extras;
			discard_work(work);
			stale++;
		}
	}
	mutex_unlock(stgd_lock);

	applog(LOG_DEBUG, "Discarded %d stales that didn't match current hash", stale);

	/* Dec queued outside the loop to not have recursive locks */
	for (i = 0; i < stale; i++)
		dec_queued();

	return stale;
}

static bool queue_request(struct thr_info *thr, bool needed);

static void restart_threads(void)
{
	int i, j, stale, fd;
	struct cgpu_info *cgpu;
	struct thr_info *thr;

	/* Discard staged work that is now stale */
	stale = discard_stale();

	for (i = 0; i < stale; i++)
		queue_request(NULL, true);

	for (i = 0; i < total_devices; ++i)
	{
		cgpu = devices[i];
		for (j = 0; j < cgpu->threads; ++j)
		{
			thr = &cgpu->thread[j];
			fd = thr->_work_restart_fd_w;
			thr->work_restart = true;
			if (fd != -1)
				write(fd, "\0", 1);
		}
	}
}

static void set_curblock(char *hexstr, unsigned char *hash)
{
	unsigned char hash_swap[32];
	struct timeval tv_now;
	char *old_hash;

	strcpy(current_block, hexstr);
	gettimeofday(&tv_now, NULL);
	get_timestamp(blocktime, &tv_now);
	swap256(hash_swap, hash);

	/* Don't free current_hash directly to avoid dereferencing when read
	 * elsewhere */
	mutex_lock(&ch_lock);
	old_hash = current_hash;
	current_hash = bin2hex(hash_swap, 16);
	free(old_hash);
	mutex_unlock(&ch_lock);

	if (unlikely(!current_hash))
		quit (1, "set_curblock OOM");
	applog(LOG_INFO, "New block: %s...", current_hash);
}

/* Search to see if this string is from a block that has been seen before */
static bool block_exists(char *hexstr)
{
	struct block *s;

	rd_lock(&blk_lock);
	HASH_FIND_STR(blocks, hexstr, s);
	rd_unlock(&blk_lock);
	if (s)
		return true;
	return false;
}

/* Tests if this work is from a block that has been seen before */
static inline bool from_existing_block(struct work *work)
{
	char *hexstr = bin2hex(work->data, 18);
	bool ret;

	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "from_existing_block OOM");
		return true;
	}
	ret = block_exists(hexstr);
	free(hexstr);
	return ret;
}

static void test_work_current(struct work *work)
{
	char *hexstr;

	if (opt_benchmark)
		return;

	hexstr = bin2hex(work->data, 18);
	if (unlikely(!hexstr)) {
		applog(LOG_ERR, "stage_thread OOM");
		return;
	}

	/* Search to see if this block exists yet and if not, consider it a
	 * new block and set the current block details to this one */
	if (!block_exists(hexstr)) {
		struct block *s = calloc(sizeof(struct block), 1);

		if (unlikely(!s))
			quit (1, "test_work_current OOM");
		strcpy(s->hash, hexstr);
		wr_lock(&blk_lock);
		HASH_ADD_STR(blocks, hash, s);
		wr_unlock(&blk_lock);
		set_curblock(hexstr, work->data);
		if (unlikely(++new_blocks == 1))
			goto out_free;

		work_block++;

		if (work->longpoll) {
			applog(LOG_NOTICE, "LONGPOLL from pool %d detected new block",
			       work->pool->pool_no);
			work->longpoll = false;
		} else if (have_longpoll)
			applog(LOG_NOTICE, "New block detected on network before longpoll");
		else
			applog(LOG_NOTICE, "New block detected on network");
		restart_threads();
	} else if (work->longpoll) {
		work->longpoll = false;
		if (work->pool == current_pool()) {
			applog(LOG_NOTICE, "LONGPOLL from pool %d requested work restart",
				work->pool->pool_no);
			work_block++;
			restart_threads();
		}
	}
out_free:
	free(hexstr);
}

static int tv_sort(struct work *worka, struct work *workb)
{
	return worka->tv_staged.tv_sec - workb->tv_staged.tv_sec;
}

static bool hash_push(struct work *work)
{
	bool rc = true;

	mutex_lock(stgd_lock);
	if (likely(!getq->frozen)) {
		HASH_ADD_INT(staged_work, id, work);
		HASH_SORT(staged_work, tv_sort);
		if (work->clone || work->longpoll)
			++staged_extras;
	} else
		rc = false;
	pthread_cond_signal(&getq->cond);
	mutex_unlock(stgd_lock);
	return rc;
}

static void *stage_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	bool ok = true;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (ok) {
		struct work *work = NULL;

		applog(LOG_DEBUG, "Popping work to stage thread");

		work = tq_pop(mythr->q, NULL);
		if (unlikely(!work)) {
			applog(LOG_ERR, "Failed to tq_pop in stage_thread");
			ok = false;
			break;
		}
		work->work_block = work_block;

		test_work_current(work);

		applog(LOG_DEBUG, "Pushing work to getwork queue");

		if (unlikely(!hash_push(work))) {
			applog(LOG_WARNING, "Failed to hash_push in stage_thread");
			continue;
		}
	}

	tq_freeze(mythr->q);
	return NULL;
}

static bool stage_work(struct work *work)
{
	applog(LOG_DEBUG, "Pushing work to stage thread");

	if (unlikely(!tq_push(thr_info[stage_thr_id].q, work))) {
		applog(LOG_ERR, "Could not tq_push work in stage_work");
		return false;
	}
	return true;
}

#ifdef HAVE_CURSES
int curses_int(const char *query)
{
	int ret;
	char *cvar;

	cvar = curses_input(query);
	ret = atoi(cvar);
	free(cvar);
	return ret;
}
#endif

#ifdef HAVE_CURSES
static bool input_pool(bool live);
#endif

int active_pools(void)
{
	int ret = 0;
	int i;

	for (i = 0; i < total_pools; i++) {
		if ((pools[i])->enabled == POOL_ENABLED)
			ret++;
	}
	return ret;
}

#ifdef HAVE_CURSES
static void display_pool_summary(struct pool *pool)
{
	double efficiency = 0.0;

	if (curses_active_locked()) {
		wlog("Pool: %s\n", pool->rpc_url);
		if (pool->solved)
			wlog("SOLVED %d BLOCK%s!\n", pool->solved, pool->solved > 1 ? "S" : "");
		wlog("%s own long-poll support\n", pool->hdr_path ? "Has" : "Does not have");
		wlog(" Queued work requests: %d\n", pool->getwork_requested);
		wlog(" Share submissions: %d\n", pool->accepted + pool->rejected);
		wlog(" Accepted shares: %d\n", pool->accepted);
		wlog(" Rejected shares: %d\n", pool->rejected);
		if (pool->accepted || pool->rejected)
			wlog(" Reject ratio: %.1f%%\n", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));
		efficiency = pool->getwork_requested ? pool->accepted * 100.0 / pool->getwork_requested : 0.0;
		wlog(" Efficiency (accepted / queued): %.0f%%\n", efficiency);

		wlog(" Discarded work due to new blocks: %d\n", pool->discarded_work);
		wlog(" Stale submissions discarded due to new blocks: %d\n", pool->stale_shares);
		wlog(" Unable to get work from server occasions: %d\n", pool->getfail_occasions);
		wlog(" Submitting work remotely delay occasions: %d\n\n", pool->remotefail_occasions);
		unlock_curses();
	}
}
#endif

/* We can't remove the memory used for this struct pool because there may
 * still be work referencing it. We just remove it from the pools list */
void remove_pool(struct pool *pool)
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
	pool->removed = true;
	total_pools--;
}

void write_config(FILE *fcfg)
{
	int i;

	/* Write pool values */
	fputs("{\n\"pools\" : [", fcfg);
	for(i = 0; i < total_pools; i++) {
		fprintf(fcfg, "%s\n\t{\n\t\t\"url\" : \"%s\",", i > 0 ? "," : "", pools[i]->rpc_url);
		fprintf(fcfg, "\n\t\t\"user\" : \"%s\",", pools[i]->rpc_user);
		fprintf(fcfg, "\n\t\t\"pass\" : \"%s\"\n\t}", pools[i]->rpc_pass);
		}
	fputs("\n]\n", fcfg);

	if (nDevs) {
		/* Write GPU device values */
		fputs(",\n\"intensity\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, gpus[i].dynamic ? "%sd" : "%s%d", i > 0 ? "," : "", gpus[i].intensity);
#ifdef HAVE_OPENCL
		fputs("\",\n\"vectors\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "",
				gpus[i].vwidth);
		fputs("\",\n\"worksize\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "",
				(int)gpus[i].work_size);
		fputs("\",\n\"kernel\" : \"", fcfg);
		for(i = 0; i < nDevs; i++) {
			fprintf(fcfg, "%s", i > 0 ? "," : "");
			switch (gpus[i].kernel) {
				case KL_NONE: // Shouldn't happen
					break;
				case KL_POCLBM:
					fprintf(fcfg, "poclbm");
					break;
				case KL_PHATK:
					fprintf(fcfg, "phatk");
					break;
				case KL_DIAKGCN:
					fprintf(fcfg, "diakgcn");
					break;
				case KL_DIABLO:
					fprintf(fcfg, "diablo");
					break;
			}
		}
#ifdef HAVE_ADL
		fputs("\",\n\"gpu-engine\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d-%d", i > 0 ? "," : "", gpus[i].min_engine, gpus[i].gpu_engine);
		fputs("\",\n\"gpu-fan\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d-%d", i > 0 ? "," : "", gpus[i].min_fan, gpus[i].gpu_fan);
		fputs("\",\n\"gpu-memclock\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].gpu_memclock);
		fputs("\",\n\"gpu-memdiff\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].gpu_memdiff);
		fputs("\",\n\"gpu-powertune\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].gpu_powertune);
		fputs("\",\n\"gpu-vddc\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%1.3f", i > 0 ? "," : "", gpus[i].gpu_vddc);
		fputs("\",\n\"temp-cutoff\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].cutofftemp);
		fputs("\",\n\"temp-overheat\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].adl.overtemp);
		fputs("\",\n\"temp-target\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].adl.targettemp);
#endif
#endif
		fputs("\"", fcfg);
	}
#ifdef HAVE_ADL
	if (opt_reorder)
		fprintf(fcfg, ",\n\"gpu-reorder\" : true");
#endif
#ifdef WANT_CPUMINE
	fprintf(fcfg, ",\n\"algo\" : \"%s\"", algo_names[opt_algo]);
#endif

	/* Simple bool and int options */
	struct opt_table *opt;
	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name = strdup(opt->names);
		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			if (p[1] != '-')
				continue;
			if (opt->type & OPT_NOARG &&
			   ((void *)opt->cb == (void *)opt_set_bool || (void *)opt->cb == (void *)opt_set_invbool) &&
			   (*(bool *)opt->u.arg == ((void *)opt->cb == (void *)opt_set_bool)))
				fprintf(fcfg, ",\n\"%s\" : true", p+2);

			if (opt->type & OPT_HASARG &&
			   ((void *)opt->cb_arg == (void *)set_int_0_to_9999 ||
			   (void *)opt->cb_arg == (void *)set_int_1_to_65535 ||
			   (void *)opt->cb_arg == (void *)set_int_0_to_10 ||
			   (void *)opt->cb_arg == (void *)set_int_1_to_10) && opt->desc != opt_hidden)
				fprintf(fcfg, ",\n\"%s\" : \"%d\"", p+2, *(int *)opt->u.arg);
		}
	}

	/* Special case options */
	fprintf(fcfg, ",\n\"shares\" : \"%d\"", opt_shares);
	if (pool_strategy == POOL_LOADBALANCE)
		fputs(",\n\"load-balance\" : true", fcfg);
	if (pool_strategy == POOL_ROUNDROBIN)
		fputs(",\n\"round-robin\" : true", fcfg);
	if (pool_strategy == POOL_ROTATE)
		fprintf(fcfg, ",\n\"rotate\" : \"%d\"", opt_rotate_period);
#if defined(unix)
	if (opt_stderr_cmd && *opt_stderr_cmd)
		fprintf(fcfg, ",\n\"monitor\" : \"%s\"", opt_stderr_cmd);
#endif // defined(unix)
	if (opt_kernel_path && *opt_kernel_path) {
		char *kpath = strdup(opt_kernel_path);
		if (kpath[strlen(kpath)-1] == '/')
			kpath[strlen(kpath)-1] = 0;
		fprintf(fcfg, ",\n\"kernel-path\" : \"%s\"", kpath);
	}
	if (schedstart.enable)
		fprintf(fcfg, ",\n\"sched-time\" : \"%d:%d\"", schedstart.tm.tm_hour, schedstart.tm.tm_min);
	if (schedstop.enable)
		fprintf(fcfg, ",\n\"stop-time\" : \"%d:%d\"", schedstop.tm.tm_hour, schedstop.tm.tm_min);
	if (opt_socks_proxy && *opt_socks_proxy)
		fprintf(fcfg, ",\n\"socks-proxy\" : \"%s\"", opt_socks_proxy);
	for(i = 0; i < nDevs; i++)
		if (gpus[i].deven == DEV_DISABLED)
			break;
	if (i < nDevs)
		for (i = 0; i < nDevs; i++)
			if (gpus[i].deven != DEV_DISABLED)
				fprintf(fcfg, ",\n\"device\" : \"%d\"", i);
	if (opt_api_allow)
		fprintf(fcfg, ",\n\"api-allow\" : \"%s\"", opt_api_allow);
	if (strcmp(opt_api_description, PACKAGE_STRING) != 0)
		fprintf(fcfg, ",\n\"api-description\" : \"%s\"", opt_api_description);
	if (opt_icarus_timing)
		fprintf(fcfg, ",\n\"icarus-timing\" : \"%s\"", opt_icarus_timing);
	fputs("\n}", fcfg);
}

#ifdef HAVE_CURSES
static void display_pools(void)
{
	struct pool *pool;
	int selected, i;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
updated:
	for (i = 0; i < total_pools; i++) {
		pool = pools[i];

		if (pool == current_pool())
			wattron(logwin, A_BOLD);
		if (pool->enabled != POOL_ENABLED)
			wattron(logwin, A_DIM);
		wlogprint("%d: ", pool->pool_no);
		switch (pool->enabled) {
			case POOL_ENABLED:
				wlogprint("Enabled ");
				break;
			case POOL_DISABLED:
				wlogprint("Disabled ");
				break;
			case POOL_REJECTING:
				wlogprint("Rejecting ");
				break;
		}
		wlogprint("%s Priority %d: %s  User:%s\n",
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
		pool->enabled = POOL_DISABLED;
		remove_pool(pool);
		goto updated;
	} else if (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		pool->enabled = POOL_ENABLED;
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
		pool->enabled = POOL_DISABLED;
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
		pool->enabled = POOL_ENABLED;
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
	} else
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
	clear_logwin();
retry:
	wlogprint("[N]ormal [C]lear [S]ilent mode (disable all output)\n");
	wlogprint("[D]ebug:%s\n[P]er-device:%s\n[Q]uiet:%s\n[V]erbose:%s\n[R]PC debug:%s\n[L]og interval:%d\n",
		opt_debug ? "on" : "off",
	        want_per_device_stats? "on" : "off",
		opt_quiet ? "on" : "off",
		opt_log_output ? "on" : "off",
		opt_protocol ? "on" : "off",
		opt_log_interval);
	wlogprint("Select an option or any other key to return\n");
	input = getch();
	if (!strncasecmp(&input, "q", 1)) {
		opt_quiet ^= true;
		wlogprint("Quiet mode %s\n", opt_quiet ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "v", 1)) {
		opt_log_output ^= true;
		if (opt_log_output)
			opt_quiet = false;
		wlogprint("Verbose mode %s\n", opt_log_output ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "n", 1)) {
		opt_log_output = false;
		opt_debug = false;
		opt_quiet = false;
		opt_protocol = false;
		want_per_device_stats = false;
		wlogprint("Output mode reset to normal\n");
		goto retry;
	} else if (!strncasecmp(&input, "d", 1)) {
		opt_debug ^= true;
		opt_log_output = opt_debug;
		if (opt_debug)
			opt_quiet = false;
		wlogprint("Debug mode %s\n", opt_debug ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "p", 1)) {
		want_per_device_stats ^= true;
		opt_log_output = want_per_device_stats;
		wlogprint("Per-device stats %s\n", want_per_device_stats ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "r", 1)) {
		opt_protocol ^= true;
		if (opt_protocol)
			opt_quiet = false;
		wlogprint("RPC protocol debugging %s\n", opt_protocol ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "c", 1))
		clear_logwin();
	else if (!strncasecmp(&input, "l", 1)) {
		selected = curses_int("Interval in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_log_interval = selected;
		wlogprint("Log interval set to %d seconds\n", opt_log_interval);
		goto retry;
	} else if (!strncasecmp(&input, "s", 1)) {
		opt_realquiet = true;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}
#endif

#ifdef HAVE_CURSES
static void set_options(void)
{
	int selected;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
retry:
	wlogprint("\n[L]ongpoll: %s\n", want_longpoll ? "On" : "Off");
	wlogprint("[Q]ueue: %d\n[S]cantime: %d\n[E]xpiry: %d\n[R]etries: %d\n"
		  "[P]ause: %d\n[W]rite config file\n[B]FGMiner restart\n",
		opt_queue, opt_scantime, opt_expiry, opt_retries, opt_fail_pause);
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
	} else if (!strncasecmp(&input, "l", 1)) {
		if (want_longpoll)
			stop_longpoll();
		else
			start_longpoll();
		applog(LOG_WARNING, "Longpoll %s", want_longpoll ? "enabled" : "disabled");
		goto retry;
	} else if  (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Set scantime in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_scantime = selected;
		goto retry;
	} else if  (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Set expiry time in seconds");
		if (selected < 0 || selected > 9999) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		opt_expiry = selected;
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
	} else if  (!strncasecmp(&input, "w", 1)) {
		FILE *fcfg;
		char *str, filename[PATH_MAX], prompt[PATH_MAX + 50];

#if defined(unix)
		if (getenv("HOME") && *getenv("HOME")) {
		        strcpy(filename, getenv("HOME"));
			strcat(filename, "/");
		}
		else
			strcpy(filename, "");
		strcat(filename, ".bfgminer/");
		mkdir(filename, 0777);
#else
		strcpy(filename, "");
#endif
		strcat(filename, def_conf);
		sprintf(prompt, "Config filename to write (Enter for default) [%s]", filename);
		str = curses_input(prompt);
		if (strcmp(str, "-1")) {
			struct stat statbuf;

			strcpy(filename, str);
			if (!stat(filename, &statbuf)) {
				wlogprint("File exists, overwrite?\n");
				input = getch();
				if (strncasecmp(&input, "y", 1))
					goto retry;
			}
		}
		fcfg = fopen(filename, "w");
		if (!fcfg) {
			wlogprint("Cannot open or create file\n");
			goto retry;
		}
		write_config(fcfg);
		fclose(fcfg);
		goto retry;

	} else if (!strncasecmp(&input, "b", 1)) {
		wlogprint("Are you sure?\n");
		input = getch();
		if (!strncasecmp(&input, "y", 1))
			app_restart();
		else
			clear_logwin();
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}

static void *input_thread(void __maybe_unused *userdata)
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
		else if (have_opencl && !strncasecmp(&input, "g", 1))
			manage_gpu();
		if (opt_realquiet) {
			disable_curses();
			break;
		}
	}

	return NULL;
}
#endif

/* This thread should not be shut down unless a problem occurs */
static void *workio_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	bool ok = true;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (ok) {
		struct workio_cmd *wc;

		applog(LOG_DEBUG, "Popping work to work thread");

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
		default:
			ok = false;
			break;
		}
	}

	tq_freeze(mythr->q);

	return NULL;
}

static void *api_thread(void *userdata)
{
	struct thr_info *mythr = userdata;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	api(api_thr_id);

	PTH(mythr) = 0L;

	return NULL;
}

void thread_reportin(struct thr_info *thr)
{
	gettimeofday(&thr->last, NULL);
	thr->cgpu->status = LIFE_WELL;
	thr->getwork = false;
	thr->cgpu->device_last_well = time(NULL);
}

static inline void thread_reportout(struct thr_info *thr)
{
	thr->getwork = true;
}

static void hashmeter(int thr_id, struct timeval *diff,
		      unsigned long long hashes_done)
{
	struct timeval temp_tv_end, total_diff;
	double secs;
	double local_secs;
	double utility, efficiency = 0.0;
	static double local_mhashes_done = 0;
	static double rolling = 0;
	double local_mhashes = (double)hashes_done / 1000000.0;
	bool showlog = false;

	/* Update the last time this thread reported in */
	if (thr_id >= 0) {
		gettimeofday(&thr_info[thr_id].last, NULL);
		thr_info[thr_id].cgpu->device_last_well = time(NULL);
	}

	/* Don't bother calculating anything if we're not displaying it */
	if (opt_realquiet || !opt_log_interval)
		return;

	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

	/* So we can call hashmeter from a non worker thread */
	if (thr_id >= 0) {
		struct thr_info *thr = &thr_info[thr_id];
		struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
		double thread_rolling = 0.0;
		int i;

		applog(LOG_DEBUG, "[thread %d: %llu hashes, %.0f khash/sec]",
			thr_id, hashes_done, hashes_done / secs);

		/* Rolling average for each thread and each device */
		decay_time(&thr->rolling, local_mhashes / secs);
		for (i = 0; i < mining_threads; i++) {
			struct thr_info *th = &thr_info[i];

			if (th->cgpu == cgpu)
				thread_rolling += th->rolling;
		}
		mutex_lock(&hash_lock);
		decay_time(&cgpu->rolling, thread_rolling);
		cgpu->total_mhashes += local_mhashes;
		mutex_unlock(&hash_lock);

		// If needed, output detailed, per-device stats
		if (want_per_device_stats) {
			struct timeval now;
			struct timeval elapsed;
			gettimeofday(&now, NULL);
			timeval_subtract(&elapsed, &now, &thr->cgpu->last_message_tv);
			if (opt_log_interval <= elapsed.tv_sec) {
				struct cgpu_info *cgpu = thr->cgpu;
				char logline[255];

				cgpu->last_message_tv = now;

				get_statline(logline, cgpu);
				if (!curses_active) {
					printf("%s          \r", logline);
					fflush(stdout);
				} else
					applog(LOG_INFO, "%s", logline);
			}
		}
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

	sprintf(statusline, "%s(%ds):%.1f (avg):%.1f Mh/s | Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.1f/m",
		want_per_device_stats ? "ALL " : "",
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

static void *longpoll_thread(void *userdata);

static bool pool_active(struct pool *pool, bool pinging)
{
	bool ret = false;
	json_t *val;
	CURL *curl;
	bool rolltime;

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return false;
	}

	applog(LOG_INFO, "Testing pool %s", pool->rpc_url);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, rpc_req,
			true, false, &rolltime, pool, false);

	if (val) {
		struct work *work = make_work();
		bool rc;

		rc = work_decode(json_object_get(val, "result"), work);
		if (rc) {
			applog(LOG_DEBUG, "Successfully retrieved and deciphered work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			work->pool = pool;
			work->rolltime = rolltime;
			applog(LOG_DEBUG, "Pushing pooltest work to base pool");

			tq_push(thr_info[stage_thr_id].q, work);
			total_getworks++;
			pool->getwork_requested++;
			inc_queued();
			ret = true;
			gettimeofday(&pool->tv_idle, NULL);
		} else {
			applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			free_work(work);
		}
		json_decref(val);

		if (pool->lp_url)
			goto out;

		/* Decipher the longpoll URL, if any, and store it in ->lp_url */
		if (pool->hdr_path) {
			char *copy_start, *hdr_path;
			bool need_slash = false;

			hdr_path = pool->hdr_path;
			if (strstr(hdr_path, "://")) {
				pool->lp_url = hdr_path;
				hdr_path = NULL;
			} else {
				/* absolute path, on current server */
				copy_start = (*hdr_path == '/') ? (hdr_path + 1) : hdr_path;
				if (pool->rpc_url[strlen(pool->rpc_url) - 1] != '/')
					need_slash = true;

				pool->lp_url = malloc(strlen(pool->rpc_url) + strlen(copy_start) + 2);
				if (!pool->lp_url) {
					applog(LOG_ERR, "Malloc failure in pool_active");
					return false;
				}

				sprintf(pool->lp_url, "%s%s%s", pool->rpc_url, need_slash ? "/" : "", copy_start);
			}
		} else
			pool->lp_url = NULL;

		if (want_longpoll && !pool->lp_started) {
			pool->lp_started = true;
			if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
				quit(1, "Failed to create pool longpoll thread");
		}
	} else {
		applog(LOG_DEBUG, "FAILED to retrieve work from pool %u %s",
		       pool->pool_no, pool->rpc_url);
		if (!pinging)
			applog(LOG_WARNING, "Pool %u slow/down or URL or credentials invalid", pool->pool_no);
	}
out:
	curl_easy_cleanup(curl);
	return ret;
}

static void pool_died(struct pool *pool)
{
	if (!pool_tset(pool, &pool->idle)) {
		applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
		gettimeofday(&pool->tv_idle, NULL);
		switch_pools(NULL);
	}
}

static inline int cp_prio(void)
{
	int prio;

	mutex_lock(&control_lock);
	prio = currentpool->prio;
	mutex_unlock(&control_lock);
	return prio;
}

static void pool_resus(struct pool *pool)
{
	applog(LOG_WARNING, "Pool %d %s alive", pool->pool_no, pool->rpc_url);
	if (pool->prio < cp_prio() && pool_strategy == POOL_FAILOVER)
		switch_pools(NULL);
}

static long requested_tv_sec;

static bool queue_request(struct thr_info *thr, bool needed)
{
	int rq = requests_queued();
	struct workio_cmd *wc;
	struct timeval now;

	gettimeofday(&now, NULL);

	/* Space out retrieval of extra work according to the number of mining
	 * threads */
	if (rq >= mining_threads + staged_extras &&
	    (now.tv_sec - requested_tv_sec) < (opt_scantime / (mining_threads + 1) ?: 1))
		return true;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to calloc wc in queue_request");
		return false;
	}

	wc->cmd = WC_GET_WORK;
	if (thr)
		wc->thr = thr;
	else
		wc->thr = NULL;

	/* If we're queueing work faster than we can stage it, consider the
	 * system lagging and allow work to be gathered from another pool if
	 * possible */
	if (rq && needed && !requests_staged() && !opt_fail_only)
		wc->lagging = true;

	applog(LOG_DEBUG, "Queueing getwork request to work thread");

	/* send work request to workio thread */
	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		applog(LOG_ERR, "Failed to tq_push in queue_request");
		workio_cmd_free(wc);
		return false;
	}

	requested_tv_sec = now.tv_sec;
	inc_queued();
	return true;
}

static struct work *hash_pop(const struct timespec *abstime)
{
	struct work *work = NULL;
	int rc = 0;

	mutex_lock(stgd_lock);
	while (!getq->frozen && !HASH_COUNT(staged_work) && !rc)
		rc = pthread_cond_timedwait(&getq->cond, stgd_lock, abstime);

	if (HASH_COUNT(staged_work)) {
		work = staged_work;
		HASH_DEL(staged_work, work);
		if (work->clone || work->longpoll)
			--staged_extras;
	}
	mutex_unlock(stgd_lock);

	return work;
}

static inline bool should_roll(struct work *work)
{
	if (work->pool == current_pool() || pool_strategy == POOL_LOADBALANCE)
		return true;
	return false;
}

static inline bool can_roll(struct work *work)
{
	return (work->pool && !stale_work(work, false) && work->rolltime &&
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
	applog(LOG_DEBUG, "Successfully rolled work");

	/* This is now a different work item so it needs a different ID for the
	 * hashtable */
	work->id = total_work++;
}

static bool reuse_work(struct work *work)
{
	if (can_roll(work) && should_roll(work)) {
		roll_work(work);
		return true;
	}
	return false;
}

static bool get_work(struct work *work, bool requested, struct thr_info *thr,
		     const int thr_id)
{
	bool newreq = false, ret = false;
	struct timespec abstime = {0, 0};
	struct timeval now;
	struct work *work_heap;
	struct pool *pool;
	int failures = 0;

	/* Tell the watchdog thread this thread is waiting on getwork and
	 * should not be restarted */
	thread_reportout(thr);

	if (opt_benchmark) {
		get_benchmark_work(work);
		thread_reportin(thr);
		return true;
	}
retry:
	pool = current_pool();
	if (!requested || requests_queued() < opt_queue) {
		if (unlikely(!queue_request(thr, true))) {
			applog(LOG_WARNING, "Failed to queue_request in get_work");
			goto out;
		}
		newreq = true;
	}

	if (reuse_work(work)) {
		ret = true;
		goto out;
	}

	if (requested && !newreq && !requests_staged() && requests_queued() >= mining_threads &&
	    !pool_tset(pool, &pool->lagging)) {
		applog(LOG_WARNING, "Pool %d not providing work fast enough", pool->pool_no);
		pool->getfail_occasions++;
		total_go++;
	}

	newreq = requested = false;
	gettimeofday(&now, NULL);
	abstime.tv_sec = now.tv_sec + 60;

	applog(LOG_DEBUG, "Popping work from get queue to get work");

	/* wait for 1st response, or get cached response */
	work_heap = hash_pop(&abstime);
	if (unlikely(!work_heap)) {
		/* Attempt to switch pools if this one times out */
		pool_died(pool);
		goto retry;
	}

	if (stale_work(work_heap, false)) {
		dec_queued();
		discard_work(work_heap);
		goto retry;
	}

	pool = work_heap->pool;
	/* If we make it here we have succeeded in getting fresh work */
	if (!work_heap->mined) {
		pool_tclear(pool, &pool->lagging);
		if (pool_tclear(pool, &pool->idle))
			pool_resus(pool);
	}

	memcpy(work, work_heap, sizeof(*work));

	/* Hand out a clone if we can roll this work item */
	if (reuse_work(work_heap)) {
		applog(LOG_DEBUG, "Pushing divided work to get queue head");

		stage_work(work_heap);
		work->clone = true;
	} else {
		dec_queued();
		free_work(work_heap);
	}

	ret = true;
out:
	if (unlikely(ret == false)) {
		if ((opt_retries >= 0) && (++failures > opt_retries)) {
			applog(LOG_ERR, "Failed %d times to get_work");
			return ret;
		}
		applog(LOG_DEBUG, "Retrying after %d seconds", fail_pause);
		sleep(fail_pause);
		fail_pause += opt_fail_pause;
		goto retry;
	}
	fail_pause = opt_fail_pause;

	work->thr_id = thr_id;
	thread_reportin(thr);
	if (ret)
		work->mined = true;
	return ret;
}

bool submit_work_sync(struct thr_info *thr, const struct work *work_in)
{
	struct workio_cmd *wc;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to calloc wc in submit_work_sync");
		return false;
	}

	wc->u.work = make_work();
	wc->cmd = WC_SUBMIT_WORK;
	wc->thr = thr;
	memcpy(wc->u.work, work_in, sizeof(*work_in));
	wc->u.work->share_found_time = time(NULL);

	applog(LOG_DEBUG, "Pushing submit work to work thread");

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

bool hashtest(const struct work *work)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[128];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	unsigned char hash2[32];
	uint32_t *hash2_32 = (uint32_t *)hash2;
	int i;

	for (i = 0; i < 80 / 4; i++)
		swap32[i] = swab32(data32[i]);

	sha2(swap, 80, hash1, false);
	sha2(hash1, 32, hash2, false);

	for (i = 0; i < 32 / 4; i++)
		hash2_32[i] = swab32(hash2_32[i]);

	memcpy((void*)work->hash, hash2, 32);

	return fulltest(work->hash, work->target);

}

bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	work->data[64 + 12 + 0] = (nonce >> 0) & 0xff;
	work->data[64 + 12 + 1] = (nonce >> 8) & 0xff;
	work->data[64 + 12 + 2] = (nonce >> 16) & 0xff;
	work->data[64 + 12 + 3] = (nonce >> 24) & 0xff;

	/* Do one last check before attempting to submit the work */
	if (!hashtest(work)) {
		applog(LOG_INFO, "Share below target");
		return true;
	}
	return submit_work_sync(thr, work);
}

static inline bool abandon_work(struct work *work, struct timeval *wdiff, uint64_t hashes)
{
	if (wdiff->tv_sec > opt_scantime ||
	    work->blk.nonce >= MAXTHREADS - hashes ||
	    hashes >= 0xfffffffe ||
	    stale_work(work, false))
		return true;
	return false;
}

void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	const struct device_api *api = cgpu->api;
	struct cgminer_stats *dev_stats = &(cgpu->cgminer_stats);
	struct cgminer_stats *pool_stats;
	struct timeval getwork_start;

	/* Try to cycle approximately 5 times before each log update */
	const unsigned long def_cycle = opt_log_interval / 5 ? : 1;
	long cycle;
	struct timeval tv_start, tv_end, tv_workstart, tv_lastupdate;
	struct timeval diff, sdiff, wdiff;
	uint32_t max_nonce = api->can_limit_work ? api->can_limit_work(mythr) : 0xffffffff;
	unsigned long long hashes_done = 0;
	unsigned long long hashes;
	struct work *work = make_work();
	unsigned const int request_interval = opt_scantime * 2 / 3 ? : 1;
	unsigned const long request_nonce = MAXTHREADS / 3 * 2;
	bool requested = false;
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	gettimeofday(&getwork_start, NULL);

	if (api->thread_init && !api->thread_init(mythr)) {
		cgpu->device_last_not_well = time(NULL);
		cgpu->device_not_well_reason = REASON_THREAD_FAIL_INIT;
		cgpu->thread_fail_init_count++;

		goto out;
	}

	thread_reportout(mythr);
	applog(LOG_DEBUG, "Popping ping in miner thread");
	tq_pop(mythr->q, NULL); /* Wait for a ping to start */

	sdiff.tv_sec = sdiff.tv_usec = 0;
	gettimeofday(&tv_lastupdate, NULL);

	while (1) {
		mythr->work_restart = false;
		if (api->free_work && likely(work->pool))
			api->free_work(mythr, work);
		if (unlikely(!get_work(work, requested, mythr, thr_id))) {
			applog(LOG_ERR, "work retrieval failed, exiting "
				"mining thread %d", thr_id);
			break;
		}
		requested = false;
		cycle = (can_roll(work) && should_roll(work)) ? 1 : def_cycle;
		gettimeofday(&tv_workstart, NULL);
		work->blk.nonce = 0;
		cgpu->max_hashes = 0;
		if (api->prepare_work && !api->prepare_work(mythr, work)) {
			applog(LOG_ERR, "work prepare failed, exiting "
				"mining thread %d", thr_id);
			break;
		}

		do {
			gettimeofday(&tv_start, NULL);

			timersub(&tv_start, &getwork_start, &getwork_start);

			timeradd(&getwork_start,
				&(dev_stats->getwork_wait),
				&(dev_stats->getwork_wait));
			if (timercmp(&getwork_start, &(dev_stats->getwork_wait_max), >)) {
				dev_stats->getwork_wait_max.tv_sec = getwork_start.tv_sec;
				dev_stats->getwork_wait_max.tv_usec = getwork_start.tv_usec;
			}
			if (timercmp(&getwork_start, &(dev_stats->getwork_wait_min), <)) {
				dev_stats->getwork_wait_min.tv_sec = getwork_start.tv_sec;
				dev_stats->getwork_wait_min.tv_usec = getwork_start.tv_usec;
			}
			dev_stats->getwork_calls++;

			pool_stats = &(work->pool->cgminer_stats);

			timeradd(&getwork_start,
				&(pool_stats->getwork_wait),
				&(pool_stats->getwork_wait));
			if (timercmp(&getwork_start, &(pool_stats->getwork_wait_max), >)) {
				pool_stats->getwork_wait_max.tv_sec = getwork_start.tv_sec;
				pool_stats->getwork_wait_max.tv_usec = getwork_start.tv_usec;
			}
			if (timercmp(&getwork_start, &(pool_stats->getwork_wait_min), <)) {
				pool_stats->getwork_wait_min.tv_sec = getwork_start.tv_sec;
				pool_stats->getwork_wait_min.tv_usec = getwork_start.tv_usec;
			}
			pool_stats->getwork_calls++;

			hashes = api->scanhash(mythr, work, work->blk.nonce + max_nonce);

			gettimeofday(&getwork_start, NULL);

			if (unlikely(mythr->work_restart)) {

				/* Apart from device_thread 0, we stagger the
				 * starting of every next thread to try and get
				 * all devices busy before worrying about
				 * getting work for their extra threads */
				if (mythr->device_thread) {
					struct timespec rgtp;

					rgtp.tv_sec = 0;
					rgtp.tv_nsec = 250 * mythr->device_thread * 1000000;
					nanosleep(&rgtp, NULL);
				}
				break;
			}

			if (unlikely(!hashes)) {
				applog(LOG_ERR, "%s %d failure, disabling!", api->name, cgpu->device_id);
				cgpu->deven = DEV_DISABLED;

				cgpu->device_last_not_well = time(NULL);
				cgpu->device_not_well_reason = REASON_THREAD_ZERO_HASH;
				cgpu->thread_zero_hash_count++;

				goto disabled;
			}

			hashes_done += hashes;
			if (hashes > cgpu->max_hashes)
				cgpu->max_hashes = hashes;

			gettimeofday(&tv_end, NULL);
			timeval_subtract(&diff, &tv_end, &tv_start);
			sdiff.tv_sec += diff.tv_sec;
			sdiff.tv_usec += diff.tv_usec;
			if (sdiff.tv_usec > 1000000) {
				++sdiff.tv_sec;
				sdiff.tv_usec -= 1000000;
			}

			timeval_subtract(&wdiff, &tv_end, &tv_workstart);
			if (!requested) {
				if (wdiff.tv_sec > request_interval || work->blk.nonce > request_nonce) {
					thread_reportout(mythr);
					if (unlikely(!queue_request(mythr, false))) {
						applog(LOG_ERR, "Failed to queue_request in miner_thread %d", thr_id);

						cgpu->device_last_not_well = time(NULL);
						cgpu->device_not_well_reason = REASON_THREAD_FAIL_QUEUE;
						cgpu->thread_fail_queue_count++;

						goto out;
					}
					thread_reportin(mythr);
					requested = true;
				}
			}

			if (unlikely(sdiff.tv_sec < cycle)) {
				if (likely(!api->can_limit_work || max_nonce == 0xffffffff))
					continue;

				{
					int mult = 1000000 / ((sdiff.tv_usec + 0x400) / 0x400) + 0x10;
					mult *= cycle;
					if (max_nonce > (0xffffffff * 0x400) / mult)
						max_nonce = 0xffffffff;
					else
						max_nonce = (max_nonce * mult) / 0x400;
				}
			} else if (unlikely(sdiff.tv_sec > cycle) && api->can_limit_work) {
				max_nonce = max_nonce * cycle / sdiff.tv_sec;
			} else if (unlikely(sdiff.tv_usec > 100000) && api->can_limit_work) {
				max_nonce = max_nonce * 0x400 / (((cycle * 1000000) + sdiff.tv_usec) / (cycle * 1000000 / 0x400));
			}

			timeval_subtract(&diff, &tv_end, &tv_lastupdate);
			if (diff.tv_sec >= opt_log_interval) {
				hashmeter(thr_id, &diff, hashes_done);
				hashes_done = 0;
				tv_lastupdate = tv_end;
			}

			if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED)) {
				applog(LOG_WARNING, "Thread %d being disabled", thr_id);
disabled:
				mythr->rolling = mythr->cgpu->rolling = 0;
				applog(LOG_DEBUG, "Popping wakeup ping in miner thread");
				thread_reportout(mythr);
				tq_pop(mythr->q, NULL); /* Ignore ping that's popped */
				thread_reportin(mythr);
				applog(LOG_WARNING, "Thread %d being re-enabled", thr_id);
			}

			sdiff.tv_sec = sdiff.tv_usec = 0;
		} while (!abandon_work(work, &wdiff, cgpu->max_hashes));
	}

out:
	if (api->thread_shutdown)
		api->thread_shutdown(mythr);

	thread_reportin(mythr);
	applog(LOG_ERR, "Thread %d failure, exiting", thr_id);
	tq_freeze(mythr->q);

	return NULL;
}

enum {
	STAT_SLEEP_INTERVAL		= 1,
	STAT_CTR_INTERVAL		= 10000000,
	FAILURE_INTERVAL		= 30,
};

/* Stage another work item from the work returned in a longpoll */
static void convert_to_work(json_t *val, bool rolltime, struct pool *pool)
{
	struct work *work, *work_clone;
	bool rc;

	work = make_work();

	rc = work_decode(json_object_get(val, "result"), work);
	if (unlikely(!rc)) {
		applog(LOG_ERR, "Could not convert longpoll data to work");
		free_work(work);
		return;
	}
	work->pool = pool;
	work->rolltime = rolltime;
	work->longpoll = true;

	/* We'll be checking this work item twice, but we already know it's
	 * from a new block so explicitly force the new block detection now
	 * rather than waiting for it to hit the stage thread. This also
	 * allows testwork to know whether LP discovered the block or not. */
	test_work_current(work);

	/* Don't use backup LPs as work if we have failover-only enabled. Use
	 * the longpoll work from a pool that has been rejecting shares as a
	 * way to detect when the pool has recovered.
	 */
	if (pool != current_pool() && opt_fail_only && pool->enabled != POOL_REJECTING) {
		free_work(work);
		return;
	}

	work_clone = make_work();
	memcpy(work_clone, work, sizeof(struct work));
	while (reuse_work(work)) {
		work_clone->clone = true;
		work_clone->longpoll = false;
		applog(LOG_DEBUG, "Pushing rolled converted work to stage thread");
		if (unlikely(!stage_work(work_clone)))
			break;
		work_clone = make_work();
		memcpy(work_clone, work, sizeof(struct work));
	}
	free_work(work_clone);

	applog(LOG_DEBUG, "Pushing converted work to stage thread");

	if (unlikely(!stage_work(work)))
		free_work(work);
	else
		applog(LOG_DEBUG, "Converted longpoll data to work");
}

/* If we want longpoll, enable it for the chosen default pool, or, if
 * the pool does not support longpoll, find the first one that does
 * and use its longpoll support */
static struct pool *select_longpoll_pool(struct pool *cp)
{
	int i;

	if (cp->hdr_path)
		return cp;
	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->hdr_path)
			return pool;
	}
	return NULL;
}

/* This will make the longpoll thread wait till it's the current pool, or it
 * has been flagged as rejecting, before attempting to open any connections.
 */
static void wait_lpcurrent(struct pool *pool)
{
	if (pool->enabled == POOL_REJECTING)
		return;

	while (pool != current_pool()) {
		mutex_lock(&lp_lock);
		pthread_cond_wait(&lp_cond, &lp_lock);
	}
}

static void *longpoll_thread(void *userdata)
{
	struct pool *cp = (struct pool *)userdata;
	/* This *pool is the source of the actual longpoll, not the pool we've
	 * tied it to */
	struct pool *pool = NULL;
	struct timeval start, end;
	CURL *curl = NULL;
	int failures = 0;
	bool rolltime;

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		goto out;
	}

retry_pool:
	pool = select_longpoll_pool(cp);
	if (!pool) {
		applog(LOG_WARNING, "No suitable long-poll found for pool %s", cp->rpc_url);
		while (!pool) {
			sleep(60);
			pool = select_longpoll_pool(cp);
		}
	}

	/* Any longpoll from any pool is enough for this to be true */
	have_longpoll = true;

	wait_lpcurrent(cp);

	if (cp == pool)
		applog(LOG_WARNING, "Long-polling activated for %s", pool->lp_url);
	else
		applog(LOG_WARNING, "Long-polling activated for pool %s via %s", cp->rpc_url, pool->lp_url);

	while (42) {
		json_t *val, *soval;

		wait_lpcurrent(cp);

		gettimeofday(&start, NULL);

		/* Longpoll connections can be persistent for a very long time
		 * and any number of issues could have come up in the meantime
		 * so always establish a fresh connection instead of relying on
		 * a persistent one. */
		curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		val = json_rpc_call(curl, pool->lp_url, pool->rpc_userpass, rpc_req,
				    false, true, &rolltime, pool, false);
		if (likely(val)) {
			soval = json_object_get(json_object_get(val, "result"), "submitold");
			if (soval)
				pool->submit_old = json_is_true(soval);
			else
				pool->submit_old = false;
			convert_to_work(val, rolltime, pool);
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
			if (opt_retries == -1 || failures++ < opt_retries) {
				if (failures == 1)
					applog(LOG_WARNING,
					       "longpoll failed for %s, retrying every 30s", pool->lp_url);
				sleep(30);
			} else {
				applog(LOG_ERR,
					"longpoll failed for %s, ending thread", pool->lp_url);
				goto out;
			}
		}
		if (pool != cp) {
			pool = select_longpoll_pool(cp);
			if (unlikely(!pool))
				goto retry_pool;
		}

		if (unlikely(pool->removed))
			break;
	}

out:
	if (curl)
		curl_easy_cleanup(curl);

	return NULL;
}

static void stop_longpoll(void)
{
	int i;
	
	want_longpoll = false;
	for (i = 0; i < total_pools; ++i)
	{
		struct pool *pool = pools[i];
		
		if (unlikely(!pool->lp_started))
			continue;
		
		pool->lp_started = false;
		pthread_cancel(pool->longpoll_thread);
	}
	have_longpoll = false;
}

static void start_longpoll(void)
{
	int i;
	
	want_longpoll = true;
	for (i = 0; i < total_pools; ++i)
	{
		struct pool *pool = pools[i];
		
		if (unlikely(pool->removed || pool->lp_started || !pool->lp_url))
			continue;
		
		pool->lp_started = true;
		if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
			quit(1, "Failed to create pool longpoll thread");
	}
}

void reinit_device(struct cgpu_info *cgpu)
{
	if (cgpu->api->reinit_device)
		cgpu->api->reinit_device(cgpu);
}

static struct timeval rotate_tv;

/* We reap curls if they are unused for over a minute */
static void reap_curl(struct pool *pool)
{
	struct curl_ent *ent, *iter;
	struct timeval now;

	gettimeofday(&now, NULL);
	mutex_lock(&pool->pool_lock);
	list_for_each_entry_safe(ent, iter, &pool->curlring, node) {
		if (now.tv_sec - ent->tv.tv_sec > 60) {
			applog(LOG_DEBUG, "Reaped curl %d from pool %d", pool->curls, pool->pool_no);
			pool->curls--;
			list_del(&ent->node);
			curl_easy_cleanup(ent->curl);
			free(ent);
		}
	}
	mutex_unlock(&pool->pool_lock);
}

static void *watchpool_thread(void __maybe_unused *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (42) {
		struct timeval now;
		int i;

		gettimeofday(&now, NULL);

		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			if (!opt_benchmark)
				reap_curl(pool);
			if (pool->enabled == POOL_DISABLED)
				continue;

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 60) {
				gettimeofday(&pool->tv_idle, NULL);
				if (pool_active(pool, true) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
			}
		}

		if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
			gettimeofday(&rotate_tv, NULL);
			switch_pools(NULL);
		}

		sleep(30);
	}
	return NULL;
}


/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
static void *watchdog_thread(void __maybe_unused *userdata)
{
	const unsigned int interval = 3;
	struct timeval zero_tv;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	memset(&zero_tv, 0, sizeof(struct timeval));
	gettimeofday(&rotate_tv, NULL);

	while (1) {
		int i;
		struct timeval now;

		sleep(interval);
		if (requests_queued() < opt_queue)
			queue_request(NULL, false);

		hashmeter(-1, &zero_tv, 0);

#ifdef HAVE_CURSES
		if (curses_active_locked()) {
			change_logwinsize();
			curses_print_status();
			for (i = 0; i < mining_threads; i++)
				curses_print_devstatus(i);
			touchwin(statuswin);
			wrefresh(statuswin);
			touchwin(logwin);
			wrefresh(logwin);
			unlock_curses();
		}
#endif

		gettimeofday(&now, NULL);

		if (!sched_paused && !should_run()) {
			applog(LOG_WARNING, "Pausing execution as per stop time %02d:%02d scheduled",
			       schedstop.tm.tm_hour, schedstop.tm.tm_min);
			if (!schedstart.enable) {
				quit(0, "Terminating execution as planned");
				break;
			}

			applog(LOG_WARNING, "Will restart execution as scheduled at %02d:%02d",
			       schedstart.tm.tm_hour, schedstart.tm.tm_min);
			sched_paused = true;
			for (i = 0; i < mining_threads; i++) {
				struct thr_info *thr;
				thr = &thr_info[i];

				thr->pause = true;
			}
		} else if (sched_paused && should_run()) {
			applog(LOG_WARNING, "Restarting execution as per start time %02d:%02d scheduled",
				schedstart.tm.tm_hour, schedstart.tm.tm_min);
			if (schedstop.enable)
				applog(LOG_WARNING, "Will pause execution as scheduled at %02d:%02d",
					schedstop.tm.tm_hour, schedstop.tm.tm_min);
			sched_paused = false;

			for (i = 0; i < mining_threads; i++) {
				struct thr_info *thr;
				thr = &thr_info[i];

				/* Don't touch disabled devices */
				if (thr->cgpu->deven == DEV_DISABLED)
					continue;
				thr->pause = false;
				tq_push(thr->q, &ping);
			}
		}

#ifdef HAVE_OPENCL
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			struct thr_info *thr = cgpu->thread;
			enum dev_enable *denable;
			int gpu;

			if (cgpu->api != &opencl_api)
				continue;
			/* Use only one thread per device to determine if the GPU is healthy */
			if (i >= nDevs)
				break;
			gpu = thr->cgpu->device_id;
			denable = &cgpu->deven;
#ifdef HAVE_ADL
			if (adl_active && gpus[gpu].has_adl)
				gpu_autotune(gpu, denable);
			if (opt_debug && gpus[gpu].has_adl) {
				int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
				float temp = 0, vddc = 0;

				if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune))
					applog(LOG_DEBUG, "%.1f C  F: %d%%(%dRPM)  E: %dMHz  M: %dMhz  V: %.3fV  A: %d%%  P: %d%%",
					temp, fanpercent, fanspeed, engineclock, memclock, vddc, activity, powertune);
			}
#endif
			/* Thread is waiting on getwork or disabled */
			if (thr->getwork || *denable == DEV_DISABLED)
				continue;

			if (gpus[gpu].status != LIFE_WELL && now.tv_sec - thr->last.tv_sec < 60) {
				applog(LOG_ERR, "Device %d recovered, GPU %d declared WELL!", i, gpu);
				gpus[gpu].status = LIFE_WELL;
				gpus[gpu].device_last_well = time(NULL);
			} else if (now.tv_sec - thr->last.tv_sec > 60 && gpus[gpu].status == LIFE_WELL) {
				thr->rolling = thr->cgpu->rolling = 0;
				gpus[gpu].status = LIFE_SICK;
				applog(LOG_ERR, "Device %d idle for more than 60 seconds, GPU %d declared SICK!", i, gpu);
				gettimeofday(&thr->sick, NULL);

				gpus[gpu].device_last_not_well = time(NULL);
				gpus[gpu].device_not_well_reason = REASON_DEV_SICK_IDLE_60;
				gpus[gpu].dev_sick_idle_60_count++;
#ifdef HAVE_ADL
				if (adl_active && gpus[gpu].has_adl && gpu_activity(gpu) > 50) {
					applog(LOG_ERR, "GPU still showing activity suggesting a hard hang.");
					applog(LOG_ERR, "Will not attempt to auto-restart it.");
				} else
#endif
				if (opt_restart) {
					applog(LOG_ERR, "Attempting to restart GPU");
					reinit_device(thr->cgpu);
				}
			} else if (now.tv_sec - thr->last.tv_sec > 600 && gpus[i].status == LIFE_SICK) {
				gpus[gpu].status = LIFE_DEAD;
				applog(LOG_ERR, "Device %d not responding for more than 10 minutes, GPU %d declared DEAD!", i, gpu);
				gettimeofday(&thr->sick, NULL);

				gpus[gpu].device_last_not_well = time(NULL);
				gpus[gpu].device_not_well_reason = REASON_DEV_DEAD_IDLE_600;
				gpus[gpu].dev_dead_idle_600_count++;
			} else if (now.tv_sec - thr->sick.tv_sec > 60 &&
				   (gpus[i].status == LIFE_SICK || gpus[i].status == LIFE_DEAD)) {
				/* Attempt to restart a GPU that's sick or dead once every minute */
				gettimeofday(&thr->sick, NULL);
#ifdef HAVE_ADL
				if (adl_active && gpus[gpu].has_adl && gpu_activity(gpu) > 50) {
					/* Again do not attempt to restart a device that may have hard hung */
				} else
#endif
				if (opt_restart)
					reinit_device(thr->cgpu);
			}
		}
#endif
	}

	return NULL;
}

static void log_print_status(struct cgpu_info *cgpu)
{
	char logline[255];

	get_statline(logline, cgpu);
	applog(LOG_WARNING, "%s", logline);
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

	applog(LOG_WARNING, "\nSummary of runtime statistics:\n");
	applog(LOG_WARNING, "Started at %s", datestamp);
	if (total_pools == 1)
		applog(LOG_WARNING, "Pool: %s", pools[0]->rpc_url);
#ifdef WANT_CPUMINE
	if (opt_n_threads)
		applog(LOG_WARNING, "CPU hasher algorithm used: %s", algo_names[opt_algo]);
#endif
	applog(LOG_WARNING, "Runtime: %d hrs : %d mins : %d secs", hours, mins, secs);
	if (total_secs)
		applog(LOG_WARNING, "Average hashrate: %.1f Megahash/s", total_mhashes_done / total_secs);
	applog(LOG_WARNING, "Solved blocks: %d", found_blocks);
	applog(LOG_WARNING, "Queued work requests: %d", total_getworks);
	applog(LOG_WARNING, "Share submissions: %d", total_accepted + total_rejected);
	applog(LOG_WARNING, "Accepted shares: %d", total_accepted);
	applog(LOG_WARNING, "Rejected shares: %d", total_rejected);
	if (total_accepted || total_rejected)
		applog(LOG_WARNING, "Reject ratio: %.1f%%", (double)(total_rejected * 100) / (double)(total_accepted + total_rejected));
	applog(LOG_WARNING, "Hardware errors: %d", hw_errors);
	applog(LOG_WARNING, "Efficiency (accepted / queued): %.0f%%", efficiency);
	applog(LOG_WARNING, "Utility (accepted shares / min): %.2f/min\n", utility);

	applog(LOG_WARNING, "Discarded work due to new blocks: %d", total_discarded);
	applog(LOG_WARNING, "Stale submissions discarded due to new blocks: %d", total_stale);
	applog(LOG_WARNING, "Unable to get work from server occasions: %d", total_go);
	applog(LOG_WARNING, "Work items generated locally: %d", local_work);
	applog(LOG_WARNING, "Submitting work remotely delay occasions: %d", total_ro);
	applog(LOG_WARNING, "New blocks detected on network: %d\n", new_blocks);

	if (total_pools > 1) {
		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			applog(LOG_WARNING, "Pool: %s", pool->rpc_url);
			if (pool->solved)
				applog(LOG_WARNING, "SOLVED %d BLOCK%s!", pool->solved, pool->solved > 1 ? "S" : "");
			applog(LOG_WARNING, " Queued work requests: %d", pool->getwork_requested);
			applog(LOG_WARNING, " Share submissions: %d", pool->accepted + pool->rejected);
			applog(LOG_WARNING, " Accepted shares: %d", pool->accepted);
			applog(LOG_WARNING, " Rejected shares: %d", pool->rejected);
			if (pool->accepted || pool->rejected)
				applog(LOG_WARNING, " Reject ratio: %.1f%%", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));
			efficiency = pool->getwork_requested ? pool->accepted * 100.0 / pool->getwork_requested : 0.0;
			applog(LOG_WARNING, " Efficiency (accepted / queued): %.0f%%", efficiency);

			applog(LOG_WARNING, " Discarded work due to new blocks: %d", pool->discarded_work);
			applog(LOG_WARNING, " Stale submissions discarded due to new blocks: %d", pool->stale_shares);
			applog(LOG_WARNING, " Unable to get work from server occasions: %d", pool->getfail_occasions);
			applog(LOG_WARNING, " Submitting work remotely delay occasions: %d\n", pool->remotefail_occasions);
		}
	}

	applog(LOG_WARNING, "Summary of per device statistics:\n");
	for (i = 0; i < total_devices; ++i)
		log_print_status(devices[i]);

	if (opt_shares)
		applog(LOG_WARNING, "Mined %d accepted shares of %d requested\n", total_accepted, opt_shares);
	fflush(stdout);
	fflush(stderr);
	if (opt_shares > total_accepted)
		applog(LOG_WARNING, "WARNING - Mined only %d shares of %d requested.", total_accepted, opt_shares);
}

static void clean_up(void)
{
#ifdef HAVE_OPENCL
	clear_adl(nDevs);
#endif
#ifdef HAVE_LIBUSB
        libusb_exit(NULL);
#endif

	gettimeofday(&total_tv_end, NULL);
#ifdef HAVE_CURSES
	disable_curses();
#endif
	if (!opt_realquiet && successful_connect)
		print_summary();

	if (opt_n_threads)
		free(cpus);

	curl_global_cleanup();
}

void quit(int status, const char *format, ...)
{
	va_list ap;

	clean_up();

	if (format) {
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
	}
	fprintf(stderr, "\n");
	fflush(stderr);

#if defined(unix)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif

	exit(status);
}

#ifdef HAVE_CURSES
char *curses_input(const char *query)
{
	char *input;

	echo();
	input = malloc(255);
	if (!input)
		quit(1, "Failed to malloc input");
	leaveok(logwin, false);
	wlogprint("%s:\n", query);
	wgetnstr(logwin, input, 255);
	if (!strlen(input))
		strcpy(input, "-1");
	leaveok(logwin, true);
	noecho();
	return input;
}
#endif

int add_pool_details(bool live, char *url, char *user, char *pass)
{
	struct pool *pool;

	if (total_pools == MAX_POOLS)
		return ADD_POOL_MAXIMUM;

	pool = add_pool();

	pool->rpc_url = url;
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);

	/* Test the pool is not idle if we're live running, otherwise
	 * it will be tested separately */
	pool->enabled = POOL_ENABLED;
	if (live && !pool_active(pool, false))
		pool->idle = true;

	return ADD_POOL_OK;
}

#ifdef HAVE_CURSES
static bool input_pool(bool live)
{
	char *url = NULL, *user = NULL, *pass = NULL;
	bool ret = false;

	immedok(logwin, true);
	if (total_pools == MAX_POOLS) {
		wlogprint("Reached maximum number of pools.\n");
		goto out;
	}
	wlogprint("Input server details.\n");

	url = curses_input("URL");
	if (!url)
		goto out;

	if (strncmp(url, "http://", 7) &&
	    strncmp(url, "https://", 8)) {
		char *httpinput;

		httpinput = malloc(255);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		strcpy(httpinput, "http://");
		strncat(httpinput, url, 248);
		free(url);
		url = httpinput;
	}

	user = curses_input("Username");
	if (!user)
		goto out;

	pass = curses_input("Password");
	if (!pass)
		goto out;

	ret = (add_pool_details(live, url, user, pass) == ADD_POOL_OK);
out:
	immedok(logwin, false);

	if (!ret) {
		if (url)
			free(url);
		if (user)
			free(user);
		if (pass)
			free(pass);
	}
	return ret;
}
#endif

#if defined(unix)
	static void fork_monitor()
	{
		// Make a pipe: [readFD, writeFD]
		int pfd[2];
		int r = pipe(pfd);
		if (r<0) {
			perror("pipe - failed to create pipe for --monitor");
			exit(1);
		}

		// Make stderr write end of pipe
		fflush(stderr);
		r = dup2(pfd[1], 2);
		if (r<0) {
			perror("dup2 - failed to alias stderr to write end of pipe for --monitor");
			exit(1);
		}
		r = close(pfd[1]);
		if (r<0) {
			perror("close - failed to close write end of pipe for --monitor");
			exit(1);
		}

		// Don't allow a dying monitor to kill the main process
		sighandler_t sr0 = signal(SIGPIPE, SIG_IGN);
		sighandler_t sr1 = signal(SIGPIPE, SIG_IGN);
		if (SIG_ERR==sr0 || SIG_ERR==sr1) {
			perror("signal - failed to edit signal mask for --monitor");
			exit(1);
		}

		// Fork a child process
		forkpid = fork();
		if (forkpid<0) {
			perror("fork - failed to fork child process for --monitor");
			exit(1);
		}

		// Child: launch monitor command
		if (0==forkpid) {
			// Make stdin read end of pipe
			r = dup2(pfd[0], 0);
			if (r<0) {
				perror("dup2 - in child, failed to alias read end of pipe to stdin for --monitor");
				exit(1);
			}
			close(pfd[0]);
			if (r<0) {
				perror("close - in child, failed to close read end of  pipe for --monitor");
				exit(1);
			}

			// Launch user specified command
			execl("/bin/bash", "/bin/bash", "-c", opt_stderr_cmd, (char*)NULL);
			perror("execl - in child failed to exec user specified command for --monitor");
			exit(1);
		}

		// Parent: clean up unused fds and bail
		r = close(pfd[0]);
		if (r<0) {
			perror("close - failed to close read end of pipe for --monitor");
			exit(1);
		}
	}
#endif // defined(unix)

#ifdef HAVE_CURSES
void enable_curses(void) {
	int x,y;

	lock_curses();
	if (curses_active) {
		unlock_curses();
		return;
	}

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
	curses_active = true;
	statusy = logstart;
	unlock_curses();
}
#endif

/* TODO: fix need a dummy CPU device_api even if no support for CPU mining */
#ifndef WANT_CPUMINE
struct device_api cpu_api;
struct device_api cpu_api = {
	.name = "CPU",
};
#endif

#ifdef USE_BITFORCE
extern struct device_api bitforce_api;
#endif

#ifdef USE_ICARUS
extern struct device_api icarus_api;
#endif

#ifdef USE_ZTEX
extern struct device_api ztex_api;
#endif


static int cgminer_id_count = 0;

void enable_device(struct cgpu_info *cgpu)
{
	cgpu->deven = DEV_ENABLED;
	devices[cgpu->cgminer_id = cgminer_id_count++] = cgpu;
	mining_threads += cgpu->threads;
#ifdef HAVE_OPENCL
	if (cgpu->api == &opencl_api) {
		gpu_threads += cgpu->threads;
	}
#endif
}

struct _cgpu_devid_counter {
	char name[4];
	int lastid;
	UT_hash_handle hh;
};

bool add_cgpu(struct cgpu_info*cgpu)
{
	static struct _cgpu_devid_counter *devids = NULL;
	struct _cgpu_devid_counter *d;
	
	HASH_FIND_STR(devids, cgpu->api->name, d);
	if (d)
		cgpu->device_id = ++d->lastid;
	else
	{
		d = malloc(sizeof(*d));
		memcpy(d->name, cgpu->api->name, sizeof(d->name));
		cgpu->device_id = d->lastid = 0;
		HASH_ADD_STR(devids, name, d);
	}
	devices[total_devices++] = cgpu;
	return true;
}

int main(int argc, char *argv[])
{
	struct block *block, *tmpblock;
	struct work *work, *tmpwork;
	bool pools_active = false;
	struct sigaction handler;
	struct thr_info *thr;
	unsigned int k;
	int i, j;

	/* This dangerous functions tramples random dynamically allocated
	 * variables so do it before anything at all */
	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		quit(1, "Failed to curl_global_init");

	initial_args = malloc(sizeof(char *) * (argc + 1));
	for  (i = 0; i < argc; i++)
		initial_args[i] = strdup(argv[i]);
	initial_args[argc] = NULL;
#ifdef HAVE_LIBUSB
        libusb_init(NULL);
#endif

	mutex_init(&hash_lock);
	mutex_init(&qd_lock);
#ifdef HAVE_CURSES
	mutex_init(&curses_lock);
#endif
	mutex_init(&control_lock);
	mutex_init(&sharelog_lock);
	mutex_init(&ch_lock);
	rwlock_init(&blk_lock);
	rwlock_init(&netacc_lock);

	mutex_init(&lp_lock);
	if (unlikely(pthread_cond_init(&lp_cond, NULL)))
		quit(1, "Failed to pthread_cond_init lp_cond");

	sprintf(packagename, "%s %s", PACKAGE, VERSION);

#ifdef WANT_CPUMINE
	init_max_name_len();
#endif

	handler.sa_handler = &sighandler;
	handler.sa_flags = 0;
	sigemptyset(&handler.sa_mask);
	sigaction(SIGTERM, &handler, &termhandler);
	sigaction(SIGINT, &handler, &inthandler);

	opt_kernel_path = alloca(PATH_MAX);
	strcpy(opt_kernel_path, CGMINER_PREFIX);
	cgminer_path = alloca(PATH_MAX);
	strcpy(cgminer_path, dirname(argv[0]));
	strcat(cgminer_path, "/");
#ifdef WANT_CPUMINE
	// Hack to make cgminer silent when called recursively on WIN32
	int skip_to_bench = 0;
	#if defined(WIN32)
		char buf[32];
		if (GetEnvironmentVariable("BFGMINER_BENCH_ALGO", buf, 16))
			skip_to_bench = 1;
		if (GetEnvironmentVariable("CGMINER_BENCH_ALGO", buf, 16))
			skip_to_bench = 1;
	#endif // defined(WIN32)
#endif

	devcursor = 8;
	logstart = devcursor + 1;
	logcursor = logstart + 1;

	block = calloc(sizeof(struct block), 1);
	if (unlikely(!block))
		quit (1, "main OOM");
	for (i = 0; i < 36; i++)
		strcat(block->hash, "0");
	HASH_ADD_STR(blocks, hash, block);
	strcpy(current_block, block->hash);

	INIT_LIST_HEAD(&scan_devices);

	memset(gpus, 0, sizeof(gpus));
	for (i = 0; i < MAX_GPUDEVICES; i++)
		gpus[i].dynamic = true;

	memset(devices, 0, sizeof(devices));

	/* parse command line */
	opt_register_table(opt_config_table,
			   "Options for both config file and command line");
	opt_register_table(opt_cmdline_table,
			   "Options for command line only");

	opt_parse(&argc, argv, applog_and_exit);
	if (argc != 1)
		quit(1, "Unexpected extra commandline arguments");

	if (!config_loaded)
		load_default_config();

	if (opt_benchmark) {
		struct pool *pool;

		want_longpoll = false;
		pool = calloc(sizeof(struct pool), 1);
		pool->pool_no = 0;
		pools[total_pools++] = pool;
		pthread_mutex_init(&pool->pool_lock, NULL);
		pool->rpc_url = malloc(255);
		strcpy(pool->rpc_url, "Benchmark");
		pool->rpc_user = pool->rpc_url;
		pool->rpc_pass = pool->rpc_url;
		pool->enabled = POOL_ENABLED;
		pool->idle = false;
		successful_connect = true;
	}

#ifdef HAVE_CURSES
	if (opt_realquiet || devices_enabled == -1)
		use_curses = false;

	if (use_curses)
		enable_curses();
#endif

	applog(LOG_WARNING, "Started %s", packagename);
	if (cnfbuf) {
		applog(LOG_NOTICE, "Loaded configuration file %s", cnfbuf);
		switch (fileconf_load) {
			case 0:
				applog(LOG_WARNING, "Fatal JSON error in configuration file.");
				applog(LOG_WARNING, "Configuration file could not be used.");
				break;
			case -1:
				applog(LOG_WARNING, "Error in configuration file, partially loaded.");
				if (use_curses)
					applog(LOG_WARNING, "Start BFGMiner with -T to see what failed to load.");
				break;
			default:
				break;
		}
		free(cnfbuf);
		cnfbuf = NULL;
	}

	strcat(opt_kernel_path, "/");

	if (want_per_device_stats)
		opt_log_output = true;

#ifdef WANT_CPUMINE
	if (0<=opt_bench_algo) {
		double rate = bench_algo_stage3(opt_bench_algo);
		if (!skip_to_bench) {
			printf("%.5f (%s)\n", rate, algo_names[opt_bench_algo]);
		} else {
			// Write result to shared memory for parent
			#if defined(WIN32)
				char unique_name[64];
				if (GetEnvironmentVariable("BFGMINER_SHARED_MEM", unique_name, 32) || GetEnvironmentVariable("CGMINER_SHARED_MEM", unique_name, 32)) {
					HANDLE map_handle = CreateFileMapping(
						INVALID_HANDLE_VALUE,   // use paging file
						NULL,                   // default security attributes
						PAGE_READWRITE,         // read/write access
						0,                      // size: high 32-bits
						4096,			// size: low 32-bits
						unique_name		// name of map object
					);
					if (NULL!=map_handle) {
						void *shared_mem = MapViewOfFile(
							map_handle,	// object to map view of
							FILE_MAP_WRITE, // read/write access
							0,              // high offset:  map from
							0,              // low offset:   beginning
							0		// default: map entire file
						);
						if (NULL!=shared_mem)
							CopyMemory(shared_mem, &rate, sizeof(rate));
						(void)UnmapViewOfFile(shared_mem);
					}
					(void)CloseHandle(map_handle);
				}
			#endif
		}
		exit(0);
	}
#endif

#ifdef HAVE_OPENCL
	if (!opt_nogpu)
		opencl_api.api_detect();
#endif

#ifdef USE_ICARUS
	icarus_api.api_detect();
#endif

#ifdef USE_BITFORCE
	bitforce_api.api_detect();
#endif

#ifdef USE_ZTEX
	ztex_api.api_detect();
#endif

#ifdef WANT_CPUMINE
	cpu_api.api_detect();
#endif

	if (devices_enabled == -1) {
		applog(LOG_ERR, "Devices detected:");
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			if (cgpu->name)
				applog(LOG_ERR, " %2d. %s %d: %s (driver: %s)", i, cgpu->api->name, cgpu->device_id, cgpu->name, cgpu->api->dname);
			else
				applog(LOG_ERR, " %2d. %s %d (driver: %s)", i, cgpu->api->name, cgpu->device_id, cgpu->api->dname);
		}
		quit(0, "%d devices listed", total_devices);
	}

	mining_threads = 0;
	gpu_threads = 0;
	if (devices_enabled) {
		for (i = 0; i < (int)(sizeof(devices_enabled) * 8) - 1; ++i) {
			if (devices_enabled & (1 << i)) {
				if (i >= total_devices)
					quit (1, "Command line options set a device that doesn't exist");
				enable_device(devices[i]);
			} else if (i < total_devices) {
				if (opt_removedisabled) {
					if (devices[i]->api == &cpu_api)
						--opt_n_threads;
				} else {
					enable_device(devices[i]);
				}
				devices[i]->deven = DEV_DISABLED;
			}
		}
		total_devices = cgminer_id_count;
	} else {
		for (i = 0; i < total_devices; ++i)
			enable_device(devices[i]);
	}

	if (!total_devices)
		quit(1, "All devices disabled, cannot mine!");

	load_temp_cutoffs();

	for (i = 0; i < total_devices; ++i)
		devices[i]->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

	logstart += total_devices;
	logcursor = logstart + 1;

#ifdef HAVE_CURSES
	check_winsizes();
#endif

	if (!total_pools) {
		applog(LOG_WARNING, "Need to specify at least one pool server.");
#ifdef HAVE_CURSES
		if (!use_curses || !input_pool(false))
#endif
			quit(1, "Pool setup failed");
	}

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

		if (!pool->rpc_userpass) {
			if (!pool->rpc_user || !pool->rpc_pass)
				quit(1, "No login credentials supplied for pool %u %s", i, pool->rpc_url);
			pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
			if (!pool->rpc_userpass)
				quit(1, "Failed to malloc userpass");
			sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);
		} else {
			pool->rpc_user = malloc(strlen(pool->rpc_userpass) + 1);
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
		openlog(PACKAGE, LOG_PID, LOG_USER);
#endif

	#if defined(unix)
		if (opt_stderr_cmd)
			fork_monitor();
	#endif // defined(unix)

	total_threads = mining_threads + 7;
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

	stage_thr_id = mining_threads + 1;
	thr = &thr_info[stage_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "Failed to tq_new");
	/* start stage thread */
	if (thr_info_create(thr, NULL, stage_thread, thr))
		quit(1, "stage thread create failed");
	pthread_detach(thr->pth);

	/* Create a unique get work queue */
	getq = tq_new();
	if (!getq)
		quit(1, "Failed to create getq");
	/* We use the getq mutex as the staged lock */
	stgd_lock = &getq->mutex;

	if (opt_benchmark)
		goto begin_bench;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool  = pools[i];

		pool->enabled = POOL_ENABLED;
		pool->idle = true;
	}

	applog(LOG_NOTICE, "Probing for an alive pool");
	do {
		/* Look for at least one active pool before starting */
		for (i = 0; i < total_pools; i++) {
			struct pool *pool  = pools[i];
			if (pool_active(pool, false)) {
				if (!currentpool)
					currentpool = pool;
				applog(LOG_INFO, "Pool %d %s active", pool->pool_no, pool->rpc_url);
				pools_active = true;
				break;
			} else {
				if (pool == currentpool)
					currentpool = NULL;
				applog(LOG_WARNING, "Unable to get work from pool %d %s", pool->pool_no, pool->rpc_url);
			}
		}

		if (!pools_active) {
			applog(LOG_ERR, "No servers were found that could be used to get work from.");
			applog(LOG_ERR, "Please check the details from the list below of the servers you have input");
			applog(LOG_ERR, "Most likely you have input the wrong URL, forgotten to add a port, or have not set up workers");
			for (i = 0; i < total_pools; i++) {
				struct pool *pool;

				pool = pools[i];
				applog(LOG_WARNING, "Pool: %d  URL: %s  User: %s  Password: %s",
				       i, pool->rpc_url, pool->rpc_user, pool->rpc_pass);
			}
#ifdef HAVE_CURSES
			if (use_curses) {
				halfdelay(150);
				applog(LOG_ERR, "Press any key to exit, or BFGMiner will try again in 15s.");
				if (getch() != ERR)
					quit(0, "No servers could be used! Exiting.");
				nocbreak();
			} else
#endif
				quit(0, "No servers could be used! Exiting.");
		}
	} while (!pools_active);

begin_bench:
	total_mhashes_done = 0;
	for (i = 0; i < total_devices; i++) {
		struct cgpu_info *cgpu = devices[i];

		cgpu->rolling = cgpu->total_mhashes = 0;
	}
	
	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);
	get_datestamp(datestamp, &total_tv_start);

#ifndef HAVE_OPENCL
	opt_g_threads = 0;
#endif

	// Start threads
	k = 0;
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];

		for (j = 0; j < cgpu->threads; ++j, ++k) {
			thr = &thr_info[k];
			thr->id = k;
			thr->cgpu = cgpu;
			thr->device_thread = j;
			thr->work_restart_fd = thr->_work_restart_fd_w = -1;

			thr->q = tq_new();
			if (!thr->q)
				quit(1, "tq_new failed in starting %s%d mining thread (#%d)", cgpu->api->name, cgpu->device_id, i);

			/* Enable threads for devices set not to mine but disable
			 * their queue in case we wish to enable them later */
			if (cgpu->deven != DEV_DISABLED) {
				applog(LOG_DEBUG, "Pushing ping to thread %d", thr->id);

				tq_push(thr->q, &ping);
			}

			if (cgpu->api->thread_prepare && !cgpu->api->thread_prepare(thr))
				continue;

			if (!thr->work_restart_fd)
			{
				int pipefd[2];
				if (!pipe(pipefd))
				{
					thr->work_restart_fd = pipefd[0];
					thr->_work_restart_fd_w = pipefd[1];
				}
				else
					thr->work_restart_fd = -1;
			}

			thread_reportout(thr);

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				quit(1, "thread %d create failed", thr->id);

			cgpu->thread = thr;
		}
	}

#ifdef HAVE_OPENCL
	applog(LOG_INFO, "%d gpu miner threads started", gpu_threads);
	for (i = 0; i < nDevs; i++)
		pause_dynamic_threads(i);
#endif

#ifdef WANT_CPUMINE
	applog(LOG_INFO, "%d cpu miner threads started, "
		"using SHA256 '%s' algorithm.",
		opt_n_threads,
		algo_names[opt_algo]);
#endif

	gettimeofday(&total_tv_start, NULL);
	gettimeofday(&total_tv_end, NULL);

	watchpool_thr_id = mining_threads + 2;
	thr = &thr_info[watchpool_thr_id];
	/* start watchpool thread */
	if (thr_info_create(thr, NULL, watchpool_thread, NULL))
		quit(1, "watchpool thread create failed");
	pthread_detach(thr->pth);

	watchdog_thr_id = mining_threads + 3;
	thr = &thr_info[watchdog_thr_id];
	/* start watchdog thread */
	if (thr_info_create(thr, NULL, watchdog_thread, NULL))
		quit(1, "watchdog thread create failed");
	pthread_detach(thr->pth);

	/* Create reinit gpu thread */
	gpur_thr_id = mining_threads + 4;
	thr = &thr_info[gpur_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "tq_new failed for gpur_thr_id");
	if (thr_info_create(thr, NULL, reinit_gpu, thr))
		quit(1, "reinit_gpu thread create failed");

	/* Create API socket thread */
	api_thr_id = mining_threads + 5;
	thr = &thr_info[api_thr_id];
	if (thr_info_create(thr, NULL, api_thread, thr))
		quit(1, "API thread create failed");
	pthread_detach(thr->pth);

#ifdef HAVE_CURSES
	/* Create curses input thread for keyboard input. Create this last so
	 * that we know all threads are created since this can call kill_work
	 * to try and shut down ll previous threads. */
	input_thr_id = mining_threads + 6;
	thr = &thr_info[input_thr_id];
	if (thr_info_create(thr, NULL, input_thread, thr))
		quit(1, "input thread create failed");
	pthread_detach(thr->pth);
#endif

	/* main loop - simply wait for workio thread to exit. This is not the
	 * normal exit path and only occurs should the workio_thread die
	 * unexpectedly */
	pthread_join(thr_info[work_thr_id].pth, NULL);
	applog(LOG_INFO, "workio thread dead, exiting.");

	clean_up();

	/* Not really necessary, but let's clean this up too anyway */
	HASH_ITER(hh, staged_work, work, tmpwork) {
		HASH_DEL(staged_work, work);
		free_work(work);
	}
	HASH_ITER(hh, blocks, block, tmpblock) {
		HASH_DEL(blocks, block);
		free(block);
	}

#if defined(unix)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif

	return 0;
}
