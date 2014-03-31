/*
 * Copyright 2013 Luke Dashjr
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
			.threads = 0,
			.device_data = client,
			.device_path = user,
			.min_nonce_diff = (opt_scrypt ? (1./0x10000) : 1.),
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
		};
		
		b = HASH_COUNT(proxy_clients);
		HASH_ADD_KEYPTR(hh, proxy_clients, client->username, strlen(user), client);
		mutex_unlock(&proxy_clients_mutex);
		
		if (!b)
			proxy_first_client(cgpu);
	}
	else
	{
		mutex_unlock(&proxy_clients_mutex);
		cgpu = client->cgpu;
	}
	thread_reportin(cgpu->thr[0]);
	return client;
}

#ifdef HAVE_CURSES
static
void proxy_wlogprint_status(struct cgpu_info *cgpu)
{
	struct proxy_client *client = cgpu->device_data;
	wlogprint("Username: %s\n", client->username);
}
#endif

struct device_drv proxy_drv = {
	.dname = "proxy",
	.name = "PXY",
#ifdef HAVE_CURSES
	.proc_wlogprint_status = proxy_wlogprint_status,
#endif
};
