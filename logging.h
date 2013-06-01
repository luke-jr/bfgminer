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

/* original / legacy debug flags */
extern bool opt_debug;
extern bool opt_debug_console;
extern bool opt_log_output;
extern bool opt_realquiet;
extern bool want_per_device_stats;

/* global log_level, messages with lower or equal prio are logged */
extern int opt_log_level;

/* low-level logging functions with priority parameter */
extern void vapplog(int prio, const char *fmt, va_list ap) FORMAT_SYNTAX_CHECK(printf, 2, 0);
extern void applog(int prio, const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 2, 3);

/* high-level logging functions with implicit priority */
extern void log_error(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);
extern void log_warning(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);
extern void log_notice(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);
extern void log_info(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);
extern void log_debug(const char *fmt, ...) FORMAT_SYNTAX_CHECK(printf, 1, 2);

#endif /* __LOGGING_H__ */
