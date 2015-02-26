/*
 * Copyright 2011-2014 Andrew Smith
 * Copyright 2011-2014 Con Kolivas
 * Copyright 2012-2015 Luke Dashjr
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
#define _MEMORY_DEBUG_MASTER 1

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include <uthash.h>

#include "compat.h"
#include "deviceapi.h"
#ifdef USE_LIBMICROHTTPD
#include "httpsrv.h"
#endif
#include "miner.h"
#include "util.h"
#include "driver-cpu.h" /* for algo_names[], TODO: re-factor dependency */
#include "driver-opencl.h"

#define HAVE_AN_FPGA 1

// Max amount of data to buffer before sending on the socket
#define RPC_SOCKBUFSIZ     0x10000

// BUFSIZ varies on Windows and Linux
#define TMPBUFSIZ	8192

// Number of requests to queue - normally would be small
// However lots of PGA's may mean more
#define QUEUE	100

static const char *UNAVAILABLE = " - API will not be available";
static const char *MUNAVAILABLE = " - API multicast listener will not be available";

static const char *BLANK = "";
static const char *COMMA = ",";
#define COMSTR ","
static const char SEPARATOR = '|';
#define SEPSTR "|"
static const char GPUSEP = ',';
#define CMDJOIN '+'
#define JOIN_CMD "CMD="
#define BETWEEN_JOIN SEPSTR

static const char *APIVERSION = "3.1";
static const char *DEAD = "Dead";
static const char *SICK = "Sick";
static const char *NOSTART = "NoStart";
static const char *INIT = "Initialising";
static const char *WAIT = "Waiting";
static const char *DISABLED = "Disabled";
static const char *ALIVE = "Alive";
static const char *REJECTING = "Rejecting";
static const char *UNKNOWN = "Unknown";
#define _DYNAMIC "D"
#ifdef USE_OPENCL
static const char *DYNAMIC = _DYNAMIC;
#endif

static const char *YES = "Y";
static const char *NO = "N";
static const char *NULLSTR = "(null)";

static const char *TRUESTR = "true";
static const char *FALSESTR = "false";

#ifdef USE_SCRYPT
static const char *SCRYPTSTR = "scrypt";
#endif
#ifdef USE_SHA256D
static const char *SHA256STR = "sha256";
#endif

static const char *OSINFO =
#if defined(__linux)
			"Linux";
#else
#if defined(__APPLE__)
			"Apple";
#else
#if defined (__CYGWIN__)
			"Cygwin";
#elif defined (WIN32)
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
#define _MINECONFIG	"CONFIG"
#define _GPU		"GPU"

#ifdef HAVE_AN_FPGA
#define _PGA		"PGA"
#endif

#ifdef USE_CPUMINING
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
#define _CHECK		"CHECK"
#define _MINECOIN	"COIN"
#define _DEBUGSET	"DEBUG"
#define _SETCONFIG	"SETCONFIG"

static const char ISJSON = '{';
#define JSON0		"{"
#define JSON1		"\""
#define JSON2		"\":["
#define JSON3		"]"
#define JSON4		",\"id\":1"
// If anyone cares, id=0 for truncated output
#define JSON4_TRUNCATED	",\"id\":0"
#define JSON5		"}"

#define JSON_START	JSON0
#define JSON_DEVS	JSON1 _DEVS JSON2
#define JSON_POOLS	JSON1 _POOLS JSON2
#define JSON_SUMMARY	JSON1 _SUMMARY JSON2
#define JSON_STATUS	JSON1 _STATUS JSON2
#define JSON_VERSION	JSON1 _VERSION JSON2
#define JSON_MINECONFIG	JSON1 _MINECONFIG JSON2
#define JSON_GPU	JSON1 _GPU JSON2

#ifdef HAVE_AN_FPGA
#define JSON_PGA	JSON1 _PGA JSON2
#endif

#ifdef USE_CPUMINING
#define JSON_CPU	JSON1 _CPU JSON2
#endif

#define JSON_GPUS	JSON1 _GPUS JSON2
#define JSON_PGAS	JSON1 _PGAS JSON2
#define JSON_CPUS	JSON1 _CPUS JSON2
#define JSON_NOTIFY	JSON1 _NOTIFY JSON2
#define JSON_CLOSE	JSON3
#define JSON_MINESTATS	JSON1 _MINESTATS JSON2
#define JSON_CHECK	JSON1 _CHECK JSON2
#define JSON_DEBUGSET	JSON1 _DEBUGSET JSON2
#define JSON_SETCONFIG	JSON1 _SETCONFIG JSON2
#define JSON_END	JSON4 JSON5
#define JSON_END_TRUNCATED	JSON4_TRUNCATED JSON5
#define JSON_BETWEEN_JOIN	","

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

#ifdef USE_CPUMINING
#define MSG_CPUNON 16
#define MSG_CPUDEV 18
#define MSG_INVCPU 19
#define MSG_ALRENAC 98
#define MSG_ALRDISC 99
#define MSG_CPUMRE 100
#define MSG_CPUREN 101
#define MSG_CPUDIS 102
#define MSG_CPUREI 103
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
#define MSG_MINECONFIG 33
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
#define MSG_PGAREI 0x101
#define MSG_PGAUNW 65
#endif

#define MSG_REMLASTP 66
#define MSG_ACTPOOL 67
#define MSG_REMPOOL 68
#define MSG_DEVDETAILS 69
#define MSG_MINESTATS 70
#define MSG_MISCHK 71
#define MSG_CHECK 72
#define MSG_POOLPRIO 73
#define MSG_DUPPID 74
#define MSG_MISBOOL 75
#define MSG_INVBOOL 76
#define MSG_FOO 77
#define MSG_MINECOIN 78
#define MSG_DEBUGSET 79
#define MSG_PGAIDENT 80
#define MSG_PGANOID 81

#define MSG_SETCONFIG 82
#define MSG_UNKCON 83
#define MSG_INVNUM 84
#define MSG_CONPAR 85
#define MSG_CONVAL 86

#ifdef HAVE_AN_FPGA
#define MSG_MISPGAOPT 89
#define MSG_PGANOSET 90
#define MSG_PGAHELP 91
#define MSG_PGASETOK 92
#define MSG_PGASETERR 93
#endif

#define MSG_ZERMIS 94
#define MSG_ZERINV 95
#define MSG_ZERSUM 96
#define MSG_ZERNOSUM 97

#define MSG_DEVSCAN 0x100
#define MSG_BYE 0x101

#define MSG_INVNEG 121
#define MSG_SETQUOTA 122

#define USE_ALTMSG 0x4000

enum code_severity {
	SEVERITY_ERR,
	SEVERITY_WARN,
	SEVERITY_INFO,
	SEVERITY_SUCC,
	SEVERITY_FAIL
};

enum code_parameters {
	PARAM_COUNT,
	PARAM_GPU,
	PARAM_PGA,
	PARAM_CPU,
	PARAM_PID,
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
	PARAM_BOOL,
	PARAM_SET,
	PARAM_NONE
};

struct CODES {
	const enum code_severity severity;
	const int code;
	const enum code_parameters params;
	const char *description;
} codes[] = {
#ifdef USE_OPENCL
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
						"%d PGA(s)"
 },

 { SEVERITY_ERR,   MSG_NODEVS,	PARAM_NONE,	"No PGAs"
 },

 { SEVERITY_SUCC,  MSG_SUMM,	PARAM_NONE,	"Summary" },
#ifdef USE_OPENCL
 { SEVERITY_INFO,  MSG_GPUDIS,	PARAM_GPU,	"GPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_GPUREI,	PARAM_GPU,	"GPU %d restart attempted" },
#endif
 { SEVERITY_ERR,   MSG_INVCMD,	PARAM_NONE,	"Invalid command" },
 { SEVERITY_ERR,   MSG_MISID,	PARAM_NONE,	"Missing device id parameter" },
#ifdef USE_OPENCL
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
#ifdef USE_CPUMINING
 { SEVERITY_ERR,   MSG_CPUNON,	PARAM_NONE,	"No CPUs" },
 { SEVERITY_SUCC,  MSG_CPUDEV,	PARAM_CPU,	"CPU%d" },
 { SEVERITY_ERR,   MSG_INVCPU,	PARAM_CPUMAX,	"Invalid CPU id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_ALRENAC,	PARAM_CPU,	"CPU %d already enabled" },
 { SEVERITY_INFO,  MSG_ALRDISC,	PARAM_CPU,	"CPU %d already disabled" },
 { SEVERITY_WARN,  MSG_CPUMRE,	PARAM_CPU,	"CPU %d must be restarted first" },
 { SEVERITY_INFO,  MSG_CPUREN,	PARAM_CPU,	"CPU %d sent enable message" },
 { SEVERITY_INFO,  MSG_CPUDIS,	PARAM_CPU,	"CPU %d set disable flag" },
 { SEVERITY_INFO,  MSG_CPUREI,	PARAM_CPU,	"CPU %d restart attempted" },
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
 { SEVERITY_ERR,   MSG_INVINT,	PARAM_STR,	"Invalid intensity (%s) - must be '" _DYNAMIC  "' or range -10 - 31" },
 { SEVERITY_INFO,  MSG_GPUINT,	PARAM_BOTH,	"GPU %d set new intensity to %s" },
 { SEVERITY_SUCC,  MSG_MINECONFIG,PARAM_NONE,	"BFGMiner config" },
#ifdef USE_OPENCL
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
 { SEVERITY_SUCC,  MSG_POOLPRIO,PARAM_NONE,	"Changed pool priorities" },
 { SEVERITY_ERR,   MSG_DUPPID,	PARAM_PID,	"Duplicate pool specified %d" },
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
 { SEVERITY_SUCC,  MSG_MINESTATS,PARAM_NONE,	"BFGMiner stats" },
 { SEVERITY_ERR,   MSG_MISCHK,	PARAM_NONE,	"Missing check cmd" },
 { SEVERITY_SUCC,  MSG_CHECK,	PARAM_NONE,	"Check command" },
 { SEVERITY_ERR,   MSG_MISBOOL,	PARAM_NONE,	"Missing parameter: true/false" },
 { SEVERITY_ERR,   MSG_INVBOOL,	PARAM_NONE,	"Invalid parameter should be true or false" },
 { SEVERITY_SUCC,  MSG_FOO,	PARAM_BOOL,	"Failover-Only set to %s" },
 { SEVERITY_SUCC,  MSG_MINECOIN,PARAM_NONE,	"BFGMiner coin" },
 { SEVERITY_SUCC,  MSG_DEBUGSET,PARAM_NONE,	"Debug settings" },
#ifdef HAVE_AN_FPGA
 { SEVERITY_SUCC,  MSG_PGAIDENT,PARAM_PGA,	"Identify command sent to PGA%d" },
 { SEVERITY_WARN,  MSG_PGANOID,	PARAM_PGA,	"PGA%d does not support identify" },
#endif
 { SEVERITY_SUCC,  MSG_SETCONFIG,PARAM_SET,	"Set config '%s' to %d" },
 { SEVERITY_ERR,   MSG_UNKCON,	PARAM_STR,	"Unknown config '%s'" },
 { SEVERITY_ERR,   MSG_INVNUM,	PARAM_BOTH,	"Invalid number (%d) for '%s' range is 0-9999" },
 { SEVERITY_ERR,   MSG_INVNEG,	PARAM_BOTH,	"Invalid negative number (%d) for '%s'" },
 { SEVERITY_SUCC,  MSG_SETQUOTA,PARAM_SET,	"Set pool '%s' to quota %d'" },
 { SEVERITY_ERR,   MSG_CONPAR,	PARAM_NONE,	"Missing config parameters 'name,N'" },
 { SEVERITY_ERR,   MSG_CONVAL,	PARAM_STR,	"Missing config value N for '%s,N'" },
#ifdef HAVE_AN_FPGA
 { SEVERITY_ERR,   MSG_MISPGAOPT, PARAM_NONE,	"Missing option after PGA number" },
 { SEVERITY_WARN,  MSG_PGANOSET, PARAM_PGA,	"PGA %d does not support pgaset" },
 { SEVERITY_INFO,  MSG_PGAHELP, PARAM_BOTH,	"PGA %d set help: %s" },
 { SEVERITY_SUCC,  MSG_PGASETOK, PARAM_PGA,	"PGA %d set OK" },
 { SEVERITY_SUCC,  MSG_PGASETOK | USE_ALTMSG, PARAM_BOTH,	"PGA %d set OK: %s" },
 { SEVERITY_ERR,   MSG_PGASETERR, PARAM_BOTH,	"PGA %d set failed: %s" },
#endif
 { SEVERITY_ERR,   MSG_ZERMIS,	PARAM_NONE,	"Missing zero parameters" },
 { SEVERITY_ERR,   MSG_ZERINV,	PARAM_STR,	"Invalid zero parameter '%s'" },
 { SEVERITY_SUCC,  MSG_ZERSUM,	PARAM_STR,	"Zeroed %s stats with summary" },
 { SEVERITY_SUCC,  MSG_ZERNOSUM, PARAM_STR,	"Zeroed %s stats without summary" },
 { SEVERITY_SUCC,  MSG_DEVSCAN, PARAM_COUNT,	"Added %d new device(s)" },
 { SEVERITY_SUCC,  MSG_BYE,		PARAM_STR,	"%s" },
 { SEVERITY_FAIL, 0, 0, NULL }
};

static const char *localaddr = "127.0.0.1";

static int my_thr_id = 0;
static bool bye;

// Used to control quit restart access to shutdown variables
static pthread_mutex_t quit_restart_lock;

static bool do_a_quit;
static bool do_a_restart;

static time_t when = 0;	// when the request occurred
static bool per_proc;

struct IP4ACCESS {
	in_addr_t ip;
	in_addr_t mask;
	char group;
};

#define GROUP(g) (toupper(g))
#define PRIVGROUP GROUP('W')
#define NOPRIVGROUP GROUP('R')
#define ISPRIVGROUP(g) (GROUP(g) == PRIVGROUP)
#define GROUPOFFSET(g) (GROUP(g) - GROUP('A'))
#define VALIDGROUP(g) (GROUP(g) >= GROUP('A') && GROUP(g) <= GROUP('Z'))
#define COMMANDS(g) (apigroups[GROUPOFFSET(g)].commands)
#define DEFINEDGROUP(g) (ISPRIVGROUP(g) || COMMANDS(g) != NULL)

struct APIGROUPS {
	// This becomes a string like: "|cmd1|cmd2|cmd3|" so it's quick to search
	char *commands;
} apigroups['Z' - 'A' + 1]; // only A=0 to Z=25 (R: noprivs, W: allprivs)

static struct IP4ACCESS *ipaccess = NULL;
static int ips = 0;

struct io_data {
	bytes_t data;
	SOCKETTYPE sock;
	
	// Whether to add various things
	bool close;
};
static struct io_data *rpc_io_data;

static void io_reinit(struct io_data *io_data)
{
	bytes_reset(&io_data->data);
	io_data->close = false;
}

static
struct io_data *sock_io_new()
{
	struct io_data *io_data = malloc(sizeof(struct io_data));
	bytes_init(&io_data->data);
	io_data->sock = INVSOCK;
	io_reinit(io_data);
	return io_data;
}

static
size_t io_flush(struct io_data *io_data, bool complete)
{
	size_t sent = 0, tosend = bytes_len(&io_data->data);
	ssize_t n;
	struct timeval timeout = {0, complete ? 50000: 0}, tv;
	fd_set wd;
	int count = 0;
	
	while (tosend)
	{
		FD_ZERO(&wd);
		FD_SET(io_data->sock, &wd);
		tv = timeout;
		if (select(io_data->sock + 1, NULL, &wd, NULL, &tv) < 1)
			break;
		
		n = send(io_data->sock, (void*)&bytes_buf(&io_data->data)[sent], tosend, 0);
		if (SOCKETFAIL(n))
		{
			if (!sock_blocks())
				applog(LOG_WARNING, "API: send (%lu) failed: %s", (unsigned long)tosend, SOCKERRMSG);
			break;
		}
		if (count <= 1)
		{
			if (n == tosend)
				applog(LOG_DEBUG, "API: sent all of %lu first go", (unsigned long)tosend);
			else
				applog(LOG_DEBUG, "API: sent %ld of %lu first go", (long)n, (unsigned long)tosend);
		}
		else
		{
			if (n == tosend)
				applog(LOG_DEBUG, "API: sent all of remaining %lu (count=%d)", (unsigned long)tosend, count);
			else
				applog(LOG_DEBUG, "API: sent %ld of remaining %lu (count=%d)", (long)n, (unsigned long)tosend, count);
		}
		sent += n;
		tosend -= n;
	}
	
	bytes_shift(&io_data->data, sent);
	
	return sent;
}

static bool io_add(struct io_data *io_data, char *buf)
{
	size_t len = strlen(buf);
	if (bytes_len(&io_data->data) + len > RPC_SOCKBUFSIZ)
		io_flush(io_data, false);
	bytes_append(&io_data->data, buf, len);
	return true;
}

static void io_close(struct io_data *io_data)
{
	io_data->close = true;
}

static void io_free()
{
	bytes_free(&rpc_io_data->data);
	free(rpc_io_data);
	rpc_io_data = NULL;
}

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

static struct api_data *api_add_extra(struct api_data *root, struct api_data *extra)
{
	struct api_data *tmp;

	if (root) {
		if (extra) {
			// extra tail
			tmp = extra->prev;

			// extra prev = root tail
			extra->prev = root->prev;

			// root tail next = extra
			root->prev->next = extra;

			// extra tail next = root
			tmp->next = root;

			// root prev = extra tail
			root->prev = tmp;
		}
	} else
		root = extra;

	return root;
}

static
struct api_data *api_add_data_full(struct api_data *root, const char * const name, enum api_data_type type, const void *data, bool copy_data)
{
	struct api_data *api_data;

	api_data = (struct api_data *)malloc(sizeof(struct api_data));

	api_data->name = strdup(name);
	api_data->type = type;

	if (root == NULL) {
		root = api_data;
		root->prev = root;
		root->next = root;
	}
	else {
		api_data->prev = root->prev;
		root->prev = api_data;
		api_data->next = root;
		api_data->prev->next = api_data;
	}

	api_data->data_was_malloc = copy_data;

	// Avoid crashing on bad data
	if (data == NULL) {
		api_data->type = type = API_CONST;
		data = NULLSTR;
		api_data->data_was_malloc = copy_data = false;
	}

	if (!copy_data)
	{
		api_data->data = data;
		if (type == API_JSON)
			json_incref((json_t *)data);
	}
	else
	{
		size_t datalen = 0;
		switch(type) {
			case API_ESCAPE:
			case API_STRING:
			case API_CONST:
				datalen = strlen(data) + 1;
				break;
			case API_UINT8:
				datalen = sizeof(uint8_t);
				break;
			case API_INT16:
				datalen = sizeof(int16_t);
				break;
			case API_UINT16:
				datalen = sizeof(uint16_t);
				break;
			case API_INT:
				datalen = sizeof(int);
				break;
			case API_UINT:
				datalen = sizeof(unsigned int);
				break;
			case API_UINT32:
				datalen = sizeof(uint32_t);
				break;
			case API_UINT64:
				datalen = sizeof(uint64_t);
				break;
			case API_DOUBLE:
			case API_ELAPSED:
			case API_MHS:
			case API_MHTOTAL:
			case API_UTILITY:
			case API_FREQ:
			case API_HS:
			case API_DIFF:
			case API_PERCENT:
				datalen = sizeof(double);
				break;
			case API_BOOL:
				datalen = sizeof(bool);
				break;
			case API_TIMEVAL:
				datalen = sizeof(struct timeval);
				break;
			case API_TIME:
				datalen = sizeof(time_t);
				break;
			case API_VOLTS:
			case API_TEMP:
				datalen = sizeof(float);
				break;
			case API_JSON:
				api_data->data_was_malloc = false;
				api_data->data = json_deep_copy((json_t *)data);
				break;
			default:
				applog(LOG_ERR, "API: unknown1 data type %d ignored", type);
				api_data->type = API_STRING;
				api_data->data_was_malloc = false;
				api_data->data = UNKNOWN;
				break;
		}
		if (datalen)
		{
			void * const copied_data = malloc(datalen);
			memcpy(copied_data, data, datalen);
			api_data->data = copied_data;
		}
	}

	return root;
}

struct api_data *api_add_escape(struct api_data * const root, const char * const name, const char * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_ESCAPE, data, copy_data);
}

struct api_data *api_add_string(struct api_data * const root, const char * const name, const char * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_STRING, data, copy_data);
}

struct api_data *api_add_const(struct api_data * const root, const char * const name, const char * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_CONST, data, copy_data);
}

struct api_data *api_add_uint8(struct api_data * const root, const char * const name, const uint8_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_UINT8, data, copy_data);
}

struct api_data *api_add_int16(struct api_data * const root, const char * const name, const uint16_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_INT16, data, copy_data);
}

struct api_data *api_add_uint16(struct api_data * const root, const char * const name, const uint16_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_UINT16, data, copy_data);
}

struct api_data *api_add_int(struct api_data * const root, const char * const name, const int * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_INT, data, copy_data);
}

struct api_data *api_add_uint(struct api_data * const root, const char * const name, const unsigned int * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_UINT, data, copy_data);
}

struct api_data *api_add_uint32(struct api_data * const root, const char * const name, const uint32_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_UINT32, data, copy_data);
}

struct api_data *api_add_uint64(struct api_data * const root, const char * const name, const uint64_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_UINT64, data, copy_data);
}

struct api_data *api_add_double(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_DOUBLE, data, copy_data);
}

struct api_data *api_add_elapsed(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_ELAPSED, data, copy_data);
}

struct api_data *api_add_bool(struct api_data * const root, const char * const name, const bool * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_BOOL, data, copy_data);
}

struct api_data *api_add_timeval(struct api_data * const root, const char * const name, const struct timeval * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_TIMEVAL, data, copy_data);
}

struct api_data *api_add_time(struct api_data * const root, const char * const name, const time_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_TIME, data, copy_data);
}

struct api_data *api_add_mhs(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_MHS, data, copy_data);
}

struct api_data *api_add_mhtotal(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_MHTOTAL, data, copy_data);
}

struct api_data *api_add_temp(struct api_data * const root, const char * const name, const float * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_TEMP, data, copy_data);
}

struct api_data *api_add_utility(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_UTILITY, data, copy_data);
}

struct api_data *api_add_freq(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_FREQ, data, copy_data);
}

struct api_data *api_add_volts(struct api_data * const root, const char * const name, const float * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_VOLTS, data, copy_data);
}

struct api_data *api_add_hs(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_HS, data, copy_data);
}

struct api_data *api_add_diff(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_DIFF, data, copy_data);
}

// json_t is not const since we generally increase the refcount
struct api_data *api_add_json(struct api_data * const root, const char * const name, json_t * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_JSON, data, copy_data);
}

struct api_data *api_add_percent(struct api_data * const root, const char * const name, const double * const data, const bool copy_data)
{
	return api_add_data_full(root, name, API_PERCENT, data, copy_data);
}

static struct api_data *print_data(struct api_data *root, char *buf, bool isjson, bool precom)
{
	struct api_data *tmp;
	bool first = true;
	char *original, *escape;
	char *quote;

	*buf = '\0';

	if (precom) {
		*(buf++) = *COMMA;
		*buf = '\0';
	}

	if (isjson) {
		strcpy(buf, JSON0);
		buf = strchr(buf, '\0');
		quote = JSON1;
	} else
		quote = (char *)BLANK;

	while (root) {
		if (!first)
			*(buf++) = *COMMA;
		else
			first = false;

		sprintf(buf, "%s%s%s%s", quote, root->name, quote, isjson ? ":" : "=");

		buf = strchr(buf, '\0');

		switch(root->type) {
			case API_STRING:
			case API_CONST:
				sprintf(buf, "%s%s%s", quote, (char *)(root->data), quote);
				break;
			case API_ESCAPE:
				original = (char *)(root->data);
				escape = escape_string((char *)(root->data), isjson);
				sprintf(buf, "%s%s%s", quote, escape, quote);
				if (escape != original)
					free(escape);
				break;
			case API_UINT8:
				sprintf(buf, "%u", *(uint8_t *)root->data);
				break;
			case API_INT16:
				sprintf(buf, "%d", *(int16_t *)root->data);
				break;
			case API_UINT16:
				sprintf(buf, "%u", *(uint16_t *)root->data);
				break;
			case API_INT:
				sprintf(buf, "%d", *((int *)(root->data)));
				break;
			case API_UINT:
				sprintf(buf, "%u", *((unsigned int *)(root->data)));
				break;
			case API_UINT32:
				sprintf(buf, "%"PRIu32, *((uint32_t *)(root->data)));
				break;
			case API_UINT64:
				sprintf(buf, "%"PRIu64, *((uint64_t *)(root->data)));
				break;
			case API_TIME:
				sprintf(buf, "%lu", *((unsigned long *)(root->data)));
				break;
			case API_DOUBLE:
				sprintf(buf, "%f", *((double *)(root->data)));
				break;
			case API_ELAPSED:
				sprintf(buf, "%.0f", *((double *)(root->data)));
				break;
			case API_UTILITY:
			case API_FREQ:
			case API_MHS:
				sprintf(buf, "%.3f", *((double *)(root->data)));
				break;
			case API_VOLTS:
				sprintf(buf, "%.3f", *((float *)(root->data)));
				break;
			case API_MHTOTAL:
				sprintf(buf, "%.4f", *((double *)(root->data)));
				break;
			case API_HS:
				sprintf(buf, "%.15f", *((double *)(root->data)));
				break;
			case API_DIFF:
			{
				const double *fp = root->data;
				if (fmod(*fp, 1.))
					sprintf(buf, "%.8f", *fp);
				else
					sprintf(buf, "%.0f", *fp);
				break;
			}
			case API_BOOL:
				sprintf(buf, "%s", *((bool *)(root->data)) ? TRUESTR : FALSESTR);
				break;
			case API_TIMEVAL:
				sprintf(buf, "%"PRIu64".%06lu",
					(uint64_t)((struct timeval *)(root->data))->tv_sec,
					(unsigned long)((struct timeval *)(root->data))->tv_usec);
				break;
			case API_TEMP:
				sprintf(buf, "%.2f", *((float *)(root->data)));
				break;
			case API_JSON:
				escape = json_dumps((json_t *)(root->data), JSON_COMPACT);
				strcpy(buf, escape);
				free(escape);
				break;
			case API_PERCENT:
				sprintf(buf, "%.4f", *((double *)(root->data)) * 100.0);
				break;
			default:
				applog(LOG_ERR, "API: unknown2 data type %d ignored", root->type);
				sprintf(buf, "%s%s%s", quote, UNKNOWN, quote);
				break;
		}

		buf = strchr(buf, '\0');

		free(root->name);
		if (root->type == API_JSON)
			json_decref((json_t *)root->data);
		if (root->data_was_malloc)
			free((void*)root->data);

		if (root->next == root) {
			free(root);
			root = NULL;
		} else {
			tmp = root;
			root = tmp->next;
			root->prev = tmp->prev;
			root->prev->next = root;
			free(tmp);
		}
	}

	strcpy(buf, isjson ? JSON5 : SEPSTR);

	return root;
}

#ifdef HAVE_AN_FPGA
static int numpgas()
{
	int count = 0;
	int i;

	rd_lock(&devices_lock);
	for (i = 0; i < total_devices; i++) {
		if (devices[i]->device != devices[i] && !per_proc)
			continue;
		++count;
	}
	rd_unlock(&devices_lock);
	return count;
}

static int pgadevice(int pgaid)
{
	int count = 0;
	int i;

	rd_lock(&devices_lock);
	for (i = 0; i < total_devices; i++) {
		if (devices[i]->device != devices[i] && !per_proc)
			continue;
		++count;
		if (count == (pgaid + 1))
			goto foundit;
	}

	rd_unlock(&devices_lock);
	return -1;

foundit:

	rd_unlock(&devices_lock);
	return i;
}
#endif

// All replies (except BYE and RESTART) start with a message
//  thus for JSON, message() inserts JSON_START at the front
//  and send_result() adds JSON_END at the end
static void message(struct io_data * const io_data, const int messageid2, const int paramid, const char * const param2, const bool isjson)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	char buf2[TMPBUFSIZ];
	char severity[2];
#ifdef HAVE_AN_FPGA
	int pga;
#endif
#ifdef USE_CPUMINING
	int cpu;
#endif
	int i;
	int messageid = messageid2 & ~USE_ALTMSG;

	if (isjson)
		io_add(io_data, JSON_START JSON_STATUS);

	for (i = 0; codes[i].severity != SEVERITY_FAIL; i++) {
		if (codes[i].code == messageid2) {
			switch (codes[i].severity) {
				case SEVERITY_WARN:
					severity[0] = 'W';
					break;
				case SEVERITY_INFO:
					severity[0] = 'I';
					break;
				case SEVERITY_SUCC:
					severity[0] = 'S';
					break;
				case SEVERITY_ERR:
				default:
					severity[0] = 'E';
					break;
			}
			severity[1] = '\0';

			switch(codes[i].params) {
				case PARAM_COUNT:
				case PARAM_GPU:
				case PARAM_PGA:
				case PARAM_CPU:
				case PARAM_PID:
					sprintf(buf, codes[i].description, paramid);
					break;
				case PARAM_POOL:
					sprintf(buf, codes[i].description, paramid, pools[paramid]->rpc_url);
					break;
#ifdef USE_OPENCL
				case PARAM_GPUMAX:
					sprintf(buf, codes[i].description, paramid, nDevs - 1);
					break;
#endif
#ifdef HAVE_AN_FPGA
				case PARAM_PGAMAX:
					pga = numpgas();
					sprintf(buf, codes[i].description, paramid, pga - 1);
					break;
#endif
#ifdef USE_CPUMINING
				case PARAM_CPUMAX:
					if (opt_n_threads > 0)
						cpu = num_processors;
					else
						cpu = 0;
					sprintf(buf, codes[i].description, paramid, cpu - 1);
					break;
#endif
				case PARAM_PMAX:
					sprintf(buf, codes[i].description, total_pools);
					break;
				case PARAM_POOLMAX:
					sprintf(buf, codes[i].description, paramid, total_pools - 1);
					break;
				case PARAM_DMAX:
					pga = numpgas();

					sprintf(buf, codes[i].description
						, pga
						);
					break;
				case PARAM_CMD:
					sprintf(buf, codes[i].description, JSON_COMMAND);
					break;
				case PARAM_STR:
					sprintf(buf, codes[i].description, param2);
					break;
				case PARAM_BOTH:
					sprintf(buf, codes[i].description, paramid, param2);
					break;
				case PARAM_BOOL:
					sprintf(buf, codes[i].description, paramid ? TRUESTR : FALSESTR);
					break;
				case PARAM_SET:
					sprintf(buf, codes[i].description, param2, paramid);
					break;
				case PARAM_NONE:
				default:
					strcpy(buf, codes[i].description);
			}

			root = api_add_string(root, _STATUS, severity, false);
			root = api_add_time(root, "When", &when, false);
			root = api_add_int(root, "Code", &messageid, false);
			root = api_add_escape(root, "Msg", buf, false);
			root = api_add_escape(root, "Description", opt_api_description, false);

			root = print_data(root, buf2, isjson, false);
			io_add(io_data, buf2);
			if (isjson)
				io_add(io_data, JSON_CLOSE);
			return;
		}
	}

	root = api_add_string(root, _STATUS, "F", false);
	root = api_add_time(root, "When", &when, false);
	int id = -1;
	root = api_add_int(root, "Code", &id, false);
	sprintf(buf, "%d", messageid);
	root = api_add_escape(root, "Msg", buf, false);
	root = api_add_escape(root, "Description", opt_api_description, false);

	root = print_data(root, buf2, isjson, false);
	io_add(io_data, buf2);
	if (isjson)
		io_add(io_data, JSON_CLOSE);
}

static void apiversion(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;

	message(io_data, MSG_VERSION, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_VERSION : _VERSION COMSTR);

	root = api_add_string(root, "Miner", bfgminer_name_space_ver, false);
	root = api_add_string(root, "CGMiner", bfgminer_ver, false);
	root = api_add_const(root, "API", APIVERSION, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

static void minerconfig(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	struct driver_registration *reg, *regtmp;
	int pgacount = 0;
	char *adlinuse = (char *)NO;
	int i;
#ifdef HAVE_ADL
	const char *adl = YES;

	for (i = 0; i < nDevs; i++) {
		struct opencl_device_data * const data = gpus[i].device_data;
		if (data->has_adl) {
			adlinuse = (char *)YES;
			break;
		}
	}
#else
	const char *adl = NO;
#endif

#ifdef HAVE_AN_FPGA
	pgacount = numpgas();
#endif

	message(io_data, MSG_MINECONFIG, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_MINECONFIG : _MINECONFIG COMSTR);

	root = api_add_int(root, "PGA Count", &pgacount, false);
	root = api_add_int(root, "Pool Count", &total_pools, false);
	root = api_add_const(root, "ADL", (char *)adl, false);
	root = api_add_string(root, "ADL in use", adlinuse, false);
	root = api_add_const(root, "Strategy", strategies[pool_strategy].s, false);
	root = api_add_int(root, "Log Interval", &opt_log_interval, false);
	
	strcpy(buf, ""
#ifdef USE_LIBMICROHTTPD
			" SGW"
#endif
#ifdef USE_LIBEVENT
			" SSM"
#endif
	);

	BFG_FOREACH_DRIVER_BY_DNAME(reg, regtmp)
	{
		const struct device_drv * const drv = reg->drv;
		tailsprintf(buf, sizeof(buf), " %s", drv->name);
	}
	root = api_add_const(root, "Device Code", &buf[1], true);
	
	root = api_add_const(root, "OS", OSINFO, false);
	root = api_add_bool(root, "Failover-Only", &opt_fail_only, false);
	root = api_add_int(root, "ScanTime", &opt_scantime, false);
	root = api_add_int(root, "Queue", &opt_queue, false);
	root = api_add_int(root, "Expiry", &opt_expiry, false);
#if BLKMAKER_VERSION > 0
	root = api_add_string(root, "Coinbase-Sig", opt_coinbase_sig, true);
#endif
	
	struct bfg_loaded_configfile *configfile;
	i = 0;
	LL_FOREACH(bfg_loaded_configfiles, configfile)
	{
		snprintf(buf, sizeof(buf), "ConfigFile%d", i++);
		root = api_add_string(root, buf, configfile->filename, false);
	}

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

static const char*
bool2str(bool b)
{
	return b ? YES : NO;
}

static const char *status2str(enum alive status)
{
	switch (status) {
		case LIFE_WELL:
			return ALIVE;
		case LIFE_SICK:
			return SICK;
		case LIFE_DEAD:
		case LIFE_DEAD2:
			return DEAD;
		case LIFE_NOSTART:
			return NOSTART;
		case LIFE_INIT:
		case LIFE_INIT2:
			return INIT;
		case LIFE_WAIT:
			return WAIT;
		case LIFE_MIXED:
			return "Mixed";
		default:
			return UNKNOWN;
	}
}

static
struct api_data *api_add_device_identifier(struct api_data *root, struct cgpu_info *cgpu)
{
	root = api_add_string(root, "Name", cgpu->drv->name, false);
	root = api_add_int(root, "ID", &(cgpu->device_id), false);
	if (per_proc)
		root = api_add_int(root, "ProcID", &(cgpu->proc_id), false);
	return root;
}

static
int find_index_by_cgpu(struct cgpu_info *cgpu)
{
	if (per_proc)
		return cgpu->cgminer_id;
	
	int n = 0, i;
	
	// Quickly traverse the devices array backward until we reach the 0th device, counting as we go
	rd_lock(&devices_lock);
	while (true)
	{
		i = cgpu->device->cgminer_id;
		if (!i)
			break;
		cgpu = devices[--i];
		++n;
	}
	rd_unlock(&devices_lock);
	return n;
}

static void devdetail_an(struct io_data *io_data, struct cgpu_info *cgpu, bool isjson, bool precom)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	int n;

	cgpu_utility(cgpu);

	n = find_index_by_cgpu(cgpu);

	root = api_add_int(root, "DEVDETAILS", &n, true);
	root = api_add_device_identifier(root, cgpu);
	if (!per_proc)
		root = api_add_int(root, "Processors", &cgpu->procs, false);
	root = api_add_string(root, "Driver", cgpu->drv->dname, false);
	if (cgpu->kname)
		root = api_add_string(root, "Kernel", cgpu->kname, false);
	if (cgpu->name)
		root = api_add_string(root, "Model", cgpu->name, false);
	if (cgpu->dev_manufacturer)
		root = api_add_string(root, "Manufacturer", cgpu->dev_manufacturer, false);
	if (cgpu->dev_product)
		root = api_add_string(root, "Product", cgpu->dev_product, false);
	if (cgpu->dev_serial)
		root = api_add_string(root, "Serial", cgpu->dev_serial, false);
	if (cgpu->device_path)
		root = api_add_string(root, "Device Path", cgpu->device_path, false);
	
	root = api_add_int(root, "Target Temperature", &cgpu->targettemp, false);
	root = api_add_int(root, "Cutoff Temperature", &cgpu->cutofftemp, false);

	if ((per_proc || cgpu->procs <= 1) && cgpu->drv->get_api_extra_device_detail)
		root = api_add_extra(root, cgpu->drv->get_api_extra_device_detail(cgpu));

	root = print_data(root, buf, isjson, precom);
	io_add(io_data, buf);
}

static
void devstatus_an(struct io_data *io_data, struct cgpu_info *cgpu, bool isjson, bool precom)
{
	struct cgpu_info *proc;
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	int n;

	n = find_index_by_cgpu(cgpu);

	double runtime = cgpu_runtime(cgpu);
	bool enabled = false;
	double total_mhashes = 0, rolling = 0, utility = 0;
	enum alive status = cgpu->status;
	float temp = -1;
	int accepted = 0, rejected = 0, stale = 0, hw_errors = 0;
	double diff1 = 0, bad_diff1 = 0;
	double diff_accepted = 0, diff_rejected = 0, diff_stale = 0;
	int last_share_pool = -1;
	time_t last_share_pool_time = -1, last_device_valid_work = -1;
	double last_share_diff = -1;
	int procs = per_proc ? 1 : cgpu->procs, i;
	for (i = 0, proc = cgpu; i < procs; ++i, proc = proc->next_proc)
	{
		cgpu_utility(proc);
		if (proc->deven != DEV_DISABLED)
			enabled = true;
		total_mhashes += proc->total_mhashes;
		rolling += proc->drv->get_proc_rolling_hashrate ? proc->drv->get_proc_rolling_hashrate(proc) : proc->rolling;
		utility += proc->utility;
		accepted += proc->accepted;
		rejected += proc->rejected;
		stale += proc->stale;
		hw_errors += proc->hw_errors;
		diff1 += proc->diff1;
		diff_accepted += proc->diff_accepted;
		diff_rejected += proc->diff_rejected;
		diff_stale += proc->diff_stale;
		bad_diff1 += proc->bad_diff1;
		if (status != proc->status)
			status = LIFE_MIXED;
		if (proc->temp > temp)
			temp = proc->temp;
		if (proc->last_share_pool_time > last_share_pool_time)
		{
			last_share_pool_time = proc->last_share_pool_time;
			last_share_pool = proc->last_share_pool;
			last_share_diff = proc->last_share_diff;
		}
		if (proc->last_device_valid_work > last_device_valid_work)
			last_device_valid_work = proc->last_device_valid_work;
		if (per_proc)
			break;
	}

	root = api_add_int(root, "PGA", &n, true);
	root = api_add_device_identifier(root, cgpu);
	root = api_add_string(root, "Enabled", bool2str(enabled), false);
	root = api_add_string(root, "Status", status2str(status), false);
	if (temp > 0)
		root = api_add_temp(root, "Temperature", &temp, false);
	
	root = api_add_elapsed(root, "Device Elapsed", &runtime, false);
	double mhs = total_mhashes / runtime;
	root = api_add_mhs(root, "MHS av", &mhs, false);
	char mhsname[27];
	sprintf(mhsname, "MHS %ds", opt_log_interval);
	root = api_add_mhs(root, mhsname, &rolling, false);
	root = api_add_mhs(root, "MHS rolling", &rolling, false);
	root = api_add_int(root, "Accepted", &accepted, false);
	root = api_add_int(root, "Rejected", &rejected, false);
	root = api_add_int(root, "Hardware Errors", &hw_errors, false);
	root = api_add_utility(root, "Utility", &utility, false);
	root = api_add_int(root, "Stale", &stale, false);
	if (last_share_pool != -1)
	{
		root = api_add_int(root, "Last Share Pool", &last_share_pool, false);
		root = api_add_time(root, "Last Share Time", &last_share_pool_time, false);
	}
	root = api_add_mhtotal(root, "Total MH", &total_mhashes, false);
	double work_utility = diff1 / runtime * 60;
	root = api_add_diff(root, "Diff1 Work", &diff1, false);
	root = api_add_utility(root, "Work Utility", &work_utility, false);
	root = api_add_diff(root, "Difficulty Accepted", &diff_accepted, false);
	root = api_add_diff(root, "Difficulty Rejected", &diff_rejected, false);
	root = api_add_diff(root, "Difficulty Stale", &diff_stale, false);
	if (last_share_diff > 0)
		root = api_add_diff(root, "Last Share Difficulty", &last_share_diff, false);
	if (last_device_valid_work != -1)
		root = api_add_time(root, "Last Valid Work", &last_device_valid_work, false);
	double hwp = (bad_diff1 + diff1) ?
			(double)(bad_diff1) / (double)(bad_diff1 + diff1) : 0;
	root = api_add_percent(root, "Device Hardware%", &hwp, false);
	double rejp = diff1 ?
			(double)(diff_rejected) / (double)(diff1) : 0;
	root = api_add_percent(root, "Device Rejected%", &rejp, false);

	if ((per_proc || cgpu->procs <= 1) && cgpu->drv->get_api_extra_device_status)
		root = api_add_extra(root, cgpu->drv->get_api_extra_device_status(cgpu));

	root = print_data(root, buf, isjson, precom);
	io_add(io_data, buf);
}

#ifdef USE_OPENCL
static void gpustatus(struct io_data *io_data, int gpu, bool isjson, bool precom)
{
        if (gpu < 0 || gpu >= nDevs)
                return;
        devstatus_an(io_data, &gpus[gpu], isjson, precom);
}
#endif

#ifdef HAVE_AN_FPGA
static void pgastatus(struct io_data *io_data, int pga, bool isjson, bool precom)
{
        int dev = pgadevice(pga);
        if (dev < 0) // Should never happen
                return;
        devstatus_an(io_data, get_devices(dev), isjson, precom);
}
#endif

#ifdef USE_CPUMINING
static void cpustatus(struct io_data *io_data, int cpu, bool isjson, bool precom)
{
        if (opt_n_threads <= 0 || cpu < 0 || cpu >= num_processors)
                return;
        devstatus_an(io_data, &cpus[cpu], isjson, precom);
}
#endif

static void
devinfo_internal(void (*func)(struct io_data *, struct cgpu_info*, bool, bool), int msg, struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	bool io_open = false;
	int i;

	if (total_devices == 0) {
		message(io_data, MSG_NODEVS, 0, NULL, isjson);
		return;
	}


	message(io_data, msg, 0, NULL, isjson);
	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_DEVS);

	for (i = 0; i < total_devices; ++i) {
		cgpu = get_devices(i);
		if (per_proc || cgpu->device == cgpu)
			func(io_data, cgpu, isjson, isjson && i > 0);
	}

	if (isjson && io_open)
		io_close(io_data);
}

static void devdetail(struct io_data *io_data, SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	return devinfo_internal(devdetail_an, MSG_DEVDETAILS, io_data, c, param, isjson, group);
}

static void devstatus(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	return devinfo_internal(devstatus_an, MSG_DEVS, io_data, c, param, isjson, group);
}

#ifdef USE_OPENCL
static void gpudev(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	bool io_open = false;
	int id;

	if (nDevs == 0) {
		message(io_data, MSG_GPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		message(io_data, MSG_INVGPU, id, NULL, isjson);
		return;
	}

	message(io_data, MSG_GPUDEV, id, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_GPU);

	gpustatus(io_data, id, isjson, false);

	if (isjson && io_open)
		io_close(io_data);
}
#endif

static void devscan(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	int n;
	bool io_open = false;
	
	applog(LOG_DEBUG, "RPC: request to scan %s for devices",
	       param);
	
	if (param && !param[0])
		param = NULL;
	
	n = scan_serial(param);
	
	message(io_data, MSG_DEVSCAN, n, NULL, isjson);
	
	io_open = io_add(io_data, isjson ? COMSTR JSON_DEVS : _DEVS COMSTR);

	n = total_devices - n;
	for (int i = n; i < total_devices; ++i)
		devdetail_an(io_data, get_devices(i), isjson, i > n);
	
	if (isjson && io_open)
		io_close(io_data);
}

#ifdef HAVE_AN_FPGA
static
struct cgpu_info *get_pga_cgpu(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group, int *id_p, int *dev_p)
{
	int numpga = numpgas();
	
	if (numpga == 0) {
		message(io_data, MSG_PGANON, 0, NULL, isjson);
		return NULL;
	}
	
	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return NULL;
	}
	
	*id_p = atoi(param);
	if (*id_p < 0 || *id_p >= numpga) {
		message(io_data, MSG_INVPGA, *id_p, NULL, isjson);
		return NULL;
	}
	
	*dev_p = pgadevice(*id_p);
	if (*dev_p < 0) { // Should never happen
		message(io_data, MSG_INVPGA, *id_p, NULL, isjson);
		return NULL;
	}
	
	return get_devices(*dev_p);
}

static void pgadev(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	bool io_open = false;
	int id, dev;

	if (!get_pga_cgpu(io_data, c, param, isjson, group, &id, &dev))
		return;
	
	message(io_data, MSG_PGADEV, id, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_PGA);

	pgastatus(io_data, id, isjson, false);

	if (isjson && io_open)
		io_close(io_data);
}

static void pgaenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu, *proc;
	int id, dev;
	bool already;

	cgpu = get_pga_cgpu(io_data, c, param, isjson, group, &id, &dev);
	if (!cgpu)
		return;
	
	applog(LOG_DEBUG, "API: request to pgaenable %s id %d device %d %s",
			per_proc ? "proc" : "dev", id, dev, cgpu->proc_repr_ns);

	already = true;
	int procs = per_proc ? 1 : cgpu->procs, i;
	for (i = 0, proc = cgpu; i < procs; ++i, proc = proc->next_proc)
	{
		if (proc->deven == DEV_DISABLED)
		{
			proc_enable(proc);
			already = false;
		}
	}
	
	if (already)
	{
		message(io_data, MSG_PGALRENA, id, NULL, isjson);
		return;
	}

#if 0 /* A DISABLED device wont change status FIXME: should disabling make it WELL? */
	if (cgpu->status != LIFE_WELL) {
		message(io_data, MSG_PGAUNW, id, NULL, isjson);
		return;
	}
#endif

	message(io_data, MSG_PGAENA, id, NULL, isjson);
}

static void pgadisable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu, *proc;
	int id, dev;
	bool already;

	cgpu = get_pga_cgpu(io_data, c, param, isjson, group, &id, &dev);
	if (!cgpu)
		return;
	
	applog(LOG_DEBUG, "API: request to pgadisable %s id %d device %d %s",
			per_proc ? "proc" : "dev", id, dev, cgpu->proc_repr_ns);

	already = true;
	int procs = per_proc ? 1 : cgpu->procs, i;
	for (i = 0, proc = cgpu; i < procs; ++i, proc = proc->next_proc)
	{
		if (proc->deven != DEV_DISABLED)
		{
			cgpu->deven = DEV_DISABLED;
			already = false;
		}
	}
	
	if (already)
	{
		message(io_data, MSG_PGALRDIS, id, NULL, isjson);
		return;
	}

	message(io_data, MSG_PGADIS, id, NULL, isjson);
}

static void pgarestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	int id, dev;
	
	cgpu = get_pga_cgpu(io_data, c, param, isjson, group, &id, &dev);
	if (!cgpu)
		return;
	
	applog(LOG_DEBUG, "API: request to pgarestart dev id %d device %d %s",
			id, dev, cgpu->dev_repr);
	
	reinit_device(cgpu);
	
	message(io_data, MSG_PGAREI, id, NULL, isjson);
}

static void pgaidentify(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	struct device_drv *drv;
	int id, dev;

	cgpu = get_pga_cgpu(io_data, c, param, isjson, group, &id, &dev);
	if (!cgpu)
		return;
	
	drv = cgpu->drv;

	if (drv->identify_device && drv->identify_device(cgpu))
		message(io_data, MSG_PGAIDENT, id, NULL, isjson);
	else
		message(io_data, MSG_PGANOID, id, NULL, isjson);
}
#endif

#ifdef USE_CPUMINING
static void cpudev(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	bool io_open = false;
	int id;

	if (opt_n_threads <= 0)
	{
		message(io_data, MSG_CPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= num_processors) {
		message(io_data, MSG_INVCPU, id, NULL, isjson);
		return;
	}

	message(io_data, MSG_CPUDEV, id, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_CPU);

	cpustatus(io_data, id, isjson, false);

	if (isjson && io_open)
		io_close(io_data);
}
#endif

static void poolstatus(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open = false;
	char *status, *lp;
	int i;

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	message(io_data, MSG_POOL, 0, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_POOLS);

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->removed)
			continue;

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
				if (pool->failover_only)
					status = "Failover";
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

		root = api_add_int(root, "POOL", &i, false);
		root = api_add_escape(root, "URL", pool->rpc_url, false);
		root = api_add_string(root, "Status", status, false);
		root = api_add_int(root, "Priority", &(pool->prio), false);
		root = api_add_int(root, "Quota", &pool->quota, false);
		root = api_add_string(root, "Mining Goal", pool->goal->name, false);
		root = api_add_string(root, "Long Poll", lp, false);
		root = api_add_uint(root, "Getworks", &(pool->getwork_requested), false);
		root = api_add_int(root, "Accepted", &(pool->accepted), false);
		root = api_add_int(root, "Rejected", &(pool->rejected), false);
		root = api_add_int(root, "Works", &pool->works, false);
		root = api_add_uint(root, "Discarded", &(pool->discarded_work), false);
		root = api_add_uint(root, "Stale", &(pool->stale_shares), false);
		root = api_add_uint(root, "Get Failures", &(pool->getfail_occasions), false);
		root = api_add_uint(root, "Remote Failures", &(pool->remotefail_occasions), false);
		root = api_add_escape(root, "User", pool->rpc_user, false);
		root = api_add_time(root, "Last Share Time", &(pool->last_share_time), false);
		root = api_add_diff(root, "Diff1 Shares", &(pool->diff1), false);
		if (pool->rpc_proxy) {
			root = api_add_escape(root, "Proxy", pool->rpc_proxy, false);
		} else {
			root = api_add_const(root, "Proxy", BLANK, false);
		}
		root = api_add_diff(root, "Difficulty Accepted", &(pool->diff_accepted), false);
		root = api_add_diff(root, "Difficulty Rejected", &(pool->diff_rejected), false);
		root = api_add_diff(root, "Difficulty Stale", &(pool->diff_stale), false);
		root = api_add_diff(root, "Last Share Difficulty", &(pool->last_share_diff), false);
		root = api_add_bool(root, "Has Stratum", &(pool->has_stratum), false);
		root = api_add_bool(root, "Stratum Active", &(pool->stratum_active), false);
		if (pool->stratum_active)
			root = api_add_escape(root, "Stratum URL", pool->stratum_url, false);
		else
			root = api_add_const(root, "Stratum URL", BLANK, false);
		root = api_add_diff(root, "Best Share", &(pool->best_diff), true);
		if (pool->admin_msg)
			root = api_add_escape(root, "Message", pool->admin_msg, true);
		double rejp = (pool->diff_accepted + pool->diff_rejected + pool->diff_stale) ?
				(double)(pool->diff_rejected) / (double)(pool->diff_accepted + pool->diff_rejected + pool->diff_stale) : 0;
		root = api_add_percent(root, "Pool Rejected%", &rejp, false);
		double stalep = (pool->diff_accepted + pool->diff_rejected + pool->diff_stale) ?
				(double)(pool->diff_stale) / (double)(pool->diff_accepted + pool->diff_rejected + pool->diff_stale) : 0;
		root = api_add_percent(root, "Pool Stale%", &stalep, false);

		root = print_data(root, buf, isjson, isjson && (i > 0));
		io_add(io_data, buf);
	}

	if (isjson && io_open)
		io_close(io_data);
}

static void summary(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	double utility, mhs, work_utility;

	message(io_data, MSG_SUMM, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_SUMMARY : _SUMMARY COMSTR);

	// stop hashmeter() changing some while copying
	mutex_lock(&hash_lock);

	utility = total_accepted / ( total_secs ? total_secs : 1 ) * 60;
	mhs = total_mhashes_done / total_secs;
	work_utility = total_diff1 / ( total_secs ? total_secs : 1 ) * 60;

	root = api_add_elapsed(root, "Elapsed", &(total_secs), true);
#if defined(USE_CPUMINING) && defined(USE_SHA256D)
	if (opt_n_threads > 0)
		root = api_add_string(root, "Algorithm", (algo_names[opt_algo] ?: NULLSTR), false);
#endif
	root = api_add_mhs(root, "MHS av", &(mhs), false);
	char mhsname[27];
	sprintf(mhsname, "MHS %ds", opt_log_interval);
	root = api_add_mhs(root, mhsname, &(total_rolling), false);
	root = api_add_uint(root, "Found Blocks", &(found_blocks), true);
	root = api_add_int(root, "Getworks", &(total_getworks), true);
	root = api_add_int(root, "Accepted", &(total_accepted), true);
	root = api_add_int(root, "Rejected", &(total_rejected), true);
	root = api_add_int(root, "Hardware Errors", &(hw_errors), true);
	root = api_add_utility(root, "Utility", &(utility), false);
	root = api_add_int(root, "Discarded", &(total_discarded), true);
	root = api_add_int(root, "Stale", &(total_stale), true);
	root = api_add_uint(root, "Get Failures", &(total_go), true);
	root = api_add_uint(root, "Local Work", &(local_work), true);
	root = api_add_uint(root, "Remote Failures", &(total_ro), true);
	root = api_add_uint(root, "Network Blocks", &(new_blocks), true);
	root = api_add_mhtotal(root, "Total MH", &(total_mhashes_done), true);
	root = api_add_diff(root, "Diff1 Work", &total_diff1, true);
	root = api_add_utility(root, "Work Utility", &(work_utility), false);
	root = api_add_diff(root, "Difficulty Accepted", &(total_diff_accepted), true);
	root = api_add_diff(root, "Difficulty Rejected", &(total_diff_rejected), true);
	root = api_add_diff(root, "Difficulty Stale", &(total_diff_stale), true);
	root = api_add_diff(root, "Best Share", &(best_diff), true);
	double hwp = (total_bad_diff1 + total_diff1) ?
			(double)(total_bad_diff1) / (double)(total_bad_diff1 + total_diff1) : 0;
	root = api_add_percent(root, "Device Hardware%", &hwp, false);
	double rejp = total_diff1 ?
			(double)(total_diff_rejected) / (double)(total_diff1) : 0;
	root = api_add_percent(root, "Device Rejected%", &rejp, false);
	double prejp = (total_diff_accepted + total_diff_rejected + total_diff_stale) ?
			(double)(total_diff_rejected) / (double)(total_diff_accepted + total_diff_rejected + total_diff_stale) : 0;
	root = api_add_percent(root, "Pool Rejected%", &prejp, false);
	double stalep = (total_diff_accepted + total_diff_rejected + total_diff_stale) ?
			(double)(total_diff_stale) / (double)(total_diff_accepted + total_diff_rejected + total_diff_stale) : 0;
	root = api_add_percent(root, "Pool Stale%", &stalep, false);
	root = api_add_time(root, "Last getwork", &last_getwork, false);

	mutex_unlock(&hash_lock);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

#ifdef USE_OPENCL
static void gpuenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;

	if (!nDevs) {
		message(io_data, MSG_GPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		message(io_data, MSG_INVGPU, id, NULL, isjson);
		return;
	}

	applog(LOG_DEBUG, "API: request to gpuenable gpuid %d %s",
			id, gpus[id].proc_repr_ns);

	if (gpus[id].deven != DEV_DISABLED) {
		message(io_data, MSG_ALRENA, id, NULL, isjson);
		return;
	}

	if (gpus[id].status != LIFE_WELL)
	{
		message(io_data, MSG_GPUMRE, id, NULL, isjson);
		return;
	}
	proc_enable(&gpus[id]);

	message(io_data, MSG_GPUREN, id, NULL, isjson);
}

static void gpudisable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;

	if (nDevs == 0) {
		message(io_data, MSG_GPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		message(io_data, MSG_INVGPU, id, NULL, isjson);
		return;
	}

	applog(LOG_DEBUG, "API: request to gpudisable gpuid %d %s",
			id, gpus[id].proc_repr_ns);

	if (gpus[id].deven == DEV_DISABLED) {
		message(io_data, MSG_ALRDIS, id, NULL, isjson);
		return;
	}

	gpus[id].deven = DEV_DISABLED;

	message(io_data, MSG_GPUDIS, id, NULL, isjson);
}

static void gpurestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;

	if (nDevs == 0) {
		message(io_data, MSG_GPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		message(io_data, MSG_INVGPU, id, NULL, isjson);
		return;
	}

	reinit_device(&gpus[id]);

	message(io_data, MSG_GPUREI, id, NULL, isjson);
}
#endif

static void gpucount(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	int numgpu = 0;

#ifdef USE_OPENCL
	numgpu = nDevs;
#endif

	message(io_data, MSG_NUMGPU, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_GPUS : _GPUS COMSTR);

	root = api_add_int(root, "Count", &numgpu, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

static void pgacount(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	int count = 0;

#ifdef HAVE_AN_FPGA
	count = numpgas();
#endif

	message(io_data, MSG_NUMPGA, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_PGAS : _PGAS COMSTR);

	root = api_add_int(root, "Count", &count, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

#ifdef USE_CPUMINING
static void cpuenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;

	if (opt_n_threads <= 0)
	{
		message(io_data, MSG_CPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= opt_n_threads) {
		message(io_data, MSG_INVCPU, id, NULL, isjson);
		return;
	}

	applog(LOG_DEBUG, "API: request to cpuenable cpuid %d %s",
			id, cpus[id].proc_repr_ns);

	if (cpus[id].deven != DEV_DISABLED) {
		message(io_data, MSG_ALRENAC, id, NULL, isjson);
		return;
	}

	if (cpus[id].status != LIFE_WELL)
	{
		message(io_data, MSG_CPUMRE, id, NULL, isjson);
		return;
	}
	proc_enable(&cpus[id]);

	message(io_data, MSG_CPUREN, id, NULL, isjson);
}

static void cpudisable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;

	if (opt_n_threads <= 0)
	{
		message(io_data, MSG_CPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= opt_n_threads) {
		message(io_data, MSG_INVCPU, id, NULL, isjson);
		return;
	}

	applog(LOG_DEBUG, "API: request to cpudisable cpuid %d %s",
			id, cpus[id].proc_repr_ns);

	if (cpus[id].deven == DEV_DISABLED) {
		message(io_data, MSG_ALRDISC, id, NULL, isjson);
		return;
	}

	cpus[id].deven = DEV_DISABLED;

	message(io_data, MSG_CPUDIS, id, NULL, isjson);
}

static void cpurestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;

	if (opt_n_threads <= 0)
	{
		message(io_data, MSG_CPUNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= opt_n_threads) {
		message(io_data, MSG_INVCPU, id, NULL, isjson);
		return;
	}

	reinit_device(&cpus[id]);

	message(io_data, MSG_CPUREI, id, NULL, isjson);
}
#endif

static void cpucount(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	int count = 0;

#ifdef USE_CPUMINING
	count = opt_n_threads > 0 ? num_processors : 0;
#endif

	message(io_data, MSG_NUMCPU, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_CPUS : _CPUS COMSTR);

	root = api_add_int(root, "Count", &count, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

static void switchpool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct pool *pool;
	int id;

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	cg_rlock(&control_lock);
	if (id < 0 || id >= total_pools) {
		cg_runlock(&control_lock);
		message(io_data, MSG_INVPID, id, NULL, isjson);
		return;
	}

	pool = pools[id];
	manual_enable_pool(pool);
	cg_runlock(&control_lock);
	switch_pools(pool);

	message(io_data, MSG_SWITCHP, id, NULL, isjson);
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

static bool pooldetails(char *param, char **url, char **user, char **pass, char **goalname)
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
	
	if (*param)
		*goalname = buf;
	
	// copy goalname
	copyadvanceafter(',', &param, &buf);

	return true;

exitsama:
	free(ptr);
	return false;
}

static void addpool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	char *url, *user, *pass, *goalname = "default";
	struct pool *pool;
	char *ptr;

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPDP, 0, NULL, isjson);
		return;
	}

	if (!pooldetails(param, &url, &user, &pass, &goalname))
	{
		ptr = escape_string(param, isjson);
		message(io_data, MSG_INVPDP, 0, ptr, isjson);
		if (ptr != param)
			free(ptr);
		ptr = NULL;
		return;
	}

	struct mining_goal_info * const goal = get_mining_goal(goalname);
	pool = add_pool2(goal);
	detect_stratum(pool, url);
	add_pool_details(pool, true, url, user, pass);

	ptr = escape_string(url, isjson);
	message(io_data, MSG_ADDPOOL, 0, ptr, isjson);
	if (ptr != url)
		free(ptr);
	ptr = NULL;
}

static void enablepool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct pool *pool;
	int id;

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= total_pools) {
		message(io_data, MSG_INVPID, id, NULL, isjson);
		return;
	}

	pool = pools[id];
	if (pool->enabled == POOL_ENABLED && !pool->failover_only) {
		message(io_data, MSG_ALRENAP, id, NULL, isjson);
		return;
	}

	manual_enable_pool(pool);

	message(io_data, MSG_ENAPOOL, id, NULL, isjson);
}

static void poolpriority(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int i;

	switch (prioritize_pools(param, &i)) {
		case MSG_NOPOOL:
			message(io_data, MSG_NOPOOL, 0, NULL, isjson);
			return;
		case MSG_MISPID:
			message(io_data, MSG_MISPID, 0, NULL, isjson);
			return;
		case MSG_INVPID:
			message(io_data, MSG_INVPID, i, NULL, isjson);
			return;
		case MSG_DUPPID:
			message(io_data, MSG_DUPPID, i, NULL, isjson);
			return;
		case MSG_POOLPRIO:
		default:
			message(io_data, MSG_POOLPRIO, 0, NULL, isjson);
			return;
	}
}

static void poolquota(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct pool *pool;
	int quota, id;
	char *comma;

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPID, 0, NULL, isjson);
		return;
	}

	comma = strchr(param, ',');
	if (!comma) {
		message(io_data, MSG_CONVAL, 0, param, isjson);
		return;
	}

	*(comma++) = '\0';

	id = atoi(param);
	if (id < 0 || id >= total_pools) {
		message(io_data, MSG_INVPID, id, NULL, isjson);
		return;
	}
	pool = pools[id];

	quota = atoi(comma);
	if (quota < 0) {
		message(io_data, MSG_INVNEG, quota, pool->rpc_url, isjson);
		return;
	}

	pool->quota = quota;
	adjust_quota_gcd();
	message(io_data, MSG_SETQUOTA, quota, pool->rpc_url, isjson);
}

static void disablepool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct pool *pool;
	int id;

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= total_pools) {
		message(io_data, MSG_INVPID, id, NULL, isjson);
		return;
	}

	pool = pools[id];
	if (pool->enabled == POOL_DISABLED) {
		message(io_data, MSG_ALRDISP, id, NULL, isjson);
		return;
	}

	if (enabled_pools <= 1) {
		message(io_data, MSG_DISLASTP, id, NULL, isjson);
		return;
	}

	disable_pool(pool, POOL_DISABLED);

	message(io_data, MSG_DISPOOL, id, NULL, isjson);
}

static void removepool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct pool *pool;
	char *rpc_url;
	bool dofree = false;
	int id;

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= total_pools) {
		message(io_data, MSG_INVPID, id, NULL, isjson);
		return;
	}

	if (total_pools <= 1) {
		message(io_data, MSG_REMLASTP, id, NULL, isjson);
		return;
	}

	pool = pools[id];
	if (pool == current_pool())
		switch_pools(NULL);

	if (pool == current_pool()) {
		message(io_data, MSG_ACTPOOL, id, NULL, isjson);
		return;
	}

	rpc_url = escape_string(pool->rpc_url, isjson);
	if (rpc_url != pool->rpc_url)
		dofree = true;

	remove_pool(pool);

	message(io_data, MSG_REMPOOL, id, rpc_url, isjson);

	if (dofree)
		free(rpc_url);
	rpc_url = NULL;
}

#ifdef USE_OPENCL
static bool splitgpuvalue(struct io_data *io_data, char *param, int *gpu, char **value, bool isjson)
{
	int id;
	char *gpusep;

	if (nDevs == 0) {
		message(io_data, MSG_GPUNON, 0, NULL, isjson);
		return false;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return false;
	}

	gpusep = strchr(param, GPUSEP);
	if (gpusep == NULL) {
		message(io_data, MSG_MISVAL, 0, NULL, isjson);
		return false;
	}

	*(gpusep++) = '\0';

	id = atoi(param);
	if (id < 0 || id >= nDevs) {
		message(io_data, MSG_INVGPU, id, NULL, isjson);
		return false;
	}

	*gpu = id;
	*value = gpusep;

	return true;
}

static void gpuintensity(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	int id;
	char *value;
	char intensitystr[7];
	char buf[TMPBUFSIZ];

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	struct cgpu_info * const cgpu = &gpus[id];
	struct opencl_device_data * const data = gpus[id].device_data;
	
	enum bfg_set_device_replytype success;
	proc_set_device(cgpu, "intensity", value, buf, &success);
	if (success == SDR_OK)
	{
		if (data->dynamic)
			strcpy(intensitystr, DYNAMIC);
		else
		{
			const char *iunit;
			float intensity = opencl_proc_get_intensity(cgpu, &iunit);
			snprintf(intensitystr, sizeof(intensitystr), "%s%g", iunit, intensity);
		}
	}
	else
	{
		message(io_data, MSG_INVINT, 0, value, isjson);
		return;
	}

	message(io_data, MSG_GPUINT, id, intensitystr, isjson);
}

static void gpumem(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
	int id;
	char *value;
	char buf[TMPBUFSIZ];

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	struct cgpu_info * const cgpu = &gpus[id];
	
	enum bfg_set_device_replytype success;
	proc_set_device(cgpu, "memclock", value, buf, &success);
	if (success != SDR_OK)
		message(io_data, MSG_GPUMERR, id, value, isjson);
	else
		message(io_data, MSG_GPUMEM, id, value, isjson);
#else
	message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

static void gpuengine(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
	int id;
	char *value;
	char buf[TMPBUFSIZ];

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	struct cgpu_info * const cgpu = &gpus[id];
	
	enum bfg_set_device_replytype success;
	proc_set_device(cgpu, "clock", value, buf, &success);
	if (success != SDR_OK)
		message(io_data, MSG_GPUEERR, id, value, isjson);
	else
		message(io_data, MSG_GPUENG, id, value, isjson);
#else
	message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

static void gpufan(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
	int id;
	char *value;
	char buf[TMPBUFSIZ];

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	struct cgpu_info * const cgpu = &gpus[id];
	
	enum bfg_set_device_replytype success;
	proc_set_device(cgpu, "fan", value, buf, &success);
	if (success != SDR_OK)
		message(io_data, MSG_GPUFERR, id, value, isjson);
	else
		message(io_data, MSG_GPUFAN, id, value, isjson);
#else
	message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}

static void gpuvddc(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
	int id;
	char *value;
	char buf[TMPBUFSIZ];

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	struct cgpu_info * const cgpu = &gpus[id];
	
	enum bfg_set_device_replytype success;
	proc_set_device(cgpu, "voltage", value, buf, &success);
	if (success != SDR_OK)
		message(io_data, MSG_GPUVERR, id, value, isjson);
	else
		message(io_data, MSG_GPUVDDC, id, value, isjson);
#else
	message(io_data, MSG_NOADL, 0, NULL, isjson);
#endif
}
#endif

void doquit(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	message(io_data, MSG_BYE, 0, _BYE, isjson);

	bye = true;
	do_a_quit = true;
}

void dorestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	message(io_data, MSG_BYE, 0, _RESTART, isjson);

	bye = true;
	do_a_restart = true;
}

void privileged(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	message(io_data, MSG_ACCOK, 0, NULL, isjson);
}

void notifystatus(struct io_data *io_data, int device, struct cgpu_info *cgpu, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *proc;
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	char *reason;
	
	time_t last_not_well = 0;
	enum dev_reason uninitialised_var(enum_reason);
	int thread_fail_init_count = 0, thread_zero_hash_count = 0, thread_fail_queue_count = 0;
	int dev_sick_idle_60_count = 0, dev_dead_idle_600_count = 0;
	int dev_nostart_count = 0, dev_over_heat_count = 0, dev_thermal_cutoff_count = 0, dev_comms_error_count = 0, dev_throttle_count = 0;

	int procs = per_proc ? 1 : cgpu->procs, i;
	for (i = 0, proc = cgpu; i < procs; ++i, proc = proc->next_proc)
	{
		if (proc->device_last_not_well > last_not_well)
		{
			last_not_well = proc->device_last_not_well;
			enum_reason = proc->device_not_well_reason;
			thread_fail_init_count   += proc->thread_fail_init_count;
			thread_zero_hash_count   += proc->thread_zero_hash_count;
			thread_fail_queue_count  += proc->thread_fail_queue_count;
			dev_sick_idle_60_count   += proc->dev_sick_idle_60_count;
			dev_dead_idle_600_count  += proc->dev_dead_idle_600_count;
			dev_nostart_count        += proc->dev_nostart_count;
			dev_over_heat_count      += proc->dev_over_heat_count;
			dev_thermal_cutoff_count += proc->dev_thermal_cutoff_count;
			dev_comms_error_count    += proc->dev_comms_error_count;
			dev_throttle_count       += proc->dev_throttle_count;
		}
		if (per_proc)
			break;
	}
	
	if (last_not_well == 0)
		reason = REASON_NONE;
	else
		switch (enum_reason)
		{
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
			case REASON_DEV_COMMS_ERROR:
				reason = REASON_DEV_COMMS_ERROR_STR;
				break;
			default:
				reason = REASON_UNKNOWN_STR;
				break;
		}

	// ALL counters (and only counters) must start the name with a '*'
	// Simplifies future external support for identifying new counters
	root = api_add_int(root, "NOTIFY", &device, false);
	root = api_add_device_identifier(root, cgpu);
	if (per_proc)
		root = api_add_time(root, "Last Well", &(cgpu->device_last_well), false);
	root = api_add_time(root, "Last Not Well", &last_not_well, false);
	root = api_add_string(root, "Reason Not Well", reason, false);
	root = api_add_int(root, "*Thread Fail Init", &thread_fail_init_count, false);
	root = api_add_int(root, "*Thread Zero Hash", &thread_zero_hash_count, false);
	root = api_add_int(root, "*Thread Fail Queue", &thread_fail_queue_count, false);
	root = api_add_int(root, "*Dev Sick Idle 60s", &dev_sick_idle_60_count, false);
	root = api_add_int(root, "*Dev Dead Idle 600s", &dev_dead_idle_600_count, false);
	root = api_add_int(root, "*Dev Nostart", &dev_nostart_count, false);
	root = api_add_int(root, "*Dev Over Heat", &dev_over_heat_count, false);
	root = api_add_int(root, "*Dev Thermal Cutoff", &dev_thermal_cutoff_count, false);
	root = api_add_int(root, "*Dev Comms Error", &dev_comms_error_count, false);
	root = api_add_int(root, "*Dev Throttle", &dev_throttle_count, false);

	root = print_data(root, buf, isjson, isjson && (device > 0));
	io_add(io_data, buf);
}

static
void notify(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, char group)
{
	struct cgpu_info *cgpu;
	bool io_open = false;
	int i, n = 0;

	if (total_devices == 0) {
		message(io_data, MSG_NODEVS, 0, NULL, isjson);
		return;
	}

	message(io_data, MSG_NOTIFY, 0, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_NOTIFY);

	for (i = 0; i < total_devices; i++) {
		cgpu = get_devices(i);
		if (cgpu->device == cgpu || per_proc)
			notifystatus(io_data, n++, cgpu, isjson, group);
	}

	if (isjson && io_open)
		io_close(io_data);
}

void dosave(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
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
		message(io_data, MSG_BADFN, 0, ptr, isjson);
		if (ptr != param)
			free(ptr);
		ptr = NULL;
		return;
	}

	write_config(fcfg);
	fclose(fcfg);

	ptr = escape_string(param, isjson);
	message(io_data, MSG_SAVED, 0, ptr, isjson);
	if (ptr != param)
		free(ptr);
	ptr = NULL;
}

static int itemstats(struct io_data *io_data, int i, char *id, struct cgminer_stats *stats, struct cgminer_pool_stats *pool_stats, struct api_data *extra, bool isjson)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	double elapsed;

	root = api_add_int(root, "STATS", &i, false);
	root = api_add_string(root, "ID", id, false);
	elapsed = stats_elapsed(stats);
	root = api_add_elapsed(root, "Elapsed", &elapsed, false);
	root = api_add_uint32(root, "Calls", &(stats->getwork_calls), false);
	root = api_add_timeval(root, "Wait", &(stats->getwork_wait), false);
	root = api_add_timeval(root, "Max", &(stats->getwork_wait_max), false);
	root = api_add_timeval(root, "Min", &(stats->getwork_wait_min), false);

	if (pool_stats) {
		root = api_add_uint32(root, "Pool Calls", &(pool_stats->getwork_calls), false);
		root = api_add_uint32(root, "Pool Attempts", &(pool_stats->getwork_attempts), false);
		root = api_add_timeval(root, "Pool Wait", &(pool_stats->getwork_wait), false);
		root = api_add_timeval(root, "Pool Max", &(pool_stats->getwork_wait_max), false);
		root = api_add_timeval(root, "Pool Min", &(pool_stats->getwork_wait_min), false);
		root = api_add_double(root, "Pool Av", &(pool_stats->getwork_wait_rolling), false);
		root = api_add_bool(root, "Work Had Roll Time", &(pool_stats->hadrolltime), false);
		root = api_add_bool(root, "Work Can Roll", &(pool_stats->canroll), false);
		root = api_add_bool(root, "Work Had Expire", &(pool_stats->hadexpire), false);
		root = api_add_uint32(root, "Work Roll Time", &(pool_stats->rolltime), false);
		root = api_add_diff(root, "Work Diff", &(pool_stats->last_diff), false);
		root = api_add_diff(root, "Min Diff", &(pool_stats->min_diff), false);
		root = api_add_diff(root, "Max Diff", &(pool_stats->max_diff), false);
		root = api_add_uint32(root, "Min Diff Count", &(pool_stats->min_diff_count), false);
		root = api_add_uint32(root, "Max Diff Count", &(pool_stats->max_diff_count), false);
		root = api_add_uint64(root, "Times Sent", &(pool_stats->times_sent), false);
		root = api_add_uint64(root, "Bytes Sent", &(pool_stats->bytes_sent), false);
		root = api_add_uint64(root, "Times Recv", &(pool_stats->times_received), false);
		root = api_add_uint64(root, "Bytes Recv", &(pool_stats->bytes_received), false);
		root = api_add_uint64(root, "Net Bytes Sent", &(pool_stats->net_bytes_sent), false);
		root = api_add_uint64(root, "Net Bytes Recv", &(pool_stats->net_bytes_received), false);
	}

	if (extra)
		root = api_add_extra(root, extra);

	root = print_data(root, buf, isjson, isjson && (i > 0));
	io_add(io_data, buf);

	return ++i;
}

static void minerstats(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	bool io_open = false;
	struct api_data *extra;
	char id[20];
	int i, j;

	message(io_data, MSG_MINESTATS, 0, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_MINESTATS);

	i = 0;
	for (j = 0; j < total_devices; j++) {
		cgpu = get_devices(j);

		if (cgpu && cgpu->drv) {
			if (cgpu->drv->get_api_stats)
				extra = cgpu->drv->get_api_stats(cgpu);
			else
				extra = NULL;

			i = itemstats(io_data, i, cgpu->proc_repr_ns, &(cgpu->cgminer_stats), NULL, extra, isjson);
		}
	}

	for (j = 0; j < total_pools; j++) {
		struct pool *pool = pools[j];

		sprintf(id, "POOL%d", j);
		i = itemstats(io_data, i, id, &(pool->cgminer_stats), &(pool->cgminer_pool_stats), NULL, isjson);
	}

	if (isjson && io_open)
		io_close(io_data);
}

static void failoveronly(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISBOOL, 0, NULL, isjson);
		return;
	}

	*param = tolower(*param);

	if (*param != 't' && *param != 'f') {
		message(io_data, MSG_INVBOOL, 0, NULL, isjson);
		return;
	}

	bool tf = (*param == 't');

	opt_fail_only = tf;

	message(io_data, MSG_FOO, tf, NULL, isjson);
}

static void minecoin(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];

	message(io_data, MSG_MINECOIN, 0, NULL, isjson);

	struct mining_goal_info *goal, *tmpgoal;
	bool precom = false;
	HASH_ITER(hh, mining_goals, goal, tmpgoal)
	{
		if (goal->is_default)
			io_add(io_data, isjson ? COMSTR JSON1 _MINECOIN JSON2 : _MINECOIN COMSTR);
		else
		{
			sprintf(buf, isjson ? COMSTR JSON1 _MINECOIN "%u" JSON2 : _MINECOIN "%u" COMSTR, goal->id);
			io_add(io_data, buf);
		}
		
		switch (goal->malgo->algo)
		{
#ifdef USE_SCRYPT
			case POW_SCRYPT:
				root = api_add_const(root, "Hash Method", SCRYPTSTR, false);
				break;
#endif
#ifdef USE_SHA256D
			case POW_SHA256D:
				root = api_add_const(root, "Hash Method", SHA256STR, false);
				break;
#endif
			default:
				root = api_add_const(root, "Hash Method", goal->malgo->name, false);
				break;
		}

		cg_rlock(&ch_lock);
		struct blockchain_info * const blkchain = goal->blkchain;
		struct block_info * const blkinfo = blkchain->currentblk;
		root = api_add_time(root, "Current Block Time", &blkinfo->first_seen_time, true);
		char fullhash[(sizeof(blkinfo->prevblkhash) * 2) + 1];
		blkhashstr(fullhash, blkinfo->prevblkhash);
		root = api_add_string(root, "Current Block Hash", fullhash, true);
		cg_runlock(&ch_lock);

		root = api_add_bool(root, "LP", &goal->have_longpoll, false);
		root = api_add_diff(root, "Network Difficulty", &goal->current_diff, true);
		
		root = api_add_diff(root, "Difficulty Accepted", &goal->diff_accepted, false);
		
		root = print_data(root, buf, isjson, precom);
		io_add(io_data, buf);
		if (isjson)
			io_add(io_data, JSON_CLOSE);
	}
}

static void debugstate(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;

	if (param == NULL)
		param = (char *)BLANK;
	else
		*param = tolower(*param);

	switch(*param) {
	case 's':
		opt_realquiet = true;
		break;
	case 'q':
		opt_quiet ^= true;
		break;
	case 'v':
		opt_log_output ^= true;
		if (opt_log_output)
			opt_quiet = false;
		break;
	case 'd':
		opt_debug ^= true;
		opt_log_output = opt_debug;
		if (opt_debug)
			opt_quiet = false;
		break;
	case 'r':
		opt_protocol ^= true;
		if (opt_protocol)
			opt_quiet = false;
		break;
	case 'p':
		want_per_device_stats ^= true;
		opt_log_output = want_per_device_stats;
		break;
	case 'n':
		opt_log_output = false;
		opt_debug = false;
		opt_quiet = false;
		opt_protocol = false;
		want_per_device_stats = false;
		opt_worktime = false;
		break;
	case 'w':
		opt_worktime ^= true;
		break;
#ifdef _MEMORY_DEBUG
	case 'y':
		cgmemspeedup();
		break;
	case 'z':
		cgmemrpt();
		break;
#endif
	default:
		// anything else just reports the settings
		break;
	}

	message(io_data, MSG_DEBUGSET, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_DEBUGSET : _DEBUGSET COMSTR);

	root = api_add_bool(root, "Silent", &opt_realquiet, false);
	root = api_add_bool(root, "Quiet", &opt_quiet, false);
	root = api_add_bool(root, "Verbose", &opt_log_output, false);
	root = api_add_bool(root, "Debug", &opt_debug, false);
	root = api_add_bool(root, "RPCProto", &opt_protocol, false);
	root = api_add_bool(root, "PerDevice", &want_per_device_stats, false);
	root = api_add_bool(root, "WorkTime", &opt_worktime, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

extern void stratumsrv_change_port();

static void setconfig(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	char *comma;
	int value;

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_CONPAR, 0, NULL, isjson);
		return;
	}

	comma = strchr(param, ',');
	if (!comma) {
		message(io_data, MSG_CONVAL, 0, param, isjson);
		return;
	}

	*(comma++) = '\0';

#if BLKMAKER_VERSION > 0
	if (strcasecmp(param, "coinbase-sig") == 0) {
		free(opt_coinbase_sig);
		opt_coinbase_sig = strdup(comma);
		message(io_data, MSG_SETCONFIG, 1, param, isjson);
		return;
	}
#endif

	value = atoi(comma);
	if (value < 0 || value > 9999) {
		message(io_data, MSG_INVNUM, value, param, isjson);
		return;
	}

	if (strcasecmp(param, "queue") == 0)
		opt_queue = value;
	else if (strcasecmp(param, "scantime") == 0)
		opt_scantime = value;
	else if (strcasecmp(param, "expiry") == 0)
		opt_expiry = value;
#ifdef USE_LIBMICROHTTPD
	else if (strcasecmp(param, "http-port") == 0)
	{
		httpsrv_stop();
		httpsrv_port = value;
		if (httpsrv_port != -1)
			httpsrv_start(httpsrv_port);
	}
#endif
#ifdef USE_LIBEVENT
	else if (strcasecmp(param, "stratum-port") == 0)
	{
		stratumsrv_port = value;
		stratumsrv_change_port();
	}
#endif
	else {
		message(io_data, MSG_UNKCON, 0, param, isjson);
		return;
	}

	message(io_data, MSG_SETCONFIG, value, param, isjson);
}

#ifdef HAVE_AN_FPGA
static void pgaset(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	char buf[TMPBUFSIZ];
	int numpga = numpgas();

	if (numpga == 0) {
		message(io_data, MSG_PGANON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	char *opt = strchr(param, ',');
	if (opt)
		*(opt++) = '\0';
	if (!opt || !*opt) {
		message(io_data, MSG_MISPGAOPT, 0, NULL, isjson);
		return;
	}

	int id = atoi(param);
	if (id < 0 || id >= numpga) {
		message(io_data, MSG_INVPGA, id, NULL, isjson);
		return;
	}

	int dev = pgadevice(id);
	if (dev < 0) { // Should never happen
		message(io_data, MSG_INVPGA, id, NULL, isjson);
		return;
	}

	cgpu = get_devices(dev);

	char *set = strchr(opt, ',');
	if (set)
		*(set++) = '\0';

	enum bfg_set_device_replytype success;
	const char *ret = proc_set_device(cgpu, opt, set, buf, &success);
	switch (success)
	{
		case SDR_HELP:
			message(io_data, MSG_PGAHELP, id, ret, isjson);
			break;
		case SDR_OK:
			if (ret)
				message(io_data, MSG_PGASETOK | USE_ALTMSG, id, ret, isjson);
			else
				message(io_data, MSG_PGASETOK, id, NULL, isjson);
			break;
		case SDR_UNKNOWN:
		case SDR_ERR:
			message(io_data, MSG_PGASETERR, id, ret, isjson);
			break;
		case SDR_AUTO:
		case SDR_NOSUPP:
			message(io_data, MSG_PGANOSET, id, NULL, isjson);
	}
}
#endif

static void dozero(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	if (param == NULL || *param == '\0') {
		message(io_data, MSG_ZERMIS, 0, NULL, isjson);
		return;
	}

	char *sum = strchr(param, ',');
	if (sum)
		*(sum++) = '\0';
	if (!sum || !*sum) {
		message(io_data, MSG_MISBOOL, 0, NULL, isjson);
		return;
	}

	bool all = false;
	bool bs = false;
	if (strcasecmp(param, "all") == 0)
		all = true;
	else if (strcasecmp(param, "bestshare") == 0)
		bs = true;

	if (all == false && bs == false) {
		message(io_data, MSG_ZERINV, 0, param, isjson);
		return;
	}

	*sum = tolower(*sum);
	if (*sum != 't' && *sum != 'f') {
		message(io_data, MSG_INVBOOL, 0, NULL, isjson);
		return;
	}

	bool dosum = (*sum == 't');
	if (dosum)
		print_summary();

	if (all)
		zero_stats();
	if (bs)
		zero_bestshare();

	if (dosum)
		message(io_data, MSG_ZERSUM, 0, all ? "All" : "BestShare", isjson);
	else
		message(io_data, MSG_ZERNOSUM, 0, all ? "All" : "BestShare", isjson);
}

static void checkcommand(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, char group);

struct CMDS {
	char *name;
	void (*func)(struct io_data *, SOCKETTYPE, char *, bool, char);
	bool iswritemode;
	bool joinable;
} cmds[] = {
	{ "version",		apiversion,	false,	true },
	{ "config",		minerconfig,	false,	true },
	{ "devscan",		devscan,	true,	false },
	{ "devs",		devstatus,	false,	true },
	{ "procs",		devstatus,	false,	true },
	{ "pools",		poolstatus,	false,	true },
	{ "summary",		summary,	false,	true },
#ifdef USE_OPENCL
	{ "gpuenable",		gpuenable,	true,	false },
	{ "gpudisable",		gpudisable,	true,	false },
	{ "gpurestart",		gpurestart,	true,	false },
	{ "gpu",		gpudev,		false,	false },
#endif
#ifdef HAVE_AN_FPGA
	{ "pga",		pgadev,		false,	false },
	{ "pgaenable",		pgaenable,	true,	false },
	{ "pgadisable",		pgadisable,	true,	false },
	{ "pgarestart",		pgarestart,	true,	false },
	{ "pgaidentify",	pgaidentify,	true,	false },
	{ "proc",		pgadev,		false,	false },
	{ "procenable",		pgaenable,	true,	false },
	{ "procdisable",		pgadisable,	true,	false },
	{ "procidentify",	pgaidentify,	true,	false },
#endif
#ifdef USE_CPUMINING
	{ "cpuenable",		cpuenable,	true,	false },
	{ "cpudisable",		cpudisable,	true,	false },
	{ "cpurestart",		cpurestart,	true,	false },
	{ "cpu",		cpudev,		false,	false },
#endif
	{ "gpucount",		gpucount,	false,	true },
	{ "pgacount",		pgacount,	false,	true },
	{ "proccount",		pgacount,	false,	true },
	{ "cpucount",		cpucount,	false,	true },
	{ "switchpool",		switchpool,	true,	false },
	{ "addpool",		addpool,	true,	false },
	{ "poolpriority",	poolpriority,	true,	false },
	{ "poolquota",		poolquota,	true,	false },
	{ "enablepool",		enablepool,	true,	false },
	{ "disablepool",	disablepool,	true,	false },
	{ "removepool",		removepool,	true,	false },
#ifdef USE_OPENCL
	{ "gpuintensity",	gpuintensity,	true,	false },
	{ "gpumem",		gpumem,		true,	false },
	{ "gpuengine",		gpuengine,	true,	false },
	{ "gpufan",		gpufan,		true,	false },
	{ "gpuvddc",		gpuvddc,	true,	false },
#endif
	{ "save",		dosave,		true,	false },
	{ "quit",		doquit,		true,	false },
	{ "privileged",		privileged,	true,	false },
	{ "notify",		notify,		false,	true },
	{ "procnotify",		notify,		false,	true },
	{ "devdetails",		devdetail,	false,	true },
	{ "procdetails",		devdetail,	false,	true },
	{ "restart",		dorestart,	true,	false },
	{ "stats",		minerstats,	false,	true },
	{ "check",		checkcommand,	false,	false },
	{ "failover-only",	failoveronly,	true,	false },
	{ "coin",		minecoin,	false,	true },
	{ "debug",		debugstate,	true,	false },
	{ "setconfig",		setconfig,	true,	false },
#ifdef HAVE_AN_FPGA
	{ "pgaset",		pgaset,		true,	false },
	{ "procset",		pgaset,		true,	false },
#endif
	{ "zero",		dozero,		true,	false },
	{ NULL,			NULL,		false,	false }
};

static void checkcommand(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	char cmdbuf[100];
	bool found, access;
	int i;

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISCHK, 0, NULL, isjson);
		return;
	}

	found = false;
	access = false;
	for (i = 0; cmds[i].name != NULL; i++) {
		if (strcmp(cmds[i].name, param) == 0) {
			found = true;

			sprintf(cmdbuf, "|%s|", param);
			if (ISPRIVGROUP(group) || strstr(COMMANDS(group), cmdbuf))
				access = true;

			break;
		}
	}

	message(io_data, MSG_CHECK, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_CHECK : _CHECK COMSTR);

	root = api_add_const(root, "Exists", found ? YES : NO, false);
	root = api_add_const(root, "Access", access ? YES : NO, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

static void head_join(struct io_data *io_data, char *cmdptr, bool isjson, bool *firstjoin)
{
	char *ptr;

	if (*firstjoin) {
		if (isjson)
			io_add(io_data, JSON0);
		*firstjoin = false;
	} else {
		if (isjson)
			io_add(io_data, JSON_BETWEEN_JOIN);
	}

	// External supplied string
	ptr = escape_string(cmdptr, isjson);

	if (isjson) {
		io_add(io_data, JSON1);
		io_add(io_data, ptr);
		io_add(io_data, JSON2);
	} else {
		io_add(io_data, JOIN_CMD);
		io_add(io_data, ptr);
		io_add(io_data, BETWEEN_JOIN);
	}

	if (ptr != cmdptr)
		free(ptr);
}

static void tail_join(struct io_data *io_data, bool isjson)
{
	if (io_data->close) {
		io_add(io_data, JSON_CLOSE);
		io_data->close = false;
	}

	if (isjson) {
		io_add(io_data, JSON_END);
		io_add(io_data, JSON3);
	}
}

static void send_result(struct io_data *io_data, SOCKETTYPE c, bool isjson)
{
	if (io_data->close)
		io_add(io_data, JSON_CLOSE);
	
	if (isjson)
		io_add(io_data, JSON_END);
	
	// Null-terminate reply, including sending the \0 on the socket
	bytes_append(&io_data->data, "", 1);
	
	applog(LOG_DEBUG, "API: send reply: (%ld) '%.10s%s'",
	       (long)bytes_len(&io_data->data),
	       bytes_buf(&io_data->data),
	       bytes_len(&io_data->data) > 10 ? "..." : BLANK);
	
	io_flush(io_data, true);
	
	if (bytes_len(&io_data->data))
		applog(LOG_WARNING, "RPC: Timed out with %ld bytes left to send",
		       (long)bytes_len(&io_data->data));
}

static
void _tidyup_socket(SOCKETTYPE * const sockp)
{
	if (*sockp != INVSOCK) {
		shutdown(*sockp, SHUT_RDWR);
		CLOSESOCKET(*sockp);
		*sockp = INVSOCK;
		free(sockp);
	}
}

static
void tidyup_socket(void * const arg)
{
	mutex_lock(&quit_restart_lock);
	_tidyup_socket(arg);
	mutex_unlock(&quit_restart_lock);
}

static void tidyup(__maybe_unused void *arg)
{
	mutex_lock(&quit_restart_lock);

	SOCKETTYPE *apisock = (SOCKETTYPE *)arg;

	bye = true;

	_tidyup_socket(apisock);

	if (ipaccess != NULL) {
		free(ipaccess);
		ipaccess = NULL;
	}

	io_free();

	mutex_unlock(&quit_restart_lock);
}

/*
 * Interpret --api-groups G:cmd1:cmd2:cmd3,P:cmd4,*,...
 */
static void setup_groups()
{
	char *api_groups = opt_api_groups ? opt_api_groups : (char *)BLANK;
	char *buf, *ptr, *next, *colon;
	char group;
	char commands[TMPBUFSIZ];
	char cmdbuf[100];
	char *cmd;
	bool addstar, did;
	int i;

	buf = malloc(strlen(api_groups) + 1);
	if (unlikely(!buf))
		quit(1, "Failed to malloc ipgroups buf");

	strcpy(buf, api_groups);

	next = buf;
	// for each group defined
	while (next && *next) {
		ptr = next;
		next = strchr(ptr, ',');
		if (next)
			*(next++) = '\0';

		// Validate the group
		if (*(ptr+1) != ':') {
			colon = strchr(ptr, ':');
			if (colon)
				*colon = '\0';
			quit(1, "API invalid group name '%s'", ptr);
		}

		group = GROUP(*ptr);
		if (!VALIDGROUP(group))
			quit(1, "API invalid group name '%c'", *ptr);

		if (group == PRIVGROUP)
			quit(1, "API group name can't be '%c'", PRIVGROUP);

		if (group == NOPRIVGROUP)
			quit(1, "API group name can't be '%c'", NOPRIVGROUP);

		if (apigroups[GROUPOFFSET(group)].commands != NULL)
			quit(1, "API duplicate group name '%c'", *ptr);

		ptr += 2;

		// Validate the command list (and handle '*')
		cmd = &(commands[0]);
		*(cmd++) = SEPARATOR;
		*cmd = '\0';
		addstar = false;
		while (ptr && *ptr) {
			colon = strchr(ptr, ':');
			if (colon)
				*(colon++) = '\0';

			if (strcmp(ptr, "*") == 0)
				addstar = true;
			else {
				did = false;
				for (i = 0; cmds[i].name != NULL; i++) {
					if (strcasecmp(ptr, cmds[i].name) == 0) {
						did = true;
						break;
					}
				}
				if (did) {
					// skip duplicates
					sprintf(cmdbuf, "|%s|", cmds[i].name);
					if (strstr(commands, cmdbuf) == NULL) {
						strcpy(cmd, cmds[i].name);
						cmd += strlen(cmds[i].name);
						*(cmd++) = SEPARATOR;
						*cmd = '\0';
					}
				} else {
					quit(1, "API unknown command '%s' in group '%c'", ptr, group);
				}
			}

			ptr = colon;
		}

		// * = allow all non-iswritemode commands
		if (addstar) {
			for (i = 0; cmds[i].name != NULL; i++) {
				if (cmds[i].iswritemode == false) {
					// skip duplicates
					sprintf(cmdbuf, "|%s|", cmds[i].name);
					if (strstr(commands, cmdbuf) == NULL) {
						strcpy(cmd, cmds[i].name);
						cmd += strlen(cmds[i].name);
						*(cmd++) = SEPARATOR;
						*cmd = '\0';
					}
				}
			}
		}

		ptr = apigroups[GROUPOFFSET(group)].commands = malloc(strlen(commands) + 1);
		if (unlikely(!ptr))
			quit(1, "Failed to malloc group commands buf");

		strcpy(ptr, commands);
	}

	// Now define R (NOPRIVGROUP) as all non-iswritemode commands
	cmd = &(commands[0]);
	*(cmd++) = SEPARATOR;
	*cmd = '\0';
	for (i = 0; cmds[i].name != NULL; i++) {
		if (cmds[i].iswritemode == false) {
			strcpy(cmd, cmds[i].name);
			cmd += strlen(cmds[i].name);
			*(cmd++) = SEPARATOR;
			*cmd = '\0';
		}
	}

	ptr = apigroups[GROUPOFFSET(NOPRIVGROUP)].commands = malloc(strlen(commands) + 1);
	if (unlikely(!ptr))
		quit(1, "Failed to malloc noprivgroup commands buf");

	strcpy(ptr, commands);

	// W (PRIVGROUP) is handled as a special case since it simply means all commands

	free(buf);
	return;
}

/*
 * Interpret [W:]IP[/Prefix][,[R|W:]IP2[/Prefix2][,...]] --api-allow option
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
	char group;

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

		group = NOPRIVGROUP;

		if (VALIDGROUP(*ptr) && *(ptr+1) == ':') {
			if (DEFINEDGROUP(*ptr))
				group = GROUP(*ptr);

			ptr += 2;
		}

		ipaccess[ips].group = group;

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
					ipaccess[ips].mask |= (octet << (24 - (8 * (mask >> 3))));
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

				ipaccess[ips].ip |= (octet << (24 - (i * 8)));

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
	RenameThread("rpc_quit");

	// allow thread creator to finish whatever it's doing
	mutex_lock(&quit_restart_lock);
	mutex_unlock(&quit_restart_lock);

	if (opt_debug)
		applog(LOG_DEBUG, "API: killing BFGMiner");

	kill_work();

	return NULL;
}

static void *restart_thread(__maybe_unused void *userdata)
{
	RenameThread("rpc_restart");

	// allow thread creator to finish whatever it's doing
	mutex_lock(&quit_restart_lock);
	mutex_unlock(&quit_restart_lock);

	if (opt_debug)
		applog(LOG_DEBUG, "API: restarting BFGMiner");

	app_restart();

	return NULL;
}

static bool check_connect(struct sockaddr_in *cli, char **connectaddr, char *group)
{
	bool addrok = false;
	int i;

	*connectaddr = inet_ntoa(cli->sin_addr);

	*group = NOPRIVGROUP;
	if (opt_api_allow) {
		int client_ip = htonl(cli->sin_addr.s_addr);
		for (i = 0; i < ips; i++) {
			if ((client_ip & ipaccess[i].mask) == ipaccess[i].ip) {
				addrok = true;
				*group = ipaccess[i].group;
				break;
			}
		}
	} else {
		if (opt_api_network)
			addrok = true;
		else
			addrok = (strcmp(*connectaddr, localaddr) == 0);
	}

	return addrok;
}

static void mcast()
{
	struct sockaddr_in listen;
	struct ip_mreq grp;
	struct sockaddr_in came_from;
	struct timeval bindstart;
	const char *binderror;
	SOCKETTYPE *mcastsock;
	SOCKETTYPE reply_sock;
	socklen_t came_from_siz;
	char *connectaddr;
	ssize_t rep;
	int bound;
	int count;
	int reply_port;
	bool addrok;
	char group;

	char expect[] = "cgminer-"; // first 8 bytes constant
	char *expect_code;
	size_t expect_code_len;
	char buf[1024];
	char replybuf[1024];

	memset(&grp, 0, sizeof(grp));
	grp.imr_multiaddr.s_addr = inet_addr(opt_api_mcast_addr);
	if (grp.imr_multiaddr.s_addr == INADDR_NONE)
		quit(1, "Invalid Multicast Address");
	grp.imr_interface.s_addr = INADDR_ANY;

	mcastsock = malloc(sizeof(*mcastsock));
	*mcastsock = INVSOCK;
	pthread_cleanup_push(tidyup_socket, mcastsock);
	
	*mcastsock = bfg_socket(AF_INET, SOCK_DGRAM, 0);
	
	int optval = 1;
	if (SOCKETFAIL(setsockopt(*mcastsock, SOL_SOCKET, SO_REUSEADDR, (void *)(&optval), sizeof(optval)))) {
		applog(LOG_ERR, "API mcast setsockopt SO_REUSEADDR failed (%s)%s", SOCKERRMSG, MUNAVAILABLE);
		goto die;
	}

	memset(&listen, 0, sizeof(listen));
	listen.sin_family = AF_INET;
	listen.sin_addr.s_addr = INADDR_ANY;
	listen.sin_port = htons(opt_api_mcast_port);

	// try for more than 1 minute ... in case the old one hasn't completely gone yet
	bound = 0;
	timer_set_now(&bindstart);
	while (bound == 0) {
		if (SOCKETFAIL(bind(*mcastsock, (struct sockaddr *)(&listen), sizeof(listen)))) {
			binderror = SOCKERRMSG;
			if (timer_elapsed(&bindstart, NULL) > 61)
				break;
			else
				cgsleep_ms(30000);
		} else
			bound = 1;
	}

	if (bound == 0) {
		applog(LOG_ERR, "API mcast bind to port %d failed (%s)%s", opt_api_port, binderror, MUNAVAILABLE);
		goto die;
	}

	if (SOCKETFAIL(setsockopt(*mcastsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)(&grp), sizeof(grp)))) {
		applog(LOG_ERR, "API mcast join failed (%s)%s", SOCKERRMSG, MUNAVAILABLE);
		goto die;
	}

	expect_code_len = sizeof(expect) + strlen(opt_api_mcast_code);
	expect_code = malloc(expect_code_len+1);
	if (!expect_code)
		quit(1, "Failed to malloc mcast expect_code");
	snprintf(expect_code, expect_code_len+1, "%s%s-", expect, opt_api_mcast_code);

	count = 0;
	while (80085) {
		cgsleep_ms(1000);

		count++;
		came_from_siz = sizeof(came_from);
		if (SOCKETFAIL(rep = recvfrom(*mcastsock, buf, sizeof(buf) - 1,
						0, (struct sockaddr *)(&came_from), &came_from_siz))) {
			applog(LOG_DEBUG, "API mcast failed count=%d (%s) (%d)",
					count, SOCKERRMSG, (int)*mcastsock);
			continue;
		}

		addrok = check_connect(&came_from, &connectaddr, &group);
		applog(LOG_DEBUG, "API mcast from %s - %s",
					connectaddr, addrok ? "Accepted" : "Ignored");
		if (!addrok)
			continue;

		buf[rep] = '\0';
		if (rep > 0 && buf[rep-1] == '\n')
			buf[--rep] = '\0';

		applog(LOG_DEBUG, "API mcast request rep=%d (%s) from %s:%d",
					(int)rep, buf,
					inet_ntoa(came_from.sin_addr),
					ntohs(came_from.sin_port));

		if ((size_t)rep > expect_code_len && memcmp(buf, expect_code, expect_code_len) == 0) {
			reply_port = atoi(&buf[expect_code_len]);
			if (reply_port < 1 || reply_port > 65535) {
				applog(LOG_DEBUG, "API mcast request ignored - invalid port (%s)",
							&buf[expect_code_len]);
			} else {
				applog(LOG_DEBUG, "API mcast request OK port %s=%d",
							&buf[expect_code_len], reply_port);

				came_from.sin_port = htons(reply_port);
				reply_sock = bfg_socket(AF_INET, SOCK_DGRAM, 0);

				snprintf(replybuf, sizeof(replybuf),
							"cgm-%s-%d-%s",
							opt_api_mcast_code,
							opt_api_port, opt_api_mcast_des);

				rep = sendto(reply_sock, replybuf, strlen(replybuf)+1,
						0, (struct sockaddr *)(&came_from),
						sizeof(came_from));
				if (SOCKETFAIL(rep)) {
					applog(LOG_DEBUG, "API mcast send reply failed (%s) (%d)",
								SOCKERRMSG, (int)reply_sock);
				} else {
					applog(LOG_DEBUG, "API mcast send reply (%s) succeeded (%d) (%d)",
								replybuf, (int)rep, (int)reply_sock);
				}

				shutdown(reply_sock, SHUT_RDWR);
				CLOSESOCKET(reply_sock);
			}
		} else
			applog(LOG_DEBUG, "API mcast request was no good");
	}

die:
	;  // statement in case pthread_cleanup_pop doesn't start with one
	pthread_cleanup_pop(true);
}

static void *mcast_thread(void *userdata)
{
	pthread_detach(pthread_self());
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("api_mcast");

	mcast();

	return NULL;
}

void mcast_init()
{
	struct thr_info *thr;

	thr = calloc(1, sizeof(*thr));
	if (!thr)
		quit(1, "Failed to calloc mcast thr");

	if (thr_info_create(thr, NULL, mcast_thread, thr))
		quit(1, "API mcast thread create failed");
}

void api(int api_thr_id)
{
	struct io_data *io_data;
	struct thr_info bye_thr;
	char buf[TMPBUFSIZ];
	char param_buf[TMPBUFSIZ];
	SOCKETTYPE c;
	int n, bound;
	char *connectaddr;
	const char *binderror;
	struct timeval bindstart;
	short int port = opt_api_port;
	struct sockaddr_in serv;
	struct sockaddr_in cli;
	socklen_t clisiz;
	char cmdbuf[100];
	char *cmd = NULL, *cmdptr, *cmdsbuf;
	char *param;
	bool addrok;
	char group;
	json_error_t json_err;
	json_t *json_config = NULL;
	json_t *json_val;
	bool isjson;
	bool did, isjoin, firstjoin;
	int i;

	SOCKETTYPE *apisock;

	if (!opt_api_listen) {
		applog(LOG_DEBUG, "API not running%s", UNAVAILABLE);
		return;
	}

	apisock = malloc(sizeof(*apisock));
	*apisock = INVSOCK;

	rpc_io_data =
	io_data = sock_io_new();

	mutex_init(&quit_restart_lock);

	pthread_cleanup_push(tidyup, (void *)apisock);
	my_thr_id = api_thr_id;

	setup_groups();

	if (opt_api_allow) {
		setup_ipaccess();

		if (ips == 0) {
			applog(LOG_WARNING, "API not running (no valid IPs specified)%s", UNAVAILABLE);
			pthread_exit(NULL);
		}
	}

	*apisock = bfg_socket(AF_INET, SOCK_STREAM, 0);
	if (*apisock == INVSOCK) {
		applog(LOG_ERR, "API1 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
		pthread_exit(NULL);
	}
	
	memset(&serv, 0, sizeof(serv));

	serv.sin_family = AF_INET;

	if (!opt_api_allow && !opt_api_network) {
		serv.sin_addr.s_addr = inet_addr(localaddr);
		if (serv.sin_addr.s_addr == (in_addr_t)INVINETADDR) {
			applog(LOG_ERR, "API2 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
			pthread_exit(NULL);
		}
	}

	serv.sin_port = htons(port);

#ifndef WIN32
	// On linux with SO_REUSEADDR, bind will get the port if the previous
	// socket is closed (even if it is still in TIME_WAIT) but fail if
	// another program has it open - which is what we want
	int optval = 1;
	// If it doesn't work, we don't really care - just show a debug message
	if (SOCKETFAIL(setsockopt(*apisock, SOL_SOCKET, SO_REUSEADDR, (void *)(&optval), sizeof(optval))))
		applog(LOG_DEBUG, "API setsockopt SO_REUSEADDR failed (ignored): %s", SOCKERRMSG);
#else
	// On windows a 2nd program can bind to a port>1024 already in use unless
	// SO_EXCLUSIVEADDRUSE is used - however then the bind to a closed port
	// in TIME_WAIT will fail until the timeout - so we leave the options alone
#endif

	// try for more than 1 minute ... in case the old one hasn't completely gone yet
	bound = 0;
	cgtime(&bindstart);
	while (bound == 0) {
		if (SOCKETFAIL(bind(*apisock, (struct sockaddr *)(&serv), sizeof(serv)))) {
			binderror = SOCKERRMSG;
			if (timer_elapsed(&bindstart, NULL) > 61)
				break;
			else {
				applog(LOG_WARNING, "API bind to port %d failed - trying again in 30sec", port);
				cgsleep_ms(30000);
			}
		} else
			bound = 1;
	}

	if (bound == 0) {
		applog(LOG_ERR, "API bind to port %d failed (%s)%s", port, binderror, UNAVAILABLE);
		pthread_exit(NULL);
	}

	if (SOCKETFAIL(listen(*apisock, QUEUE))) {
		applog(LOG_ERR, "API3 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
		pthread_exit(NULL);
	}

	if (opt_api_allow)
		applog(LOG_WARNING, "API running in IP access mode on port %d", port);
	else {
		if (opt_api_network)
			applog(LOG_WARNING, "API running in UNRESTRICTED read access mode on port %d", port);
		else
			applog(LOG_WARNING, "API running in local read access mode on port %d", port);
	}

	if (opt_api_mcast)
		mcast_init();

	while (!bye) {
		clisiz = sizeof(cli);
		if (SOCKETFAIL(c = accept(*apisock, (struct sockaddr *)(&cli), &clisiz))) {
			applog(LOG_ERR, "API failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
			goto die;
		}

		addrok = check_connect(&cli, &connectaddr, &group);
		applog(LOG_DEBUG, "API: connection from %s - %s",
					connectaddr, addrok ? "Accepted" : "Ignored");

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

			firstjoin = isjoin = false;
			if (!SOCKETFAIL(n)) {
				// the time of the request in now
				when = time(NULL);
				io_reinit(io_data);
				io_data->sock = c;

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
						message(io_data, MSG_INVJSON, 0, NULL, isjson);
						send_result(io_data, c, isjson);
						did = true;
					}
					else {
						json_val = json_object_get(json_config, JSON_COMMAND);
						if (json_val == NULL) {
							message(io_data, MSG_MISCMD, 0, NULL, isjson);
							send_result(io_data, c, isjson);
							did = true;
						}
						else {
							if (!json_is_string(json_val)) {
								message(io_data, MSG_INVCMD, 0, NULL, isjson);
								send_result(io_data, c, isjson);
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

				if (!did) {
					if (strchr(cmd, CMDJOIN)) {
						firstjoin = isjoin = true;
						// cmd + leading+tailing '|' + '\0'
						cmdsbuf = malloc(strlen(cmd) + 3);
						if (!cmdsbuf)
							quithere(1, "OOM cmdsbuf");
						strcpy(cmdsbuf, "|");
						param = NULL;
					}

					cmdptr = cmd;
					do {
						did = false;
						if (isjoin) {
							cmd = strchr(cmdptr, CMDJOIN);
							if (cmd)
								*(cmd++) = '\0';
							if (!*cmdptr)
								goto inochi;
						}

						for (i = 0; cmds[i].name != NULL; i++) {
							if (strcmp(cmdptr, cmds[i].name) == 0) {
								sprintf(cmdbuf, "|%s|", cmdptr);
								if (isjoin) {
									if (strstr(cmdsbuf, cmdbuf)) {
										did = true;
										break;
									}
									strcat(cmdsbuf, cmdptr);
									strcat(cmdsbuf, "|");
									head_join(io_data, cmdptr, isjson, &firstjoin);
									if (!cmds[i].joinable) {
										message(io_data, MSG_ACCDENY, 0, cmds[i].name, isjson);
										did = true;
										tail_join(io_data, isjson);
										break;
									}
								}
								if (ISPRIVGROUP(group) || strstr(COMMANDS(group), cmdbuf))
								{
									per_proc = !strncmp(cmds[i].name, "proc", 4);
									(cmds[i].func)(io_data, c, param, isjson, group);
								}
								else {
									message(io_data, MSG_ACCDENY, 0, cmds[i].name, isjson);
									applog(LOG_DEBUG, "API: access denied to '%s' for '%s' command", connectaddr, cmds[i].name);
								}

								did = true;
								if (!isjoin)
									send_result(io_data, c, isjson);
								else
									tail_join(io_data, isjson);
								break;
							}
						}

						if (!did) {
							if (isjoin)
								head_join(io_data, cmdptr, isjson, &firstjoin);
							message(io_data, MSG_INVCMD, 0, NULL, isjson);
							if (isjoin)
								tail_join(io_data, isjson);
							else
								send_result(io_data, c, isjson);
						}
inochi:
						if (isjoin)
							cmdptr = cmd;
					} while (isjoin && cmdptr);
				}

				if (isjson)
					json_decref(json_config);

				if (isjoin)
					send_result(io_data, c, isjson);
			}
		}
		shutdown(c, SHUT_RDWR);
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
