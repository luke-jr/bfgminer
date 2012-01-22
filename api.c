/*
 * Copyright 2011-2012 Andrew Smith
 * Copyright 2011-2012 Con Kolivas
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
#define MYBUFSIZ	32768

// Number of requests to queue - normally would be small
#define QUEUE	10

static char *io_buffer = NULL;
static char *msg_buffer = NULL;
static SOCKETTYPE sock = INVSOCK;

static const char *UNAVAILABLE = " - API will not be available";

//static const char *BLANK = "";
static const char *COMMA = ",";
static const char SEPARATOR = '|';
static const char GPUSEP = ',';

static const char *APIVERSION = "1.0";
static const char *DEAD = "Dead";
static const char *SICK = "Sick";
static const char *NOSTART = "NoStart";
static const char *DISABLED = "Disabled";
static const char *ALIVE = "Alive";
#define _DYNAMIC "D"
static const char *DYNAMIC = _DYNAMIC;

static const char *YES = "Y";
static const char *NO = "N";

#define _DEVS		"DEVS"
#define _POOLS		"POOLS"
#define _SUMMARY	"SUMMARY"
#define _STATUS		"STATUS"
#define _VERSION	"VERSION"
#define _MINECON	"CONFIG"

#ifdef WANT_CPUMINE
#define _CPU		"CPU"
#endif

#define _GPU		"GPU"
#define _CPUS		"CPUS"
#define _GPUS		"GPUS"
#define _BYE		"BYE"

static const char ISJSON = '{';
#define JSON0		"{"
#define JSON1		"\""
#define JSON2		"\":["
#define JSON3		"]"
#define JSON4		",\"id\":1}"

#define JSON_START	JSON0
#define JSON_DEVS	JSON1 _DEVS JSON2
#define JSON_POOLS	JSON1 _POOLS JSON2
#define JSON_SUMMARY	JSON1 _SUMMARY JSON2
#define JSON_STATUS	JSON1 _STATUS JSON2
#define JSON_VERSION	JSON1 _VERSION JSON2
#define JSON_MINECON	JSON1 _MINECON JSON2
#define JSON_GPU	JSON1 _GPU JSON2

#ifdef WANT_CPUMINE
#define JSON_CPU	JSON1 _CPU JSON2
#endif

#define JSON_GPUS	JSON1 _GPUS JSON2
#define JSON_CPUS	JSON1 _CPUS JSON2
#define JSON_BYE	JSON1 _BYE JSON1
#define JSON_CLOSE	JSON3
#define JSON_END	JSON4

static const char *JSON_COMMAND = "command";
static const char *JSON_PARAMETER = "parameter";

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
#define MSG_GPUDEV 17

#ifdef WANT_CPUMINE
#define MSG_CPUNON 16
#define MSG_CPUDEV 18
#define MSG_INVCPU 19
#endif

#define MSG_NUMGPU 20
#define MSG_NUMCPU 21
#define MSG_VERSION 22
#define MSG_INVJSON 23
#define MSG_MISCMD 24
#define MSG_MISPID 25
#define MSG_INVPID 26
#define MSG_SWITCHP 27
#define MSG_MISVAL 28
#define MSG_NOADL 29
#define MSG_NOGPUADL 30
#define MSG_INVINT 31
#define MSG_GPUINT 32
#define MSG_MINECON 33
#define MSG_GPUMERR 34
#define MSG_GPUMEM 35
#define MSG_GPUEERR 36
#define MSG_GPUENG 37
#define MSG_GPUVERR 38
#define MSG_GPUVDDC 39
#define MSG_GPUFERR 40
#define MSG_GPUFAN 41
#define MSG_MISFN 42
#define MSG_BADFN 43
#define MSG_SAVED 44

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
	PARAM_POOLMAX,
#ifdef WANT_CPUMINE
	PARAM_GCMAX,
#else
	PARAM_GMAX,
#endif
	PARAM_CMD,
	PARAM_POOL,
	PARAM_STR,
	PARAM_BOTH,
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
#ifdef WANT_CPUMINE
 { SEVERITY_SUCC,  MSG_DEVS,	PARAM_GCMAX,	"%d GPU(s) - %d CPU(s)" },
 { SEVERITY_ERR,   MSG_NODEVS,	PARAM_NONE,	"No GPUs/CPUs" },
#else
 { SEVERITY_SUCC,  MSG_DEVS,	PARAM_GMAX,	"%d GPU(s)" },
 { SEVERITY_ERR,   MSG_NODEVS,	PARAM_NONE,	"No GPUs" },
#endif
 { SEVERITY_SUCC,  MSG_SUMM,	PARAM_NONE,	"Summary" },
 { SEVERITY_INFO,  MSG_GPUDIS,	PARAM_GPU,	"GPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_GPUREI,	PARAM_GPU,	"GPU %d restart attempted" },
 { SEVERITY_ERR,   MSG_INVCMD,	PARAM_NONE,	"Invalid command" },
 { SEVERITY_ERR,   MSG_MISID,	PARAM_NONE,	"Missing device id parameter" },
 { SEVERITY_SUCC,  MSG_GPUDEV,	PARAM_GPU,	"GPU%d" },
#ifdef WANT_CPUMINE
 { SEVERITY_ERR,   MSG_CPUNON,	PARAM_NONE,	"No CPUs" },
 { SEVERITY_SUCC,  MSG_CPUDEV,	PARAM_CPU,	"CPU%d" },
 { SEVERITY_ERR,   MSG_INVCPU,	PARAM_CPUMAX,	"Invalid CPU id %d - range is 0 - %d" },
#endif
 { SEVERITY_SUCC,  MSG_NUMGPU,	PARAM_NONE,	"GPU count" },
 { SEVERITY_SUCC,  MSG_NUMCPU,	PARAM_NONE,	"CPU count" },
 { SEVERITY_SUCC,  MSG_VERSION,	PARAM_NONE,	"CGMiner versions" },
 { SEVERITY_ERR,   MSG_INVJSON,	PARAM_NONE,	"Invalid JSON" },
 { SEVERITY_ERR,   MSG_MISCMD,	PARAM_CMD,	"Missing JSON '%s'" },
 { SEVERITY_ERR,   MSG_MISPID,	PARAM_NONE,	"Missing pool id parameter" },
 { SEVERITY_ERR,   MSG_INVPID,	PARAM_POOLMAX,	"Invalid pool id %d - range is 0 - %d" },
 { SEVERITY_SUCC,  MSG_SWITCHP,	PARAM_POOL,	"Switching to pool %d:'%s'" },
 { SEVERITY_ERR,   MSG_MISVAL,	PARAM_NONE,	"Missing comma after GPU number" },
 { SEVERITY_ERR,   MSG_NOADL,	PARAM_NONE,	"ADL is not available" },
 { SEVERITY_ERR,   MSG_NOGPUADL,PARAM_GPU,	"GPU %d does not have ADL" },
 { SEVERITY_ERR,   MSG_INVINT,	PARAM_STR,	"Invalid intensity (%s) - must be '" _DYNAMIC  "' or range -10 - 10" },
 { SEVERITY_INFO,  MSG_GPUINT,	PARAM_BOTH,	"GPU %d set new intensity to %s" },
 { SEVERITY_SUCC,  MSG_MINECON, PARAM_NONE,	"CGMiner config" },
 { SEVERITY_ERR,   MSG_GPUMERR,	PARAM_BOTH,	"Setting GPU %d memoryclock to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUMEM,	PARAM_BOTH,	"Setting GPU %d memoryclock to (%s) reported succeess" },
 { SEVERITY_ERR,   MSG_GPUEERR,	PARAM_BOTH,	"Setting GPU %d clock to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUENG,	PARAM_BOTH,	"Setting GPU %d clock to (%s) reported succeess" },
 { SEVERITY_ERR,   MSG_GPUVERR,	PARAM_BOTH,	"Setting GPU %d vddc to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUVDDC,	PARAM_BOTH,	"Setting GPU %d vddc to (%s) reported succeess" },
 { SEVERITY_ERR,   MSG_GPUFERR,	PARAM_BOTH,	"Setting GPU %d fan to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUFAN,	PARAM_BOTH,	"Setting GPU %d fan to (%s) reported succeess" },
 { SEVERITY_ERR,   MSG_MISFN,	PARAM_NONE,	"Missing save filename parameter" },
 { SEVERITY_ERR,   MSG_BADFN,	PARAM_STR,	"Can't open or create save file '%s'" },
 { SEVERITY_ERR,   MSG_SAVED,	PARAM_STR,	"Configuration saved to file '%s'" },
 { SEVERITY_FAIL }
};

static int bye = 0;
static bool ping = true;

// All replies (except BYE) start with a message
//  thus for JSON, message() inserts JSON_START at the front
//  and send_result() adds JSON_END at the end
static char *message(int messageid, int paramid, char *param2, bool isjson)
{
	char severity;
	char *ptr;
#ifdef WANT_CPUMINE
	int cpu;
#endif
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

			if (isjson)
				sprintf(msg_buffer, JSON_START JSON_STATUS "{\"" _STATUS "\":\"%c\",\"Code\":%d,\"Msg\":\"", severity, messageid);
			else
				sprintf(msg_buffer, _STATUS "=%c,Code=%d,Msg=", severity, messageid);

			ptr = msg_buffer + strlen(msg_buffer);

			switch(codes[i].params) {
			case PARAM_GPU:
			case PARAM_CPU:
				sprintf(ptr, codes[i].description, paramid);
				break;
			case PARAM_POOL:
				sprintf(ptr, codes[i].description, paramid, pools[paramid]->rpc_url);
				break;
			case PARAM_GPUMAX:
				sprintf(ptr, codes[i].description, paramid, nDevs - 1);
				break;
			case PARAM_PMAX:
				sprintf(ptr, codes[i].description, total_pools);
				break;
			case PARAM_POOLMAX:
				sprintf(ptr, codes[i].description, paramid, total_pools - 1);
				break;
#ifdef WANT_CPUMINE
			case PARAM_GCMAX:
				if (opt_n_threads > 0)
					cpu = num_processors;
				else
					cpu = 0;

				sprintf(ptr, codes[i].description, nDevs, cpu);
				break;
#else
			case PARAM_GMAX:
				sprintf(ptr, codes[i].description, nDevs);
				break;
#endif
			case PARAM_CMD:
				sprintf(ptr, codes[i].description, JSON_COMMAND);
				break;
			case PARAM_STR:
				sprintf(ptr, codes[i].description, param2);
				break;
			case PARAM_BOTH:
				sprintf(ptr, codes[i].description, paramid, param2);
				break;
			case PARAM_NONE:
			default:
				strcpy(ptr, codes[i].description);
			}

			ptr = msg_buffer + strlen(msg_buffer);

			if (isjson)
				sprintf(ptr, "\",\"Description\":\"%s\"}" JSON_CLOSE, opt_api_description);
			else
				sprintf(ptr, ",Description=%s%c", opt_api_description, SEPARATOR);

			return msg_buffer;
		}
	}

	if (isjson)
		sprintf(msg_buffer, JSON_START JSON_STATUS "{\"" _STATUS "\":\"F\",\"Code\":-1,\"Msg\":\"%d\",\"Description\":\"%s\"}" JSON_CLOSE,
			messageid, opt_api_description);
	else
		sprintf(msg_buffer, _STATUS "=F,Code=-1,Msg=%d,Description=%s%c",
			messageid, opt_api_description, SEPARATOR);

	return msg_buffer;
}

static void apiversion(SOCKETTYPE c, char *param, bool isjson)
{
	if (isjson)
		sprintf(io_buffer, "%s," JSON_VERSION "{\"CGMiner\":\"%s\",\"API\":\"%s\"}" JSON_CLOSE,
			message(MSG_VERSION, 0, NULL, isjson),
			VERSION, APIVERSION);
	else
		sprintf(io_buffer, "%s" _VERSION ",CGMiner=%s,API=%s%c",
			message(MSG_VERSION, 0, NULL, isjson),
			VERSION, APIVERSION, SEPARATOR);
}

static void minerconfig(SOCKETTYPE c, char *param, bool isjson)
{
	char buf[BUFSIZ];
	int cpucount = 0;
	char *adlinuse = (char *)NO;
#ifdef HAVE_ADL
	const char *adl = YES;
	int i;

	for (i = 0; i < nDevs; i++) {
		if (gpus[i].has_adl) {
			adlinuse = (char *)YES;
			break;
		}
	}
#else
	const char *adl = NO;
#endif

#ifdef WANT_CPUMINE
	cpucount = opt_n_threads > 0 ? num_processors : 0;
#endif

	strcpy(io_buffer, message(MSG_MINECON, 0, NULL, isjson));

	if (isjson)
		sprintf(buf, "," JSON_MINECON "{\"GPU Count\":%d,\"CPU Count\":%d,\"Pool Count\":%d,\"ADL\":\"%s\",\"ADL in use\":\"%s\",\"Strategy\":\"%s\"}" JSON_CLOSE, nDevs, cpucount, total_pools, adl, adlinuse, strategies[pool_strategy].s);
	else
		sprintf(buf, _MINECON ",GPU Count=%d,CPU Count=%d,Pool Count=%d,ADL=%s,ADL in use=%s,Strategy=%s%c", nDevs, cpucount, total_pools, adl, adlinuse, strategies[pool_strategy].s, SEPARATOR);

	strcat(io_buffer, buf);
}

static void gpustatus(int gpu, bool isjson)
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

		if (cgpu->enabled)
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

		if (isjson)
			sprintf(buf, "{\"GPU\":%d,\"Enabled\":\"%s\",\"Status\":\"%s\",\"Temperature\":%.2f,\"Fan Speed\":%d,\"Fan Percent\":%d,\"GPU Clock\":%d,\"Memory Clock\":%d,\"GPU Voltage\":%.3f,\"GPU Activity\":%d,\"Powertune\":%d,\"MHS av\":%.2f,\"MHS %ds\":%.2f,\"Accepted\":%d,\"Rejected\":%d,\"Hardware Errors\":%d,\"Utility\":%.2f,\"Intensity\":\"%s\"}",
				gpu, enabled, status, gt, gf, gp, gc, gm, gv, ga, pt,
				cgpu->total_mhashes / total_secs, opt_log_interval, cgpu->rolling,
				cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
				cgpu->utility, intensity);
		else
			sprintf(buf, "GPU=%d,Enabled=%s,Status=%s,Temperature=%.2f,Fan Speed=%d,Fan Percent=%d,GPU Clock=%d,Memory Clock=%d,GPU Voltage=%.3f,GPU Activity=%d,Powertune=%d,MHS av=%.2f,MHS %ds=%.2f,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Intensity=%s%c",
				gpu, enabled, status, gt, gf, gp, gc, gm, gv, ga, pt,
				cgpu->total_mhashes / total_secs, opt_log_interval, cgpu->rolling,
				cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
				cgpu->utility, intensity, SEPARATOR);

		strcat(io_buffer, buf);
	}
}

#ifdef WANT_CPUMINE
static void cpustatus(int cpu, bool isjson)
{
	char buf[BUFSIZ];

	if (opt_n_threads > 0 && cpu >= 0 && cpu < num_processors) {
		struct cgpu_info *cgpu = &cpus[cpu];

		cgpu->utility = cgpu->accepted / ( total_secs ? total_secs : 1 ) * 60;

		if (isjson)
			sprintf(buf, "{\"CPU\":%d,\"MHS av\":%.2f,\"MHS %ds\":%.2f,\"Accepted\":%d,\"Rejected\":%d,\"Utility\":%.2f}",
				cpu, cgpu->total_mhashes / total_secs,
				opt_log_interval, cgpu->rolling,
				cgpu->accepted, cgpu->rejected,
				cgpu->utility);
		else
			sprintf(buf, "CPU=%d,MHS av=%.2f,MHS %ds=%.2f,Accepted=%d,Rejected=%d,Utility=%.2f%c",
				cpu, cgpu->total_mhashes / total_secs,
				opt_log_interval, cgpu->rolling,
				cgpu->accepted, cgpu->rejected,
				cgpu->utility, SEPARATOR);

		strcat(io_buffer, buf);
	}
}
#endif

static void devstatus(SOCKETTYPE c, char *param, bool isjson)
{
	int i;

	if (nDevs == 0 && opt_n_threads == 0) {
		strcpy(io_buffer, message(MSG_NODEVS, 0, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_DEVS, 0, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_DEVS);
	}

	for (i = 0; i < nDevs; i++) {
		if (isjson && i > 0)
			strcat(io_buffer, COMMA);

		gpustatus(i, isjson);
	}

#ifdef WANT_CPUMINE
	if (opt_n_threads > 0)
		for (i = 0; i < num_processors; i++) {
			if (isjson && (i > 0 || nDevs > 0))
				strcat(io_buffer, COMMA);

			cpustatus(i, isjson);
		}
#endif

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

static void gpudev(SOCKETTYPE c, char *param, bool isjson)
{
	int id;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_GPUDEV, id, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_GPU);
	}

	gpustatus(id, isjson);

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

#ifdef WANT_CPUMINE
static void cpudev(SOCKETTYPE c, char *param, bool isjson)
{
	int id;

	if (opt_n_threads == 0) {
		strcpy(io_buffer, message(MSG_CPUNON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= num_processors) {
		strcpy(io_buffer, message(MSG_INVCPU, id, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_CPUDEV, id, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_CPU);
	}

	cpustatus(id, isjson);

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}
#endif

static void poolstatus(SOCKETTYPE c, char *param, bool isjson)
{
	char buf[BUFSIZ];
	char *status, *lp;
	int i;

	if (total_pools == 0) {
		strcpy(io_buffer, message(MSG_NOPOOL, 0, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_POOL, 0, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_POOLS);
	}

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (!pool->enabled)
			status = (char *)DISABLED;
		else {
			if (pool->idle)
				status = (char *)DEAD;
			else
				status = (char *)ALIVE;
		}

		if (pool->hdr_path)
			lp = (char *)YES;
		else
			lp = (char *)NO;

		if (isjson)
			sprintf(buf, "%s{\"POOL\":%d,\"URL\":\"%s\",\"Status\":\"%s\",\"Priority\":%d,\"Long Poll\":\"%s\",\"Getworks\":%d,\"Accepted\":%d,\"Rejected\":%d,\"Discarded\":%d,\"Stale\":%d,\"Get Failures\":%d,\"Remote Failures\":%d}",
				(i > 0) ? COMMA : "",
				i, pool->rpc_url, status, pool->prio, lp,
				pool->getwork_requested,
				pool->accepted, pool->rejected,
				pool->discarded_work,
				pool->stale_shares,
				pool->getfail_occasions,
				pool->remotefail_occasions);
		else
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

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

static void summary(SOCKETTYPE c, char *param, bool isjson)
{
	double utility, mhs;

#ifdef WANT_CPUMINE
	char *algo = (char *)(algo_names[opt_algo]);
	if (algo == NULL)
		algo = "(null)";
#endif

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	mhs = total_mhashes_done / total_secs;

#ifdef WANT_CPUMINE
	if (isjson)
		sprintf(io_buffer, "%s," JSON_SUMMARY "{\"Elapsed\":%.0f,\"Algorithm\":\"%s\",\"MHS av\":%.2f,\"Found Blocks\":%d,\"Getworks\":%d,\"Accepted\":%d,\"Rejected\":%d,\"Hardware Errors\":%d,\"Utility\":%.2f,\"Discarded\":%d,\"Stale\":%d,\"Get Failures\":%d,\"Local Work\":%u,\"Remote Failures\":%u,\"Network Blocks\":%u}" JSON_CLOSE,
			message(MSG_SUMM, 0, NULL, isjson),
			total_secs, algo, mhs, found_blocks,
			total_getworks, total_accepted, total_rejected,
			hw_errors, utility, total_discarded, total_stale,
			total_go, local_work, total_ro, new_blocks);
	else
		sprintf(io_buffer, "%s" _SUMMARY ",Elapsed=%.0f,Algorithm=%s,MHS av=%.2f,Found Blocks=%d,Getworks=%d,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Discarded=%d,Stale=%d,Get Failures=%d,Local Work=%u,Remote Failures=%u,Network Blocks=%u%c",
			message(MSG_SUMM, 0, NULL, isjson),
			total_secs, algo, mhs, found_blocks,
			total_getworks, total_accepted, total_rejected,
			hw_errors, utility, total_discarded, total_stale,
			total_go, local_work, total_ro, new_blocks, SEPARATOR);
#else
	if (isjson)
		sprintf(io_buffer, "%s," JSON_SUMMARY "{\"Elapsed\":%.0f,\"MHS av\":%.2f,\"Found Blocks\":%d,\"Getworks\":%d,\"Accepted\":%d,\"Rejected\":%d,\"Hardware Errors\":%d,\"Utility\":%.2f,\"Discarded\":%d,\"Stale\":%d,\"Get Failures\":%d,\"Local Work\":%u,\"Remote Failures\":%u,\"Network Blocks\":%u}" JSON_CLOSE,
			message(MSG_SUMM, 0, NULL, isjson),
			total_secs, mhs, found_blocks,
			total_getworks, total_accepted, total_rejected,
			hw_errors, utility, total_discarded, total_stale,
			total_go, local_work, total_ro, new_blocks);
	else
		sprintf(io_buffer, "%s" _SUMMARY ",Elapsed=%.0f,MHS av=%.2f,Found Blocks=%d,Getworks=%d,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Discarded=%d,Stale=%d,Get Failures=%d,Local Work=%u,Remote Failures=%u,Network Blocks=%u%c",
			message(MSG_SUMM, 0, NULL, isjson),
			total_secs, mhs, found_blocks,
			total_getworks, total_accepted, total_rejected,
			hw_errors, utility, total_discarded, total_stale,
			total_go, local_work, total_ro, new_blocks, SEPARATOR);
#endif
}

static void gpuenable(SOCKETTYPE c, char *param, bool isjson)
{
	struct thr_info *thr;
	int gpu;
	int id;
	int i;

	if (gpu_threads == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id, NULL, isjson));
		return;
	}

	if (gpus[id].enabled) {
		strcpy(io_buffer, message(MSG_ALRENA, id, NULL, isjson));
		return;
	}

	for (i = 0; i < gpu_threads; i++) {
		gpu = thr_info[i].cgpu->device_id;
		if (gpu == id) {
			thr = &thr_info[i];
			if (thr->cgpu->status != LIFE_WELL) {
				strcpy(io_buffer, message(MSG_GPUMRE, id, NULL, isjson));
				return;
			}

			gpus[id].enabled = true;
			tq_push(thr->q, &ping);

		}
	}

	strcpy(io_buffer, message(MSG_GPUREN, id, NULL, isjson));
}

static void gpudisable(SOCKETTYPE c, char *param, bool isjson)
{
	int id;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id, NULL, isjson));
		return;
	}

	if (!gpus[id].enabled) {
		strcpy(io_buffer, message(MSG_ALRDIS, id, NULL, isjson));
		return;
	}

	gpus[id].enabled = false;

	strcpy(io_buffer, message(MSG_GPUDIS, id, NULL, isjson));
}

static void gpurestart(SOCKETTYPE c, char *param, bool isjson)
{
	int id;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id, NULL, isjson));
		return;
	}

	reinit_device(&gpus[id]);

	strcpy(io_buffer, message(MSG_GPUREI, id, NULL, isjson));
}

static void gpucount(SOCKETTYPE c, char *param, bool isjson)
{
	char buf[BUFSIZ];

	strcpy(io_buffer, message(MSG_NUMGPU, 0, NULL, isjson));

	if (isjson)
		sprintf(buf, "," JSON_GPUS "{\"Count\":%d}" JSON_CLOSE, nDevs);
	else
		sprintf(buf, _GPUS ",Count=%d%c", nDevs, SEPARATOR);

	strcat(io_buffer, buf);
}

static void cpucount(SOCKETTYPE c, char *param, bool isjson)
{
	char buf[BUFSIZ];
	int count = 0;

#ifdef WANT_CPUMINE
	count = opt_n_threads > 0 ? num_processors : 0;
#endif

	strcpy(io_buffer, message(MSG_NUMCPU, 0, NULL, isjson));

	if (isjson)
		sprintf(buf, "," JSON_CPUS "{\"Count\":%d}" JSON_CLOSE, count);
	else
		sprintf(buf, _CPUS ",Count=%d%c", count, SEPARATOR);

	strcat(io_buffer, buf);
}

static void switchpool(SOCKETTYPE c, char *param, bool isjson)
{
	struct pool *pool;
	int id;

	if (total_pools == 0) {
		strcpy(io_buffer, message(MSG_NOPOOL, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISPID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= total_pools) {
		strcpy(io_buffer, message(MSG_INVPID, id, NULL, isjson));
		return;
	}

	pool = pools[id];
	pool->enabled = true;
	switch_pools(pool);

	strcpy(io_buffer, message(MSG_SWITCHP, id, NULL, isjson));
}

static bool splitgpuvalue(char *param, int *gpu, char **value, bool isjson)
{
	int id;
	char *gpusep;

	if (nDevs == 0) {
		strcpy(io_buffer, message(MSG_GPUNON, 0, NULL, isjson));
		return false;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return false;
	}

	gpusep = strchr(param, GPUSEP);
	if (gpusep == NULL) {
		strcpy(io_buffer, message(MSG_MISVAL, 0, NULL, isjson));
		return false;
	}

	*(gpusep++) = '\0';

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		strcpy(io_buffer, message(MSG_INVGPU, id, NULL, isjson));
		return false;
	}

	*gpu = id;
	*value = gpusep;

	return true;
}

static void gpuintensity(SOCKETTYPE c, char *param, bool isjson)
{
	int id;
	char *value;
	int intensity;
	char intensitystr[7];

	if (!splitgpuvalue(param, &id, &value, isjson))
		return;

	if (!strncasecmp(value, DYNAMIC, 1)) {
		gpus[id].dynamic = true;
		strcpy(intensitystr, DYNAMIC);
	}
	else {
		intensity = atoi(value);
		if (intensity < -10 || intensity > 10) {
			strcpy(io_buffer, message(MSG_INVINT, 0, value, isjson));
			return;
		}

		gpus[id].dynamic = false;
		gpus[id].intensity = intensity;
		sprintf(intensitystr, "%d", intensity);
	}

	strcpy(io_buffer, message(MSG_GPUINT, id, intensitystr, isjson));
}

static void gpumem(SOCKETTYPE c, char *param, bool isjson)
{
	int id;
	char *value;
	int clock;

	if (!splitgpuvalue(param, &id, &value, isjson))
		return;

	clock = atoi(value);

	if (set_memoryclock(id, clock))
		strcpy(io_buffer, message(MSG_GPUMERR, id, value, isjson));
	else
		strcpy(io_buffer, message(MSG_GPUMEM, id, value, isjson));
}

static void gpuengine(SOCKETTYPE c, char *param, bool isjson)
{
	int id;
	char *value;
	int clock;

	if (!splitgpuvalue(param, &id, &value, isjson))
		return;

	clock = atoi(value);

	if (set_engineclock(id, clock))
		strcpy(io_buffer, message(MSG_GPUEERR, id, value, isjson));
	else
		strcpy(io_buffer, message(MSG_GPUENG, id, value, isjson));
}

static void gpufan(SOCKETTYPE c, char *param, bool isjson)
{
	int id;
	char *value;
	int fan;

	if (!splitgpuvalue(param, &id, &value, isjson))
		return;

	fan = atoi(value);

	if (set_fanspeed(id, fan))
		strcpy(io_buffer, message(MSG_GPUFERR, id, value, isjson));
	else
		strcpy(io_buffer, message(MSG_GPUFAN, id, value, isjson));
}

static void gpuvddc(SOCKETTYPE c, char *param, bool isjson)
{
	int id;
	char *value;
	float vddc;

	if (!splitgpuvalue(param, &id, &value, isjson))
		return;

	vddc = atof(value);

	if (set_vddc(id, vddc))
		strcpy(io_buffer, message(MSG_GPUVERR, id, value, isjson));
	else
		strcpy(io_buffer, message(MSG_GPUVDDC, id, value, isjson));
}

static void send_result(SOCKETTYPE c, bool isjson);

void doquit(SOCKETTYPE c, char *param, bool isjson)
{
	if (isjson)
		strcpy(io_buffer, JSON_START JSON_BYE);
	else
		strcpy(io_buffer, _BYE);

	send_result(c, isjson);
	*io_buffer = '\0';
	bye = 1;
	kill_work();
}

void dosave(SOCKETTYPE c, char *param, bool isjson)
{
	FILE *fcfg;

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISFN, 0, NULL, isjson));
		return;
	}

	fcfg = fopen(param, "w");
	if (!fcfg) {
		strcpy(io_buffer, message(MSG_BADFN, 0, param, isjson));
		return;
	}

	write_config(fcfg);
	fclose(fcfg);

	strcpy(io_buffer, message(MSG_SAVED, 0, param, isjson));
}

struct CMDS {
	char *name;
	void (*func)(SOCKETTYPE, char *, bool);
} cmds[] = {
	{ "version",		apiversion },
	{ "config",		minerconfig },
	{ "devs",		devstatus },
	{ "pools",		poolstatus },
	{ "summary",		summary },
	{ "gpuenable",		gpuenable },
	{ "gpudisable",		gpudisable },
	{ "gpurestart",		gpurestart },
	{ "gpu",		gpudev },
#ifdef WANT_CPUMINE
	{ "cpu",		cpudev },
#endif
	{ "gpucount",		gpucount },
	{ "cpucount",		cpucount },
	{ "switchpool",		switchpool },
	{ "gpuintensity",	gpuintensity },
	{ "gpumem",		gpumem},
	{ "gpuengine",		gpuengine},
	{ "gpufan",		gpufan},
	{ "gpuvddc",		gpuvddc},
	{ "save",		dosave },
	{ "quit",		doquit },
	{ NULL }
};

static void send_result(SOCKETTYPE c, bool isjson)
{
	int n;
	int len;

	if (isjson)
		strcat(io_buffer, JSON_END);

	len = strlen(io_buffer);

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

static void tidyup()
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
	char param_buf[BUFSIZ];
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
	char *cmd;
	char *param;
	bool addrok;
	json_error_t json_err;
	json_t *json_config;
	json_t *json_val;
	bool isjson;
	bool did;
	int i;

	/* This should be done first to ensure curl has already called WSAStartup() in windows */
	sleep(opt_log_interval);

	if (!opt_api_listen) {
		applog(LOG_DEBUG, "API not running%s", UNAVAILABLE);
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

	// try for more than 1 minute ... in case the old one hasn't completely gone yet
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

				if (*buf != ISJSON) {
					isjson = false;

					param = strchr(buf, SEPARATOR);
					if (param != NULL)
						*(param++) = '\0';

					cmd = buf;
				}
				else {
					isjson = true;

					param = NULL;

					json_config = json_loadb(buf, n, 0, &json_err);

					if (!json_is_object(json_config)) {
						strcpy(io_buffer, message(MSG_INVJSON, 0, NULL, isjson));
						send_result(c, isjson);
						did = true;
					}
					else {
						json_val = json_object_get(json_config, JSON_COMMAND);
						if (json_val == NULL) {
							strcpy(io_buffer, message(MSG_MISCMD, 0, NULL, isjson));
							send_result(c, isjson);
							did = true;
						}
						else {
							if (!json_is_string(json_val)) {
								strcpy(io_buffer, message(MSG_INVCMD, 0, NULL, isjson));
								send_result(c, isjson);
								did = true;
							}
							else {
								cmd = (char *)json_string_value(json_val);
								json_val = json_object_get(json_config, JSON_PARAMETER);
								if (json_is_string(json_val))
									param = (char *)json_string_value(json_val);
								else if (json_is_integer(json_val)) {
									sprintf(param_buf, "%d", (int)json_integer_value(json_val));
									param = param_buf;
								} else if (json_is_real(json_val)) {
									sprintf(param_buf, "%f", (double)json_real_value(json_val));
									param = param_buf;
								}
							}
						}
					}
				}

				if (!did)
					for (i = 0; cmds[i].name != NULL; i++) {
						if (strcmp(cmd, cmds[i].name) == 0) {
							(cmds[i].func)(c, param, isjson);
							send_result(c, isjson);
							did = true;
							break;
						}
					}

				if (!did) {
					strcpy(io_buffer, message(MSG_INVCMD, 0, NULL, isjson));
					send_result(c, isjson);
				}
			}
		}
		CLOSESOCKET(c);
	}
die:
	tidyup();
}
