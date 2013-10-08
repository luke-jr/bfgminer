/*
 * Copyright 2013 Luke Dashjr
 * Copyright 2012-2013 Con Kolivas
 * Copyright 2011 Andrew Smith
 * Copyright 2011 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <curl/curl.h>
#include <jansson.h>

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

extern void gen_hash(unsigned char *data, unsigned char *hash, int len);
extern void hash_data(unsigned char *out_hash, const unsigned char *data);
extern void real_block_target(unsigned char *target, const unsigned char *data);
extern bool hash_target_check(const unsigned char *hash, const unsigned char *target);
extern bool hash_target_check_v(const unsigned char *hash, const unsigned char *target);

bool _stratum_send(struct pool *pool, char *s, ssize_t len, bool force);
#define stratum_send(pool, s, len)  _stratum_send(pool, s, len, false)
bool sock_full(struct pool *pool);
char *recv_line(struct pool *pool);
bool parse_method(struct pool *pool, char *s);
bool extract_sockaddr(struct pool *pool, char *url);
bool auth_stratum(struct pool *pool);
bool initiate_stratum(struct pool *pool);
void suspend_stratum(struct pool *pool);
void dev_error(struct cgpu_info *dev, enum dev_reason reason);
void *realloc_strcat(char *ptr, char *s);
extern char *sanestr(char *o, char *s);
void RenameThread(const char* name);

typedef SOCKETTYPE notifier_t[2];
extern void notifier_init(notifier_t);
extern void notifier_wake(notifier_t);
extern void notifier_read(notifier_t);

/* Align a size_t to 4 byte boundaries for fussy arches */
static inline void align_len(size_t *len)
{
	if (*len % 4)
		*len += 4 - (*len % 4);
}

#endif /* __UTIL_H__ */
