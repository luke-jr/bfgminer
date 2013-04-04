/*
 * Copyright 2011-2012 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <unistd.h>

#include "logging.h"
#include "miner.h"

bool opt_debug = false;
bool opt_log_output = false;

/* per default priorities higher than LOG_NOTICE are logged */
int opt_log_level = LOG_NOTICE;

static void my_log_curses(__maybe_unused int prio, char *f, va_list ap)
{
	if (opt_quiet && prio != LOG_ERR)
		return;

#ifdef HAVE_CURSES
	extern bool use_curses;
	if (use_curses && log_curses_only(prio, f, ap))
		;
	else
#endif
	{
		int len = strlen(f);

		strcpy(f + len - 1, "                    \n");

		mutex_lock(&console_lock);
		vprintf(f, ap);
		mutex_unlock(&console_lock);
	}
}

static void log_generic(int prio, const char *fmt, va_list ap);

void vapplog(int prio, const char *fmt, va_list ap)
{
	if (!opt_debug && prio == LOG_DEBUG)
		return;
	if (use_syslog || opt_log_output || prio <= LOG_NOTICE)
		log_generic(prio, fmt, ap);
}

void applog(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vapplog(prio, fmt, ap);
	va_end(ap);
}


/* high-level logging functions, based on global opt_log_level */

/*
 * generic log function used by priority specific ones
 * equals vapplog() without additional priority checks
 */
static void log_generic(int prio, const char *fmt, va_list ap)
{
#ifdef HAVE_SYSLOG_H
	if (use_syslog) {
		vsyslog(prio, fmt, ap);
	}
#else
	if (0) {}
#endif
	else {
		char *f;
		int len;
		struct timeval tv = {0, 0};
		struct tm *tm;

		gettimeofday(&tv, NULL);

		const time_t tmp_time = tv.tv_sec;
		tm = localtime(&tmp_time);

		len = 40 + strlen(fmt) + 22;
		f = alloca(len);
		sprintf(f, " [%d-%02d-%02d %02d:%02d:%02d] %s\n",
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec,
			fmt);
		/* Only output to stderr if it's not going to the screen as well */
		if (!isatty(fileno((FILE *)stderr))) {
			va_list apc;

			va_copy(apc, ap);
			vfprintf(stderr, f, apc);	/* atomic write to stderr */
			fflush(stderr);
		}

		my_log_curses(prio, f, ap);
	}
}
/* we can not generalize variable argument list */
#define LOG_TEMPLATE(PRIO)		\
	if (PRIO <= opt_log_level) {	\
		va_list ap;		\
		va_start(ap, fmt);	\
		vapplog(PRIO, fmt, ap);	\
		va_end(ap);		\
	}

void log_error(const char *fmt, ...)
{
	LOG_TEMPLATE(LOG_ERR);
}

void log_warning(const char *fmt, ...)
{
	LOG_TEMPLATE(LOG_WARNING);
}

void log_notice(const char *fmt, ...)
{
	LOG_TEMPLATE(LOG_NOTICE);
}

void log_info(const char *fmt, ...)
{
	LOG_TEMPLATE(LOG_INFO);
}

void log_debug(const char *fmt, ...)
{
	LOG_TEMPLATE(LOG_DEBUG);
}
