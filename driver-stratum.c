/*
 * Copyright 2013-2016 Luke Dashjr
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

#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/thread.h>

#include <jansson.h>

#include "deviceapi.h"
#include "driver-proxy.h"
#include "miner.h"
#include "util.h"
#include "work2d.h"

#define _ssm_client_octets     work2d_xnonce1sz
#define _ssm_client_xnonce2sz  work2d_xnonce2sz
static char *_ssm_notify, *_ssm_setgoal;
static int _ssm_notify_sz, _ssm_setgoal_sz;
static struct stratumsrv_job *_ssm_last_ssj;
static struct event *ev_notify;
static notifier_t _ssm_update_notifier;

struct stratumsrv_job {
	char *my_job_id;
	
	struct timeval tv_prepared;
	struct stratum_work swork;
	float job_pdiff[WORK2D_MAX_DIVISIONS+1];
	
	UT_hash_handle hh;
};

static struct stratumsrv_job *_ssm_jobs;
static struct work _ssm_cur_job_work;
static uint64_t _ssm_jobid;

static struct event_base *_smm_evbase;
static bool _smm_running;
static struct evconnlistener *_smm_listener;

struct stratumsrv_conn_userlist {
	struct proxy_client *client;
	struct stratumsrv_conn *conn;
	struct stratumsrv_conn_userlist *client_next;
	struct stratumsrv_conn_userlist *next;
};

enum stratumsrv_conn_capability {
	SCC_NOTIFY    = 1 << 0,
	SCC_SET_DIFF  = 1 << 1,
	SCC_SET_GOAL  = 1 << 2,
};
typedef uint8_t stratumsrv_conn_capabilities_t;

struct stratumsrv_conn {
	struct bufferevent *bev;
	stratumsrv_conn_capabilities_t capabilities;
	uint32_t xnonce1_le;
	struct timeval tv_hashes_done;
	bool hashes_done_ext;
	float current_share_pdiff;
	bool desired_default_share_pdiff;  // Set if any authenticated user is configured for the default
	float desired_share_pdiff;
	struct stratumsrv_conn_userlist *authorised_users;
	
	struct stratumsrv_conn *next;
};

static struct stratumsrv_conn *_ssm_connections;

static
void stratumsrv_send_set_difficulty(struct stratumsrv_conn * const conn, const float share_pdiff)
{
	struct bufferevent * const bev = conn->bev;
	char buf[0x100];
	const double bdiff = pdiff_to_bdiff(share_pdiff);
	conn->current_share_pdiff = share_pdiff;
	const int prec = double_find_precision(bdiff, 10.);
	const size_t bufsz = snprintf(buf, sizeof(buf), "{\"params\":[%.*f],\"id\":null,\"method\":\"mining.set_difficulty\"}\n", prec, bdiff);
	bufferevent_write(bev, buf, bufsz);
}

static
float stratumsrv_choose_share_pdiff(const struct stratumsrv_conn * const conn, const struct mining_algorithm * const malgo)
{
	float conn_pdiff = conn->desired_share_pdiff;
	if (conn->desired_default_share_pdiff && malgo->reasonable_low_nonce_diff < conn_pdiff)
		conn_pdiff = malgo->reasonable_low_nonce_diff;
	return conn_pdiff;
}

static void stratumsrv_boot_all_subscribed(const char *);
static void _ssj_free(struct stratumsrv_job *);
static void stratumsrv_job_pruner();

static
bool stratumsrv_update_notify_str(struct pool * const pool)
{
	const bool clean = _ssm_cur_job_work.pool ? stale_work(&_ssm_cur_job_work, true) : true;
	struct timeval tv_now;
	
	cg_rlock(&pool->data_lock);
	
	if (!pool_has_usable_swork(pool))
	{
fail:
		cg_runlock(&pool->data_lock);
		applog(LOG_WARNING, "SSM: No usable 2D work upstream!");
		if (clean)
			stratumsrv_boot_all_subscribed("Current upstream pool does not have usable 2D work");
		return false;
	}
	
	timer_set_now(&tv_now);
	
	{
		struct work work;
		work2d_gen_dummy_work_for_stale_check(&work, &pool->swork, &tv_now, NULL);
		
		const bool is_stale = stale_work2(&work, false, true);
		
		clean_work(&work);
		
		if (is_stale) {
			cg_runlock(&pool->data_lock);
			applog(LOG_DEBUG, "SSM: Ignoring work update notification while pool %d has stale swork", pool->pool_no);
			return false;
		}
	}
	
	struct stratumsrv_conn *conn;
	const struct stratum_work * const swork = &pool->swork;
	const int n2size = pool->swork.n2size;
	const size_t coinb2_offset = swork->nonce2_offset + n2size;
	const size_t coinb2_len = bytes_len(&swork->coinbase) - swork->nonce2_offset - n2size;
	
	if (_ssm_last_ssj &&
	    !(memcmp(&swork->header1[0], &_ssm_last_ssj->swork.header1[0], 0x24)
	   || swork->nonce2_offset != _ssm_last_ssj->swork.nonce2_offset
	   || bytes_len(&swork->coinbase) != bytes_len(&_ssm_last_ssj->swork.coinbase)
	   || memcmp(bytes_buf(&swork->coinbase), bytes_buf(&_ssm_last_ssj->swork.coinbase), swork->nonce2_offset)
	   || memcmp(&bytes_buf(&swork->coinbase)[coinb2_offset], &bytes_buf(&_ssm_last_ssj->swork.coinbase)[coinb2_offset], coinb2_len)
	   || memcmp(swork->diffbits, _ssm_last_ssj->swork.diffbits, 4)
	)) {
		cg_runlock(&pool->data_lock);
		applog(LOG_DEBUG, "SSM: Updating with (near?-)identical work2d; skipping...");
		return false;
	}
	
	char my_job_id[33];
	int i;
	struct stratumsrv_job *ssj;
	ssize_t n2pad = work2d_pad_xnonce_size(swork);
	if (n2pad < 0)
	{
		goto fail;
	}
	size_t coinb1in_lenx = swork->nonce2_offset * 2;
	size_t n2padx = n2pad * 2;
	size_t coinb1_lenx = coinb1in_lenx + n2padx;
	size_t coinb2_lenx = coinb2_len * 2;
	sprintf(my_job_id, "%"PRIx64"-%"PRIx64, (uint64_t)time(NULL), _ssm_jobid++);
	// NOTE: The buffer has up to 2 extra/unused bytes:
	// NOTE: - If clean is "true", we spare the extra needed for "false"
	// NOTE: - The first merkle link does not need a comma, but we cannot subtract it without breaking the case of zero merkle links
	size_t bufsz = 24 /* sprintf 1 constant */ + strlen(my_job_id) + 64 /* prevhash */ + coinb1_lenx + coinb2_lenx + (swork->merkles * 67) + 49 /* sprintf 2 constant */ + 8 /* version */ + 8 /* nbits */ + 8 /* ntime */ + 5 /* clean */ + 1;
	char * const buf = malloc(bufsz);
	char *p = buf;
	char prevhash[65], coinb1[coinb1_lenx + 1], coinb2[coinb2_lenx + 1], version[9], nbits[9], ntime[9];
	uint32_t ntime_n;
	bin2hex(prevhash, &swork->header1[4], 32);
	bin2hex(coinb1, bytes_buf(&swork->coinbase), swork->nonce2_offset);
	work2d_pad_xnonce(&coinb1[coinb1in_lenx], swork, true);
	coinb1[coinb1_lenx] = '\0';
	bin2hex(coinb2, &bytes_buf(&swork->coinbase)[coinb2_offset], coinb2_len);
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
	
	const size_t setgoalbufsz = 49 + strlen(pool->goal->name) + (pool->goalname ? (1 + strlen(pool->goalname)) : 0) + 12 + strlen(pool->goal->malgo->name) + 5 + 1;
	char * const setgoalbuf = malloc(setgoalbufsz);
	snprintf(setgoalbuf, setgoalbufsz, "{\"method\":\"mining.set_goal\",\"id\":null,\"params\":[\"%s%s%s\",{\"malgo\":\"%s\"}]}\n", pool->goal->name, pool->goalname ? "/" : "", pool->goalname ?: "", pool->goal->malgo->name);
	
	ssj = malloc(sizeof(*ssj));
	*ssj = (struct stratumsrv_job){
		.my_job_id = strdup(my_job_id),
	};
	ssj->tv_prepared = tv_now;
	stratum_work_cpy(&ssj->swork, swork);
	
	cg_runlock(&pool->data_lock);
	
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
	
	HASH_ADD_KEYPTR(hh, _ssm_jobs, ssj->my_job_id, strlen(ssj->my_job_id), ssj);
	
	if (likely(_ssm_cur_job_work.pool))
		clean_work(&_ssm_cur_job_work);
	work2d_gen_dummy_work_for_stale_check(&_ssm_cur_job_work, &ssj->swork, &ssj->tv_prepared, NULL);
	
	_ssm_notify_sz = p - buf;
	assert(_ssm_notify_sz <= bufsz);
	free(_ssm_notify);
	_ssm_notify = buf;
	const bool setgoal_changed = _ssm_setgoal ? strcmp(setgoalbuf, _ssm_setgoal) : true;
	if (setgoal_changed)
	{
		free(_ssm_setgoal);
		_ssm_setgoal = setgoalbuf;
		_ssm_setgoal_sz = setgoalbufsz - 1;
	}
	else
		free(setgoalbuf);
	_ssm_last_ssj = ssj;
	
	float pdiff = target_diff(ssj->swork.target);
	const struct mining_goal_info * const goal = pool->goal;
	const struct mining_algorithm * const malgo = goal->malgo;
	LL_FOREACH(_ssm_connections, conn)
	{
		if (unlikely(!conn->xnonce1_le))
			continue;
		if (setgoal_changed && (conn->capabilities & SCC_SET_GOAL))
			bufferevent_write(conn->bev, setgoalbuf, setgoalbufsz);
		if (likely(conn->capabilities & SCC_SET_DIFF))
		{
			float conn_pdiff = stratumsrv_choose_share_pdiff(conn, malgo);
			if (pdiff < conn_pdiff)
				conn_pdiff = pdiff;
			ssj->job_pdiff[conn->xnonce1_le] = conn_pdiff;
			if (conn_pdiff != conn->current_share_pdiff)
				stratumsrv_send_set_difficulty(conn, conn_pdiff);
		}
		if (likely(conn->capabilities & SCC_NOTIFY))
			bufferevent_write(conn->bev, _ssm_notify, _ssm_notify_sz);
	}
	
	return true;
}

void stratumsrv_client_changed_diff(struct proxy_client * const client)
{
	int connections_affected = 0, connections_changed = 0;
	struct stratumsrv_conn_userlist *ule, *ule2;
	LL_FOREACH2(client->stratumsrv_connlist, ule, client_next)
	{
		struct stratumsrv_conn * const conn = ule->conn;
		
		++connections_affected;
		
		float desired_share_pdiff = client->desired_share_pdiff;
		bool any_default_share_pdiff = !desired_share_pdiff;
		LL_FOREACH(conn->authorised_users, ule2)
		{
			struct proxy_client * const other_client = ule2->client;
			if (!other_client->desired_share_pdiff)
				any_default_share_pdiff = true;
			else
			if (other_client->desired_share_pdiff < desired_share_pdiff)
				desired_share_pdiff = other_client->desired_share_pdiff;
		}
		BFGINIT(desired_share_pdiff, FLT_MAX);
		if (conn->desired_share_pdiff != desired_share_pdiff || conn->desired_default_share_pdiff != any_default_share_pdiff)
		{
			conn->desired_share_pdiff = desired_share_pdiff;
			conn->desired_default_share_pdiff = any_default_share_pdiff;
			++connections_changed;
		}
	}
	if (connections_affected)
		applog(LOG_DEBUG, "Proxy-share difficulty change for user '%s' affected %d connections (%d changed difficulty)", client->username, connections_affected, connections_changed);
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
	_ssm_last_ssj = NULL;
	
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
	
	if (fd == _ssm_update_notifier[0])
	{
		evtimer_del(ev_notify);
		notifier_read(_ssm_update_notifier);
		applog(LOG_DEBUG, "SSM: Update triggered by notifier");
	}
	
	stratumsrv_update_notify_str(pool);
	
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
void stratumsrv_success2(struct bufferevent * const bev, const char * const idstr, const char * const resultstr)
{
	if (!idstr)
		return;
	
	size_t bufsz = 32 + strlen(resultstr) + strlen(idstr);
	char buf[bufsz];
	
	bufsz = sprintf(buf, "{\"result\":%s,\"id\":%s,\"error\":null}\n", resultstr, idstr);
	bufferevent_write(bev, buf, bufsz);
}

static inline
void _stratumsrv_success(struct bufferevent * const bev, const char * const idstr)
{
	stratumsrv_success2(bev, idstr, "true");
}

static
void stratumsrv_mining_capabilities(struct bufferevent * const bev, json_t * const params, const char * const idstr, struct stratumsrv_conn * const conn)
{
	if (json_is_null(params) || (!json_is_array(params)))
		return_stratumsrv_failure(20, "Bad params");
	
	conn->capabilities = 0;
	
	json_t * const caps = (json_array_size(params) < 1) ? NULL : json_array_get(params, 0);
	if (caps && (!json_is_null(caps)) && json_is_object(caps))
	{
		for (void *iter = json_object_iter(caps); iter; iter = json_object_iter_next(caps, iter))
		{
			const char * const s = json_object_iter_key(iter);
			if (!strcasecmp(s, "notify"))
				conn->capabilities |= SCC_NOTIFY;
			else
			if (!strcasecmp(s, "set_difficulty"))
				conn->capabilities |= SCC_SET_DIFF;
			else
			if (!strcasecmp(s, "set_goal"))
				conn->capabilities |= SCC_SET_GOAL;
		}
	}
	
	stratumsrv_success2(bev, idstr, "null");
}

static
void stratumsrv_mining_subscribe(struct bufferevent * const bev, json_t * const params, const char * const idstr, struct stratumsrv_conn * const conn)
{
	uint32_t * const xnonce1_p = &conn->xnonce1_le;
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
	
	if (conn->capabilities & SCC_SET_GOAL)
		bufferevent_write(conn->bev, _ssm_setgoal, _ssm_setgoal_sz);
	if (likely(conn->capabilities & SCC_SET_DIFF))
	{
		const struct pool * const pool = _ssm_last_ssj->swork.pool;
		const struct mining_goal_info * const goal = pool->goal;
		const struct mining_algorithm * const malgo = goal->malgo;
		float pdiff = target_diff(_ssm_last_ssj->swork.target);
		const float conn_pdiff = stratumsrv_choose_share_pdiff(conn, malgo);
		if (pdiff > conn_pdiff)
			pdiff = conn_pdiff;
		_ssm_last_ssj->job_pdiff[*xnonce1_p] = pdiff;
		stratumsrv_send_set_difficulty(conn, pdiff);
	}
	if (likely(conn->capabilities & SCC_NOTIFY))
		bufferevent_write(bev, _ssm_notify, _ssm_notify_sz);
}

static
void stratumsrv_mining_authorize(struct bufferevent * const bev, json_t * const params, const char * const idstr, struct stratumsrv_conn * const conn)
{
	const char * const username = __json_array_string(params, 0);
	if (!username)
		return_stratumsrv_failure(20, "Missing or non-String username parameter");
	
	struct proxy_client * const client = stratumsrv_find_or_create_client(username);
	
	if (unlikely(!client))
		return_stratumsrv_failure(20, "Failed creating new cgpu");
	
	if (client->desired_share_pdiff)
	{
		if (!conn->authorised_users)
			conn->desired_default_share_pdiff = false;
		if ((!conn->authorised_users) || client->desired_share_pdiff < conn->desired_share_pdiff)
			conn->desired_share_pdiff = client->desired_share_pdiff;
	}
	else
	{
		conn->desired_default_share_pdiff = true;
		if (!conn->authorised_users)
			conn->desired_share_pdiff = FLT_MAX;
	}
	
	struct stratumsrv_conn_userlist *ule = malloc(sizeof(*ule));
	*ule = (struct stratumsrv_conn_userlist){
		.client = client,
		.conn = conn,
	};
	LL_PREPEND(conn->authorised_users, ule);
	LL_PREPEND2(client->stratumsrv_connlist, ule, client_next);
	
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
	
	float nonce_diff = ssj->job_pdiff[*xnonce1_p];
	if (unlikely(nonce_diff <= 0))
	{
		applog(LOG_WARNING, "Unknown share difficulty for SSM job %s", ssj->my_job_id);
		nonce_diff = conn->current_share_pdiff;
	}
	
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
		const uint64_t hashes = (float)0x100000000 * nonce_diff;
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
		stratumsrv_mining_authorize(bev, params, idstr, conn);
	else
	if (!strcasecmp(method, "mining.subscribe"))
		stratumsrv_mining_subscribe(bev, params, idstr, conn);
	else
	if (!strcasecmp(method, "mining.capabilities"))
		stratumsrv_mining_capabilities(bev, params, idstr, conn);
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
	struct stratumsrv_conn_userlist *ule, *uletmp;
	
	bufferevent_free(bev);
	LL_DELETE(_ssm_connections, conn);
	release_work2d_(conn->xnonce1_le);
	LL_FOREACH_SAFE(conn->authorised_users, ule, uletmp)
	{
		struct proxy_client * const client = ule->client;
		LL_DELETE(conn->authorised_users, ule);
		LL_DELETE2(client->stratumsrv_connlist, ule, client_next);
		free(ule);
	}
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

// See also, proxy_set_diff in driver-proxy.c
static
const char *stratumsrv_init_diff(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct stratumsrv_conn * const conn = proc->device_data;
	
	double nv = atof(newvalue);
	if (nv < 0)
		return "Invalid difficulty";
	
	if (nv <= minimum_pdiff)
		nv = minimum_pdiff;
	conn->desired_share_pdiff = nv;
	conn->desired_default_share_pdiff = false;
	
	return NULL;
}

static const struct bfg_set_device_definition stratumsrv_set_device_funcs_newconnect[] = {
	{"diff", stratumsrv_init_diff, NULL},
	{NULL},
};

static
void stratumlistener(struct evconnlistener *listener, evutil_socket_t sock, struct sockaddr *addr, int len, void *p)
{
	struct stratumsrv_conn *conn;
	struct event_base *evbase = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(evbase, sock, BEV_OPT_CLOSE_ON_FREE);
	conn = malloc(sizeof(*conn));
	*conn = (struct stratumsrv_conn){
		.bev = bev,
		.capabilities = SCC_NOTIFY | SCC_SET_DIFF,
		.desired_share_pdiff = FLT_MAX,
		.desired_default_share_pdiff = true,
	};
	drv_set_defaults(&proxy_drv, stratumsrv_set_device_funcs_newconnect, conn, NULL, NULL, 1);
	LL_PREPEND(_ssm_connections, conn);
	bufferevent_setcb(bev, stratumsrv_read, NULL, stratumsrv_event, conn);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
}

static bool stratumsrv_init_server(void);

bool stratumsrv_change_port(const unsigned port)
{
	if (!_smm_running) {
		if (!stratumsrv_init_server()) {
			return false;
		}
	}
	
	struct event_base * const evbase = _smm_evbase;
	struct evconnlistener * const old_smm_listener = _smm_listener;
	
	struct sockaddr_in sin = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = INADDR_ANY,
		.sin_port = htons(port),
	};
	_smm_listener = evconnlistener_new_bind(evbase, stratumlistener, NULL, (
		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_REUSEABLE
	), 0x10, (void*)&sin, sizeof(sin));
	
	if (!_smm_listener) {
		applog(LOG_ERR, "SSM: Failed to listen on port %u", (unsigned)port);
		return false;
	}
	
	// NOTE: libevent doesn't seem to implement LEV_OPT_CLOSE_ON_EXEC for Windows, so we must do this ourselves
	set_cloexec_socket(evconnlistener_get_fd(_smm_listener), true);
	
	if (old_smm_listener) {
		evconnlistener_free(old_smm_listener);
	}
	stratumsrv_port = port;
	
	return true;
}

static
void *stratumsrv_thread(__maybe_unused void *p)
{
	pthread_detach(pthread_self());
	RenameThread("stratumsrv");
	
	struct event_base *evbase = _smm_evbase;
	event_base_dispatch(evbase);
	_smm_running = false;
	
	return NULL;
}

static
bool stratumsrv_init_server() {
	work2d_init();
	
	if (-1
#if EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED
	 && evthread_use_windows_threads()
#endif
#if EVTHREAD_USE_PTHREADS_IMPLEMENTED
	 && evthread_use_pthreads()
#endif
	) {
		applog(LOG_ERR, "SSM: %s failed", "event_use_*threads");
		return false;
	}
	
	struct event_base *evbase = event_base_new();
	if (!evbase) {
		applog(LOG_ERR, "SSM: %s failed", "event_base_new");
		return false;
	}
	_smm_evbase = evbase;
	
	{
		ev_notify = evtimer_new(evbase, _stratumsrv_update_notify, NULL);
		if (!ev_notify) {
			applog(LOG_ERR, "SSM: %s failed", "evtimer_new");
			return false;
		}
		_stratumsrv_update_notify(-1, 0, NULL);
	}
	{
		notifier_init(_ssm_update_notifier);
		struct event *ev_update_notifier = event_new(evbase, _ssm_update_notifier[0], EV_READ | EV_PERSIST, _stratumsrv_update_notify, NULL);
		if (!ev_update_notifier) {
			applog(LOG_ERR, "SSM: %s failed", "event_new");
			return false;
		}
		event_add(ev_update_notifier, NULL);
	}
	
	_smm_running = true;
	
	pthread_t pth;
	if (unlikely(pthread_create(&pth, NULL, stratumsrv_thread, NULL)))
		quit(1, "stratumsrv thread create failed");
	
	return true;
}
