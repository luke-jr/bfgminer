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


#ifdef __SSE2__
#define WANT_SSE2_4WAY 1
#endif

#if defined(__i386__) && defined(HAS_YASM) && defined(__SSE2__)
#define WANT_X8632_SSE2 1
#endif

#if defined(__i386__) || defined(__x86_64__)
#define WANT_VIA_PADLOCK 1
#endif

#if defined(__x86_64__) && defined(HAS_YASM)
#define WANT_X8664_SSE2 1
#endif

#if defined(__x86_64__) && defined(HAS_YASM)
#define WANT_X8664_SSE4 1
#endif

#if !defined(WIN32) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#define WANT_BUILTIN_BSWAP
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

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#else
enum {
	LOG_ERR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG,
};
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
};

struct cgpu_info {
	int is_gpu;
	int cpu_gpu;
	int accepted;
	int rejected;
	int hw_errors;
	double rolling;
	double total_mhashes;
	unsigned int getworks;
	double efficiency;
	double utility;
	enum alive status;
	char init[40];
	struct timeval last_message_tv;
};

struct thread_q {
	struct list_head	q;

	bool frozen;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
};

struct thr_info {
	int		id;
	pthread_t	*pth;
	struct thread_q	*q;
	struct cgpu_info *cgpu;
	struct timeval last;
	struct timeval sick;

	bool	pause;
	bool	getwork;
	double	rolling;
};

extern inline int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg);

static inline uint32_t swab32(uint32_t v)
{
#ifdef WANT_BUILTIN_BSWAP
	return __builtin_bswap32(v);
#else
	return bswap_32(v);
#endif
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

struct pool;

extern bool opt_debug;
extern bool opt_protocol;
extern bool opt_log_output;
extern char *opt_kernel_path;
extern const uint32_t sha256_init_state[];
extern json_t *json_rpc_call(CURL *curl, const char *url, const char *userpass,
			     const char *rpc_req, bool, bool, bool *,
			     struct pool *pool);
extern char *bin2hex(const unsigned char *p, size_t len);
extern bool hex2bin(unsigned char *p, const char *hexstr, size_t len);

extern unsigned int ScanHash_4WaySSE2(int, const unsigned char *pmidstate,
	unsigned char *pdata, unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce, unsigned long *nHashesDone, uint32_t nonce);

extern unsigned int scanhash_sse2_amd64(int, const unsigned char *pmidstate,
	unsigned char *pdata, unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce, unsigned long *nHashesDone);

extern bool scanhash_via(int, unsigned char *data_inout,
	const unsigned char *target,
	uint32_t max_nonce, unsigned long *hashes_done, uint32_t n);

extern bool scanhash_c(int, const unsigned char *midstate, unsigned char *data,
	      unsigned char *hash1, unsigned char *hash,
	      const unsigned char *target,
	      uint32_t max_nonce, unsigned long *hashes_done, uint32_t n);
extern bool scanhash_cryptopp(int, const unsigned char *midstate,unsigned char *data,
	      unsigned char *hash1, unsigned char *hash,
	      const unsigned char *target,
	      uint32_t max_nonce, unsigned long *hashes_done, uint32_t n);
extern bool scanhash_asm32(int, const unsigned char *midstate,unsigned char *data,
	      unsigned char *hash1, unsigned char *hash,
	      const unsigned char *target,
	      uint32_t max_nonce, unsigned long *hashes_done, uint32_t nonce);
extern int scanhash_sse2_64(int, const unsigned char *pmidstate, unsigned char *pdata,
	unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce, unsigned long *nHashesDone,
	uint32_t nonce);

extern int scanhash_sse4_64(int, const unsigned char *pmidstate, unsigned char *pdata,
	unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce, unsigned long *nHashesDone,
	uint32_t nonce);

extern int scanhash_sse2_32(int, const unsigned char *pmidstate, unsigned char *pdata,
	unsigned char *phash1, unsigned char *phash,
	const unsigned char *ptarget,
	uint32_t max_nonce, unsigned long *nHashesDone,
	uint32_t nonce);

extern int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);

extern bool fulltest(const unsigned char *hash, const unsigned char *target);

extern int opt_scantime;

struct work_restart {
	volatile unsigned long	restart;
	char			padding[128 - sizeof(unsigned long)];
};

extern int hw_errors;
extern bool use_syslog;
extern struct thr_info *thr_info;
extern int longpoll_thr_id;
extern struct work_restart *work_restart;

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
} dev_blk_ctx;
#else
typedef struct {
	uint32_t nonce;
} dev_blk_ctx;
#endif

struct pool {
	int pool_no;
	int prio;
	int accepted, rejected;
	bool submit_fail;
	bool idle;
	bool lagging;
	bool probed;
	bool enabled;

	char *hdr_path;

	unsigned int getwork_requested;
	unsigned int stale_shares;
	unsigned int discarded_work;
	unsigned int localgen_occasions;
	unsigned int remotefail_occasions;
	struct timeval tv_idle;

	char *rpc_url;
	char *rpc_userpass;
	char *rpc_user, *rpc_pass;

	pthread_mutex_t pool_lock;
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
	bool		rolltime;

	int		id;
	UT_hash_handle hh;
};

enum cl_kernel {
	KL_NONE,
	KL_POCLBM,
	KL_PHATK,
};

bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce);

extern void kill_work(void);
extern void log_curses(int prio, const char *f, va_list ap);
extern void vapplog(int prio, const char *fmt, va_list ap);
extern void applog(int prio, const char *fmt, ...);
extern struct thread_q *tq_new(void);
extern void tq_free(struct thread_q *tq);
extern bool tq_push(struct thread_q *tq, void *data);
extern void *tq_pop(struct thread_q *tq, const struct timespec *abstime);
extern void tq_freeze(struct thread_q *tq);
extern void tq_thaw(struct thread_q *tq);
extern bool successful_connect;
extern enum cl_kernel chosen_kernel;

#endif /* __MINER_H__ */
