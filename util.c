/*
 * Copyright 2011-2014 Con Kolivas
 * Copyright 2011-2014 Luke Dashjr
 * Copyright 2014 Nate Woolls
 * Copyright 2010-2011 Jeff Garzik
 * Copyright 2012 Giel van Schijndel
 * Copyright 2012 Gavin Andresen
 * Copyright 2013 Lingchao Xu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <jansson.h>
#include <curl/curl.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#ifdef HAVE_SYS_PRCTL_H
# include <sys/prctl.h>
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__)
# include <pthread_np.h>
#endif
#ifndef WIN32
#include <fcntl.h>
# ifdef __linux
#  include <sys/prctl.h>
# endif
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <netdb.h>
#else
# include <windows.h>
# include <winsock2.h>
# include <mstcpip.h>
# include <ws2tcpip.h>
# include <mmsystem.h>
#endif

#include <libbase58.h>
#include <utlist.h>

#ifdef NEED_BFG_LOWL_VCOM
#include "lowl-vcom.h"
#endif
#include "miner.h"
#include "compat.h"
#include "util.h"

#define DEFAULT_SOCKWAIT 60

bool successful_connect = false;
struct timeval nettime;

struct data_buffer {
	void		*buf;
	size_t		len;
	curl_socket_t	*idlemarker;
};

struct upload_buffer {
	const void	*buf;
	size_t		len;
	size_t		pos;
};

struct header_info {
	char		*lp_path;
	int		rolltime;
	char		*reason;
	char		*stratum_url;
	bool		hadrolltime;
	bool		canroll;
	bool		hadexpire;
};

struct tq_ent {
	void			*data;
	struct tq_ent *prev;
	struct tq_ent *next;
};

static void databuf_free(struct data_buffer *db)
{
	if (!db)
		return;

	free(db->buf);
#ifdef DEBUG_DATABUF
	applog(LOG_DEBUG, "databuf_free(%p)", db->buf);
#endif

	memset(db, 0, sizeof(*db));
}

struct json_rpc_call_state {
	struct data_buffer all_data;
	struct header_info hi;
	void *priv;
	char curl_err_str[CURL_ERROR_SIZE];
	struct curl_slist *headers;
	struct upload_buffer upload_data;
	struct pool *pool;
	bool longpoll;
};

// aka data_buffer_write
static size_t all_data_cb(const void *ptr, size_t size, size_t nmemb,
			  void *user_data)
{
	struct data_buffer *db = user_data;
	size_t oldlen, newlen;

	oldlen = db->len;
	if (unlikely(nmemb == 0 || size == 0 || oldlen >= SIZE_MAX - size))
		return 0;
	if (unlikely(nmemb > (SIZE_MAX - oldlen) / size))
		nmemb = (SIZE_MAX - oldlen) / size;

	size_t len = size * nmemb;
	void *newmem;
	static const unsigned char zero = 0;

	if (db->idlemarker) {
		const unsigned char *cptr = ptr;
		for (size_t i = 0; i < len; ++i)
			if (!(isCspace(cptr[i]) || cptr[i] == '{')) {
				*db->idlemarker = CURL_SOCKET_BAD;
				db->idlemarker = NULL;
				break;
			}
	}

	newlen = oldlen + len;

	newmem = realloc(db->buf, newlen + 1);
#ifdef DEBUG_DATABUF
	applog(LOG_DEBUG, "data_buffer_write realloc(%p, %lu) => %p", db->buf, (long unsigned)(newlen + 1), newmem);
#endif
	if (!newmem)
		return 0;

	db->buf = newmem;
	db->len = newlen;
	memcpy(db->buf + oldlen, ptr, len);
	memcpy(db->buf + newlen, &zero, 1);	/* null terminate */

	return nmemb;
}

static size_t upload_data_cb(void *ptr, size_t size, size_t nmemb,
			     void *user_data)
{
	struct json_rpc_call_state * const state = user_data;
	struct upload_buffer * const ub = &state->upload_data;
	unsigned int len = size * nmemb;
	
	if (state->longpoll)
	{
		struct pool * const pool = state->pool;
		pool->lp_active = true;
	}

	if (len > ub->len - ub->pos)
		len = ub->len - ub->pos;

	if (len) {
		memcpy(ptr, ub->buf + ub->pos, len);
		ub->pos += len;
	}

	return len;
}

#if LIBCURL_VERSION_NUM >= 0x071200
static int seek_data_cb(void *user_data, curl_off_t offset, int origin)
{
	struct json_rpc_call_state * const state = user_data;
	struct upload_buffer * const ub = &state->upload_data;
	
	switch (origin) {
		case SEEK_SET:
			if (offset < 0 || offset > ub->len)
				return 1;
			ub->pos = offset;
			break;
		case SEEK_CUR:
			// Check the offset is valid, taking care to avoid overflows or negative unsigned numbers
			if (offset < 0 && ub->pos < (size_t)-offset)
				return 1;
			if (ub->len < offset)
				return 1;
			if (ub->pos > ub->len - offset)
				return 1;
			ub->pos += offset;
			break;
		case SEEK_END:
			if (offset > 0 || (size_t)-offset > ub->len)
				return 1;
			ub->pos = ub->len + offset;
			break;
		default:
			return 1;  /* CURL_SEEKFUNC_FAIL */
	}
	
	return 0;  /* CURL_SEEKFUNC_OK */
}
#endif

static size_t resp_hdr_cb(void *ptr, size_t size, size_t nmemb, void *user_data)
{
	struct header_info *hi = user_data;
	size_t remlen, slen, ptrlen = size * nmemb;
	char *rem, *val = NULL, *key = NULL;
	void *tmp;

	val = calloc(1, ptrlen);
	key = calloc(1, ptrlen);
	if (!key || !val)
		goto out;

	tmp = memchr(ptr, ':', ptrlen);
	if (!tmp || (tmp == ptr))	/* skip empty keys / blanks */
		goto out;
	slen = tmp - ptr;
	if ((slen + 1) == ptrlen)	/* skip key w/ no value */
		goto out;
	memcpy(key, ptr, slen);		/* store & nul term key */
	key[slen] = 0;

	rem = ptr + slen + 1;		/* trim value's leading whitespace */
	remlen = ptrlen - slen - 1;
	while ((remlen > 0) && (isCspace(*rem))) {
		remlen--;
		rem++;
	}

	memcpy(val, rem, remlen);	/* store value, trim trailing ws */
	val[remlen] = 0;
	while ((*val) && (isCspace(val[strlen(val) - 1])))
		val[strlen(val) - 1] = 0;

	if (!*val)			/* skip blank value */
		goto out;

	if (opt_protocol)
		applog(LOG_DEBUG, "HTTP hdr(%s): %s", key, val);

	if (!strcasecmp("X-Roll-Ntime", key)) {
		hi->hadrolltime = true;
		if (!strncasecmp("N", val, 1))
			applog(LOG_DEBUG, "X-Roll-Ntime: N found");
		else {
			hi->canroll = true;

			/* Check to see if expire= is supported and if not, set
			 * the rolltime to the default scantime */
			if (strlen(val) > 7 && !strncasecmp("expire=", val, 7)) {
				sscanf(val + 7, "%d", &hi->rolltime);
				hi->hadexpire = true;
			} else
				hi->rolltime = opt_scantime;
			applog(LOG_DEBUG, "X-Roll-Ntime expiry set to %d", hi->rolltime);
		}
	}

	if (!strcasecmp("X-Long-Polling", key)) {
		hi->lp_path = val;	/* steal memory reference */
		val = NULL;
	}

	if (!strcasecmp("X-Reject-Reason", key)) {
		hi->reason = val;	/* steal memory reference */
		val = NULL;
	}

	if (!strcasecmp("X-Stratum", key)) {
		hi->stratum_url = val;
		val = NULL;
	}

out:
	free(key);
	free(val);
	return ptrlen;
}

static int keep_sockalive(SOCKETTYPE fd)
{
	const int tcp_one = 1;
	const int tcp_keepidle = 45;
	const int tcp_keepintvl = 30;
	int ret = 0;

	if (unlikely(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&tcp_one, sizeof(tcp_one))))
		ret = 1;

#ifndef WIN32
	int flags = fcntl(fd, F_GETFL, 0);

	fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	u_long flags = 1;

	ioctlsocket(fd, FIONBIO, &flags);
#endif

	if (!opt_delaynet)
#ifndef __linux
		if (unlikely(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one))))
#else /* __linux */
		if (unlikely(setsockopt(fd, SOL_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one))))
#endif /* __linux */
			ret = 1;

#ifdef __linux

	if (unlikely(setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &tcp_one, sizeof(tcp_one))))
		ret = 1;

	if (unlikely(setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &tcp_keepidle, sizeof(tcp_keepidle))))
		ret = 1;

	if (unlikely(setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &tcp_keepintvl, sizeof(tcp_keepintvl))))
		ret = 1;
#endif /* __linux */

#ifdef __APPLE_CC__

	if (unlikely(setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &tcp_keepintvl, sizeof(tcp_keepintvl))))
		ret = 1;

#endif /* __APPLE_CC__ */

#ifdef WIN32

	const int zero = 0;
	struct tcp_keepalive vals;
	vals.onoff = 1;
	vals.keepalivetime = tcp_keepidle * 1000;
	vals.keepaliveinterval = tcp_keepintvl * 1000;

	DWORD outputBytes;

	if (unlikely(WSAIoctl(fd, SIO_KEEPALIVE_VALS, &vals, sizeof(vals), NULL, 0, &outputBytes, NULL, NULL)))
		ret = 1;

	/* Windows happily submits indefinitely to the send buffer blissfully
	 * unaware nothing is getting there without gracefully failing unless
	 * we disable the send buffer */
	if (unlikely(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&zero, sizeof(zero))))
		ret = 1;
#endif /* WIN32 */

	return ret;
}

void set_cloexec_socket(SOCKETTYPE sock, const bool cloexec)
{
#ifdef WIN32
	SetHandleInformation((HANDLE)sock, HANDLE_FLAG_INHERIT, cloexec ? 0 : HANDLE_FLAG_INHERIT);
#elif defined(F_GETFD) && defined(F_SETFD) && defined(O_CLOEXEC)
	const int curflags = fcntl(sock, F_GETFD);
	int flags = curflags;
	if (cloexec)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;
	if (flags != curflags)
		fcntl(sock, F_SETFD, flags);
#endif
}

int json_rpc_call_sockopt_cb(void __maybe_unused *userdata, curl_socket_t fd,
			     curlsocktype __maybe_unused purpose)
{
	return keep_sockalive(fd);
}

static void last_nettime(struct timeval *last)
{
	rd_lock(&netacc_lock);
	last->tv_sec = nettime.tv_sec;
	last->tv_usec = nettime.tv_usec;
	rd_unlock(&netacc_lock);
}

static void set_nettime(void)
{
	wr_lock(&netacc_lock);
	cgtime(&nettime);
	wr_unlock(&netacc_lock);
}

static int curl_debug_cb(__maybe_unused CURL *handle, curl_infotype type,
			 char *data, size_t size,
			 void *userdata)
{
	struct pool *pool = (struct pool *)userdata;

	switch(type) {
		case CURLINFO_HEADER_IN:
		case CURLINFO_DATA_IN:
		case CURLINFO_SSL_DATA_IN:
			pool->cgminer_pool_stats.bytes_received += size;
			total_bytes_rcvd += size;
			pool->cgminer_pool_stats.net_bytes_received += size;
			break;
		case CURLINFO_HEADER_OUT:
		case CURLINFO_DATA_OUT:
		case CURLINFO_SSL_DATA_OUT:
			pool->cgminer_pool_stats.bytes_sent += size;
			total_bytes_sent += size;
			pool->cgminer_pool_stats.net_bytes_sent += size;
			break;
		case CURLINFO_TEXT:
		{
			if (!opt_protocol)
				break;
			// data is not null-terminated, so we need to copy and terminate it for applog
			char datacp[size + 1];
			memcpy(datacp, data, size);
			while (likely(size) && unlikely(isCspace(datacp[size-1])))
				--size;
			if (unlikely(!size))
				break;
			datacp[size] = '\0';
			applog(LOG_DEBUG, "Pool %u: %s", pool->pool_no, datacp);
			break;
		}
		default:
			break;
	}
	return 0;
}

json_t *json_web_config(CURL *curl, const char *url)
{
	struct data_buffer all_data = {NULL, 0};
	char curl_err_str[CURL_ERROR_SIZE];
	json_error_t err;
	long timeout = 60;
	json_t *val;
	int rc;

	memset(&err, 0, sizeof(err));

	/* it is assumed that 'curl' is freshly [re]initialized at this pt */

	curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
	curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1);
	
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

	val = NULL;
	rc = curl_easy_perform(curl);
	if (rc) {
		applog(LOG_ERR, "HTTP config request of '%s' failed: %s",
				url, curl_err_str);
		goto c_out;
	}

	if (!all_data.buf) {
		applog(LOG_ERR, "Empty config data received from '%s'",
				url);
		goto c_out;
	}

	val = JSON_LOADS(all_data.buf, &err);
	if (!val) {
		applog(LOG_ERR, "JSON config decode of '%s' failed(%d): %s",
				url, err.line, err.text);
		goto c_out;
	}

c_out:
	databuf_free(&all_data);
	curl_easy_reset(curl);
	return val;
}

void json_rpc_call_async(CURL *curl, const char *url,
		      const char *userpass, const char *rpc_req,
		      bool longpoll,
		      struct pool *pool, bool share,
		      void *priv)
{
	struct json_rpc_call_state *state = malloc(sizeof(struct json_rpc_call_state));
	*state = (struct json_rpc_call_state){
		.priv = priv,
		.pool = pool,
	};
	long timeout = longpoll ? (60 * 60) : 60;
	char len_hdr[64], user_agent_hdr[128];
	struct curl_slist *headers = NULL;

	if (longpoll)
	{
		state->all_data.idlemarker = &pool->lp_socket;
		state->longpoll = true;
	}

	/* it is assumed that 'curl' is freshly [re]initialized at this pt */

	curl_easy_setopt(curl, CURLOPT_PRIVATE, state);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

	/* We use DEBUGFUNCTION to count bytes sent/received, and verbose is needed
	 * to enable it */
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *)pool);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

	/* Shares are staggered already and delays in submission can be costly
	 * so do not delay them */
	if (!opt_delaynet || share)
		curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state->all_data);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_data_cb);
	curl_easy_setopt(curl, CURLOPT_READDATA, state);
#if LIBCURL_VERSION_NUM >= 0x071200
	curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, &seek_data_cb);
	curl_easy_setopt(curl, CURLOPT_SEEKDATA, state);
#endif
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &state->curl_err_str[0]);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, resp_hdr_cb);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state->hi);

	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
	if (pool->rpc_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, pool->rpc_proxy);
	} else if (opt_socks_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, opt_socks_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
	}
	if (userpass) {
		curl_easy_setopt(curl, CURLOPT_USERPWD, userpass);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	}
	if (longpoll)
		curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, json_rpc_call_sockopt_cb);
	curl_easy_setopt(curl, CURLOPT_POST, 1);

	if (opt_protocol)
		applog(LOG_DEBUG, "JSON protocol request:\n%s", rpc_req);

	state->upload_data.buf = rpc_req;
	state->upload_data.len = strlen(rpc_req);
	state->upload_data.pos = 0;
	sprintf(len_hdr, "Content-Length: %lu",
		(unsigned long) state->upload_data.len);
	sprintf(user_agent_hdr, "User-Agent: %s", bfgminer_name_slash_ver);

	headers = curl_slist_append(headers,
		"Content-type: application/json");
	headers = curl_slist_append(headers,
		"X-Mining-Extensions: longpoll midstate rollntime submitold");

	if (longpoll)
		headers = curl_slist_append(headers,
			"X-Minimum-Wait: 0");

	if (likely(global_hashrate)) {
		char ghashrate[255];

		sprintf(ghashrate, "X-Mining-Hashrate: %"PRIu64, (uint64_t)global_hashrate);
		headers = curl_slist_append(headers, ghashrate);
	}

	headers = curl_slist_append(headers, len_hdr);
	headers = curl_slist_append(headers, user_agent_hdr);
	headers = curl_slist_append(headers, "Expect:"); /* disable Expect hdr*/

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	state->headers = headers;

	if (opt_delaynet) {
		/* Don't delay share submission, but still track the nettime */
		if (!share) {
			long long now_msecs, last_msecs;
			struct timeval now, last;

			cgtime(&now);
			last_nettime(&last);
			now_msecs = (long long)now.tv_sec * 1000;
			now_msecs += now.tv_usec / 1000;
			last_msecs = (long long)last.tv_sec * 1000;
			last_msecs += last.tv_usec / 1000;
			if (now_msecs > last_msecs && now_msecs - last_msecs < 250) {
				struct timespec rgtp;

				rgtp.tv_sec = 0;
				rgtp.tv_nsec = (250 - (now_msecs - last_msecs)) * 1000000;
				nanosleep(&rgtp, NULL);
			}
		}
		set_nettime();
	}
}

json_t *json_rpc_call_completed(CURL *curl, int rc, bool probe, int *rolltime, void *out_priv)
{
	struct json_rpc_call_state *state;
	if (curl_easy_getinfo(curl, CURLINFO_PRIVATE, (void*)&state) != CURLE_OK) {
		applog(LOG_ERR, "Failed to get private curl data");
		if (out_priv)
			*(void**)out_priv = NULL;
		goto err_out;
	}
	if (out_priv)
		*(void**)out_priv = state->priv;

	json_t *val, *err_val, *res_val;
	json_error_t err;
	struct pool *pool = state->pool;
	bool probing = probe && !pool->probed;

	if (rc) {
		applog(LOG_DEBUG, "HTTP request failed: %s", state->curl_err_str);
		goto err_out;
	}

	if (!state->all_data.buf) {
		applog(LOG_DEBUG, "Empty data received in json_rpc_call.");
		goto err_out;
	}

	pool->cgminer_pool_stats.times_sent++;
	pool->cgminer_pool_stats.times_received++;

	if (probing) {
		pool->probed = true;
		/* If X-Long-Polling was found, activate long polling */
		if (state->hi.lp_path) {
			if (pool->hdr_path != NULL)
				free(pool->hdr_path);
			pool->hdr_path = state->hi.lp_path;
		} else
			pool->hdr_path = NULL;
		if (state->hi.stratum_url) {
			pool->stratum_url = state->hi.stratum_url;
			state->hi.stratum_url = NULL;
		}
	} else {
		if (state->hi.lp_path) {
			free(state->hi.lp_path);
			state->hi.lp_path = NULL;
		}
		if (state->hi.stratum_url) {
			free(state->hi.stratum_url);
			state->hi.stratum_url = NULL;
		}
	}

	if (pool->force_rollntime)
	{
		state->hi.canroll = true;
		state->hi.hadexpire = true;
		state->hi.rolltime = pool->force_rollntime;
	}
	
	if (rolltime)
		*rolltime = state->hi.rolltime;
	pool->cgminer_pool_stats.rolltime = state->hi.rolltime;
	pool->cgminer_pool_stats.hadrolltime = state->hi.hadrolltime;
	pool->cgminer_pool_stats.canroll = state->hi.canroll;
	pool->cgminer_pool_stats.hadexpire = state->hi.hadexpire;

	val = JSON_LOADS(state->all_data.buf, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);

		if (opt_protocol)
			applog(LOG_DEBUG, "JSON protocol response:\n%s", (char*)state->all_data.buf);

		goto err_out;
	}

	if (opt_protocol) {
		char *s = json_dumps(val, JSON_INDENT(3));

		applog(LOG_DEBUG, "JSON protocol response:\n%s", s);
		free(s);
	}

	/* JSON-RPC valid response returns a non-null 'result',
	 * and a null 'error'.
	 */
	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val ||(err_val && !json_is_null(err_val))) {
		char *s;

		if (err_val)
			s = json_dumps(err_val, JSON_INDENT(3));
		else
			s = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC call failed: %s", s);

		free(s);
		json_decref(val);

		goto err_out;
	}

	if (state->hi.reason) {
		json_object_set_new(val, "reject-reason", json_string(state->hi.reason));
		free(state->hi.reason);
		state->hi.reason = NULL;
	}
	successful_connect = true;
	databuf_free(&state->all_data);
	curl_slist_free_all(state->headers);
	curl_easy_reset(curl);
	free(state);
	return val;

err_out:
	databuf_free(&state->all_data);
	curl_slist_free_all(state->headers);
	curl_easy_reset(curl);
	if (!successful_connect)
		applog(LOG_DEBUG, "Failed to connect in json_rpc_call");
	curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
	free(state);
	return NULL;
}

json_t *json_rpc_call(CURL *curl, const char *url,
		      const char *userpass, const char *rpc_req,
		      bool probe, bool longpoll, int *rolltime,
		      struct pool *pool, bool share)
{
	json_rpc_call_async(curl, url, userpass, rpc_req, longpoll, pool, share, NULL);
	int rc = curl_easy_perform(curl);
	return json_rpc_call_completed(curl, rc, probe, rolltime, NULL);
}

bool our_curl_supports_proxy_uris()
{
	curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
	return data->age && data->version_num >= (( 7 <<16)|( 21 <<8)| 7);  // 7.21.7
}

// NOTE: This assumes reference URI is a root
char *absolute_uri(char *uri, const char *ref)
{
	if (strstr(uri, "://"))
		return strdup(uri);

	char *copy_start, *abs;
	bool need_slash = false;

	copy_start = (uri[0] == '/') ? &uri[1] : uri;
	if (ref[strlen(ref) - 1] != '/')
		need_slash = true;

	abs = malloc(strlen(ref) + strlen(copy_start) + 2);
	if (!abs) {
		applog(LOG_ERR, "Malloc failure in absolute_uri");
		return NULL;
	}

	sprintf(abs, "%s%s%s", ref, need_slash ? "/" : "", copy_start);

	return abs;
}

static const char _hexchars[0x10] = "0123456789abcdef";

void bin2hex(char *out, const void *in, size_t len)
{
	const unsigned char *p = in;
	while (len--)
	{
		(out++)[0] = _hexchars[p[0] >> 4];
		(out++)[0] = _hexchars[p[0] & 0xf];
		++p;
	}
	out[0] = '\0';
}

static inline
int _hex2bin_char(const char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return (c - 'a') + 10;
	if (c >= 'A' && c <= 'F')
		return (c - 'A') + 10;
	return -1;
}

/* Does the reverse of bin2hex but does not allocate any ram */
bool hex2bin(unsigned char *p, const char *hexstr, size_t len)
{
	int n, o;
	
	while (len--)
	{
		n = _hex2bin_char((hexstr++)[0]);
		if (unlikely(n == -1))
		{
badchar:
			if (!hexstr[-1])
				applog(LOG_ERR, "hex2bin: str truncated");
			else
				applog(LOG_ERR, "hex2bin: invalid character 0x%02x", (int)hexstr[-1]);
			return false;
		}
		o = _hex2bin_char((hexstr++)[0]);
		if (unlikely(o == -1))
			goto badchar;
		(p++)[0] = (n << 4) | o;
	}
	
	return likely(!hexstr[0]);
}

size_t ucs2_to_utf8(char * const out, const uint16_t * const in, const size_t sz)
{
	uint8_t *p = (void*)out;
	for (int i = 0; i < sz; ++i)
	{
		const uint16_t c = in[i];
		if (c < 0x80)
			p++[0] = c;
		else
		{
			if (c < 0x800)
				p++[0] = 0xc0 | (c >> 6);
			else
			{
				p++[0] = 0xe0 | (c >> 12);
				p++[0] = 0x80 | ((c >> 6) & 0x3f);
			}
			p++[0] = 0x80 | (c & 0x3f);
		}
	}
	return p - (uint8_t*)(void*)out;
}

char *ucs2_to_utf8_dup(uint16_t * const in, size_t sz)
{
	char * const out = malloc((sz * 4) + 1);
	sz = ucs2_to_utf8(out, in, sz);
	out[sz] = '\0';
	return out;
}

// Example output: 0000000000000000000000000000000000000000000000000000ffff00000000 (bdiff 1)
void real_block_target(unsigned char *target, const unsigned char *data)
{
	uint8_t targetshift;

	if (unlikely(data[72] < 3 || data[72] > 0x20))
	{
		// Invalid (out of bounds) target
		memset(target, 0xff, 32);
		return;
	}

	targetshift = data[72] - 3;
	memset(target, 0, targetshift);
	target[targetshift++] = data[75];
	target[targetshift++] = data[74];
	target[targetshift++] = data[73];
	memset(&target[targetshift], 0, 0x20 - targetshift);
}

bool hash_target_check(const unsigned char *hash, const unsigned char *target)
{
	const uint32_t *h32 = (uint32_t*)&hash[0];
	const uint32_t *t32 = (uint32_t*)&target[0];
	for (int i = 7; i >= 0; --i) {
		uint32_t h32i = le32toh(h32[i]);
		uint32_t t32i = le32toh(t32[i]);
		if (h32i > t32i)
			return false;
		if (h32i < t32i)
			return true;
	}
	return true;
}

bool hash_target_check_v(const unsigned char *hash, const unsigned char *target)
{
	bool rc;

	rc = hash_target_check(hash, target);

	if (opt_debug) {
		unsigned char hash_swap[32], target_swap[32];
		char hash_str[65];
		char target_str[65];

		for (int i = 0; i < 32; ++i) {
			hash_swap[i] = hash[31-i];
			target_swap[i] = target[31-i];
		}

		bin2hex(hash_str, hash_swap, 32);
		bin2hex(target_str, target_swap, 32);

		applog(LOG_DEBUG, " Proof: %s\nTarget: %s\nTrgVal? %s",
			hash_str,
			target_str,
			rc ? "YES (hash <= target)" :
			     "no (false positive; hash > target)");
	}

	return rc;
}

struct thread_q *tq_new(void)
{
	struct thread_q *tq;

	tq = calloc(1, sizeof(*tq));
	if (!tq)
		return NULL;

	pthread_mutex_init(&tq->mutex, NULL);
	pthread_cond_init(&tq->cond, bfg_condattr);

	return tq;
}

void tq_free(struct thread_q *tq)
{
	struct tq_ent *ent, *iter;

	if (!tq)
		return;

	DL_FOREACH_SAFE(tq->q, ent, iter) {
		DL_DELETE(tq->q, ent);
		free(ent);
	}

	pthread_cond_destroy(&tq->cond);
	pthread_mutex_destroy(&tq->mutex);

	memset(tq, 0, sizeof(*tq));	/* poison */
	free(tq);
}

static void tq_freezethaw(struct thread_q *tq, bool frozen)
{
	mutex_lock(&tq->mutex);
	tq->frozen = frozen;
	pthread_cond_signal(&tq->cond);
	mutex_unlock(&tq->mutex);
}

void tq_freeze(struct thread_q *tq)
{
	tq_freezethaw(tq, true);
}

void tq_thaw(struct thread_q *tq)
{
	tq_freezethaw(tq, false);
}

bool tq_push(struct thread_q *tq, void *data)
{
	struct tq_ent *ent;
	bool rc = true;

	ent = calloc(1, sizeof(*ent));
	if (!ent)
		return false;

	ent->data = data;

	mutex_lock(&tq->mutex);
	if (!tq->frozen) {
		DL_APPEND(tq->q, ent);
	} else {
		free(ent);
		rc = false;
	}
	pthread_cond_signal(&tq->cond);
	mutex_unlock(&tq->mutex);

	return rc;
}

void *tq_pop(struct thread_q * const tq)
{
	struct tq_ent *ent;
	void *rval = NULL;
	int rc;

	mutex_lock(&tq->mutex);
	if (tq->q)
		goto pop;

	rc = pthread_cond_wait(&tq->cond, &tq->mutex);
	if (rc)
		goto out;
	if (!tq->q)
		goto out;
pop:
	ent = tq->q;
	rval = ent->data;

	DL_DELETE(tq->q, ent);
	free(ent);
out:
	mutex_unlock(&tq->mutex);

	return rval;
}

int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg)
{
	int rv = pthread_create(&thr->pth, attr, start, arg);
	if (likely(!rv))
		thr->has_pth = true;
	return rv;
}

void thr_info_freeze(struct thr_info *thr)
{
	struct tq_ent *ent, *iter;
	struct thread_q *tq;

	if (!thr)
		return;

	tq = thr->q;
	if (!tq)
		return;

	mutex_lock(&tq->mutex);
	tq->frozen = true;
	DL_FOREACH_SAFE(tq->q, ent, iter) {
		DL_DELETE(tq->q, ent);
		free(ent);
	}
	mutex_unlock(&tq->mutex);
}

void thr_info_cancel(struct thr_info *thr)
{
	if (!thr)
		return;

	if (thr->has_pth) {
		pthread_cancel(thr->pth);
		thr->has_pth = false;
	}
}

#ifndef HAVE_PTHREAD_CANCEL

// Bionic (Android) is intentionally missing pthread_cancel, so it is implemented using pthread_kill

enum pthread_cancel_workaround_mode {
	PCWM_DEFAULT   = 0,
	PCWM_TERMINATE = 1,
	PCWM_ASYNC     = 2,
	PCWM_DISABLED  = 4,
	PCWM_CANCELLED = 8,
};

static pthread_key_t key_pcwm;
struct sigaction pcwm_orig_term_handler;

static
void do_pthread_cancel_exit(int flags)
{
	if (!(flags & PCWM_ASYNC))
		// NOTE: Logging disables cancel while mutex held, so this is safe
		applog(LOG_WARNING, "pthread_cancel workaround: Cannot defer cancellation, terminating thread NOW");
	pthread_exit(PTHREAD_CANCELED);
}

static
void sighandler_pthread_cancel(int sig)
{
	int flags = (int)pthread_getspecific(key_pcwm);
	if (flags & PCWM_TERMINATE)  // Main thread
	{
		// Restore original handler and call it
		if (sigaction(sig, &pcwm_orig_term_handler, NULL))
			quit(1, "pthread_cancel workaround: Failed to restore original handler");
		raise(SIGTERM);
		quit(1, "pthread_cancel workaround: Original handler returned");
	}
	if (flags & PCWM_CANCELLED)  // Already pending cancel
		return;
	if (flags & PCWM_DISABLED)
	{
		flags |= PCWM_CANCELLED;
		if (pthread_setspecific(key_pcwm, (void*)flags))
			quit(1, "pthread_cancel workaround: pthread_setspecific failed (setting PCWM_CANCELLED)");
		return;
	}
	do_pthread_cancel_exit(flags);
}

void pthread_testcancel(void)
{
	int flags = (int)pthread_getspecific(key_pcwm);
	if (flags & PCWM_CANCELLED && !(flags & PCWM_DISABLED))
		do_pthread_cancel_exit(flags);
}

int pthread_setcancelstate(int state, int *oldstate)
{
	int flags = (int)pthread_getspecific(key_pcwm);
	if (oldstate)
		*oldstate = (flags & PCWM_DISABLED) ? PTHREAD_CANCEL_DISABLE : PTHREAD_CANCEL_ENABLE;
	if (state == PTHREAD_CANCEL_DISABLE)
		flags |= PCWM_DISABLED;
	else
	{
		if (flags & PCWM_CANCELLED)
			do_pthread_cancel_exit(flags);
		flags &= ~PCWM_DISABLED;
	}
	if (pthread_setspecific(key_pcwm, (void*)flags))
		return -1;
	return 0;
}

int pthread_setcanceltype(int type, int *oldtype)
{
	int flags = (int)pthread_getspecific(key_pcwm);
	if (oldtype)
		*oldtype = (flags & PCWM_ASYNC) ? PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED;
	if (type == PTHREAD_CANCEL_ASYNCHRONOUS)
		flags |= PCWM_ASYNC;
	else
		flags &= ~PCWM_ASYNC;
	if (pthread_setspecific(key_pcwm, (void*)flags))
		return -1;
	return 0;
}

void setup_pthread_cancel_workaround()
{
	if (pthread_key_create(&key_pcwm, NULL))
		quit(1, "pthread_cancel workaround: pthread_key_create failed");
	if (pthread_setspecific(key_pcwm, (void*)PCWM_TERMINATE))
		quit(1, "pthread_cancel workaround: pthread_setspecific failed");
	struct sigaction new_sigact = {
		.sa_handler = sighandler_pthread_cancel,
	};
	if (sigaction(SIGTERM, &new_sigact, &pcwm_orig_term_handler))
		quit(1, "pthread_cancel workaround: Failed to install SIGTERM handler");
}

#endif

static void _now_gettimeofday(struct timeval *);
static void _cgsleep_us_r_nanosleep(cgtimer_t *, int64_t);

#ifdef HAVE_POOR_GETTIMEOFDAY
static struct timeval tv_timeofday_offset;
static struct timeval _tv_timeofday_lastchecked;
static pthread_mutex_t _tv_timeofday_mutex = PTHREAD_MUTEX_INITIALIZER;

static
void bfg_calibrate_timeofday(struct timeval *expected, char *buf)
{
	struct timeval actual, delta;
	timeradd(expected, &tv_timeofday_offset, expected);
	_now_gettimeofday(&actual);
	if (expected->tv_sec >= actual.tv_sec - 1 && expected->tv_sec <= actual.tv_sec + 1)
		// Within reason - no change necessary
		return;
	
	timersub(&actual, expected, &delta);
	timeradd(&tv_timeofday_offset, &delta, &tv_timeofday_offset);
	sprintf(buf, "Recalibrating timeofday offset (delta %ld.%06lds)", (long)delta.tv_sec, (long)delta.tv_usec);
	*expected = actual;
}

void bfg_gettimeofday(struct timeval *out)
{
	char buf[64] = "";
	timer_set_now(out);
	mutex_lock(&_tv_timeofday_mutex);
	if (_tv_timeofday_lastchecked.tv_sec < out->tv_sec - 21)
		bfg_calibrate_timeofday(out, buf);
	else
		timeradd(out, &tv_timeofday_offset, out);
	mutex_unlock(&_tv_timeofday_mutex);
	if (unlikely(buf[0]))
		applog(LOG_WARNING, "%s", buf);
}
#endif

#ifdef WIN32
static LARGE_INTEGER _perffreq;

static
void _now_queryperformancecounter(struct timeval *tv)
{
	LARGE_INTEGER now;
	if (unlikely(!QueryPerformanceCounter(&now)))
		quit(1, "QueryPerformanceCounter failed");
	
	*tv = (struct timeval){
		.tv_sec = now.QuadPart / _perffreq.QuadPart,
		.tv_usec = (now.QuadPart % _perffreq.QuadPart) * 1000000 / _perffreq.QuadPart,
	};
}
#endif

static void bfg_init_time();

static
void _now_is_not_set(__maybe_unused struct timeval *tv)
{
	bfg_init_time();
	timer_set_now(tv);
}

void (*timer_set_now)(struct timeval *tv) = _now_is_not_set;
void (*cgsleep_us_r)(cgtimer_t *, int64_t) = _cgsleep_us_r_nanosleep;

#ifdef HAVE_CLOCK_GETTIME_MONOTONIC
static clockid_t bfg_timer_clk;

static
void _now_clock_gettime(struct timeval *tv)
{
	struct timespec ts;
	if (unlikely(clock_gettime(bfg_timer_clk, &ts)))
		quit(1, "clock_gettime failed");
	
	*tv = (struct timeval){
		.tv_sec = ts.tv_sec,
		.tv_usec = ts.tv_nsec / 1000,
	};
}

#ifdef HAVE_CLOCK_NANOSLEEP
static
void _cgsleep_us_r_monotonic(cgtimer_t *tv_start, int64_t us)
{
	struct timeval tv_end[1];
	struct timespec ts_end[1];
	int ret;
	
	timer_set_delay(tv_end, tv_start, us);
	timeval_to_spec(ts_end, tv_end);
	do {
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts_end, NULL);
	} while (ret == EINTR);
}
#endif

static
bool _bfg_try_clock_gettime(clockid_t clk)
{
	struct timespec ts;
	if (clock_gettime(clk, &ts))
		return false;
	
	bfg_timer_clk = clk;
	timer_set_now = _now_clock_gettime;
	return true;
}
#endif

pthread_condattr_t *bfg_condattr_()
{
	return NULL;
}

static
void bfg_init_time()
{
	if (timer_set_now != _now_is_not_set)
		return;
	
#ifdef HAVE_CLOCK_GETTIME_MONOTONIC
#ifdef HAVE_CLOCK_GETTIME_MONOTONIC_RAW
	if (_bfg_try_clock_gettime(CLOCK_MONOTONIC_RAW))
		applog(LOG_DEBUG, "Timers: Using clock_gettime(CLOCK_MONOTONIC_RAW)");
	else
#endif
	if (_bfg_try_clock_gettime(CLOCK_MONOTONIC))
	{
		applog(LOG_DEBUG, "Timers: Using clock_gettime(CLOCK_MONOTONIC)");
#ifdef HAVE_CLOCK_NANOSLEEP
		cgsleep_us_r = _cgsleep_us_r_monotonic;
#endif
	}
	else
#endif
#ifdef WIN32
	if (QueryPerformanceFrequency(&_perffreq) && _perffreq.QuadPart)
	{
		timer_set_now = _now_queryperformancecounter;
		applog(LOG_DEBUG, "Timers: Using QueryPerformanceCounter");
	}
	else
#endif
	{
		timer_set_now = _now_gettimeofday;
		applog(LOG_DEBUG, "Timers: Using gettimeofday");
	}
	
#ifdef HAVE_POOR_GETTIMEOFDAY
	char buf[64] = "";
	struct timeval tv;
	timer_set_now(&tv);
	bfg_calibrate_timeofday(&tv, buf);
	applog(LOG_DEBUG, "%s", buf);
#endif
}

void subtime(struct timeval *a, struct timeval *b)
{
	timersub(a, b, b);
}

void addtime(struct timeval *a, struct timeval *b)
{
	timeradd(a, b, b);
}

bool time_more(struct timeval *a, struct timeval *b)
{
	return timercmp(a, b, >);
}

bool time_less(struct timeval *a, struct timeval *b)
{
	return timercmp(a, b, <);
}

void copy_time(struct timeval *dest, const struct timeval *src)
{
	memcpy(dest, src, sizeof(struct timeval));
}

void timespec_to_val(struct timeval *val, const struct timespec *spec)
{
	val->tv_sec = spec->tv_sec;
	val->tv_usec = spec->tv_nsec / 1000;
}

void timeval_to_spec(struct timespec *spec, const struct timeval *val)
{
	spec->tv_sec = val->tv_sec;
	spec->tv_nsec = val->tv_usec * 1000;
}

void us_to_timeval(struct timeval *val, int64_t us)
{
	lldiv_t tvdiv = lldiv(us, 1000000);

	val->tv_sec = tvdiv.quot;
	val->tv_usec = tvdiv.rem;
}

void us_to_timespec(struct timespec *spec, int64_t us)
{
	lldiv_t tvdiv = lldiv(us, 1000000);

	spec->tv_sec = tvdiv.quot;
	spec->tv_nsec = tvdiv.rem * 1000;
}

void ms_to_timespec(struct timespec *spec, int64_t ms)
{
	lldiv_t tvdiv = lldiv(ms, 1000);

	spec->tv_sec = tvdiv.quot;
	spec->tv_nsec = tvdiv.rem * 1000000;
}

void timeraddspec(struct timespec *a, const struct timespec *b)
{
	a->tv_sec += b->tv_sec;
	a->tv_nsec += b->tv_nsec;
	if (a->tv_nsec >= 1000000000) {
		a->tv_nsec -= 1000000000;
		a->tv_sec++;
	}
}

#ifndef WIN32
static
void _now_gettimeofday(struct timeval *tv)
{
	gettimeofday(tv, NULL);
}
#else
/* Windows start time is since 1601 lol so convert it to unix epoch 1970. */
#define EPOCHFILETIME (116444736000000000LL)

void _now_gettimeofday(struct timeval *tv)
{
	FILETIME ft;
	LARGE_INTEGER li;

	GetSystemTimeAsFileTime(&ft);
	li.LowPart  = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	li.QuadPart -= EPOCHFILETIME;

	/* SystemTime is in decimicroseconds so divide by an unusual number */
	tv->tv_sec  = li.QuadPart / 10000000;
	tv->tv_usec = li.QuadPart % 10000000;
}
#endif

void cgsleep_ms_r(cgtimer_t *tv_start, int ms)
{
	cgsleep_us_r(tv_start, ((int64_t)ms) * 1000);
}

static
void _cgsleep_us_r_nanosleep(cgtimer_t *tv_start, int64_t us)
{
	struct timeval tv_timer[1], tv[1];
	struct timespec ts[1];
	
	timer_set_delay(tv_timer, tv_start, us);
	while (true)
	{
		timer_set_now(tv);
		if (!timercmp(tv_timer, tv, >))
			return;
		timersub(tv_timer, tv, tv);
		timeval_to_spec(ts, tv);
		nanosleep(ts, NULL);
	}
}

void cgsleep_ms(int ms)
{
	cgtimer_t ts_start;

	cgsleep_prepare_r(&ts_start);
	cgsleep_ms_r(&ts_start, ms);
}

void cgsleep_us(int64_t us)
{
	cgtimer_t ts_start;

	cgsleep_prepare_r(&ts_start);
	cgsleep_us_r(&ts_start, us);
}

/* Returns the microseconds difference between end and start times as a double */
double us_tdiff(struct timeval *end, struct timeval *start)
{
	return end->tv_sec * 1000000 + end->tv_usec - start->tv_sec * 1000000 - start->tv_usec;
}

/* Returns the seconds difference between end and start times as a double */
double tdiff(struct timeval *end, struct timeval *start)
{
	return end->tv_sec - start->tv_sec + (end->tv_usec - start->tv_usec) / 1000000.0;
}


int double_find_precision(double f, const double base)
{
	int rv = 0;
	for ( ; floor(f) != f; ++rv)
		f *= base;
	return rv;
}


int utf8_len(const uint8_t b)
{
	if (!(b & 0x80))
		return 1;
	if (!(b & 0x20))
		return 2;
	else
	if (!(b & 0x10))
		return 3;
	else
		return 4;
}

int32_t utf8_decode(const void *b, int *out_len)
{
	int32_t w;
	const unsigned char *s = b;
	
	*out_len = utf8_len(s[0]);
	
	if (*out_len == 1)
		// ASCII
		return s[0];
	
#ifdef STRICT_UTF8
	if (unlikely(!(s[0] & 0x40)))
		goto invalid;
	if (unlikely(s[0] & 0x38 == 0x38))
		goto invalid;
#endif
	
	w = s[0] & ((2 << (6 - *out_len)) - 1);
	for (int i = 1; i < *out_len; ++i)
	{
#ifdef STRICT_UTF8
		if (unlikely((s[i] & 0xc0) != 0x80))
			goto invalid;
#endif
		w = (w << 6) | (s[i] & 0x3f);
	}
	
#if defined(STRICT_UTF8)
	if (unlikely(w > 0x10FFFF))
		goto invalid;
	
	// FIXME: UTF-8 requires smallest possible encoding; check it
#endif
	
	return w;

#ifdef STRICT_UTF8
invalid:
	*out_len = 1;
	return REPLACEMENT_CHAR;
#endif
}

size_t utf8_strlen(const void * const b)
{
	const uint8_t *s = b;
	size_t c = 0;
	int clen, i;
	while (s[0])
	{
		clen = utf8_len(s[0]);
		for (i = 0; i < clen; ++i)
			if (!s[i])
				clen = 1;
		++c;
		s += clen;
	}
	return c;
}

static
void _utf8_test(const char *s, const wchar_t expected, int expectedlen)
{
	int len;
	wchar_t r;
	
	if (expected != REPLACEMENT_CHAR)
	{
		len = utf8_len(((uint8_t*)s)[0]);
		if (len != expectedlen)
		{
			++unittest_failures;
			applog(LOG_ERR, "UTF-8 test U+%06lX (len %d) failed: got utf8_len=>%d", (unsigned long)expected, expectedlen, len);
		}
		len = utf8_strlen(s);
		if (len != (s[0] ? 1 : 0))
		{
			++unittest_failures;
			applog(LOG_ERR, "UTF-8 test U+%06lX (len %d) failed: got utf8_strlen=>%d", (unsigned long)expected, expectedlen, len);
		}
		len = -1;
	}
	
	r = utf8_decode(s, &len);
	if (unlikely(r != expected || expectedlen != len))
	{
		++unittest_failures;
		applog(LOG_ERR, "UTF-8 test U+%06lX (len %d) failed: got U+%06lX (len %d)", (unsigned long)expected, expectedlen, (unsigned long)r, len);
	}
}
#define _test_intrange(s, ...)  _test_intrange(s, (int[]){ __VA_ARGS__ })

void utf8_test()
{
	_utf8_test("", 0, 1);
	_utf8_test("\1", 1, 1);
	_utf8_test("\x7f", 0x7f, 1);
#if WCHAR_MAX >= 0x80
	_utf8_test("\xc2\x80", 0x80, 2);
#if WCHAR_MAX >= 0xff
	_utf8_test("\xc3\xbf", 0xff, 2);
#if WCHAR_MAX >= 0x7ff
	_utf8_test("\xdf\xbf", 0x7ff, 2);
#if WCHAR_MAX >= 0x800
	_utf8_test("\xe0\xa0\x80", 0x800, 3);
#if WCHAR_MAX >= 0xffff
	_utf8_test("\xef\xbf\xbf", 0xffff, 3);
#if WCHAR_MAX >= 0x10000
	_utf8_test("\xf0\x90\x80\x80", 0x10000, 4);
#if WCHAR_MAX >= 0x10ffff
	_utf8_test("\xf4\x8f\xbf\xbf", 0x10ffff, 4);
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#ifdef STRICT_UTF8
	_utf8_test("\x80", REPLACEMENT_CHAR, 1);
	_utf8_test("\xbf", REPLACEMENT_CHAR, 1);
	_utf8_test("\xfe", REPLACEMENT_CHAR, 1);
	_utf8_test("\xff", REPLACEMENT_CHAR, 1);
#endif
}


bool extract_sockaddr(char *url, char **sockaddr_url, char **sockaddr_port)
{
	char *url_begin, *url_end, *ipv6_begin, *ipv6_end, *port_start = NULL;
	char url_address[256], port[6];
	int url_len, port_len = 0;

	url_begin = strstr(url, "//");
	if (!url_begin)
		url_begin = url;
	else
		url_begin += 2;

	/* Look for numeric ipv6 entries */
	ipv6_begin = strstr(url_begin, "[");
	ipv6_end = strstr(url_begin, "]");
	if (ipv6_begin && ipv6_end && ipv6_end > ipv6_begin)
		url_end = strstr(ipv6_end, ":");
	else
		url_end = strstr(url_begin, ":");
	if (url_end) {
		url_len = url_end - url_begin;
		port_len = strlen(url_begin) - url_len - 1;
		if (port_len < 1)
			return false;
		port_start = url_end + 1;
	} else
		url_len = strlen(url_begin);

	if (url_len < 1)
		return false;
	
	if (url_len >= sizeof(url_address))
	{
		applog(LOG_WARNING, "%s: Truncating overflowed address '%.*s'",
		       __func__, url_len, url_begin);
		url_len = sizeof(url_address) - 1;
	}

	sprintf(url_address, "%.*s", url_len, url_begin);

	if (port_len) {
		char *slash;

		snprintf(port, 6, "%.*s", port_len, port_start);
		slash = strchr(port, '/');
		if (slash)
			*slash = '\0';
	} else
		strcpy(port, "80");

	free(*sockaddr_port);
	*sockaddr_port = strdup(port);
	free(*sockaddr_url);
	*sockaddr_url = strdup(url_address);

	return true;
}

enum send_ret {
	SEND_OK,
	SEND_SELECTFAIL,
	SEND_SENDFAIL,
	SEND_INACTIVE
};

/* Send a single command across a socket, appending \n to it. This should all
 * be done under stratum lock except when first establishing the socket */
static enum send_ret __stratum_send(struct pool *pool, char *s, ssize_t len)
{
	SOCKETTYPE sock = pool->sock;
	ssize_t ssent = 0;

	strcat(s, "\n");
	len++;

	while (len > 0 ) {
		struct timeval timeout = {1, 0};
		size_t sent = 0;
		CURLcode rc;
		fd_set wd;
retry:
		FD_ZERO(&wd);
		FD_SET(sock, &wd);
		if (select(sock + 1, NULL, &wd, NULL, &timeout) < 1) {
			if (interrupted())
				goto retry;
			return SEND_SELECTFAIL;
		}
		rc = curl_easy_send(pool->stratum_curl, s + ssent, len, &sent);
		if (rc != CURLE_OK)
		{
			if (rc != CURLE_AGAIN)
				return SEND_SENDFAIL;
			sent = 0;
		}
		ssent += sent;
		len -= sent;
	}

	pool->cgminer_pool_stats.times_sent++;
	pool->cgminer_pool_stats.bytes_sent += ssent;
	total_bytes_sent += ssent;
	pool->cgminer_pool_stats.net_bytes_sent += ssent;
	return SEND_OK;
}

bool _stratum_send(struct pool *pool, char *s, ssize_t len, bool force)
{
	enum send_ret ret = SEND_INACTIVE;

	if (opt_protocol)
		applog(LOG_DEBUG, "Pool %u: SEND: %s", pool->pool_no, s);

	mutex_lock(&pool->stratum_lock);
	if (pool->stratum_active || force)
		ret = __stratum_send(pool, s, len);
	mutex_unlock(&pool->stratum_lock);

	/* This is to avoid doing applog under stratum_lock */
	switch (ret) {
		default:
		case SEND_OK:
			break;
		case SEND_SELECTFAIL:
			applog(LOG_DEBUG, "Write select failed on pool %d sock", pool->pool_no);
			suspend_stratum(pool);
			break;
		case SEND_SENDFAIL:
			applog(LOG_DEBUG, "Failed to send in stratum_send");
			suspend_stratum(pool);
			break;
		case SEND_INACTIVE:
			applog(LOG_DEBUG, "Stratum send failed due to no pool stratum_active");
			break;
	}
	return (ret == SEND_OK);
}

static bool socket_full(struct pool *pool, int wait)
{
	SOCKETTYPE sock = pool->sock;
	struct timeval timeout;
	fd_set rd;

	if (sock == INVSOCK)
		return true;
	
	if (unlikely(wait < 0))
		wait = 0;
	FD_ZERO(&rd);
	FD_SET(sock, &rd);
	timeout.tv_usec = 0;
	timeout.tv_sec = wait;
	if (select(sock + 1, &rd, NULL, NULL, &timeout) > 0)
		return true;
	return false;
}

/* Check to see if Santa's been good to you */
bool sock_full(struct pool *pool)
{
	if (strlen(pool->sockbuf))
		return true;

	return (socket_full(pool, 0));
}

static void clear_sockbuf(struct pool *pool)
{
	strcpy(pool->sockbuf, "");
}

static void clear_sock(struct pool *pool)
{
	size_t n = 0;

	mutex_lock(&pool->stratum_lock);
	do {
		n = 0;
		if (pool->sock)
			curl_easy_recv(pool->stratum_curl, pool->sockbuf, RECVSIZE, &n);
	} while (n > 0);
	mutex_unlock(&pool->stratum_lock);

	clear_sockbuf(pool);
}

/* Make sure the pool sockbuf is large enough to cope with any coinbase size
 * by reallocing it to a large enough size rounded up to a multiple of RBUFSIZE
 * and zeroing the new memory */
static void recalloc_sock(struct pool *pool, size_t len)
{
	size_t old, new;

	old = strlen(pool->sockbuf);
	new = old + len + 1;
	if (new < pool->sockbuf_size)
		return;
	new = new + (RBUFSIZE - (new % RBUFSIZE));
	// Avoid potentially recursive locking
	// applog(LOG_DEBUG, "Recallocing pool sockbuf to %lu", (unsigned long)new);
	pool->sockbuf = realloc(pool->sockbuf, new);
	if (!pool->sockbuf)
		quithere(1, "Failed to realloc pool sockbuf");
	memset(pool->sockbuf + old, 0, new - old);
	pool->sockbuf_size = new;
}

/* Peeks at a socket to find the first end of line and then reads just that
 * from the socket and returns that as a malloced char */
char *recv_line(struct pool *pool)
{
	char *tok, *sret = NULL;
	ssize_t len, buflen;
	int waited = 0;

	if (!strstr(pool->sockbuf, "\n")) {
		struct timeval rstart, now;

		cgtime(&rstart);
		if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
			applog(LOG_DEBUG, "Timed out waiting for data on socket_full");
			goto out;
		}

		do {
			char s[RBUFSIZE];
			size_t slen;
			size_t n = 0;
			CURLcode rc;

			memset(s, 0, RBUFSIZE);
			rc = curl_easy_recv(pool->stratum_curl, s, RECVSIZE, &n);
			if (rc == CURLE_OK && !n)
			{
				applog(LOG_DEBUG, "Socket closed waiting in recv_line");
				suspend_stratum(pool);
				break;
			}
			cgtime(&now);
			waited = tdiff(&now, &rstart);
			if (rc != CURLE_OK)
			{
				if (rc != CURLE_AGAIN || !socket_full(pool, DEFAULT_SOCKWAIT - waited))
				{
					applog(LOG_DEBUG, "Failed to recv sock in recv_line");
					suspend_stratum(pool);
					break;
				}
			} else {
				slen = strlen(s);
				recalloc_sock(pool, slen);
				strcat(pool->sockbuf, s);
			}
		} while (waited < DEFAULT_SOCKWAIT && !strstr(pool->sockbuf, "\n"));
	}

	buflen = strlen(pool->sockbuf);
	tok = strtok(pool->sockbuf, "\n");
	if (!tok) {
		applog(LOG_DEBUG, "Failed to parse a \\n terminated string in recv_line");
		goto out;
	}
	sret = strdup(tok);
	len = strlen(sret);

	/* Copy what's left in the buffer after the \n, including the
	 * terminating \0 */
	if (buflen > len + 1)
		memmove(pool->sockbuf, pool->sockbuf + len + 1, buflen - len + 1);
	else
		strcpy(pool->sockbuf, "");

	pool->cgminer_pool_stats.times_received++;
	pool->cgminer_pool_stats.bytes_received += len;
	total_bytes_rcvd += len;
	pool->cgminer_pool_stats.net_bytes_received += len;

out:
	if (!sret)
		clear_sock(pool);
	else if (opt_protocol)
		applog(LOG_DEBUG, "Pool %u: RECV: %s", pool->pool_no, sret);
	return sret;
}

/* Dumps any JSON value as a string. Just like jansson 2.1's JSON_ENCODE_ANY
 * flag, but this is compatible with 2.0. */
char *json_dumps_ANY(json_t *json, size_t flags)
{
	switch (json_typeof(json))
	{
		case JSON_ARRAY:
		case JSON_OBJECT:
			return json_dumps(json, flags);
		default:
			break;
	}
	char *rv;
#ifdef JSON_ENCODE_ANY
	rv = json_dumps(json, JSON_ENCODE_ANY | flags);
	if (rv)
		return rv;
#endif
	json_t *tmp = json_array();
	char *s;
	int i;
	size_t len;
	
	if (!tmp)
		quithere(1, "Failed to allocate json array");
	if (json_array_append(tmp, json))
		quithere(1, "Failed to append temporary array");
	s = json_dumps(tmp, flags);
	if (!s)
		return NULL;
	for (i = 0; s[i] != '['; ++i)
		if (unlikely(!(s[i] && isCspace(s[i]))))
			quithere(1, "Failed to find opening bracket in array dump");
	len = strlen(&s[++i]) - 1;
	if (unlikely(s[i+len] != ']'))
		quithere(1, "Failed to find closing bracket in array dump");
	rv = malloc(len + 1);
	memcpy(rv, &s[i], len);
	rv[len] = '\0';
	free(s);
	json_decref(tmp);
	return rv;
}

/* Extracts a string value from a json array with error checking. To be used
 * when the value of the string returned is only examined and not to be stored.
 * See json_array_string below */
const char *__json_array_string(json_t *val, unsigned int entry)
{
	json_t *arr_entry;

	if (json_is_null(val))
		return NULL;
	if (!json_is_array(val))
		return NULL;
	if (entry > json_array_size(val))
		return NULL;
	arr_entry = json_array_get(val, entry);
	if (!json_is_string(arr_entry))
		return NULL;

	return json_string_value(arr_entry);
}

/* Creates a freshly malloced dup of __json_array_string */
static char *json_array_string(json_t *val, unsigned int entry)
{
	const char *buf = __json_array_string(val, entry);

	if (buf)
		return strdup(buf);
	return NULL;
}

void *my_memrchr(const void * const datap, const int c, const size_t sz)
{
	const uint8_t *data = datap;
	const uint8_t *p = &data[sz];
	while (p > data)
		if (*--p == c)
			return (void *)p;
	return NULL;
}

bool isCalpha(const int c)
{
	if (c >= 'A' && c <= 'Z')
		return true;
	if (c >= 'a' && c <= 'z')
		return true;
	return false;
}

bool match_strtok(const char * const optlist, const char * const delim, const char * const needle)
{
	const size_t optlist_sz = strlen(optlist) + 1;
	char opts[optlist_sz];
	memcpy(opts, optlist, optlist_sz);
	for (char *el, *nextptr, *s = opts; (el = strtok_r(s, delim, &nextptr)); s = NULL)
		if (!strcasecmp(el, needle))
			return true;
	return false;
}

static
bool _appdata_file_call(const char * const appname, const char * const filename, const appdata_file_callback_t cb, void * const userp, const char * const path)
{
	if (!(path && path[0]))
		return false;
	char filepath[PATH_MAX];
	snprintf(filepath, sizeof(filepath), "%s/%s/%s", path, appname, filename);
	if (!access(filepath, R_OK))
		return cb(filepath, userp);
	return false;
}

#define _APPDATA_FILE_CALL(appname, path)  do{  \
	if (_appdata_file_call(appname, filename, cb, userp, path))  \
		return true;  \
}while(0)

bool appdata_file_call(const char *appname, const char * const filename, const appdata_file_callback_t cb, void * const userp)
{
	size_t appname_len = strlen(appname);
	char appname_lcd[appname_len + 1];
	appname_lcd[0] = '.';
	char *appname_lc = &appname_lcd[1];
	for (size_t i = 0; i <= appname_len; ++i)
		appname_lc[i] = tolower(appname[i]);
	appname_lc[appname_len] = '\0';
	
	const char * const HOME = getenv("HOME");
	
	_APPDATA_FILE_CALL(".", ".");
	
#ifdef WIN32
	_APPDATA_FILE_CALL(appname, getenv("APPDATA"));
#elif defined(__APPLE__)
	if (HOME && HOME[0])
	{
		char AppSupport[strlen(HOME) + 28 + 1];
		snprintf(AppSupport, sizeof(AppSupport), "%s/Library/Application Support", HOME);
		_APPDATA_FILE_CALL(appname, AppSupport);
	}
#endif
	
	_APPDATA_FILE_CALL(appname_lcd, HOME);
	
#ifdef WIN32
	_APPDATA_FILE_CALL(appname, getenv("ALLUSERSAPPDATA"));
#elif defined(__APPLE__)
	_APPDATA_FILE_CALL(appname, "/Library/Application Support");
#endif
#ifndef WIN32
	_APPDATA_FILE_CALL(appname_lc, "/etc");
#endif
	
	return false;
}

static
bool _appdata_file_find_first(const char * const filepath, void *userp)
{
	char **rv = userp;
	*rv = strdup(filepath);
	return true;
}

char *appdata_file_find_first(const char * const appname, const char * const filename)
{
	char *rv;
	if (appdata_file_call(appname, filename, _appdata_file_find_first, &rv))
		return rv;
	return NULL;
}

const char *get_registered_domain(size_t * const out_domainlen, const char * const fqdn, const size_t fqdnlen)
{
	const char *s;
	int dots = 0;
	
	for (s = &fqdn[fqdnlen-1]; s >= fqdn; --s)
	{
		if (s[0] == '.')
		{
			*out_domainlen = fqdnlen - (&s[1] - fqdn);
			if (++dots >= 2 && *out_domainlen > 5)
				return &s[1];
		}
		else
		if (!(dots || isCalpha(s[0])))
		{
			*out_domainlen = fqdnlen;
			return fqdn;
		}
	}
	
	*out_domainlen = fqdnlen;
	return fqdn;
}

const char *extract_domain(size_t * const out_domainlen, const char * const uri, const size_t urilen)
{
	const char *p = uri, *b, *q, *s;
	bool alldigit;
	
	p = memchr(&p[1], '/', urilen - (&p[1] - uri));
	if (p)
	{
		if (p[-1] == ':')
		{
			// part of the URI scheme, ignore it
			while (p[0] == '/')
				++p;
			p = memchr(p, '/', urilen - (p - uri));
		}
	}
	if (!p)
	{
		p = memchr(uri, '?', urilen) ?:
		    memchr(uri, '#', urilen) ?:
		    &uri[urilen];
	}
	
	s = p;
	q = my_memrchr(uri, ':', p - uri);
	if (q)
	{
		alldigit = true;
		for (q = b = &q[1]; q < p; ++q)
			if (!isdigit(q[0]))
			{
				alldigit = false;
				break;
			}
		if (alldigit && p != b)
			p = &b[-1];
	}
	
	alldigit = true;
	for (b = uri; b < p; ++b)
	{
		if (b[0] == ':')
			break;
		if (alldigit && !isdigit(b[0]))
			alldigit = false;
	}
	if ((b < p && b[0] == ':') && (b == uri || !alldigit))
		b = &b[1];
	else
		b = uri;
	while (b <= p && b[0] == '/')
		++b;
	if (p - b > 1 && b[0] == '[' && p[-1] == ']')
	{
		++b;
		--p;
	}
	else
	if (memchr(b, ':', p - b))
		p = s;
	if (p > b && p[-1] == '.')
		--p;
	
	*out_domainlen = p - b;
	return b;
}

bool match_domains(const char * const a, const size_t alen, const char * const b, const size_t blen)
{
	size_t a_domainlen, b_domainlen;
	const char *a_domain, *b_domain;
	a_domain = extract_domain(&a_domainlen, a, alen);
	a_domain = get_registered_domain(&a_domainlen, a_domain, a_domainlen);
	b_domain = extract_domain(&b_domainlen, b, blen);
	b_domain = get_registered_domain(&b_domainlen, b_domain, b_domainlen);
	if (a_domainlen != b_domainlen)
		return false;
	return !strncasecmp(a_domain, b_domain, a_domainlen);
}

static
void _test_extract_domain(const char * const expect, const char * const uri)
{
	size_t sz;
	const char * const d = extract_domain(&sz, uri, strlen(uri));
	if (sz != strlen(expect) || strncasecmp(d, expect, sz))
	{
		++unittest_failures;
		applog(LOG_WARNING, "extract_domain \"%s\" test failed; got \"%.*s\" instead of \"%s\"",
		       uri, (int)sz, d, expect);
	}
}

static
void _test_get_regd_domain(const char * const expect, const char * const fqdn)
{
	size_t sz;
	const char * const d = get_registered_domain(&sz, fqdn, strlen(fqdn));
	if (d == NULL || sz != strlen(expect) || strncasecmp(d, expect, sz))
	{
		++unittest_failures;
		applog(LOG_WARNING, "get_registered_domain \"%s\" test failed; got \"%.*s\" instead of \"%s\"",
		       fqdn, (int)sz, d, expect);
	}
}

void test_domain_funcs()
{
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st:3334");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st:3334/abc/abc/");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st/abc/abc/");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st");
	_test_extract_domain("s.m.eligius.st", "http:s.m.eligius.st");
	_test_extract_domain("s.m.eligius.st", "stratum+tcp:s.m.eligius.st");
	_test_extract_domain("s.m.eligius.st", "stratum+tcp:s.m.eligius.st:3334");
	_test_extract_domain("s.m.eligius.st", "stratum+tcp://s.m.eligius.st:3334");
	_test_extract_domain("s.m.eligius.st", "stratum+tcp://s.m.eligius.st:3334///");
	_test_extract_domain("s.m.eligius.st", "stratum+tcp://s.m.eligius.st.:3334///");
	_test_extract_domain("s.m.eligius.st", "s.m.eligius.st:3334");
	_test_extract_domain("s.m.eligius.st", "s.m.eligius.st:3334///");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st:3334#foo");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st:3334?foo");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st#foo");
	_test_extract_domain("s.m.eligius.st", "http://s.m.eligius.st?foo");
	_test_extract_domain("s.m.eligius.st", "s.m.eligius.st#foo");
	_test_extract_domain("s.m.eligius.st", "s.m.eligius.st?foo");
	_test_extract_domain("s.m.eligius.st", "s.m.eligius.st:3334#foo");
	_test_extract_domain("s.m.eligius.st", "s.m.eligius.st:3334?foo");
	_test_extract_domain("foohost", "foohost:3334");
	_test_extract_domain("foohost", "foohost:3334///");
	_test_extract_domain("foohost", "foohost:3334/abc.com//");
	_test_extract_domain("", "foohost:");
	_test_extract_domain("3334", "foohost://3334/abc.com//");
	_test_extract_domain("192.0.2.0", "foohost:192.0.2.0");
	_test_extract_domain("192.0.2.0", "192.0.2.0:3334");
	_test_extract_domain("192.0.2.0", "192.0.2.0:3334///");
	_test_extract_domain("2001:db8::1", "2001:db8::1");
	_test_extract_domain("2001:db8::1", "http://[2001:db8::1]");
	_test_extract_domain("2001:db8::1", "http:[2001:db8::1]");
	_test_extract_domain("2001:db8::1", "http://[2001:db8::1]:42");
	_test_extract_domain("2001:db8::1", "http://[2001:db8::1]:42/abc//def/ghi");
	_test_extract_domain("2001:db8::cafe", "http://[2001:db8::cafe]");
	_test_extract_domain("2001:db8::cafe", "http:[2001:db8::cafe]");
	_test_extract_domain("2001:db8::cafe", "http://[2001:db8::cafe]:42");
	_test_extract_domain("2001:db8::cafe", "http://[2001:db8::cafe]:42/abc//def/ghi");
	_test_get_regd_domain("eligius.st", "s.m.eligius.st");
	_test_get_regd_domain("eligius.st", "eligius.st");
	_test_get_regd_domain("foohost.co.uk", "myserver.foohost.co.uk");
	_test_get_regd_domain("foohost", "foohost");
	_test_get_regd_domain("192.0.2.0", "192.0.2.0");
	_test_get_regd_domain("2001:db8::1", "2001:db8::1");
}

struct bfg_strtobool_keyword {
	bool val;
	const char *keyword;
};

bool bfg_strtobool(const char * const s, char ** const endptr, __maybe_unused const int opts)
{
	struct bfg_strtobool_keyword keywords[] = {
		{false, "disable"},
		{false, "false"},
		{false, "never"},
		{false, "none"},
		{false, "off"},
		{false, "no"},
		{false, "0"},
		
		{true , "enable"},
		{true , "always"},
		{true , "force"},
		{true , "true"},
		{true , "yes"},
		{true , "on"},
	};
	
	const int total_keywords = sizeof(keywords) / sizeof(*keywords);
	for (int i = 0; i < total_keywords; ++i)
	{
		const size_t kwlen = strlen(keywords[i].keyword);
		if (!strncasecmp(keywords[i].keyword, s, kwlen))
		{
			if (endptr)
				*endptr = (char*)&s[kwlen];
			return keywords[i].val;
		}
	}
	
	char *lend;
	strtol(s, &lend, 0);
	if (lend > s)
	{
		if (endptr)
			*endptr = lend;
		// Any number other than "0" is intentionally considered true, including 0x0
		return true;
	}
	
	*endptr = (char*)s;
	return false;
}

#define URI_FIND_PARAM_FOUND ((const char *)uri_find_param)

const char *uri_find_param(const char * const uri, const char * const param, bool * const invert_p)
{
	const char *start = strchr(uri, '#');
	if (invert_p)
		*invert_p = false;
	if (!start)
		return NULL;
	const char *p = start;
	++start;
nextmatch:
	p = strstr(&p[1], param);
	if (!p)
		return NULL;
	const char *q = &p[strlen(param)];
	if (isCalpha(q[0]))
		goto nextmatch;
	if (invert_p && p - start >= 2 && (!strncasecmp(&p[-2], "no", 2)) && !isCalpha(p[-3]))
		*invert_p = true;
	else
	if (isCalpha(p[-1]))
		goto nextmatch;
	if (q[0] == '=')
		return &q[1];
	return URI_FIND_PARAM_FOUND;
}

enum bfg_tristate uri_get_param_bool2(const char * const uri, const char * const param)
{
	bool invert, foundval = true;
	const char *q = uri_find_param(uri, param, &invert);
	if (!q)
		return BTS_UNKNOWN;
	else
	if (q != URI_FIND_PARAM_FOUND)
	{
		char *end;
		bool v = bfg_strtobool(q, &end, 0);
		if (end > q && !isCalpha(end[0]))
			foundval = v;
	}
	if (invert)
		foundval = !foundval;
	return foundval;
}

bool uri_get_param_bool(const char * const uri, const char * const param, const bool defval)
{
	const enum bfg_tristate rv = uri_get_param_bool2(uri, param);
	if (rv == BTS_UNKNOWN)
		return defval;
	return rv;
}

static
void _test_uri_find_param(const char * const uri, const char * const param, const int expect_offset, const int expect_invert)
{
	bool invert;
	const char *actual = uri_find_param(uri, param, (expect_invert >= 0) ? &invert : NULL);
	int actual_offset;
	if (actual == URI_FIND_PARAM_FOUND)
		actual_offset = -1;
	else
	if (!actual)
		actual_offset = -2;
	else
		actual_offset = actual - uri;
	int actual_invert = (expect_invert >= 0) ? (invert ? 1 : 0) : -1;
	if (actual_offset != expect_offset || expect_invert != actual_invert)
	{
		++unittest_failures;
		applog(LOG_WARNING, "%s(\"%s\", \"%s\", %s) test failed (offset: expect=%d actual=%d; invert: expect=%d actual=%d)",
		       "uri_find_param", uri, param, (expect_invert >= 0) ? "(invert)" : "NULL",
		       expect_offset, actual_offset,
		       expect_invert, actual_invert);
	}
}

static
void _test_uri_get_param(const char * const uri, const char * const param, const bool defval, const bool expect)
{
	const bool actual = uri_get_param_bool(uri, param, defval);
	if (actual != expect)
	{
		++unittest_failures;
		applog(LOG_WARNING, "%s(\"%s\", \"%s\", %s) test failed",
		       "uri_get_param_bool", uri, param, defval ? "true" : "false");
	}
}

void test_uri_get_param()
{
	_test_uri_find_param("stratum+tcp://footest/#redirect", "redirect", -1, -1);
	_test_uri_find_param("stratum+tcp://footest/#redirectme", "redirect", -2, -1);
	_test_uri_find_param("stratum+tcp://footest/#noredirect", "redirect", -2, -1);
	_test_uri_find_param("stratum+tcp://footest/#noredirect", "redirect", -1, 1);
	_test_uri_find_param("stratum+tcp://footest/#redirect", "redirect", -1, 0);
	_test_uri_find_param("stratum+tcp://footest/#redirect=", "redirect", 32, -1);
	_test_uri_find_param("stratum+tcp://footest/#noredirect=", "redirect", 34, 1);
	_test_uri_get_param("stratum+tcp://footest/#redirect", "redirect", false, true);
	_test_uri_get_param("stratum+tcp://footest/#redirectme", "redirect", false, false);
	_test_uri_get_param("stratum+tcp://footest/#noredirect", "redirect", false, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=0", "redirect", false, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=1", "redirect", false, true);
	_test_uri_get_param("stratum+tcp://footest/#redirect", "redirect", true, true);
	_test_uri_get_param("stratum+tcp://footest/#redirectme", "redirect", true, true);
	_test_uri_get_param("stratum+tcp://footest/#noredirect", "redirect", true, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=0", "redirect", true, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=1", "redirect", true, true);
	_test_uri_get_param("stratum+tcp://footest/#redirect=0,foo=1", "redirect", true, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=1,foo=0", "redirect", false, true);
	_test_uri_get_param("stratum+tcp://footest/#foo=1,noredirect=0,foo=1", "redirect", false, true);
	_test_uri_get_param("stratum+tcp://footest/#bar=0,noredirect=1,foo=0", "redirect", true, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=false", "redirect", true, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=no", "redirect", true, false);
	_test_uri_get_param("stratum+tcp://footest/#redirect=yes", "redirect", false, true);
}

void stratum_probe_transparency(struct pool *pool)
{
	// Request transaction data to discourage pools from doing anything shady
	char s[1024];
	int sLen;
	sLen = sprintf(s, "{\"params\": [\"%s\"], \"id\": \"txlist%s\", \"method\": \"mining.get_transactions\"}",
	        pool->swork.job_id,
	        pool->swork.job_id);
	stratum_send(pool, s, sLen);
	if ((!pool->swork.opaque) && !timer_isset(&pool->swork.tv_transparency))
		timer_set_delay_from_now(&pool->swork.tv_transparency, 21093750L);
	pool->swork.transparency_probed = true;
}

size_t script_to_address(char *out, size_t outsz, const uint8_t *script, size_t scriptsz, bool testnet)
{
	char addr[35];
	size_t size = sizeof(addr);
	bool bok = false;
	
	if (scriptsz == 25 && script[0] == 0x76 && script[1] == 0xa9 && script[2] == 0x14 && script[23] == 0x88 && script[24] == 0xac)
		bok = b58check_enc(addr, &size, testnet ? 0x6f : 0x00, &script[3], 20);
	else if (scriptsz == 23 && script[0] == 0xa9 && script[1] == 0x14 && script[22] == 0x87)
		bok = b58check_enc(addr, &size, testnet ? 0xc4 : 0x05, &script[2], 20);
	if (!bok)
		return 0;
	if (outsz >= size)
		strcpy(out, addr);
	return size;
}

size_t varint_decode(const uint8_t *p, size_t size, uint64_t *n)
{
	if (size > 8 && p[0] == 0xff)
	{
		*n = upk_u64le(p, 1);
		return 9;
	}
	if (size > 4 && p[0] == 0xfe)
	{
		*n = upk_u32le(p, 1);
		return 5;
	}
	if (size > 2 && p[0] == 0xfd)
	{
		*n = upk_u16le(p, 1);
		return 3;
	}
	if (size > 0 && p[0] <= 0xfc)
	{
		*n = p[0];
		return 1;
	}
	return 0;
}

/* Caller ensure cb_param is an valid pointer */
bool check_coinbase(const uint8_t *coinbase, size_t cbsize, const struct coinbase_param *cb_param)
{
	int i;
	size_t pos;
	uint64_t len, total, target, amount, curr_pk_script_len;
	bool found_target = false;
	
	if (cbsize < 62)
		/* Smallest possible length */
		applogr(false, LOG_ERR, "Coinbase check: invalid length -- %lu", (unsigned long)cbsize);
	pos = 4; /* Skip the version */
	
	if (coinbase[pos] != 1)
		applogr(false, LOG_ERR, "Coinbase check: multiple inputs in coinbase: 0x%02x", coinbase[pos]);
	pos += 1 /* varint length */ + 32 /* prevhash */ + 4 /* 0xffffffff */;
	
	if (coinbase[pos] < 2 || coinbase[pos] > 100)
		applogr(false, LOG_ERR, "Coinbase check: invalid input script sig length: 0x%02x", coinbase[pos]);
	pos += 1 /* varint length */ + coinbase[pos] + 4 /* 0xffffffff */;
	
	if (cbsize <= pos)
incomplete_cb:
		applogr(false, LOG_ERR, "Coinbase check: incomplete coinbase for payout check");
	
	total = target = 0;
	
	i = varint_decode(coinbase + pos, cbsize - pos, &len);
	if (!i)
		goto incomplete_cb;
	pos += i;
	
	while (len-- > 0)
	{
		if (cbsize <= pos + 8)
			goto incomplete_cb;
		
		amount = upk_u64le(coinbase, pos);
		pos += 8; /* amount length */
		
		total += amount;
		
		i = varint_decode(coinbase + pos, cbsize - pos, &curr_pk_script_len);
		if (!i || cbsize <= pos + i + curr_pk_script_len)
			goto incomplete_cb;
		pos += i;
		
		struct bytes_hashtbl *ah = NULL;
		HASH_FIND(hh, cb_param->scripts, &coinbase[pos], curr_pk_script_len, ah);
		if (ah)
		{
			found_target = true;
			target += amount;
		}
		
		if (opt_debug)
		{
			char s[(curr_pk_script_len * 2) + 3];
			i = script_to_address(s, sizeof(s), &coinbase[pos], curr_pk_script_len, cb_param->testnet);
			if (!(i && i <= sizeof(s)))
			{
				s[0] = '[';
				bin2hex(&s[1], &coinbase[pos], curr_pk_script_len);
				strcpy(&s[(curr_pk_script_len * 2) + 1], "]");
			}
			applog(LOG_DEBUG, "Coinbase output: %10"PRIu64" -- %s%s", amount, s, ah ? "*" : "");
		}
		
		pos += curr_pk_script_len;
	}
	if (total < cb_param->total)
		applogr(false, LOG_ERR, "Coinbase check: lopsided total output amount = %"PRIu64", expecting >=%"PRIu64, total, cb_param->total);
	if (cb_param->scripts)
	{
		if (cb_param->perc && !(total && (float)((double)target / total) >= cb_param->perc))
			applogr(false, LOG_ERR, "Coinbase check: lopsided target/total = %g(%"PRIu64"/%"PRIu64"), expecting >=%g", (total ? (double)target / total : (double)0), target, total, cb_param->perc);
		else
		if (!found_target)
			applogr(false, LOG_ERR, "Coinbase check: not found any target addr");
	}
	
	if (cbsize < pos + 4)
		applogr(false, LOG_ERR, "Coinbase check: No room for locktime");
	pos += 4;
	
	if (opt_debug)
		applog(LOG_DEBUG, "Coinbase: (size, pos, addr_count, target, total) = (%lu, %lu, %d, %"PRIu64", %"PRIu64")", (unsigned long)cbsize, (unsigned long)pos, (int)(HASH_COUNT(cb_param->scripts)), target, total);
	
	return true;
}

static bool parse_notify(struct pool *pool, json_t *val)
{
	const char *prev_hash, *coinbase1, *coinbase2, *bbversion, *nbit, *ntime;
	char *job_id;
	bool clean, ret = false;
	int merkles, i;
	size_t cb1_len, cb2_len;
	json_t *arr;

	arr = json_array_get(val, 4);
	if (!arr || !json_is_array(arr))
		goto out;

	merkles = json_array_size(arr);
	for (i = 0; i < merkles; i++)
		if (!json_is_string(json_array_get(arr, i)))
			goto out;

	prev_hash = __json_array_string(val, 1);
	coinbase1 = __json_array_string(val, 2);
	coinbase2 = __json_array_string(val, 3);
	bbversion = __json_array_string(val, 5);
	nbit = __json_array_string(val, 6);
	ntime = __json_array_string(val, 7);
	clean = json_is_true(json_array_get(val, 8));

	if (!prev_hash || !coinbase1 || !coinbase2 || !bbversion || !nbit || !ntime)
		goto out;
	
	job_id = json_array_string(val, 0);
	if (!job_id)
		goto out;

	cg_wlock(&pool->data_lock);
	cgtime(&pool->swork.tv_received);
	free(pool->swork.job_id);
	pool->swork.job_id = job_id;
	if (pool->swork.tr)
	{
		tmpl_decref(pool->swork.tr);
		pool->swork.tr = NULL;
	}
	pool->submit_old = !clean;
	pool->swork.clean = true;
	
	// stratum_set_goal ensures these are the same pointer if they match
	if (pool->goalname != pool->next_goalname)
	{
		free(pool->goalname);
		pool->goalname = pool->next_goalname;
		mining_goal_reset(pool->goal);
	}
	if (pool->next_goal_malgo)
	{
		goal_set_malgo(pool->goal, pool->next_goal_malgo);
		pool->next_goal_malgo = NULL;
	}
	
	if (pool->next_nonce1)
	{
		free(pool->swork.nonce1);
		pool->n1_len = strlen(pool->next_nonce1) / 2;
		pool->swork.nonce1 = pool->next_nonce1;
		pool->next_nonce1 = NULL;
	}
	int n2size = pool->swork.n2size = pool->next_n2size;
	pool->nonce2sz  = (n2size > sizeof(pool->nonce2)) ? sizeof(pool->nonce2) : n2size;
#ifdef WORDS_BIGENDIAN
	pool->nonce2off = (n2size < sizeof(pool->nonce2)) ? (sizeof(pool->nonce2) - n2size) : 0;
#endif
	
	hex2bin(&pool->swork.header1[0], bbversion,  4);
	hex2bin(&pool->swork.header1[4], prev_hash, 32);
	hex2bin((void*)&pool->swork.ntime, ntime, 4);
	pool->swork.ntime = be32toh(pool->swork.ntime);
	hex2bin(&pool->swork.diffbits[0], nbit, 4);
	
	/* Nominally allow a driver to ntime roll 60 seconds */
	set_simple_ntime_roll_limit(&pool->swork.ntime_roll_limits, pool->swork.ntime, 60, &pool->swork.tv_received);
	
	cb1_len = strlen(coinbase1) / 2;
	pool->swork.nonce2_offset = cb1_len + pool->n1_len;
	cb2_len = strlen(coinbase2) / 2;

	bytes_resize(&pool->swork.coinbase, pool->swork.nonce2_offset + pool->swork.n2size + cb2_len);
	uint8_t *coinbase = bytes_buf(&pool->swork.coinbase);
	hex2bin(coinbase, coinbase1, cb1_len);
	hex2bin(&coinbase[cb1_len], pool->swork.nonce1, pool->n1_len);
	// NOTE: gap for nonce2, filled at work generation time
	hex2bin(&coinbase[pool->swork.nonce2_offset + pool->swork.n2size], coinbase2, cb2_len);
	
	bytes_resize(&pool->swork.merkle_bin, 32 * merkles);
	for (i = 0; i < merkles; i++)
		hex2bin(&bytes_buf(&pool->swork.merkle_bin)[i * 32], json_string_value(json_array_get(arr, i)), 32);
	pool->swork.merkles = merkles;
	pool->nonce2 = 0;
	
	memcpy(pool->swork.target, pool->next_target, 0x20);
	
	pool_check_coinbase(pool, coinbase, bytes_len(&pool->swork.coinbase));
	
	cg_wunlock(&pool->data_lock);

	applog(LOG_DEBUG, "Received stratum notify from pool %u with job_id=%s",
	       pool->pool_no, job_id);
	if (opt_debug && opt_protocol)
	{
		applog(LOG_DEBUG, "job_id: %s", job_id);
		applog(LOG_DEBUG, "prev_hash: %s", prev_hash);
		applog(LOG_DEBUG, "coinbase1: %s", coinbase1);
		applog(LOG_DEBUG, "coinbase2: %s", coinbase2);
		for (i = 0; i < merkles; i++)
			applog(LOG_DEBUG, "merkle%d: %s", i, json_string_value(json_array_get(arr, i)));
		applog(LOG_DEBUG, "bbversion: %s", bbversion);
		applog(LOG_DEBUG, "nbit: %s", nbit);
		applog(LOG_DEBUG, "ntime: %s", ntime);
		applog(LOG_DEBUG, "clean: %s", clean ? "yes" : "no");
	}

	/* A notify message is the closest stratum gets to a getwork */
	pool->getwork_requested++;
	total_getworks++;

	if ((merkles && (!pool->swork.transparency_probed || rand() <= RAND_MAX / (opt_skip_checks + 1))) || timer_isset(&pool->swork.tv_transparency))
		if (pool->probed)
			stratum_probe_transparency(pool);

	ret = true;
out:
	return ret;
}

static bool parse_diff(struct pool *pool, json_t *val)
{
	const struct mining_goal_info * const goal = pool->goal;
	const struct mining_algorithm * const malgo = goal->malgo;
	double diff;

	diff = json_number_value(json_array_get(val, 0));
	if (diff == 0)
		return false;

	if ((int64_t)diff != diff)
	{
		// Assume fractional values are proper bdiff per specification
		// Allow integers to be interpreted as pdiff, since the difference is trivial and some pools see it this way
		diff = bdiff_to_pdiff(diff);
	}
	
#ifdef USE_SHA256D
	if (malgo->algo == POW_SHA256D && diff < 1 && diff > 0.999)
		diff = 1;
#endif
	
#ifdef USE_SCRYPT
	// Broken Scrypt pools multiply difficulty by 0x10000
	const double broken_scrypt_diff_multiplier = 0x10000;

	/* 7/12/2014: P2Pool code was fixed: https://github.com/forrestv/p2pool/pull/210
	   7/15/2014: Popular pools unfixed: wemineltc, dogehouse, p2pool.org
                  Cannot find a broken Scrypt pool that will dispense diff lower than 16 */

	// Ideally pools will fix their implementation and we can remove this
	// This should suffice until miners are hashing Scrypt at ~1-7 Gh/s (based on a share rate target of 10-60s)

	const double minimum_broken_scrypt_diff = 16;
	// Diff 16 at 1.15 Gh/s = 1 share / 60s
	// Diff 16 at 7.00 Gh/s = 1 share / 10s

	if (malgo->algo == POW_SCRYPT && (diff >= minimum_broken_scrypt_diff))
		diff /= broken_scrypt_diff_multiplier;
#endif

	cg_wlock(&pool->data_lock);
	set_target_to_pdiff(pool->next_target, diff);
	cg_wunlock(&pool->data_lock);

	applog(LOG_DEBUG, "Pool %d stratum difficulty set to %g", pool->pool_no, diff);

	return true;
}

static
bool stratum_set_extranonce(struct pool * const pool, json_t * const val, json_t * const params)
{
	char *nonce1 = NULL;
	int n2size = 0;
	json_t *j;
	
	if (!json_is_array(params))
		goto err;
	switch (json_array_size(params))
	{
		default:  // >=2
			// n2size
			j = json_array_get(params, 1);
			if (json_is_number(j))
			{
				n2size = json_integer_value(j);
				if (n2size < 1)
					goto err;
			}
			else
			if (!json_is_null(j))
				goto err;
			// fallthru
		case 1:
			// nonce1
			j = json_array_get(params, 0);
			if (json_is_string(j))
				nonce1 = strdup(json_string_value(j));
			else
			if (!json_is_null(j))
				goto err;
			break;
		case 0:
			applog(LOG_WARNING, "Pool %u: No-op mining.set_extranonce?", pool->pool_no);
			return true;
	}
	
	cg_wlock(&pool->data_lock);
	if (nonce1)
	{
		free(pool->next_nonce1);
		pool->next_nonce1 = nonce1;
	}
	if (n2size)
		pool->next_n2size = n2size;
	cg_wunlock(&pool->data_lock);
	
	return true;

err:
	applog(LOG_ERR, "Pool %u: Invalid mining.set_extranonce", pool->pool_no);
	
	json_t *id = json_object_get(val, "id");
	if (id && !json_is_null(id))
	{
		char s[RBUFSIZE], *idstr;
		idstr = json_dumps_ANY(id, 0);
		sprintf(s, "{\"id\": %s, \"result\": null, \"error\": [20, \"Invalid params\"]}", idstr);
		free(idstr);
		stratum_send(pool, s, strlen(s));
	}
	
	return true;
}

static
bool stratum_set_goal(struct pool * const pool, json_t * const val, json_t * const params)
{
	if (!uri_get_param_bool(pool->rpc_url, "goalreset", false))
		return false;
	
	const char * const new_goalname = __json_array_string(params, 0);
	struct mining_algorithm *new_malgo = NULL;
	const char *emsg = NULL;
	
	if (json_is_array(params) && json_array_size(params) > 1)
	{
		json_t * const j_goaldesc = json_array_get(params, 1);
		if (json_is_object(j_goaldesc))
		{
			json_t * const j_malgo = json_object_get(j_goaldesc, "malgo");
			if (j_malgo && json_is_string(j_malgo))
			{
				const char * const newvalue = json_string_value(j_malgo);
				new_malgo = mining_algorithm_by_alias(newvalue);
				// Even if it's the current malgo, we should reset next_goal_malgo in case of a prior set_goal
				if (new_malgo == pool->goal->malgo)
				{}  // Do nothing, assignment takes place below
				if (new_malgo && uri_get_param_bool(pool->rpc_url, "change_goal_malgo", false))
				{}  // Do nothing, assignment takes place below
				else
				{
					emsg = "Mining algorithm not supported";
					// Ignore even the goal name, if we are failing
					goto out;
				}
				if (new_malgo == pool->goal->malgo)
					new_malgo = NULL;
			}
		}
	}
	
	// Even if the goal name is not changing, we need to adopt and configuration change
	pool->next_goal_malgo = new_malgo;
	
	if (pool->next_goalname && pool->next_goalname != pool->goalname)
		free(pool->next_goalname);
	
	// This compares goalname to new_goalname, but matches NULL correctly :)
	if (pool->goalname ? !strcmp(pool->goalname, new_goalname) : !new_goalname)
		pool->next_goalname = pool->goalname;
	else
		pool->next_goalname = maybe_strdup(new_goalname);
	
out: ;
	json_t * const j_id = json_object_get(val, "id");
	if (j_id && !json_is_null(j_id))
	{
		char * const idstr = json_dumps_ANY(j_id, 0);
		char buf[0x80];
		if (unlikely(emsg))
			snprintf(buf, sizeof(buf), "{\"id\":%s,\"result\":true,\"error\":null}", idstr);
		else
			snprintf(buf, sizeof(buf), "{\"id\":%s,\"result\":null,\"error\":[-1,\"%s\",null]}", idstr, emsg);
		free(idstr);
		stratum_send(pool, buf, strlen(buf));
	}
	
	return true;
}

static bool parse_reconnect(struct pool *pool, json_t *val)
{
	if (opt_disable_client_reconnect)
		return false;
	
	const char *url;
	char address[256];
	json_t *port_json;

	url = __json_array_string(val, 0);
	if (!url)
		url = pool->sockaddr_url;
	else
	if (!pool_may_redirect_to(pool, url))
		return false;

	port_json = json_array_get(val, 1);
	if (json_is_number(port_json))
	{
		const unsigned port = json_number_value(port_json);
		snprintf(address, sizeof(address), "%s:%u", url, port);
	}
	else
	{
		const char *port;
		if (json_is_string(port_json))
			port = json_string_value(port_json);
		else
			port = pool->stratum_port;
		
		snprintf(address, sizeof(address), "%s:%s", url, port);
	}

	if (!extract_sockaddr(address, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	pool->stratum_url = pool->sockaddr_url;

	applog(LOG_NOTICE, "Reconnect requested from pool %d to %s", pool->pool_no, address);

	if (!restart_stratum(pool))
		return false;

	return true;
}

static bool send_version(struct pool *pool, json_t *val)
{
	char s[RBUFSIZE], *idstr;
	json_t *id = json_object_get(val, "id");
	
	if (!(id && !json_is_null(id)))
		return false;

	idstr = json_dumps_ANY(id, 0);
	sprintf(s, "{\"id\": %s, \"result\": \"%s\", \"error\": null}", idstr, bfgminer_name_slash_ver);
	free(idstr);
	if (!stratum_send(pool, s, strlen(s)))
		return false;

	return true;
}

static bool stratum_show_message(struct pool *pool, json_t *val, json_t *params)
{
	char *msg;
	char s[RBUFSIZE], *idstr;
	json_t *id = json_object_get(val, "id");
	msg = json_array_string(params, 0);
	
	if (likely(msg))
	{
		free(pool->admin_msg);
		pool->admin_msg = msg;
		applog(LOG_NOTICE, "Message from pool %u: %s", pool->pool_no, msg);
	}
	
	if (!(id && !json_is_null(id)))
		return true;
	
	idstr = json_dumps_ANY(id, 0);
	if (likely(msg))
		sprintf(s, "{\"id\": %s, \"result\": true, \"error\": null}", idstr);
	else
		sprintf(s, "{\"id\": %s, \"result\": null, \"error\": [-1, \"Failed to parse message\", null]}", idstr);
	free(idstr);
	if (!stratum_send(pool, s, strlen(s)))
		return false;
	
	return true;
}

bool parse_method(struct pool *pool, char *s)
{
	json_t *val = NULL, *method, *err_val, *params;
	json_error_t err;
	bool ret = false;
	const char *buf;

	if (!s)
		goto out;

	val = JSON_LOADS(s, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	method = json_object_get(val, "method");
	if (!method)
		goto out;
	err_val = json_object_get(val, "error");
	params = json_object_get(val, "params");

	if (err_val && !json_is_null(err_val)) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC method decode failed: %s", ss);

		free(ss);

		goto out;
	}

	buf = json_string_value(method);
	if (!buf)
		goto out;

	if (!strncasecmp(buf, "mining.notify", 13)) {
		if (parse_notify(pool, params))
			pool->stratum_notify = ret = true;
		else
			pool->stratum_notify = ret = false;
		goto out;
	}

	if (!strncasecmp(buf, "mining.set_difficulty", 21) && parse_diff(pool, params)) {
		ret = true;
		goto out;
	}

	if (!strncasecmp(buf, "client.reconnect", 16) && parse_reconnect(pool, params)) {
		ret = true;
		goto out;
	}

	if (!strncasecmp(buf, "client.get_version", 18) && send_version(pool, val)) {
		ret = true;
		goto out;
	}

	if (!strncasecmp(buf, "client.show_message", 19) && stratum_show_message(pool, val, params)) {
		ret = true;
		goto out;
	}
	
	if (!strncasecmp(buf, "mining.set_extranonce", 21) && stratum_set_extranonce(pool, val, params)) {
		ret = true;
		goto out;
	}
	
	// Usage: mining.set_goal("goal name", {"malgo":"SHA256d", ...})
	if (!strncasecmp(buf, "mining.set_goal", 15) && stratum_set_goal(pool, val, params))
		return_via(out, ret = true);
	
out:
	if (val)
		json_decref(val);

	return ret;
}

extern bool parse_stratum_response(struct pool *, char *s);

bool auth_stratum(struct pool *pool)
{
	json_t *val = NULL, *res_val, *err_val;
	char s[RBUFSIZE], *sret = NULL;
	json_error_t err;
	bool ret = false;

	sprintf(s, "{\"id\": \"auth\", \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}",
	        pool->rpc_user, pool->rpc_pass);

	if (!stratum_send(pool, s, strlen(s)))
		goto out;

	/* Parse all data in the queue and anything left should be auth */
	while (42) {
		sret = recv_line(pool);
		if (!sret)
			goto out;
		if (parse_method(pool, sret))
			free(sret);
		else
		{
			bool unknown = true;
			val = JSON_LOADS(sret, &err);
			json_t *j_id = json_object_get(val, "id");
			if (json_is_string(j_id))
			{
				if (!strcmp(json_string_value(j_id), "auth"))
					break;
				else
				if (!strcmp(json_string_value(j_id), "xnsub"))
					unknown = false;
			}
			if (unknown)
				applog(LOG_WARNING, "Pool %u: Unknown stratum msg: %s", pool->pool_no, sret);
			free(sret);
		}
	}

	free(sret);
	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val || json_is_false(res_val) || (err_val && !json_is_null(err_val)))  {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");
		applog(LOG_WARNING, "pool %d JSON stratum auth failed: %s", pool->pool_no, ss);
		free(ss);

		goto out;
	}

	ret = true;
	applog(LOG_INFO, "Stratum authorisation success for pool %d", pool->pool_no);
	pool->probed = true;
	successful_connect = true;
out:
	if (val)
		json_decref(val);

	if (pool->stratum_notify)
		stratum_probe_transparency(pool);

	return ret;
}

curl_socket_t grab_socket_opensocket_cb(void *clientp, __maybe_unused curlsocktype purpose, struct curl_sockaddr *addr)
{
	struct pool *pool = clientp;
	curl_socket_t sck = bfg_socket(addr->family, addr->socktype, addr->protocol);
	pool->sock = sck;
	return sck;
}

static bool setup_stratum_curl(struct pool *pool)
{
	CURL *curl = NULL;
	char s[RBUFSIZE];
	bool ret = false;
	bool tls_only = false, try_tls = true;
	bool tlsca = uri_get_param_bool(pool->rpc_url, "tlsca", false);
	
	{
		const enum bfg_tristate tlsparam = uri_get_param_bool2(pool->rpc_url, "tls");
		if (tlsparam != BTS_UNKNOWN)
			try_tls = tls_only = tlsparam;
		else
		if (tlsca)
			// If tlsca is enabled, require TLS by default
			tls_only = true;
	}

	applog(LOG_DEBUG, "initiate_stratum with sockbuf=%p", pool->sockbuf);
	mutex_lock(&pool->stratum_lock);
	timer_unset(&pool->swork.tv_transparency);
	pool->stratum_active = false;
	pool->stratum_notify = false;
	pool->swork.transparency_probed = false;
	if (pool->stratum_curl)
		curl_easy_cleanup(pool->stratum_curl);
	pool->stratum_curl = curl_easy_init();
	if (unlikely(!pool->stratum_curl))
		quithere(1, "Failed to curl_easy_init");
	if (pool->sockbuf)
		pool->sockbuf[0] = '\0';

	curl = pool->stratum_curl;

	if (!pool->sockbuf) {
		pool->sockbuf = calloc(RBUFSIZE, 1);
		if (!pool->sockbuf)
			quithere(1, "Failed to calloc pool sockbuf");
		pool->sockbuf_size = RBUFSIZE;
	}

	curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, pool->curl_err_str);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	if (!opt_delaynet)
		curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);

	/* We use DEBUGFUNCTION to count bytes sent/received, and verbose is needed
	 * to enable it */
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *)pool);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	// CURLINFO_LASTSOCKET is broken on Win64 (which has a wider SOCKET type than curl_easy_getinfo returns), so we use this hack for now
	curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, grab_socket_opensocket_cb);
	curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, pool);
	
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, (long)(tlsca ? 2 : 0));
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, (long)(tlsca ? 1 : 0));
	if (pool->rpc_proxy) {
		curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1);
		curl_easy_setopt(curl, CURLOPT_PROXY, pool->rpc_proxy);
	} else if (opt_socks_proxy) {
		curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1);
		curl_easy_setopt(curl, CURLOPT_PROXY, opt_socks_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
	}
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1);
	
retry:
	/* Create a http url for use with curl */
	sprintf(s, "http%s://%s:%s", try_tls ? "s" : "",
	        pool->sockaddr_url, pool->stratum_port);
	curl_easy_setopt(curl, CURLOPT_URL, s);
	
	pool->sock = INVSOCK;
	if (curl_easy_perform(curl)) {
		if (try_tls)
		{
			applog(LOG_DEBUG, "Stratum connect failed with TLS to pool %u: %s",
			       pool->pool_no, pool->curl_err_str);
			if (!tls_only)
			{
				try_tls = false;
				goto retry;
			}
		}
		else
			applog(LOG_INFO, "Stratum connect failed to pool %d: %s",
			       pool->pool_no, pool->curl_err_str);
errout:
		curl_easy_cleanup(curl);
		pool->stratum_curl = NULL;
		goto out;
	}
	if (pool->sock == INVSOCK)
	{
		applog(LOG_ERR, "Stratum connect succeeded, but technical problem extracting socket (pool %u)", pool->pool_no);
		goto errout;
	}
	keep_sockalive(pool->sock);

	pool->cgminer_pool_stats.times_sent++;
	pool->cgminer_pool_stats.times_received++;
	ret = true;

out:
	mutex_unlock(&pool->stratum_lock);
	
	return ret;
}

static char *get_sessionid(json_t *val)
{
	char *ret = NULL;
	json_t *arr_val;
	int arrsize, i;

	arr_val = json_array_get(val, 0);
	if (!arr_val || !json_is_array(arr_val))
		goto out;
	arrsize = json_array_size(arr_val);
	for (i = 0; i < arrsize; i++) {
		json_t *arr = json_array_get(arr_val, i);
		const char *notify;

		if (!arr | !json_is_array(arr))
			break;
		notify = __json_array_string(arr, 0);
		if (!notify)
			continue;
		if (!strncasecmp(notify, "mining.notify", 13)) {
			ret = json_array_string(arr, 1);
			break;
		}
	}
out:
	return ret;
}

void suspend_stratum(struct pool *pool)
{
	clear_sockbuf(pool);
	applog(LOG_INFO, "Closing socket for stratum pool %d", pool->pool_no);

	mutex_lock(&pool->stratum_lock);
	pool->stratum_active = pool->stratum_notify = false;
	if (pool->stratum_curl) {
		curl_easy_cleanup(pool->stratum_curl);
	}
	pool->stratum_curl = NULL;
	pool->sock = INVSOCK;
	mutex_unlock(&pool->stratum_lock);
}

bool initiate_stratum(struct pool *pool)
{
	bool ret = false, recvd = false, noresume = false, sockd = false;
	bool trysuggest = request_target_str;
	char s[RBUFSIZE], *sret = NULL, *nonce1, *sessionid;
	json_t *val = NULL, *res_val, *err_val;
	json_error_t err;
	int n2size;

resend:
	if (!setup_stratum_curl(pool)) {
		sockd = false;
		goto out;
	}

	sockd = true;

	clear_sock(pool);
	
	if (trysuggest)
	{
		int sz = sprintf(s, "{\"id\": null, \"method\": \"mining.suggest_target\", \"params\": [\"%s\"]}", request_target_str);
		if (!_stratum_send(pool, s, sz, true))
		{
			applog(LOG_DEBUG, "Pool %u: Failed to send suggest_target in initiate_stratum", pool->pool_no);
			goto out;
		}
		recvd = true;
	}
	
	if (uri_get_param_bool(pool->rpc_url, "goalreset", false))
	{
		// Default: ["notify", "set_difficulty"] (but these must be explicit if mining.capabilities is used)
		snprintf(s, sizeof(s), "{\"id\":null,\"method\":\"mining.capabilities\",\"params\":[{\"notify\":[],\"set_difficulty\":{},\"set_goal\":[],\"malgo\":{");
		struct mining_algorithm *malgo;
		LL_FOREACH(mining_algorithms, malgo)
		{
			tailsprintf(s, sizeof(s), "\"%s\":{}%c", malgo->name, malgo->next ? ',' : '}');
		}
		if (request_target_str)
			tailsprintf(s, sizeof(s), ",\"suggested_target\":\"%s\"", request_target_str);
		tailsprintf(s, sizeof(s), "}]}");
		_stratum_send(pool, s, strlen(s), true);
	}
	
	if (noresume) {
		sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": []}", swork_id++);
	} else {
		if (pool->sessionid)
			sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\"%s\", \"%s\"]}", swork_id++, bfgminer_name_slash_ver, pool->sessionid);
		else
			sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\"%s\"]}", swork_id++, bfgminer_name_slash_ver);
	}

	if (!_stratum_send(pool, s, strlen(s), true)) {
		applog(LOG_DEBUG, "Failed to send s in initiate_stratum");
		goto out;
	}

	recvd = true;
	
	if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
		applog(LOG_DEBUG, "Timed out waiting for response in initiate_stratum");
		goto out;
	}

	sret = recv_line(pool);
	if (!sret)
		goto out;

	val = JSON_LOADS(sret, &err);
	free(sret);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val || json_is_null(res_val) ||
	    (err_val && !json_is_null(err_val))) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC decode failed: %s", ss);

		free(ss);

		goto out;
	}

	sessionid = get_sessionid(res_val);
	if (!sessionid)
		applog(LOG_DEBUG, "Failed to get sessionid in initiate_stratum");
	nonce1 = json_array_string(res_val, 1);
	if (!nonce1) {
		applog(LOG_INFO, "Failed to get nonce1 in initiate_stratum");
		free(sessionid);
		goto out;
	}
	n2size = json_integer_value(json_array_get(res_val, 2));
	if (n2size < 1)
	{
		applog(LOG_INFO, "Failed to get n2size in initiate_stratum");
		free(sessionid);
		free(nonce1);
		goto out;
	}

	cg_wlock(&pool->data_lock);
	free(pool->sessionid);
	pool->sessionid = sessionid;
	free(pool->next_nonce1);
	pool->next_nonce1 = nonce1;
	pool->next_n2size = n2size;
	cg_wunlock(&pool->data_lock);

	if (sessionid)
		applog(LOG_DEBUG, "Pool %d stratum session id: %s", pool->pool_no, pool->sessionid);

	ret = true;
out:
	if (val)
	{
		json_decref(val);
		val = NULL;
	}

	if (ret) {
		if (!pool->stratum_url)
			pool->stratum_url = pool->sockaddr_url;
		pool->stratum_active = true;
		set_target_to_pdiff(pool->next_target, 1);
		if (opt_protocol) {
			applog(LOG_DEBUG, "Pool %d confirmed mining.subscribe with extranonce1 %s extran2size %d",
			       pool->pool_no, pool->next_nonce1, pool->next_n2size);
		}
		
		if (uri_get_param_bool(pool->rpc_url, "xnsub", false))
		{
			sprintf(s, "{\"id\": \"xnsub\", \"method\": \"mining.extranonce.subscribe\", \"params\": []}");
			_stratum_send(pool, s, strlen(s), true);
		}
	} else {
		if (recvd)
		{
			if (trysuggest)
			{
				applog(LOG_DEBUG, "Pool %u: Failed to connect stratum with mining.suggest_target, retrying without", pool->pool_no);
				trysuggest = false;
				goto resend;
			}
			if (!noresume)
			{
				applog(LOG_DEBUG, "Failed to resume stratum, trying afresh");
				noresume = true;
				goto resend;
			}
		}
		applog(LOG_DEBUG, "Initiate stratum failed");
		if (sockd)
			suspend_stratum(pool);
	}

	return ret;
}

bool restart_stratum(struct pool *pool)
{
	bool ret = true;
	
	mutex_lock(&pool->pool_test_lock);
	
	if (pool->stratum_active)
		suspend_stratum(pool);
	
	if (!initiate_stratum(pool))
		return_via(out, ret = false);
	if (!auth_stratum(pool))
		return_via(out, ret = false);
	
out:
	mutex_unlock(&pool->pool_test_lock);
	
	return ret;
}

void dev_error_update(struct cgpu_info *dev, enum dev_reason reason)
{
	dev->device_last_not_well = time(NULL);
	cgtime(&dev->tv_device_last_not_well);
	dev->device_not_well_reason = reason;
}

void dev_error(struct cgpu_info *dev, enum dev_reason reason)
{
	dev_error_update(dev, reason);

	switch (reason) {
		case REASON_THREAD_FAIL_INIT:
			dev->thread_fail_init_count++;
			break;
		case REASON_THREAD_ZERO_HASH:
			dev->thread_zero_hash_count++;
			break;
		case REASON_THREAD_FAIL_QUEUE:
			dev->thread_fail_queue_count++;
			break;
		case REASON_DEV_SICK_IDLE_60:
			dev->dev_sick_idle_60_count++;
			break;
		case REASON_DEV_DEAD_IDLE_600:
			dev->dev_dead_idle_600_count++;
			break;
		case REASON_DEV_NOSTART:
			dev->dev_nostart_count++;
			break;
		case REASON_DEV_OVER_HEAT:
			dev->dev_over_heat_count++;
			break;
		case REASON_DEV_THERMAL_CUTOFF:
			dev->dev_thermal_cutoff_count++;
			break;
		case REASON_DEV_COMMS_ERROR:
			dev->dev_comms_error_count++;
			break;
		case REASON_DEV_THROTTLE:
			dev->dev_throttle_count++;
			break;
	}
}

/* Realloc an existing string to fit an extra string s, appending s to it. */
void *realloc_strcat(char *ptr, char *s)
{
	size_t old = strlen(ptr), len = strlen(s);
	char *ret;

	if (!len)
		return ptr;

	len += old + 1;
	align_len(&len);

	ret = malloc(len);
	if (unlikely(!ret))
		quithere(1, "Failed to malloc");

	sprintf(ret, "%s%s", ptr, s);
	free(ptr);
	return ret;
}

static
bool sanechars[] = {
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false,
	false, false, false, false, false, true , false, false,
	true , true , true , true , true , true , true , true ,
	true , true , false, false, false, false, false, false,
	false, true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true ,
	true , true , true , false, false, false, false, false,
	false, true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true ,
	true , true , true , false, false, false, false, false,
};

char *sanestr(char *o, char *s)
{
	char *rv = o;
	bool br = false;
	
	for ( ; s[0]; ++s)
	{
		if (sanechars[s[0] & 0x7f])
		{
			if (br)
			{
				br = false;
				if (s[0] >= '0' && s[0] <= '9')
					(o++)[0] = '_';
			}
			(o++)[0] = s[0];
		}
		else
		if (o != s && o[-1] >= '0' && o[-1] <= '9')
			br = true;
	}
	o[0] = '\0';
	return rv;
}

void RenameThread(const char* name)
{
#if defined(PR_SET_NAME)
	// Only the first 15 characters are used (16 - NUL terminator)
	prctl(PR_SET_NAME, name, 0, 0, 0);
#elif defined(__APPLE__)
	pthread_setname_np(name);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__))
	pthread_set_name_np(pthread_self(), name);
#else
	// Prevent warnings for unused parameters...
	(void)name;
#endif
}

static pthread_key_t key_bfgtls;
struct bfgtls_data {
	char *bfg_strerror_result;
	size_t bfg_strerror_resultsz;
#ifdef WIN32
	LPSTR bfg_strerror_socketresult;
#endif
#ifdef NEED_BFG_LOWL_VCOM
	struct detectone_meta_info_t __detectone_meta_info;
#endif
	unsigned probe_result_flags;
};

static
struct bfgtls_data *get_bfgtls()
{
	struct bfgtls_data *bfgtls = pthread_getspecific(key_bfgtls);
	if (bfgtls)
		return bfgtls;
	
	void *p;
	
	bfgtls = malloc(sizeof(*bfgtls));
	if (!bfgtls)
		quithere(1, "malloc bfgtls failed");
	p = malloc(64);
	if (!p)
		quithere(1, "malloc bfg_strerror_result failed");
	*bfgtls = (struct bfgtls_data){
		.bfg_strerror_resultsz = 64,
		.bfg_strerror_result = p,
	};
	if (pthread_setspecific(key_bfgtls, bfgtls))
		quithere(1, "pthread_setspecific failed");
	
	return bfgtls;
}

static
void bfgtls_free(void * const p)
{
	struct bfgtls_data * const bfgtls = p;
	free(bfgtls->bfg_strerror_result);
#ifdef WIN32
	if (bfgtls->bfg_strerror_socketresult)
		LocalFree(bfgtls->bfg_strerror_socketresult);
#endif
	free(bfgtls);
}

#ifdef NEED_BFG_LOWL_VCOM
struct detectone_meta_info_t *_detectone_meta_info()
{
	return &get_bfgtls()->__detectone_meta_info;
}
#endif

unsigned *_bfg_probe_result_flags()
{
	return &get_bfgtls()->probe_result_flags;
}

void bfg_init_threadlocal()
{
	if (pthread_key_create(&key_bfgtls, bfgtls_free))
		quithere(1, "pthread_key_create failed");
}

static
bool bfg_grow_buffer(char ** const bufp, size_t * const bufszp, size_t minimum)
{
	if (minimum <= *bufszp)
		return false;
	
	while (minimum > *bufszp)
		*bufszp = 2;
	*bufp = realloc(*bufp, *bufszp);
	if (unlikely(!*bufp))
		quithere(1, "realloc failed");
	
	return true;
}

static
const char *bfg_strcpy_growing_buffer(char ** const bufp, size_t * const bufszp, const char *src)
{
	if (!src)
		return NULL;
	
	const size_t srcsz = strlen(src) + 1;
	
	bfg_grow_buffer(bufp, bufszp, srcsz);
	memcpy(*bufp, src, srcsz);
	
	return *bufp;
}

// Guaranteed to always return some string (or quit)
const char *bfg_strerror(int e, enum bfg_strerror_type type)
{
	static __maybe_unused pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	struct bfgtls_data *bfgtls = get_bfgtls();
	size_t * const bufszp = &bfgtls->bfg_strerror_resultsz;
	char ** const bufp = &bfgtls->bfg_strerror_result;
	const char *have = NULL;
	
	switch (type) {
		case BST_LIBUSB:
// NOTE: Nested preprocessor checks since the latter isn't defined at all without the former
#ifdef HAVE_LIBUSB
#	if HAVE_DECL_LIBUSB_ERROR_NAME
			// libusb makes no guarantees for thread-safety or persistence
			mutex_lock(&mutex);
			have = bfg_strcpy_growing_buffer(bufp, bufszp, libusb_error_name(e));
			mutex_unlock(&mutex);
#	endif
#endif
			break;
		case BST_SOCKET:
		case BST_SYSTEM:
		{
#ifdef WIN32
			// Windows has a different namespace for system and socket errors
			LPSTR *msg = &bfgtls->bfg_strerror_socketresult;
			if (*msg)
				LocalFree(*msg);
			if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, e, 0, (LPSTR)msg, 0, 0))
			{
				LPSTR msgp = *msg;
				size_t n = strlen(msgp);
				while (isCspace(msgp[--n]))
					msgp[n] = '\0';
				return *msg;
			}
			*msg = NULL;
			
			break;
#endif
		}
			// Fallthru on non-WIN32
		case BST_ERRNO:
		{
#ifdef __STRERROR_S_WORKS
			// FIXME: Not sure how to get this on MingW64
retry:
			if (likely(!strerror_s(*bufp, *bufszp, e)))
			{
				if (bfg_grow_buffer(bufp, bufszp, strlen(*bufp) + 2))
					goto retry;
				return *bufp;
			}
// TODO: XSI strerror_r
// TODO: GNU strerror_r
#else
			mutex_lock(&mutex);
			have = bfg_strcpy_growing_buffer(bufp, bufszp, strerror(e));
			mutex_unlock(&mutex);
#endif
		}
	}
	
	if (have)
		return *bufp;
	
	// Failback: Stringify the number
	static const char fmt[] = "%s error #%d", *typestr;
	switch (type) {
		case BST_ERRNO:
			typestr = "System";
			break;
		case BST_SOCKET:
			typestr = "Socket";
			break;
		case BST_LIBUSB:
			typestr = "libusb";
			break;
		default:
			typestr = "Unexpected";
	}
	int sz = snprintf((char*)bfgtls, 0, fmt, typestr, e) + 1;
	bfg_grow_buffer(bufp, bufszp, sz);
	sprintf(*bufp, fmt, typestr, e);
	return *bufp;
}

void notifier_init(notifier_t pipefd)
{
#ifdef WIN32
#define WindowsErrorStr(e)  bfg_strerror(e, BST_SOCKET)
	SOCKET listener, connecter, acceptor;
	listener = bfg_socket(AF_INET, SOCK_STREAM, 0);
	if (listener == INVALID_SOCKET)
		quit(1, "Failed to create listener socket"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	connecter = bfg_socket(AF_INET, SOCK_STREAM, 0);
	if (connecter == INVALID_SOCKET)
		quit(1, "Failed to create connect socket"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	struct sockaddr_in inaddr = {
		.sin_family = AF_INET,
		.sin_addr = {
			.s_addr = htonl(INADDR_LOOPBACK),
		},
		.sin_port = 0,
	};
	{
		static const int reuse = 1;
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
	}
	if (bind(listener, (struct sockaddr*)&inaddr, sizeof(inaddr)) == SOCKET_ERROR)
		quit(1, "Failed to bind listener socket"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	socklen_t inaddr_sz = sizeof(inaddr);
	if (getsockname(listener, (struct sockaddr*)&inaddr, &inaddr_sz) == SOCKET_ERROR)
		quit(1, "Failed to getsockname"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	if (listen(listener, 1) == SOCKET_ERROR)
		quit(1, "Failed to listen"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	inaddr.sin_family = AF_INET;
	inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(connecter, (struct sockaddr*)&inaddr, inaddr_sz) == SOCKET_ERROR)
		quit(1, "Failed to connect"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	acceptor = accept(listener, NULL, NULL);
	if (acceptor == INVALID_SOCKET)
		quit(1, "Failed to accept"IN_FMT_FFL": %s",
		     __FILE__, __func__, __LINE__, WindowsErrorStr(WSAGetLastError()));
	closesocket(listener);
	pipefd[0] = connecter;
	pipefd[1] = acceptor;
#else
	if (pipe(pipefd))
		quithere(1, "Failed to create pipe");
#endif
}


void *bfg_slurp_file(void * const bufp, size_t bufsz, const char * const filename)
{
	char *buf = bufp;
	FILE * const F = fopen(filename, "r");
	if (!F)
		goto err;
	
	if (!buf)
	{
		fseek(F, 0, SEEK_END);
		const long filesz = ftell(F);
		if (unlikely(filesz < 0))
		{
			fclose(F);
			goto err;
		}
		rewind(F);
		bufsz = filesz + 1;
		buf = malloc(bufsz);
	}
	const size_t rsz = fread(buf, 1, bufsz - 1, F);
	fclose(F);
	buf[rsz] = '\0';
	return buf;

err:
	if (buf)
		buf[0] = '\0';
	return NULL;
}


void notifier_wake(notifier_t fd)
{
	if (fd[1] == INVSOCK)
		return;
	if (1 !=
#ifdef WIN32
	send(fd[1], "\0", 1, 0)
#else
	write(fd[1], "\0", 1)
#endif
	)
		applog(LOG_WARNING, "Error trying to wake notifier");
}

void notifier_read(notifier_t fd)
{
	char buf[0x10];
#ifdef WIN32
	IGNORE_RETURN_VALUE(recv(fd[0], buf, sizeof(buf), 0));
#else
	IGNORE_RETURN_VALUE(read(fd[0], buf, sizeof(buf)));
#endif
}

bool notifier_wait(notifier_t notifier, const struct timeval *tvp_timeout)
{
	struct timeval tv_now, tv_timeout;
	fd_set rfds;
	int e;
	
	while (true)
	{
		FD_ZERO(&rfds);
		FD_SET(notifier[0], &rfds);
		tv_timeout = *tvp_timeout;
		timer_set_now(&tv_now);
		e = select(notifier[0]+1, &rfds, NULL, NULL, select_timeout(&tv_timeout, &tv_now));
		if (e > 0)
			return true;
		if (e == 0)
			return false;
	}
}

bool notifier_wait_us(notifier_t notifier, const unsigned long long usecs)
{
	struct timeval tv_timeout = TIMEVAL_USECS(usecs);
	return notifier_wait(notifier, &tv_timeout);
}

void notifier_reset(notifier_t notifier)
{
	fd_set rfds;
	struct timeval tv_timeout = { .tv_sec = 0, };
	FD_ZERO(&rfds);
	FD_SET(notifier[0], &rfds);
	while (select(notifier[0]+1, &rfds, NULL, NULL, &tv_timeout) != 0)
		notifier_read(notifier);
}

void notifier_init_invalid(notifier_t fd)
{
	fd[0] = fd[1] = INVSOCK;
}

void notifier_destroy(notifier_t fd)
{
#ifdef WIN32
	closesocket(fd[0]);
	closesocket(fd[1]);
#else
	close(fd[0]);
	close(fd[1]);
#endif
	fd[0] = fd[1] = INVSOCK;
}

void _bytes_alloc_failure(size_t sz)
{
	quit(1, "bytes_resize failed to allocate %lu bytes", (unsigned long)sz);
}


char *trimmed_strdup(const char *s)
{
	size_t n;
	char *c;
	
	while (isspace(s[0]))
		++s;
	n = strlen(s) - 1;
	while (isspace(s[n]))
		--n;
	++n;
	c = malloc(n + 1);
	c[n] = '\0';
	memcpy(c, s, n);
	return c;
}


void *cmd_thread(void *cmdp)
{
	const char *cmd = cmdp;
	applog(LOG_DEBUG, "Executing command: %s", cmd);
	int rc = system(cmd);
	if (rc)
		applog(LOG_WARNING, "Command returned %d exit code: %s", rc, cmd);
	return NULL;
}

void run_cmd(const char *cmd)
{
	if (!cmd)
		return;
	pthread_t pth;
	pthread_create(&pth, NULL, cmd_thread, (void*)cmd);
}


uint8_t crc5usb(unsigned char *ptr, uint8_t len)
{
    uint8_t i, j, k;
    uint8_t crc = 0x1f;
	
    uint8_t crcin[5] = {1, 1, 1, 1, 1};
    uint8_t crcout[5] = {1, 1, 1, 1, 1};
    uint8_t din = 0;
	
    j = 0x80;
    k = 0;
	
    for (i = 0; i < len; i++)
    {
    	if (*ptr & j)
    		din = 1;
    	else
    		din = 0;
		
    	crcout[0] = crcin[4] ^ din;
    	crcout[1] = crcin[0];
    	crcout[2] = crcin[1] ^ crcin[4] ^ din;
    	crcout[3] = crcin[2];
    	crcout[4] = crcin[3];
		
        j = j >> 1;
        k++;
        if (k == 8)
        {
            j = 0x80;
            k = 0;
            ptr++;
        }
        memcpy(crcin, crcout, 5);
    }
	
    crc = 0;
    if(crcin[4])
    	crc |= 0x10;
	
    if(crcin[3])
    	crc |= 0x08;
	
    if(crcin[2])
    	crc |= 0x04;
	
    if(crcin[1])
    	crc |= 0x02;
	
    if(crcin[0])
    	crc |= 0x01;
	
    return crc;
}

static uint8_t _crc8ccitt_table[0x100];

void bfg_init_checksums(void)
{
	for (int i = 0; i < 0x100; ++i)
	{
		uint8_t crc = i;
		for (int j = 0; j < 8; ++j)
			crc = (crc << 1) ^ ((crc & 0x80) ? 7 : 0);
		_crc8ccitt_table[i] = crc & 0xff;
	}
}

uint8_t crc8ccitt(const void * const buf, const size_t buflen)
{
	const uint8_t *p = buf;
	uint8_t crc = 0xff;
	for (int i = 0; i < buflen; ++i)
		crc = _crc8ccitt_table[crc ^ *p++];
	return crc;
}

static uint16_t crc16tab[] = {
	0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
	0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
	0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
	0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
	0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
	0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
	0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
	0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
	0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
	0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
	0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
	0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
	0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
	0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
	0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
	0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
	0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
	0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
	0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
	0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
	0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
	0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
	0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
	0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
	0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
	0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
	0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
	0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
	0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
	0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
	0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
	0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0,
};

static
uint16_t crc16_floating(uint16_t next_byte, uint16_t seed)
{
	return ((seed << 8) ^ crc16tab[(seed >> 8) ^ next_byte]) & 0xFFFF;
}

uint16_t crc16(const void *p, size_t sz, uint16_t crc)
{
	const uint8_t * const s = p;
	for (size_t i = 0; i < sz; ++i)
		crc = crc16_floating(s[i], crc);
	return crc;
}
