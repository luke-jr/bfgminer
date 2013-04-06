#ifndef __MINER_H__
#define __MINER_H__

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <jansson.h>
#include <curl/curl.h>

#include <blkmaker.h>
#include <blktemplate.h>

#if defined(WORDS_BIGENDIAN) && !defined(__BIG_ENDIAN__)
/* uthash.h depends on __BIG_ENDIAN__ on BE platforms */
#define __BIG_ENDIAN__ 1
#endif

#include "elist.h"
#include "uthash.h"
#include "logging.h"
#include "util.h"

#ifdef HAVE_OPENCL
#include "CL/cl.h"
#endif /* HAVE_OPENCL */

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
#elif defined __GNUC__
# ifndef WIN32
#  define alloca __builtin_alloca
# else
#  include <malloc.h>
# endif
#elif defined _AIX
# define alloca __alloca
#elif defined _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# ifndef HAVE_ALLOCA
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
#  define htobe32(x) bswap_32(x)
#  define htobe64(x) bswap_64(x)
# else
#  define htole16(x) bswap_16(x)
#  define htole32(x) bswap_32(x)
#  define htole64(x) bswap_64(x)
#  define htobe32(x) (x)
#  define htobe64(x) (x)
# endif
#endif
#ifndef be32toh
# define le32toh(x) htole32(x)
# define le64toh(x) htole64(x)
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

#ifndef roundl
#define roundl(x)   (long double)((long long)((x==0)?0.0:((x)+((x)>0)?0.5:-0.5)))
#endif

enum alive {
	LIFE_WELL,
	LIFE_SICK,
	LIFE_DEAD,
	LIFE_NOSTART,
	LIFE_INIT,
	LIFE_WAIT,
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

struct api_data;
struct thr_info;
struct work;

struct device_api {
	const char *dname;
	const char *name;

	// API-global functions
	void (*api_detect)();

	// Device-specific functions
	void (*get_dev_statline_before)(char *, struct cgpu_info *);
	void (*get_dev_statline_after)(char *, struct cgpu_info *);

	// Processor-specific functions
	void (*reinit_device)(struct cgpu_info *);
	void (*get_statline_before)(char *, struct cgpu_info *);
	void (*get_statline)(char *, struct cgpu_info *);
	struct api_data* (*get_api_extra_device_detail)(struct cgpu_info *);
	struct api_data* (*get_api_extra_device_status)(struct cgpu_info *);
	struct api_data *(*get_api_stats)(struct cgpu_info *);
	bool (*get_stats)(struct cgpu_info *);
	bool (*identify_device)(struct cgpu_info *);  // e.g. to flash a led
	char *(*set_device)(struct cgpu_info *, char *option, char *setting, char *replybuf);

	// Thread-specific functions
	bool (*thread_prepare)(struct thr_info *);
	void (*minerloop)(struct thr_info *);
	uint64_t (*can_limit_work)(struct thr_info *);
	bool (*thread_init)(struct thr_info *);
	bool (*prepare_work)(struct thr_info *, struct work *);
#ifdef USE_AVALON
	int64_t (*scanhash_queue)(struct thr_info *, struct work **, int64_t);
#endif
	int64_t (*scanhash)(struct thr_info *, struct work *, int64_t);
	void (*hw_error)(struct thr_info *);
	void (*thread_shutdown)(struct thr_info *);
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
	DEV_DISABLED,
	DEV_RECOVER,
	DEV_RECOVER_ERR,
};

enum cl_kernels {
	KL_NONE,
	KL_POCLBM,
	KL_PHATK,
	KL_DIAKGCN,
	KL_DIABLO,
	KL_SCRYPT,
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

struct cgpu_info {
	int cgminer_id;
	int device_line_id;
	const struct device_api *api;
	const char *devtype;
	int device_id;
	char *dev_repr;
	char *dev_repr_ns;
	const char *name;
	
	int procs;
	int proc_id;
	char proc_repr[8];
	char proc_repr_ns[8];
	struct cgpu_info *device;
	struct cgpu_info *next_proc;
	
	const char *device_path;
	FILE *device_file;
	union {
#ifdef USE_ZTEX
		struct libztex_device *device_ztex;
#endif
		int device_fd;
#ifdef USE_X6500
		struct ft232r_device_handle *device_ft232r;
#endif
	};
#ifdef USE_BITFORCE
	struct timeval work_start_tv;
	unsigned int wait_ms;
	unsigned int sleep_ms;
	double avg_wait_f;
	unsigned int avg_wait_d;
	uint32_t nonces;
	bool polling;
#endif
#if defined(USE_BITFORCE) || defined(USE_ICARUS)
	bool flash_led;
#endif
	void *cgpu_data;
	pthread_mutex_t		device_mutex;
	pthread_cond_t	device_cond;

	enum dev_enable deven;
	int accepted;
	int rejected;
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
#ifdef HAVE_OPENCL
	bool mapped;
	int virtual_gpu;
	int virtual_adl;
	int intensity;
	bool dynamic;

	cl_uint vwidth;
	size_t work_size;
	enum cl_kernels kernel;
	cl_ulong max_alloc;

#ifdef USE_SCRYPT
	int opt_lg, lookup_gap;
	size_t opt_tc, thread_concurrency;
	size_t shaders;
#endif
	struct timeval tv_gpustart;
	int intervals;
#endif

	float temp;
	int cutofftemp;
	int targettemp;

#ifdef HAVE_ADL
	bool has_adl;
	struct gpu_adl adl;

	int gpu_engine;
	int min_engine;
	int gpu_fan;
	int min_fan;
	int gpu_memclock;
	int gpu_memdiff;
	int gpu_powertune;
	float gpu_vddc;
#endif
	int diff1;
	double diff_accepted;
	double diff_rejected;
	int last_share_pool;
	time_t last_share_pool_time;
	double last_share_diff;

	time_t device_last_well;
	time_t device_last_not_well;
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
};

extern void renumber_cgpu(struct cgpu_info *);
extern bool add_cgpu(struct cgpu_info*);

struct thread_q {
	struct list_head	q;

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

	bool	pause;
	time_t	getwork;
	double	rolling;

	// Used by minerloop_async
	struct work *prev_work;
	struct work *work;
	struct work *next_work;
	enum thr_busy_state busy_state;
	struct timeval tv_morework;
	struct work *results_work;
	bool _job_transition_in_progress;
	bool _proceed_with_new_job;
	struct timeval tv_results_jobstart;
	struct timeval tv_jobstart;
	struct timeval tv_poll;
	notifier_t notifier;
	bool starting_next_work;
	uint32_t _max_nonce;
	notifier_t mutex_request;

	// Used by minerloop_queue
	struct list_head work_list;
	bool queue_full;
	bool _last_sbr_state;

	bool	work_restart;
	notifier_t work_restart_notifier;
};

extern int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg);
extern void thr_info_cancel(struct thr_info *thr);
extern void thr_info_freeze(struct thr_info *thr);
extern void nmsleep(unsigned int msecs);
extern double us_tdiff(struct timeval *end, struct timeval *start);
extern double tdiff(struct timeval *end, struct timeval *start);

struct string_elist {
	char *string;
	bool free_me;

	struct list_head list;
};

static inline void string_elist_add(const char *s, struct list_head *head)
{
	struct string_elist *n;

	n = calloc(1, sizeof(*n));
	n->string = strdup(s);
	n->free_me = true;
	list_add_tail(&n->list, head);
}

static inline void string_elist_del(struct string_elist *item)
{
	if (item->free_me)
		free(item->string);
	list_del(&item->list);
	free(item);
}


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
#  define swap32tobe(out, in, sz)  ((out == in) ? (void)0 : memmove(out, in, sz))
#  define LOCAL_swap32be(type, var, sz)  ;
#  define swap32tole(out, in, sz)  swap32yes(out, in, sz)
#  define LOCAL_swap32le(type, var, sz)  LOCAL_swap32(type, var, sz)
#else
#  define swap32tobe(out, in, sz)  swap32yes(out, in, sz)
#  define LOCAL_swap32be(type, var, sz)  LOCAL_swap32(type, var, sz)
#  define swap32tole(out, in, sz)  ((out == in) ? (void)0 : memmove(out, in, sz))
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

#define flip32(dest_p, src_p) swap32yes(dest_p, src_p, 32 / 4)

extern void quit(int status, const char *format, ...);

static inline void mutex_lock(pthread_mutex_t *lock)
{
	if (unlikely(pthread_mutex_lock(lock)))
		quit(1, "WTF MUTEX ERROR ON LOCK!");
}

static inline void mutex_unlock(pthread_mutex_t *lock)
{
	if (unlikely(pthread_mutex_unlock(lock)))
		quit(1, "WTF MUTEX ERROR ON UNLOCK!");
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

static inline void rd_unlock(pthread_rwlock_t *lock)
{
	rw_unlock(lock);
}

static inline void wr_unlock(pthread_rwlock_t *lock)
{
	rw_unlock(lock);
}

static inline void mutex_init(pthread_mutex_t *lock)
{
	if (unlikely(pthread_mutex_init(lock, NULL)))
		quit(1, "Failed to pthread_mutex_init");
}

static inline void rwlock_init(pthread_rwlock_t *lock)
{
	if (unlikely(pthread_rwlock_init(lock, NULL)))
		quit(1, "Failed to pthread_rwlock_init");
}

struct pool;

extern bool opt_protocol;
extern char *opt_coinbase_sig;
extern bool have_longpoll;
extern int opt_skip_checks;
extern char *opt_kernel_path;
extern char *opt_socks_proxy;
extern char *cgminer_path;
extern bool opt_fail_only;
extern bool opt_autofan;
extern bool opt_autoengine;
extern bool use_curses;
extern char *opt_api_allow;
extern char *opt_api_groups;
extern char *opt_api_description;
extern int opt_api_port;
extern bool opt_api_listen;
extern bool opt_api_network;
extern bool opt_delaynet;
extern bool opt_restart;
extern char *opt_icarus_options;
extern char *opt_icarus_timing;
extern bool opt_worktime;
extern char *opt_avalon_options;
#ifdef USE_BITFORCE
extern bool opt_bfl_noncerange;
#endif
extern int swork_id;

extern pthread_rwlock_t netacc_lock;

extern const uint32_t sha256_init_state[];
extern json_t *json_rpc_call(CURL *curl, const char *url, const char *userpass,
			     const char *rpc_req, bool, bool, int *,
			     struct pool *pool, bool);
extern bool our_curl_supports_proxy_uris();
extern char *bin2hex(const unsigned char *p, size_t len);
extern bool hex2bin(unsigned char *p, const char *hexstr, size_t len);

typedef bool (*sha256_func)(struct thr_info*, const unsigned char *pmidstate,
	unsigned char *pdata,
	unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce,
	uint32_t *last_nonce,
	uint32_t nonce);

extern bool fulltest(const unsigned char *hash, const unsigned char *target);

extern int opt_queue;
extern int opt_scantime;
extern int opt_expiry;

extern pthread_mutex_t hash_lock;
extern pthread_mutex_t console_lock;
extern pthread_mutex_t ch_lock;

extern void thread_reportin(struct thr_info *thr);
extern void thread_reportout(struct thr_info *);
extern void clear_stratum_shares(struct pool *pool);
extern void hashmeter2(struct thr_info *);
extern bool stale_work(struct work *, bool share);
extern bool stale_work_future(struct work *, bool share, unsigned long ustime);

extern void kill_work(void);
extern void app_restart(void);

extern void mt_enable(struct thr_info *thr);
extern void proc_enable(struct cgpu_info *);
extern void reinit_device(struct cgpu_info *cgpu);

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
extern bool detect_stratum(struct pool *pool, char *url);
extern void print_summary(void);
extern struct pool *add_pool(void);
extern void add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass);

#define MAX_GPUDEVICES 16

#define MIN_INTENSITY -10
#define _MIN_INTENSITY_STR "-10"
#ifdef USE_SCRYPT
#define MAX_INTENSITY 20
#define _MAX_INTENSITY_STR "20"
#else
#define MAX_INTENSITY 14
#define _MAX_INTENSITY_STR "14"
#endif

extern struct list_head scan_devices;
extern bool opt_force_dev_init;
extern int nDevs;
extern int opt_n_threads;
extern int num_processors;
extern int hw_errors;
extern bool use_syslog;
extern bool opt_quiet;
extern struct thr_info *thr_info;
extern struct cgpu_info gpus[MAX_GPUDEVICES];
extern int gpu_threads;
#ifdef USE_SCRYPT
extern bool opt_scrypt;
#else
#define opt_scrypt (0)
#endif
extern double total_secs;
extern int mining_threads;
extern struct cgpu_info *cpus;
extern int total_devices;
extern struct cgpu_info **devices;
extern int total_pools;
extern struct pool **pools;
extern const char *algo_names[];
extern enum sha256_algos opt_algo;
extern struct strategies strategies[];
extern enum pool_strategy pool_strategy;
extern int opt_rotate_period;
extern double total_mhashes_done;
extern unsigned int new_blocks;
extern unsigned int found_blocks;
extern int total_accepted, total_rejected, total_diff1;;
extern int total_getworks, total_stale, total_discarded;
extern uint64_t total_bytes_xfer;
extern double total_diff_accepted, total_diff_rejected, total_diff_stale;
extern unsigned int local_work;
extern unsigned int total_go, total_ro;
extern const int opt_cutofftemp;
extern int opt_hysteresis;
extern int opt_fail_pause;
extern int opt_log_interval;
extern unsigned long long global_hashrate;
extern char *current_fullhash;
extern double current_diff;
extern uint64_t best_diff;
extern struct timeval block_timeval;

#ifdef HAVE_OPENCL
typedef struct {
	cl_uint ctx_a; cl_uint ctx_b; cl_uint ctx_c; cl_uint ctx_d;
	cl_uint ctx_e; cl_uint ctx_f; cl_uint ctx_g; cl_uint ctx_h;
	cl_uint cty_a; cl_uint cty_b; cl_uint cty_c; cl_uint cty_d;
	cl_uint cty_e; cl_uint cty_f; cl_uint cty_g; cl_uint cty_h;
	cl_uint merkle; cl_uint ntime; cl_uint nbits; cl_uint nonce;
	cl_uint fW0; cl_uint fW1; cl_uint fW2; cl_uint fW3; cl_uint fW15;
	cl_uint fW01r; cl_uint fcty_e; cl_uint fcty_e2;
	cl_uint W16; cl_uint W17; cl_uint W2;
	cl_uint PreVal4; cl_uint T1;
	cl_uint C1addK5; cl_uint D1A; cl_uint W2A; cl_uint W17_2;
	cl_uint PreVal4addT1; cl_uint T1substate0;
	cl_uint PreVal4_2;
	cl_uint PreVal0;
	cl_uint PreW18;
	cl_uint PreW19;
	cl_uint PreW31;
	cl_uint PreW32;

	/* For diakgcn */
	cl_uint B1addK6, PreVal0addK7, W16addK16, W17addK17;
	cl_uint zeroA, zeroB;
	cl_uint oneA, twoA, threeA, fourA, fiveA, sixA, sevenA;
#ifdef USE_SCRYPT
	struct work *work;
#endif
} dev_blk_ctx;
#else
typedef struct {
	uint32_t nonce;
} dev_blk_ctx;
#endif

struct curl_ent {
	CURL *curl;
	struct list_head node;
	struct timeval tv;
};

/* Disabled needs to be the lowest enum as a freshly calloced value will then
 * equal disabled */
enum pool_enable {
	POOL_DISABLED,
	POOL_ENABLED,
	POOL_REJECTING,
};

enum pool_protocol {
	PLP_NONE,
	PLP_GETWORK,
	PLP_GETBLOCKTEMPLATE,
};

struct stratum_work {
	char *job_id;
	char *prev_hash;
	char *coinbase1;
	char *coinbase2;
	char **merkle;
	char *bbversion;
	char *nbit;
	char *ntime;
	bool clean;

	size_t cb1_len;
	size_t cb2_len;
	size_t cb_len;

	size_t header_len;
	int merkles;
	double diff;

	bool transparency_probed;
	time_t transparency_time;
	bool opaque;
};

#define RBUFSIZE 8192
#define RECVSIZE (RBUFSIZE - 4)

struct pool {
	int pool_no;
	int prio;
	int accepted, rejected;
	int seq_rejects;
	int seq_getfails;
	int solved;
	int diff1;
	char diff[8];

	double diff_accepted;
	double diff_rejected;
	double diff_stale;

	bool submit_fail;
	bool idle;
	bool lagging;
	bool probed;
	int force_rollntime;
	enum pool_enable enabled;
	bool submit_old;
	bool removed;
	bool lp_started;
	unsigned char	work_restart_id;
	uint32_t	block_id;

	enum pool_protocol proto;

	char *hdr_path;
	char *lp_url;
	char *lp_id;
	enum pool_protocol lp_proto;
	curl_socket_t lp_socket;

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

	struct thread_q *submit_q;
	struct thread_q *getwork_q;

	pthread_t longpoll_thread;
	pthread_t submit_thread;
	pthread_t getwork_thread;

	int curls;
	pthread_cond_t cr_cond;
	struct list_head curlring;
	struct submit_work_state *sws_waiting_on_curl;

	time_t last_share_time;
	double last_share_diff;
	uint64_t best_diff;

	struct cgminer_stats cgminer_stats;
	struct cgminer_pool_stats cgminer_pool_stats;

	/* Stratum variables */
	char *stratum_url;
	char *stratum_port;
	CURL *stratum_curl;
	SOCKETTYPE sock;
	char *sockbuf;
	size_t sockbuf_size;
	char *sockaddr_url; /* stripped url used for sockaddr */
	char *nonce1;
	size_t n1_len;
	uint32_t nonce2;
	int n2size;
	char *sessionid;
	bool has_stratum;
	bool stratum_active;
	time_t last_work_time;  /* only set for Stratum right now */
	bool stratum_auth;
	bool stratum_notify;
	struct stratum_work swork;
	pthread_t stratum_thread;
	pthread_mutex_t stratum_lock;
	char *admin_msg;

	pthread_mutex_t last_work_lock;
	struct work *last_work_copy;
};

#define GETWORK_MODE_TESTPOOL 'T'
#define GETWORK_MODE_POOL 'P'
#define GETWORK_MODE_LP 'L'
#define GETWORK_MODE_BENCHMARK 'B'
#define GETWORK_MODE_STRATUM 'S'
#define GETWORK_MODE_GBT 'G'

struct work {
	unsigned char	data[128];
	unsigned char	midstate[32];
	unsigned char	target[32];
	unsigned char	hash[32];

	int		rolls;

	dev_blk_ctx	blk;

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
	bool		block;
	bool		queued;

	bool		stratum;
	char 		*job_id;
	char		*nonce2;
	char		*ntime;
	double		sdiff;
	char		*nonce1;

	unsigned char	work_restart_id;
	int		id;
	UT_hash_handle hh;
	
	double		work_difficulty;

	blktemplate_t	*tmpl;
	int		*tmpl_refcount;
	unsigned int	dataid;
	bool		do_foreign_submit;

	struct timeval	tv_getwork;
	struct timeval	tv_getwork_reply;
	struct timeval	tv_cloned;
	struct timeval	tv_work_start;
	struct timeval	tv_work_found;
	char		getwork_mode;

	/* Used to queue shares in submit_waiting */
	struct list_head list;
};

extern void get_datestamp(char *, struct timeval *);
enum test_nonce2_result {
	TNR_GOOD,
	TNR_HIGH,
	TNR_BAD,
};
extern enum test_nonce2_result _test_nonce2(struct work *, uint32_t nonce, bool checktarget);
#define test_nonce(work, nonce, checktarget)  (_test_nonce2(work, nonce, checktarget) == TNR_GOOD)
#define test_nonce2(work, nonce)  (_test_nonce2(work, nonce, true))
extern void submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce);
extern bool abandon_work(struct work *, struct timeval *work_runtime, uint64_t hashes);
extern void tailsprintf(char *f, const char *fmt, ...);
extern void wlogprint(const char *f, ...);
extern int curses_int(const char *query);
extern char *curses_input(const char *query);
extern void kill_work(void);
extern int prioritize_pools(char *param, int *pid);
extern void validate_pool_priorities(void);
extern void switch_pools(struct pool *selected);
extern void remove_pool(struct pool *pool);
extern void write_config(FILE *fcfg);
extern void zero_bestshare(void);
extern void zero_stats(void);
extern void default_save_file(char *filename);
extern bool log_curses_only(int prio, const char *f, va_list ap) FORMAT_SYNTAX_CHECK(printf, 2, 0);
extern void clear_logwin(void);
extern bool pool_tclear(struct pool *pool, bool *var);
extern struct thread_q *tq_new(void);
extern void tq_free(struct thread_q *tq);
extern bool tq_push(struct thread_q *tq, void *data);
extern void *tq_pop(struct thread_q *tq, const struct timespec *abstime);
extern void tq_freeze(struct thread_q *tq);
extern void tq_thaw(struct thread_q *tq);
extern bool successful_connect;
extern void adl(void);
extern void clean_work(struct work *work);
extern void free_work(struct work *work);
extern void __copy_work(struct work *work, const struct work *base_work);
extern struct work *copy_work(const struct work *base_work);

enum api_data_type {
	API_ESCAPE,
	API_STRING,
	API_CONST,
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
};

struct api_data {
	enum api_data_type type;
	char *name;
	void *data;
	bool data_was_malloc;
	struct api_data *prev;
	struct api_data *next;
};

extern struct api_data *api_add_escape(struct api_data *root, char *name, char *data, bool copy_data);
extern struct api_data *api_add_string(struct api_data *root, char *name, const char *data, bool copy_data);
extern struct api_data *api_add_const(struct api_data *root, char *name, const char *data, bool copy_data);
extern struct api_data *api_add_int(struct api_data *root, char *name, int *data, bool copy_data);
extern struct api_data *api_add_uint(struct api_data *root, char *name, unsigned int *data, bool copy_data);
extern struct api_data *api_add_uint32(struct api_data *root, char *name, uint32_t *data, bool copy_data);
extern struct api_data *api_add_uint64(struct api_data *root, char *name, uint64_t *data, bool copy_data);
extern struct api_data *api_add_double(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_elapsed(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_bool(struct api_data *root, char *name, bool *data, bool copy_data);
extern struct api_data *api_add_timeval(struct api_data *root, char *name, struct timeval *data, bool copy_data);
extern struct api_data *api_add_time(struct api_data *root, char *name, time_t *data, bool copy_data);
extern struct api_data *api_add_mhs(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_mhstotal(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_temp(struct api_data *root, char *name, float *data, bool copy_data);
extern struct api_data *api_add_utility(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_freq(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_volts(struct api_data *root, char *name, float *data, bool copy_data);
extern struct api_data *api_add_hs(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_diff(struct api_data *root, char *name, double *data, bool copy_data);
extern struct api_data *api_add_json(struct api_data *root, char *name, json_t *data, bool copy_data);

#endif /* __MINER_H__ */
