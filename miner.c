/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2011-2013 Luke Dashjr
 * Copyright 2012-2013 Andrew Smith
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

#include <limits.h>
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
#include <dirent.h>

#ifndef WIN32
#include <sys/resource.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif
#include <ccan/opt/opt.h>
#include <jansson.h>
#include <curl/curl.h>
#include <libgen.h>
#include <sha2.h>
#include <utlist.h>

#include <blkmaker.h>
#include <blkmaker_jansson.h>
#include <blktemplate.h>

#include "compat.h"
#include "deviceapi.h"
#include "miner.h"
#include "findnonce.h"
#include "adl.h"
#include "driver-cpu.h"
#include "driver-opencl.h"
#include "bench_block.h"
#include "scrypt.h"

#ifdef USE_AVALON
#include "driver-avalon.h"
#endif

#ifdef USE_X6500
#include "ft232r.h"
#endif

#if defined(unix)
	#include <errno.h>
	#include <fcntl.h>
	#include <sys/wait.h>
#endif

#ifdef USE_SCRYPT
#include "scrypt.h"
#endif

#if defined(USE_AVALON) || defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_MODMINER) || defined(USE_X6500) || defined(USE_ZTEX)
#	define USE_FPGA
#	define USE_FPGA_SERIAL
#endif

struct strategies strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
	{ "Balance" },
};

static char packagename[256];

bool opt_protocol;
static bool opt_benchmark;
static bool want_longpoll = true;
static bool want_gbt = true;
static bool want_getwork = true;
#if BLKMAKER_VERSION > 1
struct _cbscript_t {
	char *data;
	size_t sz;
};
static struct _cbscript_t opt_coinbase_script;
static uint32_t template_nonce;
#endif
#if BLKMAKER_VERSION < 1
const
#endif
char *opt_coinbase_sig;
char *request_target_str;
float request_pdiff = 1.0;
double request_bdiff;
static bool want_stratum = true;
bool have_longpoll;
int opt_skip_checks;
bool want_per_device_stats;
bool use_syslog;
bool opt_quiet_work_updates;
bool opt_quiet;
bool opt_realquiet;
bool opt_loginput;
bool opt_compact;
bool opt_show_procs;
const int opt_cutofftemp = 95;
int opt_hysteresis = 3;
static int opt_retries = -1;
int opt_fail_pause = 5;
int opt_log_interval = 5;
int opt_queue = 1;
int opt_scantime = 60;
int opt_expiry = 120;
int opt_expiry_lp = 3600;
int opt_bench_algo = -1;
static const bool opt_time = true;
unsigned long long global_hashrate;

#ifdef HAVE_OPENCL
int opt_dynamic_interval = 7;
int nDevs;
int opt_g_threads = -1;
int gpu_threads;
#endif
#ifdef USE_SCRYPT
static char detect_algo = 1;
bool opt_scrypt;
#else
static char detect_algo;
#endif
bool opt_restart = true;
static bool opt_nogpu;

struct string_elist *scan_devices;
bool opt_force_dev_init;
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
static int opt_submit_threads = 0x40;
bool opt_fail_only;
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
bool opt_disable_pool;
char *opt_icarus_options = NULL;
char *opt_icarus_timing = NULL;
bool opt_worktime;
#ifdef USE_AVALON
char *opt_avalon_options = NULL;
#endif

char *opt_kernel_path;
char *cgminer_path;

#if defined(USE_BITFORCE)
bool opt_bfl_noncerange;
#endif
#define QUIET	(opt_quiet || opt_realquiet)

struct thr_info *control_thr;
struct thr_info **mining_thr;
static int gwsched_thr_id;
static int stage_thr_id;
static int watchpool_thr_id;
static int watchdog_thr_id;
#ifdef HAVE_CURSES
static int input_thr_id;
#endif
int gpur_thr_id;
static int api_thr_id;
static int total_control_threads;

pthread_mutex_t hash_lock;
static pthread_mutex_t *stgd_lock;
pthread_mutex_t console_lock;
cglock_t ch_lock;
static pthread_rwlock_t blk_lock;
static pthread_mutex_t sshare_lock;

pthread_rwlock_t netacc_lock;
pthread_rwlock_t mining_thr_lock;
pthread_rwlock_t devices_lock;

static pthread_mutex_t lp_lock;
static pthread_cond_t lp_cond;

pthread_cond_t gws_cond;

bool shutting_down;

double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;
static struct timeval miner_started;

cglock_t control_lock;
pthread_mutex_t stats_lock;

static pthread_mutex_t submitting_lock;
static int total_submitting;
static struct work *submit_waiting;
notifier_t submit_waiting_notifier;

int hw_errors;
int total_accepted, total_rejected, total_diff1;
int total_getworks, total_stale, total_discarded;
uint64_t total_bytes_xfer;
double total_diff_accepted, total_diff_rejected, total_diff_stale;
static int staged_rollable;
unsigned int new_blocks;
unsigned int found_blocks;

unsigned int local_work;
unsigned int total_go, total_ro;

struct pool **pools;
static struct pool *currentpool = NULL;

int total_pools, enabled_pools;
enum pool_strategy pool_strategy = POOL_FAILOVER;
int opt_rotate_period;
static int total_urls, total_users, total_passes;

static
#ifndef HAVE_CURSES
const
#endif
bool curses_active;

static char current_block[40];

/* Protected by ch_lock */
static char *current_hash;
static uint32_t current_block_id;
char *current_fullhash;

static char datestamp[40];
static char blocktime[32];
struct timeval block_timeval;
static char best_share[8] = "0";
double current_diff = 0xFFFFFFFFFFFFFFFFULL;
static char block_diff[8];
static char net_hashrate[10];
uint64_t best_diff = 0;

static bool known_blkheight_current;
static uint32_t known_blkheight;
static uint32_t known_blkheight_blkid;

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
	struct work *work;
	int id;
	time_t sshare_time;
};

static struct stratum_share *stratum_shares = NULL;

char *opt_socks_proxy = NULL;

static const char def_conf[] = "bfgminer.conf";
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

struct sigaction termhandler, inthandler;

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
	struct tm tm;
	bool within_range;

	if (!schedstart.enable && !schedstop.enable)
		return true;

	cgtime(&tv);
	localtime_r(&tv.tv_sec, &tm);

	// NOTE: This is delicately balanced so that should_run is always false if schedstart==schedstop
	if (time_before(&schedstop.tm, &schedstart.tm))
		within_range = (time_before(&tm, &schedstop.tm) || !time_before(&tm, &schedstart.tm));
	else
		within_range = (time_before(&tm, &schedstop.tm) && !time_before(&tm, &schedstart.tm));

	if (within_range && !schedstop.enable)
		/* This is a once off event with no stop time set */
		schedstart.enable = false;

	return within_range;
}

void get_datestamp(char *f, struct timeval *tv)
{
	struct tm _tm;
	struct tm *tm = &_tm;

	localtime_r(&tv->tv_sec, tm);
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
	struct tm _tm;
	struct tm *tm = &_tm;

	localtime_r(&tv->tv_sec, tm);
	sprintf(f, "[%02d:%02d:%02d]",
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static void applog_and_exit(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);

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

struct thr_info *get_thread(int thr_id)
{
	struct thr_info *thr;

	rd_lock(&mining_thr_lock);
	thr = mining_thr[thr_id];
	rd_unlock(&mining_thr_lock);

	return thr;
}

static struct cgpu_info *get_thr_cgpu(int thr_id)
{
	struct thr_info *thr = get_thread(thr_id);

	return thr->cgpu;
}

struct cgpu_info *get_devices(int id)
{
	struct cgpu_info *cgpu;

	rd_lock(&devices_lock);
	cgpu = devices[id];
	rd_unlock(&devices_lock);

	return cgpu;
}

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
	cgpu = get_thr_cgpu(thr_id);
	pool = work->pool;
	t = (unsigned long int)(work->tv_work_found.tv_sec);
	target = bin2hex(work->target, sizeof(work->target));
	hash = bin2hex(work->hash, sizeof(work->hash));
	data = bin2hex(work->data, sizeof(work->data));

	// timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
	rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->proc_repr_ns, thr_id, hash, data);
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

/* Return value is ignored if not called from add_pool_details */
struct pool *add_pool(void)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to malloc pool in add_pool");
	pool->pool_no = pool->prio = total_pools;
	mutex_init(&pool->last_work_lock);
	mutex_init(&pool->pool_lock);
	if (unlikely(pthread_cond_init(&pool->cr_cond, NULL)))
		quit(1, "Failed to pthread_cond_init in add_pool");
	cglock_init(&pool->data_lock);
	mutex_init(&pool->stratum_lock);
	pool->swork.transparency_time = (time_t)-1;

	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;

	pool->rpc_proxy = NULL;

	pool->sock = INVSOCK;
	pool->lp_socket = CURL_SOCKET_BAD;

	pools = realloc(pools, sizeof(struct pool *) * (total_pools + 2));
	pools[total_pools++] = pool;

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

	cg_rlock(&control_lock);
	pool = currentpool;
	cg_runlock(&control_lock);

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

char *set_strdup(const char *arg, char **p)
{
	*p = strdup((char *)arg);
	return NULL;
}

#if BLKMAKER_VERSION > 1
static char *set_b58addr(const char *arg, struct _cbscript_t *p)
{
	size_t scriptsz = blkmk_address_to_script(NULL, 0, arg);
	if (!scriptsz)
		return "Invalid address";
	char *script = malloc(scriptsz);
	if (blkmk_address_to_script(script, scriptsz, arg) != scriptsz) {
		free(script);
		return "Failed to convert address to script";
	}
	p->data = script;
	p->sz = scriptsz;
	return NULL;
}
#endif

static void bdiff_target_leadzero(unsigned char *target, double diff);

char *set_request_diff(const char *arg, float *p)
{
	unsigned char target[32];
	char *e = opt_set_floatval(arg, p);
	if (e)
		return e;
	
	request_bdiff = (double)*p * 0.9999847412109375;
	bdiff_target_leadzero(target, request_bdiff);
	request_target_str = bin2hex(target, 32);
	
	return NULL;
}

#ifdef USE_FPGA_SERIAL
#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

static
char* add_serial_all(char*arg, char*p) {
#ifdef HAVE_LIBUDEV

	struct udev *udev = udev_new();
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	struct udev_list_entry *list_entry;

	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_property(enumerate, "ID_SERIAL", "*");
	udev_enumerate_scan_devices(enumerate);
	udev_list_entry_foreach(list_entry, udev_enumerate_get_list_entry(enumerate)) {
		struct udev_device *device = udev_device_new_from_syspath(
			udev_enumerate_get_udev(enumerate),
			udev_list_entry_get_name(list_entry)
		);
		if (!device)
			continue;

		const char *devpath = udev_device_get_devnode(device);
		if (devpath) {
			size_t pLen = p - arg;
			size_t dLen = strlen(devpath) + 1;
			char dev[dLen + pLen];
			memcpy(dev, arg, pLen);
			memcpy(&dev[pLen], devpath, dLen);
			applog(LOG_DEBUG, "scan-serial: libudev all-adding %s", dev);
			string_elist_add(dev, &scan_devices);
		}

		udev_device_unref(device);
	}
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	return NULL;

#elif defined(WIN32)

	size_t bufLen = 0x10;  // temp!
tryagain: ;
	char buf[bufLen];
	if (!QueryDosDevice(NULL, buf, bufLen)) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			bufLen *= 2;
			applog(LOG_DEBUG, "scan-serial: QueryDosDevice returned insufficent buffer error; enlarging to %lx", (unsigned long)bufLen);
			goto tryagain;
		}
		return "scan-serial: Error occurred trying to enumerate COM ports with QueryDosDevice";
	}
	size_t tLen = p - arg;
	char dev[12 + tLen];
	memcpy(dev, arg, tLen);
	memcpy(&dev[tLen], "\\\\.\\", 4);
	char *devp = &dev[tLen + 4];
	for (char *t = buf; *t; t += tLen) {
		tLen = strlen(t) + 1;
		if (strncmp("COM", t, 3))
			continue;
		memcpy(devp, t, tLen);
		applog(LOG_DEBUG, "scan-serial: QueryDosDevice all-adding %s", dev);
		string_elist_add(dev, &scan_devices);
	}
	return NULL;

#else

	DIR *D;
	struct dirent *de;
	const char devdir[] = "/dev";
	const size_t devdirlen = sizeof(devdir) - 1;
	char devpath[sizeof(devdir) + NAME_MAX];
	char *devfile = devpath + devdirlen + 1;
	
	D = opendir(devdir);
	if (!D)
		return "scan-serial 'all' is not supported on this platform";
	memcpy(devpath, devdir, devdirlen);
	devpath[devdirlen] = '/';
	while ( (de = readdir(D)) ) {
		if (strncmp(de->d_name, "tty", 3))
			continue;
		if (strncmp(&de->d_name[3], "USB", 3) && strncmp(&de->d_name[3], "ACM", 3))
			continue;
		
		strcpy(devfile, de->d_name);
		applog(LOG_DEBUG, "scan-serial: /dev glob all-adding %s", devpath);
		string_elist_add(devpath, &scan_devices);
	}
	closedir(D);
	
	return NULL;

#endif
}

static char *add_serial(char *arg)
{
	char *p = strchr(arg, ':');
	if (p)
		++p;
	else
		p = arg;
	if (!strcasecmp(p, "all")) {
		return add_serial_all(arg, p);
	}

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

	if (!strncasecmp(url, "stratum+tcp://", 14)) {
		pool->rpc_url = strdup(url);
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

	if (detect_stratum(pool, arg))
		return NULL;

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

	if (total_users != total_passes)
		return "User + pass options must be balanced before userpass";
	++total_users;
	++total_passes;
	if (total_users > total_pools)
		add_pool();

	pool = pools[total_users - 1];
	updup = strdup(arg);
	opt_set_charp(arg, &pool->rpc_userpass);
	pool->rpc_user = strtok(updup, ":");
	if (!pool->rpc_user)
		return "Failed to find : delimited user info";
	pool->rpc_pass = strtok(NULL, ":");
	if (!pool->rpc_pass)
		pool->rpc_pass = "";

	return NULL;
}

static char *set_pool_priority(const char *arg)
{
	struct pool *pool;

	if (!total_pools)
		return "Usage of --pool-priority before pools are defined does not make sense";

	pool = pools[total_pools - 1];
	opt_set_intval(arg, &pool->prio);

	return NULL;
}

static char *set_pool_proxy(const char *arg)
{
	struct pool *pool;

	if (!total_pools)
		return "Usage of --pool-proxy before pools are defined does not make sense";

	if (!our_curl_supports_proxy_uris())
		return "Your installed cURL library does not support proxy URIs. At least version 7.21.7 is required.";

	pool = pools[total_pools - 1];
	opt_set_charp(arg, &pool->rpc_proxy);

	return NULL;
}

static char *set_pool_force_rollntime(const char *arg)
{
	struct pool *pool;
	
	if (!total_pools)
		return "Usage of --force-rollntime before pools are defined does not make sense";
	
	pool = pools[total_pools - 1];
	opt_set_intval(arg, &pool->force_rollntime);
	
	return NULL;
}

static char *enable_debug(bool *flag)
{
	*flag = true;
	opt_debug_console = true;
	/* Turn on verbose output, too. */
	opt_log_output = true;
	return NULL;
}

static char *set_schedtime(const char *arg, struct schedtime *st)
{
	if (sscanf(arg, "%d:%d", &st->tm.tm_hour, &st->tm.tm_min) != 2)
	{
		if (strcasecmp(arg, "now"))
		return "Invalid time set, should be HH:MM";
	} else
		schedstop.tm.tm_sec = 0;
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

static char *temp_cutoff_str = "";
static char *temp_target_str = "";

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

char *set_temp_target(char *arg)
{
	int val;

	if (!(arg && arg[0]))
		return "Invalid parameters for set temp target";
	val = atoi(arg);
	if (val < 0 || val > 200)
		return "Invalid value passed to set temp target";
	temp_target_str = arg;

	return NULL;
}

// For a single element string, this always returns the number (for all calls)
// For multi-element strings, it returns each element as a number in order, and 0 when there are no more
static int temp_strtok(char *base, char **n)
{
	char *i = *n;
	char *p = strchr(i, ',');
	if (p) {
		p[0] = '\0';
		*n = &p[1];
	}
	else
	if (base != i)
		*n = strchr(i, '\0');
	return atoi(i);
}

static void load_temp_config()
{
	int i, val = 0, target_off;
	char *cutoff_n, *target_n;
	struct cgpu_info *cgpu;

	cutoff_n = temp_cutoff_str;
	target_n = temp_target_str;

	for (i = 0; i < total_devices; ++i) {
		cgpu = get_devices(i);
		
		// cutoff default may be specified by driver during probe; otherwise, opt_cutofftemp (const)
		if (!cgpu->cutofftemp)
			cgpu->cutofftemp = opt_cutofftemp;
		
		// target default may be specified by driver, and is moved with offset; otherwise, offset minus 6
		if (cgpu->targettemp)
			target_off = cgpu->targettemp - cgpu->cutofftemp;
		else
			target_off = -6;
		
		val = temp_strtok(temp_cutoff_str, &cutoff_n);
		if (val < 0 || val > 200)
			quit(1, "Invalid value passed to set temp cutoff");
		if (val)
			cgpu->cutofftemp = val;
		
		val = temp_strtok(temp_target_str, &target_n);
		if (val < 0 || val > 200)
			quit(1, "Invalid value passed to set temp target");
		if (val)
			cgpu->targettemp = val;
		else
			cgpu->targettemp = cgpu->cutofftemp + target_off;
		
		applog(LOG_DEBUG, "%"PRIprepr": Set temperature config: target=%d cutoff=%d",
		       cgpu->proc_repr,
		       cgpu->targettemp, cgpu->cutofftemp);
	}
	if (cutoff_n != temp_cutoff_str && cutoff_n[0])
		quit(1, "Too many values passed to set temp cutoff");
	if (target_n != temp_target_str && target_n[0])
		quit(1, "Too many values passed to set temp target");
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

#ifdef USE_AVALON
static char *set_avalon_options(const char *arg)
{
	opt_set_charp(arg, &opt_avalon_options);

	return NULL;
}
#endif

__maybe_unused
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
		     "\tfastauto*\tQuick benchmark at startup to pick a working algorithm\n"
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
		     "Description placed in the API status header, default: BFGMiner version"),
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
			"Run BFGMiner in benchmark mode - produces no shares"),
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
#if BLKMAKER_VERSION > 1
	OPT_WITH_ARG("--coinbase-addr",
		     set_b58addr, NULL, &opt_coinbase_script,
		     "Set coinbase payout address for solo mining"),
	OPT_WITH_ARG("--coinbase-payout|--cbaddr|--cb-addr|--payout",
		     set_b58addr, NULL, &opt_coinbase_script,
		     opt_hidden),
#endif
#if BLKMAKER_VERSION > 0
	OPT_WITH_ARG("--coinbase-sig",
		     set_strdup, NULL, &opt_coinbase_sig,
		     "Set coinbase signature when possible"),
	OPT_WITH_ARG("--coinbase|--cbsig|--cb-sig|--cb|--prayer",
		     set_strdup, NULL, &opt_coinbase_sig,
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
	OPT_WITHOUT_ARG("--debuglog",
		     opt_set_bool, &opt_debug,
		     "Enable debug logging"),
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
	OPT_WITHOUT_ARG("--disable-rejecting",
			opt_set_bool, &opt_disable_pool,
			"Automatically disable pools that continually reject shares"),
#if defined(WANT_CPUMINE) && (defined(HAVE_OPENCL) || defined(USE_FPGA))
	OPT_WITHOUT_ARG("--enable-cpu|-C",
			opt_set_bool, &opt_usecpu,
			"Enable CPU mining with other mining (default: no CPU mining if other devices exist)"),
#endif
	OPT_WITH_ARG("--expiry|-E",
		     set_int_0_to_9999, opt_show_intval, &opt_expiry,
		     "Upper bound on how many seconds after getting work we consider a share from it stale (w/o longpoll active)"),
	OPT_WITH_ARG("--expiry-lp",
		     set_int_0_to_9999, opt_show_intval, &opt_expiry_lp,
		     "Upper bound on how many seconds after getting work we consider a share from it stale (with longpoll active)"),
	OPT_WITHOUT_ARG("--failover-only",
			opt_set_bool, &opt_fail_only,
			"Don't leak work to backup pools when primary pool is lagging"),
#ifdef USE_FPGA
	OPT_WITHOUT_ARG("--force-dev-init",
	        opt_set_bool, &opt_force_dev_init,
	        "Always initialize devices when possible (such as bitstream uploads to some FPGAs)"),
#endif
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
		     "GPU engine (over)clock range in MHz - one value, range and/or comma separated list (e.g. 850-900,900,750-850)"),
	OPT_WITH_ARG("--gpu-fan",
		     set_gpu_fan, NULL, NULL,
		     "GPU fan percentage range - one value, range and/or comma separated list (e.g. 0-85,85,65)"),
	OPT_WITH_ARG("--gpu-map",
		     set_gpu_map, NULL, NULL,
		     "Map OpenCL to ADL device order manually, paired CSV (e.g. 1:0,2:1 maps OpenCL 1 to ADL 0, 2 to 1)"),
	OPT_WITH_ARG("--gpu-memclock",
		     set_gpu_memclock, NULL, NULL,
		     "Set the GPU memory (over)clock in MHz - one value for all or separate by commas for per card"),
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
#if defined(HAVE_OPENCL) || defined(USE_MODMINER) || defined(USE_X6500) || defined(USE_ZTEX)
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
#ifdef USE_AVALON
	OPT_WITH_ARG("--avalon-options",
		     set_avalon_options, NULL, NULL,
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
	OPT_WITHOUT_ARG("--no-gbt",
			opt_set_invbool, &want_gbt,
			"Disable getblocktemplate support"),
	OPT_WITHOUT_ARG("--no-getwork",
			opt_set_invbool, &want_getwork,
			"Disable getwork support"),
	OPT_WITHOUT_ARG("--no-longpoll",
			opt_set_invbool, &want_longpoll,
			"Disable X-Long-Polling support"),
	OPT_WITHOUT_ARG("--no-pool-disable",
			opt_set_invbool, &opt_disable_pool,
			opt_hidden),
	OPT_WITHOUT_ARG("--no-restart",
			opt_set_invbool, &opt_restart,
			"Do not attempt to restart devices that hang"
	),
	OPT_WITHOUT_ARG("--no-show-processors",
			opt_set_invbool, &opt_show_procs,
			opt_hidden),
	OPT_WITHOUT_ARG("--no-show-procs",
			opt_set_invbool, &opt_show_procs,
			opt_hidden),
	OPT_WITHOUT_ARG("--no-stratum",
			opt_set_invbool, &want_stratum,
			"Disable Stratum detection"),
	OPT_WITHOUT_ARG("--no-submit-stale",
			opt_set_invbool, &opt_submit_stale,
		        "Don't submit shares if they are detected as stale"),
#ifdef HAVE_OPENCL
	OPT_WITHOUT_ARG("--no-opencl-binaries",
	                opt_set_invbool, &opt_opencl_binaries,
	                "Don't attempt to use or save OpenCL kernel binaries"),
#endif
	OPT_WITH_ARG("--pass|-p",
		     set_pass, NULL, NULL,
		     "Password for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--per-device-stats",
			opt_set_bool, &want_per_device_stats,
			"Force verbose mode and output per-device statistics"),
	OPT_WITH_ARG("--pool-priority",
			 set_pool_priority, NULL, NULL,
			 "Priority for just the previous-defined pool"),
	OPT_WITH_ARG("--pool-proxy|-x",
		     set_pool_proxy, NULL, NULL,
		     "Proxy URI to use for connecting to just the previous-defined pool"),
	OPT_WITH_ARG("--force-rollntime",  // NOTE: must be after --pass for config file ordering
			 set_pool_force_rollntime, NULL, NULL,
			 opt_hidden),
	OPT_WITHOUT_ARG("--protocol-dump|-P",
			opt_set_bool, &opt_protocol,
			"Verbose dump of protocol-level activities"),
	OPT_WITH_ARG("--queue|-Q",
		     set_int_0_to_9999, opt_show_intval, &opt_queue,
		     "Minimum number of work items to have queued (0+)"),
	OPT_WITHOUT_ARG("--quiet|-q",
			opt_set_bool, &opt_quiet,
			"Disable logging output, display status and errors"),
	OPT_WITHOUT_ARG("--quiet-work-updates|--quiet-work-update",
			opt_set_bool, &opt_quiet_work_updates,
			opt_hidden),
	OPT_WITHOUT_ARG("--real-quiet",
			opt_set_bool, &opt_realquiet,
			"Disable all output"),
	OPT_WITHOUT_ARG("--remove-disabled",
		     opt_set_bool, &opt_removedisabled,
	         "Remove disabled devices entirely, as if they didn't exist"),
	OPT_WITH_ARG("--request-diff",
	             set_request_diff, opt_show_floatval, &request_pdiff,
	             "Request a specific difficulty from pools"),
	OPT_WITH_ARG("--retries",
		     opt_set_intval, opt_show_intval, &opt_retries,
		     "Number of times to retry failed submissions before giving up (-1 means never)"),
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
		     "Serial port to probe for mining devices"),
#endif
	OPT_WITH_ARG("--scan-time|-s",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     "Upper bound on time spent scanning current work, in seconds"),
	OPT_WITH_ARG("--scantime",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     opt_hidden),
	OPT_WITH_ARG("--sched-start",
		     set_schedtime, NULL, &schedstart,
		     "Set a time of day in HH:MM to start mining (a once off without a stop time)"),
	OPT_WITH_ARG("--sched-stop",
		     set_schedtime, NULL, &schedstop,
		     "Set a time of day in HH:MM to stop mining (will quit without a start time)"),
#ifdef USE_SCRYPT
	OPT_WITHOUT_ARG("--scrypt",
			opt_set_bool, &opt_scrypt,
			"Use the scrypt algorithm for mining (non-bitcoin)"),
#ifdef HAVE_OPENCL
	OPT_WITH_ARG("--shaders",
		     set_shaders, NULL, NULL,
		     "GPU shaders per card for tuning scrypt, comma separated"),
#endif
#endif
	OPT_WITH_ARG("--sharelog",
		     set_sharelog, NULL, NULL,
		     "Append share log to file"),
	OPT_WITH_ARG("--shares",
		     opt_set_intval, NULL, &opt_shares,
		     "Quit after mining N shares (default: unlimited)"),
	OPT_WITHOUT_ARG("--show-processors",
			opt_set_bool, &opt_show_procs,
			"Show per processor statistics in summary"),
	OPT_WITHOUT_ARG("--show-procs",
			opt_set_bool, &opt_show_procs,
			opt_hidden),
	OPT_WITH_ARG("--skip-security-checks",
			set_int_0_to_9999, NULL, &opt_skip_checks,
			"Skip security checks sometimes to save bandwidth; only check 1/<arg>th of the time (default: never skip)"),
	OPT_WITH_ARG("--socks-proxy",
		     opt_set_charp, NULL, &opt_socks_proxy,
		     "Set socks4 proxy (host:port)"),
	OPT_WITHOUT_ARG("--submit-stale",
			opt_set_bool, &opt_submit_stale,
	                opt_hidden),
	OPT_WITHOUT_ARG("--submit-threads",
	                opt_set_intval, &opt_submit_threads,
	                "Minimum number of concurrent share submissions (default: 64)"),
#ifdef HAVE_SYSLOG_H
	OPT_WITHOUT_ARG("--syslog",
			opt_set_bool, &use_syslog,
			"Use system log for output messages (default: standard error)"),
#endif
	OPT_WITH_ARG("--temp-cutoff",
		     set_temp_cutoff, opt_show_intval, &opt_cutofftemp,
		     "Temperature where a device will be automatically disabled, one value or comma separated list"),
	OPT_WITH_ARG("--temp-hysteresis",
		     set_int_1_to_10, opt_show_intval, &opt_hysteresis,
		     "Set how much the temperature can fluctuate outside limits when automanaging speeds"),
#ifdef HAVE_ADL
	OPT_WITH_ARG("--temp-overheat",
		     set_temp_overheat, opt_show_intval, &opt_overheattemp,
		     "Overheat temperature when automatically managing fan and GPU speeds, one value or comma separated list"),
#endif
	OPT_WITH_ARG("--temp-target",
		     set_temp_target, NULL, NULL,
		     "Target temperature when automatically managing fan and clock speeds, one value or comma separated list"),
	OPT_WITHOUT_ARG("--text-only|-T",
			opt_set_invbool, &use_curses,
#ifdef HAVE_CURSES
			"Disable ncurses formatted screen output"
#else
			opt_hidden
#endif
	),
#if defined(USE_SCRYPT) && defined(HAVE_OPENCL)
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
		char *p, *name, *sp;

		/* We don't handle subtables. */
		assert(!(opt->type & OPT_SUBTABLE));

		/* Pull apart the option name(s). */
		name = strdup(opt->names);
		for (p = strtok_r(name, "|", &sp); p; p = strtok_r(NULL, "|", &sp)) {
			char *err = "Invalid value";

			/* Ignore short options. */
			if (p[1] != '-')
				continue;

			val = json_object_get(config, p+2);
			if (!val)
				continue;

			if (opt->type & OPT_HASARG) {
			  if (json_is_string(val)) {
				err = opt->cb_arg(json_string_value(val),
						  opt->u.arg);
			  } else if (json_is_number(val)) {
					char buf[256], *p, *q;
					snprintf(buf, 256, "%f", json_number_value(val));
					if ( (p = strchr(buf, '.')) ) {
						// Trim /\.0*$/ to work properly with integer-only arguments
						q = p;
						while (*(++q) == '0') {}
						if (*q == '\0')
							*p = '\0';
					}
					err = opt->cb_arg(buf, opt->u.arg);
			  } else if (json_is_array(val)) {
				int n, size = json_array_size(val);

				err = NULL;
				for (n = 0; n < size && !err; n++) {
					if (json_is_string(json_array_get(val, n)))
						err = opt->cb_arg(json_string_value(json_array_get(val, n)), opt->u.arg);
					else if (json_is_object(json_array_get(val, n)))
						err = parse_config(json_array_get(val, n), false);
				}
			  }
			} else if (opt->type & OPT_NOARG) {
				if (json_is_true(val))
					err = opt->cb(opt->u.arg);
				else if (json_is_boolean(val)) {
					if (opt->cb == (void*)opt_set_bool)
						err = opt_set_invbool(opt->u.arg);
					else if (opt->cb == (void*)opt_set_invbool)
						err = opt_set_bool(opt->u.arg);
				}
			}

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

static void load_default_config(void)
{
	cnfbuf = malloc(PATH_MAX);

#if defined(unix)
	if (getenv("HOME") && *getenv("HOME")) {
	        strcpy(cnfbuf, getenv("HOME"));
		strcat(cnfbuf, "/");
	} else
		strcpy(cnfbuf, "");
	char *dirp = cnfbuf + strlen(cnfbuf);
	strcpy(dirp, ".bfgminer/");
	strcat(dirp, def_conf);
	if (access(cnfbuf, R_OK))
		// No BFGMiner config, try Cgminer's...
		strcpy(dirp, ".cgminer/cgminer.conf");
#else
	strcpy(cnfbuf, "");
	strcat(cnfbuf, def_conf);
#endif
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
#ifdef USE_AVALON
		"avalon "
#endif
#ifdef USE_MODMINER
		"modminer "
#endif
#ifdef USE_X6500
		"x6500 "
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

	swap32yes(&data.i[0], work->data, 16);
	sha2_context ctx;
	sha2_starts(&ctx);
	sha2_update(&ctx, data.c, 64);
	memcpy(work->midstate, ctx.state, sizeof(work->midstate));
	swap32tole(work->midstate, work->midstate, 8);
}

static struct work *make_work(void)
{
	struct work *work = calloc(1, sizeof(struct work));

	if (unlikely(!work))
		quit(1, "Failed to calloc work in make_work");

	cg_wlock(&control_lock);
	work->id = total_work++;
	cg_wunlock(&control_lock);

	return work;
}

/* This is the central place all work that is about to be retired should be
 * cleaned to remove any dynamically allocated arrays within the struct */
void clean_work(struct work *work)
{
	free(work->job_id);
	free(work->nonce2);
	free(work->ntime);
	free(work->nonce1);

	if (work->tmpl) {
		struct pool *pool = work->pool;
		mutex_lock(&pool->pool_lock);
		bool free_tmpl = !--*work->tmpl_refcount;
		mutex_unlock(&pool->pool_lock);
		if (free_tmpl) {
			blktmpl_free(work->tmpl);
			free(work->tmpl_refcount);
		}
	}

	memset(work, 0, sizeof(struct work));
}

/* All dynamically allocated work structs should be freed here to not leak any
 * ram from arrays allocated within the work struct */
void free_work(struct work *work)
{
	clean_work(work);
	free(work);
}

static char *workpadding = "000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000";

// Must only be called with ch_lock held!
static
void __update_block_title(const unsigned char *hash_swap)
{
	char *tmp;
	if (hash_swap) {
		// Only provided when the block has actually changed
		free(current_hash);
		current_hash = malloc(3 /* ... */ + 16 /* block hash segment */ + 1);
		tmp = bin2hex(&hash_swap[24], 8);
		sprintf(current_hash, "...%s", tmp);
		free(tmp);
		known_blkheight_current = false;
	} else if (likely(known_blkheight_current)) {
		return;
	}
	if (current_block_id == known_blkheight_blkid) {
		// FIXME: The block number will overflow this sometime around AD 2025-2027
		if (known_blkheight < 1000000) {
			memmove(&current_hash[3], &current_hash[11], 8);
			sprintf(&current_hash[11], " #%6u", known_blkheight);
		}
		known_blkheight_current = true;
	}
}

static
void have_block_height(uint32_t block_id, uint32_t blkheight)
{
	if (known_blkheight == blkheight)
		return;
	applog(LOG_DEBUG, "Learned that block id %08" PRIx32 " is height %" PRIu32, be32toh(block_id), blkheight);
	cg_wlock(&ch_lock);
	known_blkheight = blkheight;
	known_blkheight_blkid = block_id;
	if (block_id == current_block_id)
		__update_block_title(NULL);
	cg_wunlock(&ch_lock);
}

static bool work_decode(struct pool *pool, struct work *work, json_t *val)
{
	json_t *res_val = json_object_get(val, "result");
	json_t *tmp_val;
	bool ret = false;

	if (unlikely(detect_algo == 1)) {
		json_t *tmp = json_object_get(res_val, "algorithm");
		const char *v = tmp ? json_string_value(tmp) : "";
		if (strncasecmp(v, "scrypt", 6))
			detect_algo = 2;
	}
	
	if (work->tmpl) {
		const char *err = blktmpl_add_jansson(work->tmpl, res_val, time(NULL));
		if (err) {
			applog(LOG_ERR, "blktmpl error: %s", err);
			return false;
		}
		work->rolltime = blkmk_time_left(work->tmpl, time(NULL));
#if BLKMAKER_VERSION > 1
		if (opt_coinbase_script.sz)
		{
			bool newcb;
#if BLKMAKER_VERSION > 2
			blkmk_init_generation2(work->tmpl, opt_coinbase_script.data, opt_coinbase_script.sz, &newcb);
#else
			newcb = !work->tmpl->cbtxn;
			blkmk_init_generation(work->tmpl, opt_coinbase_script.data, opt_coinbase_script.sz);
#endif
			if (newcb)
			{
				ssize_t ae = blkmk_append_coinbase_safe(work->tmpl, &template_nonce, sizeof(template_nonce));
				if (ae < (ssize_t)sizeof(template_nonce))
					applog(LOG_WARNING, "Cannot append template-nonce to coinbase on pool %u (%"PRId64") - you might be wasting hashing!", work->pool->pool_no, (int64_t)ae);
				++template_nonce;
			}
		}
#endif
#if BLKMAKER_VERSION > 0
		{
			ssize_t ae = blkmk_append_coinbase_safe(work->tmpl, opt_coinbase_sig, 101);
			static bool appenderr = false;
			if (ae <= 0) {
				if (opt_coinbase_sig) {
					applog((appenderr ? LOG_DEBUG : LOG_WARNING), "Cannot append coinbase signature at all on pool %u (%"PRId64")", pool->pool_no, (int64_t)ae);
					appenderr = true;
				}
			} else if (ae >= 3 || opt_coinbase_sig) {
				const char *cbappend = opt_coinbase_sig;
				if (!cbappend) {
					const char full[] = PACKAGE " " VERSION;
					if ((size_t)ae >= sizeof(full) - 1)
						cbappend = full;
					else if ((size_t)ae >= sizeof(PACKAGE) - 1)
						cbappend = PACKAGE;
					else
						cbappend = "BFG";
				}
				size_t cbappendsz = strlen(cbappend);
				static bool truncatewarning = false;
				if (cbappendsz <= (size_t)ae) {
					if (cbappendsz < (size_t)ae)
						// If we have space, include the trailing \0
						++cbappendsz;
					ae = cbappendsz;
					truncatewarning = false;
				} else {
					char *tmp = malloc(ae + 1);
					memcpy(tmp, opt_coinbase_sig, ae);
					tmp[ae] = '\0';
					applog((truncatewarning ? LOG_DEBUG : LOG_WARNING),
					       "Pool %u truncating appended coinbase signature at %"PRId64" bytes: %s(%s)",
					       pool->pool_no, (int64_t)ae, tmp, &opt_coinbase_sig[ae]);
					free(tmp);
					truncatewarning = true;
				}
				ae = blkmk_append_coinbase_safe(work->tmpl, cbappend, ae);
				if (ae <= 0) {
					applog((appenderr ? LOG_DEBUG : LOG_WARNING), "Error appending coinbase signature (%"PRId64")", (int64_t)ae);
					appenderr = true;
				} else
					appenderr = false;
			}
		}
#endif
		if (blkmk_get_data(work->tmpl, work->data, 80, time(NULL), NULL, &work->dataid) < 76)
			return false;
		swap32yes(work->data, work->data, 80 / 4);
		memcpy(&work->data[80], "\0\0\0\x80\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x80\x02\0\0", 48);

		const struct blktmpl_longpoll_req *lp;
		if ((lp = blktmpl_get_longpoll(work->tmpl)) && ((!pool->lp_id) || strcmp(lp->id, pool->lp_id))) {
			free(pool->lp_id);
			pool->lp_id = strdup(lp->id);

#if 0  /* This just doesn't work :( */
			curl_socket_t sock = pool->lp_socket;
			if (sock != CURL_SOCKET_BAD) {
				pool->lp_socket = CURL_SOCKET_BAD;
				applog(LOG_WARNING, "Pool %u long poll request hanging, reconnecting", pool->pool_no);
				shutdown(sock, SHUT_RDWR);
			}
#endif
		}
	}
	else
	if (unlikely(!jobj_binary(res_val, "data", work->data, sizeof(work->data), true))) {
		applog(LOG_ERR, "JSON inval data");
		return false;
	}

	if (!jobj_binary(res_val, "midstate", work->midstate, sizeof(work->midstate), false)) {
		// Calculate it ourselves
		applog(LOG_DEBUG, "Calculating midstate locally");
		calc_midstate(work);
	}

	if (unlikely(!jobj_binary(res_val, "target", work->target, sizeof(work->target), true))) {
		applog(LOG_ERR, "JSON inval target");
		return false;
	}
	if (work->tmpl) {
		for (size_t i = 0; i < sizeof(work->target) / 2; ++i)
		{
			int p = (sizeof(work->target) - 1) - i;
			unsigned char c = work->target[i];
			work->target[i] = work->target[p];
			work->target[p] = c;
		}
	}

	if ( (tmp_val = json_object_get(res_val, "height")) ) {
		uint32_t blkheight = json_number_value(tmp_val);
		uint32_t block_id = ((uint32_t*)work->data)[1];
		have_block_height(block_id, blkheight);
	}

	memset(work->hash, 0, sizeof(work->hash));

	cgtime(&work->tv_staged);

	ret = true;

	return ret;
}

int dev_from_id(int thr_id)
{
	struct cgpu_info *cgpu = get_thr_cgpu(thr_id);

	return cgpu->device_id;
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

static int __total_staged(void)
{
	return HASH_COUNT(staged_work);
}

static int total_staged(void)
{
	int ret;

	mutex_lock(stgd_lock);
	ret = __total_staged();
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
static int devsummaryYOffset;
static int total_lines;
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

// Cancellable getch
int my_cancellable_getch(void)
{
	// This only works because the macro only hits direct getch() calls
	typedef int (*real_getch_t)(void);
	const real_getch_t real_getch = __real_getch;

	int type, rv;
	bool sct;

	sct = !pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &type);
	rv = real_getch();
	if (sct)
		pthread_setcanceltype(type, &type);

	return rv;
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
		sprintf(suffix, "k");
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

static float
utility_to_hashrate(double utility)
{
	return utility * 0x4444444;
}

static const char*_unitchar = "kMGTPEZY?";

static void
hashrate_pick_unit(float hashrate, unsigned char*unit)
{
	unsigned char i;
	for (i = 0; i <= *unit; ++i)
		hashrate /= 1e3;
	while (hashrate >= 1000)
	{
		hashrate /= 1e3;
		if (likely(_unitchar[*unit] != '?'))
			++*unit;
	}
}

enum h2bs_fmt {
	H2B_NOUNIT,  // "xxx.x"
	H2B_SHORT,   // "xxx.xMH/s"
	H2B_SPACED,  // "xxx.x MH/s"
};
static const size_t h2bs_fmt_size[] = {6, 10, 11};

static char*
hashrate_to_bufstr(char*buf, float hashrate, signed char unitin, enum h2bs_fmt fmt)
{
	unsigned char prec, i, ucp, unit;
	if (unitin == -1)
	{
		unit = 0;
		hashrate_pick_unit(hashrate, &unit);
	}
	else
		unit = unitin;
	
	i = 5;
	switch (fmt) {
	case H2B_SPACED:
		buf[i++] = ' ';
	case H2B_SHORT:
		buf[i++] = _unitchar[unit];
		strcpy(&buf[i], "h/s");
	default:
		break;
	}
	
	for (i = 0; i <= unit; ++i)
		hashrate /= 1000;
	if (hashrate >= 100 || unit < 2)
		prec = 1;
	else
	if (hashrate >= 10)
		prec = 2;
	else
		prec = 3;
	ucp = (fmt == H2B_NOUNIT ? '\0' : buf[5]);
	sprintf(buf, "%5.*f", prec, hashrate);
	buf[5] = ucp;
	return buf;
}

static void
ti_hashrate_bufstr(char**out, float current, float average, float sharebased, enum h2bs_fmt longfmt)
{
	unsigned char unit = 0;
	
	hashrate_pick_unit(current, &unit);
	hashrate_pick_unit(average, &unit);
	hashrate_pick_unit(sharebased, &unit);
	
	hashrate_to_bufstr(out[0], current, unit, H2B_NOUNIT);
	hashrate_to_bufstr(out[1], average, unit, H2B_NOUNIT);
	hashrate_to_bufstr(out[2], sharebased, unit, longfmt);
}

static const char *
percentf(unsigned p, unsigned t, char *buf)
{
	if (!p)
		return "none";
	if (!t)
		return "100%";
	p = p * 10000 / (p + t);
	if (p < 100)
		sprintf(buf, ".%02u%%", p);  // ".01%"
	else
	if (p < 1000)
		sprintf(buf, "%u.%u%%", p / 100, (p % 100) / 10);  // "9.1%"
	else
		sprintf(buf, " %2u%%", p / 100);  // " 99%"
	return buf;
}

#ifdef HAVE_CURSES
static void adj_width(int var, int *length);
#endif

static void get_statline2(char *buf, struct cgpu_info *cgpu, bool for_curses)
{
#ifdef HAVE_CURSES
	static int awidth = 1, rwidth = 1, swidth = 1, hwwidth = 1;
#else
	assert(for_curses == false);
#endif
	struct device_drv *drv = cgpu->drv;
	void (*statline_func)(char *, struct cgpu_info *);
	enum h2bs_fmt hashrate_style = for_curses ? H2B_SHORT : H2B_SPACED;
	char cHr[h2bs_fmt_size[H2B_NOUNIT]], aHr[h2bs_fmt_size[H2B_NOUNIT]], uHr[h2bs_fmt_size[hashrate_style]];
	char rejpcbuf[6];
	
	if (!opt_show_procs)
		cgpu = cgpu->device;
	
	cgpu->utility = cgpu->accepted / total_secs * 60;
	cgpu->utility_diff1 = cgpu->diff_accepted / total_secs * 60;
	
	double rolling = cgpu->rolling;
	double mhashes = cgpu->total_mhashes;
	double wutil = cgpu->utility_diff1;
	int accepted = cgpu->accepted;
	int rejected = cgpu->rejected;
	int stale = cgpu->stale;
	int hwerrs = cgpu->hw_errors;
	
	if (!opt_show_procs)
		for (struct cgpu_info *slave = cgpu; (slave = slave->next_proc); )
		{
			slave->utility = slave->accepted / total_secs * 60;
			slave->utility_diff1 = slave->diff_accepted / total_secs * 60;
			
			rolling += slave->rolling;
			mhashes += slave->total_mhashes;
			wutil += slave->utility_diff1;
			accepted += slave->accepted;
			rejected += slave->rejected;
			stale += slave->stale;
			hwerrs += slave->hw_errors;
		}
	
	ti_hashrate_bufstr(
		(char*[]){cHr, aHr, uHr},
		1e6*rolling,
		1e6*mhashes / total_secs,
		utility_to_hashrate(wutil),
		hashrate_style);

	// Processor representation
#ifdef HAVE_CURSES
	if (for_curses)
	{
		if (opt_show_procs)
			sprintf(buf, " %"PRIprepr": ", cgpu->proc_repr);
		else
			sprintf(buf, " %s: ", cgpu->dev_repr);
	}
	else
#endif
		sprintf(buf, "%s ", opt_show_procs ? cgpu->proc_repr_ns : cgpu->dev_repr_ns);
	
	if (drv->get_dev_statline_before || drv->get_statline_before)
	{
		if (drv->get_dev_statline_before && drv->get_statline_before)
			statline_func = opt_show_procs ? drv->get_statline_before : drv->get_dev_statline_before;
		else
			statline_func = drv->get_statline_before ?: drv->get_dev_statline_before;
		statline_func(buf, cgpu);
	}
	else
		tailsprintf(buf, "               | ");
	
#ifdef HAVE_CURSES
	if (for_curses)
	{
		const char *cHrStatsOpt[] = {"DEAD ", "SICK ", "OFF  ", "REST ", " ERR ", "WAIT ", cHr};
		int cHrStatsI = (sizeof(cHrStatsOpt) / sizeof(*cHrStatsOpt)) - 1;
		bool all_dead = true, all_off = true;
		for (struct cgpu_info *proc = cgpu; proc; proc = proc->next_proc)
		{
			switch (cHrStatsI) {
				default:
					if (proc->status == LIFE_WAIT)
						cHrStatsI = 5;
				case 5:
					if (proc->deven == DEV_RECOVER_ERR)
						cHrStatsI = 4;
				case 4:
					if (proc->deven == DEV_RECOVER)
						cHrStatsI = 3;
				case 3:
					if (proc->status == LIFE_SICK || proc->status == LIFE_DEAD)
					{
						cHrStatsI = 1;
						all_off = false;
					}
					else
					if (likely(proc->deven != DEV_DISABLED))
						all_off = false;
				case 1:
					break;
			}
			if (likely(proc->status != LIFE_DEAD))
				all_dead = false;
			if (opt_show_procs)
				break;
		}
		if (unlikely(all_dead))
			cHrStatsI = 0;
		else
		if (unlikely(all_off))
			cHrStatsI = 2;
		
		adj_width(accepted, &awidth);
		adj_width(rejected, &rwidth);
		adj_width(stale, &swidth);
		adj_width(hwerrs, &hwwidth);
		
		tailsprintf(buf, "%s/%s/%s | A:%*d R:%*d+%*d(%s) HW:%*d",
		            cHrStatsOpt[cHrStatsI],
		            aHr, uHr,
		            awidth, accepted,
		            rwidth, rejected,
		            swidth, stale,
		            percentf(rejected + stale, accepted, rejpcbuf),
		            hwwidth, hwerrs
		);
	}
	else
#endif
	{
		tailsprintf(buf, "%ds:%s avg:%s u:%s | A:%d R:%d+%d(%s) HW:%d",
			opt_log_interval,
			cHr, aHr, uHr,
			accepted,
			rejected,
			stale,
			percentf(rejected + stale, accepted, rejpcbuf),
			hwerrs);
	}
	
	if (drv->get_dev_statline_after || drv->get_statline)
	{
		if (drv->get_dev_statline_after && drv->get_statline)
			statline_func = opt_show_procs ? drv->get_statline : drv->get_dev_statline_after;
		else
			statline_func = drv->get_statline ?: drv->get_dev_statline_after;
		statline_func(buf, cgpu);
	}
}

#define get_statline(buf, cgpu)  get_statline2(buf, cgpu, false)

static void text_print_status(int thr_id)
{
	struct cgpu_info *cgpu;
	char logline[256];

	cgpu = get_thr_cgpu(thr_id);
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
	struct timeval now, tv;
	float efficiency;
	double utility;

	efficiency = total_bytes_xfer ? total_diff_accepted * 2048. / total_bytes_xfer : 0.0;

	wattron(statuswin, A_BOLD);
	mvwprintw(statuswin, 0, 0, " " PACKAGE " version " VERSION " - Started: %s", datestamp);
	if (!gettimeofday(&now, NULL))
	{
		unsigned int days, hours;
		div_t d;
		
		timersub(&now, &miner_started, &tv);
		d = div(tv.tv_sec, 86400);
		days = d.quot;
		d = div(d.rem, 3600);
		hours = d.quot;
		d = div(d.rem, 60);
		wprintw(statuswin, " - [%3u day%c %02d:%02d:%02d]"
			, days
			, (days == 1) ? ' ' : 's'
			, hours
			, d.quot
			, d.rem
		);
	}
	wattroff(statuswin, A_BOLD);
	mvwhline(statuswin, 1, 0, '-', 80);
	mvwprintw(statuswin, 2, 0, " %s", statusline);
	wclrtoeol(statuswin);

	utility = total_accepted / total_secs * 60;

	mvwprintw(statuswin, 3, 0, " ST: %d  GF: %d  NB: %d  AS: %d  RF: %d  E: %.2f  U:%.1f/m  BS:%s",
		total_staged(),
		total_go,
		new_blocks,
		total_submitting,
		total_ro,
		efficiency,
		utility,
		best_share);
	wclrtoeol(statuswin);
	if ((pool_strategy == POOL_LOADBALANCE  || pool_strategy == POOL_BALANCE) && total_pools > 1) {
		mvwprintw(statuswin, 4, 0, " Connected to multiple pools with%s LP",
			have_longpoll ? "": "out");
	} else if (pool->has_stratum) {
		mvwprintw(statuswin, 4, 0, " Connected to %s diff %s with stratum as user %s",
			pool->sockaddr_url, pool->diff, pool->rpc_user);
	} else {
		mvwprintw(statuswin, 4, 0, " Connected to %s diff %s with%s LP as user %s",
			pool->sockaddr_url, pool->diff, have_longpoll ? "": "out", pool->rpc_user);
	}
	wclrtoeol(statuswin);
	mvwprintw(statuswin, 5, 0, " Block: %s  Diff:%s (%s)  Started: %s",
		  current_hash, block_diff, net_hashrate, blocktime);
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
	struct cgpu_info *cgpu;
	char logline[256];
	int ypos;

	if (opt_compact)
		return;

	cgpu = get_thr_cgpu(thr_id);

	/* Check this isn't out of the window size */
	if (opt_show_procs)
	ypos = cgpu->cgminer_id;
	else
	{
		if (cgpu->proc_id)
			return;
		ypos = cgpu->device_line_id;
	}
	ypos += devsummaryYOffset;
	if (ypos < 0)
		return;
	ypos += devcursor;
	if (ypos >= statusy - 1)
		return;

	if (wmove(statuswin, ypos, 0) == ERR)
		return;
	
	get_statline2(logline, cgpu, true);
	waddstr(statuswin, logline);

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

static int device_line_id_count;

static void switch_compact(void)
{
	if (opt_compact) {
		logstart = devcursor + 1;
		logcursor = logstart + 1;
	} else {
		total_lines = (opt_show_procs ? total_devices : device_line_id_count);
		logstart = devcursor + total_lines + 1;
		logcursor = logstart + 1;
	}
	check_winsizes();
}

#define change_summarywinsize  switch_compact

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
		wclear(logwin);
		unlock_curses();
	}
}

void logwin_update(void)
{
	if (curses_active_locked()) {
		touchwin(logwin);
		wrefresh(logwin);
		unlock_curses();
	}
}
#endif

static void enable_pool(struct pool *pool)
{
	if (pool->enabled != POOL_ENABLED) {
		enabled_pools++;
		pool->enabled = POOL_ENABLED;
	}
}

#ifdef HAVE_CURSES
static void disable_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_DISABLED;
}
#endif

static void reject_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_REJECTING;
}

static uint64_t share_diff(const struct work *);

static
void share_result_msg(const struct work *work, const char *disp, const char *reason, bool resubmit, const char *worktime) {
	struct cgpu_info *cgpu;
	const unsigned char *hashpart = &work->hash[opt_scrypt ? 26 : 24];
	char shrdiffdisp[16];
	int tgtdiff = floor(work->work_difficulty);
	char tgtdiffdisp[16];
	char where[20];
	
	cgpu = get_thr_cgpu(work->thr_id);
	
	suffix_string(work->share_diff, shrdiffdisp, 0);
	suffix_string(tgtdiff, tgtdiffdisp, 0);
	
	if (total_pools > 1)
		sprintf(where, " pool %d", work->pool->pool_no);
	else
		where[0] = '\0';
	
	applog(LOG_NOTICE, "%s %02x%02x%02x%02x %"PRIprepr"%s Diff %s/%s%s %s%s",
	       disp,
	       (unsigned)hashpart[3], (unsigned)hashpart[2], (unsigned)hashpart[1], (unsigned)hashpart[0],
	       cgpu->proc_repr,
	       where,
	       shrdiffdisp, tgtdiffdisp,
	       reason,
	       resubmit ? "(resubmit)" : "",
	       worktime
	);
}

static bool test_work_current(struct work *);
static void _submit_work_async(struct work *);

static
void maybe_local_submit(const struct work *work)
{
#if BLKMAKER_VERSION > 3
	if (unlikely(work->block && work->tmpl))
	{
		// This is a block with a full template (GBT)
		// Regardless of the result, submit to local bitcoind(s) as well
		struct work *work_cp;
		char *p;
		
		for (int i = 0; i < total_pools; ++i)
		{
			p = strchr(pools[i]->rpc_url, '#');
			if (likely(!(p && strstr(&p[1], "allblocks"))))
				continue;
			
			work_cp = copy_work(work);
			work_cp->pool = pools[i];
			work_cp->do_foreign_submit = true;
			_submit_work_async(work_cp);
		}
	}
#endif
}

/* Theoretically threads could race when modifying accepted and
 * rejected values but the chance of two submits completing at the
 * same time is zero so there is no point adding extra locking */
static void
share_result(json_t *val, json_t *res, json_t *err, const struct work *work,
	     /*char *hashshow,*/ bool resubmit, char *worktime)
{
	struct pool *pool = work->pool;
	struct cgpu_info *cgpu;

	cgpu = get_thr_cgpu(work->thr_id);

	if ((json_is_null(err) || !err) && (json_is_null(res) || json_is_true(res))) {
		mutex_lock(&stats_lock);
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		mutex_unlock(&stats_lock);

		pool->seq_rejects = 0;
		cgpu->last_share_pool = pool->pool_no;
		cgpu->last_share_pool_time = time(NULL);
		cgpu->last_share_diff = work->work_difficulty;
		pool->last_share_time = cgpu->last_share_pool_time;
		pool->last_share_diff = work->work_difficulty;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
		if (!QUIET) {
			share_result_msg(work, "Accepted", "", resubmit, worktime);
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

		if (unlikely(work->block)) {
			// Force moving on to this new block :)
			struct work fakework;
			memset(&fakework, 0, sizeof(fakework));
			fakework.pool = work->pool;

			// Copy block version, bits, and time from share
			memcpy(&fakework.data[ 0], &work->data[ 0], 4);
			memcpy(&fakework.data[68], &work->data[68], 8);

			// Set prevblock to winning hash (swap32'd)
			swap32yes(&fakework.data[4], &work->hash[0], 32 / 4);

			test_work_current(&fakework);
		}
	} else {
		mutex_lock(&stats_lock);
		cgpu->rejected++;
		total_rejected++;
		pool->rejected++;
		cgpu->diff_rejected += work->work_difficulty;
		total_diff_rejected += work->work_difficulty;
		pool->diff_rejected += work->work_difficulty;
		pool->seq_rejects++;
		mutex_unlock(&stats_lock);

		applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!QUIET) {
			char where[20];
			char disposition[36] = "reject";
			char reason[32];

			strcpy(reason, "");
			if (total_pools > 1)
				sprintf(where, "pool %d", work->pool->pool_no);
			else
				strcpy(where, "");

			if (!json_is_string(res))
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

			share_result_msg(work, "Rejected", reason, resubmit, worktime);
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
	
	maybe_local_submit(work);
}

static char *submit_upstream_work_request(struct work *work)
{
	char *hexstr = NULL;
	char *s, *sd;
	struct pool *pool = work->pool;

	if (work->tmpl) {
		json_t *req;
		unsigned char data[80];
		
		swap32yes(data, work->data, 80 / 4);
#if BLKMAKER_VERSION > 3
		if (work->do_foreign_submit)
			req = blkmk_submit_foreign_jansson(work->tmpl, data, work->dataid, le32toh(*((uint32_t*)&work->data[76])));
		else
#endif
			req = blkmk_submit_jansson(work->tmpl, data, work->dataid, le32toh(*((uint32_t*)&work->data[76])));
		s = json_dumps(req, 0);
		json_decref(req);
		sd = bin2hex(data, 80);
	} else {

	/* build hex string */
	hexstr = bin2hex(work->data, sizeof(work->data));

	/* build JSON-RPC request */
		s = strdup("{\"method\": \"getwork\", \"params\": [ \"");
		s = realloc_strcat(s, hexstr);
		s = realloc_strcat(s, "\" ], \"id\":1}");

		free(hexstr);
		sd = s;

	}

	applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, sd);
	if (work->tmpl)
		free(sd);
	else
		s = realloc_strcat(s, "\n");

	return s;
}

static bool submit_upstream_work_completed(struct work *work, bool resubmit, struct timeval *ptv_submit, json_t *val) {
	json_t *res, *err;
	bool rc = false;
	int thr_id = work->thr_id;
	struct pool *pool = work->pool;
	struct timeval tv_submit_reply;
	char worktime[200] = "";

	cgtime(&tv_submit_reply);

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
	err = json_object_get(val, "error");

	if (!QUIET) {
		if (opt_worktime) {
			char workclone[20];
			struct tm _tm;
			struct tm *tm, tm_getwork, tm_submit_reply;
			tm = &_tm;
			double getwork_time = tdiff((struct timeval *)&(work->tv_getwork_reply),
							(struct timeval *)&(work->tv_getwork));
			double getwork_to_work = tdiff((struct timeval *)&(work->tv_work_start),
							(struct timeval *)&(work->tv_getwork_reply));
			double work_time = tdiff((struct timeval *)&(work->tv_work_found),
							(struct timeval *)&(work->tv_work_start));
			double work_to_submit = tdiff(ptv_submit,
							(struct timeval *)&(work->tv_work_found));
			double submit_time = tdiff(&tv_submit_reply, ptv_submit);
			int diffplaces = 3;

			localtime_r(&(work->tv_getwork.tv_sec), tm);
			memcpy(&tm_getwork, tm, sizeof(struct tm));
			localtime_r(&(tv_submit_reply.tv_sec), tm);
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

	share_result(val, res, err, work, resubmit, worktime);

	if (!opt_realquiet)
		print_status(thr_id);
	if (!want_per_device_stats) {
		char logline[256];
		struct cgpu_info *cgpu;

		cgpu = get_thr_cgpu(thr_id);
		
		get_statline(logline, cgpu);
		applog(LOG_INFO, "%s", logline);
	}

	json_decref(val);

	rc = true;
out:
	return rc;
}

/* Specifies whether we can use this pool for work or not. */
static bool pool_unworkable(struct pool *pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	if (pool->has_stratum && !pool->stratum_active)
		return true;
	return false;
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

		if (pool_unworkable(pool))
			continue;
		if (pool->shares < lowest) {
			lowest = pool->shares;
			ret = pool;
		}
	}

	ret->shares++;
	return ret;
}

static bool pool_active(struct pool *, bool pinging);
static void pool_died(struct pool *);

/* Select any active pool in a rotating fashion when loadbalance is chosen */
static inline struct pool *select_pool(bool lagging)
{
	static int rotating_pool = 0;
	struct pool *pool, *cp;
	int tested;

	cp = current_pool();

retry:
	if (pool_strategy == POOL_BALANCE)
	{
		pool = select_balanced(cp);
		goto have_pool;
	}

	if (pool_strategy != POOL_LOADBALANCE && (!lagging || opt_fail_only))
		pool = cp;
	else
		pool = NULL;

	/* Try to find the first pool in the rotation that is usable */
	tested = 0;
	while (!pool && tested++ < total_pools) {
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
		pool = pools[rotating_pool];
		if (!pool_unworkable(pool))
			break;
		pool = NULL;
	}
	/* If still nothing is usable, use the current pool */
	if (!pool)
		pool = cp;

have_pool:
	if (cp != pool)
	{
		if (!pool_active(pool, false))
		{
			pool_died(pool);
			goto retry;
		}
		pool_tclear(pool, &pool->idle);
	}
	return pool;
}

static double DIFFEXACTONE = 26959946667150639794667015087019630673637144422540572481103610249215.0;
static const uint64_t diffone = 0xFFFF000000000000ull;

static double target_diff(const unsigned char *target)
{
	double targ = 0;
	signed int i;

	for (i = 31; i >= 0; --i)
		targ = (targ * 0x100) + target[i];

	return DIFFEXACTONE / (targ ?: 1);
}

/*
 * Calculate the work share difficulty
 */
static void calc_diff(struct work *work, int known)
{
	struct cgminer_pool_stats *pool_stats = &(work->pool->cgminer_pool_stats);
	double difficulty;

	if (!known) {
		work->work_difficulty = target_diff(work->target);
	} else
		work->work_difficulty = known;
	difficulty = work->work_difficulty;

	pool_stats->last_diff = difficulty;
	suffix_string((uint64_t)difficulty, work->pool->diff, 0);

	if (difficulty == pool_stats->min_diff)
		pool_stats->min_diff_count++;
	else if (difficulty < pool_stats->min_diff || pool_stats->min_diff == 0) {
		pool_stats->min_diff = difficulty;
		pool_stats->min_diff_count = 1;
	}

	if (difficulty == pool_stats->max_diff)
		pool_stats->max_diff_count++;
	else if (difficulty > pool_stats->max_diff) {
		pool_stats->max_diff = difficulty;
		pool_stats->max_diff_count = 1;
	}
}

static void get_benchmark_work(struct work *work)
{
	// Use a random work block pulled from a pool
	static uint8_t bench_block[] = { CGMINER_BENCHMARK_BLOCK };

	size_t bench_size = sizeof(*work);
	size_t work_size = sizeof(bench_block);
	size_t min_size = (work_size < bench_size ? work_size : bench_size);
	memset(work, 0, sizeof(*work));
	memcpy(work, &bench_block, min_size);
	work->mandatory = true;
	work->pool = pools[0];
	cgtime(&work->tv_getwork);
	copy_time(&work->tv_getwork_reply, &work->tv_getwork);
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
	calc_diff(work, 0);
}

static void wake_gws(void);

static void update_last_work(struct work *work)
{
	if (!work->tmpl)
		// Only save GBT jobs, since rollntime isn't coordinated well yet
		return;

	struct pool *pool = work->pool;
	mutex_lock(&pool->last_work_lock);
	if (pool->last_work_copy)
		free_work(pool->last_work_copy);
	pool->last_work_copy = copy_work(work);
	pool->last_work_copy->work_restart_id = pool->work_restart_id;
	mutex_unlock(&pool->last_work_lock);
}

static
void gbt_req_target(json_t *req)
{
	json_t *j;
	json_t *n;
	
	if (!request_target_str)
		return;
	
	j = json_object_get(req, "params");
	if (!j)
	{
		n = json_array();
		if (!n)
			return;
		if (json_object_set_new(req, "params", n))
			goto erradd;
		j = n;
	}
	
	n = json_array_get(j, 0);
	if (!n)
	{
		n = json_object();
		if (!n)
			return;
		if (json_array_append_new(j, n))
			goto erradd;
	}
	j = n;
	
	n = json_string(request_target_str);
	if (!n)
		return;
	if (json_object_set_new(j, "target", n))
		goto erradd;
	
	return;

erradd:
	json_decref(n);
}

static char *prepare_rpc_req2(struct work *work, enum pool_protocol proto, const char *lpid, bool probe)
{
	char *rpc_req;

	clean_work(work);
	switch (proto) {
		case PLP_GETWORK:
			work->getwork_mode = GETWORK_MODE_POOL;
			return strdup(getwork_req);
		case PLP_GETBLOCKTEMPLATE:
			work->getwork_mode = GETWORK_MODE_GBT;
			work->tmpl_refcount = malloc(sizeof(*work->tmpl_refcount));
			if (!work->tmpl_refcount)
				return NULL;
			work->tmpl = blktmpl_create();
			if (!work->tmpl)
				goto gbtfail2;
			*work->tmpl_refcount = 1;
			gbt_capabilities_t caps = blktmpl_addcaps(work->tmpl);
			if (!caps)
				goto gbtfail;
			caps |= GBT_LONGPOLL;
			json_t *req = blktmpl_request_jansson(caps, lpid);
			if (!req)
				goto gbtfail;
			
			if (probe)
				gbt_req_target(req);
			
			rpc_req = json_dumps(req, 0);
			if (!rpc_req)
				goto gbtfail;
			json_decref(req);
			return rpc_req;
		default:
			return NULL;
	}
	return NULL;

gbtfail:
	blktmpl_free(work->tmpl);
	work->tmpl = NULL;
gbtfail2:
	free(work->tmpl_refcount);
	work->tmpl_refcount = NULL;
	return NULL;
}

#define prepare_rpc_req(work, proto, lpid)  prepare_rpc_req2(work, proto, lpid, false)
#define prepare_rpc_req_probe(work, proto, lpid)  prepare_rpc_req2(work, proto, lpid, true)

static const char *pool_protocol_name(enum pool_protocol proto)
{
	switch (proto) {
		case PLP_GETBLOCKTEMPLATE:
			return "getblocktemplate";
		case PLP_GETWORK:
			return "getwork";
		default:
			return "UNKNOWN";
	}
}

static enum pool_protocol pool_protocol_fallback(enum pool_protocol proto)
{
	switch (proto) {
		case PLP_GETBLOCKTEMPLATE:
			if (want_getwork)
			return PLP_GETWORK;
		default:
			return PLP_NONE;
	}
}

static bool get_upstream_work(struct work *work, CURL *curl)
{
	struct pool *pool = work->pool;
	struct cgminer_pool_stats *pool_stats = &(pool->cgminer_pool_stats);
	struct timeval tv_elapsed;
	json_t *val = NULL;
	bool rc = false;
	char *url;
	enum pool_protocol proto;

	char *rpc_req;

	if (pool->proto == PLP_NONE)
		pool->proto = PLP_GETBLOCKTEMPLATE;

tryagain:
	rpc_req = prepare_rpc_req(work, pool->proto, NULL);
	work->pool = pool;
	if (!rpc_req)
		return false;

	applog(LOG_DEBUG, "DBG: sending %s get RPC call: %s", pool->rpc_url, rpc_req);

	url = pool->rpc_url;

	cgtime(&work->tv_getwork);

	val = json_rpc_call(curl, url, pool->rpc_userpass, rpc_req, false,
			    false, &work->rolltime, pool, false);
	pool_stats->getwork_attempts++;

	free(rpc_req);

	if (likely(val)) {
		rc = work_decode(pool, work, val);
		if (unlikely(!rc))
			applog(LOG_DEBUG, "Failed to decode work in get_upstream_work");
	} else if (PLP_NONE != (proto = pool_protocol_fallback(pool->proto))) {
		applog(LOG_WARNING, "Pool %u failed getblocktemplate request; falling back to getwork protocol", pool->pool_no);
		pool->proto = proto;
		goto tryagain;
	} else
		applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");

	cgtime(&work->tv_getwork_reply);
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
	calc_diff(work, 0);
	total_getworks++;
	pool->getwork_requested++;

	if (rc)
		update_last_work(work);

	if (likely(val))
		json_decref(val);

	return rc;
}

#ifdef HAVE_CURSES
static void disable_curses(void)
{
	if (curses_active_locked()) {
		use_curses = false;
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

static void __kill_work(void)
{
	struct thr_info *thr;
	int i;

	if (!successful_connect)
		return;

	applog(LOG_INFO, "Received kill message");

	shutting_down = true;

	applog(LOG_DEBUG, "Prompting submit_work thread to finish");
	notifier_wake(submit_waiting_notifier);

	applog(LOG_DEBUG, "Killing off watchpool thread");
	/* Kill the watchpool thread */
	thr = &control_thr[watchpool_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Killing off watchdog thread");
	/* Kill the watchdog thread */
	thr = &control_thr[watchdog_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Killing off mining threads");
	/* Kill the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		thr = get_thread(i);
		if (thr->cgpu->threads)
		{
			thr_info_cancel(thr);
			pthread_join(thr->pth, NULL);
		}
	}

	applog(LOG_DEBUG, "Killing off stage thread");
	/* Stop the others */
	thr = &control_thr[stage_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Killing off API thread");
	thr = &control_thr[api_thr_id];
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
#ifndef _WIN64
const
#endif
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

	if (unlikely(!ce))
		quit(1, "Failed to calloc in recruit_curl");

	ce->curl = curl_easy_init();
	if (unlikely(!ce->curl))
		quit(1, "Failed to init in recruit_curl");

	LL_PREPEND(pool->curllist, ce);
	pool->curls++;
}

/* Grab an available curl if there is one. If not, then recruit extra curls
 * unless we are in a submit_fail situation, or we have opt_delaynet enabled
 * and there are already 5 curls in circulation. Limit total number to the
 * number of mining threads per pool as well to prevent blasting a pool during
 * network delays/outages. */
static struct curl_ent *pop_curl_entry3(struct pool *pool, int blocking)
{
	int curl_limit = opt_delaynet ? 5 : (mining_threads + opt_queue) * 2;
	bool recruited = false;
	struct curl_ent *ce;

	mutex_lock(&pool->pool_lock);
retry:
	if (!pool->curls) {
		recruit_curl(pool);
		recruited = true;
	} else if (!pool->curllist) {
		if (blocking < 2 && pool->curls >= curl_limit && (blocking || pool->curls >= opt_submit_threads)) {
			if (!blocking) {
				mutex_unlock(&pool->pool_lock);
				return NULL;
			}
			pthread_cond_wait(&pool->cr_cond, &pool->pool_lock);
			goto retry;
		} else {
			recruit_curl(pool);
			recruited = true;
		}
	}
	ce = pool->curllist;
	LL_DELETE(pool->curllist, ce);
	mutex_unlock(&pool->pool_lock);

	if (recruited)
		applog(LOG_DEBUG, "Recruited curl for pool %d", pool->pool_no);
	return ce;
}

static struct curl_ent *pop_curl_entry2(struct pool *pool, bool blocking)
{
	return pop_curl_entry3(pool, blocking ? 1 : 0);
}

__maybe_unused
static struct curl_ent *pop_curl_entry(struct pool *pool)
{
	return pop_curl_entry3(pool, 1);
}

static void push_curl_entry(struct curl_ent *ce, struct pool *pool)
{
	mutex_lock(&pool->pool_lock);
	if (!ce || !ce->curl)
		quit(1, "Attempted to add NULL in push_curl_entry");
	LL_PREPEND(pool->curllist, ce);
	cgtime(&ce->tv);
	pthread_cond_broadcast(&pool->cr_cond);
	mutex_unlock(&pool->pool_lock);
}

bool stale_work(struct work *work, bool share);

static inline bool should_roll(struct work *work)
{
	struct timeval now;
	time_t expiry;

	if (work->pool != current_pool() && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE)
		return false;

	if (stale_work(work, false))
		return false;

	if (work->rolltime > opt_scantime)
		expiry = work->rolltime;
	else
		expiry = opt_scantime;
	expiry = expiry * 2 / 3;

	/* We shouldn't roll if we're unlikely to get one shares' duration
	 * work out of doing so */
	cgtime(&now);
	if (now.tv_sec - work->tv_staged.tv_sec > expiry)
		return false;
	
	return true;
}

/* Limit rolls to 7000 to not beyond 2 hours in the future where bitcoind will
 * reject blocks as invalid. */
static inline bool can_roll(struct work *work)
{
	if (work->stratum)
		return false;
	if (!(work->pool && !work->clone))
		return false;
	if (work->tmpl) {
		if (stale_work(work, false))
			return false;
		return blkmk_work_left(work->tmpl);
	}
	return (work->rolltime &&
		work->rolls < 7000 && !stale_work(work, false));
}

static void roll_work(struct work *work)
{
	if (work->tmpl) {
		if (blkmk_get_data(work->tmpl, work->data, 80, time(NULL), NULL, &work->dataid) < 76)
			applog(LOG_ERR, "Failed to get next data from template; spinning wheels!");
		swap32yes(work->data, work->data, 80 / 4);
		calc_midstate(work);
		applog(LOG_DEBUG, "Successfully rolled extranonce to dataid %u", work->dataid);
	} else {

	uint32_t *work_ntime;
	uint32_t ntime;

	work_ntime = (uint32_t *)(work->data + 68);
	ntime = be32toh(*work_ntime);
	ntime++;
	*work_ntime = htobe32(ntime);

		applog(LOG_DEBUG, "Successfully rolled time header in work");
	}

	local_work++;
	work->rolls++;
	work->blk.nonce = 0;

	/* This is now a different work item so it needs a different ID for the
	 * hashtable */
	work->id = total_work++;
}

/* Duplicates any dynamically allocated arrays within the work struct to
 * prevent a copied work struct from freeing ram belonging to another struct */
void __copy_work(struct work *work, const struct work *base_work)
{
	int id = work->id;

	clean_work(work);
	memcpy(work, base_work, sizeof(struct work));
	/* Keep the unique new id assigned during make_work to prevent copied
	 * work from having the same id. */
	work->id = id;
	if (base_work->job_id)
		work->job_id = strdup(base_work->job_id);
	if (base_work->nonce1)
		work->nonce1 = strdup(base_work->nonce1);
	if (base_work->nonce2)
		work->nonce2 = strdup(base_work->nonce2);
	if (base_work->ntime)
		work->ntime = strdup(base_work->ntime);

	if (base_work->tmpl) {
		struct pool *pool = work->pool;
		mutex_lock(&pool->pool_lock);
		++*work->tmpl_refcount;
		mutex_unlock(&pool->pool_lock);
	}
}

/* Generates a copy of an existing work struct, creating fresh heap allocations
 * for all dynamically allocated arrays within the struct */
struct work *copy_work(const struct work *base_work)
{
	struct work *work = make_work();

	__copy_work(work, base_work);

	return work;
}

static struct work *make_clone(struct work *work)
{
	struct work *work_clone = copy_work(work);

	work_clone->clone = true;
	cgtime((struct timeval *)&(work_clone->tv_cloned));
	work_clone->longpoll = false;
	work_clone->mandatory = false;
	/* Make cloned work appear slightly older to bias towards keeping the
	 * master work item which can be further rolled */
	work_clone->tv_staged.tv_sec -= 1;

	return work_clone;
}

static void stage_work(struct work *work);

static bool clone_available(void)
{
	struct work *work_clone = NULL, *work, *tmp;
	bool cloned = false;

	mutex_lock(stgd_lock);
	if (!staged_rollable)
		goto out_unlock;

	HASH_ITER(hh, staged_work, work, tmp) {
		if (can_roll(work) && should_roll(work)) {
			roll_work(work);
			work_clone = make_clone(work);
			roll_work(work);
			cloned = true;
			break;
		}
	}

out_unlock:
	mutex_unlock(stgd_lock);

	if (cloned) {
		applog(LOG_DEBUG, "Pushing cloned available work to stage thread");
		stage_work(work_clone);
	}
	return cloned;
}

static void pool_died(struct pool *pool)
{
	if (!pool_tset(pool, &pool->idle)) {
		cgtime(&pool->tv_idle);
		if (pool == current_pool()) {
			applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
			switch_pools(NULL);
		} else
			applog(LOG_INFO, "Pool %d %s failed to return work", pool->pool_no, pool->rpc_url);
	}
}

bool stale_work(struct work *work, bool share)
{
	unsigned work_expiry;
	struct pool *pool;
	uint32_t block_id;
	unsigned getwork_delay;

	if (opt_benchmark)
		return false;

	block_id = ((uint32_t*)work->data)[1];
	pool = work->pool;

	/* Technically the rolltime should be correct but some pools
	 * advertise a broken expire= that is lower than a meaningful
	 * scantime */
	if (work->rolltime >= opt_scantime || work->tmpl)
		work_expiry = work->rolltime;
	else
		work_expiry = opt_expiry;

	unsigned max_expiry = (have_longpoll ? opt_expiry_lp : opt_expiry);
	if (work_expiry > max_expiry)
		work_expiry = max_expiry;

	if (share) {
		/* If the share isn't on this pool's latest block, it's stale */
		if (pool->block_id && pool->block_id != block_id)
		{
			applog(LOG_DEBUG, "Share stale due to block mismatch (%08lx != %08lx)", (long)block_id, (long)pool->block_id);
			return true;
		}

		/* If the pool doesn't want old shares, then any found in work before
		 * the most recent longpoll is stale */
		if ((!pool->submit_old) && work->work_restart_id != pool->work_restart_id)
		{
			applog(LOG_DEBUG, "Share stale due to mandatory work update (%02x != %02x)", work->work_restart_id, pool->work_restart_id);
			return true;
		}
	} else {
		/* If this work isn't for the latest Bitcoin block, it's stale */
		/* But only care about the current pool if failover-only */
		if (enabled_pools <= 1 || opt_fail_only) {
			if (pool->block_id && block_id != pool->block_id)
			{
				applog(LOG_DEBUG, "Work stale due to block mismatch (%08lx != 1 ? %08lx : %08lx)", (long)block_id, (long)pool->block_id, (long)current_block_id);
				return true;
			}
		} else {
			if (block_id != current_block_id)
			{
				applog(LOG_DEBUG, "Work stale due to block mismatch (%08lx != 0 ? %08lx : %08lx)", (long)block_id, (long)pool->block_id, (long)current_block_id);
				return true;
			}
		}

		/* If the pool has asked us to restart since this work, it's stale */
		if (work->work_restart_id != pool->work_restart_id)
		{
			applog(LOG_DEBUG, "Work stale due to work update (%02x != %02x)", work->work_restart_id, pool->work_restart_id);
			return true;
		}

	if (pool->has_stratum && work->job_id) {
		bool same_job;

		if (!pool->stratum_active || !pool->stratum_notify) {
			applog(LOG_DEBUG, "Work stale due to stratum inactive");
			return true;
		}

		same_job = true;

		cg_rlock(&pool->data_lock);
		if (strcmp(work->job_id, pool->swork.job_id))
			same_job = false;
		cg_runlock(&pool->data_lock);

		if (!same_job) {
			applog(LOG_DEBUG, "Work stale due to stratum job_id mismatch");
			return true;
		}
	}

	/* Factor in the average getwork delay of this pool, rounding it up to
	 * the nearest second */
	getwork_delay = pool->cgminer_pool_stats.getwork_wait_rolling * 5 + 1;
	if (unlikely(work_expiry <= getwork_delay + 5))
		work_expiry = 5;
	else
		work_expiry -= getwork_delay;

	}

	double elapsed_since_staged = difftime(time(NULL), work->tv_staged.tv_sec);
	if (elapsed_since_staged > work_expiry) {
		applog(LOG_DEBUG, "%s stale due to expiry (%.0f >= %u)", share?"Share":"Work", elapsed_since_staged, work_expiry);
		return true;
	}

	/* If the user only wants strict failover, any work from a pool other than
	 * the current one is always considered stale */
	if (opt_fail_only && !share && pool != current_pool() && !work->mandatory &&
	    pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		applog(LOG_DEBUG, "Work stale due to fail only pool mismatch (pool %u vs %u)", pool->pool_no, current_pool()->pool_no);
		return true;
	}

	return false;
}

static uint64_t share_diff(const struct work *work)
{
	uint64_t ret;
	bool new_best = false;

	ret = target_diff(work->hash);

	cg_wlock(&control_lock);
	if (unlikely(ret > best_diff)) {
		new_best = true;
		best_diff = ret;
		suffix_string(best_diff, best_share, 0);
	}
	if (unlikely(ret > work->pool->best_diff))
		work->pool->best_diff = ret;
	cg_wunlock(&control_lock);

	if (unlikely(new_best))
		applog(LOG_INFO, "New best share: %s", best_share);

	return ret;
}

static void regen_hash(struct work *work)
{
	hash_data(work->hash, work->data);
}

static void rebuild_hash(struct work *work)
{
	if (opt_scrypt)
		scrypt_regenhash(work);
	else
		regen_hash(work);

	work->share_diff = share_diff(work);
	if (unlikely(work->share_diff >= current_diff)) {
		work->block = true;
		work->pool->solved++;
		found_blocks++;
		work->mandatory = true;
		applog(LOG_NOTICE, "Found block for pool %d!", work->pool->pool_no);
	}
}

static void submit_discard_share2(const char *reason, struct work *work)
{
	struct cgpu_info *cgpu = get_thr_cgpu(work->thr_id);

	sharelog(reason, work);

	mutex_lock(&stats_lock);
	++total_stale;
	++cgpu->stale;
	++(work->pool->stale_shares);
	total_diff_stale += work->work_difficulty;
	work->pool->diff_stale += work->work_difficulty;
	mutex_unlock(&stats_lock);
}

static void submit_discard_share(struct work *work)
{
	submit_discard_share2("discard", work);
}

struct submit_work_state {
	struct work *work;
	bool resubmit;
	struct curl_ent *ce;
	int failures;
	time_t staleexpire;
	char *s;
	struct timeval tv_submit;
	struct submit_work_state *next;
};

static int my_curl_timer_set(__maybe_unused CURLM *curlm, long timeout_ms, void *userp)
{
	long *p_timeout_us = userp;
	
	const long max_ms = LONG_MAX / 1000;
	if (max_ms < timeout_ms)
		timeout_ms = max_ms;
	
	*p_timeout_us = timeout_ms * 1000;
	return 0;
}

static void sws_has_ce(struct submit_work_state *sws)
{
	struct pool *pool = sws->work->pool;
	sws->s = submit_upstream_work_request(sws->work);
	cgtime(&sws->tv_submit);
	json_rpc_call_async(sws->ce->curl, pool->rpc_url, pool->rpc_userpass, sws->s, false, pool, true, sws);
}

static struct submit_work_state *begin_submission(struct work *work)
{
	struct pool *pool;
	struct submit_work_state *sws = NULL;

	pool = work->pool;
	sws = malloc(sizeof(*sws));
	*sws = (struct submit_work_state){
		.work = work,
	};

	rebuild_hash(work);

	if (stale_work(work, true)) {
		work->stale = true;
		if (opt_submit_stale)
			applog(LOG_NOTICE, "Pool %d stale share detected, submitting as user requested", pool->pool_no);
		else if (pool->submit_old)
			applog(LOG_NOTICE, "Pool %d stale share detected, submitting as pool requested", pool->pool_no);
		else {
			applog(LOG_NOTICE, "Pool %d stale share detected, discarding", pool->pool_no);
			submit_discard_share(work);
			goto out;
		}
		sws->staleexpire = time(NULL) + 300;
	}

	if (work->stratum) {
		char *s;

		s = malloc(1024);

		sws->s = s;
	} else {
		/* submit solution to bitcoin via JSON-RPC */
		sws->ce = pop_curl_entry2(pool, false);
		if (sws->ce) {
			sws_has_ce(sws);
		} else {
			sws->next = pool->sws_waiting_on_curl;
			pool->sws_waiting_on_curl = sws;
			if (sws->next)
				applog(LOG_DEBUG, "submit_thread queuing submission");
			else
				applog(LOG_WARNING, "submit_thread queuing submissions (see --submit-threads)");
		}
	}

	return sws;

out:
	free(sws);
	return NULL;
}

static bool retry_submission(struct submit_work_state *sws)
{
	struct work *work = sws->work;
	struct pool *pool = work->pool;

		sws->resubmit = true;
		if ((!work->stale) && stale_work(work, true)) {
			work->stale = true;
			if (opt_submit_stale)
				applog(LOG_NOTICE, "Pool %d share became stale during submission failure, will retry as user requested", pool->pool_no);
			else if (pool->submit_old)
				applog(LOG_NOTICE, "Pool %d share became stale during submission failure, will retry as pool requested", pool->pool_no);
			else {
				applog(LOG_NOTICE, "Pool %d share became stale during submission failure, discarding", pool->pool_no);
				submit_discard_share(work);
				return false;
			}
			sws->staleexpire = time(NULL) + 300;
		}
		if (unlikely((opt_retries >= 0) && (++sws->failures > opt_retries))) {
			applog(LOG_ERR, "Pool %d failed %d submission retries, discarding", pool->pool_no, opt_retries);
			submit_discard_share(work);
			return false;
		}
		else if (work->stale) {
			if (unlikely(opt_retries < 0 && sws->staleexpire <= time(NULL))) {
				applog(LOG_NOTICE, "Pool %d stale share failed to submit for 5 minutes, discarding", pool->pool_no);
				submit_discard_share(work);
				return false;
			}
		}

		/* pause, then restart work-request loop */
		applog(LOG_INFO, "json_rpc_call failed on submit_work, retrying");

		cgtime(&sws->tv_submit);
		json_rpc_call_async(sws->ce->curl, pool->rpc_url, pool->rpc_userpass, sws->s, false, pool, true, sws);
	
	return true;
}

static void free_sws(struct submit_work_state *sws)
{
	free(sws->s);
	free_work(sws->work);
	free(sws);
}

static void *submit_work_thread(__maybe_unused void *userdata)
{
	int wip = 0;
	CURLM *curlm;
	long curlm_timeout_us = -1;
	struct timeval curlm_timer;
	struct submit_work_state *sws, **swsp;
	struct submit_work_state *write_sws = NULL;
	unsigned tsreduce = 0;

	pthread_detach(pthread_self());

	RenameThread("submit_work");

	applog(LOG_DEBUG, "Creating extra submit work thread");

	curlm = curl_multi_init();
	curlm_timeout_us = -1;
	curl_multi_setopt(curlm, CURLMOPT_TIMERDATA, &curlm_timeout_us);
	curl_multi_setopt(curlm, CURLMOPT_TIMERFUNCTION, my_curl_timer_set);

	fd_set rfds, wfds, efds;
	int maxfd;
	struct timeval tv_timeout, tv_now;
	int n;
	CURLMsg *cm;
	FD_ZERO(&rfds);
	while (1) {
		mutex_lock(&submitting_lock);
		total_submitting -= tsreduce;
		tsreduce = 0;
		if (FD_ISSET(submit_waiting_notifier[0], &rfds)) {
			notifier_read(submit_waiting_notifier);
		}
		
		// Receive any new submissions
		while (submit_waiting) {
			struct work *work = submit_waiting;
			DL_DELETE(submit_waiting, work);
			if ( (sws = begin_submission(work)) ) {
				if (sws->ce)
					curl_multi_add_handle(curlm, sws->ce->curl);
				else if (sws->s) {
					sws->next = write_sws;
					write_sws = sws;
				}
				++wip;
			}
			else {
				--total_submitting;
				free_work(work);
			}
		}
		
		if (unlikely(shutting_down && !wip))
			break;
		mutex_unlock(&submitting_lock);
		
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_ZERO(&efds);
		tv_timeout.tv_sec = -1;
		
		// Setup cURL with select
		// Need to call perform to ensure the timeout gets updated
		curl_multi_perform(curlm, &n);
		curl_multi_fdset(curlm, &rfds, &wfds, &efds, &maxfd);
		if (curlm_timeout_us >= 0)
		{
			timer_set_delay_from_now(&curlm_timer, curlm_timeout_us);
			reduce_timeout_to(&tv_timeout, &curlm_timer);
		}
		
		// Setup waiting stratum submissions with select
		for (sws = write_sws; sws; sws = sws->next)
		{
			struct pool *pool = sws->work->pool;
			int fd = pool->sock;
			if (fd == INVSOCK || (!pool->stratum_init) || !pool->stratum_notify)
				continue;
			FD_SET(fd, &wfds);
			set_maxfd(&maxfd, fd);
		}
		
		// Setup "submit waiting" notifier with select
		FD_SET(submit_waiting_notifier[0], &rfds);
		set_maxfd(&maxfd, submit_waiting_notifier[0]);
		
		// Wait for something interesting to happen :)
		cgtime(&tv_now);
		if (select(maxfd+1, &rfds, &wfds, &efds, select_timeout(&tv_timeout, &tv_now)) < 0) {
			FD_ZERO(&rfds);
			continue;
		}
		
		// Handle any stratum ready-to-write results
		for (swsp = &write_sws; (sws = *swsp); ) {
			struct work *work = sws->work;
			struct pool *pool = work->pool;
			int fd = pool->sock;
			bool sessionid_match;
			
			if (fd == INVSOCK || (!pool->stratum_init) || (!pool->stratum_notify) || !FD_ISSET(fd, &wfds)) {
next_write_sws:
				// TODO: Check if stale, possibly discard etc
				swsp = &sws->next;
				continue;
			}
			
			cg_rlock(&pool->data_lock);
			// NOTE: cgminer only does this check on retries, but BFGMiner does it for even the first/normal submit; therefore, it needs to be such that it always is true on the same connection regardless of session management
			// NOTE: Worst case scenario for a false positive: the pool rejects it as H-not-zero
			sessionid_match = (!pool->nonce1) || !strcmp(work->nonce1, pool->nonce1);
			cg_runlock(&pool->data_lock);
			if (!sessionid_match)
			{
				applog(LOG_DEBUG, "No matching session id for resubmitting stratum share");
				submit_discard_share2("disconnect", work);
				++tsreduce;
next_write_sws_del:
				// Clear the fd from wfds, to avoid potentially blocking on other submissions to the same socket
				FD_CLR(fd, &wfds);
				// Delete sws for this submission, since we're done with it
				*swsp = sws->next;
				free_sws(sws);
				--wip;
				continue;
			}
			
			char *s = sws->s;
			struct stratum_share *sshare = calloc(sizeof(struct stratum_share), 1);
			int sshare_id;
			uint32_t nonce;
			char *noncehex;
			
			sshare->sshare_time = time(NULL);
			sshare->work = copy_work(work);
			nonce = *((uint32_t *)(work->data + 76));
			noncehex = bin2hex((const unsigned char *)&nonce, 4);
			
			mutex_lock(&sshare_lock);
			/* Give the stratum share a unique id */
			sshare_id =
			sshare->id = swork_id++;
			HASH_ADD_INT(stratum_shares, id, sshare);
			sprintf(s, "{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"], \"id\": %d, \"method\": \"mining.submit\"}",
				pool->rpc_user, work->job_id, work->nonce2, work->ntime, noncehex, sshare->id);
			mutex_unlock(&sshare_lock);
			
			free(noncehex);
			
			applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->stratum_url, s);

			if (likely(stratum_send(pool, s, strlen(s)))) {
				if (pool_tclear(pool, &pool->submit_fail))
					applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);
				applog(LOG_DEBUG, "Successfully submitted, adding to stratum_shares db");
				goto next_write_sws_del;
			} else if (!pool_tset(pool, &pool->submit_fail)) {
				// Undo stuff
				mutex_lock(&sshare_lock);
				// NOTE: Need to find it again in case something else has consumed it already (like the stratum-disconnect resubmitter...)
				HASH_FIND_INT(stratum_shares, &sshare_id, sshare);
				if (sshare)
					HASH_DEL(stratum_shares, sshare);
				mutex_unlock(&sshare_lock);
				if (sshare)
				{
					free_work(sshare->work);
					free(sshare);
				}
				
				applog(LOG_WARNING, "Pool %d stratum share submission failure", pool->pool_no);
				total_ro++;
				pool->remotefail_occasions++;
				
				if (!sshare)
					goto next_write_sws_del;
				
				goto next_write_sws;
			}
		}
		
		// Handle any cURL activities
		curl_multi_perform(curlm, &n);
		while( (cm = curl_multi_info_read(curlm, &n)) ) {
			if (cm->msg == CURLMSG_DONE)
			{
				bool finished;
				json_t *val = json_rpc_call_completed(cm->easy_handle, cm->data.result, false, NULL, &sws);
				curl_multi_remove_handle(curlm, cm->easy_handle);
				finished = submit_upstream_work_completed(sws->work, sws->resubmit, &sws->tv_submit, val);
				if (!finished) {
					if (retry_submission(sws))
						curl_multi_add_handle(curlm, sws->ce->curl);
					else
						finished = true;
				}
				
				if (finished) {
					--wip;
					++tsreduce;
					struct pool *pool = sws->work->pool;
					if (pool->sws_waiting_on_curl) {
						pool->sws_waiting_on_curl->ce = sws->ce;
						sws_has_ce(pool->sws_waiting_on_curl);
						pool->sws_waiting_on_curl = pool->sws_waiting_on_curl->next;
						curl_multi_add_handle(curlm, sws->ce->curl);
					} else {
						push_curl_entry(sws->ce, sws->work->pool);
					}
					free_sws(sws);
				}
			}
		}
	}
	assert(!write_sws);
	mutex_unlock(&submitting_lock);

	curl_multi_cleanup(curlm);

	applog(LOG_DEBUG, "submit_work thread exiting");

	return NULL;
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

int prioritize_pools(char *param, int *pid)
{
	char *ptr, *next;
	int i, pr, prio = 0;

	if (total_pools == 0) {
		return MSG_NOPOOL;
	}

	if (param == NULL || *param == '\0') {
		return MSG_MISPID;
	}

	bool pools_changed[total_pools];
	int new_prio[total_pools];
	for (i = 0; i < total_pools; ++i)
		pools_changed[i] = false;

	next = param;
	while (next && *next) {
		ptr = next;
		next = strchr(ptr, ',');
		if (next)
			*(next++) = '\0';

		i = atoi(ptr);
		if (i < 0 || i >= total_pools) {
			*pid = i;
			return MSG_INVPID;
		}

		if (pools_changed[i]) {
			*pid = i;
			return MSG_DUPPID;
		}

		pools_changed[i] = true;
		new_prio[i] = prio++;
	}

	// Only change them if no errors
	for (i = 0; i < total_pools; i++) {
		if (pools_changed[i])
			pools[i]->prio = new_prio[i];
	}

	// In priority order, cycle through the unchanged pools and append them
	for (pr = 0; pr < total_pools; pr++)
		for (i = 0; i < total_pools; i++) {
			if (!pools_changed[i] && pools[i]->prio == pr) {
				pools[i]->prio = prio++;
				pools_changed[i] = true;
				break;
			}
		}

	if (current_pool()->prio)
		switch_pools(NULL);

	return MSG_POOLPRIO;
}

void validate_pool_priorities(void)
{
	// TODO: this should probably do some sort of logging
	int i, j;
	bool used[total_pools];
	bool valid[total_pools];

	for (i = 0; i < total_pools; i++)
		used[i] = valid[i] = false;

	for (i = 0; i < total_pools; i++) {
		if (pools[i]->prio >=0 && pools[i]->prio < total_pools) {
			if (!used[pools[i]->prio]) {
				valid[i] = true;
				used[pools[i]->prio] = true;
			}
		}
	}

	for (i = 0; i < total_pools; i++) {
		if (!valid[i]) {
			for (j = 0; j < total_pools; j++) {
				if (!used[j]) {
					applog(LOG_WARNING, "Pool %d priority changed from %d to %d", i, pools[i]->prio, j);
					pools[i]->prio = j;
					used[j] = true;
					break;
				}
			}
		}
	}
}

static void clear_pool_work(struct pool *pool);

/* Specifies whether we can switch to this pool or not. */
static bool pool_unusable(struct pool *pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	return false;
}

void switch_pools(struct pool *selected)
{
	struct pool *pool, *last_pool;
	int i, pool_no, next_pool;

	cg_wlock(&control_lock);
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
				if (pool_unusable(pool))
					continue;
				pool_no = pool->pool_no;
				break;
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
				if (pool_unusable(pool))
					continue;
				pool_no = next_pool;
				break;
			}
			break;
		default:
			break;
	}

	currentpool = pools[pool_no];
	pool = currentpool;
	cg_wunlock(&control_lock);

	/* Set the lagging flag to avoid pool not providing work fast enough
	 * messages in failover only mode since  we have to get all fresh work
	 * as in restart_threads */
	if (opt_fail_only)
		pool_tset(pool, &pool->lagging);

	if (pool != last_pool)
	{
		pool->block_id = 0;
		if (pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
			applog(LOG_WARNING, "Switching to pool %d %s", pool->pool_no, pool->rpc_url);
			if (pool->last_work_copy || pool->has_stratum || opt_fail_only)
				clear_pool_work(last_pool);
		}
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

static void wake_gws(void)
{
	mutex_lock(stgd_lock);
	pthread_cond_signal(&gws_cond);
	mutex_unlock(stgd_lock);
}

static void discard_stale(void)
{
	struct work *work, *tmp;
	int stale = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (stale_work(work, false)) {
			HASH_DEL(staged_work, work);
			discard_work(work);
			stale++;
		}
	}
	pthread_cond_signal(&gws_cond);
	mutex_unlock(stgd_lock);

	if (stale)
		applog(LOG_DEBUG, "Discarded %d stales that didn't match current hash", stale);
}

bool stale_work_future(struct work *work, bool share, unsigned long ustime)
{
	bool rv;
	struct timeval tv, orig;
	ldiv_t d;
	
	d = ldiv(ustime, 1000000);
	tv = (struct timeval){
		.tv_sec = d.quot,
		.tv_usec = d.rem,
	};
	orig = work->tv_staged;
	timersub(&orig, &tv, &work->tv_staged);
	rv = stale_work(work, share);
	work->tv_staged = orig;
	
	return rv;
}

static void restart_threads(void)
{
	struct pool *cp = current_pool();
	int i;
	struct thr_info *thr;

	/* Artificially set the lagging flag to avoid pool not providing work
	 * fast enough  messages after every long poll */
	pool_tset(cp, &cp->lagging);

	/* Discard staged work that is now stale */
	discard_stale();

	rd_lock(&mining_thr_lock);
	
	for (i = 0; i < mining_threads; i++)
	{
		thr = mining_thr[i];
		thr->work_restart = true;
	}
	
	for (i = 0; i < mining_threads; i++)
	{
		thr = mining_thr[i];
		notifier_wake(thr->work_restart_notifier);
	}
	
	rd_unlock(&mining_thr_lock);
}

static char *blkhashstr(unsigned char *hash)
{
	unsigned char hash_swap[32];
	swap256(hash_swap, hash);
	swap32tole(hash_swap, hash_swap, 32 / 4);
	return bin2hex(hash_swap, 32);
}

static void set_curblock(char *hexstr, unsigned char *hash)
{
	unsigned char hash_swap[32];

	current_block_id = ((uint32_t*)hash)[0];
	strcpy(current_block, hexstr);
	swap256(hash_swap, hash);
	swap32tole(hash_swap, hash_swap, 32 / 4);

	cg_wlock(&ch_lock);
	cgtime(&block_timeval);
	__update_block_title(hash_swap);
	free(current_fullhash);
	current_fullhash = bin2hex(hash_swap, 32);
	get_timestamp(blocktime, &block_timeval);
	cg_wunlock(&ch_lock);

	applog(LOG_INFO, "New block: %s diff %s (%s)", current_hash, block_diff, net_hashrate);
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

static void set_blockdiff(const struct work *work)
{
	unsigned char target[32];
	double diff;
	uint64_t diff64;

	real_block_target(target, work->data);
	diff = target_diff(target);
	diff64 = diff;

	suffix_string(diff64, block_diff, 0);
	hashrate_to_bufstr(net_hashrate, diff * 7158278, -1, H2B_SHORT);
	if (unlikely(current_diff != diff))
		applog(LOG_NOTICE, "Network difficulty changed to %s (%s)", block_diff, net_hashrate);
	current_diff = diff;
}

static bool test_work_current(struct work *work)
{
	bool ret = true;
	char *hexstr;

	if (work->mandatory)
		return ret;

	uint32_t block_id = ((uint32_t*)(work->data))[1];

	/* Hack to work around dud work sneaking into test */
	hexstr = bin2hex(work->data + 8, 18);
	if (!strncmp(hexstr, "000000000000000000000000000000000000", 36))
		goto out_free;

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
		set_blockdiff(work);
		wr_unlock(&blk_lock);
		work->pool->block_id = block_id;

		if (deleted_block)
			applog(LOG_DEBUG, "Deleted block %d from database", deleted_block);
		template_nonce = 0;
		set_curblock(hexstr, &work->data[4]);
		if (unlikely(new_blocks == 1))
			goto out_free;

		if (!work->stratum) {
			if (work->longpoll) {
				applog(LOG_NOTICE, "Longpoll from pool %d detected new block",
				       work->pool->pool_no);
			} else if (have_longpoll)
				applog(LOG_NOTICE, "New block detected on network before longpoll");
			else
				applog(LOG_NOTICE, "New block detected on network");
		}
		restart_threads();
	} else {
		bool restart = false;
		struct pool *curpool = NULL;
		if (unlikely(work->pool->block_id != block_id)) {
			bool was_active = work->pool->block_id != 0;
			work->pool->block_id = block_id;
			if (!work->longpoll)
				update_last_work(work);
			if (was_active) {  // Pool actively changed block
				if (work->pool == (curpool = current_pool()))
					restart = true;
				if (block_id == current_block_id) {
					// Caught up, only announce if this pool is the one in use
					if (restart)
						applog(LOG_NOTICE, "%s %d caught up to new block",
						       work->longpoll ? "Longpoll from pool" : "Pool",
						       work->pool->pool_no);
				} else {
					// Switched to a block we know, but not the latest... why?
					// This might detect pools trying to double-spend or 51%,
					// but let's not make any accusations until it's had time
					// in the real world.
					free(hexstr);
					hexstr = blkhashstr(&work->data[4]);
					applog(LOG_WARNING, "%s %d is issuing work for an old block: %s",
					       work->longpoll ? "Longpoll from pool" : "Pool",
					       work->pool->pool_no,
					       hexstr);
				}
			}
		}
	  if (work->longpoll) {
		++work->pool->work_restart_id;
		update_last_work(work);
		if ((!restart) && work->pool == current_pool()) {
			applog(
			       (opt_quiet_work_updates ? LOG_DEBUG : LOG_NOTICE),
			       "Longpoll from pool %d requested work update",
				work->pool->pool_no);
			restart = true;
		}
	  }
		if (restart)
			restart_threads();
	}
	work->longpoll = false;
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
	pthread_cond_broadcast(&getq->cond);
	mutex_unlock(stgd_lock);

	return rc;
}

static void *stage_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	bool ok = true;

#ifndef HAVE_PTHREAD_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

	RenameThread("stage");

	while (ok) {
		struct work *work = NULL;

		applog(LOG_DEBUG, "Popping work to stage thread");

		work = tq_pop(mythr->q, NULL);
		if (unlikely(!work)) {
			applog(LOG_ERR, "Failed to tq_pop in stage_thread");
			ok = false;
			break;
		}
		work->work_restart_id = work->pool->work_restart_id;

		test_work_current(work);

		applog(LOG_DEBUG, "Pushing work to getwork queue (queued=%c)", work->queued?'Y':'N');

		if (unlikely(!hash_push(work))) {
			applog(LOG_WARNING, "Failed to hash_push in stage_thread");
			continue;
		}
	}

	tq_freeze(mythr->q);
	return NULL;
}

static void stage_work(struct work *work)
{
	applog(LOG_DEBUG, "Pushing work from pool %d to hash queue", work->pool->pool_no);
	work->work_restart_id = work->pool->work_restart_id;
	work->pool->last_work_time = time(NULL);
	test_work_current(work);
	hash_push(work);
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
		wlog("%s own long-poll support\n", pool->lp_url ? "Has" : "Does not have");
		wlog(" Queued work requests: %d\n", pool->getwork_requested);
		wlog(" Share submissions: %d\n", pool->accepted + pool->rejected);
		wlog(" Accepted shares: %d\n", pool->accepted);
		wlog(" Rejected shares: %d + %d stale (%.2f%%)\n",
		     pool->rejected, pool->stale_shares,
		     (float)(pool->rejected + pool->stale_shares) / (float)(pool->rejected + pool->stale_shares + pool->accepted)
		);
		wlog(" Accepted difficulty shares: %1.f\n", pool->diff_accepted);
		wlog(" Rejected difficulty shares: %1.f\n", pool->diff_rejected);
		uint64_t pool_bytes_xfer = pool->cgminer_pool_stats.net_bytes_received + pool->cgminer_pool_stats.net_bytes_sent;
		efficiency = pool_bytes_xfer ? pool->diff_accepted * 2048. / pool_bytes_xfer : 0.0;
		wlog(" Efficiency (accepted * difficulty / 2 KB): %.2f\n", efficiency);

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
	pool->has_stratum = false;
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
		fprintf(fcfg, "%s\n\t{\n\t\t\"url\" : \"%s\",", i > 0 ? "," : "", json_escape(pools[i]->rpc_url));
		if (pools[i]->rpc_proxy)
			fprintf(fcfg, "\n\t\t\"pool-proxy\" : \"%s\",", json_escape(pools[i]->rpc_proxy));
		fprintf(fcfg, "\n\t\t\"user\" : \"%s\",", json_escape(pools[i]->rpc_user));
		fprintf(fcfg, "\n\t\t\"pass\" : \"%s\",", json_escape(pools[i]->rpc_pass));
		fprintf(fcfg, "\n\t\t\"pool-priority\" : \"%d\"", pools[i]->prio);
		if (pools[i]->force_rollntime)
			fprintf(fcfg, ",\n\t\t\"force-rollntime\" : %d", pools[i]->force_rollntime);
		fprintf(fcfg, "\n\t}");
	}
	fputs("\n]\n", fcfg);

	fputs(",\n\"temp-cutoff\" : \"", fcfg);
	for (i = 0; i < total_devices; ++i)
		fprintf(fcfg, "%s%d", i > 0 ? "," : "", devices[i]->cutofftemp);
	fputs("\",\n\"temp-target\" : \"", fcfg);
	for (i = 0; i < total_devices; ++i)
		fprintf(fcfg, "%s%d", i > 0 ? "," : "", devices[i]->targettemp);
	fputs("\"", fcfg);
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
		fputs("\",\n\"temp-overheat\" : \"", fcfg);
		for(i = 0; i < nDevs; i++)
			fprintf(fcfg, "%s%d", i > 0 ? "," : "", gpus[i].adl.overtemp);
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
			   (void *)opt->cb_arg == (void *)set_int_1_to_10) &&
			   opt->desc != opt_hidden &&
			   0 <= *(int *)opt->u.arg)
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
	
	// We can only remove devices or disable them by default, but not both...
	if (!(opt_removedisabled && devices_enabled))
	{
		// Don't need to remove any, so we can set defaults here
		for (i = 0; i < total_devices; ++i)
			if (devices[i]->deven == DEV_DISABLED)
			{
				// At least one device is in fact disabled, so include device params
				fprintf(fcfg, ",\n\"device\" : [");
				bool first = true;
				for (i = 0; i < total_devices; ++i)
					if (devices[i]->deven != DEV_DISABLED)
					{
						fprintf(fcfg, "%s\n\t%d", first ? "" : ",", i);
						first = false;
					}
				fprintf(fcfg, "\n]");
				
				break;
			}
	}
	else
	if (devices_enabled) {
		// Mark original device params and remove-disabled
		fprintf(fcfg, ",\n\"device\" : [");
		bool first = true;
		for (i = 0; i < (int)(sizeof(devices_enabled) * 8) - 1; ++i) {
			if (devices_enabled & (1 << i))
			{
				fprintf(fcfg, "%s\n\t%d", first ? "" : ",", i);
				first = false;
			}
		}
		fprintf(fcfg, "\n]");
		fprintf(fcfg, ",\n\"remove-disabled\" : true");
	}
	
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

void zero_bestshare(void)
{
	int i;

	best_diff = 0;
	memset(best_share, 0, 8);
	suffix_string(best_diff, best_share, 0);

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];
		pool->best_diff = 0;
	}
}

void zero_stats(void)
{
	int i;

	cgtime(&total_tv_start);
	miner_started = total_tv_start;
	total_mhashes_done = 0;
	total_getworks = 0;
	total_accepted = 0;
	total_rejected = 0;
	hw_errors = 0;
	total_stale = 0;
	total_discarded = 0;
	total_bytes_xfer = 0;
	new_blocks = 0;
	local_work = 0;
	total_go = 0;
	total_ro = 0;
	total_secs = 1.0;
	total_diff1 = 0;
	found_blocks = 0;
	total_diff_accepted = 0;
	total_diff_rejected = 0;
	total_diff_stale = 0;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->getwork_requested = 0;
		pool->accepted = 0;
		pool->rejected = 0;
		pool->solved = 0;
		pool->getwork_requested = 0;
		pool->stale_shares = 0;
		pool->discarded_work = 0;
		pool->getfail_occasions = 0;
		pool->remotefail_occasions = 0;
		pool->last_share_time = 0;
		pool->diff1 = 0;
		pool->diff_accepted = 0;
		pool->diff_rejected = 0;
		pool->diff_stale = 0;
		pool->last_share_diff = 0;
		pool->cgminer_stats.getwork_calls = 0;
		pool->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		pool->cgminer_stats.getwork_wait_max.tv_sec = 0;
		pool->cgminer_stats.getwork_wait_max.tv_usec = 0;
		pool->cgminer_pool_stats.getwork_calls = 0;
		pool->cgminer_pool_stats.getwork_attempts = 0;
		pool->cgminer_pool_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		pool->cgminer_pool_stats.getwork_wait_max.tv_sec = 0;
		pool->cgminer_pool_stats.getwork_wait_max.tv_usec = 0;
		pool->cgminer_pool_stats.min_diff = 0;
		pool->cgminer_pool_stats.max_diff = 0;
		pool->cgminer_pool_stats.min_diff_count = 0;
		pool->cgminer_pool_stats.max_diff_count = 0;
		pool->cgminer_pool_stats.times_sent = 0;
		pool->cgminer_pool_stats.bytes_sent = 0;
		pool->cgminer_pool_stats.net_bytes_sent = 0;
		pool->cgminer_pool_stats.times_received = 0;
		pool->cgminer_pool_stats.bytes_received = 0;
		pool->cgminer_pool_stats.net_bytes_received = 0;
	}

	zero_bestshare();

	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = get_devices(i);

		mutex_lock(&hash_lock);
		cgpu->total_mhashes = 0;
		cgpu->accepted = 0;
		cgpu->rejected = 0;
		cgpu->stale = 0;
		cgpu->hw_errors = 0;
		cgpu->utility = 0.0;
		cgpu->utility_diff1 = 0;
		cgpu->last_share_pool_time = 0;
		cgpu->diff1 = 0;
		cgpu->diff_accepted = 0;
		cgpu->diff_rejected = 0;
		cgpu->last_share_diff = 0;
		cgpu->thread_fail_init_count = 0;
		cgpu->thread_zero_hash_count = 0;
		cgpu->thread_fail_queue_count = 0;
		cgpu->dev_sick_idle_60_count = 0;
		cgpu->dev_dead_idle_600_count = 0;
		cgpu->dev_nostart_count = 0;
		cgpu->dev_over_heat_count = 0;
		cgpu->dev_thermal_cutoff_count = 0;
		cgpu->dev_comms_error_count = 0;
		cgpu->dev_throttle_count = 0;
		cgpu->cgminer_stats.getwork_calls = 0;
		cgpu->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		cgpu->cgminer_stats.getwork_wait_max.tv_sec = 0;
		cgpu->cgminer_stats.getwork_wait_max.tv_usec = 0;
		mutex_unlock(&hash_lock);
	}
}

#ifdef HAVE_CURSES
static void display_pools(void)
{
	struct pool *pool;
	int selected, i, j;
	char input;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
updated:
	for (j = 0; j < total_pools; j++) {
		for (i = 0; i < total_pools; i++) {
			pool = pools[i];

			if (pool->prio != j)
				continue;

			if (pool == current_pool())
				wattron(logwin, A_BOLD);
			if (pool->enabled != POOL_ENABLED)
				wattron(logwin, A_DIM);
			wlogprint("%d: ", pool->prio);
			switch (pool->enabled) {
				case POOL_ENABLED:
					wlogprint("Enabled  ");
					break;
				case POOL_DISABLED:
					wlogprint("Disabled ");
					break;
				case POOL_REJECTING:
					wlogprint("Rejectin ");
					break;
			}
			if (pool->idle)
				wlogprint("Dead ");
			else
			if (pool->has_stratum)
				wlogprint("Strtm");
			else
			if (pool->lp_url && pool->proto != pool->lp_proto)
				wlogprint("Mixed");
			else
				switch (pool->proto) {
					case PLP_GETBLOCKTEMPLATE:
						wlogprint(" GBT ");
						break;
					case PLP_GETWORK:
						wlogprint("GWork");
						break;
					default:
						wlogprint("Alive");
				}
			wlogprint(" Pool %d: %s  User:%s\n",
				pool->pool_no,
				pool->rpc_url, pool->rpc_user);
			wattroff(logwin, A_BOLD | A_DIM);

			break; //for (i = 0; i < total_pools; i++)
		}
	}
retry:
	wlogprint("\nCurrent pool management strategy: %s\n",
		strategies[pool_strategy].s);
	if (pool_strategy == POOL_ROTATE)
		wlogprint("Set to rotate every %d minutes\n", opt_rotate_period);
	wlogprint("[F]ailover only %s\n", opt_fail_only ? "enabled" : "disabled");
	wlogprint("[A]dd pool [R]emove pool [D]isable pool [E]nable pool [P]rioritize pool\n");
	wlogprint("[C]hange management strategy [S]witch pool [I]nformation\n");
	wlogprint("Or press any other key to continue\n");
	logwin_update();
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
			wlogprint("%d: %s\n", i, strategies[i].s);
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
        } else if (!strncasecmp(&input, "p", 1)) {
			char *prilist = curses_input("Enter new pool priority (comma separated list)");
			int res = prioritize_pools(prilist, &i);
			free(prilist);
			switch (res) {
        		case MSG_NOPOOL:
        			wlogprint("No pools\n");
        			goto retry;
        		case MSG_MISPID:
        			wlogprint("Missing pool id parameter\n");
        			goto retry;
        		case MSG_INVPID:
        			wlogprint("Invalid pool id %d - range is 0 - %d\n", i, total_pools - 1);
        			goto retry;
        		case MSG_DUPPID:
        			wlogprint("Duplicate pool specified %d\n", i);
        			goto retry;
        		case MSG_POOLPRIO:
        		default:
        			goto updated;
        	}
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}

static const char *summary_detail_level_str(void)
{
	if (opt_compact)
		return "compact";
	if (opt_show_procs)
		return "processors";
	return "devices";
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
		  "[R]PC debug:%s\n[W]orkTime details:%s\nsu[M]mary detail level:%s\n"
		  "[L]og interval:%d\n[Z]ero statistics\n",
		opt_debug_console ? "on" : "off",
	        want_per_device_stats? "on" : "off",
		opt_quiet ? "on" : "off",
		opt_log_output ? "on" : "off",
		opt_protocol ? "on" : "off",
		opt_worktime ? "on" : "off",
		summary_detail_level_str(),
		opt_log_interval);
	wlogprint("Select an option or any other key to return\n");
	logwin_update();
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
		opt_debug_console = false;
		opt_quiet = false;
		opt_protocol = false;
		opt_compact = false;
		opt_show_procs = false;
		devsummaryYOffset = 0;
		want_per_device_stats = false;
		wlogprint("Output mode reset to normal\n");
		switch_compact();
		goto retry;
	} else if (!strncasecmp(&input, "d", 1)) {
		opt_debug = true;
		opt_debug_console ^= true;
		opt_log_output = opt_debug_console;
		if (opt_debug_console)
			opt_quiet = false;
		wlogprint("Debug mode %s\n", opt_debug_console ? "enabled" : "disabled");
		goto retry;
	} else if (!strncasecmp(&input, "m", 1)) {
		if (opt_compact)
			opt_compact = false;
		else
		if (!opt_show_procs)
			opt_show_procs = true;
		else
		{
			opt_compact = true;
			opt_show_procs = false;
			devsummaryYOffset = 0;
		}
		wlogprint("su[M]mary detail level changed to: %s\n", summary_detail_level_str());
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
	} else if (!strncasecmp(&input, "z", 1)) {
		zero_stats();
		goto retry;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}
#endif

void default_save_file(char *filename)
{
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
	wlogprint("\n[L]ongpoll: %s\n", want_longpoll ? "On" : "Off");
	wlogprint("[Q]ueue: %d\n[S]cantime: %d\n[E]xpiry: %d\n[R]etries: %d\n"
		  "[W]rite config file\n[B]FGMiner restart\n",
		opt_queue, opt_scantime, opt_expiry, opt_retries);
	wlogprint("Select an option or any other key to return\n");
	logwin_update();
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
	} else if  (!strncasecmp(&input, "w", 1)) {
		FILE *fcfg;
		char *str, filename[PATH_MAX], prompt[PATH_MAX + 50];

		default_save_file(filename);
		sprintf(prompt, "Config filename to write (Enter for default) [%s]", filename);
		str = curses_input(prompt);
		if (strcmp(str, "-1")) {
			struct stat statbuf;

			strcpy(filename, str);
			free(str);
			if (!stat(filename, &statbuf)) {
				wlogprint("File exists, overwrite?\n");
				input = getch();
				if (strncasecmp(&input, "y", 1))
					goto retry;
			}
		}
		else
			free(str);
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
	RenameThread("input");

	if (!curses_active)
		return NULL;

	while (1) {
		int input;

		input = getch();
		switch (input) {
		case 'q': case 'Q':
			kill_work();
			return NULL;
		case 'd': case 'D':
			display_options();
			break;
		case 'p': case 'P':
			display_pools();
			break;
		case 's': case 'S':
			set_options();
			break;
		case 'g': case 'G':
			if (have_opencl)
				manage_gpu();
			break;
#ifdef HAVE_CURSES
		case KEY_DOWN:
			if (devsummaryYOffset < -(total_lines + devcursor - statusy))
				break;
			devsummaryYOffset -= 2;
		case KEY_UP:
			if (devsummaryYOffset == 0)
				break;
			++devsummaryYOffset;
			if (curses_active_locked()) {
				int i;
				for (i = 0; i < mining_threads; i++)
					curses_print_devstatus(i);
				touchwin(statuswin);
				wrefresh(statuswin);
				unlock_curses();
			}
			break;
#endif
		}
		if (opt_realquiet) {
			disable_curses();
			break;
		}
	}

	return NULL;
}
#endif

static void *api_thread(void *userdata)
{
	struct thr_info *mythr = userdata;

	pthread_detach(pthread_self());
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("rpc");

	api(api_thr_id);

	mythr->has_pth = false;

	return NULL;
}

void thread_reportin(struct thr_info *thr)
{
	cgtime(&thr->last);
	thr->cgpu->status = LIFE_WELL;
	thr->getwork = 0;
	thr->cgpu->device_last_well = time(NULL);
}

void thread_reportout(struct thr_info *thr)
{
	thr->getwork = time(NULL);
}

static void hashmeter(int thr_id, struct timeval *diff,
		      uint64_t hashes_done)
{
	struct timeval temp_tv_end, total_diff;
	double secs;
	double local_secs;
	static double local_mhashes_done = 0;
	static double rolling = 0;
	double local_mhashes = (double)hashes_done / 1000000.0;
	bool showlog = false;
	char cHr[h2bs_fmt_size[H2B_NOUNIT]], aHr[h2bs_fmt_size[H2B_NOUNIT]], uHr[h2bs_fmt_size[H2B_SPACED]];
	char rejpcbuf[6];
	struct thr_info *thr;

	/* Update the last time this thread reported in */
	if (thr_id >= 0) {
		thr = get_thread(thr_id);
		cgtime(&(thr->last));
		thr->cgpu->device_last_well = time(NULL);
	}

	secs = (double)diff->tv_sec + ((double)diff->tv_usec / 1000000.0);

	/* So we can call hashmeter from a non worker thread */
	if (thr_id >= 0) {
		struct cgpu_info *cgpu = thr->cgpu;
		int threadobj = cgpu->threads ?: 1;
		double thread_rolling = 0.0;
		int i;

		applog(LOG_DEBUG, "[thread %d: %"PRIu64" hashes, %.1f khash/sec]",
			thr_id, hashes_done, hashes_done / 1000 / secs);

		/* Rolling average for each thread and each device */
		decay_time(&thr->rolling, local_mhashes / secs);
		for (i = 0; i < threadobj; i++)
			thread_rolling += cgpu->thr[i]->rolling;

		mutex_lock(&hash_lock);
		decay_time(&cgpu->rolling, thread_rolling);
		cgpu->total_mhashes += local_mhashes;
		mutex_unlock(&hash_lock);

		// If needed, output detailed, per-device stats
		if (want_per_device_stats) {
			struct timeval now;
			struct timeval elapsed;
			struct timeval *last_msg_tv = opt_show_procs ? &thr->cgpu->last_message_tv : &thr->cgpu->device->last_message_tv;

			cgtime(&now);
			timersub(&now, last_msg_tv, &elapsed);
			if (opt_log_interval <= elapsed.tv_sec) {
				struct cgpu_info *cgpu = thr->cgpu;
				char logline[255];

				*last_msg_tv = now;

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
	cgtime(&temp_tv_end);
	timersub(&temp_tv_end, &total_tv_end, &total_diff);

	total_mhashes_done += local_mhashes;
	local_mhashes_done += local_mhashes;
	/* Only update with opt_log_interval */
	if (total_diff.tv_sec < opt_log_interval)
		goto out_unlock;
	showlog = true;
	cgtime(&total_tv_end);

	local_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);
	decay_time(&rolling, local_mhashes_done / local_secs);
	global_hashrate = roundl(rolling) * 1000000;

	timersub(&total_tv_end, &total_tv_start, &total_diff);
	total_secs = (double)total_diff.tv_sec +
		((double)total_diff.tv_usec / 1000000.0);

	ti_hashrate_bufstr(
		(char*[]){cHr, aHr, uHr},
		1e6*rolling,
		1e6*total_mhashes_done / total_secs,
		utility_to_hashrate(total_diff_accepted / (total_secs ?: 1) * 60),
		H2B_SPACED);

	sprintf(statusline, "%s%ds:%s avg:%s u:%s | A:%d R:%d+%d(%s) HW:%d",
		want_per_device_stats ? "ALL " : "",
		opt_log_interval,
		cHr, aHr,
		uHr,
		total_accepted,
		total_rejected,
		total_stale,
		percentf(total_rejected + total_stale, total_accepted, rejpcbuf),
		hw_errors);


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

void hashmeter2(struct thr_info *thr)
{
	struct timeval tv_now, tv_elapsed;
	
	timerclear(&thr->tv_hashes_done);
	
	cgtime(&tv_now);
	timersub(&tv_now, &thr->tv_lastupdate, &tv_elapsed);
	/* Update the hashmeter at most 5 times per second */
	if (tv_elapsed.tv_sec > 0 || tv_elapsed.tv_usec > 200) {
		hashmeter(thr->id, &tv_elapsed, thr->hashes_done);
		thr->hashes_done = 0;
		thr->tv_lastupdate = tv_now;
	}
}

static void stratum_share_result(json_t *val, json_t *res_val, json_t *err_val,
				 struct stratum_share *sshare)
{
	struct work *work = sshare->work;

	share_result(val, res_val, err_val, work, false, "");
}

/* Parses stratum json responses and tries to find the id that the request
 * matched to and treat it accordingly. */
bool parse_stratum_response(struct pool *pool, char *s)
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

	if (!json_is_integer(id_val)) {
		if (json_is_string(id_val)
		 && !strncmp(json_string_value(id_val), "txlist", 6)
		 && !strcmp(json_string_value(id_val) + 6, pool->swork.job_id)
		 && json_is_array(res_val)) {
			// Check that the transactions actually hash to the merkle links
			{
				unsigned maxtx = 1 << pool->swork.merkles;
				unsigned mintx = maxtx >> 1;
				--maxtx;
				unsigned acttx = (unsigned)json_array_size(res_val);
				if (acttx < mintx || acttx > maxtx) {
					applog(LOG_WARNING, "Pool %u is sending mismatched block contents to us (%u is not %u-%u)",
					       pool->pool_no, acttx, mintx, maxtx);
					goto fishy;
				}
				// TODO: Check hashes match actual merkle links
			}

			if (pool->swork.opaque) {
				pool->swork.opaque = false;
				applog(LOG_NOTICE, "Pool %u now providing block contents to us",
				       pool->pool_no);
			}
			pool->swork.transparency_time = (time_t)-1;

fishy:
			ret = true;
		}

		goto out;
	}

	id = json_integer_value(id_val);

	mutex_lock(&sshare_lock);
	HASH_FIND_INT(stratum_shares, &id, sshare);
	if (sshare)
		HASH_DEL(stratum_shares, sshare);
	mutex_unlock(&sshare_lock);

	if (!sshare) {
		double pool_diff;

		/* Since the share is untracked, we can only guess at what the
		 * work difficulty is based on the current pool diff. */
		cg_rlock(&pool->data_lock);
		pool_diff = pool->swork.diff;
		cg_runlock(&pool->data_lock);

		if (json_is_true(res_val)) {
			applog(LOG_NOTICE, "Accepted untracked stratum share from pool %d", pool->pool_no);

			/* We don't know what device this came from so we can't
			 * attribute the work to the relevant cgpu */
			mutex_lock(&stats_lock);
			total_accepted++;
			pool->accepted++;
			total_diff_accepted += pool_diff;
			pool->diff_accepted += pool_diff;
			mutex_unlock(&stats_lock);
		} else {
			applog(LOG_NOTICE, "Rejected untracked stratum share from pool %d", pool->pool_no);

			mutex_lock(&stats_lock);
			total_rejected++;
			pool->rejected++;
			total_diff_rejected += pool_diff;
			pool->diff_rejected += pool_diff;
			mutex_unlock(&stats_lock);
		}
		goto out;
	}
	else {
		mutex_lock(&submitting_lock);
		--total_submitting;
		mutex_unlock(&submitting_lock);
	}
	stratum_share_result(val, res_val, err_val, sshare);
	free_work(sshare->work);
	free(sshare);

	ret = true;
out:
	if (val)
		json_decref(val);

	return ret;
}

static void shutdown_stratum(struct pool *pool)
{
	// Shut down Stratum as if we never had it
	pool->stratum_active = false;
	pool->stratum_init = false;
	pool->has_stratum = false;
	shutdown(pool->sock, SHUT_RDWR);
	free(pool->stratum_url);
	if (pool->sockaddr_url == pool->stratum_url)
		pool->sockaddr_url = NULL;
	pool->stratum_url = NULL;
}

void clear_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	struct work *work;
	struct cgpu_info *cgpu;
	double diff_cleared = 0;
	int cleared = 0;

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work->pool == pool) {
			HASH_DEL(stratum_shares, sshare);
			
			work = sshare->work;
			sharelog("disconnect", work);
			
			cgpu = get_thr_cgpu(work->thr_id);
			++cgpu->stale;
			diff_cleared += sshare->work->work_difficulty;
			free_work(sshare->work);
			free(sshare);
			cleared++;
		}
	}
	mutex_unlock(&sshare_lock);

	if (cleared) {
		applog(LOG_WARNING, "Lost %d shares due to stratum disconnect on pool %d", cleared, pool->pool_no);
		mutex_lock(&stats_lock);
		pool->stale_shares += cleared;
		total_stale += cleared;
		pool->diff_stale += diff_cleared;
		total_diff_stale += diff_cleared;
		mutex_unlock(&stats_lock);

		mutex_lock(&submitting_lock);
		total_submitting -= cleared;
		mutex_unlock(&submitting_lock);
	}
}

static void resubmit_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	struct work *work;
	unsigned resubmitted = 0;

	mutex_lock(&sshare_lock);
	mutex_lock(&submitting_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work->pool != pool)
			continue;
		
		HASH_DEL(stratum_shares, sshare);
		
		work = sshare->work;
		DL_APPEND(submit_waiting, work);
		
		free(sshare);
		++resubmitted;
	}
	mutex_unlock(&submitting_lock);
	mutex_unlock(&sshare_lock);

	if (resubmitted) {
		notifier_wake(submit_waiting_notifier);
		applog(LOG_DEBUG, "Resubmitting %u shares due to stratum disconnect on pool %u", resubmitted, pool->pool_no);
	}
}

static void clear_pool_work(struct pool *pool)
{
	struct work *work, *tmp;
	int cleared = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (work->pool == pool) {
			HASH_DEL(staged_work, work);
			free_work(work);
			cleared++;
		}
	}
	mutex_unlock(stgd_lock);
}

static int cp_prio(void)
{
	int prio;

	cg_rlock(&control_lock);
	prio = currentpool->prio;
	cg_runlock(&control_lock);

	return prio;
}

/* We only need to maintain a secondary pool connection when we need the
 * capacity to get work from the backup pools while still on the primary */
static bool cnx_needed(struct pool *pool)
{
	struct pool *cp;

	/* Balance strategies need all pools online */
	if (pool_strategy == POOL_BALANCE)
		return true;
	if (pool_strategy == POOL_LOADBALANCE)
		return true;

	/* Idle stratum pool needs something to kick it alive again */
	if (pool->has_stratum && pool->idle)
		return true;

	/* Getwork pools without opt_fail_only need backup pools up to be able
	 * to leak shares */
	cp = current_pool();
	if (cp == pool)
		return true;
	if (!cp->has_stratum && (!opt_fail_only || !cp->hdr_path))
		return true;

	/* Keep the connection open to allow any stray shares to be submitted
	 * on switching pools for 2 minutes. */
	if (difftime(time(NULL), pool->last_work_time) < 120)
		return true;

	/* If the pool has only just come to life and is higher priority than
	 * the current pool keep the connection open so we can fail back to
	 * it. */
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		return true;

	if (pool_unworkable(cp))
		return true;

	return false;
}

static void wait_lpcurrent(struct pool *pool);
static void pool_resus(struct pool *pool);
static void gen_stratum_work(struct pool *pool, struct work *work);

static void stratum_resumed(struct pool *pool)
{
	if (!pool->stratum_notify)
		return;
	if (pool_tclear(pool, &pool->idle)) {
		applog(LOG_INFO, "Stratum connection to pool %d resumed", pool->pool_no);
		pool_resus(pool);
	}
}

static bool supports_resume(struct pool *pool)
{
	bool ret;

	cg_rlock(&pool->data_lock);
	ret = (pool->sessionid != NULL);
	cg_runlock(&pool->data_lock);

	return ret;
}

/* One stratum thread per pool that has stratum waits on the socket checking
 * for new messages and for the integrity of the socket connection. We reset
 * the connection based on the integrity of the receive side only as the send
 * side will eventually expire data it fails to send. */
static void *stratum_thread(void *userdata)
{
	struct pool *pool = (struct pool *)userdata;

	pthread_detach(pthread_self());

	char threadname[20];
	snprintf(threadname, 20, "stratum%u", pool->pool_no);
	RenameThread(threadname);

	srand(time(NULL) + (intptr_t)userdata);

	while (42) {
		struct timeval timeout;
		int sel_ret;
		fd_set rd;
		char *s;

		if (unlikely(!pool->has_stratum))
			break;

		/* Check to see whether we need to maintain this connection
		 * indefinitely or just bring it up when we switch to this
		 * pool */
		if (!sock_full(pool) && !cnx_needed(pool)) {
			suspend_stratum(pool);
			clear_stratum_shares(pool);
			clear_pool_work(pool);

			wait_lpcurrent(pool);
			if (!restart_stratum(pool)) {
				pool_died(pool);
				while (!restart_stratum(pool)) {
					if (pool->removed)
						goto out;
					nmsleep(30000);
				}
			}
		}

		FD_ZERO(&rd);
		FD_SET(pool->sock, &rd);
		timeout.tv_sec = 120;
		timeout.tv_usec = 0;

		/* If we fail to receive any notify messages for 2 minutes we
		 * assume the connection has been dropped and treat this pool
		 * as dead */
		if (!sock_full(pool) && (sel_ret = select(pool->sock + 1, &rd, NULL, NULL, &timeout)) < 1) {
			applog(LOG_DEBUG, "Stratum select failed on pool %d with value %d", pool->pool_no, sel_ret);
			s = NULL;
		} else
			s = recv_line(pool);
		if (!s) {
			if (!pool->has_stratum)
				break;

			applog(LOG_NOTICE, "Stratum connection to pool %d interrupted", pool->pool_no);
			pool->getfail_occasions++;
			total_go++;

			pool->sock = INVSOCK;

			/* If the socket to our stratum pool disconnects, all
			 * submissions need to be discarded or resent. */
			if (!supports_resume(pool))
				clear_stratum_shares(pool);
			else
				resubmit_stratum_shares(pool);
			clear_pool_work(pool);
			if (pool == current_pool())
				restart_threads();

			if (restart_stratum(pool))
				continue;

			shutdown_stratum(pool);
			pool_died(pool);
			break;
		}

		/* Check this pool hasn't died while being a backup pool and
		 * has not had its idle flag cleared */
		stratum_resumed(pool);

		if (!parse_method(pool, s) && !parse_stratum_response(pool, s))
			applog(LOG_INFO, "Unknown stratum msg: %s", s);
		free(s);
		if (pool->swork.clean) {
			struct work *work = make_work();

			/* Generate a single work item to update the current
			 * block database */
			pool->swork.clean = false;
			gen_stratum_work(pool, work);

			/* Try to extract block height from coinbase scriptSig */
			char *hex_height = &pool->swork.coinbase1[8 /*version*/ + 2 /*txin count*/ + 72 /*prevout*/ + 2 /*scriptSig len*/ + 2 /*push opcode*/];
			unsigned char cb_height_sz;
			hex2bin(&cb_height_sz, &hex_height[-2], 1);
			if (cb_height_sz == 3) {
				// FIXME: The block number will overflow this by AD 2173
				uint32_t block_id = ((uint32_t*)work->data)[1];
				uint32_t height = 0;
				hex2bin((unsigned char*)&height, hex_height, 3);
				height = le32toh(height);
				have_block_height(block_id, height);
			}

			++pool->work_restart_id;
			if (test_work_current(work)) {
				/* Only accept a work update if this stratum
				 * connection is from the current pool */
				if (pool == current_pool()) {
					restart_threads();
					applog(
					       (opt_quiet_work_updates ? LOG_DEBUG : LOG_NOTICE),
					       "Stratum from pool %d requested work update", pool->pool_no);
				}
			} else
				applog(LOG_NOTICE, "Stratum from pool %d detected new block", pool->pool_no);
			free_work(work);
		}

		if (pool->swork.transparency_time != (time_t)-1 && difftime(time(NULL), pool->swork.transparency_time) > 21.09375) {
			// More than 4 timmills past since requested transactions
			pool->swork.transparency_time = (time_t)-1;
			pool->swork.opaque = true;
			applog(LOG_WARNING, "Pool %u is hiding block contents from us",
			       pool->pool_no);
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
	char *rpc_req;
	struct work *work;
	enum pool_protocol proto;

		applog(LOG_INFO, "Testing pool %s", pool->rpc_url);

	/* This is the central point we activate stratum when we can */
	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return false;
	}

	if (!(want_gbt || want_getwork))
		goto nohttp;

	work = make_work();

	/* Probe for GBT support on first pass */
	proto = want_gbt ? PLP_GETBLOCKTEMPLATE : PLP_GETWORK;

tryagain:
	rpc_req = prepare_rpc_req_probe(work, proto, NULL);
	work->pool = pool;
	if (!rpc_req)
		goto out;

	pool->probed = false;
	cgtime(&tv_getwork);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, rpc_req,
			true, false, &rolltime, pool, false);
	cgtime(&tv_getwork_reply);

	free(rpc_req);

	/* Detect if a http getwork pool has an X-Stratum header at startup,
	 * and if so, switch to that in preference to getwork if it works */
	if (pool->stratum_url && want_stratum && (pool->has_stratum || stratum_works(pool))) {
		if (!pool->has_stratum) {

		applog(LOG_NOTICE, "Switching pool %d %s to %s", pool->pool_no, pool->rpc_url, pool->stratum_url);
		if (!pool->rpc_url)
			pool->rpc_url = strdup(pool->stratum_url);
		pool->has_stratum = true;

		}

		free_work(work);
		if (val)
			json_decref(val);

retry_stratum:
		curl_easy_cleanup(curl);
		
		/* We create the stratum thread for each pool just after
		 * successful authorisation. Once the init flag has been set
		 * we never unset it and the stratum thread is responsible for
		 * setting/unsetting the active flag */
		bool init = pool_tset(pool, &pool->stratum_init);

		if (!init) {
			bool ret = initiate_stratum(pool) && auth_stratum(pool);

			if (ret)
			{
				detect_algo = 2;
				init_stratum_thread(pool);
			}
			else
				pool_tclear(pool, &pool->stratum_init);
			return ret;
		}
		return pool->stratum_active;
	}
	else if (pool->has_stratum)
		shutdown_stratum(pool);

	if (val) {
		bool rc;
		json_t *res;

		res = json_object_get(val, "result");
		if ((!json_is_object(res)) || (proto == PLP_GETBLOCKTEMPLATE && !json_object_get(res, "bits")))
			goto badwork;

		work->rolltime = rolltime;
		rc = work_decode(pool, work, val);
		if (rc) {
			applog(LOG_DEBUG, "Successfully retrieved and deciphered work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			work->pool = pool;
			copy_time(&work->tv_getwork, &tv_getwork);
			copy_time(&work->tv_getwork_reply, &tv_getwork_reply);
			work->getwork_mode = GETWORK_MODE_TESTPOOL;
			calc_diff(work, 0);

			update_last_work(work);

			applog(LOG_DEBUG, "Pushing pooltest work to base pool");

			tq_push(control_thr[stage_thr_id].q, work);
			total_getworks++;
			pool->getwork_requested++;
			ret = true;
			cgtime(&pool->tv_idle);
		} else {
badwork:
			json_decref(val);
			applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			pool->proto = proto = pool_protocol_fallback(proto);
			if (PLP_NONE != proto)
				goto tryagain;
			free_work(work);
			goto out;
		}
		json_decref(val);

		if (proto != pool->proto) {
			pool->proto = proto;
			applog(LOG_INFO, "Selected %s protocol for pool %u", pool_protocol_name(proto), pool->pool_no);
		}

		if (pool->lp_url)
			goto out;

		/* Decipher the longpoll URL, if any, and store it in ->lp_url */

		const struct blktmpl_longpoll_req *lp;
		if (work->tmpl && (lp = blktmpl_get_longpoll(work->tmpl))) {
			// NOTE: work_decode takes care of lp id
			pool->lp_url = lp->uri ? absolute_uri(lp->uri, pool->rpc_url) : pool->rpc_url;
			if (!pool->lp_url)
			{
				ret = false;
				goto out;
			}
			pool->lp_proto = PLP_GETBLOCKTEMPLATE;
		}
		else
		if (pool->hdr_path && want_getwork) {
			pool->lp_url = absolute_uri(pool->hdr_path, pool->rpc_url);
			if (!pool->lp_url)
			{
				ret = false;
				goto out;
			}
			pool->lp_proto = PLP_GETWORK;
		} else
			pool->lp_url = NULL;

		if (want_longpoll && !pool->lp_started) {
			pool->lp_started = true;
			if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
				quit(1, "Failed to create pool longpoll thread");
		}
	} else if (PLP_NONE != (proto = pool_protocol_fallback(proto))) {
		pool->proto = proto;
		goto tryagain;
	} else {
		free_work(work);
nohttp:
		/* If we failed to parse a getwork, this could be a stratum
		 * url without the prefix stratum+tcp:// so let's check it */
		if (extract_sockaddr(pool, pool->rpc_url) && initiate_stratum(pool)) {
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

static void pool_resus(struct pool *pool)
{
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio()) {
		applog(LOG_WARNING, "Pool %d %s alive", pool->pool_no, pool->rpc_url);
		switch_pools(NULL);
	} else
		applog(LOG_INFO, "Pool %d %s alive", pool->pool_no, pool->rpc_url);
}

static struct work *hash_pop(void)
{
	struct work *work = NULL, *tmp;
	int hc;

retry:
	mutex_lock(stgd_lock);
	while (!getq->frozen && !HASH_COUNT(staged_work))
		pthread_cond_wait(&getq->cond, stgd_lock);

	hc = HASH_COUNT(staged_work);
	/* Find clone work if possible, to allow masters to be reused */
	if (hc > staged_rollable) {
		HASH_ITER(hh, staged_work, work, tmp) {
			if (!work_rollable(work))
				break;
		}
	} else
		work = staged_work;
	
	if (can_roll(work) && should_roll(work))
	{
		// Instead of consuming it, force it to be cloned and grab the clone
		mutex_unlock(stgd_lock);
		clone_available();
		goto retry;
	}
	
	HASH_DEL(staged_work, work);
	if (work_rollable(work))
		staged_rollable--;

	/* Signal the getwork scheduler to look for more work */
	pthread_cond_signal(&gws_cond);

	/* Signal hash_pop again in case there are mutliple hash_pop waiters */
	pthread_cond_signal(&getq->cond);
	mutex_unlock(stgd_lock);
	work->pool->last_work_time = time(NULL);

	return work;
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
		stage_work(work_clone);
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

void gen_hash(unsigned char *data, unsigned char *hash, int len)
{
	unsigned char hash1[32];

	sha2(data, len, hash1);
	sha2(hash1, 32, hash);
}

/* Diff 1 is a 256 bit unsigned integer of
 * 0x00000000ffff0000000000000000000000000000000000000000000000000000
 * so we use a big endian 64 bit unsigned integer centred on the 5th byte to
 * cover a huge range of difficulty targets, though not all 256 bits' worth */
static void bdiff_target_leadzero(unsigned char *target, double diff)
{
	uint64_t *data64, h64;
	double d64;

	d64 = diffone;
	d64 /= diff;
	d64 = ceil(d64);
	h64 = d64;

	memset(target, 0, 32);
	if (d64 < 18446744073709551616.0) {
		unsigned char *rtarget = target;
		memset(rtarget, 0, 32);
		if (opt_scrypt)
			data64 = (uint64_t *)(rtarget + 2);
		else
			data64 = (uint64_t *)(rtarget + 4);
		*data64 = htobe64(h64);
	} else {
		/* Support for the classic all FFs just-below-1 diff */
		if (opt_scrypt)
			memset(&target[2], 0xff, 30);
		else
			memset(&target[4], 0xff, 28);
	}
}

void set_target(unsigned char *dest_target, double diff)
{
	unsigned char rtarget[32];
	bdiff_target_leadzero(rtarget, diff);
	swab256(dest_target, rtarget);
	
	if (opt_debug) {
		char *htarget = bin2hex(rtarget, 32);

		applog(LOG_DEBUG, "Generated target %s", htarget);
		free(htarget);
	}
}

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */
static void gen_stratum_work(struct pool *pool, struct work *work)
{
	unsigned char *coinbase, merkle_root[32], merkle_sha[64];
	char *header, *merkle_hash;
	uint32_t *data32, *swap32;
	size_t alloc_len;
	int i;

	clean_work(work);

	/* Use intermediate lock to update the one pool variable */
	cg_ilock(&pool->data_lock);

	/* Generate coinbase */
	work->nonce2 = bin2hex((const unsigned char *)&pool->nonce2, pool->n2size);
	pool->nonce2++;

	/* Downgrade to a read lock to read off the pool variables */
	cg_dlock(&pool->data_lock);
	alloc_len = pool->swork.cb_len;
	align_len(&alloc_len);
	coinbase = calloc(alloc_len, 1);
	if (unlikely(!coinbase))
		quit(1, "Failed to calloc coinbase in gen_stratum_work");
	hex2bin(coinbase, pool->swork.coinbase1, pool->swork.cb1_len);
	hex2bin(coinbase + pool->swork.cb1_len, pool->nonce1, pool->n1_len);
	hex2bin(coinbase + pool->swork.cb1_len + pool->n1_len, work->nonce2, pool->n2size);
	hex2bin(coinbase + pool->swork.cb1_len + pool->n1_len + pool->n2size, pool->swork.coinbase2, pool->swork.cb2_len);

	/* Generate merkle root */
	gen_hash(coinbase, merkle_root, pool->swork.cb_len);
	free(coinbase);
	memcpy(merkle_sha, merkle_root, 32);
	for (i = 0; i < pool->swork.merkles; i++) {
		unsigned char merkle_bin[32];

		hex2bin(merkle_bin, pool->swork.merkle[i], 32);
		memcpy(merkle_sha + 32, merkle_bin, 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
	data32 = (uint32_t *)merkle_sha;
	swap32 = (uint32_t *)merkle_root;
	flip32(swap32, data32);
	merkle_hash = bin2hex((const unsigned char *)merkle_root, 32);

	header = calloc(pool->swork.header_len, 1);
	if (unlikely(!header))
		quit(1, "Failed to calloc header in gen_stratum_work");
	sprintf(header, "%s%s%s%s%s%s%s",
		pool->swork.bbversion,
		pool->swork.prev_hash,
		merkle_hash,
		pool->swork.ntime,
		pool->swork.nbit,
		"00000000", /* nonce */
		workpadding);

	/* Store the stratum work diff to check it still matches the pool's
	 * stratum diff when submitting shares */
	work->sdiff = pool->swork.diff;

	/* Copy parameters required for share submission */
	work->job_id = strdup(pool->swork.job_id);
	work->nonce1 = strdup(pool->nonce1);
	work->ntime = strdup(pool->swork.ntime);
	cg_runlock(&pool->data_lock);

	applog(LOG_DEBUG, "Generated stratum merkle %s", merkle_hash);
	applog(LOG_DEBUG, "Generated stratum header %s", header);
	applog(LOG_DEBUG, "Work job_id %s nonce2 %s ntime %s", work->job_id, work->nonce2, work->ntime);

	free(merkle_hash);

	/* Convert hex data to binary data for work */
	if (unlikely(!hex2bin(work->data, header, 128)))
		quit(1, "Failed to convert header to data in gen_stratum_work");
	free(header);
	calc_midstate(work);

	set_target(work->target, work->sdiff);

	local_work++;
	work->pool = pool;
	work->stratum = true;
	work->blk.nonce = 0;
	work->id = total_work++;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_STRATUM;
	work->work_restart_id = work->pool->work_restart_id;
	calc_diff(work, 0);

	cgtime(&work->tv_staged);
}

void request_work(struct thr_info *thr)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct cgminer_stats *dev_stats = &(cgpu->cgminer_stats);

	/* Tell the watchdog thread this thread is waiting on getwork and
	 * should not be restarted */
	thread_reportout(thr);
	
	// HACK: Since get_work still blocks, reportout all processors dependent on this thread
	for (struct cgpu_info *proc = thr->cgpu->next_proc; proc; proc = proc->next_proc)
	{
		if (proc->threads)
			break;
		thread_reportout(proc->thr[0]);
	}

	cgtime(&dev_stats->_get_start);
}

// FIXME: Make this non-blocking (and remove HACK above)
struct work *get_work(struct thr_info *thr)
{
	const int thr_id = thr->id;
	struct cgpu_info *cgpu = thr->cgpu;
	struct cgminer_stats *dev_stats = &(cgpu->cgminer_stats);
	struct cgminer_stats *pool_stats;
	struct timeval tv_get;
	struct work *work = NULL;

	applog(LOG_DEBUG, "%"PRIpreprv": Popping work from get queue to get work", cgpu->proc_repr);
	while (!work) {
		work = hash_pop();
		if (stale_work(work, false)) {
			discard_work(work);
			work = NULL;
			wake_gws();
		}
	}
	applog(LOG_DEBUG, "%"PRIpreprv": Got work from get queue to get work for thread %d", cgpu->proc_repr, thr_id);

	work->thr_id = thr_id;
	thread_reportin(thr);
	
	// HACK: Since get_work still blocks, reportin all processors dependent on this thread
	for (struct cgpu_info *proc = thr->cgpu->next_proc; proc; proc = proc->next_proc)
	{
		if (proc->threads)
			break;
		thread_reportin(proc->thr[0]);
	}
	
	work->mined = true;
	work->blk.nonce = 0;

	cgtime(&tv_get);
	timersub(&tv_get, &dev_stats->_get_start, &tv_get);

	timeradd(&tv_get, &dev_stats->getwork_wait, &dev_stats->getwork_wait);
	if (timercmp(&tv_get, &dev_stats->getwork_wait_max, >))
		dev_stats->getwork_wait_max = tv_get;
	if (timercmp(&tv_get, &dev_stats->getwork_wait_min, <))
		dev_stats->getwork_wait_min = tv_get;
	++dev_stats->getwork_calls;

	pool_stats = &(work->pool->cgminer_stats);
	timeradd(&tv_get, &pool_stats->getwork_wait, &pool_stats->getwork_wait);
	if (timercmp(&tv_get, &pool_stats->getwork_wait_max, >))
		pool_stats->getwork_wait_max = tv_get;
	if (timercmp(&tv_get, &pool_stats->getwork_wait_min, <))
		pool_stats->getwork_wait_min = tv_get;
	++pool_stats->getwork_calls;

	return work;
}

static
void _submit_work_async(struct work *work)
{
	applog(LOG_DEBUG, "Pushing submit work to work thread");

	mutex_lock(&submitting_lock);
	++total_submitting;
	DL_APPEND(submit_waiting, work);
	mutex_unlock(&submitting_lock);

	notifier_wake(submit_waiting_notifier);
}

static void submit_work_async(struct work *work_in, struct timeval *tv_work_found)
{
	struct work *work = copy_work(work_in);

	if (tv_work_found)
		copy_time(&work->tv_work_found, tv_work_found);
	
	_submit_work_async(work);
}

void inc_hw_errors(struct thr_info *thr)
{
	mutex_lock(&stats_lock);
	hw_errors++;
	thr->cgpu->hw_errors++;
	mutex_unlock(&stats_lock);

	if (thr->cgpu->drv->hw_error)
		thr->cgpu->drv->hw_error(thr);
}

enum test_nonce2_result hashtest2(struct work *work, bool checktarget)
{
	uint32_t *hash2_32 = (uint32_t *)&work->hash[0];

	hash_data(work->hash, work->data);

	if (hash2_32[7] != 0)
		return TNR_BAD;

	if (!checktarget)
		return TNR_GOOD;

	if (!hash_target_check_v(work->hash, work->target))
		return TNR_HIGH;

	return TNR_GOOD;
}

enum test_nonce2_result _test_nonce2(struct work *work, uint32_t nonce, bool checktarget)
{
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);
	*work_nonce = htole32(nonce);

#ifdef USE_SCRYPT
	if (opt_scrypt)
		// NOTE: Depends on scrypt_test return matching enum values
		return scrypt_test(work->data, work->target, nonce);
#endif

	return hashtest2(work, checktarget);
}

/* Returns true if nonce for work was a valid share */
bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);
	uint32_t bak_nonce = *work_nonce;
	struct timeval tv_work_found;
	enum test_nonce2_result res;
	bool ret = true;

	thread_reportout(thr);

	cgtime(&tv_work_found);
	*work_nonce = htole32(nonce);

	mutex_lock(&stats_lock);
	total_diff1++;
	thr->cgpu->diff1++;
	work->pool->diff1++;
	mutex_unlock(&stats_lock);

	/* Do one last check before attempting to submit the work */
	/* Side effect: sets work->data for us */
	res = test_nonce2(work, nonce);
	
	if (unlikely(res == TNR_BAD))
		{
			struct cgpu_info *cgpu = thr->cgpu;
			applog(LOG_WARNING, "%"PRIpreprv": invalid nonce - HW error",
			       cgpu->proc_repr);
			inc_hw_errors(thr);
			ret = false;
			goto out;
		}
	
	mutex_lock(&stats_lock);
	thr->cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);
	
	if (res == TNR_HIGH)
	{
			// Share above target, normal
			/* Check the diff of the share, even if it didn't reach the
			 * target, just to set the best share value if it's higher. */
			share_diff(work);
			goto out;
	}
	
	submit_work_async(work, &tv_work_found);
out:
	*work_nonce = bak_nonce;
	thread_reportin(thr);

	return ret;
}

bool abandon_work(struct work *work, struct timeval *wdiff, uint64_t hashes)
{
	if (wdiff->tv_sec > opt_scantime ||
	    work->blk.nonce >= MAXTHREADS - hashes ||
	    hashes >= 0xfffffffe ||
	    stale_work(work, false))
		return true;
	return false;
}

static
void __thr_being_msg(struct thr_info *thr, const char *being)
{
	struct cgpu_info *proc = thr->cgpu;
	
	if (proc->threads > 1)
		applog(LOG_WARNING, "%"PRIpreprv" (thread %d) being %s", proc->proc_repr, thr->id, being);
	else
		applog(LOG_WARNING, "%"PRIpreprv" being %s", proc->proc_repr, being);
}

void mt_disable_start(struct thr_info *mythr)
{
	hashmeter2(mythr);
	if (mythr->prev_work)
		free_work(mythr->prev_work);
	mythr->prev_work = mythr->work;
	mythr->work = NULL;
	mythr->_job_transition_in_progress = false;
	__thr_being_msg(mythr, "disabled");
	mythr->rolling = mythr->cgpu->rolling = 0;
	thread_reportout(mythr);
}

/* Create a hashtable of work items for devices with a queue. The device
 * driver must have a custom queue_full function or it will default to true
 * and put only one work item in the queue. Work items should not be removed
 * from this hashtable until they are no longer in use anywhere. Once a work
 * item is physically queued on the device itself, the work->queued flag
 * should be set under cgpu->qlock write lock to prevent it being dereferenced
 * while still in use. */
static void fill_queue(struct thr_info *mythr, struct cgpu_info *cgpu, struct device_drv *drv, const int thr_id)
{
	thread_reportout(mythr);
	do {
		bool need_work;

		rd_lock(&cgpu->qlock);
		need_work = (HASH_COUNT(cgpu->queued_work) == cgpu->queued_count);
		rd_unlock(&cgpu->qlock);

		if (need_work) {
			struct work *work = get_work(mythr);

			wr_lock(&cgpu->qlock);
			HASH_ADD_INT(cgpu->queued_work, id, work);
			wr_unlock(&cgpu->qlock);
		}
		/* The queue_full function should be used by the driver to
		 * actually place work items on the physical device if it
		 * does have a queue. */
	} while (drv->queue_full && !drv->queue_full(cgpu));
}

/* This function is for retrieving one work item from the queued hashtable of
 * available work items that are not yet physically on a device (which is
 * flagged with the work->queued bool). Code using this function must be able
 * to handle NULL as a return which implies there is no work available. */
struct work *get_queued(struct cgpu_info *cgpu)
{
	struct work *work, *tmp, *ret = NULL;

	wr_lock(&cgpu->qlock);
	HASH_ITER(hh, cgpu->queued_work, work, tmp) {
		if (!work->queued) {
			work->queued = true;
			cgpu->queued_count++;
			ret = work;
			break;
		}
	}
	wr_unlock(&cgpu->qlock);

	return ret;
}

/* This function is for finding an already queued work item in the
 * given que hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The calling function must lock access to the que if it is required.
 * The common values for midstatelen, offset, datalen are 32, 64, 12 */
struct work *__find_work_bymidstate(struct work *que, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work, *tmp, *ret = NULL;

	HASH_ITER(hh, que, work, tmp) {
		if (work->queued &&
		    memcmp(work->midstate, midstate, midstatelen) == 0 &&
		    memcmp(work->data + offset, data, datalen) == 0) {
			ret = work;
			break;
		}
	}

	return ret;
}

/* This function is for finding an already queued work item in the
 * device's queued_work hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The common values for midstatelen, offset, datalen are 32, 64, 12 */
struct work *find_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *ret;

	rd_lock(&cgpu->qlock);
	ret = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	rd_unlock(&cgpu->qlock);

	return ret;
}

/* This function should be used by queued device drivers when they're sure
 * the work struct is no longer in use. */
void work_completed(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	if (work->queued)
		cgpu->queued_count--;
	HASH_DEL(cgpu->queued_work, work);
	wr_unlock(&cgpu->qlock);

	free_work(work);
}

static void flush_queue(struct cgpu_info *cgpu)
{
	struct work *work, *tmp;
	int discarded = 0;

	wr_lock(&cgpu->qlock);
	HASH_ITER(hh, cgpu->queued_work, work, tmp) {
		/* Can only discard the work items if they're not physically
		 * queued on the device. */
		if (!work->queued) {
			HASH_DEL(cgpu->queued_work, work);
			discard_work(work);
			discarded++;
		}
	}
	wr_unlock(&cgpu->qlock);

	if (discarded)
		applog(LOG_DEBUG, "Discarded %d queued work items", discarded);
}

/* This version of hash work is for devices that are fast enough to always
 * perform a full nonce range and need a queue to maintain the device busy.
 * Work creation and destruction is not done from within this function
 * directly. */
void hash_queued_work(struct thr_info *mythr)
{
	const long cycle = opt_log_interval / 5 ? : 1;
	struct timeval tv_start = {0, 0}, tv_end;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	const int thr_id = mythr->id;
	int64_t hashes_done = 0;

	while (42) {
		struct timeval diff;
		int64_t hashes;

		mythr->work_restart = false;

		fill_queue(mythr, cgpu, drv, thr_id);

		thread_reportin(mythr);
		hashes = drv->scanwork(mythr);
		if (unlikely(hashes == -1 )) {
			applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
			cgpu->deven = DEV_DISABLED;
			dev_error(cgpu, REASON_THREAD_ZERO_HASH);
			mt_disable(mythr);
		}

		hashes_done += hashes;
		cgtime(&tv_end);
		timersub(&tv_end, &tv_start, &diff);
		if (diff.tv_sec >= cycle) {
			hashmeter(thr_id, &diff, hashes_done);
			hashes_done = 0;
			copy_time(&tv_start, &tv_end);
		}

		if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
			mt_disable(mythr);

		if (unlikely(mythr->work_restart)) {
			flush_queue(cgpu);
			if (drv->flush_work)
				drv->flush_work(cgpu);
		}
	}
}

void mt_disable_finish(struct thr_info *mythr)
{
	struct device_drv *drv = mythr->cgpu->drv;
	
	thread_reportin(mythr);
	__thr_being_msg(mythr, "re-enabled");
	if (drv->thread_enable)
		drv->thread_enable(mythr);
}

void mt_disable(struct thr_info *mythr)
{
	mt_disable_start(mythr);
	applog(LOG_DEBUG, "Waiting for wakeup notification in miner thread");
	do {
		notifier_read(mythr->notifier);
	} while (mythr->pause);
	mt_disable_finish(mythr);
}


enum {
	STAT_SLEEP_INTERVAL		= 1,
	STAT_CTR_INTERVAL		= 10000000,
	FAILURE_INTERVAL		= 30,
};

/* Stage another work item from the work returned in a longpoll */
static void convert_to_work(json_t *val, int rolltime, struct pool *pool, struct work *work, struct timeval *tv_lp, struct timeval *tv_lp_reply)
{
	bool rc;

	work->rolltime = rolltime;
	rc = work_decode(pool, work, val);
	if (unlikely(!rc)) {
		applog(LOG_ERR, "Could not convert longpoll data to work");
		free_work(work);
		return;
	}
	total_getworks++;
	pool->getwork_requested++;
	work->pool = pool;
	copy_time(&work->tv_getwork, tv_lp);
	copy_time(&work->tv_getwork_reply, tv_lp_reply);
	calc_diff(work, 0);

	if (pool->enabled == POOL_REJECTING)
		work->mandatory = true;

	work->longpoll = true;
	work->getwork_mode = GETWORK_MODE_LP;

	update_last_work(work);

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

	stage_work(work);
	applog(LOG_DEBUG, "Converted longpoll data to work");
}

/* If we want longpoll, enable it for the chosen default pool, or, if
 * the pool does not support longpoll, find the first one that does
 * and use its longpoll support */
static struct pool *select_longpoll_pool(struct pool *cp)
{
	int i;

	if (cp->lp_url)
		return cp;
	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->has_stratum || pool->lp_url)
			return pool;
	}
	return NULL;
}

/* This will make the longpoll thread wait till it's the current pool, or it
 * has been flagged as rejecting, before attempting to open any connections.
 */
static void wait_lpcurrent(struct pool *pool)
{
	if (cnx_needed(pool))
		return;

	while (pool != current_pool() && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		mutex_lock(&lp_lock);
		pthread_cond_wait(&lp_cond, &lp_lock);
		mutex_unlock(&lp_lock);
	}
}

static curl_socket_t save_curl_socket(void *vpool, __maybe_unused curlsocktype purpose, struct curl_sockaddr *addr) {
	struct pool *pool = vpool;
	curl_socket_t sock = socket(addr->family, addr->socktype, addr->protocol);
	pool->lp_socket = sock;
	return sock;
}

static void *longpoll_thread(void *userdata)
{
	struct pool *cp = (struct pool *)userdata;
	/* This *pool is the source of the actual longpoll, not the pool we've
	 * tied it to */
	struct timeval start, reply, end;
	struct pool *pool = NULL;
	char threadname[20];
	CURL *curl = NULL;
	int failures = 0;
	char *lp_url;
	int rolltime;

#ifndef HAVE_PTHREAD_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

	snprintf(threadname, 20, "longpoll%u", cp->pool_no);
	RenameThread(threadname);

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return NULL;
	}

retry_pool:
	pool = select_longpoll_pool(cp);
	if (!pool) {
		applog(LOG_WARNING, "No suitable long-poll found for %s", cp->rpc_url);
		while (!pool) {
			nmsleep(60000);
			pool = select_longpoll_pool(cp);
		}
	}

	if (pool->has_stratum) {
		applog(LOG_WARNING, "Block change for %s detection via %s stratum",
		       cp->rpc_url, pool->rpc_url);
		goto out;
	}

	/* Any longpoll from any pool is enough for this to be true */
	have_longpoll = true;

	wait_lpcurrent(cp);

	{
		lp_url = pool->lp_url;
		if (cp == pool)
			applog(LOG_WARNING, "Long-polling activated for %s (%s)", lp_url, pool_protocol_name(pool->lp_proto));
		else
			applog(LOG_WARNING, "Long-polling activated for %s via %s (%s)", cp->rpc_url, lp_url, pool_protocol_name(pool->lp_proto));
	}

	while (42) {
		json_t *val, *soval;

		struct work *work = make_work();
		char *lpreq;
		lpreq = prepare_rpc_req(work, pool->lp_proto, pool->lp_id);
		work->pool = pool;
		if (!lpreq)
		{
			free_work(work);
			goto lpfail;
		}

		wait_lpcurrent(cp);

		cgtime(&start);

		/* Longpoll connections can be persistent for a very long time
		 * and any number of issues could have come up in the meantime
		 * so always establish a fresh connection instead of relying on
		 * a persistent one. */
		curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, save_curl_socket);
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, pool);
		val = json_rpc_call(curl, lp_url, pool->rpc_userpass,
				    lpreq, false, true, &rolltime, pool, false);
		pool->lp_socket = CURL_SOCKET_BAD;

		cgtime(&reply);

		free(lpreq);

		if (likely(val)) {
			soval = json_object_get(json_object_get(val, "result"), "submitold");
			if (soval)
				pool->submit_old = json_is_true(soval);
			else
				pool->submit_old = false;
			convert_to_work(val, rolltime, pool, work, &start, &reply);
			failures = 0;
			json_decref(val);
		} else {
			/* Some pools regularly drop the longpoll request so
			 * only see this as longpoll failure if it happens
			 * immediately and just restart it the rest of the
			 * time. */
			cgtime(&end);
			free_work(work);
			if (end.tv_sec - start.tv_sec > 30)
				continue;
			if (failures == 1)
				applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s", lp_url);
lpfail:
			nmsleep(30000);
		}

		if (pool != cp) {
			pool = select_longpoll_pool(cp);
			if (pool->has_stratum) {
				applog(LOG_WARNING, "Block change for %s detection via %s stratum",
				       cp->rpc_url, pool->rpc_url);
				break;
			}
			if (unlikely(!pool))
				goto retry_pool;
		}

		if (unlikely(pool->removed))
			break;
	}

out:
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
	if (cgpu->drv->reinit_device)
		cgpu->drv->reinit_device(cgpu);
}

static struct timeval rotate_tv;

/* We reap curls if they are unused for over a minute */
static void reap_curl(struct pool *pool)
{
	struct curl_ent *ent, *iter;
	struct timeval now;
	int reaped = 0;

	cgtime(&now);

	mutex_lock(&pool->pool_lock);
	LL_FOREACH_SAFE(pool->curllist, ent, iter) {
		if (pool->curls < 2)
			break;
		if (now.tv_sec - ent->tv.tv_sec > 300) {
			reaped++;
			pool->curls--;
			LL_DELETE(pool->curllist, ent);
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

#ifndef HAVE_PTHREAD_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

	RenameThread("watchpool");

	while (42) {
		struct timeval now;
		int i;

		if (++intervals > 20)
			intervals = 0;
		cgtime(&now);

		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			if (!opt_benchmark)
				reap_curl(pool);

			/* Get a rolling utility per pool over 10 mins */
			if (intervals > 19) {
				int shares = pool->diff1 - pool->last_shares;

				pool->last_shares = pool->diff1;
				pool->utility = (pool->utility + (double)shares * 0.63) / 1.63;
				pool->shares = pool->utility;
			}

			if (pool->enabled == POOL_DISABLED)
				continue;

			/* Don't start testing any pools if the test threads
			 * from startup are still doing their first attempt. */
			if (unlikely(pool->testing)) {
				pthread_join(pool->test_thread, NULL);
				pool->testing = false;
			}

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 30) {
				cgtime(&pool->tv_idle);
				if (pool_active(pool, true) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
			}
		}

		if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
			cgtime(&rotate_tv);
			switch_pools(NULL);
		}

		nmsleep(30000);
			
	}
	return NULL;
}

void mt_enable(struct thr_info *thr)
{
	applog(LOG_DEBUG, "Waking up thread %d", thr->id);
	notifier_wake(thr->notifier);
}

void proc_enable(struct cgpu_info *cgpu)
{
	int j;

	cgpu->deven = DEV_ENABLED;
	for (j = cgpu->threads ?: 1; j--; )
		mt_enable(cgpu->thr[j]);
}

#define device_recovered(cgpu)  proc_enable(cgpu)

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
#define WATCHDOG_INTERVAL		2
#define WATCHDOG_SICK_TIME		60
#define WATCHDOG_DEAD_TIME		600
#define WATCHDOG_SICK_COUNT		(WATCHDOG_SICK_TIME/WATCHDOG_INTERVAL)
#define WATCHDOG_DEAD_COUNT		(WATCHDOG_DEAD_TIME/WATCHDOG_INTERVAL)

static void *watchdog_thread(void __maybe_unused *userdata)
{
	const unsigned int interval = WATCHDOG_INTERVAL;
	struct timeval zero_tv;

#ifndef HAVE_PTHREAD_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

	RenameThread("watchdog");

	memset(&zero_tv, 0, sizeof(struct timeval));
	cgtime(&rotate_tv);

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

		cgtime(&now);

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

			rd_lock(&mining_thr_lock);
			for (i = 0; i < mining_threads; i++)
				mining_thr[i]->pause = true;
			rd_unlock(&mining_thr_lock);
		} else if (sched_paused && should_run()) {
			applog(LOG_WARNING, "Restarting execution as per start time %02d:%02d scheduled",
				schedstart.tm.tm_hour, schedstart.tm.tm_min);
			if (schedstop.enable)
				applog(LOG_WARNING, "Will pause execution as scheduled at %02d:%02d",
					schedstop.tm.tm_hour, schedstop.tm.tm_min);
			sched_paused = false;

			for (i = 0; i < mining_threads; i++) {
				struct thr_info *thr;

				thr = get_thread(i);
				thr->pause = false;
			}
			
			for (i = 0; i < total_devices; ++i)
			{
				struct cgpu_info *cgpu = get_devices(i);
				
				/* Don't touch disabled devices */
				if (cgpu->deven == DEV_DISABLED)
					continue;
				proc_enable(cgpu);
			}
		}

		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = get_devices(i);
			struct thr_info *thr = cgpu->thr[0];
			enum dev_enable *denable;
			char *dev_str = cgpu->proc_repr;
			int gpu;

			if (cgpu->drv->get_stats)
			  cgpu->drv->get_stats(cgpu);

			gpu = cgpu->device_id;
			denable = &cgpu->deven;

#ifdef HAVE_ADL
			if (adl_active && cgpu->has_adl)
				gpu_autotune(gpu, denable);
			if (opt_debug && cgpu->has_adl) {
				int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
				float temp = 0, vddc = 0;

				if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune))
					applog(LOG_DEBUG, "%.1f C  F: %d%%(%dRPM)  E: %dMHz  M: %dMHz  V: %.3fV  A: %d%%  P: %d%%",
					temp, fanpercent, fanspeed, engineclock, memclock, vddc, activity, powertune);
			}
#endif
			
			/* Thread is disabled */
			if (*denable == DEV_DISABLED)
				continue;
			else
			if (*denable == DEV_RECOVER_ERR) {
				if (opt_restart && difftime(time(NULL), cgpu->device_last_not_well) > cgpu->reinit_backoff) {
					applog(LOG_NOTICE, "Attempting to reinitialize %s",
					       dev_str);
					if (cgpu->reinit_backoff < 300)
						cgpu->reinit_backoff *= 2;
					device_recovered(cgpu);
				}
				continue;
			}
			else
			if (*denable == DEV_RECOVER) {
				if (opt_restart && cgpu->temp < cgpu->targettemp) {
					applog(LOG_NOTICE, "%s recovered to temperature below target, re-enabling",
					       dev_str);
					device_recovered(cgpu);
				}
				cgpu->device_last_not_well = time(NULL);
				cgpu->device_not_well_reason = REASON_DEV_THERMAL_CUTOFF;
				continue;
			}
			else
			if (cgpu->temp > cgpu->cutofftemp)
			{
				applog(LOG_WARNING, "%s hit thermal cutoff limit, disabling!",
				       dev_str);
				*denable = DEV_RECOVER;

				dev_error(cgpu, REASON_DEV_THERMAL_CUTOFF);
			}

			if (thr->getwork) {
				if (cgpu->status == LIFE_WELL && thr->getwork < now.tv_sec - opt_log_interval) {
					int thrid;
					bool cgpu_idle = true;
					thr->rolling = 0;
					for (thrid = 0; thrid < cgpu->threads; ++thrid)
						if (!cgpu->thr[thrid]->getwork)
							cgpu_idle = false;
					if (cgpu_idle) {
						cgpu->rolling = 0;
						cgpu->status = LIFE_WAIT;
					}
				}
				continue;
			}
			else if (cgpu->status == LIFE_WAIT)
				cgpu->status = LIFE_WELL;

#ifdef WANT_CPUMINE
			if (!strcmp(cgpu->drv->dname, "cpu"))
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
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_SICK_IDLE_60);
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
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_DEAD_IDLE_600);
			} else if (now.tv_sec - thr->sick.tv_sec > 60 &&
				   (cgpu->status == LIFE_SICK || cgpu->status == LIFE_DEAD)) {
				/* Attempt to restart a GPU that's sick or dead once every minute */
				cgtime(&thr->sick);
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

void print_summary(void)
{
	struct timeval diff;
	int hours, mins, secs, i;
	double utility, efficiency = 0.0;

	timersub(&total_tv_end, &total_tv_start, &diff);
	hours = diff.tv_sec / 3600;
	mins = (diff.tv_sec % 3600) / 60;
	secs = diff.tv_sec % 60;

	utility = total_accepted / total_secs * 60;
	efficiency = total_bytes_xfer ? total_diff_accepted * 2048. / total_bytes_xfer : 0.0;

	applog(LOG_WARNING, "\nSummary of runtime statistics:\n");
	applog(LOG_WARNING, "Started at %s", datestamp);
	if (total_pools == 1)
		applog(LOG_WARNING, "Pool: %s", pools[0]->rpc_url);
#ifdef WANT_CPUMINE
	if (opt_n_threads)
		applog(LOG_WARNING, "CPU hasher algorithm used: %s", algo_names[opt_algo]);
#endif
	applog(LOG_WARNING, "Runtime: %d hrs : %d mins : %d secs", hours, mins, secs);
	applog(LOG_WARNING, "Average hashrate: %.1f Megahash/s", total_mhashes_done / total_secs);
	applog(LOG_WARNING, "Solved blocks: %d", found_blocks);
	applog(LOG_WARNING, "Best share difficulty: %s", best_share);
	applog(LOG_WARNING, "Share submissions: %d", total_accepted + total_rejected);
	applog(LOG_WARNING, "Accepted shares: %d", total_accepted);
	applog(LOG_WARNING, "Rejected shares: %d + %d stale (%.2f%%)",
	       total_rejected, total_stale,
	       (float)(total_rejected + total_stale) / (float)(total_rejected + total_stale + total_accepted)
	);
	applog(LOG_WARNING, "Accepted difficulty shares: %1.f", total_diff_accepted);
	applog(LOG_WARNING, "Rejected difficulty shares: %1.f", total_diff_rejected);
	applog(LOG_WARNING, "Hardware errors: %d", hw_errors);
	applog(LOG_WARNING, "Efficiency (accepted shares * difficulty / 2 KB): %.2f", efficiency);
	applog(LOG_WARNING, "Utility (accepted shares / min): %.2f/min\n", utility);

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
			applog(LOG_WARNING, " Share submissions: %d", pool->accepted + pool->rejected);
			applog(LOG_WARNING, " Accepted shares: %d", pool->accepted);
			applog(LOG_WARNING, " Rejected shares: %d + %d stale (%.2f%%)",
			       pool->rejected, pool->stale_shares,
			       (float)(pool->rejected + pool->stale_shares) / (float)(pool->rejected + pool->stale_shares + pool->accepted)
			);
			applog(LOG_WARNING, " Accepted difficulty shares: %1.f", pool->diff_accepted);
			applog(LOG_WARNING, " Rejected difficulty shares: %1.f", pool->diff_rejected);
			uint64_t pool_bytes_xfer = pool->cgminer_pool_stats.net_bytes_received + pool->cgminer_pool_stats.net_bytes_sent;
			efficiency = pool_bytes_xfer ? pool->diff_accepted * 2048. / pool_bytes_xfer : 0.0;
			applog(LOG_WARNING, " Efficiency (accepted * difficulty / 2 KB): %.2f", efficiency);

			applog(LOG_WARNING, " Unable to get work from server occasions: %d", pool->getfail_occasions);
			applog(LOG_WARNING, " Submitting work remotely delay occasions: %d\n", pool->remotefail_occasions);
		}
	}

	applog(LOG_WARNING, "Summary of per device statistics:\n");
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = get_devices(i);

		if ((!cgpu->proc_id) && cgpu->next_proc)
		{
			// Device summary line
			opt_show_procs = false;
			log_print_status(cgpu);
			opt_show_procs = true;
		}
		log_print_status(cgpu);
	}

	if (opt_shares) {
		applog(LOG_WARNING, "Mined %d accepted shares of %d requested\n", total_accepted, opt_shares);
		if (opt_shares > total_accepted)
			applog(LOG_WARNING, "WARNING - Mined only %d shares of %d requested.", total_accepted, opt_shares);
	}
	applog(LOG_WARNING, " ");

	fflush(stderr);
	fflush(stdout);
}

static void clean_up(void)
{
#ifdef HAVE_OPENCL
	clear_adl(nDevs);
#endif
#ifdef HAVE_LIBUSB
        libusb_exit(NULL);
#endif

	cgtime(&total_tv_end);
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

	if (status) {
		const char *ev = getenv("__BFGMINER_SEGFAULT_ERRQUIT");
		if (unlikely(ev && ev[0] && ev[0] != '0')) {
			const char **p = NULL;
			// NOTE debugger can bypass with: p = &p
			*p = format;  // Segfault, hopefully dumping core
		}
	}

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

static bool pools_active = false;

static void *test_pool_thread(void *arg)
{
	struct pool *pool = (struct pool *)arg;

	if (pool_active(pool, false)) {
		pool_tset(pool, &pool->lagging);
		pool_tclear(pool, &pool->idle);
		bool first_pool = false;

		cg_wlock(&control_lock);
		if (!pools_active) {
			currentpool = pool;
			if (pool->pool_no != 0)
				first_pool = true;
			pools_active = true;
		}
		cg_wunlock(&control_lock);

		if (unlikely(first_pool))
			applog(LOG_NOTICE, "Switching to pool %d %s - first alive pool", pool->pool_no, pool->rpc_url);

		pool_resus(pool);
	} else
		pool_died(pool);

	return NULL;
}

/* Always returns true that the pool details were added unless we are not
 * live, implying this is the only pool being added, so if no pools are
 * active it returns false. */
bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass)
{
	pool->rpc_url = url;
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);

	pool->testing = true;
	pool->idle = true;
	enable_pool(pool);

	pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	if (!live) {
		pthread_join(pool->test_thread, NULL);
		pool->testing = false;
		return pools_active;
	}
	return true;
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

	if (!detect_stratum(pool, url) && strncmp(url, "http://", 7) &&
	    strncmp(url, "https://", 8)) {
		char *httpinput;

		httpinput = malloc(256);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		strcpy(httpinput, "http://");
		strncat(httpinput, url, 248);
		free(url);
		url = httpinput;
	}

	ret = add_pool_details(pool, live, url, user, pass);
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
	int x;
	__maybe_unused int y;

	lock_curses();
	if (curses_active) {
		unlock_curses();
		return;
	}

	mainwin = initscr();
	keypad(mainwin, true);
	getmaxyx(mainwin, y, x);
	statuswin = newwin(logstart, x, 0, 0);
	leaveok(statuswin, true);
	// For whatever reason, PDCurses crashes if the logwin is initialized to height y-logcursor
	// We resize the window later anyway, so just start it off at 1 :)
	logwin = newwin(1, 0, logcursor, 0);
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

/* TODO: fix need a dummy CPU device_drv even if no support for CPU mining */
#ifndef WANT_CPUMINE
struct device_drv cpu_drv;
struct device_drv cpu_drv = {
	.name = "CPU",
};
#endif

#ifdef USE_BITFORCE
extern struct device_drv bitforce_drv;
#endif

#ifdef USE_ICARUS
extern struct device_drv cairnsmore_drv;
extern struct device_drv icarus_drv;
#endif

#ifdef USE_AVALON
extern struct device_drv avalon_drv;
#endif

#ifdef USE_MODMINER
extern struct device_drv modminer_drv;
#endif

#ifdef USE_X6500
extern struct device_drv x6500_api;
#endif

#ifdef USE_ZTEX
extern struct device_drv ztex_drv;
#endif


static int cgminer_id_count = 0;
static int device_line_id_count;

void register_device(struct cgpu_info *cgpu)
{
	cgpu->deven = DEV_ENABLED;

	wr_lock(&devices_lock);
	devices[cgpu->cgminer_id = cgminer_id_count++] = cgpu;
	wr_unlock(&devices_lock);

	if (!cgpu->proc_id)
		cgpu->device_line_id = device_line_id_count++;
	mining_threads += cgpu->threads ?: 1;
#ifdef HAVE_CURSES
	adj_width(mining_threads, &dev_width);
#endif
#ifdef HAVE_OPENCL
	if (cgpu->drv == &opencl_api) {
		gpu_threads += cgpu->threads;
	}
#endif

	rwlock_init(&cgpu->qlock);
	cgpu->queued_work = NULL;
}

struct _cgpu_devid_counter {
	char name[4];
	int lastid;
	UT_hash_handle hh;
};

void renumber_cgpu(struct cgpu_info *cgpu)
{
	static struct _cgpu_devid_counter *devids = NULL;
	struct _cgpu_devid_counter *d;
	
	HASH_FIND_STR(devids, cgpu->drv->name, d);
	if (d)
		cgpu->device_id = ++d->lastid;
	else {
		d = malloc(sizeof(*d));
		memcpy(d->name, cgpu->drv->name, sizeof(d->name));
		cgpu->device_id = d->lastid = 0;
		HASH_ADD_STR(devids, name, d);
	}
}

static bool my_blkmaker_sha256_callback(void *digest, const void *buffer, size_t length)
{
	sha2(buffer, length, digest);
	return true;
}

#ifndef HAVE_PTHREAD_CANCEL
extern void setup_pthread_cancel_workaround();
extern struct sigaction pcwm_orig_term_handler;
#endif

static void probe_pools(void)
{
	int i;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->testing = true;
		pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	}
}

static void raise_fd_limits(void)
{
#ifdef HAVE_SETRLIMIT
	struct rlimit fdlimit;
	unsigned long old_soft_limit;
	
	if (getrlimit(RLIMIT_NOFILE, &fdlimit))
		applogr(, LOG_DEBUG, "setrlimit: Failed to getrlimit(RLIMIT_NOFILE)");
	
	if (fdlimit.rlim_cur == RLIM_INFINITY)
		applogr(, LOG_DEBUG, "setrlimit: Soft fd limit already infinite");
	
	if (fdlimit.rlim_cur == fdlimit.rlim_max)
		applogr(, LOG_DEBUG, "setrlimit: Soft fd limit already identical to hard limit (%lu)", (unsigned long)fdlimit.rlim_max);
	
	old_soft_limit = fdlimit.rlim_cur;
	fdlimit.rlim_cur = fdlimit.rlim_max;
	if (setrlimit(RLIMIT_NOFILE, &fdlimit))
		applogr(, LOG_DEBUG, "setrlimit: Failed to increase soft fd limit from %lu to hard limit of %lu", old_soft_limit, (unsigned long)fdlimit.rlim_max);
	
	applog(LOG_DEBUG, "setrlimit: Increased soft fd limit from %lu to hard limit of %lu", old_soft_limit, (unsigned long)fdlimit.rlim_max);
#else
	applog(LOG_DEBUG, "setrlimit: Not supported by platform");
#endif
}

int main(int argc, char *argv[])
{
	struct sigaction handler;
	struct thr_info *thr;
	struct block *block;
	unsigned int k;
	int i, j;
	char *s;

	blkmk_sha256_impl = my_blkmaker_sha256_callback;

#ifndef HAVE_PTHREAD_CANCEL
	setup_pthread_cancel_workaround();
#endif

	/* This dangerous functions tramples random dynamically allocated
	 * variables so do it before anything at all */
	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		quit(1, "Failed to curl_global_init");

	initial_args = malloc(sizeof(char *) * (argc + 1));
	for  (i = 0; i < argc; i++)
		initial_args[i] = strdup(argv[i]);
	initial_args[argc] = NULL;

#ifdef HAVE_LIBUSB
	int err = libusb_init(NULL);
	if (err) {
		fprintf(stderr, "libusb_init() failed err %d", err);
		fflush(stderr);
		quit(1, "libusb_init() failed");
	}
#endif

	mutex_init(&hash_lock);
	mutex_init(&console_lock);
	cglock_init(&control_lock);
	mutex_init(&stats_lock);
	mutex_init(&sharelog_lock);
	cglock_init(&ch_lock);
	mutex_init(&sshare_lock);
	rwlock_init(&blk_lock);
	rwlock_init(&netacc_lock);
	rwlock_init(&mining_thr_lock);
	rwlock_init(&devices_lock);

	mutex_init(&lp_lock);
	if (unlikely(pthread_cond_init(&lp_cond, NULL)))
		quit(1, "Failed to pthread_cond_init lp_cond");

	if (unlikely(pthread_cond_init(&gws_cond, NULL)))
		quit(1, "Failed to pthread_cond_init gws_cond");

	notifier_init(submit_waiting_notifier);

	sprintf(packagename, "%s %s", PACKAGE, VERSION);

#ifdef WANT_CPUMINE
	init_max_name_len();
#endif

	handler.sa_handler = &sighandler;
	handler.sa_flags = 0;
	sigemptyset(&handler.sa_mask);
#ifdef HAVE_PTHREAD_CANCEL
	sigaction(SIGTERM, &handler, &termhandler);
#else
	// Need to let pthread_cancel emulation handle SIGTERM first
	termhandler = pcwm_orig_term_handler;
	pcwm_orig_term_handler = handler;
#endif
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

	mutex_init(&submitting_lock);

#ifdef HAVE_OPENCL
	memset(gpus, 0, sizeof(gpus));
	for (i = 0; i < MAX_GPUDEVICES; i++)
		gpus[i].dynamic = true;
#endif

	schedstart.tm.tm_sec = 1;
	schedstop .tm.tm_sec = 1;

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

#ifndef HAVE_PTHREAD_CANCEL
	// Can't do this any earlier, or config isn't loaded
	applog(LOG_DEBUG, "pthread_cancel workaround in use");
#endif

	raise_fd_limits();
	
	if (opt_benchmark) {
		struct pool *pool;

		want_longpoll = false;
		pool = add_pool();
		pool->rpc_url = malloc(255);
		strcpy(pool->rpc_url, "Benchmark");
		pool->rpc_user = pool->rpc_url;
		pool->rpc_pass = pool->rpc_url;
		enable_pool(pool);
		pool->idle = false;
		successful_connect = true;
	}

#ifdef USE_X6500
	ft232r_scan();
#endif

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

				if (GetEnvironmentVariable("BFGMINER_SHARED_MEM", unique_name, 32) || GetEnvironmentVariable("CGMINER_SHARED_MEM", unique_name, 32)) {
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
		opencl_api.drv_detect();
	gpu_threads = 0;
#endif

#ifdef USE_ICARUS
	if (!opt_scrypt)
	{
		cairnsmore_drv.drv_detect();
		icarus_drv.drv_detect();
	}
#endif

#ifdef USE_AVALON
	if (!opt_scrypt)
		avalon_drv.drv_detect();
#endif

#ifdef USE_BITFORCE
	if (!opt_scrypt)
		bitforce_drv.drv_detect();
#endif

#ifdef USE_MODMINER
	if (!opt_scrypt)
		modminer_drv.drv_detect();
#endif

#ifdef USE_X6500
	if (!opt_scrypt)
		x6500_api.drv_detect();
#endif

#ifdef USE_ZTEX
	if (!opt_scrypt)
		ztex_drv.drv_detect();
#endif

#ifdef WANT_CPUMINE
	cpu_drv.drv_detect();
#endif

#ifdef USE_X6500
	ft232r_scan_free();
#endif

	for (i = 0; i < total_devices; ++i)
		if (!devices[i]->devtype)
			devices[i]->devtype = "PGA";

	if (devices_enabled == -1) {
		applog(LOG_ERR, "Devices detected:");
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			if (cgpu->name)
				applog(LOG_ERR, " %2d. %"PRIprepr": %s (driver: %s)", i, cgpu->proc_repr, cgpu->name, cgpu->drv->dname);
			else
				applog(LOG_ERR, " %2d. %"PRIprepr" (driver: %s)", i, cgpu->proc_repr, cgpu->drv->dname);
		}
		quit(0, "%d devices listed", total_devices);
	}

	mining_threads = 0;
	if (devices_enabled) {
		for (i = 0; i < (int)(sizeof(devices_enabled) * 8) - 1; ++i) {
			if (devices_enabled & (1 << i)) {
				if (i >= total_devices)
					quit (1, "Command line options set a device that doesn't exist");
				register_device(devices[i]);
			} else if (i < total_devices) {
				if (opt_removedisabled) {
					if (devices[i]->drv == &cpu_drv)
						--opt_n_threads;
				} else {
					register_device(devices[i]);
				}
				devices[i]->deven = DEV_DISABLED;
			}
		}
		total_devices = cgminer_id_count;
	} else {
		for (i = 0; i < total_devices; ++i)
			register_device(devices[i]);
	}

#ifdef USE_USBUTILS
	if (!total_devices) {
		applog(LOG_WARNING, "No devices detected!");
		applog(LOG_WARNING, "Waiting for USB hotplug devices or press q to quit");
	}
#else
	if (!total_devices)
		quit(1, "All devices disabled, cannot mine!");
#endif

	load_temp_config();

	for (i = 0; i < total_devices; ++i)
		devices[i]->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

#ifdef HAVE_CURSES
	change_summarywinsize();
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
		pool->cgminer_pool_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

		if (!pool->rpc_url)
			quit(1, "No URI supplied for pool %u", i);
		
		if (!pool->rpc_userpass) {
			if (!pool->rpc_user || !pool->rpc_pass)
				quit(1, "No login credentials supplied for pool %u %s", i, pool->rpc_url);
			pool->rpc_userpass = malloc(strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2);
			if (!pool->rpc_userpass)
				quit(1, "Failed to malloc userpass");
			sprintf(pool->rpc_userpass, "%s:%s", pool->rpc_user, pool->rpc_pass);
		}
	}
	/* Set the currentpool to pool with priority 0 */
	validate_pool_priorities();
	for (i = 0; i < total_pools; i++) {
		struct pool *pool  = pools[i];

		if (!pool->prio)
			currentpool = pool;
	}

#ifdef HAVE_SYSLOG_H
	if (use_syslog)
		openlog(PACKAGE, LOG_PID, LOG_USER);
#endif

	#if defined(unix)
		if (opt_stderr_cmd)
			fork_monitor();
	#endif // defined(unix)

	mining_thr = calloc(mining_threads, sizeof(thr));
	if (!mining_thr)
		quit(1, "Failed to calloc mining_thr");
	for (i = 0; i < mining_threads; i++) {
		mining_thr[i] = calloc(1, sizeof(*thr));
		if (!mining_thr[i])
			quit(1, "Failed to calloc mining_thr[%d]", i);
	}

	total_control_threads = 7;
	control_thr = calloc(total_control_threads, sizeof(*thr));
	if (!control_thr)
		quit(1, "Failed to calloc control_thr");

	gwsched_thr_id = 0;
	stage_thr_id = 1;
	thr = &control_thr[stage_thr_id];
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
		int slept = 0;

		/* Look for at least one active pool before starting */
		probe_pools();
		do {
			sleep(1);
			slept++;
		} while (!pools_active && slept < 60);

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
				cbreak();
			} else
#endif
				quit(0, "No servers could be used! Exiting.");
		}
	} while (!pools_active);

#ifdef USE_SCRYPT
	if (detect_algo == 1 && !opt_scrypt) {
		applog(LOG_NOTICE, "Detected scrypt algorithm");
		opt_scrypt = true;
	}
#endif
	detect_algo = 0;

begin_bench:
	total_mhashes_done = 0;
	for (i = 0; i < total_devices; i++) {
		struct cgpu_info *cgpu = devices[i];

		cgpu->rolling = cgpu->total_mhashes = 0;
	}
	
	cgtime(&total_tv_start);
	cgtime(&total_tv_end);
	miner_started = total_tv_start;
	if (schedstart.tm.tm_sec)
		localtime_r(&miner_started.tv_sec, &schedstart.tm);
	if (schedstop.tm.tm_sec)
		localtime_r(&miner_started.tv_sec, &schedstop .tm);
	get_datestamp(datestamp, &total_tv_start);

	// Initialise processors and threads
	k = 0;
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		struct device_drv *api = cgpu->drv;
		int threadobj = cgpu->threads;
		if (!threadobj)
			// Create a fake thread object to handle hashmeter etc
			threadobj = 1;
		cgpu->thr = calloc(threadobj + 1, sizeof(*cgpu->thr));
		cgpu->thr[threadobj] = NULL;
		cgpu->status = LIFE_INIT;

		cgpu->max_hashes = 0;

		// Setup thread structs before starting any of the threads, in case they try to interact
		for (j = 0; j < threadobj; ++j, ++k) {
			thr = get_thread(k);
			thr->id = k;
			thr->cgpu = cgpu;
			thr->device_thread = j;
			thr->work_restart_notifier[1] = INVSOCK;
			thr->mutex_request[1] = INVSOCK;
			thr->_job_transition_in_progress = true;
			timerclear(&thr->tv_morework);
			thr->_last_sbr_state = true;

			thr->scanhash_working = true;
			thr->hashes_done = 0;
			timerclear(&thr->tv_hashes_done);
			cgtime(&thr->tv_lastupdate);
			thr->tv_poll.tv_sec = -1;
			thr->_max_nonce = api->can_limit_work ? api->can_limit_work(thr) : 0xffffffff;

			cgpu->thr[j] = thr;
		}
	}

	// Start threads
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		if (!cgpu->threads)
			memcpy(&cgpu->thr[0]->notifier, &cgpu->device->thr[0]->notifier, sizeof(cgpu->thr[0]->notifier));
		for (j = 0; j < cgpu->threads; ++j) {
			thr = cgpu->thr[j];

			notifier_init(thr->notifier);

			/* Enable threads for devices set not to mine but disable
			 * their queue in case we wish to enable them later */
			if (cgpu->drv->thread_prepare && !cgpu->drv->thread_prepare(thr))
				continue;

			thread_reportout(thr);

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				quit(1, "thread %d create failed", thr->id);
		}
		if (cgpu->deven == DEV_ENABLED)
			proc_enable(cgpu);
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

	cgtime(&total_tv_start);
	cgtime(&total_tv_end);

	{
		pthread_t submit_thread;
		if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, NULL)))
			quit(1, "submit_work thread create failed");
	}

	watchpool_thr_id = 2;
	thr = &control_thr[watchpool_thr_id];
	/* start watchpool thread */
	if (thr_info_create(thr, NULL, watchpool_thread, NULL))
		quit(1, "watchpool thread create failed");
	pthread_detach(thr->pth);

	watchdog_thr_id = 3;
	thr = &control_thr[watchdog_thr_id];
	/* start watchdog thread */
	if (thr_info_create(thr, NULL, watchdog_thread, NULL))
		quit(1, "watchdog thread create failed");
	pthread_detach(thr->pth);

#ifdef HAVE_OPENCL
	/* Create reinit gpu thread */
	gpur_thr_id = 4;
	thr = &control_thr[gpur_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "tq_new failed for gpur_thr_id");
	if (thr_info_create(thr, NULL, reinit_gpu, thr))
		quit(1, "reinit_gpu thread create failed");
#endif	

	/* Create API socket thread */
	api_thr_id = 5;
	thr = &control_thr[api_thr_id];
	if (thr_info_create(thr, NULL, api_thread, thr))
		quit(1, "API thread create failed");

#ifdef HAVE_CURSES
	/* Create curses input thread for keyboard input. Create this last so
	 * that we know all threads are created since this can call kill_work
	 * to try and shut down ll previous threads. */
	input_thr_id = 6;
	thr = &control_thr[input_thr_id];
	if (thr_info_create(thr, NULL, input_thread, thr))
		quit(1, "input thread create failed");
	pthread_detach(thr->pth);
#endif

	/* Just to be sure */
	if (total_control_threads != 7)
		quit(1, "incorrect total_control_threads (%d) should be 7", total_control_threads);

	/* Once everything is set up, main() becomes the getwork scheduler */
	while (42) {
		int ts, max_staged = opt_queue;
		struct pool *pool, *cp;
		bool lagging = false;
		struct curl_ent *ce;
		struct work *work;

		cp = current_pool();

		/* If the primary pool is a getwork pool and cannot roll work,
		 * try to stage one extra work per mining thread */
		if (!cp->has_stratum && cp->proto != PLP_GETBLOCKTEMPLATE && !staged_rollable)
			max_staged += mining_threads;

		mutex_lock(stgd_lock);
		ts = __total_staged();

		if (!cp->has_stratum && cp->proto != PLP_GETBLOCKTEMPLATE && !ts && !opt_fail_only)
			lagging = true;

		/* Wait until hash_pop tells us we need to create more work */
		if (ts > max_staged) {
			pthread_cond_wait(&gws_cond, stgd_lock);
			ts = __total_staged();
		}
		mutex_unlock(stgd_lock);

		if (ts > max_staged)
			continue;

		work = make_work();

		if (lagging && !pool_tset(cp, &cp->lagging)) {
			applog(LOG_WARNING, "Pool %d not providing work fast enough", cp->pool_no);
			cp->getfail_occasions++;
			total_go++;
		}
		pool = select_pool(lagging);
retry:
		if (pool->has_stratum) {
			while (!pool->stratum_active || !pool->stratum_notify) {
				struct pool *altpool = select_pool(true);

				if (altpool == pool && pool->has_stratum)
					nmsleep(5000);
				pool = altpool;
				goto retry;
			}
			gen_stratum_work(pool, work);
			applog(LOG_DEBUG, "Generated stratum work");
			stage_work(work);
			continue;
		}

		if (pool->last_work_copy) {
			mutex_lock(&pool->last_work_lock);
			struct work *last_work = pool->last_work_copy;
			if (!last_work)
				{}
			else
			if (can_roll(last_work) && should_roll(last_work)) {
				free_work(work);
				work = make_clone(pool->last_work_copy);
				mutex_unlock(&pool->last_work_lock);
				roll_work(work);
				applog(LOG_DEBUG, "Generated work from latest GBT job in get_work_thread with %d seconds left", (int)blkmk_time_left(work->tmpl, time(NULL)));
				stage_work(work);
				continue;
			} else if (last_work->tmpl && pool->proto == PLP_GETBLOCKTEMPLATE && blkmk_work_left(last_work->tmpl) > (unsigned long)mining_threads) {
				// Don't free last_work_copy, since it is used to detect upstream provides plenty of work per template
			} else {
				free_work(last_work);
				pool->last_work_copy = NULL;
			}
			mutex_unlock(&pool->last_work_lock);
		}

		if (clone_available()) {
			applog(LOG_DEBUG, "Cloned getwork work");
			free_work(work);
			continue;
		}

		if (opt_benchmark) {
			get_benchmark_work(work);
			applog(LOG_DEBUG, "Generated benchmark work");
			stage_work(work);
			continue;
		}

		work->pool = pool;
		ce = pop_curl_entry3(pool, 2);
		/* obtain new work from bitcoin via JSON-RPC */
		if (!get_upstream_work(work, ce->curl)) {
			struct pool *next_pool;

			/* Make sure the pool just hasn't stopped serving
			 * requests but is up as we'll keep hammering it */
			push_curl_entry(ce, pool);
			++pool->seq_getfails;
			pool_died(pool);
			next_pool = select_pool(!opt_fail_only);
			if (pool == next_pool) {
				applog(LOG_DEBUG, "Pool %d json_rpc_call failed on get work, retrying in 5s", pool->pool_no);
				nmsleep(5000);
			} else {
				applog(LOG_DEBUG, "Pool %d json_rpc_call failed on get work, failover activated", pool->pool_no);
				pool = next_pool;
			}
			goto retry;
		}
		if (ts >= max_staged)
			pool_tclear(pool, &pool->lagging);
		if (pool_tclear(pool, &pool->idle))
			pool_resus(pool);

		applog(LOG_DEBUG, "Generated getwork work");
		stage_work(work);
		push_curl_entry(ce, pool);
	}

	return 0;
}
