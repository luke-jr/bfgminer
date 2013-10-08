#ifndef __UTIL_H__
#define __UTIL_H__

#include <curl/curl.h>
#include <jansson.h>

#include "compat.h"

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

	#define SOCKETTYPE int
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define INVINETADDR -1
	#define CLOSESOCKET close

	#define SOCKERRMSG strerror(errno)
	static inline bool sock_blocks(void)
	{
		return (errno == EAGAIN || errno == EWOULDBLOCK);
	}
#elif defined WIN32
	#include <ws2tcpip.h>
	#include <winsock2.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((int)(a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define INVINETADDR INADDR_NONE
	#define CLOSESOCKET closesocket

	extern char *WSAErrorMsg(void);
	#define SOCKERRMSG WSAErrorMsg()

	static inline bool sock_blocks(void)
	{
		return (WSAGetLastError() == WSAEWOULDBLOCK);
	}
	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif

	#ifndef in_addr_t
	#define in_addr_t uint32_t
	#endif
#endif

#define IGNORE_RETURN_VALUE(expr)  {if(expr);}(void)0

#if JANSSON_MAJOR_VERSION >= 2
#define JSON_LOADS(str, err_ptr) json_loads((str), 0, (err_ptr))
#else
#define JSON_LOADS(str, err_ptr) json_loads((str), (err_ptr))
#endif
extern char *json_dumps_ANY(json_t *, size_t flags);

struct pool;
enum dev_reason;
struct cgpu_info;

extern void json_rpc_call_async(CURL *, const char *url, const char *userpass, const char *rpc_req, bool longpoll, struct pool *pool, bool share, void *priv);
extern json_t *json_rpc_call_completed(CURL *, int rc, bool probe, int *rolltime, void *out_priv);

extern char *absolute_uri(char *uri, const char *ref);  // ref must be a root URI

extern void gen_hash(unsigned char *data, unsigned char *hash, int len);
extern void hash_data(unsigned char *out_hash, const unsigned char *data);
extern void real_block_target(unsigned char *target, const unsigned char *data);
extern bool hash_target_check(const unsigned char *hash, const unsigned char *target);
extern bool hash_target_check_v(const unsigned char *hash, const unsigned char *target);

bool stratum_send(struct pool *pool, char *s, ssize_t len);
bool sock_full(struct pool *pool);
char *recv_line(struct pool *pool);
bool parse_method(struct pool *pool, char *s);
bool extract_sockaddr(struct pool *pool, char *url);
bool auth_stratum(struct pool *pool);
bool initiate_stratum(struct pool *pool);
bool restart_stratum(struct pool *pool);
void suspend_stratum(struct pool *pool);
void dev_error(struct cgpu_info *dev, enum dev_reason reason);
void *realloc_strcat(char *ptr, char *s);
extern char *sanestr(char *o, char *s);
void RenameThread(const char* name);

typedef SOCKETTYPE notifier_t[2];
extern void notifier_init(notifier_t);
extern void notifier_wake(notifier_t);
extern void notifier_read(notifier_t);
extern void notifier_destroy(notifier_t);

/* Align a size_t to 4 byte boundaries for fussy arches */
static inline void align_len(size_t *len)
{
	if (*len % 4)
		*len += 4 - (*len % 4);
}


static inline
void set_maxfd(int *p_maxfd, int fd)
{
	if (fd > *p_maxfd)
		*p_maxfd = fd;
}


#define TIMEVAL_USECS(usecs)  (  \
	(struct timeval){  \
		.tv_sec = (usecs) / 1000000,  \
		.tv_usec = (usecs) % 1000000,  \
	}  \
)

#define timer_set_delay(tvp_timer, tvp_now, usecs)  do {  \
	struct timeval tv_add = TIMEVAL_USECS(usecs);  \
	timeradd(&tv_add, tvp_now, tvp_timer);  \
} while(0)

#define timer_set_delay_from_now(tvp_timer, usecs)  do {  \
	struct timeval tv_now;  \
	gettimeofday(&tv_now, NULL);  \
	timer_set_delay(tvp_timer, &tv_now, usecs);  \
} while(0)

static inline
bool timer_passed(struct timeval *tvp_timer, struct timeval *tvp_now)
{
	return (tvp_timer->tv_sec != -1 && timercmp(tvp_timer, tvp_now, <));
}

static inline
void reduce_timeout_to(struct timeval *tvp_timeout, struct timeval *tvp_time)
{
	if (tvp_time->tv_sec == -1)
		return;
	if (tvp_timeout->tv_sec == -1 /* no timeout */ || timercmp(tvp_time, tvp_timeout, <))
		*tvp_timeout = *tvp_time;
}

static inline
struct timeval *select_timeout(struct timeval *tvp_timeout, struct timeval *tvp_now)
{
	if (tvp_timeout->tv_sec == -1)
		return NULL;
	
	if (timercmp(tvp_timeout, tvp_now, <))
		timerclear(tvp_timeout);
	else
		timersub(tvp_timeout, tvp_now, tvp_timeout);
	
	return tvp_timeout;
}


#endif /* __UTIL_H__ */
