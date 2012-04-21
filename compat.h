#ifndef __COMPAT_H__
#define __COMPAT_H__

#ifdef WIN32
#include <time.h>
#include <pthread.h>

#include <windows.h>

static inline void nanosleep(struct timespec *rgtp, void *__unused)
{
	Sleep(rgtp->tv_nsec / 1000000);
}
static inline void sleep(unsigned int secs)
{
	Sleep(secs * 1000);
}

enum {
	PRIO_PROCESS		= 0,
};

static inline int setpriority(int which, int who, int prio)
{
	/* FIXME - actually do something */
	return 0;
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

#endif /* __COMPAT_H__ */
