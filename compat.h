#ifndef __COMPAT_H__
#define __COMPAT_H__

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <stdbool.h>

// NOTE: Nested preprocessor checks since the latter isn't defined at all without the former
#ifdef HAVE_LIBUSB
#	if ! HAVE_DECL_LIBUSB_ERROR_NAME
		static char my_libusb_error_name_buf[0x10];
#		define libusb_error_name(x) (sprintf(my_libusb_error_name_buf, "%d", x), my_libusb_error_name_buf)
#	endif
#endif

#ifdef WIN32
#include <errno.h>
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

static inline int nanosleep(const struct timespec *req, struct timespec *rem)
{
	struct timeval tstart;
	DWORD msecs;

	gettimeofday(&tstart, NULL);
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

			gettimeofday(&tnow, NULL);
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

static inline int sleep(unsigned int secs)
{
	struct timespec req, rem;
	req.tv_sec = secs;
	req.tv_nsec = 0;
	if (!nanosleep(&req, &rem))
		return 0;
	return rem.tv_sec + (rem.tv_nsec ? 1 : 0);
}

enum {
	PRIO_PROCESS		= 0,
};

static inline int setpriority(__maybe_unused int which, __maybe_unused int who, __maybe_unused int prio)
{
	return -!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
}

typedef unsigned long int ulong;
typedef unsigned short int ushort;
typedef unsigned int uint;

#ifndef __SUSECONDS_T_TYPE
typedef long suseconds_t;
#endif

#define PTH(thr) ((thr)->pth.p)
#else
#define PTH(thr) ((thr)->pth)
#endif /* WIN32 */

#ifndef HAVE_PTHREAD_CANCEL

// Bionic (Android) is intentionally missing pthread_cancel, so it is implemented using pthread_kill (handled in util.c)
#include <pthread.h>
#include <signal.h>
#define pthread_cancel(pth)  pthread_kill(pth, SIGTERM)
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
