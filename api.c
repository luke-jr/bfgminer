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
#include <sys/types.h>

#include "compat.h"
#include "miner.h"

#if defined(unix)
	#include <errno.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Big enough for largest API request
//  though a PC with 100s of CPUs may exceed the size ...
// Current code assumes it can socket send this size also
#define MYBUFSIZ	16384

// Number of requets to queue - normally would be small
#define QUEUE	30

static char *buffer = NULL;
static char error_buffer[BUFSIZ];

static const char *UNAVAILABLE = " - API will not be available";

static const char *BLANK = "";
static const char SEPARATOR = '|';
static const char *SEPARATORSTR = "|";

#define MSG_INVGPU 1
#define MSG_ALRENA 2
#define MSG_ALRDIS 3
#define MSG_GPUMRE 4
#define MSG_GPUREN 5
#define MSG_GPUNON 6
#define MSG_POOL 7
#define MSG_NOPOOL 8
#define MSG_DEVS 9
#define MSG_NODEVS 10
#define MSG_SUMM 11
#define MSG_GPUDIS 12
#define MSG_GPUREI 13
#define MSG_INVCMD 14

enum code_severity {
	SEVERITY_ERR,
	SEVERITY_WARN,
	SEVERITY_INFO,
	SEVERITY_SUCC,
	SEVERITY_FAIL
};

enum code_parameters {
	PARAM_GPU,
	PARAM_NONE,
	PARAM_GPUMAX,
	PARAM_PMAX,
	PARAM_GCMAX,
};

struct CODES {
	enum code_severity severity;
	int code;
	enum code_parameters params;
	const char *description;
} codes[] = {
 { SEVERITY_ERR,   MSG_INVGPU,	PARAM_GPUMAX,	"Invalid GPU id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_ALRENA,	PARAM_GPU,	"GPU %d already enabled" },
 { SEVERITY_INFO,  MSG_ALRDIS,	PARAM_GPU,	"GPU %d already disabled" },
 { SEVERITY_WARN,  MSG_GPUMRE,	PARAM_GPU,	"GPU %d must be restarted first" },
 { SEVERITY_INFO,  MSG_GPUREN,	PARAM_GPU,	"GPU %d sent enable message" },
 { SEVERITY_WARN,  MSG_GPUNON,	PARAM_NONE,	"No GPUs" },
 { SEVERITY_SUCC,  MSG_POOL,	PARAM_PMAX,	"%d Pool" },
 { SEVERITY_WARN,  MSG_NOPOOL,	PARAM_NONE,	"No pools" },
 { SEVERITY_SUCC,  MSG_DEVS,	PARAM_GCMAX,	"%d GPU - %d CPU" },
 { SEVERITY_WARN,  MSG_NODEVS,	PARAM_NONE,	"No GPUs/CPUs" },
 { SEVERITY_SUCC,  MSG_SUMM,	PARAM_NONE,	"Summary" },
 { SEVERITY_INFO,  MSG_GPUDIS,	PARAM_GPU,	"GPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_GPUREI,	PARAM_GPU,	"GPU %d restart attempted" },
 { SEVERITY_ERR,   MSG_INVCMD,	PARAM_NONE,	"Invalid command" },
 { SEVERITY_FAIL }
};

static const char *APIVERSION = "0.2";
static const char *DEAD = "DEAD";
static const char *SICK = "SICK";
static const char *NOSTART = "NOSTART";
static const char *DISABLED = "DISABLED";
static const char *ALIVE = "ALIVE";
static const char *DYNAMIC = "D";

static const char *YES = "Y";
static const char *NO = "N";

static int bye = 0;
static bool ping = true;

static char *message(int messageid, int gpuid)
{
	char severity;
	char *ptr;
	int i;

	for (i = 0; codes[i].severity != SEVERITY_FAIL; i++) {
		if (codes[i].code == messageid) {
			switch (codes[i].severity) {
			case SEVERITY_WARN:
				severity = 'W';
				break;
			case SEVERITY_INFO:
				severity = 'I';
				break;
			case SEVERITY_SUCC:
				severity = 'S';
				break;
			case SEVERITY_ERR:
			default:
				severity = 'E';
				break;
			}

			sprintf(error_buffer, "STATUS=%c,CODE=%d,MSG=",
					severity, messageid);

			ptr = error_buffer + strlen(error_buffer);

			switch(codes[i].params) {
			case PARAM_GPU:
				sprintf(ptr, codes[i].description, gpuid);
				break;
			case PARAM_GPUMAX:
				sprintf(ptr, codes[i].description,
						gpuid, gpu_threads - 1);
				break;
			case PARAM_PMAX:
				sprintf(ptr, codes[i].description, total_pools);
				break;
			case PARAM_GCMAX:
				sprintf(ptr, codes[i].description,
						gpu_threads,
						(mining_threads - gpu_threads));
				break;
			case PARAM_NONE:
			default:
				strcpy(ptr, codes[i].description);
			}

			strcat(error_buffer, SEPARATORSTR);

			return error_buffer;
		}
	}

	sprintf(error_buffer, "STATUS=F,CODE=-1,MSG=%d%c", messageid, SEPARATOR);
	return error_buffer;
}

char *apiversion(char *params)
{
	return (char *)APIVERSION;
}

void gpustatus(int thr_id)
{
	char intensity[10];
	char buf[BUFSIZ];
	char *enabled;
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

		if (gpu_devices[gpu])
			enabled = (char *)YES;
		else
			enabled = (char *)NO;

		if (cgpu->status == LIFE_DEAD)
			status = (char *)DEAD;
		else if (cgpu->status == LIFE_SICK)
			status = (char *)SICK;
		else if (cgpu->status == LIFE_NOSTART)
			status = (char *)NOSTART;
		else
			status = (char *)ALIVE;

		if (cgpu->dynamic)
			strcpy(intensity, DYNAMIC);
		else
			sprintf(intensity, "%d", gpus->intensity);

		sprintf(buf, "GPU=%d,GT=%.2f,FR=%d,FP=%d,EN=%s,STA=%s,MHS=%.2f,A=%d,R=%d,HW=%d,U=%.2f,I=%s%c",
			gpu, gt, gf, gp, enabled, status,
			cgpu->total_mhashes / total_secs,
			cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->utility, intensity, SEPARATOR);

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

		sprintf(buf, "CPU=%d,STA=%.2f,MHS=%.2f,A=%d,R=%d,U=%.2f%c",
			cpu, cgpu->rolling,
			cgpu->total_mhashes / total_secs,
			cgpu->accepted, cgpu->rejected,
			cgpu->utility, SEPARATOR);

		strcat(buffer, buf);
	}
}

char *devstatus(char *params)
{
	int i;

	if (gpu_threads == 0 && mining_threads == 0)
		return message(MSG_NODEVS, 0);

	strcpy(buffer, message(MSG_DEVS, 0));

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

	if (total_pools == 0)
		return message(MSG_NOPOOL, 0);

	strcpy(buffer, message(MSG_POOL, 0));

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

		sprintf(buf, "POOL=%d,URL=%s,STA=%s,PRI=%d,LP=%s,Q=%d,A=%d,R=%d,DW=%d,ST=%d,GF=%d,RF=%d%c",
			i, pool->rpc_url, status, pool->prio, lp,
			pool->getwork_requested,
			pool->accepted, pool->rejected,
			pool->discarded_work,
			pool->stale_shares,
			pool->getfail_occasions,
			pool->remotefail_occasions, SEPARATOR);

		strcat(buffer, buf);
	}

	return buffer;
}

char *summary(char *params)
{
	double utility, mhs;

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	mhs = total_mhashes_done / total_secs;

	sprintf(buffer, "%sSUMMARY=all,EL=%.0lf,ALGO=%s,MHS=%.2lf,SOL=%d,Q=%d,A=%d,R=%d,HW=%d,U=%.2lf,DW=%d,ST=%d,GF=%d,LW=%d,RO=%d,BC=%d%c",
		message(MSG_SUMM, 0),
		total_secs, algo_names[opt_algo], mhs, found_blocks,
		total_getworks, total_accepted, total_rejected,
		hw_errors, utility, total_discarded, total_stale,
		total_go, local_work, total_ro, new_blocks, SEPARATOR);

	return buffer;
}

char *gpuenable(char *params)
{
	struct thr_info *thr;
	int gpu;
	int id;
	int i;

	if (gpu_threads == 0)
		return message(MSG_GPUNON, 0);

	id = atoi(params);
	if (id < 0 || id >= gpu_threads)
		return message(MSG_INVGPU, id);

	if (gpu_devices[id])
		return message(MSG_ALRENA, id);

	for (i = 0; i < gpu_threads; i++) {
		gpu = thr_info[i].cgpu->cpu_gpu;
		if (gpu == id) {
			thr = &thr_info[i];
			if (thr->cgpu->status != LIFE_WELL)
				return message(MSG_GPUMRE, id);

			gpu_devices[id] = true;
			tq_push(thr->q, &ping);

			return message(MSG_GPUREN, id);
		}
	}

	return message(-2, 0);
}

char *gpudisable(char *params)
{
	int id;

	if (gpu_threads == 0)
		return message(MSG_GPUNON, 0);

	id = atoi(params);
	if (id < 0 || id >= gpu_threads)
		return message(MSG_INVGPU, id);

	if (!gpu_devices[id])
		return message(MSG_ALRDIS, id);

	gpu_devices[id] = false;

	return message(MSG_GPUDIS, id);
}

char *gpurestart(char *params)
{
	int id;

	if (gpu_threads == 0)
		return message(MSG_GPUNON, 0);

	id = atoi(params);
	if (id < 0 || id >= gpu_threads)
		return message(MSG_INVGPU, id);

	reinit_device(&gpus[id]);

	return message(MSG_GPUREI, id);
}

char *doquit(char *params)
{
	bye = 1;
	kill_work();
	return NULL;
}

struct CMDS {
	char *name;
	char *(*func)(char *);
} cmds[] = {
	{ "apiversion",	apiversion },
	{ "dev",	devstatus },
	{ "pool",	poolstatus },
	{ "summary",	summary },
	{ "gpuenable",	gpuenable },
	{ "gpudisable",	gpudisable },
	{ "gpurestart",	gpurestart },
	{ "quit",	doquit },
	{ NULL,		NULL }
};

void send_result(int c, char *result)
{
	int n;

	if (result == NULL)
		result = (char *)BLANK;

	// ignore failure - it's closed immediately anyway
	n = write(c, result, strlen(result)+1);
}

void api(void)
{
	char buf[BUFSIZ];
	const char *addr = "127.0.0.1";
	int c, sock, n, bound;
	char tmpaddr[32];
	char *binderror;
	time_t bindstart;
	short int port = opt_api_port;
	struct sockaddr_in serv;
	struct sockaddr_in cli;
	socklen_t clisiz;
	long long counter;
	char *result;
	char *params;
	int did;
	int i;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		applog(LOG_ERR, "API1 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
		return;
	}

	memset(&serv, 0, sizeof(serv));

	serv.sin_family = AF_INET;
	if (!opt_api_listen) {
		if (inet_pton(AF_INET, addr, &(serv.sin_addr)) == 0) {
			applog(LOG_ERR, "API2 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
			return;
		}
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
				applog(LOG_WARNING, "API bind to port %d failed - trying again in 15sec", port);
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

	sleep(opt_log_interval);

	if (opt_api_listen)
		applog(LOG_WARNING, "API running in UNRESTRICTED access mode");
	else
		applog(LOG_WARNING, "API running in restricted access mode");

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

		if (!opt_api_listen)
			inet_ntop(AF_INET, &(cli.sin_addr), &(tmpaddr[0]), sizeof(tmpaddr)-1);

		if (opt_api_listen || strcmp(tmpaddr, addr) == 0) {
			n = read(c, &buf[0], BUFSIZ-1);
			if (n >= 0) {
				did = false;
				buf[n] = '\0';
				params = strchr(buf, SEPARATOR);
				if (params != NULL)
					*(params++) = '\0';

				for (i = 0; cmds[i].name != NULL; i++) {
					if (strcmp(buf, cmds[i].name) == 0) {
						result = (cmds[i].func)(params);
						send_result(c, result);
						did = true;
						break;
					}
				}
				if (!did)
					send_result(c, message(MSG_INVCMD, 0));
			}
		}
		close(c);
	}

	close(sock);
	free(buffer);
}
