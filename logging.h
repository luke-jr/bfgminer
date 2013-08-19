/*
 * Copyright 2012 zefir
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include "config.h"

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

/* debug flags */
extern bool opt_debug;
extern bool opt_debug_console;
extern bool opt_log_output;
extern bool opt_log_microseconds;
extern bool opt_realquiet;
extern bool want_per_device_stats;

/* global log_level, messages with lower or equal prio are logged */
extern int opt_log_level;

#define LOGBUFSIZ 256

extern void _applog(int prio, const char *str);

#define IN_FMT_FFL " in %s %s():%d"

#define applog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, tmp42); \
	} \
} while (0)

#define applogr(rv, prio, ...)  do {  \
	applog(prio, __VA_ARGS__);  \
	return rv;  \
} while (0)

extern void _bfg_clean_up(void);

#define quit(status, fmt, ...) do { \
	_bfg_clean_up();  \
	if (fmt) { \
		fprintf(stderr, fmt, ##__VA_ARGS__);  \
	} \
	fprintf(stderr, "\n");  \
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

extern void hexdump(const void *, unsigned int len);

#endif /* __LOGGING_H__ */
