#ifndef __OCL_H__
#define __OCL_H__
#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

typedef struct {
	cl_context context;
	cl_kernel kernel;
	cl_command_queue commandQueue;
	cl_program program;
	cl_mem outputBuffer;
	int hasBitAlign;
	cl_uint preferred_vwidth;
	size_t max_work_size;
	size_t work_size;
} _clState;

extern char *file_contents(const char *filename, int *length);
extern int clDevicesNum();
extern _clState *initCl(unsigned int gpu, char *name, size_t nameSize);

#endif /* __OCL_H__ */
