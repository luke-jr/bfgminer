/*
 * Copyright 2013-2014 Luke Dashjr
 * Copyright 2012 zefir
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFG_LOGGING_H
#define BFG_LOGGING_H

#include "config.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#else
enum {
	LOG_ERR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG,
};
#endif

#include "util.h"

/* debug flags */
extern bool opt_debug;
extern bool opt_debug_console;
extern bool opt_log_output;
extern bool opt_log_microseconds;
extern bool opt_realquiet;
extern bool want_per_device_stats;

/* global log_level, messages with lower or equal prio are logged */
extern int opt_log_level;

#define return_via(label, stmt)  do {  \
	stmt;  \
	goto label;  \
} while (0)

#define LOGBUFSIZ 0x1000

extern void _applog(int prio, const char *str);

#define IN_FMT_FFL " in %s %s():%d"

#define applog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, tmp42); \
	} \
} while (0)

#define applogsiz(prio, _SIZ, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
			char tmp42[_SIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, tmp42); \
	} \
} while (0)

#define applogr(rv, prio, ...)  do {  \
	applog(prio, __VA_ARGS__);  \
	return rv;  \
} while (0)

#define return_via_applog(label, expr, prio, ...)  do {  \
	applog(prio, __VA_ARGS__);  \
	expr;  \
	goto label;  \
} while (0)

#define appperror(prio, s)  do {  \
	const char *_tmp43 = bfg_strerror(errno, BST_ERRNO);  \
	if (s && s[0])  \
		applog(prio, "%s: %s", s, _tmp43);  \
	else  \
		_applog(prio, _tmp43);  \
} while (0)

#define perror(s)  appperror(LOG_ERR, s)

#define applogfailinfo(prio, failed, fmt, ...)  do {  \
	applog(prio, "Failed to %s"IN_FMT_FFL": "fmt,  \
	       failed,  \
	       __FILE__, __func__, __LINE__,  \
	       __VA_ARGS__);  \
} while (0)

#define applogfailinfor(rv, prio, failed, fmt, ...)  do {  \
	applogfailinfo(prio, failed, fmt, __VA_ARGS__);  \
	return rv;  \
} while (0)

#define return_via_applogfailinfo(label, expr, prio, failed, fmt, ...)  do {  \
	applogfailinfo(prio, failed, fmt, __VA_ARGS__);  \
	expr;  \
	goto label;  \
} while (0)

#define applogfail(prio, failed)  do {  \
	applog(prio, "Failed to %s"IN_FMT_FFL,  \
	       failed,  \
	       __FILE__, __func__, __LINE__);  \
} while (0)

#define applogfailr(rv, prio, failed)  do {  \
	applogfail(prio, failed);  \
	return rv;  \
} while (0)

#define return_via_applogfail(label, expr, prio, failed)  do {  \
	applogfail(prio, failed);  \
	expr;  \
	goto label;  \
} while (0)

extern void _bfg_clean_up(bool);

#define quit(status, fmt, ...) do { \
	_bfg_clean_up(false);  \
	if (fmt) { \
		fprintf(stderr, "\n" fmt, ##__VA_ARGS__);  \
	} \
	fflush(stderr);  \
	_quit(status); \
} while (0)

#define quithere(status, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt IN_FMT_FFL, \
				##__VA_ARGS__, __FILE__, __func__, __LINE__); \
		_applog(LOG_ERR, tmp42); \
	} \
	_quit(status); \
} while (0)

#define quitfrom(status, _file, _func, _line, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt IN_FMT_FFL, \
				##__VA_ARGS__, _file, _func, _line); \
		_applog(LOG_ERR, tmp42); \
	} \
	_quit(status); \
} while (0)

#ifdef HAVE_CURSES

#define wlog(fmt, ...) do { \
	char tmp42[LOGBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	_wlog(tmp42); \
} while (0)

#define wlogprint(fmt, ...) do { \
	char tmp42[LOGBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	_wlogprint(tmp42); \
} while (0)

#endif

#endif /* __LOGGING_H__ */
