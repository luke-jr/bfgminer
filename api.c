/*
 * Copyright 2011 Kano
 * Copyright 2011 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "compat.h"
#include "miner.h"

#if defined(unix)
	#include <errno.h>
//	#include <fcntl.h>
//	#include <sys/wait.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Big enough for largest API request
//  though a PC with 100s of CPUs may exceed the size ...
// Current code assumes it can socket send this size also
#define MYBUFSIZ	16384

// Socket is on 127.0.0.1
#define QUEUE	10

static char *buffer = NULL;

static const char *UNAVAILABLE = " - API will not be available";

static const char *BLANK = "";

static const char *APIVERSION = "0.1";
static const char *DEAD = "DEAD";
static const char *SICK = "SICK";
static const char *DISABLED = "DISABLED";
static const char *ALIVE = "ALIVE";

static const char *YES = "Y";
static const char *NO = "N";

static int bye = 0;

char *apiversion(char *params)
{
	return (char *)APIVERSION;
}

void gpustatus(int thr_id)
{
	char buf[BUFSIZ];
	char status_buf[BUFSIZ];
	char *status;
	float gt;
	int gf, gp;

	if (thr_id >= 0 && thr_id < gpu_threads) {
		int gpu = thr_info[thr_id].cgpu->cpu_gpu;
		struct cgpu_info *cgpu = &gpus[gpu];

		cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

#ifdef HAVE_ADL
		if (cgpu->has_adl) {
			gt = gpu_temp(gpu);
			gf = gpu_fanspeed(gpu);
			gp = gpu_fanpercent(gpu);
		}
		else
#endif
		gt = gf = gp = 0;

		if (cgpu->status == LIFE_DEAD)
			status = (char *)DEAD;
		else if (cgpu->status == LIFE_SICK)
			status = (char *)SICK;
		else if (!gpu_devices[gpu])
			status = (char *)DISABLED;
		else {
			sprintf(status_buf, "%.1f", cgpu->rolling);
			status = status_buf;
		}

		sprintf(buf, "GPU=%d,GT=%.2f,FR=%d,FP=%d,STA=%s,MHS=%.2f,A=%d,R=%d,HW=%d,U=%.2f,I=%d|",
			gpu, gt, gf, gp, status,
			cgpu->total_mhashes / total_secs,
			cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->utility, gpus->intensity);

		strcat(buffer, buf);
	}
}

void cpustatus(int thr_id)
{
	char buf[BUFSIZ];

	if (thr_id >= gpu_threads) {
		int cpu = thr_info[thr_id].cgpu->cpu_gpu;
		struct cgpu_info *cgpu = &cpus[cpu];

		cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

		sprintf(buf, "CPU=%d,STA=%.2f,MHS=%.2f,A=%d,R=%d,U=%.2f|",
			cpu, cgpu->rolling,
			cgpu->total_mhashes / total_secs,
			cgpu->accepted, cgpu->rejected,
			cgpu->utility);

		strcat(buffer, buf);
	}
}

char *devstatus(char *params)
{
	int i;

	*buffer = '\0';

	for (i = 0; i < gpu_threads; i++)
		gpustatus(i);

	for (i = gpu_threads; i < mining_threads; i++)
		cpustatus(i);

	return buffer;
}

char *poolstatus(char *params)
{
	char buf[BUFSIZ];
	char *status, *lp;
	int i;

	*buffer = '\0';

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (!pool->enabled)
			status = (char *)DISABLED;
		else
		{
			if (pool->idle)
				status = (char *)DEAD;
			else
				status = (char *)ALIVE;
		}

		if (pool->hdr_path)
			lp = (char *)YES;
		else
			lp = (char *)NO;

		sprintf(buf, "POOL=%d,URL=%s,STA=%s,PRI=%d,LP=%s,Q=%d,A=%d,R=%d,DW=%d,ST=%d,GF=%d,RF=%d|",
			i, pool->rpc_url, status, pool->prio, lp,
			pool->getwork_requested,
			pool->accepted, pool->rejected,
			pool->discarded_work,
			pool->stale_shares,
			pool->getfail_occasions,
			pool->remotefail_occasions);

		strcat(buffer, buf);
	}

	return buffer;
}

struct CMDS {
	char *name;
	char *(*func)(char *);
} cmds[] = {
	{ "apiversion",	apiversion },
	{ "dev",	devstatus },
	{ "pool",	poolstatus },
};

#define CMDMAX 3

void send_result(int c, char *result)
{
	int n;

	if (result == NULL)
		result = (char *)BLANK;

	// ignore failure - it's closed immediately anyway
	n = write(c, result, strlen(result)+1);
}

void api()
{
	char buf[BUFSIZ];
	const char *addr;
	int c, sock, n, bound;
	char tmpaddr[32];
	char *binderror;
	time_t bindstart;
	short int port = 4028;
	struct sockaddr_in serv;
	struct sockaddr_in cli;
	socklen_t clisiz;
	long long counter;
	char *result;
	char *params;
	int i;

	addr = "127.0.0.1";

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		applog(LOG_ERR, "API1 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
		return;
	}

	memset(&serv, 0, sizeof(serv));

	serv.sin_family = AF_INET;
	if (inet_pton(AF_INET, addr, &(serv.sin_addr)) == 0) {
		applog(LOG_ERR, "API2 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
		return;
	}
	serv.sin_port = htons(port);

	// try for 1 minute ... in case the old one hasn't completely gone yet
	bound = 0;
	bindstart = time(NULL);
	while (bound == 0) {
		if (bind(sock, (struct sockaddr *)(&serv), sizeof(serv)) < 0) {
			binderror = strerror(errno);
			if ((time(NULL) - bindstart) > 61)
				break;
			else {
				applog(LOG_ERR, "API bind to port %d failed - trying again in 15sec", port);
				sleep(15);
			}
		}
		else
			bound = 1;
	}

	if (bound == 0) {
		applog(LOG_ERR, "API bind to port %d failed (%s)%s", port, binderror, UNAVAILABLE);
		return;
	}

	if (listen(sock, QUEUE) < 0) {
		applog(LOG_ERR, "API3 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
		close(sock);
		return;
	}

	buffer = malloc(MYBUFSIZ+1);

	counter = 0;
	while (bye == 0) {
		counter++;

		clisiz = sizeof(cli);
		if ((c = accept(sock, (struct sockaddr *)(&cli), &clisiz)) < 0) {
			applog(LOG_ERR, "API failed (%s)%s", strerror(errno), UNAVAILABLE);
			close(sock);
			free(buffer);
			return;
		}

		inet_ntop(AF_INET, &(cli.sin_addr), &(tmpaddr[0]), sizeof(tmpaddr)-1);
		if (strcmp(tmpaddr, addr) == 0) {
			n = read(c, &buf[0], BUFSIZ-1);
			if (n >= 0) {
				buf[n] = '\0';
				params = strchr(buf, '|');
				if (params != NULL)
					*(params++) = '\0';

				for (i = 0; i < CMDMAX; i++) {
					if (strcmp(buf, cmds[i].name) == 0) {
						result = (cmds[i].func)(params);
						send_result(c, result);
						close(c);
						break;
					}
				}
			}
		}
		close(c);
	}

	close(sock);
	free(buffer);
}
