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
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define INVINETADDR -1
	#define CLOSESOCKET close

	#define SOCKERRMSG strerror(errno)
#endif

#ifdef WIN32
	#include <winsock2.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define INVINETADDR INADDR_NONE
	#define CLOSESOCKET closesocket

	static char WSAbuf[1024];

	struct WSAERRORS {
		int id;
		char *code;
	} WSAErrors[] = {
		{ 0,			"No error" },
		{ WSAEINTR,		"Interrupted system call" },
		{ WSAEBADF,		"Bad file number" },
		{ WSAEACCES,		"Permission denied" },
		{ WSAEFAULT,		"Bad address" },
		{ WSAEINVAL,		"Invalid argument" },
		{ WSAEMFILE,		"Too many open sockets" },
		{ WSAEWOULDBLOCK,	"Operation would block" },
		{ WSAEINPROGRESS,	"Operation now in progress" },
		{ WSAEALREADY,		"Operation already in progress" },
		{ WSAENOTSOCK,		"Socket operation on non-socket" },
		{ WSAEDESTADDRREQ,	"Destination address required" },
		{ WSAEMSGSIZE,		"Message too long" },
		{ WSAEPROTOTYPE,	"Protocol wrong type for socket" },
		{ WSAENOPROTOOPT,	"Bad protocol option" },
		{ WSAEPROTONOSUPPORT,	"Protocol not supported" },
		{ WSAESOCKTNOSUPPORT,	"Socket type not supported" },
		{ WSAEOPNOTSUPP,	"Operation not supported on socket" },
		{ WSAEPFNOSUPPORT,	"Protocol family not supported" },
		{ WSAEAFNOSUPPORT,	"Address family not supported" },
		{ WSAEADDRINUSE,	"Address already in use" },
		{ WSAEADDRNOTAVAIL,	"Can't assign requested address" },
		{ WSAENETDOWN,		"Network is down" },
		{ WSAENETUNREACH,	"Network is unreachable" },
		{ WSAENETRESET,		"Net connection reset" },
		{ WSAECONNABORTED,	"Software caused connection abort" },
		{ WSAECONNRESET,	"Connection reset by peer" },
		{ WSAENOBUFS,		"No buffer space available" },
		{ WSAEISCONN,		"Socket is already connected" },
		{ WSAENOTCONN,		"Socket is not connected" },
		{ WSAESHUTDOWN,		"Can't send after socket shutdown" },
		{ WSAETOOMANYREFS,	"Too many references, can't splice" },
		{ WSAETIMEDOUT,		"Connection timed out" },
		{ WSAECONNREFUSED,	"Connection refused" },
		{ WSAELOOP,		"Too many levels of symbolic links" },
		{ WSAENAMETOOLONG,	"File name too long" },
		{ WSAEHOSTDOWN,		"Host is down" },
		{ WSAEHOSTUNREACH,	"No route to host" },
		{ WSAENOTEMPTY,		"Directory not empty" },
		{ WSAEPROCLIM,		"Too many processes" },
		{ WSAEUSERS,		"Too many users" },
		{ WSAEDQUOT,		"Disc quota exceeded" },
		{ WSAESTALE,		"Stale NFS file handle" },
		{ WSAEREMOTE,		"Too many levels of remote in path" },
		{ WSASYSNOTREADY,	"Network system is unavailable" },
		{ WSAVERNOTSUPPORTED,	"Winsock version out of range" },
		{ WSANOTINITIALISED,	"WSAStartup not yet called" },
		{ WSAEDISCON,		"Graceful shutdown in progress" },
		{ WSAHOST_NOT_FOUND,	"Host not found" },
		{ WSANO_DATA,		"No host data of that type was found" },
		{ -1,			"Unknown error code" }
	};

	static char *WSAErrorMsg()
	{
		char *msg;
		int i;
		int id = WSAGetLastError();

		/* Assume none of them are actually -1 */
		for (i = 0; WSAErrors[i].id != -1; i++)
			if (WSAErrors[i].id == id)
				break;

		sprintf(WSAbuf, "Socket Error: (%d) %s", id, WSAErrors[i].code);

		return &(WSAbuf[0]);
	}

	#define SOCKERRMSG WSAErrorMsg()

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
#define MSG_VERSION 22

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
 { SEVERITY_SUCC,  MSG_VERSION,	PARAM_CPU,	"CGMiner versions" },
 { SEVERITY_FAIL }
};

static const char *APIVERSION = "0.5";
static const char *DEAD = "Dead";
static const char *SICK = "Sick";
static const char *NOSTART = "NoStart";
static const char *DISABLED = "Disabled";
static const char *ALIVE = "Alive";
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

			sprintf(msg_buffer, "STATUS=%c,Code=%d,Msg=", severity, messageid);

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

			ptr = msg_buffer + strlen(msg_buffer);

			sprintf(ptr, ",Description=%s%c",
				opt_api_description, SEPARATOR);

			return msg_buffer;
		}
	}

	sprintf(msg_buffer, "STATUS=F,Code=-1,Msg=%d,Description=%s%c",
		messageid, opt_api_description, SEPARATOR);

	return msg_buffer;
}

void apiversion(SOCKETTYPE c, char *params)
{
	sprintf(io_buffer, "%sVERSION,CGMiner=%s,API=%s%c",
		message(MSG_VERSION, 0),
		VERSION, APIVERSION, SEPARATOR);
}

void gpustatus(int gpu)
{
	char intensity[20];
	char buf[BUFSIZ];
	char *enabled;
	char *status;
	float gt, gv;
	int ga, gf, gp, gc, gm, pt;

	if (gpu >= 0 && gpu < nDevs) {
		struct cgpu_info *cgpu = &gpus[gpu];

		cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

#ifdef HAVE_ADL
		if (!gpu_stats(gpu, &gt, &gc, &gm, &gv, &ga, &gf, &gp, &pt))
#endif
			gt = gv = gm = gc = ga = gf = gp = pt = 0;

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

		sprintf(buf, "GPU=%d,Enabled=%s,Status=%s,Temperature=%.2f,Fan Speed=%d,Fan Percent=%d,GPU Clock=%d,Memory Clock=%d,GPU Voltage=%.3f,GPU Activity=%d,Powertune=%d,MHS=%.2f,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Intensity=%s%c",
			gpu, enabled, status, gt, gf, gp, gc, gm, gv, ga, pt,
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

		sprintf(buf, "CPU=%d,Status=%.2f,MHS=%.2f,Accepted=%d,Rejected=%d,Utility=%.2f%c",
			cpu, cgpu->rolling,
			cgpu->total_mhashes / total_secs,
			cgpu->accepted, cgpu->rejected,
			cgpu->utility, SEPARATOR);

		strcat(io_buffer, buf);
	}
}

void devstatus(SOCKETTYPE c, char *params)
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

void gpudev(SOCKETTYPE c, char *params)
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

void cpudev(SOCKETTYPE c, char *params)
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

void poolstatus(SOCKETTYPE c, char *params)
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

		sprintf(buf, "POOL=%d,URL=%s,Status=%s,Priority=%d,Long Poll=%s,Getworks=%d,Accepted=%d,Rejected=%d,Discarded=%d,Stale=%d,Get Failures=%d,Remote Failures=%d%c",
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

void summary(SOCKETTYPE c, char *params)
{
	double utility, mhs;

	char *algo = (char *)(algo_names[opt_algo]);
	if (algo == NULL)
		algo = "(null)";

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	mhs = total_mhashes_done / total_secs;

	sprintf(io_buffer, "%sSUMMARY,Elapsed=%.0f,Algorithm=%s,MHS=%.2f,Found Blocks=%d,Getworks=%d,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Discarded=%d,Stale=%d,Get Failures=%d,Local Work=%u,Remote Failures=%u,Network Blocks=%u%c",
		message(MSG_SUMM, 0),
		total_secs, algo, mhs, found_blocks,
		total_getworks, total_accepted, total_rejected,
		hw_errors, utility, total_discarded, total_stale,
		total_go, local_work, total_ro, new_blocks, SEPARATOR);
}

void gpuenable(SOCKETTYPE c, char *params)
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

void gpudisable(SOCKETTYPE c, char *params)
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

void gpurestart(SOCKETTYPE c, char *params)
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

void gpucount(SOCKETTYPE c, char *params)
{
	char buf[BUFSIZ];

	strcpy(io_buffer, message(MSG_NUMGPU, 0));

	sprintf(buf, "GPUS,Count=%d|", nDevs);

	strcat(io_buffer, buf);
}

void cpucount(SOCKETTYPE c, char *params)
{
	char buf[BUFSIZ];

	strcpy(io_buffer, message(MSG_NUMCPU, 0));

	sprintf(buf, "CPUS,Count=%d|", opt_n_threads > 0 ? num_processors : 0);

	strcat(io_buffer, buf);
}

void send_result(SOCKETTYPE c);

void doquit(SOCKETTYPE c, char *params)
{
	strcpy(io_buffer, "bye");
	send_result(c);
	*io_buffer = '\0';
	bye = 1;
	kill_work();
}

struct CMDS {
	char *name;
	void (*func)(SOCKETTYPE, char *);
} cmds[] = {
	{ "version",	apiversion },
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
	int len = strlen(io_buffer);

	if (opt_debug)
		applog(LOG_DEBUG, "DBG: send reply: (%d) '%.10s%s'", len+1, io_buffer, len > 10 ? "..." : "");

	// ignore failure - it's closed immediately anyway
	n = send(c, io_buffer, len+1, 0);

	if (opt_debug) {
		if (SOCKETFAIL(n))
			applog(LOG_DEBUG, "DBG: send failed: %s", SOCKERRMSG);
		else
			applog(LOG_DEBUG, "DBG: sent %d", n);
	}

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
	char *connectaddr;
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

	/* This should be done first to ensure curl has already called WSAStartup() in windows */
	sleep(opt_log_interval);

	if (!opt_api_listen) {
		applog(LOG_WARNING, "API not running%s", UNAVAILABLE);
		return;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVSOCK) {
		applog(LOG_ERR, "API1 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
		return;
	}

	memset(&serv, 0, sizeof(serv));

	serv.sin_family = AF_INET;

	if (!opt_api_network) {
		serv.sin_addr.s_addr = inet_addr(localaddr);
		if (serv.sin_addr.s_addr == INVINETADDR) {
			applog(LOG_ERR, "API2 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
			return;
		}
	}

	serv.sin_port = htons(port);

	// try for 1 minute ... in case the old one hasn't completely gone yet
	bound = 0;
	bindstart = time(NULL);
	while (bound == 0) {
		if (SOCKETFAIL(bind(sock, (struct sockaddr *)(&serv), sizeof(serv)))) {
			binderror = SOCKERRMSG;
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

	if (SOCKETFAIL(listen(sock, QUEUE))) {
		applog(LOG_ERR, "API3 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
		CLOSESOCKET(sock);
		return;
	}

	if (opt_api_network)
		applog(LOG_WARNING, "API running in UNRESTRICTED access mode");
	else
		applog(LOG_WARNING, "API running in restricted access mode");

	io_buffer = malloc(MYBUFSIZ+1);
	msg_buffer = malloc(MYBUFSIZ+1);

	while (bye == 0) {
		clisiz = sizeof(cli);
		if (SOCKETFAIL(c = accept(sock, (struct sockaddr *)(&cli), &clisiz))) {
			applog(LOG_ERR, "API failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
			goto die;
		}

		if (opt_api_network)
			addrok = true;
		else {
			connectaddr = inet_ntoa(cli.sin_addr);
			addrok = (strcmp(connectaddr, localaddr) == 0);
		}

		if (opt_debug) {
			connectaddr = inet_ntoa(cli.sin_addr);
			applog(LOG_DEBUG, "DBG: connection from %s - %s", connectaddr, addrok ? "Accepted" : "Ignored");
		}

		if (addrok) {
			n = recv(c, &buf[0], BUFSIZ-1, 0);
			if (SOCKETFAIL(n))
				buf[0] = '\0';
			else
				buf[n] = '\0';

			if (opt_debug) {
				if (SOCKETFAIL(n))
					applog(LOG_DEBUG, "DBG: recv failed: %s", SOCKERRMSG);
				else
					applog(LOG_DEBUG, "DBG: recv command: (%d) '%s'", n, buf);
			}

			if (!SOCKETFAIL(n)) {
				did = false;
				params = strchr(buf, SEPARATOR);
				if (params == NULL)
					params = (char *)BLANK;
				else
					*(params++) = '\0';

				for (i = 0; cmds[i].name != NULL; i++) {
					if (strcmp(buf, cmds[i].name) == 0) {
						(cmds[i].func)(c, params);
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
