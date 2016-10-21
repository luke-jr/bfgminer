#ifndef BFG_OCL_H
#define BFG_OCL_H

#include <stdbool.h>
#include <stdio.h>

#include "CL/cl.h"

#include "miner.h"

#define SCRYPT_CLBUFFER0_SZ      (128)
#define FULLHEADER_CLBUFFER0_SZ  ( 80)
#ifdef USE_SCRYPT
#	define MAX_CLBUFFER0_SZ  SCRYPT_CLBUFFER0_SZ
#elif USE_OPENCL_FULLHEADER
#	define MAX_CLBUFFER0_SZ  FULLHEADER_CLBUFFER0_SZ
#endif

struct mining_algorithm;
struct opencl_kernel_info;
typedef struct _clState _clState;

struct _clState {
	cl_device_id devid;
	char *platform_ver_str;
	bool is_mesa;
	
	cl_context context;
	cl_command_queue commandQueue;
	
	cl_mem outputBuffer;
#ifdef MAX_CLBUFFER0_SZ
	cl_mem CLbuffer0;
#endif
#ifdef USE_SCRYPT
	cl_mem padbuffer8;
	size_t padbufsize;
	void * cldata;
#endif
	bool hasBitAlign;
	cl_uint preferred_vwidth;
	cl_uint vwidth;
	size_t max_work_size;
	cl_uint max_compute_units;
};

extern FILE *opencl_open_kernel(const char *filename);
extern char *file_contents(const char *filename, int *length);
extern char *opencl_kernel_source(const char *filename, int *out_sourcelen, enum cl_kernels *out_kinterface, struct mining_algorithm **);
extern int clDevicesNum(void);
extern _clState *opencl_create_clState(unsigned int gpu, char *name, size_t nameSize);
extern bool opencl_load_kernel(struct cgpu_info *, _clState *clState, const char *name, struct opencl_kernel_info *, const char *kernel_file, const struct mining_algorithm *);

#endif /* __OCL_H__ */
