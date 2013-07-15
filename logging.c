/*
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2013 Andrew Smith
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
		mutex_lock(&console_lock);
		printf("%s%s%s", datetime, str, "                    \n");
		mutex_unlock(&console_lock);
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
		char datetime[64];
		struct timeval tv = {0, 0};
		struct tm *tm;

		cgtime(&tv);

		const time_t tmp_time = tv.tv_sec;
		tm = localtime(&tmp_time);

		snprintf(datetime, sizeof(datetime), " [%d-%02d-%02d %02d:%02d:%02d] ",
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec);

		/* Only output to stderr if it's not going to the screen as well */
		if (!isatty(fileno((FILE *)stderr))) {
			fprintf(stderr, "%s%s\n", datetime, str);	/* atomic write to stderr */
			fflush(stderr);
		}

		my_log_curses(prio, datetime, str);
	}
}
