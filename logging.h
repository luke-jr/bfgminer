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

/* debug flags */
extern bool opt_debug;
extern bool opt_log_output;
extern bool opt_realquiet;
extern bool want_per_device_stats;

/* global log_level, messages with lower or equal prio are logged */
extern int opt_log_level;

#define LOGBUFSIZ 256

extern void _applog(int prio, const char *str);

#define IN_FMT_FFL " in %s %s():%d"

#define applog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, tmp42); \
		} \
	} \
} while (0)

#define applogsiz(prio, _SIZ, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[_SIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, tmp42); \
		} \
	} \
} while (0)

#define quit(status, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
		_applog(LOG_ERR, tmp42); \
	} \
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
