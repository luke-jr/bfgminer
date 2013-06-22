/*
 * Copyright 2011-2013 Andrew Smith
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
#define _MEMORY_DEBUG_MASTER 1

#include "config.h"

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
#include "util.h"

#if defined(USE_BFLSC) || defined(USE_AVALON)
#define HAVE_AN_ASIC 1
#endif

#if defined(USE_BITFORCE) || defined(USE_ICARUS) || defined(USE_ZTEX) || defined(USE_MODMINER)
#define HAVE_AN_FPGA 1
#endif

// Big enough for largest API request
//  though a PC with 100s of PGAs may exceed the size ...
//  data is truncated at the end of the last record that fits
//	but still closed correctly for JSON
// Current code assumes it can socket send this size + JSON_CLOSE + JSON_END
#define SOCKBUFSIZ	65432

// BUFSIZ varies on Windows and Linux
#define TMPBUFSIZ	8192

// Number of requests to queue - normally would be small
// However lots of PGA's may mean more
#define QUEUE	100

#if defined WIN32
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

char *WSAErrorMsg(void) {
	int i;
	int id = WSAGetLastError();

	/* Assume none of them are actually -1 */
	for (i = 0; WSAErrors[i].id != -1; i++)
		if (WSAErrors[i].id == id)
			break;

	sprintf(WSAbuf, "Socket Error: (%d) %s", id, WSAErrors[i].code);

	return &(WSAbuf[0]);
}
#endif

static const char *UNAVAILABLE = " - API will not be available";

static const char *BLANK = "";
static const char *COMMA = ",";
#define COMSTR ","
static const char SEPARATOR = '|';
#define SEPSTR "|"
static const char GPUSEP = ',';

static const char *APIVERSION = "1.26";
static const char *DEAD = "Dead";
#if defined(HAVE_OPENCL) || defined(HAVE_AN_FPGA) || defined(HAVE_AN_ASIC)
static const char *SICK = "Sick";
static const char *NOSTART = "NoStart";
static const char *INIT = "Initialising";
#endif
static const char *DISABLED = "Disabled";
static const char *ALIVE = "Alive";
static const char *REJECTING = "Rejecting";
static const char *UNKNOWN = "Unknown";
#define _DYNAMIC "D"
#ifdef HAVE_OPENCL
static const char *DYNAMIC = _DYNAMIC;
#endif

static __maybe_unused const char *NONE = "None";

static const char *YES = "Y";
static const char *NO = "N";
static const char *NULLSTR = "(null)";

static const char *TRUESTR = "true";
static const char *FALSESTR = "false";

#ifdef USE_SCRYPT
static const char *SCRYPTSTR = "scrypt";
#endif
static const char *SHA256STR = "sha256";

static const char *DEVICECODE = ""
#ifdef HAVE_OPENCL
			"GPU "
#endif
#ifdef USE_BFLSC
			"BAS "
#endif
#ifdef USE_BITFORCE
			"BFL "
#endif
#ifdef USE_ICARUS
			"ICA "
#endif
#ifdef USE_AVALON
			"AVA "
#endif
#ifdef USE_ZTEX
			"ZTX "
#endif
#ifdef USE_MODMINER
			"MMQ "
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
#define _MINECONFIG	"CONFIG"
#define _GPU		"GPU"

#ifdef HAVE_AN_FPGA
#define _PGA		"PGA"
#endif

#ifdef HAVE_AN_ASIC
#define _ASC		"ASC"
#endif

#define _GPUS		"GPUS"
#define _PGAS		"PGAS"
#define _ASCS		"ASCS"
#define _NOTIFY		"NOTIFY"
#define _DEVDETAILS	"DEVDETAILS"
#define _BYE		"BYE"
#define _RESTART	"RESTART"
#define _MINESTATS	"STATS"
#define _CHECK		"CHECK"
#define _MINECOIN	"COIN"
#define _DEBUGSET	"DEBUG"
#define _SETCONFIG	"SETCONFIG"
#define _USBSTATS	"USBSTATS"

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

#ifdef HAVE_AN_ASIC
#define JSON_ASC	JSON1 _ASC JSON2
#endif

#define JSON_GPUS	JSON1 _GPUS JSON2
#define JSON_PGAS	JSON1 _PGAS JSON2
#define JSON_ASCS	JSON1 _ASCS JSON2
#define JSON_NOTIFY	JSON1 _NOTIFY JSON2
#define JSON_DEVDETAILS	JSON1 _DEVDETAILS JSON2
#define JSON_BYE	JSON1 _BYE JSON1
#define JSON_RESTART	JSON1 _RESTART JSON1
#define JSON_CLOSE	JSON3
#define JSON_MINESTATS	JSON1 _MINESTATS JSON2
#define JSON_CHECK	JSON1 _CHECK JSON2
#define JSON_MINECOIN	JSON1 _MINECOIN JSON2
#define JSON_DEBUGSET	JSON1 _DEBUGSET JSON2
#define JSON_SETCONFIG	JSON1 _SETCONFIG JSON2
#define JSON_USBSTATS	JSON1 _USBSTATS JSON2
#define JSON_END	JSON4 JSON5
#define JSON_END_TRUNCATED	JSON4_TRUNCATED JSON5

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

#define MSG_NUMGPU 20

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
#define MSG_USBSTA 87
#define MSG_NOUSTA 88

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
#define MSG_PGAUSBNODEV 98
#define MSG_INVHPLG 99
#define MSG_HOTPLUG 100
#define MSG_DISHPLG 101
#define MSG_NOHPLG 102
#define MSG_MISHPLG 103

#define MSG_NUMASC 104
#ifdef HAVE_AN_ASIC
#define MSG_ASCNON 105
#define MSG_ASCDEV 106
#define MSG_INVASC 107
#define MSG_ASCLRENA 108
#define MSG_ASCLRDIS 109
#define MSG_ASCENA 110
#define MSG_ASCDIS 111
#define MSG_ASCUNW 112
#define MSG_ASCIDENT 113
#define MSG_ASCNOID 114
#endif
#define MSG_ASCUSBNODEV 115

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
	PARAM_ASC,
	PARAM_PID,
	PARAM_GPUMAX,
	PARAM_PGAMAX,
	PARAM_ASCMAX,
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
	PARAM_INT,
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
#if defined(HAVE_AN_ASIC) && defined(HAVE_OPENCL)
						" - "
#endif
#ifdef HAVE_AN_ASIC
						"%d ASC(s)"
#endif
#if defined(HAVE_AN_FPGA) && (defined(HAVE_OPENCL) || defined(HAVE_AN_ASIC))
						" - "
#endif
#ifdef HAVE_AN_FPGA
						"%d PGA(s)"
#endif
#if (defined(HAVE_OPENCL) || defined(HAVE_AN_ASIC) || defined(HAVE_AN_FPGA))
						" - "
#endif
 },

 { SEVERITY_ERR,   MSG_NODEVS,	PARAM_NONE,	"No GPUs"
#ifdef HAVE_AN_ASIC
						"/ASCs"
#endif
#ifdef HAVE_AN_FPGA
						"/PGAs"
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
 { SEVERITY_SUCC,  MSG_NUMGPU,	PARAM_NONE,	"GPU count" },
 { SEVERITY_SUCC,  MSG_NUMPGA,	PARAM_NONE,	"PGA count" },
 { SEVERITY_SUCC,  MSG_NUMASC,	PARAM_NONE,	"ASC count" },
 { SEVERITY_SUCC,  MSG_VERSION,	PARAM_NONE,	"CGMiner versions" },
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
 { SEVERITY_SUCC,  MSG_MINECONFIG,PARAM_NONE,	"CGMiner config" },
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
 { SEVERITY_SUCC,  MSG_MINESTATS,PARAM_NONE,	"CGMiner stats" },
 { SEVERITY_ERR,   MSG_MISCHK,	PARAM_NONE,	"Missing check cmd" },
 { SEVERITY_SUCC,  MSG_CHECK,	PARAM_NONE,	"Check command" },
 { SEVERITY_ERR,   MSG_MISBOOL,	PARAM_NONE,	"Missing parameter: true/false" },
 { SEVERITY_ERR,   MSG_INVBOOL,	PARAM_NONE,	"Invalid parameter should be true or false" },
 { SEVERITY_SUCC,  MSG_FOO,	PARAM_BOOL,	"Failover-Only set to %s" },
 { SEVERITY_SUCC,  MSG_MINECOIN,PARAM_NONE,	"CGMiner coin" },
 { SEVERITY_SUCC,  MSG_DEBUGSET,PARAM_NONE,	"Debug settings" },
#ifdef HAVE_AN_FPGA
 { SEVERITY_SUCC,  MSG_PGAIDENT,PARAM_PGA,	"Identify command sent to PGA%d" },
 { SEVERITY_WARN,  MSG_PGANOID,	PARAM_PGA,	"PGA%d does not support identify" },
#endif
 { SEVERITY_SUCC,  MSG_SETCONFIG,PARAM_SET,	"Set config '%s' to %d" },
 { SEVERITY_ERR,   MSG_UNKCON,	PARAM_STR,	"Unknown config '%s'" },
 { SEVERITY_ERR,   MSG_INVNUM,	PARAM_BOTH,	"Invalid number (%d) for '%s' range is 0-9999" },
 { SEVERITY_ERR,   MSG_CONPAR,	PARAM_NONE,	"Missing config parameters 'name,N'" },
 { SEVERITY_ERR,   MSG_CONVAL,	PARAM_STR,	"Missing config value N for '%s,N'" },
 { SEVERITY_SUCC,  MSG_USBSTA,	PARAM_NONE,	"USB Statistics" },
 { SEVERITY_INFO,  MSG_NOUSTA,	PARAM_NONE,	"No USB Statistics" },
#ifdef HAVE_AN_FPGA
 { SEVERITY_ERR,   MSG_MISPGAOPT, PARAM_NONE,	"Missing option after PGA number" },
 { SEVERITY_WARN,  MSG_PGANOSET, PARAM_PGA,	"PGA %d does not support pgaset" },
 { SEVERITY_INFO,  MSG_PGAHELP, PARAM_BOTH,	"PGA %d set help: %s" },
 { SEVERITY_SUCC,  MSG_PGASETOK, PARAM_BOTH,	"PGA %d set OK" },
 { SEVERITY_ERR,   MSG_PGASETERR, PARAM_BOTH,	"PGA %d set failed: %s" },
#endif
 { SEVERITY_ERR,   MSG_ZERMIS,	PARAM_NONE,	"Missing zero parameters" },
 { SEVERITY_ERR,   MSG_ZERINV,	PARAM_STR,	"Invalid zero parameter '%s'" },
 { SEVERITY_SUCC,  MSG_ZERSUM,	PARAM_STR,	"Zeroed %s stats with summary" },
 { SEVERITY_SUCC,  MSG_ZERNOSUM, PARAM_STR,	"Zeroed %s stats without summary" },
#ifdef USE_USBUTILS
 { SEVERITY_ERR,   MSG_PGAUSBNODEV, PARAM_PGA,	"PGA%d has no device" },
 { SEVERITY_ERR,   MSG_ASCUSBNODEV, PARAM_PGA,	"ASC%d has no device" },
#endif
 { SEVERITY_ERR,   MSG_INVHPLG,	PARAM_STR,	"Invalid value for hotplug (%s) must be 0..9999" },
 { SEVERITY_SUCC,  MSG_HOTPLUG,	PARAM_INT,	"Hotplug check set to %ds" },
 { SEVERITY_SUCC,  MSG_DISHPLG,	PARAM_NONE,	"Hotplug disabled" },
 { SEVERITY_WARN,  MSG_NOHPLG,	PARAM_NONE,	"Hotplug is not available" },
 { SEVERITY_ERR,   MSG_MISHPLG,	PARAM_NONE,	"Missing hotplug parameter" },
#ifdef HAVE_AN_ASIC
 { SEVERITY_ERR,   MSG_ASCNON,	PARAM_NONE,	"No ASCs" },
 { SEVERITY_SUCC,  MSG_ASCDEV,	PARAM_ASC,	"ASC%d" },
 { SEVERITY_ERR,   MSG_INVASC,	PARAM_ASCMAX,	"Invalid ASC id %d - range is 0 - %d" },
 { SEVERITY_INFO,  MSG_ASCLRENA,PARAM_ASC,	"ASC %d already enabled" },
 { SEVERITY_INFO,  MSG_ASCLRDIS,PARAM_ASC,	"ASC %d already disabled" },
 { SEVERITY_INFO,  MSG_ASCENA,	PARAM_ASC,	"ASC %d sent enable message" },
 { SEVERITY_INFO,  MSG_ASCDIS,	PARAM_ASC,	"ASC %d set disable flag" },
 { SEVERITY_ERR,   MSG_ASCUNW,	PARAM_ASC,	"ASC %d is not flagged WELL, cannot enable" },
 { SEVERITY_SUCC,  MSG_ASCIDENT,PARAM_ASC,	"Identify command sent to ASC%d" },
 { SEVERITY_WARN,  MSG_ASCNOID,	PARAM_ASC,	"ASC%d does not support identify" },
#endif
 { SEVERITY_FAIL, 0, 0, NULL }
};

static int my_thr_id = 0;
static bool bye;

// Used to control quit restart access to shutdown variables
static pthread_mutex_t quit_restart_lock;

static bool do_a_quit;
static bool do_a_restart;

static time_t when = 0;	// when the request occurred

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
	size_t siz;
	char *ptr;
	char *cur;
	bool sock;
	bool full;
	bool close;
};

struct io_list {
	struct io_data *io_data;
	struct io_list *prev;
	struct io_list *next;
};

static struct io_list *io_head = NULL;

#define io_new(init) _io_new(init, false)
#define sock_io_new() _io_new(SOCKBUFSIZ, true)

static void io_reinit(struct io_data *io_data)
{
	io_data->cur = io_data->ptr;
	*(io_data->ptr) = '\0';
	io_data->full = false;
	io_data->close = false;
}

static struct io_data *_io_new(size_t initial, bool socket_buf)
{
	struct io_data *io_data;
	struct io_list *io_list;

	io_data = malloc(sizeof(*io_data));
	io_data->ptr = malloc(initial);
	io_data->siz = initial;
	io_data->sock = socket_buf;
	io_reinit(io_data);

	io_list = malloc(sizeof(*io_list));

	io_list->io_data = io_data;

	if (io_head) {
		io_list->next = io_head;
		io_list->prev = io_head->prev;
		io_list->next->prev = io_list;
		io_list->prev->next = io_list;
	} else {
		io_list->prev = io_list;
		io_list->next = io_list;
		io_head = io_list;
	}

	return io_data;
}

static bool io_add(struct io_data *io_data, char *buf)
{
	size_t len, dif, tot;

	if (io_data->full)
		return false;

	len = strlen(buf);
	dif = io_data->cur - io_data->ptr;
	tot = len + 1 + dif;

	if (tot > io_data->siz) {
		size_t new = io_data->siz * 2;

		if (new < tot)
			new = tot * 2;

		if (io_data->sock) {
			if (new > SOCKBUFSIZ) {
				if (tot > SOCKBUFSIZ) {
					io_data->full = true;
					return false;
				}

				new = SOCKBUFSIZ;
			}
		}

		io_data->ptr = realloc(io_data->ptr, new);
		io_data->cur = io_data->ptr + dif;
		io_data->siz = new;
	}

	memcpy(io_data->cur, buf, len + 1);
	io_data->cur += len;

	return true;
}

static bool io_put(struct io_data *io_data, char *buf)
{
	io_reinit(io_data);
	return io_add(io_data, buf);
}

static void io_close(struct io_data *io_data)
{
	io_data->close = true;
}

static void io_free()
{
	struct io_list *io_list, *io_next;

	if (io_head) {
		io_list = io_head;
		do {
			io_next = io_list->next;

			free(io_list->io_data);
			free(io_list);

			io_list = io_next;
		} while (io_list != io_head);

		io_head = NULL;
	}
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

static struct api_data *api_add_data_full(struct api_data *root, char *name, enum api_data_type type, void *data, bool copy_data)
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
		data = (void *)NULLSTR;
		api_data->data_was_malloc = copy_data = false;
	}

	if (!copy_data)
		api_data->data = data;
	else
		switch(type) {
			case API_ESCAPE:
			case API_STRING:
			case API_CONST:
				api_data->data = (void *)malloc(strlen((char *)data) + 1);
				strcpy((char*)(api_data->data), (char *)data);
				break;
			case API_INT:
				api_data->data = (void *)malloc(sizeof(int));
				*((int *)(api_data->data)) = *((int *)data);
				break;
			case API_UINT:
				api_data->data = (void *)malloc(sizeof(unsigned int));
				*((unsigned int *)(api_data->data)) = *((unsigned int *)data);
				break;
			case API_UINT32:
				api_data->data = (void *)malloc(sizeof(uint32_t));
				*((uint32_t *)(api_data->data)) = *((uint32_t *)data);
				break;
			case API_UINT64:
				api_data->data = (void *)malloc(sizeof(uint64_t));
				*((uint64_t *)(api_data->data)) = *((uint64_t *)data);
				break;
			case API_DOUBLE:
			case API_ELAPSED:
			case API_MHS:
			case API_MHTOTAL:
			case API_UTILITY:
			case API_FREQ:
			case API_HS:
			case API_DIFF:
				api_data->data = (void *)malloc(sizeof(double));
				*((double *)(api_data->data)) = *((double *)data);
				break;
			case API_BOOL:
				api_data->data = (void *)malloc(sizeof(bool));
				*((bool *)(api_data->data)) = *((bool *)data);
				break;
			case API_TIMEVAL:
				api_data->data = (void *)malloc(sizeof(struct timeval));
				memcpy(api_data->data, data, sizeof(struct timeval));
				break;
			case API_TIME:
				api_data->data = (void *)malloc(sizeof(time_t));
				*(time_t *)(api_data->data) = *((time_t *)data);
				break;
			case API_VOLTS:
			case API_TEMP:
				api_data->data = (void *)malloc(sizeof(float));
				*((float *)(api_data->data)) = *((float *)data);
				break;
			default:
				applog(LOG_ERR, "API: unknown1 data type %d ignored", type);
				api_data->type = API_STRING;
				api_data->data_was_malloc = false;
				api_data->data = (void *)UNKNOWN;
				break;
		}

	return root;
}

struct api_data *api_add_escape(struct api_data *root, char *name, char *data, bool copy_data)
{
	return api_add_data_full(root, name, API_ESCAPE, (void *)data, copy_data);
}

struct api_data *api_add_string(struct api_data *root, char *name, char *data, bool copy_data)
{
	return api_add_data_full(root, name, API_STRING, (void *)data, copy_data);
}

struct api_data *api_add_const(struct api_data *root, char *name, const char *data, bool copy_data)
{
	return api_add_data_full(root, name, API_CONST, (void *)data, copy_data);
}

struct api_data *api_add_int(struct api_data *root, char *name, int *data, bool copy_data)
{
	return api_add_data_full(root, name, API_INT, (void *)data, copy_data);
}

struct api_data *api_add_uint(struct api_data *root, char *name, unsigned int *data, bool copy_data)
{
	return api_add_data_full(root, name, API_UINT, (void *)data, copy_data);
}

struct api_data *api_add_uint32(struct api_data *root, char *name, uint32_t *data, bool copy_data)
{
	return api_add_data_full(root, name, API_UINT32, (void *)data, copy_data);
}

struct api_data *api_add_uint64(struct api_data *root, char *name, uint64_t *data, bool copy_data)
{
	return api_add_data_full(root, name, API_UINT64, (void *)data, copy_data);
}

struct api_data *api_add_double(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_DOUBLE, (void *)data, copy_data);
}

struct api_data *api_add_elapsed(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_ELAPSED, (void *)data, copy_data);
}

struct api_data *api_add_bool(struct api_data *root, char *name, bool *data, bool copy_data)
{
	return api_add_data_full(root, name, API_BOOL, (void *)data, copy_data);
}

struct api_data *api_add_timeval(struct api_data *root, char *name, struct timeval *data, bool copy_data)
{
	return api_add_data_full(root, name, API_TIMEVAL, (void *)data, copy_data);
}

struct api_data *api_add_time(struct api_data *root, char *name, time_t *data, bool copy_data)
{
	return api_add_data_full(root, name, API_TIME, (void *)data, copy_data);
}

struct api_data *api_add_mhs(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_MHS, (void *)data, copy_data);
}

struct api_data *api_add_mhtotal(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_MHTOTAL, (void *)data, copy_data);
}

struct api_data *api_add_temp(struct api_data *root, char *name, float *data, bool copy_data)
{
	return api_add_data_full(root, name, API_TEMP, (void *)data, copy_data);
}

struct api_data *api_add_utility(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_UTILITY, (void *)data, copy_data);
}

struct api_data *api_add_freq(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_FREQ, (void *)data, copy_data);
}

struct api_data *api_add_volts(struct api_data *root, char *name, float *data, bool copy_data)
{
	return api_add_data_full(root, name, API_VOLTS, (void *)data, copy_data);
}

struct api_data *api_add_hs(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_HS, (void *)data, copy_data);
}

struct api_data *api_add_diff(struct api_data *root, char *name, double *data, bool copy_data)
{
	return api_add_data_full(root, name, API_DIFF, (void *)data, copy_data);
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
				sprintf(buf, "%.2f", *((double *)(root->data)));
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
				sprintf(buf, "%.8f", *((double *)(root->data)));
				break;
			case API_BOOL:
				sprintf(buf, "%s", *((bool *)(root->data)) ? TRUESTR : FALSESTR);
				break;
			case API_TIMEVAL:
				sprintf(buf, "%ld.%06ld",
					((struct timeval *)(root->data))->tv_sec,
					((struct timeval *)(root->data))->tv_usec);
				break;
			case API_TEMP:
				sprintf(buf, "%.2f", *((float *)(root->data)));
				break;
			default:
				applog(LOG_ERR, "API: unknown2 data type %d ignored", root->type);
				sprintf(buf, "%s%s%s", quote, UNKNOWN, quote);
				break;
		}

		buf = strchr(buf, '\0');

		free(root->name);
		if (root->data_was_malloc)
			free(root->data);

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

#ifdef HAVE_AN_ASIC
static int numascs()
{
	int count = 0;
	int i;

	rd_lock(&devices_lock);
	for (i = 0; i < total_devices; i++) {
#ifdef USE_AVALON
		if (devices[i]->drv->drv_id == DRIVER_AVALON)
			count++;
#endif
#ifdef USE_BFLSC
		if (devices[i]->drv->drv_id == DRIVER_BFLSC)
			count++;
#endif
	}
	rd_unlock(&devices_lock);
	return count;
}

static int ascdevice(int ascid)
{
	int count = 0;
	int i;

	rd_lock(&devices_lock);
	for (i = 0; i < total_devices; i++) {
#ifdef USE_AVALON
		if (devices[i]->drv->drv_id == DRIVER_AVALON)
			count++;
#endif
#ifdef USE_BFLSC
		if (devices[i]->drv->drv_id == DRIVER_BFLSC)
			count++;
#endif
		if (count == (ascid + 1))
			goto foundit;
	}

	rd_unlock(&devices_lock);
	return -1;

foundit:

	rd_unlock(&devices_lock);
	return i;
}
#endif

#ifdef HAVE_AN_FPGA
static int numpgas()
{
	int count = 0;
	int i;

	rd_lock(&devices_lock);
	for (i = 0; i < total_devices; i++) {
#ifdef USE_BITFORCE
		if (devices[i]->drv->drv_id == DRIVER_BITFORCE)
			count++;
#endif
#ifdef USE_ICARUS
		if (devices[i]->drv->drv_id == DRIVER_ICARUS)
			count++;
#endif
#ifdef USE_ZTEX
		if (devices[i]->drv->drv_id == DRIVER_ZTEX)
			count++;
#endif
#ifdef USE_MODMINER
		if (devices[i]->drv->drv_id == DRIVER_MODMINER)
			count++;
#endif
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
#ifdef USE_BITFORCE
		if (devices[i]->drv->drv_id == DRIVER_BITFORCE)
			count++;
#endif
#ifdef USE_ICARUS
		if (devices[i]->drv->drv_id == DRIVER_ICARUS)
			count++;
#endif
#ifdef USE_ZTEX
		if (devices[i]->drv->drv_id == DRIVER_ZTEX)
			count++;
#endif
#ifdef USE_MODMINER
		if (devices[i]->drv->drv_id == DRIVER_MODMINER)
			count++;
#endif
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
static void message(struct io_data *io_data, int messageid, int paramid, char *param2, bool isjson)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	char buf2[TMPBUFSIZ];
	char severity[2];
#ifdef HAVE_AN_ASIC
	int asc;
#endif
#ifdef HAVE_AN_FPGA
	int pga;
#endif
	int i;

	io_reinit(io_data);

	if (isjson)
		io_put(io_data, JSON_START JSON_STATUS);

	for (i = 0; codes[i].severity != SEVERITY_FAIL; i++) {
		if (codes[i].code == messageid) {
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
				case PARAM_GPU:
				case PARAM_PGA:
				case PARAM_ASC:
				case PARAM_PID:
				case PARAM_INT:
					sprintf(buf, codes[i].description, paramid);
					break;
				case PARAM_POOL:
					sprintf(buf, codes[i].description, paramid, pools[paramid]->rpc_url);
					break;
#ifdef HAVE_OPENCL
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
#ifdef HAVE_AN_ASIC
				case PARAM_ASCMAX:
					asc = numascs();
					sprintf(buf, codes[i].description, paramid, asc - 1);
					break;
#endif
				case PARAM_PMAX:
					sprintf(buf, codes[i].description, total_pools);
					break;
				case PARAM_POOLMAX:
					sprintf(buf, codes[i].description, paramid, total_pools - 1);
					break;
				case PARAM_DMAX:
#ifdef HAVE_AN_ASIC
					asc = numascs();
#endif
#ifdef HAVE_AN_FPGA
					pga = numpgas();
#endif

					sprintf(buf, codes[i].description
#ifdef HAVE_OPENCL
						, nDevs
#endif
#ifdef HAVE_AN_ASIC
						, asc
#endif
#ifdef HAVE_AN_FPGA
						, pga
#endif
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

	root = api_add_string(root, "CGMiner", VERSION, false);
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
	int gpucount = 0;
	int asccount = 0;
	int pgacount = 0;
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

#ifdef HAVE_AN_ASIC
	asccount = numascs();
#endif

#ifdef HAVE_AN_FPGA
	pgacount = numpgas();
#endif

	message(io_data, MSG_MINECONFIG, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_MINECONFIG : _MINECONFIG COMSTR);

	root = api_add_int(root, "GPU Count", &gpucount, false);
	root = api_add_int(root, "ASC Count", &asccount, false);
	root = api_add_int(root, "PGA Count", &pgacount, false);
	root = api_add_int(root, "Pool Count", &total_pools, false);
	root = api_add_const(root, "ADL", (char *)adl, false);
	root = api_add_string(root, "ADL in use", adlinuse, false);
	root = api_add_const(root, "Strategy", strategies[pool_strategy].s, false);
	root = api_add_int(root, "Log Interval", &opt_log_interval, false);
	root = api_add_const(root, "Device Code", DEVICECODE, false);
	root = api_add_const(root, "OS", OSINFO, false);
	root = api_add_bool(root, "Failover-Only", &opt_fail_only, false);
	root = api_add_int(root, "ScanTime", &opt_scantime, false);
	root = api_add_int(root, "Queue", &opt_queue, false);
	root = api_add_int(root, "Expiry", &opt_expiry, false);
#ifdef USE_USBUTILS
	if (hotplug_time == 0)
		root = api_add_const(root, "Hotplug", DISABLED, false);
	else
		root = api_add_int(root, "Hotplug", &hotplug_time, false);
#else
	root = api_add_const(root, "Hotplug", NONE, false);
#endif

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

#if defined(HAVE_OPENCL) || defined(HAVE_AN_FPGA) || defined(HAVE_AN_ASIC)
static const char *status2str(enum alive status)
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
		case LIFE_INIT:
			return INIT;
		default:
			return UNKNOWN;
	}
}
#endif

#ifdef HAVE_OPENCL
static void gpustatus(struct io_data *io_data, int gpu, bool isjson, bool precom)
{
	struct api_data *root = NULL;
	char intensity[20];
	char buf[TMPBUFSIZ];
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

		if (cgpu->deven != DEV_DISABLED)
			enabled = (char *)YES;
		else
			enabled = (char *)NO;

		status = (char *)status2str(cgpu->status);

		if (cgpu->dynamic)
			strcpy(intensity, DYNAMIC);
		else
			sprintf(intensity, "%d", cgpu->intensity);

		root = api_add_int(root, "GPU", &gpu, false);
		root = api_add_string(root, "Enabled", enabled, false);
		root = api_add_string(root, "Status", status, false);
		root = api_add_temp(root, "Temperature", &gt, false);
		root = api_add_int(root, "Fan Speed", &gf, false);
		root = api_add_int(root, "Fan Percent", &gp, false);
		root = api_add_int(root, "GPU Clock", &gc, false);
		root = api_add_int(root, "Memory Clock", &gm, false);
		root = api_add_volts(root, "GPU Voltage", &gv, false);
		root = api_add_int(root, "GPU Activity", &ga, false);
		root = api_add_int(root, "Powertune", &pt, false);
		double mhs = cgpu->total_mhashes / total_secs;
		root = api_add_mhs(root, "MHS av", &mhs, false);
		char mhsname[27];
		sprintf(mhsname, "MHS %ds", opt_log_interval);
		root = api_add_mhs(root, mhsname, &(cgpu->rolling), false);
		root = api_add_int(root, "Accepted", &(cgpu->accepted), false);
		root = api_add_int(root, "Rejected", &(cgpu->rejected), false);
		root = api_add_int(root, "Hardware Errors", &(cgpu->hw_errors), false);
		root = api_add_utility(root, "Utility", &(cgpu->utility), false);
		root = api_add_string(root, "Intensity", intensity, false);
		int last_share_pool = cgpu->last_share_pool_time > 0 ?
					cgpu->last_share_pool : -1;
		root = api_add_int(root, "Last Share Pool", &last_share_pool, false);
		root = api_add_time(root, "Last Share Time", &(cgpu->last_share_pool_time), false);
		root = api_add_mhtotal(root, "Total MH", &(cgpu->total_mhashes), false);
		root = api_add_int(root, "Diff1 Work", &(cgpu->diff1), false);
		root = api_add_diff(root, "Difficulty Accepted", &(cgpu->diff_accepted), false);
		root = api_add_diff(root, "Difficulty Rejected", &(cgpu->diff_rejected), false);
		root = api_add_diff(root, "Last Share Difficulty", &(cgpu->last_share_diff), false);
		root = api_add_time(root, "Last Valid Work", &(cgpu->last_device_valid_work), false);

		root = print_data(root, buf, isjson, precom);
		io_add(io_data, buf);
	}
}
#endif

#ifdef HAVE_AN_ASIC
static void ascstatus(struct io_data *io_data, int asc, bool isjson, bool precom)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	char *enabled;
	char *status;
	int numasc = numascs();

	if (numasc > 0 && asc >= 0 && asc < numasc) {
		int dev = ascdevice(asc);
		if (dev < 0) // Should never happen
			return;

		struct cgpu_info *cgpu = get_devices(dev);
		float temp = cgpu->temp;
		struct timeval now;
		double dev_runtime;

		if (cgpu->dev_start_tv.tv_sec == 0)
			dev_runtime = total_secs;
		else {
			cgtime(&now);
			dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
		}

		if (dev_runtime < 1.0)
			dev_runtime = 1.0;

		cgpu->utility = cgpu->accepted / dev_runtime * 60;

		if (cgpu->deven != DEV_DISABLED)
			enabled = (char *)YES;
		else
			enabled = (char *)NO;

		status = (char *)status2str(cgpu->status);

		root = api_add_int(root, "ASC", &asc, false);
		root = api_add_string(root, "Name", cgpu->drv->name, false);
		root = api_add_int(root, "ID", &(cgpu->device_id), false);
		root = api_add_string(root, "Enabled", enabled, false);
		root = api_add_string(root, "Status", status, false);
		root = api_add_temp(root, "Temperature", &temp, false);
		double mhs = cgpu->total_mhashes / dev_runtime;
		root = api_add_mhs(root, "MHS av", &mhs, false);
		char mhsname[27];
		sprintf(mhsname, "MHS %ds", opt_log_interval);
		root = api_add_mhs(root, mhsname, &(cgpu->rolling), false);
		root = api_add_int(root, "Accepted", &(cgpu->accepted), false);
		root = api_add_int(root, "Rejected", &(cgpu->rejected), false);
		root = api_add_int(root, "Hardware Errors", &(cgpu->hw_errors), false);
		root = api_add_utility(root, "Utility", &(cgpu->utility), false);
		int last_share_pool = cgpu->last_share_pool_time > 0 ?
					cgpu->last_share_pool : -1;
		root = api_add_int(root, "Last Share Pool", &last_share_pool, false);
		root = api_add_time(root, "Last Share Time", &(cgpu->last_share_pool_time), false);
		root = api_add_mhtotal(root, "Total MH", &(cgpu->total_mhashes), false);
		root = api_add_int(root, "Diff1 Work", &(cgpu->diff1), false);
		root = api_add_diff(root, "Difficulty Accepted", &(cgpu->diff_accepted), false);
		root = api_add_diff(root, "Difficulty Rejected", &(cgpu->diff_rejected), false);
		root = api_add_diff(root, "Last Share Difficulty", &(cgpu->last_share_diff), false);
#ifdef USE_USBUTILS
		root = api_add_bool(root, "No Device", &(cgpu->usbinfo.nodev), false);
#endif
		root = api_add_time(root, "Last Valid Work", &(cgpu->last_device_valid_work), false);

		root = print_data(root, buf, isjson, precom);
		io_add(io_data, buf);
	}
}
#endif

#ifdef HAVE_AN_FPGA
static void pgastatus(struct io_data *io_data, int pga, bool isjson, bool precom)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	char *enabled;
	char *status;
	int numpga = numpgas();

	if (numpga > 0 && pga >= 0 && pga < numpga) {
		int dev = pgadevice(pga);
		if (dev < 0) // Should never happen
			return;

		struct cgpu_info *cgpu = get_devices(dev);
		double frequency = 0;
		float temp = cgpu->temp;
		struct timeval now;
		double dev_runtime;

		if (cgpu->dev_start_tv.tv_sec == 0)
			dev_runtime = total_secs;
		else {
			cgtime(&now);
			dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
		}

		if (dev_runtime < 1.0)
			dev_runtime = 1.0;

#ifdef USE_ZTEX
		if (cgpu->drv->drv_id == DRIVER_ZTEX && cgpu->device_ztex)
			frequency = cgpu->device_ztex->freqM1 * (cgpu->device_ztex->freqM + 1);
#endif
#ifdef USE_MODMINER
		if (cgpu->drv->drv_id == DRIVER_MODMINER)
			frequency = cgpu->clock;
#endif

		cgpu->utility = cgpu->accepted / dev_runtime * 60;

		if (cgpu->deven != DEV_DISABLED)
			enabled = (char *)YES;
		else
			enabled = (char *)NO;

		status = (char *)status2str(cgpu->status);

		root = api_add_int(root, "PGA", &pga, false);
		root = api_add_string(root, "Name", cgpu->drv->name, false);
		root = api_add_int(root, "ID", &(cgpu->device_id), false);
		root = api_add_string(root, "Enabled", enabled, false);
		root = api_add_string(root, "Status", status, false);
		root = api_add_temp(root, "Temperature", &temp, false);
		double mhs = cgpu->total_mhashes / dev_runtime;
		root = api_add_mhs(root, "MHS av", &mhs, false);
		char mhsname[27];
		sprintf(mhsname, "MHS %ds", opt_log_interval);
		root = api_add_mhs(root, mhsname, &(cgpu->rolling), false);
		root = api_add_int(root, "Accepted", &(cgpu->accepted), false);
		root = api_add_int(root, "Rejected", &(cgpu->rejected), false);
		root = api_add_int(root, "Hardware Errors", &(cgpu->hw_errors), false);
		root = api_add_utility(root, "Utility", &(cgpu->utility), false);
		int last_share_pool = cgpu->last_share_pool_time > 0 ?
					cgpu->last_share_pool : -1;
		root = api_add_int(root, "Last Share Pool", &last_share_pool, false);
		root = api_add_time(root, "Last Share Time", &(cgpu->last_share_pool_time), false);
		root = api_add_mhtotal(root, "Total MH", &(cgpu->total_mhashes), false);
		root = api_add_freq(root, "Frequency", &frequency, false);
		root = api_add_int(root, "Diff1 Work", &(cgpu->diff1), false);
		root = api_add_diff(root, "Difficulty Accepted", &(cgpu->diff_accepted), false);
		root = api_add_diff(root, "Difficulty Rejected", &(cgpu->diff_rejected), false);
		root = api_add_diff(root, "Last Share Difficulty", &(cgpu->last_share_diff), false);
#ifdef USE_USBUTILS
		root = api_add_bool(root, "No Device", &(cgpu->usbinfo.nodev), false);
#endif
		root = api_add_time(root, "Last Valid Work", &(cgpu->last_device_valid_work), false);

		root = print_data(root, buf, isjson, precom);
		io_add(io_data, buf);
	}
}
#endif

static void devstatus(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	bool io_open = false;
	int devcount = 0;
	int numgpu = 0;
	int numasc = 0;
	int numpga = 0;
	int i;

#ifdef HAVE_OPENCL
	numgpu = nDevs;
#endif

#ifdef HAVE_AN_ASIC
	numasc = numascs();
#endif

#ifdef HAVE_AN_FPGA
	numpga = numpgas();
#endif

	if (numgpu == 0 && numpga == 0 && numasc == 0) {
		message(io_data, MSG_NODEVS, 0, NULL, isjson);
		return;
	}


	message(io_data, MSG_DEVS, 0, NULL, isjson);
	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_DEVS);

#ifdef HAVE_OPENCL
	for (i = 0; i < nDevs; i++) {
		gpustatus(io_data, i, isjson, isjson && devcount > 0);

		devcount++;
	}
#endif
#ifdef HAVE_AN_ASIC
	if (numasc > 0) {
		for (i = 0; i < numasc; i++) {
			ascstatus(io_data, i, isjson, isjson && devcount > 0);

			devcount++;
		}
	}
#endif

#ifdef HAVE_AN_FPGA
	if (numpga > 0) {
		for (i = 0; i < numpga; i++) {
			pgastatus(io_data, i, isjson, isjson && devcount > 0);

			devcount++;
		}
	}
#endif

	if (isjson && io_open)
		io_close(io_data);
}

#ifdef HAVE_OPENCL
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

#ifdef HAVE_AN_FPGA
static void pgadev(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	bool io_open = false;
	int numpga = numpgas();
	int id;

	if (numpga == 0) {
		message(io_data, MSG_PGANON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numpga) {
		message(io_data, MSG_INVPGA, id, NULL, isjson);
		return;
	}

	message(io_data, MSG_PGADEV, id, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_PGA);

	pgastatus(io_data, id, isjson, false);

	if (isjson && io_open)
		io_close(io_data);
}

static void pgaenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	int numpga = numpgas();
	struct thr_info *thr;
	int pga;
	int id;
	int i;

	if (numpga == 0) {
		message(io_data, MSG_PGANON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
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

	applog(LOG_DEBUG, "API: request to pgaenable pgaid %d device %d %s%u",
			id, dev, cgpu->drv->name, cgpu->device_id);

	if (cgpu->deven != DEV_DISABLED) {
		message(io_data, MSG_PGALRENA, id, NULL, isjson);
		return;
	}

#if 0 /* A DISABLED device wont change status FIXME: should disabling make it WELL? */
	if (cgpu->status != LIFE_WELL) {
		message(io_data, MSG_PGAUNW, id, NULL, isjson);
		return;
	}
#endif

#ifdef USE_USBUTILS
	if (cgpu->usbinfo.nodev) {
		message(io_data, MSG_PGAUSBNODEV, id, NULL, isjson);
		return;
	}
#endif

	for (i = 0; i < mining_threads; i++) {
		thr = get_thread(i);
		pga = thr->cgpu->cgminer_id;
		if (pga == dev) {
			cgpu->deven = DEV_ENABLED;
			applog(LOG_DEBUG, "API: Pushing sem post to thread %d", thr->id);
			cgsem_post(&thr->sem);
		}
	}

	message(io_data, MSG_PGAENA, id, NULL, isjson);
}

static void pgadisable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	int numpga = numpgas();
	int id;

	if (numpga == 0) {
		message(io_data, MSG_PGANON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
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

	applog(LOG_DEBUG, "API: request to pgadisable pgaid %d device %d %s%u",
			id, dev, cgpu->drv->name, cgpu->device_id);

	if (cgpu->deven == DEV_DISABLED) {
		message(io_data, MSG_PGALRDIS, id, NULL, isjson);
		return;
	}

	cgpu->deven = DEV_DISABLED;

	message(io_data, MSG_PGADIS, id, NULL, isjson);
}

static void pgaidentify(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	struct device_drv *drv;
	int numpga = numpgas();
	int id;

	if (numpga == 0) {
		message(io_data, MSG_PGANON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
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
	drv = cgpu->drv;

	if (!drv->identify_device)
		message(io_data, MSG_PGANOID, id, NULL, isjson);
	else {
		drv->identify_device(cgpu);
		message(io_data, MSG_PGAIDENT, id, NULL, isjson);
	}
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
		root = api_add_string(root, "Long Poll", lp, false);
		root = api_add_uint(root, "Getworks", &(pool->getwork_requested), false);
		root = api_add_int(root, "Accepted", &(pool->accepted), false);
		root = api_add_int(root, "Rejected", &(pool->rejected), false);
		root = api_add_uint(root, "Discarded", &(pool->discarded_work), false);
		root = api_add_uint(root, "Stale", &(pool->stale_shares), false);
		root = api_add_uint(root, "Get Failures", &(pool->getfail_occasions), false);
		root = api_add_uint(root, "Remote Failures", &(pool->remotefail_occasions), false);
		root = api_add_escape(root, "User", pool->rpc_user, false);
		root = api_add_time(root, "Last Share Time", &(pool->last_share_time), false);
		root = api_add_int(root, "Diff1 Shares", &(pool->diff1), false);
		if (pool->rpc_proxy) {
			root = api_add_const(root, "Proxy Type", proxytype(pool->rpc_proxytype), false);
			root = api_add_escape(root, "Proxy", pool->rpc_proxy, false);
		} else {
			root = api_add_const(root, "Proxy Type", BLANK, false);
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
		root = api_add_bool(root, "Has GBT", &(pool->has_gbt), false);
		root = api_add_uint64(root, "Best Share", &(pool->best_diff), true);

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
	root = api_add_mhs(root, "MHS av", &(mhs), false);
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
	root = api_add_utility(root, "Work Utility", &(work_utility), false);
	root = api_add_diff(root, "Difficulty Accepted", &(total_diff_accepted), true);
	root = api_add_diff(root, "Difficulty Rejected", &(total_diff_rejected), true);
	root = api_add_diff(root, "Difficulty Stale", &(total_diff_stale), true);
	root = api_add_uint64(root, "Best Share", &(best_diff), true);

	mutex_unlock(&hash_lock);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

#ifdef HAVE_OPENCL
static void gpuenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct thr_info *thr;
	int gpu;
	int id;
	int i;

	if (gpu_threads == 0) {
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

	applog(LOG_DEBUG, "API: request to gpuenable gpuid %d %s%u",
			id, gpus[id].drv->name, gpus[id].device_id);

	if (gpus[id].deven != DEV_DISABLED) {
		message(io_data, MSG_ALRENA, id, NULL, isjson);
		return;
	}

	for (i = 0; i < gpu_threads; i++) {
		thr = get_thread(i);
		gpu = thr->cgpu->device_id;
		if (gpu == id) {
			if (thr->cgpu->status != LIFE_WELL) {
				message(io_data, MSG_GPUMRE, id, NULL, isjson);
				return;
			}
			gpus[id].deven = DEV_ENABLED;
			applog(LOG_DEBUG, "API Pushing sem post to thread %d", thr->id);
			cgsem_post(&thr->sem);
		}
	}

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

	applog(LOG_DEBUG, "API: request to gpudisable gpuid %d %s%u",
			id, gpus[id].drv->name, gpus[id].device_id);

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

#ifdef HAVE_OPENCL
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
	pool->enabled = POOL_ENABLED;
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

static void addpool(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	char *url, *user, *pass;
	struct pool *pool;
	char *ptr;

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPDP, 0, NULL, isjson);
		return;
	}

	if (!pooldetails(param, &url, &user, &pass)) {
		ptr = escape_string(param, isjson);
		message(io_data, MSG_INVPDP, 0, ptr, isjson);
		if (ptr != param)
			free(ptr);
		ptr = NULL;
		return;
	}

	pool = add_pool();
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
	if (pool->enabled == POOL_ENABLED) {
		message(io_data, MSG_ALRENAP, id, NULL, isjson);
		return;
	}

	pool->enabled = POOL_ENABLED;
	if (pool->prio < current_pool()->prio)
		switch_pools(pool);

	message(io_data, MSG_ENAPOOL, id, NULL, isjson);
}

static void poolpriority(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	char *ptr, *next;
	int i, pr, prio = 0;

	// TODO: all cgminer code needs a mutex added everywhere for change
	//	access to total_pools and also parts of the pools[] array,
	//	just copying total_pools here wont solve that

	if (total_pools == 0) {
		message(io_data, MSG_NOPOOL, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISPID, 0, NULL, isjson);
		return;
	}

	bool pools_changed[total_pools];
	int new_prio[total_pools];
	for (i = 0; i < total_pools; ++i)
		pools_changed[i] = false;

	next = param;
	while (next && *next) {
		ptr = next;
		next = strchr(ptr, ',');
		if (next)
			*(next++) = '\0';

		i = atoi(ptr);
		if (i < 0 || i >= total_pools) {
			message(io_data, MSG_INVPID, i, NULL, isjson);
			return;
		}

		if (pools_changed[i]) {
			message(io_data, MSG_DUPPID, i, NULL, isjson);
			return;
		}

		pools_changed[i] = true;
		new_prio[i] = prio++;
	}

	// Only change them if no errors
	for (i = 0; i < total_pools; i++) {
		if (pools_changed[i])
			pools[i]->prio = new_prio[i];
	}

	// In priority order, cycle through the unchanged pools and append them
	for (pr = 0; pr < total_pools; pr++)
		for (i = 0; i < total_pools; i++) {
			if (!pools_changed[i] && pools[i]->prio == pr) {
				pools[i]->prio = prio++;
				pools_changed[i] = true;
				break;
			}
		}

	if (current_pool()->prio)
		switch_pools(NULL);

	message(io_data, MSG_POOLPRIO, 0, NULL, isjson);
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

	pool->enabled = POOL_DISABLED;
	if (pool == current_pool())
		switch_pools(NULL);

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

	pool->enabled = POOL_DISABLED;
	rpc_url = escape_string(pool->rpc_url, isjson);
	if (rpc_url != pool->rpc_url)
		dofree = true;

	remove_pool(pool);

	message(io_data, MSG_REMPOOL, id, rpc_url, isjson);

	if (dofree)
		free(rpc_url);
	rpc_url = NULL;
}

#ifdef HAVE_OPENCL
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
	int intensity;
	char intensitystr[7];

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	if (!strncasecmp(value, DYNAMIC, 1)) {
		gpus[id].dynamic = true;
		strcpy(intensitystr, DYNAMIC);
	}
	else {
		intensity = atoi(value);
		if (intensity < MIN_INTENSITY || intensity > MAX_INTENSITY) {
			message(io_data, MSG_INVINT, 0, value, isjson);
			return;
		}

		gpus[id].dynamic = false;
		gpus[id].intensity = intensity;
		sprintf(intensitystr, "%d", intensity);
	}

	message(io_data, MSG_GPUINT, id, intensitystr, isjson);
}

static void gpumem(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef HAVE_ADL
	int id;
	char *value;
	int clock;

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	clock = atoi(value);

	if (set_memoryclock(id, clock))
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
	int clock;

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	clock = atoi(value);

	if (set_engineclock(id, clock))
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
	int fan;

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	fan = atoi(value);

	if (set_fanspeed(id, fan))
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
	float vddc;

	if (!splitgpuvalue(io_data, param, &id, &value, isjson))
		return;

	vddc = atof(value);

	if (set_vddc(id, vddc))
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
	if (isjson)
		io_put(io_data, JSON_START JSON_BYE);
	else
		io_put(io_data, _BYE);

	bye = true;
	do_a_quit = true;
}

void dorestart(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	if (isjson)
		io_put(io_data, JSON_START JSON_RESTART);
	else
		io_put(io_data, _RESTART);

	bye = true;
	do_a_restart = true;
}

void privileged(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	message(io_data, MSG_ACCOK, 0, NULL, isjson);
}

void notifystatus(struct io_data *io_data, int device, struct cgpu_info *cgpu, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
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
	root = api_add_string(root, "Name", cgpu->drv->name, false);
	root = api_add_int(root, "ID", &(cgpu->device_id), false);
	root = api_add_time(root, "Last Well", &(cgpu->device_last_well), false);
	root = api_add_time(root, "Last Not Well", &(cgpu->device_last_not_well), false);
	root = api_add_string(root, "Reason Not Well", reason, false);
	root = api_add_int(root, "*Thread Fail Init", &(cgpu->thread_fail_init_count), false);
	root = api_add_int(root, "*Thread Zero Hash", &(cgpu->thread_zero_hash_count), false);
	root = api_add_int(root, "*Thread Fail Queue", &(cgpu->thread_fail_queue_count), false);
	root = api_add_int(root, "*Dev Sick Idle 60s", &(cgpu->dev_sick_idle_60_count), false);
	root = api_add_int(root, "*Dev Dead Idle 600s", &(cgpu->dev_dead_idle_600_count), false);
	root = api_add_int(root, "*Dev Nostart", &(cgpu->dev_nostart_count), false);
	root = api_add_int(root, "*Dev Over Heat", &(cgpu->dev_over_heat_count), false);
	root = api_add_int(root, "*Dev Thermal Cutoff", &(cgpu->dev_thermal_cutoff_count), false);
	root = api_add_int(root, "*Dev Comms Error", &(cgpu->dev_comms_error_count), false);
	root = api_add_int(root, "*Dev Throttle", &(cgpu->dev_throttle_count), false);

	root = print_data(root, buf, isjson, isjson && (device > 0));
	io_add(io_data, buf);
}

static void notify(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, char group)
{
	struct cgpu_info *cgpu;
	bool io_open = false;
	int i;

	if (total_devices == 0) {
		message(io_data, MSG_NODEVS, 0, NULL, isjson);
		return;
	}

	message(io_data, MSG_NOTIFY, 0, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_NOTIFY);

	for (i = 0; i < total_devices; i++) {
		cgpu = get_devices(i);
		notifystatus(io_data, i, cgpu, isjson, group);
	}

	if (isjson && io_open)
		io_close(io_data);
}

static void devdetails(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open = false;
	struct cgpu_info *cgpu;
	int i;

	if (total_devices == 0) {
		message(io_data, MSG_NODEVS, 0, NULL, isjson);
		return;
	}

	message(io_data, MSG_DEVDETAILS, 0, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_DEVDETAILS);

	for (i = 0; i < total_devices; i++) {
		cgpu = get_devices(i);

		root = api_add_int(root, "DEVDETAILS", &i, false);
		root = api_add_string(root, "Name", cgpu->drv->name, false);
		root = api_add_int(root, "ID", &(cgpu->device_id), false);
		root = api_add_string(root, "Driver", cgpu->drv->dname, false);
		root = api_add_const(root, "Kernel", cgpu->kname ? : BLANK, false);
		root = api_add_const(root, "Model", cgpu->name ? : BLANK, false);
		root = api_add_const(root, "Device Path", cgpu->device_path ? : BLANK, false);

		root = print_data(root, buf, isjson, isjson && (i > 0));
		io_add(io_data, buf);
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

static int itemstats(struct io_data *io_data, int i, char *id, struct cgminer_stats *stats, struct cgminer_pool_stats *pool_stats, struct api_data *extra, struct cgpu_info *cgpu, bool isjson)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];

	root = api_add_int(root, "STATS", &i, false);
	root = api_add_string(root, "ID", id, false);
	root = api_add_elapsed(root, "Elapsed", &(total_secs), false);
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

	if (cgpu) {
#ifdef USE_USBUTILS
		char pipe_details[128];

		if (cgpu->usbinfo.pipe_count)
			snprintf(pipe_details, sizeof(pipe_details),
				 "%"PRIu64" %"PRIu64"/%"PRIu64"/%"PRIu64" %lu",
				 cgpu->usbinfo.pipe_count,
				 cgpu->usbinfo.clear_err_count,
				 cgpu->usbinfo.retry_err_count,
				 cgpu->usbinfo.clear_fail_count,
				 (unsigned long)(cgpu->usbinfo.last_pipe));
		else
			strcpy(pipe_details, "0");

		root = api_add_string(root, "USB Pipe", pipe_details, true);
#endif
	}

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

			sprintf(id, "%s%d", cgpu->drv->name, cgpu->device_id);
			i = itemstats(io_data, i, id, &(cgpu->cgminer_stats), NULL, extra, cgpu, isjson);
		}
	}

	for (j = 0; j < total_pools; j++) {
		struct pool *pool = pools[j];

		sprintf(id, "POOL%d", j);
		i = itemstats(io_data, i, id, &(pool->cgminer_stats), &(pool->cgminer_pool_stats), NULL, NULL, isjson);
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
	bool io_open;

	message(io_data, MSG_MINECOIN, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_MINECOIN : _MINECOIN COMSTR);

#ifdef USE_SCRYPT
	if (opt_scrypt)
		root = api_add_const(root, "Hash Method", SCRYPTSTR, false);
	else
#endif
		root = api_add_const(root, "Hash Method", SHA256STR, false);

	cg_rlock(&ch_lock);
	if (current_fullhash && *current_fullhash) {
		root = api_add_timeval(root, "Current Block Time", &block_timeval, true);
		root = api_add_string(root, "Current Block Hash", current_fullhash, true);
	} else {
		struct timeval t = {0,0};
		root = api_add_timeval(root, "Current Block Time", &t, true);
		root = api_add_const(root, "Current Block Hash", BLANK, false);
	}
	cg_runlock(&ch_lock);

	root = api_add_bool(root, "LP", &have_longpoll, false);
	root = api_add_diff(root, "Network Difficulty", &current_diff, true);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
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
	else {
		message(io_data, MSG_UNKCON, 0, param, isjson);
		return;
	}

	message(io_data, MSG_SETCONFIG, value, param, isjson);
}

static void usbstats(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;

#ifdef USE_USBUTILS
	char buf[TMPBUFSIZ];
	bool io_open = false;
	int count = 0;

	root = api_usb_stats(&count);
#endif

	if (!root) {
		message(io_data, MSG_NOUSTA, 0, NULL, isjson);
		return;
	}

#ifdef USE_USBUTILS
	message(io_data, MSG_USBSTA, 0, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_USBSTATS);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);

	while (42) {
		root = api_usb_stats(&count);
		if (!root)
			break;

		root = print_data(root, buf, isjson, isjson);
		io_add(io_data, buf);
	}

	if (isjson && io_open)
		io_close(io_data);
#endif
}

#ifdef HAVE_AN_FPGA
static void pgaset(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	struct device_drv *drv;
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
	drv = cgpu->drv;

	char *set = strchr(opt, ',');
	if (set)
		*(set++) = '\0';

	if (!drv->set_device)
		message(io_data, MSG_PGANOSET, id, NULL, isjson);
	else {
		char *ret = drv->set_device(cgpu, opt, set, buf);
		if (ret) {
			if (strcasecmp(opt, "help") == 0)
				message(io_data, MSG_PGAHELP, id, ret, isjson);
			else
				message(io_data, MSG_PGASETERR, id, ret, isjson);
		} else
			message(io_data, MSG_PGASETOK, id, NULL, isjson);
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

static void dohotplug(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
#ifdef USE_USBUTILS
	int value;

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISHPLG, 0, NULL, isjson);
		return;
	}

	value = atoi(param);
	if (value < 0 || value > 9999) {
		message(io_data, MSG_INVHPLG, 0, param, isjson);
		return;
	}

	hotplug_time = value;

	if (value)
		message(io_data, MSG_HOTPLUG, value, NULL, isjson);
	else
		message(io_data, MSG_DISHPLG, 0, NULL, isjson);
#else
	message(io_data, MSG_NOHPLG, 0, NULL, isjson);
	return;
#endif
}

#ifdef HAVE_AN_ASIC
static void ascdev(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	bool io_open = false;
	int numasc = numascs();
	int id;

	if (numasc == 0) {
		message(io_data, MSG_ASCNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numasc) {
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	message(io_data, MSG_ASCDEV, id, NULL, isjson);

	if (isjson)
		io_open = io_add(io_data, COMSTR JSON_ASC);

	ascstatus(io_data, id, isjson, false);

	if (isjson && io_open)
		io_close(io_data);
}

static void ascenable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	int numasc = numascs();
	struct thr_info *thr;
	int asc;
	int id;
	int i;

	if (numasc == 0) {
		message(io_data, MSG_ASCNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numasc) {
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	int dev = ascdevice(id);
	if (dev < 0) { // Should never happen
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	cgpu = get_devices(dev);

	applog(LOG_DEBUG, "API: request to ascenable ascid %d device %d %s%u",
			id, dev, cgpu->drv->name, cgpu->device_id);

	if (cgpu->deven != DEV_DISABLED) {
		message(io_data, MSG_ASCLRENA, id, NULL, isjson);
		return;
	}

#if 0 /* A DISABLED device wont change status FIXME: should disabling make it WELL? */
	if (cgpu->status != LIFE_WELL) {
		message(io_data, MSG_ASCUNW, id, NULL, isjson);
		return;
	}
#endif

#ifdef USE_USBUTILS
	if (cgpu->usbinfo.nodev) {
		message(io_data, MSG_ASCUSBNODEV, id, NULL, isjson);
		return;
	}
#endif

	for (i = 0; i < mining_threads; i++) {
		thr = get_thread(i);
		asc = thr->cgpu->cgminer_id;
		if (asc == dev) {
			cgpu->deven = DEV_ENABLED;
			applog(LOG_DEBUG, "API: Pushing sem post to thread %d", thr->id);
			cgsem_post(&thr->sem);
		}
	}

	message(io_data, MSG_ASCENA, id, NULL, isjson);
}

static void ascdisable(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	int numasc = numascs();
	int id;

	if (numasc == 0) {
		message(io_data, MSG_ASCNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numasc) {
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	int dev = ascdevice(id);
	if (dev < 0) { // Should never happen
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	cgpu = get_devices(dev);

	applog(LOG_DEBUG, "API: request to ascdisable ascid %d device %d %s%u",
			id, dev, cgpu->drv->name, cgpu->device_id);

	if (cgpu->deven == DEV_DISABLED) {
		message(io_data, MSG_ASCLRDIS, id, NULL, isjson);
		return;
	}

	cgpu->deven = DEV_DISABLED;

	message(io_data, MSG_ASCDIS, id, NULL, isjson);
}

static void ascidentify(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, __maybe_unused char group)
{
	struct cgpu_info *cgpu;
	struct device_drv *drv;
	int numasc = numascs();
	int id;

	if (numasc == 0) {
		message(io_data, MSG_ASCNON, 0, NULL, isjson);
		return;
	}

	if (param == NULL || *param == '\0') {
		message(io_data, MSG_MISID, 0, NULL, isjson);
		return;
	}

	id = atoi(param);
	if (id < 0 || id >= numasc) {
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	int dev = ascdevice(id);
	if (dev < 0) { // Should never happen
		message(io_data, MSG_INVASC, id, NULL, isjson);
		return;
	}

	cgpu = get_devices(dev);
	drv = cgpu->drv;

	if (!drv->identify_device)
		message(io_data, MSG_ASCNOID, id, NULL, isjson);
	else {
		drv->identify_device(cgpu);
		message(io_data, MSG_ASCIDENT, id, NULL, isjson);
	}
}
#endif

static void asccount(struct io_data *io_data, __maybe_unused SOCKETTYPE c, __maybe_unused char *param, bool isjson, __maybe_unused char group)
{
	struct api_data *root = NULL;
	char buf[TMPBUFSIZ];
	bool io_open;
	int count = 0;

#ifdef HAVE_AN_ASIC
	count = numascs();
#endif

	message(io_data, MSG_NUMASC, 0, NULL, isjson);
	io_open = io_add(io_data, isjson ? COMSTR JSON_ASCS : _ASCS COMSTR);

	root = api_add_int(root, "Count", &count, false);

	root = print_data(root, buf, isjson, false);
	io_add(io_data, buf);
	if (isjson && io_open)
		io_close(io_data);
}

static void checkcommand(struct io_data *io_data, __maybe_unused SOCKETTYPE c, char *param, bool isjson, char group);

struct CMDS {
	char *name;
	void (*func)(struct io_data *, SOCKETTYPE, char *, bool, char);
	bool iswritemode;
} cmds[] = {
	{ "version",		apiversion,	false },
	{ "config",		minerconfig,	false },
	{ "devs",		devstatus,	false },
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
	{ "pgaidentify",	pgaidentify,	true },
#endif
	{ "gpucount",		gpucount,	false },
	{ "pgacount",		pgacount,	false },
	{ "switchpool",		switchpool,	true },
	{ "addpool",		addpool,	true },
	{ "poolpriority",	poolpriority,	true },
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
	{ "check",		checkcommand,	false },
	{ "failover-only",	failoveronly,	true },
	{ "coin",		minecoin,	false },
	{ "debug",		debugstate,	true },
	{ "setconfig",		setconfig,	true },
	{ "usbstats",		usbstats,	false },
#ifdef HAVE_AN_FPGA
	{ "pgaset",		pgaset,		true },
#endif
	{ "zero",		dozero,		true },
	{ "hotplug",		dohotplug,	true },
#ifdef HAVE_AN_ASIC
	{ "asc",		ascdev,		false },
	{ "ascenable",		ascenable,	true },
	{ "ascdisable",		ascdisable,	true },
	{ "ascidentify",	ascidentify,	true },
#endif
	{ "asccount",		asccount,	false },
	{ NULL,			NULL,		false }
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

static void send_result(struct io_data *io_data, SOCKETTYPE c, bool isjson)
{
	char buf[SOCKBUFSIZ + sizeof(JSON_CLOSE) + sizeof(JSON_END)];
	int count, res, tosend, len, n;

	strcpy(buf, io_data->ptr);

	if (io_data->close)
		strcat(buf, JSON_CLOSE);

	if (isjson) {
		if (io_data->full)
			strcat(buf, JSON_END_TRUNCATED);
		else
			strcat(buf, JSON_END);
	}

	len = strlen(buf);
	tosend = len+1;

	applog(LOG_DEBUG, "API: send reply: (%d) '%.10s%s'", tosend, buf, len > 10 ? "..." : BLANK);

	count = 0;
	while (count++ < 5 && tosend > 0) {
		// allow 50ms per attempt
		struct timeval timeout = {0, 50000};
		fd_set wd;

		FD_ZERO(&wd);
		FD_SET(c, &wd);
		if ((res = select(c + 1, NULL, &wd, NULL, &timeout)) < 1) {
			applog(LOG_WARNING, "API: send select failed (%d)", res);
			return;
		}

		n = send(c, buf, tosend, 0);

		if (SOCKETFAIL(n)) {
			if (sock_blocks())
				continue;

			applog(LOG_WARNING, "API: send (%d) failed: %s", tosend, SOCKERRMSG);

			return;
		} else {
			if (count <= 1) {
				if (n == tosend)
					applog(LOG_DEBUG, "API: sent all of %d first go", tosend);
				else
					applog(LOG_DEBUG, "API: sent %d of %d first go", n, tosend);
			} else {
				if (n == tosend)
					applog(LOG_DEBUG, "API: sent all of remaining %d (count=%d)", tosend, count);
				else
					applog(LOG_DEBUG, "API: sent %d of remaining %d (count=%d)", n, tosend, count);
			}

			tosend -= n;
		}
	}
}

static void tidyup(__maybe_unused void *arg)
{
	mutex_lock(&quit_restart_lock);

	SOCKETTYPE *apisock = (SOCKETTYPE *)arg;

	bye = true;

	if (*apisock != INVSOCK) {
		shutdown(*apisock, SHUT_RDWR);
		CLOSESOCKET(*apisock);
		*apisock = INVSOCK;
	}

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

		if (isalpha(*ptr) && *(ptr+1) == ':') {
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
	struct io_data *io_data;
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
	char cmdbuf[100];
	char *cmd;
	char *param;
	bool addrok;
	char group;
	json_error_t json_err;
	json_t *json_config;
	json_t *json_val;
	bool isjson;
	bool did;
	int i;

	SOCKETTYPE *apisock;

	apisock = malloc(sizeof(*apisock));
	*apisock = INVSOCK;

	if (!opt_api_listen) {
		applog(LOG_DEBUG, "API not running%s", UNAVAILABLE);
		return;
	}

	io_data = sock_io_new();

	mutex_init(&quit_restart_lock);

	pthread_cleanup_push(tidyup, (void *)apisock);
	my_thr_id = api_thr_id;

	setup_groups();

	if (opt_api_allow) {
		setup_ipaccess();

		if (ips == 0) {
			applog(LOG_WARNING, "API not running (no valid IPs specified)%s", UNAVAILABLE);
			return;
		}
	}

	/* This should be done before curl in needed
	 * to ensure curl has already called WSAStartup() in windows */
	nmsleep(opt_log_interval*1000);

	*apisock = socket(AF_INET, SOCK_STREAM, 0);
	if (*apisock == INVSOCK) {
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
	bindstart = time(NULL);
	while (bound == 0) {
		if (SOCKETFAIL(bind(*apisock, (struct sockaddr *)(&serv), sizeof(serv)))) {
			binderror = SOCKERRMSG;
			if ((time(NULL) - bindstart) > 61)
				break;
			else {
				applog(LOG_WARNING, "API bind to port %d failed - trying again in 30sec", port);
				nmsleep(30000);
			}
		} else
			bound = 1;
	}

	if (bound == 0) {
		applog(LOG_ERR, "API bind to port %d failed (%s)%s", port, binderror, UNAVAILABLE);
		return;
	}

	if (SOCKETFAIL(listen(*apisock, QUEUE))) {
		applog(LOG_ERR, "API3 initialisation failed (%s)%s", SOCKERRMSG, UNAVAILABLE);
		CLOSESOCKET(*apisock);
		return;
	}

	if (opt_api_allow)
		applog(LOG_WARNING, "API running in IP access mode on port %d (%d)", port, (int)*apisock);
	else {
		if (opt_api_network)
			applog(LOG_WARNING, "API running in UNRESTRICTED read access mode on port %d (%d)", port, (int)*apisock);
		else
			applog(LOG_WARNING, "API running in local read access mode on port %d (%d)", port, (int)*apisock);
	}

	while (!bye) {
		clisiz = sizeof(cli);
		if (SOCKETFAIL(c = accept(*apisock, (struct sockaddr *)(&cli), &clisiz))) {
			applog(LOG_ERR, "API failed (%s)%s (%d)", SOCKERRMSG, UNAVAILABLE, (int)*apisock);
			goto die;
		}

		connectaddr = inet_ntoa(cli.sin_addr);

		addrok = false;
		group = NOPRIVGROUP;
		if (opt_api_allow) {
			int client_ip = htonl(cli.sin_addr.s_addr);
			for (i = 0; i < ips; i++) {
				if ((client_ip & ipaccess[i].mask) == ipaccess[i].ip) {
					addrok = true;
					group = ipaccess[i].group;
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
				io_reinit(io_data);

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

				if (!did)
					for (i = 0; cmds[i].name != NULL; i++) {
						if (strcmp(cmd, cmds[i].name) == 0) {
							sprintf(cmdbuf, "|%s|", cmd);
							if (ISPRIVGROUP(group) || strstr(COMMANDS(group), cmdbuf))
								(cmds[i].func)(io_data, c, param, isjson, group);
							else {
								message(io_data, MSG_ACCDENY, 0, cmds[i].name, isjson);
								applog(LOG_DEBUG, "API: access denied to '%s' for '%s' command", connectaddr, cmds[i].name);
							}

							send_result(io_data, c, isjson);
							did = true;
							break;
						}
					}

				if (!did) {
					message(io_data, MSG_INVCMD, 0, NULL, isjson);
					send_result(io_data, c, isjson);
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
