/*
 * Copyright 2013-2015 Luke Dashjr
 * Copyright 2012-2014 Con Kolivas
 * Copyright 2011 Andrew Smith
 * Copyright 2011 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFG_UTIL_H
#define BFG_UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include <curl/curl.h>
#include <jansson.h>

#include "compat.h"

#define INVALID_TIMESTAMP ((time_t)-1)

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

	#define SOCKETTYPE int
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define INVINETADDR -1
	#define CLOSESOCKET close

	#define SOCKERR (errno)
	#define SOCKERRMSG bfg_strerror(errno, BST_SOCKET)
	static inline bool sock_blocks(void)
	{
		return (errno == EAGAIN || errno == EWOULDBLOCK);
	}
	static inline bool interrupted(void)
	{
		return (errno == EINTR);
	}
#elif defined WIN32
	#include <ws2tcpip.h>
	#include <winsock2.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((int)(a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define INVINETADDR INADDR_NONE
	#define CLOSESOCKET closesocket

	#define SOCKERR (WSAGetLastError())
	#define SOCKERRMSG bfg_strerror(WSAGetLastError(), BST_SOCKET)

	/* Check for windows variants of the errors as well as when ming
	 * decides to wrap the error into the errno equivalent. */
	static inline bool sock_blocks(void)
	{
		return (WSAGetLastError() == WSAEWOULDBLOCK || errno == EAGAIN);
	}
	static inline bool interrupted(void)
	{
		return (WSAGetLastError() == WSAEINTR || errno == EINTR);
	}
	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif

	#ifndef in_addr_t
	#define in_addr_t uint32_t
	#endif
#endif

#define IGNORE_RETURN_VALUE(expr)  {if(expr);}(void)0

enum bfg_tristate {
	BTS_FALSE = (int)false,
	BTS_TRUE  = (int)true,
	BTS_UNKNOWN,
};

#if JANSSON_MAJOR_VERSION >= 2
#define JSON_LOADS(str, err_ptr) json_loads((str), 0, (err_ptr))
#else
#define JSON_LOADS(str, err_ptr) json_loads((str), (err_ptr))
#endif
extern char *json_dumps_ANY(json_t *, size_t flags);

static inline
const char *bfg_json_obj_string(json_t *json, const char *key, const char *fail)
{
	json = json_object_get(json, key);
	if (!json)
		return fail;
	return json_string_value(json) ?: fail;
}

extern const char *__json_array_string(json_t *, unsigned int entry);

#ifndef min
#  define min(a, b)  ((a) < (b) ? (a) : (b))
#endif

extern void *my_memrchr(const void *, int, size_t);

extern bool isCalpha(int);
static inline
bool isCspace(int c)
{
	switch (c)
	{
		case ' ': case '\f': case '\n': case '\r': case '\t': case '\v':
			return true;
		default:
			return false;
	}
}

extern bool match_strtok(const char *optlist, const char *delim, const char *needle);

typedef bool (*appdata_file_callback_t)(const char *, void *);
extern bool appdata_file_call(const char *appname, const char *filename, appdata_file_callback_t, void *userp);
extern char *appdata_file_find_first(const char *appname, const char *filename);

extern const char *get_registered_domain(size_t *out_len, const char *, size_t len);
extern const char *extract_domain(size_t *out_len, const char *uri, size_t urilen);
extern bool match_domains(const char *a, size_t alen, const char *b, size_t blen);
extern void test_domain_funcs();

extern bool bfg_strtobool(const char *, char **endptr, int opts);

extern enum bfg_tristate uri_get_param_bool2(const char *ri, const char *param);
extern bool uri_get_param_bool(const char *uri, const char *param, bool defval);
extern void test_uri_get_param();


enum bfg_gpio_value {
	BGV_LOW   =  0,
	BGV_HIGH  =  1,
	BGV_ERROR = -1,
};


typedef struct timeval cgtimer_t;

struct thr_info;
struct pool;
enum dev_reason;
struct cgpu_info;


extern void set_cloexec_socket(SOCKETTYPE, bool cloexec);

static inline
SOCKETTYPE bfg_socket(const int domain, const int type, const int protocol)
{
	const bool cloexec = true;
	SOCKETTYPE sock;
#ifdef WIN32
# ifndef WSA_FLAG_NO_HANDLE_INHERIT
#  define WSA_FLAG_NO_HANDLE_INHERIT 0x80
# endif
	sock = WSASocket(domain, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED | ((cloexec) ? WSA_FLAG_NO_HANDLE_INHERIT : 0));
	if (sock == INVSOCK)
#endif
	sock = socket(domain, type, protocol);
	if (sock == INVSOCK)
		return INVSOCK;
	set_cloexec_socket(sock, cloexec);
	return sock;
}


extern void json_rpc_call_async(CURL *, const char *url, const char *userpass, const char *rpc_req, bool longpoll, struct pool *pool, bool share, void *priv);
extern json_t *json_rpc_call_completed(CURL *, int rc, bool probe, int *rolltime, void *out_priv);

extern char *absolute_uri(char *uri, const char *ref);  // ref must be a root URI

extern size_t ucs2_to_utf8(char *out, const uint16_t *in, size_t sz);
extern char *ucs2_to_utf8_dup(uint16_t *in, size_t sz);

#define BFGINIT(var, val)  do{  \
	if (!(var))       \
		(var) = val;  \
}while(0)

extern void gen_hash(unsigned char *data, unsigned char *hash, int len);
extern void real_block_target(unsigned char *target, const unsigned char *data);
extern bool hash_target_check(const unsigned char *hash, const unsigned char *target);
extern bool hash_target_check_v(const unsigned char *hash, const unsigned char *target);

int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg);
void thr_info_freeze(struct thr_info *thr);
void thr_info_cancel(struct thr_info *thr);
void subtime(struct timeval *a, struct timeval *b);
void addtime(struct timeval *a, struct timeval *b);
bool time_more(struct timeval *a, struct timeval *b);
bool time_less(struct timeval *a, struct timeval *b);
void copy_time(struct timeval *dest, const struct timeval *src);
void timespec_to_val(struct timeval *val, const struct timespec *spec);
void timeval_to_spec(struct timespec *spec, const struct timeval *val);
void us_to_timeval(struct timeval *val, int64_t us);
void us_to_timespec(struct timespec *spec, int64_t us);
void ms_to_timespec(struct timespec *spec, int64_t ms);
void timeraddspec(struct timespec *a, const struct timespec *b);
void cgsleep_ms(int ms);
void cgsleep_us(int64_t us);
#define cgtimer_time(ts_start) timer_set_now(ts_start)
#define cgsleep_prepare_r(ts_start) cgtimer_time(ts_start)
void cgsleep_ms_r(cgtimer_t *ts_start, int ms);
void (*cgsleep_us_r)(cgtimer_t *ts_start, int64_t us);

static inline
int cgtimer_to_ms(cgtimer_t *cgt)
{
	return (cgt->tv_sec * 1000) + (cgt->tv_usec / 1000);
}

extern int bfg_cond_timedwait(pthread_cond_t * restrict, pthread_mutex_t * restrict, const struct timeval *);
extern pthread_condattr_t *bfg_condattr_();
#define bfg_condattr (bfg_condattr_())

#define cgtimer_sub(a, b, res)  timersub(a, b, res)
double us_tdiff(struct timeval *end, struct timeval *start);
double tdiff(struct timeval *end, struct timeval *start);
bool _stratum_send(struct pool *pool, char *s, ssize_t len, bool force);
#define stratum_send(pool, s, len)  _stratum_send(pool, s, len, false)
bool sock_full(struct pool *pool);
char *recv_line(struct pool *pool);
bool parse_method(struct pool *pool, char *s);
bool extract_sockaddr(char *url, char **sockaddr_url, char **sockaddr_port);
bool auth_stratum(struct pool *pool);
bool initiate_stratum(struct pool *pool);
bool restart_stratum(struct pool *pool);
void suspend_stratum(struct pool *pool);
extern void dev_error_update(struct cgpu_info *, enum dev_reason);
void dev_error(struct cgpu_info *dev, enum dev_reason reason);
void *realloc_strcat(char *ptr, char *s);
extern char *sanestr(char *o, char *s);
void RenameThread(const char* name);

enum bfg_strerror_type {
	BST_ERRNO,
	BST_SOCKET,
	BST_LIBUSB,
	BST_SYSTEM,
};
extern const char *bfg_strerror(int, enum bfg_strerror_type);

extern void *bfg_slurp_file(void *buf, size_t bufsz, const char *filename);

typedef SOCKETTYPE notifier_t[2];
extern void notifier_init(notifier_t);
extern void notifier_wake(notifier_t);
extern void notifier_read(notifier_t);
extern bool notifier_wait(notifier_t, const struct timeval *);
extern bool notifier_wait_us(notifier_t, unsigned long long usecs);
extern void notifier_reset(notifier_t);
extern void notifier_init_invalid(notifier_t);
extern void notifier_destroy(notifier_t);

/* Align a size_t to 4 byte boundaries for fussy arches */
static inline void align_len(size_t *len)
{
	if (*len % 4)
		*len += 4 - (*len % 4);
}


static inline
uint8_t bitflip8(uint8_t p)
{
	p = ((p & 0xaa) >> 1) | ((p & 0x55) << 1);
	p = ((p & 0xcc) >> 2) | ((p & 0x33) << 2);
	p = ((p & 0xf0) >> 4) | ((p & 0x0f) << 4);
	return p;
}

static inline
uint8_t upk_u8(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return buf[offset];
}

#define upk_u8be(buf, offset)  upk_u8(buf, offset)

static inline
uint16_t upk_u16be(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint16_t)buf[offset+0]) <<    8)
	     | (((uint16_t)buf[offset+1]) <<    0);
}

static inline
uint32_t upk_u32be(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint32_t)buf[offset+0]) << 0x18)
	     | (((uint32_t)buf[offset+1]) << 0x10)
	     | (((uint32_t)buf[offset+2]) <<    8)
	     | (((uint32_t)buf[offset+3]) <<    0);
}

static inline
uint64_t upk_u64be(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint64_t)buf[offset+0]) << 0x38)
	     | (((uint64_t)buf[offset+1]) << 0x30)
	     | (((uint64_t)buf[offset+2]) << 0x28)
	     | (((uint64_t)buf[offset+3]) << 0x20)
	     | (((uint64_t)buf[offset+4]) << 0x18)
	     | (((uint64_t)buf[offset+5]) << 0x10)
	     | (((uint64_t)buf[offset+6]) <<    8)
	     | (((uint64_t)buf[offset+7]) <<    0);
}

#define upk_u8le(buf, offset)  upk_u8(buf, offset)

static inline
uint16_t upk_u16le(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint16_t)buf[offset+0]) <<    0)
	     | (((uint16_t)buf[offset+1]) <<    8);
}

static inline
uint32_t upk_u32le(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint32_t)buf[offset+0]) <<    0)
	     | (((uint32_t)buf[offset+1]) <<    8)
	     | (((uint32_t)buf[offset+2]) << 0x10)
	     | (((uint32_t)buf[offset+3]) << 0x18);
}

static inline
uint64_t upk_u64le(const void * const bufp, const int offset)
{
	const uint8_t * const buf = bufp;
	return (((uint64_t)buf[offset+0]) <<    0)
	     | (((uint64_t)buf[offset+1]) <<    8)
	     | (((uint64_t)buf[offset+2]) << 0x10)
	     | (((uint64_t)buf[offset+3]) << 0x18)
	     | (((uint64_t)buf[offset+4]) << 0x20)
	     | (((uint64_t)buf[offset+5]) << 0x28)
	     | (((uint64_t)buf[offset+6]) << 0x30)
	     | (((uint64_t)buf[offset+7]) << 0x38);
}


static inline
void pk_u8(void * const bufp, const int offset, const uint8_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset] = nv;
}

#define pk_u8be(buf, offset, nv)  pk_u8(buf, offset, nv)

static inline
void pk_u16be(void * const bufp, const int offset, const uint16_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    8) & 0xff;
	buf[offset+1] = (nv >>    0) & 0xff;
}

static inline
void pk_u32be(void * const bufp, const int offset, const uint32_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >> 0x18) & 0xff;
	buf[offset+1] = (nv >> 0x10) & 0xff;
	buf[offset+2] = (nv >>    8) & 0xff;
	buf[offset+3] = (nv >>    0) & 0xff;
}

static inline
void pk_u64be(void * const bufp, const int offset, const uint64_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >> 0x38) & 0xff;
	buf[offset+1] = (nv >> 0x30) & 0xff;
	buf[offset+2] = (nv >> 0x28) & 0xff;
	buf[offset+3] = (nv >> 0x20) & 0xff;
	buf[offset+4] = (nv >> 0x18) & 0xff;
	buf[offset+5] = (nv >> 0x10) & 0xff;
	buf[offset+6] = (nv >>    8) & 0xff;
	buf[offset+7] = (nv >>    0) & 0xff;
}

#define pk_u8le(buf, offset, nv)  pk_u8(buf, offset, nv)

static inline
void pk_u16le(void * const bufp, const int offset, const uint16_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    0) & 0xff;
	buf[offset+1] = (nv >>    8) & 0xff;
}

static inline
void pk_u32le(void * const bufp, const int offset, const uint32_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    0) & 0xff;
	buf[offset+1] = (nv >>    8) & 0xff;
	buf[offset+2] = (nv >> 0x10) & 0xff;
	buf[offset+3] = (nv >> 0x18) & 0xff;
}

static inline
void pk_u64le(void * const bufp, const int offset, const uint64_t nv)
{
	uint8_t * const buf = bufp;
	buf[offset+0] = (nv >>    0) & 0xff;
	buf[offset+1] = (nv >>    8) & 0xff;
	buf[offset+2] = (nv >> 0x10) & 0xff;
	buf[offset+3] = (nv >> 0x18) & 0xff;
	buf[offset+4] = (nv >> 0x20) & 0xff;
	buf[offset+5] = (nv >> 0x28) & 0xff;
	buf[offset+6] = (nv >> 0x30) & 0xff;
	buf[offset+7] = (nv >> 0x38) & 0xff;
}

#define _pk_uNle(bitwidth, newvalue)  do{  \
	uint ## bitwidth ## _t _mask = 1;  \
	_mask <<= _bitlen;  \
	--_mask;  \
	uint ## bitwidth ## _t _filt = _mask;  \
	_filt <<= _bitoff;  \
	_filt = ~_filt;  \
	uint ## bitwidth ## _t _u = upk_u ## bitwidth ## le(_buf, 0);  \
	_u = (_u & _filt) | (((newvalue) & _mask) << _bitoff);  \
	pk_u ## bitwidth ## le(_buf, 0, _u);  \
}while(0)

#define pk_uNle(bufp, offset, bitoffset, bitlength, newvalue)  do{  \
	uint8_t * const _buf = &((uint8_t *)(bufp))[offset];  \
	const int _bitoff = (bitoffset), _bitlen = bitlength;  \
	const int _bittot = bitoffset + bitlength;  \
	_Static_assert((bitoffset + bitlength) <= 0x40, "Too many bits addressed in pk_uNle (bitoffset + bitlength must be <= 64)");  \
	if (_bittot <=    8)  \
		_pk_uNle( 8, newvalue);  \
	else  \
	if (_bittot <= 0x10)  \
		_pk_uNle(16, newvalue);  \
	else  \
	if (_bittot <= 0x20)  \
		_pk_uNle(32, newvalue);  \
	else  \
		_pk_uNle(64, newvalue);  \
}while(0)

#define is_power_of_two(n)  \
	(0 == ((n) & ((n) - 1)))

static inline
uint32_t upper_power_of_two_u32(uint32_t n)
{
	--n;
	for (int i = 1; i <= 0x10; i *= 2)
		n |= n >> i;
	++n;
	return n;
}


typedef struct bytes_t {
	uint8_t *buf;
	size_t sz;
	size_t allocsz;
} bytes_t;

#define BYTES_INIT {.buf=NULL,}

static inline
void bytes_init(bytes_t *b)
{
	*b = (bytes_t)BYTES_INIT;
}

// This can't be inline without ugly const/non-const issues
#define bytes_buf(b)  ((b)->buf)

static inline
size_t bytes_len(const bytes_t *b)
{
	return b->sz;
}

static inline
ssize_t bytes_find(const bytes_t * const b, const uint8_t needle)
{
	const size_t blen = bytes_len(b);
	const uint8_t * const buf = bytes_buf(b);
	for (int i = 0; i < blen; ++i)
		if (buf[i] == needle)
			return i;
	return -1;
}

static inline
bool bytes_eq(const bytes_t * const a, const bytes_t * const b)
{
	if (a->sz != b->sz)
		return false;
	return !memcmp(a->buf, b->buf, a->sz);
}

extern void _bytes_alloc_failure(size_t);

static inline
void bytes_extend_buf(bytes_t * const b, const size_t newsz)
{
	if (newsz <= b->allocsz)
		return;
	
	if (!b->allocsz)
		b->allocsz = 0x10;
	do {
		b->allocsz *= 2;
	} while (newsz > b->allocsz);
	b->buf = realloc(b->buf, b->allocsz);
	if (!b->buf)
		_bytes_alloc_failure(b->allocsz);
}

static inline
void bytes_resize(bytes_t * const b, const size_t newsz)
{
	bytes_extend_buf(b, newsz);
	b->sz = newsz;
}

static inline
void *bytes_preappend(bytes_t * const b, const size_t addsz)
{
	size_t origsz = bytes_len(b);
	bytes_extend_buf(b, origsz + addsz);
	return &bytes_buf(b)[origsz];
}

static inline
void bytes_postappend(bytes_t * const b, const size_t addsz)
{
	size_t origsz = bytes_len(b);
	bytes_resize(b, origsz + addsz);
}

static inline
void bytes_append(bytes_t * const b, const void * const add, const size_t addsz)
{
	void * const appendbuf = bytes_preappend(b, addsz);
	memcpy(appendbuf, add, addsz);
	bytes_postappend(b, addsz);
}

static inline
void bytes_cat(bytes_t *b, const bytes_t *cat)
{
	bytes_append(b, bytes_buf(cat), bytes_len(cat));
}

static inline
void bytes_cpy(bytes_t *dst, const bytes_t *src)
{
	dst->sz = src->sz;
	if (!dst->sz) {
		dst->allocsz = 0;
		dst->buf = NULL;
		return;
	}
	dst->allocsz = src->allocsz;
	size_t half;
	while (dst->sz <= (half = dst->allocsz / 2))
		dst->allocsz = half;
	dst->buf = malloc(dst->allocsz);
	memcpy(dst->buf, src->buf, dst->sz);
}

// Efficiently moves the data from src to dst, emptying src in the process
static inline
void bytes_assimilate(bytes_t * const dst, bytes_t * const src)
{
	void * const buf = dst->buf;
	const size_t allocsz = dst->allocsz;
	*dst = *src;
	*src = (bytes_t){
		.buf = buf,
		.allocsz = allocsz,
	};
}

static inline
void bytes_assimilate_raw(bytes_t * const b, void * const buf, const size_t bufsz, const size_t buflen)
{
	free(b->buf);
	b->buf = buf;
	b->allocsz = bufsz;
	b->sz = buflen;
}

static inline
void bytes_shift(bytes_t *b, size_t shift)
{
	if (shift >= b->sz)
	{
		b->sz = 0;
		return;
	}
	b->sz -= shift;
	memmove(bytes_buf(b), &bytes_buf(b)[shift], bytes_len(b));
}

static inline
void bytes_reset(bytes_t *b)
{
	b->sz = 0;
}

static inline
void bytes_nullterminate(bytes_t *b)
{
	bytes_append(b, "", 1);
	--b->sz;
}

static inline
void bytes_free(bytes_t *b)
{
	free(b->buf);
	bytes_init(b);
}


static inline
void set_maxfd(int *p_maxfd, int fd)
{
	if (fd > *p_maxfd)
		*p_maxfd = fd;
}


static inline
void timer_unset(struct timeval *tvp)
{
	tvp->tv_sec = -1;
}

static inline
bool timer_isset(const struct timeval *tvp)
{
	return tvp->tv_sec != -1;
}

extern void (*timer_set_now)(struct timeval *);
#define cgtime(tvp)  timer_set_now(tvp)

#define TIMEVAL_USECS(usecs)  (  \
	(struct timeval){  \
		.tv_sec = (usecs) / 1000000,  \
		.tv_usec = (usecs) % 1000000,  \
	}  \
)

static inline
long timeval_to_us(const struct timeval *tvp)
{
	return ((long)tvp->tv_sec * 1000000) + tvp->tv_usec;
}

#define timer_set_delay(tvp_timer, tvp_now, usecs)  do {  \
	struct timeval tv_add = TIMEVAL_USECS(usecs);  \
	timeradd(&tv_add, tvp_now, tvp_timer);  \
} while(0)

#define timer_set_delay_from_now(tvp_timer, usecs)  do {  \
	struct timeval tv_now;  \
	timer_set_now(&tv_now);  \
	timer_set_delay(tvp_timer, &tv_now, usecs);  \
} while(0)

static inline
const struct timeval *_bfg_nullisnow(const struct timeval *tvp, struct timeval *tvp_buf)
{
	if (tvp)
		return tvp;
	cgtime(tvp_buf);
	return tvp_buf;
}

static inline
long timer_elapsed_us(const struct timeval *tvp_timer, const struct timeval *tvp_now)
{
	struct timeval tv;
	const struct timeval *_tvp_now = _bfg_nullisnow(tvp_now, &tv);
	timersub(_tvp_now, tvp_timer, &tv);
	return timeval_to_us(&tv);
}

#define ms_tdiff(end, start)  (timer_elapsed_us(start, end) / 1000)

static inline
int timer_elapsed(const struct timeval *tvp_timer, const struct timeval *tvp_now)
{
	struct timeval tv;
	const struct timeval *_tvp_now = _bfg_nullisnow(tvp_now, &tv);
	timersub(_tvp_now, tvp_timer, &tv);
	return tv.tv_sec;
}

static inline
long timer_remaining_us(const struct timeval *tvp_timer, const struct timeval *tvp_now)
{
	struct timeval tv;
	const struct timeval *_tvp_now = _bfg_nullisnow(tvp_now, &tv);
	timersub(tvp_timer, _tvp_now, &tv);
	return timeval_to_us(&tv);
}

static inline
bool timer_passed(const struct timeval *tvp_timer, const struct timeval *tvp_now)
{
	if (!timer_isset(tvp_timer))
		return false;
	
	struct timeval tv;
	const struct timeval *_tvp_now = _bfg_nullisnow(tvp_now, &tv);
	
	return timercmp(tvp_timer, _tvp_now, <);
}

#if defined(WIN32) && !defined(HAVE_POOR_GETTIMEOFDAY)
#define HAVE_POOR_GETTIMEOFDAY
#endif

#ifdef HAVE_POOR_GETTIMEOFDAY
extern void bfg_gettimeofday(struct timeval *);
#else
#define bfg_gettimeofday(out)  gettimeofday(out, NULL)
#endif

static inline
void reduce_timeout_to(struct timeval *tvp_timeout, struct timeval *tvp_time)
{
	if (!timer_isset(tvp_time))
		return;
	if ((!timer_isset(tvp_timeout)) || timercmp(tvp_time, tvp_timeout, <))
		*tvp_timeout = *tvp_time;
}

static inline
struct timeval *select_timeout(struct timeval *tvp_timeout, struct timeval *tvp_now)
{
	if (!timer_isset(tvp_timeout))
		return NULL;
	
	if (timercmp(tvp_timeout, tvp_now, <))
		timerclear(tvp_timeout);
	else
		timersub(tvp_timeout, tvp_now, tvp_timeout);
	
	return tvp_timeout;
}


#define _SNP2(fn, ...)  do{  \
        int __n42 = fn(s, sz, __VA_ARGS__);  \
        s += __n42;  \
        sz = (sz <= __n42) ? 0 : (sz - __n42);  \
        rv += __n42;  \
}while(0)

#define _SNP(...)  _SNP2(snprintf, __VA_ARGS__)


extern int double_find_precision(double, double base);


#define REPLACEMENT_CHAR (0xFFFD)
#define U8_DEGREE "\xc2\xb0"
#define U8_MICRO  "\xc2\xb5"
#define U8_HLINE  "\xe2\x94\x80"
#define U8_BTEE   "\xe2\x94\xb4"
extern int utf8_len(uint8_t);
extern int32_t utf8_decode(const void *, int *out_len);
extern size_t utf8_strlen(const void *);
extern void utf8_test();



#define RUNONCE(rv)  do {  \
	static bool _runonce = false;  \
	if (_runonce)  \
		return rv;  \
	_runonce = true;  \
} while(0)


static inline
char *maybe_strdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

static inline
void maybe_strdup_if_null(const char **p, const char *s)
{
	if (!*p)
		*p = maybe_strdup(s);
}

extern char *trimmed_strdup(const char *);


extern void run_cmd(const char *cmd);


extern bool bm1382_freq_to_reg_data(uint8_t *out_reg_data, float mhz);


extern uint8_t crc5usb(unsigned char *ptr, uint8_t len);
extern void bfg_init_checksums(void);
extern uint8_t crc8ccitt(const void *, size_t);

extern uint16_t crc16(const void *, size_t, uint16_t init);
#define crc16ffff(  DATA, SZ)  crc16(DATA, SZ, 0xffff)
#define crc16xmodem(DATA, SZ)  crc16(DATA, SZ, 0)

#endif /* __UTIL_H__ */
