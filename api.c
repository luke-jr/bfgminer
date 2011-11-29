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
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

	#define SOCKETTYPE int
	#define BINDERROR < 0
	#define LISTENERROR BINDERROR
	#define ACCEPTERROR BINDERROR
	#define INVSOCK -1
	#define CLOSESOCKET close
#endif

#ifdef WIN32
	#include <winsock2.h>
	#include "inet_ntop.h"
	#include "inet_pton.h"

	#define SOCKETTYPE SOCKET
	#define BINDERROR == SOCKET_ERROR
	#define LISTENERROR BINDERROR
	#define ACCEPTERROR BINDERROR
	#define INVSOCK INVALID_SOCKET
	#define CLOSESOCKET closesocket
	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif
#endif

// Big enough for largest API request
//  though a PC with 100s of CPUs may exceed the size ...
// Current code assumes it can socket send this size also
#define MYBUFSIZ	16384

// Number of requests to queue - normally would be small
#define QUEUE	10

static char *io_buffer = NULL;
static char *msg_buffer = NULL;
static SOCKETTYPE sock = INVSOCK;

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
#define MSG_MISID 15
#define MSG_CPUNON 16
#define MSG_GPUDEV 17
#define MSG_CPUDEV 18
#define MSG_INVCPU 19
#define MSG_NUMGPU 20
#define MSG_NUMCPU 21

enum code_severity {
	SEVERITY_ERR,
	SEVERITY_WARN,
	SEVERITY_INFO,
	SEVERITY_SUCC,
	SEVERITY_FAIL
};

enum code_parameters {
	PARAM_GPU,
	PARAM_CPU,
	PARAM_GPUMAX,
	PARAM_CPUMAX,
	PARAM_PMAX,
	PARAM_GCMAX,
	PARAM_NONE
};

struct CODES {
	const enum code_severity severity;
	const int code;
	const enum code_parameters params;
	const char *description;
} codes[] = {
 { SEVERITY_ERR,   MSG_INVGPU,	PARAM_GPUMAX,	"Invalid GPU id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_ALRENA,	PARAM_GPU,	"GPU %d already enabled" },
 { SEVERITY_INFO,  MSG_ALRDIS,	PARAM_GPU,	"GPU %d already disabled" },
 { SEVERITY_WARN,  MSG_GPUMRE,	PARAM_GPU,	"GPU %d must be restarted first" },
 { SEVERITY_INFO,  MSG_GPUREN,	PARAM_GPU,	"GPU %d sent enable message" },
 { SEVERITY_ERR,   MSG_GPUNON,	PARAM_NONE,	"No GPUs" },
 { SEVERITY_SUCC,  MSG_POOL,	PARAM_PMAX,	"%d Pool(s)" },
 { SEVERITY_ERR,   MSG_NOPOOL,	PARAM_NONE,	"No pools" },
 { SEVERITY_SUCC,  MSG_DEVS,	PARAM_GCMAX,	"%d GPU(s) - %d CPU(s)" },
 { SEVERITY_ERR,   MSG_NODEVS,	PARAM_NONE,	"No GPUs/CPUs" },
 { SEVERITY_SUCC,  MSG_SUMM,	PARAM_NONE,	"Summary" },
 { SEVERITY_INFO,  MSG_GPUDIS,	PARAM_GPU,	"GPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_GPUREI,	PARAM_GPU,	"GPU %d restart attempted" },
 { SEVERITY_ERR,   MSG_INVCMD,	PARAM_NONE,	"Invalid command" },
 { SEVERITY_ERR,   MSG_MISID,	PARAM_NONE,	"Missing device id parameter" },
 { SEVERITY_ERR,   MSG_CPUNON,	PARAM_NONE,	"No CPUs" },
 { SEVERITY_SUCC,  MSG_GPUDEV,	PARAM_GPU,	"GPU%d" },
 { SEVERITY_SUCC,  MSG_CPUDEV,	PARAM_CPU,	"CPU%d" },
 { SEVERITY_ERR,   MSG_INVCPU,	PARAM_CPUMAX,	"Invalid CPU id %d - range is 0 - %d" },
 { SEVERITY_SUCC,  MSG_NUMGPU,	PARAM_NONE,	"GPU count" },
 { SEVERITY_SUCC,  MSG_NUMCPU,	PARAM_NONE,	"CPU count" },
 { SEVERITY_FAIL }
};

static const char *APIVERSION = "0.3";
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
	int cpu;
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

			sprintf(msg_buffer, "STATUS=%c,CODE=%d,MSG=", severity, messageid);

			ptr = msg_buffer + strlen(msg_buffer);

			switch(codes[i].params) {
			case PARAM_GPU:
				sprintf(ptr, codes[i].description, gpuid);
				break;
			case PARAM_CPU:
				sprintf(ptr, codes[i].description, gpuid);
				break;
			case PARAM_GPUMAX:
				sprintf(ptr, codes[i].description, gpuid, nDevs - 1);
				break;
			case PARAM_PMAX:
				sprintf(ptr, codes[i].description, total_pools);
				break;
			case PARAM_GCMAX:
				if (opt_n_threads > 0)
					cpu = num_processors;
				else
					cpu = 0;

				sprintf(ptr, codes[i].description, nDevs, cpu);
				break;
			case PARAM_NONE:
			default:
				strcpy(ptr, codes[i].description);
			}

			strcat(msg_buffer, SEPARATORSTR);

			return msg_buffer;
		}
	}

	sprintf(msg_buffer, "STATUS=F,CODE=-1,MSG=%d%c", messageid, SEPARATOR);
	return msg_buffer;
}

void apiversion(char *params)
{
	strcpy(io_buffer, APIVERSION);
}

void gpustatus(int gpu)
{
	char intensity[20];
	char buf[BUFSIZ];
	char *enabled;
	char *status;
	float gt;
	int gf, gp;

	if (gpu >= 0 && gpu < nDevs) {
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

		strcat(io_buffer, buf);
	}
}

void cpustatus(int cpu)
{
	char buf[BUFSIZ];

	if (opt_n_threads > 0 && cpu >= 0 && cpu < num_processors) {
		struct cgpu_info *cgpu = &cpus[cpu];

		cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

		sprintf(buf, "CPU=%d,STA=%.2f,MHS=%.2f,A=%d,R=%d,U=%.2f%c",
			cpu, cgpu->rolling,
			cgpu->total_mhashes / total_secs,
			cgpu->accepted, cgpu->rejected,
			cgpu->utility, SEPARATOR);

		strcat(io_buffer, buf);
	}
}

void devstatus(char *params)
{
	int i;

	if (nDevs == 0 && opt_n_threads == 0) {
		strcpy(io_buffer, message(MSG_NODEVS, 0));
		return;
	}

	strcpy(io_buffer, message(MSG_DEVS, 0));

	for (i = 0; i < nDevs; i++)
		gpustatus(i);

	if (opt_n_threads > 0)
		for (i = 0; i < num_processors; i++)
			cpustatus(i);
}

void gpudev(char *params)
{
	int id;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0));
		return;
	}

	if (*params == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0));
		return;
	}

	id = atoi(params);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id));
		return;
	}

	strcpy(io_buffer, message(MSG_GPUDEV, id));

	gpustatus(id);
}

void cpudev(char *params)
{
	int id;

	if (opt_n_threads == 0) {
		strcpy(io_buffer, message(MSG_CPUNON, 0));
		return;
	}

	if (*params == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0));
		return;
	}

	id = atoi(params);
	if (id < 0 || id >= num_processors) {
		strcpy(io_buffer, message(MSG_INVCPU, id));
		return;
	}

	strcpy(io_buffer, message(MSG_CPUDEV, id));

	cpustatus(id);
}

void poolstatus(char *params)
{
	char buf[BUFSIZ];
	char *status, *lp;
	int i;

	if (total_pools == 0) {
		strcpy(io_buffer, message(MSG_NOPOOL, 0));
		return;
	}

	strcpy(io_buffer, message(MSG_POOL, 0));

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

		strcat(io_buffer, buf);
	}
}

void summary(char *params)
{
	double utility, mhs;

	char *algo = (char *)(algo_names[opt_algo]);
	if (algo == NULL)
		algo = "(null)";

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	mhs = total_mhashes_done / total_secs;

	sprintf(io_buffer, "%sSUMMARY=all,EL=%.0f,ALGO=%s,MHS=%.2f,SOL=%d,Q=%d,A=%d,R=%d,HW=%d,U=%.2f,DW=%d,ST=%d,GF=%d,LW=%u,RO=%u,BC=%u%c",
		message(MSG_SUMM, 0),
		total_secs, algo, mhs, found_blocks,
		total_getworks, total_accepted, total_rejected,
		hw_errors, utility, total_discarded, total_stale,
		total_go, local_work, total_ro, new_blocks, SEPARATOR);
}

void gpuenable(char *params)
{
	struct thr_info *thr;
	int gpu;
	int id;
	int i;

	if (gpu_threads == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0));
		return;
	}

	if (*params == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0));
		return;
	}

	id = atoi(params);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id));
		return;
	}

	if (gpu_devices[id]) {
		strcpy(io_buffer, message(MSG_ALRENA, id));
		return;
	}

	for (i = 0; i < gpu_threads; i++) {
		gpu = thr_info[i].cgpu->cpu_gpu;
		if (gpu == id) {
			thr = &thr_info[i];
			if (thr->cgpu->status != LIFE_WELL) {
				strcpy(io_buffer, message(MSG_GPUMRE, id));
				return;
			}

			gpu_devices[id] = true;
			tq_push(thr->q, &ping);

		}
	}

	strcpy(io_buffer, message(MSG_GPUREN, id));
}

void gpudisable(char *params)
{
	int id;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0));
		return;
	}

	if (*params == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0));
		return;
	}

	id = atoi(params);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id));
		return;
	}

	if (!gpu_devices[id]) {
		strcpy(io_buffer, message(MSG_ALRDIS, id));
		return;
	}

	gpu_devices[id] = false;

	strcpy(io_buffer, message(MSG_GPUDIS, id));
}

void gpurestart(char *params)
{
	int id;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0));
		return;
	}

	if (*params == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0));
		return;
	}

	id = atoi(params);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id));
		return;
	}

	reinit_device(&gpus[id]);

	strcpy(io_buffer, message(MSG_GPUREI, id));
}

void gpucount(char *params)
{
	char buf[BUFSIZ];

	strcpy(io_buffer, message(MSG_NUMGPU, 0));

	sprintf(buf, "GPUS,COUNT=%d|", nDevs);

	strcat(io_buffer, buf);
}

void cpucount(char *params)
{
	char buf[BUFSIZ];

	strcpy(io_buffer, message(MSG_NUMCPU, 0));

	sprintf(buf, "CPUS,COUNT=%d|", opt_n_threads > 0 ? num_processors : 0);

	strcat(io_buffer, buf);
}

void doquit(char *params)
{
	*io_buffer = '\0';
	bye = 1;
	kill_work();
}

struct CMDS {
	char *name;
	void (*func)(char *);
} cmds[] = {
	{ "apiversion",	apiversion },
	{ "devs",	devstatus },
	{ "pools",	poolstatus },
	{ "summary",	summary },
	{ "gpuenable",	gpuenable },
	{ "gpudisable",	gpudisable },
	{ "gpurestart",	gpurestart },
	{ "gpu",	gpudev },
	{ "cpu",	cpudev },
	{ "gpucount",	gpucount },
	{ "cpucount",	cpucount },
	{ "quit",	doquit },
	{ NULL }
};

void send_result(SOCKETTYPE c)
{
	int n;

	// ignore failure - it's closed immediately anyway
	n = send(c, io_buffer, strlen(io_buffer)+1, 0);
}

void tidyup()
{
	bye = 1;

	if (sock != INVSOCK) {
		shutdown(sock, SHUT_RDWR);
		CLOSESOCKET(sock);
		sock = INVSOCK;
	}

	if (msg_buffer != NULL) {
		free(msg_buffer);
		msg_buffer = NULL;
	}

	if (io_buffer != NULL) {
		free(io_buffer);
		io_buffer = NULL;
	}
}

void api(void)
{
	char buf[BUFSIZ];
	const char *localaddr = "127.0.0.1";
	SOCKETTYPE c;
	int n, bound;
	char connectaddr[32];
	char *binderror;
	time_t bindstart;
	short int port = opt_api_port;
	struct sockaddr_in serv;
	struct sockaddr_in cli;
	socklen_t clisiz;
	char *params;
	bool addrok;
	bool did;
	int i;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVSOCK) {
		applog(LOG_ERR, "API1 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
		return;
	}

	memset(&serv, 0, sizeof(serv));

	serv.sin_family = AF_INET;

	if (!opt_api_listen) {
		if (inet_pton(AF_INET, localaddr, &(serv.sin_addr)) == 0) {
			applog(LOG_ERR, "API2 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
			return;
		}
	}

	serv.sin_port = htons(port);

	// try for 1 minute ... in case the old one hasn't completely gone yet
	bound = 0;
	bindstart = time(NULL);
	while (bound == 0) {
		if (bind(sock, (struct sockaddr *)(&serv), sizeof(serv)) BINDERROR) {
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

	if (listen(sock, QUEUE) LISTENERROR) {
		applog(LOG_ERR, "API3 initialisation failed (%s)%s", strerror(errno), UNAVAILABLE);
		CLOSESOCKET(sock);
		return;
	}

	sleep(opt_log_interval);

	if (opt_api_listen)
		applog(LOG_WARNING, "API running in UNRESTRICTED access mode");
	else
		applog(LOG_WARNING, "API running in restricted access mode");

	io_buffer = malloc(MYBUFSIZ+1);
	msg_buffer = malloc(MYBUFSIZ+1);

	while (bye == 0) {
		clisiz = sizeof(cli);
		if ((c = accept(sock, (struct sockaddr *)(&cli), &clisiz)) ACCEPTERROR) {
			applog(LOG_ERR, "API failed (%s)%s", strerror(errno), UNAVAILABLE);
			goto die;
		}

		if (opt_api_listen)
			addrok = true;
		else {
			inet_ntop(AF_INET, &(cli.sin_addr), &(connectaddr[0]), sizeof(connectaddr)-1);
			addrok = (strcmp(connectaddr, localaddr) == 0);
		}

		if (addrok) {
			n = recv(c, &buf[0], BUFSIZ-1, 0);
			if (n >= 0) {
				did = false;
				buf[n] = '\0';
				params = strchr(buf, SEPARATOR);
				if (params == NULL)
					params = (char *)BLANK;
				else
					*(params++) = '\0';

				for (i = 0; cmds[i].name != NULL; i++) {
					if (strcmp(buf, cmds[i].name) == 0) {
						(cmds[i].func)(params);
						send_result(c);
						did = true;
						break;
					}
				}
				if (!did) {
					strcpy(io_buffer, message(MSG_INVCMD, 0));
					send_result(c);
				}
			}
		}
		CLOSESOCKET(c);
	}
die:
	tidyup();
}
