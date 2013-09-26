#ifndef __UTIL_H__
#define __UTIL_H__

#include <semaphore.h>

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

	#define SOCKETTYPE long
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

#if JANSSON_MAJOR_VERSION >= 2
#define JSON_LOADS(str, err_ptr) json_loads((str), 0, (err_ptr))
#else
#define JSON_LOADS(str, err_ptr) json_loads((str), (err_ptr))
#endif

#ifdef HAVE_LIBCURL
typedef curl_proxytype proxytypes_t;
#else
typedef int proxytypes_t;
#endif /* HAVE_LIBCURL */

/* cgminer specific unnamed semaphore implementations to cope with osx not
 * implementing them. */
#ifdef __APPLE__
struct cgsem {
	int pipefd[2];
};

typedef struct cgsem cgsem_t;
#else
typedef sem_t cgsem_t;
#endif
typedef struct timespec cgtimer_t;

struct thr_info;
struct pool;
enum dev_reason;
struct cgpu_info;
int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg);
void thr_info_cancel(struct thr_info *thr);
void cgtime(struct timeval *tv);
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
void cgtimer_time(cgtimer_t *ts_start);
#define cgsleep_prepare_r(ts_start) cgtimer_time(ts_start)
void cgsleep_ms_r(cgtimer_t *ts_start, int ms);
void cgsleep_us_r(cgtimer_t *ts_start, int64_t us);
int cgtimer_to_ms(cgtimer_t *cgt);
void cgtimer_sub(cgtimer_t *a, cgtimer_t *b, cgtimer_t *res);
double us_tdiff(struct timeval *end, struct timeval *start);
int ms_tdiff(struct timeval *end, struct timeval *start);
double tdiff(struct timeval *end, struct timeval *start);
bool stratum_send(struct pool *pool, char *s, ssize_t len);
bool sock_full(struct pool *pool);
char *recv_line(struct pool *pool);
bool parse_method(struct pool *pool, char *s);
bool extract_sockaddr(char *url, char **sockaddr_url, char **sockaddr_port);
bool auth_stratum(struct pool *pool);
bool initiate_stratum(struct pool *pool);
bool restart_stratum(struct pool *pool);
void suspend_stratum(struct pool *pool);
void dev_error(struct cgpu_info *dev, enum dev_reason reason);
void *realloc_strcat(char *ptr, char *s);
void *str_text(char *ptr);
void RenameThread(const char* name);
void _cgsem_init(cgsem_t *cgsem, const char *file, const char *func, const int line);
void _cgsem_post(cgsem_t *cgsem, const char *file, const char *func, const int line);
void _cgsem_wait(cgsem_t *cgsem, const char *file, const char *func, const int line);
void _cgsem_destroy(cgsem_t *cgsem);

#define cgsem_init(_sem) _cgsem_init(_sem, __FILE__, __func__, __LINE__)
#define cgsem_post(_sem) _cgsem_post(_sem, __FILE__, __func__, __LINE__)
#define cgsem_wait(_sem) _cgsem_wait(_sem, __FILE__, __func__, __LINE__)
#define cgsem_destroy(_sem) _cgsem_destroy(_sem)

/* Align a size_t to 4 byte boundaries for fussy arches */
static inline void align_len(size_t *len)
{
	if (*len % 4)
		*len += 4 - (*len % 4);
}

#endif /* __UTIL_H__ */
