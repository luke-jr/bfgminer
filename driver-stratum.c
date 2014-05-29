/*
 * Copyright 2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>

#include <jansson.h>

#include "deviceapi.h"
#include "driver-proxy.h"
#include "miner.h"
#include "util.h"
#include "work2d.h"

#define _ssm_client_octets     work2d_xnonce1sz
#define _ssm_client_xnonce2sz  work2d_xnonce2sz
static char *_ssm_notify;
static int _ssm_notify_sz;
static struct event *ev_notify;
static notifier_t _ssm_update_notifier;

struct stratumsrv_job {
	char *my_job_id;
	
	struct timeval tv_prepared;
	struct stratum_work swork;
	
	UT_hash_handle hh;
};

static struct stratumsrv_job *_ssm_jobs;
static struct work _ssm_cur_job_work;
static uint64_t _ssm_jobid;

static struct event_base *_smm_evbase;
static bool _smm_running;
static struct evconnlistener *_smm_listener;

struct stratumsrv_conn {
	struct bufferevent *bev;
	uint32_t xnonce1_le;
	struct timeval tv_hashes_done;
	bool hashes_done_ext;
	
	struct stratumsrv_conn *next;
};

static struct stratumsrv_conn *_ssm_connections;

#define _ssm_gen_dummy_work work2d_gen_dummy_work

static
bool stratumsrv_update_notify_str(struct pool * const pool, bool clean)
{
	cg_rlock(&pool->data_lock);
	
	struct stratumsrv_conn *conn;
	const struct stratum_work * const swork = &pool->swork;
	const int n2size = pool->swork.n2size;
	char my_job_id[33];
	int i;
	struct stratumsrv_job *ssj;
	ssize_t n2pad = work2d_pad_xnonce_size(swork);
	if (n2pad < 0)
		return false;
	size_t coinb1in_lenx = swork->nonce2_offset * 2;
	size_t n2padx = n2pad * 2;
	size_t coinb1_lenx = coinb1in_lenx + n2padx;
	size_t coinb2_len = bytes_len(&swork->coinbase) - swork->nonce2_offset - n2size;
	size_t coinb2_lenx = coinb2_len * 2;
	sprintf(my_job_id, "%"PRIx64"-%"PRIx64, (uint64_t)time(NULL), _ssm_jobid++);
	size_t bufsz = 166 + strlen(my_job_id) + coinb1_lenx + coinb2_lenx + (swork->merkles * 67);
	char * const buf = malloc(bufsz);
	char *p = buf;
	char prevhash[65], coinb1[coinb1_lenx + 1], coinb2[coinb2_lenx], version[9], nbits[9], ntime[9];
	uint32_t ntime_n;
	bin2hex(prevhash, &swork->header1[4], 32);
	bin2hex(coinb1, bytes_buf(&swork->coinbase), swork->nonce2_offset);
	work2d_pad_xnonce(&coinb1[coinb1in_lenx], swork, true);
	coinb1[coinb1_lenx] = '\0';
	bin2hex(coinb2, &bytes_buf(&swork->coinbase)[swork->nonce2_offset + n2size], coinb2_len);
	p += sprintf(p, "{\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",[", my_job_id, prevhash, coinb1, coinb2);
	for (i = 0; i < swork->merkles; ++i)
	{
		if (i)
			*p++ = ',';
		*p++ = '"';
		bin2hex(p, &bytes_buf(&swork->merkle_bin)[i * 32], 32);
		p += 64;
		*p++ = '"';
	}
	bin2hex(version, swork->header1, 4);
	bin2hex(nbits, swork->diffbits, 4);
	ntime_n = htobe32(swork->ntime + timer_elapsed(&swork->tv_received, NULL));
	bin2hex(ntime, &ntime_n, 4);
	p += sprintf(p, "],\"%s\",\"%s\",\"%s\",%s],\"method\":\"mining.notify\",\"id\":null}\n", version, nbits, ntime, clean ? "true" : "false");
	
	ssj = malloc(sizeof(*ssj));
	*ssj = (struct stratumsrv_job){
		.my_job_id = strdup(my_job_id),
	};
	timer_set_now(&ssj->tv_prepared);
	stratum_work_cpy(&ssj->swork, swork);
	
	cg_runlock(&pool->data_lock);
	
	ssj->swork.data_lock_p = NULL;
	HASH_ADD_KEYPTR(hh, _ssm_jobs, ssj->my_job_id, strlen(ssj->my_job_id), ssj);
	
	if (likely(_ssm_cur_job_work.pool))
		clean_work(&_ssm_cur_job_work);
	_ssm_gen_dummy_work(&_ssm_cur_job_work, &ssj->swork, &ssj->tv_prepared, NULL, 0);
	
	_ssm_notify_sz = p - buf;
	assert(_ssm_notify_sz <= bufsz);
	free(_ssm_notify);
	_ssm_notify = buf;
	
	LL_FOREACH(_ssm_connections, conn)
	{
		if (unlikely(!conn->xnonce1_le))
			continue;
		bufferevent_write(conn->bev, _ssm_notify, _ssm_notify_sz);
	}
	
	return true;
}

static
void _ssj_free(struct stratumsrv_job * const ssj)
{
	free(ssj->my_job_id);
	stratum_work_clean(&ssj->swork);
	free(ssj);
}

static
void stratumsrv_job_pruner()
{
	struct stratumsrv_job *ssj, *tmp_ssj;
	struct timeval tv_now;
	
	timer_set_now(&tv_now);
	
	HASH_ITER(hh, _ssm_jobs, ssj, tmp_ssj)
	{
		if (timer_elapsed(&ssj->tv_prepared, &tv_now) <= opt_expiry)
			break;
		HASH_DEL(_ssm_jobs, ssj);
		applog(LOG_DEBUG, "SSM: Pruning job_id %s", ssj->my_job_id);
		_ssj_free(ssj);
	}
}

static void stratumsrv_client_close(struct stratumsrv_conn *);

static
void stratumsrv_conn_close_completion_cb(struct bufferevent *bev, void *p)
{
	struct evbuffer * const output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output))
		// Still have more data to write...
		return;
	stratumsrv_client_close(p);
}

static void stratumsrv_event(struct bufferevent *, short, void *);

static
void stratumsrv_boot(struct stratumsrv_conn * const conn, const char * const msg)
{
	struct bufferevent * const bev = conn->bev;
	char buf[58 + strlen(msg)];
	int bufsz = sprintf(buf, "{\"params\":[\"%s\"],\"method\":\"client.show_message\",\"id\":null}\n", msg);
	bufferevent_write(bev, buf, bufsz);
	bufferevent_setcb(bev, NULL, stratumsrv_conn_close_completion_cb, stratumsrv_event, conn);
}

static
void stratumsrv_boot_all_subscribed(const char * const msg)
{
	struct stratumsrv_conn *conn, *tmp_conn;
	
	free(_ssm_notify);
	_ssm_notify = NULL;
	
	// Boot all connections
	LL_FOREACH_SAFE(_ssm_connections, conn, tmp_conn)
	{
		if (!conn->xnonce1_le)
			continue;
		stratumsrv_boot(conn, msg);
	}
}

static
void _stratumsrv_update_notify(evutil_socket_t fd, short what, __maybe_unused void *p)
{
	struct pool *pool = current_pool();
	bool clean;
	
	if (fd == _ssm_update_notifier[0])
	{
		evtimer_del(ev_notify);
		notifier_read(_ssm_update_notifier);
		applog(LOG_DEBUG, "SSM: Update triggered by notifier");
	}
	
	clean = _ssm_cur_job_work.pool ? stale_work(&_ssm_cur_job_work, true) : true;
	if (clean)
	{
		struct stratumsrv_job *ssj, *tmp;
		
		applog(LOG_DEBUG, "SSM: Current replacing job stale, pruning all jobs");
		HASH_ITER(hh, _ssm_jobs, ssj, tmp)
		{
			HASH_DEL(_ssm_jobs, ssj);
			_ssj_free(ssj);
		}
	}
	else
		stratumsrv_job_pruner();
	
	if (!pool_has_usable_swork(pool))
	{
		applog(LOG_WARNING, "SSM: No usable 2D work upstream!");
		if (clean)
			stratumsrv_boot_all_subscribed("Current upstream pool does not have usable 2D work");
		goto out;
	}
	
	if (!stratumsrv_update_notify_str(pool, clean))
	{
		applog(LOG_WARNING, "SSM: Failed to subdivide upstream stratum notify!");
		if (clean)
			stratumsrv_boot_all_subscribed("Current upstream pool does not have active stratum");
	}
	
out: ;
	struct timeval tv_scantime = {
		.tv_sec = opt_scantime,
	};
	evtimer_add(ev_notify, &tv_scantime);
}

static struct proxy_client *_stratumsrv_find_or_create_client(const char *);

static
struct proxy_client *(*stratumsrv_find_or_create_client)(const char *) = _stratumsrv_find_or_create_client;

static
struct proxy_client *_stratumsrv_find_or_create_client(const char *user)
{
	struct proxy_client * const client = proxy_find_or_create_client(user);
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	
	if (!client)
		return NULL;
	
	cgpu = client->cgpu;
	thr = cgpu->thr[0];
	
	memcpy(thr->work_restart_notifier, _ssm_update_notifier, sizeof(thr->work_restart_notifier));
	stratumsrv_find_or_create_client = proxy_find_or_create_client;
	
	return client;
}

static
void _stratumsrv_failure(struct bufferevent * const bev, const char * const idstr, const int e, const char * const emsg)
{
	if (!idstr)
		return;
	
	char buf[0x100];
	size_t bufsz = snprintf(buf, sizeof(buf), "{\"error\":[%d,\"%s\",null],\"id\":%s,\"result\":null}\n", e, emsg, idstr);
	bufferevent_write(bev, buf, bufsz);
}
#define return_stratumsrv_failure(e, emsg)  do{  \
	_stratumsrv_failure(bev, idstr, e, emsg);    \
	return;                                      \
}while(0)

static
void _stratumsrv_success(struct bufferevent * const bev, const char * const idstr)
{
	if (!idstr)
		return;
	
	size_t bufsz = 36 + strlen(idstr);
	char buf[bufsz];
	
	bufsz = sprintf(buf, "{\"result\":true,\"id\":%s,\"error\":null}\n", idstr);
	bufferevent_write(bev, buf, bufsz);
}

static
void stratumsrv_mining_subscribe(struct bufferevent *bev, json_t *params, const char *idstr, uint32_t *xnonce1_p)
{
	char buf[90 + strlen(idstr) + (_ssm_client_octets * 2 * 2) + 0x10];
	char xnonce1x[(_ssm_client_octets * 2) + 1];
	int bufsz;
	
	if (!_ssm_notify)
	{
		evtimer_del(ev_notify);
		_stratumsrv_update_notify(-1, 0, NULL);
		if (!_ssm_notify)
			return_stratumsrv_failure(20, "No notify set (upstream not stratum?)");
	}
	
	if (!*xnonce1_p)
	{
		if (!reserve_work2d_(xnonce1_p))
			return_stratumsrv_failure(20, "Maximum clients already connected");
	}
	
	bin2hex(xnonce1x, xnonce1_p, _ssm_client_octets);
	bufsz = sprintf(buf, "{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"x\"],[\"mining.notify\",\"%s\"]],\"%s\",%d],\"error\":null}\n", idstr, xnonce1x, xnonce1x, _ssm_client_xnonce2sz);
	bufferevent_write(bev, buf, bufsz);
	bufferevent_write(bev, "{\"params\":[", 11);
	if (opt_scrypt)
		bufferevent_write(bev, "0.000015258556232", 17);
	else
		bufferevent_write(bev, "15.999755859375", 15);
	bufferevent_write(bev, "],\"id\":null,\"method\":\"mining.set_difficulty\"}\n", 46);
	bufferevent_write(bev, _ssm_notify, _ssm_notify_sz);
}

static
void stratumsrv_mining_authorize(struct bufferevent *bev, json_t *params, const char *idstr, uint32_t *xnonce1_p)
{
	struct proxy_client * const client = stratumsrv_find_or_create_client(__json_array_string(params, 0));
	
	if (unlikely(!client))
		return_stratumsrv_failure(20, "Failed creating new cgpu");
	
	_stratumsrv_success(bev, idstr);
}

static
void stratumsrv_mining_submit(struct bufferevent *bev, json_t *params, const char *idstr, struct stratumsrv_conn * const conn)
{
	uint32_t * const xnonce1_p = &conn->xnonce1_le;
	struct stratumsrv_job *ssj;
	struct proxy_client *client = stratumsrv_find_or_create_client(__json_array_string(params, 0));
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	const char * const job_id = __json_array_string(params, 1);
	const char * const extranonce2 = __json_array_string(params, 2);
	const char * const ntime = __json_array_string(params, 3);
	const char * const nonce = __json_array_string(params, 4);
	uint8_t xnonce2[work2d_xnonce2sz];
	uint32_t ntime_n, nonce_n;
	const float nonce_diff = opt_scrypt ? (1./0x10000) : 16.;
	bool is_stale;
	
	if (unlikely(!client))
		return_stratumsrv_failure(20, "Failed creating new cgpu");
	if (unlikely(!(job_id && extranonce2 && ntime && nonce)))
		return_stratumsrv_failure(20, "Couldn't understand parameters");
	if (unlikely(strlen(nonce) < 8))
		return_stratumsrv_failure(20, "nonce too short");
	if (unlikely(strlen(ntime) < 8))
		return_stratumsrv_failure(20, "ntime too short");
	if (unlikely(strlen(extranonce2) < _ssm_client_xnonce2sz * 2))
		return_stratumsrv_failure(20, "extranonce2 too short");
	
	cgpu = client->cgpu;
	thr = cgpu->thr[0];
	
	// Lookup job_id
	HASH_FIND_STR(_ssm_jobs, job_id, ssj);
	if (!ssj)
		return_stratumsrv_failure(21, "Job not found");
	
	hex2bin(xnonce2, extranonce2, work2d_xnonce2sz);
	
	// Submit nonce
	hex2bin((void*)&ntime_n, ntime, 4);
	ntime_n = be32toh(ntime_n);
	hex2bin((void*)&nonce_n, nonce, 4);
	nonce_n = le32toh(nonce_n);
	if (!work2d_submit_nonce(thr, &ssj->swork, &ssj->tv_prepared, xnonce2, *xnonce1_p, nonce_n, ntime_n, &is_stale, nonce_diff))
		_stratumsrv_failure(bev, idstr, 23, "H-not-zero");
	else
	if (is_stale)
		_stratumsrv_failure(bev, idstr, 21, "stale");
	else
		_stratumsrv_success(bev, idstr);
	
	if (!conn->hashes_done_ext)
	{
		struct timeval tv_now, tv_delta;
		timer_set_now(&tv_now);
		timersub(&tv_now, &conn->tv_hashes_done, &tv_delta);
		conn->tv_hashes_done = tv_now;
		const uint64_t hashes = opt_scrypt ? 0x10000 : 0x1000000000;
		hashes_done(thr, hashes, &tv_delta, NULL);
	}
}

static
void stratumsrv_mining_hashes_done(struct bufferevent * const bev, json_t * const params, const char * const idstr, struct stratumsrv_conn * const conn)
{
	double f;
	struct timeval tv_delta;
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	struct proxy_client * const client = stratumsrv_find_or_create_client(__json_array_string(params, 0));
	json_t *jduration = json_array_get(params, 1);
	json_t *jhashcount = json_array_get(params, 2);
	
	if (!(json_is_number(jduration) && json_is_number(jhashcount)))
		return_stratumsrv_failure(20, "mining.hashes_done(String username, Number duration-in-seconds, Number hashcount)");
	
	cgpu = client->cgpu;
	thr = cgpu->thr[0];
	
	f = json_number_value(jduration);
	tv_delta.tv_sec = f;
	tv_delta.tv_usec = (f - tv_delta.tv_sec) * 1e6;
	
	f = json_number_value(jhashcount);
	hashes_done(thr, f, &tv_delta, NULL);
	
	conn->hashes_done_ext = true;
}

static
bool stratumsrv_process_line(struct bufferevent * const bev, const char * const ln, void * const p)
{
	struct stratumsrv_conn *conn = p;
	json_error_t jerr;
	json_t *json, *params, *j2;
	const char *method;
	char *idstr;
	
	json = JSON_LOADS(ln, &jerr);
	if (!json)
	{
		if (strncmp(ln, "GET ", 4) && strncmp(ln, "POST ", 5) && ln[0] != '\x16' /* TLS handshake */)
			applog(LOG_ERR, "SSM: JSON parse error: %s", ln);
		return false;
	}
	
	method = bfg_json_obj_string(json, "method", NULL);
	if (!method)
	{
		applog(LOG_ERR, "SSM: JSON missing method: %s", ln);
errout:
		json_decref(json);
		return false;
	}
	
	params = json_object_get(json, "params");
	if (!params)
	{
		applog(LOG_ERR, "SSM: JSON missing params: %s", ln);
		goto errout;
	}
	
	applog(LOG_DEBUG, "SSM: RECV: %s", ln);
	
	j2 = json_object_get(json, "id");
	idstr = (j2 && !json_is_null(j2)) ? json_dumps_ANY(j2, 0) : NULL;
	
	if (!strcasecmp(method, "mining.submit"))
		stratumsrv_mining_submit(bev, params, idstr, conn);
	else
	if (!strcasecmp(method, "mining.hashes_done"))
		stratumsrv_mining_hashes_done(bev, params, idstr, conn);
	else
	if (!strcasecmp(method, "mining.authorize"))
		stratumsrv_mining_authorize(bev, params, idstr, &conn->xnonce1_le);
	else
	if (!strcasecmp(method, "mining.subscribe"))
		stratumsrv_mining_subscribe(bev, params, idstr, &conn->xnonce1_le);
	else
		_stratumsrv_failure(bev, idstr, -3, "Method not supported");
	
	free(idstr);
	json_decref(json);
	return true;
}

static
void stratumsrv_client_close(struct stratumsrv_conn * const conn)
{
	struct bufferevent * const bev = conn->bev;
	
	bufferevent_free(bev);
	LL_DELETE(_ssm_connections, conn);
	release_work2d_(conn->xnonce1_le);
	free(conn);
}

static
void stratumsrv_read(struct bufferevent *bev, void *p)
{
	struct evbuffer *input = bufferevent_get_input(bev);
	char *ln;
	bool rv;
	
	while ( (ln = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY)) )
	{
		rv = stratumsrv_process_line(bev, ln, p);
		free(ln);
		if (unlikely(!rv))
		{
			stratumsrv_client_close(p);
			break;
		}
	}
}

static
void stratumsrv_event(struct bufferevent *bev, short events, void *p)
{
	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
	{
		if (events & BEV_EVENT_ERROR)
			applog(LOG_ERR, "Error from bufferevent");
		if (events & BEV_EVENT_EOF)
			applog(LOG_DEBUG, "EOF from bufferevent");
		stratumsrv_client_close(p);
	}
}

static
void stratumlistener(struct evconnlistener *listener, evutil_socket_t sock, struct sockaddr *addr, int len, void *p)
{
	struct stratumsrv_conn *conn;
	struct event_base *evbase = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(evbase, sock, BEV_OPT_CLOSE_ON_FREE);
	conn = malloc(sizeof(*conn));
	*conn = (struct stratumsrv_conn){
		.bev = bev,
	};
	LL_PREPEND(_ssm_connections, conn);
	bufferevent_setcb(bev, stratumsrv_read, NULL, stratumsrv_event, conn);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
}

void stratumsrv_start();

void stratumsrv_change_port()
{
	struct event_base * const evbase = _smm_evbase;
	
	if (_smm_listener)
		evconnlistener_free(_smm_listener);
	
	if (!_smm_running)
	{
		stratumsrv_start();
		return;
	}
	
	struct sockaddr_in sin = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = INADDR_ANY,
		.sin_port = htons(stratumsrv_port),
	};
	_smm_listener = evconnlistener_new_bind(evbase, stratumlistener, NULL, (
		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_REUSEABLE
	), 0x10, (void*)&sin, sizeof(sin));
}

static
void *stratumsrv_thread(__maybe_unused void *p)
{
	pthread_detach(pthread_self());
	RenameThread("stratumsrv");
	
	work2d_init();
	
	struct event_base *evbase = event_base_new();
	_smm_evbase = evbase;
	{
		ev_notify = evtimer_new(evbase, _stratumsrv_update_notify, NULL);
		_stratumsrv_update_notify(-1, 0, NULL);
	}
	{
		notifier_init(_ssm_update_notifier);
		struct event *ev_update_notifier = event_new(evbase, _ssm_update_notifier[0], EV_READ | EV_PERSIST, _stratumsrv_update_notify, NULL);
		event_add(ev_update_notifier, NULL);
	}
	stratumsrv_change_port();
	event_base_dispatch(evbase);
	
	return NULL;
}

void stratumsrv_start()
{
	_smm_running = true;
	pthread_t pth;
	if (unlikely(pthread_create(&pth, NULL, stratumsrv_thread, NULL)))
		quit(1, "stratumsrv thread create failed");
}
