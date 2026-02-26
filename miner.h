/*
 * Copyright 2012-2014 Luke Dashjr
 * Copyright 2014 Nate Woolls
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2012-2013 Andrew Smith
 * Copyright 2011 Glenn Francis Murray
 * Copyright 2010-2011 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFG_MINER_H
#define BFG_MINER_H

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <jansson.h>
#include <curl/curl.h>
#include <sched.h>
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <blkmaker.h>
#include <blktemplate.h>

#if defined(WORDS_BIGENDIAN) && !defined(__BIG_ENDIAN__)
/* uthash.h depends on __BIG_ENDIAN__ on BE platforms */
#define __BIG_ENDIAN__ 1
#endif

#include <uthash.h>
#include <utlist.h>

#include "logging.h"
#include "util.h"

extern const char * const bfgminer_name_space_ver;
extern const char * const bfgminer_name_slash_ver;
extern const char * const bfgminer_ver;

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif !defined alloca
# ifdef __GNUC__
#  define alloca __builtin_alloca
# elif defined _AIX
#  define alloca __alloca
# elif defined _MSC_VER
#  include <malloc.h>
#  define alloca _alloca
# elif !defined HAVE_ALLOCA
#  ifdef  __cplusplus
extern "C"
#  endif
void *alloca (size_t);
# endif
#endif

#ifdef __MINGW32__
#include <windows.h>
#include <io.h>
static inline int fsync (int fd)
{
	return (FlushFileBuffers ((HANDLE) _get_osfhandle (fd))) ? 0 : -1;
}

#ifndef EWOULDBLOCK
# define EWOULDBLOCK EAGAIN
#endif

#ifndef MSG_DONTWAIT
# define MSG_DONTWAIT 0x1000000
#endif
#endif /* __MINGW32__ */

#if defined (__linux)
 #ifndef LINUX
  #define LINUX
 #endif
#endif


#ifdef HAVE_ADL
 #include "ADL/adl_sdk.h"
#endif

#ifdef HAVE_LIBUSB
  #include <libusb.h>
#endif

#ifdef USE_ZTEX
  #include "libztex.h"
#endif

#ifdef USE_BITFURY
  #include "libbitfury.h"
#endif

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif
#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif
#ifdef HAVE_LIBKERN_OSBYTEORDER_H
#include <libkern/OSByteOrder.h>
#endif
#ifndef bswap_16
#define	bswap_16(value)  \
 	((((value) & 0xff) << 8) | ((value) >> 8))

#define	bswap_32(value)	\
 	(((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
 	(uint32_t)bswap_16((uint16_t)((value) >> 16)))

#define	bswap_64(value)	\
 	(((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
 	    << 32) | \
 	(uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif

/* This assumes htobe32 is a macro and that if it doesn't exist, then the
 * also won't exist */
#ifndef htobe32
# ifndef WORDS_BIGENDIAN
#  define htole16(x) (x)
#  define htole32(x) (x)
#  define htole64(x) (x)
#  define htobe16(x) bswap_16(x)
#  define htobe32(x) bswap_32(x)
#  define htobe64(x) bswap_64(x)
# else
#  define htole16(x) bswap_16(x)
#  define htole32(x) bswap_32(x)
#  define htole64(x) bswap_64(x)
#  define htobe16(x) (x)
#  define htobe32(x) (x)
#  define htobe64(x) (x)
# endif
#endif
#ifndef be32toh
# define le16toh(x) htole16(x)
# define le32toh(x) htole32(x)
# define le64toh(x) htole64(x)
# define be16toh(x) htobe16(x)
# define be32toh(x) htobe32(x)
# define be64toh(x) htobe64(x)
#endif

#ifndef max
#  define max(a, b)  ((a) > (b) ? (a) : (b))
#endif

#undef unlikely
#undef likely
#if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#define unlikely(expr) (__builtin_expect(!!(expr), 0))
#define likely(expr) (__builtin_expect(!!(expr), 1))
#else
#define unlikely(expr) (expr)
#define likely(expr) (expr)
#endif
#ifndef __maybe_unused
#define __maybe_unused		__attribute__((unused))
#endif

#define uninitialised_var(x) x = x

#if defined(__i386__)
#define WANT_CRYPTOPP_ASM32
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifdef HAVE_CURSES
	extern int my_cancellable_getch(void);
#	ifdef getch
		// getch() is a macro
		static int __maybe_unused __real_getch(void) {
			return getch();
		}
#		undef getch
#		define getch()  my_cancellable_getch()
#	else
		// getch() is a real function
#		define __real_getch  getch
#		define getch()  my_cancellable_getch()
#	endif
#endif

enum alive {
	LIFE_WELL,
	LIFE_SICK,
	LIFE_DEAD,
	LIFE_NOSTART,
	LIFE_INIT,
	LIFE_WAIT,
	LIFE_INIT2,  // Still initializing, but safe to call functions
	LIFE_DEAD2,  // Totally dead, NOT safe to call functions
	LIFE_MIXED,  // Only valid in display variables, NOT on devices
};


enum pool_strategy {
	POOL_FAILOVER,
	POOL_ROUNDROBIN,
	POOL_ROTATE,
	POOL_LOADBALANCE,
	POOL_BALANCE,
};

#define TOP_STRATEGY (POOL_BALANCE)

struct strategies {
	const char *s;
};

struct cgpu_info;

#ifdef HAVE_ADL
struct gpu_adl {
	ADLTemperature lpTemperature;
	int iAdapterIndex;
	int lpAdapterID;
	int iBusNumber;
	char strAdapterName[256];

	ADLPMActivity lpActivity;
	ADLODParameters lpOdParameters;
	ADLODPerformanceLevels *DefPerfLev;
	ADLFanSpeedInfo lpFanSpeedInfo;
	ADLFanSpeedValue lpFanSpeedValue;
	ADLFanSpeedValue DefFanSpeedValue;

	int iEngineClock;
	int iMemoryClock;
	int iVddc;
	int iPercentage;

	bool autofan;
	bool autoengine;
	bool managed; /* Were the values ever changed on this card */

	int lastengine;
	int lasttemp;
	int targetfan;
	int overtemp;
	int minspeed;
	int maxspeed;

	int gpu;
	bool has_fanspeed;
	struct gpu_adl *twin;
};
#endif

enum pow_algorithm {
#ifdef USE_KECCAK
	POW_KECCAK,
#endif
#ifdef USE_SHA256D
	POW_SHA256D,
#endif
#ifdef USE_SCRYPT
	POW_SCRYPT,
#endif
	POW_ALGORITHM_COUNT,
};

struct api_data;
struct thr_info;
struct work;
struct lowlevel_device_info;

enum bfg_probe_result_flags_values {
	BPR_CONTINUE_PROBES = 1<< 0,
	BPR_DONT_RESCAN     = 1<< 1,
	BPR_WRONG_DEVTYPE   = BPR_CONTINUE_PROBES | BPR_DONT_RESCAN,
};
extern unsigned *_bfg_probe_result_flags();
#define bfg_probe_result_flags (*_bfg_probe_result_flags())

struct mining_algorithm;

struct device_drv {
	const char *dname;
	const char *name;
	int8_t probe_priority;
	bool lowl_probe_by_name_only;

	// DRV-global functions
	void (*drv_init)();
	// drv_min_nonce_diff's proc may be NULL
	// drv_min_nonce_diff should return negative if algorithm is not supported
	float (*drv_min_nonce_diff)(struct cgpu_info *proc, const struct mining_algorithm *);
	void (*drv_detect)();
	bool (*lowl_match)(const struct lowlevel_device_info *);
	bool (*lowl_probe)(const struct lowlevel_device_info *);

	// Processor-specific functions
	void (*watchdog)(struct cgpu_info *, const struct timeval *tv_now);
	void (*reinit_device)(struct cgpu_info *);
	bool (*override_statline_temp2)(char *buf, size_t bufsz, struct cgpu_info *, bool per_processor);
	struct api_data* (*get_api_extra_device_detail)(struct cgpu_info *);
	struct api_data* (*get_api_extra_device_status)(struct cgpu_info *);
	struct api_data *(*get_api_stats)(struct cgpu_info *);
	bool (*get_stats)(struct cgpu_info *);
	bool (*identify_device)(struct cgpu_info *);  // e.g. to flash a led
	char *(*set_device)(struct cgpu_info *, char *option, char *setting, char *replybuf);
	void (*proc_wlogprint_status)(struct cgpu_info *);
	void (*proc_tui_wlogprint_choices)(struct cgpu_info *);
	const char *(*proc_tui_handle_choice)(struct cgpu_info *, int input);
	void (*zero_stats)(struct cgpu_info *);
	double (*get_proc_rolling_hashrate)(struct cgpu_info *);

	// Thread-specific functions
	bool (*thread_prepare)(struct thr_info *);
	void (*minerloop)(struct thr_info *);
	uint64_t (*can_limit_work)(struct thr_info *);
	bool (*thread_init)(struct thr_info *);
	bool (*prepare_work)(struct thr_info *, struct work *);
	int64_t (*scanhash)(struct thr_info *, struct work *, int64_t);
	int64_t (*scanwork)(struct thr_info *);

	/* Used to extract work from the hash table of queued work and tell
	 * the main loop that it should not add any further work to the table.
	 */
	bool (*queue_full)(struct cgpu_info *);
	void (*flush_work)(struct cgpu_info *);

	void (*hw_error)(struct thr_info *);
	void (*thread_shutdown)(struct thr_info *);
	void (*thread_disable)(struct thr_info *);
	void (*thread_enable)(struct thr_info *);

	// Can be used per-thread or per-processor (only with minerloop async or queue!)
	void (*poll)(struct thr_info *);

	// === Implemented by minerloop_async ===
	bool (*job_prepare)(struct thr_info*, struct work*, uint64_t);
	void (*job_start)(struct thr_info*);
	void (*job_get_results)(struct thr_info*, struct work*);
	int64_t (*job_process_results)(struct thr_info*, struct work*, bool stopping);

	// === Implemented by minerloop_queue ===
	bool (*queue_append)(struct thr_info *, struct work *);
	void (*queue_flush)(struct thr_info *);
};

enum dev_enable {
	DEV_ENABLED,
	DEV_DISABLED,     // Disabled by user
	DEV_RECOVER,      // Disabled by temperature cutoff in watchdog
	DEV_RECOVER_ERR,  // Disabled by communications error
	DEV_RECOVER_DRV,  // Disabled by driver
};

enum cl_kernels {
	KL_NONE,
#ifdef USE_SHA256D
	KL_POCLBM,
	KL_PHATK,
	KL_DIAKGCN,
	KL_DIABLO,
#endif
#ifdef USE_OPENCL_FULLHEADER
	KL_FULLHEADER,
#endif
#ifdef USE_SCRYPT
	KL_SCRYPT,
#endif
	OPENCL_KERNEL_INTERFACE_COUNT,
};

enum dev_reason {
	REASON_THREAD_FAIL_INIT,
	REASON_THREAD_ZERO_HASH,
	REASON_THREAD_FAIL_QUEUE,
	REASON_DEV_SICK_IDLE_60,
	REASON_DEV_DEAD_IDLE_600,
	REASON_DEV_NOSTART,
	REASON_DEV_OVER_HEAT,
	REASON_DEV_THERMAL_CUTOFF,
	REASON_DEV_COMMS_ERROR,
	REASON_DEV_THROTTLE,
};

#define REASON_NONE			"None"
#define REASON_THREAD_FAIL_INIT_STR	"Thread failed to init"
#define REASON_THREAD_ZERO_HASH_STR	"Thread got zero hashes"
#define REASON_THREAD_FAIL_QUEUE_STR	"Thread failed to queue work"
#define REASON_DEV_SICK_IDLE_60_STR	"Device idle for 60s"
#define REASON_DEV_DEAD_IDLE_600_STR	"Device dead - idle for 600s"
#define REASON_DEV_NOSTART_STR		"Device failed to start"
#define REASON_DEV_OVER_HEAT_STR	"Device over heated"
#define REASON_DEV_THERMAL_CUTOFF_STR	"Device reached thermal cutoff"
#define REASON_DEV_COMMS_ERROR_STR	"Device comms error"
#define REASON_DEV_THROTTLE_STR		"Device throttle"
#define REASON_UNKNOWN_STR		"Unknown reason - code bug"

#define MIN_SEC_UNSET 99999999

enum {
	MSG_NOPOOL		= 8,
	MSG_MISPID		= 25,
	MSG_INVPID		= 26,
	MSG_DUPPID		= 74,
	MSG_POOLPRIO	= 73,
};

struct cgminer_stats {
	struct timeval start_tv;
	
	uint32_t getwork_calls;
	struct timeval getwork_wait;
	struct timeval getwork_wait_max;
	struct timeval getwork_wait_min;

	struct timeval _get_start;
};

// Just the actual network getworks to the pool
struct cgminer_pool_stats {
	uint32_t getwork_calls;
	uint32_t getwork_attempts;
	struct timeval getwork_wait;
	struct timeval getwork_wait_max;
	struct timeval getwork_wait_min;
	double getwork_wait_rolling;
	bool hadrolltime;
	bool canroll;
	bool hadexpire;
	uint32_t rolltime;
	double min_diff;
	double max_diff;
	double last_diff;
	uint32_t min_diff_count;
	uint32_t max_diff_count;
	uint64_t times_sent;
	uint64_t bytes_sent;
	uint64_t net_bytes_sent;
	uint64_t times_received;
	uint64_t bytes_received;
	uint64_t net_bytes_received;
};


#define PRIprepr "-6s"
#define PRIpreprv "s"

#define ALLOC_H2B_NOUNIT  6
#define ALLOC_H2B_SHORT   7
#define ALLOC_H2B_SPACED  8
#define ALLOC_H2B_SHORTV  7


struct cgpu_info {
	int cgminer_id;
	int device_line_id;
	struct device_drv *drv;
	const struct bfg_set_device_definition *set_device_funcs;
	int device_id;
	char *dev_repr;
	char *dev_repr_ns;
	const char *name;
	
	int procs;
	int proc_id;
	char proc_repr[9];
	char proc_repr_ns[9];
	struct cgpu_info *device;
	struct cgpu_info *next_proc;
	int extra_work_queue;
	
	const char *device_path;
	void *device_data;
	const char *dev_manufacturer;
	const char *dev_product;
	const char *dev_serial;
	union {
#ifdef USE_ZTEX
		struct libztex_device *device_ztex;
#endif
		int device_fd;
#ifdef USE_X6500
		struct ft232r_device_handle *device_ft232r;
#endif
	};
#ifdef USE_AVALON
	struct work **works;
	int work_array;
	int queued;
	int results;
#endif
#ifdef USE_BITFORCE
	struct timeval work_start_tv;
	unsigned int wait_ms;
	unsigned int sleep_ms;
	double avg_wait_f;
	unsigned int avg_wait_d;
	uint32_t nonces;
	bool polling;
#endif
#if defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_TWINFURY)
	bool flash_led;
#endif
	pthread_mutex_t		device_mutex;
	pthread_cond_t	device_cond;

	enum dev_enable deven;
	bool already_set_defaults;
	int accepted;
	int rejected;
	int stale;
	double bad_diff1;
	int hw_errors;
	double rolling;
	double total_mhashes;
	double utility;
	double utility_diff1;
	enum alive status;
	char init[40];
	struct timeval last_message_tv;

	int threads;
	struct thr_info **thr;

	int64_t max_hashes;

	const char *kname;

	float temp;
	int cutofftemp;
	int targettemp;
	bool targettemp_user;

	double diff1;
	double diff_accepted;
	double diff_rejected;
	double diff_stale;
	int last_share_pool;
	time_t last_share_pool_time;
	double last_share_diff;
	time_t last_device_valid_work;

	time_t device_last_well;
	time_t device_last_not_well;
	struct timeval tv_device_last_not_well;
	enum dev_reason device_not_well_reason;
	float reinit_backoff;
	int thread_fail_init_count;
	int thread_zero_hash_count;
	int thread_fail_queue_count;
	int dev_sick_idle_60_count;
	int dev_dead_idle_600_count;
	int dev_nostart_count;
	int dev_over_heat_count;	// It's a warning but worth knowing
	int dev_thermal_cutoff_count;
	int dev_comms_error_count;
	int dev_throttle_count;

	struct cgminer_stats cgminer_stats;

	pthread_rwlock_t qlock;
	struct work *queued_work;
	struct work *unqueued_work;
	unsigned int queued_count;

	bool disable_watchdog;
	bool shutdown;
};

extern void renumber_cgpu(struct cgpu_info *);
extern bool add_cgpu(struct cgpu_info*);

struct tq_ent;

struct thread_q {
	struct tq_ent *q;

	bool frozen;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
};

enum thr_busy_state {
	TBS_IDLE,
	TBS_GETTING_RESULTS,
	TBS_STARTING_JOB,
};

struct thr_info {
	int		id;
	int		device_thread;
	bool		primary_thread;

	bool		has_pth;
	pthread_t	pth;
	struct thread_q	*q;
	struct cgpu_info *cgpu;
	void *cgpu_data;
	struct timeval last;
	struct timeval sick;

	bool	scanhash_working;
	uint64_t hashes_done;
	struct timeval tv_hashes_done;
	struct timeval tv_lastupdate;
	struct timeval _tv_last_hashes_done_call;

	bool	pause;
	time_t	getwork;
	double	rolling;

	// Used by minerloop_async
	struct work *prev_work;
	struct work *work;
	struct work *next_work;
	enum thr_busy_state busy_state;
	bool _mt_disable_called;
	struct timeval tv_morework;
	struct work *results_work;
	bool _job_transition_in_progress;
	bool _proceed_with_new_job;
	struct timeval tv_results_jobstart;
	struct timeval tv_jobstart;
	struct timeval tv_poll;
	struct timeval tv_watchdog;
	notifier_t notifier;
	bool starting_next_work;
	uint32_t _max_nonce;
	notifier_t mutex_request;

	// Used by minerloop_queue
	struct work *work_list;
	bool queue_full;

	bool	work_restart;
	notifier_t work_restart_notifier;
};

struct string_elist {
	char *string;
	bool free_me;

	struct string_elist *prev;
	struct string_elist *next;
};

static inline void string_elist_add(const char *s, struct string_elist **head)
{
	struct string_elist *n;

	n = calloc(1, sizeof(*n));
	n->string = strdup(s);
	n->free_me = true;
	DL_APPEND(*head, n);
}

static inline void string_elist_del(struct string_elist **head, struct string_elist *item)
{
	if (item->free_me)
		free(item->string);
	DL_DELETE(*head, item);
	free(item);
}


struct bfg_loaded_configfile {
	char *filename;
	int fileconf_load;
	
	struct bfg_loaded_configfile *next;
};

extern struct bfg_loaded_configfile *bfg_loaded_configfiles;


static inline uint32_t swab32(uint32_t v)
{
	return bswap_32(v);
}

static inline void swap256(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;

	dest[0] = src[7];
	dest[1] = src[6];
	dest[2] = src[5];
	dest[3] = src[4];
	dest[4] = src[3];
	dest[5] = src[2];
	dest[6] = src[1];
	dest[7] = src[0];
}

static inline void swap32yes(void*out, const void*in, size_t sz) {
	size_t swapcounter = 0;
	for (swapcounter = 0; swapcounter < sz; ++swapcounter)
		(((uint32_t*)out)[swapcounter]) = swab32(((uint32_t*)in)[swapcounter]);
}

#define LOCAL_swap32(type, var, sz)  \
	type __swapped_ ## var[sz * 4 / sizeof(type)];  \
	swap32yes(__swapped_ ## var, var, sz);  \
	var = __swapped_ ## var;  \
// end

#ifdef WORDS_BIGENDIAN
#  define swap32tobe(out, in, sz)  ((out == in) ? (void)0 : (void)memmove(out, in, (sz)*4))
#  define LOCAL_swap32be(type, var, sz)  ;
#  define swap32tole(out, in, sz)  swap32yes(out, in, sz)
#  define LOCAL_swap32le(type, var, sz)  LOCAL_swap32(type, var, sz)
#else
#  define swap32tobe(out, in, sz)  swap32yes(out, in, sz)
#  define LOCAL_swap32be(type, var, sz)  LOCAL_swap32(type, var, sz)
#  define swap32tole(out, in, sz)  ((out == in) ? (void)0 : (void)memmove(out, in, (sz)*4))
#  define LOCAL_swap32le(type, var, sz)  ;
#endif

static inline void swab256(void *dest_p, const void *src_p)
{
	uint32_t *dest = dest_p;
	const uint32_t *src = src_p;

	dest[0] = swab32(src[7]);
	dest[1] = swab32(src[6]);
	dest[2] = swab32(src[5]);
	dest[3] = swab32(src[4]);
	dest[4] = swab32(src[3]);
	dest[5] = swab32(src[2]);
	dest[6] = swab32(src[1]);
	dest[7] = swab32(src[0]);
}

static inline
void bswap_96p(void * const dest_p, const void * const src_p)
{
	uint32_t * const dest = dest_p;
	const uint32_t * const src = src_p;
	
	dest[0] = bswap_32(src[2]);
	dest[1] = bswap_32(src[1]);
	dest[2] = bswap_32(src[0]);
}

static inline
void bswap_32mult(void * const dest_p, const void * const src_p, const size_t sz)
{
	const uint32_t *s = src_p;
	const uint32_t *s_end = &s[sz];
	uint32_t *d = dest_p;
	d = &d[sz - 1];
	
	for ( ; s < s_end; ++s, --d)
		*d = bswap_32(*s);
}

#define flip12(dest_p, src_p) swap32yes(dest_p, src_p, 12 / 4)
#define flip32(dest_p, src_p) swap32yes(dest_p, src_p, 32 / 4)

#define WATCHDOG_INTERVAL  2
extern void bfg_watchdog(struct cgpu_info *, struct timeval *tvp_now);

extern void _quit(int status);

static inline void mutex_lock(pthread_mutex_t *lock)
{
	if (unlikely(pthread_mutex_lock(lock)))
		quit(1, "WTF MUTEX ERROR ON LOCK!");
}

static inline void mutex_unlock_noyield(pthread_mutex_t *lock)
{
	if (unlikely(pthread_mutex_unlock(lock)))
		quit(1, "WTF MUTEX ERROR ON UNLOCK!");
}

static inline void mutex_unlock(pthread_mutex_t *lock)
{
	mutex_unlock_noyield(lock);
	sched_yield();
}

static inline int mutex_trylock(pthread_mutex_t *lock)
{
	return pthread_mutex_trylock(lock);
}

static inline void wr_lock(pthread_rwlock_t *lock)
{
	if (unlikely(pthread_rwlock_wrlock(lock)))
		quit(1, "WTF WRLOCK ERROR ON LOCK!");
}

static inline void rd_lock(pthread_rwlock_t *lock)
{
	if (unlikely(pthread_rwlock_rdlock(lock)))
		quit(1, "WTF RDLOCK ERROR ON LOCK!");
}

static inline void rw_unlock(pthread_rwlock_t *lock)
{
	if (unlikely(pthread_rwlock_unlock(lock)))
		quit(1, "WTF RWLOCK ERROR ON UNLOCK!");
}

static inline void rd_unlock_noyield(pthread_rwlock_t *lock)
{
	rw_unlock(lock);
}

static inline void wr_unlock_noyield(pthread_rwlock_t *lock)
{
	rw_unlock(lock);
}

static inline void rd_unlock(pthread_rwlock_t *lock)
{
	rw_unlock(lock);
	sched_yield();
}

static inline void wr_unlock(pthread_rwlock_t *lock)
{
	rw_unlock(lock);
	sched_yield();
}

static inline void mutex_init(pthread_mutex_t *lock)
{
	if (unlikely(pthread_mutex_init(lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");
}

static inline void mutex_destroy(pthread_mutex_t *lock)
{
	/* Ignore return code. This only invalidates the mutex on linux but
	 * releases resources on windows. */
	pthread_mutex_destroy(lock);
}

static inline void rwlock_init(pthread_rwlock_t *lock)
{
	if (unlikely(pthread_rwlock_init(lock, NULL)))
		quit(1, "Failed to pthread_rwlock_init");
}

/* cgminer locks, a write biased variant of rwlocks */
struct cglock {
	pthread_mutex_t mutex;
	pthread_rwlock_t rwlock;
};

typedef struct cglock cglock_t;

static inline void rwlock_destroy(pthread_rwlock_t *lock)
{
	pthread_rwlock_destroy(lock);
}

static inline void cglock_init(cglock_t *lock)
{
	mutex_init(&lock->mutex);
	rwlock_init(&lock->rwlock);
}

static inline void cglock_destroy(cglock_t *lock)
{
	rwlock_destroy(&lock->rwlock);
	mutex_destroy(&lock->mutex);
}

/* Read lock variant of cglock. Cannot be promoted. */
static inline void cg_rlock(cglock_t *lock)
{
	mutex_lock(&lock->mutex);
	rd_lock(&lock->rwlock);
	mutex_unlock_noyield(&lock->mutex);
}

/* Intermediate variant of cglock - behaves as a read lock but can be promoted
 * to a write lock or demoted to read lock. */
static inline void cg_ilock(cglock_t *lock)
{
	mutex_lock(&lock->mutex);
}

/* Upgrade intermediate variant to a write lock */
static inline void cg_ulock(cglock_t *lock)
{
	wr_lock(&lock->rwlock);
}

/* Write lock variant of cglock */
static inline void cg_wlock(cglock_t *lock)
{
	mutex_lock(&lock->mutex);
	wr_lock(&lock->rwlock);
}

/* Downgrade write variant to a read lock */
static inline void cg_dwlock(cglock_t *lock)
{
	wr_unlock_noyield(&lock->rwlock);
	rd_lock(&lock->rwlock);
	mutex_unlock_noyield(&lock->mutex);
}

/* Demote a write variant to an intermediate variant */
static inline void cg_dwilock(cglock_t *lock)
{
	wr_unlock(&lock->rwlock);
}

/* Downgrade intermediate variant to a read lock */
static inline void cg_dlock(cglock_t *lock)
{
	rd_lock(&lock->rwlock);
	mutex_unlock(&lock->mutex);
}

static inline void cg_runlock(cglock_t *lock)
{
	rd_unlock(&lock->rwlock);
}

static inline void cg_iunlock(cglock_t *lock)
{
	mutex_unlock(&lock->mutex);
}

static inline void cg_wunlock(cglock_t *lock)
{
	wr_unlock_noyield(&lock->rwlock);
	mutex_unlock(&lock->mutex);
}

struct pool;

#define API_MCAST_CODE "FTW"
#define API_MCAST_ADDR "224.0.0.75"

extern bool opt_protocol;
extern bool opt_dev_protocol;
extern char *opt_coinbase_sig;
extern char *request_target_str;
extern float request_pdiff;
extern double request_bdiff;
extern int opt_skip_checks;
extern char *opt_kernel_path;
extern char *opt_socks_proxy;
extern char *cmd_idle, *cmd_sick, *cmd_dead;
extern char *cgminer_path;
extern bool opt_fail_only;
extern bool opt_autofan;
extern bool opt_autoengine;
extern bool use_curses;
extern int last_logstatusline_len;
#ifdef HAVE_LIBUSB
extern bool have_libusb;
#endif
extern int httpsrv_port;
extern long stratumsrv_port;
extern char *opt_api_allow;
extern bool opt_api_mcast;
extern char *opt_api_mcast_addr;
extern char *opt_api_mcast_code;
extern char *opt_api_mcast_des;
extern int opt_api_mcast_port;
extern char *opt_api_groups;
extern char *opt_api_description;
extern int opt_api_port;
extern bool opt_api_listen;
extern bool opt_api_network;
extern bool opt_delaynet;
extern time_t last_getwork;
extern bool opt_disable_client_reconnect;
extern bool opt_restart;
extern char *opt_icarus_options;
extern char *opt_icarus_timing;
extern bool opt_worktime;
#ifdef USE_AVALON
extern char *opt_avalon_options;
#endif
#ifdef USE_KLONDIKE
extern char *opt_klondike_options;
#endif
#ifdef USE_BITFORCE
extern bool opt_bfl_noncerange;
#endif
extern int swork_id;

extern pthread_rwlock_t netacc_lock;

extern const uint32_t sha256_init_state[];
extern json_t *json_web_config(CURL *curl, const char *url);
extern json_t *json_rpc_call(CURL *curl, const char *url, const char *userpass,
			     const char *rpc_req, bool, bool, int *,
			     struct pool *pool, bool);
extern bool our_curl_supports_proxy_uris();
extern void bin2hex(char *out, const void *in, size_t len);
extern bool hex2bin(unsigned char *p, const char *hexstr, size_t len);

extern int opt_queue;
extern int opt_scantime;
extern int opt_expiry;

extern cglock_t control_lock;
extern pthread_mutex_t stats_lock;
extern pthread_mutex_t hash_lock;
extern pthread_mutex_t console_lock;
extern cglock_t ch_lock;
extern pthread_rwlock_t mining_thr_lock;
extern pthread_rwlock_t devices_lock;


extern bool _bfg_console_cancel_disabled;
extern int _bfg_console_prev_cancelstate;

static inline
void bfg_console_lock(void)
{
	int prev_cancelstate;
	bool cancel_disabled = !pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &prev_cancelstate);
	mutex_lock(&console_lock);
	_bfg_console_cancel_disabled = cancel_disabled;
	if (cancel_disabled)
		_bfg_console_prev_cancelstate = prev_cancelstate;
}

static inline
void bfg_console_unlock(void)
{
	int prev_cancelstate;
	bool cancel_disabled = _bfg_console_cancel_disabled;
	if (cancel_disabled)
	{
		prev_cancelstate = _bfg_console_prev_cancelstate;
		mutex_unlock(&console_lock);
		pthread_setcancelstate(prev_cancelstate, &prev_cancelstate);
	}
	else
		mutex_unlock(&console_lock);
}


extern void thread_reportin(struct thr_info *thr);
extern void thread_reportout(struct thr_info *);
extern void clear_stratum_shares(struct pool *pool);
extern void hashmeter2(struct thr_info *);
extern bool stale_work2(struct work *, bool share, bool have_pool_data_lock);
#define stale_work(work, share)  stale_work2(work, share, false)
extern bool stale_work_future(struct work *, bool share, unsigned long ustime);
extern void blkhashstr(char *out, const unsigned char *hash);
static const float minimum_pdiff = max(FLT_MIN, 1./0x100000000);
extern void set_target_to_pdiff(void *dest_target, double pdiff);
#define bdiff_to_pdiff(n) (n * 1.0000152587)
extern void set_target_to_bdiff(void *dest_target, double bdiff);
#define pdiff_to_bdiff(n) (n * 0.9999847412109375)
extern double target_diff(const unsigned char *target);

extern void kill_work(void);
extern void app_restart(void);

extern void __thr_being_msg(int prio, struct thr_info *, const char *);
extern void mt_enable(struct thr_info *thr);
extern void proc_enable(struct cgpu_info *);
extern void reinit_device(struct cgpu_info *cgpu);

extern void cgpu_set_defaults(struct cgpu_info *);
extern void drv_set_defaults(const struct device_drv *, const void *, void *userp, const char *devpath, const char *serial, int mode);

#ifdef HAVE_ADL
extern bool gpu_stats(int gpu, float *temp, int *engineclock, int *memclock, float *vddc, int *activity, int *fanspeed, int *fanpercent, int *powertune);
extern int set_fanspeed(int gpu, int iFanSpeed);
extern int set_vddc(int gpu, float fVddc);
extern int set_engineclock(int gpu, int iEngineClock);
extern int set_memoryclock(int gpu, int iMemoryClock);
#endif

extern void api(int thr_id);

extern struct pool *current_pool(void);
extern int enabled_pools;
extern bool get_intrange(const char *arg, int *val1, int *val2);
extern bool detect_stratum(struct pool *pool, char *url);
extern void print_summary(void);
extern struct mining_algorithm *mining_algorithm_by_alias(const char *alias);
extern struct mining_goal_info *get_mining_goal(const char *name);
extern void goal_set_malgo(struct mining_goal_info *, struct mining_algorithm *);
extern void mining_goal_reset(struct mining_goal_info * const goal);
extern void adjust_quota_gcd(void);
extern struct pool *add_pool2(struct mining_goal_info *);
#define add_pool()  add_pool2(get_mining_goal("default"))
extern bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass);
extern int bfg_strategy_parse(const char *strategy);
extern bool bfg_strategy_change(int strategy, const char *param);

#define MAX_GPUDEVICES 16
#define MAX_DEVICES 4096

struct block_info {
	uint32_t block_id;
	uint8_t prevblkhash[0x20];
	unsigned block_seen_order;  // new_blocks when this block was first seen; was 'block_no'
	uint32_t height;
	time_t first_seen_time;
	
	UT_hash_handle hh;
};

struct blockchain_info {
	struct block_info *blocks;
	struct block_info *currentblk;
	uint64_t currentblk_subsidy;  // only valid when height is known! (and assumes Bitcoin)
	char currentblk_first_seen_time_str[0x20];  // was global blocktime
};

struct _clState;
struct cgpu_info;
struct mining_algorithm;

struct mining_algorithm {
	const char *name;
	const char *aliases;
	
	enum pow_algorithm algo;
	uint8_t ui_skip_hash_bytes;
	uint8_t worktime_skip_prevblk_u32;
	float reasonable_low_nonce_diff;
	
	void (*hash_data_f)(void *digest, const void *data);
	
	int goal_refs;
	int staged;
	int base_queue;
	
	struct mining_algorithm *next;
	
#ifdef USE_OPENCL
	bool opencl_nodefault;
	float (*opencl_oclthreads_to_intensity)(unsigned long oclthreads);
	unsigned long (*opencl_intensity_to_oclthreads)(float intensity);
	unsigned long opencl_min_oclthreads;
	unsigned long opencl_max_oclthreads;
	float opencl_min_nonce_diff;
	char *(*opencl_get_default_kernel_file)(const struct mining_algorithm *, struct cgpu_info *, struct _clState *);
#endif
};

struct mining_goal_info {
	unsigned id;
	char *name;
	bool is_default;
	
	struct blockchain_info *blkchain;
	
	bytes_t *generation_script;  // was opt_coinbase_script
	
	struct mining_algorithm *malgo;
	double current_diff;
	char current_diff_str[ALLOC_H2B_SHORTV];  // was global block_diff
	char net_hashrate[ALLOC_H2B_SHORT];
	
	char *current_goal_detail;
	
	double diff_accepted;
	
	bool have_longpoll;
	
	UT_hash_handle hh;
};

extern struct string_elist *scan_devices;
extern bool opt_force_dev_init;
extern int nDevs;
extern int opt_n_threads;
extern int num_processors;
extern int hw_errors;
extern bool use_syslog;
extern bool opt_quiet;
extern struct thr_info *control_thr;
extern struct thr_info **mining_thr;
extern struct cgpu_info gpus[MAX_GPUDEVICES];
extern double total_secs;
extern int mining_threads;
extern struct cgpu_info *cpus;
extern int total_devices;
extern struct cgpu_info **devices;
extern int total_devices_new;
extern struct cgpu_info **devices_new;
extern int total_pools;
extern struct pool **pools;
extern const char *algo_names[];
extern enum sha256_algos opt_algo;
extern struct strategies strategies[];
extern enum pool_strategy pool_strategy;
extern int opt_rotate_period;
extern double total_rolling;
extern double total_mhashes_done;
extern unsigned int new_blocks;
extern unsigned int found_blocks;
extern int total_accepted, total_rejected;
extern int total_getworks, total_stale, total_discarded;
extern uint64_t total_bytes_rcvd, total_bytes_sent;
#define total_bytes_xfer (total_bytes_rcvd + total_bytes_sent)
extern double total_diff1, total_bad_diff1;
extern double total_diff_accepted, total_diff_rejected, total_diff_stale;
extern unsigned int local_work;
extern unsigned int total_go, total_ro;
extern const int opt_cutofftemp;
extern int opt_hysteresis;
extern int opt_fail_pause;
extern int opt_log_interval;
extern unsigned long long global_hashrate;
extern unsigned unittest_failures;
extern double best_diff;
extern struct mining_algorithm *mining_algorithms;
extern struct mining_goal_info *mining_goals;

struct curl_ent {
	CURL *curl;
	struct curl_ent *next;
	struct timeval tv;
};

/* Disabled needs to be the lowest enum as a freshly calloced value will then
 * equal disabled */
enum pool_enable {
	POOL_DISABLED,
	POOL_ENABLED,
	POOL_REJECTING,
	POOL_MISBEHAVING,
};

enum pool_protocol {
	PLP_NONE,
	PLP_GETWORK,
	PLP_GETBLOCKTEMPLATE,
};

struct bfg_tmpl_ref {
	blktemplate_t *tmpl;
	int refcount;
	pthread_mutex_t mutex;
};

struct ntime_roll_limits {
	uint32_t min;
	uint32_t max;
	struct timeval tv_ref;
	int16_t minoff;
	int16_t maxoff;
};

struct stratum_work {
	// Used only as a session id for resuming
	char *nonce1;
	
	struct bfg_tmpl_ref *tr;
	char *job_id;
	bool clean;
	
	bytes_t coinbase;
	size_t nonce2_offset;
	int n2size;
	
	int merkles;
	bytes_t merkle_bin;
	
	uint8_t header1[36];
	uint8_t diffbits[4];
	
	uint32_t ntime;
	struct timeval tv_received;
	struct ntime_roll_limits ntime_roll_limits;
	
	struct timeval tv_expire;

	uint8_t target[32];

	bool transparency_probed;
	struct timeval tv_transparency;
	bool opaque;
	
	cglock_t *data_lock_p;
	
	struct pool *pool;
	unsigned char work_restart_id;
};

#define RBUFSIZE 8192
#define RECVSIZE (RBUFSIZE - 4)

/*
 * Build an hash table in case there are lots
 * of addresses to check against
 */
struct bytes_hashtbl {
	bytes_t b;
	UT_hash_handle hh;
};

struct coinbase_param {
	bool testnet;
	struct bytes_hashtbl *scripts;
	int64_t total;
	float perc;
};

struct pool {
	int pool_no;
	int prio;
	int accepted, rejected;
	int seq_rejects;
	int seq_getfails;
	int solved;
	double diff1;
	char diff[ALLOC_H2B_SHORTV];
	int quota;
	int quota_gcd;
	int quota_used;
	int works;

	double diff_accepted;
	double diff_rejected;
	double diff_stale;

	bool submit_fail;
	bool idle;
	bool lagging;
	bool probed;
	int force_rollntime;
	enum pool_enable enabled;
	bool failover_only;  // NOTE: Ignored by failover and loadbalance strategies (use priority and quota respectively)
	bool submit_old;
	bool removed;
	bool lp_started;
	unsigned char	work_restart_id;
	time_t work_restart_time;
	char work_restart_timestamp[11];
	uint32_t	block_id;
	struct mining_goal_info *goal;
	enum bfg_tristate pool_diff_effective_retroactively;

	enum pool_protocol proto;

	char *hdr_path;
	char *lp_url;
	char *lp_id;
	enum pool_protocol lp_proto;
	curl_socket_t lp_socket;
	bool lp_active;

	unsigned int getwork_requested;
	unsigned int stale_shares;
	unsigned int discarded_work;
	unsigned int getfail_occasions;
	unsigned int remotefail_occasions;
	struct timeval tv_idle;

	double utility;
	int last_shares, shares;

	char *rpc_url;
	char *rpc_userpass;
	char *rpc_user, *rpc_pass;
	char *rpc_proxy;

	pthread_mutex_t pool_lock;
	pthread_mutex_t pool_test_lock;
	cglock_t data_lock;

	struct thread_q *submit_q;
	struct thread_q *getwork_q;

	pthread_t longpoll_thread;
	pthread_t test_thread;
	bool testing;

	int curls;
	pthread_cond_t cr_cond;
	struct curl_ent *curllist;
	struct submit_work_state *sws_waiting_on_curl;

	time_t last_work_time;
	struct timeval tv_last_work_time;
	time_t last_share_time;
	double last_share_diff;
	double best_diff;

	struct cgminer_stats cgminer_stats;
	struct cgminer_pool_stats cgminer_pool_stats;

	/* Stratum variables */
	char *stratum_url;
	char *stratum_port;
	CURL *stratum_curl;
	char curl_err_str[CURL_ERROR_SIZE];
	SOCKETTYPE sock;
	char *sockbuf;
	size_t sockbuf_size;
	char *sockaddr_url; /* stripped url used for sockaddr */
	size_t n1_len;
	uint64_t nonce2;
	int nonce2sz;
#ifdef WORDS_BIGENDIAN
	int nonce2off;
#endif
	char *sessionid;
	bool has_stratum;
	bool stratum_active;
	bool stratum_init;
	bool stratum_notify;
	struct stratum_work swork;
	char *goalname;
	char *next_goalname;
	struct mining_algorithm *next_goal_malgo;
	uint8_t next_target[0x20];
	char *next_nonce1;
	int next_n2size;
	pthread_t stratum_thread;
	pthread_mutex_t stratum_lock;
	char *admin_msg;

	/* param for coinbase check */
	struct coinbase_param cb_param;
	
	pthread_mutex_t last_work_lock;
	struct work *last_work_copy;
};

#define GETWORK_MODE_TESTPOOL 'T'
#define GETWORK_MODE_POOL 'P'
#define GETWORK_MODE_LP 'L'
#define GETWORK_MODE_BENCHMARK 'B'
#define GETWORK_MODE_STRATUM 'S'
#define GETWORK_MODE_GBT 'G'

typedef unsigned work_device_id_t;
#define PRIwdi "04x"

struct work {
	unsigned char	data[128];
	unsigned char	midstate[32];
	unsigned char	target[32];
	unsigned char	hash[32];

	double share_diff;

	int		rolls;
	struct ntime_roll_limits ntime_roll_limits;

	struct {
		uint32_t nonce;
	} blk;

	struct thr_info	*thr;
	int		thr_id;
	struct pool	*pool;
	struct timeval	tv_staged;

	bool		mined;
	bool		clone;
	bool		cloned;
	int		rolltime;
	bool		longpoll;
	bool		stale;
	bool		mandatory;
	bool spare;
	bool		block;

	bool		stratum;
	char 		*job_id;
	bytes_t		nonce2;
	char		*nonce1;

	unsigned char	work_restart_id;
	int		id;
	work_device_id_t device_id;
	UT_hash_handle hh;
	
	// Please don't use this if it's at all possible, I'd like to get rid of it eventually.
	void *device_data;
	void *(*device_data_dup_func)(struct work *);
	void (*device_data_free_func)(struct work *);
	
	double		work_difficulty;
	float		nonce_diff;

	// Allow devices to identify work if multiple sub-devices
	// DEPRECATED: New code should be using multiple processors instead
	int		subid;
	
	// Allow devices to timestamp work for their own purposes
	struct timeval	tv_stamp;

	struct bfg_tmpl_ref *tr;
	unsigned int	dataid;
	bool		do_foreign_submit;

	struct timeval	tv_getwork;
	time_t		ts_getwork;
	struct timeval	tv_getwork_reply;
	struct timeval	tv_cloned;
	struct timeval	tv_work_start;
	struct timeval	tv_work_found;
	char		getwork_mode;

	/* Used to queue shares in submit_waiting */
	struct work *prev;
	struct work *next;
};

extern void get_datestamp(char *, size_t, time_t);
#define get_now_datestamp(buf, bufsz)  get_datestamp(buf, bufsz, INVALID_TIMESTAMP)
extern void get_benchmark_work(struct work *, bool use_swork);
extern void stratum_work_cpy(struct stratum_work *dst, const struct stratum_work *src);
extern void stratum_work_clean(struct stratum_work *);
extern bool pool_has_usable_swork(const struct pool *);
extern void gen_stratum_work2(struct work *, struct stratum_work *);
extern void gen_stratum_work3(struct work *, struct stratum_work *, cglock_t *data_lock_p);
extern void inc_hw_errors3(struct thr_info *thr, const struct work *work, const uint32_t *bad_nonce_p, float nonce_diff);
static inline
void inc_hw_errors2(struct thr_info * const thr, const struct work * const work, const uint32_t *bad_nonce_p)
{
	inc_hw_errors3(thr, work, bad_nonce_p, work ? work->nonce_diff : 1.);
}
#define UNKNOWN_NONCE ((uint32_t*)inc_hw_errors2)
static inline
void inc_hw_errors(struct thr_info * const thr, const struct work * const work, const uint32_t bad_nonce)
{
	inc_hw_errors2(thr, work, work ? &bad_nonce : NULL);
}
#define inc_hw_errors_only(thr)  inc_hw_errors(thr, NULL, 0)
enum test_nonce2_result {
	TNR_GOOD = 1,
	TNR_HIGH = 0,
	TNR_BAD = -1,
};
extern enum test_nonce2_result _test_nonce2(struct work *, uint32_t nonce, bool checktarget);
#define test_nonce(work, nonce, checktarget)  (_test_nonce2(work, nonce, checktarget) == TNR_GOOD)
#define test_nonce2(work, nonce)  (_test_nonce2(work, nonce, true))
extern bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce);
extern bool submit_noffset_nonce(struct thr_info *thr, struct work *work, uint32_t nonce,
			  int noffset);
extern void __add_queued(struct cgpu_info *cgpu, struct work *work);
extern struct work *get_queued(struct cgpu_info *cgpu);
extern void add_queued(struct cgpu_info *cgpu, struct work *work);
extern struct work *get_queue_work(struct thr_info *thr, struct cgpu_info *cgpu, int thr_id);
extern struct work *__find_work_bymidstate(struct work *que, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern struct work *find_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern struct work *clone_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern void __work_completed(struct cgpu_info *cgpu, struct work *work);
extern int age_queued_work(struct cgpu_info *cgpu, double secs);
extern void work_completed(struct cgpu_info *cgpu, struct work *work);
extern struct work *take_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen);
extern void flush_queue(struct cgpu_info *cgpu);
extern bool abandon_work(struct work *, struct timeval *work_runtime, uint64_t hashes);
extern void hash_queued_work(struct thr_info *mythr);
extern void get_statline3(char *buf, size_t bufsz, struct cgpu_info *, bool for_curses, bool opt_show_procs);
extern void tailsprintf(char *buf, size_t bufsz, const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 3, 4);
extern void _wlog(const char *str);
extern void _wlogprint(const char *str);
extern int curses_int(const char *query);
extern char *curses_input(const char *query);
extern bool drv_ready(struct cgpu_info *);
extern double stats_elapsed(struct cgminer_stats *);
#define cgpu_runtime(cgpu)  stats_elapsed(&((cgpu)->cgminer_stats))
extern double cgpu_utility(struct cgpu_info *);
extern void kill_work(void);
extern int prioritize_pools(char *param, int *pid);
extern void validate_pool_priorities(void);
extern void enable_pool(struct pool *);
extern void manual_enable_pool(struct pool *);
extern void disable_pool(struct pool *, enum pool_enable);
extern void switch_pools(struct pool *selected);
extern void remove_pool(struct pool *pool);
extern void write_config(FILE *fcfg);
extern void zero_bestshare(void);
extern void zero_stats(void);
extern void default_save_file(char *filename);
extern bool _log_curses_only(int prio, const char *datetime, const char *str);
extern void clear_logwin(void);
extern void logwin_update(void);
extern bool pool_tclear(struct pool *pool, bool *var);
extern bool pool_may_redirect_to(struct pool *, const char *uri);
extern void pool_check_coinbase(struct pool *, const uint8_t *cbtxn, size_t cbtxnsz);
extern struct thread_q *tq_new(void);
extern void tq_free(struct thread_q *tq);
extern bool tq_push(struct thread_q *tq, void *data);
extern void *tq_pop(struct thread_q *);
extern void tq_freeze(struct thread_q *tq);
extern void tq_thaw(struct thread_q *tq);
extern bool successful_connect;
extern void adl(void);
extern void tmpl_decref(struct bfg_tmpl_ref *);
extern void clean_work(struct work *work);
extern void free_work(struct work *work);
extern void __copy_work(struct work *work, const struct work *base_work);
extern struct work *copy_work_noffset(const struct work *base_work, int noffset);
#define copy_work(work_in) copy_work_noffset(work_in, 0)
extern double share_diff(const struct work *);
extern const char *bfg_workpadding_bin;
extern void set_simple_ntime_roll_limit(struct ntime_roll_limits *, uint32_t ntime_base, int ntime_roll, const struct timeval *tvp_ref);
extern void work_set_simple_ntime_roll_limit(struct work *, int ntime_roll, const struct timeval *tvp_ref);
extern int work_ntime_range(struct work *, const struct timeval *tvp_earliest, const struct timeval *tvp_latest, int desired_roll);

static inline
struct mining_algorithm *work_mining_algorithm(const struct work * const work)
{
	const struct pool * const pool = work->pool;
	const struct mining_goal_info * const goal = pool->goal;
	struct mining_algorithm * const malgo = goal->malgo;
	return malgo;
}

extern void work_hash(struct work *);

#define NTIME_DATA_OFFSET  0x44

static inline
uint32_t work_get_ntime(const struct work * const work)
{
	return upk_u32be(work->data, 0x44);
}

static inline
void work_set_ntime(struct work * const work, const uint32_t ntime)
{
	pk_u32be(work->data, 0x44, ntime);
}


extern char *devpath_to_devid(const char *);
extern struct thr_info *get_thread(int thr_id);
extern struct cgpu_info *get_devices(int id);
extern int create_new_cgpus(void (*addfunc)(void*), void *arg);
extern int scan_serial(const char *);
extern bool check_coinbase(const uint8_t *, size_t, const struct coinbase_param *cb_param);

enum api_data_type {
	API_ESCAPE,
	API_STRING,
	API_CONST,
	API_UINT8,
	API_INT16,
	API_UINT16,
	API_INT,
	API_UINT,
	API_UINT32,
	API_UINT64,
	API_DOUBLE,
	API_ELAPSED,
	API_BOOL,
	API_TIMEVAL,
	API_TIME,
	API_MHS,
	API_MHTOTAL,
	API_TEMP,
	API_UTILITY,
	API_FREQ,
	API_VOLTS,
	API_HS,
	API_DIFF,
	API_JSON,
	API_PERCENT
};

struct api_data {
	enum api_data_type type;
	char *name;
	const void *data;
	bool data_was_malloc;
	struct api_data *prev;
	struct api_data *next;
};

extern struct api_data *api_add_escape(struct api_data *root, const char *name, const char *data, bool copy_data);
extern struct api_data *api_add_string(struct api_data *root, const char *name, const char *data, bool copy_data);
extern struct api_data *api_add_const(struct api_data *root, const char *name, const char *data, bool copy_data);
extern struct api_data *api_add_uint8(struct api_data *root, const char *name, const uint8_t *data, bool copy_data);
extern struct api_data *api_add_int16(struct api_data *root, const char *name, const uint16_t *data, bool copy_data);
extern struct api_data *api_add_uint16(struct api_data *root, const char *name, const uint16_t *data, bool copy_data);
extern struct api_data *api_add_int(struct api_data *root, const char *name, const int *data, bool copy_data);
extern struct api_data *api_add_uint(struct api_data *root, const char *name, const unsigned int *data, bool copy_data);
extern struct api_data *api_add_uint32(struct api_data *root, const char *name, const uint32_t *data, bool copy_data);
extern struct api_data *api_add_uint64(struct api_data *root, const char *name, const uint64_t *data, bool copy_data);
extern struct api_data *api_add_double(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_elapsed(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_bool(struct api_data *root, const char *name, const bool *data, bool copy_data);
extern struct api_data *api_add_timeval(struct api_data *root, const char *name, const struct timeval *data, bool copy_data);
extern struct api_data *api_add_time(struct api_data *root, const char *name, const time_t *data, bool copy_data);
extern struct api_data *api_add_mhs(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_mhstotal(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_temp(struct api_data *root, const char *name, const float *data, bool copy_data);
extern struct api_data *api_add_utility(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_freq(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_volts(struct api_data *root, const char *name, const float *data, bool copy_data);
extern struct api_data *api_add_hs(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_diff(struct api_data *root, const char *name, const double *data, bool copy_data);
extern struct api_data *api_add_json(struct api_data *root, const char *name, json_t *data, bool copy_data);
extern struct api_data *api_add_percent(struct api_data *root, const char *name, const double *data, bool copy_data);

#endif /* __MINER_H__ */
