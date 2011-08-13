#ifndef __OCL_H__
#define __OCL_H__
#include "config.h"
#ifdef HAVE_OPENCL
#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#include "miner.h"

typedef struct {
	cl_context context;
	cl_kernel kernel;
	cl_command_queue commandQueue;
	cl_program program;
	cl_mem outputBuffer;
	cl_uint preferred_vwidth;
} _clState;

extern char *file_contents(const char *filename, int *length);
extern int clDevicesNum();
extern int preinit_devices(void);
extern _clState *initCQ(_clState *clState, unsigned int gpu);
extern _clState *initCl(struct cgpu_info *cgpu, char *name, size_t nameSize);
#endif /* HAVE_OPENCL */
#endif /* __OCL_H__ */
