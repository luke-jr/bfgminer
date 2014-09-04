/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2012-2014 Luke Dashjr
 * Copyright 2013 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdbool.h>
#include <unistd.h>

#include "compat.h"
#include "logging.h"
#include "miner.h"

bool opt_debug = false;
bool opt_debug_console = false;  // Only used if opt_debug is also enabled
bool opt_log_output = false;
bool opt_log_microseconds;

/* per default priorities higher than LOG_NOTICE are logged */
int opt_log_level = LOG_NOTICE;

static void _my_log_curses(int prio, const char *datetime, const char *str)
{
#ifdef HAVE_CURSES
	extern bool use_curses;
	if (use_curses && _log_curses_only(prio, datetime, str))
		;
	else
#endif
	{
		last_logstatusline_len = -1;
		printf("\n %s %s\r", datetime, str);
		fflush(stdout);
	}
}

/* high-level logging function, based on global opt_log_level */

/*
 * log function
 */
void _applog(int prio, const char *str)
{
#ifdef HAVE_SYSLOG_H
	if (use_syslog) {
		syslog(prio, "%s", str);
	}
#else
	if (0) {}
#endif
	else {
		bool writetocon =
			(opt_debug_console || (opt_log_output && prio != LOG_DEBUG) || prio <= LOG_NOTICE)
		 && !(opt_quiet && prio != LOG_ERR);
		bool writetofile = !isatty(fileno((FILE *)stderr));
		if (!(writetocon || writetofile))
			return;

		char datetime[64];

		if (opt_log_microseconds)
		{
			struct timeval tv;
			struct tm tm;
			
			bfg_gettimeofday(&tv);
			localtime_r(&tv.tv_sec, &tm);
			
			snprintf(datetime, sizeof(datetime), "[%d-%02d-%02d %02d:%02d:%02d.%06ld]",
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				(long)tv.tv_usec);
		}
		else
			get_now_datestamp(datetime, sizeof(datetime));

		if (writetofile || writetocon)
		{
			bfg_console_lock();
			
			/* Only output to stderr if it's not going to the screen as well */
			if (writetofile) {
				fprintf(stderr, " %s %s\n", datetime, str);	/* atomic write to stderr */
				fflush(stderr);
			}

			if (writetocon)
				_my_log_curses(prio, datetime, str);
			
			bfg_console_unlock();
		}
	}
}
