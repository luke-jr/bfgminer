/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <jansson.h>
#include <curl/curl.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
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
# include <ws2tcpip.h>
# include <mmsystem.h>
#endif

#include "miner.h"
#include "elist.h"
#include "compat.h"
#include "util.h"

bool successful_connect = false;
struct timeval nettime;

struct data_buffer {
	void		*buf;
	size_t		len;
};

struct upload_buffer {
	const void	*buf;
	size_t		len;
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
	struct list_head	q_node;
};

static void databuf_free(struct data_buffer *db)
{
	if (!db)
		return;

	free(db->buf);

	memset(db, 0, sizeof(*db));
}

static size_t all_data_cb(const void *ptr, size_t size, size_t nmemb,
			  void *user_data)
{
	struct data_buffer *db = user_data;
	size_t len = size * nmemb;
	size_t oldlen, newlen;
	void *newmem;
	static const unsigned char zero = 0;

	oldlen = db->len;
	newlen = oldlen + len;

	newmem = realloc(db->buf, newlen + 1);
	if (!newmem)
		return 0;

	db->buf = newmem;
	db->len = newlen;
	memcpy(db->buf + oldlen, ptr, len);
	memcpy(db->buf + newlen, &zero, 1);	/* null terminate */

	return len;
}

static size_t upload_data_cb(void *ptr, size_t size, size_t nmemb,
			     void *user_data)
{
	struct upload_buffer *ub = user_data;
	unsigned int len = size * nmemb;

	if (len > ub->len)
		len = ub->len;

	if (len) {
		memcpy(ptr, ub->buf, len);
		ub->buf += len;
		ub->len -= len;
	}

	return len;
}

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
	while ((remlen > 0) && (isspace(*rem))) {
		remlen--;
		rem++;
	}

	memcpy(val, rem, remlen);	/* store value, trim trailing ws */
	val[remlen] = 0;
	while ((*val) && (isspace(val[strlen(val) - 1])))
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

static void keep_sockalive(SOCKETTYPE fd)
{
	const int tcp_one = 1;
#ifndef WIN32
	const int tcp_keepidle = 45;
	const int tcp_keepintvl = 30;
	int flags = fcntl(fd, F_GETFL, 0);

	fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	u_long flags = 1;

	ioctlsocket(fd, FIONBIO, &flags);
#endif

	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const void *)&tcp_one, sizeof(tcp_one));
	if (!opt_delaynet)
#ifndef __linux
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one));
#else /* __linux */
		setsockopt(fd, SOL_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &tcp_keepidle, sizeof(tcp_keepidle));
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &tcp_keepintvl, sizeof(tcp_keepintvl));
#endif /* __linux */

#ifdef __APPLE_CC__
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &tcp_keepintvl, sizeof(tcp_keepintvl));
#endif /* __APPLE_CC__ */

}

#if CURL_HAS_KEEPALIVE
static void keep_curlalive(CURL *curl)
{
	const int tcp_keepidle = 45;
	const int tcp_keepintvl = 30;
	const long int keepalive = 1;

	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, keepalive);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, tcp_keepidle);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, tcp_keepintvl);
}
#else
static void keep_curlalive(CURL *curl)
{
	SOCKETTYPE sock;

	curl_easy_getinfo(curl, CURLINFO_LASTSOCKET, (long *)&sock);
	keep_sockalive(sock);
}
#endif

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
			 __maybe_unused char *data, size_t size, void *userdata)
{
	struct pool *pool = (struct pool *)userdata;

	switch(type) {
		case CURLINFO_HEADER_IN:
		case CURLINFO_DATA_IN:
		case CURLINFO_SSL_DATA_IN:
			pool->cgminer_pool_stats.net_bytes_received += size;
			break;
		case CURLINFO_HEADER_OUT:
		case CURLINFO_DATA_OUT:
		case CURLINFO_SSL_DATA_OUT:
			pool->cgminer_pool_stats.net_bytes_sent += size;
			break;
		case CURLINFO_TEXT:
		default:
			break;
	}
	return 0;
}

json_t *json_rpc_call(CURL *curl, const char *url,
		      const char *userpass, const char *rpc_req,
		      bool probe, bool longpoll, int *rolltime,
		      struct pool *pool, bool share)
{
	long timeout = longpoll ? (60 * 60) : 60;
	struct data_buffer all_data = {NULL, 0};
	struct header_info hi = {NULL, 0, NULL, NULL, false, false, false};
	char len_hdr[64], user_agent_hdr[128];
	char curl_err_str[CURL_ERROR_SIZE];
	struct curl_slist *headers = NULL;
	struct upload_buffer upload_data;
	json_t *val, *err_val, *res_val;
	bool probing = false;
	double byte_count;
	json_error_t err;
	int rc;

	memset(&err, 0, sizeof(err));

	/* it is assumed that 'curl' is freshly [re]initialized at this pt */

	if (probe)
		probing = !pool->probed;
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

	// CURLOPT_VERBOSE won't write to stderr if we use CURLOPT_DEBUGFUNCTION
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
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_data_cb);
	curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, resp_hdr_cb);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hi);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
	if (pool->rpc_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, pool->rpc_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, pool->rpc_proxytype);
	} else if (opt_socks_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, opt_socks_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
	}
	if (userpass) {
		curl_easy_setopt(curl, CURLOPT_USERPWD, userpass);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	}
	if (longpoll)
		keep_curlalive(curl);
	curl_easy_setopt(curl, CURLOPT_POST, 1);

	if (opt_protocol)
		applog(LOG_DEBUG, "JSON protocol request:\n%s", rpc_req);

	upload_data.buf = rpc_req;
	upload_data.len = strlen(rpc_req);
	sprintf(len_hdr, "Content-Length: %lu",
		(unsigned long) upload_data.len);
	sprintf(user_agent_hdr, "User-Agent: %s", PACKAGE_STRING);

	headers = curl_slist_append(headers,
		"Content-type: application/json");
	headers = curl_slist_append(headers,
		"X-Mining-Extensions: longpoll midstate rollntime submitold");

	if (likely(global_hashrate)) {
		char ghashrate[255];

		sprintf(ghashrate, "X-Mining-Hashrate: %llu", global_hashrate);
		headers = curl_slist_append(headers, ghashrate);
	}

	headers = curl_slist_append(headers, len_hdr);
	headers = curl_slist_append(headers, user_agent_hdr);
	headers = curl_slist_append(headers, "Expect:"); /* disable Expect hdr*/

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

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

	rc = curl_easy_perform(curl);
	if (rc) {
		applog(LOG_INFO, "HTTP request failed: %s", curl_err_str);
		goto err_out;
	}

	if (!all_data.buf) {
		applog(LOG_DEBUG, "Empty data received in json_rpc_call.");
		goto err_out;
	}

	pool->cgminer_pool_stats.times_sent++;
	if (curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &byte_count) == CURLE_OK)
		pool->cgminer_pool_stats.bytes_sent += byte_count;
	pool->cgminer_pool_stats.times_received++;
	if (curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &byte_count) == CURLE_OK)
		pool->cgminer_pool_stats.bytes_received += byte_count;

	if (probing) {
		pool->probed = true;
		/* If X-Long-Polling was found, activate long polling */
		if (hi.lp_path) {
			if (pool->hdr_path != NULL)
				free(pool->hdr_path);
			pool->hdr_path = hi.lp_path;
		} else
			pool->hdr_path = NULL;
		if (hi.stratum_url) {
			pool->stratum_url = hi.stratum_url;
			hi.stratum_url = NULL;
		}
	} else {
		if (hi.lp_path) {
			free(hi.lp_path);
			hi.lp_path = NULL;
		}
		if (hi.stratum_url) {
			free(hi.stratum_url);
			hi.stratum_url = NULL;
		}
	}

	*rolltime = hi.rolltime;
	pool->cgminer_pool_stats.rolltime = hi.rolltime;
	pool->cgminer_pool_stats.hadrolltime = hi.hadrolltime;
	pool->cgminer_pool_stats.canroll = hi.canroll;
	pool->cgminer_pool_stats.hadexpire = hi.hadexpire;

	val = JSON_LOADS(all_data.buf, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);

		if (opt_protocol)
			applog(LOG_DEBUG, "JSON protocol response:\n%s", (char *)(all_data.buf));

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

		goto err_out;
	}

	if (hi.reason) {
		json_object_set_new(val, "reject-reason", json_string(hi.reason));
		free(hi.reason);
		hi.reason = NULL;
	}
	successful_connect = true;
	databuf_free(&all_data);
	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	return val;

err_out:
	databuf_free(&all_data);
	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	if (!successful_connect)
		applog(LOG_DEBUG, "Failed to connect in json_rpc_call");
	curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
	return NULL;
}

#if (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 10) || (LIBCURL_VERSION_MAJOR > 7)
static struct {
	const char *name;
	curl_proxytype proxytype;
} proxynames[] = {
	{ "http:",	CURLPROXY_HTTP },
#if (LIBCURL_VERSION_MAJOR > 7) || (LIBCURL_VERSION_MINOR > 19) || (LIBCURL_VERSION_MINOR == 19 && LIBCURL_VERSION_PATCH >= 4)
	{ "http0:",	CURLPROXY_HTTP_1_0 },
#endif
#if (LIBCURL_VERSION_MAJOR > 7) || (LIBCURL_VERSION_MINOR > 15) || (LIBCURL_VERSION_MINOR == 15 && LIBCURL_VERSION_PATCH >= 2)
	{ "socks4:",	CURLPROXY_SOCKS4 },
#endif
	{ "socks5:",	CURLPROXY_SOCKS5 },
#if (LIBCURL_VERSION_MAJOR > 7) || (LIBCURL_VERSION_MINOR >= 18)
	{ "socks4a:",	CURLPROXY_SOCKS4A },
	{ "socks5h:",	CURLPROXY_SOCKS5_HOSTNAME },
#endif
	{ NULL,	0 }
};
#endif

const char *proxytype(curl_proxytype proxytype)
{
	int i;

	for (i = 0; proxynames[i].name; i++)
		if (proxynames[i].proxytype == proxytype)
			return proxynames[i].name;

	return "invalid";
}

char *get_proxy(char *url, struct pool *pool)
{
	pool->rpc_proxy = NULL;

#if (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 10) || (LIBCURL_VERSION_MAJOR > 7)
	char *split;
	int plen, len, i;

	for (i = 0; proxynames[i].name; i++) {
		plen = strlen(proxynames[i].name);
		if (strncmp(url, proxynames[i].name, plen) == 0) {
			if (!(split = strchr(url, '|')))
				return url;

			*split = '\0';
			len = split - url;
			pool->rpc_proxy = malloc(1 + len - plen);
			if (!(pool->rpc_proxy))
				quit(1, "Failed to malloc rpc_proxy");

			strcpy(pool->rpc_proxy, url + plen);
			pool->rpc_proxytype = proxynames[i].proxytype;
			url = split + 1;
			break;
		}
	}
#endif
	return url;
}

/* Returns a malloced array string of a binary value of arbitrary length. The
 * array is rounded up to a 4 byte size to appease architectures that need
 * aligned array  sizes */
char *bin2hex(const unsigned char *p, size_t len)
{
	unsigned int i;
	ssize_t slen;
	char *s;

	slen = len * 2 + 1;
	if (slen % 4)
		slen += 4 - (slen % 4);
	s = calloc(slen, 1);
	if (unlikely(!s))
		quit(1, "Failed to calloc in bin2hex");

	for (i = 0; i < len; i++)
		sprintf(s + (i * 2), "%02x", (unsigned int) p[i]);

	return s;
}

/* Does the reverse of bin2hex but does not allocate any ram */
bool hex2bin(unsigned char *p, const char *hexstr, size_t len)
{
	bool ret = false;

	while (*hexstr && len) {
		char hex_byte[4];
		unsigned int v;

		if (unlikely(!hexstr[1])) {
			applog(LOG_ERR, "hex2bin str truncated");
			return ret;
		}

		memset(hex_byte, 0, 4);
		hex_byte[0] = hexstr[0];
		hex_byte[1] = hexstr[1];

		if (unlikely(sscanf(hex_byte, "%x", &v) != 1)) {
			applog(LOG_INFO, "hex2bin sscanf '%s' failed", hex_byte);
			return ret;
		}

		*p = (unsigned char) v;

		p++;
		hexstr += 2;
		len--;
	}

	if (likely(len == 0 && *hexstr == 0))
		ret = true;
	return ret;
}

bool fulltest(const unsigned char *hash, const unsigned char *target)
{
	unsigned char hash_swap[32], target_swap[32];
	uint32_t *hash32 = (uint32_t *) hash_swap;
	uint32_t *target32 = (uint32_t *) target_swap;
	char *hash_str, *target_str;
	bool rc = true;
	int i;

	swap256(hash_swap, hash);
	swap256(target_swap, target);

	for (i = 0; i < 32/4; i++) {
		uint32_t h32tmp = htobe32(hash32[i]);
		uint32_t t32tmp = htole32(target32[i]);

		target32[i] = swab32(target32[i]);	/* for printing */

		if (h32tmp > t32tmp) {
			rc = false;
			break;
		}
		if (h32tmp < t32tmp) {
			rc = true;
			break;
		}
	}

	if (opt_debug) {
		hash_str = bin2hex(hash_swap, 32);
		target_str = bin2hex(target_swap, 32);

		applog(LOG_DEBUG, " Proof: %s\nTarget: %s\nTrgVal? %s",
			hash_str,
			target_str,
			rc ? "YES (hash <= target)" :
			     "no (false positive; hash > target)");

		free(hash_str);
		free(target_str);
	}

	return rc;
}

struct thread_q *tq_new(void)
{
	struct thread_q *tq;

	tq = calloc(1, sizeof(*tq));
	if (!tq)
		return NULL;

	INIT_LIST_HEAD(&tq->q);
	pthread_mutex_init(&tq->mutex, NULL);
	pthread_cond_init(&tq->cond, NULL);

	return tq;
}

void tq_free(struct thread_q *tq)
{
	struct tq_ent *ent, *iter;

	if (!tq)
		return;

	list_for_each_entry_safe(ent, iter, &tq->q, q_node) {
		list_del(&ent->q_node);
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
	INIT_LIST_HEAD(&ent->q_node);

	mutex_lock(&tq->mutex);
	if (!tq->frozen) {
		list_add_tail(&ent->q_node, &tq->q);
	} else {
		free(ent);
		rc = false;
	}
	pthread_cond_signal(&tq->cond);
	mutex_unlock(&tq->mutex);

	return rc;
}

void *tq_pop(struct thread_q *tq, const struct timespec *abstime)
{
	struct tq_ent *ent;
	void *rval = NULL;
	int rc;

	mutex_lock(&tq->mutex);
	if (!list_empty(&tq->q))
		goto pop;

	if (abstime)
		rc = pthread_cond_timedwait(&tq->cond, &tq->mutex, abstime);
	else
		rc = pthread_cond_wait(&tq->cond, &tq->mutex);
	if (rc)
		goto out;
	if (list_empty(&tq->q))
		goto out;
pop:
	ent = list_entry(tq->q.next, struct tq_ent, q_node);
	rval = ent->data;

	list_del(&ent->q_node);
	free(ent);
out:
	mutex_unlock(&tq->mutex);

	return rval;
}

int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg)
{
	cgsem_init(&thr->sem);

	return pthread_create(&thr->pth, attr, start, arg);
}

void thr_info_cancel(struct thr_info *thr)
{
	if (!thr)
		return;

	if (PTH(thr) != 0L) {
		pthread_cancel(thr->pth);
		PTH(thr) = 0L;
	}
	cgsem_destroy(&thr->sem);
}

/* Provide a ms based sleep that uses nanosleep to avoid poor usleep accuracy
 * on SMP machines */
void nmsleep(unsigned int msecs)
{
	struct timespec twait, tleft;
	int ret;
	ldiv_t d;

#ifdef WIN32
	timeBeginPeriod(1);
#endif
	d = ldiv(msecs, 1000);
	tleft.tv_sec = d.quot;
	tleft.tv_nsec = d.rem * 1000000;
	do {
		twait.tv_sec = tleft.tv_sec;
		twait.tv_nsec = tleft.tv_nsec;
		ret = nanosleep(&twait, &tleft);
	} while (ret == -1 && errno == EINTR);
#ifdef WIN32
	timeEndPeriod(1);
#endif
}

/* Same for usecs */
void nusleep(unsigned int usecs)
{
	struct timespec twait, tleft;
	int ret;
	ldiv_t d;

#ifdef WIN32
	timeBeginPeriod(1);
#endif
	d = ldiv(usecs, 1000000);
	tleft.tv_sec = d.quot;
	tleft.tv_nsec = d.rem * 1000;
	do {
		twait.tv_sec = tleft.tv_sec;
		twait.tv_nsec = tleft.tv_nsec;
		ret = nanosleep(&twait, &tleft);
	} while (ret == -1 && errno == EINTR);
#ifdef WIN32
	timeEndPeriod(1);
#endif
}

/* This is a cgminer gettimeofday wrapper. Since we always call gettimeofday
 * with tz set to NULL, and windows' default resolution is only 15ms, this
 * gives us higher resolution times on windows. */
void cgtime(struct timeval *tv)
{
#ifdef WIN32
	timeBeginPeriod(1);
#endif
	gettimeofday(tv, NULL);
#ifdef WIN32
	timeEndPeriod(1);
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

bool extract_sockaddr(struct pool *pool, char *url)
{
	char *url_begin, *url_end, *ipv6_begin, *ipv6_end, *port_start = NULL;
	char url_address[256], port[6];
	int url_len, port_len = 0;

	pool->sockaddr_url = url;
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

	sprintf(url_address, "%.*s", url_len, url_begin);

	if (port_len)
		snprintf(port, 6, "%.*s", port_len, port_start);
	else
		strcpy(port, "80");

	pool->stratum_port = strdup(port);
	pool->sockaddr_url = strdup(url_address);

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
		ssize_t sent;
		fd_set wd;

		FD_ZERO(&wd);
		FD_SET(sock, &wd);
		if (select(sock + 1, NULL, &wd, NULL, &timeout) < 1)
			return SEND_SELECTFAIL;
#ifdef __APPLE__
		sent = send(pool->sock, s + ssent, len, SO_NOSIGPIPE);
#elif WIN32
		sent = send(pool->sock, s + ssent, len, 0);
#else
		sent = send(pool->sock, s + ssent, len, MSG_NOSIGNAL);
#endif
		if (sent < 0) {
			if (!sock_blocks())
				return SEND_SENDFAIL;
			sent = 0;
		}
		ssent += sent;
		len -= sent;
	}

	pool->cgminer_pool_stats.times_sent++;
	pool->cgminer_pool_stats.bytes_sent += ssent;
	pool->cgminer_pool_stats.net_bytes_sent += ssent;
	return SEND_OK;
}

bool stratum_send(struct pool *pool, char *s, ssize_t len)
{
	enum send_ret ret = SEND_INACTIVE;

	if (opt_protocol)
		applog(LOG_DEBUG, "SEND: %s", s);

	mutex_lock(&pool->stratum_lock);
	if (pool->stratum_active)
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

static bool socket_full(struct pool *pool, bool wait)
{
	SOCKETTYPE sock = pool->sock;
	struct timeval timeout;
	fd_set rd;

	FD_ZERO(&rd);
	FD_SET(sock, &rd);
	timeout.tv_usec = 0;
	if (wait)
		timeout.tv_sec = 60;
	else
		timeout.tv_sec = 1;
	if (select(sock + 1, &rd, NULL, NULL, &timeout) > 0)
		return true;
	return false;
}

/* Check to see if Santa's been good to you */
bool sock_full(struct pool *pool)
{
	if (strlen(pool->sockbuf))
		return true;

	return (socket_full(pool, false));
}

static void clear_sockbuf(struct pool *pool)
{
	strcpy(pool->sockbuf, "");
}

static void clear_sock(struct pool *pool)
{
	ssize_t n;

	mutex_lock(&pool->stratum_lock);
	do {
		if (pool->sock)
			n = recv(pool->sock, pool->sockbuf, RECVSIZE, 0);
		else
			n = 0;
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
	// applog(LOG_DEBUG, "Recallocing pool sockbuf to %d", new);
	pool->sockbuf = realloc(pool->sockbuf, new);
	if (!pool->sockbuf)
		quit(1, "Failed to realloc pool sockbuf in recalloc_sock");
	memset(pool->sockbuf + old, 0, new - old);
	pool->sockbuf_size = new;
}

/* Peeks at a socket to find the first end of line and then reads just that
 * from the socket and returns that as a malloced char */
char *recv_line(struct pool *pool)
{
	ssize_t len, buflen;
	char *tok, *sret = NULL;

	if (!strstr(pool->sockbuf, "\n")) {
		struct timeval rstart, now;

		cgtime(&rstart);
		if (!socket_full(pool, true)) {
			applog(LOG_DEBUG, "Timed out waiting for data on socket_full");
			goto out;
		}

		do {
			char s[RBUFSIZE];
			size_t slen;
			ssize_t n;

			memset(s, 0, RBUFSIZE);
			n = recv(pool->sock, s, RECVSIZE, 0);
			if (!n) {
				applog(LOG_DEBUG, "Socket closed waiting in recv_line");
				suspend_stratum(pool);
				break;
			}
			if (n < 0) {
				if (!sock_blocks() || !socket_full(pool, false)) {
					applog(LOG_DEBUG, "Failed to recv sock in recv_line");
					suspend_stratum(pool);
					break;
				}
			} else {
				slen = strlen(s);
				recalloc_sock(pool, slen);
				strcat(pool->sockbuf, s);
			}
			cgtime(&now);
		} while (tdiff(&now, &rstart) < 60 && !strstr(pool->sockbuf, "\n"));
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
	pool->cgminer_pool_stats.net_bytes_received += len;
out:
	if (!sret)
		clear_sock(pool);
	else if (opt_protocol)
		applog(LOG_DEBUG, "RECVD: %s", sret);
	return sret;
}

/* Extracts a string value from a json array with error checking. To be used
 * when the value of the string returned is only examined and not to be stored.
 * See json_array_string below */
static char *__json_array_string(json_t *val, unsigned int entry)
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

	return (char *)json_string_value(arr_entry);
}

/* Creates a freshly malloced dup of __json_array_string */
static char *json_array_string(json_t *val, unsigned int entry)
{
	char *buf = __json_array_string(val, entry);

	if (buf)
		return strdup(buf);
	return NULL;
}

static bool parse_notify(struct pool *pool, json_t *val)
{
	char *job_id, *prev_hash, *coinbase1, *coinbase2, *bbversion, *nbit, *ntime;
	bool clean, ret = false;
	int merkles, i;
	json_t *arr;

	arr = json_array_get(val, 4);
	if (!arr || !json_is_array(arr))
		goto out;

	merkles = json_array_size(arr);

	job_id = json_array_string(val, 0);
	prev_hash = json_array_string(val, 1);
	coinbase1 = json_array_string(val, 2);
	coinbase2 = json_array_string(val, 3);
	bbversion = json_array_string(val, 5);
	nbit = json_array_string(val, 6);
	ntime = json_array_string(val, 7);
	clean = json_is_true(json_array_get(val, 8));

	if (!job_id || !prev_hash || !coinbase1 || !coinbase2 || !bbversion || !nbit || !ntime) {
		/* Annoying but we must not leak memory */
		if (job_id)
			free(job_id);
		if (prev_hash)
			free(prev_hash);
		if (coinbase1)
			free(coinbase1);
		if (coinbase2)
			free(coinbase2);
		if (bbversion)
			free(bbversion);
		if (nbit)
			free(nbit);
		if (ntime)
			free(ntime);
		goto out;
	}

	cg_wlock(&pool->data_lock);
	free(pool->swork.job_id);
	free(pool->swork.prev_hash);
	free(pool->swork.coinbase1);
	free(pool->swork.coinbase2);
	free(pool->swork.bbversion);
	free(pool->swork.nbit);
	free(pool->swork.ntime);
	pool->swork.job_id = job_id;
	pool->swork.prev_hash = prev_hash;
	pool->swork.coinbase1 = coinbase1;
	pool->swork.cb1_len = strlen(coinbase1) / 2;
	pool->swork.coinbase2 = coinbase2;
	pool->swork.cb2_len = strlen(coinbase2) / 2;
	pool->swork.bbversion = bbversion;
	pool->swork.nbit = nbit;
	pool->swork.ntime = ntime;
	pool->swork.clean = clean;
	pool->swork.cb_len = pool->swork.cb1_len + pool->n1_len + pool->n2size + pool->swork.cb2_len;

	for (i = 0; i < pool->swork.merkles; i++)
		free(pool->swork.merkle[i]);
	if (merkles) {
		pool->swork.merkle = realloc(pool->swork.merkle, sizeof(char *) * merkles + 1);
		for (i = 0; i < merkles; i++)
			pool->swork.merkle[i] = json_array_string(arr, i);
	}
	pool->swork.merkles = merkles;
	if (clean)
		pool->nonce2 = 0;
	pool->swork.header_len = strlen(pool->swork.bbversion) +
				 strlen(pool->swork.prev_hash) +
				 strlen(pool->swork.ntime) +
				 strlen(pool->swork.nbit) +
	/* merkle_hash */	 32 +
	/* nonce */		 8 +
	/* workpadding */	 96;
	pool->swork.header_len = pool->swork.header_len * 2 + 1;
	align_len(&pool->swork.header_len);
	cg_wunlock(&pool->data_lock);

	if (opt_protocol) {
		applog(LOG_DEBUG, "job_id: %s", job_id);
		applog(LOG_DEBUG, "prev_hash: %s", prev_hash);
		applog(LOG_DEBUG, "coinbase1: %s", coinbase1);
		applog(LOG_DEBUG, "coinbase2: %s", coinbase2);
		for (i = 0; i < merkles; i++)
			applog(LOG_DEBUG, "merkle%d: %s", i, pool->swork.merkle[i]);
		applog(LOG_DEBUG, "bbversion: %s", bbversion);
		applog(LOG_DEBUG, "nbit: %s", nbit);
		applog(LOG_DEBUG, "ntime: %s", ntime);
		applog(LOG_DEBUG, "clean: %s", clean ? "yes" : "no");
	}

	/* A notify message is the closest stratum gets to a getwork */
	pool->getwork_requested++;
	total_getworks++;
	ret = true;
out:
	return ret;
}

static bool parse_diff(struct pool *pool, json_t *val)
{
	double old_diff, diff;

	diff = json_number_value(json_array_get(val, 0));
	if (diff == 0)
		return false;

	cg_wlock(&pool->data_lock);
	old_diff = pool->swork.diff;
	pool->swork.diff = diff;
	cg_wunlock(&pool->data_lock);

	if (old_diff != diff) {
		int idiff = diff;

		if ((double)idiff == diff)
			applog(LOG_NOTICE, "Pool %d difficulty changed to %d",
			       pool->pool_no, idiff);
		else
			applog(LOG_NOTICE, "Pool %d difficulty changed to %f",
			       pool->pool_no, diff);
	} else
		applog(LOG_DEBUG, "Pool %d difficulty set to %f", pool->pool_no,
		       diff);

	return true;
}

static bool parse_reconnect(struct pool *pool, json_t *val)
{
	char *url, *port, address[256];

	memset(address, 0, 255);
	url = (char *)json_string_value(json_array_get(val, 0));
	if (!url)
		url = pool->sockaddr_url;

	port = (char *)json_string_value(json_array_get(val, 1));
	if (!port)
		port = pool->stratum_port;

	sprintf(address, "%s:%s", url, port);

	if (!extract_sockaddr(pool, address))
		return false;

	pool->stratum_url = pool->sockaddr_url;

	applog(LOG_NOTICE, "Reconnect requested from pool %d to %s", pool->pool_no, address);

	if (!restart_stratum(pool))
		return false;

	return true;
}

static bool send_version(struct pool *pool, json_t *val)
{
	char s[RBUFSIZE];
	int id = json_integer_value(json_object_get(val, "id"));
	
	if (!id)
		return false;

	sprintf(s, "{\"id\": %d, \"result\": \""PACKAGE"/"VERSION"\", \"error\": null}", id);
	if (!stratum_send(pool, s, strlen(s)))
		return false;

	return true;
}

static bool show_message(struct pool *pool, json_t *val)
{
	char *msg;

	if (!json_is_array(val))
		return false;
	msg = (char *)json_string_value(json_array_get(val, 0));
	if (!msg)
		return false;
	applog(LOG_NOTICE, "Pool %d message: %s", pool->pool_no, msg);
	return true;
}

bool parse_method(struct pool *pool, char *s)
{
	json_t *val = NULL, *method, *err_val, *params;
	json_error_t err;
	bool ret = false;
	char *buf;

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

	buf = (char *)json_string_value(method);
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

	if (!strncasecmp(buf, "client.show_message", 19) && show_message(pool, params)) {
		ret = true;
		goto out;
	}
out:
	if (val)
		json_decref(val);

	return ret;
}

bool auth_stratum(struct pool *pool)
{
	json_t *val = NULL, *res_val, *err_val;
	char s[RBUFSIZE], *sret = NULL;
	json_error_t err;
	bool ret = false;

	sprintf(s, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}",
		swork_id++, pool->rpc_user, pool->rpc_pass);

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
			break;
	}

	val = JSON_LOADS(sret, &err);
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

	return ret;
}

static bool setup_stratum_socket(struct pool *pool)
{
	struct addrinfo *servinfo, *hints, *p;
	int sockd;

	mutex_lock(&pool->stratum_lock);
	pool->stratum_active = false;
	if (pool->sock)
		CLOSESOCKET(pool->sock);
	pool->sock = 0;
	mutex_unlock(&pool->stratum_lock);

	hints = &pool->stratum_hints;
	memset(hints, 0, sizeof(struct addrinfo));
	hints->ai_family = AF_UNSPEC;
	hints->ai_socktype = SOCK_STREAM;
	if (getaddrinfo(pool->sockaddr_url, pool->stratum_port, hints, &servinfo) != 0) {
		if (!pool->probed) {
			applog(LOG_WARNING, "Failed to resolve (?wrong URL) %s:%s",
			       pool->sockaddr_url, pool->stratum_port);
			pool->probed = true;
		} else {
			applog(LOG_INFO, "Failed to getaddrinfo for %s:%s",
			       pool->sockaddr_url, pool->stratum_port);
		}
		return false;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		sockd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockd == -1) {
			applog(LOG_DEBUG, "Failed socket");
			continue;
		}

		if (connect(sockd, p->ai_addr, p->ai_addrlen) == -1) {
			CLOSESOCKET(sockd);
			applog(LOG_DEBUG, "Failed connect");
			continue;
		}

		break;
	}
	if (p == NULL) {
		applog(LOG_INFO, "Failed to find a stratum servinfo on %s:%s",
		       pool->sockaddr_url, pool->stratum_port);
		freeaddrinfo(servinfo);
		return false;
	}
	freeaddrinfo(servinfo);

	if (!pool->sockbuf) {
		pool->sockbuf = calloc(RBUFSIZE, 1);
		if (!pool->sockbuf)
			quit(1, "Failed to calloc pool sockbuf in initiate_stratum");
		pool->sockbuf_size = RBUFSIZE;
	}

	pool->sock = sockd;
	keep_sockalive(sockd);
	return true;
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
		char *notify;

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
	if (pool->sock)
		CLOSESOCKET(pool->sock);
	pool->sock = 0;
	mutex_unlock(&pool->stratum_lock);
}

bool initiate_stratum(struct pool *pool)
{
	bool ret = false, recvd = false, noresume = false, sockd = false;
	char s[RBUFSIZE], *sret = NULL, *nonce1, *sessionid;
	json_t *val = NULL, *res_val, *err_val;
	json_error_t err;
	int n2size;

resend:
	if (!setup_stratum_socket(pool)) {
		sockd = false;
		goto out;
	}

	sockd = true;

	if (recvd) {
		/* Get rid of any crap lying around if we're resending */
		clear_sock(pool);
		sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": []}", swork_id++);
	} else {
		if (pool->sessionid)
			sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\""PACKAGE"/"VERSION"\", \"%s\"]}", swork_id++, pool->sessionid);
		else
			sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\""PACKAGE"/"VERSION"\"]}", swork_id++);
	}

	if (__stratum_send(pool, s, strlen(s)) != SEND_OK) {
		applog(LOG_DEBUG, "Failed to send s in initiate_stratum");
		goto out;
	}

	if (!socket_full(pool, true)) {
		applog(LOG_DEBUG, "Timed out waiting for response in initiate_stratum");
		goto out;
	}

	sret = recv_line(pool);
	if (!sret)
		goto out;

	recvd = true;

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
	if (!n2size) {
		applog(LOG_INFO, "Failed to get n2size in initiate_stratum");
		free(sessionid);
		free(nonce1);
		goto out;
	}

	cg_wlock(&pool->data_lock);
	pool->sessionid = sessionid;
	pool->nonce1 = nonce1;
	pool->n1_len = strlen(nonce1) / 2;
	pool->n2size = n2size;
	cg_wunlock(&pool->data_lock);

	if (sessionid)
		applog(LOG_DEBUG, "Pool %d stratum session id: %s", pool->pool_no, pool->sessionid);

	ret = true;
out:
	if (val)
		json_decref(val);

	if (ret) {
		if (!pool->stratum_url)
			pool->stratum_url = pool->sockaddr_url;
		pool->stratum_active = true;
		pool->swork.diff = 1;
		if (opt_protocol) {
			applog(LOG_DEBUG, "Pool %d confirmed mining.subscribe with extranonce1 %s extran2size %d",
			       pool->pool_no, pool->nonce1, pool->n2size);
		}
	} else {
		if (recvd && !noresume) {
			/* Reset the sessionid used for stratum resuming in case the pool
			* does not support it, or does not know how to respond to the
			* presence of the sessionid parameter. */
			cg_wlock(&pool->data_lock);
			free(pool->sessionid);
			free(pool->nonce1);
			pool->sessionid = pool->nonce1 = NULL;
			cg_wunlock(&pool->data_lock);

			applog(LOG_DEBUG, "Failed to resume stratum, trying afresh");
			noresume = true;
			goto resend;
		}
		applog(LOG_DEBUG, "Initiate stratum failed");
		if (sockd)
			suspend_stratum(pool);
	}

	return ret;
}

bool restart_stratum(struct pool *pool)
{
	if (pool->stratum_active)
		suspend_stratum(pool);
	if (!initiate_stratum(pool))
		return false;
	if (!auth_stratum(pool))
		return false;
	return true;
}

void dev_error(struct cgpu_info *dev, enum dev_reason reason)
{
	dev->device_last_not_well = time(NULL);
	dev->device_not_well_reason = reason;

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
		quit(1, "Failed to malloc in realloc_strcat");

	sprintf(ret, "%s%s", ptr, s);
	free(ptr);
	return ret;
}

/* Make a text readable version of a string using 0xNN for < ' ' or > '~'
 * Including 0x00 at the end
 * You must free the result yourself */
void *str_text(char *ptr)
{
	unsigned char *uptr;
	char *ret, *txt;

	if (ptr == NULL) {
		ret = strdup("(null)");

		if (unlikely(!ret))
			quit(1, "Failed to malloc in text_str null");
	}

	uptr = (unsigned char *)ptr;

	ret = txt = malloc(strlen(ptr)*4+5); // Guaranteed >= needed
	if (unlikely(!txt))
		quit(1, "Failed to malloc in text_str txt");

	do {
		if (*uptr < ' ' || *uptr > '~') {
			sprintf(txt, "0x%02x", *uptr);
			txt += 4;
		} else
			*(txt++) = *uptr;
	} while (*(uptr++));

	*txt = '\0';

	return ret;
}

void RenameThread(const char* name)
{
#if defined(PR_SET_NAME)
	// Only the first 15 characters are used (16 - NUL terminator)
	prctl(PR_SET_NAME, name, 0, 0, 0);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__))
	pthread_set_name_np(pthread_self(), name);
#elif defined(MAC_OSX)
	pthread_setname_np(name);
#else
	// Prevent warnings for unused parameters...
	(void)name;
#endif
}

/* cgminer specific wrappers for true unnamed semaphore usage on platforms
 * that support them and for apple which does not. We use a single byte across
 * a pipe to emulate semaphore behaviour there. */
#ifdef __APPLE__
void cgsem_init(cgsem_t *cgsem)
{
	int flags, fd, i;

	if (pipe(cgsem->pipefd) == -1)
		quit(1, "Failed pipe in cgsem_init");

	/* Make the pipes FD_CLOEXEC to allow them to close should we call
	 * execv on restart. */
	for (i = 0; i < 2; i++) {
		fd = cgsem->pipefd[i];
		flags = fcntl(fd, F_GETFD, 0);
		flags |= FD_CLOEXEC;
		if (fcntl(fd, F_SETFD, flags) == -1)
			quit(1, "Failed to fcntl in cgsem_init");
	}
}

void cgsem_post(cgsem_t *cgsem)
{
	const char buf = 1;
	int ret;

	ret = write(cgsem->pipefd[1], &buf, 1);
	if (unlikely(ret == 0))
		applog(LOG_WARNING, "Failed to write in cgsem_post");
}

void cgsem_wait(cgsem_t *cgsem)
{
	char buf;
	int ret;

	ret = read(cgsem->pipefd[0], &buf, 1);
	if (unlikely(ret == 0))
		applog(LOG_WARNING, "Failed to read in cgsem_wait");
}

void cgsem_destroy(cgsem_t *cgsem)
{
	close(cgsem->pipefd[1]);
	close(cgsem->pipefd[0]);
}
#else
void cgsem_init(cgsem_t *cgsem)
{
	if (sem_init(cgsem, 0, 0))
		quit(1, "Failed to sem_init in cgsem_init");
}

void cgsem_post(cgsem_t *cgsem)
{
	if (unlikely(sem_post(cgsem)))
		quit(1, "Failed to sem_post in cgsem_post");
}

void cgsem_wait(cgsem_t *cgsem)
{
	if (unlikely(sem_wait(cgsem)))
		quit(1, "Failed to sem_wait in cgsem_wait");
}

void cgsem_destroy(cgsem_t *cgsem)
{
	sem_destroy(cgsem);
}
#endif
