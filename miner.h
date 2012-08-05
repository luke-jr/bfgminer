#ifndef __MINER_H__
#define __MINER_H__

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <jansson.h>
#include <curl/curl.h>
#include "elist.h"
#include "uthash.h"
#include "logging.h"

#ifdef HAVE_OPENCL
#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
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

#if defined (__linux)
 #ifndef LINUX
  #define LINUX
 #endif
#endif

#ifdef WIN32
  #ifndef timersub
    #define timersub(a, b, result)                     \
    do {                                               \
      (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
      (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
      if ((result)->tv_usec < 0) {                     \
        --(result)->tv_sec;                            \
        (result)->tv_usec += 1000000;                  \
      }                                                \
    } while (0)
  #endif
 #ifndef timeradd
 # define timeradd(a, b, result)			      \
   do {							      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;	      \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;	      \
    if ((result)->tv_usec >= 1000000)			      \
      {							      \
	++(result)->tv_sec;				      \
	(result)->tv_usec -= 1000000;			      \
      }							      \
   } while (0)
 #endif
#endif


#ifdef HAVE_ADL
 #include "ADL_SDK/adl_sdk.h"
#endif

#ifdef HAVE_LIBUSB
  #include <libusb-1.0/libusb.h>
#endif

#ifdef USE_ZTEX
  #include "libztex.h"
#endif

#if !defined(WIN32) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#define bswap_16 __builtin_bswap16
#define bswap_32 __builtin_bswap32
#define bswap_64 __builtin_bswap64
#else
#if HAVE_BYTESWAP_H
#include <byteswap.h>
#elif defined(USE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#else
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
#endif /* !defined(__GLXBYTEORDER_H__) */

/* This assumes htobe32 is a macro in endian.h */
#ifndef htobe32
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define be32toh(x) bswap_32(x)
#  define htobe32(x) bswap_32(x)
# elif __BYTE_ORDER == __BIG_ENDIAN
#  define be32toh(x) (x)
#  define htobe32(x) (x)
#else
#error UNKNOWN BYTE ORDER
#endif
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
#define __maybe_unused		__attribute__((unused))

#define uninitialised_var(x) x = x

#if defined(__i386__)
#define WANT_CRYPTOPP_ASM32
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

enum alive {
	LIFE_WELL,
	LIFE_SICK,
	LIFE_DEAD,
	LIFE_NOSTART,
	LIFE_INIT,
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
	int targettemp;
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
	char*dname;
	char*name;

	// API-global functions
	void (*api_detect)();

	// Device-specific functions
	void (*reinit_device)(struct cgpu_info*);
	void (*get_statline_before)(char*, struct cgpu_info*);
	void (*get_statline)(char*, struct cgpu_info*);
	struct api_data *(*get_api_stats)(struct cgpu_info*);
	bool (*get_stats)(struct cgpu_info*);

	// Thread-specific functions
	bool (*thread_prepare)(struct thr_info*);
	uint64_t (*can_limit_work)(struct thr_info*);
	bool (*thread_init)(struct thr_info*);
	void (*free_work)(struct thr_info*, struct work*);
	bool (*prepare_work)(struct thr_info*, struct work*);
	int64_t (*scanhash)(struct thr_info*, struct work*, int64_t);
	void (*thread_shutdown)(struct thr_info*);
	void (*thread_enable)(struct thr_info*);
};

enum dev_enable {
	DEV_ENABLED,
	DEV_DISABLED,
	DEV_RECOVER,
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
#define REASON_UNKNOWN_STR		"Unknown reason - code bug"

#define MIN_SEC_UNSET 99999999

struct cgminer_stats {
	uint32_t getwork_calls;
	struct timeval getwork_wait;
	struct timeval getwork_wait_max;
	struct timeval getwork_wait_min;
};

// Just the actual network getworks to the pool
struct cgminer_pool_stats {
	uint32_t getwork_calls;
	uint32_t getwork_attempts;
	struct timeval getwork_wait;
	struct timeval getwork_wait_max;
	struct timeval getwork_wait_min;
	double getwork_wait_rolling;
};

struct cgpu_info {
	int cgminer_id;
	struct device_api *api;
	int device_id;
	char *name;
	char *device_path;
	FILE *device_file;
	union {
#ifdef USE_ZTEX
		struct libztex_device *device_ztex;
#endif
		int device_fd;
	};
#ifdef USE_BITFORCE
	struct timeval work_start_tv;
	unsigned int wait_ms;
	unsigned int sleep_ms;
	double avg_wait_f;
	unsigned int avg_wait_d;
	uint32_t nonces;
	bool nonce_range;
#endif
	pthread_mutex_t		device_mutex;

	enum dev_enable deven;
	int accepted;
	int rejected;
	int hw_errors;
	double rolling;
	double total_mhashes;
	double utility;
	enum alive status;
	char init[40];
	struct timeval last_message_tv;

	int threads;
	struct thr_info **thr;

	unsigned int max_hashes;

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
	int opt_tc, thread_concurrency;
	int shaders;
#endif
	struct timeval tv_gpustart;;
	struct timeval tv_gpuend;
	double gpu_us_average;
#endif

	float temp;
	int cutofftemp;

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
	int last_share_pool;
	time_t last_share_pool_time;

	time_t device_last_well;
	time_t device_last_not_well;
	enum dev_reason device_not_well_reason;
	int thread_fail_init_count;
	int thread_zero_hash_count;
	int thread_fail_queue_count;
	int dev_sick_idle_60_count;
	int dev_dead_idle_600_count;
	int dev_nostart_count;
	int dev_over_heat_count;	// It's a warning but worth knowing
	int dev_thermal_cutoff_count;
	int dev_comms_error_count;

	struct cgminer_stats cgminer_stats;
};

extern bool add_cgpu(struct cgpu_info*);

struct thread_q {
	struct list_head	q;

	bool frozen;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
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

	bool	pause;
	bool	getwork;
	double	rolling;

	bool	work_restart;
};

extern int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg);
extern void thr_info_cancel(struct thr_info *thr);
extern void thr_info_freeze(struct thr_info *thr);
extern void nmsleep(unsigned int msecs);

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
extern char *opt_kernel_path;
extern char *opt_socks_proxy;
extern char *cgminer_path;
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
#ifdef USE_BITFORCE
extern bool opt_bfl_noncerange;
#endif

extern pthread_rwlock_t netacc_lock;

extern const uint32_t sha256_init_state[];
extern json_t *json_rpc_call(CURL *curl, const char *url, const char *userpass,
			     const char *rpc_req, bool, bool, int *,
			     struct pool *pool, bool);
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

extern int opt_scantime;

extern pthread_mutex_t console_lock;

extern pthread_mutex_t restart_lock;
extern pthread_cond_t restart_cond;

extern void thread_reportin(struct thr_info *thr);
extern bool queue_request(struct thr_info *thr, bool needed);
extern int restart_wait(unsigned int mstime);

extern void kill_work(void);

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
extern void add_pool_details(bool live, char *url, char *user, char *pass);

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
extern int total_accepted, total_rejected;
extern int total_getworks, total_stale, total_discarded;
extern unsigned int local_work;
extern unsigned int total_go, total_ro;
extern const int opt_cutofftemp;
extern int opt_log_interval;
extern unsigned long long global_hashrate;

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

struct pool {
	int pool_no;
	int prio;
	int accepted, rejected;
	int seq_rejects;
	int solved;
	int queued;
	int staged;

	bool submit_fail;
	bool idle;
	bool lagging;
	bool probed;
	enum pool_enable enabled;
	bool submit_old;
	bool removed;
	bool lp_started;

	char *hdr_path;
	char *lp_url;

	unsigned int getwork_requested;
	unsigned int stale_shares;
	unsigned int discarded_work;
	unsigned int getfail_occasions;
	unsigned int remotefail_occasions;
	struct timeval tv_idle;

	char *rpc_url;
	char *rpc_userpass;
	char *rpc_user, *rpc_pass;

	pthread_mutex_t pool_lock;

	struct thread_q *submit_q;
	struct thread_q *getwork_q;

	pthread_t longpoll_thread;
	pthread_t submit_thread;
	pthread_t getwork_thread;

	int curls;
	pthread_cond_t cr_cond;
	struct list_head curlring;

	time_t last_share_time;

	struct cgminer_stats cgminer_stats;
	struct cgminer_pool_stats cgminer_pool_stats;
};

struct work {
	unsigned char	data[128];
	unsigned char	hash1[64];
	unsigned char	midstate[32];
	unsigned char	target[32];
	unsigned char	hash[32];

	int		rolls;

	uint32_t	output[1];
	uint32_t	valid;
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

	unsigned int	work_block;
	int		id;
	UT_hash_handle hh;

	time_t share_found_time;
};

#ifdef USE_MODMINER 
struct modminer_fpga_state {
	bool work_running;
	struct work running_work;
	struct timeval tv_workstart;
	uint32_t hashes;

	char next_work_cmd[46];

	unsigned char clock;
	int no_nonce_counter;
	int good_share_counter;
	time_t last_cutoff_reduced;

	unsigned char temp;
};
#endif

extern void get_datestamp(char *, struct timeval *);
extern bool test_nonce(struct work *work, uint32_t nonce);
bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce);
extern void tailsprintf(char *f, const char *fmt, ...);
extern void wlogprint(const char *f, ...);
extern int curses_int(const char *query);
extern char *curses_input(const char *query);
extern void kill_work(void);
extern void switch_pools(struct pool *selected);
extern void remove_pool(struct pool *pool);
extern void write_config(FILE *fcfg);
extern void default_save_file(char *filename);
extern bool log_curses_only(int prio, const char *f, va_list ap);
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
extern void app_restart(void);

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
	API_HS
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
extern struct api_data *api_add_string(struct api_data *root, char *name, char *data, bool copy_data);
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

#endif /* __MINER_H__ */
