/*
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2012-2013 Luke Dashjr
 * Copyright 2013 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

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

static void my_log_curses(int prio, const char *datetime, const char *str)
{
	if (opt_quiet && prio != LOG_ERR)
		return;

#ifdef HAVE_CURSES
	extern bool use_curses;
	if (use_curses && log_curses_only(prio, datetime, str))
		;
	else
#endif
	{
		int cancelstate;
		bool scs;
		scs = !pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancelstate);
		mutex_lock(&console_lock);
		printf(" %s %s%s", datetime, str, "                    \n");
		mutex_unlock(&console_lock);
		if (scs)
			pthread_setcancelstate(cancelstate, &cancelstate);
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
		bool writetocon = opt_debug_console || (opt_log_output && prio != LOG_DEBUG) || prio <= LOG_NOTICE;
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
			
			sprintf(datetime, "[%d-%02d-%02d %02d:%02d:%02d.%06ld]",
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				(long)tv.tv_usec);
		}
		else
			get_now_datestamp(datetime);

		/* Only output to stderr if it's not going to the screen as well */
		if (writetofile) {
			fprintf(stderr, " %s %s\n", datetime, str);	/* atomic write to stderr */
			fflush(stderr);
		}

		if (writetocon)
			my_log_curses(prio, datetime, str);
	}
}
