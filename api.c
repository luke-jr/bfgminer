/*
 * Copyright 2011-2012 Andrew Smith
 * Copyright 2011-2012 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 *
 * Note: the code always includes GPU support even if there are no GPUs
 *	this simplifies handling multiple other device code being included
 *	depending on compile options
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include "compat.h"
#include "miner.h"
#include "driver-cpu.h" /* for algo_names[], TODO: re-factor dependency */

#if defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_ZTEX) || defined(USE_MODMINER)
#define HAVE_AN_FPGA 1
#endif

#if defined(unix) || defined(__APPLE__)
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
	#include <ws2tcpip.h>
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

	#ifndef in_addr_t
	#define in_addr_t uint32_t
	#endif
#endif

// Big enough for largest API request
//  though a PC with 100s of PGAs/CPUs may exceed the size ...
// Current code assumes it can socket send this size also
#define MYBUFSIZ	65432	// TODO: intercept before it's exceeded

// BUFSIZ varies on Windows and Linux
#define TMPBUFSIZ	8192

// Number of requests to queue - normally would be small
// However lots of PGA's may mean more
#define QUEUE	100

static char *io_buffer = NULL;
static char *msg_buffer = NULL;
static SOCKETTYPE sock = INVSOCK;

static const char *UNAVAILABLE = " - API will not be available";

static const char *BLANK = "";
static const char *COMMA = ",";
static const char SEPARATOR = '|';
#define SEPSTR "|"
static const char GPUSEP = ',';

static const char *APIVERSION = "1.12";
static const char *DEAD = "Dead";
static const char *SICK = "Sick";
static const char *NOSTART = "NoStart";
static const char *DISABLED = "Disabled";
static const char *ALIVE = "Alive";
static const char *REJECTING = "Rejecting";
static const char *UNKNOWN = "Unknown";
#define _DYNAMIC "D"
static const char *DYNAMIC = _DYNAMIC;

static const char *YES = "Y";
static const char *NO = "N";

static const char *DEVICECODE = ""
#ifdef HAVE_OPENCL
			"GPU "
#endif
#ifdef USE_BITFORCE
			"BFL "
#endif
#ifdef USE_ICARUS
			"ICA "
#endif
#ifdef USE_ZTEX
			"ZTX "
#endif
#ifdef USE_MODMINER
			"MMQ "
#endif
#ifdef WANT_CPUMINE
			"CPU "
#endif
			"";

static const char *OSINFO =
#if defined(__linux)
			"Linux";
#else
#if defined(__APPLE__)
			"Apple";
#else
#if defined (WIN32)
			"Windows";
#else
#if defined(unix)
			"Unix";
#else
			"Unknown";
#endif
#endif
#endif
#endif

#define _DEVS		"DEVS"
#define _POOLS		"POOLS"
#define _SUMMARY	"SUMMARY"
#define _STATUS		"STATUS"
#define _VERSION	"VERSION"
#define _MINECON	"CONFIG"
#define _GPU		"GPU"

#ifdef HAVE_AN_FPGA
#define _PGA		"PGA"
#endif

#ifdef WANT_CPUMINE
#define _CPU		"CPU"
#endif

#define _GPUS		"GPUS"
#define _PGAS		"PGAS"
#define _CPUS		"CPUS"
#define _NOTIFY		"NOTIFY"
#define _DEVDETAILS	"DEVDETAILS"
#define _BYE		"BYE"
#define _RESTART	"RESTART"
#define _MINESTATS	"STATS"

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

#ifdef HAVE_AN_FPGA
#define JSON_PGA	JSON1 _PGA JSON2
#endif

#ifdef WANT_CPUMINE
#define JSON_CPU	JSON1 _CPU JSON2
#endif

#define JSON_GPUS	JSON1 _GPUS JSON2
#define JSON_PGAS	JSON1 _PGAS JSON2
#define JSON_CPUS	JSON1 _CPUS JSON2
#define JSON_NOTIFY	JSON1 _NOTIFY JSON2
#define JSON_DEVDETAILS	JSON1 _DEVDETAILS JSON2
#define JSON_BYE	JSON1 _BYE JSON1
#define JSON_RESTART	JSON1 _RESTART JSON1
#define JSON_CLOSE	JSON3
#define JSON_MINESTATS	JSON1 _MINESTATS JSON2
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
#define MSG_ACCDENY 45
#define MSG_ACCOK 46
#define MSG_ENAPOOL 47
#define MSG_DISPOOL 48
#define MSG_ALRENAP 49
#define MSG_ALRDISP 50
#define MSG_DISLASTP 51
#define MSG_MISPDP 52
#define MSG_INVPDP 53
#define MSG_TOOMANYP 54
#define MSG_ADDPOOL 55

#ifdef HAVE_AN_FPGA
#define MSG_PGANON 56
#define MSG_PGADEV 57
#define MSG_INVPGA 58
#endif

#define MSG_NUMPGA 59
#define MSG_NOTIFY 60

#ifdef HAVE_AN_FPGA
#define MSG_PGALRENA 61
#define MSG_PGALRDIS 62
#define MSG_PGAENA 63
#define MSG_PGADIS 64
#define MSG_PGAUNW 65
#endif

#define MSG_REMLASTP 66
#define MSG_ACTPOOL 67
#define MSG_REMPOOL 68
#define MSG_DEVDETAILS 69
#define MSG_MINESTATS 70

enum code_severity {
	SEVERITY_ERR,
	SEVERITY_WARN,
	SEVERITY_INFO,
	SEVERITY_SUCC,
	SEVERITY_FAIL
};

enum code_parameters {
	PARAM_GPU,
	PARAM_PGA,
	PARAM_CPU,
	PARAM_GPUMAX,
	PARAM_PGAMAX,
	PARAM_CPUMAX,
	PARAM_PMAX,
	PARAM_POOLMAX,

// Single generic case: have the code resolve it - see below
	PARAM_DMAX,

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
#ifdef HAVE_OPENCL
 { SEVERITY_ERR,   MSG_INVGPU,	PARAM_GPUMAX,	"Invalid GPU id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_ALRENA,	PARAM_GPU,	"GPU %d already enabled" },
 { SEVERITY_INFO,  MSG_ALRDIS,	PARAM_GPU,	"GPU %d already disabled" },
 { SEVERITY_WARN,  MSG_GPUMRE,	PARAM_GPU,	"GPU %d must be restarted first" },
 { SEVERITY_INFO,  MSG_GPUREN,	PARAM_GPU,	"GPU %d sent enable message" },
#endif
 { SEVERITY_ERR,   MSG_GPUNON,	PARAM_NONE,	"No GPUs" },
 { SEVERITY_SUCC,  MSG_POOL,	PARAM_PMAX,	"%d Pool(s)" },
 { SEVERITY_ERR,   MSG_NOPOOL,	PARAM_NONE,	"No pools" },

 { SEVERITY_SUCC,  MSG_DEVS,	PARAM_DMAX,
#ifdef HAVE_OPENCL
		 	 	 	 	"%d GPU(s)"
#endif
#if defined(HAVE_AN_FPGA) && defined(HAVE_OPENCL)
						" - "
#endif
#ifdef HAVE_AN_FPGA
						"%d PGA(s)"
#endif
#if defined(WANT_CPUMINE) && (defined(HAVE_OPENCL) || defined(HAVE_AN_FPGA))
						" - "
#endif
#ifdef WANT_CPUMINE
						"%d CPU(s)"
#endif
 },

 { SEVERITY_ERR,   MSG_NODEVS,	PARAM_NONE,	"No GPUs"
#ifdef HAVE_AN_FPGA
						"/PGAs"
#endif
#ifdef WANT_CPUMINE
						"/CPUs"
#endif
 },

 { SEVERITY_SUCC,  MSG_SUMM,	PARAM_NONE,	"Summary" },
#ifdef HAVE_OPENCL
 { SEVERITY_INFO,  MSG_GPUDIS,	PARAM_GPU,	"GPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_GPUREI,	PARAM_GPU,	"GPU %d restart attempted" },
#endif
 { SEVERITY_ERR,   MSG_INVCMD,	PARAM_NONE,	"Invalid command" },
 { SEVERITY_ERR,   MSG_MISID,	PARAM_NONE,	"Missing device id parameter" },
#ifdef HAVE_OPENCL
 { SEVERITY_SUCC,  MSG_GPUDEV,	PARAM_GPU,	"GPU%d" },
#endif
#ifdef HAVE_AN_FPGA
 { SEVERITY_ERR,   MSG_PGANON,	PARAM_NONE,	"No PGAs" },
 { SEVERITY_SUCC,  MSG_PGADEV,	PARAM_PGA,	"PGA%d" },
 { SEVERITY_ERR,   MSG_INVPGA,	PARAM_PGAMAX,	"Invalid PGA id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_PGALRENA,PARAM_PGA,	"PGA %d already enabled" },
 { SEVERITY_INFO,  MSG_PGALRDIS,PARAM_PGA,	"PGA %d already disabled" },
 { SEVERITY_INFO,  MSG_PGAENA,	PARAM_PGA,	"PGA %d sent enable message" },
 { SEVERITY_INFO,  MSG_PGADIS,	PARAM_PGA,	"PGA %d set disable flag" },
 { SEVERITY_ERR,   MSG_PGAUNW,	PARAM_PGA,	"PGA %d is not flagged WELL, cannot enable" },
#endif
#ifdef WANT_CPUMINE
 { SEVERITY_ERR,   MSG_CPUNON,	PARAM_NONE,	"No CPUs" },
 { SEVERITY_SUCC,  MSG_CPUDEV,	PARAM_CPU,	"CPU%d" },
 { SEVERITY_ERR,   MSG_INVCPU,	PARAM_CPUMAX,	"Invalid CPU id %d - range is 0 - %d" },
#endif
 { SEVERITY_SUCC,  MSG_NUMGPU,	PARAM_NONE,	"GPU count" },
 { SEVERITY_SUCC,  MSG_NUMPGA,	PARAM_NONE,	"PGA count" },
 { SEVERITY_SUCC,  MSG_NUMCPU,	PARAM_NONE,	"CPU count" },
 { SEVERITY_SUCC,  MSG_VERSION,	PARAM_NONE,	"BFGMiner versions" },
 { SEVERITY_ERR,   MSG_INVJSON,	PARAM_NONE,	"Invalid JSON" },
 { SEVERITY_ERR,   MSG_MISCMD,	PARAM_CMD,	"Missing JSON '%s'" },
 { SEVERITY_ERR,   MSG_MISPID,	PARAM_NONE,	"Missing pool id parameter" },
 { SEVERITY_ERR,   MSG_INVPID,	PARAM_POOLMAX,	"Invalid pool id %d - range is 0 - %d" },
 { SEVERITY_SUCC,  MSG_SWITCHP,	PARAM_POOL,	"Switching to pool %d:'%s'" },
 { SEVERITY_ERR,   MSG_MISVAL,	PARAM_NONE,	"Missing comma after GPU number" },
 { SEVERITY_ERR,   MSG_NOADL,	PARAM_NONE,	"ADL is not available" },
 { SEVERITY_ERR,   MSG_NOGPUADL,PARAM_GPU,	"GPU %d does not have ADL" },
 { SEVERITY_ERR,   MSG_INVINT,	PARAM_STR,	"Invalid intensity (%s) - must be '" _DYNAMIC  "' or range " _MIN_INTENSITY_STR " - " _MAX_INTENSITY_STR },
 { SEVERITY_INFO,  MSG_GPUINT,	PARAM_BOTH,	"GPU %d set new intensity to %s" },
 { SEVERITY_SUCC,  MSG_MINECON, PARAM_NONE,	"BFGMiner config" },
#ifdef HAVE_OPENCL
 { SEVERITY_ERR,   MSG_GPUMERR,	PARAM_BOTH,	"Setting GPU %d memoryclock to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUMEM,	PARAM_BOTH,	"Setting GPU %d memoryclock to (%s) reported success" },
 { SEVERITY_ERR,   MSG_GPUEERR,	PARAM_BOTH,	"Setting GPU %d clock to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUENG,	PARAM_BOTH,	"Setting GPU %d clock to (%s) reported success" },
 { SEVERITY_ERR,   MSG_GPUVERR,	PARAM_BOTH,	"Setting GPU %d vddc to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUVDDC,	PARAM_BOTH,	"Setting GPU %d vddc to (%s) reported success" },
 { SEVERITY_ERR,   MSG_GPUFERR,	PARAM_BOTH,	"Setting GPU %d fan to (%s) reported failure" },
 { SEVERITY_SUCC,  MSG_GPUFAN,	PARAM_BOTH,	"Setting GPU %d fan to (%s) reported success" },
#endif
 { SEVERITY_ERR,   MSG_MISFN,	PARAM_NONE,	"Missing save filename parameter" },
 { SEVERITY_ERR,   MSG_BADFN,	PARAM_STR,	"Can't open or create save file '%s'" },
 { SEVERITY_SUCC,  MSG_SAVED,	PARAM_STR,	"Configuration saved to file '%s'" },
 { SEVERITY_ERR,   MSG_ACCDENY,	PARAM_STR,	"Access denied to '%s' command" },
 { SEVERITY_SUCC,  MSG_ACCOK,	PARAM_NONE,	"Privileged access OK" },
 { SEVERITY_SUCC,  MSG_ENAPOOL,	PARAM_POOL,	"Enabling pool %d:'%s'" },
 { SEVERITY_SUCC,  MSG_DISPOOL,	PARAM_POOL,	"Disabling pool %d:'%s'" },
 { SEVERITY_INFO,  MSG_ALRENAP,	PARAM_POOL,	"Pool %d:'%s' already enabled" },
 { SEVERITY_INFO,  MSG_ALRDISP,	PARAM_POOL,	"Pool %d:'%s' already disabled" },
 { SEVERITY_ERR,   MSG_DISLASTP,PARAM_POOL,	"Cannot disable last active pool %d:'%s'" },
 { SEVERITY_ERR,   MSG_MISPDP,	PARAM_NONE,	"Missing addpool details" },
 { SEVERITY_ERR,   MSG_INVPDP,	PARAM_STR,	"Invalid addpool details '%s'" },
 { SEVERITY_ERR,   MSG_TOOMANYP,PARAM_NONE,	"Reached maximum number of pools (%d)" },
 { SEVERITY_SUCC,  MSG_ADDPOOL,	PARAM_STR,	"Added pool '%s'" },
 { SEVERITY_ERR,   MSG_REMLASTP,PARAM_POOL,	"Cannot remove last pool %d:'%s'" },
 { SEVERITY_ERR,   MSG_ACTPOOL, PARAM_POOL,	"Cannot remove active pool %d:'%s'" },
 { SEVERITY_SUCC,  MSG_REMPOOL, PARAM_BOTH,	"Removed pool %d:'%s'" },
 { SEVERITY_SUCC,  MSG_NOTIFY,	PARAM_NONE,	"Notify" },
 { SEVERITY_SUCC,  MSG_DEVDETAILS,PARAM_NONE,	"Device Details" },
 { SEVERITY_SUCC,  MSG_MINESTATS,PARAM_NONE,	"CGMiner stats" },
 { SEVERITY_FAIL, 0, 0, NULL }
};

static int my_thr_id = 0;
static bool bye;
static bool ping = true;

// Used to control quit restart access to shutdown variables
static pthread_mutex_t quit_restart_lock;

static bool do_a_quit;
static bool do_a_restart;

static time_t when = 0;	// when the request occurred

struct IP4ACCESS {
	in_addr_t ip;
	in_addr_t mask;
	bool writemode;
};

static struct IP4ACCESS *ipaccess = NULL;
static int ips = 0;

#ifdef USE_BITFORCE
extern struct device_api bitforce_api;
#endif

#ifdef USE_ICARUS
extern struct device_api icarus_api;
#endif

#ifdef USE_ZTEX
extern struct device_api ztex_api;
#endif

#ifdef USE_MODMINER
extern struct device_api modminer_api;
#endif

// This is only called when expected to be needed (rarely)
// i.e. strings outside of the codes control (input from the user)
static char *escape_string(char *str, bool isjson)
{
	char *buf, *ptr;
	int count;

	count = 0;
	for (ptr = str; *ptr; ptr++) {
		switch (*ptr) {
		case ',':
		case '|':
		case '=':
			if (!isjson)
				count++;
			break;
		case '"':
			if (isjson)
				count++;
			break;
		case '\\':
			count++;
			break;
		}
	}

	if (count == 0)
		return str;

	buf = malloc(strlen(str) + count + 1);
	if (unlikely(!buf))
		quit(1, "Failed to malloc escape buf");

	ptr = buf;
	while (*str)
		switch (*str) {
		case ',':
		case '|':
		case '=':
			if (!isjson)
				*(ptr++) = '\\';
			*(ptr++) = *(str++);
			break;
		case '"':
			if (isjson)
				*(ptr++) = '\\';
			*(ptr++) = *(str++);
			break;
		case '\\':
			*(ptr++) = '\\';
			*(ptr++) = *(str++);
			break;
		default:
			*(ptr++) = *(str++);
			break;
		}

	*ptr = '\0';

	return buf;
}

#ifdef HAVE_AN_FPGA
static int numpgas()
{
	int count = 0;
	int i;

	for (i = 0; i < total_devices; i++) {
#ifdef USE_BITFORCE
		if (devices[i]->api == &bitforce_api)
			count++;
#endif
#ifdef USE_ICARUS
		if (devices[i]->api == &icarus_api)
			count++;
#endif
#ifdef USE_ZTEX
		if (devices[i]->api == &ztex_api)
			count++;
#endif
#ifdef USE_MODMINER
		if (devices[i]->api == &modminer_api)
			count++;
#endif
	}
	return count;
}

static int pgadevice(int pgaid)
{
	int count = 0;
	int i;

	for (i = 0; i < total_devices; i++) {
#ifdef USE_BITFORCE
		if (devices[i]->api == &bitforce_api)
			count++;
#endif
#ifdef USE_ICARUS
		if (devices[i]->api == &icarus_api)
			count++;
#endif
#ifdef USE_ZTEX
		if (devices[i]->api == &ztex_api)
			count++;
#endif
#ifdef USE_MODMINER
		if (devices[i]->api == &modminer_api)
			count++;
#endif
		if (count == (pgaid + 1))
			return i;
	}
	return -1;
}
#endif

// All replies (except BYE and RESTART) start with a message
//  thus for JSON, message() inserts JSON_START at the front
//  and send_result() adds JSON_END at the end
static char *message(int messageid, int paramid, char *param2, bool isjson)
{
	char severity;
	char *ptr;
#ifdef HAVE_AN_FPGA
	int pga;
#endif
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

			sprintf(msg_buffer, isjson
				? JSON_START JSON_STATUS "{\"" _STATUS "\":\"%c\",\"When\":%lu,\"Code\":%d,\"Msg\":\""
				: _STATUS "=%c,When=%lu,Code=%d,Msg=",
				severity, (unsigned long)when, messageid);

			ptr = msg_buffer + strlen(msg_buffer);

			switch(codes[i].params) {
			case PARAM_GPU:
			case PARAM_PGA:
			case PARAM_CPU:
				sprintf(ptr, codes[i].description, paramid);
				break;
			case PARAM_POOL:
				sprintf(ptr, codes[i].description, paramid, pools[paramid]->rpc_url);
				break;
#ifdef HAVE_OPENCL
			case PARAM_GPUMAX:
				sprintf(ptr, codes[i].description, paramid, nDevs - 1);
				break;
#endif
#ifdef HAVE_AN_FPGA
			case PARAM_PGAMAX:
				pga = numpgas();
				sprintf(ptr, codes[i].description, paramid, pga - 1);
				break;
#endif
#ifdef WANT_CPUMINE
			case PARAM_CPUMAX:
				if (opt_n_threads > 0)
					cpu = num_processors;
				else
					cpu = 0;
				sprintf(ptr, codes[i].description, paramid, cpu - 1);
				break;
#endif
			case PARAM_PMAX:
				sprintf(ptr, codes[i].description, total_pools);
				break;
			case PARAM_POOLMAX:
				sprintf(ptr, codes[i].description, paramid, total_pools - 1);
				break;
			case PARAM_DMAX:
#ifdef HAVE_AN_FPGA
				pga = numpgas();
#endif
#ifdef WANT_CPUMINE
				if (opt_n_threads > 0)
					cpu = num_processors;
				else
					cpu = 0;
#endif

				sprintf(ptr, codes[i].description
#ifdef HAVE_OPENCL
					, nDevs
#endif
#ifdef HAVE_AN_FPGA
					, pga
#endif
#ifdef WANT_CPUMINE
					, cpu
#endif
					);
				break;
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

			sprintf(ptr, isjson
				? "\",\"Description\":\"%s\"}" JSON_CLOSE
				: ",Description=%s" SEPSTR,
				opt_api_description);

			return msg_buffer;
		}
	}

	sprintf(msg_buffer, isjson
		? JSON_START JSON_STATUS "{\"" _STATUS "\":\"F\",\"When\":%lu,\"Code\":-1,\"Msg\":\"%d\",\"Description\":\"%s\"}" JSON_CLOSE
		: _STATUS "=F,When=%lu,Code=-1,Msg=%d,Description=%s" SEPSTR,
		(unsigned long)when, messageid, opt_api_description);

	return msg_buffer;
}

static void apiversion(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	sprintf(io_buffer, isjson
		? "%s," JSON_VERSION "{\"CGMiner\":\"%s\",\"API\":\"%s\"}" JSON_CLOSE
		: "%s" _VERSION ",CGMiner=%s,API=%s" SEPSTR,
		message(MSG_VERSION, 0, NULL, isjson),
		VERSION, APIVERSION);
}

static void minerconfig(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char buf[TMPBUFSIZ];
	int gpucount = 0;
	int pgacount = 0;
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

#ifdef HAVE_OPENCL
	gpucount = nDevs;
#endif

#ifdef HAVE_AN_FPGA
	pgacount = numpgas();
#endif

#ifdef WANT_CPUMINE
	cpucount = opt_n_threads > 0 ? num_processors : 0;
#endif

	strcpy(io_buffer, message(MSG_MINECON, 0, NULL, isjson));

	sprintf(buf, isjson
		? "," JSON_MINECON "{\"GPU Count\":%d,\"PGA Count\":%d,\"CPU Count\":%d,\"Pool Count\":%d,\"ADL\":\"%s\",\"ADL in use\":\"%s\",\"Strategy\":\"%s\",\"Log Interval\":%d,\"Device Code\":\"%s\",\"OS\":\"%s\"}" JSON_CLOSE
		: _MINECON ",GPU Count=%d,PGA Count=%d,CPU Count=%d,Pool Count=%d,ADL=%s,ADL in use=%s,Strategy=%s,Log Interval=%d,Device Code=%s,OS=%s" SEPSTR,

		gpucount, pgacount, cpucount, total_pools, adl, adlinuse,
		strategies[pool_strategy].s, opt_log_interval, DEVICECODE, OSINFO);

	strcat(io_buffer, buf);
}

static const char*
bool2str(bool b)
{
	return b ? YES : NO;
}

static const char*
status2str(enum alive status)
{
	switch (status) {
	case LIFE_WELL:
		return ALIVE;
	case LIFE_SICK:
		return SICK;
	case LIFE_DEAD:
		return DEAD;
	case LIFE_NOSTART:
		return NOSTART;
	default:
		return UNKNOWN;
	}
}

#ifdef JSON_ENCODE_ANY
#	define json_dumps_val(value, flags)  json_dumps(value, (flags) | JSON_ENCODE_ANY)
#else
static char*
json_dumps_val(json_t*root, unsigned long flags)
{
	json_t *json;
	char *s, *d;
	size_t l;
	
	json = json_array();
	json_array_append(json, root);
	s = json_dumps(json, flags);
	json_decref(json);
	if (!s)
		return s;
	
	assert(s[0] == '[');
	d = strdup(&s[1]);
	free(s);
	l = strlen(d);
	assert(l);
	--l;
	assert(d[l] == ']');
	d[l] = '\0';
	
	return d;
}
#endif

static void
append_kv(char *buf, json_t*info, bool isjson)
{
	json_t *value;
	const char *key, *tmpl = isjson ? ",\"%s\":%s" : ",%s=%s";
	char *vdump;
	void *it;

	for (it = json_object_iter(info); it; it = json_object_iter_next(info, it)) {
		key = json_object_iter_key(it);
		value = json_object_iter_value(it);

		if (isjson || !json_is_string(value))
			vdump = json_dumps_val(value, JSON_COMPACT);
		else
			vdump = strdup(json_string_value(value));
		tailsprintf(buf, tmpl, key, vdump);
		free(vdump);
	}
}

static void
devdetail_an(char *buf, struct cgpu_info *cgpu, bool isjson)
{
	tailsprintf(buf, isjson
				? "{\"%s\":%d,Driver=%s"
				: "%s=%d,Driver=%s",
			cgpu->api->name, cgpu->device_id,
			cgpu->api->dname
	);

	if (cgpu->kname)
		tailsprintf(buf, isjson ? ",\"Kernel\":\"%s\"" : ",Kernel=%s", cgpu->kname);
	if (cgpu->name)
		tailsprintf(buf, isjson ? ",\"Model\":\"%s\"" : ",Model=%s", cgpu->name);
	if (cgpu->device_path)
		tailsprintf(buf, isjson ? ",\"Device Path\":\"%s\"" : ",Device Path=%s", cgpu->device_path);

	if (cgpu->api->get_extra_device_detail) {
		json_t *info = cgpu->api->get_extra_device_detail(cgpu);
		append_kv(buf, info, isjson);
		json_decref(info);
	}

	tailsprintf(buf, "%c", isjson ? '}' : SEPARATOR);
}

static void devstatus_an(char *buf, struct cgpu_info *cgpu, bool isjson)
{
	tailsprintf(buf, isjson
				? "{\"%s\":%d,\"Enabled\":\"%s\",\"Status\":\"%s\",\"Temperature\":%.2f,\"MHS av\":%.2f,\"MHS %ds\":%.2f,\"Accepted\":%d,\"Rejected\":%d,\"Hardware Errors\":%d,\"Utility\":%.2f,\"Last Share Pool\":%d,\"Last Share Time\":%lu,\"Total MH\":%.4f"
				: "%s=%d,Enabled=%s,Status=%s,Temperature=%.2f,MHS av=%.2f,MHS %ds=%.2f,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Last Share Pool=%d,Last Share Time=%lu,Total MH=%.4f",
			cgpu->api->name, cgpu->device_id,
			bool2str(cgpu->deven != DEV_DISABLED),
			status2str(cgpu->status),
			cgpu->temp,
			cgpu->total_mhashes / total_secs, opt_log_interval, cgpu->rolling,
			cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->utility,
			((unsigned long)(cgpu->last_share_pool_time) > 0) ? cgpu->last_share_pool : -1,
			(unsigned long)(cgpu->last_share_pool_time), cgpu->total_mhashes
	);

	if (cgpu->api->get_extra_device_status) {
		json_t *info = cgpu->api->get_extra_device_status(cgpu);
		append_kv(buf, info, isjson);
		json_decref(info);
	}

	tailsprintf(buf, "%c", isjson ? '}' : SEPARATOR);
}

#ifdef HAVE_OPENCL
static void gpustatus(int gpu, bool isjson)
{
	if (gpu < 0 || gpu >= nDevs)
		return;
	devstatus_an(io_buffer, &gpus[gpu], isjson);
}
#endif
#ifdef HAVE_AN_FPGA
static void pgastatus(int pga, bool isjson)
{
	int dev = pgadevice(pga);
	if (dev < 0) // Should never happen
		return;
	devstatus_an(io_buffer, devices[dev], isjson);
}
#endif

__maybe_unused
static void cpustatus(int cpu, bool isjson)
{
	if (opt_n_threads <= 0 || cpu < 0 || cpu >= num_processors)
		return;
	devstatus_an(io_buffer, &cpus[cpu], isjson);
}

static void
devinfo_internal(void (*func)(char*, struct cgpu_info*, bool),
                 __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	int i;

	if (total_devices == 0) {
		strcpy(io_buffer, message(MSG_NODEVS, 0, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_DEVS, 0, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_DEVS);
	}

	for (i = 0; i < total_devices; ++i) {
		if (isjson && i)
			strcat(io_buffer, COMMA);

		func(io_buffer, devices[i], isjson);
	}

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

static void
devdetail(SOCKETTYPE c, char *param, bool isjson)
{
	return devinfo_internal(devdetail_an, c, param, isjson);
}

static void
devstatus(SOCKETTYPE c, char *param, bool isjson)
{
	return devinfo_internal(devstatus_an, c, param, isjson);
}

#ifdef HAVE_OPENCL
static void gpudev(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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
#endif
#ifdef HAVE_AN_FPGA
static void pgadev(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
{
	int numpga = numpgas();
	int id;

	if (numpga == 0) {
		strcpy(io_buffer, message(MSG_PGANON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numpga) {
		strcpy(io_buffer, message(MSG_INVPGA, id, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_PGADEV, id, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_PGA);
	}

	pgastatus(id, isjson);

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

static void pgaenable(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
{
	int numpga = numpgas();
	struct thr_info *thr;
	int pga;
	int id;
	int i;

	if (numpga == 0) {
		strcpy(io_buffer, message(MSG_PGANON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numpga) {
		strcpy(io_buffer, message(MSG_INVPGA, id, NULL, isjson));
		return;
	}

	int dev = pgadevice(id);
	if (dev < 0) { // Should never happen
		strcpy(io_buffer, message(MSG_INVPGA, id, NULL, isjson));
		return;
	}

	struct cgpu_info *cgpu = devices[dev];

	if (cgpu->deven != DEV_DISABLED) {
		strcpy(io_buffer, message(MSG_PGALRENA, id, NULL, isjson));
		return;
	}

	if (cgpu->status != LIFE_WELL) {
		strcpy(io_buffer, message(MSG_PGAUNW, id, NULL, isjson));
		return;
	}

	for (i = 0; i < mining_threads; i++) {
		pga = thr_info[i].cgpu->device_id;
		if (pga == dev) {
			thr = &thr_info[i];
			cgpu->deven = DEV_ENABLED;
			tq_push(thr->q, &ping);
		}
	}

	strcpy(io_buffer, message(MSG_PGAENA, id, NULL, isjson));
}

static void pgadisable(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
{
	int numpga = numpgas();
	int id;

	if (numpga == 0) {
		strcpy(io_buffer, message(MSG_PGANON, 0, NULL, isjson));
		return;
	}

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISID, 0, NULL, isjson));
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numpga) {
		strcpy(io_buffer, message(MSG_INVPGA, id, NULL, isjson));
		return;
	}

	int dev = pgadevice(id);
	if (dev < 0) { // Should never happen
		strcpy(io_buffer, message(MSG_INVPGA, id, NULL, isjson));
		return;
	}

	struct cgpu_info *cgpu = devices[dev];

	if (cgpu->deven == DEV_DISABLED) {
		strcpy(io_buffer, message(MSG_PGALRDIS, id, NULL, isjson));
		return;
	}

	cgpu->deven = DEV_DISABLED;

	strcpy(io_buffer, message(MSG_PGADIS, id, NULL, isjson));
}
#endif

#ifdef WANT_CPUMINE
static void cpudev(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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

static void poolstatus(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char buf[TMPBUFSIZ];
	char *status, *lp;
	char *rpc_url;
	char *rpc_user;
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

		switch (pool->enabled) {
		case POOL_DISABLED:
			status = (char *)DISABLED;
			break;
		case POOL_REJECTING:
			status = (char *)REJECTING;
			break;
		case POOL_ENABLED:
			if (pool->idle)
				status = (char *)DEAD;
			else
				status = (char *)ALIVE;
			break;
		default:
			status = (char *)UNKNOWN;
			break;
		}

		if (pool->hdr_path)
			lp = (char *)YES;
		else
			lp = (char *)NO;

		rpc_url = escape_string(pool->rpc_url, isjson);
		rpc_user = escape_string(pool->rpc_user, isjson);

		sprintf(buf, isjson
			? "%s{\"POOL\":%d,\"URL\":\"%s\",\"Status\":\"%s\",\"Priority\":%d,\"Long Poll\":\"%s\",\"Getworks\":%d,\"Accepted\":%d,\"Rejected\":%d,\"Discarded\":%d,\"Stale\":%d,\"Get Failures\":%d,\"Remote Failures\":%d,\"User\":\"%s\",\"Last Share Time\":%lu}"
			: "%sPOOL=%d,URL=%s,Status=%s,Priority=%d,Long Poll=%s,Getworks=%d,Accepted=%d,Rejected=%d,Discarded=%d,Stale=%d,Get Failures=%d,Remote Failures=%d,User=%s,Last Share Time=%lu" SEPSTR,
			(isjson && (i > 0)) ? COMMA : BLANK,
			i, rpc_url, status, pool->prio, lp,
			pool->getwork_requested,
			pool->accepted, pool->rejected,
			pool->discarded_work,
			pool->stale_shares,
			pool->getfail_occasions,
			pool->remotefail_occasions,
			rpc_user, pool->last_share_time);

		strcat(io_buffer, buf);

		if (rpc_url != pool->rpc_url)
			free(rpc_url);
		rpc_url = NULL;

		if (rpc_user != pool->rpc_user)
			free(rpc_user);
		rpc_user = NULL;
	}

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

static void summary(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
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
	sprintf(io_buffer, isjson
		? "%s," JSON_SUMMARY "{\"Elapsed\":%.0f,\"Algorithm\":\"%s\",\"MHS av\":%.2f,\"Found Blocks\":%d,\"Getworks\":%d,\"Accepted\":%d,\"Rejected\":%d,\"Hardware Errors\":%d,\"Utility\":%.2f,\"Discarded\":%d,\"Stale\":%d,\"Get Failures\":%d,\"Local Work\":%u,\"Remote Failures\":%u,\"Network Blocks\":%u,\"Total MH\":%.4f}" JSON_CLOSE
		: "%s" _SUMMARY ",Elapsed=%.0f,Algorithm=%s,MHS av=%.2f,Found Blocks=%d,Getworks=%d,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Discarded=%d,Stale=%d,Get Failures=%d,Local Work=%u,Remote Failures=%u,Network Blocks=%u,Total MH=%.4f" SEPSTR,
		message(MSG_SUMM, 0, NULL, isjson),
		total_secs, algo, mhs, found_blocks,
		total_getworks, total_accepted, total_rejected,
		hw_errors, utility, total_discarded, total_stale,
		total_go, local_work, total_ro, new_blocks, total_mhashes_done);
#else
	sprintf(io_buffer, isjson
		? "%s," JSON_SUMMARY "{\"Elapsed\":%.0f,\"MHS av\":%.2f,\"Found Blocks\":%d,\"Getworks\":%d,\"Accepted\":%d,\"Rejected\":%d,\"Hardware Errors\":%d,\"Utility\":%.2f,\"Discarded\":%d,\"Stale\":%d,\"Get Failures\":%d,\"Local Work\":%u,\"Remote Failures\":%u,\"Network Blocks\":%u,\"Total MH\":%.4f}" JSON_CLOSE
		: "%s" _SUMMARY ",Elapsed=%.0f,MHS av=%.2f,Found Blocks=%d,Getworks=%d,Accepted=%d,Rejected=%d,Hardware Errors=%d,Utility=%.2f,Discarded=%d,Stale=%d,Get Failures=%d,Local Work=%u,Remote Failures=%u,Network Blocks=%u,Total MH=%.4f" SEPSTR,
		message(MSG_SUMM, 0, NULL, isjson),
		total_secs, mhs, found_blocks,
		total_getworks, total_accepted, total_rejected,
		hw_errors, utility, total_discarded, total_stale,
		total_go, local_work, total_ro, new_blocks, total_mhashes_done);
#endif
}
#ifdef HAVE_OPENCL
static void gpuenable(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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

	if (gpus[id].deven != DEV_DISABLED) {
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

			gpus[id].deven = DEV_ENABLED;
			tq_push(thr->q, &ping);

		}
	}

	strcpy(io_buffer, message(MSG_GPUREN, id, NULL, isjson));
}

static void gpudisable(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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

	if (gpus[id].deven == DEV_DISABLED) {
		strcpy(io_buffer, message(MSG_ALRDIS, id, NULL, isjson));
		return;
	}

	gpus[id].deven = DEV_DISABLED;

	strcpy(io_buffer, message(MSG_GPUDIS, id, NULL, isjson));
}

static void gpurestart(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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
#endif
static void gpucount(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char buf[TMPBUFSIZ];
	int numgpu = 0;

#ifdef HAVE_OPENCL
	numgpu = nDevs;
#endif

	strcpy(io_buffer, message(MSG_NUMGPU, 0, NULL, isjson));

	sprintf(buf, isjson
		? "," JSON_GPUS "{\"Count\":%d}" JSON_CLOSE
		: _GPUS ",Count=%d" SEPSTR,
		numgpu);

	strcat(io_buffer, buf);
}


static void pgacount(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char buf[TMPBUFSIZ];
	int count = 0;

#ifdef HAVE_AN_FPGA
	count = numpgas();
#endif

	strcpy(io_buffer, message(MSG_NUMPGA, 0, NULL, isjson));

	sprintf(buf, isjson
		? "," JSON_PGAS "{\"Count\":%d}" JSON_CLOSE
		: _PGAS ",Count=%d" SEPSTR,
		count);

	strcat(io_buffer, buf);
}

static void cpucount(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char buf[TMPBUFSIZ];
	int count = 0;

#ifdef WANT_CPUMINE
	count = opt_n_threads > 0 ? num_processors : 0;
#endif

	strcpy(io_buffer, message(MSG_NUMCPU, 0, NULL, isjson));

	sprintf(buf, isjson
		? "," JSON_CPUS "{\"Count\":%d}" JSON_CLOSE
		: _CPUS ",Count=%d" SEPSTR,
		count);

	strcat(io_buffer, buf);
}

static void switchpool(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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
	pool->enabled = POOL_ENABLED;
	switch_pools(pool);

	strcpy(io_buffer, message(MSG_SWITCHP, id, NULL, isjson));
}

static void copyadvanceafter(char ch, char **param, char **buf)
{
#define src_p (*param)
#define dst_b (*buf)

	while (*src_p && *src_p != ch) {
		if (*src_p == '\\' && *(src_p+1) != '\0')
			src_p++;

		*(dst_b++) = *(src_p++);
	}
	if (*src_p)
		src_p++;

	*(dst_b++) = '\0';
}

static bool pooldetails(char *param, char **url, char **user, char **pass)
{
	char *ptr, *buf;

	ptr = buf = malloc(strlen(param)+1);
	if (unlikely(!buf))
		quit(1, "Failed to malloc pooldetails buf");

	*url = buf;

	// copy url
	copyadvanceafter(',', &param, &buf);

	if (!(*param)) // missing user
		goto exitsama;

	*user = buf;

	// copy user
	copyadvanceafter(',', &param, &buf);

	if (!*param) // missing pass
		goto exitsama;

	*pass = buf;

	// copy pass
	copyadvanceafter(',', &param, &buf);

	return true;

exitsama:
	free(ptr);
	return false;
}

static void addpool(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
{
	char *url, *user, *pass;
	char *ptr;

	if (param == NULL || *param == '\0') {
		strcpy(io_buffer, message(MSG_MISPDP, 0, NULL, isjson));
		return;
	}

	if (!pooldetails(param, &url, &user, &pass)) {
		ptr = escape_string(param, isjson);
		strcpy(io_buffer, message(MSG_INVPDP, 0, ptr, isjson));
		if (ptr != param)
			free(ptr);
		ptr = NULL;
		return;
	}

	if (add_pool_details(true, url, user, pass) == ADD_POOL_MAXIMUM) {
		strcpy(io_buffer, message(MSG_TOOMANYP, MAX_POOLS, NULL, isjson));
		return;
	}

	ptr = escape_string(url, isjson);
	strcpy(io_buffer, message(MSG_ADDPOOL, 0, ptr, isjson));
	if (ptr != url)
		free(ptr);
	ptr = NULL;
}

static void enablepool(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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
	if (pool->enabled == POOL_ENABLED) {
		strcpy(io_buffer, message(MSG_ALRENAP, id, NULL, isjson));
		return;
	}

	pool->enabled = POOL_ENABLED;
	if (pool->prio < current_pool()->prio)
		switch_pools(pool);

	strcpy(io_buffer, message(MSG_ENAPOOL, id, NULL, isjson));
}

static void disablepool(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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
	if (pool->enabled == POOL_DISABLED) {
		strcpy(io_buffer, message(MSG_ALRDISP, id, NULL, isjson));
		return;
	}

	if (active_pools() <= 1) {
		strcpy(io_buffer, message(MSG_DISLASTP, id, NULL, isjson));
		return;
	}

	pool->enabled = POOL_DISABLED;
	if (pool == current_pool())
		switch_pools(NULL);

	strcpy(io_buffer, message(MSG_DISPOOL, id, NULL, isjson));
}

static void removepool(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
{
	struct pool *pool;
	char *rpc_url;
	bool dofree = false;
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

	if (total_pools <= 1) {
		strcpy(io_buffer, message(MSG_REMLASTP, id, NULL, isjson));
		return;
	}

	pool = pools[id];
	if (pool == current_pool())
		switch_pools(NULL);

	if (pool == current_pool()) {
		strcpy(io_buffer, message(MSG_ACTPOOL, id, NULL, isjson));
		return;
	}

	pool->enabled = POOL_DISABLED;
	rpc_url = escape_string(pool->rpc_url, isjson);
	if (rpc_url != pool->rpc_url)
		dofree = true;

	remove_pool(pool);

	strcpy(io_buffer, message(MSG_REMPOOL, id, rpc_url, isjson));

	if (dofree)
		free(rpc_url);
	rpc_url = NULL;
}

#ifdef HAVE_OPENCL
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
static void gpuintensity(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
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
		if (intensity < MIN_INTENSITY || intensity > MAX_INTENSITY) {
			strcpy(io_buffer, message(MSG_INVINT, 0, value, isjson));
			return;
		}

		gpus[id].dynamic = false;
		gpus[id].intensity = intensity;
		sprintf(intensitystr, "%d", intensity);
	}

	strcpy(io_buffer, message(MSG_GPUINT, id, intensitystr, isjson));
}

static void gpumem(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
#ifdef HAVE_ADL
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
#else
	strcpy(io_buffer, message(MSG_NOADL, 0, NULL, isjson));
#endif
}

static void gpuengine(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
#ifdef HAVE_ADL
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
#else
	strcpy(io_buffer, message(MSG_NOADL, 0, NULL, isjson));
#endif
}

static void gpufan(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
#ifdef HAVE_ADL
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
#else
	strcpy(io_buffer, message(MSG_NOADL, 0, NULL, isjson));
#endif
}

static void gpuvddc(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
#ifdef HAVE_ADL
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
#else
	strcpy(io_buffer, message(MSG_NOADL, 0, NULL, isjson));
#endif
}
#endif
void doquit(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	if (isjson)
		strcpy(io_buffer, JSON_START JSON_BYE);
	else
		strcpy(io_buffer, _BYE);

	bye = true;
	do_a_quit = true;
}

void dorestart(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	if (isjson)
		strcpy(io_buffer, JSON_START JSON_RESTART);
	else
		strcpy(io_buffer, _RESTART);

	bye = true;
	do_a_restart = true;
}

void privileged(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	strcpy(io_buffer, message(MSG_ACCOK, 0, NULL, isjson));
}

void notifystatus(int device, struct cgpu_info *cgpu, bool isjson)
{
	char buf[TMPBUFSIZ];
	char *reason;

	if (cgpu->device_last_not_well == 0)
		reason = REASON_NONE;
	else
		switch(cgpu->device_not_well_reason) {
		case REASON_THREAD_FAIL_INIT:
			reason = REASON_THREAD_FAIL_INIT_STR;
			break;
		case REASON_THREAD_ZERO_HASH:
			reason = REASON_THREAD_ZERO_HASH_STR;
			break;
		case REASON_THREAD_FAIL_QUEUE:
			reason = REASON_THREAD_FAIL_QUEUE_STR;
			break;
		case REASON_DEV_SICK_IDLE_60:
			reason = REASON_DEV_SICK_IDLE_60_STR;
			break;
		case REASON_DEV_DEAD_IDLE_600:
			reason = REASON_DEV_DEAD_IDLE_600_STR;
			break;
		case REASON_DEV_NOSTART:
			reason = REASON_DEV_NOSTART_STR;
			break;
		case REASON_DEV_OVER_HEAT:
			reason = REASON_DEV_OVER_HEAT_STR;
			break;
		case REASON_DEV_THERMAL_CUTOFF:
			reason = REASON_DEV_THERMAL_CUTOFF_STR;
			break;
		default:
			reason = REASON_UNKNOWN_STR;
			break;
		}

	// ALL counters (and only counters) must start the name with a '*'
	// Simplifies future external support for adding new counters
	sprintf(buf, isjson
		? "%s{\"NOTIFY\":%d,\"Name\":\"%s\",\"ID\":%d,\"Last Well\":%lu,\"Last Not Well\":%lu,\"Reason Not Well\":\"%s\",\"*Thread Fail Init\":%d,\"*Thread Zero Hash\":%d,\"*Thread Fail Queue\":%d,\"*Dev Sick Idle 60s\":%d,\"*Dev Dead Idle 600s\":%d,\"*Dev Nostart\":%d,\"*Dev Over Heat\":%d,\"*Dev Thermal Cutoff\":%d}"
		: "%sNOTIFY=%d,Name=%s,ID=%d,Last Well=%lu,Last Not Well=%lu,Reason Not Well=%s,*Thread Fail Init=%d,*Thread Zero Hash=%d,*Thread Fail Queue=%d,*Dev Sick Idle 60s=%d,*Dev Dead Idle 600s=%d,*Dev Nostart=%d,*Dev Over Heat=%d,*Dev Thermal Cutoff=%d" SEPSTR,
		(isjson && (device > 0)) ? COMMA : BLANK,
		device, cgpu->api->name, cgpu->device_id,
		cgpu->device_last_well, cgpu->device_last_not_well, reason,
		cgpu->thread_fail_init_count, cgpu->thread_zero_hash_count,
		cgpu->thread_fail_queue_count, cgpu->dev_sick_idle_60_count,
		cgpu->dev_dead_idle_600_count, cgpu->dev_nostart_count,
		cgpu->dev_over_heat_count, cgpu->dev_thermal_cutoff_count);

	strcat(io_buffer, buf);
}

static void notify(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	int i;

	if (total_devices == 0) {
		strcpy(io_buffer, message(MSG_NODEVS, 0, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_NOTIFY, 0, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_NOTIFY);
	}

	for (i = 0; i < total_devices; i++)
		notifystatus(i, devices[i], isjson);

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

static void devdetails(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char buf[TMPBUFSIZ];
	struct cgpu_info *cgpu;
	int i;

	if (total_devices == 0) {
		strcpy(io_buffer, message(MSG_NODEVS, 0, NULL, isjson));
		return;
	}

	strcpy(io_buffer, message(MSG_DEVDETAILS, 0, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_DEVDETAILS);
	}

	for (i = 0; i < total_devices; i++) {
		cgpu = devices[i];

		sprintf(buf, isjson
			? "%s{\"DEVDETAILS\":%d,\"Name\":\"%s\",\"ID\":%d,\"Driver\":\"%s\",\"Kernel\":\"%s\",\"Model\":\"%s\",\"Device Path\":\"%s\"}"
			: "%sDEVDETAILS=%d,Name=%s,ID=%d,Driver=%s,Kernel=%s,Model=%s,Device Path=%s" SEPSTR,
			(isjson && (i > 0)) ? COMMA : BLANK,
			i, cgpu->api->name, cgpu->device_id,
			cgpu->api->dname, cgpu->kname ? : BLANK,
			cgpu->name ? : BLANK, cgpu->device_path ? : BLANK);

		strcat(io_buffer, buf);
	}

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

void dosave(__maybe_unused SOCKETTYPE c, char *param, bool isjson)
{
	char filename[PATH_MAX];
	FILE *fcfg;
	char *ptr;

	if (param == NULL || *param == '\0') {
		default_save_file(filename);
		param = filename;
	}

	fcfg = fopen(param, "w");
	if (!fcfg) {
		ptr = escape_string(param, isjson);
		strcpy(io_buffer, message(MSG_BADFN, 0, ptr, isjson));
		if (ptr != param)
			free(ptr);
		ptr = NULL;
		return;
	}

	write_config(fcfg);
	fclose(fcfg);

	ptr = escape_string(param, isjson);
	strcpy(io_buffer, message(MSG_SAVED, 0, ptr, isjson));
	if (ptr != param)
		free(ptr);
	ptr = NULL;
}

static int itemstats(int i, char *id, struct cgminer_stats *stats, struct cgminer_pool_stats *pool_stats, char *extra, bool isjson)
{
	char buf[TMPBUFSIZ];

	if (stats->getwork_calls || (extra != NULL && *extra))
	{
		if (extra == NULL)
			extra = (char *)BLANK;

		sprintf(buf, isjson
			? "%s{\"STATS\":%d,\"ID\":\"%s\",\"Elapsed\":%.0f,\"Calls\":%d,\"Wait\":%ld.%06ld,\"Max\":%ld.%06ld,\"Min\":%ld.%06ld"
			: "%sSTATS=%d,ID=%s,Elapsed=%.0f,Calls=%d,Wait=%ld.%06ld,Max=%ld.%06ld,Min=%ld.%06ld",
			(isjson && (i > 0)) ? COMMA : BLANK,
			i, id, total_secs, stats->getwork_calls,
			stats->getwork_wait.tv_sec, stats->getwork_wait.tv_usec,
			stats->getwork_wait_max.tv_sec, stats->getwork_wait_max.tv_usec,
			stats->getwork_wait_min.tv_sec, stats->getwork_wait_min.tv_usec);

		strcat(io_buffer, buf);

		if (pool_stats) {
			sprintf(buf, isjson
				? ",\"Pool Calls\":%d,\"Pool Attempts\":%d,\"Pool Wait\":%ld.%06ld,\"Pool Max\":%ld.%06ld,\"Pool Min\":%ld.%06ld,\"Pool Av\":%f"
				: ",Pool Calls=%d,Pool Attempts=%d,Pool Wait=%ld.%06ld,Pool Max=%ld.%06ld,Pool Min=%ld.%06ld,Pool Av=%f",
				pool_stats->getwork_calls, pool_stats->getwork_attempts,
				pool_stats->getwork_wait.tv_sec, pool_stats->getwork_wait.tv_usec,
				pool_stats->getwork_wait_max.tv_sec, pool_stats->getwork_wait_max.tv_usec,
				pool_stats->getwork_wait_min.tv_sec, pool_stats->getwork_wait_min.tv_usec,
				pool_stats->getwork_wait_rolling);

			strcat(io_buffer, buf);
		}

		sprintf(buf, isjson
			? "%s}"
			: "%s" SEPSTR,
			extra);

		strcat(io_buffer, buf);

		i++;
	}

	return i;
}
static void minerstats(__maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson)
{
	char extra[TMPBUFSIZ];
	char id[20];
	int i, j;

	strcpy(io_buffer, message(MSG_MINESTATS, 0, NULL, isjson));

	if (isjson) {
		strcat(io_buffer, COMMA);
		strcat(io_buffer, JSON_MINESTATS);
	}

	i = 0;
	for (j = 0; j < total_devices; j++) {
		struct cgpu_info *cgpu = devices[j];

		extra[0] = '\0';
		if (cgpu->api->get_extra_device_perf_stats) {
			json_t *info = cgpu->api->get_extra_device_perf_stats(cgpu);
			append_kv(extra, info, isjson);
			json_decref(info);
		}

		sprintf(id, "%s%d", cgpu->api->name, cgpu->device_id);
		i = itemstats(i, id, &(cgpu->cgminer_stats), NULL, extra, isjson);
	}

	for (j = 0; j < total_pools; j++) {
		struct pool *pool = pools[j];

		sprintf(id, "POOL%d", j);
		i = itemstats(i, id, &(pool->cgminer_stats), &(pool->cgminer_pool_stats), NULL, isjson);
	}

	if (isjson)
		strcat(io_buffer, JSON_CLOSE);
}

struct CMDS {
	char *name;
	void (*func)(SOCKETTYPE, char *, bool);
	bool requires_writemode;
} cmds[] = {
	{ "version",		apiversion,	false },
	{ "config",		minerconfig,	false },
	{ "devs",		devstatus,	false },
	{ "devdetail",	devdetail,	false },
	{ "pools",		poolstatus,	false },
	{ "summary",		summary,	false },
#ifdef HAVE_OPENCL
	{ "gpuenable",		gpuenable,	true },
	{ "gpudisable",		gpudisable,	true },
	{ "gpurestart",		gpurestart,	true },
	{ "gpu",		gpudev,		false },
#endif
#ifdef HAVE_AN_FPGA
	{ "pga",		pgadev,		false },
	{ "pgaenable",		pgaenable,	true },
	{ "pgadisable",		pgadisable,	true },
#endif
#ifdef WANT_CPUMINE
	{ "cpu",		cpudev,		false },
#endif
	{ "gpucount",		gpucount,	false },
	{ "pgacount",		pgacount,	false },
	{ "cpucount",		cpucount,	false },
	{ "switchpool",		switchpool,	true },
	{ "addpool",		addpool,	true },
	{ "enablepool",		enablepool,	true },
	{ "disablepool",	disablepool,	true },
	{ "removepool",		removepool,	true },
#ifdef HAVE_OPENCL
	{ "gpuintensity",	gpuintensity,	true },
	{ "gpumem",		gpumem,		true },
	{ "gpuengine",		gpuengine,	true },
	{ "gpufan",		gpufan,		true },
	{ "gpuvddc",		gpuvddc,	true },
#endif
	{ "save",		dosave,		true },
	{ "quit",		doquit,		true },
	{ "privileged",		privileged,	true },
	{ "notify",		notify,		false },
	{ "devdetails",		devdetails,	false },
	{ "restart",		dorestart,	true },
	{ "stats",		minerstats,	false },
	{ NULL,			NULL,		false }
};

static void send_result(SOCKETTYPE c, bool isjson)
{
	int n;
	int len;

	if (isjson)
		strcat(io_buffer, JSON_END);

	len = strlen(io_buffer);

	applog(LOG_DEBUG, "API: send reply: (%d) '%.10s%s'", len+1, io_buffer, len > 10 ? "..." : BLANK);

	// ignore failure - it's closed immediately anyway
	n = send(c, io_buffer, len+1, 0);

	if (opt_debug) {
		if (SOCKETFAIL(n))
			applog(LOG_DEBUG, "API: send failed: %s", SOCKERRMSG);
		else
			applog(LOG_DEBUG, "API: sent %d", n);
	}
}

static void tidyup(__maybe_unused void *arg)
{
	mutex_lock(&quit_restart_lock);

	bye = true;

	if (sock != INVSOCK) {
		shutdown(sock, SHUT_RDWR);
		CLOSESOCKET(sock);
		sock = INVSOCK;
	}

	if (ipaccess != NULL) {
		free(ipaccess);
		ipaccess = NULL;
	}

	if (msg_buffer != NULL) {
		free(msg_buffer);
		msg_buffer = NULL;
	}

	if (io_buffer != NULL) {
		free(io_buffer);
		io_buffer = NULL;
	}

	mutex_unlock(&quit_restart_lock);
}

/*
 * Interpret [R|W:]IP[/Prefix][,[R|W:]IP2[/Prefix2][,...]] --api-allow option
 *	special case of 0/0 allows /0 (means all IP addresses)
 */
#define ALLIP4 "0/0"
/*
 * N.B. IP4 addresses are by Definition 32bit big endian on all platforms
 */
static void setup_ipaccess()
{
	char *buf, *ptr, *comma, *slash, *dot;
	int ipcount, mask, octet, i;
	bool writemode;

	buf = malloc(strlen(opt_api_allow) + 1);
	if (unlikely(!buf))
		quit(1, "Failed to malloc ipaccess buf");

	strcpy(buf, opt_api_allow);

	ipcount = 1;
	ptr = buf;
	while (*ptr)
		if (*(ptr++) == ',')
			ipcount++;

	// possibly more than needed, but never less
	ipaccess = calloc(ipcount, sizeof(struct IP4ACCESS));
	if (unlikely(!ipaccess))
		quit(1, "Failed to calloc ipaccess");

	ips = 0;
	ptr = buf;
	while (ptr && *ptr) {
		while (*ptr == ' ' || *ptr == '\t')
			ptr++;

		if (*ptr == ',') {
			ptr++;
			continue;
		}

		comma = strchr(ptr, ',');
		if (comma)
			*(comma++) = '\0';

		writemode = false;

		if (isalpha(*ptr) && *(ptr+1) == ':') {
			if (tolower(*ptr) == 'w')
				writemode = true;

			ptr += 2;
		}

		ipaccess[ips].writemode = writemode;

		if (strcmp(ptr, ALLIP4) == 0)
			ipaccess[ips].ip = ipaccess[ips].mask = 0;
		else {
			slash = strchr(ptr, '/');
			if (!slash)
				ipaccess[ips].mask = 0xffffffff;
			else {
				*(slash++) = '\0';
				mask = atoi(slash);
				if (mask < 1 || mask > 32)
					goto popipo; // skip invalid/zero

				ipaccess[ips].mask = 0;
				while (mask-- >= 0) {
					octet = 1 << (mask % 8);
					ipaccess[ips].mask |= (octet << (8 * (mask >> 3)));
				}
			}

			ipaccess[ips].ip = 0; // missing default to '.0'
			for (i = 0; ptr && (i < 4); i++) {
				dot = strchr(ptr, '.');
				if (dot)
					*(dot++) = '\0';

				octet = atoi(ptr);
				if (octet < 0 || octet > 0xff)
					goto popipo; // skip invalid

				ipaccess[ips].ip |= (octet << (i * 8));

				ptr = dot;
			}

			ipaccess[ips].ip &= ipaccess[ips].mask;
		}

		ips++;
popipo:
		ptr = comma;
	}

	free(buf);
}

static void *quit_thread(__maybe_unused void *userdata)
{
	// allow thread creator to finish whatever it's doing
	mutex_lock(&quit_restart_lock);
	mutex_unlock(&quit_restart_lock);

	if (opt_debug)
		applog(LOG_DEBUG, "API: killing cgminer");

	kill_work();

	return NULL;
}

static void *restart_thread(__maybe_unused void *userdata)
{
	// allow thread creator to finish whatever it's doing
	mutex_lock(&quit_restart_lock);
	mutex_unlock(&quit_restart_lock);

	if (opt_debug)
		applog(LOG_DEBUG, "API: restarting cgminer");

	app_restart();

	return NULL;
}

void api(int api_thr_id)
{
	struct thr_info bye_thr;
	char buf[TMPBUFSIZ];
	char param_buf[TMPBUFSIZ];
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
	bool writemode;
	json_error_t json_err;
	json_t *json_config;
	json_t *json_val;
	bool isjson;
	bool did;
	int i;

	mutex_init(&quit_restart_lock);

	pthread_cleanup_push(tidyup, NULL);
	my_thr_id = api_thr_id;

	/* This should be done first to ensure curl has already called WSAStartup() in windows */
	sleep(opt_log_interval);

	if (!opt_api_listen) {
		applog(LOG_DEBUG, "API not running%s", UNAVAILABLE);
		return;
	}

	if (opt_api_allow) {
		setup_ipaccess();

		if (ips == 0) {
			applog(LOG_WARNING, "API not running (no valid IPs specified)%s", UNAVAILABLE);
			return;
		}
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVSOCK) {
		applog(LOG_ERR, "API1 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
		return;
	}

	memset(&serv, 0, sizeof(serv));

	serv.sin_family = AF_INET;

	if (!opt_api_allow && !opt_api_network) {
		serv.sin_addr.s_addr = inet_addr(localaddr);
		if (serv.sin_addr.s_addr == (in_addr_t)INVINETADDR) {
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
				applog(LOG_WARNING, "API bind to port %d failed - trying again in 30sec", port);
				sleep(30);
			}
		} else
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

	if (opt_api_allow)
		applog(LOG_WARNING, "API running in IP access mode");
	else {
		if (opt_api_network)
			applog(LOG_WARNING, "API running in UNRESTRICTED access mode");
		else
			applog(LOG_WARNING, "API running in local access mode");
	}

	io_buffer = malloc(MYBUFSIZ+1);
	msg_buffer = malloc(MYBUFSIZ+1);

	while (!bye) {
		clisiz = sizeof(cli);
		if (SOCKETFAIL(c = accept(sock, (struct sockaddr *)(&cli), &clisiz))) {
			applog(LOG_ERR, "API failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
			goto die;
		}

		connectaddr = inet_ntoa(cli.sin_addr);

		addrok = false;
		writemode = false;
		if (opt_api_allow) {
			for (i = 0; i < ips; i++) {
				if ((cli.sin_addr.s_addr & ipaccess[i].mask) == ipaccess[i].ip) {
					addrok = true;
					writemode = ipaccess[i].writemode;
					break;
				}
			}
		} else {
			if (opt_api_network)
				addrok = true;
			else
				addrok = (strcmp(connectaddr, localaddr) == 0);
		}

		if (opt_debug)
			applog(LOG_DEBUG, "API: connection from %s - %s", connectaddr, addrok ? "Accepted" : "Ignored");

		if (addrok) {
			n = recv(c, &buf[0], TMPBUFSIZ-1, 0);
			if (SOCKETFAIL(n))
				buf[0] = '\0';
			else
				buf[n] = '\0';

			if (opt_debug) {
				if (SOCKETFAIL(n))
					applog(LOG_DEBUG, "API: recv failed: %s", SOCKERRMSG);
				else
					applog(LOG_DEBUG, "API: recv command: (%d) '%s'", n, buf);
			}

			if (!SOCKETFAIL(n)) {
				// the time of the request in now
				when = time(NULL);

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

#if JANSSON_MAJOR_VERSION > 2 || (JANSSON_MAJOR_VERSION == 2 && JANSSON_MINOR_VERSION > 0)
					json_config = json_loadb(buf, n, 0, &json_err);
#elif JANSSON_MAJOR_VERSION > 1
					json_config = json_loads(buf, 0, &json_err);
#else
					json_config = json_loads(buf, &json_err);
#endif

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
							if (cmds[i].requires_writemode && !writemode) {
								strcpy(io_buffer, message(MSG_ACCDENY, 0, cmds[i].name, isjson));
								applog(LOG_DEBUG, "API: access denied to '%s' for '%s' command", connectaddr, cmds[i].name);
							}
							else
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
	/* Blank line fix for older compilers since pthread_cleanup_pop is a
	 * macro that gets confused by a label existing immediately before it
	 */
	;
	pthread_cleanup_pop(true);

	if (opt_debug)
		applog(LOG_DEBUG, "API: terminating due to: %s",
				do_a_quit ? "QUIT" : (do_a_restart ? "RESTART" : (bye ? "BYE" : "UNKNOWN!")));

	mutex_lock(&quit_restart_lock);

	if (do_a_restart) {
		if (thr_info_create(&bye_thr, NULL, restart_thread, &bye_thr)) {
			mutex_unlock(&quit_restart_lock);
			quit(1, "API failed to initiate a restart - aborting");
		}
		pthread_detach(bye_thr.pth);
	} else if (do_a_quit) {
		if (thr_info_create(&bye_thr, NULL, quit_thread, &bye_thr)) {
			mutex_unlock(&quit_restart_lock);
			quit(1, "API failed to initiate a clean quit - aborting");
		}
		pthread_detach(bye_thr.pth);
	}

	mutex_unlock(&quit_restart_lock);
}
