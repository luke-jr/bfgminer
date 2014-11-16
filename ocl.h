#ifndef BFG_OCL_H
#define BFG_OCL_H

#include "config.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef USE_OPENCL
#include "CL/cl.h"

#include "miner.h"

struct opencl_kernel_info;
typedef struct _clState _clState;

typedef cl_int (*queue_kernel_parameters_func_t)(const struct opencl_kernel_info *, _clState *, struct work *, cl_uint);

struct opencl_kernel_info {
	bool loaded;
	cl_program program;
	cl_kernel kernel;
	bool goffset;
	enum cl_kernels interface;
	size_t wsize;
	queue_kernel_parameters_func_t queue_kernel_parameters;
};

struct _clState {
	cl_device_id devid;
	char *platform_ver_str;
	bool is_mesa;
	
	cl_context context;
	cl_command_queue commandQueue;
	
	struct opencl_kernel_info kernel_sha256d;
	struct opencl_kernel_info kernel_scrypt;
	
	cl_mem outputBuffer;
#ifdef USE_SCRYPT
	cl_mem CLbuffer0;
	cl_mem padbuffer8;
	size_t padbufsize;
	void * cldata;
#endif
	bool hasBitAlign;
	bool hasOpenCL11plus;
	cl_uint preferred_vwidth;
	cl_uint vwidth;
	size_t max_work_size;
	cl_uint max_compute_units;
};

extern FILE *opencl_open_kernel(const char *filename);
extern char *file_contents(const char *filename, int *length);
extern char *opencl_kernel_source(const char *filename, int *out_sourcelen, enum cl_kernels *out_kinterface);
extern int clDevicesNum(void);
extern _clState *opencl_create_clState(unsigned int gpu, char *name, size_t nameSize);
extern bool opencl_load_kernel(struct cgpu_info *, _clState *clState, const char *name, struct opencl_kernel_info *, const char *kernel_file, const struct mining_algorithm *);
#endif /* USE_OPENCL */
#endif /* __OCL_H__ */
