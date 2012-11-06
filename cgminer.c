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
#include "scrypt.h"

#if defined(unix)
	#include <errno.h>
	#include <fcntl.h>
	#include <sys/wait.h>
#endif

#if defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_MODMINER)
#	define USE_FPGA
#	define USE_FPGA_SERIAL
#elif defined(USE_ZTEX)
#	define USE_FPGA
#endif

enum workio_commands {
	WC_GET_WORK,
	WC_SUBMIT_WORK,
};

struct workio_cmd {
	enum workio_commands	cmd;
	struct thr_info		*thr;
	struct work		*work;
	struct pool		*pool;
};

struct strategies strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
	{ "Balance" },
};

static char packagename[255];

bool opt_protocol;
static bool opt_benchmark;
bool have_longpoll;
bool want_per_device_stats;
bool use_syslog;
bool opt_quiet;
bool opt_realquiet;
bool opt_loginput;
bool opt_compact;
const int opt_cutofftemp = 95;
int opt_log_interval = 5;
int opt_queue = 1;
int opt_scantime = 60;
int opt_expiry = 120;
int opt_bench_algo = -1;
static const bool opt_time = true;
unsigned long long global_hashrate;

#ifdef HAVE_OPENCL
int opt_dynamic_interval = 7;
int nDevs;
int opt_g_threads = 2;
int gpu_threads;
#ifdef USE_SCRYPT
bool opt_scrypt;
#endif
#endif
bool opt_restart = true;
static bool opt_nogpu;

struct list_head scan_devices;
static signed int devices_enabled;
static bool opt_removedisabled;
int total_devices;
struct cgpu_info **devices;
bool have_opencl;
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
bool opt_fail_only;
static bool opt_fix_protocol;
bool opt_autofan;
bool opt_autoengine;
bool opt_noadl;
char *opt_api_allow = NULL;
char *opt_api_groups;
char *opt_api_description = PACKAGE_STRING;
int opt_api_port = 4028;
bool opt_api_listen;
bool opt_api_network;
bool opt_delaynet;
bool opt_disable_pool = true;
char *opt_icarus_options = NULL;
char *opt_icarus_timing = NULL;
bool opt_worktime;

char *opt_kernel_path;
char *cgminer_path;

#if defined(USE_BITFORCE)
bool opt_bfl_noncerange;
#endif
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
pthread_mutex_t console_lock;
pthread_mutex_t ch_lock;
static pthread_rwlock_t blk_lock;
static pthread_mutex_t sshare_lock;

pthread_rwlock_t netacc_lock;

static pthread_mutex_t lp_lock;
static pthread_cond_t lp_cond;

pthread_mutex_t restart_lock;
pthread_cond_t restart_cond;

double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;

pthread_mutex_t control_lock;

int hw_errors;
int total_accepted, total_rejected, total_diff1;
int total_getworks, total_stale, total_discarded;
double total_diff_accepted, total_diff_rejected, total_diff_stale;
static int total_queued, staged_rollable;
unsigned int new_blocks;
static unsigned int work_block;
unsigned int found_blocks;

unsigned int local_work;
unsigned int total_go, total_ro;

struct pool **pools;
static struct pool *currentpool = NULL;

int total_pools, enabled_pools;
enum pool_strategy pool_strategy = POOL_FAILOVER;
int opt_rotate_period;
static int total_urls, total_users, total_passes, total_userpasses;

static
#ifndef HAVE_CURSES
const
#endif
bool curses_active;

static char current_block[40];
static char *current_hash;
char *current_fullhash;
static char datestamp[40];
static char blocktime[30];
struct timeval block_timeval;
static char best_share[8] = "0";
static uint64_t best_diff = 0;

struct block {
	char hash[40];
	UT_hash_handle hh;
	int block_no;
};

static struct block *blocks = NULL;


int swork_id;

/* For creating a hash database of stratum shares submitted that have not had
 * a response yet */
struct stratum_share {
	UT_hash_handle hh;
	bool block;
	struct work work;
	int id;
};

static struct stratum_share *stratum_shares = NULL;

char *opt_socks_proxy = NULL;

static const char def_conf[] = "cgminer.conf";
static char *default_config;
static bool config_loaded;
static int include_count;
#define JSON_INCLUDE_CONF "include"
#define JSON_LOAD_ERROR "JSON decode of file '%s' failed\n %s"
#define JSON_LOAD_ERROR_LEN strlen(JSON_LOAD_ERROR)
#define JSON_MAX_DEPTH 10
#define JSON_MAX_DEPTH_ERR "Too many levels of JSON includes (limit 10) or a loop"

#if defined(unix)
	static char *opt_stderr_cmd = NULL;
	static int forkpid;
#endif // defined(unix)

bool ping = true;

struct sigaction termhandler, inthandler, segvhandler, bushandler, illhandler;

struct thread_q *getq;

static int total_work;
struct work *staged_work = NULL;

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
	char *target, *hash, *data;
	struct cgpu_info *cgpu;
	unsigned long int t;
	struct pool *pool;
	int thr_id, rv;
	char s[1024];
	size_t ret;

	if (!sharelog_file)
		return;

	thr_id = work->thr_id;
	cgpu = thr_info[thr_id].cgpu;
	pool = work->pool;
	t = (unsigned long int)(work->tv_work_found.tv_sec);
	target = bin2hex(work->target, sizeof(work->target));
	hash = bin2hex(work->hash, sizeof(work->hash));
	data = bin2hex(work->data, sizeof(work->data));

	// timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
	rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s%u,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->api->name, cgpu->device_id, thr_id, hash, data);
	free(target);
	free(hash);
	free(data);
	if (rv >= (int)(sizeof(s)))
		s[sizeof(s) - 1] = '\0';
	else if (rv < 0) {
		applog(LOG_ERR, "sharelog printf error");
		return;
	}

	mutex_lock(&sharelog_lock);
	ret = fwrite(s, rv, 1, sharelog_file);
	fflush(sharelog_file);
	mutex_unlock(&sharelog_lock);
	if (ret != 1)
		applog(LOG_ERR, "sharelog fwrite error");
}

static char *getwork_req = "{\"method\": \"getwork\", \"params\": [], \"id\":0}\n";

static char *gbt_req = "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": [{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"]}]}\n";

/* Return value is ignored if not called from add_pool_details */
struct pool *add_pool(void)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to malloc pool in add_pool");
	pool->pool_no = pool->prio = total_pools;
	pools = realloc(pools, sizeof(struct pool *) * (total_pools + 2));
	pools[total_pools++] = pool;
	mutex_init(&pool->pool_lock);
	if (unlikely(pthread_cond_init(&pool->cr_cond, NULL)))
		quit(1, "Failed to pthread_cond_init in add_pool");
	mutex_init(&pool->stratum_lock);
	mutex_init(&pool->gbt_lock);
	INIT_LIST_HEAD(&pool->curlring);

	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;

	pool->rpc_req = getwork_req;
	pool->rpc_proxy = NULL;

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

#ifdef USE_FPGA_SERIAL
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

static char *set_balance(enum pool_strategy *strategy)
{
	*strategy = POOL_BALANCE;
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

/* Detect that url is for a stratum protocol either via the presence of
 * stratum+tcp or by detecting a stratum server response */
bool detect_stratum(struct pool *pool, char *url)
{
	if (!extract_sockaddr(pool, url))
		return false;

	if (opt_scrypt)
		return false;

	if (!strncasecmp(url, "stratum+tcp://", 14)) {
		pool->has_stratum = true;
		pool->stratum_url = pool->sockaddr_url;
		return true;
	}

	return false;
}

static char *set_url(char *arg)
{
	struct pool *pool;

	total_urls++;
	if (total_urls > total_pools)
		add_pool();
	pool = pools[total_urls - 1];

	arg = get_proxy(arg, pool);

	if (detect_stratum(pool, arg)) {
		if (!pool->rpc_url)
			pool->rpc_url = strdup(pool->stratum_url);
		return NULL;
	}

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
	char *updup;

	if (total_users || total_passes)
		return "Use only user + pass or userpass, but not both";
	total_userpasses++;
	if (total_userpasses > total_pools)
		add_pool();

	pool = pools[total_userpasses - 1];
	updup = strdup(arg);
	opt_set_charp(arg, &pool->rpc_userpass);
	pool->rpc_user = strtok(updup, ":");
	if (!pool->rpc_user)
		return "Failed to find : delimited user info";
	pool->rpc_pass = strtok(NULL, ":");
	if (!pool->rpc_pass)
		return "Failed to find : delimited pass info";

	return NULL;
}

static char *enable_debug(bool *flag)
{
	*flag = true;
	/* Turn on verbose output, too. */
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

static char* set_sharelog(char *arg)
{
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
	} else {
		for (i = device; i < total_devices; ++i) {
			if (!devices[i]->cutofftemp)
				devices[i]->cutofftemp = opt_cutofftemp;
		}
		return;
	}
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

static char *set_api_groups(const char *arg)
{
	opt_set_charp(arg, &opt_api_groups);

	return NULL;
}

static char *set_api_description(const char *arg)
{
	opt_set_charp(arg, &opt_api_description);

	return NULL;
}

#ifdef USE_ICARUS
static char *set_icarus_options(const char *arg)
{
	opt_set_charp(arg, &opt_icarus_options);

	return NULL;
}

static char *set_icarus_timing(const char *arg)
{
	opt_set_charp(arg, &opt_icarus_timing);

	return NULL;
}
#endif

static char *set_null(const char __maybe_unused *arg)
{
	return NULL;
}

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
		     "Allow API access only to the given list of [G:]IP[/Prefix] addresses[/subnets]"),
	OPT_WITH_ARG("--api-description",
		     set_api_description, NULL, NULL,
		     "Description placed in the API status header, default: cgminer version"),
	OPT_WITH_ARG("--api-groups",
		     set_api_groups, NULL, NULL,
		     "API one letter groups G:cmd:cmd[,P:cmd:*...] defining the cmds a groups can use"),
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
	OPT_WITHOUT_ARG("--balance",
		     set_balance, &pool_strategy,
		     "Change multipool strategy from failover to even share balance"),
	OPT_WITHOUT_ARG("--benchmark",
			opt_set_bool, &opt_benchmark,
			"Run cgminer in benchmark mode - produces no shares"),
#if defined(USE_BITFORCE)
	OPT_WITHOUT_ARG("--bfl-range",
			opt_set_bool, &opt_bfl_noncerange,
			"Use nonce range on bitforce devices if supported"),
#endif
#ifdef WANT_CPUMINE
	OPT_WITH_ARG("--bench-algo|-b",
		     set_int_0_to_9999, opt_show_intval, &opt_bench_algo,
		     opt_hidden),
#endif
#ifdef HAVE_CURSES
	OPT_WITHOUT_ARG("--compact",
			opt_set_bool, &opt_compact,
			"Use compact display without per device statistics"),
#endif
#ifdef WANT_CPUMINE
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
#if defined(WANT_CPUMINE) && (defined(HAVE_OPENCL) || defined(USE_FPGA))
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
	OPT_WITHOUT_ARG("--fix-protocol",
			opt_set_bool, &opt_fix_protocol,
			"Do not redirect to a different getwork protocol (eg. stratum)"),
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
#ifdef USE_SCRYPT
	OPT_WITH_ARG("--lookup-gap",
		     set_lookup_gap, NULL, NULL,
		     "Set GPU lookup gap for scrypt mining, comma separated"),
#endif
	OPT_WITH_ARG("--intensity|-I",
		     set_intensity, NULL, NULL,
		     "Intensity of GPU scanning (d or " _MIN_INTENSITY_STR " -> " _MAX_INTENSITY_STR ", default: d to maintain desktop interactivity)"),
#endif
#if defined(HAVE_OPENCL) || defined(HAVE_MODMINER)
	OPT_WITH_ARG("--kernel-path|-K",
		     opt_set_charp, opt_show_charp, &opt_kernel_path,
	             "Specify a path to where bitstream and kernel files are"),
#endif
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--kernel|-k",
		     set_kernel, NULL, NULL,
		     "Override sha256 kernel to use (diablo, poclbm, phatk or diakgcn) - one value or comma separated"),
#endif
#ifdef USE_ICARUS
	OPT_WITH_ARG("--icarus-options",
		     set_icarus_options, NULL, NULL,
		     opt_hidden),
	OPT_WITH_ARG("--icarus-timing",
		     set_icarus_timing, NULL, NULL,
		     opt_hidden),
#endif
	OPT_WITHOUT_ARG("--load-balance",
		     set_loadbalance, &pool_strategy,
		     "Change multipool strategy from failover to efficiency based balance"),
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
	OPT_WITHOUT_ARG("--no-pool-disable",
			opt_set_invbool, &opt_disable_pool,
			"Do not automatically disable pools that continually reject shares"),
	OPT_WITHOUT_ARG("--no-restart",
			opt_set_invbool, &opt_restart,
#ifdef HAVE_OPENCL
			"Do not attempt to restart GPUs that hang or cgminer if it crashes"
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
	OPT_WITH_ARG("--retries",
		     set_null, NULL, NULL,
		     opt_hidden),
	OPT_WITH_ARG("--retry-pause",
		     set_null, NULL, NULL,
		     opt_hidden),
	OPT_WITH_ARG("--rotate",
		     set_rotate, opt_show_intval, &opt_rotate_period,
		     "Change multipool strategy from failover to regularly rotate at N minutes"),
	OPT_WITHOUT_ARG("--round-robin",
		     set_rr, &pool_strategy,
		     "Change multipool strategy from failover to round robin on failure"),
#ifdef USE_FPGA_SERIAL
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
#ifdef USE_SCRYPT
	OPT_WITHOUT_ARG("--scrypt",
			opt_set_bool, &opt_scrypt,
			"Use the scrypt algorithm for mining (litecoin only)"),
	OPT_WITH_ARG("--shaders",
		     set_shaders, NULL, NULL,
		     "GPU shaders per card for tuning scrypt, comma separated"),
#endif
	OPT_WITH_ARG("--sharelog",
		     set_sharelog, NULL, NULL,
		     "Append share log to file"),
	OPT_WITH_ARG("--shares",
		     opt_set_intval, NULL, &opt_shares,
		     "Quit after mining N shares (default: unlimited)"),
	OPT_WITH_ARG("--socks-proxy",
		     opt_set_charp, NULL, &opt_socks_proxy,
		     "Set socks4 proxy (host:port)"),
#ifdef HAVE_SYSLOG_H
	OPT_WITHOUT_ARG("--syslog",
			opt_set_bool, &use_syslog,
			"Use system log for output messages (default: standard error)"),
#endif
#if defined(HAVE_ADL) || defined(USE_BITFORCE) || defined(USE_MODMINER)
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
#ifdef USE_SCRYPT
	OPT_WITH_ARG("--thread-concurrency",
		     set_thread_concurrency, NULL, NULL,
		     "Set GPU thread concurrency for scrypt mining, comma separated"),
#endif
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
	OPT_WITHOUT_ARG("--worktime",
			opt_set_bool, &opt_worktime,
			"Display extra work time debug information"),
	OPT_WITH_ARG("--pools",
			opt_set_bool, NULL, NULL, opt_hidden),
	OPT_ENDTABLE
};

static char *load_config(const char *arg, void __maybe_unused *unused);

static int fileconf_load;

static char *parse_config(json_t *config, bool fileconf)
{
	static char err_buf[200];
	struct opt_table *opt;
	json_t *val;

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
		json_error = malloc(JSON_LOAD_ERROR_LEN + strlen(arg) + strlen(err.text));
		if (!json_error)
			quit(1, "Malloc failure in json error");

		sprintf(json_error, JSON_LOAD_ERROR, arg, err.text);
		return json_error;
	}

	config_loaded = true;

	/* Parse the config now, so we can override it.  That can keep pointers
	 * so don't free config object. */
	return parse_config(config, true);
}

static char *set_default_config(const char *arg)
{
	opt_set_charp(arg, &default_config);

	return NULL;
}

void default_save_file(char *filename);

static void load_default_config(void)
{
	cnfbuf = malloc(PATH_MAX);

	default_save_file(cnfbuf);

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
#ifdef USE_MODMINER
		"modminer "
#endif
#ifdef USE_ZTEX
		"ztex "
#endif
#ifdef USE_SCRYPT
		"scrypt "
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
	OPT_WITH_ARG("--default-config",
		     set_default_config, NULL, NULL,
		     "Specify the filename of the default config file\n"
		     "Loaded at start and used when saving without a name."),
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

static void calc_midstate(struct work *work)
{
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

/* Generate a GBT coinbase from the existing GBT variables stored. Must be
 * entered under gbt_lock */
static void __build_gbt_coinbase(struct pool *pool)
{
	int cbt_len, cal_len, orig_len;
	unsigned char *coinbase;
	uint8_t *extra_len;

	cbt_len = strlen(pool->coinbasetxn) / 2;
	pool->coinbase_len = cbt_len + 4;
	/* We add 4 bytes of extra data corresponding to nonce2 of stratum */
	cal_len = pool->coinbase_len + 1;
	if (cal_len % 4)
		cal_len += 4 - (cal_len % 4);
	coinbase = calloc(cal_len, 1);
	hex2bin(coinbase, pool->coinbasetxn, 42);
	extra_len = (uint8_t *)(coinbase + 41);
	orig_len = *extra_len;
	hex2bin(coinbase + 42, pool->coinbasetxn + 84, orig_len);
	memcpy(coinbase + 42 + orig_len, &pool->nonce2, 4);
	*extra_len += 4;
	hex2bin(coinbase + 42 + *extra_len, pool->coinbasetxn + 84 + (orig_len * 2), cbt_len - orig_len - 42);
	pool->nonce2++;
	free(pool->gbt_coinbase);
	pool->gbt_coinbase = coinbase;
}

static void gen_hash(unsigned char *data, unsigned char *hash, int len);

/* Process transactions with GBT by storing the binary value of the first
 * transaction, and the hashes of the remaining transactions since these
 * remain constant with an altered coinbase when generating work. Must be
 * entered under gbt_lock */
static bool __build_gbt_txns(struct pool *pool, json_t *res_val)
{
	json_t *txn_array;
	bool ret = false;
	int i, cal_len;

	free(pool->txn_hashes);
	pool->txn_hashes = NULL;
	pool->gbt_txns = 0;

	txn_array = json_object_get(res_val, "transactions");
	if (!json_is_array(txn_array))
		goto out;

	ret = true;
	pool->gbt_txns = json_array_size(txn_array);
	if (!pool->gbt_txns)
		goto out;

	pool->txn_hashes = calloc(32 * (pool->gbt_txns + 1), 1);
	if (unlikely(!pool->txn_hashes))
		quit(1, "Failed to calloc txn_hashes in __build_gbt_txns");

	for (i = 0; i < pool->gbt_txns; i++) {
		json_t *txn_val = json_object_get(json_array_get(txn_array, i), "data");
		const char *txn = json_string_value(txn_val);
		int txn_len = strlen(txn);
		unsigned char *txn_bin;

		cal_len = txn_len;
		if (cal_len % 4)
			cal_len += 4 - (cal_len % 4);
		txn_bin = calloc(cal_len, 1);
		if (unlikely(!txn_bin))
			quit(1, "Failed to calloc txn_bin in __build_gbt_txns");
		if (unlikely(!hex2bin(txn_bin, txn, txn_len / 2)))
			quit(1, "Failed to hex2bin txn_bin");

		gen_hash(txn_bin, pool->txn_hashes + (32 * i), txn_len / 2);
		free(txn_bin);
	}
out:
	return ret;
}

static unsigned char *__gbt_merkleroot(struct pool *pool)
{
	unsigned char *merkle_hash;
	int i, txns;

	merkle_hash = calloc(32 * (pool->gbt_txns + 2), 1);
	if (unlikely(!merkle_hash))
		quit(1, "Failed to calloc merkle_hash in __gbt_merkleroot");

	gen_hash(pool->gbt_coinbase, merkle_hash, pool->coinbase_len);

	if (pool->gbt_txns)
		memcpy(merkle_hash + 32, pool->txn_hashes, pool->gbt_txns * 32);

	txns = pool->gbt_txns + 1;
	while (txns > 1) {
		if (txns % 2) {
			memcpy(&merkle_hash[txns * 32], &merkle_hash[(txns - 1) * 32], 32);
			txns++;
		}
		for (i = 0; i < txns; i += 2){
			unsigned char hashout[32];

			gen_hash(merkle_hash + (i * 32), hashout, 64);
			memcpy(merkle_hash + (i / 2 * 32), hashout, 32);
		}
		txns /= 2;
	}
	return merkle_hash;
}

static void calc_diff(struct work *work, int known);

static void gen_gbt_work(struct pool *pool, struct work *work)
{
	unsigned char *merkleroot;
	char *cbhex;

	mutex_lock(&pool->gbt_lock);
	__build_gbt_coinbase(pool);
	merkleroot = __gbt_merkleroot(pool);

	memcpy(work->data, &pool->gbt_version, 4);
	memcpy(work->data + 4, pool->previousblockhash, 32);
	memcpy(work->data + 4 + 32 + 32, &pool->curtime, 4);
	memcpy(work->data + 4 + 32 + 32 + 4, &pool->gbt_bits, 4);

	memcpy(work->target, pool->gbt_target, 32);

	cbhex = bin2hex(pool->gbt_coinbase, pool->coinbase_len);
	sprintf(work->gbt_coinbase, "%s", cbhex);
	free(cbhex);

	/* For encoding the block data on submission */
	work->gbt_txns = pool->gbt_txns + 1;
	mutex_unlock(&pool->gbt_lock);

	memcpy(work->data + 4 + 32, merkleroot, 32);
	flip32(work->data + 4 + 32, merkleroot);
	free(merkleroot);
	memset(work->data + 4 + 32 + 32 + 4 + 4, 0, 4); /* nonce */

	hex2bin(work->data + 4 + 32 + 32 + 4 + 4 + 4, "000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000", 48);
	hex2bin(work->hash1, "00000000000000000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000000000000000010000", 64);

	if (opt_debug) {
		char *header = bin2hex(work->data, 128);

		applog(LOG_DEBUG, "Generated GBT header %s", header);
		applog(LOG_DEBUG, "Work coinbase %s", work->gbt_coinbase);
		free(header);
	}

	calc_midstate(work);
	local_work++;
	work->pool = pool;
	work->gbt = true;
	work->id = total_work++;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_GBT;
	work->work_block = work_block;
	calc_diff(work, 0);
	gettimeofday(&work->tv_staged, NULL);
}

static bool gbt_decode(struct pool *pool, json_t *res_val)
{
	const char *previousblockhash;
	const char *target;
	const char *coinbasetxn;
	const char *longpollid;
	unsigned char hash_swap[32];
	int expires;
	int version;
	int curtime;
	bool submitold;
	const char *bits;

	previousblockhash = json_string_value(json_object_get(res_val, "previousblockhash"));
	target = json_string_value(json_object_get(res_val, "target"));
	coinbasetxn = json_string_value(json_object_get(json_object_get(res_val, "coinbasetxn"), "data"));
	longpollid = json_string_value(json_object_get(res_val, "longpollid"));
	expires = json_integer_value(json_object_get(res_val, "expires"));
	version = json_integer_value(json_object_get(res_val, "version"));
	curtime = json_integer_value(json_object_get(res_val, "curtime"));
	submitold = json_is_true(json_object_get(res_val, "submitold"));
	bits = json_string_value(json_object_get(res_val, "bits"));

	if (!previousblockhash || !target || !coinbasetxn || !longpollid ||
	    !expires || !version || !curtime || !bits) {
		applog(LOG_ERR, "JSON failed to decode GBT");
		return false;
	}

	applog(LOG_DEBUG, "previousblockhash: %s", previousblockhash);
	applog(LOG_DEBUG, "target: %s", target);
	applog(LOG_DEBUG, "coinbasetxn: %s", coinbasetxn);
	applog(LOG_DEBUG, "longpollid: %s", longpollid);
	applog(LOG_DEBUG, "expires: %d", expires);
	applog(LOG_DEBUG, "version: %d", version);
	applog(LOG_DEBUG, "curtime: %d", curtime);
	applog(LOG_DEBUG, "submitold: %s", submitold ? "true" : "false");
	applog(LOG_DEBUG, "bits: %s", bits);

	mutex_lock(&pool->gbt_lock);
	free(pool->coinbasetxn);
	pool->coinbasetxn = strdup(coinbasetxn);
	free(pool->longpollid);
	pool->longpollid = strdup(longpollid);

	hex2bin(hash_swap, previousblockhash, 32);
	swap256(pool->previousblockhash, hash_swap);

	hex2bin(hash_swap, target, 32);
	swab256(pool->gbt_target, hash_swap);

	pool->gbt_expires = expires;
	pool->gbt_version = htobe32(version);
	pool->curtime = htobe32(curtime);
	pool->submit_old = submitold;

	hex2bin((unsigned char *)&pool->gbt_bits, bits, 4);

	__build_gbt_txns(pool, res_val);
	mutex_unlock(&pool->gbt_lock);

	return true;
}

static bool getwork_decode(json_t *res_val, struct work *work)
{
	if (unlikely(!jobj_binary(res_val, "data", work->data, sizeof(work->data), true))) {
		applog(LOG_ERR, "JSON inval data");
		return false;
	}

	if (!jobj_binary(res_val, "midstate", work->midstate, sizeof(work->midstate), false)) {
		// Calculate it ourselves
		applog(LOG_DEBUG, "Calculating midstate locally");
		calc_midstate(work);
	}

	if (!jobj_binary(res_val, "hash1", work->hash1, sizeof(work->hash1), false)) {
		// Always the same anyway
		memcpy(work->hash1, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x80\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\0\0", 64);
	}

	if (unlikely(!jobj_binary(res_val, "target", work->target, sizeof(work->target), true))) {
		applog(LOG_ERR, "JSON inval target");
		return false;
	}
	return true;
}

static bool work_decode(struct pool *pool, struct work *work, json_t *val)
{
	json_t *res_val = json_object_get(val, "result");
	bool ret = false;

	if (!res_val || json_is_null(res_val)) {
		applog(LOG_ERR, "JSON Failed to decode result");
		goto out;
	}

	if (pool->has_gbt) {
		if (unlikely(!gbt_decode(pool, res_val)))
			goto out;
		gettimeofday(&pool->tv_template, NULL);
		work->gbt = true;
		ret = true;
		goto out;
	} else if (unlikely(!getwork_decode(res_val, work)))
		goto out;

	memset(work->hash, 0, sizeof(work->hash));

	gettimeofday(&work->tv_staged, NULL);

	ret = true;

out:
	return ret;
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

static int total_staged(void)
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
double total_secs = 1.0;
static char statusline[256];
/* logstart is where the log window should start */
static int devcursor, logstart, logcursor;
#ifdef HAVE_CURSES
/* statusy is where the status window goes up to in cases where it won't fit at startup */
static int statusy;
#endif
#ifdef HAVE_OPENCL
struct cgpu_info gpus[MAX_GPUDEVICES]; /* Maximum number apparently possible */
#endif
struct cgpu_info *cpus;

#ifdef HAVE_CURSES
static inline void unlock_curses(void)
{
	mutex_unlock(&console_lock);
}

static inline void lock_curses(void)
{
	mutex_lock(&console_lock);
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

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void suffix_string(uint64_t val, char *buf, int sigdigits)
{
	const double  dkilo = 1000.0;
	const uint64_t kilo = 1000ull;
	const uint64_t mega = 1000000ull;
	const uint64_t giga = 1000000000ull;
	const uint64_t tera = 1000000000000ull;
	const uint64_t peta = 1000000000000000ull;
	const uint64_t exa  = 1000000000000000000ull;
	char suffix[2] = "";
	bool decimal = true;
	double dval;

	if (val >= exa) {
		val /= peta;
		dval = (double)val / dkilo;
		sprintf(suffix, "E");
	} else if (val >= peta) {
		val /= tera;
		dval = (double)val / dkilo;
		sprintf(suffix, "P");
	} else if (val >= tera) {
		val /= giga;
		dval = (double)val / dkilo;
		sprintf(suffix, "T");
	} else if (val >= giga) {
		val /= mega;
		dval = (double)val / dkilo;
		sprintf(suffix, "G");
	} else if (val >= mega) {
		val /= kilo;
		dval = (double)val / dkilo;
		sprintf(suffix, "M");
	} else if (val >= kilo) {
		dval = (double)val / dkilo;
		sprintf(suffix, "K");
	} else {
		dval = val;
		decimal = false;
	}

	if (!sigdigits) {
		if (decimal)
			sprintf(buf, "%.3g%s", dval, suffix);
		else
			sprintf(buf, "%d%s", (unsigned int)dval, suffix);
	} else {
		/* Always show sigdigits + 1, padded on right with zeroes
		 * followed by suffix */
		int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

		sprintf(buf, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
	}
}

static void get_statline(char *buf, struct cgpu_info *cgpu)
{
	char displayed_hashes[16], displayed_rolling[16];
	uint64_t dh64, dr64;

	dh64 = (double)cgpu->total_mhashes / total_secs * 1000000ull;
	dr64 = (double)cgpu->rolling * 1000000ull;
	suffix_string(dh64, displayed_hashes, 4);
	suffix_string(dr64, displayed_rolling, 4);

	sprintf(buf, "%s%d ", cgpu->api->name, cgpu->device_id);
	if (cgpu->api->get_statline_before)
		cgpu->api->get_statline_before(buf, cgpu);
	else
		tailsprintf(buf, "               | ");
	tailsprintf(buf, "(%ds):%s (avg):%sh/s | A:%d R:%d HW:%d U:%.1f/m",
		opt_log_interval,
		displayed_rolling,
		displayed_hashes,
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

static int global_queued(void);

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
	mvwprintw(statuswin, 3, 0, " TQ: %d  ST: %d  SS: %d  DW: %d  NB: %d  LW: %d  GF: %d  RF: %d  WU: %.1f",
		global_queued(), total_staged(), total_stale, total_discarded, new_blocks,
		local_work, total_go, total_ro, total_diff1 / total_secs * 60);
	wclrtoeol(statuswin);
	if (pool->has_stratum) {
		mvwprintw(statuswin, 4, 0, " Connected to %s with stratum as user %s",
			pool->sockaddr_url, pool->rpc_user);
	} else if ((pool_strategy == POOL_LOADBALANCE  || pool_strategy == POOL_BALANCE) && total_pools > 1) {
		mvwprintw(statuswin, 4, 0, " Connected to multiple pools with%s LP",
			have_longpoll ? "": "out");
	} else {
		mvwprintw(statuswin, 4, 0, " Connected to %s with%s %s as user %s",
			pool->sockaddr_url, have_longpoll ? "": "out",
			pool->has_gbt ? "GBT" : "LP", pool->rpc_user);
	}
	wclrtoeol(statuswin);
	mvwprintw(statuswin, 5, 0, " Block: %s...  Started: %s  Best share: %s   ", current_hash, blocktime, best_share);
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

static int dev_width;

static void curses_print_devstatus(int thr_id)
{
	static int awidth = 1, rwidth = 1, hwwidth = 1, uwidth = 1;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	char logline[255];
	char displayed_hashes[16], displayed_rolling[16];
	uint64_t dh64, dr64;

	if (devcursor + cgpu->cgminer_id > LINES - 2 || opt_compact)
		return;

	cgpu->utility = cgpu->accepted / total_secs * 60;

	wmove(statuswin,devcursor + cgpu->cgminer_id, 0);
	wprintw(statuswin, " %s %*d: ", cgpu->api->name, dev_width, cgpu->device_id);
	if (cgpu->api->get_statline_before) {
		logline[0] = '\0';
		cgpu->api->get_statline_before(logline, cgpu);
		wprintw(statuswin, "%s", logline);
	}
	else
		wprintw(statuswin, "               | ");

	dh64 = (double)cgpu->total_mhashes / total_secs * 1000000ull;
	dr64 = (double)cgpu->rolling * 1000000ull;
	suffix_string(dh64, displayed_hashes, 4);
	suffix_string(dr64, displayed_rolling, 4);

	if (cgpu->status == LIFE_DEAD)
		wprintw(statuswin, "DEAD ");
	else if (cgpu->status == LIFE_SICK)
		wprintw(statuswin, "SICK ");
	else if (cgpu->deven == DEV_DISABLED)
		wprintw(statuswin, "OFF  ");
	else if (cgpu->deven == DEV_RECOVER)
		wprintw(statuswin, "REST  ");
	else
		wprintw(statuswin, "%6s", displayed_rolling);
	adj_width(cgpu->accepted, &awidth);
	adj_width(cgpu->rejected, &rwidth);
	adj_width(cgpu->hw_errors, &hwwidth);
	adj_width(cgpu->utility, &uwidth);

	wprintw(statuswin, "/%6sh/s | A:%*d R:%*d HW:%*d U:%*.2f/m",
			displayed_hashes,
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
static inline void change_logwinsize(void)
{
	int x, y, logx, logy;

	getmaxyx(mainwin, y, x);
	if (x < 80 || y < 25)
		return;

	if (y > statusy + 2 && statusy < logstart) {
		if (y - 2 < logstart)
			statusy = y - 2;
		else
			statusy = logstart;
		logcursor = statusy + 1;
		mvwin(logwin, logcursor, 0);
		wresize(statuswin, statusy, x);
	}

	y -= logcursor;
	getmaxyx(logwin, logy, logx);
	/* Detect screen size change */
	if (x != logx || y != logy)
		wresize(logwin, y, x);
}

static void check_winsizes(void)
{
	if (!use_curses)
		return;
	if (curses_active_locked()) {
		int y, x;

		erase();
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

static void switch_compact(void)
{
	if (opt_compact) {
		logstart = devcursor + 1;
		logcursor = logstart + 1;
	} else {
		logstart = devcursor + total_devices + 1;
		logcursor = logstart + 1;
	}
	check_winsizes();
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
bool log_curses_only(int prio, const char *f, va_list ap)
{
	bool high_prio;

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
		return true;
	}
	return false;
}

void clear_logwin(void)
{
	if (curses_active_locked()) {
		erase();
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

static void enable_pool(struct pool *pool)
{
	if (pool->enabled != POOL_ENABLED) {
		enabled_pools++;
		pool->enabled = POOL_ENABLED;
	}
}

static void disable_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_DISABLED;
}

static void reject_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_REJECTING;
}

/* Theoretically threads could race when modifying accepted and
 * rejected values but the chance of two submits completing at the
 * same time is zero so there is no point adding extra locking */
static void
share_result(json_t *val, json_t *res, json_t *err, const struct work *work,
	     char *hashshow, bool resubmit, char *worktime)
{
	struct pool *pool = work->pool;
	struct cgpu_info *cgpu = thr_info[work->thr_id].cgpu;

	if (json_is_true(res) || (work->gbt && json_is_null(res))) {
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		pool->seq_rejects = 0;
		cgpu->last_share_pool = pool->pool_no;
		cgpu->last_share_pool_time = time(NULL);
		cgpu->last_share_diff = work->work_difficulty;
		pool->last_share_time = cgpu->last_share_pool_time;
		pool->last_share_diff = work->work_difficulty;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
		if (!QUIET) {
			if (total_pools > 1)
				applog(LOG_NOTICE, "Accepted %s %s %d pool %d %s%s",
				       hashshow, cgpu->api->name, cgpu->device_id, work->pool->pool_no, resubmit ? "(resubmit)" : "", worktime);
			else
				applog(LOG_NOTICE, "Accepted %s %s %d %s%s",
				       hashshow, cgpu->api->name, cgpu->device_id, resubmit ? "(resubmit)" : "", worktime);
		}
		sharelog("accept", work);
		if (opt_shares && total_accepted >= opt_shares) {
			applog(LOG_WARNING, "Successfully mined %d accepted shares as requested and exiting.", opt_shares);
			kill_work();
			return;
		}

		/* Detect if a pool that has been temporarily disabled for
		 * continually rejecting shares has started accepting shares.
		 * This will only happen with the work returned from a
		 * longpoll */
		if (unlikely(pool->enabled == POOL_REJECTING)) {
			applog(LOG_WARNING, "Rejecting pool %d now accepting shares, re-enabling!", pool->pool_no);
			enable_pool(pool);
			switch_pools(NULL);
		}
	} else {
		cgpu->rejected++;
		total_rejected++;
		pool->rejected++;
		cgpu->diff_rejected += work->work_difficulty;
		total_diff_rejected += work->work_difficulty;
		pool->diff_rejected += work->work_difficulty;
		pool->seq_rejects++;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!QUIET) {
			char where[17];
			char disposition[36] = "reject";
			char reason[32];

			strcpy(reason, "");
			if (total_pools > 1)
				sprintf(where, "pool %d", work->pool->pool_no);
			else
				strcpy(where, "");

			if (!work->gbt)
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
			} else if (work->stratum && err && json_is_array(err)) {
				json_t *reason_val = json_array_get(err, 1);
				char *reason_str;

				if (reason_val && json_is_string(reason_val)) {
					reason_str = (char *)json_string_value(reason_val);
					snprintf(reason, 31, " (%s)", reason_str);
				}
			}

			applog(LOG_NOTICE, "Rejected %s %s %d %s%s %s%s",
			       hashshow, cgpu->api->name, cgpu->device_id, where, reason, resubmit ? "(resubmit)" : "", worktime);
			sharelog(disposition, work);
		}

		/* Once we have more than a nominal amount of sequential rejects,
		 * at least 10 and more than 3 mins at the current utility,
		 * disable the pool because some pool error is likely to have
		 * ensued. Do not do this if we know the share just happened to
		 * be stale due to networking delays.
		 */
		if (pool->seq_rejects > 10 && !work->stale && opt_disable_pool && enabled_pools > 1) {
			double utility = total_accepted / total_secs * 60;

			if (pool->seq_rejects > utility * 3) {
				applog(LOG_WARNING, "Pool %d rejected %d sequential shares, disabling!",
				       pool->pool_no, pool->seq_rejects);
				reject_pool(pool);
				if (pool == current_pool())
					switch_pools(NULL);
				pool->seq_rejects = 0;
			}
		}
	}
}

static const uint64_t diffone = 0xFFFF000000000000ull;

static uint64_t share_diff(const struct work *work)
{
	uint64_t *data64, d64;
	char rhash[36];
	uint64_t ret;

	swab256(rhash, work->hash);
	data64 = (uint64_t *)(rhash + 4);
	d64 = be64toh(*data64);
	if (unlikely(!d64))
		d64 = 1;
	ret = diffone / d64;
	if (ret > best_diff) {
		best_diff = ret;
		suffix_string(best_diff, best_share, 0);
	}
	return ret;
}

static uint32_t scrypt_diff(const struct work *work)
{
	const uint32_t scrypt_diffone = 0x0000fffful;
	uint32_t d32 = work->outputhash, ret;

	if (unlikely(!d32))
		d32 = 1;
	ret = scrypt_diffone / d32;
	if (ret > best_diff) {
		best_diff = ret;
		suffix_string(best_diff, best_share, 0);
	}
	return ret;
}

static bool submit_upstream_work(struct work *work, CURL *curl, bool resubmit)
{
	char *hexstr = NULL;
	json_t *val, *res, *err;
	char s[1024];
	bool rc = false;
	int thr_id = work->thr_id;
	struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
	struct pool *pool = work->pool;
	int rolltime;
	uint32_t *hash32;
	struct timeval tv_submit, tv_submit_reply;
	char hashshow[64 + 1] = "";
	char worktime[200] = "";

#ifdef __BIG_ENDIAN__
        int swapcounter = 0;
        for (swapcounter = 0; swapcounter < 32; swapcounter++)
            (((uint32_t*) (work->data))[swapcounter]) = swab32(((uint32_t*) (work->data))[swapcounter]);
#endif

	/* build hex string */
	hexstr = bin2hex(work->data, sizeof(work->data));

	/* build JSON-RPC request */
	if (work->gbt) {
		char gbt_block[1024], *varint, *header;
		unsigned char data[80];

		flip80(data, work->data);
		header = bin2hex(data, 80);
		sprintf(gbt_block, "%s", header);
		free(header);

		if (work->gbt_txns < 0xfd) {
			uint8_t val = work->gbt_txns;

			varint = bin2hex((const unsigned char *)&val, 1);
		} else if (work->gbt_txns <= 0xffff) {
			uint16_t val = htole16(work->gbt_txns);

			strcat(gbt_block, "fd");
			varint = bin2hex((const unsigned char *)&val, 2);
		} else {
			uint32_t val = htole32(work->gbt_txns);

			strcat(gbt_block, "fe");
			varint = bin2hex((const unsigned char *)&val, 4);
		}
		strcat(gbt_block, varint);
		free(varint);
		strcat(gbt_block, work->gbt_coinbase);

		sprintf(s, "{\"id\": 0, \"method\": \"submitblock\", \"params\": [\"%s\", {}]}", gbt_block);
	} else
		sprintf(s, "{\"method\": \"getwork\", \"params\": [ \"%s\" ], \"id\":1}", hexstr);
	applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, s);
	strcat(s, "\n");

	gettimeofday(&tv_submit, NULL);
	/* issue JSON-RPC request */
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, s, false, false, &rolltime, pool, true);
	gettimeofday(&tv_submit_reply, NULL);
	if (unlikely(!val)) {
		applog(LOG_INFO, "submit_upstream_work json_rpc_call failed");
		if (!pool_tset(pool, &pool->submit_fail)) {
			total_ro++;
			pool->remotefail_occasions++;
			applog(LOG_WARNING, "Pool %d communication failure, caching submissions", pool->pool_no);
		}
		sleep(5);
		goto out;
	} else if (pool_tclear(pool, &pool->submit_fail))
		applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);

	res = json_object_get(val, "result");
	err = json_object_get(val, "error");

	if (!QUIET) {
		int intdiff = floor(work->work_difficulty);
		char diffdisp[16];

		hash32 = (uint32_t *)(work->hash);
		if (opt_scrypt) {
			uint32_t sharediff;

			scrypt_outputhash(work);
			sharediff = scrypt_diff(work);
			suffix_string(sharediff, diffdisp, 0);

			sprintf(hashshow, "%08lx Diff %s/%d", (unsigned long)work->outputhash, diffdisp, intdiff);
		} else {
			uint64_t sharediff = share_diff(work);

			suffix_string(sharediff, diffdisp, 0);

			sprintf(hashshow, "%08lx Diff %s/%d%s", (unsigned long)(hash32[6]), diffdisp, intdiff,
				work->block? " BLOCK!" : "");
		}

		if (opt_worktime) {
			char workclone[20];
			struct tm *tm, tm_getwork, tm_submit_reply;
			double getwork_time = tdiff((struct timeval *)&(work->tv_getwork_reply),
							(struct timeval *)&(work->tv_getwork));
			double getwork_to_work = tdiff((struct timeval *)&(work->tv_work_start),
							(struct timeval *)&(work->tv_getwork_reply));
			double work_time = tdiff((struct timeval *)&(work->tv_work_found),
							(struct timeval *)&(work->tv_work_start));
			double work_to_submit = tdiff(&tv_submit,
							(struct timeval *)&(work->tv_work_found));
			double submit_time = tdiff(&tv_submit_reply, &tv_submit);
			int diffplaces = 3;

			tm = localtime(&(work->tv_getwork.tv_sec));
			memcpy(&tm_getwork, tm, sizeof(struct tm));
			tm = localtime(&(tv_submit_reply.tv_sec));
			memcpy(&tm_submit_reply, tm, sizeof(struct tm));

			if (work->clone) {
				sprintf(workclone, "C:%1.3f",
					tdiff((struct timeval *)&(work->tv_cloned),
						(struct timeval *)&(work->tv_getwork_reply)));
			}
			else
				strcpy(workclone, "O");

			if (work->work_difficulty < 1)
				diffplaces = 6;

			sprintf(worktime, " <-%08lx.%08lx M:%c D:%1.*f G:%02d:%02d:%02d:%1.3f %s (%1.3f) W:%1.3f (%1.3f) S:%1.3f R:%02d:%02d:%02d",
				(unsigned long)swab32(*(uint32_t *)&(work->data[opt_scrypt ? 32 : 28])),
				(unsigned long)swab32(*(uint32_t *)&(work->data[opt_scrypt ? 28 : 24])),
				work->getwork_mode, diffplaces, work->work_difficulty,
				tm_getwork.tm_hour, tm_getwork.tm_min,
				tm_getwork.tm_sec, getwork_time, workclone,
				getwork_to_work, work_time, work_to_submit, submit_time,
				tm_submit_reply.tm_hour, tm_submit_reply.tm_min,
				tm_submit_reply.tm_sec);
		}
	}

	share_result(val, res, err, work, hashshow, resubmit, worktime);

	cgpu->utility = cgpu->accepted / total_secs * 60;

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
	return rc;
}

/* In balanced mode, the amount of diff1 solutions per pool is monitored as a
 * rolling average per 10 minutes and if pools start getting more, it biases
 * away from them to distribute work evenly. The share count is reset to the
 * rolling average every 10 minutes to not send all work to one pool after it
 * has been disabled/out for an extended period. */
static struct pool *select_balanced(struct pool *cp)
{
	int i, lowest = cp->shares;
	struct pool *ret = cp;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->idle || pool->enabled != POOL_ENABLED)
			continue;
		if (pool->shares < lowest) {
			lowest = pool->shares;
			ret = pool;
		}
	}

	ret->shares++;
	return ret;
}

/* Select any active pool in a rotating fashion when loadbalance is chosen */
static inline struct pool *select_pool(bool lagging)
{
	static int rotating_pool = 0;
	struct pool *pool, *cp;

	cp = current_pool();

	if (pool_strategy == POOL_BALANCE)
		return select_balanced(cp);

	if (pool_strategy != POOL_LOADBALANCE && (!lagging || opt_fail_only))
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

static double DIFFEXACTONE = 26959946667150639794667015087019630673637144422540572481103610249216.0;

/*
 * Calculate the work share difficulty
 */
static void calc_diff(struct work *work, int known)
{
	struct cgminer_pool_stats *pool_stats = &(work->pool->cgminer_pool_stats);

	if (opt_scrypt) {
		uint64_t *data64, d64;
		char rtarget[36];

		swab256(rtarget, work->target);
		data64 = (uint64_t *)(rtarget + 2);
		d64 = be64toh(*data64);
		if (unlikely(!d64))
			d64 = 1;
		work->work_difficulty = diffone / d64;
	} else if (!known) {
		double targ = 0;
		int i;

		for (i = 31; i >= 0; i--) {
			targ *= 256;
			targ += work->target[i];
		}

		work->work_difficulty = DIFFEXACTONE / (targ ? : DIFFEXACTONE);
	} else
		work->work_difficulty = known;

	pool_stats->last_diff = work->work_difficulty;

	if (work->work_difficulty == pool_stats->min_diff)
		pool_stats->min_diff_count++;
	else if (work->work_difficulty < pool_stats->min_diff || pool_stats->min_diff == 0) {
		pool_stats->min_diff = work->work_difficulty;
		pool_stats->min_diff_count = 1;
	}

	if (work->work_difficulty == pool_stats->max_diff)
		pool_stats->max_diff_count++;
	else if (work->work_difficulty > pool_stats->max_diff) {
		pool_stats->max_diff = work->work_difficulty;
		pool_stats->max_diff_count = 1;
	}
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
	work->mandatory = true;
	work->pool = pools[0];
	gettimeofday(&(work->tv_getwork), NULL);
	memcpy(&(work->tv_getwork_reply), &(work->tv_getwork), sizeof(struct timeval));
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
	calc_diff(work, 0);
}

static bool get_upstream_work(struct work *work, CURL *curl)
{
	struct pool *pool = work->pool;
	struct cgminer_pool_stats *pool_stats = &(pool->cgminer_pool_stats);
	struct timeval tv_elapsed;
	json_t *val = NULL;
	bool rc = false;
	char *url;

	applog(LOG_DEBUG, "DBG: sending %s get RPC call: %s", pool->rpc_url, pool->rpc_req);

	url = pool->rpc_url;

	gettimeofday(&(work->tv_getwork), NULL);

	val = json_rpc_call(curl, url, pool->rpc_userpass, pool->rpc_req, false,
			    false, &work->rolltime, pool, false);
	pool_stats->getwork_attempts++;

	if (likely(val)) {
		rc = work_decode(pool, work, val);
		if (unlikely(!rc))
			applog(LOG_DEBUG, "Failed to decode work in get_upstream_work");
	} else
		applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");

	gettimeofday(&(work->tv_getwork_reply), NULL);
	timersub(&(work->tv_getwork_reply), &(work->tv_getwork), &tv_elapsed);
	pool_stats->getwork_wait_rolling += ((double)tv_elapsed.tv_sec + ((double)tv_elapsed.tv_usec / 1000000)) * 0.63;
	pool_stats->getwork_wait_rolling /= 1.63;

	timeradd(&tv_elapsed, &(pool_stats->getwork_wait), &(pool_stats->getwork_wait));
	if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_max), >)) {
		pool_stats->getwork_wait_max.tv_sec = tv_elapsed.tv_sec;
		pool_stats->getwork_wait_max.tv_usec = tv_elapsed.tv_usec;
	}
	if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_min), <)) {
		pool_stats->getwork_wait_min.tv_sec = tv_elapsed.tv_sec;
		pool_stats->getwork_wait_min.tv_usec = tv_elapsed.tv_usec;
	}
	pool_stats->getwork_calls++;

	work->pool = pool;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_POOL;
	calc_diff(work, 0);
	total_getworks++;
	pool->getwork_requested++;

	if (likely(val))
		json_decref(val);

	return rc;
}

static struct work *make_work(void)
{
	struct work *work = calloc(1, sizeof(struct work));

	if (unlikely(!work))
		quit(1, "Failed to calloc work in make_work");
	mutex_lock(&control_lock);
	work->id = total_work++;
	mutex_unlock(&control_lock);
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
		free_work(wc->work);
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

	if (!successful_connect)
		return;

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

static inline void __app_restart(void)
{
#if defined(unix)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif

	execv(initial_args[0], initial_args);
}

void app_restart(void)
{
	applog(LOG_WARNING, "Attempting to restart %s", packagename);

	__kill_work();
	clean_up();

	__app_restart();

	/* We shouldn't reach here */
	applog(LOG_WARNING, "Failed to restart application");
}

/* Returns all signal handlers to their defaults */
static inline void __sighandler(void)
{
	/* Restore signal handlers so we can still quit if kill_work fails */
	sigaction(SIGTERM, &termhandler, NULL);
	sigaction(SIGINT, &inthandler, NULL);
	if (opt_restart) {
		sigaction(SIGSEGV, &segvhandler, NULL);
		sigaction(SIGILL, &illhandler, NULL);
#ifndef WIN32
		sigaction(SIGBUS, &bushandler, NULL);
#endif
	}
}

static void sighandler(int __maybe_unused sig)
{
	__sighandler();
	kill_work();
}

/* Handles segfaults and other crashes by attempting to restart cgminer. Try to
 * do as little as possible since we are probably corrupted. */
static void seghandler(int sig)
{
	__sighandler();
	fprintf(stderr, "\nCrashed with signal %d! Will attempt to restart\n", sig);
	__app_restart();
	/* We shouldn't reach here */
	fprintf(stderr, "Failed to restart, exiting now\n");

	exit(1);
}

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
 * and there are already 5 curls in circulation. Limit total number to the
 * number of mining threads per pool as well to prevent blasting a pool during
 * network delays/outages. */
static struct curl_ent *pop_curl_entry(struct pool *pool)
{
	int curl_limit = opt_delaynet ? 5 : (mining_threads + opt_queue) * 2;
	struct curl_ent *ce;

	mutex_lock(&pool->pool_lock);
retry:
	if (!pool->curls)
		recruit_curl(pool);
	else if (list_empty(&pool->curlring)) {
		if (pool->curls >= curl_limit) {
			pthread_cond_wait(&pool->cr_cond, &pool->pool_lock);
			goto retry;
		} else
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

/* This is overkill, but at least we'll know accurately how much work is
 * queued to prevent ever being left without work */
static void inc_queued(struct pool *pool)
{
	mutex_lock(&qd_lock);
	total_queued++;
	pool->queued++;
	mutex_unlock(&qd_lock);
}

static void dec_queued(struct pool *pool)
{
	mutex_lock(&qd_lock);
	total_queued--;
	pool->queued--;
	mutex_unlock(&qd_lock);
}

static int __global_queued(void)
{
	return total_queued;
}

static int global_queued(void)
{
	int ret;

	mutex_lock(&qd_lock);
	ret = __global_queued();
	mutex_unlock(&qd_lock);
	return ret;
}

static bool stale_work(struct work *work, bool share);

static inline bool should_roll(struct work *work)
{
	struct timeval now;
	time_t expiry;

	if (work->pool != current_pool() && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE)
		return false;

	if (work->rolltime > opt_scantime)
		expiry = work->rolltime;
	else
		expiry = opt_scantime;
	expiry = expiry * 2 / 3;

	/* We shouldn't roll if we're unlikely to get one shares' duration
	 * work out of doing so */
	gettimeofday(&now, NULL);
	if (now.tv_sec - work->tv_staged.tv_sec > expiry)
		return false;
	
	return true;
}

/* Limit rolls to 7000 to not beyond 2 hours in the future where bitcoind will
 * reject blocks as invalid. */
static inline bool can_roll(struct work *work)
{
	return (!work->stratum && work->pool && work->rolltime && !work->clone &&
		work->rolls < 7000 && !stale_work(work, false));
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

static struct work *make_clone(struct work *work)
{
	struct work *work_clone = make_work();

	memcpy(work_clone, work, sizeof(struct work));
	work_clone->clone = true;
	gettimeofday((struct timeval *)&(work_clone->tv_cloned), NULL);
	work_clone->longpoll = false;
	work_clone->mandatory = false;
	/* Make cloned work appear slightly older to bias towards keeping the
	 * master work item which can be further rolled */
	work_clone->tv_staged.tv_sec -= 1;

	return work_clone;
}

static bool stage_work(struct work *work);

static bool clone_available(void)
{
	struct work *work, *tmp;
	bool cloned = false;

	if (!staged_rollable)
		goto out;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (can_roll(work) && should_roll(work)) {
			struct work *work_clone;

			roll_work(work);
			work_clone = make_clone(work);
			roll_work(work);
			applog(LOG_DEBUG, "Pushing cloned available work to stage thread");
			if (unlikely(!stage_work(work_clone))) {
				free(work_clone);
				break;
			}
			cloned = true;
			break;
		}
	}
	mutex_unlock(stgd_lock);

out:
	return cloned;
}

static bool queue_request(void);

static void pool_died(struct pool *pool)
{
	if (!pool_tset(pool, &pool->idle)) {
		applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
		gettimeofday(&pool->tv_idle, NULL);
		switch_pools(NULL);
	}
}

static void gen_stratum_work(struct pool *pool, struct work *work);

static void *get_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	struct work *ret_work= NULL;
	struct curl_ent *ce = NULL;
	struct pool *pool;

	pthread_detach(pthread_self());

	applog(LOG_DEBUG, "Creating extra get work thread");

retry:
	pool = wc->pool;

	if (pool->has_stratum) {
		while (!pool->stratum_active) {
			struct pool *altpool = select_pool(true);

			sleep(5);
			if (altpool != pool) {
				wc->pool = altpool;
				inc_queued(altpool);
				dec_queued(pool);
				goto retry;
			}
		}
		ret_work = make_work();
		gen_stratum_work(pool, ret_work);
		if (unlikely(!stage_work(ret_work))) {
			applog(LOG_ERR, "Failed to stage stratum work in get_work_thread");
			kill_work();
			free(ret_work);
		}
		dec_queued(pool);
		goto out;
	}

	if (pool->has_gbt) {
		while (pool->idle) {
			struct pool *altpool = select_pool(true);

			sleep(5);
			if (altpool != pool) {
				wc->pool = altpool;
				inc_queued(altpool);
				dec_queued(pool);
				goto retry;
			}
		}
		ret_work = make_work();
		gen_gbt_work(pool, ret_work);
		if (unlikely(!stage_work(ret_work))) {
			applog(LOG_ERR, "Failed to stage gbt work in get_work_thread");
			kill_work();
			free(ret_work);
		}
		dec_queued(pool);
		goto out;
	}

	if (clone_available()) {
		dec_queued(pool);
		goto out;
	}

	ret_work = make_work();
	ret_work->thr = NULL;

	if (opt_benchmark) {
		get_benchmark_work(ret_work);
		ret_work->queued = true;
	} else {
		ret_work->pool = wc->pool;

		if (!ce)
			ce = pop_curl_entry(pool);

		/* obtain new work from bitcoin via JSON-RPC */
		if (!get_upstream_work(ret_work, ce->curl)) {
			applog(LOG_DEBUG, "Pool %d json_rpc_call failed on get work, retrying in 5s", pool->pool_no);
			sleep(5);
			dec_queued(pool);
			/* Make sure the pool just hasn't stopped serving
			 * requests but is up as we'll keep hammering it */
			if (++pool->seq_getfails > mining_threads + opt_queue)
				pool_died(pool);
			queue_request();
			free_work(ret_work);
			goto out;
		}
		pool->seq_getfails = 0;

		ret_work->queued = true;
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
	if (ce)
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
	time_t work_expiry;
	struct pool *pool;
	int getwork_delay;

	if (work->work_block != work_block) {
		applog(LOG_DEBUG, "Work stale due to block mismatch");
		return true;
	}

	/* Technically the rolltime should be correct but some pools
	 * advertise a broken expire= that is lower than a meaningful
	 * scantime */
	if (work->rolltime > opt_scantime)
		work_expiry = work->rolltime;
	else
		work_expiry = opt_expiry;

	pool = work->pool;
	/* Factor in the average getwork delay of this pool, rounding it up to
	 * the nearest second */
	getwork_delay = pool->cgminer_pool_stats.getwork_wait_rolling * 5 + 1;
	work_expiry -= getwork_delay;
	if (unlikely(work_expiry < 5))
		work_expiry = 5;

	gettimeofday(&now, NULL);
	if ((now.tv_sec - work->tv_staged.tv_sec) >= work_expiry) {
		applog(LOG_DEBUG, "Work stale due to expiry");
		return true;
	}

	if (opt_fail_only && !share && pool != current_pool() && !work->mandatory &&
	    pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		applog(LOG_DEBUG, "Work stale due to fail only pool mismatch");
		return true;
	}

	return false;
}

static void check_solve(struct work *work)
{
#ifndef MIPSEB
	/* This segfaults on openwrt */
	work->block = regeneratehash(work);
	if (unlikely(work->block)) {
		work->pool->solved++;
		found_blocks++;
		work->mandatory = true;
		applog(LOG_NOTICE, "Found block for pool %d!", work->pool->pool_no);
	}
#endif
}

static void *submit_work_thread(void *userdata)
{
	struct workio_cmd *wc = (struct workio_cmd *)userdata;
	struct work *work = wc->work;
	struct pool *pool = work->pool;
	bool resubmit = false;
	struct curl_ent *ce;

	pthread_detach(pthread_self());

	applog(LOG_DEBUG, "Creating extra submit work thread");

	check_solve(work);

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
			total_diff_stale += work->work_difficulty;
			pool->diff_stale += work->work_difficulty;
			goto out;
		}
		work->stale = true;
	}

	if (work->stratum) {
		struct stratum_share *sshare = calloc(sizeof(struct stratum_share), 1);
		uint32_t *hash32 = (uint32_t *)work->hash, nonce;
		char *noncehex;
		char s[1024];

		memcpy(&sshare->work, work, sizeof(struct work));
		mutex_lock(&sshare_lock);
		/* Give the stratum share a unique id */
		sshare->id = swork_id++;
		HASH_ADD_INT(stratum_shares, id, sshare);
		mutex_unlock(&sshare_lock);

		nonce = *((uint32_t *)(work->data + 76));
		noncehex = bin2hex((const unsigned char *)&nonce, 4);
		memset(s, 0, 1024);
		sprintf(s, "{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"], \"id\": %d, \"method\": \"mining.submit\"}",
			pool->rpc_user, work->job_id, work->nonce2, work->ntime, noncehex, sshare->id);
		free(noncehex);

		applog(LOG_INFO, "Submitting share %08lx to pool %d", (unsigned long)(hash32[6]), pool->pool_no);

		if (likely(stratum_send(pool, s, strlen(s)))) {
			if (pool_tclear(pool, &pool->submit_fail))
					applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);
			applog(LOG_DEBUG, "Successfully submitted, adding to stratum_shares db");
		} else {
			applog(LOG_WARNING, "Failed to submit stratum share to pool %d", pool->pool_no);
			mutex_lock(&sshare_lock);
			HASH_DEL(stratum_shares, sshare);
			mutex_unlock(&sshare_lock);
			free(sshare);
			pool->stale_shares++;
			total_stale++;

			if (!pool_tset(pool, &pool->submit_fail)) {
				total_ro++;
				pool->remotefail_occasions++;
				applog(LOG_WARNING, "Pool %d share submission failure", pool->pool_no);
			}
		}

		goto out;
	}

	ce = pop_curl_entry(pool);
	/* submit solution to bitcoin via JSON-RPC */
	while (!submit_upstream_work(work, ce->curl, resubmit)) {
		resubmit = true;
		if (stale_work(work, true)) {
			applog(LOG_NOTICE, "Share became stale while retrying submit, discarding");
			total_stale++;
			pool->stale_shares++;
			total_diff_stale += work->work_difficulty;
			pool->diff_stale += work->work_difficulty;
			break;
		}

		/* pause, then restart work-request loop */
		applog(LOG_INFO, "json_rpc_call failed on submit_work, retrying");
	}
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

static bool pool_active(struct pool *pool, bool pinging);

void switch_pools(struct pool *selected)
{
	struct pool *pool, *last_pool;
	int i, pool_no, next_pool;

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
		case POOL_BALANCE:
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
			if (selected && !selected->idle) {
				pool_no = selected->pool_no;
				break;
			}
			next_pool = pool_no;
			/* Select the next alive pool */
			for (i = 1; i < total_pools; i++) {
				next_pool++;
				if (next_pool >= total_pools)
					next_pool = 0;
				pool = pools[next_pool];
				if (!pool->idle && pool->enabled == POOL_ENABLED) {
					pool_no = next_pool;
					break;
				}
			}
			break;
		default:
			break;
	}

	currentpool = pools[pool_no];
	pool = currentpool;
	mutex_unlock(&control_lock);

	/* Set the lagging flag to avoid pool not providing work fast enough
	 * messages in failover only mode since  we have to get all fresh work
	 * as in restart_threads */
	if (opt_fail_only)
		pool_tset(pool, &pool->lagging);

	if (pool != last_pool) {
		applog(LOG_WARNING, "Switching to %s", pool->rpc_url);
		/* Get a fresh block template since we may not have an up to
		 * date one */
		if (pool->has_gbt)
			pool_active(pool, true);
	}

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

static void discard_stale(void)
{
	struct work *work, *tmp;
	int stale = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (stale_work(work, false)) {
			HASH_DEL(staged_work, work);
			work->pool->staged--;
			discard_work(work);
			stale++;
		}
	}
	mutex_unlock(stgd_lock);

	if (stale) {
		applog(LOG_DEBUG, "Discarded %d stales that didn't match current hash", stale);
		while (stale-- > 0)
			queue_request();
	}
}

/* A generic wait function for threads that poll that will wait a specified
 * time tdiff waiting on the pthread conditional that is broadcast when a
 * work restart is required. Returns the value of pthread_cond_timedwait
 * which is zero if the condition was met or ETIMEDOUT if not.
 */
int restart_wait(unsigned int mstime)
{
	struct timeval now, then, tdiff;
	struct timespec abstime;
	int rc;

	tdiff.tv_sec = mstime / 1000;
	tdiff.tv_usec = mstime * 1000 - (tdiff.tv_sec * 1000000);
	gettimeofday(&now, NULL);
	timeradd(&now, &tdiff, &then);
	abstime.tv_sec = then.tv_sec;
	abstime.tv_nsec = then.tv_usec * 1000;

	mutex_lock(&restart_lock);
	rc = pthread_cond_timedwait(&restart_cond, &restart_lock, &abstime);
	mutex_unlock(&restart_lock);

	return rc;
}
	
static void restart_threads(void)
{
	struct pool *cp = current_pool();
	int i;

	/* Artificially set the lagging flag to avoid pool not providing work
	 * fast enough  messages after every long poll */
	pool_tset(cp, &cp->lagging);

	/* Discard staged work that is now stale */
	discard_stale();

	for (i = 0; i < mining_threads; i++)
		thr_info[i].work_restart = true;

	mutex_lock(&restart_lock);
	pthread_cond_broadcast(&restart_cond);
	mutex_unlock(&restart_lock);
}

static void set_curblock(char *hexstr, unsigned char *hash)
{
	unsigned char hash_swap[32];
	unsigned char block_hash_swap[32];
	char *old_hash;

	strcpy(current_block, hexstr);
	swap256(hash_swap, hash);
	swap256(block_hash_swap, hash + 4);

	/* Don't free current_hash directly to avoid dereferencing when read
	 * elsewhere - and update block_timeval inside the same lock */
	mutex_lock(&ch_lock);
	gettimeofday(&block_timeval, NULL);
	old_hash = current_hash;
	current_hash = bin2hex(hash_swap + 2, 12);
	free(old_hash);
	old_hash = current_fullhash;
	current_fullhash = bin2hex(block_hash_swap, 32);
	free(old_hash);
	mutex_unlock(&ch_lock);

	get_timestamp(blocktime, &block_timeval);

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
	char *hexstr = bin2hex(work->data + 8, 18);
	bool ret;

	ret = block_exists(hexstr);
	free(hexstr);
	return ret;
}

static int block_sort(struct block *blocka, struct block *blockb)
{
	return blocka->block_no - blockb->block_no;
}

static bool test_work_current(struct work *work)
{
	bool ret = true;
	char *hexstr;

	if (work->mandatory)
		return ret;

	hexstr = bin2hex(work->data + 8, 18);

	/* Search to see if this block exists yet and if not, consider it a
	 * new block and set the current block details to this one */
	if (!block_exists(hexstr)) {
		struct block *s = calloc(sizeof(struct block), 1);
		int deleted_block = 0;
		ret = false;

		if (unlikely(!s))
			quit (1, "test_work_current OOM");
		strcpy(s->hash, hexstr);
		s->block_no = new_blocks++;
		wr_lock(&blk_lock);
		/* Only keep the last hour's worth of blocks in memory since
		 * work from blocks before this is virtually impossible and we
		 * want to prevent memory usage from continually rising */
		if (HASH_COUNT(blocks) > 6) {
			struct block *oldblock;

			HASH_SORT(blocks, block_sort);
			oldblock = blocks;
			deleted_block = oldblock->block_no;
			HASH_DEL(blocks, oldblock);
			free(oldblock);
		}
		HASH_ADD_STR(blocks, hash, s);
		wr_unlock(&blk_lock);
		if (deleted_block)
			applog(LOG_DEBUG, "Deleted block %d from database", deleted_block);
		set_curblock(hexstr, work->data);
		if (unlikely(new_blocks == 1))
			goto out_free;

		work_block++;

		if (!work->stratum) {
			if (work->longpoll) {
				applog(LOG_NOTICE, "%sLONGPOLL from pool %d detected new block",
				       work->gbt ? "GBT " : "", work->pool->pool_no);
				work->longpoll = false;
			} else if (have_longpoll)
				applog(LOG_NOTICE, "New block detected on network before longpoll");
			else
				applog(LOG_NOTICE, "New block detected on network");
		}
		restart_threads();
	} else if (work->longpoll) {
		work->longpoll = false;
		if (work->pool == current_pool()) {
			applog(LOG_NOTICE, "%sLONGPOLL from pool %d requested work restart",
			       work->gbt ? "GBT " : "", work->pool->pool_no);
			work_block++;
			restart_threads();
		}
	}
out_free:
	free(hexstr);
	return ret;
}

static int tv_sort(struct work *worka, struct work *workb)
{
	return worka->tv_staged.tv_sec - workb->tv_staged.tv_sec;
}

static bool work_rollable(struct work *work)
{
	return (!work->clone && work->rolltime);
}

static bool hash_push(struct work *work)
{
	bool rc = true;

	mutex_lock(stgd_lock);
	if (work_rollable(work))
		staged_rollable++;
	if (likely(!getq->frozen)) {
		HASH_ADD_INT(staged_work, id, work);
		HASH_SORT(staged_work, tv_sort);
	} else
		rc = false;
	pthread_cond_signal(&getq->cond);
	mutex_unlock(stgd_lock);

	work->pool->staged++;

	if (work->queued) {
		work->queued = false;
		dec_queued(work->pool);
	}

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
		wlog(" Accepted difficulty shares: %1.f\n", pool->diff_accepted);
		wlog(" Rejected difficulty shares: %1.f\n", pool->diff_rejected);
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

/* add a mutex if this needs to be thread safe in the future */
static struct JE {
	char *buf;
	struct JE *next;
} *jedata = NULL;

static void json_escape_free()
{
	struct JE *jeptr = jedata;
	struct JE *jenext;

	jedata = NULL;

	while (jeptr) {
		jenext = jeptr->next;
		free(jeptr->buf);
		free(jeptr);
		jeptr = jenext;
	}
}

static char *json_escape(char *str)
{
	struct JE *jeptr;
	char *buf, *ptr;

	/* 2x is the max, may as well just allocate that */
	ptr = buf = malloc(strlen(str) * 2 + 1);

	jeptr = malloc(sizeof(*jeptr));

	jeptr->buf = buf;
	jeptr->next = jedata;
	jedata = jeptr;

	while (*str) {
		if (*str == '\\' || *str == '"')
			*(ptr++) = '\\';

		*(ptr++) = *(str++);
	}

	*ptr = '\0';

	return buf;
}

void write_config(FILE *fcfg)
{
	int i;

	/* Write pool values */
	fputs("{\n\"pools\" : [", fcfg);
	for(i = 0; i < total_pools; i++) {
		fprintf(fcfg, "%s\n\t{\n\t\t\"url\" : \"%s%s%s%s\",", i > 0 ? "," : "",
			pools[i]->rpc_proxy ? json_escape((char *)proxytype(pools[i]->rpc_proxytype)) : "",
			pools[i]->rpc_proxy ? json_escape(pools[i]->rpc_proxy) : "",
			pools[i]->rpc_proxy ? "|" : "",
			json_escape(pools[i]->rpc_url));
		fprintf(fcfg, "\n\t\t\"user\" : \"%s\",", json_escape(pools[i]->rpc_user));
		fprintf(fcfg, "\n\t\t\"pass\" : \"%s\"\n\t}", json_escape(pools[i]->rpc_pass));
		}
	fputs("\n]\n", fcfg);

#ifdef HAVE_OPENCL
	if (nDevs) {
		/* Write GPU device values */
		fputs(",\n\"intensity\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, gpus[i].dynamic ? "%sd" : "%s%d", i > 0 ? "," : "", gpus[i].intensity);
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
				case KL_SCRYPT:
					fprintf(fcfg, "scrypt");
					break;
			}
		}
#ifdef USE_SCRYPT
		fputs("\",\n\"lookup-gap\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "",
				(int)gpus[i].opt_lg);
		fputs("\",\n\"thread-concurrency\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "",
				(int)gpus[i].opt_tc);
		fputs("\",\n\"shaders\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "",
				(int)gpus[i].shaders);
#endif
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
		fputs("\"", fcfg);
	}
#endif
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
	if (pool_strategy == POOL_BALANCE)
		fputs(",\n\"balance\" : true", fcfg);
	if (pool_strategy == POOL_LOADBALANCE)
		fputs(",\n\"load-balance\" : true", fcfg);
	if (pool_strategy == POOL_ROUNDROBIN)
		fputs(",\n\"round-robin\" : true", fcfg);
	if (pool_strategy == POOL_ROTATE)
		fprintf(fcfg, ",\n\"rotate\" : \"%d\"", opt_rotate_period);
#if defined(unix)
	if (opt_stderr_cmd && *opt_stderr_cmd)
		fprintf(fcfg, ",\n\"monitor\" : \"%s\"", json_escape(opt_stderr_cmd));
#endif // defined(unix)
	if (opt_kernel_path && *opt_kernel_path) {
		char *kpath = strdup(opt_kernel_path);
		if (kpath[strlen(kpath)-1] == '/')
			kpath[strlen(kpath)-1] = 0;
		fprintf(fcfg, ",\n\"kernel-path\" : \"%s\"", json_escape(kpath));
	}
	if (schedstart.enable)
		fprintf(fcfg, ",\n\"sched-time\" : \"%d:%d\"", schedstart.tm.tm_hour, schedstart.tm.tm_min);
	if (schedstop.enable)
		fprintf(fcfg, ",\n\"stop-time\" : \"%d:%d\"", schedstop.tm.tm_hour, schedstop.tm.tm_min);
	if (opt_socks_proxy && *opt_socks_proxy)
		fprintf(fcfg, ",\n\"socks-proxy\" : \"%s\"", json_escape(opt_socks_proxy));
#ifdef HAVE_OPENCL
	for(i = 0; i < nDevs; i++)
		if (gpus[i].deven == DEV_DISABLED)
			break;
	if (i < nDevs)
		for (i = 0; i < nDevs; i++)
			if (gpus[i].deven != DEV_DISABLED)
				fprintf(fcfg, ",\n\"device\" : \"%d\"", i);
#endif
	if (opt_api_allow)
		fprintf(fcfg, ",\n\"api-allow\" : \"%s\"", json_escape(opt_api_allow));
	if (strcmp(opt_api_description, PACKAGE_STRING) != 0)
		fprintf(fcfg, ",\n\"api-description\" : \"%s\"", json_escape(opt_api_description));
	if (opt_api_groups)
		fprintf(fcfg, ",\n\"api-groups\" : \"%s\"", json_escape(opt_api_groups));
	if (opt_icarus_options)
		fprintf(fcfg, ",\n\"icarus-options\" : \"%s\"", json_escape(opt_icarus_options));
	if (opt_icarus_timing)
		fprintf(fcfg, ",\n\"icarus-timing\" : \"%s\"", json_escape(opt_icarus_timing));
	fputs("\n}\n", fcfg);

	json_escape_free();
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
	wlogprint("[F]ailover only %s\n", opt_fail_only ? "enabled" : "disabled");
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
		disable_pool(pool);
		remove_pool(pool);
		goto updated;
	} else if (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		enable_pool(pool);
		switch_pools(pool);
		goto updated;
	} else if (!strncasecmp(&input, "d", 1)) {
		if (enabled_pools <= 1) {
			wlogprint("Cannot disable last pool");
			goto retry;
		}
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		disable_pool(pool);
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
		enable_pool(pool);
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
	} else if (!strncasecmp(&input, "f", 1)) {
		opt_fail_only ^= true;
		goto updated;
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
	wlogprint("[D]ebug:%s\n[P]er-device:%s\n[Q]uiet:%s\n[V]erbose:%s\n"
		  "[R]PC debug:%s\n[W]orkTime details:%s\nco[M]pact: %s\n"
		  "[L]og interval:%d\n",
		opt_debug ? "on" : "off",
	        want_per_device_stats? "on" : "off",
		opt_quiet ? "on" : "off",
		opt_log_output ? "on" : "off",
		opt_protocol ? "on" : "off",
		opt_worktime ? "on" : "off",
		opt_compact ? "on" : "off",
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
		opt_compact = false;
		want_per_device_stats = false;
		wlogprint("Output mode reset to normal\n");
		switch_compact();
		goto retry;
	} else if (!strncasecmp(&input, "d", 1)) {
		opt_debug ^= true;
		opt_log_output = opt_debug;
		if (opt_debug)
			opt_quiet = false;
		wlogprint("Debug mode %s\n", opt_debug ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "m", 1)) {
		opt_compact ^= true;
		wlogprint("Compact mode %s\n", opt_compact ? "enabled" : "disabled");
		switch_compact();
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
	} else if (!strncasecmp(&input, "w", 1)) {
		opt_worktime ^= true;
		wlogprint("WorkTime details %s\n", opt_worktime ? "enabled" : "disabled");
		goto retry;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}
#endif

void default_save_file(char *filename)
{
	if (default_config && *default_config) {
		strcpy(filename, default_config);
		return;
	}

#if defined(unix)
	if (getenv("HOME") && *getenv("HOME")) {
	        strcpy(filename, getenv("HOME"));
		strcat(filename, "/");
	}
	else
		strcpy(filename, "");
	strcat(filename, ".cgminer/");
	mkdir(filename, 0777);
#else
	strcpy(filename, "");
#endif
	strcat(filename, def_conf);
}

#ifdef HAVE_CURSES
static void set_options(void)
{
	int selected;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
retry:
	wlogprint("[Q]ueue: %d\n[S]cantime: %d\n[E]xpiry: %d\n"
		  "[W]rite config file\n[C]gminer restart\n",
		opt_queue, opt_scantime, opt_expiry);
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
	} else if  (!strncasecmp(&input, "w", 1)) {
		FILE *fcfg;
		char *str, filename[PATH_MAX], prompt[PATH_MAX + 50];

		default_save_file(filename);
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

	} else if (!strncasecmp(&input, "c", 1)) {
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

	pthread_detach(pthread_self());
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
		      uint64_t hashes_done)
{
	struct timeval temp_tv_end, total_diff;
	double secs;
	double local_secs;
	double utility, efficiency = 0.0;
	static double local_mhashes_done = 0;
	static double rolling = 0;
	double local_mhashes;
	bool showlog = false;
	char displayed_hashes[16], displayed_rolling[16];
	uint64_t dh64, dr64;

	local_mhashes = (double)hashes_done / 1000000.0;
	/* Update the last time this thread reported in */
	if (thr_id >= 0) {
		gettimeofday(&thr_info[thr_id].last, NULL);
		thr_info[thr_id].cgpu->device_last_well = time(NULL);
	}

	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

	/* So we can call hashmeter from a non worker thread */
	if (thr_id >= 0) {
		struct thr_info *thr = &thr_info[thr_id];
		struct cgpu_info *cgpu = thr_info[thr_id].cgpu;
		double thread_rolling = 0.0;
		int i;

		applog(LOG_DEBUG, "[thread %d: %"PRIu64" hashes, %.1f khash/sec]",
			thr_id, hashes_done, hashes_done / 1000 / secs);

		/* Rolling average for each thread and each device */
		decay_time(&thr->rolling, local_mhashes / secs);
		for (i = 0; i < cgpu->threads; i++)
			thread_rolling += cgpu->thr[i]->rolling;

		mutex_lock(&hash_lock);
		decay_time(&cgpu->rolling, thread_rolling);
		cgpu->total_mhashes += local_mhashes;
		mutex_unlock(&hash_lock);

		// If needed, output detailed, per-device stats
		if (want_per_device_stats) {
			struct timeval now;
			struct timeval elapsed;

			gettimeofday(&now, NULL);
			timersub(&now, &thr->cgpu->last_message_tv, &elapsed);
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
	timersub(&temp_tv_end, &total_tv_end, &total_diff);

	total_mhashes_done += local_mhashes;
	local_mhashes_done += local_mhashes;
	if (total_diff.tv_sec < opt_log_interval)
		/* Only update the total every opt_log_interval seconds */
		goto out_unlock;
	showlog = true;
	gettimeofday(&total_tv_end, NULL);

	local_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);
	decay_time(&rolling, local_mhashes_done / local_secs);
	global_hashrate = roundl(rolling) * 1000000;

	timersub(&total_tv_end, &total_tv_start, &total_diff);
	total_secs = (double)total_diff.tv_sec +
		((double)total_diff.tv_usec / 1000000.0);

	utility = total_accepted / total_secs * 60;
	efficiency = total_getworks ? total_accepted * 100.0 / total_getworks : 0.0;

	dh64 = (double)total_mhashes_done / total_secs * 1000000ull;
	dr64 = (double)rolling * 1000000ull;
	suffix_string(dh64, displayed_hashes, 4);
	suffix_string(dr64, displayed_rolling, 4);

	sprintf(statusline, "%s(%ds):%s (avg):%sh/s | Q:%d  A:%d  R:%d  HW:%d  E:%.0f%%  U:%.1f/m",
		want_per_device_stats ? "ALL " : "",
		opt_log_interval, displayed_rolling, displayed_hashes,
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

static void stratum_share_result(json_t *val, json_t *res_val, json_t *err_val,
				 struct stratum_share *sshare)
{
	struct work *work = &sshare->work;
	uint64_t sharediff = share_diff(work);
	char hashshow[65];
	uint32_t *hash32;
	char diffdisp[16];
	int intdiff;

	hash32 = (uint32_t *)(work->hash);
	intdiff = floor(work->work_difficulty);
	suffix_string(sharediff, diffdisp, 0);
	sprintf(hashshow, "%08lx Diff %s/%d%s", (unsigned long)(hash32[6]), diffdisp, intdiff,
		work->block? " BLOCK!" : "");
	share_result(val, res_val, err_val, work, hashshow, false, "");
}

/* Parses stratum json responses and tries to find the id that the request
 * matched to and treat it accordingly. */
static bool parse_stratum_response(struct pool *pool, char *s)
{
	json_t *val = NULL, *err_val, *res_val, *id_val;
	struct stratum_share *sshare;
	json_error_t err;
	bool ret = false;
	int id;

	val = JSON_LOADS(s, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");
	id_val = json_object_get(val, "id");

	if (json_is_null(id_val) || !id_val) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC non method decode failed: %s", ss);

		free(ss);

		goto out;
	}

	id = json_integer_value(id_val);
	mutex_lock(&sshare_lock);
	HASH_FIND_INT(stratum_shares, &id, sshare);
	if (sshare)
		HASH_DEL(stratum_shares, sshare);
	mutex_unlock(&sshare_lock);
	if (!sshare) {
		if (json_is_true(res_val))
			applog(LOG_NOTICE, "Accepted untracked stratum share from pool %d", pool->pool_no);
		else
			applog(LOG_NOTICE, "Rejected untracked stratum share from pool %d", pool->pool_no);
		goto out;
	}
	stratum_share_result(val, res_val, err_val, sshare);
	free(sshare);

	ret = true;
out:
	if (val)
		json_decref(val);

	return ret;
}

static void pool_resus(struct pool *pool);

static void clear_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	int cleared = 0;

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work.pool == pool) {
			HASH_DEL(stratum_shares, sshare);
			free(sshare);
			cleared++;
		}
	}
	mutex_unlock(&sshare_lock);

	if (cleared) {
		applog(LOG_WARNING, "Lost %d shares due to stratum disconnect on pool %d", cleared, pool->pool_no);
		pool->stale_shares++;
		total_stale++;
	}
}

/* One stratum thread per pool that has stratum waits on the socket checking
 * for new messages and for the integrity of the socket connection. We reset
 * the connection based on the integrity of the receive side only as the send
 * side will eventually expire data it fails to send. */
static void *stratum_thread(void *userdata)
{
	struct pool *pool = (struct pool *)userdata;

	pthread_detach(pthread_self());

	while (42) {
		struct timeval timeout;
		fd_set rd;
		char *s;

		if (unlikely(pool->removed))
			break;

		FD_ZERO(&rd);
		FD_SET(pool->sock, &rd);
		timeout.tv_sec = 90;
		timeout.tv_usec = 0;

		/* The protocol specifies that notify messages should be sent
		 * every minute so if we fail to receive any for 90 seconds we
		 * assume the connection has been dropped and treat this pool
		 * as dead */
		if (unlikely(select(pool->sock + 1, &rd, NULL, NULL, &timeout) < 1))
			s = NULL;
		else
			s = recv_line(pool);
		if (!s) {
			applog(LOG_INFO, "Stratum connection to pool %d interrupted", pool->pool_no);
			pool->getfail_occasions++;
			total_go++;

			/* If the socket to our stratum pool disconnects, all
			 * tracked submitted shares are lost and we will leak
			 * the memory if we don't discard their records. */
			clear_stratum_shares(pool);

			if (initiate_stratum(pool) && auth_stratum(pool))
				continue;

			pool_died(pool);
			while (!initiate_stratum(pool) || !auth_stratum(pool)) {
				if (pool->removed)
					goto out;
				sleep(30);
			}
			applog(LOG_INFO, "Stratum connection to pool %d resumed", pool->pool_no);
			pool_tclear(pool, &pool->idle);
			pool_resus(pool);
			continue;
		}

		if (!parse_method(pool, s) && !parse_stratum_response(pool, s))
			applog(LOG_INFO, "Unknown stratum msg: %s", s);
		free(s);
		if (pool->swork.clean) {
			struct work work;
			memset(&work, 0, sizeof(work));

			/* Generate a single work item to update the current
			 * block database */
			pool->swork.clean = false;
			gen_stratum_work(pool, &work);
			if (test_work_current(&work)) {
				/* Only accept a work restart if this stratum
				 * connection is from the current pool */
				if (pool == current_pool()) {
					restart_threads();
					applog(LOG_NOTICE, "Stratum from pool %d requested work restart", pool->pool_no);
				}
			} else
				applog(LOG_NOTICE, "Stratum from pool %d detected new block", pool->pool_no);
		}

	}

out:
	return NULL;
}

static void init_stratum_thread(struct pool *pool)
{
	if (unlikely(pthread_create(&pool->stratum_thread, NULL, stratum_thread, (void *)pool)))
		quit(1, "Failed to create stratum thread");
}

static void *longpoll_thread(void *userdata);

static bool stratum_works(struct pool *pool)
{
	applog(LOG_INFO, "Testing pool %d stratum %s", pool->pool_no, pool->stratum_url);
	if (!extract_sockaddr(pool, pool->stratum_url))
		return false;

	if (!initiate_stratum(pool))
		return false;

	return true;
}

static bool pool_active(struct pool *pool, bool pinging)
{
	struct timeval tv_getwork, tv_getwork_reply;
	bool ret = false;
	json_t *val;
	CURL *curl;
	int rolltime;

	if (pool->has_gbt)
		applog(LOG_DEBUG, "Retrieving block template from pool %s", pool->rpc_url);
	else
		applog(LOG_INFO, "Testing pool %s", pool->rpc_url);

	/* This is the central point we activate stratum when we can */
retry_stratum:
	if (pool->has_stratum) {
		/* We create the stratum thread for each pool just after
		 * successful authorisation. Once the auth flag has been set
		 * we never unset it and the stratum thread is responsible for
		 * setting/unsetting the active flag */
		if (pool->stratum_auth)
			return pool->stratum_active;
		if (!pool->stratum_active && !initiate_stratum(pool))
			return false;
		if (!auth_stratum(pool))
			return false;
		pool->stratum_auth = true;
		pool->idle = false;
		init_stratum_thread(pool);
		return true;
	}

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return false;
	}

	/* Probe for GBT support on first pass */
	if (!pool->probed && !opt_fix_protocol) {
		applog(LOG_DEBUG, "Probing for GBT support");
		val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
				    gbt_req, true, false, &rolltime, pool, false);
		if (val) {
			json_t *res_val, *mutables;
			int i, mutsize = 0;

			res_val = json_object_get(val, "result");
			if (res_val) {
				mutables = json_object_get(res_val, "mutable");
				mutsize = json_array_size(mutables);
			}

			for (i = 0; i < mutsize; i++) {
				json_t *arrval = json_array_get(mutables, i);

				if (json_is_string(arrval)) {
					const char *mutable = json_string_value(arrval);

					/* Only use GBT if it supports coinbase append */
					if (!strncasecmp(mutable, "coinbase/append", 15)) {
						pool->has_gbt = true;
						pool->rpc_req = gbt_req;
						break;
					}
				}
			}
			json_decref(val);
		}

		if (pool->has_gbt)
			applog(LOG_DEBUG, "GBT coinbase append support found, switching to GBT protocol");
		else
			applog(LOG_DEBUG, "No GBT coinbase append support found, using getwork protocol");
	}

	gettimeofday(&tv_getwork, NULL);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
			    pool->rpc_req, true, false, &rolltime, pool, false);
	gettimeofday(&tv_getwork_reply, NULL);

	/* Detect if a http getwork pool has an X-Stratum header at startup,
	 * and if so, switch to that in preference to getwork if it works */
	if (pool->stratum_url && !opt_fix_protocol && stratum_works(pool)) {
		applog(LOG_NOTICE, "Switching pool %d %s to %s", pool->pool_no, pool->rpc_url, pool->stratum_url);
		if (!pool->rpc_url)
			pool->rpc_url = strdup(pool->stratum_url);
		pool->has_stratum = true;
		curl_easy_cleanup(curl);

		goto retry_stratum;
	}

	if (val) {
		struct work *work = make_work();
		bool rc;

		rc = work_decode(pool, work, val);
		if (rc) {
			applog(LOG_DEBUG, "Successfully retrieved and deciphered work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			work->pool = pool;
			work->rolltime = rolltime;
			memcpy(&(work->tv_getwork), &tv_getwork, sizeof(struct timeval));
			memcpy(&(work->tv_getwork_reply), &tv_getwork_reply, sizeof(struct timeval));
			work->getwork_mode = GETWORK_MODE_TESTPOOL;
			calc_diff(work, 0);
			applog(LOG_DEBUG, "Pushing pooltest work to base pool");

			tq_push(thr_info[stage_thr_id].q, work);
			total_getworks++;
			pool->getwork_requested++;
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

		if (!pool->lp_started) {
			pool->lp_started = true;
			if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
				quit(1, "Failed to create pool longpoll thread");
		}
	} else {
		/* If we failed to parse a getwork, this could be a stratum
		 * url without the prefix stratum+tcp:// so let's check it */
		if (initiate_stratum(pool)) {
			pool->has_stratum = true;
			goto retry_stratum;
		}
		applog(LOG_DEBUG, "FAILED to retrieve work from pool %u %s",
		       pool->pool_no, pool->rpc_url);
		if (!pinging)
			applog(LOG_WARNING, "Pool %u slow/down or URL or credentials invalid", pool->pool_no);
	}
out:
	curl_easy_cleanup(curl);
	return ret;
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

static bool queue_request(void)
{
	int ts, tq, maxq = opt_queue + mining_threads;
	struct pool *pool, *cp;
	struct workio_cmd *wc;
	bool lagging;

	ts = total_staged();
	tq = global_queued();
	if (ts && ts + tq >= maxq)
		return true;

	cp = current_pool();
	lagging = !opt_fail_only && cp->lagging && !ts && cp->queued >= maxq;
	if (!lagging && cp->staged + cp->queued >= maxq)
		return true;

	pool = select_pool(lagging);
	if (pool->staged + pool->queued >= maxq)
		return true;

	inc_queued(pool);

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to calloc wc in queue_request");
		return false;
	}

	wc->cmd = WC_GET_WORK;
	wc->pool = pool;

	applog(LOG_DEBUG, "Queueing getwork request to work thread");

	/* send work request to workio thread */
	if (unlikely(!tq_push(thr_info[work_thr_id].q, wc))) {
		applog(LOG_ERR, "Failed to tq_push in queue_request");
		workio_cmd_free(wc);
		return false;
	}

	return true;
}

static struct work *hash_pop(const struct timespec *abstime)
{
	struct work *work = NULL, *tmp;
	int rc = 0, hc;

	mutex_lock(stgd_lock);
	while (!getq->frozen && !HASH_COUNT(staged_work) && !rc)
		rc = pthread_cond_timedwait(&getq->cond, stgd_lock, abstime);

	hc = HASH_COUNT(staged_work);

	if (likely(hc)) {
		/* Find clone work if possible, to allow masters to be reused */
		if (hc > staged_rollable) {
			HASH_ITER(hh, staged_work, work, tmp) {
				if (!work_rollable(work))
					break;
			}
		} else
			work = staged_work;
		HASH_DEL(staged_work, work);
		work->pool->staged--;
		if (work_rollable(work))
			staged_rollable--;
	}
	mutex_unlock(stgd_lock);

	queue_request();

	return work;
}

static bool reuse_work(struct work *work, struct pool *pool)
{
	if (pool->has_stratum) {
		if (!pool->stratum_active)
			return false;
		applog(LOG_DEBUG, "Reusing stratum work");
		gen_stratum_work(pool, work);
		return true;
	}

	if (pool->has_gbt) {
		if (pool->idle)
			return false;
		applog(LOG_DEBUG, "Reusing GBT work");
		gen_gbt_work(pool, work);
		return true;
	}

	if (can_roll(work) && should_roll(work)) {
		roll_work(work);
		return true;
	}
	return false;
}

/* Clones work by rolling it if possible, and returning a clone instead of the
 * original work item which gets staged again to possibly be rolled again in
 * the future */
static struct work *clone_work(struct work *work)
{
	int mrs = mining_threads + opt_queue - total_staged();
	struct work *work_clone;
	bool cloned;

	if (mrs < 1)
		return work;

	cloned = false;
	work_clone = make_clone(work);
	while (mrs-- > 0 && can_roll(work) && should_roll(work)) {
		applog(LOG_DEBUG, "Pushing rolled converted work to stage thread");
		if (unlikely(!stage_work(work_clone))) {
			cloned = false;
			break;
		}
		roll_work(work);
		work_clone = make_clone(work);
		/* Roll it again to prevent duplicates should this be used
		 * directly later on */
		roll_work(work);
		cloned = true;
	}

	if (cloned) {
		stage_work(work);
		return work_clone;
	}

	free_work(work_clone);

	return work;
}

static void gen_hash(unsigned char *data, unsigned char *hash, int len)
{
	unsigned char hash1[36];

	sha2(data, len, hash1, false);
	sha2(hash1, 32, hash, false);
}

/* Diff 1 is a 256 bit unsigned integer of
 * 0x00000000ffff0000000000000000000000000000000000000000000000000000
 * so we use a big endian 64 bit unsigned integer centred on the 5th byte to
 * cover a huge range of difficulty targets, though not all 256 bits' worth */
static void set_work_target(struct work *work, int diff)
{
	unsigned char rtarget[36], target[36];
	uint64_t *data64, h64;

	h64 = diffone;
	h64 /= (uint64_t)diff;
	memset(rtarget, 0, 32);
	data64 = (uint64_t *)(rtarget + 4);
	*data64 = htobe64(h64);
	swab256(target, rtarget);
	if (opt_debug) {
		char *htarget = bin2hex(target, 32);

		applog(LOG_DEBUG, "Generated target %s", htarget);
		free(htarget);
	}
	memcpy(work->target, target, 32);
}

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */
static void gen_stratum_work(struct pool *pool, struct work *work)
{
	unsigned char *coinbase, merkle_root[36], merkle_sha[68], *merkle_hash;
	char header[260], hash1[132], *nonce2;
	int len, cb1_len, n1_len, cb2_len, i;
	uint32_t *data32, *swap32;

	memset(work->job_id, 0, 64);
	memset(work->nonce2, 0, 64);
	memset(work->ntime, 0, 16);

	mutex_lock(&pool->pool_lock);

	/* Generate coinbase */
	nonce2 = bin2hex((const unsigned char *)&pool->nonce2, pool->n2size);
	pool->nonce2++;
	cb1_len = strlen(pool->swork.coinbase1) / 2;
	n1_len = strlen(pool->nonce1) / 2;
	cb2_len = strlen(pool->swork.coinbase2) / 2;
	len = cb1_len + n1_len + pool->n2size + cb2_len;
	coinbase = alloca(len + 1);
	hex2bin(coinbase, pool->swork.coinbase1, cb1_len);
	hex2bin(coinbase + cb1_len, pool->nonce1, n1_len);
	hex2bin(coinbase + cb1_len + n1_len, nonce2, pool->n2size);
	hex2bin(coinbase + cb1_len + n1_len + pool->n2size, pool->swork.coinbase2, cb2_len);

	/* Generate merkle root */
	gen_hash(coinbase, merkle_root, len);
	memcpy(merkle_sha, merkle_root, 32);
	for (i = 0; i < pool->swork.merkles; i++) {
		unsigned char merkle_bin[36];

		hex2bin(merkle_bin, pool->swork.merkle[i], 32);
		memcpy(merkle_sha + 32, merkle_bin, 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
	data32 = (uint32_t *)merkle_sha;
	swap32 = (uint32_t *)merkle_root;
	for (i = 0; i < 32 / 4; i++)
		swap32[i] = swab32(data32[i]);
	merkle_hash = (unsigned char *)bin2hex((const unsigned char *)merkle_root, 32);

	sprintf(header, "%s", pool->swork.bbversion);
	strcat(header, pool->swork.prev_hash);
	strcat(header, (char *)merkle_hash);
	strcat(header, pool->swork.ntime);
	strcat(header, pool->swork.nbit);
	strcat(header, "00000000"); /* nonce */
	strcat(header, "000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000");

	/* Store the stratum work diff to check it still matches the pool's
	 * stratum diff when submitting shares */
	work->sdiff = pool->swork.diff;

	/* Copy parameters required for share submission */
	sprintf(work->job_id, "%s", pool->swork.job_id);
	sprintf(work->nonce2, "%s", nonce2);
	sprintf(work->ntime, "%s", pool->swork.ntime);
	free(nonce2);

	mutex_unlock(&pool->pool_lock);

	applog(LOG_DEBUG, "Generated stratum merkle %s", merkle_hash);
	applog(LOG_DEBUG, "Generated stratum header %s", header);
	applog(LOG_DEBUG, "Work job_id %s nonce2 %s ntime %s", work->job_id, work->nonce2, work->ntime);

	free(merkle_hash);

	/* Convert hex data to binary data for work */
	if (unlikely(!hex2bin(work->data, header, 128)))
		quit(1, "Failed to convert header to data in gen_stratum_work");
	calc_midstate(work);
	sprintf(hash1, "00000000000000000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000000000000000010000");
	if (unlikely(!hex2bin(work->hash1, hash1, 64)))
		quit(1,  "Failed to convert hash1 in gen_stratum_work");

	set_work_target(work, work->sdiff);

	local_work++;
	work->pool = pool;
	work->stratum = true;
	work->blk.nonce = 0;
	work->id = total_work++;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_STRATUM;
	work->work_block = work_block;
	calc_diff(work, work->sdiff);

	gettimeofday(&work->tv_staged, NULL);
}

static void get_work(struct work *work, struct thr_info *thr, const int thr_id)
{
	struct timespec abstime = {0, 0};
	struct work *work_heap;
	struct timeval now;
	struct pool *pool;

	/* Tell the watchdog thread this thread is waiting on getwork and
	 * should not be restarted */
	thread_reportout(thr);

	if (opt_benchmark) {
		get_benchmark_work(work);
		goto out;
	}

	/* Reset these flags in case we switch pools with these work structs */
	work->stratum = work->gbt = false;
retry:
	pool = current_pool();

	if (reuse_work(work, pool))
		goto out;

	/* If we were unable to reuse work from a stratum pool, it implies the
	 * pool is inactive and unless we have another pool to grab work from
	 * we can only wait till it comes alive or another pool comes online */
	if (pool->has_stratum) {
		sleep(5);
		goto retry;
	}
	if (!pool->lagging && !total_staged() && global_queued() >= mining_threads + opt_queue) {
		struct cgpu_info *cgpu = thr->cgpu;
		bool stalled = true;
		int i;

		/* Check to see if all the threads on the device that called
		 * get_work are waiting on work and only consider the pool
		 * lagging if true */
		for (i = 0; i < cgpu->threads; i++) {
			if (!cgpu->thr[i]->getwork) {
				stalled = false;
				break;
			}
		}

		if (stalled && !pool_tset(pool, &pool->lagging)) {
			applog(LOG_WARNING, "Pool %d not providing work fast enough", pool->pool_no);
			pool->getfail_occasions++;
			total_go++;
		}
	}

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
		discard_work(work_heap);
		goto retry;
	}

	pool = work_heap->pool;
	/* If we make it here we have succeeded in getting fresh work */
	if (!work_heap->mined) {
		/* Only clear the lagging flag if we are staging them at a
		 * rate faster then we're using them */
		if (pool->lagging && total_staged())
			pool_tclear(pool, &pool->lagging);
		if (pool_tclear(pool, &pool->idle))
			pool_resus(pool);
	}

	memcpy(work, work_heap, sizeof(struct work));
	free_work(work_heap);

out:
	work->thr_id = thr_id;
	thread_reportin(thr);
	work->mined = true;
}

bool submit_work_sync(struct thr_info *thr, const struct work *work_in, struct timeval *tv_work_found)
{
	struct workio_cmd *wc;

	/* fill out work request message */
	wc = calloc(1, sizeof(*wc));
	if (unlikely(!wc)) {
		applog(LOG_ERR, "Failed to calloc wc in submit_work_sync");
		return false;
	}

	wc->work = make_work();
	wc->cmd = WC_SUBMIT_WORK;
	wc->thr = thr;
	memcpy(wc->work, work_in, sizeof(*work_in));
	if (tv_work_found)
		memcpy(&(wc->work->tv_work_found), tv_work_found, sizeof(struct timeval));

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

static bool hashtest(struct thr_info *thr, struct work *work)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[128];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	unsigned char hash2[32];
	uint32_t *hash2_32 = (uint32_t *)hash2;
	struct pool *pool = work->pool;
	int i;

	for (i = 0; i < 80 / 4; i++)
		swap32[i] = swab32(data32[i]);

	sha2(swap, 80, hash1, false);
	sha2(hash1, 32, hash2, false);

	for (i = 0; i < 32 / 4; i++)
		hash2_32[i] = swab32(hash2_32[i]);

	memcpy((void*)work->hash, hash2, 32);

	if (hash2_32[7] != 0) {
		applog(LOG_WARNING, "%s%d: invalid nonce - HW error",
				thr->cgpu->api->name, thr->cgpu->device_id);
		hw_errors++;
		thr->cgpu->hw_errors++;

		if (thr->cgpu->api->hw_error)
			thr->cgpu->api->hw_error(thr);

		return false;
	}

	if (work->stratum) {
		int diff;

		mutex_lock(&pool->pool_lock);
		diff = pool->swork.diff;
		mutex_unlock(&pool->pool_lock);

		/* Retarget share only if pool diff has dropped since we
		 * generated this work */
		if (unlikely(work->sdiff > diff)) {
			applog(LOG_DEBUG, "Share needs retargetting to match pool");
			set_work_target(work, diff);
		}
	}

	bool test = fulltest(work->hash, work->target);
	if (!test)
		applog(LOG_INFO, "Share below target");

	return test;
}

static bool test_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	if (opt_scrypt) {
		uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);

		*work_nonce = nonce;
		return true;
	}

	work->data[64 + 12 + 0] = (nonce >> 0) & 0xff;
	work->data[64 + 12 + 1] = (nonce >> 8) & 0xff;
	work->data[64 + 12 + 2] = (nonce >> 16) & 0xff;
	work->data[64 + 12 + 3] = (nonce >> 24) & 0xff;

	return hashtest(thr, work);
}

bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	struct timeval tv_work_found;
	gettimeofday(&tv_work_found, NULL);

	total_diff1++;
	thr->cgpu->diff1++;
	work->pool->diff1++;

	/* Do one last check before attempting to submit the work */
	/* Side effect: sets work->data for us */
	if (!test_nonce(thr, work, nonce))
		return true;

	return submit_work_sync(thr, work, &tv_work_found);
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

static void mt_disable(struct thr_info *mythr, const int thr_id,
		       struct device_api *api)
{
	applog(LOG_WARNING, "Thread %d being disabled", thr_id);
	mythr->rolling = mythr->cgpu->rolling = 0;
	applog(LOG_DEBUG, "Popping wakeup ping in miner thread");
	thread_reportout(mythr);
	do {
		tq_pop(mythr->q, NULL); /* Ignore ping that's popped */
	} while (mythr->pause);
	thread_reportin(mythr);
	applog(LOG_WARNING, "Thread %d being re-enabled", thr_id);
	if (api->thread_enable)
		api->thread_enable(mythr);
}

void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_api *api = cgpu->api;
	struct cgminer_stats *dev_stats = &(cgpu->cgminer_stats);
	struct cgminer_stats *pool_stats;
	struct timeval getwork_start;

	/* Try to cycle approximately 5 times before each log update */
	const long cycle = opt_log_interval / 5 ? : 1;
	struct timeval tv_start, tv_end, tv_workstart, tv_lastupdate;
	struct timeval diff, sdiff, wdiff = {0, 0};
	uint32_t max_nonce = api->can_limit_work ? api->can_limit_work(mythr) : 0xffffffff;
	int64_t hashes_done = 0;
	int64_t hashes;
	struct work *work = make_work();
	const bool primary = (!mythr->device_thread) || mythr->primary_thread;

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
		get_work(work, mythr, thr_id);
		cgpu->new_work = true;

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

			gettimeofday(&(work->tv_work_start), NULL);

			thread_reportin(mythr);
			hashes = api->scanhash(mythr, work, work->blk.nonce + max_nonce);
			thread_reportin(mythr);

			gettimeofday(&getwork_start, NULL);

			if (unlikely(hashes == -1)) {
				applog(LOG_ERR, "%s %d failure, disabling!", api->name, cgpu->device_id);
				cgpu->deven = DEV_DISABLED;

				cgpu->device_last_not_well = time(NULL);
				cgpu->device_not_well_reason = REASON_THREAD_ZERO_HASH;
				cgpu->thread_zero_hash_count++;

				mt_disable(mythr, thr_id, api);
			}

			hashes_done += hashes;
			if (hashes > cgpu->max_hashes)
				cgpu->max_hashes = hashes;

			gettimeofday(&tv_end, NULL);
			timersub(&tv_end, &tv_start, &diff);
			sdiff.tv_sec += diff.tv_sec;
			sdiff.tv_usec += diff.tv_usec;
			if (sdiff.tv_usec > 1000000) {
				++sdiff.tv_sec;
				sdiff.tv_usec -= 1000000;
			}

			timersub(&tv_end, &tv_workstart, &wdiff);

			if (unlikely((long)sdiff.tv_sec < cycle)) {
				int mult;

				if (likely(!api->can_limit_work || max_nonce == 0xffffffff))
					continue;

				mult = 1000000 / ((sdiff.tv_usec + 0x400) / 0x400) + 0x10;
				mult *= cycle;
				if (max_nonce > (0xffffffff * 0x400) / mult)
					max_nonce = 0xffffffff;
				else
					max_nonce = (max_nonce * mult) / 0x400;
			} else if (unlikely(sdiff.tv_sec > cycle) && api->can_limit_work)
				max_nonce = max_nonce * cycle / sdiff.tv_sec;
			else if (unlikely(sdiff.tv_usec > 100000) && api->can_limit_work)
				max_nonce = max_nonce * 0x400 / (((cycle * 1000000) + sdiff.tv_usec) / (cycle * 1000000 / 0x400));

			timersub(&tv_end, &tv_lastupdate, &diff);
			if (diff.tv_sec >= opt_log_interval) {
				hashmeter(thr_id, &diff, hashes_done);
				hashes_done = 0;
				tv_lastupdate = tv_end;
			}

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
				mt_disable(mythr, thr_id, api);

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
static void convert_to_work(json_t *val, int rolltime, struct pool *pool, struct timeval *tv_lp, struct timeval *tv_lp_reply)
{
	struct work *work;
	bool rc;

	work = make_work();

	rc = work_decode(pool, work, val);
	if (unlikely(!rc)) {
		applog(LOG_ERR, "Could not convert longpoll data to work");
		free_work(work);
		return;
	}
	work->pool = pool;
	work->rolltime = rolltime;
	memcpy(&(work->tv_getwork), tv_lp, sizeof(struct timeval));
	memcpy(&(work->tv_getwork_reply), tv_lp_reply, sizeof(struct timeval));
	calc_diff(work, 0);

	if (pool->enabled == POOL_REJECTING)
		work->mandatory = true;

	if (pool->has_gbt)
		gen_gbt_work(pool, work);
	work->longpoll = true;
	work->getwork_mode = GETWORK_MODE_LP;

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

	work = clone_work(work);

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

	if (cp->hdr_path || cp->has_gbt)
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
	if (pool->enabled == POOL_REJECTING || pool_strategy == POOL_LOADBALANCE || pool_strategy == POOL_BALANCE)
		return;

	while (pool != current_pool() && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		mutex_lock(&lp_lock);
		pthread_cond_wait(&lp_cond, &lp_lock);
		mutex_unlock(&lp_lock);
	}
}

static void *longpoll_thread(void *userdata)
{
	struct pool *cp = (struct pool *)userdata;
	/* This *pool is the source of the actual longpoll, not the pool we've
	 * tied it to */
	struct pool *pool = NULL;
	struct timeval start, reply, end;
	CURL *curl = NULL;
	int failures = 0;
	char lpreq[1024];
	char *lp_url;
	int rolltime;

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

	if (pool->has_gbt) {
		lp_url = pool->rpc_url;
		applog(LOG_WARNING, "GBT longpoll ID activated for %s", lp_url);
	} else {
		strcpy(lpreq, getwork_req);

		lp_url = pool->lp_url;
		if (cp == pool)
			applog(LOG_WARNING, "Long-polling activated for %s", lp_url);
		else
			applog(LOG_WARNING, "Long-polling activated for pool %s via %s", cp->rpc_url, lp_url);
	}

	while (42) {
		json_t *val, *soval;

		wait_lpcurrent(cp);

		gettimeofday(&start, NULL);

		/* Update the longpollid every time, but do it under lock to
		 * avoid races */
		if (pool->has_gbt) {
			mutex_lock(&pool->gbt_lock);
			sprintf(lpreq, "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": "
				"[{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"], "
				"\"longpollid\": \"%s\"}]}\n", pool->longpollid);
			mutex_unlock(&pool->gbt_lock);
		}

		/* Longpoll connections can be persistent for a very long time
		 * and any number of issues could have come up in the meantime
		 * so always establish a fresh connection instead of relying on
		 * a persistent one. */
		curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		val = json_rpc_call(curl, lp_url, pool->rpc_userpass,
				    lpreq, false, true, &rolltime, pool, false);

		gettimeofday(&reply, NULL);

		if (likely(val)) {
			soval = json_object_get(json_object_get(val, "result"), "submitold");
			if (soval)
				pool->submit_old = json_is_true(soval);
			else
				pool->submit_old = false;
			convert_to_work(val, rolltime, pool, &start, &reply);
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
			if (failures == 1)
				applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s", lp_url);
			sleep(30);
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
	int reaped = 0;

	gettimeofday(&now, NULL);
	mutex_lock(&pool->pool_lock);
	list_for_each_entry_safe(ent, iter, &pool->curlring, node) {
		if (pool->curls < 2)
			break;
		if (now.tv_sec - ent->tv.tv_sec > 300) {
			reaped++;
			pool->curls--;
			list_del(&ent->node);
			curl_easy_cleanup(ent->curl);
			free(ent);
		}
	}
	mutex_unlock(&pool->pool_lock);
	if (reaped)
		applog(LOG_DEBUG, "Reaped %d curl%s from pool %d", reaped, reaped > 1 ? "s" : "", pool->pool_no);
}

static void *watchpool_thread(void __maybe_unused *userdata)
{
	int intervals = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	while (42) {
		struct timeval now;
		int i;

		if (++intervals > 20)
			intervals = 0;
		gettimeofday(&now, NULL);

		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			if (!opt_benchmark)
				reap_curl(pool);

			if (pool->enabled == POOL_DISABLED || pool->has_stratum)
				continue;

			/* Apart from longpollid comms, we retrieve a fresh
			 * template if more than 30 seconds has elapsed since
			 * the last one to keep the data current and as a test
			 * for when the pool dies. */
			if (!pool->idle && pool->has_gbt && pool == current_pool() &&
			    now.tv_sec - pool->tv_template.tv_sec > 60) {
				if (!pool_active(pool, true))
					pool_died(pool);
			}

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 30) {
				gettimeofday(&pool->tv_idle, NULL);
				if (pool_active(pool, true) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
			}

			/* Get a rolling utility per pool over 10 mins */
			if (intervals > 19) {
				int shares = pool->diff1 - pool->last_shares;

				pool->last_shares = pool->diff1;
				pool->utility = (pool->utility + (double)shares * 0.63) / 1.63;
				pool->shares = pool->utility;
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
#define WATCHDOG_INTERVAL		3
#define WATCHDOG_SICK_TIME		60
#define WATCHDOG_DEAD_TIME		600
#define WATCHDOG_SICK_COUNT		(WATCHDOG_SICK_TIME/WATCHDOG_INTERVAL)
#define WATCHDOG_DEAD_COUNT		(WATCHDOG_DEAD_TIME/WATCHDOG_INTERVAL)

static void *watchdog_thread(void __maybe_unused *userdata)
{
	const unsigned int interval = WATCHDOG_INTERVAL;
	struct timeval zero_tv;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	memset(&zero_tv, 0, sizeof(struct timeval));
	gettimeofday(&rotate_tv, NULL);

	while (1) {
		int i;
		struct timeval now;

		sleep(interval);

		discard_stale();

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

		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			struct thr_info *thr = cgpu->thr[0];
			enum dev_enable *denable;
			char dev_str[8];
			int gpu;

			if (cgpu->api->get_stats)
			  cgpu->api->get_stats(cgpu);

			gpu = cgpu->device_id;
			denable = &cgpu->deven;
			sprintf(dev_str, "%s%d", cgpu->api->name, gpu);

#ifdef HAVE_ADL
			if (adl_active && cgpu->has_adl)
				gpu_autotune(gpu, denable);
			if (opt_debug && cgpu->has_adl) {
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

#ifdef WANT_CPUMINE
			if (!strcmp(cgpu->api->dname, "cpu"))
				continue;
#endif
			if (cgpu->status != LIFE_WELL && (now.tv_sec - thr->last.tv_sec < WATCHDOG_SICK_TIME)) {
				if (cgpu->status != LIFE_INIT)
				applog(LOG_ERR, "%s: Recovered, declaring WELL!", dev_str);
				cgpu->status = LIFE_WELL;
				cgpu->device_last_well = time(NULL);
			} else if (cgpu->status == LIFE_WELL && (now.tv_sec - thr->last.tv_sec > WATCHDOG_SICK_TIME)) {
				thr->rolling = cgpu->rolling = 0;
				cgpu->status = LIFE_SICK;
				applog(LOG_ERR, "%s: Idle for more than 60 seconds, declaring SICK!", dev_str);
				gettimeofday(&thr->sick, NULL);

				cgpu->device_last_not_well = time(NULL);
				cgpu->device_not_well_reason = REASON_DEV_SICK_IDLE_60;
				cgpu->dev_sick_idle_60_count++;
#ifdef HAVE_ADL
				if (adl_active && cgpu->has_adl && gpu_activity(gpu) > 50) {
					applog(LOG_ERR, "GPU still showing activity suggesting a hard hang.");
					applog(LOG_ERR, "Will not attempt to auto-restart it.");
				} else
#endif
				if (opt_restart) {
					applog(LOG_ERR, "%s: Attempting to restart", dev_str);
					reinit_device(cgpu);
				}
			} else if (cgpu->status == LIFE_SICK && (now.tv_sec - thr->last.tv_sec > WATCHDOG_DEAD_TIME)) {
				cgpu->status = LIFE_DEAD;
				applog(LOG_ERR, "%s: Not responded for more than 10 minutes, declaring DEAD!", dev_str);
				gettimeofday(&thr->sick, NULL);

				cgpu->device_last_not_well = time(NULL);
				cgpu->device_not_well_reason = REASON_DEV_DEAD_IDLE_600;
				cgpu->dev_dead_idle_600_count++;
			} else if (now.tv_sec - thr->sick.tv_sec > 60 &&
				   (cgpu->status == LIFE_SICK || cgpu->status == LIFE_DEAD)) {
				/* Attempt to restart a GPU that's sick or dead once every minute */
				gettimeofday(&thr->sick, NULL);
#ifdef HAVE_ADL
				if (adl_active && cgpu->has_adl && gpu_activity(gpu) > 50) {
					/* Again do not attempt to restart a device that may have hard hung */
				} else
#endif
				if (opt_restart)
					reinit_device(cgpu);
			}
		}
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
	double utility, efficiency = 0.0, displayed_hashes, work_util;
	bool mhash_base = true;

	timersub(&total_tv_end, &total_tv_start, &diff);
	hours = diff.tv_sec / 3600;
	mins = (diff.tv_sec % 3600) / 60;
	secs = diff.tv_sec % 60;

	utility = total_accepted / total_secs * 60;
	efficiency = total_getworks ? total_accepted * 100.0 / total_getworks : 0.0;
	work_util = total_diff1 / total_secs * 60;

	applog(LOG_WARNING, "\nSummary of runtime statistics:\n");
	applog(LOG_WARNING, "Started at %s", datestamp);
	if (total_pools == 1)
		applog(LOG_WARNING, "Pool: %s", pools[0]->rpc_url);
#ifdef WANT_CPUMINE
	if (opt_n_threads)
		applog(LOG_WARNING, "CPU hasher algorithm used: %s", algo_names[opt_algo]);
#endif
	applog(LOG_WARNING, "Runtime: %d hrs : %d mins : %d secs", hours, mins, secs);
	displayed_hashes = total_mhashes_done / total_secs;
	if (displayed_hashes < 1) {
		displayed_hashes *= 1000;
		mhash_base = false;
	}

	applog(LOG_WARNING, "Average hashrate: %.1f %shash/s", displayed_hashes, mhash_base? "Mega" : "Kilo");
	applog(LOG_WARNING, "Solved blocks: %d", found_blocks);
	applog(LOG_WARNING, "Best share difficulty: %s", best_share);
	applog(LOG_WARNING, "Queued work requests: %d", total_getworks);
	applog(LOG_WARNING, "Share submissions: %d", total_accepted + total_rejected);
	applog(LOG_WARNING, "Accepted shares: %d", total_accepted);
	applog(LOG_WARNING, "Rejected shares: %d", total_rejected);
	applog(LOG_WARNING, "Accepted difficulty shares: %1.f", total_diff_accepted);
	applog(LOG_WARNING, "Rejected difficulty shares: %1.f", total_diff_rejected);
	if (total_accepted || total_rejected)
		applog(LOG_WARNING, "Reject ratio: %.1f%%", (double)(total_rejected * 100) / (double)(total_accepted + total_rejected));
	applog(LOG_WARNING, "Hardware errors: %d", hw_errors);
	applog(LOG_WARNING, "Efficiency (accepted / queued): %.0f%%", efficiency);
	applog(LOG_WARNING, "Utility (accepted shares / min): %.2f/min", utility);
	applog(LOG_WARNING, "Work Utility (diff1 shares solved / min): %.2f/min\n", work_util);

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
			applog(LOG_WARNING, " Accepted difficulty shares: %1.f", pool->diff_accepted);
			applog(LOG_WARNING, " Rejected difficulty shares: %1.f", pool->diff_rejected);
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

void add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass)
{
	url = get_proxy(url, pool);

	pool->rpc_url = url;
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);

	enable_pool(pool);

	/* Prevent noise on startup */
	pool->lagging = true;

	/* Test the pool is not idle if we're live running, otherwise
	 * it will be tested separately */
	if (live && !pool_active(pool, false))
		pool->idle = true;
}

#ifdef HAVE_CURSES
static bool input_pool(bool live)
{
	char *url = NULL, *user = NULL, *pass = NULL;
	struct pool *pool;
	bool ret = false;

	immedok(logwin, true);
	wlogprint("Input server details.\n");

	url = curses_input("URL");
	if (!url)
		goto out;

	user = curses_input("Username");
	if (!user)
		goto out;

	pass = curses_input("Password");
	if (!pass)
		goto out;

	pool = add_pool();

	if (detect_stratum(pool, url))
		url = strdup(pool->stratum_url);
	else if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
		char *httpinput;

		httpinput = malloc(255);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		strcpy(httpinput, "http://");
		strncat(httpinput, url, 248);
		free(url);
		url = httpinput;
	}

	add_pool_details(pool, live, url, user, pass);
	ret = true;
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

	if (r < 0) {
		perror("pipe - failed to create pipe for --monitor");
		exit(1);
	}

	// Make stderr write end of pipe
	fflush(stderr);
	r = dup2(pfd[1], 2);
	if (r < 0) {
		perror("dup2 - failed to alias stderr to write end of pipe for --monitor");
		exit(1);
	}
	r = close(pfd[1]);
	if (r < 0) {
		perror("close - failed to close write end of pipe for --monitor");
		exit(1);
	}

	// Don't allow a dying monitor to kill the main process
	sighandler_t sr0 = signal(SIGPIPE, SIG_IGN);
	sighandler_t sr1 = signal(SIGPIPE, SIG_IGN);
	if (SIG_ERR == sr0 || SIG_ERR == sr1) {
		perror("signal - failed to edit signal mask for --monitor");
		exit(1);
	}

	// Fork a child process
	forkpid = fork();
	if (forkpid < 0) {
		perror("fork - failed to fork child process for --monitor");
		exit(1);
	}

	// Child: launch monitor command
	if (0 == forkpid) {
		// Make stdin read end of pipe
		r = dup2(pfd[0], 0);
		if (r < 0) {
			perror("dup2 - in child, failed to alias read end of pipe to stdin for --monitor");
			exit(1);
		}
		close(pfd[0]);
		if (r < 0) {
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
	if (r < 0) {
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

#ifdef USE_MODMINER
extern struct device_api modminer_api;
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
#ifdef HAVE_CURSES
	adj_width(mining_threads, &dev_width);
#endif
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
	else {
		d = malloc(sizeof(*d));
		memcpy(d->name, cgpu->api->name, sizeof(d->name));
		cgpu->device_id = d->lastid = 0;
		HASH_ADD_STR(devids, name, d);
	}
	devices = realloc(devices, sizeof(struct cgpu_info *) * (total_devices + 2));
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
	char *s;
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
	mutex_init(&console_lock);
	mutex_init(&control_lock);
	mutex_init(&sharelog_lock);
	mutex_init(&ch_lock);
	mutex_init(&sshare_lock);
	rwlock_init(&blk_lock);
	rwlock_init(&netacc_lock);

	mutex_init(&lp_lock);
	if (unlikely(pthread_cond_init(&lp_cond, NULL)))
		quit(1, "Failed to pthread_cond_init lp_cond");

	mutex_init(&restart_lock);
	if (unlikely(pthread_cond_init(&restart_cond, NULL)))
		quit(1, "Failed to pthread_cond_init restart_cond");

	sprintf(packagename, "%s %s", PACKAGE, VERSION);

#ifdef WANT_CPUMINE
	init_max_name_len();
#endif

	handler.sa_handler = &sighandler;
	handler.sa_flags = 0;
	sigemptyset(&handler.sa_mask);
	sigaction(SIGTERM, &handler, &termhandler);
	sigaction(SIGINT, &handler, &inthandler);
#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	opt_kernel_path = alloca(PATH_MAX);
	strcpy(opt_kernel_path, CGMINER_PREFIX);
	cgminer_path = alloca(PATH_MAX);
	s = strdup(argv[0]);
	strcpy(cgminer_path, dirname(s));
	free(s);
	strcat(cgminer_path, "/");
#ifdef WANT_CPUMINE
	// Hack to make cgminer silent when called recursively on WIN32
	int skip_to_bench = 0;
	#if defined(WIN32)
		char buf[32];
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

#ifdef HAVE_OPENCL
	memset(gpus, 0, sizeof(gpus));
	for (i = 0; i < MAX_GPUDEVICES; i++)
		gpus[i].dynamic = true;
#endif

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

	if (opt_restart) {
		struct sigaction shandler;

		shandler.sa_handler = &seghandler;
		shandler.sa_flags = 0;
		sigemptyset(&shandler.sa_mask);
		sigaction(SIGSEGV, &shandler, &segvhandler);
		sigaction(SIGILL, &shandler, &illhandler);
#ifndef WIN32
		sigaction(SIGBUS, &shandler, &bushandler);
#endif
	}
	if (opt_benchmark) {
		struct pool *pool;

		pool = add_pool();
		pool->rpc_url = malloc(255);
		strcpy(pool->rpc_url, "Benchmark");
		pool->rpc_user = pool->rpc_url;
		pool->rpc_pass = pool->rpc_url;
		enable_pool(pool);
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
					applog(LOG_WARNING, "Start cgminer with -T to see what failed to load.");
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
#ifdef USE_SCRYPT
	if (opt_scrypt)
		set_scrypt_algo(&opt_algo);
	else
#endif
	if (0 <= opt_bench_algo) {
		double rate = bench_algo_stage3(opt_bench_algo);

		if (!skip_to_bench)
			printf("%.5f (%s)\n", rate, algo_names[opt_bench_algo]);
		else {
			// Write result to shared memory for parent
#if defined(WIN32)
				char unique_name[64];

				if (GetEnvironmentVariable("CGMINER_SHARED_MEM", unique_name, 32)) {
					HANDLE map_handle = CreateFileMapping(
						INVALID_HANDLE_VALUE,   // use paging file
						NULL,                   // default security attributes
						PAGE_READWRITE,         // read/write access
						0,                      // size: high 32-bits
						4096,			// size: low 32-bits
						unique_name		// name of map object
					);
					if (NULL != map_handle) {
						void *shared_mem = MapViewOfFile(
							map_handle,	// object to map view of
							FILE_MAP_WRITE, // read/write access
							0,              // high offset:  map from
							0,              // low offset:   beginning
							0		// default: map entire file
						);
						if (NULL != shared_mem)
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
	gpu_threads = 0;
#endif

#ifdef USE_ICARUS
	if (!opt_scrypt)
		icarus_api.api_detect();
#endif

#ifdef USE_BITFORCE
	if (!opt_scrypt)
		bitforce_api.api_detect();
#endif

#ifdef USE_MODMINER
	if (!opt_scrypt)
		modminer_api.api_detect();
#endif

#ifdef USE_ZTEX
	if (!opt_scrypt)
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

	if (!opt_compact) {
		logstart += total_devices;
		logcursor = logstart + 1;
#ifdef HAVE_CURSES
		check_winsizes();
#endif
	}

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
		pool->cgminer_pool_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

		if (!pool->rpc_userpass) {
			if (!pool->rpc_user || !pool->rpc_pass)
				quit(1, "No login credentials supplied for pool %u %s", i, pool->rpc_url);
			pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
			if (!pool->rpc_userpass)
				quit(1, "Failed to malloc userpass");
			sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);
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

		enable_pool(pool);
		pool->idle = true;
	}

	applog(LOG_NOTICE, "Probing for an alive pool");
	do {
		/* Look for at least one active pool before starting */
		for (i = 0; i < total_pools; i++) {
			struct pool *pool  = pools[i];
			if (pool_active(pool, false)) {
				pool_tclear(pool, &pool->idle);
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
				applog(LOG_ERR, "Press any key to exit, or cgminer will try again in 15s.");
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

	// Start threads
	k = 0;
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		cgpu->thr = malloc(sizeof(*cgpu->thr) * (cgpu->threads+1));
		cgpu->thr[cgpu->threads] = NULL;
		cgpu->status = LIFE_INIT;

		for (j = 0; j < cgpu->threads; ++j, ++k) {
			thr = &thr_info[k];
			thr->id = k;
			thr->cgpu = cgpu;
			thr->device_thread = j;

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

			thread_reportout(thr);

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				quit(1, "thread %d create failed", thr->id);

			cgpu->thr[j] = thr;
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

#ifdef HAVE_OPENCL
	/* Create reinit gpu thread */
	gpur_thr_id = mining_threads + 4;
	thr = &thr_info[gpur_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "tq_new failed for gpur_thr_id");
	if (thr_info_create(thr, NULL, reinit_gpu, thr))
		quit(1, "reinit_gpu thread create failed");
#endif	

	/* Create API socket thread */
	api_thr_id = mining_threads + 5;
	thr = &thr_info[api_thr_id];
	if (thr_info_create(thr, NULL, api_thread, thr))
		quit(1, "API thread create failed");

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

	for (i = 0; i < mining_threads + opt_queue; i++)
		queue_request();

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

