/*
 * Copyright 2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdint.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <jansson.h>
#include <microhttpd.h>
#include <uthash.h>

#include "deviceapi.h"
#include "httpsrv.h"
#include "miner.h"

struct device_drv getwork_drv;

struct getwork_client {
	char *username;
	struct cgpu_info *cgpu;
	struct work *work;
	struct timeval tv_hashes_done;
	
	UT_hash_handle hh;
};

static
struct getwork_client *getwork_clients;
static
pthread_mutex_t getwork_clients_mutex;

static
void prune_worklog()
{
	struct getwork_client *client, *tmp;
	struct work *work, *tmp2;
	struct timeval tv_now;
	
	timer_set_now(&tv_now);
	
	mutex_lock(&getwork_clients_mutex);
	HASH_ITER(hh, getwork_clients, client, tmp)
	{
		HASH_ITER(hh, client->work, work, tmp2)
		{
			if (timer_elapsed(&work->tv_work_start, &tv_now) <= opt_expiry)
				break;
			HASH_DEL(client->work, work);
			free_work(work);
		}
	}
	mutex_unlock(&getwork_clients_mutex);
}

static
pthread_t prune_worklog_pth;

static
void *prune_worklog_thread(void *userdata)
{
	struct cgpu_info *cgpu = userdata;
	
	pthread_detach(pthread_self());
	RenameThread("SGW_pruner");
	
	while (!cgpu->shutdown)
	{
		prune_worklog();
		sleep(60);
	}
	return NULL;
}

static
void getwork_init()
{
	mutex_init(&getwork_clients_mutex);
}

static
void getwork_first_client()
{
	pthread_create(&prune_worklog_pth, NULL, prune_worklog_thread, getwork_clients);
}

static
void getwork_prepare_resp(struct MHD_Response *resp)
{
	httpsrv_prepare_resp(resp);
	MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
	MHD_add_response_header(resp, "X-Mining-Extensions", "hashesdone");
}

static
struct MHD_Response *getwork_gen_error(int16_t errcode, const char *errmsg, const char *idstr, size_t idstr_sz)
{
	size_t replysz = 0x40 + strlen(errmsg) + idstr_sz;
	char * const reply = malloc(replysz);
	replysz = snprintf(reply, replysz, "{\"result\":null,\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%s}", errcode, errmsg, idstr ?: "0");
	struct MHD_Response * const resp = MHD_create_response_from_buffer(replysz, reply, MHD_RESPMEM_MUST_FREE);
	getwork_prepare_resp(resp);
	return resp;
}

static
int getwork_error(struct MHD_Connection *conn, int16_t errcode, const char *errmsg, const char *idstr, size_t idstr_sz)
{
	struct MHD_Response * const resp = getwork_gen_error(errcode, errmsg, idstr, idstr_sz);
	const int ret = MHD_queue_response(conn, 500, resp);
	MHD_destroy_response(resp);
	return ret;
}

int handle_getwork(struct MHD_Connection *conn, bytes_t *upbuf)
{
	static bool _init = false, b;
	struct getwork_client *client;
	struct MHD_Response *resp;
	char *user, *idstr = NULL;
	const char *submit = NULL;
	size_t idstr_sz = 1;
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	json_t *json = NULL, *j2;
	json_error_t jerr;
	struct work *work;
	char *reply;
	const char *hashesdone = NULL;
	int ret;
	
	if (unlikely(!_init))
	{
		_init = true;
		getwork_init();
	}
	
	if (bytes_len(upbuf))
	{
		bytes_nullterminate(upbuf);
		json = JSON_LOADS((char*)bytes_buf(upbuf), &jerr);
		if (!json)
		{
			ret = getwork_error(conn, -32700, "JSON parse error", idstr, idstr_sz);
			goto out;
		}
		j2 = json_object_get(json, "id");
		if (j2)
		{
			idstr = json_dumps_ANY(j2, 0);
			idstr_sz = strlen(idstr);
		}
		if (strcmp("getwork", bfg_json_obj_string(json, "method", "getwork")))
		{
			ret = getwork_error(conn, -32601, "Only getwork supported", idstr, idstr_sz);
			goto out;
		}
		j2 = json_object_get(json, "params");
		submit = j2 ? __json_array_string(j2, 0) : NULL;
	}
	
	user = MHD_basic_auth_get_username_password(conn, NULL);
	if (!user)
	{
		resp = getwork_gen_error(-4096, "Please provide a username", idstr, idstr_sz);
		ret = MHD_queue_basic_auth_fail_response(conn, PACKAGE, resp);
		goto out;
	}
	
	mutex_lock(&getwork_clients_mutex);
	HASH_FIND_STR(getwork_clients, user, client);
	if (!client)
	{
		cgpu = malloc(sizeof(*cgpu));
		client = malloc(sizeof(*client));
		*cgpu = (struct cgpu_info){
			.drv = &getwork_drv,
			.threads = 0,
			.device_data = client,
			.device_path = user,
		};
		if (unlikely(!create_new_cgpus(add_cgpu_live, cgpu)))
		{
			free(client);
			free(cgpu);
			ret = getwork_error(conn, -32603, "Failed creating new cgpu", idstr, idstr_sz);
			goto out;
		}
		*client = (struct getwork_client){
			.username = user,
			.cgpu = cgpu,
		};
		
		b = HASH_COUNT(getwork_clients);
		HASH_ADD_KEYPTR(hh, getwork_clients, client->username, strlen(user), client);
		mutex_unlock(&getwork_clients_mutex);
		
		if (!b)
			getwork_first_client();
	}
	else
	{
		mutex_unlock(&getwork_clients_mutex);
		free(user);
		cgpu = client->cgpu;
	}
	user = NULL;
	thr = cgpu->thr[0];
	
	hashesdone = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "X-Hashes-Done");
	
	if (submit)
	{
		unsigned char hdr[80];
		const char *rejreason;
		uint32_t nonce;
		
		// NOTE: expecting hex2bin to fail since we only parse 80 of the 128
		hex2bin(hdr, submit, 80);
		nonce = le32toh(*(uint32_t *)&hdr[76]);
		HASH_FIND(hh, client->work, hdr, 76, work);
		if (!work)
		{
			inc_hw_errors2(thr, NULL, &nonce);
			rejreason = "unknown-work";
		}
		else
		{
			if (!submit_nonce(thr, work, nonce))
				rejreason = "H-not-zero";
			else
			if (stale_work(work, true))
				rejreason = "stale";
			else
				rejreason = NULL;
			
			if (!hashesdone)
				hashesdone = "0x100000000";
		}
		
		reply = malloc(36 + idstr_sz);
		const size_t replysz =
		sprintf(reply, "{\"error\":null,\"result\":%s,\"id\":%s}",
		        rejreason ? "false" : "true", idstr);
		resp = MHD_create_response_from_buffer(replysz, reply, MHD_RESPMEM_MUST_FREE);
		getwork_prepare_resp(resp);
		MHD_add_response_header(resp, "X-Mining-Identifier", cgpu->proc_repr);
		if (rejreason)
			MHD_add_response_header(resp, "X-Reject-Reason", rejreason);
		ret = MHD_queue_response(conn, 200, resp);
		MHD_destroy_response(resp);
		goto out;
	}
	
	if (cgpu->deven == DEV_DISABLED)
	{
		resp = getwork_gen_error(-10, "Virtual device has been disabled", idstr, idstr_sz);
		MHD_add_response_header(resp, "X-Mining-Identifier", cgpu->proc_repr);
		ret = MHD_queue_response(conn, 500, resp);
		MHD_destroy_response(resp);
		goto out;
	}
	
	{
		const size_t replysz = 590 + idstr_sz;
		
		work = get_work(thr);
		reply = malloc(replysz);
		memcpy(reply, "{\"error\":null,\"result\":{\"target\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffff00000000\",\"data\":\"", 108);
		bin2hex(&reply[108], work->data, 128);
		memcpy(&reply[364], "\",\"midstate\":\"", 14);
		bin2hex(&reply[378], work->midstate, 32);
		memcpy(&reply[442], "\",\"hash1\":\"00000000000000000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000000000000000010000\"},\"id\":", 147);
		memcpy(&reply[589], idstr ?: "0", idstr_sz);
		memcpy(&reply[589 + idstr_sz], "}", 1);
		
		timer_set_now(&work->tv_work_start);
		HASH_ADD_KEYPTR(hh, client->work, work->data, 76, work);
		
		resp = MHD_create_response_from_buffer(replysz, reply, MHD_RESPMEM_MUST_FREE);
		getwork_prepare_resp(resp);
		MHD_add_response_header(resp, "X-Mining-Identifier", cgpu->proc_repr);
		ret = MHD_queue_response(conn, 200, resp);
		MHD_destroy_response(resp);
	}
	
out:
	if (hashesdone)
	{
		struct timeval tv_now, tv_delta;
		long long lld = strtoll(hashesdone, NULL, 0);
		timer_set_now(&tv_now);
		timersub(&tv_now, &client->tv_hashes_done, &tv_delta);
		client->tv_hashes_done = tv_now;
		hashes_done(thr, lld, &tv_delta, NULL);
	}
	
	free(user);
	free(idstr);
	if (json)
		json_decref(json);
	return ret;
}

#ifdef HAVE_CURSES
static
void getwork_wlogprint_status(struct cgpu_info *cgpu)
{
	struct getwork_client *client = cgpu->device_data;
	wlogprint("Username: %s\n", client->username);
}
#endif

struct device_drv getwork_drv = {
	.dname = "getwork",
	.name = "SGW",
#ifdef HAVE_CURSES
	.proc_wlogprint_status = getwork_wlogprint_status,
#endif
};
