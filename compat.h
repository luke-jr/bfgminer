/*
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#ifndef BFG_COMPAT_H
#define BFG_COMPAT_H

#include "config.h"

#include <stdbool.h>

#if !(defined(WIN32) || defined(unix))
#define unix
#endif

#if defined(LL_FOREACH) && !defined(LL_FOREACH2)

// Missing from uthash before 1.9.7

#define LL_DELETE2(head,del,next)                                                              \
do {                                                                                           \
  LDECLTYPE(head) _tmp;                                                                        \
  if ((head) == (del)) {                                                                       \
    (head)=(head)->next;                                                                       \
  } else {                                                                                     \
    _tmp = head;                                                                               \
    while (_tmp->next && (_tmp->next != (del))) {                                              \
      _tmp = _tmp->next;                                                                       \
    }                                                                                          \
    if (_tmp->next) {                                                                          \
      _tmp->next = ((del)->next);                                                              \
    }                                                                                          \
  }                                                                                            \
} while (0)

#define LL_FOREACH2(head,el,next)                                                              \
    for(el=head;el;el=(el)->next)

#define LL_FOREACH_SAFE2(head,el,tmp,next)                                                     \
  for((el)=(head);(el) && (tmp = (el)->next, 1); (el) = tmp)

#define LL_PREPEND2(head,add,next)                                                             \
do {                                                                                           \
  (add)->next = head;                                                                          \
  head = add;                                                                                  \
} while (0)

#endif

#ifdef WIN32
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

#include <windows.h>

#ifndef __maybe_unused
#define __maybe_unused		__attribute__((unused))
#endif

  #ifndef timersub
    #define timersub(a, b, result)                     \
    do {                                               \
      (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
      (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
      if ((result)->tv_usec < 0) {                     \
        --(result)->tv_sec;                            \
        (result)->tv_usec += 1000000;                  \
      }                                                \
    } while (0)
  #endif
 #ifndef timeradd
 # define timeradd(a, b, result)			      \
   do {							      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;	      \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;	      \
    if ((result)->tv_usec >= 1000000)			      \
      {							      \
	++(result)->tv_sec;				      \
	(result)->tv_usec -= 1000000;			      \
      }							      \
   } while (0)
 #endif

// Some versions of MingW define this, but don't handle the timeval.tv_sec case that we use
#ifdef localtime_r
#undef localtime_r
#endif
// localtime is thread-safe on Windows
// We also use this with timeval.tv_sec, which is incorrectly smaller than time_t on Windows
// Need to cast to time_t* to suppress warning - actual problem shouldn't be possible in practice
#define localtime_r(timep, result)  (  \
	memcpy(result,  \
		(  \
			(sizeof(*timep) == sizeof(time_t))  \
			? localtime((time_t*)timep)  \
			: localtime_convert(*timep)  \
		),  \
		sizeof(*result)  \
	)  \
)

static inline
struct tm *localtime_convert(time_t t)
{
	return localtime(&t);
}
#endif

#ifndef HAVE_NANOSLEEP
extern void (*timer_set_now)(struct timeval *);
#define cgtime(tvp)  timer_set_now(tvp)

static inline int nanosleep(const struct timespec *req, struct timespec *rem)
{
	struct timeval tstart;
	DWORD msecs;

	cgtime(&tstart);
	msecs = (req->tv_sec * 1000) + ((999999 + req->tv_nsec) / 1000000);

	if (SleepEx(msecs, true) == WAIT_IO_COMPLETION) {
		if (rem) {
			struct timeval tdone, tnow, tleft;
			tdone.tv_sec = tstart.tv_sec + req->tv_sec;
			tdone.tv_usec = tstart.tv_usec + ((999 + req->tv_nsec) / 1000);
			if (tdone.tv_usec > 1000000) {
				tdone.tv_usec -= 1000000;
				++tdone.tv_sec;
			}

			cgtime(&tnow);
			if (timercmp(&tnow, &tdone, >))
				return 0;
			timersub(&tdone, &tnow, &tleft);

			rem->tv_sec = tleft.tv_sec;
			rem->tv_nsec = tleft.tv_usec * 1000;
		}
		errno = EINTR;
		return -1;
	}
	return 0;
}

#undef cgtime
#endif

#ifndef HAVE_SLEEP
static inline int sleep(unsigned int secs)
{
	struct timespec req, rem;
	req.tv_sec = secs;
	req.tv_nsec = 0;
	if (!nanosleep(&req, &rem))
		return 0;
	return rem.tv_sec + (rem.tv_nsec ? 1 : 0);
}
#endif

#ifdef WIN32
enum {
	PRIO_PROCESS		= 0,
};

static inline int setpriority(__maybe_unused int which, __maybe_unused int who, __maybe_unused int prio)
{
	return -!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

typedef unsigned long int ulong;
typedef unsigned short int ushort;
typedef unsigned int uint;

#ifndef __SUSECONDS_T_TYPE
typedef long suseconds_t;
#endif

#endif /* WIN32 */

#ifndef HAVE_LOG2
#	define log2(n)  (log(n) / log(2))
#endif

#ifndef HAVE_PTHREAD_CANCEL

// Bionic (Android) is intentionally missing pthread_cancel, so it is implemented using pthread_kill (handled in util.c)
#include <pthread.h>
#include <signal.h>
#define pthread_cancel(pth)  pthread_kill(pth, SIGTERM)
extern void pthread_testcancel(void);
#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE  0
#define PTHREAD_CANCEL_DISABLE 1
#endif
#ifndef PTHREAD_CANCEL_DEFERRED
#define PTHREAD_CANCEL_DEFERRED     0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif
#ifndef PTHREAD_CANCELED
#define PTHREAD_CANCELED ((void*)-1)
#endif

#endif

#endif /* __COMPAT_H__ */
