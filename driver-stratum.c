#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arpa/inet.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>

#include <jansson.h>

#include "deviceapi.h"
#include "driver-proxy.h"
#include "miner.h"
#include "util.h"

#define MAX_CLIENTS 255

static bool _ssm_xnonce1s[MAX_CLIENTS + 1] = { true };
static uint8_t _ssm_client_octets;
static uint8_t _ssm_client_xnonce2sz;
static char *_ssm_notify;
static int _ssm_notify_sz;
static struct event *ev_notify;
static notifier_t _ssm_update_notifier;

struct stratumsrv_job {
	char *my_job_id;
	
	struct pool *pool;
	uint8_t work_restart_id;
	uint8_t n2size;
	struct timeval tv_prepared;
	struct stratum_work swork;
	char *nonce1;
	
	UT_hash_handle hh;
};

static struct stratumsrv_job *_ssm_jobs;
static struct work _ssm_cur_job_work;
static uint64_t _ssm_jobid;

struct stratumsrv_conn {
	struct bufferevent *bev;
	uint32_t xnonce1_le;
	struct stratumsrv_conn *next;
};

static struct stratumsrv_conn *_ssm_connections;

static
void _ssm_gen_dummy_work(struct work *work, struct stratumsrv_job *ssj, const char * const extranonce2, uint32_t xnonce1)
{
	uint8_t *p, *s;
	
	*work = (struct work){
		.pool = ssj->pool,
		.work_restart_id = ssj->work_restart_id,
		.tv_staged = ssj->tv_prepared,
	};
	bytes_resize(&work->nonce2, ssj->n2size);
	s = bytes_buf(&work->nonce2);
	p = &s[ssj->n2size - _ssm_client_xnonce2sz];
	if (extranonce2)
		hex2bin(p, extranonce2, _ssm_client_xnonce2sz);
#ifndef __OPTIMIZE__
	else
		memset(p, '\0', _ssm_client_xnonce2sz);
#endif
	p -= _ssm_client_octets;
	memcpy(p, &xnonce1, _ssm_client_octets);
	if (p != s)
		memset(s, '\xbb', p - s);
	gen_stratum_work2(work, &ssj->swork, ssj->nonce1);
}

static
void stratumsrv_update_notify_str(struct pool * const pool, bool clean)
{
	struct stratumsrv_conn *conn;
	const struct stratum_work * const swork = &pool->swork;
	const int n2size = pool->n2size;
	char my_job_id[17];
	int i;
	struct stratumsrv_job *ssj;
	ssize_t n2pad = n2size - _ssm_client_octets - _ssm_client_xnonce2sz;
	if (n2pad < 0)
	{}// FIXME
	size_t coinb1in_lenx = swork->nonce2_offset * 2;
	size_t n2padx = n2pad * 2;
	size_t coinb1_lenx = coinb1in_lenx + n2padx;
	size_t coinb2_len = bytes_len(&swork->coinbase) - swork->nonce2_offset - n2size;
	size_t coinb2_lenx = coinb2_len * 2;
	sprintf(my_job_id, "%"PRIx64, _ssm_jobid++);
	size_t bufsz = 166 + strlen(my_job_id) + coinb1_lenx + coinb2_lenx + (swork->merkles * 67);
	char * const buf = malloc(bufsz);
	char *p = buf;
	char prevhash[65], coinb1[coinb1_lenx + 1], coinb2[coinb2_lenx], version[9], nbits[9], ntime[9];
	uint32_t ntime_n;
	bin2hex(prevhash, &swork->header1[4], 32);
	bin2hex(coinb1, bytes_buf(&swork->coinbase), swork->nonce2_offset);
	memset(&coinb1[coinb1in_lenx], 'B', n2padx);
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
		
		.pool = pool,
		.work_restart_id = pool->work_restart_id,
		.n2size = n2size,
		.nonce1 = strdup(pool->nonce1),
	};
	timer_set_now(&ssj->tv_prepared);
	stratum_work_cpy(&ssj->swork, swork);
	ssj->swork.data_lock_p = NULL;
	HASH_ADD_KEYPTR(hh, _ssm_jobs, ssj->my_job_id, strlen(ssj->my_job_id), ssj);
	_ssm_gen_dummy_work(&_ssm_cur_job_work, ssj, NULL, 0);
	
	_ssm_notify_sz = p - buf;
	assert(_ssm_notify_sz <= bufsz);
	_ssm_notify = buf;
	
	LL_FOREACH(_ssm_connections, conn)
	{
		if (unlikely(!conn->xnonce1_le))
			continue;
		bufferevent_write(conn->bev, _ssm_notify, _ssm_notify_sz);
	}
}

static
void _ssj_free(struct stratumsrv_job * const ssj)
{
	free(ssj->my_job_id);
	stratum_work_clean(&ssj->swork);
	free(ssj->nonce1);
	free(ssj);
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
	
	if (!pool->stratum_notify)
	{
		applog(LOG_WARNING, "SSM: Not using a stratum server upstream!");
		// FIXME
		return;
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
	
	stratumsrv_update_notify_str(pool, clean);
	
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
	
	if (!*xnonce1_p)
	{
		uint32_t xnonce1;
		for (xnonce1 = MAX_CLIENTS; _ssm_xnonce1s[xnonce1]; --xnonce1)
			if (!xnonce1)
				return_stratumsrv_failure(20, "Maximum clients already connected");
		*xnonce1_p = htole32(xnonce1);
	}
	
	bin2hex(xnonce1x, xnonce1_p, _ssm_client_octets);
	bufsz = sprintf(buf, "{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"x\"],[\"mining.notify\",\"%s\"]],\"%s\",%d],\"error\":null}\n", idstr, xnonce1x, xnonce1x, _ssm_client_xnonce2sz);
	bufferevent_write(bev, buf, bufsz);
	bufferevent_write(bev, "{\"params\":[0.9999847412109375],\"id\":null,\"method\":\"mining.set_difficulty\"}\n", 75);
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
void stratumsrv_mining_submit(struct bufferevent *bev, json_t *params, const char *idstr, uint32_t *xnonce1_p)
{
	struct work _work, *work;
	struct stratumsrv_job *ssj;
	struct proxy_client *client = stratumsrv_find_or_create_client(__json_array_string(params, 0));
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	const char * const job_id = __json_array_string(params, 1);
	const char * const extranonce2 = __json_array_string(params, 2);
	const char * const ntime = __json_array_string(params, 3);
	const char * const nonce = __json_array_string(params, 4);
	uint32_t nonce_n;
	
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
	
	// Generate dummy work
	work = &_work;
	_ssm_gen_dummy_work(work, ssj, extranonce2, *xnonce1_p);
	
	// Submit nonce
	hex2bin(&work->data[68], ntime, 4);
	hex2bin((void*)&nonce_n, nonce, 4);
	nonce_n = le32toh(nonce_n);
	if (!submit_nonce(thr, work, nonce_n))
		_stratumsrv_failure(bev, idstr, 23, "H-not-zero");
	else
	if (stale_work(work, true))
		_stratumsrv_failure(bev, idstr, 21, "stale");
	else
		_stratumsrv_success(bev, idstr);
	
	clean_work(work);
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
		applog(LOG_ERR, "SSM: JSON parse error: %s", ln);
		return false;
	}
	
	method = bfg_json_obj_string(json, "method", NULL);
	if (!method)
	{
		applog(LOG_ERR, "SSM: JSON missing method: %s", ln);
		return false;
	}
	
	params = json_object_get(json, "params");
	if (!params)
	{
		applog(LOG_ERR, "SSM: JSON missing params: %s", ln);
		return false;
	}
	
	applog(LOG_DEBUG, "SSM: RECV: %s", ln);
	
	j2 = json_object_get(json, "id");
	idstr = (j2 && !json_is_null(j2)) ? json_dumps_ANY(j2, 0) : NULL;
	
	if (!strcasecmp(method, "mining.submit"))
		stratumsrv_mining_submit(bev, params, idstr, &conn->xnonce1_le);
	else
	if (!strcasecmp(method, "mining.authorize"))
		stratumsrv_mining_authorize(bev, params, idstr, &conn->xnonce1_le);
	else
	if (!strcasecmp(method, "mining.subscribe"))
		stratumsrv_mining_subscribe(bev, params, idstr, &conn->xnonce1_le);
	else
		_stratumsrv_failure(bev, idstr, -3, "Method not supported");
	
	free(idstr);
	return true;
}

static
void stratumsrv_client_close(struct stratumsrv_conn * const conn)
{
	struct bufferevent * const bev = conn->bev;
	
	bufferevent_free(bev);
	LL_DELETE(_ssm_connections, conn);
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

void *stratumsrv_thread(__maybe_unused void *p)
{
	pthread_detach(pthread_self());
	RenameThread("stratumsrv");
	
	for (uint64_t n = MAX_CLIENTS; n; n >>= 8)
		++_ssm_client_octets;
	_ssm_client_xnonce2sz = 2;
	
	struct event_base *evbase = event_base_new();
	{
		ev_notify = evtimer_new(evbase, _stratumsrv_update_notify, NULL);
		_stratumsrv_update_notify(-1, 0, NULL);
	}
	{
		notifier_init(_ssm_update_notifier);
		struct event *ev_update_notifier = event_new(evbase, _ssm_update_notifier[0], EV_READ | EV_PERSIST, _stratumsrv_update_notify, NULL);
		event_add(ev_update_notifier, NULL);
	}
	{
		struct sockaddr_in sin = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = INADDR_ANY,
			.sin_port = htons(3334),
		};
		evconnlistener_new_bind(evbase, stratumlistener, NULL, (
			LEV_OPT_CLOSE_ON_FREE | LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_REUSEABLE
		), 0x10, (void*)&sin, sizeof(sin));
	}
	event_base_dispatch(evbase);
	
	return NULL;
}
