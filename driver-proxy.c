/*
 * Copyright 2013-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <unistd.h>

#include <pthread.h>

#include <uthash.h>

#include "deviceapi.h"
#include "driver-proxy.h"
#include "miner.h"
#include "util.h"

BFG_REGISTER_DRIVER(proxy_drv)
static const struct bfg_set_device_definition proxy_set_device_funcs[];

static
struct proxy_client *proxy_clients;
static
pthread_mutex_t proxy_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static
void prune_worklog()
{
	struct proxy_client *client, *tmp;
	struct work *work, *tmp2;
	struct timeval tv_now;
	
	timer_set_now(&tv_now);
	
	mutex_lock(&proxy_clients_mutex);
	HASH_ITER(hh, proxy_clients, client, tmp)
	{
		HASH_ITER(hh, client->work, work, tmp2)
		{
			if (timer_elapsed(&work->tv_work_start, &tv_now) <= opt_expiry)
				break;
			HASH_DEL(client->work, work);
			free_work(work);
		}
	}
	mutex_unlock(&proxy_clients_mutex);
}

static
pthread_t prune_worklog_pth;

static
void *prune_worklog_thread(void *userdata)
{
	struct cgpu_info *cgpu = userdata;
	
	pthread_detach(pthread_self());
	RenameThread("PXY_pruner");
	
	while (!cgpu->shutdown)
	{
		prune_worklog();
		sleep(60);
	}
	return NULL;
}

static
float proxy_min_nonce_diff(struct cgpu_info * const proc, const struct mining_algorithm * const malgo)
{
	return minimum_pdiff;
}

static
void proxy_first_client(struct cgpu_info *cgpu)
{
	pthread_create(&prune_worklog_pth, NULL, prune_worklog_thread, cgpu);
}

struct proxy_client *proxy_find_or_create_client(const char *username)
{
	struct proxy_client *client;
	struct cgpu_info *cgpu;
	char *user;
	int b;
	
	if (!username)
		return NULL;
	
	mutex_lock(&proxy_clients_mutex);
	HASH_FIND_STR(proxy_clients, username, client);
	if (!client)
	{
		user = strdup(username);
		cgpu = malloc(sizeof(*cgpu));
		client = malloc(sizeof(*client));
		*cgpu = (struct cgpu_info){
			.drv = &proxy_drv,
			.set_device_funcs = proxy_set_device_funcs,
			.threads = 0,
			.device_data = client,
			.device_path = user,
		};
		timer_set_now(&cgpu->cgminer_stats.start_tv);
		if (unlikely(!create_new_cgpus(add_cgpu_live, cgpu)))
		{
			free(client);
			free(cgpu);
			free(user);
			return NULL;
		}
		*client = (struct proxy_client){
			.username = user,
			.cgpu = cgpu,
			.desired_share_pdiff = 0.,
		};
		
		b = HASH_COUNT(proxy_clients);
		HASH_ADD_KEYPTR(hh, proxy_clients, client->username, strlen(user), client);
		mutex_unlock(&proxy_clients_mutex);
		
		if (!b)
			proxy_first_client(cgpu);
		
		cgpu_set_defaults(cgpu);
	}
	else
	{
		mutex_unlock(&proxy_clients_mutex);
		cgpu = client->cgpu;
	}
	thread_reportin(cgpu->thr[0]);
	return client;
}

// See also, stratumsrv_init_diff in driver-stratum.c
static
const char *proxy_set_diff(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const success)
{
	struct proxy_client * const client = proc->device_data;
	double nv = atof(newvalue);
	if (nv < 0)
		return "Invalid difficulty";
	
	if (nv <= minimum_pdiff)
		nv = minimum_pdiff;
	client->desired_share_pdiff = nv;
	
#ifdef USE_LIBEVENT
	stratumsrv_client_changed_diff(client);
#endif
	
	return NULL;
}

#ifdef HAVE_CURSES
static
void proxy_wlogprint_status(struct cgpu_info *cgpu)
{
	struct proxy_client *client = cgpu->device_data;
	wlogprint("Username: %s\n", client->username);
}
#endif

static const struct bfg_set_device_definition proxy_set_device_funcs[] = {
	{"diff", proxy_set_diff, "desired share difficulty for clients"},
	{NULL},
};

struct device_drv proxy_drv = {
	.dname = "proxy",
	.name = "PXY",
	.drv_min_nonce_diff = proxy_min_nonce_diff,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = proxy_wlogprint_status,
#endif
};
