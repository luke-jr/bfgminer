/*
 * Copyright 2011-2014 Con Kolivas
 * Copyright 2011-2017 Luke Dashjr
 * Copyright 2014 Nate Woolls
 * Copyright 2012-2014 Andrew Smith
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef HAVE_CURSES
#ifdef USE_UNICODE
#define PDC_WIDE
#endif
// Must be before stdbool, since pdcurses typedefs bool :/
#include <curses.h>
#endif

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
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
#include <wctype.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifndef WIN32
#include <sys/resource.h>
#include <sys/socket.h>
#if defined(HAVE_LIBUDEV) && defined(HAVE_SYS_EPOLL_H)
#include <libudev.h>
#include <sys/epoll.h>
#define HAVE_BFG_HOTPLUG
#endif
#else
#include <winsock2.h>
#include <windows.h>
#include <dbt.h>
#define HAVE_BFG_HOTPLUG
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
#include <libbase58.h>

#include "compat.h"
#include "deviceapi.h"
#include "logging.h"
#include "miner.h"
#include "adl.h"
#include "driver-cpu.h"
#include "driver-opencl.h"
#include "util.h"

#ifdef USE_AVALON
#include "driver-avalon.h"
#endif

#ifdef HAVE_BFG_LOWLEVEL
#include "lowlevel.h"
#endif

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <fcntl.h>
	#include <sys/wait.h>
#endif

#ifdef USE_SCRYPT
#include "malgo/scrypt.h"
#endif

#if defined(USE_AVALON) || defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_MODMINER) || defined(USE_NANOFURY) || defined(USE_X6500) || defined(USE_ZTEX)
#	define USE_FPGA
#endif

enum bfg_quit_summary {
	BQS_DEFAULT,
	BQS_NONE,
	BQS_DEVS,
	BQS_PROCS,
	BQS_DETAILED,
};

struct strategies strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
	{ "Balance" },
};

#define packagename bfgminer_name_space_ver

bool opt_protocol;
bool opt_dev_protocol;
static bool opt_benchmark, opt_benchmark_intense;
static bool want_longpoll = true;
static bool want_gbt = true;
static bool want_getwork = true;
#if BLKMAKER_VERSION > 1
static bool opt_load_bitcoin_conf = true;
static uint32_t coinbase_script_block_id;
static uint32_t template_nonce;
#endif
#if BLKMAKER_VERSION > 0
char *opt_coinbase_sig;
#endif
static enum bfg_quit_summary opt_quit_summary = BQS_DEFAULT;
static bool include_serial_in_statline;
char *request_target_str;
float request_pdiff = 1.0;
double request_bdiff;
static bool want_stratum = true;
int opt_skip_checks;
bool want_per_device_stats;
bool use_syslog;
bool opt_quiet_work_updates = true;
bool opt_quiet;
bool opt_realquiet;
int loginput_size;
bool opt_compact;
bool opt_show_procs;
const int opt_cutofftemp = 95;
int opt_hysteresis = 3;
static int opt_retries = -1;
int opt_fail_pause = 5;
int opt_log_interval = 20;
int opt_queue = 1;
int opt_scantime = 60;
int opt_expiry = 120;
int opt_expiry_lp = 3600;
unsigned long long global_hashrate;
static bool opt_unittest = false;
unsigned unittest_failures;
unsigned long global_quota_gcd = 1;
time_t last_getwork;

#ifdef USE_OPENCL
int opt_dynamic_interval = 7;
int nDevs;
int opt_g_threads = -1;
#endif
#ifdef USE_SCRYPT
static char detect_algo = 1;
#else
static char detect_algo;
#endif
bool opt_restart = true;

#ifdef USE_LIBMICROHTTPD
#include "httpsrv.h"
int httpsrv_port = -1;
#endif
#ifdef USE_LIBEVENT
long stratumsrv_port = -1;
#endif

const
int rescan_delay_ms = 1000;
#ifdef HAVE_BFG_HOTPLUG
bool opt_hotplug = 1;
const
int hotplug_delay_ms = 100;
#else
const bool opt_hotplug;
#endif
struct string_elist *scan_devices;
static struct string_elist *opt_set_device_list;
bool opt_force_dev_init;
static struct string_elist *opt_devices_enabled_list;
static bool opt_display_devs;
int total_devices;
struct cgpu_info **devices;
int total_devices_new;
struct cgpu_info **devices_new;
bool have_opencl;
int opt_n_threads = -1;
int mining_threads;
int base_queue;
int num_processors;
#ifdef HAVE_CURSES
bool use_curses = true;
#else
bool use_curses;
#endif
int last_logstatusline_len;
#ifdef HAVE_LIBUSB
bool have_libusb;
#endif
static bool opt_submit_stale = true;
static float opt_shares;
static int opt_submit_threads = 0x40;
bool opt_fail_only;
int opt_fail_switch_delay = 300;
bool opt_autofan;
bool opt_autoengine;
bool opt_noadl;
char *opt_api_allow = NULL;
char *opt_api_groups;
char *opt_api_description = PACKAGE_STRING;
int opt_api_port = 4028;
bool opt_api_listen;
bool opt_api_mcast;
char *opt_api_mcast_addr = API_MCAST_ADDR;
char *opt_api_mcast_code = API_MCAST_CODE;
char *opt_api_mcast_des = "";
int opt_api_mcast_port = 4028;
bool opt_api_network;
bool opt_delaynet;
bool opt_disable_pool;
bool opt_disable_client_reconnect = false;
static bool no_work;
bool opt_worktime;
bool opt_weighed_stats;

char *opt_kernel_path;
char *cgminer_path;

#if defined(USE_BITFORCE)
bool opt_bfl_noncerange;
#endif
#define QUIET	(opt_quiet || opt_realquiet)

struct thr_info *control_thr;
struct thr_info **mining_thr;
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

double total_rolling;
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
int total_accepted, total_rejected;
int total_getworks, total_stale, total_discarded;
uint64_t total_bytes_rcvd, total_bytes_sent;
double total_diff1, total_bad_diff1;
double total_diff_accepted, total_diff_rejected, total_diff_stale;
static int staged_rollable, staged_spare;
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

#ifdef HAVE_CURSES
#if !(defined(PDCURSES) || defined(NCURSES_VERSION))
const
#endif
short default_bgcolor = COLOR_BLACK;
static int attr_title = A_BOLD;
#endif

static
#if defined(HAVE_CURSES) && defined(USE_UNICODE)
bool use_unicode;
static
bool have_unicode_degrees;
static
wchar_t unicode_micro = 'u';
#else
const bool use_unicode;
static
const bool have_unicode_degrees;
#ifdef HAVE_CURSES
static
const char unicode_micro = 'u';
#endif
#endif

#ifdef HAVE_CURSES
#define U8_BAD_START "\xef\x80\x81"
#define U8_BAD_END   "\xef\x80\x80"
#define AS_BAD(x) U8_BAD_START x U8_BAD_END

/* logstart is where the log window should start */
static int devcursor, logstart, logcursor;

bool selecting_device;
unsigned selected_device;
#endif

static int max_lpdigits;

// current_hash was replaced with goal->current_goal_detail
// current_block_id was replaced with blkchain->currentblk->block_id

static char datestamp[40];
static char best_share[ALLOC_H2B_SHORTV] = "0";
double best_diff = 0;

struct mining_algorithm *mining_algorithms;
struct mining_goal_info *mining_goals;
int active_goals = 1;


int swork_id;

/* For creating a hash database of stratum shares submitted that have not had
 * a response yet */
struct stratum_share {
	UT_hash_handle hh;
	bool block;
	struct work *work;
	int id;
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
#define JSON_WEB_ERROR "WEB config err"

char *cmd_idle, *cmd_sick, *cmd_dead;

#if defined(unix) || defined(__APPLE__)
	static char *opt_stderr_cmd = NULL;
	static int forkpid;
#endif // defined(unix)

#ifdef HAVE_CHROOT
char *chroot_dir;
#endif

#ifdef HAVE_PWD_H
char *opt_setuid;
#endif

struct sigaction termhandler, inthandler;

struct thread_q *getq;

static int total_work;
static bool staged_full;
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
	struct tm tm;
	time_t tt;
	bool within_range;

	if (!schedstart.enable && !schedstop.enable)
		return true;

	tt = time(NULL);
	localtime_r(&tt, &tm);

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

void get_datestamp(char *f, size_t fsiz, time_t tt)
{
	struct tm _tm;
	struct tm *tm = &_tm;
	
	if (tt == INVALID_TIMESTAMP)
		tt = time(NULL);

	localtime_r(&tt, tm);
	snprintf(f, fsiz, "[%d-%02d-%02d %02d:%02d:%02d]",
		tm->tm_year + 1900,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static
void get_timestamp(char *f, size_t fsiz, time_t tt)
{
	struct tm _tm;
	struct tm *tm = &_tm;

	localtime_r(&tt, tm);
	snprintf(f, fsiz, "[%02d:%02d:%02d]",
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static void applog_and_exit(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);

static char exit_buf[512];

static void applog_and_exit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(exit_buf, sizeof(exit_buf), fmt, ap);
	va_end(ap);
	_applog(LOG_ERR, exit_buf);
	exit(1);
}

static
float drv_min_nonce_diff(const struct device_drv * const drv, struct cgpu_info * const proc, const struct mining_algorithm * const malgo)
{
	if (drv->drv_min_nonce_diff)
		return drv->drv_min_nonce_diff(proc, malgo);
#ifdef USE_SHA256D
	return (malgo->algo == POW_SHA256D) ? 1. : -1.;
#else
	return -1.;
#endif
}

char *devpath_to_devid(const char *devpath)
{
#ifndef WIN32
	if (devpath[0] != '/')
		return NULL;
	struct stat my_stat;
	if (stat(devpath, &my_stat))
		return NULL;
	char *devs = malloc(6 + (sizeof(dev_t) * 2) + 1);
	memcpy(devs, "dev_t:", 6);
	bin2hex(&devs[6], &my_stat.st_rdev, sizeof(dev_t));
#else
	if (!strncmp(devpath, "\\\\.\\", 4))
		devpath += 4;
	if (strncasecmp(devpath, "COM", 3) || !devpath[3])
		return NULL;
	devpath += 3;
	char *p;
	strtol(devpath, &p, 10);
	if (p[0])
		return NULL;
	const int sz = (p - devpath);
	char *devs = malloc(4 + sz + 1);
	sprintf(devs, "com:%s", devpath);
#endif
	return devs;
}

static
bool devpaths_match(const char * const ap, const char * const bp)
{
	char * const a = devpath_to_devid(ap);
	if (!a)
		return false;
	char * const b = devpath_to_devid(bp);
	bool rv = false;
	if (b)
	{
		rv = !strcmp(a, b);
		free(b);
	}
	free(a);
	return rv;
}

static
int proc_letter_to_number(const char *s, const char ** const rem)
{
	int n = 0, c;
	for ( ; s[0]; ++s)
	{
		if (unlikely(n > INT_MAX / 26))
			break;
		c = tolower(s[0]) - 'a';
		if (unlikely(c < 0 || c > 25))
			break;
		if (unlikely(INT_MAX - c < n))
			break;
		n = (n * 26) + c;
	}
	*rem = s;
	return n;
}

static
bool cgpu_match(const char * const pattern, const struct cgpu_info * const cgpu)
{
	// all - matches anything
	// d0 - matches all processors of device 0
	// d0-3 - matches all processors of device 0, 1, 2, or 3
	// d0a - matches first processor of device 0
	// 0 - matches processor 0
	// 0-4 - matches processors 0, 1, 2, 3, or 4
	// ___ - matches all processors on all devices using driver/name ___
	// ___0 - matches all processors of 0th device using driver/name ___
	// ___0a - matches first processor of 0th device using driver/name ___
	// @* - matches device with serial or path *
	// @*@a - matches first processor of device with serial or path *
	// ___@* - matches device with serial or path * using driver/name ___
	if (!strcasecmp(pattern, "all"))
		return true;
	
	const struct device_drv * const drv = cgpu->drv;
	const char *p = pattern, *p2;
	size_t L;
	int n, i, c = -1;
	int n2;
	int proc_first = -1, proc_last = -1;
	struct cgpu_info *device;
	
	if (!(strncasecmp(drv->dname, p, (L = strlen(drv->dname)))
	   && strncasecmp(drv-> name, p, (L = strlen(drv-> name)))))
		// dname or name
		p = &pattern[L];
	else
	if (p[0] == 'd' && (isdigit(p[1]) || p[1] == '-'))
		// d#
		++p;
	else
	if (isdigit(p[0]) || p[0] == '@' || p[0] == '-')
		// # or @
		{}
	else
		return false;
	
	L = p - pattern;
	while (isspace(p[0]))
		++p;
	if (p[0] == '@')
	{
		// Serial/path
		const char * const ser = &p[1];
		for (p = ser; p[0] != '@' && p[0] != '\0'; ++p)
		{}
		p2 = (p[0] == '@') ? &p[1] : p;
		const size_t serlen = (p - ser);
		p = "";
		n = n2 = 0;
		const char * const devpath = cgpu->device_path ?: "";
		const char * const devser = cgpu->dev_serial ?: "";
		if ((!strncmp(devpath, ser, serlen)) && devpath[serlen] == '\0')
		{}  // Match
		else
		if ((!strncmp(devser, ser, serlen)) && devser[serlen] == '\0')
		{}  // Match
		else
		{
			char devpath2[serlen + 1];
			memcpy(devpath2, ser, serlen);
			devpath2[serlen] = '\0';
			if (!devpaths_match(devpath, ser))
				return false;
		}
	}
	else
	{
		if (isdigit(p[0]))
			n = strtol(p, (void*)&p2, 0);
		else
		{
			n = 0;
			p2 = p;
		}
		if (p2[0] == '-')
		{
			++p2;
			if (p2[0] && isdigit(p2[0]))
				n2 = strtol(p2, (void*)&p2, 0);
			else
				n2 = INT_MAX;
		}
		else
			n2 = n;
		if (p == pattern)
		{
			if (!p[0])
				return true;
			if (p2 && p2[0])
				goto invsyntax;
			for (i = n; i <= n2; ++i)
			{
				if (i >= total_devices)
					break;
				if (cgpu == devices[i])
					return true;
			}
			return false;
		}
	}
	
	if (p2[0])
	{
		proc_first = proc_letter_to_number(&p2[0], &p2);
		if (p2[0] == '-')
		{
			++p2;
			if (p2[0])
				proc_last = proc_letter_to_number(p2, &p2);
			else
				proc_last = INT_MAX;
		}
		else
			proc_last = proc_first;
		if (p2[0])
			goto invsyntax;
	}
	
	if (L > 1 || tolower(pattern[0]) != 'd' || !p[0])
	{
		if ((L == 3 && !strncasecmp(pattern, drv->name, 3)) ||
			(!L) ||
			(L == strlen(drv->dname) && !strncasecmp(pattern, drv->dname, L)))
			{}  // Matched name or dname
		else
			return false;
		if (p[0] && (cgpu->device_id < n || cgpu->device_id > n2))
			return false;
		if (proc_first != -1 && (cgpu->proc_id < proc_first || cgpu->proc_id > proc_last))
			return false;
		return true;
	}
	
	// d#
	
	c = -1;
	for (i = 0; ; ++i)
	{
		if (i == total_devices)
			return false;
		if (devices[i]->device != devices[i])
			continue;
		++c;
		if (c < n)
			continue;
		if (c > n2)
			break;
		
		for (device = devices[i]; device; device = device->next_proc)
		{
			if (proc_first != -1 && (device->proc_id < proc_first || device->proc_id > proc_last))
				continue;
			if (device == cgpu)
				return true;
		}
	}
	return false;

invsyntax:
	applog(LOG_WARNING, "%s: Invalid syntax: %s", __func__, pattern);
	return false;
}

#define TEST_CGPU_MATCH(pattern)  \
	if (!cgpu_match(pattern, &cgpu))  \
	{  \
		++unittest_failures;  \
		applog(LOG_ERR, "%s: Pattern \"%s\" should have matched!", __func__, pattern);  \
	}  \
// END TEST_CGPU_MATCH
#define TEST_CGPU_NOMATCH(pattern)  \
	if (cgpu_match(pattern, &cgpu))  \
	{  \
		++unittest_failures;  \
		applog(LOG_ERR, "%s: Pattern \"%s\" should NOT have matched!", __func__, pattern);  \
	}  \
// END TEST_CGPU_MATCH
static __maybe_unused
void test_cgpu_match()
{
	struct device_drv drv = {
		.dname = "test",
		.name = "TST",
	};
	struct cgpu_info cgpu = {
		.drv = &drv,
		.device = &cgpu,
		.device_id = 1,
		.proc_id = 1,
		.proc_repr = "TST 1b",
	}, cgpu0a = {
		.drv = &drv,
		.device = &cgpu0a,
		.device_id = 0,
		.proc_id = 0,
		.proc_repr = "TST 0a",
	}, cgpu1a = {
		.drv = &drv,
		.device = &cgpu0a,
		.device_id = 1,
		.proc_id = 0,
		.proc_repr = "TST 1a",
	};
	struct cgpu_info *devices_list[3] = {&cgpu0a, &cgpu1a, &cgpu,};
	devices = devices_list;
	total_devices = 3;
	TEST_CGPU_MATCH("all")
	TEST_CGPU_MATCH("d1")
	TEST_CGPU_NOMATCH("d2")
	TEST_CGPU_MATCH("d0-5")
	TEST_CGPU_NOMATCH("d0-0")
	TEST_CGPU_NOMATCH("d2-5")
	TEST_CGPU_MATCH("d-1")
	TEST_CGPU_MATCH("d1-")
	TEST_CGPU_NOMATCH("d-0")
	TEST_CGPU_NOMATCH("d2-")
	TEST_CGPU_MATCH("2")
	TEST_CGPU_NOMATCH("3")
	TEST_CGPU_MATCH("1-2")
	TEST_CGPU_MATCH("2-3")
	TEST_CGPU_NOMATCH("1-1")
	TEST_CGPU_NOMATCH("3-4")
	TEST_CGPU_MATCH("TST")
	TEST_CGPU_MATCH("test")
	TEST_CGPU_MATCH("tst")
	TEST_CGPU_MATCH("TEST")
	TEST_CGPU_NOMATCH("TSF")
	TEST_CGPU_NOMATCH("TS")
	TEST_CGPU_NOMATCH("TSTF")
	TEST_CGPU_MATCH("TST1")
	TEST_CGPU_MATCH("test1")
	TEST_CGPU_MATCH("TST0-1")
	TEST_CGPU_MATCH("TST 1")
	TEST_CGPU_MATCH("TST 1-2")
	TEST_CGPU_MATCH("TEST 1-2")
	TEST_CGPU_NOMATCH("TST2")
	TEST_CGPU_NOMATCH("TST2-3")
	TEST_CGPU_NOMATCH("TST0-0")
	TEST_CGPU_MATCH("TST1b")
	TEST_CGPU_MATCH("tst1b")
	TEST_CGPU_NOMATCH("TST1c")
	TEST_CGPU_NOMATCH("TST1bb")
	TEST_CGPU_MATCH("TST0-1b")
	TEST_CGPU_NOMATCH("TST0-1c")
	TEST_CGPU_MATCH("TST1a-d")
	TEST_CGPU_NOMATCH("TST1a-a")
	TEST_CGPU_NOMATCH("TST1-a")
	TEST_CGPU_NOMATCH("TST1c-z")
	TEST_CGPU_NOMATCH("TST1c-")
	TEST_CGPU_MATCH("@")
	TEST_CGPU_NOMATCH("@abc")
	TEST_CGPU_MATCH("@@b")
	TEST_CGPU_NOMATCH("@@c")
	TEST_CGPU_MATCH("TST@")
	TEST_CGPU_NOMATCH("TST@abc")
	TEST_CGPU_MATCH("TST@@b")
	TEST_CGPU_NOMATCH("TST@@c")
	TEST_CGPU_MATCH("TST@@b-f")
	TEST_CGPU_NOMATCH("TST@@c-f")
	TEST_CGPU_NOMATCH("TST@@-a")
	cgpu.device_path = "/dev/test";
	cgpu.dev_serial = "testy";
	TEST_CGPU_MATCH("TST@/dev/test")
	TEST_CGPU_MATCH("TST@testy")
	TEST_CGPU_NOMATCH("TST@")
	TEST_CGPU_NOMATCH("TST@/dev/test5@b")
	TEST_CGPU_NOMATCH("TST@testy3@b")
	TEST_CGPU_MATCH("TST@/dev/test@b")
	TEST_CGPU_MATCH("TST@testy@b")
	TEST_CGPU_NOMATCH("TST@/dev/test@c")
	TEST_CGPU_NOMATCH("TST@testy@c")
	cgpu.device_path = "usb:000:999";
	TEST_CGPU_MATCH("TST@usb:000:999")
	drv.dname = "test7";
	TEST_CGPU_MATCH("test7")
	TEST_CGPU_MATCH("TEST7")
	TEST_CGPU_NOMATCH("test&")
	TEST_CGPU_MATCH("test7 1-2")
	TEST_CGPU_MATCH("test7@testy@b")
}

static
int cgpu_search(const char * const pattern, const int first)
{
	int i;
	struct cgpu_info *cgpu;
	
#define CHECK_CGPU_SEARCH  do{      \
	cgpu = get_devices(i);          \
	if (cgpu_match(pattern, cgpu))  \
		return i;                   \
}while(0)
	for (i = first; i < total_devices; ++i)
		CHECK_CGPU_SEARCH;
	for (i = 0; i < first; ++i)
		CHECK_CGPU_SEARCH;
#undef CHECK_CGPU_SEARCH
	return -1;
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

static pthread_mutex_t noncelog_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *noncelog_file = NULL;

static
void noncelog(const struct work * const work)
{
	const int thr_id = work->thr_id;
	const struct cgpu_info *proc = get_thr_cgpu(thr_id);
	char buf[0x200], hash[65], data[161], midstate[65];
	int rv;
	size_t ret;
	
	bin2hex(hash, work->hash, 32);
	bin2hex(data, work->data, 80);
	bin2hex(midstate, work->midstate, 32);
	
	// timestamp,proc,hash,data,midstate
	rv = snprintf(buf, sizeof(buf), "%lu,%s,%s,%s,%s\n",
	              (unsigned long)time(NULL), proc->proc_repr_ns,
	              hash, data, midstate);
	
	if (unlikely(rv < 1))
	{
		applog(LOG_ERR, "noncelog printf error");
		return;
	}
	
	mutex_lock(&noncelog_lock);
	ret = fwrite(buf, rv, 1, noncelog_file);
	fflush(noncelog_file);
	mutex_unlock(&noncelog_lock);
	
	if (ret != 1)
		applog(LOG_ERR, "noncelog fwrite error");
}

static void sharelog(const char*disposition, const struct work*work)
{
	char target[(sizeof(work->target) * 2) + 1];
	char hash[(sizeof(work->hash) * 2) + 1];
	char data[(sizeof(work->data) * 2) + 1];
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
	t = work->ts_getwork + timer_elapsed(&work->tv_getwork, &work->tv_work_found);
	bin2hex(target, work->target, sizeof(work->target));
	bin2hex(hash, work->hash, sizeof(work->hash));
	bin2hex(data, work->data, sizeof(work->data));

	// timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
	rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->proc_repr_ns, thr_id, hash, data);
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

#ifdef HAVE_CURSES
static void switch_logsize(void);
#endif

static void hotplug_trigger();

void goal_set_malgo(struct mining_goal_info * const goal, struct mining_algorithm * const malgo)
{
	if (goal->malgo == malgo)
		return;
	
	if (goal->malgo)
		--goal->malgo->goal_refs;
	if (malgo->goal_refs++)
		// First time using a new mining algorithm may means we need to add mining hardware to support it
		// api_thr_id is used as an ugly hack to determine if mining has started - if not, we do NOT want to try to hotplug anything (let the initial detect handle it)
		if (opt_hotplug && api_thr_id)
			hotplug_trigger();
	goal->malgo = malgo;
}

struct mining_algorithm *mining_algorithm_by_alias(const char * const alias)
{
	struct mining_algorithm *malgo;
	LL_FOREACH(mining_algorithms, malgo)
	{
		if (match_strtok(malgo->aliases, "|", alias))
			return malgo;
	}
	return NULL;
}

#ifdef USE_SCRYPT
extern struct mining_algorithm malgo_scrypt;

static
const char *set_malgo_scrypt()
{
	goal_set_malgo(get_mining_goal("default"), &malgo_scrypt);
	return NULL;
}
#endif

static
int mining_goals_name_cmp(const struct mining_goal_info * const a, const struct mining_goal_info * const b)
{
	// default always goes first
	if (a->is_default)
		return -1;
	if (b->is_default)
		return 1;
	return strcmp(a->name, b->name);
}

static
void blkchain_init_block(struct blockchain_info * const blkchain)
{
	struct block_info * const dummy_block = calloc(sizeof(*dummy_block), 1);
	memset(dummy_block->prevblkhash, 0, 0x20);
	HASH_ADD(hh, blkchain->blocks, prevblkhash, sizeof(dummy_block->prevblkhash), dummy_block);
	blkchain->currentblk = dummy_block;
}

extern struct mining_algorithm malgo_sha256d;

struct mining_goal_info *get_mining_goal(const char * const name)
{
	static unsigned next_goal_id;
	struct mining_goal_info *goal;
	HASH_FIND_STR(mining_goals, name, goal);
	if (!goal)
	{
		struct blockchain_info * const blkchain = malloc(sizeof(*blkchain) + sizeof(*goal));
		goal = (void*)(&blkchain[1]);
		
		*blkchain = (struct blockchain_info){
			.currentblk = NULL,
		};
		blkchain_init_block(blkchain);
		
		*goal = (struct mining_goal_info){
			.id = next_goal_id++,
			.name = strdup(name),
			.is_default = !strcmp(name, "default"),
			.blkchain = blkchain,
			.current_diff = 0xFFFFFFFFFFFFFFFFULL,
		};
#ifdef USE_SHA256D
		goal_set_malgo(goal, &malgo_sha256d);
#else
		// NOTE: Basically random default
		goal_set_malgo(goal, mining_algorithms);
#endif
		HASH_ADD_KEYPTR(hh, mining_goals, goal->name, strlen(goal->name), goal);
		HASH_SORT(mining_goals, mining_goals_name_cmp);
		
#ifdef HAVE_CURSES
		devcursor = 7 + active_goals;
		switch_logsize();
#endif
	}
	return goal;
}

void mining_goal_reset(struct mining_goal_info * const goal)
{
	struct blockchain_info * const blkchain = goal->blkchain;
	struct block_info *blkinfo, *tmpblkinfo;
	HASH_ITER(hh, blkchain->blocks, blkinfo, tmpblkinfo)
	{
		HASH_DEL(blkchain->blocks, blkinfo);
		free(blkinfo);
	}
	blkchain_init_block(blkchain);
}

static char *getwork_req = "{\"method\": \"getwork\", \"params\": [], \"id\":0}\n";

/* Adjust all the pools' quota to the greatest common denominator after a pool
 * has been added or the quotas changed. */
void adjust_quota_gcd(void)
{
	unsigned long gcd, lowest_quota = ~0UL, quota;
	struct pool *pool;
	int i;

	for (i = 0; i < total_pools; i++) {
		pool = pools[i];
		quota = pool->quota;
		if (!quota)
			continue;
		if (quota < lowest_quota)
			lowest_quota = quota;
	}

	if (likely(lowest_quota < ~0UL)) {
		gcd = lowest_quota;
		for (i = 0; i < total_pools; i++) {
			pool = pools[i];
			quota = pool->quota;
			if (!quota)
				continue;
			while (quota % gcd)
				gcd--;
		}
	} else
		gcd = 1;

	for (i = 0; i < total_pools; i++) {
		pool = pools[i];
		pool->quota_used *= global_quota_gcd;
		pool->quota_used /= gcd;
		pool->quota_gcd = pool->quota / gcd;
	}

	global_quota_gcd = gcd;
	applog(LOG_DEBUG, "Global quota greatest common denominator set to %lu", gcd);
}

/* Return value is ignored if not called from add_pool_details */
struct pool *add_pool2(struct mining_goal_info * const goal)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to malloc pool in add_pool");
	pool->pool_no = pool->prio = total_pools;
	mutex_init(&pool->last_work_lock);
	mutex_init(&pool->pool_lock);
	mutex_init(&pool->pool_test_lock);
	if (unlikely(pthread_cond_init(&pool->cr_cond, bfg_condattr)))
		quit(1, "Failed to pthread_cond_init in add_pool");
	cglock_init(&pool->data_lock);
	pool->swork.data_lock_p = &pool->data_lock;
	mutex_init(&pool->stratum_lock);
	timer_unset(&pool->swork.tv_transparency);
	pool->swork.pool = pool;
	pool->goal = goal;

	pool->idle = true;
	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;
	
	cgtime(&pool->cgminer_stats.start_tv);
	pool->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
	pool->cgminer_pool_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

	pool->rpc_proxy = NULL;
	pool->quota = 1;

	pool->sock = INVSOCK;
	pool->lp_socket = CURL_SOCKET_BAD;

	pools = realloc(pools, sizeof(struct pool *) * (total_pools + 2));
	pools[total_pools++] = pool;
	
	if (opt_benchmark)
	{
		// Immediately remove it
		remove_pool(pool);
		return pool;
	}
	
	adjust_quota_gcd();
	
	if (!currentpool)
		currentpool = pool;
	
	enable_pool(pool);

	return pool;
}

static
void pool_set_uri(struct pool * const pool, char * const uri)
{
	pool->rpc_url = uri;
	pool->pool_diff_effective_retroactively = uri_get_param_bool2(uri, "retrodiff");
}

static
bool pool_diff_effective_retroactively(struct pool * const pool)
{
	if (pool->pool_diff_effective_retroactively != BTS_UNKNOWN) {
		return pool->pool_diff_effective_retroactively;
	}
	
	// By default, we enable retrodiff for stratum pools since some servers implement mining.set_difficulty in this way
	// Note that share_result will explicitly disable BTS_UNKNOWN -> BTS_FALSE if a retrodiff share is rejected specifically for its failure to meet the target.
	return pool->stratum_active;
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

#if defined(USE_CPUMINING) && !defined(USE_SHA256D)
static
char *arg_ignored(const char * const arg)
{
	return NULL;
}
#endif

static
char *set_bool_ignore_arg(const char * const arg, bool * const b)
{
	return opt_set_bool(b);
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

static char *set_long_1_to_65535_or_neg1(const char * const arg, long * const i)
{
	const long min = 1, max = 65535;
	
	char * const err = opt_set_longval(arg, i);
	
	if (err) {
		return err;
	}
	
	if (*i != -1 && (*i < min || *i > max)) {
		return "Value out of range";
	}
	
	return NULL;
}

char *set_strdup(const char *arg, char **p)
{
	*p = strdup((char *)arg);
	return NULL;
}

#if BLKMAKER_VERSION > 1
static
char *set_b58addr(const char * const arg, bytes_t * const b)
{
	size_t scriptsz = blkmk_address_to_script(NULL, 0, arg);
	if (!scriptsz)
		return "Invalid address";
	char *script = malloc(scriptsz);
	if (blkmk_address_to_script(script, scriptsz, arg) != scriptsz) {
		free(script);
		return "Failed to convert address to script";
	}
	bytes_assimilate_raw(b, script, scriptsz, scriptsz);
	return NULL;
}

static char *set_generate_addr2(struct mining_goal_info *, const char *);

static
char *set_generate_addr(char *arg)
{
	char * const colon = strchr(arg, ':');
	struct mining_goal_info *goal;
	if (colon)
	{
		colon[0] = '\0';
		goal = get_mining_goal(arg);
		arg = &colon[1];
	}
	else
		goal = get_mining_goal("default");
	
	return set_generate_addr2(goal, arg);
}

static
char *set_generate_addr2(struct mining_goal_info * const goal, const char * const arg)
{
	bytes_t newscript = BYTES_INIT;
	char *estr = set_b58addr(arg, &newscript);
	if (estr)
	{
		bytes_free(&newscript);
		return estr;
	}
	if (!goal->generation_script)
	{
		goal->generation_script = malloc(sizeof(*goal->generation_script));
		bytes_init(goal->generation_script);
	}
	bytes_assimilate(goal->generation_script, &newscript);
	bytes_free(&newscript);
	
	return NULL;
}
#endif

static
char *set_quit_summary(const char * const arg)
{
	if (!(strcasecmp(arg, "none") && strcasecmp(arg, "no")))
		opt_quit_summary = BQS_NONE;
	else
	if (!(strcasecmp(arg, "devs") && strcasecmp(arg, "devices")))
		opt_quit_summary = BQS_DEVS;
	else
	if (!(strcasecmp(arg, "procs") && strcasecmp(arg, "processors") && strcasecmp(arg, "chips") && strcasecmp(arg, "cores")))
		opt_quit_summary = BQS_PROCS;
	else
	if (!(strcasecmp(arg, "detailed") && strcasecmp(arg, "detail") && strcasecmp(arg, "all")))
		opt_quit_summary = BQS_DETAILED;
	else
		return "Quit summary must be one of none/devs/procs/detailed";
	return NULL;
}

static void pdiff_target_leadzero(void *, double);

char *set_request_diff(const char *arg, float *p)
{
	unsigned char target[32];
	char *e = opt_set_floatval(arg, p);
	if (e)
		return e;
	
	request_bdiff = (double)*p * 0.9999847412109375;
	pdiff_target_leadzero(target, *p);
	request_target_str = malloc(65);
	bin2hex(request_target_str, target, 32);
	
	return NULL;
}

#ifdef NEED_BFG_LOWL_VCOM
extern struct lowlevel_device_info *_vcom_devinfo_findorcreate(struct lowlevel_device_info **, const char *);

#ifdef WIN32
void _vcom_devinfo_scan_querydosdevice(struct lowlevel_device_info ** const devinfo_list)
{
	char dev[PATH_MAX];
	char *devp = dev;
	size_t bufLen = 0x100;
tryagain: ;
	char buf[bufLen];
	if (!QueryDosDevice(NULL, buf, bufLen)) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			bufLen *= 2;
			applog(LOG_DEBUG, "QueryDosDevice returned insufficent buffer error; enlarging to %lx", (unsigned long)bufLen);
			goto tryagain;
		}
		applogr(, LOG_WARNING, "Error occurred trying to enumerate COM ports with QueryDosDevice");
	}
	size_t tLen;
	memcpy(devp, "\\\\.\\", 4);
	devp = &devp[4];
	for (char *t = buf; *t; t += tLen) {
		tLen = strlen(t) + 1;
		if (strncmp("COM", t, 3))
			continue;
		memcpy(devp, t, tLen);
		// NOTE: We depend on _vcom_devinfo_findorcreate to further check that there's a number (and only a number) on the end
		_vcom_devinfo_findorcreate(devinfo_list, dev);
	}
}
#else
void _vcom_devinfo_scan_lsdev(struct lowlevel_device_info ** const devinfo_list)
{
	char dev[PATH_MAX];
	char *devp = dev;
	DIR *D;
	struct dirent *de;
	const char devdir[] = "/dev";
	const size_t devdirlen = sizeof(devdir) - 1;
	char *devpath = devp;
	char *devfile = devpath + devdirlen + 1;
	
	D = opendir(devdir);
	if (!D)
		applogr(, LOG_DEBUG, "No /dev directory to look for VCOM devices in");
	memcpy(devpath, devdir, devdirlen);
	devpath[devdirlen] = '/';
	while ( (de = readdir(D)) ) {
		if (!strncmp(de->d_name, "cu.", 3)
			//don't probe Bluetooth devices - causes bus errors and segfaults
			&& strncmp(de->d_name, "cu.Bluetooth", 12))
			goto trydev;
		if (strncmp(de->d_name, "tty", 3))
			continue;
		if (strncmp(&de->d_name[3], "USB", 3) && strncmp(&de->d_name[3], "ACM", 3))
			continue;
		
trydev:
		strcpy(devfile, de->d_name);
		_vcom_devinfo_findorcreate(devinfo_list, dev);
	}
	closedir(D);
}
#endif
#endif

static char *add_serial(const char *arg)
{
	string_elist_add(arg, &scan_devices);
	return NULL;
}

static
char *opt_string_elist_add(const char *arg, struct string_elist **elist)
{
	string_elist_add(arg, elist);
	return NULL;
}

bool get_intrange(const char *arg, int *val1, int *val2)
{
	// NOTE: This could be done with sscanf, but its %n is broken in strange ways on Windows
	char *p, *p2;
	
	*val1 = strtol(arg, &p, 0);
	if (arg == p)
		// Zero-length ending number, invalid
		return false;
	while (true)
	{
		if (!p[0])
		{
			*val2 = *val1;
			return true;
		}
		if (p[0] == '-')
			break;
		if (!isspace(p[0]))
			// Garbage, invalid
			return false;
		++p;
	}
	p2 = &p[1];
	*val2 = strtol(p2, &p, 0);
	if (p2 == p)
		// Zero-length ending number, invalid
		return false;
	while (true)
	{
		if (!p[0])
			return true;
		if (!isspace(p[0]))
			// Garbage, invalid
			return false;
		++p;
	}
}

static
void _test_intrange(const char *s, const int v[2])
{
	int a[2];
	if (!get_intrange(s, &a[0], &a[1]))
	{
		++unittest_failures;
		applog(LOG_ERR, "Test \"%s\" failed: returned false", s);
	}
	for (int i = 0; i < 2; ++i)
		if (unlikely(a[i] != v[i]))
		{
			++unittest_failures;
			applog(LOG_ERR, "Test \"%s\" failed: value %d should be %d but got %d", s, i, v[i], a[i]);
		}
}
#define _test_intrange(s, ...)  _test_intrange(s, (int[]){ __VA_ARGS__ })

static
void _test_intrange_fail(const char *s)
{
	int a[2];
	if (get_intrange(s, &a[0], &a[1]))
	{
		++unittest_failures;
		applog(LOG_ERR, "Test !\"%s\" failed: returned true with %d and %d", s, a[0], a[1]);
	}
}

static
void test_intrange()
{
	_test_intrange("-1--2", -1, -2);
	_test_intrange("-1-2", -1, 2);
	_test_intrange("1--2", 1, -2);
	_test_intrange("1-2", 1, 2);
	_test_intrange("111-222", 111, 222);
	_test_intrange(" 11 - 22 ", 11, 22);
	_test_intrange("+11-+22", 11, 22);
	_test_intrange("-1", -1, -1);
	_test_intrange_fail("all");
	_test_intrange_fail("1-");
	_test_intrange_fail("");
	_test_intrange_fail("1-54x");
}

static char *set_devices(char *arg)
{
	if (*arg) {
		if (*arg == '?') {
			opt_display_devs = true;
			return NULL;
		}
	} else
		return "Invalid device parameters";

	string_elist_add(arg, &opt_devices_enabled_list);

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

static
char *set_benchmark_intense()
{
	opt_benchmark = true;
	opt_benchmark_intense = true;
	return NULL;
}

/* Detect that url is for a stratum protocol either via the presence of
 * stratum+tcp or by detecting a stratum server response */
bool detect_stratum(struct pool *pool, char *url)
{
	if (!extract_sockaddr(url, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	if (!strncasecmp(url, "stratum+tcp://", 14)) {
		pool_set_uri(pool, strdup(url));
		pool->has_stratum = true;
		pool->stratum_url = pool->sockaddr_url;
		return true;
	}

	return false;
}

static struct pool *add_url(void)
{
	total_urls++;
	if (total_urls > total_pools)
		add_pool();
	return pools[total_urls - 1];
}

static void setup_url(struct pool *pool, char *arg)
{
	if (detect_stratum(pool, arg))
		return;

	opt_set_charp(arg, &pool->rpc_url);
	if (strncmp(arg, "http://", 7) &&
	    strncmp(arg, "https://", 8)) {
		const size_t L = strlen(arg);
		char *httpinput;

		httpinput = malloc(8 + L);
		if (!httpinput)
			quit(1, "Failed to malloc httpinput");
		sprintf(httpinput, "http://%s", arg);
		pool_set_uri(pool, httpinput);
	}
}

static char *set_url(char *arg)
{
	struct pool *pool = add_url();

	setup_url(pool, arg);
	return NULL;
}

static char *set_quota(char *arg)
{
	char *semicolon = strchr(arg, ';'), *url;
	int len, qlen, quota;
	struct pool *pool;

	if (!semicolon)
		return "No semicolon separated quota;URL pair found";
	len = strlen(arg);
	*semicolon = '\0';
	qlen = strlen(arg);
	if (!qlen)
		return "No parameter for quota found";
	len -= qlen + 1;
	if (len < 1)
		return "No parameter for URL found";
	quota = atoi(arg);
	if (quota < 0)
		return "Invalid negative parameter for quota set";
	url = arg + qlen + 1;
	pool = add_url();
	setup_url(pool, url);
	pool->quota = quota;
	applog(LOG_INFO, "Setting pool %d to quota %d", pool->pool_no, pool->quota);
	adjust_quota_gcd();

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
	pool->rpc_user = updup;
	pool->rpc_pass = strchr(updup, ':');
	if (pool->rpc_pass)
		pool->rpc_pass++[0] = '\0';
	else
		pool->rpc_pass = &updup[strlen(updup)];

	return NULL;
}

static char *set_cbcaddr(char *arg)
{
	struct pool *pool;
	char *p, *addr;
	bytes_t target_script = BYTES_INIT;
	
	if (!total_pools)
		return "Define pool first, then the --coinbase-check-addr list";
	
	pool = pools[total_pools - 1];
	
	/* NOTE: 'x' is a new prefix which leads both mainnet and testnet address, we would
	 * need support it later, but now leave the code just so.
	 *
	 * Regarding details of address prefix 'x', check the below URL:
	 * https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki#Serialization_format
	 */
	pool->cb_param.testnet = (arg[0] != '1' && arg[0] != '3' && arg[0] != 'x');
	
	for (; (addr = strtok_r(arg, ",", &p)); arg = NULL)
	{
		struct bytes_hashtbl *ah;
		
		if (set_b58addr(addr, &target_script))
			/* No bother to free memory since we are going to exit anyway */
			return "Invalid address in --coinbase-check-address list";
		
		HASH_FIND(hh, pool->cb_param.scripts, bytes_buf(&target_script), bytes_len(&target_script), ah);
		if (!ah)
		{
			/* Note: for the below allocated memory we have good way to release its memory
			 * since we can't be sure there are no reference to the pool struct when remove_pool() 
			 * get called.
			 *
			 * We just hope the remove_pool() would not be called many many times during
			 * the whole running life of this program.
			 */
			ah = malloc(sizeof(*ah));
			bytes_init(&ah->b);
			bytes_assimilate(&ah->b, &target_script);
			HASH_ADD(hh, pool->cb_param.scripts, b.buf, bytes_len(&ah->b), ah);
		}
	}
	bytes_free(&target_script);
	
	return NULL;
}

static char *set_cbctotal(const char *arg)
{
	struct pool *pool;
	
	if (!total_pools)
		return "Define pool first, then the --coinbase-check-total argument";
	
	pool = pools[total_pools - 1];
	pool->cb_param.total = atoll(arg);
	if (pool->cb_param.total < 0)
		return "The total payout amount in coinbase should be greater than 0";
	
	return NULL;
}

static char *set_cbcperc(const char *arg)
{
	struct pool *pool;
	
	if (!total_pools)
		return "Define pool first, then the --coinbase-check-percent argument";
	
	pool = pools[total_pools - 1];
	if (!pool->cb_param.scripts)
		return "Define --coinbase-check-addr list first, then the --coinbase-check-total argument";
	
	pool->cb_param.perc = atof(arg) / 100;
	if (pool->cb_param.perc < 0.0 || pool->cb_param.perc > 1.0)
		return "The percentage should be between 0 and 100";
	
	return NULL;
}

static
const char *goal_set(struct mining_goal_info * const goal, const char * const optname, const char * const newvalue, bytes_t * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	*out_success = SDR_ERR;
	if (!(strcasecmp(optname, "malgo") && strcasecmp(optname, "algo")))
	{
		if (!newvalue)
			return "Goal option 'malgo' requires a value (eg, SHA256d)";
		struct mining_algorithm * const new_malgo = mining_algorithm_by_alias(newvalue);
		if (!new_malgo)
			return "Unrecognised mining algorithm";
		goal_set_malgo(goal, new_malgo);
		goto success;
	}
#if BLKMAKER_VERSION > 1
	if (match_strtok("generate-to|generate-to-addr|generate-to-address|genaddress|genaddr|gen-address|gen-addr|generate-address|generate-addr|coinbase-addr|coinbase-address|coinbase-payout|cbaddress|cbaddr|cb-address|cb-addr|payout", "|", optname))
	{
		if (!newvalue)
			return "Missing value for 'generate-to' goal option";
		const char * const emsg = set_generate_addr2(goal, newvalue);
		if (emsg)
			return emsg;
		goto success;
	}
#endif
	*out_success = SDR_UNKNOWN;
	return "Unknown goal option";

success:
	*out_success = SDR_OK;
	return NULL;
}

// May leak replybuf if returning an error
static
const char *set_goal_params(struct mining_goal_info * const goal, char *arg)
{
	bytes_t replybuf = BYTES_INIT;
	for (char *param, *nextptr; (param = strtok_r(arg, ",", &nextptr)); arg = NULL)
	{
		char *val = strchr(param, '=');
		if (val)
			val++[0] = '\0';
		enum bfg_set_device_replytype success;
		const char * const emsg = goal_set(goal, param, val, &replybuf, &success);
		if (success != SDR_OK)
			return emsg ?: "Error setting goal param";
	}
	bytes_free(&replybuf);
	return NULL;
}

static
const char *set_pool_goal(const char * const arg)
{
	struct pool *pool;
	
	if (!total_pools)
		return "Usage of --pool-goal before pools are defined does not make sense";
	
	pool = pools[total_pools - 1];
	char *param = strchr(arg, ':');
	if (param)
		param++[0] = '\0';
	pool->goal = get_mining_goal(arg);
	
	if (param)
		return set_goal_params(pool->goal, param);
	
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

static
char *set_log_file(char *arg)
{
	char *r = "";
	long int i = strtol(arg, &r, 10);
	int fd, stderr_fd = fileno(stderr);

	if ((!*r) && i >= 0 && i <= INT_MAX)
		fd = i;
	else
	if (!strcmp(arg, "-"))
	{
		fd = fileno(stdout);
		if (unlikely(fd == -1))
			return "Standard output missing for log-file";
	}
	else
	{
		fd = open(arg, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
		if (unlikely(fd == -1))
			return "Failed to open log-file";
	}
	
	close(stderr_fd);
	if (unlikely(-1 == dup2(fd, stderr_fd)))
		return "Failed to dup2 for log-file";
	close(fd);
	
	return NULL;
}

static
char *_bfgopt_set_file(const char *arg, FILE **F, const char *mode, const char *purpose)
{
	char *r = "";
	long int i = strtol(arg, &r, 10);
	static char *err = NULL;
	const size_t errbufsz = 0x100;

	free(err);
	err = NULL;
	
	if ((!*r) && i >= 0 && i <= INT_MAX) {
		*F = fdopen((int)i, mode);
		if (!*F)
		{
			err = malloc(errbufsz);
			snprintf(err, errbufsz, "Failed to open fd %d for %s",
			         (int)i, purpose);
			return err;
		}
	} else if (!strcmp(arg, "-")) {
		*F = (mode[0] == 'a') ? stdout : stdin;
		if (!*F)
		{
			err = malloc(errbufsz);
			snprintf(err, errbufsz, "Standard %sput missing for %s",
			         (mode[0] == 'a') ? "out" : "in", purpose);
			return err;
		}
	} else {
		*F = fopen(arg, mode);
		if (!*F)
		{
			err = malloc(errbufsz);
			snprintf(err, errbufsz, "Failed to open %s for %s",
			         arg, purpose);
			return err;
		}
	}

	return NULL;
}

static char *set_noncelog(char *arg)
{
	return _bfgopt_set_file(arg, &noncelog_file, "a", "nonce log");
}

static char *set_sharelog(char *arg)
{
	return _bfgopt_set_file(arg, &sharelog_file, "a", "share log");
}

static
void _add_set_device_option(const char * const func, const char * const buf)
{
	applog(LOG_DEBUG, "%s: Using --set-device %s", func, buf);
	string_elist_add(buf, &opt_set_device_list);
}

#define add_set_device_option(...)  do{  \
	char _tmp1718[0x100];  \
	snprintf(_tmp1718, sizeof(_tmp1718), __VA_ARGS__);  \
	_add_set_device_option(__func__, _tmp1718);  \
}while(0)

char *set_temp_cutoff(char *arg)
{
	if (strchr(arg, ','))
		return "temp-cutoff no longer supports comma-delimited syntax, use --set-device for better control";
	applog(LOG_WARNING, "temp-cutoff is deprecated! Use --set-device for better control");
	
	add_set_device_option("all:temp-cutoff=%s", arg);
	
	return NULL;
}

char *set_temp_target(char *arg)
{
	if (strchr(arg, ','))
		return "temp-target no longer supports comma-delimited syntax, use --set-device for better control";
	applog(LOG_WARNING, "temp-target is deprecated! Use --set-device for better control");
	
	add_set_device_option("all:temp-target=%s", arg);
	
	return NULL;
}

#ifdef USE_OPENCL
static
char *set_no_opencl_binaries(__maybe_unused void * const dummy)
{
	applog(LOG_WARNING, "The --no-opencl-binaries option is deprecated! Use --set-device OCL:binary=no");
	add_set_device_option("OCL:binary=no");
	return NULL;
}
#endif

static
char *disable_pool_redirect(__maybe_unused void * const dummy)
{
	opt_disable_client_reconnect = true;
	want_stratum = false;
	return NULL;
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

static char *set_api_mcast_des(const char *arg)
{
	opt_set_charp(arg, &opt_api_mcast_des);

	return NULL;
}

#ifdef USE_ICARUS
extern const struct bfg_set_device_definition icarus_set_device_funcs[];

static char *set_icarus_options(const char *arg)
{
	if (strchr(arg, ','))
		return "icarus-options no longer supports comma-delimited syntax, see README.FPGA for better control";
	applog(LOG_WARNING, "icarus-options is deprecated! See README.FPGA for better control");
	
	char *opts = strdup(arg), *argdup;
	argdup = opts;
	const struct bfg_set_device_definition *sdf = icarus_set_device_funcs;
	const char *drivers[] = {"antminer", "cairnsmore", "erupter", "icarus"};
	char *saveptr, *opt;
	for (int i = 0; i < 4; ++i, ++sdf)
	{
		opt = strtok_r(opts, ":", &saveptr);
		opts = NULL;
		
		if (!opt)
			break;
		
		if (!opt[0])
			continue;
		
		for (int j = 0; j < 4; ++j)
			add_set_device_option("%s:%s=%s", drivers[j], sdf->optname, opt);
	}
	free(argdup);
	return NULL;
}

static char *set_icarus_timing(const char *arg)
{
	if (strchr(arg, ','))
		return "icarus-timing no longer supports comma-delimited syntax, see README.FPGA for better control";
	applog(LOG_WARNING, "icarus-timing is deprecated! See README.FPGA for better control");
	
	const char *drivers[] = {"antminer", "cairnsmore", "erupter", "icarus"};
	for (int j = 0; j < 4; ++j)
		add_set_device_option("%s:timing=%s", drivers[j], arg);
	return NULL;
}
#endif

#ifdef USE_AVALON
extern const struct bfg_set_device_definition avalon_set_device_funcs[];

static char *set_avalon_options(const char *arg)
{
	if (strchr(arg, ','))
		return "avalon-options no longer supports comma-delimited syntax, see README.FPGA for better control";
	applog(LOG_WARNING, "avalon-options is deprecated! See README.FPGA for better control");
	
	char *opts = strdup(arg), *argdup;
	argdup = opts;
	const struct bfg_set_device_definition *sdf = avalon_set_device_funcs;
	char *saveptr, *opt;
	for (int i = 0; i < 5; ++i, ++sdf)
	{
		opt = strtok_r(opts, ":", &saveptr);
		opts = NULL;
		
		if (!opt)
			break;
		
		if (!opt[0])
			continue;
		
		add_set_device_option("avalon:%s=%s", sdf->optname, opt);
	}
	free(argdup);
	return NULL;
}
#endif

#ifdef USE_KLONDIKE
static char *set_klondike_options(const char *arg)
{
	int hashclock;
	double temptarget;
	switch (sscanf(arg, "%d:%lf", &hashclock, &temptarget))
	{
		default:
			return "Unrecognised --klondike-options";
		case 2:
			add_set_device_option("klondike:temp-target=%lf", temptarget);
			// fallthru
		case 1:
			add_set_device_option("klondike:clock=%d", hashclock);
	}
	applog(LOG_WARNING, "klondike-options is deprecated! Use --set-device for better control");
	
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
#ifdef USE_CPUMINING
#ifdef USE_SHA256D
	OPT_WITH_ARG("--algo",
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
	OPT_WITH_ARG("-a",
	             set_algo, show_algo, &opt_algo,
	             opt_hidden),
#else
	// NOTE: Silently ignoring option, since it is plausable a non-SHA256d miner was using it just to skip benchmarking
	OPT_WITH_ARG("--algo|-a", arg_ignored, NULL, NULL, opt_hidden),
#endif  /* USE_SHA256D */
#endif  /* USE_CPUMINING */
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
	OPT_WITHOUT_ARG("--api-mcast",
			opt_set_bool, &opt_api_mcast,
			"Enable API Multicast listener, default: disabled"),
	OPT_WITH_ARG("--api-mcast-addr",
		     opt_set_charp, opt_show_charp, &opt_api_mcast_addr,
		     "API Multicast listen address"),
	OPT_WITH_ARG("--api-mcast-code",
		     opt_set_charp, opt_show_charp, &opt_api_mcast_code,
		     "Code expected in the API Multicast message, don't use '-'"),
	OPT_WITH_ARG("--api-mcast-des",
		     set_api_mcast_des, NULL, NULL,
		     "Description appended to the API Multicast reply, default: ''"),
	OPT_WITH_ARG("--api-mcast-port",
		     set_int_1_to_65535, opt_show_intval, &opt_api_mcast_port,
		     "API Multicast listen port"),
	OPT_WITHOUT_ARG("--api-network",
			opt_set_bool, &opt_api_network,
			"Allow API (if enabled) to listen on/for any address, default: only 127.0.0.1"),
	OPT_WITH_ARG("--api-port",
		     set_int_1_to_65535, opt_show_intval, &opt_api_port,
		     "Port number of miner API"),
#ifdef HAVE_ADL
	OPT_WITHOUT_ARG("--auto-fan",
			opt_set_bool, &opt_autofan,
			opt_hidden),
	OPT_WITHOUT_ARG("--auto-gpu",
			opt_set_bool, &opt_autoengine,
			opt_hidden),
#endif
	OPT_WITHOUT_ARG("--balance",
		     set_balance, &pool_strategy,
		     "Change multipool strategy from failover to even share balance"),
	OPT_WITHOUT_ARG("--benchmark",
			opt_set_bool, &opt_benchmark,
			"Run BFGMiner in benchmark mode - produces no shares"),
	OPT_WITHOUT_ARG("--benchmark-intense",
			set_benchmark_intense, &opt_benchmark_intense,
			"Run BFGMiner in intensive benchmark mode - produces no shares"),
#if defined(USE_BITFORCE)
	OPT_WITHOUT_ARG("--bfl-range",
			opt_set_bool, &opt_bfl_noncerange,
			"Use nonce range on bitforce devices if supported"),
#endif
#ifdef HAVE_CHROOT
        OPT_WITH_ARG("--chroot-dir",
                     opt_set_charp, NULL, &chroot_dir,
                     "Chroot to a directory right after startup"),
#endif
	OPT_WITH_ARG("--cmd-idle",
	             opt_set_charp, NULL, &cmd_idle,
	             "Execute a command when a device is allowed to be idle (rest or wait)"),
	OPT_WITH_ARG("--cmd-sick",
	             opt_set_charp, NULL, &cmd_sick,
	             "Execute a command when a device is declared sick"),
	OPT_WITH_ARG("--cmd-dead",
	             opt_set_charp, NULL, &cmd_dead,
	             "Execute a command when a device is declared dead"),
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
#ifdef USE_CPUMINING
	OPT_WITH_ARG("--cpu-threads",
		     force_nthreads_int, opt_show_intval, &opt_n_threads,
		     "Number of miner CPU threads"),
	OPT_WITH_ARG("-t",
	             force_nthreads_int, opt_show_intval, &opt_n_threads,
	             opt_hidden),
#endif
	OPT_WITHOUT_ARG("--debug|-D",
		     enable_debug, &opt_debug,
		     "Enable debug output"),
	OPT_WITHOUT_ARG("--debuglog",
		     opt_set_bool, &opt_debug,
		     "Enable debug logging"),
	OPT_WITHOUT_ARG("--device-protocol-dump",
			opt_set_bool, &opt_dev_protocol,
			"Verbose dump of device protocol-level activities"),
	OPT_WITH_ARG("--device|-d",
		     set_devices, NULL, NULL,
	             "Enable only devices matching pattern (default: all)"),
	OPT_WITHOUT_ARG("--disable-rejecting",
			opt_set_bool, &opt_disable_pool,
			"Automatically disable pools that continually reject shares"),
#ifdef USE_LIBMICROHTTPD
	OPT_WITH_ARG("--http-port",
	             opt_set_intval, opt_show_intval, &httpsrv_port,
	             "Port number to listen on for HTTP getwork miners (-1 means disabled)"),
#endif
	OPT_WITH_ARG("--expiry",
		     set_int_0_to_9999, opt_show_intval, &opt_expiry,
		     "Upper bound on how many seconds after getting work we consider a share from it stale (w/o longpoll active)"),
	OPT_WITH_ARG("-E",
	             set_int_0_to_9999, opt_show_intval, &opt_expiry,
	             opt_hidden),
	OPT_WITH_ARG("--expiry-lp",
		     set_int_0_to_9999, opt_show_intval, &opt_expiry_lp,
		     "Upper bound on how many seconds after getting work we consider a share from it stale (with longpoll active)"),
	OPT_WITHOUT_ARG("--failover-only",
			opt_set_bool, &opt_fail_only,
			"Don't leak work to backup pools when primary pool is lagging"),
	OPT_WITH_ARG("--failover-switch-delay",
			set_int_1_to_65535, opt_show_intval, &opt_fail_switch_delay,
			"Delay in seconds before switching back to a failed pool"),
#ifdef USE_FPGA
	OPT_WITHOUT_ARG("--force-dev-init",
	        opt_set_bool, &opt_force_dev_init,
	        "Always initialize devices when possible (such as bitstream uploads to some FPGAs)"),
#endif
#if BLKMAKER_VERSION > 1
	OPT_WITH_ARG("--generate-to",
	             set_generate_addr, NULL, NULL,
	             "Set an address to generate to for solo mining"),
	OPT_WITH_ARG("--generate-to-addr|--generate-to-address|--genaddress|--genaddr|--gen-address|--gen-addr|--generate-address|--generate-addr|--coinbase-addr|--coinbase-address|--coinbase-payout|--cbaddress|--cbaddr|--cb-address|--cb-addr|--payout",
	             set_generate_addr, NULL, NULL,
	             opt_hidden),
#endif
#ifdef USE_OPENCL
	OPT_WITH_ARG("--gpu-dyninterval",
		     set_int_1_to_65535, opt_show_intval, &opt_dynamic_interval,
		     opt_hidden),
	OPT_WITH_ARG("--gpu-platform",
		     set_int_0_to_9999, opt_show_intval, &opt_platform_id,
		     "Select OpenCL platform ID to use for GPU mining"),
	OPT_WITH_ARG("--gpu-threads|-g",
	             set_gpu_threads, opt_show_intval, &opt_g_threads,
	             opt_hidden),
#ifdef HAVE_ADL
	OPT_WITH_ARG("--gpu-engine",
		     set_gpu_engine, NULL, NULL,
	             opt_hidden),
	OPT_WITH_ARG("--gpu-fan",
		     set_gpu_fan, NULL, NULL,
	             opt_hidden),
	OPT_WITH_ARG("--gpu-map",
		     set_gpu_map, NULL, NULL,
		     "Map OpenCL to ADL device order manually, paired CSV (e.g. 1:0,2:1 maps OpenCL 1 to ADL 0, 2 to 1)"),
	OPT_WITH_ARG("--gpu-memclock",
		     set_gpu_memclock, NULL, NULL,
	             opt_hidden),
	OPT_WITH_ARG("--gpu-memdiff",
		     set_gpu_memdiff, NULL, NULL,
	             opt_hidden),
	OPT_WITH_ARG("--gpu-powertune",
		     set_gpu_powertune, NULL, NULL,
	             opt_hidden),
	OPT_WITHOUT_ARG("--gpu-reorder",
			opt_set_bool, &opt_reorder,
			"Attempt to reorder GPU devices according to PCI Bus ID"),
	OPT_WITH_ARG("--gpu-vddc",
		     set_gpu_vddc, NULL, NULL,
	             opt_hidden),
#endif
#ifdef USE_SCRYPT
	OPT_WITH_ARG("--lookup-gap",
		     set_lookup_gap, NULL, NULL,
	             opt_hidden),
#endif
	OPT_WITH_ARG("--intensity|-I",
	             set_intensity, NULL, NULL,
	             opt_hidden),
#endif
#if defined(USE_OPENCL) || defined(USE_MODMINER) || defined(USE_X6500) || defined(USE_ZTEX)
	OPT_WITH_ARG("--kernel-path",
		     opt_set_charp, opt_show_charp, &opt_kernel_path,
	             "Specify a path to where bitstream and kernel files are"),
	OPT_WITH_ARG("-K",
	             opt_set_charp, opt_show_charp, &opt_kernel_path,
	             opt_hidden),
#endif
#ifdef USE_OPENCL
	OPT_WITH_ARG("--kernel|-k",
	             set_kernel, NULL, NULL,
	             opt_hidden),
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
#ifdef USE_KLONDIKE
	OPT_WITH_ARG("--klondike-options",
		     set_klondike_options, NULL, NULL,
		     "Set klondike options clock:temptarget"),
#endif
	OPT_WITHOUT_ARG("--load-balance",
		     set_loadbalance, &pool_strategy,
		     "Change multipool strategy from failover to quota based balance"),
	OPT_WITH_ARG("--log|-l",
		     set_int_0_to_9999, opt_show_intval, &opt_log_interval,
		     "Interval in seconds between log output"),
	OPT_WITH_ARG("--log-file|-L",
	             set_log_file, NULL, NULL,
	             "Append log file for output messages"),
	OPT_WITH_ARG("--logfile",
	             set_log_file, NULL, NULL,
	             opt_hidden),
	OPT_WITHOUT_ARG("--log-microseconds",
	                opt_set_bool, &opt_log_microseconds,
	                "Include microseconds in log output"),
#if defined(unix) || defined(__APPLE__)
	OPT_WITH_ARG("--monitor|-m",
		     opt_set_charp, NULL, &opt_stderr_cmd,
		     "Use custom pipe cmd for output messages"),
#endif // defined(unix)
	OPT_WITHOUT_ARG("--net-delay",
			opt_set_bool, &opt_delaynet,
			"Impose small delays in networking to avoid overloading slow routers"),
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
	OPT_WITHOUT_ARG("--no-hotplug",
#ifdef HAVE_BFG_HOTPLUG
	                opt_set_invbool, &opt_hotplug,
	                "Disable hotplug detection"
#else
	                set_null, &opt_hotplug,
	                opt_hidden
#endif
	),
	OPT_WITHOUT_ARG("--no-local-bitcoin",
#if BLKMAKER_VERSION > 1
	                opt_set_invbool, &opt_load_bitcoin_conf,
	                "Disable adding pools for local bitcoin RPC servers"),
#else
	                set_null, NULL, opt_hidden),
#endif
	OPT_WITHOUT_ARG("--no-longpoll",
			opt_set_invbool, &want_longpoll,
			"Disable X-Long-Polling support"),
	OPT_WITHOUT_ARG("--no-pool-disable",
			opt_set_invbool, &opt_disable_pool,
			opt_hidden),
	OPT_WITHOUT_ARG("--no-client-reconnect",
			opt_set_invbool, &opt_disable_client_reconnect,
			opt_hidden),
	OPT_WITHOUT_ARG("--no-pool-redirect",
			disable_pool_redirect, NULL,
			"Ignore pool requests to redirect to another server"),
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
#ifdef USE_OPENCL
	OPT_WITHOUT_ARG("--no-opencl-binaries",
	                set_no_opencl_binaries, NULL,
	                opt_hidden),
#endif
	OPT_WITHOUT_ARG("--no-unicode",
#ifdef USE_UNICODE
	                opt_set_invbool, &use_unicode,
	                "Don't use Unicode characters in TUI"
#else
	                set_null, &use_unicode,
	                opt_hidden
#endif
	),
	OPT_WITH_ARG("--noncelog",
		     set_noncelog, NULL, NULL,
		     "Create log of all nonces found"),
	OPT_WITH_ARG("--pass|-p",
		     set_pass, NULL, NULL,
		     "Password for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--per-device-stats",
			opt_set_bool, &want_per_device_stats,
			"Force verbose mode and output per-device statistics"),
	OPT_WITH_ARG("--userpass|-O",
	             set_userpass, NULL, NULL,
	             "Username:Password pair for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--pool-goal",
			 set_pool_goal, NULL, NULL,
			 "Named goal for the previous-defined pool"),
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
	OPT_WITH_ARG("--quit-summary",
	             set_quit_summary, NULL, NULL,
	             "Summary printed when you quit: none/devs/procs/detailed"),
	OPT_WITH_ARG("--quota|-U",
		     set_quota, NULL, NULL,
		     "quota;URL combination for server with load-balance strategy quotas"),
	OPT_WITHOUT_ARG("--real-quiet",
			opt_set_bool, &opt_realquiet,
			"Disable all output"),
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
	OPT_WITH_ARG("--scan|-S",
		     add_serial, NULL, NULL,
		     "Configure how to scan for mining devices"),
	OPT_WITH_ARG("--scan-device|--scan-serial|--devscan",
		     add_serial, NULL, NULL,
		     opt_hidden),
	OPT_WITH_ARG("--scan-time",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     "Upper bound on time spent scanning current work, in seconds"),
	OPT_WITH_ARG("-s",
		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
		     opt_hidden),
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
	                set_malgo_scrypt, NULL,
			"Use the scrypt algorithm for mining (non-bitcoin)"),
#endif
	OPT_WITH_ARG("--set-device|--set",
			opt_string_elist_add, NULL, &opt_set_device_list,
			"Set default parameters on devices; eg"
			", NFY:osc6_bits=50"
			", bfl:voltage=<value>"
			", compac:clock=<value>"
	),

#if defined(USE_SCRYPT) && defined(USE_OPENCL)
	OPT_WITH_ARG("--shaders",
		     set_shaders, NULL, NULL,
	             opt_hidden),
#endif
#ifdef HAVE_PWD_H
        OPT_WITH_ARG("--setuid",
                     opt_set_charp, NULL, &opt_setuid,
                     "Username of an unprivileged user to run as"),
#endif
	OPT_WITH_ARG("--sharelog",
		     set_sharelog, NULL, NULL,
		     "Append share log to file"),
	OPT_WITH_ARG("--shares",
		     opt_set_floatval, NULL, &opt_shares,
		     "Quit after mining 2^32 * N hashes worth of shares (default: unlimited)"),
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
		     "Set socks proxy (host:port)"),
#ifdef USE_LIBEVENT
	OPT_WITH_ARG("--stratum-port",
	             set_long_1_to_65535_or_neg1, opt_show_longval, &stratumsrv_port,
	             "Port number to listen on for stratum miners (-1 means disabled)"),
#endif
	OPT_WITHOUT_ARG("--submit-stale",
			opt_set_bool, &opt_submit_stale,
	                opt_hidden),
	OPT_WITH_ARG("--submit-threads",
	                opt_set_intval, opt_show_intval, &opt_submit_threads,
	                "Minimum number of concurrent share submissions (default: 64)"),
#ifdef HAVE_SYSLOG_H
	OPT_WITHOUT_ARG("--syslog",
			opt_set_bool, &use_syslog,
			"Use system log for output messages (default: standard error)"),
#endif
	OPT_WITH_ARG("--temp-cutoff",
		     set_temp_cutoff, NULL, &opt_cutofftemp,
		     opt_hidden),
	OPT_WITH_ARG("--temp-hysteresis",
		     set_int_1_to_10, opt_show_intval, &opt_hysteresis,
		     "Set how much the temperature can fluctuate outside limits when automanaging speeds"),
#ifdef HAVE_ADL
	OPT_WITH_ARG("--temp-overheat",
		     set_temp_overheat, opt_show_intval, &opt_overheattemp,
	             opt_hidden),
#endif
	OPT_WITH_ARG("--temp-target",
		     set_temp_target, NULL, NULL,
		     opt_hidden),
	OPT_WITHOUT_ARG("--text-only|-T",
			opt_set_invbool, &use_curses,
#ifdef HAVE_CURSES
			"Disable ncurses formatted screen output"
#else
			opt_hidden
#endif
	),
#if defined(USE_SCRYPT) && defined(USE_OPENCL)
	OPT_WITH_ARG("--thread-concurrency",
		     set_thread_concurrency, NULL, NULL,
	             opt_hidden),
#endif
#ifdef USE_UNICODE
	OPT_WITHOUT_ARG("--unicode",
	                opt_set_bool, &use_unicode,
	                "Use Unicode characters in TUI"),
#endif
	OPT_WITH_ARG("--url|-o",
		     set_url, NULL, NULL,
		     "URL for bitcoin JSON-RPC server"),
	OPT_WITH_ARG("--user|-u",
		     set_user, NULL, NULL,
		     "Username for bitcoin JSON-RPC server"),
#ifdef USE_OPENCL
	OPT_WITH_ARG("--vectors|-v",
	             set_vector, NULL, NULL,
	             opt_hidden),
#endif
	OPT_WITHOUT_ARG("--verbose",
			opt_set_bool, &opt_log_output,
			"Log verbose output to stderr as well as status output"),
	OPT_WITHOUT_ARG("--verbose-work-updates|--verbose-work-update",
			opt_set_invbool, &opt_quiet_work_updates,
			opt_hidden),
	OPT_WITHOUT_ARG("--weighed-stats",
	                opt_set_bool, &opt_weighed_stats,
	                "Display statistics weighed to difficulty 1"),
#ifdef USE_OPENCL
	OPT_WITH_ARG("--worksize|-w",
	             set_worksize, NULL, NULL,
	             opt_hidden),
#endif
	OPT_WITHOUT_ARG("--unittest",
			opt_set_bool, &opt_unittest, opt_hidden),
	OPT_WITH_ARG("--coinbase-check-addr",
			set_cbcaddr, NULL, NULL,
			"A list of address to check against in coinbase payout list received from the previous-defined pool, separated by ','"),
	OPT_WITH_ARG("--cbcheck-addr|--cbc-addr|--cbcaddr",
			set_cbcaddr, NULL, NULL,
			opt_hidden),
	OPT_WITH_ARG("--coinbase-check-total",
			set_cbctotal, NULL, NULL,
			"The least total payout amount expected in coinbase received from the previous-defined pool"),
	OPT_WITH_ARG("--cbcheck-total|--cbc-total|--cbctotal",
			set_cbctotal, NULL, NULL,
			opt_hidden),
	OPT_WITH_ARG("--coinbase-check-percent",
			set_cbcperc, NULL, NULL,
			"The least benefit percentage expected for the sum of addr(s) listed in --cbaddr argument for previous-defined pool"),
	OPT_WITH_ARG("--cbcheck-percent|--cbc-percent|--cbcpercent|--cbcperc",
			set_cbcperc, NULL, NULL,
			opt_hidden),
	OPT_WITHOUT_ARG("--worktime",
			opt_set_bool, &opt_worktime,
			"Display extra work time debug information"),
	OPT_WITH_ARG("--pools",
			opt_set_bool, NULL, NULL, opt_hidden),
	OPT_ENDTABLE
};

static char *load_config(const char *arg, void __maybe_unused *unused);

static char *parse_config(json_t *config, bool fileconf, int * const fileconf_load_p)
{
	static char err_buf[200];
	struct opt_table *opt;
	json_t *val;

	if (fileconf && !*fileconf_load_p)
		*fileconf_load_p = 1;

	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name, *sp;

		/* We don't handle subtables. */
		assert(!(opt->type & OPT_SUBTABLE));

		if (!opt->names)
			continue;

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
						err = parse_config(json_array_get(val, n), false, fileconf_load_p);
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
					*fileconf_load_p = -1;
				} else {
					snprintf(err_buf, sizeof(err_buf), "Parsing JSON option %s: %s",
						p, err);
					free(name);
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

struct bfg_loaded_configfile *bfg_loaded_configfiles;

char conf_web1[] = "http://";
char conf_web2[] = "https://";

static char *load_web_config(const char *arg)
{
	json_t *val;
	CURL *curl;
	struct bfg_loaded_configfile *cfginfo;

	curl = curl_easy_init();
	if (unlikely(!curl))
		quithere(1, "CURL initialisation failed");

	val = json_web_config(curl, arg);

	curl_easy_cleanup(curl);

	if (!val || !json_is_object(val))
		return JSON_WEB_ERROR;

	cfginfo = malloc(sizeof(*cfginfo));
	*cfginfo = (struct bfg_loaded_configfile){
		.filename = strdup(arg),
	};
	LL_APPEND(bfg_loaded_configfiles, cfginfo);

	config_loaded = true;

	return parse_config(val, true, &cfginfo->fileconf_load);
}

static char *load_config(const char *arg, void __maybe_unused *unused)
{
	json_error_t err;
	json_t *config;
	char *json_error;
	size_t siz;
	struct bfg_loaded_configfile *cfginfo;

	if (strncasecmp(arg, conf_web1, sizeof(conf_web1)-1) == 0 ||
	    strncasecmp(arg, conf_web2, sizeof(conf_web2)-1) == 0)
		return load_web_config(arg);

	cfginfo = malloc(sizeof(*cfginfo));
	*cfginfo = (struct bfg_loaded_configfile){
		.filename = strdup(arg),
	};
	LL_APPEND(bfg_loaded_configfiles, cfginfo);

	if (++include_count > JSON_MAX_DEPTH)
		return JSON_MAX_DEPTH_ERR;

#if JANSSON_MAJOR_VERSION > 1
	config = json_load_file(arg, 0, &err);
#else
	config = json_load_file(arg, &err);
#endif
	if (!json_is_object(config)) {
		siz = JSON_LOAD_ERROR_LEN + strlen(arg) + strlen(err.text);
		json_error = malloc(siz);
		if (!json_error)
			quit(1, "Malloc failure in json error");

		snprintf(json_error, siz, JSON_LOAD_ERROR, arg, err.text);
		return json_error;
	}

	config_loaded = true;

	/* Parse the config now, so we can override it.  That can keep pointers
	 * so don't free config object. */
	return parse_config(config, true, &cfginfo->fileconf_load);
}

static
bool _load_default_configs(const char * const filepath, void * __maybe_unused userp)
{
	bool * const found_defcfg_p = userp;
	*found_defcfg_p = true;
	
	load_config(filepath, NULL);
	
	// Regardless of status of loading the config file, we should continue loading other defaults
	return false;
}

static void load_default_config(void)
{
	bool found_defcfg = false;
	appdata_file_call("BFGMiner", def_conf, _load_default_configs, &found_defcfg);
	
	if (!found_defcfg)
	{
		// No BFGMiner config, try Cgminer's...
		appdata_file_call("cgminer", "cgminer.conf", _load_default_configs, &found_defcfg);
	}
}

extern const char *opt_argv0;

static
void bfg_versioninfo(void)
{
	puts(packagename);
	printf("  Lowlevel:%s\n", BFG_LOWLLIST);
	printf("  Drivers:%s\n", BFG_DRIVERLIST);
	printf("  Algorithms:%s\n", BFG_ALGOLIST);
	printf("  Options:%s\n", BFG_OPTLIST);
}

static char *opt_verusage_and_exit(const char *extra)
{
	bfg_versioninfo();
	printf("%s", opt_usage(opt_argv0, extra));
	fflush(stdout);
	exit(0);
}

static
const char *my_opt_version_and_exit(void)
{
	bfg_versioninfo();
	fflush(stdout);
	exit(0);
}

/* These options are parsed before anything else */
static struct opt_table opt_early_table[] = {
	// Default config is loaded in command line order, like a regular config
	OPT_EARLY_WITH_ARG("--config|-c|--default-config",
	                   set_bool_ignore_arg, NULL, &config_loaded,
	                   opt_hidden),
	OPT_EARLY_WITHOUT_ARG("--no-config|--no-default-config",
	                opt_set_bool, &config_loaded,
	                "Inhibit loading default config file"),
	OPT_ENDTABLE
};

/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
	OPT_WITH_ARG("--config|-c",
		     load_config, NULL, NULL,
		     "Load a JSON-format configuration file\n"
		     "See example.conf for an example configuration."),
	OPT_EARLY_WITHOUT_ARG("--no-config",
	                opt_set_bool, &config_loaded,
	                opt_hidden),
	OPT_EARLY_WITHOUT_ARG("--no-default-config",
	                opt_set_bool, &config_loaded,
	                "Inhibit loading default config file"),
	OPT_WITHOUT_ARG("--default-config",
	                load_default_config, NULL,
	                "Always load the default config file"),
	OPT_WITHOUT_ARG("--help|-h",
			opt_verusage_and_exit, NULL,
			"Print this message"),
#ifdef USE_OPENCL
	OPT_WITHOUT_ARG("--ndevs|-n",
			print_ndevs_and_exit, &nDevs,
			opt_hidden),
#endif
	OPT_WITHOUT_ARG("--version|-V",
			my_opt_version_and_exit, NULL,
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
	sha256_ctx ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, data.c, 64);
	memcpy(work->midstate, ctx.h, sizeof(work->midstate));
	swap32tole(work->midstate, work->midstate, 8);
}

static
struct bfg_tmpl_ref *tmpl_makeref(blktemplate_t * const tmpl)
{
	struct bfg_tmpl_ref * const tr = malloc(sizeof(*tr));
	*tr = (struct bfg_tmpl_ref){
		.tmpl = tmpl,
		.refcount = 1,
	};
	mutex_init(&tr->mutex);
	return tr;
}

static
void tmpl_incref(struct bfg_tmpl_ref * const tr)
{
	mutex_lock(&tr->mutex);
	++tr->refcount;
	mutex_unlock(&tr->mutex);
}

void tmpl_decref(struct bfg_tmpl_ref * const tr)
{
	mutex_lock(&tr->mutex);
	bool free_tmpl = !--tr->refcount;
	mutex_unlock(&tr->mutex);
	if (free_tmpl)
	{
		blktmpl_free(tr->tmpl);
		mutex_destroy(&tr->mutex);
		free(tr);
	}
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
	bytes_free(&work->nonce2);
	free(work->nonce1);
	if (work->device_data_free_func)
		work->device_data_free_func(work);

	if (work->tr)
		tmpl_decref(work->tr);

	memset(work, 0, sizeof(struct work));
}

/* All dynamically allocated work structs should be freed here to not leak any
 * ram from arrays allocated within the work struct */
void free_work(struct work *work)
{
	clean_work(work);
	free(work);
}

const char *bfg_workpadding_bin = "\0\0\0\x80\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x80\x02\0\0";
#define workpadding_bin  bfg_workpadding_bin

static const size_t block_info_str_sz = 3 /* ... */ + 16 /* block hash segment */ + 1;

static
void block_info_str(char * const out, const struct block_info * const blkinfo)
{
	unsigned char hash_swap[32];
	swap256(hash_swap, blkinfo->prevblkhash);
	swap32tole(hash_swap, hash_swap, 32 / 4);
	
	memset(out, '.', 3);
	// FIXME: The block number will overflow this sometime around AD 2025-2027
	if (blkinfo->height > 0 && blkinfo->height < 1000000)
	{
		bin2hex(&out[3], &hash_swap[0x1c], 4);
		snprintf(&out[11], block_info_str_sz-11, " #%6u", blkinfo->height);
	}
	else
		bin2hex(&out[3], &hash_swap[0x18], 8);
}

#ifdef HAVE_CURSES
static void update_block_display(bool);
#endif

// Must only be called with ch_lock held!
static
void __update_block_title(struct mining_goal_info * const goal)
{
	struct blockchain_info * const blkchain = goal->blkchain;
	
	if (!goal->current_goal_detail)
		goal->current_goal_detail = malloc(block_info_str_sz);
	block_info_str(goal->current_goal_detail, blkchain->currentblk);
#ifdef HAVE_CURSES
	update_block_display(false);
#endif
}

static struct block_info *block_exists(const struct blockchain_info *, const void *);

static
void have_block_height(struct mining_goal_info * const goal, const void * const prevblkhash, uint32_t blkheight)
{
	struct blockchain_info * const blkchain = goal->blkchain;
	struct block_info * const blkinfo = block_exists(blkchain, prevblkhash);
	if ((!blkinfo) || blkinfo->height)
		return;
	
	uint32_t block_id = ((uint32_t*)prevblkhash)[0];
	applog(LOG_DEBUG, "Learned that block id %08" PRIx32 " is height %" PRIu32, (uint32_t)be32toh(block_id), blkheight);
	cg_wlock(&ch_lock);
	blkinfo->height = blkheight;
	if (blkinfo == blkchain->currentblk)
	{
		blkchain->currentblk_subsidy = 5000000000LL >> (blkheight / 210000);
		__update_block_title(goal);
	}
	cg_wunlock(&ch_lock);
}

static
void pool_set_opaque(struct pool *pool, bool opaque)
{
	if (pool->swork.opaque == opaque)
		return;
	
	pool->swork.opaque = opaque;
	if (opaque)
		applog(LOG_WARNING, "Pool %u is hiding block contents from us",
		       pool->pool_no);
	else
		applog(LOG_NOTICE, "Pool %u now providing block contents to us",
		       pool->pool_no);
}

bool pool_may_redirect_to(struct pool * const pool, const char * const uri)
{
	if (uri_get_param_bool(pool->rpc_url, "redirect", false))
		return true;
	return match_domains(pool->rpc_url, strlen(pool->rpc_url), uri, strlen(uri));
}

void pool_check_coinbase(struct pool * const pool, const uint8_t * const cbtxn, const size_t cbtxnsz)
{
	if (uri_get_param_bool(pool->rpc_url, "skipcbcheck", false))
	{}
	else
	if (!check_coinbase(cbtxn, cbtxnsz, &pool->cb_param))
	{
		if (pool->enabled == POOL_ENABLED)
		{
			applog(LOG_ERR, "Pool %d misbehaving (%s), disabling!", pool->pool_no, "coinbase check");
			disable_pool(pool, POOL_MISBEHAVING);
		}
	}
	else
	if (pool->enabled == POOL_MISBEHAVING)
	{
		applog(LOG_NOTICE, "Pool %d no longer misbehaving, re-enabling!", pool->pool_no);
		enable_pool(pool);
	}
}

void set_simple_ntime_roll_limit(struct ntime_roll_limits * const nrl, const uint32_t ntime_base, const int ntime_roll, const struct timeval * const tvp_ref)
{
	const int offsets = max(ntime_roll, 60);
	*nrl = (struct ntime_roll_limits){
		.min = ntime_base,
		.max = ntime_base + ntime_roll,
		.tv_ref = *tvp_ref,
		.minoff = -offsets,
		.maxoff = offsets,
	};
}

void work_set_simple_ntime_roll_limit(struct work * const work, const int ntime_roll, const struct timeval * const tvp_ref)
{
	set_simple_ntime_roll_limit(&work->ntime_roll_limits, upk_u32be(work->data, 0x44), ntime_roll, tvp_ref);
}

int work_ntime_range(struct work * const work, const struct timeval * const tvp_earliest, const struct timeval * const tvp_latest, const int desired_roll)
{
	const struct ntime_roll_limits * const nrl = &work->ntime_roll_limits;
	const uint32_t ref_ntime = work_get_ntime(work);
	const int earliest_elapsed = timer_elapsed(&nrl->tv_ref, tvp_earliest);
	const int   latest_elapsed = timer_elapsed(&nrl->tv_ref, tvp_latest);
	// minimum ntime is the latest possible result (add a second to spare) adjusted for minimum offset (or fixed minimum ntime)
	uint32_t min_ntime = max(nrl->min, ref_ntime + latest_elapsed+1 + nrl->minoff);
	// maximum ntime is the earliest possible result adjusted for maximum offset (or fixed maximum ntime)
	uint32_t max_ntime = min(nrl->max, ref_ntime + earliest_elapsed + nrl->maxoff);
	if (max_ntime < min_ntime)
		return -1;
	
	if (max_ntime - min_ntime > desired_roll)
	{
		// Adjust min_ntime upward for accuracy, when possible
		const int mid_elapsed = ((latest_elapsed - earliest_elapsed) / 2) + earliest_elapsed;
		uint32_t ideal_ntime = ref_ntime + mid_elapsed;
		if (ideal_ntime > min_ntime)
			min_ntime = min(ideal_ntime, max_ntime - desired_roll);
	}
	
	work_set_ntime(work, min_ntime);
	return max_ntime - min_ntime;
}

#if BLKMAKER_VERSION > 1
static
bool goal_has_at_least_one_getcbaddr(const struct mining_goal_info * const goal)
{
	for (int i = 0; i < total_pools; ++i)
	{
		struct pool * const pool = pools[i];
		if (uri_get_param_bool(pool->rpc_url, "getcbaddr", false))
			return true;
	}
	return false;
}

static
void refresh_bitcoind_address(struct mining_goal_info * const goal, const bool fresh)
{
	struct blockchain_info * const blkchain = goal->blkchain;
	
	if (!goal_has_at_least_one_getcbaddr(goal))
		return;
	
	char getcbaddr_req[60];
	CURL *curl = NULL;
	json_t *json, *j2;
	const char *s, *s2;
	bytes_t newscript = BYTES_INIT;
	
	snprintf(getcbaddr_req, sizeof(getcbaddr_req), "{\"method\":\"get%saddress\",\"id\":0,\"params\":[\"BFGMiner\"]}", fresh ? "new" : "account");
	
	for (int i = 0; i < total_pools; ++i)
	{
		struct pool * const pool = pools[i];
		if (!uri_get_param_bool(pool->rpc_url, "getcbaddr", false))
			continue;
		if (pool->goal != goal)
			continue;
		
		applog(LOG_DEBUG, "Refreshing coinbase address from pool %d", pool->pool_no);
		if (!curl)
		{
			curl = curl_easy_init();
			if (unlikely(!curl))
			{
				applogfail(LOG_ERR, "curl_easy_init");
				break;
			}
		}
		json = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass, getcbaddr_req, false, false, NULL, pool, true);
		if (unlikely((!json) || !json_is_null( (j2 = json_object_get(json, "error")) )))
		{
			const char *estrc;
			char *estr = NULL;
			if (!(json && j2))
				estrc = NULL;
			else
			{
				estrc = json_string_value(j2);
				if (!estrc)
					estrc = estr = json_dumps_ANY(j2, JSON_ENSURE_ASCII | JSON_SORT_KEYS);
			}
			applog(LOG_WARNING, "Error %cetting coinbase address from pool %d: %s", 'g', pool->pool_no, estrc);
			free(estr);
			json_decref(json);
			continue;
		}
		s = bfg_json_obj_string(json, "result", NULL);
		if (unlikely(!s))
		{
			applog(LOG_WARNING, "Error %cetting coinbase address from pool %d: %s", 'g', pool->pool_no, "(return value was not a String)");
			json_decref(json);
			continue;
		}
		s2 = set_b58addr(s, &newscript);
		if (unlikely(s2))
		{
			applog(LOG_WARNING, "Error %cetting coinbase address from pool %d: %s", 's', pool->pool_no, s2);
			json_decref(json);
			continue;
		}
		cg_ilock(&control_lock);
		if (goal->generation_script)
		{
			if (bytes_eq(&newscript, goal->generation_script))
			{
				cg_iunlock(&control_lock);
				applog(LOG_DEBUG, "Pool %d returned coinbase address already in use (%s)", pool->pool_no, s);
				json_decref(json);
				break;
			}
			cg_ulock(&control_lock);
		}
		else
		{
			cg_ulock(&control_lock);
			goal->generation_script = malloc(sizeof(*goal->generation_script));
			bytes_init(goal->generation_script);
		}
		bytes_assimilate(goal->generation_script, &newscript);
		coinbase_script_block_id = blkchain->currentblk->block_id;
		cg_wunlock(&control_lock);
		applog(LOG_NOTICE, "Now using coinbase address %s, provided by pool %d", s, pool->pool_no);
		json_decref(json);
		break;
	}
	
	bytes_free(&newscript);
	if (curl)
		curl_easy_cleanup(curl);
}
#endif

#define GBT_XNONCESZ (sizeof(uint32_t))

#if BLKMAKER_VERSION > 6
#define blkmk_append_coinbase_safe(tmpl, append, appendsz)  \
       blkmk_append_coinbase_safe2(tmpl, append, appendsz, GBT_XNONCESZ, false)
#endif

static bool work_decode(struct pool *pool, struct work *work, json_t *val)
{
	json_t *res_val = json_object_get(val, "result");
	json_t *tmp_val;
	bool ret = false;
	struct timeval tv_now;

	if (unlikely(detect_algo == 1)) {
		json_t *tmp = json_object_get(res_val, "algorithm");
		const char *v = tmp ? json_string_value(tmp) : "";
		if (strncasecmp(v, "scrypt", 6))
			detect_algo = 2;
	}
	
	timer_set_now(&tv_now);
	
	if (work->tr)
	{
		blktemplate_t * const tmpl = work->tr->tmpl;
		const char *err = blktmpl_add_jansson(tmpl, res_val, tv_now.tv_sec);
		if (err) {
			applog(LOG_ERR, "blktmpl error: %s", err);
			return false;
		}
		work->rolltime = blkmk_time_left(tmpl, tv_now.tv_sec);
#if BLKMAKER_VERSION > 1
		struct mining_goal_info * const goal = pool->goal;
		const uint32_t tmpl_block_id = ((uint32_t*)tmpl->prevblk)[0];
		if ((!tmpl->cbtxn) && coinbase_script_block_id != tmpl_block_id)
			refresh_bitcoind_address(goal, false);
		if (goal->generation_script)
		{
			bool newcb;
#if BLKMAKER_VERSION > 2
			blkmk_init_generation2(tmpl, bytes_buf(goal->generation_script), bytes_len(goal->generation_script), &newcb);
#else
			newcb = !tmpl->cbtxn;
			blkmk_init_generation(tmpl, bytes_buf(goal->generation_script), bytes_len(goal->generation_script));
#endif
			if (newcb)
			{
				ssize_t ae = blkmk_append_coinbase_safe(tmpl, &template_nonce, sizeof(template_nonce));
				if (ae < (ssize_t)sizeof(template_nonce))
					applog(LOG_WARNING, "Cannot append template-nonce to coinbase on pool %u (%"PRId64") - you might be wasting hashing!", work->pool->pool_no, (int64_t)ae);
				++template_nonce;
			}
		}
#endif
#if BLKMAKER_VERSION > 0
		{
			ssize_t ae = blkmk_append_coinbase_safe(tmpl, opt_coinbase_sig, 101);
			static bool appenderr = false;
			if (ae <= 0) {
				if (opt_coinbase_sig) {
					applog((appenderr ? LOG_DEBUG : LOG_WARNING), "Cannot append coinbase signature at all on pool %u (%"PRId64")", pool->pool_no, (int64_t)ae);
					appenderr = true;
				}
			} else if (ae >= 3 || opt_coinbase_sig) {
				const char *cbappend = opt_coinbase_sig;
				const char * const full = bfgminer_name_space_ver;
				char *need_free = NULL;
				if (!cbappend) {
					if ((size_t)ae >= sizeof(full) - 1)
						cbappend = full;
					else if ((size_t)ae >= sizeof(PACKAGE) - 1)
					{
						const char *pos = strchr(full, '-');
						size_t sz = (pos - full);
						if (pos && ae > sz)
						{
							cbappend = need_free = malloc(sz + 1);
							memcpy(need_free, full, sz);
							need_free[sz] = '\0';
						}
						else
							cbappend = PACKAGE;
					}
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
				ae = blkmk_append_coinbase_safe(tmpl, cbappend, ae);
				free(need_free);
				if (ae <= 0) {
					applog((appenderr ? LOG_DEBUG : LOG_WARNING), "Error appending coinbase signature (%"PRId64")", (int64_t)ae);
					appenderr = true;
				} else
					appenderr = false;
			}
		}
#endif
		if (blkmk_get_data(tmpl, work->data, 80, tv_now.tv_sec, NULL, &work->dataid) < 76)
			return false;
		swap32yes(work->data, work->data, 80 / 4);
		memcpy(&work->data[80], workpadding_bin, 48);
		
		work->ntime_roll_limits = (struct ntime_roll_limits){
			.min = tmpl->mintime,
			.max = tmpl->maxtime,
			.tv_ref = tv_now,
			.minoff = tmpl->mintimeoff,
			.maxoff = tmpl->maxtimeoff,
		};

		const struct blktmpl_longpoll_req *lp;
		mutex_lock(&pool->pool_lock);
		if ((lp = blktmpl_get_longpoll(tmpl)) && ((!pool->lp_id) || strcmp(lp->id, pool->lp_id))) {
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
		mutex_unlock(&pool->pool_lock);
	}
	else
	if (unlikely(!jobj_binary(res_val, "data", work->data, sizeof(work->data), true))) {
		applog(LOG_ERR, "JSON inval data");
		return false;
	}
	else
		work_set_simple_ntime_roll_limit(work, 0, &tv_now);

	if (!jobj_binary(res_val, "midstate", work->midstate, sizeof(work->midstate), false)) {
		// Calculate it ourselves
		applog(LOG_DEBUG, "Calculating midstate locally");
		calc_midstate(work);
	}

	if (unlikely(!jobj_binary(res_val, "target", work->target, sizeof(work->target), true))) {
		applog(LOG_ERR, "JSON inval target");
		return false;
	}
	if (work->tr)
	{
		for (size_t i = 0; i < sizeof(work->target) / 2; ++i)
		{
			int p = (sizeof(work->target) - 1) - i;
			unsigned char c = work->target[i];
			work->target[i] = work->target[p];
			work->target[p] = c;
		}
	}

	if ( (tmp_val = json_object_get(res_val, "height")) ) {
		struct mining_goal_info * const goal = pool->goal;
		uint32_t blkheight = json_number_value(tmp_val);
		const void * const prevblkhash = &work->data[4];
		have_block_height(goal, prevblkhash, blkheight);
	}

	memset(work->hash, 0, sizeof(work->hash));

	work->tv_staged = tv_now;
	
#if BLKMAKER_VERSION > 6
	if (work->tr)
	{
		blktemplate_t * const tmpl = work->tr->tmpl;
		uint8_t buf[80];
		int16_t expire;
		uint8_t *cbtxn;
		size_t cbtxnsz;
		size_t cbextranonceoffset;
		int branchcount;
		libblkmaker_hash_t *branches;
		
		if (blkmk_get_mdata(tmpl, buf, sizeof(buf), tv_now.tv_sec, &expire, &cbtxn, &cbtxnsz, &cbextranonceoffset, &branchcount, &branches, GBT_XNONCESZ, false))
		{
			struct stratum_work * const swork = &pool->swork;
			const size_t branchdatasz = branchcount * 0x20;
			
			pool_check_coinbase(pool, cbtxn, cbtxnsz);
			
			cg_wlock(&pool->data_lock);
			if (swork->tr)
				tmpl_decref(swork->tr);
			swork->tr = work->tr;
			tmpl_incref(swork->tr);
			bytes_assimilate_raw(&swork->coinbase, cbtxn, cbtxnsz, cbtxnsz);
			swork->nonce2_offset = cbextranonceoffset;
			bytes_assimilate_raw(&swork->merkle_bin, branches, branchdatasz, branchdatasz);
			swork->merkles = branchcount;
			swap32yes(swork->header1, &buf[0], 36 / 4);
			swork->ntime = le32toh(*(uint32_t *)(&buf[68]));
			swork->tv_received = tv_now;
			swap32yes(swork->diffbits, &buf[72], 4 / 4);
			memcpy(swork->target, work->target, sizeof(swork->target));
			free(swork->job_id);
			swork->job_id = NULL;
			swork->clean = true;
			swork->work_restart_id = pool->work_restart_id;
			// FIXME: Do something with expire
			pool->nonce2sz = swork->n2size = GBT_XNONCESZ;
			pool->nonce2 = 0;
			cg_wunlock(&pool->data_lock);
		}
		else
			applog(LOG_DEBUG, "blkmk_get_mdata failed for pool %u", pool->pool_no);
	}
#endif  // BLKMAKER_VERSION > 6
	pool_set_opaque(pool, !work->tr);

	ret = true;

	return ret;
}

/* Returns whether the pool supports local work generation or not. */
static bool pool_localgen(struct pool *pool)
{
	return (pool->last_work_copy || pool->has_stratum);
}

int dev_from_id(int thr_id)
{
	struct cgpu_info *cgpu = get_thr_cgpu(thr_id);

	return cgpu->device_id;
}

/* Create an exponentially decaying average over the opt_log_interval */
void decay_time(double *f, double fadd, double fsecs)
{
	double ftotal, fprop;

	fprop = 1.0 - 1 / (exp(fsecs / (double)opt_log_interval));
	ftotal = 1.0 + fprop;
	*f += (fadd * fprop);
	*f /= ftotal;
}

static
int __total_staged(const bool include_spares)
{
	int tot = HASH_COUNT(staged_work);
	if (!include_spares)
		tot -= staged_spare;
	return tot;
}

static int total_staged(const bool include_spares)
{
	int ret;

	mutex_lock(stgd_lock);
	ret = __total_staged(include_spares);
	mutex_unlock(stgd_lock);

	return ret;
}

#ifdef HAVE_CURSES
WINDOW *mainwin, *statuswin, *logwin;
#endif
double total_secs = 1.0;
#ifdef HAVE_CURSES
static char statusline[256];
/* statusy is where the status window goes up to in cases where it won't fit at startup */
static int statusy;
static int devsummaryYOffset;
static int total_lines;
#endif
#ifdef USE_OPENCL
struct cgpu_info gpus[MAX_GPUDEVICES]; /* Maximum number apparently possible */
#endif
struct cgpu_info *cpus;

bool _bfg_console_cancel_disabled;
int _bfg_console_prev_cancelstate;

#ifdef HAVE_CURSES
#define   lock_curses()  bfg_console_lock()
#define unlock_curses()  bfg_console_unlock()

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

#ifdef PDCURSES
static
int bfg_wresize(WINDOW *win, int lines, int columns)
{
	int rv = wresize(win, lines, columns);
	int x, y;
	getyx(win, y, x);
	if (unlikely(y >= lines || x >= columns))
	{
		if (y >= lines)
			y = lines - 1;
		if (x >= columns)
			x = columns - 1;
		wmove(win, y, x);
	}
	return rv;
}
#else
#	define bfg_wresize wresize
#endif

#endif

void tailsprintf(char *buf, size_t bufsz, const char *fmt, ...)
{
	va_list ap;
	size_t presz = strlen(buf);
	
	va_start(ap, fmt);
	vsnprintf(&buf[presz], bufsz - presz, fmt, ap);
	va_end(ap);
}

double stats_elapsed(struct cgminer_stats *stats)
{
	struct timeval now;
	double elapsed;

	if (stats->start_tv.tv_sec == 0)
		elapsed = total_secs;
	else {
		cgtime(&now);
		elapsed = tdiff(&now, &stats->start_tv);
	}

	if (elapsed < 1.0)
		elapsed = 1.0;

	return elapsed;
}

bool drv_ready(struct cgpu_info *cgpu)
{
	switch (cgpu->status) {
		case LIFE_INIT:
		case LIFE_DEAD2:
			return false;
		default:
			return true;
	}
}

double cgpu_utility(struct cgpu_info *cgpu)
{
	double dev_runtime = cgpu_runtime(cgpu);
	return cgpu->utility = cgpu->accepted / dev_runtime * 60;
}

#define suffix_string(val, buf, bufsiz, sigdigits)  do{ \
	_Static_assert(sigdigits == 0, "suffix_string only supported with sigdigits==0");  \
	format_unit3(buf, bufsiz, FUP_DIFF, "", H2B_SHORTV, val, -1);  \
}while(0)

static float
utility_to_hashrate(double utility)
{
	return utility * 0x4444444;
}

static const char*_unitchar = "pn\xb5m kMGTPEZY?";
static const int _unitbase = 4;

static
void pick_unit(float hashrate, unsigned char *unit)
{
	unsigned char i;
	
	if (hashrate == 0 || !isfinite(hashrate))
	{
		if (*unit < _unitbase)
			*unit = _unitbase;
		return;
	}
	
	hashrate *= 1e12;
	for (i = 0; i < *unit; ++i)
		hashrate /= 1e3;
	
	// 1000 but with tolerance for floating-point rounding, avoid showing "1000.0"
	while (hashrate >= 999.95)
	{
		hashrate /= 1e3;
		if (likely(_unitchar[*unit] != '?'))
			++*unit;
	}
}
#define hashrate_pick_unit(hashrate, unit)  pick_unit(hashrate, unit)

enum h2bs_fmt {
	H2B_NOUNIT,  // "xxx.x"
	H2B_SHORT,   // "xxx.xMH/s"
	H2B_SPACED,  // "xxx.x MH/s"
	H2B_SHORTV,  // Like H2B_SHORT, but omit space for base unit
};

enum bfu_floatprec {
	FUP_INTEGER,
	FUP_HASHES,
	FUP_BTC,
	FUP_DIFF,
};

static
int format_unit3(char *buf, size_t sz, enum bfu_floatprec fprec, const char *measurement, enum h2bs_fmt fmt, float hashrate, signed char unitin)
{
	char *s = buf;
	unsigned char prec, i, unit;
	int rv = 0;
	
	if (unitin == -1)
	{
		unit = 0;
		hashrate_pick_unit(hashrate, &unit);
	}
	else
		unit = unitin;
	
	hashrate *= 1e12;
	
	for (i = 0; i < unit; ++i)
		hashrate /= 1000;
	
	switch (fprec)
	{
	case FUP_HASHES:
		// 100 but with tolerance for floating-point rounding, max "99.99" then "100.0"
		if (hashrate >= 99.995 || unit < 6)
			prec = 1;
		else
			prec = 2;
		_SNP("%5.*f", prec, hashrate);
		break;
	case FUP_INTEGER:
		_SNP("%3d", (int)hashrate);
		break;
	case FUP_BTC:
		if (hashrate >= 99.995)
			prec = 0;
		else
			prec = 2;
		_SNP("%5.*f", prec, hashrate);
		break;
	case FUP_DIFF:
		if (unit > _unitbase)
			_SNP("%.3g", hashrate);
		else
			_SNP("%u", (unsigned int)hashrate);
	}
	
	if (fmt != H2B_NOUNIT)
	{
		char uc[3] = {_unitchar[unit], '\0'};
		switch (fmt) {
			case H2B_SPACED:
				_SNP(" ");
			default:
				break;
			case H2B_SHORTV:
				if (isspace(uc[0]))
					uc[0] = '\0';
		}
		
		if (uc[0] == '\xb5')
			// Convert to UTF-8
			snprintf(uc, sizeof(uc), "%s", U8_MICRO);
		
		_SNP("%s%s", uc, measurement);
	}
	
	return rv;
}
#define format_unit2(buf, sz, floatprec, measurement, fmt, n, unit)  \
	format_unit3(buf, sz, floatprec ? FUP_HASHES : FUP_INTEGER, measurement, fmt, n, unit)

static
char *_multi_format_unit(char **buflist, size_t *bufszlist, bool floatprec, const char *measurement, enum h2bs_fmt fmt, const char *delim, int count, const float *numbers, bool isarray)
{
	unsigned char unit = 0;
	bool allzero = true;
	int i;
	size_t delimsz = 0;
	char *buf = buflist[0];
	size_t bufsz = bufszlist[0];
	size_t itemwidth = (floatprec ? 5 : 3);
	
	if (!isarray)
		delimsz = strlen(delim);
	
	for (i = 0; i < count; ++i)
		if (numbers[i] != 0)
		{
			pick_unit(numbers[i], &unit);
			allzero = false;
		}
	
	if (allzero)
		unit = _unitbase;
	
	--count;
	for (i = 0; i < count; ++i)
	{
		format_unit2(buf, bufsz, floatprec, NULL, H2B_NOUNIT, numbers[i], unit);
		if (isarray)
		{
			buf = buflist[i + 1];
			bufsz = bufszlist[i + 1];
		}
		else
		{
			buf += itemwidth;
			bufsz -= itemwidth;
			if (delimsz > bufsz)
				delimsz = bufsz;
			memcpy(buf, delim, delimsz);
			buf += delimsz;
			bufsz -= delimsz;
		}
	}
	
	// Last entry has the unit
	format_unit2(buf, bufsz, floatprec, measurement, fmt, numbers[count], unit);
	
	return buflist[0];
}
#define multi_format_unit2(buf, bufsz, floatprec, measurement, fmt, delim, count, ...)  _multi_format_unit((char *[]){buf}, (size_t[]){bufsz}, floatprec, measurement, fmt, delim, count, (float[]){ __VA_ARGS__ }, false)
#define multi_format_unit_array2(buflist, bufszlist, floatprec, measurement, fmt, count, ...)  (void)_multi_format_unit(buflist, bufszlist, floatprec, measurement, fmt, NULL, count, (float[]){ __VA_ARGS__ }, true)

static
int percentf3(char * const buf, size_t sz, double p, const double t)
{
	char *s = buf;
	int rv = 0;
	if (!p)
		_SNP("none");
	else
	if (t <= p)
		_SNP("100%%");
	else
	{

	p /= t;
	if (p < 0.00995)  // 0.01 but with tolerance for floating-point rounding, max ".99%"
		_SNP(".%02.0f%%", p * 10000);  // ".01%"
	else
	if (p < 0.0995)  // 0.1 but with tolerance for floating-point rounding, max "9.9%"
		_SNP("%.1f%%", p * 100);  // "9.1%"
	else
		_SNP("%3.0f%%", p * 100);  // " 99%"

	}
	
	return rv;
}
#define percentf4(buf, bufsz, p, t)  percentf3(buf, bufsz, p, p + t)

static
void test_decimal_width()
{
	// The pipe character at end of each line should perfectly line up
	char printbuf[512];
	char testbuf1[64];
	char testbuf2[64];
	char testbuf3[64];
	char testbuf4[64];
	double testn;
	int width;
	int saved;
	
	// Hotspots around 0.1 and 0.01
	saved = -1;
	for (testn = 0.09; testn <= 0.11; testn += 0.000001) {
		percentf3(testbuf1, sizeof(testbuf1), testn,  1.0);
		percentf3(testbuf2, sizeof(testbuf2), testn, 10.0);
		width = snprintf(printbuf, sizeof(printbuf), "%10g %s %s |", testn, testbuf1, testbuf2);
		if (unlikely((saved != -1) && (width != saved))) {
			++unittest_failures;
			applog(LOG_ERR, "Test width mismatch in percentf3! %d not %d at %10g", width, saved, testn);
			applog(LOG_ERR, "%s", printbuf);
		}
		saved = width;
	}
	
	// Hotspot around 100 (but test this in several units because format_unit2 also has unit<2 check)
	saved = -1;
	for (testn = 99.0; testn <= 101.0; testn += 0.0001) {
		format_unit2(testbuf1, sizeof(testbuf1), true, "x", H2B_SHORT, testn      , -1);
		format_unit2(testbuf2, sizeof(testbuf2), true, "x", H2B_SHORT, testn * 1e3, -1);
		format_unit2(testbuf3, sizeof(testbuf3), true, "x", H2B_SHORT, testn * 1e6, -1);
		snprintf(printbuf, sizeof(printbuf), "%10g %s %s %s |", testn, testbuf1, testbuf2, testbuf3);
		width = utf8_strlen(printbuf);
		if (unlikely((saved != -1) && (width != saved))) {
			++unittest_failures;
			applog(LOG_ERR, "Test width mismatch in format_unit2! %d not %d at %10g", width, saved, testn);
			applog(LOG_ERR, "%s", printbuf);
		}
		saved = width;
	}
	
	// Hotspot around unit transition boundary in pick_unit
	saved = -1;
	for (testn = 999.0; testn <= 1001.0; testn += 0.0001) {
		format_unit2(testbuf1, sizeof(testbuf1), true, "x", H2B_SHORT, testn      , -1);
		format_unit2(testbuf2, sizeof(testbuf2), true, "x", H2B_SHORT, testn * 1e3, -1);
		format_unit2(testbuf3, sizeof(testbuf3), true, "x", H2B_SHORT, testn * 1e6, -1);
		format_unit2(testbuf4, sizeof(testbuf4), true, "x", H2B_SHORT, testn * 1e9, -1);
		snprintf(printbuf, sizeof(printbuf), "%10g %s %s %s %s |", testn, testbuf1, testbuf2, testbuf3, testbuf4);
		width = utf8_strlen(printbuf);
		if (unlikely((saved != -1) && (width != saved))) {
			++unittest_failures;
			applog(LOG_ERR, "Test width mismatch in pick_unit! %d not %d at %10g", width, saved, testn);
			applog(LOG_ERR, "%s", printbuf);
		}
		saved = width;
	}
}

#ifdef HAVE_CURSES
static void adj_width(int var, int *length);
#endif

#ifdef HAVE_CURSES
static int awidth = 1, rwidth = 1, swidth = 1, hwwidth = 1;

static
void format_statline(char *buf, size_t bufsz, const char *cHr, const char *aHr, const char *uHr, int accepted, int rejected, int stale, double wnotaccepted, double waccepted, int hwerrs, double bad_diff1, double allnonces)
{
	char rejpcbuf[6];
	char bnbuf[6];
	
	adj_width(accepted, &awidth);
	adj_width(rejected, &rwidth);
	adj_width(stale, &swidth);
	adj_width(hwerrs, &hwwidth);
	percentf4(rejpcbuf, sizeof(rejpcbuf), wnotaccepted, waccepted);
	percentf3(bnbuf, sizeof(bnbuf), bad_diff1, allnonces);
	
	tailsprintf(buf, bufsz, "%s/%s/%s | A:%*d R:%*d+%*d(%s) HW:%*d/%s",
	            cHr, aHr, uHr,
	            awidth, accepted,
	            rwidth, rejected,
	            swidth, stale,
	            rejpcbuf,
	            hwwidth, hwerrs,
	            bnbuf
	);
}

static
const char *pool_proto_str(const struct pool * const pool)
{
	if (pool->idle)
		return "Dead ";
	if (pool->has_stratum)
		return "Strtm";
	if (pool->lp_url && pool->proto != pool->lp_proto)
		return "Mixed";
	switch (pool->proto)
	{
		case PLP_GETBLOCKTEMPLATE:
			return " GBT ";
		case PLP_GETWORK:
			return "GWork";
		default:
			return "Alive";
	}
}

#endif

static inline
void temperature_column(char *buf, size_t bufsz, bool maybe_unicode, const float * const temp)
{
	if (!(use_unicode && have_unicode_degrees))
		maybe_unicode = false;
	if (temp && *temp > 0.)
		if (maybe_unicode)
			snprintf(buf, bufsz, "%4.1f"U8_DEGREE"C", *temp);
		else
			snprintf(buf, bufsz, "%4.1fC", *temp);
	else
	{
		if (temp)
			snprintf(buf, bufsz, "     ");
		if (maybe_unicode)
			tailsprintf(buf, bufsz, " ");
	}
	tailsprintf(buf, bufsz, " | ");
}

void get_statline3(char *buf, size_t bufsz, struct cgpu_info *cgpu, bool for_curses, bool opt_show_procs)
{
#ifndef HAVE_CURSES
	assert(for_curses == false);
#endif
	struct device_drv *drv = cgpu->drv;
	enum h2bs_fmt hashrate_style = for_curses ? H2B_SHORT : H2B_SPACED;
	char cHr[ALLOC_H2B_NOUNIT+1], aHr[ALLOC_H2B_NOUNIT+1], uHr[max(ALLOC_H2B_SHORT, ALLOC_H2B_SPACED)+3+1];
	char rejpcbuf[6];
	char bnbuf[6];
	double dev_runtime;
	
	if (!opt_show_procs)
		cgpu = cgpu->device;
	
	dev_runtime = cgpu_runtime(cgpu);
	
	double rolling, mhashes;
	int accepted, rejected, stale;
	double waccepted;
	double wnotaccepted;
	int hwerrs;
	double bad_diff1, good_diff1;
	
	rolling = mhashes = waccepted = wnotaccepted = 0;
	accepted = rejected = stale = hwerrs = bad_diff1 = good_diff1 = 0;
	
	{
		struct cgpu_info *slave = cgpu;
		for (int i = 0; i < cgpu->procs; ++i, (slave = slave->next_proc))
		{
			slave->utility = slave->accepted / dev_runtime * 60;
			slave->utility_diff1 = slave->diff_accepted / dev_runtime * 60;
			
			rolling += drv->get_proc_rolling_hashrate ? drv->get_proc_rolling_hashrate(slave) : slave->rolling;
			mhashes += slave->total_mhashes;
			if (opt_weighed_stats)
			{
				accepted += slave->diff_accepted;
				rejected += slave->diff_rejected;
				stale += slave->diff_stale;
			}
			else
			{
				accepted += slave->accepted;
				rejected += slave->rejected;
				stale += slave->stale;
			}
			waccepted += slave->diff_accepted;
			wnotaccepted += slave->diff_rejected + slave->diff_stale;
			hwerrs += slave->hw_errors;
			bad_diff1 += slave->bad_diff1;
			good_diff1 += slave->diff1;
			
			if (opt_show_procs)
				break;
		}
	}

	double wtotal = (waccepted + wnotaccepted);
	
	multi_format_unit_array2(
		((char*[]){cHr, aHr, uHr}),
		((size_t[]){sizeof(cHr), sizeof(aHr), sizeof(uHr)}),
		true, "h/s", hashrate_style,
		3,
		1e6*rolling,
		1e6*mhashes / dev_runtime,
		utility_to_hashrate(good_diff1 * (wtotal ? (waccepted / wtotal) : 1) * 60 / dev_runtime));

	// Processor representation
#ifdef HAVE_CURSES
	if (for_curses)
	{
		if (opt_show_procs)
			snprintf(buf, bufsz, " %*s: ", -(5 + max_lpdigits), cgpu->proc_repr);
		else
			snprintf(buf, bufsz, " %s: ", cgpu->dev_repr);
	}
	else
#endif
	{
		if (opt_show_procs)
			snprintf(buf, bufsz, "%*s ", -(5 + max_lpdigits), cgpu->proc_repr_ns);
		else
			snprintf(buf, bufsz, "%-5s ", cgpu->dev_repr_ns);
	}
	
	if (include_serial_in_statline && cgpu->dev_serial)
		tailsprintf(buf, bufsz, "[serial=%s] ", cgpu->dev_serial);
	
	if (unlikely(cgpu->status == LIFE_INIT))
	{
		tailsprintf(buf, bufsz, "Initializing...");
		return;
	}
	
	{
		const size_t bufln = strlen(buf);
		const size_t abufsz = (bufln >= bufsz) ? 0 : (bufsz - bufln);
		
		if (likely(cgpu->status != LIFE_DEAD2) && drv->override_statline_temp2 && drv->override_statline_temp2(buf, bufsz, cgpu, opt_show_procs))
			temperature_column(&buf[bufln], abufsz, for_curses, NULL);
		else
		{
			float temp = cgpu->temp;
			if (!opt_show_procs)
			{
				// Find the highest temperature of all processors
				struct cgpu_info *proc = cgpu;
				for (int i = 0; i < cgpu->procs; ++i, (proc = proc->next_proc))
					if (proc->temp > temp)
						temp = proc->temp;
			}
			temperature_column(&buf[bufln], abufsz, for_curses, &temp);
		}
	}
	
#ifdef HAVE_CURSES
	if (for_curses)
	{
		const char *cHrStatsOpt[] = {AS_BAD("DEAD "), AS_BAD("SICK "), "OFF  ", AS_BAD("REST "), AS_BAD(" ERR "), AS_BAD("WAIT "), cHr};
		const char *cHrStats;
		int cHrStatsI = (sizeof(cHrStatsOpt) / sizeof(*cHrStatsOpt)) - 1;
		bool all_dead = true, all_off = true, all_rdrv = true;
		struct cgpu_info *proc = cgpu;
		for (int i = 0; i < cgpu->procs; ++i, (proc = proc->next_proc))
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
					if (proc->status == LIFE_SICK || proc->status == LIFE_DEAD || proc->status == LIFE_DEAD2)
					{
						cHrStatsI = 1;
						all_off = false;
					}
					else
					{
						if (likely(proc->deven == DEV_ENABLED))
							all_off = false;
						if (proc->deven != DEV_RECOVER_DRV)
							all_rdrv = false;
					}
				case 1:
					break;
			}
			if (likely(proc->status != LIFE_DEAD && proc->status != LIFE_DEAD2))
				all_dead = false;
			if (opt_show_procs)
				break;
		}
		if (unlikely(all_dead))
			cHrStatsI = 0;
		else
		if (unlikely(all_off))
			cHrStatsI = 2;
		cHrStats = cHrStatsOpt[cHrStatsI];
		if (cHrStatsI == 2 && all_rdrv)
			cHrStats = " RST ";
		
		format_statline(buf, bufsz,
		                cHrStats,
		                aHr, uHr,
		                accepted, rejected, stale,
		                wnotaccepted, waccepted,
		                hwerrs,
		                bad_diff1, bad_diff1 + good_diff1);
	}
	else
#endif
	{
		percentf4(rejpcbuf, sizeof(rejpcbuf), wnotaccepted, waccepted);
		percentf4(bnbuf, sizeof(bnbuf), bad_diff1, good_diff1);
		tailsprintf(buf, bufsz, "%ds:%s avg:%s u:%s | A:%d R:%d+%d(%s) HW:%d/%s",
			opt_log_interval,
			cHr, aHr, uHr,
			accepted,
			rejected,
			stale,
			rejpcbuf,
			hwerrs,
			bnbuf
		);
	}
}

#define get_statline(buf, bufsz, cgpu)               get_statline3(buf, bufsz, cgpu, false, opt_show_procs)
#define get_statline2(buf, bufsz, cgpu, for_curses)  get_statline3(buf, bufsz, cgpu, for_curses, opt_show_procs)

static void text_print_status(int thr_id)
{
	struct cgpu_info *cgpu;
	char logline[256];

	cgpu = get_thr_cgpu(thr_id);
	if (cgpu) {
		get_statline(logline, sizeof(logline), cgpu);
		printf("\n%s\r", logline);
		fflush(stdout);
	}
}

#ifdef HAVE_CURSES
static int attr_bad = A_BOLD;

#ifdef WIN32
#define swprintf snwprintf
#endif

static
void bfg_waddstr(WINDOW *win, const char *s)
{
	const char *p = s;
	int32_t w;
	int wlen;
	unsigned char stop_ascii = (use_unicode ? '|' : 0x80);
	
	while (true)
	{
		while (likely(p[0] == '\n' || (p[0] >= 0x20 && p[0] < stop_ascii)))
		{
			// Printable ASCII
			++p;
		}
		if (p != s)
			waddnstr(win, s, p - s);
		w = utf8_decode(p, &wlen);
		s = p += wlen;
		switch(w)
		{
			// NOTE: U+F000-U+F7FF are reserved for font hacks
			case '\0':
				return;
			case 0xb5:  // micro symbol
				w = unicode_micro;
				goto default_addch;
			case 0xf000:  // "bad" off
				wattroff(win, attr_bad);
				break;
			case 0xf001:  // "bad" on
				wattron(win, attr_bad);
				break;
#ifdef USE_UNICODE
			case '|':
				wadd_wch(win, WACS_VLINE);
				break;
#endif
			case 0x2500:  // BOX DRAWINGS LIGHT HORIZONTAL
			case 0x2534:  // BOX DRAWINGS LIGHT UP AND HORIZONTAL
				if (!use_unicode)
				{
					waddch(win, '-');
					break;
				}
#ifdef USE_UNICODE
				wadd_wch(win, (w == 0x2500) ? WACS_HLINE : WACS_BTEE);
				break;
#endif
			case 0x2022:
				if (w > WCHAR_MAX || !iswprint(w))
					w = '*';
			default:
default_addch:
				if (w > WCHAR_MAX || !(iswprint(w) || w == '\n'))
				{
#if REPLACEMENT_CHAR <= WCHAR_MAX
					if (iswprint(REPLACEMENT_CHAR))
						w = REPLACEMENT_CHAR;
					else
#endif
						w = '?';
				}
				{
#ifdef USE_UNICODE
					wchar_t wbuf[0x10];
					int wbuflen = sizeof(wbuf) / sizeof(*wbuf);
					wbuflen = swprintf(wbuf, wbuflen, L"%lc", (wint_t)w);
					waddnwstr(win, wbuf, wbuflen);
#else
					wprintw(win, "%lc", (wint_t)w);
#endif
				}
		}
	}
}

static inline
void bfg_hline(WINDOW *win, int y)
{
	int maxx, __maybe_unused maxy;
	getmaxyx(win, maxy, maxx);
#ifdef USE_UNICODE
	if (use_unicode)
		mvwhline_set(win, y, 0, WACS_HLINE, maxx);
	else
#endif
		mvwhline(win, y, 0, '-', maxx);
}

static
int bfg_win_linelen(WINDOW * const win)
{
	int maxx;
	int __maybe_unused y;
	getmaxyx(win, y, maxx);
	return maxx;
}

// Spaces until end of line, using current attributes (ie, not completely clear)
static
void bfg_wspctoeol(WINDOW * const win, const int offset)
{
	int x, maxx;
	int __maybe_unused y;
	getmaxyx(win, y, maxx);
	getyx(win, y, x);
	const int space_count = (maxx - x) - offset;
	
	// Check for negative - terminal too narrow
	if (space_count <= 0)
		return;
	
	char buf[space_count];
	memset(buf, ' ', space_count);
	waddnstr(win, buf, space_count);
}

static int menu_attr = A_REVERSE;

#define CURBUFSIZ 256
#define cg_mvwprintw(win, y, x, fmt, ...) do { \
	char tmp42[CURBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	wmove(win, y, x);  \
	bfg_waddstr(win, tmp42); \
} while (0)
#define cg_wprintw(win, fmt, ...) do { \
	char tmp42[CURBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	bfg_waddstr(win, tmp42); \
} while (0)

static
void update_block_display_line(const int blky, struct mining_goal_info *goal)
{
	struct blockchain_info * const blkchain = goal->blkchain;
	struct block_info * const blkinfo = blkchain->currentblk;
	double income;
	char incomestr[ALLOC_H2B_SHORT+6+1];
	
	if (blkinfo->height)
	{
		income = goal->diff_accepted * 3600 * blkchain->currentblk_subsidy / total_secs / goal->current_diff;
		format_unit3(incomestr, sizeof(incomestr), FUP_BTC, "BTC/hr", H2B_SHORT, income/1e8, -1);
	}
	else
		strcpy(incomestr, "?");
	
	int linelen = bfg_win_linelen(statuswin);
	wmove(statuswin, blky, 0);
	
	bfg_waddstr(statuswin, " Block");
	if (!goal->is_default)
		linelen -= strlen(goal->name) + 1;
	linelen -= 6;  // " Block"
	
	if (blkinfo->height && blkinfo->height < 1000000)
	{
		cg_wprintw(statuswin, " #%6u", blkinfo->height);
		linelen -= 8;
	}
	bfg_waddstr(statuswin, ":");
	
	if (linelen > 55)
		bfg_waddstr(statuswin, " ");
	if (linelen >= 65)
		bfg_waddstr(statuswin, "...");
	
	{
		char hexpbh[0x11];
		if (!(blkinfo->height && blkinfo->height < 1000000))
		{
			bin2hex(hexpbh, &blkinfo->prevblkhash[4], 4);
			bfg_waddstr(statuswin, hexpbh);
		}
		bin2hex(hexpbh, &blkinfo->prevblkhash[0], 4);
		bfg_waddstr(statuswin, hexpbh);
	}
	
	if (linelen >= 55)
		bfg_waddstr(statuswin, " ");
	
	cg_wprintw(statuswin, " Diff:%s", goal->current_diff_str);
	
	if (linelen >= 69)
		bfg_waddstr(statuswin, " ");
	
	cg_wprintw(statuswin, "(%s) ", goal->net_hashrate);
	
	if (linelen >= 62)
	{
		if (linelen >= 69)
			bfg_waddstr(statuswin, " ");
		bfg_waddstr(statuswin, "Started:");
	}
	else
		bfg_waddstr(statuswin, "S:");
	if (linelen >= 69)
		bfg_waddstr(statuswin, " ");
	
	bfg_waddstr(statuswin, blkchain->currentblk_first_seen_time_str);
	
	if (linelen >= 69)
		bfg_waddstr(statuswin, " ");
	
	cg_wprintw(statuswin, " I:%s", incomestr);
	
	if (!goal->is_default)
		cg_wprintw(statuswin, " %s", goal->name);
	
	wclrtoeol(statuswin);
}

static bool pool_actively_in_use(const struct pool *, const struct pool *);

static
void update_block_display(const bool within_console_lock)
{
	struct mining_goal_info *goal, *tmpgoal;
	int blky = 3, i, total_found_goals = 0;
	if (!within_console_lock)
		if (!curses_active_locked())
			return;
	HASH_ITER(hh, mining_goals, goal, tmpgoal)
	{
		for (i = 0; i < total_pools; ++i)
		{
			struct pool * const pool = pools[i];
			if (pool->goal == goal && pool_actively_in_use(pool, NULL))
				break;
		}
		if (i >= total_pools)
			// no pools using this goal, so it's probably stale anyway
			continue;
		update_block_display_line(blky++, goal);
		++total_found_goals;
	}
	
	// We cannot do resizing if called within someone else's console lock
	if (within_console_lock)
		return;
	
	bfg_console_unlock();
	if (total_found_goals != active_goals)
	{
		active_goals = total_found_goals;
		devcursor = 7 + active_goals;
		switch_logsize();
	}
}

static bool pool_unworkable(const struct pool *);

/* Must be called with curses mutex lock held and curses_active */
static void curses_print_status(const int ts)
{
	struct pool *pool = currentpool;
	struct timeval now, tv;
	float efficiency;
	int logdiv;

	efficiency = total_bytes_xfer ? total_diff_accepted * 2048. / total_bytes_xfer : 0.0;

	wattron(statuswin, attr_title);
	const int linelen = bfg_win_linelen(statuswin);
	int titlelen = 1 + strlen(PACKAGE) + 1 + strlen(bfgminer_ver) + 3 + 21 + 3 + 19;
	cg_mvwprintw(statuswin, 0, 0, " " PACKAGE " ");
	if (titlelen + 17 < linelen)
		cg_wprintw(statuswin, "version ");
	cg_wprintw(statuswin, "%s - ", bfgminer_ver);
	if (titlelen + 9 < linelen)
		cg_wprintw(statuswin, "Started: ");
	else
	if (titlelen + 7 <= linelen)
		cg_wprintw(statuswin, "Start: ");
	cg_wprintw(statuswin, "%s", datestamp);
	timer_set_now(&now);
	{
		unsigned int days, hours;
		div_t d;
		
		timersub(&now, &miner_started, &tv);
		d = div(tv.tv_sec, 86400);
		days = d.quot;
		d = div(d.rem, 3600);
		hours = d.quot;
		d = div(d.rem, 60);
		cg_wprintw(statuswin, " - [%3u day%c %02d:%02d:%02d]"
			, days
			, (days == 1) ? ' ' : 's'
			, hours
			, d.quot
			, d.rem
		);
	}
	bfg_wspctoeol(statuswin, 0);
	wattroff(statuswin, attr_title);
	
	wattron(statuswin, menu_attr);
	wmove(statuswin, 1, 0);
	bfg_waddstr(statuswin, " [M]anage devices [P]ool management [S]ettings [D]isplay options ");
	bfg_wspctoeol(statuswin, 14);
	bfg_waddstr(statuswin, "[H]elp [Q]uit ");
	wattroff(statuswin, menu_attr);

	if ((pool_strategy == POOL_LOADBALANCE  || pool_strategy == POOL_BALANCE) && enabled_pools > 1) {
		char poolinfo[20], poolinfo2[20];
		int poolinfooff = 0, poolinfo2off, workable_pools = 0;
		double lowdiff = DBL_MAX, highdiff = -1;
		struct pool *lowdiff_pool = pools[0], *highdiff_pool = pools[0];
		time_t oldest_work_restart = time(NULL) + 1;
		struct pool *oldest_work_restart_pool = pools[0];
		for (int i = 0; i < total_pools; ++i)
		{
			if (pool_unworkable(pools[i]))
				continue;
			
			// NOTE: Only set pool var when it's workable; if only one is, it gets used by single-pool code
			pool = pools[i];
			++workable_pools;
			
			if (poolinfooff < sizeof(poolinfo))
				poolinfooff += snprintf(&poolinfo[poolinfooff], sizeof(poolinfo) - poolinfooff, "%u,", pool->pool_no);
			
			struct cgminer_pool_stats * const pool_stats = &pool->cgminer_pool_stats;
			if (pool_stats->last_diff < lowdiff)
			{
				lowdiff = pool_stats->last_diff;
				lowdiff_pool = pool;
			}
			if (pool_stats->last_diff > highdiff)
			{
				highdiff = pool_stats->last_diff;
				highdiff_pool = pool;
			}
			
			if (oldest_work_restart >= pool->work_restart_time)
			{
				oldest_work_restart = pool->work_restart_time;
				oldest_work_restart_pool = pool;
			}
		}
		if (unlikely(!workable_pools))
			goto no_workable_pools;
		if (workable_pools == 1)
			goto one_workable_pool;
		poolinfo2off = snprintf(poolinfo2, sizeof(poolinfo2), "%u (", workable_pools);
		if (poolinfooff > sizeof(poolinfo2) - poolinfo2off - 1)
			snprintf(&poolinfo2[poolinfo2off], sizeof(poolinfo2) - poolinfo2off, "%.*s...)", (int)(sizeof(poolinfo2) - poolinfo2off - 5), poolinfo);
		else
			snprintf(&poolinfo2[poolinfo2off], sizeof(poolinfo2) - poolinfo2off, "%.*s)%*s", (int)(poolinfooff - 1), poolinfo, (int)(sizeof(poolinfo2)), "");
		cg_mvwprintw(statuswin, 2, 0, " Pools: %s  Diff:%s%s%s  %c  LU:%s",
		             poolinfo2,
		             lowdiff_pool->diff,
		             (lowdiff == highdiff) ? "" : "-",
		             (lowdiff == highdiff) ? "" : highdiff_pool->diff,
		             pool->goal->have_longpoll ? '+' : '-',
		             oldest_work_restart_pool->work_restart_timestamp);
	}
	else
	if (pool_unworkable(pool))
	{
no_workable_pools: ;
		wattron(statuswin, attr_bad);
		cg_mvwprintw(statuswin, 2, 0, " (all pools are dead) ");
		wattroff(statuswin, attr_bad);
	}
	else
	{
one_workable_pool: ;
		char pooladdr[19];
		{
			const char *rawaddr = pool->sockaddr_url;
			BFGINIT(rawaddr, pool->rpc_url);
			size_t pooladdrlen = strlen(rawaddr);
			if (pooladdrlen > 20)
				snprintf(pooladdr, sizeof(pooladdr), "...%s", &rawaddr[pooladdrlen - (sizeof(pooladdr) - 4)]);
			else
				snprintf(pooladdr, sizeof(pooladdr), "%*s", -(int)(sizeof(pooladdr) - 1), rawaddr);
		}
		cg_mvwprintw(statuswin, 2, 0, " Pool%2u: %s  Diff:%s  %c%s  LU:%s  User:%s",
		             pool->pool_no, pooladdr, pool->diff,
		             pool->goal->have_longpoll ? '+' : '-', pool_proto_str(pool),
		             pool->work_restart_timestamp,
		             pool->rpc_user);
	}
	wclrtoeol(statuswin);
	
	update_block_display(true);
	
	char bwstr[(ALLOC_H2B_SHORT*2)+3+1];
	
	cg_mvwprintw(statuswin, devcursor - 4, 0, " ST:%d  F:%d  NB:%d  AS:%d  BW:[%s]  E:%.2f  BS:%s",
		ts,
		total_go + total_ro,
		new_blocks,
		total_submitting,
		multi_format_unit2(bwstr, sizeof(bwstr),
		                   false, "B/s", H2B_SHORT, "/", 2,
		                  (float)(total_bytes_rcvd / total_secs),
		                  (float)(total_bytes_sent / total_secs)),
		efficiency,
		best_share);
	wclrtoeol(statuswin);
	
	mvwaddstr(statuswin, devcursor - 3, 0, " ");
	bfg_waddstr(statuswin, statusline);
	wclrtoeol(statuswin);
	
	int devdiv = devcursor - 2;
	logdiv = statusy - 1;
	bfg_hline(statuswin, devdiv);
	bfg_hline(statuswin, logdiv);
#ifdef USE_UNICODE
	if (use_unicode)
	{
		int offset = 8 /* device */ + 5 /* temperature */ + 1 /* padding space */;
		if (opt_show_procs && !opt_compact)
			offset += max_lpdigits;  // proc letter(s)
		if (have_unicode_degrees)
			++offset;  // degrees symbol
		mvwadd_wch(statuswin, devdiv, offset, WACS_PLUS);
		mvwadd_wch(statuswin, logdiv, offset, WACS_BTEE);
		offset += 24;  // hashrates etc
		mvwadd_wch(statuswin, devdiv, offset, WACS_PLUS);
		mvwadd_wch(statuswin, logdiv, offset, WACS_BTEE);
	}
#endif
}

static void adj_width(int var, int *length)
{
	if ((int)(log10(var) + 1) > *length)
		(*length)++;
}

static int dev_width;

static void curses_print_devstatus(struct cgpu_info *cgpu)
{
	char logline[256];
	int ypos;

	if (opt_compact)
		return;

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
	ypos += devcursor - 1;
	if (ypos >= statusy - 1)
		return;

	if (wmove(statuswin, ypos, 0) == ERR)
		return;
	
	get_statline2(logline, sizeof(logline), cgpu, true);
	if (selecting_device && (opt_show_procs ? (selected_device == cgpu->cgminer_id) : (devices[selected_device]->device == cgpu)))
		wattron(statuswin, A_REVERSE);
	bfg_waddstr(statuswin, logline);
	wattroff(statuswin, A_REVERSE);

	wclrtoeol(statuswin);
}

static
void _refresh_devstatus(const bool already_have_lock) {
	if ((!opt_compact) && (already_have_lock || curses_active_locked())) {
		int i;
		if (unlikely(!total_devices))
		{
			const int ypos = devcursor - 1;
			if (ypos < statusy - 1 && wmove(statuswin, ypos, 0) != ERR)
			{
				wattron(statuswin, attr_bad);
				bfg_waddstr(statuswin, "NO DEVICES FOUND: Press 'M' and '+' to add");
				wclrtoeol(statuswin);
				wattroff(statuswin, attr_bad);
			}
		}
		for (i = 0; i < total_devices; i++)
			curses_print_devstatus(get_devices(i));
		touchwin(statuswin);
		wrefresh(statuswin);
		if (!already_have_lock)
			unlock_curses();
	}
}
#define refresh_devstatus() _refresh_devstatus(false)

#endif

static void print_status(int thr_id)
{
	if (!curses_active)
		text_print_status(thr_id);
}

#ifdef HAVE_CURSES
static
bool set_statusy(int maxy)
{
	if (loginput_size)
	{
		maxy -= loginput_size;
		if (maxy < 0)
			maxy = 0;
	}
	
	if (logstart < maxy)
		maxy = logstart;
	
	if (statusy == maxy)
		return false;
	
	statusy = maxy;
	logcursor = statusy;
	
	return true;
}

/* Check for window resize. Called with curses mutex locked */
static inline void change_logwinsize(void)
{
	int x, y, logx, logy;

	getmaxyx(mainwin, y, x);
	if (x < 80 || y < 25)
		return;

	if (y > statusy + 2 && statusy < logstart) {
		set_statusy(y - 2);
		mvwin(logwin, logcursor, 0);
		bfg_wresize(statuswin, statusy, x);
	}

	y -= logcursor;
	getmaxyx(logwin, logy, logx);
	/* Detect screen size change */
	if (x != logx || y != logy)
		bfg_wresize(logwin, y, x);
}

static void check_winsizes(void)
{
	if (!use_curses)
		return;
	if (curses_active_locked()) {
		int y, x;

		x = getmaxx(statuswin);
		if (set_statusy(LINES - 2))
		{
			erase();
			bfg_wresize(statuswin, statusy, x);
			getmaxyx(mainwin, y, x);
			y -= logcursor;
			bfg_wresize(logwin, y, x);
			mvwin(logwin, logcursor, 0);
		}
		unlock_curses();
	}
}

static int device_line_id_count;

static void switch_logsize(void)
{
	if (curses_active_locked()) {
		if (opt_compact) {
			logstart = devcursor - 1;
			logcursor = logstart + 1;
		} else {
			total_lines = (opt_show_procs ? total_devices : device_line_id_count) ?: 1;
			logstart = devcursor + total_lines;
			logcursor = logstart;
		}
		unlock_curses();
	}
	check_winsizes();
	update_block_display(false);
}

/* For mandatory printing when mutex is already locked */
void _wlog(const char *str)
{
	static bool newline;
	size_t end = strlen(str) - 1;
	
	if (newline)
		bfg_waddstr(logwin, "\n");
	
	if (str[end] == '\n')
	{
		char *s;
		
		newline = true;
		s = alloca(end + 1);
		memcpy(s, str, end);
		s[end] = '\0';
		str = s;
	}
	else
		newline = false;
	
	bfg_waddstr(logwin, str);
}

/* Mandatory printing */
void _wlogprint(const char *str)
{
	if (curses_active_locked()) {
		_wlog(str);
		unlock_curses();
	}
}
#endif

#ifdef HAVE_CURSES
bool _log_curses_only(int prio, const char *datetime, const char *str)
{
	bool high_prio;

	high_prio = (prio == LOG_WARNING || prio == LOG_ERR);

	if (curses_active)
	{
		if (!loginput_size || high_prio) {
			wlog(" %s %s\n", datetime, str);
			if (high_prio) {
				touchwin(logwin);
				wrefresh(logwin);
			}
		}
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

void enable_pool(struct pool * const pool)
{
	if (pool->enabled != POOL_ENABLED) {
		mutex_lock(&lp_lock);
		enabled_pools++;
		pool->enabled = POOL_ENABLED;
		pthread_cond_broadcast(&lp_cond);
		mutex_unlock(&lp_lock);
		if (pool->prio < current_pool()->prio)
			switch_pools(pool);
	}
}

void manual_enable_pool(struct pool * const pool)
{
	pool->failover_only = false;
	BFGINIT(pool->quota, 1);
	enable_pool(pool);
}

void disable_pool(struct pool * const pool, const enum pool_enable enable_status)
{
	if (pool->enabled == POOL_DISABLED)
		/* had been manually disabled before */
		return;
	
	if (pool->enabled != POOL_ENABLED)
	{
		/* has been programmatically disabled already, just change to the new status directly */
		pool->enabled = enable_status;
		return;
	}
	
	/* Fall into the lock area */
	mutex_lock(&lp_lock);
	--enabled_pools;
	pool->enabled = enable_status;
	mutex_unlock(&lp_lock);
	
	if (pool == current_pool())
		switch_pools(NULL);
}

static
void share_result_msg(const struct work *work, const char *disp, const char *reason, bool resubmit, const char *worktime) {
	struct cgpu_info *cgpu;
	const struct mining_algorithm * const malgo = work_mining_algorithm(work);
	const unsigned char *hashpart = &work->hash[0x1c - malgo->ui_skip_hash_bytes];
	char shrdiffdisp[ALLOC_H2B_SHORTV];
	const double tgtdiff = work->work_difficulty;
	char tgtdiffdisp[ALLOC_H2B_SHORTV];
	char where[20];
	
	cgpu = get_thr_cgpu(work->thr_id);
	
	suffix_string(work->share_diff, shrdiffdisp, sizeof(shrdiffdisp), 0);
	suffix_string(tgtdiff, tgtdiffdisp, sizeof(tgtdiffdisp), 0);
	
	if (total_pools > 1)
		snprintf(where, sizeof(where), " pool %d", work->pool->pool_no);
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
	if (unlikely(work->block && work->tr))
	{
		// This is a block with a full template (GBT)
		// Regardless of the result, submit to local bitcoind(s) as well
		struct work *work_cp;
		
		for (int i = 0; i < total_pools; ++i)
		{
			if (!uri_get_param_bool(pools[i]->rpc_url, "allblocks", false))
				continue;
			
			applog(LOG_DEBUG, "Attempting submission of full block to pool %d", pools[i]->pool_no);
			work_cp = copy_work(work);
			work_cp->pool = pools[i];
			work_cp->do_foreign_submit = true;
			_submit_work_async(work_cp);
		}
	}
#endif
}

static
json_t *extract_reject_reason_j(json_t * const val, json_t *res, json_t * const err, const struct work * const work)
{
	if (json_is_string(res))
		return res;
	if ( (res = json_object_get(val, "reject-reason")) )
		return res;
	if (work->stratum && err && json_is_array(err) && json_array_size(err) >= 2 && (res = json_array_get(err, 1)) && json_is_string(res))
		return res;
	return NULL;
}

static
const char *extract_reject_reason(json_t * const val, json_t *res, json_t * const err, const struct work * const work)
{
	json_t * const j = extract_reject_reason_j(val, res, err, work);
	return j ? json_string_value(j) : NULL;
}

static
int put_in_parens(char * const buf, const size_t bufsz, const char * const s)
{
	if (!s)
	{
		if (bufsz)
			buf[0] = '\0';
		return 0;
	}
	
	int p = snprintf(buf, bufsz, " (%s", s);
	if (p >= bufsz - 1)
		p = bufsz - 2;
	strcpy(&buf[p], ")");
	return p + 1;
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
		struct mining_goal_info * const goal = pool->goal;
		
		mutex_lock(&stats_lock);
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		goal->diff_accepted += work->work_difficulty;
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
		if (opt_shares && total_diff_accepted >= opt_shares) {
			applog(LOG_WARNING, "Successfully mined %g accepted shares as requested and exiting.", opt_shares);
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
	}
	else
	if (!hash_target_check(work->hash, work->target))
	{
		// This was submitted despite failing the proper target
		// Quietly ignore the reject
		char reason[32];
		put_in_parens(reason, sizeof(reason), extract_reject_reason(val, res, err, work));
		applog(LOG_DEBUG, "Share above target rejected%s by pool %u as expected, ignoring", reason, pool->pool_no);
		
		// Stratum error 23 is "low difficulty share", which suggests this pool tracks job difficulty correctly.
		// Therefore, we disable retrodiff if it was enabled-by-default.
		if (pool->pool_diff_effective_retroactively == BTS_UNKNOWN) {
			json_t *errnum;
			if (work->stratum && err && json_is_array(err) && json_array_size(err) >= 1 && (errnum = json_array_get(err, 0)) && json_is_number(errnum) && ((int)json_number_value(errnum)) == 23) {
				applog(LOG_DEBUG, "Disabling retroactive difficulty adjustments for pool %u", pool->pool_no);
				pool->pool_diff_effective_retroactively = false;
			}
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
			char disposition[36] = "reject";
			char reason[32];

			const char *reasontmp = extract_reject_reason(val, res, err, work);
			int n = put_in_parens(reason, sizeof(reason), reasontmp);
			if (reason[0])
				snprintf(&disposition[6], sizeof(disposition) - 6, ":%.*s", n - 3, &reason[2]);

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
				disable_pool(pool, POOL_REJECTING);
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

	if (work->tr)
	{
		blktemplate_t * const tmpl = work->tr->tmpl;
		json_t *req;
		unsigned char data[80];
		
		swap32yes(data, work->data, 80 / 4);
#if BLKMAKER_VERSION > 6
		if (work->stratum) {
			req = blkmk_submitm_jansson(tmpl, data, bytes_buf(&work->nonce2), bytes_len(&work->nonce2), le32toh(*((uint32_t*)&work->data[76])), work->do_foreign_submit);
		} else
#endif
#if BLKMAKER_VERSION > 3
		if (work->do_foreign_submit)
			req = blkmk_submit_foreign_jansson(tmpl, data, work->dataid, le32toh(*((uint32_t*)&work->data[76])));
		else
#endif
			req = blkmk_submit_jansson(tmpl, data, work->dataid, le32toh(*((uint32_t*)&work->data[76])));
		s = json_dumps(req, 0);
		json_decref(req);
		sd = malloc(161);
		bin2hex(sd, data, 80);
	} else {

	/* build hex string */
		hexstr = malloc((sizeof(work->data) * 2) + 1);
		bin2hex(hexstr, work->data, sizeof(work->data));

	/* build JSON-RPC request */
		s = strdup("{\"method\": \"getwork\", \"params\": [ \"");
		s = realloc_strcat(s, hexstr);
		s = realloc_strcat(s, "\" ], \"id\":1}");

		free(hexstr);
		sd = s;

	}

	applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, sd);
	if (work->tr)
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
	time_t ts_submit_reply;
	char worktime[200] = "";

	cgtime(&tv_submit_reply);
	ts_submit_reply = time(NULL);

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

			localtime_r(&work->ts_getwork, tm);
			memcpy(&tm_getwork, tm, sizeof(struct tm));
			localtime_r(&ts_submit_reply, tm);
			memcpy(&tm_submit_reply, tm, sizeof(struct tm));

			if (work->clone) {
				snprintf(workclone, sizeof(workclone), "C:%1.3f",
						tdiff((struct timeval *)&(work->tv_cloned),
						(struct timeval *)&(work->tv_getwork_reply)));
			}
			else
				strcpy(workclone, "O");

			if (work->work_difficulty < 1)
				diffplaces = 6;

			const struct mining_algorithm * const malgo = work_mining_algorithm(work);
			const uint8_t * const prevblkhash = &work->data[4];
			snprintf(worktime, sizeof(worktime),
				" <-%08lx.%08lx M:%c D:%1.*f G:%02d:%02d:%02d:%1.3f %s (%1.3f) W:%1.3f (%1.3f) S:%1.3f R:%02d:%02d:%02d",
				(unsigned long)be32toh(((uint32_t *)prevblkhash)[7 - malgo->worktime_skip_prevblk_u32]),
				(unsigned long)be32toh(((uint32_t *)prevblkhash)[6 - malgo->worktime_skip_prevblk_u32]),
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
		
		get_statline(logline, sizeof(logline), cgpu);
		applog(LOG_INFO, "%s", logline);
	}

	json_decref(val);

	rc = true;
out:
	return rc;
}

/* Specifies whether we can use this pool for work or not. */
static bool pool_unworkable(const struct pool * const pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	if (pool->has_stratum && !pool->stratum_active)
		return true;
	return false;
}

static struct pool *priority_pool(int);
static bool pool_unusable(struct pool *);

static
bool pool_actively_desired(const struct pool * const pool, const struct pool *cp)
{
	if (pool->enabled != POOL_ENABLED)
		return false;
	if (pool_strategy == POOL_LOADBALANCE && pool->quota)
		return true;
	if (pool_strategy == POOL_BALANCE && !pool->failover_only)
		return true;
	if (!cp)
		cp = current_pool();
	if (pool == cp)
		return true;
	
	// If we are the highest priority, workable pool for a given algorithm, we are needed
	struct mining_algorithm * const malgo = pool->goal->malgo;
	for (int i = 0; i < total_pools; ++i)
	{
		struct pool * const other_pool = priority_pool(i);
		if (other_pool == pool)
			return true;
		if (pool_unusable(other_pool))
			continue;
		if (other_pool->goal->malgo == malgo)
			break;
	}
	
	return false;
}

static
bool pool_actively_in_use(const struct pool * const pool, const struct pool *cp)
{
	return (!pool_unworkable(pool)) && pool_actively_desired(pool, cp);
}

static
bool pool_supports_block_change_notification(struct pool * const pool)
{
	return pool->has_stratum || pool->lp_url;
}

static
bool pool_has_active_block_change_notification(struct pool * const pool)
{
	return pool->stratum_active || pool->lp_active;
}

static struct pool *_select_longpoll_pool(struct pool *, bool(*)(struct pool *));
#define select_longpoll_pool(pool)  _select_longpoll_pool(pool, pool_supports_block_change_notification)
#define pool_active_lp_pool(pool)  _select_longpoll_pool(pool, pool_has_active_block_change_notification)

/* In balanced mode, the amount of diff1 solutions per pool is monitored as a
 * rolling average per 10 minutes and if pools start getting more, it biases
 * away from them to distribute work evenly. The share count is reset to the
 * rolling average every 10 minutes to not send all work to one pool after it
 * has been disabled/out for an extended period. */
static
struct pool *select_balanced(struct pool *cp, struct mining_algorithm * const malgo)
{
	int i, lowest = cp->shares;
	struct pool *ret = cp, *failover_pool = NULL;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (malgo && pool->goal->malgo != malgo)
			continue;
		if (pool_unworkable(pool))
			continue;
		if (pool->failover_only)
		{
			BFGINIT(failover_pool, pool);
			continue;
		}
		if (pool->shares < lowest) {
			lowest = pool->shares;
			ret = pool;
		}
	}
	if (malgo && ret->goal->malgo != malgo)
		// Yes, we want failover_pool even if it's NULL
		ret = failover_pool;
	else
	if (pool_unworkable(ret) && failover_pool)
		ret = failover_pool;

	if (ret)
		++ret->shares;
	return ret;
}

static
struct pool *select_loadbalance(struct mining_algorithm * const malgo)
{
	static int rotating_pool = 0;
	struct pool *pool;
	bool avail = false;
	int tested, i, rpsave;

	for (i = 0; i < total_pools; i++) {
		struct pool *tp = pools[i];

		if (tp->quota_used < tp->quota_gcd) {
			avail = true;
			break;
		}
	}

	/* There are no pools with quota, so reset them. */
	if (!avail) {
		for (i = 0; i < total_pools; i++)
		{
			struct pool * const tp = pools[i];
			tp->quota_used -= tp->quota_gcd;
		}
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
	}

	/* Try to find the first pool in the rotation that is usable */
	// Look for the lowest integer quota_used / quota_gcd in case we are imbalanced by algorithm demands
	struct pool *pool_lowest = NULL;
	int lowest = INT_MAX;
	rpsave = rotating_pool;
	for (tested = 0; tested < total_pools; ++tested)
	{
		pool = pools[rotating_pool];
		if (malgo && pool->goal->malgo != malgo)
			goto continue_tested;
		
		if (pool->quota_used < pool->quota_gcd)
		{
			++pool->quota_used;
			if (!pool_unworkable(pool))
				goto out;
			/* Failover-only flag for load-balance means distribute
			 * unused quota to priority pool 0. */
			if (opt_fail_only)
				priority_pool(0)->quota_used--;
		}
		if (malgo)
		{
			const int count = pool->quota_used / pool->quota_gcd;
			if (count < lowest)
			{
				pool_lowest = pool;
				lowest = count;
			}
		}
		
continue_tested: ;
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
	}
	
	// Even if pool_lowest is NULL, we want to return that to indicate failure
	// Note it isn't possible to get here if !malgo
	pool = pool_lowest;
	
out: ;
	// Restore rotating_pool static, so malgo searches don't affect the usual load balancing
	if (malgo)
		rotating_pool = rpsave;
	
	return pool;
}

static
struct pool *select_failover(struct mining_algorithm * const malgo)
{
	int i;
	
	for (i = 0; i < total_pools; i++) {
		struct pool *tp = priority_pool(i);
		
		if (malgo && tp->goal->malgo != malgo)
			continue;
		
		if (!pool_unusable(tp)) {
			return tp;
		}
	}
	
	return NULL;
}

static bool pool_active(struct pool *, bool pinging);
static void pool_died(struct pool *);

/* Select any active pool in a rotating fashion when loadbalance is chosen if
 * it has any quota left. */
static inline struct pool *select_pool(bool lagging, struct mining_algorithm * const malgo)
{
	struct pool *pool = NULL, *cp;

retry:
	cp = current_pool();

	if (pool_strategy == POOL_BALANCE) {
		pool = select_balanced(cp, malgo);
		if ((!pool) || pool_unworkable(pool))
			goto simple_failover;
		goto out;
	}

	if (pool_strategy != POOL_LOADBALANCE && (!lagging || opt_fail_only)) {
		if (malgo && cp->goal->malgo != malgo)
			goto simple_failover;
		pool = cp;
		goto out;
	} else
		pool = select_loadbalance(malgo);

simple_failover:
	/* If there are no alive pools with quota, choose according to
	 * priority. */
	if (!pool) {
		pool = select_failover(malgo);
	}

	/* If still nothing is usable, use the current pool */
	if (!pool)
	{
		if (malgo && cp->goal->malgo != malgo)
		{
			applog(LOG_DEBUG, "Failed to select pool for specific mining algorithm '%s'", malgo->name);
			return NULL;
		}
		pool = cp;
	}

out:
	if (!pool_actively_in_use(pool, cp))
	{
		if (!pool_active(pool, false))
		{
			pool_died(pool);
			goto retry;
		}
		pool_tclear(pool, &pool->idle);
	}
	applog(LOG_DEBUG, "Selecting pool %d for %s%swork", pool->pool_no, malgo ? malgo->name : "", malgo ? " " : "");
	return pool;
}

static double DIFFEXACTONE = 26959946667150639794667015087019630673637144422540572481103610249215.0;

double target_diff(const unsigned char *target)
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
	suffix_string(difficulty, work->pool->diff, sizeof(work->pool->diff), 0);

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

static void gen_stratum_work(struct pool *, struct work *);
static void pool_update_work_restart_time(struct pool *);
static void restart_threads(void);

static uint32_t benchmark_blkhdr[20];
static const int benchmark_update_interval = 1;

static
void *benchmark_intense_work_update_thread(void *userp)
{
	pthread_detach(pthread_self());
	RenameThread("benchmark-intense");
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	struct pool * const pool = userp;
	struct stratum_work * const swork = &pool->swork;
	uint8_t * const blkhdr = swork->header1;
	
	while (true)
	{
		sleep(benchmark_update_interval);
		
		cg_wlock(&pool->data_lock);
		for (int i = 36; --i >= 0; )
			if (++blkhdr[i])
				break;
		cg_wunlock(&pool->data_lock);
		
		struct work *work = make_work();
		gen_stratum_work(pool, work);
		pool->swork.work_restart_id = ++pool->work_restart_id;
		pool_update_work_restart_time(pool);
		test_work_current(work);
		free_work(work);
		
		restart_threads();
	}
	return NULL;
}

static
void setup_benchmark_pool()
{
	struct pool *pool;
	
	want_longpoll = false;
	
	// Temporarily disable opt_benchmark to avoid auto-removal
	opt_benchmark = false;
	pool = add_pool();
	opt_benchmark = true;
	
	pool->rpc_url = malloc(255);
	strcpy(pool->rpc_url, "Benchmark");
	pool_set_uri(pool, pool->rpc_url);
	pool->rpc_user = pool->rpc_url;
	pool->rpc_pass = pool->rpc_url;
	enable_pool(pool);
	pool->idle = false;
	successful_connect = true;
	
	{
		uint32_t * const blkhdr = benchmark_blkhdr;
		blkhdr[2] = htobe32(1);
		blkhdr[17] = htobe32(0x7fffffff);  // timestamp
		blkhdr[18] = htobe32(0x1700ffff);  // "bits"
	}
	
	{
		struct stratum_work * const swork = &pool->swork;
		const int branchcount = 15;  // 1 MB block
		const size_t branchdatasz = branchcount * 0x20;
		const size_t coinbase_sz = (opt_benchmark_intense ? 250 : 6) * 1024;
		
		bytes_resize(&swork->coinbase, coinbase_sz);
		memset(bytes_buf(&swork->coinbase), '\xff', coinbase_sz);
		swork->nonce2_offset = 0;
		
		bytes_resize(&swork->merkle_bin, branchdatasz);
		memset(bytes_buf(&swork->merkle_bin), '\xff', branchdatasz);
		swork->merkles = branchcount;
		
		swork->header1[0] = '\xff';
		memset(&swork->header1[1], '\0', 34);
		swork->header1[35] = '\x01';
		swork->ntime = 0x7fffffff;
		timer_unset(&swork->tv_received);
		memcpy(swork->diffbits, "\x17\0\xff\xff", 4);
		const struct mining_goal_info * const goal = get_mining_goal("default");
		const struct mining_algorithm * const malgo = goal->malgo;
		set_target_to_pdiff(swork->target, malgo->reasonable_low_nonce_diff);
		pool->nonce2sz = swork->n2size = GBT_XNONCESZ;
		pool->nonce2 = 0;
	}
	
	if (opt_benchmark_intense)
	{
		pthread_t pth;
		if (unlikely(pthread_create(&pth, NULL, benchmark_intense_work_update_thread, pool)))
			applog(LOG_WARNING, "Failed to start benchmark intense work update thread");
	}
}

void get_benchmark_work(struct work *work, bool use_swork)
{
	if (use_swork)
	{
		struct timeval tv_now;
		timer_set_now(&tv_now);
		gen_stratum_work(pools[0], work);
		work->getwork_mode = GETWORK_MODE_BENCHMARK;
		work_set_simple_ntime_roll_limit(work, 0, &tv_now);
		return;
	}
	
	struct pool * const pool = pools[0];
	uint32_t * const blkhdr = benchmark_blkhdr;
	for (int i = 16; i >= 0; --i)
		if (++blkhdr[i])
			break;
	
	memcpy(&work->data[ 0], blkhdr, 80);
	memcpy(&work->data[80], workpadding_bin, 48);
	char hex[161];
	bin2hex(hex, work->data, 80);
	applog(LOG_DEBUG, "Generated benchmark header %s", hex);
	calc_midstate(work);
	memcpy(work->target, pool->swork.target, sizeof(work->target));
	
	work->mandatory = true;
	work->pool = pools[0];
	cgtime(&work->tv_getwork);
	copy_time(&work->tv_getwork_reply, &work->tv_getwork);
	copy_time(&work->tv_staged, &work->tv_getwork);
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
	calc_diff(work, 0);
	work_set_simple_ntime_roll_limit(work, 60, &work->tv_getwork);
}

static void wake_gws(void);

static void update_last_work(struct work *work)
{
	if (!work->tr)
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

static char *prepare_rpc_req2(struct work *work, enum pool_protocol proto, const char *lpid, bool probe, struct pool * const pool)
{
	char *rpc_req;

	clean_work(work);
	switch (proto) {
		case PLP_GETWORK:
			work->getwork_mode = GETWORK_MODE_POOL;
			return strdup(getwork_req);
		case PLP_GETBLOCKTEMPLATE:
			work->getwork_mode = GETWORK_MODE_GBT;
			blktemplate_t * const tmpl = blktmpl_create();
			if (!tmpl)
				goto gbtfail2;
			work->tr = tmpl_makeref(tmpl);
			gbt_capabilities_t caps = blktmpl_addcaps(tmpl);
			if (!caps)
				goto gbtfail;
			caps |= GBT_LONGPOLL;
#if BLKMAKER_VERSION > 1
			const struct mining_goal_info * const goal = pool->goal;
			if (goal->generation_script || goal_has_at_least_one_getcbaddr(goal))
				caps |= GBT_CBVALUE;
#endif
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
	tmpl_decref(work->tr);
	work->tr = NULL;
gbtfail2:
	return NULL;
}

#define prepare_rpc_req(work, proto, lpid, pool)  prepare_rpc_req2(work, proto, lpid, false, pool)
#define prepare_rpc_req_probe(work, proto, lpid, pool)  prepare_rpc_req2(work, proto, lpid, true, pool)

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
	rpc_req = prepare_rpc_req(work, pool->proto, NULL, pool);
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
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	int i;

	if (!successful_connect)
		return;

	applog(LOG_INFO, "Received kill message");

	shutting_down = true;

	applog(LOG_DEBUG, "Prompting submit_work thread to finish");
	notifier_wake(submit_waiting_notifier);

#ifdef USE_LIBMICROHTTPD
	httpsrv_stop();
#endif
	
	applog(LOG_DEBUG, "Killing off watchpool thread");
	/* Kill the watchpool thread */
	thr = &control_thr[watchpool_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Killing off watchdog thread");
	/* Kill the watchdog thread */
	thr = &control_thr[watchdog_thr_id];
	thr_info_cancel(thr);

	applog(LOG_DEBUG, "Shutting down mining threads");
	for (i = 0; i < mining_threads; i++) {
		thr = get_thread(i);
		if (!thr)
			continue;
		cgpu = thr->cgpu;
		if (!cgpu)
			continue;
		if (!cgpu->threads)
			continue;

		cgpu->shutdown = true;
		thr->work_restart = true;
		notifier_wake(thr->notifier);
		notifier_wake(thr->work_restart_notifier);
	}

	sleep(1);

	applog(LOG_DEBUG, "Killing off mining threads");
	/* Kill the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		thr = get_thread(i);
		if (!thr)
			continue;
		cgpu = thr->cgpu;
		if (cgpu->threads)
		{
			applog(LOG_WARNING, "Killing %"PRIpreprv, thr->cgpu->proc_repr);
			thr_info_cancel(thr);
		}
		cgpu->status = LIFE_DEAD2;
	}

	/* Stop the others */
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

void _bfg_clean_up(bool);

void app_restart(void)
{
	applog(LOG_WARNING, "Attempting to restart %s", packagename);

	__kill_work();
	_bfg_clean_up(true);

#if defined(unix) || defined(__APPLE__)
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
		quithere(1, "Attempted to add NULL");
	LL_PREPEND(pool->curllist, ce);
	cgtime(&ce->tv);
	pthread_cond_broadcast(&pool->cr_cond);
	mutex_unlock(&pool->pool_lock);
}

static inline bool should_roll(struct work *work)
{
	struct timeval now;
	time_t expiry;

	if (!pool_actively_in_use(work->pool, NULL))
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
	if (work->tr)
	{
		if (stale_work(work, false))
			return false;
		return blkmk_work_left(work->tr->tmpl);
	}
	return (work->rolltime &&
		work->rolls < 7000 && !stale_work(work, false));
}

static void roll_work(struct work *work)
{
	if (work->tr)
	{
		struct timeval tv_now;
		cgtime(&tv_now);
		if (blkmk_get_data(work->tr->tmpl, work->data, 80, tv_now.tv_sec, NULL, &work->dataid) < 76)
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
		work_set_simple_ntime_roll_limit(work, 0, &work->ntime_roll_limits.tv_ref);

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
static void _copy_work(struct work *work, const struct work *base_work, int noffset)
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
	bytes_cpy(&work->nonce2, &base_work->nonce2);

	if (base_work->tr)
		tmpl_incref(base_work->tr);
	
	if (noffset)
	{
		uint32_t *work_ntime = (uint32_t *)(work->data + 68);
		uint32_t ntime = be32toh(*work_ntime);

		ntime += noffset;
		*work_ntime = htobe32(ntime);
	}
	
	if (work->device_data_dup_func)
		work->device_data = work->device_data_dup_func(work);
}

/* Generates a copy of an existing work struct, creating fresh heap allocations
 * for all dynamically allocated arrays within the struct. noffset is used for
 * when a driver has internally rolled the ntime, noffset is a relative value.
 * The macro copy_work() calls this function with an noffset of 0. */
struct work *copy_work_noffset(const struct work *base_work, int noffset)
{
	struct work *work = make_work();

	_copy_work(work, base_work, noffset);

	return work;
}

void __copy_work(struct work *work, const struct work *base_work)
{
	_copy_work(work, base_work, 0);
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
			applog(LOG_DEBUG, "%s: Rolling work %d to %d", __func__, work->id, work_clone->id);
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
	mutex_lock(&lp_lock);
	if (!pool_tset(pool, &pool->idle)) {
		cgtime(&pool->tv_idle);
		pthread_cond_broadcast(&lp_cond);
		mutex_unlock(&lp_lock);
		if (pool == current_pool()) {
			applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
			switch_pools(NULL);
		} else
			applog(LOG_INFO, "Pool %d %s failed to return work", pool->pool_no, pool->rpc_url);
	}
	else
		mutex_unlock(&lp_lock);
}

bool stale_work2(struct work * const work, const bool share, const bool have_pool_data_lock)
{
	unsigned work_expiry;
	struct pool *pool;
	uint32_t block_id;
	unsigned getwork_delay;

	block_id = ((uint32_t*)work->data)[1];
	pool = work->pool;
	struct mining_goal_info * const goal = pool->goal;
	struct blockchain_info * const blkchain = goal->blkchain;

	/* Technically the rolltime should be correct but some pools
	 * advertise a broken expire= that is lower than a meaningful
	 * scantime */
	if (work->rolltime >= opt_scantime || work->tr)
		work_expiry = work->rolltime;
	else
		work_expiry = opt_expiry;

	unsigned max_expiry = (goal->have_longpoll ? opt_expiry_lp : opt_expiry);
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
				applog(LOG_DEBUG, "Work stale due to block mismatch (%08lx != 1 ? %08lx : %08lx)", (long)block_id, (long)pool->block_id, (long)blkchain->currentblk->block_id);
				return true;
			}
		} else {
			if (block_id != blkchain->currentblk->block_id)
			{
				applog(LOG_DEBUG, "Work stale due to block mismatch (%08lx != 0 ? %08lx : %08lx)", (long)block_id, (long)pool->block_id, (long)blkchain->currentblk->block_id);
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

		if (!have_pool_data_lock) {
			cg_rlock(&pool->data_lock);
		}
		if (strcmp(work->job_id, pool->swork.job_id))
			same_job = false;
		if (!have_pool_data_lock) {
			cg_runlock(&pool->data_lock);
		}

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

	int elapsed_since_staged = timer_elapsed(&work->tv_staged, NULL);
	if (elapsed_since_staged > work_expiry) {
		applog(LOG_DEBUG, "%s stale due to expiry (%d >= %u)", share?"Share":"Work", elapsed_since_staged, work_expiry);
		return true;
	}

	/* If the user only wants strict failover, any work from a pool other than
	 * the current one is always considered stale */
	if (opt_fail_only && !share && !work->mandatory && !pool_actively_in_use(pool, NULL))
	{
		applog(LOG_DEBUG, "Work stale due to fail only pool mismatch (pool %u vs %u)", pool->pool_no, current_pool()->pool_no);
		return true;
	}

	return false;
}

double share_diff(const struct work *work)
{
	double ret;
	bool new_best = false;

	ret = target_diff(work->hash);

	cg_wlock(&control_lock);
	if (unlikely(ret > best_diff)) {
		new_best = true;
		best_diff = ret;
		suffix_string(best_diff, best_share, sizeof(best_share), 0);
	}
	if (unlikely(ret > work->pool->best_diff))
		work->pool->best_diff = ret;
	cg_wunlock(&control_lock);

	if (unlikely(new_best))
		applog(LOG_INFO, "New best share: %s", best_share);

	return ret;
}

static
void work_check_for_block(struct work * const work)
{
	struct pool * const pool = work->pool;
	struct mining_goal_info * const goal = pool->goal;
	
	work->share_diff = share_diff(work);
	if (unlikely(work->share_diff >= goal->current_diff)) {
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
	cgpu->diff_stale += work->work_difficulty;
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
	struct timeval tv_staleexpire;
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

	work_check_for_block(work);

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
		timer_set_delay_from_now(&sws->tv_staleexpire, 300000000);
	}

	if (work->getwork_mode == GETWORK_MODE_STRATUM) {
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
			timer_set_delay_from_now(&sws->tv_staleexpire, 300000000);
		}
		if (unlikely((opt_retries >= 0) && (++sws->failures > opt_retries))) {
			applog(LOG_ERR, "Pool %d failed %d submission retries, discarding", pool->pool_no, opt_retries);
			submit_discard_share(work);
			return false;
		}
		else if (work->stale) {
			if (unlikely(opt_retries < 0 && timer_passed(&sws->tv_staleexpire, NULL)))
			{
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
			sessionid_match = (!pool->swork.nonce1) || !strcmp(work->nonce1, pool->swork.nonce1);
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
			char nonce2hex[(bytes_len(&work->nonce2) * 2) + 1];
			char noncehex[9];
			char ntimehex[9];
			
			sshare->work = copy_work(work);
			bin2hex(nonce2hex, bytes_buf(&work->nonce2), bytes_len(&work->nonce2));
			nonce = *((uint32_t *)(work->data + 76));
			bin2hex(noncehex, (const unsigned char *)&nonce, 4);
			bin2hex(ntimehex, (void *)&work->data[68], 4);
			
			mutex_lock(&sshare_lock);
			/* Give the stratum share a unique id */
			sshare_id =
			sshare->id = swork_id++;
			HASH_ADD_INT(stratum_shares, id, sshare);
			snprintf(s, 1024, "{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"], \"id\": %d, \"method\": \"mining.submit\"}",
				pool->rpc_user, work->job_id, nonce2hex, ntimehex, noncehex, sshare->id);
			mutex_unlock(&sshare_lock);
			
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
	struct pool *pool, *last_pool, *failover_pool = NULL;
	int i, pool_no, next_pool;

	if (selected)
		enable_pool(selected);
	
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
		/* All of these set to the master pool */
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
				if (pool->failover_only)
				{
					BFGINIT(failover_pool, pool);
					continue;
				}
				pool_no = next_pool;
				break;
			}
			break;
		default:
			break;
	}

	pool = pools[pool_no];
	if (pool_unusable(pool) && failover_pool)
		pool = failover_pool;
	currentpool = pool;
	cg_wunlock(&control_lock);
	mutex_lock(&lp_lock);
	pthread_cond_broadcast(&lp_cond);
	mutex_unlock(&lp_lock);

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
			if (pool_localgen(pool) || opt_fail_only)
				clear_pool_work(last_pool);
		}
	}

	mutex_lock(&lp_lock);
	pthread_cond_broadcast(&lp_cond);
	mutex_unlock(&lp_lock);

#ifdef HAVE_CURSES
	update_block_display(false);
#endif
}

static void discard_work(struct work *work)
{
	if (!work->clone && !work->rolls && !work->mined) {
		if (work->pool) {
			work->pool->discarded_work++;
			work->pool->quota_used--;
			work->pool->works--;
		}
		total_discarded++;
		applog(LOG_DEBUG, "Discarded work");
	} else
		applog(LOG_DEBUG, "Discarded cloned or rolled work");
	free_work(work);
}

static bool work_rollable(struct work *);

static
void unstage_work(struct work * const work)
{
	HASH_DEL(staged_work, work);
	--work_mining_algorithm(work)->staged;
	if (work_rollable(work))
		--staged_rollable;
	if (work->spare)
		--staged_spare;
	staged_full = false;
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
			unstage_work(work);
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

static
void pool_update_work_restart_time(struct pool * const pool)
{
	pool->work_restart_time = time(NULL);
	get_timestamp(pool->work_restart_timestamp, sizeof(pool->work_restart_timestamp), pool->work_restart_time);
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

void blkhashstr(char *rv, const unsigned char *hash)
{
	unsigned char hash_swap[32];
	
	swap256(hash_swap, hash);
	swap32tole(hash_swap, hash_swap, 32 / 4);
	bin2hex(rv, hash_swap, 32);
}

static
void set_curblock(struct mining_goal_info * const goal, struct block_info * const blkinfo)
{
	struct blockchain_info * const blkchain = goal->blkchain;

	blkchain->currentblk = blkinfo;
	blkchain->currentblk_subsidy = 5000000000LL >> (blkinfo->height / 210000);

	cg_wlock(&ch_lock);
	__update_block_title(goal);
	get_timestamp(blkchain->currentblk_first_seen_time_str, sizeof(blkchain->currentblk_first_seen_time_str), blkinfo->first_seen_time);
	cg_wunlock(&ch_lock);

	applog(LOG_INFO, "New block: %s diff %s (%s)", goal->current_goal_detail, goal->current_diff_str, goal->net_hashrate);
}

/* Search to see if this prevblkhash has been seen before */
static
struct block_info *block_exists(const struct blockchain_info * const blkchain, const void * const prevblkhash)
{
	struct block_info *s;

	rd_lock(&blk_lock);
	HASH_FIND(hh, blkchain->blocks, prevblkhash, 0x20, s);
	rd_unlock(&blk_lock);

	return s;
}

static int block_sort(struct block_info * const blocka, struct block_info * const blockb)
{
	return blocka->block_seen_order - blockb->block_seen_order;
}

static
void set_blockdiff(struct mining_goal_info * const goal, const struct work * const work)
{
	unsigned char target[32];
	double diff;
	uint64_t diff64;

	real_block_target(target, work->data);
	diff = target_diff(target);
	diff64 = diff;

	suffix_string(diff64, goal->current_diff_str, sizeof(goal->current_diff_str), 0);
	format_unit2(goal->net_hashrate, sizeof(goal->net_hashrate),
	             true, "h/s", H2B_SHORT, diff * 7158278, -1);
	if (unlikely(goal->current_diff != diff))
		applog(LOG_NOTICE, "Network difficulty changed to %s (%s)", goal->current_diff_str, goal->net_hashrate);
	goal->current_diff = diff;
}

static bool test_work_current(struct work *work)
{
	bool ret = true;
	
	if (work->mandatory)
		return ret;
	
	uint32_t block_id = ((uint32_t*)(work->data))[1];
	const uint8_t * const prevblkhash = &work->data[4];
	
	{
		/* Hack to work around dud work sneaking into test */
		bool dudwork = true;
		for (int i = 8; i < 26; ++i)
			if (work->data[i])
			{
				dudwork = false;
				break;
			}
		if (dudwork)
			goto out_free;
	}
	
	struct pool * const pool = work->pool;
	struct mining_goal_info * const goal = pool->goal;
	struct blockchain_info * const blkchain = goal->blkchain;
	
	/* Search to see if this block exists yet and if not, consider it a
	 * new block and set the current block details to this one */
	if (!block_exists(blkchain, prevblkhash))
	{
		struct block_info * const s = calloc(sizeof(struct block_info), 1);
		int deleted_block = 0;
		ret = false;
		
		if (unlikely(!s))
			quit (1, "test_work_current OOM");
		memcpy(s->prevblkhash, prevblkhash, sizeof(s->prevblkhash));
		s->block_id = block_id;
		s->block_seen_order = new_blocks++;
		s->first_seen_time = time(NULL);
		
		wr_lock(&blk_lock);
		/* Only keep the last hour's worth of blocks in memory since
		 * work from blocks before this is virtually impossible and we
		 * want to prevent memory usage from continually rising */
		if (HASH_COUNT(blkchain->blocks) > 6)
		{
			struct block_info *oldblock;
			
			HASH_SORT(blkchain->blocks, block_sort);
			oldblock = blkchain->blocks;
			deleted_block = oldblock->block_seen_order;
			HASH_DEL(blkchain->blocks, oldblock);
			free(oldblock);
		}
		HASH_ADD(hh, blkchain->blocks, prevblkhash, sizeof(s->prevblkhash), s);
		set_blockdiff(goal, work);
		wr_unlock(&blk_lock);
		pool->block_id = block_id;
		pool_update_work_restart_time(pool);
		
		if (deleted_block)
			applog(LOG_DEBUG, "Deleted block %d from database", deleted_block);
#if BLKMAKER_VERSION > 1
		template_nonce = 0;
#endif
		set_curblock(goal, s);
		if (unlikely(new_blocks == 1))
			goto out_free;
		
		if (!work->stratum)
		{
			if (work->longpoll)
			{
				applog(LOG_NOTICE, "Longpoll from pool %d detected new block",
				       pool->pool_no);
			}
			else
			if (goal->have_longpoll)
				applog(LOG_NOTICE, "New block detected on network before longpoll");
			else
				applog(LOG_NOTICE, "New block detected on network");
		}
		restart_threads();
	}
	else
	{
		bool restart = false;
		if (unlikely(pool->block_id != block_id))
		{
			bool was_active = pool->block_id != 0;
			pool->block_id = block_id;
			pool_update_work_restart_time(pool);
			if (!work->longpoll)
				update_last_work(work);
			if (was_active)
			{
				// Pool actively changed block
				if (pool == current_pool())
					restart = true;
				if (block_id == blkchain->currentblk->block_id)
				{
					// Caught up, only announce if this pool is the one in use
					if (restart)
						applog(LOG_NOTICE, "%s %d caught up to new block",
						       work->longpoll ? "Longpoll from pool" : "Pool",
						       pool->pool_no);
				}
				else
				{
					// Switched to a block we know, but not the latest... why?
					// This might detect pools trying to double-spend or 51%,
					// but let's not make any accusations until it's had time
					// in the real world.
					char hexstr[65];
					blkhashstr(hexstr, prevblkhash);
					applog(LOG_WARNING, "%s %d is issuing work for an old block: %s",
					       work->longpoll ? "Longpoll from pool" : "Pool",
					       pool->pool_no,
					       hexstr);
				}
			}
		}
		if (work->longpoll)
		{
			struct pool * const cp = current_pool();
			++pool->work_restart_id;
			if (work->tr && work->tr == pool->swork.tr)
				pool->swork.work_restart_id = pool->work_restart_id;
			update_last_work(work);
			pool_update_work_restart_time(pool);
			applog(
			       ((!opt_quiet_work_updates) && pool_actively_in_use(pool, cp) ? LOG_NOTICE : LOG_DEBUG),
			       "Longpoll from pool %d requested work update",
				pool->pool_no);
			if ((!restart) && pool == cp)
				restart = true;
		}
		if (restart)
			restart_threads();
	}
	work->longpoll = false;
out_free:
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
	++work_mining_algorithm(work)->staged;
	if (work->spare)
		++staged_spare;
	if (likely(!getq->frozen)) {
		HASH_ADD_INT(staged_work, id, work);
		HASH_SORT(staged_work, tv_sort);
	} else
		rc = false;
	pthread_cond_broadcast(&getq->cond);
	mutex_unlock(stgd_lock);

	return rc;
}

static void stage_work(struct work *work)
{
	applog(LOG_DEBUG, "Pushing work %d from pool %d to hash queue",
	       work->id, work->pool->pool_no);
	work->work_restart_id = work->pool->work_restart_id;
	work->pool->last_work_time = time(NULL);
	cgtime(&work->pool->tv_last_work_time);
	test_work_current(work);
	work->pool->works++;
	hash_push(work);
}

#ifdef HAVE_CURSES
int curses_int(const char *query)
{
	int ret;
	char *cvar;

	cvar = curses_input(query);
	if (unlikely(!cvar))
		return -1;
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
	char xfer[ALLOC_H2B_NOUNIT+ALLOC_H2B_SPACED+4+1], bw[ALLOC_H2B_NOUNIT+ALLOC_H2B_SPACED+6+1];
	int pool_secs;

	if (curses_active_locked()) {
		wlog("Pool: %s  Goal: %s\n", pool->rpc_url, pool->goal->name);
		if (pool->solved)
			wlog("SOLVED %d BLOCK%s!\n", pool->solved, pool->solved > 1 ? "S" : "");
		if (!pool->has_stratum)
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
		pool_secs = timer_elapsed(&pool->cgminer_stats.start_tv, NULL);
		wlog(" Network transfer: %s  (%s)\n",
		     multi_format_unit2(xfer, sizeof(xfer), true, "B", H2B_SPACED, " / ", 2,
		                       (float)pool->cgminer_pool_stats.net_bytes_received,
		                       (float)pool->cgminer_pool_stats.net_bytes_sent),
		     multi_format_unit2(bw, sizeof(bw), true, "B/s", H2B_SPACED, " / ", 2,
		                       (float)(pool->cgminer_pool_stats.net_bytes_received / pool_secs),
		                       (float)(pool->cgminer_pool_stats.net_bytes_sent / pool_secs)));
		uint64_t pool_bytes_xfer = pool->cgminer_pool_stats.net_bytes_received + pool->cgminer_pool_stats.net_bytes_sent;
		efficiency = pool_bytes_xfer ? pool->diff_accepted * 2048. / pool_bytes_xfer : 0.0;
		wlog(" Efficiency (accepted * difficulty / 2 KB): %.2f\n", efficiency);

		wlog(" Items worked on: %d\n", pool->works);
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

	disable_pool(pool, POOL_DISABLED);
	
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

static
char *json_escape(const char *str)
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

static
void _write_config_string_elist(FILE *fcfg, const char *configname, struct string_elist * const elist)
{
	if (!elist)
		return;
	
	static struct string_elist *entry;
	fprintf(fcfg, ",\n\"%s\" : [", configname);
	bool first = true;
	DL_FOREACH(elist, entry)
	{
		const char * const s = entry->string;
		fprintf(fcfg, "%s\n\t\"%s\"", first ? "" : ",", json_escape(s));
		first = false;
	}
	fprintf(fcfg, "\n]");
}

void write_config(FILE *fcfg)
{
	int i;

	/* Write pool values */
	fputs("{\n\"pools\" : [", fcfg);
	for(i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->failover_only)
			// Don't write failover-only (automatically added) pools to the config file for now
			continue;
		
		if (pool->quota != 1) {
			fprintf(fcfg, "%s\n\t{\n\t\t\"quota\" : \"%d;%s\",", i > 0 ? "," : "",
				pool->quota,
				json_escape(pool->rpc_url));
		} else {
			fprintf(fcfg, "%s\n\t{\n\t\t\"url\" : \"%s\",", i > 0 ? "," : "",
				json_escape(pool->rpc_url));
		}
		if (pool->rpc_proxy)
			fprintf(fcfg, "\n\t\t\"pool-proxy\" : \"%s\",", json_escape(pool->rpc_proxy));
		fprintf(fcfg, "\n\t\t\"user\" : \"%s\",", json_escape(pool->rpc_user));
		fprintf(fcfg, "\n\t\t\"pass\" : \"%s\",", json_escape(pool->rpc_pass));
		if (strcmp(pool->goal->name, "default"))
			fprintf(fcfg, "\n\t\t\"pool-goal\" : \"%s\",", pool->goal->name);
		fprintf(fcfg, "\n\t\t\"pool-priority\" : \"%d\"", pool->prio);
		if (pool->force_rollntime)
			fprintf(fcfg, ",\n\t\t\"force-rollntime\" : %d", pool->force_rollntime);
		fprintf(fcfg, "\n\t}");
	}
	fputs("\n]\n", fcfg);

#ifdef USE_OPENCL
	write_config_opencl(fcfg);
#endif
#if defined(USE_CPUMINING) && defined(USE_SHA256D)
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
		free(name);
	}

	/* Special case options */
	if (request_target_str)
	{
		if (request_pdiff == (long)request_pdiff)
			fprintf(fcfg, ",\n\"request-diff\" : %ld", (long)request_pdiff);
		else
			fprintf(fcfg, ",\n\"request-diff\" : %f", request_pdiff);
	}
	fprintf(fcfg, ",\n\"shares\" : %g", opt_shares);
	if (pool_strategy == POOL_BALANCE)
		fputs(",\n\"balance\" : true", fcfg);
	if (pool_strategy == POOL_LOADBALANCE)
		fputs(",\n\"load-balance\" : true", fcfg);
	if (pool_strategy == POOL_ROUNDROBIN)
		fputs(",\n\"round-robin\" : true", fcfg);
	if (pool_strategy == POOL_ROTATE)
		fprintf(fcfg, ",\n\"rotate\" : \"%d\"", opt_rotate_period);
#if defined(unix) || defined(__APPLE__)
	if (opt_stderr_cmd && *opt_stderr_cmd)
		fprintf(fcfg, ",\n\"monitor\" : \"%s\"", json_escape(opt_stderr_cmd));
#endif // defined(unix)
	if (opt_kernel_path && *opt_kernel_path) {
		char *kpath = strdup(opt_kernel_path);
		if (kpath[strlen(kpath)-1] == '/')
			kpath[strlen(kpath)-1] = 0;
		fprintf(fcfg, ",\n\"kernel-path\" : \"%s\"", json_escape(kpath));
		free(kpath);
	}
	if (schedstart.enable)
		fprintf(fcfg, ",\n\"sched-time\" : \"%d:%d\"", schedstart.tm.tm_hour, schedstart.tm.tm_min);
	if (schedstop.enable)
		fprintf(fcfg, ",\n\"stop-time\" : \"%d:%d\"", schedstop.tm.tm_hour, schedstop.tm.tm_min);
	if (opt_socks_proxy && *opt_socks_proxy)
		fprintf(fcfg, ",\n\"socks-proxy\" : \"%s\"", json_escape(opt_socks_proxy));
	
	_write_config_string_elist(fcfg, "scan", scan_devices);
#ifdef USE_LIBMICROHTTPD
	if (httpsrv_port != -1)
		fprintf(fcfg, ",\n\"http-port\" : %d", httpsrv_port);
#endif
#ifdef USE_LIBEVENT
	if (stratumsrv_port != -1)
		fprintf(fcfg, ",\n\"stratum-port\" : %ld", stratumsrv_port);
#endif
	_write_config_string_elist(fcfg, "device", opt_devices_enabled_list);
	_write_config_string_elist(fcfg, "set-device", opt_set_device_list);
	
	if (opt_api_allow)
		fprintf(fcfg, ",\n\"api-allow\" : \"%s\"", json_escape(opt_api_allow));
	if (strcmp(opt_api_mcast_addr, API_MCAST_ADDR) != 0)
		fprintf(fcfg, ",\n\"api-mcast-addr\" : \"%s\"", json_escape(opt_api_mcast_addr));
	if (strcmp(opt_api_mcast_code, API_MCAST_CODE) != 0)
		fprintf(fcfg, ",\n\"api-mcast-code\" : \"%s\"", json_escape(opt_api_mcast_code));
	if (*opt_api_mcast_des)
		fprintf(fcfg, ",\n\"api-mcast-des\" : \"%s\"", json_escape(opt_api_mcast_des));
	if (strcmp(opt_api_description, PACKAGE_STRING) != 0)
		fprintf(fcfg, ",\n\"api-description\" : \"%s\"", json_escape(opt_api_description));
	if (opt_api_groups)
		fprintf(fcfg, ",\n\"api-groups\" : \"%s\"", json_escape(opt_api_groups));
	fputs("\n}\n", fcfg);

	json_escape_free();
}

void zero_bestshare(void)
{
	int i;

	best_diff = 0;
	suffix_string(best_diff, best_share, sizeof(best_share), 0);

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];
		pool->best_diff = 0;
	}
}

void zero_stats(void)
{
	int i;
	
	applog(LOG_DEBUG, "Zeroing stats");

	cgtime(&total_tv_start);
	miner_started = total_tv_start;
	total_rolling = 0;
	total_mhashes_done = 0;
	total_getworks = 0;
	total_accepted = 0;
	total_rejected = 0;
	hw_errors = 0;
	total_stale = 0;
	total_discarded = 0;
	total_bytes_rcvd = total_bytes_sent = 0;
	new_blocks = 0;
	local_work = 0;
	total_go = 0;
	total_ro = 0;
	total_secs = 1.0;
	total_diff1 = 0;
	total_bad_diff1 = 0;
	found_blocks = 0;
	total_diff_accepted = 0;
	total_diff_rejected = 0;
	total_diff_stale = 0;
#ifdef HAVE_CURSES
	awidth = rwidth = swidth = hwwidth = 1;
#endif

	struct mining_goal_info *goal, *tmpgoal;
	HASH_ITER(hh, mining_goals, goal, tmpgoal)
	{
		goal->diff_accepted = 0;
	}
	
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
		pool->works = 0;
		pool->diff1 = 0;
		pool->diff_accepted = 0;
		pool->diff_rejected = 0;
		pool->diff_stale = 0;
		pool->last_share_diff = 0;
		pool->cgminer_stats.start_tv = total_tv_start;
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
		cgpu->bad_diff1 = 0;
		cgpu->diff1 = 0;
		cgpu->diff_accepted = 0;
		cgpu->diff_rejected = 0;
		cgpu->diff_stale = 0;
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
		cgpu->cgminer_stats.start_tv = total_tv_start;
		cgpu->cgminer_stats.getwork_calls = 0;
		cgpu->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		cgpu->cgminer_stats.getwork_wait_max.tv_sec = 0;
		cgpu->cgminer_stats.getwork_wait_max.tv_usec = 0;
		mutex_unlock(&hash_lock);
		
		if (cgpu->drv->zero_stats)
			cgpu->drv->zero_stats(cgpu);
	}
}

int bfg_strategy_parse(const char * const s)
{
	char *endptr;
	if (!(s && s[0]))
		return -1;
	long int selected = strtol(s, &endptr, 0);
	if (endptr == s || *endptr) {
		// Look-up by name
		selected = -1;
		for (unsigned i = 0; i <= TOP_STRATEGY; ++i) {
			if (!strcasecmp(strategies[i].s, s)) {
				selected = i;
			}
		}
	}
	if (selected < 0 || selected > TOP_STRATEGY) {
		return -1;
	}
	return selected;
}

bool bfg_strategy_change(const int selected, const char * const param)
{
	if (param && param[0]) {
		switch (selected) {
			case POOL_ROTATE:
			{
				char *endptr;
				long int n = strtol(param, &endptr, 0);
				if (n < 0 || n > 9999 || *endptr) {
					return false;
				}
				opt_rotate_period = n;
				break;
			}
			default:
				return false;
		}
	}
	
	mutex_lock(&lp_lock);
	pool_strategy = selected;
	pthread_cond_broadcast(&lp_cond);
	mutex_unlock(&lp_lock);
	switch_pools(NULL);
	
	return true;
}

#ifdef HAVE_CURSES
static
void loginput_mode(const int size)
{
	clear_logwin();
	loginput_size = size;
	check_winsizes();
}

static void display_pools(void)
{
	struct pool *pool;
	int selected, i, j;
	char input;

	loginput_mode(7 + total_pools);
	immedok(logwin, true);
updated:
	for (j = 0; j < total_pools; j++) {
		for (i = 0; i < total_pools; i++) {
			pool = pools[i];

			if (pool->prio != j)
				continue;

			if (pool_actively_in_use(pool, NULL))
				wattron(logwin, A_BOLD);
			if (pool->enabled != POOL_ENABLED || pool->failover_only)
				wattron(logwin, A_DIM);
			wlogprint("%d: ", pool->prio);
			switch (pool->enabled) {
				case POOL_ENABLED:
					if ((pool_strategy == POOL_LOADBALANCE) ? (!pool->quota)
					    : ((pool_strategy != POOL_FAILOVER) ? pool->failover_only : 0))
						wlogprint("Failover ");
					else
						wlogprint("Enabled  ");
					break;
				case POOL_DISABLED:
					wlogprint("Disabled ");
					break;
				case POOL_REJECTING:
					wlogprint("Rejectin ");
					break;
				case POOL_MISBEHAVING:
					wlogprint("Misbehav ");
					break;
			}
			_wlogprint(pool_proto_str(pool));
			wlogprint(" Quota %d Pool %d: %s  User:%s\n",
				pool->quota,
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
	wlogprint("Pool [A]dd [R]emove [D]isable [E]nable [P]rioritize [Q]uota change\n");
	wlogprint("[C]hange management strategy [S]witch pool [I]nformation\n");
	wlogprint("Or press any other key to continue\n");
	logwin_update();
	input = getch();

	if (!strncasecmp(&input, "a", 1)) {
		if (opt_benchmark)
		{
			wlogprint("Cannot add pools in benchmark mode");
			goto retry;
		}
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
		remove_pool(pool);
		goto updated;
	} else if (!strncasecmp(&input, "s", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		manual_enable_pool(pool);
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
		disable_pool(pool, POOL_DISABLED);
		goto updated;
	} else if (!strncasecmp(&input, "e", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		manual_enable_pool(pool);
		goto updated;
	} else if (!strncasecmp(&input, "c", 1)) {
		for (i = 0; i <= TOP_STRATEGY; i++)
			wlogprint("%d: %s\n", i, strategies[i].s);
		{
			char * const selected_str = curses_input("Select strategy type");
			selected = bfg_strategy_parse(selected_str);
			free(selected_str);
		}
		if (selected < 0 || selected > TOP_STRATEGY) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		char *param = NULL;
		if (selected == POOL_ROTATE) {
			param = curses_input("Select interval in minutes");
		}
		bool result = bfg_strategy_change(selected, param);
		free(param);
		if (!result) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
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
	} else if (!strncasecmp(&input, "q", 1)) {
		selected = curses_int("Select pool number");
		if (selected < 0 || selected >= total_pools) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		pool = pools[selected];
		selected = curses_int("Set quota");
		if (selected < 0) {
			wlogprint("Invalid negative quota\n");
			goto retry;
		}
		if (selected > 0)
			pool->failover_only = false;
		pool->quota = selected;
		adjust_quota_gcd();
		goto updated;
	} else if (!strncasecmp(&input, "f", 1)) {
		opt_fail_only ^= true;
		goto updated;
        } else if (!strncasecmp(&input, "p", 1)) {
			char *prilist = curses_input("Enter new pool priority (comma separated list)");
			if (!prilist)
			{
				wlogprint("Not changing priorities\n");
				goto retry;
			}
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
	}

	immedok(logwin, false);
	loginput_mode(0);
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

	immedok(logwin, true);
	loginput_mode(12);
retry:
	clear_logwin();
	wlogprint("[N]ormal [C]lear [S]ilent mode (disable all output)\n");
	wlogprint("[D]ebug:%s\n[P]er-device:%s\n[Q]uiet:%s\n[V]erbose:%s\n"
		  "[R]PC debug:%s\n[W]orkTime details:%s\nsu[M]mary detail level:%s\n"
		  "[L]og interval:%d\nS[T]atistical counts: %s\n[Z]ero statistics\n",
		opt_debug_console ? "on" : "off",
	        want_per_device_stats? "on" : "off",
		opt_quiet ? "on" : "off",
		opt_log_output ? "on" : "off",
		opt_protocol ? "on" : "off",
		opt_worktime ? "on" : "off",
		summary_detail_level_str(),
		opt_log_interval,
		opt_weighed_stats ? "weighed" : "absolute");
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
		switch_logsize();
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
		switch_logsize();
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
	} else if (!strncasecmp(&input, "t", 1)) {
		opt_weighed_stats ^= true;
		wlogprint("Now displaying %s statistics\n", opt_weighed_stats ? "weighed" : "absolute");
		goto retry;
	} else if (!strncasecmp(&input, "z", 1)) {
		zero_stats();
		goto retry;
	}

	immedok(logwin, false);
	loginput_mode(0);
}
#endif

void default_save_file(char *filename)
{
#if defined(unix) || defined(__APPLE__)
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

	immedok(logwin, true);
	loginput_mode(8);
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
		snprintf(prompt, sizeof(prompt), "Config filename to write (Enter for default) [%s]", filename);
		str = curses_input(prompt);
		if (str) {
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

	loginput_mode(0);
	immedok(logwin, false);
}

int scan_serial(const char *);

static
void _managetui_msg(const char *repr, const char **msg)
{
	if (*msg)
	{
		applog(LOG_DEBUG, "ManageTUI: %"PRIpreprv": %s", repr, *msg);
		wattron(logwin, A_BOLD);
		wlogprint("%s", *msg);
		wattroff(logwin, A_BOLD);
		*msg = NULL;
	}
	logwin_update();
}

void manage_device(void)
{
	char logline[256];
	const char *msg = NULL;
	struct cgpu_info *cgpu;
	const struct device_drv *drv;
	
	selecting_device = true;
	immedok(logwin, true);
	loginput_mode(12);
	
devchange:
	if (unlikely(!total_devices))
	{
		clear_logwin();
		wlogprint("(no devices)\n");
		wlogprint("[Plus] Add device(s)  [Enter] Close device manager\n");
		_managetui_msg("(none)", &msg);
		int input = getch();
		switch (input)
		{
			case '+':  case '=':  // add new device
				goto addnew;
			default:
				goto out;
		}
	}
	
	cgpu = devices[selected_device];
	drv = cgpu->drv;
	refresh_devstatus();
	
refresh:
	clear_logwin();
	wlogprint("Select processor to manage using up/down arrow keys\n");
	
	get_statline3(logline, sizeof(logline), cgpu, true, true);
	wattron(logwin, A_BOLD);
	wlogprint("%s", logline);
	wattroff(logwin, A_BOLD);
	wlogprint("\n");
	
	if (cgpu->dev_manufacturer)
		wlogprint("  %s from %s\n", (cgpu->dev_product ?: "Device"), cgpu->dev_manufacturer);
	else
	if (cgpu->dev_product)
		wlogprint("  %s\n", cgpu->dev_product);
	
	if (cgpu->dev_serial)
		wlogprint("Serial: %s\n", cgpu->dev_serial);
	
	if (cgpu->kname)
		wlogprint("Kernel: %s\n", cgpu->kname);
	
	if (drv->proc_wlogprint_status && likely(cgpu->status != LIFE_INIT))
		drv->proc_wlogprint_status(cgpu);
	
	wlogprint("\n");
	// TODO: Last share at TIMESTAMP on pool N
	// TODO: Custom device info/commands
	if (cgpu->deven != DEV_ENABLED)
		wlogprint("[E]nable ");
	if (cgpu->deven != DEV_DISABLED)
		wlogprint("[D]isable ");
	if (drv->identify_device)
		wlogprint("[I]dentify ");
	if (drv->proc_tui_wlogprint_choices && likely(cgpu->status != LIFE_INIT))
		drv->proc_tui_wlogprint_choices(cgpu);
	wlogprint("\n");
	wlogprint("[Slash] Find processor  [Plus] Add device(s)  [Enter] Close device manager\n");
	_managetui_msg(cgpu->proc_repr, &msg);
	
	while (true)
	{
		int input = getch();
		applog(LOG_DEBUG, "ManageTUI: %"PRIpreprv": (choice %d)", cgpu->proc_repr, input);
		switch (input) {
			case 'd': case 'D':
				if (cgpu->deven == DEV_DISABLED)
					msg = "Processor already disabled\n";
				else
				{
					cgpu->deven = DEV_DISABLED;
					msg = "Processor being disabled\n";
				}
				goto refresh;
			case 'e': case 'E':
				if (cgpu->deven == DEV_ENABLED)
					msg = "Processor already enabled\n";
				else
				{
					proc_enable(cgpu);
					msg = "Processor being enabled\n";
				}
				goto refresh;
			case 'i': case 'I':
				if (drv->identify_device && drv->identify_device(cgpu))
					msg = "Identify command sent\n";
				else
					goto key_default;
				goto refresh;
			case KEY_DOWN:
				if (selected_device >= total_devices - 1)
					break;
				++selected_device;
				goto devchange;
			case KEY_UP:
				if (selected_device <= 0)
					break;
				--selected_device;
				goto devchange;
			case KEY_NPAGE:
			{
				if (selected_device >= total_devices - 1)
					break;
				struct cgpu_info *mdev = devices[selected_device]->device;
				do {
					++selected_device;
				} while (devices[selected_device]->device == mdev && selected_device < total_devices - 1);
				goto devchange;
			}
			case KEY_PPAGE:
			{
				if (selected_device <= 0)
					break;
				struct cgpu_info *mdev = devices[selected_device]->device;
				do {
					--selected_device;
				} while (devices[selected_device]->device == mdev && selected_device > 0);
				goto devchange;
			}
			case '/':  case '?':  // find device
			{
				static char *pattern = NULL;
				char *newpattern = curses_input("Enter pattern");
				if (newpattern)
				{
					free(pattern);
					pattern = newpattern;
				}
				else
				if (!pattern)
					pattern = calloc(1, 1);
				int match = cgpu_search(pattern, selected_device + 1);
				if (match == -1)
				{
					msg = "Couldn't find device\n";
					goto refresh;
				}
				selected_device = match;
				goto devchange;
			}
			case '+':  case '=':  // add new device
			{
addnew:
				clear_logwin();
				_wlogprint(
					"Enter \"auto\", \"all\", or a serial port to probe for mining devices.\n"
					"Prefix by a driver name and colon to only probe a specific driver.\n"
					"For example: erupter:"
#ifdef WIN32
					"\\\\.\\COM40"
#elif defined(__APPLE__)
					"/dev/cu.SLAB_USBtoUART"
#else
					"/dev/ttyUSB39"
#endif
					"\n"
				);
				char *scanser = curses_input("Enter target");
				if (scan_serial(scanser))
				{
					selected_device = total_devices - 1;
					msg = "Device scan succeeded\n";
				}
				else
					msg = "No new devices found\n";
				goto devchange;
			}
			case 'Q': case 'q':
			case KEY_BREAK: case KEY_BACKSPACE: case KEY_CANCEL: case KEY_CLOSE: case KEY_EXIT:
			case '\x1b':  // ESC
			case KEY_ENTER:
			case '\r':  // Ctrl-M on Windows, with nonl
#ifdef PADENTER
			case PADENTER:  // pdcurses, used by Enter key on Windows with nonl
#endif
			case '\n':
				goto out;
			default:
				;
key_default:
				if (drv->proc_tui_handle_choice && likely(drv_ready(cgpu)))
				{
					msg = drv->proc_tui_handle_choice(cgpu, input);
					if (msg)
						goto refresh;
				}
		}
	}

out:
	selecting_device = false;
	loginput_mode(0);
	immedok(logwin, false);
}

void show_help(void)
{
	loginput_mode(11);
	
	// NOTE: wlogprint is a macro with a buffer limit
	_wlogprint(
		"LU: oldest explicit work update currently being used for new work\n"
		"ST: work in queue              | F: network fails   | NB: new blocks detected\n"
		"AS: shares being submitted     | BW: bandwidth (up/down)\n"
		"E: # shares * diff per 2kB bw  | I: expected income | BS: best share ever found\n"
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_BTEE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_BTEE  U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE U8_HLINE
		"\n"
		"devices/processors hashing (only for totals line), hottest temperature\n"
	);
	wlogprint(
		"hashrates: %ds decaying / all-time average / all-time average (effective)\n"
		, opt_log_interval);
	_wlogprint(
		"A: accepted shares | R: rejected+discarded(% of total)\n"
		"HW: hardware errors / % nonces invalid\n"
		"\n"
		"Press any key to clear"
	);
	
	logwin_update();
	getch();
	
	loginput_mode(0);
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
			case 'h': case 'H': case '?':
			case KEY_F(1):
				show_help();
				break;
		case 'q': case 'Q':
			kill_work();
			return NULL;
		case 'd': case 'D':
			display_options();
			break;
		case 'm': case 'M':
			manage_device();
			break;
		case 'p': case 'P':
			display_pools();
			break;
		case 's': case 'S':
			set_options();
			break;
#ifdef HAVE_CURSES
		case KEY_DOWN:
		{
			const int visible_lines = logcursor - devcursor;
			const int invisible_lines = total_lines - visible_lines;
			if (devsummaryYOffset <= -invisible_lines)
				break;
			devsummaryYOffset -= 2;
		}
		case KEY_UP:
			if (devsummaryYOffset == 0)
				break;
			++devsummaryYOffset;
			refresh_devstatus();
			break;
		case KEY_NPAGE:
		{
			const int visible_lines = logcursor - devcursor;
			const int invisible_lines = total_lines - visible_lines;
			if (devsummaryYOffset - visible_lines <= -invisible_lines)
				devsummaryYOffset = -invisible_lines;
			else
				devsummaryYOffset -= visible_lines;
			refresh_devstatus();
			break;
		}
		case KEY_PPAGE:
		{
			const int visible_lines = logcursor - devcursor;
			if (devsummaryYOffset + visible_lines >= 0)
				devsummaryYOffset = 0;
			else
				devsummaryYOffset += visible_lines;
			refresh_devstatus();
			break;
		}
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
	char logstatusline[256];
	struct timeval temp_tv_end, total_diff;
	double secs;
	double local_secs;
	static double local_mhashes_done = 0;
	double local_mhashes = (double)hashes_done / 1000000.0;
	bool showlog = false;
	char cHr[ALLOC_H2B_NOUNIT+1], aHr[ALLOC_H2B_NOUNIT+1], uHr[ALLOC_H2B_SPACED+3+1];
	char rejpcbuf[6];
	char bnbuf[6];
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
		decay_time(&thr->rolling, local_mhashes / secs, secs);
		for (i = 0; i < threadobj; i++)
			thread_rolling += cgpu->thr[i]->rolling;

		mutex_lock(&hash_lock);
		decay_time(&cgpu->rolling, thread_rolling, secs);
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

				get_statline(logline, sizeof(logline), cgpu);
				if (!curses_active) {
					printf("\n%s\r", logline);
					fflush(stdout);
				} else
					applog(LOG_INFO, "%s", logline);
			}
		}
	}

	/* Totals are updated by all threads so can race without locking */
	mutex_lock(&hash_lock);
	cgtime(&temp_tv_end);
	
	timersub(&temp_tv_end, &total_tv_start, &total_diff);
	total_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);
	
	timersub(&temp_tv_end, &total_tv_end, &total_diff);

	total_mhashes_done += local_mhashes;
	local_mhashes_done += local_mhashes;
	/* Only update with opt_log_interval */
	if (total_diff.tv_sec < opt_log_interval)
		goto out_unlock;
	showlog = true;
	cgtime(&total_tv_end);

	local_secs = (double)total_diff.tv_sec + ((double)total_diff.tv_usec / 1000000.0);
	decay_time(&total_rolling, local_mhashes_done / local_secs, local_secs);
	global_hashrate = ((unsigned long long)lround(total_rolling)) * 1000000;

	double wtotal = (total_diff_accepted + total_diff_rejected + total_diff_stale);
	
	multi_format_unit_array2(
		((char*[]){cHr, aHr, uHr}),
		((size_t[]){sizeof(cHr), sizeof(aHr), sizeof(uHr)}),
		true, "h/s", H2B_SHORT,
		3,
		1e6*total_rolling,
		1e6*total_mhashes_done / total_secs,
		utility_to_hashrate(total_diff1 * (wtotal ? (total_diff_accepted / wtotal) : 1) * 60 / total_secs));

	int ui_accepted, ui_rejected, ui_stale;
	if (opt_weighed_stats)
	{
		ui_accepted = total_diff_accepted;
		ui_rejected = total_diff_rejected;
		ui_stale = total_diff_stale;
	}
	else
	{
		ui_accepted = total_accepted;
		ui_rejected = total_rejected;
		ui_stale = total_stale;
	}
	
#ifdef HAVE_CURSES
	if (curses_active_locked()) {
		float temp = 0;
		struct cgpu_info *proc, *last_working_dev = NULL;
		int i, working_devs = 0, working_procs = 0;
		int divx;
		bool bad = false;
		
		// Find the highest temperature of all processors
		for (i = 0; i < total_devices; ++i)
		{
			proc = get_devices(i);
			
			if (proc->temp > temp)
				temp = proc->temp;
			
			if (unlikely(proc->deven == DEV_DISABLED))
				;  // Just need to block it off from both conditions
			else
			if (likely(proc->status == LIFE_WELL && proc->deven == DEV_ENABLED))
			{
				if (proc->rolling > .1)
				{
					++working_procs;
					if (proc->device != last_working_dev)
					{
						++working_devs;
						last_working_dev = proc->device;
					}
				}
			}
			else
				bad = true;
		}
		
		if (working_devs == working_procs)
			snprintf(statusline, sizeof(statusline), "%s%d        ", bad ? U8_BAD_START : "", working_devs);
		else
			snprintf(statusline, sizeof(statusline), "%s%d/%d     ", bad ? U8_BAD_START : "", working_devs, working_procs);
		
		divx = 7;
		if (opt_show_procs && !opt_compact)
			divx += max_lpdigits;
		
		if (bad)
		{
			divx += sizeof(U8_BAD_START)-1;
			strcpy(&statusline[divx], U8_BAD_END);
			divx += sizeof(U8_BAD_END)-1;
		}
		
		temperature_column(&statusline[divx], sizeof(statusline)-divx, true, &temp);
		
		format_statline(statusline, sizeof(statusline),
		                cHr, aHr,
		                uHr,
		                ui_accepted,
		                ui_rejected,
		                ui_stale,
		                total_diff_rejected + total_diff_stale, total_diff_accepted,
		                hw_errors,
		                total_bad_diff1, total_bad_diff1 + total_diff1);
		unlock_curses();
	}
#endif
	
	// Add a space
	memmove(&uHr[6], &uHr[5], strlen(&uHr[5]) + 1);
	uHr[5] = ' ';
	
	percentf4(rejpcbuf, sizeof(rejpcbuf), total_diff_rejected + total_diff_stale, total_diff_accepted);
	percentf4(bnbuf, sizeof(bnbuf), total_bad_diff1, total_diff1);
	
	snprintf(logstatusline, sizeof(logstatusline),
	         "%s%ds:%s avg:%s u:%s | A:%d R:%d+%d(%s) HW:%d/%s",
		want_per_device_stats ? "ALL " : "",
		opt_log_interval,
		cHr, aHr,
		uHr,
		ui_accepted,
		ui_rejected,
		ui_stale,
		rejpcbuf,
		hw_errors,
		bnbuf
	);


	local_mhashes_done = 0;
out_unlock:
	mutex_unlock(&hash_lock);

	if (showlog) {
		if (!curses_active) {
			if (want_per_device_stats)
				printf("\n%s\r", logstatusline);
			else
			{
				const int logstatusline_len = strlen(logstatusline);
				int padding;
				if (last_logstatusline_len > logstatusline_len)
					padding = (last_logstatusline_len - logstatusline_len);
				else
				{
					padding = 0;
					if (last_logstatusline_len == -1)
						puts("");
				}
				printf("%s%*s\r", logstatusline, padding, "");
				last_logstatusline_len = logstatusline_len;
			}
			fflush(stdout);
		} else
			applog(LOG_INFO, "%s", logstatusline);
	}
}

void hashmeter2(struct thr_info *thr)
{
	struct timeval tv_now, tv_elapsed;
	
	timerclear(&thr->tv_hashes_done);
	
	cgtime(&tv_now);
	timersub(&tv_now, &thr->tv_lastupdate, &tv_elapsed);
	/* Update the hashmeter at most 5 times per second */
	if ((thr->hashes_done && (tv_elapsed.tv_sec > 0 || tv_elapsed.tv_usec > 200000)) ||
	    tv_elapsed.tv_sec >= opt_log_interval) {
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
		 && !strncmp(json_string_value(id_val), "txlist", 6))
		{
			const bool is_array = json_is_array(res_val);
			applog(LOG_DEBUG, "Received %s for pool %u job %s",
			       is_array ? "transaction list" : "no-transaction-list response",
			       pool->pool_no, &json_string_value(id_val)[6]);
			if (!is_array)
			{
				// No need to wait for a timeout
				timer_unset(&pool->swork.tv_transparency);
				pool_set_opaque(pool, true);
				goto fishy;
			}
			if (strcmp(json_string_value(id_val) + 6, pool->swork.job_id))
				// We only care about a transaction list for the current job id
				goto fishy;
			
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

			pool_set_opaque(pool, false);
			timer_unset(&pool->swork.tv_transparency);

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
		pool_diff = target_diff(pool->swork.target);
		cg_runlock(&pool->data_lock);

		if (json_is_true(res_val)) {
			struct mining_goal_info * const goal = pool->goal;
			
			applog(LOG_NOTICE, "Accepted untracked stratum share from pool %d", pool->pool_no);

			/* We don't know what device this came from so we can't
			 * attribute the work to the relevant cgpu */
			mutex_lock(&stats_lock);
			total_accepted++;
			pool->accepted++;
			total_diff_accepted += pool_diff;
			pool->diff_accepted += pool_diff;
			goal->diff_accepted += pool_diff;
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
	int my_mining_threads = mining_threads;  // Cached outside of locking
	struct stratum_share *sshare, *tmpshare;
	struct work *work;
	struct cgpu_info *cgpu;
	double diff_cleared = 0;
	double thr_diff_cleared[my_mining_threads];
	int cleared = 0;
	int thr_cleared[my_mining_threads];
	
	// NOTE: This is per-thread rather than per-device to avoid getting devices lock in stratum_shares loop
	for (int i = 0; i < my_mining_threads; ++i)
	{
		thr_diff_cleared[i] = 0;
		thr_cleared[i] = 0;
	}

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		work = sshare->work;
		if (sshare->work->pool == pool && work->thr_id < my_mining_threads) {
			HASH_DEL(stratum_shares, sshare);
			
			sharelog("disconnect", work);
			
			diff_cleared += sshare->work->work_difficulty;
			thr_diff_cleared[work->thr_id] += work->work_difficulty;
			++thr_cleared[work->thr_id];
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
		for (int i = 0; i < my_mining_threads; ++i)
			if (thr_cleared[i])
			{
				cgpu = get_thr_cgpu(i);
				cgpu->diff_stale += thr_diff_cleared[i];
				cgpu->stale += thr_cleared[i];
			}
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
			unstage_work(work);
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

	// We want to keep a connection open for rejecting or misbehaving pools, to detect when/if they change their tune
	if (pool->enabled == POOL_DISABLED)
		return false;

	/* Idle stratum pool needs something to kick it alive again */
	if (pool->has_stratum && pool->idle)
		return true;

	/* Getwork pools without opt_fail_only need backup pools up to be able
	 * to leak shares */
	cp = current_pool();
	if (pool_actively_desired(pool, cp))
		return true;
	if (!pool_localgen(cp) && (!opt_fail_only || !cp->hdr_path))
		return true;

	/* Keep the connection open to allow any stray shares to be submitted
	 * on switching pools for 2 minutes. */
	if (timer_elapsed(&pool->tv_last_work_time, NULL) < 120)
		return true;

	/* If the pool has only just come to life and is higher priority than
	 * the current pool keep the connection open so we can fail back to
	 * it. */
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		return true;

	if (pool_unworkable(cp))
		return true;
	
	/* We've run out of work, bring anything back to life. */
	if (no_work)
		return true;
	
	// If the current pool lacks its own block change detection, see if we are needed for that
	if (pool_active_lp_pool(cp) == pool)
		return true;

	return false;
}

static void wait_lpcurrent(struct pool *pool);
static void pool_resus(struct pool *pool);

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

static bool pools_active;

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
		int sock;

		if (unlikely(!pool->has_stratum))
			break;

		/* Check to see whether we need to maintain this connection
		 * indefinitely or just bring it up when we switch to this
		 * pool */
		while (true)
		{
			sock = pool->sock;
			
			if (sock == INVSOCK)
				applog(LOG_DEBUG, "Pool %u: Invalid socket, suspending",
				       pool->pool_no);
			else
			if (!sock_full(pool) && !cnx_needed(pool) && pools_active)
				applog(LOG_DEBUG, "Pool %u: Connection not needed, suspending",
				       pool->pool_no);
			else
				break;
			
			suspend_stratum(pool);
			clear_stratum_shares(pool);
			clear_pool_work(pool);

			wait_lpcurrent(pool);
			if (!restart_stratum(pool)) {
				pool_died(pool);
				while (!restart_stratum(pool)) {
					if (pool->removed)
						goto out;
					cgsleep_ms(30000);
				}
			}
		}

		FD_ZERO(&rd);
		FD_SET(sock, &rd);
		timeout.tv_sec = 120;
		timeout.tv_usec = 0;

		/* If we fail to receive any notify messages for 2 minutes we
		 * assume the connection has been dropped and treat this pool
		 * as dead */
		if (!sock_full(pool) && (sel_ret = select(sock + 1, &rd, NULL, NULL, &timeout)) < 1) {
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

			mutex_lock(&pool->stratum_lock);
			pool->stratum_active = pool->stratum_notify = false;
			pool->sock = INVSOCK;
			mutex_unlock(&pool->stratum_lock);

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
			uint8_t *bin_height = &bytes_buf(&pool->swork.coinbase)[4 /*version*/ + 1 /*txin count*/ + 36 /*prevout*/ + 1 /*scriptSig len*/ + 1 /*push opcode*/];
			unsigned char cb_height_sz;
			cb_height_sz = bin_height[-1];
			if (cb_height_sz == 3) {
				// FIXME: The block number will overflow this by AD 2173
				struct mining_goal_info * const goal = pool->goal;
				const void * const prevblkhash = &work->data[4];
				uint32_t height = 0;
				memcpy(&height, bin_height, 3);
				height = le32toh(height);
				have_block_height(goal, prevblkhash, height);
			}

			pool->swork.work_restart_id =
			++pool->work_restart_id;
			pool_update_work_restart_time(pool);
			if (test_work_current(work)) {
				/* Only accept a work update if this stratum
				 * connection is from the current pool */
				struct pool * const cp = current_pool();
				if (pool == cp)
					restart_threads();
				
				applog(
				       ((!opt_quiet_work_updates) && pool_actively_in_use(pool, cp) ? LOG_NOTICE : LOG_DEBUG),
				       "Stratum from pool %d requested work update", pool->pool_no);
			} else
				applog(LOG_NOTICE, "Stratum from pool %d detected new block", pool->pool_no);
			free_work(work);
		}

		if (timer_passed(&pool->swork.tv_transparency, NULL)) {
			// More than 4 timmills past since requested transactions
			timer_unset(&pool->swork.tv_transparency);
			pool_set_opaque(pool, true);
		}
	}

out:
	return NULL;
}

static void init_stratum_thread(struct pool *pool)
{
	struct mining_goal_info * const goal = pool->goal;
	goal->have_longpoll = true;

	if (unlikely(pthread_create(&pool->stratum_thread, NULL, stratum_thread, (void *)pool)))
		quit(1, "Failed to create stratum thread");
}

static void *longpoll_thread(void *userdata);

static bool stratum_works(struct pool *pool)
{
	applog(LOG_INFO, "Testing pool %d stratum %s", pool->pool_no, pool->stratum_url);
	if (!extract_sockaddr(pool->stratum_url, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	if (pool->stratum_active)
		return true;
	
	if (!initiate_stratum(pool))
		return false;

	return true;
}

static
bool pool_recently_got_work(struct pool * const pool, const struct timeval * const tvp_now)
{
	return (timer_isset(&pool->tv_last_work_time) && timer_elapsed(&pool->tv_last_work_time, tvp_now) < 60);
}

static bool pool_active(struct pool *pool, bool pinging)
{
	struct timeval tv_now, tv_getwork, tv_getwork_reply;
	bool ret = false;
	json_t *val;
	CURL *curl = NULL;
	int rolltime;
	char *rpc_req;
	struct work *work;
	enum pool_protocol proto;

	if (pool->stratum_init)
	{
		if (pool->stratum_active)
			return true;
	}
	else
	if (!pool->idle)
	{
		timer_set_now(&tv_now);
		if (pool_recently_got_work(pool, &tv_now))
			return true;
	}
	
	mutex_lock(&pool->pool_test_lock);
	
	if (pool->stratum_init)
	{
		ret = pool->stratum_active;
		goto out;
	}
	
	timer_set_now(&tv_now);
	
	if (pool->idle)
	{
		if (timer_elapsed(&pool->tv_idle, &tv_now) < 30)
			goto out;
	}
	else
	if (pool_recently_got_work(pool, &tv_now))
	{
		ret = true;
		goto out;
	}
	
		applog(LOG_INFO, "Testing pool %s", pool->rpc_url);

	/* This is the central point we activate stratum when we can */
	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		goto out;
	}

	if (!(want_gbt || want_getwork))
		goto nohttp;

	work = make_work();

	/* Probe for GBT support on first pass */
	proto = want_gbt ? PLP_GETBLOCKTEMPLATE : PLP_GETWORK;

tryagain:
	rpc_req = prepare_rpc_req_probe(work, proto, NULL, pool);
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
	if (pool->stratum_url && want_stratum && pool_may_redirect_to(pool, pool->stratum_url) && (pool->has_stratum || stratum_works(pool))) {
		if (!pool->has_stratum) {

		applog(LOG_NOTICE, "Switching pool %d %s to %s", pool->pool_no, pool->rpc_url, pool->stratum_url);
		if (!pool->rpc_url)
			pool_set_uri(pool, strdup(pool->stratum_url));
		pool->has_stratum = true;

		}

		free_work(work);
		if (val)
			json_decref(val);

retry_stratum:
		;
		/* We create the stratum thread for each pool just after
		 * successful authorisation. Once the init flag has been set
		 * we never unset it and the stratum thread is responsible for
		 * setting/unsetting the active flag */
		bool init = pool_tset(pool, &pool->stratum_init);

		if (!init) {
			ret = initiate_stratum(pool) && auth_stratum(pool);

			if (ret)
			{
				detect_algo = 2;
				init_stratum_thread(pool);
			}
			else
			{
				pool_tclear(pool, &pool->stratum_init);
				pool->tv_idle = tv_getwork_reply;
			}
			goto out;
		}
		ret = pool->stratum_active;
		goto out;
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

			stage_work(work);
			total_getworks++;
			pool->getwork_requested++;
			ret = true;
			pool->tv_idle = tv_getwork_reply;
		} else {
badwork:
			json_decref(val);
			applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			pool->proto = proto = pool_protocol_fallback(proto);
			if (PLP_NONE != proto)
				goto tryagain;
			pool->tv_idle = tv_getwork_reply;
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
		if (work->tr && (lp = blktmpl_get_longpoll(work->tr->tmpl))) {
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
		pool->tv_idle = tv_getwork_reply;
		free_work(work);
nohttp:
		/* If we failed to parse a getwork, this could be a stratum
		 * url without the prefix stratum+tcp:// so let's check it */
		if (extract_sockaddr(pool->rpc_url, &pool->sockaddr_url, &pool->stratum_port) && initiate_stratum(pool)) {
			pool->has_stratum = true;
			goto retry_stratum;
		}
		applog(LOG_DEBUG, "FAILED to retrieve work from pool %u %s",
		       pool->pool_no, pool->rpc_url);
		if (!pinging)
			applog(LOG_WARNING, "Pool %u slow/down or URL or credentials invalid", pool->pool_no);
	}
out:
	if (curl)
		curl_easy_cleanup(curl);
	mutex_unlock(&pool->pool_test_lock);
	return ret;
}

static void pool_resus(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED && pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		applog(LOG_WARNING, "Pool %d %s alive, testing stability", pool->pool_no, pool->rpc_url);
	else
		applog(LOG_INFO, "Pool %d %s alive", pool->pool_no, pool->rpc_url);
}

static
void *cmd_idle_thread(void * const __maybe_unused userp)
{
	pthread_detach(pthread_self());
	RenameThread("cmd-idle");
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	
	sleep(opt_log_interval);
	pthread_testcancel();
	run_cmd(cmd_idle);
	
	return NULL;
}

static struct work *hash_pop(struct cgpu_info * const proc)
{
	int hc;
	struct work *work, *work_found, *tmp;
	enum {
		HPWS_NONE,
		HPWS_LOWDIFF,
		HPWS_SPARE,
		HPWS_ROLLABLE,
		HPWS_PERFECT,
	} work_score = HPWS_NONE;
	bool did_cmd_idle = false;
	pthread_t cmd_idle_thr;

retry:
	mutex_lock(stgd_lock);
	while (true)
	{
		work_found = NULL;
		work_score = 0;
		hc = HASH_COUNT(staged_work);
		HASH_ITER(hh, staged_work, work, tmp)
		{
			const struct mining_algorithm * const work_malgo = work_mining_algorithm(work);
			const float min_nonce_diff = drv_min_nonce_diff(proc->drv, proc, work_malgo);
#define FOUND_WORK(score)  do{  \
				if (work_score < score)  \
				{  \
					work_found = work;  \
					work_score = score;  \
				}  \
				continue;  \
}while(0)
			if (min_nonce_diff < work->work_difficulty)
			{
				if (min_nonce_diff < 0)
					continue;
				FOUND_WORK(HPWS_LOWDIFF);
			}
			if (work->spare)
				FOUND_WORK(HPWS_SPARE);
			if (work->rolltime && hc > staged_rollable)
				FOUND_WORK(HPWS_ROLLABLE);
#undef FOUND_WORK
			
			// Good match
			work_found = work;
			work_score = HPWS_PERFECT;
			break;
		}
		if (work_found)
		{
			work = work_found;
			break;
		}
		
		// Failed to get a usable work
		if (unlikely(staged_full))
		{
			if (likely(opt_queue < 10 + mining_threads))
			{
				++opt_queue;
				applog(LOG_WARNING, "Staged work underrun; increasing queue minimum to %d", opt_queue);
			}
			else
				applog(LOG_WARNING, "Staged work underrun; not automatically increasing above %d", opt_queue);
			staged_full = false;  // Let it fill up before triggering an underrun again
			no_work = true;
		}
		pthread_cond_signal(&gws_cond);
		
		if (cmd_idle && !did_cmd_idle)
		{
			if (likely(!pthread_create(&cmd_idle_thr, NULL, cmd_idle_thread, NULL)))
				did_cmd_idle = true;
		}
		pthread_cond_wait(&getq->cond, stgd_lock);
	}
	if (did_cmd_idle)
		pthread_cancel(cmd_idle_thr);
	
	no_work = false;

	if (can_roll(work) && should_roll(work))
	{
		// Instead of consuming it, force it to be cloned and grab the clone
		mutex_unlock(stgd_lock);
		clone_available();
		goto retry;
	}
	
	unstage_work(work);

	/* Signal the getwork scheduler to look for more work */
	pthread_cond_signal(&gws_cond);

	/* Signal hash_pop again in case there are mutliple hash_pop waiters */
	pthread_cond_signal(&getq->cond);
	mutex_unlock(stgd_lock);
	work->pool->last_work_time = time(NULL);
	cgtime(&work->pool->tv_last_work_time);

	return work;
}

/* Clones work by rolling it if possible, and returning a clone instead of the
 * original work item which gets staged again to possibly be rolled again in
 * the future */
static struct work *clone_work(struct work *work)
{
	int mrs = mining_threads + opt_queue - total_staged(false);
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

	sha256(data, len, hash1);
	sha256(hash1, 32, hash);
}

/* PDiff 1 is a 256 bit unsigned integer of
 * 0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff
 * so we use a big endian 32 bit unsigned integer positioned at the Nth byte to
 * cover a huge range of difficulty targets, though not all 256 bits' worth */
static void pdiff_target_leadzero(void * const target_p, double diff)
{
	uint8_t *target = target_p;
	diff *= 0x100000000;
	int skip = log2(diff) / 8;
	if (skip)
	{
		if (skip > 0x1c)
			skip = 0x1c;
		diff /= pow(0x100, skip);
		memset(target, 0, skip);
	}
	uint32_t n = 0xffffffff;
	n = (double)n / diff;
	n = htobe32(n);
	memcpy(&target[skip], &n, sizeof(n));
	memset(&target[skip + sizeof(n)], 0xff, 32 - (skip + sizeof(n)));
}

void set_target_to_pdiff(void * const dest_target, const double pdiff)
{
	unsigned char rtarget[32];
	pdiff_target_leadzero(rtarget, pdiff);
	swab256(dest_target, rtarget);
	
	if (opt_debug) {
		char htarget[65];
		bin2hex(htarget, rtarget, 32);
		applog(LOG_DEBUG, "Generated target %s", htarget);
	}
}

void set_target_to_bdiff(void * const dest_target, const double bdiff)
{
	set_target_to_pdiff(dest_target, bdiff_to_pdiff(bdiff));
}

void _test_target(void * const funcp, const char * const funcname, const bool little_endian, const void * const expectp, const double diff)
{
	uint8_t bufr[32], buf[32], expectr[32], expect[32];
	int off;
	void (*func)(void *, double) = funcp;
	
	func(little_endian ? bufr : buf, diff);
	if (little_endian)
		swab256(buf, bufr);
	
	swap32tobe(expect, expectp, 256/32);
	
	// Fuzzy comparison: the first 32 bits set must match, and the actual target must be >= the expected
	for (off = 0; off < 28 && !buf[off]; ++off)
	{}
	
	if (memcmp(&buf[off], &expect[off], 4))
	{
testfail: ;
		++unittest_failures;
		char hexbuf[65], expectbuf[65];
		bin2hex(hexbuf, buf, 32);
		bin2hex(expectbuf, expect, 32);
		applogr(, LOG_WARNING, "%s test failed: diff %g got %s (expected %s)",
		        funcname, diff, hexbuf, expectbuf);
	}
	
	if (!little_endian)
		swab256(bufr, buf);
	swab256(expectr, expect);
	
	if (!hash_target_check(expectr, bufr))
		goto testfail;
}

#define TEST_TARGET(func, le, expect, diff)  \
	_test_target(func, #func, le, expect, diff)

void test_target()
{
	uint32_t expect[8] = {0};
	// bdiff 1 should be exactly 00000000ffff0000000006f29cfd29510a6caee84634e86a57257cf03152537f due to floating-point imprecision (pdiff1 / 1.0000152590218966)
	expect[0] = 0x0000ffff;
	TEST_TARGET(set_target_to_bdiff, true, expect, 1./0x10000);
	expect[0] = 0;
	expect[1] = 0xffff0000;
	TEST_TARGET(set_target_to_bdiff, true, expect, 1);
	expect[1] >>= 1;
	TEST_TARGET(set_target_to_bdiff, true, expect, 2);
	expect[1] >>= 3;
	TEST_TARGET(set_target_to_bdiff, true, expect, 0x10);
	expect[1] >>= 4;
	TEST_TARGET(set_target_to_bdiff, true, expect, 0x100);
	
	memset(&expect[1], '\xff', 28);
	expect[0] = 0x0000ffff;
	TEST_TARGET(set_target_to_pdiff, true, expect, 1./0x10000);
	expect[0] = 0;
	TEST_TARGET(set_target_to_pdiff, true, expect, 1);
	expect[1] >>= 1;
	TEST_TARGET(set_target_to_pdiff, true, expect, 2);
	expect[1] >>= 3;
	TEST_TARGET(set_target_to_pdiff, true, expect, 0x10);
	expect[1] >>= 4;
	TEST_TARGET(set_target_to_pdiff, true, expect, 0x100);
}

void stratum_work_cpy(struct stratum_work * const dst, const struct stratum_work * const src)
{
	*dst = *src;
	if (dst->tr)
		tmpl_incref(dst->tr);
	dst->nonce1 = maybe_strdup(src->nonce1);
	dst->job_id = maybe_strdup(src->job_id);
	bytes_cpy(&dst->coinbase, &src->coinbase);
	bytes_cpy(&dst->merkle_bin, &src->merkle_bin);
	dst->data_lock_p = NULL;
}

void stratum_work_clean(struct stratum_work * const swork)
{
	if (swork->tr)
		tmpl_decref(swork->tr);
	free(swork->nonce1);
	free(swork->job_id);
	bytes_free(&swork->coinbase);
	bytes_free(&swork->merkle_bin);
}

bool pool_has_usable_swork(const struct pool * const pool)
{
	if (opt_benchmark)
		return true;
	if (pool->swork.tr)
	{
		// GBT
		struct timeval tv_now;
		timer_set_now(&tv_now);
		return blkmk_time_left(pool->swork.tr->tmpl, tv_now.tv_sec);
	}
	return pool->stratum_notify;
}

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */
static void gen_stratum_work(struct pool *pool, struct work *work)
{
	clean_work(work);
	
	cg_wlock(&pool->data_lock);
	
	const int n2size = pool->swork.n2size;
	bytes_resize(&work->nonce2, n2size);
	if (pool->nonce2sz < n2size)
		memset(&bytes_buf(&work->nonce2)[pool->nonce2sz], 0, n2size - pool->nonce2sz);
	memcpy(bytes_buf(&work->nonce2),
#ifdef WORDS_BIGENDIAN
	// NOTE: On big endian, the most significant bits are stored at the end, so skip the LSBs
	       &((char*)&pool->nonce2)[pool->nonce2off],
#else
	       &pool->nonce2,
#endif
	       pool->nonce2sz);
	pool->nonce2++;
	
	work->pool = pool;
	work->work_restart_id = pool->swork.work_restart_id;
	gen_stratum_work2(work, &pool->swork);
	
	cgtime(&work->tv_staged);
}

void gen_stratum_work2(struct work *work, struct stratum_work *swork)
{
	unsigned char *coinbase;
	
	/* Generate coinbase */
	coinbase = bytes_buf(&swork->coinbase);
	memcpy(&coinbase[swork->nonce2_offset], bytes_buf(&work->nonce2), bytes_len(&work->nonce2));

	/* Downgrade to a read lock to read off the variables */
	if (swork->data_lock_p)
		cg_dwlock(swork->data_lock_p);
	
	gen_stratum_work3(work, swork, swork->data_lock_p);
	
	if (opt_debug)
	{
		char header[161];
		char nonce2hex[(bytes_len(&work->nonce2) * 2) + 1];
		bin2hex(header, work->data, 80);
		bin2hex(nonce2hex, bytes_buf(&work->nonce2), bytes_len(&work->nonce2));
		applog(LOG_DEBUG, "Generated stratum header %s", header);
		applog(LOG_DEBUG, "Work job_id %s nonce2 %s", work->job_id, nonce2hex);
	}
}

void gen_stratum_work3(struct work * const work, struct stratum_work * const swork, cglock_t * const data_lock_p)
{
	unsigned char *coinbase, merkle_root[32], merkle_sha[64];
	uint8_t *merkle_bin;
	uint32_t *data32, *swap32;
	int i;
	
	coinbase = bytes_buf(&swork->coinbase);
	
	/* Generate merkle root */
	gen_hash(coinbase, merkle_root, bytes_len(&swork->coinbase));
	memcpy(merkle_sha, merkle_root, 32);
	merkle_bin = bytes_buf(&swork->merkle_bin);
	for (i = 0; i < swork->merkles; ++i, merkle_bin += 32) {
		memcpy(merkle_sha + 32, merkle_bin, 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
	data32 = (uint32_t *)merkle_sha;
	swap32 = (uint32_t *)merkle_root;
	flip32(swap32, data32);
	
	memcpy(&work->data[0], swork->header1, 36);
	memcpy(&work->data[36], merkle_root, 32);
	*((uint32_t*)&work->data[68]) = htobe32(swork->ntime + timer_elapsed(&swork->tv_received, NULL));
	memcpy(&work->data[72], swork->diffbits, 4);
	memset(&work->data[76], 0, 4);  // nonce
	memcpy(&work->data[80], workpadding_bin, 48);
	
	work->ntime_roll_limits = swork->ntime_roll_limits;

	/* Copy parameters required for share submission */
	memcpy(work->target, swork->target, sizeof(work->target));
	work->job_id = maybe_strdup(swork->job_id);
	work->nonce1 = maybe_strdup(swork->nonce1);
	if (data_lock_p)
		cg_runlock(data_lock_p);

	calc_midstate(work);

	local_work++;
	work->stratum = true;
	work->blk.nonce = 0;
	work->id = total_work++;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_STRATUM;
	if (swork->tr) {
		work->getwork_mode = GETWORK_MODE_GBT;
		work->tr = swork->tr;
		tmpl_incref(work->tr);
	}
	calc_diff(work, 0);
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
		work = hash_pop(cgpu);
		if (stale_work(work, false)) {
			staged_full = false;  // It wasn't really full, since it was stale :(
			discard_work(work);
			work = NULL;
			wake_gws();
		}
	}
	last_getwork = time(NULL);
	applog(LOG_DEBUG, "%"PRIpreprv": Got work %d from get queue to get work for thread %d",
	       cgpu->proc_repr, work->id, thr_id);

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
	
	if (work->work_difficulty < 1)
	{
		const float min_nonce_diff = drv_min_nonce_diff(cgpu->drv, cgpu, work_mining_algorithm(work));
		if (unlikely(work->work_difficulty < min_nonce_diff))
		{
			if (min_nonce_diff - work->work_difficulty > 1./0x10000000)
				applog(LOG_WARNING, "%"PRIpreprv": Using work with lower difficulty than device supports",
				       cgpu->proc_repr);
			work->nonce_diff = min_nonce_diff;
		}
		else
			work->nonce_diff = work->work_difficulty;
	}
	else
		work->nonce_diff = 1;

	return work;
}

struct dupe_hash_elem {
	uint8_t hash[0x20];
	struct timeval tv_prune;
	UT_hash_handle hh;
};

static
void _submit_work_async(struct work *work)
{
	applog(LOG_DEBUG, "Pushing submit work to work thread");
	
	if (opt_benchmark)
	{
		json_t * const jn = json_null(), *result = NULL;
		work_check_for_block(work);
		{
			static struct dupe_hash_elem *dupe_hashes;
			struct dupe_hash_elem *dhe, *dhetmp;
			HASH_FIND(hh, dupe_hashes, &work->hash, sizeof(dhe->hash), dhe);
			if (dhe)
				result = json_string("duplicate");
			else
			{
				struct timeval tv_now;
				timer_set_now(&tv_now);
				
				// Prune old entries
				HASH_ITER(hh, dupe_hashes, dhe, dhetmp)
				{
					if (!timer_passed(&dhe->tv_prune, &tv_now))
						break;
					HASH_DEL(dupe_hashes, dhe);
					free(dhe);
				}
				
				dhe = malloc(sizeof(*dhe));
				memcpy(dhe->hash, work->hash, sizeof(dhe->hash));
				timer_set_delay(&dhe->tv_prune, &tv_now, 337500000);
				HASH_ADD(hh, dupe_hashes, hash, sizeof(dhe->hash), dhe);
			}
		}
		if (result)
		{}
		else
		if (stale_work(work, true))
		{
			char stalemsg[0x10];
			snprintf(stalemsg, sizeof(stalemsg), "stale %us", benchmark_update_interval * (work->pool->work_restart_id - work->work_restart_id));
			result = json_string(stalemsg);
		}
		else
			result = json_incref(jn);
		share_result(jn, result, jn, work, false, "");
		free_work(work);
		json_decref(result);
		json_decref(jn);
		return;
	}

	mutex_lock(&submitting_lock);
	++total_submitting;
	DL_APPEND(submit_waiting, work);
	mutex_unlock(&submitting_lock);

	notifier_wake(submit_waiting_notifier);
}

/* Submit a copy of the tested, statistic recorded work item asynchronously */
static void submit_work_async2(struct work *work, struct timeval *tv_work_found)
{
	if (tv_work_found)
		copy_time(&work->tv_work_found, tv_work_found);
	
	_submit_work_async(work);
}

void inc_hw_errors3(struct thr_info *thr, const struct work *work, const uint32_t *bad_nonce_p, float nonce_diff)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	
	if (bad_nonce_p)
	{
		if (bad_nonce_p == UNKNOWN_NONCE)
			applog(LOG_DEBUG, "%"PRIpreprv": invalid nonce - HW error",
			       cgpu->proc_repr);
		else
			applog(LOG_DEBUG, "%"PRIpreprv": invalid nonce (%08lx) - HW error",
			       cgpu->proc_repr, (unsigned long)be32toh(*bad_nonce_p));
	}
	
	mutex_lock(&stats_lock);
	hw_errors++;
	++cgpu->hw_errors;
	if (bad_nonce_p)
	{
		total_bad_diff1 += nonce_diff;
		cgpu->bad_diff1 += nonce_diff;
	}
	mutex_unlock(&stats_lock);

	if (thr->cgpu->drv->hw_error)
		thr->cgpu->drv->hw_error(thr);
}

void work_hash(struct work * const work)
{
	const struct mining_algorithm * const malgo = work_mining_algorithm(work);
	malgo->hash_data_f(work->hash, work->data);
}

static
bool test_hash(const void * const phash, const float diff)
{
	const uint32_t * const hash = phash;
	if (diff >= 1.)
		// FIXME: > 1 should check more
		return !hash[7];
	
	const uint32_t Htarg = (uint32_t)ceil((1. / diff) - 1);
	const uint32_t tmp_hash7 = le32toh(hash[7]);
	
	applog(LOG_DEBUG, "htarget %08lx hash %08lx",
				(long unsigned int)Htarg,
				(long unsigned int)tmp_hash7);
	return (tmp_hash7 <= Htarg);
}

enum test_nonce2_result _test_nonce2(struct work *work, uint32_t nonce, bool checktarget)
{
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);
	*work_nonce = htole32(nonce);

	work_hash(work);
	
	if (!test_hash(work->hash, work->nonce_diff))
		return TNR_BAD;
	
	if (checktarget && !hash_target_check_v(work->hash, work->target))
	{
		bool high_hash = true;
		struct pool * const pool = work->pool;
		if (pool_diff_effective_retroactively(pool))
		{
			// Some stratum pools are buggy and expect difficulty changes to be immediate retroactively, so if the target has changed, check and submit just in case
			if (memcmp(pool->next_target, work->target, sizeof(work->target)))
			{
				applog(LOG_DEBUG, "Stratum pool %u target has changed since work job issued, checking that too",
				       pool->pool_no);
				if (hash_target_check_v(work->hash, pool->next_target))
				{
					high_hash = false;
					work->work_difficulty = target_diff(pool->next_target);
				}
			}
		}
		if (high_hash)
			return TNR_HIGH;
	}
	
	return TNR_GOOD;
}

/* Returns true if nonce for work was a valid share */
bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	return submit_noffset_nonce(thr, work, nonce, 0);
}

/* Allows drivers to submit work items where the driver has changed the ntime
 * value by noffset. Must be only used with a work protocol that does not ntime
 * roll itself intrinsically to generate work (eg stratum). We do not touch
 * the original work struct, but the copy of it only. */
bool submit_noffset_nonce(struct thr_info *thr, struct work *work_in, uint32_t nonce,
			  int noffset)
{
	struct work *work = make_work();
	_copy_work(work, work_in, noffset);
	
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);
	struct timeval tv_work_found;
	enum test_nonce2_result res;
	bool ret = true;

	thread_reportout(thr);

	cgtime(&tv_work_found);
	*work_nonce = htole32(nonce);
	work->thr_id = thr->id;

	/* Do one last check before attempting to submit the work */
	/* Side effect: sets work->data and work->hash for us */
	res = test_nonce2(work, nonce);
	
	if (unlikely(res == TNR_BAD))
		{
			inc_hw_errors(thr, work, nonce);
			ret = false;
			goto out;
		}
	
	mutex_lock(&stats_lock);
	total_diff1       += work->nonce_diff;
	thr ->cgpu->diff1 += work->nonce_diff;
	work->pool->diff1 += work->nonce_diff;
	thr->cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);
	
	if (noncelog_file)
		noncelog(work);
	
	if (res == TNR_HIGH)
	{
			// Share above target, normal
			/* Check the diff of the share, even if it didn't reach the
			 * target, just to set the best share value if it's higher. */
			share_diff(work);
			goto out;
	}
	
	submit_work_async2(work, &tv_work_found);
	work = NULL;  // Taken by submit_work_async2
out:
	if (work)
		free_work(work);
	thread_reportin(thr);

	return ret;
}

// return true of we should stop working on this piece of work
// returning false means we will keep scanning for a nonce
// assumptions: work->blk.nonce is the number of nonces completed in the work
// see minerloop_scanhash comments for more details & usage
bool abandon_work(struct work *work, struct timeval *wdiff, uint64_t max_hashes)
{
	if (work->blk.nonce == 0xffffffff ||                // known we are scanning a full nonce range
	    wdiff->tv_sec > opt_scantime ||                 // scan-time has elapsed (user specified, default 60s)
	    work->blk.nonce >= 0xfffffffe - max_hashes ||   // are there enough nonces left in the work
	    max_hashes >= 0xfffffffe ||                     // assume we are scanning a full nonce range
	    stale_work(work, false))                        // work is stale
		return true;
	return false;
}

void __thr_being_msg(int prio, struct thr_info *thr, const char *being)
{
	struct cgpu_info *proc = thr->cgpu;
	
	if (proc->threads > 1)
		applog(prio, "%"PRIpreprv" (thread %d) %s", proc->proc_repr, thr->id, being);
	else
		applog(prio, "%"PRIpreprv" %s", proc->proc_repr, being);
}

// Called by asynchronous minerloops, when they find their processor should be disabled
void mt_disable_start(struct thr_info *mythr)
{
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	
	if (drv->thread_disable)
		drv->thread_disable(mythr);
	
	hashmeter2(mythr);
	__thr_being_msg(LOG_WARNING, mythr, "being disabled");
	mythr->rolling = mythr->cgpu->rolling = 0;
	thread_reportout(mythr);
	mythr->_mt_disable_called = true;
}

/* Put a new unqueued work item in cgpu->unqueued_work under cgpu->qlock till
 * the driver tells us it's full so that it may extract the work item using
 * the get_queued() function which adds it to the hashtable on
 * cgpu->queued_work. */
static void fill_queue(struct thr_info *mythr, struct cgpu_info *cgpu, struct device_drv *drv, const int thr_id)
{
	thread_reportout(mythr);
	do {
		bool need_work;

		/* Do this lockless just to know if we need more unqueued work. */
		need_work = (!cgpu->unqueued_work);

		/* get_work is a blocking function so do it outside of lock
		 * to prevent deadlocks with other locks. */
		if (need_work) {
			struct work *work = get_work(mythr);

			wr_lock(&cgpu->qlock);
			/* Check we haven't grabbed work somehow between
			 * checking and picking up the lock. */
			if (likely(!cgpu->unqueued_work))
				cgpu->unqueued_work = work;
			else
				need_work = false;
			wr_unlock(&cgpu->qlock);

			if (unlikely(!need_work))
				discard_work(work);
		}
		/* The queue_full function should be used by the driver to
		 * actually place work items on the physical device if it
		 * does have a queue. */
	} while (drv->queue_full && !drv->queue_full(cgpu));
}

/* Add a work item to a cgpu's queued hashlist */
void __add_queued(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->queued_count++;
	HASH_ADD_INT(cgpu->queued_work, id, work);
}

/* This function is for retrieving one work item from the unqueued pointer and
 * adding it to the hashtable of queued work. Code using this function must be
 * able to handle NULL as a return which implies there is no work available. */
struct work *get_queued(struct cgpu_info *cgpu)
{
	struct work *work = NULL;

	wr_lock(&cgpu->qlock);
	if (cgpu->unqueued_work) {
		work = cgpu->unqueued_work;
		if (unlikely(stale_work(work, false))) {
			discard_work(work);
			work = NULL;
			wake_gws();
		} else
			__add_queued(cgpu, work);
		cgpu->unqueued_work = NULL;
	}
	wr_unlock(&cgpu->qlock);

	return work;
}

void add_queued(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	__add_queued(cgpu, work);
	wr_unlock(&cgpu->qlock);
}

/* Get fresh work and add it to cgpu's queued hashlist */
struct work *get_queue_work(struct thr_info *thr, struct cgpu_info *cgpu, int thr_id)
{
	struct work *work = get_work(thr);

	add_queued(cgpu, work);
	return work;
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
		if (memcmp(work->midstate, midstate, midstatelen) == 0 &&
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

struct work *clone_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work, *ret = NULL;

	rd_lock(&cgpu->qlock);
	work = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	if (work)
		ret = copy_work(work);
	rd_unlock(&cgpu->qlock);

	return ret;
}

void __work_completed(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->queued_count--;
	HASH_DEL(cgpu->queued_work, work);
}

/* This iterates over a queued hashlist finding work started more than secs
 * seconds ago and discards the work as completed. The driver must set the
 * work->tv_work_start value appropriately. Returns the number of items aged. */
int age_queued_work(struct cgpu_info *cgpu, double secs)
{
	struct work *work, *tmp;
	struct timeval tv_now;
	int aged = 0;

	cgtime(&tv_now);

	wr_lock(&cgpu->qlock);
	HASH_ITER(hh, cgpu->queued_work, work, tmp) {
		if (tdiff(&tv_now, &work->tv_work_start) > secs) {
			__work_completed(cgpu, work);
			aged++;
		}
	}
	wr_unlock(&cgpu->qlock);

	return aged;
}

/* This function should be used by queued device drivers when they're sure
 * the work struct is no longer in use. */
void work_completed(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	__work_completed(cgpu, work);
	wr_unlock(&cgpu->qlock);

	free_work(work);
}

/* Combines find_queued_work_bymidstate and work_completed in one function
 * withOUT destroying the work so the driver must free it. */
struct work *take_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work;

	wr_lock(&cgpu->qlock);
	work = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	if (work)
		__work_completed(cgpu, work);
	wr_unlock(&cgpu->qlock);

	return work;
}

void flush_queue(struct cgpu_info *cgpu)
{
	struct work *work = NULL;

	wr_lock(&cgpu->qlock);
	work = cgpu->unqueued_work;
	cgpu->unqueued_work = NULL;
	wr_unlock(&cgpu->qlock);

	if (work) {
		free_work(work);
		applog(LOG_DEBUG, "Discarded queued work item");
	}
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

	if (unlikely(cgpu->deven != DEV_ENABLED))
		mt_disable(mythr);
	
	while (likely(!cgpu->shutdown)) {
		struct timeval diff;
		int64_t hashes;

		fill_queue(mythr, cgpu, drv, thr_id);

		thread_reportin(mythr);
		hashes = drv->scanwork(mythr);

		/* Reset the bool here in case the driver looks for it
		 * synchronously in the scanwork loop. */
		mythr->work_restart = false;

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
	// cgpu->deven = DEV_DISABLED; set in miner_thread
}

// Called by minerloop, when it is re-enabling a processor
void mt_disable_finish(struct thr_info *mythr)
{
	struct device_drv *drv = mythr->cgpu->drv;
	
	thread_reportin(mythr);
	__thr_being_msg(LOG_WARNING, mythr, "being re-enabled");
	if (drv->thread_enable)
		drv->thread_enable(mythr);
	mythr->_mt_disable_called = false;
}

// Called by synchronous minerloops, when they find their processor should be disabled
// Calls mt_disable_start, waits until it's re-enabled, then calls mt_disable_finish
void mt_disable(struct thr_info *mythr)
{
	const struct cgpu_info * const cgpu = mythr->cgpu;
	mt_disable_start(mythr);
	applog(LOG_DEBUG, "Waiting for wakeup notification in miner thread");
	do {
		notifier_read(mythr->notifier);
	} while (mythr->pause || cgpu->deven != DEV_ENABLED);
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
static
struct pool *_select_longpoll_pool(struct pool *cp, bool(*func)(struct pool *))
{
	int i;

	if (func(cp))
		return cp;
	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];
		if (cp->goal != pool->goal)
			continue;

		if (func(pool))
			return pool;
	}
	return NULL;
}

/* This will make the longpoll thread wait till it's the current pool, or it
 * has been flagged as rejecting, before attempting to open any connections.
 */
static void wait_lpcurrent(struct pool *pool)
{
	mutex_lock(&lp_lock);
	while (!cnx_needed(pool))
	{
		pool->lp_active = false;
		pthread_cond_wait(&lp_cond, &lp_lock);
	}
	mutex_unlock(&lp_lock);
}

static curl_socket_t save_curl_socket(void *vpool, __maybe_unused curlsocktype purpose, struct curl_sockaddr *addr) {
	struct pool *pool = vpool;
	curl_socket_t sock = bfg_socket(addr->family, addr->socktype, addr->protocol);
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
			cgsleep_ms(60000);
			pool = select_longpoll_pool(cp);
		}
	}

	if (pool->has_stratum) {
		applog(LOG_WARNING, "Block change for %s detection via %s stratum",
		       cp->rpc_url, pool->rpc_url);
		goto out;
	}

	/* Any longpoll from any pool is enough for this to be true */
	pool->goal->have_longpoll = true;

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
		lpreq = prepare_rpc_req(work, pool->lp_proto, pool->lp_id, pool);
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
		curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1);
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
			if (end.tv_sec - start.tv_sec <= 30)
			{
				if (failures == 1)
					applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s", lp_url);
lpfail:
				cgsleep_ms(30000);
			}
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
	pool->lp_active = false;
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
	
	struct mining_goal_info *goal, *tmpgoal;
	HASH_ITER(hh, mining_goals, goal, tmpgoal)
	{
		goal->have_longpoll = false;
	}
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
			}

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 30) {
				if (pool_active(pool, true) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
			}

			/* Only switch pools if the failback pool has been
			 * alive for more than 5 minutes (default) to prevent
			 * intermittently failing pools from being used. */
			if (!pool->idle && pool->enabled == POOL_ENABLED && pool_strategy == POOL_FAILOVER && pool->prio < cp_prio() && now.tv_sec - pool->tv_idle.tv_sec > opt_fail_switch_delay)
			{
				if (opt_fail_switch_delay % 60)
					applog(LOG_WARNING, "Pool %d %s stable for %d second%s",
					       pool->pool_no, pool->rpc_url,
					       opt_fail_switch_delay,
					       (opt_fail_switch_delay == 1 ? "" : "s"));
				else
					applog(LOG_WARNING, "Pool %d %s stable for %d minute%s",
					       pool->pool_no, pool->rpc_url,
					       opt_fail_switch_delay / 60,
					       (opt_fail_switch_delay == 60 ? "" : "s"));
				switch_pools(NULL);
			}
		}

		if (current_pool()->idle)
			switch_pools(NULL);

		if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
			cgtime(&rotate_tv);
			switch_pools(NULL);
		}

		cgsleep_ms(30000);
			
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

void cgpu_set_defaults(struct cgpu_info * const cgpu)
{
	struct string_elist *setstr_elist;
	const char *p, *p2;
	char replybuf[0x2000];
	size_t L;
	DL_FOREACH(opt_set_device_list, setstr_elist)
	{
		const char * const setstr = setstr_elist->string;
		p = strchr(setstr, ':');
		if (!p)
			p = setstr;
		{
			L = p - setstr;
			char pattern[L + 1];
			if (L)
				memcpy(pattern, setstr, L);
			pattern[L] = '\0';
			if (!cgpu_match(pattern, cgpu))
				continue;
		}
		
		applog(LOG_DEBUG, "%"PRIpreprv": %s: Matched with set default: %s",
		       cgpu->proc_repr, __func__, setstr);
		
		if (p[0] == ':')
			++p;
		p2 = strchr(p, '=');
		if (!p2)
		{
			L = strlen(p);
			p2 = "";
		}
		else
		{
			L = p2 - p;
			++p2;
		}
		char opt[L + 1];
		if (L)
			memcpy(opt, p, L);
		opt[L] = '\0';
		
		L = strlen(p2);
		char setval[L + 1];
		if (L)
			memcpy(setval, p2, L);
		setval[L] = '\0';
		
		enum bfg_set_device_replytype success;
		p = proc_set_device(cgpu, opt, setval, replybuf, &success);
		switch (success)
		{
			case SDR_OK:
				applog(LOG_DEBUG, "%"PRIpreprv": Applied rule %s%s%s",
				       cgpu->proc_repr, setstr,
				       p ? ": " : "", p ?: "");
				break;
			case SDR_ERR:
			case SDR_HELP:
			case SDR_UNKNOWN:
				applog(LOG_DEBUG, "%"PRIpreprv": Applying rule %s: %s",
				       cgpu->proc_repr, setstr, p);
				break;
			case SDR_AUTO:
			case SDR_NOSUPP:
				applog(LOG_DEBUG, "%"PRIpreprv": set_device is not implemented (trying to apply rule: %s)",
				       cgpu->proc_repr, setstr);
		}
	}
	cgpu->already_set_defaults = true;
}

void drv_set_defaults(const struct device_drv * const drv, const void *datap, void *userp, const char * const devpath, const char * const serial, const int mode)
{
	struct device_drv dummy_drv = *drv;
	struct cgpu_info dummy_cgpu = {
		.drv = &dummy_drv,
		.device = &dummy_cgpu,
		.device_id = -1,
		.proc_id = -1,
		.device_data = userp,
		.device_path = devpath,
		.dev_serial = serial,
	};
	strcpy(dummy_cgpu.proc_repr, drv->name);
	switch (mode)
	{
		case 0:
			dummy_drv.set_device = datap;
			break;
		case 1:
			dummy_drv.set_device = NULL;
			dummy_cgpu.set_device_funcs = datap;
			break;
	}
	cgpu_set_defaults(&dummy_cgpu);
}

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
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
		const int ts = total_staged(true);
		if (curses_active_locked()) {
			change_logwinsize();
			curses_print_status(ts);
			_refresh_devstatus(true);
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
			if (!cgpu->disable_watchdog)
				bfg_watchdog(cgpu, &now);
		}
	}

	return NULL;
}

void bfg_watchdog(struct cgpu_info * const cgpu, struct timeval * const tvp_now)
{
			struct thr_info *thr = cgpu->thr[0];
			enum dev_enable *denable;
			char *dev_str = cgpu->proc_repr;

			if (likely(drv_ready(cgpu)))
			{
				if (unlikely(!cgpu->already_set_defaults))
					cgpu_set_defaults(cgpu);
				if (cgpu->drv->get_stats)
					cgpu->drv->get_stats(cgpu);
			}

			denable = &cgpu->deven;

			if (cgpu->drv->watchdog)
				cgpu->drv->watchdog(cgpu, tvp_now);
			
			/* Thread is disabled */
			if (*denable == DEV_DISABLED)
				return;
			else
			if (*denable == DEV_RECOVER_ERR) {
				if (opt_restart && timer_elapsed(&cgpu->tv_device_last_not_well, NULL) > cgpu->reinit_backoff) {
					applog(LOG_NOTICE, "Attempting to reinitialize %s",
					       dev_str);
					if (cgpu->reinit_backoff < 300)
						cgpu->reinit_backoff *= 2;
					device_recovered(cgpu);
				}
				return;
			}
			else
			if (*denable == DEV_RECOVER) {
				if (opt_restart && cgpu->temp < cgpu->targettemp) {
					applog(LOG_NOTICE, "%s recovered to temperature below target, re-enabling",
					       dev_str);
					device_recovered(cgpu);
				}
				dev_error_update(cgpu, REASON_DEV_THERMAL_CUTOFF);
				return;
			}
			else
			if (cgpu->temp > cgpu->cutofftemp)
			{
				applog(LOG_WARNING, "%s hit thermal cutoff limit at %dC, disabling!",
				       dev_str, (int)cgpu->temp);
				*denable = DEV_RECOVER;

				dev_error(cgpu, REASON_DEV_THERMAL_CUTOFF);
				run_cmd(cmd_idle);
			}

			if (thr->getwork) {
				if (cgpu->status == LIFE_WELL && thr->getwork < tvp_now->tv_sec - opt_log_interval) {
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
				return;
			}
			else if (cgpu->status == LIFE_WAIT)
				cgpu->status = LIFE_WELL;

#ifdef USE_CPUMINING
			if (!strcmp(cgpu->drv->dname, "cpu"))
				return;
#endif
			if (cgpu->status != LIFE_WELL && (tvp_now->tv_sec - thr->last.tv_sec < WATCHDOG_SICK_TIME)) {
				if (likely(cgpu->status != LIFE_INIT && cgpu->status != LIFE_INIT2))
				applog(LOG_ERR, "%s: Recovered, declaring WELL!", dev_str);
				cgpu->status = LIFE_WELL;
				cgpu->device_last_well = time(NULL);
			} else if (cgpu->status == LIFE_WELL && (tvp_now->tv_sec - thr->last.tv_sec > WATCHDOG_SICK_TIME)) {
				thr->rolling = cgpu->rolling = 0;
				cgpu->status = LIFE_SICK;
				applog(LOG_ERR, "%s: Idle for more than 60 seconds, declaring SICK!", dev_str);
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_SICK_IDLE_60);
				run_cmd(cmd_sick);
				
				if (opt_restart && cgpu->drv->reinit_device) {
					applog(LOG_ERR, "%s: Attempting to restart", dev_str);
					reinit_device(cgpu);
				}
			} else if (cgpu->status == LIFE_SICK && (tvp_now->tv_sec - thr->last.tv_sec > WATCHDOG_DEAD_TIME)) {
				cgpu->status = LIFE_DEAD;
				applog(LOG_ERR, "%s: Not responded for more than 10 minutes, declaring DEAD!", dev_str);
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_DEAD_IDLE_600);
				run_cmd(cmd_dead);
			} else if (tvp_now->tv_sec - thr->sick.tv_sec > 60 &&
				   (cgpu->status == LIFE_SICK || cgpu->status == LIFE_DEAD)) {
				/* Attempt to restart a GPU that's sick or dead once every minute */
				cgtime(&thr->sick);
				if (opt_restart)
					reinit_device(cgpu);
			}
}

static void log_print_status(struct cgpu_info *cgpu)
{
	char logline[255];

	get_statline(logline, sizeof(logline), cgpu);
	applog(LOG_WARNING, "%s", logline);
}

void print_summary(void)
{
	struct timeval diff;
	int hours, mins, secs, i;
	double utility, efficiency = 0.0;
	char xfer[(ALLOC_H2B_SPACED*2)+4+1], bw[(ALLOC_H2B_SPACED*2)+6+1];
	int pool_secs;

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
#if defined(USE_CPUMINING) && defined(USE_SHA256D)
	if (opt_n_threads > 0)
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
	applog(LOG_WARNING, "Network transfer: %s  (%s)",
	       multi_format_unit2(xfer, sizeof(xfer), true, "B", H2B_SPACED, " / ", 2,
	                         (float)total_bytes_rcvd,
	                         (float)total_bytes_sent),
	       multi_format_unit2(bw, sizeof(bw), true, "B/s", H2B_SPACED, " / ", 2,
	                         (float)(total_bytes_rcvd / total_secs),
	                         (float)(total_bytes_sent / total_secs)));
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
			pool_secs = timer_elapsed(&pool->cgminer_stats.start_tv, NULL);
			applog(LOG_WARNING, " Network transfer: %s  (%s)",
			       multi_format_unit2(xfer, sizeof(xfer), true, "B", H2B_SPACED, " / ", 2,
			                         (float)pool->cgminer_pool_stats.net_bytes_received,
			                         (float)pool->cgminer_pool_stats.net_bytes_sent),
			       multi_format_unit2(bw, sizeof(bw), true, "B/s", H2B_SPACED, " / ", 2,
			                         (float)(pool->cgminer_pool_stats.net_bytes_received / pool_secs),
			                         (float)(pool->cgminer_pool_stats.net_bytes_sent / pool_secs)));
			uint64_t pool_bytes_xfer = pool->cgminer_pool_stats.net_bytes_received + pool->cgminer_pool_stats.net_bytes_sent;
			efficiency = pool_bytes_xfer ? pool->diff_accepted * 2048. / pool_bytes_xfer : 0.0;
			applog(LOG_WARNING, " Efficiency (accepted * difficulty / 2 KB): %.2f", efficiency);

			applog(LOG_WARNING, " Items worked on: %d", pool->works);
			applog(LOG_WARNING, " Unable to get work from server occasions: %d", pool->getfail_occasions);
			applog(LOG_WARNING, " Submitting work remotely delay occasions: %d\n", pool->remotefail_occasions);
		}
	}

	if (opt_quit_summary != BQS_NONE)
	{
		if (opt_quit_summary == BQS_DEFAULT)
		{
			if (total_devices < 25)
				opt_quit_summary = BQS_PROCS;
			else
				opt_quit_summary = BQS_DEVS;
		}
		
		if (opt_quit_summary == BQS_DETAILED)
			include_serial_in_statline = true;
		applog(LOG_WARNING, "Summary of per device statistics:\n");
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = get_devices(i);
			
			if (!cgpu->proc_id)
			{
				// Device summary line
				opt_show_procs = false;
				log_print_status(cgpu);
				opt_show_procs = true;
			}
			if ((opt_quit_summary == BQS_PROCS || opt_quit_summary == BQS_DETAILED) && cgpu->procs > 1)
				log_print_status(cgpu);
		}
	}

	if (opt_shares) {
		applog(LOG_WARNING, "Mined %g accepted shares of %g requested\n", total_diff_accepted, opt_shares);
		if (opt_shares > total_diff_accepted)
			applog(LOG_WARNING, "WARNING - Mined only %g shares of %g requested.", total_diff_accepted, opt_shares);
	}
	applog(LOG_WARNING, " ");

	fflush(stderr);
	fflush(stdout);
}

void _bfg_clean_up(bool restarting)
{
#ifdef USE_OPENCL
	clear_adl(nDevs);
#endif
#ifdef HAVE_LIBUSB
	if (likely(have_libusb))
        libusb_exit(NULL);
#endif

	cgtime(&total_tv_end);
#ifdef WIN32
	timeEndPeriod(1);
#endif
	if (!restarting) {
		/* Attempting to disable curses or print a summary during a
		 * restart can lead to a deadlock. */
#ifdef HAVE_CURSES
		disable_curses();
#endif
		if (!opt_realquiet && successful_connect)
			print_summary();
	}

	if (opt_n_threads > 0)
		free(cpus);

	curl_global_cleanup();
	
#ifdef WIN32
	WSACleanup();
#endif
}

void _quit(int status)
{
	if (status) {
		const char *ev = getenv("__BFGMINER_SEGFAULT_ERRQUIT");
		if (unlikely(ev && ev[0] && ev[0] != '0')) {
			int *p = NULL;
			// NOTE debugger can bypass with: p = &p
			*p = status;  // Segfault, hopefully dumping core
		}
	}

#if defined(unix) || defined(__APPLE__)
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
	{
		free(input);
		input = NULL;
	}
	leaveok(logwin, true);
	noecho();
	return input;
}
#endif

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
		else
			applog(LOG_NOTICE, "Pool %d %s alive", pool->pool_no, pool->rpc_url);

		switch_pools(NULL);
	} else
		pool_died(pool);

	pool->testing = false;
	return NULL;
}

/* Always returns true that the pool details were added unless we are not
 * live, implying this is the only pool being added, so if no pools are
 * active it returns false. */
bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass)
{
	size_t siz;

	pool_set_uri(pool, url);
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
	pool->rpc_userpass = malloc(siz);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);

	pool->testing = true;
	pool->idle = true;
	enable_pool(pool);

	pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	if (!live) {
		pthread_join(pool->test_thread, NULL);
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
		pass = calloc(1, 1);

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

#if BLKMAKER_VERSION > 1 && defined(USE_SHA256D)
static
bool _add_local_gbt(const char * const filepath, void *userp)
{
	const bool * const live_p = userp;
	struct pool *pool;
	char buf[0x100];
	char *rpcuser = NULL, *rpcpass = NULL, *rpcconnect = NULL;
	int rpcport = 0, rpcssl = -101;
	FILE * const F = fopen(filepath, "r");
	if (!F)
		applogr(false, LOG_WARNING, "%s: Failed to open %s for reading", "add_local_gbt", filepath);
	
	while (fgets(buf, sizeof(buf), F))
	{
		if (!strncasecmp(buf, "rpcuser=", 8))
			rpcuser = trimmed_strdup(&buf[8]);
		else
		if (!strncasecmp(buf, "rpcpassword=", 12))
			rpcpass = trimmed_strdup(&buf[12]);
		else
		if (!strncasecmp(buf, "rpcport=", 8))
			rpcport = atoi(&buf[8]);
		else
		if (!strncasecmp(buf, "rpcssl=", 7))
			rpcssl = atoi(&buf[7]);
		else
		if (!strncasecmp(buf, "rpcconnect=", 11))
			rpcconnect = trimmed_strdup(&buf[11]);
		else
			continue;
		if (rpcuser && rpcpass && rpcport && rpcssl != -101 && rpcconnect)
			break;
	}
	
	fclose(F);
	
	if (!rpcpass)
	{
		applog(LOG_DEBUG, "%s: Did not find rpcpassword in %s", "add_local_gbt", filepath);
err:
		free(rpcuser);
		free(rpcpass);
		goto out;
	}
	
	if (!rpcport)
		rpcport = 8332;
	
	if (rpcssl == -101)
		rpcssl = 0;
	
	const bool have_cbaddr = get_mining_goal("default")->generation_script;
	
	const int uri_sz = 0x30;
	char * const uri = malloc(uri_sz);
	snprintf(uri, uri_sz, "http%s://%s:%d/%s#allblocks", rpcssl ? "s" : "", rpcconnect ?: "localhost", rpcport, have_cbaddr ? "" : "#getcbaddr");
	
	char hfuri[0x40];
	if (rpcconnect)
		snprintf(hfuri, sizeof(hfuri), "%s:%d", rpcconnect, rpcport);
	else
		snprintf(hfuri, sizeof(hfuri), "port %d", rpcport);
	applog(LOG_DEBUG, "Local bitcoin RPC server on %s found in %s", hfuri, filepath);
	
	for (int i = 0; i < total_pools; ++i)
	{
		struct pool *pool = pools[i];
		
		if (!(strcmp(pool->rpc_url, uri) || strcmp(pool->rpc_pass, rpcpass)))
		{
			applog(LOG_DEBUG, "Server on %s is already configured, not adding as failover", hfuri);
			free(uri);
			goto err;
		}
	}
	
	pool = add_pool();
	if (!pool)
	{
		applog(LOG_ERR, "%s: Error adding pool for bitcoin configured in %s", "add_local_gbt", filepath);
		goto err;
	}
	
	if (!rpcuser)
		rpcuser = "";
	
	pool->quota = 0;
	adjust_quota_gcd();
	pool->failover_only = true;
	add_pool_details(pool, *live_p, uri, rpcuser, rpcpass);
	
	applog(LOG_NOTICE, "Added local bitcoin RPC server on %s as pool %d", hfuri, pool->pool_no);
	
out:
	return false;
}

static
void add_local_gbt(bool live)
{
	appdata_file_call("Bitcoin", "bitcoin.conf", _add_local_gbt, &live);
}
#endif

#if defined(unix) || defined(__APPLE__)
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
#ifdef USE_UNICODE
static
wchar_t select_unicode_char(const wchar_t *opt)
{
	for ( ; *opt; ++opt)
		if (iswprint(*opt))
			return *opt;
	return '?';
}
#endif

void enable_curses(void) {
	int x;
	__maybe_unused int y;

	lock_curses();
	if (curses_active) {
		unlock_curses();
		return;
	}

#ifdef USE_UNICODE
	if (use_unicode)
	{
		setlocale(LC_CTYPE, "");
		if (iswprint(0xb0))
			have_unicode_degrees = true;
		unicode_micro = select_unicode_char(L"\xb5\u03bcu");
	}
#endif
	mainwin = initscr();
	start_color();
#if defined(PDCURSES) || defined(NCURSES_VERSION)
	if (ERR != use_default_colors())
		default_bgcolor = -1;
#endif
	if (has_colors() && ERR != init_pair(1, COLOR_WHITE, COLOR_BLUE))
	{
		menu_attr = COLOR_PAIR(1);
		if (ERR != init_pair(2, COLOR_RED, default_bgcolor))
			attr_bad |= COLOR_PAIR(2);
	}
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
	nonl();
	curses_active = true;
	statusy = logstart;
	unlock_curses();
}
#endif

/* TODO: fix need a dummy CPU device_drv even if no support for CPU mining */
#ifndef USE_CPUMINING
struct device_drv cpu_drv;
struct device_drv cpu_drv = {
	.name = "CPU",
};
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
	int thr_objs = cgpu->threads ?: 1;
	mining_threads += thr_objs;
	base_queue += thr_objs + cgpu->extra_work_queue;
	{
		const struct device_drv * const drv = cgpu->drv;
		struct mining_algorithm *malgo;
		LL_FOREACH(mining_algorithms, malgo)
		{
			if (drv_min_nonce_diff(drv, cgpu, malgo) < 0)
				continue;
			malgo->base_queue += thr_objs + cgpu->extra_work_queue;
		}
	}
#ifdef HAVE_CURSES
	adj_width(mining_threads, &dev_width);
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
	
	// Build repr strings
	sprintf(cgpu->dev_repr, "%s%2u", cgpu->drv->name, cgpu->device_id % 100);
	sprintf(cgpu->dev_repr_ns, "%s%u", cgpu->drv->name, cgpu->device_id % 100);
	strcpy(cgpu->proc_repr, cgpu->dev_repr);
	sprintf(cgpu->proc_repr_ns, "%s%u", cgpu->drv->name, cgpu->device_id);
	
	const int lpcount = cgpu->procs;
	if (lpcount > 1)
	{
		int ns;
		struct cgpu_info *slave;
		int lpdigits = 1;
		for (int i = lpcount; i > 26 && lpdigits < 3; i /= 26)
			++lpdigits;
		
		if (lpdigits > max_lpdigits)
			max_lpdigits = lpdigits;
		
		memset(&cgpu->proc_repr[5], 'a', lpdigits);
		cgpu->proc_repr[5 + lpdigits] = '\0';
		ns = strlen(cgpu->proc_repr_ns);
		strcpy(&cgpu->proc_repr_ns[ns], &cgpu->proc_repr[5]);
		
		slave = cgpu;
		for (int i = 1; i < lpcount; ++i)
		{
			slave = slave->next_proc;
			strcpy(slave->proc_repr, cgpu->proc_repr);
			strcpy(slave->proc_repr_ns, cgpu->proc_repr_ns);
			for (int x = i, y = lpdigits; --y, x; x /= 26)
			{
				slave->proc_repr_ns[ns + y] =
				slave->proc_repr[5 + y] += (x % 26);
			}
		}
	}
}

static bool my_blkmaker_sha256_callback(void *digest, const void *buffer, size_t length)
{
	sha256(buffer, length, digest);
	return true;
}

static
bool drv_algo_check(const struct device_drv * const drv)
{
	struct mining_goal_info *goal, *tmpgoal;
	HASH_ITER(hh, mining_goals, goal, tmpgoal)
	{
		if (drv_min_nonce_diff(drv, NULL, goal->malgo) >= 0)
			return true;
	}
	return false;
}

#ifndef HAVE_PTHREAD_CANCEL
extern void setup_pthread_cancel_workaround();
extern struct sigaction pcwm_orig_term_handler;
#endif

bool bfg_need_detect_rescan;
extern void probe_device(struct lowlevel_device_info *);
static void schedule_rescan(const struct timeval *);

static
void drv_detect_all()
{
	bool rescanning = false;
rescan:
	bfg_need_detect_rescan = false;
	
#ifdef HAVE_BFG_LOWLEVEL
	struct lowlevel_device_info * const infolist = lowlevel_scan(), *info, *infotmp;
	
	LL_FOREACH_SAFE(infolist, info, infotmp)
		probe_device(info);
	LL_FOREACH_SAFE(infolist, info, infotmp)
		pthread_join(info->probe_pth, NULL);
#endif
	
	struct driver_registration *reg;
	BFG_FOREACH_DRIVER_BY_PRIORITY(reg)
	{
		const struct device_drv * const drv = reg->drv;
		if (!(drv_algo_check(drv) && drv->drv_detect))
			continue;
		
		drv->drv_detect();
	}

#ifdef HAVE_BFG_LOWLEVEL
	lowlevel_scan_free();
#endif
	
	if (bfg_need_detect_rescan)
	{
		if (rescanning)
		{
			applog(LOG_DEBUG, "Device rescan requested a second time, delaying");
			struct timeval tv_when;
			timer_set_delay_from_now(&tv_when, rescan_delay_ms * 1000);
			schedule_rescan(&tv_when);
		}
		else
		{
			rescanning = true;
			applog(LOG_DEBUG, "Device rescan requested");
			goto rescan;
		}
	}
}

static
void allocate_cgpu(struct cgpu_info *cgpu, unsigned int *kp)
{
	struct thr_info *thr;
	int j;
	
	struct device_drv *api = cgpu->drv;
	cgpu->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
	
	int threadobj = cgpu->threads;
	if (!threadobj)
		// Create a fake thread object to handle hashmeter etc
		threadobj = 1;
	cgpu->thr = calloc(threadobj + 1, sizeof(*cgpu->thr));
	cgpu->thr[threadobj] = NULL;
	cgpu->status = LIFE_INIT;
	
	if (opt_devices_enabled_list)
	{
		struct string_elist *enablestr_elist;
		cgpu->deven = DEV_DISABLED;
		DL_FOREACH(opt_devices_enabled_list, enablestr_elist)
		{
			const char * const enablestr = enablestr_elist->string;
			if (cgpu_match(enablestr, cgpu))
			{
				cgpu->deven = DEV_ENABLED;
				break;
			}
		}
	}

	cgpu->max_hashes = 0;
	
	BFGINIT(cgpu->cutofftemp, opt_cutofftemp);
	BFGINIT(cgpu->targettemp, cgpu->cutofftemp - 6);

	// Setup thread structs before starting any of the threads, in case they try to interact
	for (j = 0; j < threadobj; ++j, ++*kp) {
		thr = get_thread(*kp);
		thr->id = *kp;
		thr->cgpu = cgpu;
		thr->device_thread = j;
		thr->work_restart_notifier[1] = INVSOCK;
		thr->mutex_request[1] = INVSOCK;
		thr->_job_transition_in_progress = true;
		timerclear(&thr->tv_morework);

		thr->scanhash_working = true;
		thr->hashes_done = 0;
		timerclear(&thr->tv_hashes_done);
		cgtime(&thr->tv_lastupdate);
		thr->tv_poll.tv_sec = -1;
		thr->_max_nonce = api->can_limit_work ? api->can_limit_work(thr) : 0xffffffff;

		cgpu->thr[j] = thr;
	}
	
	if (!cgpu->device->threads)
		notifier_init_invalid(cgpu->thr[0]->notifier);
	else
	if (!cgpu->threads)
		memcpy(&cgpu->thr[0]->notifier, &cgpu->device->thr[0]->notifier, sizeof(cgpu->thr[0]->notifier));
	else
	for (j = 0; j < cgpu->threads; ++j)
	{
		thr = cgpu->thr[j];
		notifier_init(thr->notifier);
	}
}

static
void start_cgpu(struct cgpu_info *cgpu)
{
	struct thr_info *thr;
	int j;
	
	for (j = 0; j < cgpu->threads; ++j) {
		thr = cgpu->thr[j];

		/* Enable threads for devices set not to mine but disable
		 * their queue in case we wish to enable them later */
		if (cgpu->drv->thread_prepare && !cgpu->drv->thread_prepare(thr))
			continue;

		thread_reportout(thr);

		if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
			quit(1, "thread %d create failed", thr->id);
		
		notifier_wake(thr->notifier);
	}
	if (cgpu->deven == DEV_ENABLED)
		proc_enable(cgpu);
}

static
void _scan_serial(void *p)
{
	const char *s = p;
	struct string_elist *iter, *tmp;
	struct string_elist *orig_scan_devices = scan_devices;
	
	if (s)
	{
		// Make temporary scan_devices list
		scan_devices = NULL;
		string_elist_add("noauto", &scan_devices);
		add_serial(s);
	}
	
	drv_detect_all();
	
	if (s)
	{
		DL_FOREACH_SAFE(scan_devices, iter, tmp)
		{
			string_elist_del(&scan_devices, iter);
		}
		scan_devices = orig_scan_devices;
	}
}

#ifdef HAVE_BFG_LOWLEVEL
static
bool _probe_device_match(const struct lowlevel_device_info * const info, const char * const ser)
{
	if (!(false
		|| (info->serial && !strcasecmp(ser, info->serial))
		|| (info->path   && !strcasecmp(ser, info->path  ))
		|| (info->devid  && !strcasecmp(ser, info->devid ))
	))
	{
		char *devid = devpath_to_devid(ser);
		if (!devid)
			return false;
		const bool different = strcmp(info->devid, devid);
		free(devid);
		if (different)
			return false;
	}
	return true;
}

static
bool _probe_device_do_probe(const struct device_drv * const drv, const struct lowlevel_device_info * const info, bool * const request_rescan_p)
{
	bfg_probe_result_flags = 0;
	if (drv->lowl_probe(info))
	{
		if (!(bfg_probe_result_flags & BPR_CONTINUE_PROBES))
			return true;
	}
	else
	if (request_rescan_p && opt_hotplug && !(bfg_probe_result_flags & BPR_DONT_RESCAN))
		*request_rescan_p = true;
	return false;
}

bool dummy_check_never_true = false;

static
void *probe_device_thread(void *p)
{
	struct lowlevel_device_info * const infolist = p;
	struct lowlevel_device_info *info = infolist;
	bool request_rescan = false;
	
	{
		char threadname[6 + strlen(info->devid) + 1];
		sprintf(threadname, "probe_%s", info->devid);
		RenameThread(threadname);
	}
	
	// If already in use, ignore
	if (bfg_claim_any(NULL, NULL, info->devid))
		applogr(NULL, LOG_DEBUG, "%s: \"%s\" already in use",
		        __func__, info->product);
	
	// if lowlevel device matches specific user assignment, probe requested driver(s)
	struct string_elist *sd_iter, *sd_tmp;
	struct driver_registration *dreg;
	DL_FOREACH_SAFE(scan_devices, sd_iter, sd_tmp)
	{
		const char * const dname = sd_iter->string;
		const char * const colon = strpbrk(dname, ":@");
		if (!(colon && colon != dname))
			continue;
		const char * const ser = &colon[1];
		LL_FOREACH2(infolist, info, same_devid_next)
		{
			if (!_probe_device_match(info, ser))
				continue;
			
			const size_t dnamelen = (colon - dname);
			char dname_nt[dnamelen + 1];
			memcpy(dname_nt, dname, dnamelen);
			dname_nt[dnamelen] = '\0';
			
			BFG_FOREACH_DRIVER_BY_PRIORITY(dreg) {
				const struct device_drv * const drv = dreg->drv;
				if (!(drv && drv->lowl_probe && drv_algo_check(drv)))
					continue;
				if (strcasecmp(drv->dname, dname_nt) && strcasecmp(drv->name, dname_nt))
					continue;
				if (_probe_device_do_probe(drv, info, &request_rescan))
					return NULL;
			}
		}
	}
	
	// probe driver(s) with auto enabled and matching VID/PID/Product/etc of device
	BFG_FOREACH_DRIVER_BY_PRIORITY(dreg)
	{
		const struct device_drv * const drv = dreg->drv;
		
		if (!drv_algo_check(drv))
			continue;
		
		// Check for "noauto" flag
		// NOTE: driver-specific configuration overrides general
		bool doauto = true;
		DL_FOREACH_SAFE(scan_devices, sd_iter, sd_tmp)
		{
			const char * const dname = sd_iter->string;
			// NOTE: Only checking flags here, NOT path/serial, so @ is unacceptable
			const char *colon = strchr(dname, ':');
			if (!colon)
				colon = &dname[-1];
			if (strcasecmp("noauto", &colon[1]) && strcasecmp("auto", &colon[1]))
				continue;
			const ssize_t dnamelen = (colon - dname);
			if (dnamelen >= 0) {
				char dname_nt[dnamelen + 1];
				memcpy(dname_nt, dname, dnamelen);
				dname_nt[dnamelen] = '\0';
				
				if (strcasecmp(drv->dname, dname_nt) && strcasecmp(drv->name, dname_nt))
					continue;
			}
			doauto = (tolower(colon[1]) == 'a');
			if (dnamelen != -1)
				break;
		}
		
		if (doauto && drv->lowl_match)
		{
			LL_FOREACH2(infolist, info, same_devid_next)
			{
				/*
				 The below call to applog is absolutely necessary
				 Starting with commit 76d0cc183b1c9ddcc0ef34d2e43bc696ef9de92e installing BFGMiner on
				 Mac OS X using Homebrew results in a binary that segfaults on startup
				 There are two unresolved issues:

				 1) The BFGMiner authors cannot find a way to install BFGMiner with Homebrew that results
				    in debug symbols being available to help troubleshoot the issue
				 2) The issue disappears when unrelated code changes are made, such as adding the following
				    call to applog with infolist and / or p
				 
				 We would encourage revisiting this in the future to come up with a more concrete solution
				 Reproducing should only require commenting / removing the following line and installing
				 BFGMiner using "brew install bfgminer --HEAD"
				 */
				if (dummy_check_never_true)
					applog(LOG_DEBUG, "lowl_match: %p(%s) %p %p %p", drv, drv->dname, info, infolist, p);
				
				if (!drv->lowl_match(info))
					continue;
				if (_probe_device_do_probe(drv, info, &request_rescan))
					return NULL;
			}
		}
	}
	
	// probe driver(s) with 'all' enabled
	DL_FOREACH_SAFE(scan_devices, sd_iter, sd_tmp)
	{
		const char * const dname = sd_iter->string;
		// NOTE: Only checking flags here, NOT path/serial, so @ is unacceptable
		const char * const colon = strchr(dname, ':');
		if (!colon)
		{
			LL_FOREACH2(infolist, info, same_devid_next)
			{
				if (
#ifdef NEED_BFG_LOWL_VCOM
					(info->lowl == &lowl_vcom && !strcasecmp(dname, "all")) ||
#endif
					_probe_device_match(info, (dname[0] == '@') ? &dname[1] : dname))
				{
					bool dont_rescan = false;
					BFG_FOREACH_DRIVER_BY_PRIORITY(dreg)
					{
						const struct device_drv * const drv = dreg->drv;
						if (!drv_algo_check(drv))
							continue;
						if (drv->lowl_probe_by_name_only)
							continue;
						if (!drv->lowl_probe)
							continue;
						if (_probe_device_do_probe(drv, info, NULL))
							return NULL;
						if (bfg_probe_result_flags & BPR_DONT_RESCAN)
							dont_rescan = true;
					}
					if (opt_hotplug && !dont_rescan)
						request_rescan = true;
					break;
				}
			}
			continue;
		}
		if (strcasecmp(&colon[1], "all"))
			continue;
		const size_t dnamelen = (colon - dname);
		char dname_nt[dnamelen + 1];
		memcpy(dname_nt, dname, dnamelen);
		dname_nt[dnamelen] = '\0';

		BFG_FOREACH_DRIVER_BY_PRIORITY(dreg) {
			const struct device_drv * const drv = dreg->drv;
			if (!(drv && drv->lowl_probe && drv_algo_check(drv)))
				continue;
			if (strcasecmp(drv->dname, dname_nt) && strcasecmp(drv->name, dname_nt))
				continue;
			LL_FOREACH2(infolist, info, same_devid_next)
			{
				if (info->lowl->exclude_from_all)
					continue;
				if (_probe_device_do_probe(drv, info, NULL))
					return NULL;
			}
		}
	}
	
	// Only actually request a rescan if we never found any cgpu
	if (request_rescan)
		bfg_need_detect_rescan = true;
	
	return NULL;
}

void probe_device(struct lowlevel_device_info * const info)
{
	pthread_create(&info->probe_pth, NULL, probe_device_thread, info);
}
#endif

int create_new_cgpus(void (*addfunc)(void*), void *arg)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	int devcount, i, mining_threads_new = 0;
	unsigned int k;
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	void *p;
	
	mutex_lock(&mutex);
	devcount = total_devices;
	
	addfunc(arg);
	
	if (!total_devices_new)
		goto out;
	
	wr_lock(&devices_lock);
	p = realloc(devices, sizeof(struct cgpu_info *) * (total_devices + total_devices_new + 1));
	if (unlikely(!p))
	{
		wr_unlock(&devices_lock);
		applog(LOG_ERR, "scan_serial: realloc failed trying to grow devices array");
		goto out;
	}
	devices = p;
	wr_unlock(&devices_lock);
	
	for (i = 0; i < total_devices_new; ++i)
	{
		cgpu = devices_new[i];
		mining_threads_new += cgpu->threads ?: 1;
	}
	
	wr_lock(&mining_thr_lock);
	mining_threads_new += mining_threads;
	p = realloc(mining_thr, sizeof(struct thr_info *) * mining_threads_new);
	if (unlikely(!p))
	{
		wr_unlock(&mining_thr_lock);
		applog(LOG_ERR, "scan_serial: realloc failed trying to grow mining_thr");
		goto out;
	}
	mining_thr = p;
	wr_unlock(&mining_thr_lock);
	for (i = mining_threads; i < mining_threads_new; ++i) {
		mining_thr[i] = calloc(1, sizeof(*thr));
		if (!mining_thr[i])
		{
			applog(LOG_ERR, "scan_serial: Failed to calloc mining_thr[%d]", i);
			for ( ; --i >= mining_threads; )
				free(mining_thr[i]);
			goto out;
		}
	}
	
	k = mining_threads;
	for (i = 0; i < total_devices_new; ++i)
	{
		cgpu = devices_new[i];
		
		allocate_cgpu(cgpu, &k);
	}
	for (i = 0; i < total_devices_new; ++i)
	{
		cgpu = devices_new[i];
		
		start_cgpu(cgpu);
		register_device(cgpu);
		++total_devices;
	}
	
#ifdef HAVE_CURSES
	switch_logsize();
#endif
	
out:
	total_devices_new = 0;
	
	devcount = total_devices - devcount;
	mutex_unlock(&mutex);
	
	return devcount;
}

int scan_serial(const char *s)
{
	return create_new_cgpus(_scan_serial, (void*)s);
}

static pthread_mutex_t rescan_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool rescan_active;
static struct timeval tv_rescan;
static notifier_t rescan_notifier;

static
void *rescan_thread(__maybe_unused void *p)
{
	pthread_detach(pthread_self());
	RenameThread("rescan");
	
	struct timeval tv_timeout, tv_now;
	fd_set rfds;
	
	while (true)
	{
		mutex_lock(&rescan_mutex);
		tv_timeout = tv_rescan;
		if (!timer_isset(&tv_timeout))
		{
			rescan_active = false;
			mutex_unlock(&rescan_mutex);
			break;
		}
		mutex_unlock(&rescan_mutex);
		
		FD_ZERO(&rfds);
		FD_SET(rescan_notifier[0], &rfds);
		const int maxfd = rescan_notifier[0];
		
		timer_set_now(&tv_now);
		if (select(maxfd+1, &rfds, NULL, NULL, select_timeout(&tv_timeout, &tv_now)) > 0)
			notifier_read(rescan_notifier);
		
		mutex_lock(&rescan_mutex);
		if (timer_passed(&tv_rescan, NULL))
		{
			timer_unset(&tv_rescan);
			mutex_unlock(&rescan_mutex);
			applog(LOG_DEBUG, "Rescan timer expired, triggering");
			scan_serial(NULL);
		}
		else
			mutex_unlock(&rescan_mutex);
	}
	return NULL;
}

static
void _schedule_rescan(const struct timeval * const tvp_when)
{
	if (rescan_active)
	{
		if (timercmp(tvp_when, &tv_rescan, <))
			applog(LOG_DEBUG, "schedule_rescan: New schedule is before current, waiting it out");
		else
		{
			applog(LOG_DEBUG, "schedule_rescan: New schedule is after current, delaying rescan");
			tv_rescan = *tvp_when;
		}
		return;
	}
	
	applog(LOG_DEBUG, "schedule_rescan: Scheduling rescan (no rescans currently pending)");
	tv_rescan = *tvp_when;
	rescan_active = true;
	
	static pthread_t pth;
	if (unlikely(pthread_create(&pth, NULL, rescan_thread, NULL)))
		applog(LOG_ERR, "Failed to start rescan thread");
}

static
void schedule_rescan(const struct timeval * const tvp_when)
{
	mutex_lock(&rescan_mutex);
	_schedule_rescan(tvp_when);
	mutex_unlock(&rescan_mutex);
}

static
void hotplug_trigger()
{
	applog(LOG_DEBUG, "%s: Scheduling rescan immediately", __func__);
	struct timeval tv_now;
	timer_set_now(&tv_now);
	schedule_rescan(&tv_now);
}

#if defined(HAVE_LIBUDEV) && defined(HAVE_SYS_EPOLL_H)

static
void *hotplug_thread(__maybe_unused void *p)
{
	pthread_detach(pthread_self());
	RenameThread("hotplug");
	
	struct udev * const udev = udev_new();
	if (unlikely(!udev))
		applogfailr(NULL, LOG_ERR, "udev_new");
	struct udev_monitor * const mon = udev_monitor_new_from_netlink(udev, "udev");
	if (unlikely(!mon))
		applogfailr(NULL, LOG_ERR, "udev_monitor_new_from_netlink");
	if (unlikely(udev_monitor_enable_receiving(mon)))
		applogfailr(NULL, LOG_ERR, "udev_monitor_enable_receiving");
	const int epfd = epoll_create(1);
	if (unlikely(epfd == -1))
		applogfailr(NULL, LOG_ERR, "epoll_create");
	{
		const int fd = udev_monitor_get_fd(mon);
		struct epoll_event ev = {
			.events = EPOLLIN | EPOLLPRI,
			.data.fd = fd,
		};
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev))
			applogfailr(NULL, LOG_ERR, "epoll_ctl");
	}
	
	struct epoll_event ev;
	int rv;
	bool pending = false;
	while (true)
	{
		rv = epoll_wait(epfd, &ev, 1, pending ? hotplug_delay_ms : -1);
		if (rv == -1)
		{
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		if (!rv)
		{
			hotplug_trigger();
			pending = false;
			continue;
		}
		struct udev_device * const device = udev_monitor_receive_device(mon);
		if (!device)
			continue;
		const char * const action = udev_device_get_action(device);
		applog(LOG_DEBUG, "%s: Received %s event", __func__, action);
		if (!strcmp(action, "add"))
			pending = true;
		udev_device_unref(device);
	}
	
	applogfailr(NULL, LOG_ERR, "epoll_wait");
}

#elif defined(WIN32)

static UINT_PTR _hotplug_wintimer_id;

VOID CALLBACK hotplug_win_timer(HWND hwnd, UINT msg, UINT_PTR idEvent, DWORD dwTime)
{
	KillTimer(NULL, _hotplug_wintimer_id);
	_hotplug_wintimer_id = 0;
	hotplug_trigger();
}

LRESULT CALLBACK hotplug_win_callback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_DEVICECHANGE && wParam == DBT_DEVNODES_CHANGED)
	{
		applog(LOG_DEBUG, "%s: Received DBT_DEVNODES_CHANGED event", __func__);
		_hotplug_wintimer_id = SetTimer(NULL, _hotplug_wintimer_id, hotplug_delay_ms, hotplug_win_timer);
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

static
void *hotplug_thread(__maybe_unused void *p)
{
	pthread_detach(pthread_self());
	
	WNDCLASS DummyWinCls = {
		.lpszClassName = "BFGDummyWinCls",
		.lpfnWndProc = hotplug_win_callback,
	};
	ATOM a = RegisterClass(&DummyWinCls);
	if (unlikely(!a))
		applogfailinfor(NULL, LOG_ERR, "RegisterClass", "%d", (int)GetLastError());
	HWND hwnd = CreateWindow((void*)(intptr_t)a, NULL, WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);
	if (unlikely(!hwnd))
		applogfailinfor(NULL, LOG_ERR, "CreateWindow", "%d", (int)GetLastError());
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	quit(0, "WM_QUIT received");
	return NULL;
}

#endif

#ifdef HAVE_BFG_HOTPLUG
static
void hotplug_start()
{
	pthread_t pth;
	if (unlikely(pthread_create(&pth, NULL, hotplug_thread, NULL)))
		applog(LOG_ERR, "Failed to start hotplug thread");
}
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
	rlim_t old_soft_limit;
	char frombuf[0x10] = "unlimited";
	char hardbuf[0x10] = "unlimited";
	
	if (getrlimit(RLIMIT_NOFILE, &fdlimit))
		applogr(, LOG_DEBUG, "setrlimit: Failed to getrlimit(RLIMIT_NOFILE)");
	
	old_soft_limit = fdlimit.rlim_cur;
	
	if (fdlimit.rlim_max > FD_SETSIZE || fdlimit.rlim_max == RLIM_INFINITY)
		fdlimit.rlim_cur = FD_SETSIZE;
	else
		fdlimit.rlim_cur = fdlimit.rlim_max;
	
	if (fdlimit.rlim_max != RLIM_INFINITY)
		snprintf(hardbuf, sizeof(hardbuf), "%lu", (unsigned long)fdlimit.rlim_max);
	if (old_soft_limit != RLIM_INFINITY)
		snprintf(frombuf, sizeof(frombuf), "%lu", (unsigned long)old_soft_limit);
	
	if (fdlimit.rlim_cur == old_soft_limit)
		applogr(, LOG_DEBUG, "setrlimit: Soft fd limit not being changed from %lu (FD_SETSIZE=%lu; hard limit=%s)",
		        (unsigned long)old_soft_limit, (unsigned long)FD_SETSIZE, hardbuf);
	
	if (setrlimit(RLIMIT_NOFILE, &fdlimit))
		applogr(, LOG_DEBUG, "setrlimit: Failed to change soft fd limit from %s to %lu (FD_SETSIZE=%lu; hard limit=%s)",
		        frombuf, (unsigned long)fdlimit.rlim_cur, (unsigned long)FD_SETSIZE, hardbuf);
	
	applog(LOG_DEBUG, "setrlimit: Changed soft fd limit from %s to %lu (FD_SETSIZE=%lu; hard limit=%s)",
	       frombuf, (unsigned long)fdlimit.rlim_cur, (unsigned long)FD_SETSIZE, hardbuf);
#else
	applog(LOG_DEBUG, "setrlimit: Not supported by platform");
#endif
}

static
void bfg_atexit(void)
{
	puts("");
}

extern void bfg_init_threadlocal();
extern bool stratumsrv_change_port(unsigned);
extern void test_aan_pll(void);

int main(int argc, char *argv[])
{
	struct sigaction handler;
	struct thr_info *thr;
	unsigned int k;
	int i;
	int rearrange_pools = 0;
	char *s;

#ifdef WIN32
	LoadLibrary("backtrace.dll");
#endif
	
	atexit(bfg_atexit);

	b58_sha256_impl = my_blkmaker_sha256_callback;
	blkmk_sha256_impl = my_blkmaker_sha256_callback;

	bfg_init_threadlocal();
#ifndef HAVE_PTHREAD_CANCEL
	setup_pthread_cancel_workaround();
#endif
	bfg_init_checksums();

#ifdef WIN32
	{
		WSADATA wsa;
		i = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (i)
			quit(1, "Failed to initialise Winsock: %s", bfg_strerror(i, BST_SOCKET));
	}
#endif
	
	/* This dangerous functions tramples random dynamically allocated
	 * variables so do it before anything at all */
	if (unlikely(curl_global_init(CURL_GLOBAL_ALL)))
		quit(1, "Failed to curl_global_init");

	initial_args = malloc(sizeof(char *) * (argc + 1));
	for  (i = 0; i < argc; i++)
		initial_args[i] = strdup(argv[i]);
	initial_args[argc] = NULL;

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
	if (unlikely(pthread_cond_init(&lp_cond, bfg_condattr)))
		quit(1, "Failed to pthread_cond_init lp_cond");

	if (unlikely(pthread_cond_init(&gws_cond, bfg_condattr)))
		quit(1, "Failed to pthread_cond_init gws_cond");

	notifier_init(submit_waiting_notifier);
	timer_unset(&tv_rescan);
	notifier_init(rescan_notifier);

	/* Create a unique get work queue */
	getq = tq_new();
	if (!getq)
		quit(1, "Failed to create getq");
	/* We use the getq mutex as the staged lock */
	stgd_lock = &getq->mutex;

#if defined(USE_CPUMINING) && defined(USE_SHA256D)
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
#else
	timeBeginPeriod(1);
#endif
	opt_kernel_path = CGMINER_PREFIX;
	cgminer_path = alloca(PATH_MAX);
	s = strdup(argv[0]);
	strcpy(cgminer_path, dirname(s));
	free(s);
	strcat(cgminer_path, "/");
#if defined(USE_CPUMINING) && defined(WIN32)
	{
		char buf[32];
		int gev = GetEnvironmentVariable("BFGMINER_BENCH_ALGO", buf, sizeof(buf));
		if (gev > 0 && gev < sizeof(buf))
		{
			setup_benchmark_pool();
			double rate = bench_algo_stage3(atoi(buf));
			
			// Write result to shared memory for parent
			char unique_name[64];
			
			if (GetEnvironmentVariable("BFGMINER_SHARED_MEM", unique_name, 32))
			{
				HANDLE map_handle = CreateFileMapping(
					INVALID_HANDLE_VALUE,   // use paging file
					NULL,                   // default security attributes
					PAGE_READWRITE,         // read/write access
					0,                      // size: high 32-bits
					4096,                   // size: low 32-bits
					unique_name             // name of map object
				);
				if (NULL != map_handle) {
					void *shared_mem = MapViewOfFile(
						map_handle,     // object to map view of
						FILE_MAP_WRITE, // read/write access
						0,              // high offset:  map from
						0,              // low offset:   beginning
						0               // default: map entire file
					);
					if (NULL != shared_mem)
						CopyMemory(shared_mem, &rate, sizeof(rate));
					(void)UnmapViewOfFile(shared_mem);
				}
				(void)CloseHandle(map_handle);
			}
			exit(0);
		}
	}
#endif

#ifdef HAVE_CURSES
	devcursor = 8;
	logstart = devcursor;
	logcursor = logstart;
#endif

	mutex_init(&submitting_lock);

	// Ensure at least the default goal is created
	get_mining_goal("default");
#ifdef USE_OPENCL
	opencl_early_init();
#endif

	schedstart.tm.tm_sec = 1;
	schedstop .tm.tm_sec = 1;

	opt_register_table(opt_early_table, NULL);
	opt_register_table(opt_config_table, NULL);
	opt_register_table(opt_cmdline_table, NULL);
	opt_early_parse(argc, argv, applog_and_exit);
	
	if (!config_loaded)
	{
		load_default_config();
		rearrange_pools = total_pools;
	}
	
	opt_free_table();
	
	/* parse command line */
	opt_register_table(opt_config_table,
			   "Options for both config file and command line");
	opt_register_table(opt_cmdline_table,
			   "Options for command line only");

	opt_parse(&argc, argv, applog_and_exit);
	if (argc != 1)
		quit(1, "Unexpected extra commandline arguments");
	
	if (rearrange_pools && rearrange_pools < total_pools)
	{
		// Prioritise commandline pools before default-config pools
		for (i = 0; i < rearrange_pools; ++i)
			pools[i]->prio += rearrange_pools;
		for ( ; i < total_pools; ++i)
			pools[i]->prio -= rearrange_pools;
	}

#ifndef HAVE_PTHREAD_CANCEL
	// Can't do this any earlier, or config isn't loaded
	applog(LOG_DEBUG, "pthread_cancel workaround in use");
#endif

#ifdef HAVE_PWD_H
	struct passwd *user_info = NULL;
	if (opt_setuid != NULL) {
		if ((user_info = getpwnam(opt_setuid)) == NULL) {
			quit(1, "Unable to find setuid user information");
		}
	}
#endif

#ifdef HAVE_CHROOT
        if (chroot_dir != NULL) {
#ifdef HAVE_PWD_H
                if (user_info == NULL && getuid() == 0) {
                        applog(LOG_WARNING, "Running as root inside chroot");
                }
#endif
                if (chroot(chroot_dir) != 0) {
                       quit(1, "Unable to chroot");
                }
		if (chdir("/"))
			quit(1, "Unable to chdir to chroot");
        }
#endif

#ifdef HAVE_PWD_H
		if (user_info != NULL) {
			if (setgid((*user_info).pw_gid) != 0)
				quit(1, "Unable to setgid");
			if (setuid((*user_info).pw_uid) != 0)
				quit(1, "Unable to setuid");
		}
#endif
	raise_fd_limits();
	
	if (opt_benchmark) {
		while (total_pools)
			remove_pool(pools[0]);

		setup_benchmark_pool();
	}
	
	if (opt_unittest) {
		test_cgpu_match();
		test_intrange();
		test_decimal_width();
		test_domain_funcs();
#ifdef USE_SCRYPT
		test_scrypt();
#endif
		test_target();
		test_uri_get_param();
		utf8_test();
#ifdef USE_JINGTIAN
		test_aan_pll();
#endif
		if (unittest_failures)
			quit(1, "Unit tests failed");
	}

#ifdef HAVE_CURSES
	if (opt_realquiet || opt_display_devs)
		use_curses = false;

	setlocale(LC_ALL, "C");
	if (use_curses)
		enable_curses();
#endif

#ifdef HAVE_LIBUSB
	int err = libusb_init(NULL);
	if (err)
		applog(LOG_WARNING, "libusb_init() failed err %d", err);
	else
		have_libusb = true;
#endif

	applog(LOG_WARNING, "Started %s", packagename);
	{
		struct bfg_loaded_configfile *configfile;
		LL_FOREACH(bfg_loaded_configfiles, configfile)
		{
			char * const cnfbuf = configfile->filename;
			int fileconf_load = configfile->fileconf_load;
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
		}
	}

	i = strlen(opt_kernel_path) + 2;
	char __kernel_path[i];
	snprintf(__kernel_path, i, "%s/", opt_kernel_path);
	opt_kernel_path = __kernel_path;

	if (want_per_device_stats)
		opt_log_output = true;

	bfg_devapi_init();
	drv_detect_all();
	total_devices = total_devices_new;
	devices = devices_new;
	total_devices_new = 0;
	devices_new = NULL;

	if (opt_display_devs) {
		int devcount = 0;
		applog(LOG_ERR, "Devices detected:");
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			char buf[0x100];
			if (cgpu->device != cgpu)
				continue;
			if (cgpu->name)
				snprintf(buf, sizeof(buf), " %s", cgpu->name);
			else
			if (cgpu->dev_manufacturer)
				snprintf(buf, sizeof(buf), " %s by %s", (cgpu->dev_product ?: "Device"), cgpu->dev_manufacturer);
			else
			if (cgpu->dev_product)
				snprintf(buf, sizeof(buf), " %s", cgpu->dev_product);
			else
				strcpy(buf, " Device");
			tailsprintf(buf, sizeof(buf), " (driver=%s; procs=%d", cgpu->drv->dname, cgpu->procs);
			if (cgpu->dev_serial)
				tailsprintf(buf, sizeof(buf), "; serial=%s", cgpu->dev_serial);
			if (cgpu->device_path)
				tailsprintf(buf, sizeof(buf), "; path=%s", cgpu->device_path);
			tailsprintf(buf, sizeof(buf), ")");
			_applog(LOG_NOTICE, buf);
			++devcount;
		}
		quit(0, "%d devices listed", devcount);
	}

	mining_threads = 0;
	for (i = 0; i < total_devices; ++i)
		register_device(devices[i]);

	if (!total_devices) {
		applog(LOG_WARNING, "No devices detected!");
		if (use_curses)
			applog(LOG_WARNING, "Waiting for devices; press 'M+' to add, or 'Q' to quit");
		else
			applog(LOG_WARNING, "Waiting for devices");
	}
	
#ifdef HAVE_CURSES
	switch_logsize();
#endif

#if BLKMAKER_VERSION > 1 && defined(USE_SHA256D)
	if (opt_load_bitcoin_conf && !(get_mining_goal("default")->malgo->algo != POW_SHA256D || opt_benchmark))
		add_local_gbt(total_pools);
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
		size_t siz;

		if (!pool->rpc_url)
			quit(1, "No URI supplied for pool %u", i);
		
		if (!pool->rpc_userpass) {
			if (!pool->rpc_user || !pool->rpc_pass)
				quit(1, "No login credentials supplied for pool %u %s", i, pool->rpc_url);
			siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
			pool->rpc_userpass = malloc(siz);
			if (!pool->rpc_userpass)
				quit(1, "Failed to malloc userpass");
			snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);
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

	#if defined(unix) || defined(__APPLE__)
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

	total_control_threads = 6;
	control_thr = calloc(total_control_threads, sizeof(*thr));
	if (!control_thr)
		quit(1, "Failed to calloc control_thr");

	if (opt_benchmark)
		goto begin_bench;

	applog(LOG_NOTICE, "Probing for an alive pool");
	do {
		bool still_testing;
		int i;

		/* Look for at least one active pool before starting */
		probe_pools();
		do {
			sleep(1);
			if (pools_active)
				break;
			still_testing = false;
			for (int i = 0; i < total_pools; ++i)
				if (pools[i]->testing)
					still_testing = true;
		} while (still_testing);

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
	if (detect_algo == 1 && get_mining_goal("default")->malgo->algo != POW_SCRYPT) {
		applog(LOG_NOTICE, "Detected scrypt algorithm");
		set_malgo_scrypt();
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
	time_t miner_start_ts = time(NULL);
	if (schedstart.tm.tm_sec)
		localtime_r(&miner_start_ts, &schedstart.tm);
	if (schedstop.tm.tm_sec)
		localtime_r(&miner_start_ts, &schedstop .tm);
	get_datestamp(datestamp, sizeof(datestamp), miner_start_ts);

	// Initialise processors and threads
	k = 0;
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		allocate_cgpu(cgpu, &k);
	}

	// Start threads
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		start_cgpu(cgpu);
	}

#ifdef USE_OPENCL
	for (i = 0; i < nDevs; i++)
		pause_dynamic_threads(i);
#endif

#if defined(USE_CPUMINING) && defined(USE_SHA256D)
	if (opt_n_threads > 0)
		applog(LOG_INFO, "%d cpu miner threads started, using '%s' algorithm.",
		       opt_n_threads, algo_names[opt_algo]);
#endif

	cgtime(&total_tv_start);
	cgtime(&total_tv_end);

	if (!opt_benchmark)
	{
		pthread_t submit_thread;
		if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, NULL)))
			quit(1, "submit_work thread create failed");
	}

	watchpool_thr_id = 1;
	thr = &control_thr[watchpool_thr_id];
	/* start watchpool thread */
	if (thr_info_create(thr, NULL, watchpool_thread, NULL))
		quit(1, "watchpool thread create failed");
	pthread_detach(thr->pth);

	watchdog_thr_id = 2;
	thr = &control_thr[watchdog_thr_id];
	/* start watchdog thread */
	if (thr_info_create(thr, NULL, watchdog_thread, NULL))
		quit(1, "watchdog thread create failed");
	pthread_detach(thr->pth);

#ifdef USE_OPENCL
	/* Create reinit gpu thread */
	gpur_thr_id = 3;
	thr = &control_thr[gpur_thr_id];
	thr->q = tq_new();
	if (!thr->q)
		quit(1, "tq_new failed for gpur_thr_id");
	if (thr_info_create(thr, NULL, reinit_gpu, thr))
		quit(1, "reinit_gpu thread create failed");
#endif	

	/* Create API socket thread */
	api_thr_id = 4;
	thr = &control_thr[api_thr_id];
	if (thr_info_create(thr, NULL, api_thread, thr))
		quit(1, "API thread create failed");
	
#ifdef USE_LIBMICROHTTPD
	if (httpsrv_port != -1)
		httpsrv_start(httpsrv_port);
#endif

#ifdef USE_LIBEVENT
	if (stratumsrv_port != -1)
		stratumsrv_change_port(stratumsrv_port);
#endif

#ifdef HAVE_BFG_HOTPLUG
	if (opt_hotplug)
		hotplug_start();
#endif

#ifdef HAVE_CURSES
	/* Create curses input thread for keyboard input. Create this last so
	 * that we know all threads are created since this can call kill_work
	 * to try and shut down ll previous threads. */
	input_thr_id = 5;
	thr = &control_thr[input_thr_id];
	if (thr_info_create(thr, NULL, input_thread, thr))
		quit(1, "input thread create failed");
	pthread_detach(thr->pth);
#endif

	/* Just to be sure */
	if (total_control_threads != 6)
		quit(1, "incorrect total_control_threads (%d) should be 7", total_control_threads);

	/* Once everything is set up, main() becomes the getwork scheduler */
	while (42) {
		int ts, max_staged = opt_queue;
		struct pool *pool, *cp;
		bool lagging = false;
		struct curl_ent *ce;
		struct work *work;
		struct mining_algorithm *malgo = NULL;

		cp = current_pool();

		// Generally, each processor needs a new work, and all at once during work restarts
		max_staged += base_queue;

		mutex_lock(stgd_lock);
		ts = __total_staged(false);

		if (!pool_localgen(cp) && !ts && !opt_fail_only)
			lagging = true;

		/* Wait until hash_pop tells us we need to create more work */
		if (ts > max_staged) {
			{
				LL_FOREACH(mining_algorithms, malgo)
				{
					if (!malgo->goal_refs)
						continue;
					if (!malgo->base_queue)
						continue;
					if (malgo->staged < malgo->base_queue + opt_queue)
					{
						mutex_unlock(stgd_lock);
						pool = select_pool(lagging, malgo);
						if (pool)
						{
							work = make_work();
							work->spare = true;
							goto retry;
						}
						mutex_lock(stgd_lock);
					}
				}
				malgo = NULL;
			}
			staged_full = true;
			pthread_cond_wait(&gws_cond, stgd_lock);
			ts = __total_staged(false);
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
		pool = select_pool(lagging, malgo);

retry:
		if (pool->has_stratum) {
			while (!pool->stratum_active || !pool->stratum_notify) {
				struct pool *altpool = select_pool(true, malgo);

				if (altpool == pool && pool->has_stratum)
					cgsleep_ms(5000);
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
				struct timeval tv_now;
				cgtime(&tv_now);
				free_work(work);
				work = make_clone(pool->last_work_copy);
				mutex_unlock(&pool->last_work_lock);
				roll_work(work);
				applog(LOG_DEBUG, "Generated work from latest GBT job in get_work_thread with %d seconds left", (int)blkmk_time_left(work->tr->tmpl, tv_now.tv_sec));
				stage_work(work);
				continue;
			} else if (last_work->tr && pool->proto == PLP_GETBLOCKTEMPLATE && blkmk_work_left(last_work->tr->tmpl) > (unsigned long)mining_threads) {
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
			get_benchmark_work(work, opt_benchmark_intense);
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
			next_pool = select_pool(!opt_fail_only, malgo);
			if (pool == next_pool) {
				applog(LOG_DEBUG, "Pool %d json_rpc_call failed on get work, retrying in 5s", pool->pool_no);
				cgsleep_ms(5000);
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
