/*
 * Copyright 2013-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <stdint.h>
#include <string.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include <jansson.h>
#include <microhttpd.h>
#include <uthash.h>

#include "deviceapi.h"
#include "driver-proxy.h"
#include "httpsrv.h"
#include "miner.h"

static
void getwork_prepare_resp(struct MHD_Response *resp, struct MHD_Connection * const conn)
{
	httpsrv_prepare_resp(resp);
	MHD_add_response_header(resp, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
	MHD_add_response_header(resp, "X-Mining-Extensions", "hashesdone");
#if MHD_VERSION >= 0x00093701
	const char * const agent = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "User-Agent");
	// Block Erupter products can't handle the Connection: close header, so force HTTP/1.0 for them
	if (!(strcmp(agent, "BE Cube BTC Miner") && strcmp(agent, "Jephis PIC Miner")))
		MHD_set_response_options(resp, MHD_RF_HTTP_VERSION_1_0_ONLY, MHD_RO_END);
#endif
}

static
struct MHD_Response *getwork_gen_error(int16_t errcode, const char *errmsg, const char *idstr, size_t idstr_sz, struct MHD_Connection * const conn)
{
	size_t replysz = 0x40 + strlen(errmsg) + idstr_sz;
	char * const reply = malloc(replysz);
	replysz = snprintf(reply, replysz, "{\"result\":null,\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%s}", errcode, errmsg, idstr ?: "0");
	struct MHD_Response * const resp = MHD_create_response_from_buffer(replysz, reply, MHD_RESPMEM_MUST_FREE);
	getwork_prepare_resp(resp, conn);
	return resp;
}

static
int getwork_error(struct MHD_Connection *conn, int16_t errcode, const char *errmsg, const char *idstr, size_t idstr_sz)
{
	struct MHD_Response * const resp = getwork_gen_error(errcode, errmsg, idstr, idstr_sz, conn);
	const int ret = MHD_queue_response(conn, 500, resp);
	MHD_destroy_response(resp);
	return ret;
}

int handle_getwork(struct MHD_Connection *conn, bytes_t *upbuf)
{
	struct proxy_client *client;
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
	long long hashes_done = -1;
	int ret;
	
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
		resp = getwork_gen_error(-4096, "Please provide a username", idstr, idstr_sz, conn);
		ret = MHD_queue_basic_auth_fail_response(conn, PACKAGE, resp);
		goto out;
	}
	
	client = proxy_find_or_create_client(user);
	free(user);
	if (!client)
	{
		ret = getwork_error(conn, -32603, "Failed creating new cgpu", idstr, idstr_sz);
		goto out;
	}
	cgpu = client->cgpu;
	thr = cgpu->thr[0];
	
	{
		const char * const hashesdone = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "X-Hashes-Done");
		if (hashesdone)
			hashes_done = strtoll(hashesdone, NULL, 0);
	}
	
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
			
			if (hashes_done == -1)
				hashes_done = (double)0x100000000 * work->nonce_diff;
		}
		
		reply = malloc(36 + idstr_sz);
		const size_t replysz =
		sprintf(reply, "{\"error\":null,\"result\":%s,\"id\":%s}",
		        rejreason ? "false" : "true", idstr);
		resp = MHD_create_response_from_buffer(replysz, reply, MHD_RESPMEM_MUST_FREE);
		getwork_prepare_resp(resp, conn);
		MHD_add_response_header(resp, "X-Mining-Identifier", cgpu->proc_repr);
		if (rejreason)
			MHD_add_response_header(resp, "X-Reject-Reason", rejreason);
		ret = MHD_queue_response(conn, 200, resp);
		MHD_destroy_response(resp);
		goto out;
	}
	
	if (cgpu->deven == DEV_DISABLED)
	{
		resp = getwork_gen_error(-10, "Virtual device has been disabled", idstr, idstr_sz, conn);
		MHD_add_response_header(resp, "X-Mining-Identifier", cgpu->proc_repr);
		ret = MHD_queue_response(conn, 500, resp);
		MHD_destroy_response(resp);
		goto out;
	}
	
	{
		size_t replysz = 590 + idstr_sz;
		
		work = get_work(thr);
		const struct mining_algorithm * const malgo = work_mining_algorithm(work);
		work->nonce_diff = client->desired_share_pdiff ?: malgo->reasonable_low_nonce_diff;
		if (work->nonce_diff > work->work_difficulty)
			work->nonce_diff = work->work_difficulty;
		
		reply = malloc(replysz);
		uint8_t target[0x20];
		set_target_to_pdiff(target, work->nonce_diff);
		memcpy(reply, "{\"error\":null,\"result\":{\"target\":\"", 34);
		bin2hex(&reply[34], target, sizeof(target));
		memcpy(&reply[98], "\",\"data\":\"", 10);
		bin2hex(&reply[108], work->data, 128);
		memcpy(&reply[364], "\",\"midstate\":\"", 14);
		bin2hex(&reply[378], work->midstate, 32);
		memcpy(&reply[442], "\",\"hash1\":\"00000000000000000000000000000000000000000000000000000000000000000000008000000000000000000000000000000000000000000000000000010000\"},\"id\":", 147);
		memcpy(&reply[589], idstr ?: "0", idstr_sz);
		memcpy(&reply[589 + idstr_sz], "}", 1);
#ifdef USE_SCRYPT
		if (malgo->algo == POW_SCRYPT)
		{
			replysz += 21;
			reply = realloc(reply, replysz);
			memmove(&reply[443 + 21], &reply[443], replysz - (443 + 21));
			memcpy(&reply[443], ",\"algorithm\":\"scrypt\"", 21);
		}
#endif
		
		timer_set_now(&work->tv_work_start);
		HASH_ADD_KEYPTR(hh, client->work, work->data, 76, work);
		
		resp = MHD_create_response_from_buffer(replysz, reply, MHD_RESPMEM_MUST_FREE);
		getwork_prepare_resp(resp, conn);
		MHD_add_response_header(resp, "X-Mining-Identifier", cgpu->proc_repr);
		ret = MHD_queue_response(conn, 200, resp);
		MHD_destroy_response(resp);
	}
	
out:
	if (hashes_done != -1)
		hashes_done2(thr, hashes_done, NULL);
	
	free(idstr);
	if (json)
		json_decref(json);
	return ret;
}
