/*
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2011-2012 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef HAVE_CURSES
#include <curses.h>
#endif

#ifndef WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <sys/types.h>

#ifndef WIN32
#include <sys/resource.h>
#endif
#include <ccan/opt/opt.h>

#define OMIT_OPENCL_API

#include "compat.h"
#include "miner.h"
#include "driver-opencl.h"
#include "findnonce.h"
#include "ocl.h"
#include "adl.h"

/* TODO: cleanup externals ********************/


#ifdef HAVE_OPENCL
/* Platform API */
CL_API_ENTRY cl_int CL_API_CALL
(*clGetPlatformIDs)(cl_uint          /* num_entries */,
                 cl_platform_id * /* platforms */,
                 cl_uint *        /* num_platforms */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clGetPlatformInfo)(cl_platform_id   /* platform */,
                  cl_platform_info /* param_name */,
                  size_t           /* param_value_size */,
                  void *           /* param_value */,
                  size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Device APIs */
CL_API_ENTRY cl_int CL_API_CALL
(*clGetDeviceIDs)(cl_platform_id   /* platform */,
               cl_device_type   /* device_type */,
               cl_uint          /* num_entries */,
               cl_device_id *   /* devices */,
               cl_uint *        /* num_devices */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clGetDeviceInfo)(cl_device_id    /* device */,
                cl_device_info  /* param_name */,
                size_t          /* param_value_size */,
                void *          /* param_value */,
                size_t *        /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Context APIs  */
CL_API_ENTRY cl_context CL_API_CALL
(*clCreateContextFromType)(const cl_context_properties * /* properties */,
                        cl_device_type          /* device_type */,
                        void (CL_CALLBACK *     /* pfn_notify*/ )(const char *, const void *, size_t, void *),
                        void *                  /* user_data */,
                        cl_int *                /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseContext)(cl_context /* context */) CL_API_SUFFIX__VERSION_1_0;

/* Command Queue APIs */
CL_API_ENTRY cl_command_queue CL_API_CALL
(*clCreateCommandQueue)(cl_context                     /* context */,
                     cl_device_id                   /* device */,
                     cl_command_queue_properties    /* properties */,
                     cl_int *                       /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseCommandQueue)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

/* Memory Object APIs */
CL_API_ENTRY cl_mem CL_API_CALL
(*clCreateBuffer)(cl_context   /* context */,
               cl_mem_flags /* flags */,
               size_t       /* size */,
               void *       /* host_ptr */,
               cl_int *     /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Program Object APIs  */
CL_API_ENTRY cl_program CL_API_CALL
(*clCreateProgramWithSource)(cl_context        /* context */,
                          cl_uint           /* count */,
                          const char **     /* strings */,
                          const size_t *    /* lengths */,
                          cl_int *          /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_program CL_API_CALL
(*clCreateProgramWithBinary)(cl_context                     /* context */,
                          cl_uint                        /* num_devices */,
                          const cl_device_id *           /* device_list */,
                          const size_t *                 /* lengths */,
                          const unsigned char **         /* binaries */,
                          cl_int *                       /* binary_status */,
                          cl_int *                       /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseProgram)(cl_program /* program */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clBuildProgram)(cl_program           /* program */,
               cl_uint              /* num_devices */,
               const cl_device_id * /* device_list */,
               const char *         /* options */,
               void (CL_CALLBACK *  /* pfn_notify */)(cl_program /* program */, void * /* user_data */),
               void *               /* user_data */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clGetProgramInfo)(cl_program         /* program */,
                 cl_program_info    /* param_name */,
                 size_t             /* param_value_size */,
                 void *             /* param_value */,
                 size_t *           /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clGetProgramBuildInfo)(cl_program            /* program */,
                      cl_device_id          /* device */,
                      cl_program_build_info /* param_name */,
                      size_t                /* param_value_size */,
                      void *                /* param_value */,
                      size_t *              /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Kernel Object APIs */
CL_API_ENTRY cl_kernel CL_API_CALL
(*clCreateKernel)(cl_program      /* program */,
               const char *    /* kernel_name */,
               cl_int *        /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseKernel)(cl_kernel   /* kernel */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clSetKernelArg)(cl_kernel    /* kernel */,
               cl_uint      /* arg_index */,
               size_t       /* arg_size */,
               const void * /* arg_value */) CL_API_SUFFIX__VERSION_1_0;

/* Flush and Finish APIs */
CL_API_ENTRY cl_int CL_API_CALL
(*clFinish)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

/* Enqueued Commands APIs */
CL_API_ENTRY cl_int CL_API_CALL
(*clEnqueueReadBuffer)(cl_command_queue    /* command_queue */,
                    cl_mem              /* buffer */,
                    cl_bool             /* blocking_read */,
                    size_t              /* offset */,
                    size_t              /* size */,
                    void *              /* ptr */,
                    cl_uint             /* num_events_in_wait_list */,
                    const cl_event *    /* event_wait_list */,
                    cl_event *          /* event */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clEnqueueWriteBuffer)(cl_command_queue   /* command_queue */,
                     cl_mem             /* buffer */,
                     cl_bool            /* blocking_write */,
                     size_t             /* offset */,
                     size_t             /* size */,
                     const void *       /* ptr */,
                     cl_uint            /* num_events_in_wait_list */,
                     const cl_event *   /* event_wait_list */,
                     cl_event *         /* event */) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY cl_int CL_API_CALL
(*clEnqueueNDRangeKernel)(cl_command_queue /* command_queue */,
                       cl_kernel        /* kernel */,
                       cl_uint          /* work_dim */,
                       const size_t *   /* global_work_offset */,
                       const size_t *   /* global_work_size */,
                       const size_t *   /* local_work_size */,
                       cl_uint          /* num_events_in_wait_list */,
                       const cl_event * /* event_wait_list */,
                       cl_event *       /* event */) CL_API_SUFFIX__VERSION_1_0;

#ifdef WIN32
#define dlsym (void*)GetProcAddress
#define dlclose FreeLibrary
#endif

#define LOAD_OCL_SYM(sym)  do { \
	if (!(sym = dlsym(cl, #sym))) {  \
		applog(LOG_ERR, "Failed to load OpenCL symbol " #sym ", no GPUs usable");  \
		dlclose(cl);  \
		return false;  \
	}  \
} while(0)

static bool
load_opencl_symbols() {
#ifndef WIN32
	void *cl = dlopen("libOpenCL.so", RTLD_LAZY);
#else
	HMODULE cl = LoadLibrary("OpenCL.dll");
#endif
	if (!cl)
	{
		applog(LOG_ERR, "Failed to load OpenCL library, no GPUs usable");
		return false;
	}
	
	LOAD_OCL_SYM(clGetPlatformIDs);
	LOAD_OCL_SYM(clGetPlatformInfo);
	LOAD_OCL_SYM(clGetDeviceIDs);
	LOAD_OCL_SYM(clGetDeviceInfo);
	LOAD_OCL_SYM(clCreateContextFromType);
	LOAD_OCL_SYM(clReleaseContext);
	LOAD_OCL_SYM(clCreateCommandQueue);
	LOAD_OCL_SYM(clReleaseCommandQueue);
	LOAD_OCL_SYM(clCreateBuffer);
	LOAD_OCL_SYM(clCreateProgramWithSource);
	LOAD_OCL_SYM(clCreateProgramWithBinary);
	LOAD_OCL_SYM(clReleaseProgram);
	LOAD_OCL_SYM(clBuildProgram);
	LOAD_OCL_SYM(clGetProgramInfo);
	LOAD_OCL_SYM(clGetProgramBuildInfo);
	LOAD_OCL_SYM(clCreateKernel);
	LOAD_OCL_SYM(clReleaseKernel);
	LOAD_OCL_SYM(clSetKernelArg);
	LOAD_OCL_SYM(clFinish);
	LOAD_OCL_SYM(clEnqueueReadBuffer);
	LOAD_OCL_SYM(clEnqueueWriteBuffer);
	LOAD_OCL_SYM(clEnqueueNDRangeKernel);
	
	return true;
}
#endif


#ifdef HAVE_CURSES
extern WINDOW *mainwin, *statuswin, *logwin;
extern void enable_curses(void);
#endif

extern int mining_threads;
extern double total_secs;
extern int opt_g_threads;
extern bool ping;
extern bool opt_loginput;
extern char *opt_kernel_path;
extern int gpur_thr_id;
extern bool opt_noadl;
extern bool have_opencl;



extern void *miner_thread(void *userdata);
extern int dev_from_id(int thr_id);
extern void tailsprintf(char *f, const char *fmt, ...);
extern void wlog(const char *f, ...);
extern void decay_time(double *f, double fadd);


/**********************************************/

#ifdef HAVE_ADL
extern float gpu_temp(int gpu);
extern int gpu_fanspeed(int gpu);
extern int gpu_fanpercent(int gpu);
#endif


#ifdef HAVE_OPENCL
char *set_vector(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set vector";
	val = atoi(nextptr);
	if (val != 1 && val != 2 && val != 4)
		return "Invalid value passed to set_vector";

	gpus[device++].vwidth = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val != 1 && val != 2 && val != 4)
			return "Invalid value passed to set_vector";

		gpus[device++].vwidth = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].vwidth = gpus[0].vwidth;
	}

	return NULL;
}

char *set_worksize(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set work size";
	val = atoi(nextptr);
	if (val < 1 || val > 9999)
		return "Invalid value passed to set_worksize";

	gpus[device++].work_size = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val < 1 || val > 9999)
			return "Invalid value passed to set_worksize";

		gpus[device++].work_size = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].work_size = gpus[0].work_size;
	}

	return NULL;
}

#ifdef USE_SCRYPT
char *set_shaders(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set lookup gap";
	val = atoi(nextptr);

	gpus[device++].shaders = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);

		gpus[device++].shaders = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].shaders = gpus[0].shaders;
	}

	return NULL;
}

char *set_lookup_gap(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set lookup gap";
	val = atoi(nextptr);

	gpus[device++].opt_lg = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);

		gpus[device++].opt_lg = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].opt_lg = gpus[0].opt_lg;
	}

	return NULL;
}

char *set_thread_concurrency(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set thread concurrency";
	val = atoi(nextptr);

	gpus[device++].opt_tc = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);

		gpus[device++].opt_tc = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].opt_tc = gpus[0].opt_tc;
	}

	return NULL;
}
#endif

static enum cl_kernels select_kernel(char *arg)
{
	if (!strcmp(arg, "diablo"))
		return KL_DIABLO;
	if (!strcmp(arg, "diakgcn"))
		return KL_DIAKGCN;
	if (!strcmp(arg, "poclbm"))
		return KL_POCLBM;
	if (!strcmp(arg, "phatk"))
		return KL_PHATK;
#ifdef USE_SCRYPT
	if (!strcmp(arg, "scrypt"))
		return KL_SCRYPT;
#endif
	return KL_NONE;
}

char *set_kernel(char *arg)
{
	enum cl_kernels kern;
	int i, device = 0;
	char *nextptr;

	if (opt_scrypt)
		return "Cannot use sha256 kernel with scrypt";
	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set kernel";
	kern = select_kernel(nextptr);
	if (kern == KL_NONE)
		return "Invalid parameter to set_kernel";
	gpus[device++].kernel = kern;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		kern = select_kernel(nextptr);
		if (kern == KL_NONE)
			return "Invalid parameter to set_kernel";

		gpus[device++].kernel = kern;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].kernel = gpus[0].kernel;
	}

	return NULL;
}
#endif

#ifdef HAVE_ADL
/* This function allows us to map an adl device to an opencl device for when
 * simple enumeration has failed to match them. */
char *set_gpu_map(char *arg)
{
	int val1 = 0, val2 = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu map";
	if (sscanf(arg, "%d:%d", &val1, &val2) != 2)
		return "Invalid description for map pair";
	if (val1 < 0 || val1 > MAX_GPUDEVICES || val2 < 0 || val2 > MAX_GPUDEVICES)
		return "Invalid value passed to set_gpu_map";

	gpus[val1].virtual_adl = val2;
	gpus[val1].mapped = true;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		if (sscanf(nextptr, "%d:%d", &val1, &val2) != 2)
			return "Invalid description for map pair";
		if (val1 < 0 || val1 > MAX_GPUDEVICES || val2 < 0 || val2 > MAX_GPUDEVICES)
			return "Invalid value passed to set_gpu_map";
		gpus[val1].virtual_adl = val2;
		gpus[val1].mapped = true;
	}

	return NULL;
}

void get_intrange(char *arg, int *val1, int *val2)
{
	if (sscanf(arg, "%d-%d", val1, val2) == 1) {
		*val2 = *val1;
		*val1 = 0;
	}
}

char *set_gpu_engine(char *arg)
{
	int i, val1 = 0, val2 = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu engine";
	get_intrange(nextptr, &val1, &val2);
	if (val1 < 0 || val1 > 9999 || val2 < 0 || val2 > 9999)
		return "Invalid value passed to set_gpu_engine";

	gpus[device].min_engine = val1;
	gpus[device].gpu_engine = val2;
	device++;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		get_intrange(nextptr, &val1, &val2);
		if (val1 < 0 || val1 > 9999 || val2 < 0 || val2 > 9999)
			return "Invalid value passed to set_gpu_engine";
		gpus[device].min_engine = val1;
		gpus[device].gpu_engine = val2;
		device++;
	}

	if (device == 1) {
		for (i = 1; i < MAX_GPUDEVICES; i++) {
			gpus[i].min_engine = gpus[0].min_engine;
			gpus[i].gpu_engine = gpus[0].gpu_engine;
		}
	}

	return NULL;
}

char *set_gpu_fan(char *arg)
{
	int i, val1 = 0, val2 = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu fan";
	get_intrange(nextptr, &val1, &val2);
	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100)
		return "Invalid value passed to set_gpu_fan";

	gpus[device].min_fan = val1;
	gpus[device].gpu_fan = val2;
	device++;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		get_intrange(nextptr, &val1, &val2);
		if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100)
			return "Invalid value passed to set_gpu_fan";

		gpus[device].min_fan = val1;
		gpus[device].gpu_fan = val2;
		device++;
	}

	if (device == 1) {
		for (i = 1; i < MAX_GPUDEVICES; i++) {
			gpus[i].min_fan = gpus[0].min_fan;
			gpus[i].gpu_fan = gpus[0].gpu_fan;
		}
	}

	return NULL;
}

char *set_gpu_memclock(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu memclock";
	val = atoi(nextptr);
	if (val < 0 || val >= 9999)
		return "Invalid value passed to set_gpu_memclock";

	gpus[device++].gpu_memclock = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val < 0 || val >= 9999)
			return "Invalid value passed to set_gpu_memclock";

		gpus[device++].gpu_memclock = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].gpu_memclock = gpus[0].gpu_memclock;
	}

	return NULL;
}

char *set_gpu_memdiff(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu memdiff";
	val = atoi(nextptr);
	if (val < -9999 || val > 9999)
		return "Invalid value passed to set_gpu_memdiff";

	gpus[device++].gpu_memdiff = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val < -9999 || val > 9999)
			return "Invalid value passed to set_gpu_memdiff";

		gpus[device++].gpu_memdiff = val;
	}
		if (device == 1) {
			for (i = device; i < MAX_GPUDEVICES; i++)
				gpus[i].gpu_memdiff = gpus[0].gpu_memdiff;
		}

			return NULL;
}

char *set_gpu_powertune(char *arg)
{
	int i, val = 0, device = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu powertune";
	val = atoi(nextptr);
	if (val < -99 || val > 99)
		return "Invalid value passed to set_gpu_powertune";

	gpus[device++].gpu_powertune = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val < -99 || val > 99)
			return "Invalid value passed to set_gpu_powertune";

		gpus[device++].gpu_powertune = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].gpu_powertune = gpus[0].gpu_powertune;
	}

	return NULL;
}

char *set_gpu_vddc(char *arg)
{
	int i, device = 0;
	float val = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu vddc";
	val = atof(nextptr);
	if (val < 0 || val >= 9999)
		return "Invalid value passed to set_gpu_vddc";

	gpus[device++].gpu_vddc = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atof(nextptr);
		if (val < 0 || val >= 9999)
			return "Invalid value passed to set_gpu_vddc";

		gpus[device++].gpu_vddc = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			gpus[i].gpu_vddc = gpus[0].gpu_vddc;
	}

	return NULL;
}

char *set_temp_overheat(char *arg)
{
	int i, val = 0, device = 0, *to;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set temp overheat";
	val = atoi(nextptr);
	if (val < 0 || val > 200)
		return "Invalid value passed to set temp overheat";

	to = &gpus[device++].adl.overtemp;
	*to = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val < 0 || val > 200)
			return "Invalid value passed to set temp overheat";

		to = &gpus[device++].adl.overtemp;
		*to = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++) {
			to = &gpus[i].adl.overtemp;
			*to = val;
		}
	}

	return NULL;
}

char *set_temp_target(char *arg)
{
	int i, val = 0, device = 0, *tt;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set temp target";
	val = atoi(nextptr);
	if (val < 0 || val > 200)
		return "Invalid value passed to set temp target";

	tt = &gpus[device++].adl.targettemp;
	*tt = val;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		val = atoi(nextptr);
		if (val < 0 || val > 200)
			return "Invalid value passed to set temp target";

		tt = &gpus[device++].adl.targettemp;
		*tt = val;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++) {
			tt = &gpus[i].adl.targettemp;
			*tt = val;
		}
	}

	return NULL;
}
#endif
#ifdef HAVE_OPENCL
char *set_intensity(char *arg)
{
	int i, device = 0, *tt;
	char *nextptr, val = 0;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set intensity";
	if (!strncasecmp(nextptr, "d", 1))
		gpus[device].dynamic = true;
	else {
		gpus[device].dynamic = false;
		val = atoi(nextptr);
		if (val < MIN_INTENSITY || val > MAX_INTENSITY)
			return "Invalid value passed to set intensity";
		tt = &gpus[device].intensity;
		*tt = val;
	}

	device++;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		if (!strncasecmp(nextptr, "d", 1))
			gpus[device].dynamic = true;
		else {
			gpus[device].dynamic = false;
			val = atoi(nextptr);
			if (val < MIN_INTENSITY || val > MAX_INTENSITY)
				return "Invalid value passed to set intensity";

			tt = &gpus[device].intensity;
			*tt = val;
		}
		device++;
	}
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++) {
			gpus[i].dynamic = gpus[0].dynamic;
			gpus[i].intensity = gpus[0].intensity;
		}
	}

	return NULL;
}
#endif


#ifdef HAVE_OPENCL
struct device_api opencl_api;

char *print_ndevs_and_exit(int *ndevs)
{
	opt_log_output = true;
	opencl_api.api_detect();
	clear_adl(*ndevs);
	applog(LOG_INFO, "%i GPU devices max detected", *ndevs);
	exit(*ndevs);
}
#endif


struct cgpu_info gpus[MAX_GPUDEVICES]; /* Maximum number apparently possible */
struct cgpu_info *cpus;



#ifdef HAVE_OPENCL

#ifdef WIN32
static UINT timeperiod_set;
#endif

void opencl_dynamic_cleanup() {
#ifdef WIN32
	if (timeperiod_set) {
		timeEndPeriod(timeperiod_set);
		timeperiod_set = 0;
	}
#endif
}

extern int opt_dynamic_interval;

/* In dynamic mode, only the first thread of each device will be in use.
 * This potentially could start a thread that was stopped with the start-stop
 * options if one were to disable dynamic from the menu on a paused GPU */
void pause_dynamic_threads(int gpu)
{
	struct cgpu_info *cgpu = &gpus[gpu];
	int i;
#ifdef WIN32
	bool any_dynamic = false;
#endif

	for (i = 1; i < cgpu->threads; i++) {
		struct thr_info *thr = &thr_info[i];

#ifdef WIN32
		if (cgpu->dynamic)
			any_dynamic = true;
#endif

		if (!thr->pause && cgpu->dynamic) {
			applog(LOG_WARNING, "Disabling extra threads due to dynamic mode.");
			applog(LOG_WARNING, "Tune dynamic intensity with --gpu-dyninterval");
		}

		thr->pause = cgpu->dynamic;
		if (!cgpu->dynamic && cgpu->deven != DEV_DISABLED)
			tq_push(thr->q, &ping);
	}

#ifdef WIN32
	if (any_dynamic) {
		if (!timeperiod_set) {
			timeperiod_set = opt_dynamic_interval > 3 ? (opt_dynamic_interval / 2) : 1;
			if (TIMERR_NOERROR != timeBeginPeriod(timeperiod_set))
				timeperiod_set = 0;
		}
	} else {
		if (timeperiod_set) {
			opencl_dynamic_cleanup();
		}
	}
#endif
}


struct device_api opencl_api;

#endif /* HAVE_OPENCL */

#if defined(HAVE_OPENCL) && defined(HAVE_CURSES)
void manage_gpu(void)
{
	struct thr_info *thr;
	int selected, gpu, i;
	char checkin[40];
	char input;

	if (!opt_g_threads)
		return;

	opt_loginput = true;
	immedok(logwin, true);
	clear_logwin();
retry:

	for (gpu = 0; gpu < nDevs; gpu++) {
		struct cgpu_info *cgpu = &gpus[gpu];
		double displayed_rolling, displayed_total;
		bool mhash_base = true;

		displayed_rolling = cgpu->rolling;
		displayed_total = cgpu->total_mhashes / total_secs;
		if (displayed_rolling < 1) {
			displayed_rolling *= 1000;
			displayed_total *= 1000;
			mhash_base = false;
		}

		wlog("GPU %d: %.1f / %.1f %sh/s | A:%d  R:%d  HW:%d  U:%.2f/m  I:%d\n",
			gpu, displayed_rolling, displayed_total, mhash_base ? "M" : "K",
			cgpu->accepted, cgpu->rejected, cgpu->hw_errors,
			cgpu->utility, cgpu->intensity);
#ifdef HAVE_ADL
		if (gpus[gpu].has_adl) {
			int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
			float temp = 0, vddc = 0;

			if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune)) {
				char logline[255];

				strcpy(logline, ""); // In case it has no data
				if (temp != -1)
					sprintf(logline, "%.1f C  ", temp);
				if (fanspeed != -1 || fanpercent != -1) {
					tailsprintf(logline, "F: ");
					if (fanpercent != -1)
						tailsprintf(logline, "%d%% ", fanpercent);
					if (fanspeed != -1)
						tailsprintf(logline, "(%d RPM) ", fanspeed);
					tailsprintf(logline, " ");
				}
				if (engineclock != -1)
					tailsprintf(logline, "E: %d MHz  ", engineclock);
				if (memclock != -1)
					tailsprintf(logline, "M: %d Mhz  ", memclock);
				if (vddc != -1)
					tailsprintf(logline, "V: %.3fV  ", vddc);
				if (activity != -1)
					tailsprintf(logline, "A: %d%%  ", activity);
				if (powertune != -1)
					tailsprintf(logline, "P: %d%%", powertune);
				tailsprintf(logline, "\n");
				wlog(logline);
			}
		}
#endif
		wlog("Last initialised: %s\n", cgpu->init);
		wlog("Intensity: ");
		if (gpus[gpu].dynamic)
			wlog("Dynamic (only one thread in use)\n");
		else
			wlog("%d\n", gpus[gpu].intensity);
		for (i = 0; i < mining_threads; i++) {
			thr = &thr_info[i];
			if (thr->cgpu != cgpu)
				continue;
			get_datestamp(checkin, &thr->last);
			displayed_rolling = thr->rolling;
			if (!mhash_base)
				displayed_rolling *= 1000;
			wlog("Thread %d: %.1f %sh/s %s ", i, displayed_rolling, mhash_base ? "M" : "K" , cgpu->deven != DEV_DISABLED ? "Enabled" : "Disabled");
			switch (cgpu->status) {
				default:
				case LIFE_WELL:
					wlog("ALIVE");
					break;
				case LIFE_SICK:
					wlog("SICK reported in %s", checkin);
					break;
				case LIFE_DEAD:
					wlog("DEAD reported in %s", checkin);
					break;
				case LIFE_INIT:
				case LIFE_NOSTART:
					wlog("Never started");
					break;
			}
			if (thr->pause)
				wlog(" paused");
			wlog("\n");
		}
		wlog("\n");
	}

	wlogprint("[E]nable [D]isable [I]ntensity [R]estart GPU %s\n",adl_active ? "[C]hange settings" : "");

	wlogprint("Or press any other key to continue\n");
	input = getch();

	if (nDevs == 1)
		selected = 0;
	else
		selected = -1;
	if (!strncasecmp(&input, "e", 1)) {
		struct cgpu_info *cgpu;

		if (selected)
			selected = curses_int("Select GPU to enable");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		if (gpus[selected].deven != DEV_DISABLED) {
			wlogprint("Device already enabled\n");
			goto retry;
		}
		gpus[selected].deven = DEV_ENABLED;
		for (i = 0; i < mining_threads; ++i) {
			thr = &thr_info[i];
			cgpu = thr->cgpu;
			if (cgpu->api != &opencl_api)
				continue;
			if (dev_from_id(i) != selected)
				continue;
			if (cgpu->status != LIFE_WELL) {
				wlogprint("Must restart device before enabling it");
				goto retry;
			}
			applog(LOG_DEBUG, "Pushing ping to thread %d", thr->id);

			tq_push(thr->q, &ping);
		}
		goto retry;
	} if (!strncasecmp(&input, "d", 1)) {
		if (selected)
			selected = curses_int("Select GPU to disable");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		if (gpus[selected].deven == DEV_DISABLED) {
			wlogprint("Device already disabled\n");
			goto retry;
		}
		gpus[selected].deven = DEV_DISABLED;
		goto retry;
	} else if (!strncasecmp(&input, "i", 1)) {
		int intensity;
		char *intvar;

		if (selected)
			selected = curses_int("Select GPU to change intensity on");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		intvar = curses_input("Set GPU scan intensity (d or " _MIN_INTENSITY_STR " -> " _MAX_INTENSITY_STR ")");
		if (!intvar) {
			wlogprint("Invalid input\n");
			goto retry;
		}
		if (!strncasecmp(intvar, "d", 1)) {
			wlogprint("Dynamic mode enabled on gpu %d\n", selected);
			gpus[selected].dynamic = true;
			pause_dynamic_threads(selected);
			free(intvar);
			goto retry;
		}
		intensity = atoi(intvar);
		free(intvar);
		if (intensity < MIN_INTENSITY || intensity > MAX_INTENSITY) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		gpus[selected].dynamic = false;
		gpus[selected].intensity = intensity;
		wlogprint("Intensity on gpu %d set to %d\n", selected, intensity);
		pause_dynamic_threads(selected);
		goto retry;
	} else if (!strncasecmp(&input, "r", 1)) {
		if (selected)
			selected = curses_int("Select GPU to attempt to restart");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		wlogprint("Attempting to restart threads of GPU %d\n", selected);
		reinit_device(&gpus[selected]);
		goto retry;
	} else if (adl_active && (!strncasecmp(&input, "c", 1))) {
		if (selected)
			selected = curses_int("Select GPU to change settings on");
		if (selected < 0 || selected >= nDevs) {
			wlogprint("Invalid selection\n");
			goto retry;
		}
		change_gpusettings(selected);
		goto retry;
	} else
		clear_logwin();

	immedok(logwin, false);
	opt_loginput = false;
}
#else
void manage_gpu(void)
{
}
#endif


#ifdef HAVE_OPENCL
static _clState *clStates[MAX_GPUDEVICES];

#define CL_SET_BLKARG(blkvar) status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->blkvar)
#define CL_SET_ARG(var) status |= clSetKernelArg(*kernel, num++, sizeof(var), (void *)&var)
#define CL_SET_VARG(args, var) status |= clSetKernelArg(*kernel, num++, args * sizeof(uint), (void *)var)

static cl_int queue_poclbm_kernel(_clState *clState, dev_blk_ctx *blk, cl_uint threads)
{
	cl_kernel *kernel = &clState->kernel;
	unsigned int num = 0;
	cl_int status = 0;

	CL_SET_BLKARG(ctx_a);
	CL_SET_BLKARG(ctx_b);
	CL_SET_BLKARG(ctx_c);
	CL_SET_BLKARG(ctx_d);
	CL_SET_BLKARG(ctx_e);
	CL_SET_BLKARG(ctx_f);
	CL_SET_BLKARG(ctx_g);
	CL_SET_BLKARG(ctx_h);

	CL_SET_BLKARG(cty_b);
	CL_SET_BLKARG(cty_c);

	
	CL_SET_BLKARG(cty_f);
	CL_SET_BLKARG(cty_g);
	CL_SET_BLKARG(cty_h);

	if (!clState->goffset) {
		cl_uint vwidth = clState->vwidth;
		uint *nonces = alloca(sizeof(uint) * vwidth);
		unsigned int i;

		for (i = 0; i < vwidth; i++)
			nonces[i] = blk->nonce + (i * threads);
		CL_SET_VARG(vwidth, nonces);
	}

	CL_SET_BLKARG(fW0);
	CL_SET_BLKARG(fW1);
	CL_SET_BLKARG(fW2);
	CL_SET_BLKARG(fW3);
	CL_SET_BLKARG(fW15);
	CL_SET_BLKARG(fW01r);

	CL_SET_BLKARG(D1A);
	CL_SET_BLKARG(C1addK5);
	CL_SET_BLKARG(B1addK6);
	CL_SET_BLKARG(W16addK16);
	CL_SET_BLKARG(W17addK17);
	CL_SET_BLKARG(PreVal4addT1);
	CL_SET_BLKARG(PreVal0);

	CL_SET_ARG(clState->outputBuffer);

	return status;
}

static cl_int queue_phatk_kernel(_clState *clState, dev_blk_ctx *blk,
				 __maybe_unused cl_uint threads)
{
	cl_kernel *kernel = &clState->kernel;
	cl_uint vwidth = clState->vwidth;
	unsigned int i, num = 0;
	cl_int status = 0;
	uint *nonces;

	CL_SET_BLKARG(ctx_a);
	CL_SET_BLKARG(ctx_b);
	CL_SET_BLKARG(ctx_c);
	CL_SET_BLKARG(ctx_d);
	CL_SET_BLKARG(ctx_e);
	CL_SET_BLKARG(ctx_f);
	CL_SET_BLKARG(ctx_g);
	CL_SET_BLKARG(ctx_h);

	CL_SET_BLKARG(cty_b);
	CL_SET_BLKARG(cty_c);
	CL_SET_BLKARG(cty_d);
	CL_SET_BLKARG(cty_f);
	CL_SET_BLKARG(cty_g);
	CL_SET_BLKARG(cty_h);

	nonces = alloca(sizeof(uint) * vwidth);
	for (i = 0; i < vwidth; i++)
		nonces[i] = blk->nonce + i;
	CL_SET_VARG(vwidth, nonces);

	CL_SET_BLKARG(W16);
	CL_SET_BLKARG(W17);
	CL_SET_BLKARG(PreVal4_2);
	CL_SET_BLKARG(PreVal0);
	CL_SET_BLKARG(PreW18);
	CL_SET_BLKARG(PreW19);
	CL_SET_BLKARG(PreW31);
	CL_SET_BLKARG(PreW32);

	CL_SET_ARG(clState->outputBuffer);

	return status;
}

static cl_int queue_diakgcn_kernel(_clState *clState, dev_blk_ctx *blk,
				   __maybe_unused cl_uint threads)
{
	cl_kernel *kernel = &clState->kernel;
	unsigned int num = 0;
	cl_int status = 0;

	if (!clState->goffset) {
		cl_uint vwidth = clState->vwidth;
		uint *nonces = alloca(sizeof(uint) * vwidth);
		unsigned int i;
		for (i = 0; i < vwidth; i++)
			nonces[i] = blk->nonce + i;
		CL_SET_VARG(vwidth, nonces);
	}

	CL_SET_BLKARG(PreVal0);
	CL_SET_BLKARG(PreVal4_2);
	CL_SET_BLKARG(cty_h);
	CL_SET_BLKARG(D1A);
	CL_SET_BLKARG(cty_b);
	CL_SET_BLKARG(cty_c);
	CL_SET_BLKARG(cty_f);
	CL_SET_BLKARG(cty_g);
	CL_SET_BLKARG(C1addK5);
	CL_SET_BLKARG(B1addK6);
	CL_SET_BLKARG(PreVal0addK7);
	CL_SET_BLKARG(W16addK16);
	CL_SET_BLKARG(W17addK17);
	CL_SET_BLKARG(PreW18);
	CL_SET_BLKARG(PreW19);
	CL_SET_BLKARG(W16);
	CL_SET_BLKARG(W17);
	CL_SET_BLKARG(PreW31);
	CL_SET_BLKARG(PreW32);

	CL_SET_BLKARG(ctx_a);
	CL_SET_BLKARG(ctx_b);
	CL_SET_BLKARG(ctx_c);
	CL_SET_BLKARG(ctx_d);
	CL_SET_BLKARG(ctx_e);
	CL_SET_BLKARG(ctx_f);
	CL_SET_BLKARG(ctx_g);
	CL_SET_BLKARG(ctx_h);

	CL_SET_BLKARG(zeroA);
	CL_SET_BLKARG(zeroB);

	CL_SET_BLKARG(oneA);
	CL_SET_BLKARG(twoA);
	CL_SET_BLKARG(threeA);
	CL_SET_BLKARG(fourA);
	CL_SET_BLKARG(fiveA);
	CL_SET_BLKARG(sixA);
	CL_SET_BLKARG(sevenA);

	CL_SET_ARG(clState->outputBuffer);

	return status;
}

static cl_int queue_diablo_kernel(_clState *clState, dev_blk_ctx *blk, cl_uint threads)
{
	cl_kernel *kernel = &clState->kernel;
	unsigned int num = 0;
	cl_int status = 0;

	if (!clState->goffset) {
		cl_uint vwidth = clState->vwidth;
		uint *nonces = alloca(sizeof(uint) * vwidth);
		unsigned int i;

		for (i = 0; i < vwidth; i++)
			nonces[i] = blk->nonce + (i * threads);
		CL_SET_VARG(vwidth, nonces);
	}


	CL_SET_BLKARG(PreVal0);
	CL_SET_BLKARG(PreVal0addK7);
	CL_SET_BLKARG(PreVal4addT1);
	CL_SET_BLKARG(PreW18);
	CL_SET_BLKARG(PreW19);
	CL_SET_BLKARG(W16);
	CL_SET_BLKARG(W17);
	CL_SET_BLKARG(W16addK16);
	CL_SET_BLKARG(W17addK17);
	CL_SET_BLKARG(PreW31);
	CL_SET_BLKARG(PreW32);

	CL_SET_BLKARG(D1A);
	CL_SET_BLKARG(cty_b);
	CL_SET_BLKARG(cty_c);
	CL_SET_BLKARG(cty_h);
	CL_SET_BLKARG(cty_f);
	CL_SET_BLKARG(cty_g);

	CL_SET_BLKARG(C1addK5);
	CL_SET_BLKARG(B1addK6);

	CL_SET_BLKARG(ctx_a);
	CL_SET_BLKARG(ctx_b);
	CL_SET_BLKARG(ctx_c);
	CL_SET_BLKARG(ctx_d);
	CL_SET_BLKARG(ctx_e);
	CL_SET_BLKARG(ctx_f);
	CL_SET_BLKARG(ctx_g);
	CL_SET_BLKARG(ctx_h);

	CL_SET_ARG(clState->outputBuffer);

	return status;
}

#ifdef USE_SCRYPT
static cl_int queue_scrypt_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
	unsigned char *midstate = blk->work->midstate;
	cl_kernel *kernel = &clState->kernel;
	unsigned int num = 0;
	cl_uint le_target;
	cl_int status = 0;

	le_target = *(cl_uint *)(blk->work->target + 28);
	clState->cldata = blk->work->data;
	status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL,NULL);

	CL_SET_ARG(clState->CLbuffer0);
	CL_SET_ARG(clState->outputBuffer);
	CL_SET_ARG(clState->padbuffer8);
	CL_SET_VARG(4, &midstate[0]);
	CL_SET_VARG(4, &midstate[16]);
	CL_SET_ARG(le_target);

	return status;
}
#endif

static void set_threads_hashes(unsigned int vectors, unsigned int *threads,
			       int64_t *hashes, size_t *globalThreads,
			       unsigned int minthreads, int intensity)
{
	if (opt_scrypt) {
		if (intensity < 0)
			intensity = 0;
		*threads = 1 << intensity;
	} else
		*threads = 1 << (15 + intensity);
	if (*threads < minthreads)
		*threads = minthreads;
	*globalThreads = *threads;
	*hashes = *threads * vectors;
}
#endif /* HAVE_OPENCL */


#ifdef HAVE_OPENCL
/* We have only one thread that ever re-initialises GPUs, thus if any GPU
 * init command fails due to a completely wedged GPU, the thread will never
 * return, unable to harm other GPUs. If it does return, it means we only had
 * a soft failure and then the reinit_gpu thread is ready to tackle another
 * GPU */
void *reinit_gpu(void *userdata)
{
	struct thr_info *mythr = userdata;
	struct cgpu_info *cgpu;
	struct thr_info *thr;
	struct timeval now;
	char name[256];
	int thr_id;
	int gpu;

	pthread_detach(pthread_self());
	rename_thr("bfg-reinit_gpu");

select_cgpu:
	cgpu = tq_pop(mythr->q, NULL);
	if (!cgpu)
		goto out;

	if (clDevicesNum() != nDevs) {
		applog(LOG_WARNING, "Hardware not reporting same number of active devices, will not attempt to restart GPU");
		goto out;
	}

	gpu = cgpu->device_id;

	for (thr_id = 0; thr_id < mining_threads; ++thr_id) {
		thr = &thr_info[thr_id];
		cgpu = thr->cgpu;
		if (cgpu->api != &opencl_api)
			continue;
		if (dev_from_id(thr_id) != gpu)
			continue;

		thr = &thr_info[thr_id];
		if (!thr) {
			applog(LOG_WARNING, "No reference to thread %d exists", thr_id);
			continue;
		}

		thr->rolling = thr->cgpu->rolling = 0;
		/* Reports the last time we tried to revive a sick GPU */
		gettimeofday(&thr->sick, NULL);
		if (!pthread_cancel(thr->pth)) {
			applog(LOG_WARNING, "Thread %d still exists, killing it off", thr_id);
		} else
			applog(LOG_WARNING, "Thread %d no longer exists", thr_id);
	}

	for (thr_id = 0; thr_id < mining_threads; ++thr_id) {
		int virtual_gpu;

		thr = &thr_info[thr_id];
		cgpu = thr->cgpu;
		if (cgpu->api != &opencl_api)
			continue;
		if (dev_from_id(thr_id) != gpu)
			continue;

		virtual_gpu = cgpu->virtual_gpu;
		/* Lose this ram cause we may get stuck here! */
		//tq_freeze(thr->q);

		thr->q = tq_new();
		if (!thr->q)
			quit(1, "Failed to tq_new in reinit_gpu");

		/* Lose this ram cause we may dereference in the dying thread! */
		//free(clState);

		applog(LOG_INFO, "Reinit GPU thread %d", thr_id);
		clStates[thr_id] = initCl(virtual_gpu, name, sizeof(name));
		if (!clStates[thr_id]) {
			applog(LOG_ERR, "Failed to reinit GPU thread %d", thr_id);
			goto select_cgpu;
		}
		applog(LOG_INFO, "initCl() finished. Found %s", name);

		if (unlikely(thr_info_create(thr, NULL, miner_thread, thr))) {
			applog(LOG_ERR, "thread %d create failed", thr_id);
			return NULL;
		}
		applog(LOG_WARNING, "Thread %d restarted", thr_id);
	}

	gettimeofday(&now, NULL);
	get_datestamp(cgpu->init, &now);

	for (thr_id = 0; thr_id < mining_threads; ++thr_id) {
		thr = &thr_info[thr_id];
		cgpu = thr->cgpu;
		if (cgpu->api != &opencl_api)
			continue;
		if (dev_from_id(thr_id) != gpu)
			continue;

		tq_push(thr->q, &ping);
	}

	goto select_cgpu;
out:
	return NULL;
}
#else
void *reinit_gpu(__maybe_unused void *userdata)
{
	return NULL;
}
#endif


#ifdef HAVE_OPENCL
struct device_api opencl_api;

static void opencl_detect()
{
	if (!load_opencl_symbols()) {
		nDevs = 0;
		return;
	}


	int i;

	nDevs = clDevicesNum();
	if (nDevs < 0) {
		applog(LOG_ERR, "clDevicesNum returned error, no GPUs usable");
		nDevs = 0;
	}

	if (!nDevs)
		return;

	for (i = 0; i < nDevs; ++i) {
		struct cgpu_info *cgpu;

		cgpu = &gpus[i];
		cgpu->devtype = "GPU";
		cgpu->deven = DEV_ENABLED;
		cgpu->api = &opencl_api;
		cgpu->device_id = i;
		cgpu->threads = opt_g_threads;
		cgpu->virtual_gpu = i;
		add_cgpu(cgpu);
	}

	if (!opt_noadl)
		init_adl(nDevs);
}

static void reinit_opencl_device(struct cgpu_info *gpu)
{
	tq_push(thr_info[gpur_thr_id].q, gpu);
}

#ifdef HAVE_ADL
static void get_opencl_statline_before(char *buf, struct cgpu_info *gpu)
{
	if (gpu->has_adl) {
		int gpuid = gpu->device_id;
		float gt = gpu_temp(gpuid);
		int gf = gpu_fanspeed(gpuid);
		int gp;

		if (gt != -1)
			tailsprintf(buf, "%5.1fC ", gt);
		else
			tailsprintf(buf, "       ", gt);
		if (gf != -1)
			tailsprintf(buf, "%4dRPM ", gf);
		else if ((gp = gpu_fanpercent(gpuid)) != -1)
			tailsprintf(buf, "%3d%%    ", gp);
		else
			tailsprintf(buf, "        ");
		tailsprintf(buf, "| ");
	}
	else
		tailsprintf(buf, "               | ");
}
#endif

static struct api_data*
get_opencl_api_extra_device_status(struct cgpu_info *gpu)
{
	struct api_data*root = NULL;

	float gt, gv;
	int ga, gf, gp, gc, gm, pt;
#ifdef HAVE_ADL
	if (!gpu_stats(gpu->device_id, &gt, &gc, &gm, &gv, &ga, &gf, &gp, &pt))
#endif
		gt = gv = gm = gc = ga = gf = gp = pt = 0;
	root = api_add_int(root, "Fan Speed", &gf, true);
	root = api_add_int(root, "Fan Percent", &gp, true);
	root = api_add_int(root, "GPU Clock", &gc, true);
	root = api_add_int(root, "Memory Clock", &gm, true);
	root = api_add_volts(root, "GPU Voltage", &gv, true);
	root = api_add_int(root, "GPU Activity", &ga, true);
	root = api_add_int(root, "Powertune", &pt, true);

	char intensity[20];
	if (gpu->dynamic)
		strcpy(intensity, "D");
	else
		sprintf(intensity, "%d", gpu->intensity);
	root = api_add_string(root, "Intensity", intensity, true);

	return root;
}

struct opencl_thread_data {
	cl_int (*queue_kernel_parameters)(_clState *, dev_blk_ctx *, cl_uint);
	uint32_t *res;
	struct work *last_work;
	struct work _last_work;
};

static uint32_t *blank_res;

static bool opencl_thread_prepare(struct thr_info *thr)
{
	char name[256];
	struct timeval now;
	struct cgpu_info *cgpu = thr->cgpu;
	int gpu = cgpu->device_id;
	int virtual_gpu = cgpu->virtual_gpu;
	int i = thr->id;
	static bool failmessage = false;

	if (!blank_res)
		blank_res = calloc(BUFFERSIZE, 1);
	if (!blank_res) {
		applog(LOG_ERR, "Failed to calloc in opencl_thread_init");
		return false;
	}

	strcpy(name, "");
	applog(LOG_INFO, "Init GPU thread %i GPU %i virtual GPU %i", i, gpu, virtual_gpu);
	clStates[i] = initCl(virtual_gpu, name, sizeof(name));
	if (!clStates[i]) {
#ifdef HAVE_CURSES
		if (use_curses)
			enable_curses();
#endif
		applog(LOG_ERR, "Failed to init GPU thread %d, disabling device %d", i, gpu);
		if (!failmessage) {
			applog(LOG_ERR, "Restarting the GPU from the menu will not fix this.");
			applog(LOG_ERR, "Try restarting BFGMiner.");
			failmessage = true;
#ifdef HAVE_CURSES
			char *buf;
			if (use_curses) {
				buf = curses_input("Press enter to continue");
				if (buf)
					free(buf);
			}
#endif
		}
		cgpu->deven = DEV_DISABLED;
		cgpu->status = LIFE_NOSTART;

		cgpu->device_last_not_well = time(NULL);
		cgpu->device_not_well_reason = REASON_DEV_NOSTART;
		cgpu->dev_nostart_count++;

		return false;
	}
	if (!cgpu->name)
		cgpu->name = strdup(name);
	if (!cgpu->kname)
	{
		switch (clStates[i]->chosen_kernel) {
			case KL_DIABLO:
				cgpu->kname = "diablo";
				break;
			case KL_DIAKGCN:
				cgpu->kname = "diakgcn";
				break;
			case KL_PHATK:
				cgpu->kname = "phatk";
				break;
#ifdef USE_SCRYPT
			case KL_SCRYPT:
				cgpu->kname = "scrypt";
				break;
#endif
			case KL_POCLBM:
				cgpu->kname = "poclbm";
				break;
			default:
				break;
		}
	}
	applog(LOG_INFO, "initCl() finished. Found %s", name);
	gettimeofday(&now, NULL);
	get_datestamp(cgpu->init, &now);

	have_opencl = true;

	return true;
}

static bool opencl_thread_init(struct thr_info *thr)
{
	const int thr_id = thr->id;
	struct cgpu_info *gpu = thr->cgpu;
	struct opencl_thread_data *thrdata;
	_clState *clState = clStates[thr_id];
	cl_int status = 0;
	thrdata = calloc(1, sizeof(*thrdata));
	thr->cgpu_data = thrdata;

	if (!thrdata) {
		applog(LOG_ERR, "Failed to calloc in opencl_thread_init");
		return false;
	}

	switch (clState->chosen_kernel) {
		case KL_POCLBM:
			thrdata->queue_kernel_parameters = &queue_poclbm_kernel;
			break;
		case KL_PHATK:
			thrdata->queue_kernel_parameters = &queue_phatk_kernel;
			break;
		case KL_DIAKGCN:
			thrdata->queue_kernel_parameters = &queue_diakgcn_kernel;
			break;
#ifdef USE_SCRYPT
		case KL_SCRYPT:
			thrdata->queue_kernel_parameters = &queue_scrypt_kernel;
			break;
#endif
		default:
		case KL_DIABLO:
			thrdata->queue_kernel_parameters = &queue_diablo_kernel;
			break;
	}

	thrdata->res = calloc(BUFFERSIZE, 1);

	if (!thrdata->res) {
		free(thrdata);
		applog(LOG_ERR, "Failed to calloc in opencl_thread_init");
		return false;
	}

	status |= clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0,
			BUFFERSIZE, blank_res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed.");
		return false;
	}

	gpu->status = LIFE_WELL;

	gpu->device_last_well = time(NULL);

	return true;
}

static void opencl_free_work(struct thr_info *thr, struct work *work)
{
	const int thr_id = thr->id;
	struct opencl_thread_data *thrdata = thr->cgpu_data;
	_clState *clState = clStates[thr_id];
	struct cgpu_info *gpu = thr->cgpu;

	if (gpu->dynamic)
		return;

	clFinish(clState->commandQueue);
	if (thrdata->res[FOUND]) {
		thrdata->last_work = &thrdata->_last_work;
		memcpy(thrdata->last_work, work, sizeof(*thrdata->last_work));
	}
}

static bool opencl_prepare_work(struct thr_info __maybe_unused *thr, struct work *work)
{
#ifdef USE_SCRYPT
	if (opt_scrypt)
		work->blk.work = work;
	else
#endif
		precalc_hash(&work->blk, (uint32_t *)(work->midstate), (uint32_t *)(work->data + 64));
	return true;
}

extern int opt_dynamic_interval;

static int64_t opencl_scanhash(struct thr_info *thr, struct work *work,
				int64_t __maybe_unused max_nonce)
{
	const int thr_id = thr->id;
	struct opencl_thread_data *thrdata = thr->cgpu_data;
	struct cgpu_info *gpu = thr->cgpu;
	_clState *clState = clStates[thr_id];
	const cl_kernel *kernel = &clState->kernel;
	const int dynamic_us = opt_dynamic_interval * 1000;

	cl_int status;
	size_t globalThreads[1];
	size_t localThreads[1] = { clState->wsize };
	unsigned int threads;
	int64_t hashes;

	/* This finish flushes the readbuffer set with CL_FALSE later */
	if (!gpu->dynamic)
		clFinish(clState->commandQueue);

	set_threads_hashes(clState->vwidth, &threads, &hashes, globalThreads,
			   localThreads[0], gpu->intensity);
	if (hashes > gpu->max_hashes)
		gpu->max_hashes = hashes;

	/* FOUND entry is used as a counter to say how many nonces exist */
	if (thrdata->res[FOUND]) {
		/* Clear the buffer again */
		status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
				BUFFERSIZE, blank_res, 0, NULL, NULL);
		if (unlikely(status != CL_SUCCESS)) {
			applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed.");
			return -1;
		}
		if (unlikely(thrdata->last_work)) {
			applog(LOG_DEBUG, "GPU %d found something in last work?", gpu->device_id);
			postcalc_hash_async(thr, thrdata->last_work, thrdata->res);
			thrdata->last_work = NULL;
		} else {
			applog(LOG_DEBUG, "GPU %d found something?", gpu->device_id);
			postcalc_hash_async(thr, work, thrdata->res);
		}
		memset(thrdata->res, 0, BUFFERSIZE);
		clFinish(clState->commandQueue);
	}

	gettimeofday(&gpu->tv_gpustart, NULL);

	status = thrdata->queue_kernel_parameters(clState, &work->blk, globalThreads[0]);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: clSetKernelArg of all params failed.");
		return -1;
	}

	if (clState->goffset) {
		size_t global_work_offset[1];

		global_work_offset[0] = work->blk.nonce;
		status = clEnqueueNDRangeKernel(clState->commandQueue, *kernel, 1, global_work_offset,
						globalThreads, localThreads, 0,  NULL, NULL);
	} else
		status = clEnqueueNDRangeKernel(clState->commandQueue, *kernel, 1, NULL,
						globalThreads, localThreads, 0,  NULL, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error %d: Enqueueing kernel onto command queue. (clEnqueueNDRangeKernel)", status);
		return -1;
	}

	status = clEnqueueReadBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
			BUFFERSIZE, thrdata->res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: clEnqueueReadBuffer failed error %d. (clEnqueueReadBuffer)", status);
		return -1;
	}

	if (gpu->dynamic) {
		struct timeval diff;
		suseconds_t gpu_us;

		clFinish(clState->commandQueue);
		gettimeofday(&gpu->tv_gpuend, NULL);
		timersub(&gpu->tv_gpuend, &gpu->tv_gpustart, &diff);
		gpu_us = diff.tv_sec * 1000000 + diff.tv_usec;
		if (likely(gpu_us >= 0)) {
			gpu->gpu_us_average = (gpu->gpu_us_average + gpu_us * 0.63) / 1.63;

			/* Try to not let the GPU be out for longer than
			 * opt_dynamic_interval in ms, but increase
			 * intensity when the system is idle in dynamic mode */
			if (gpu->gpu_us_average > dynamic_us) {
				if (gpu->intensity > MIN_INTENSITY)
					--gpu->intensity;
			} else if (gpu->gpu_us_average < dynamic_us / 2) {
				if (gpu->intensity < MAX_INTENSITY)
					++gpu->intensity;
			}
		}
	}

	/* The amount of work scanned can fluctuate when intensity changes
	 * and since we do this one cycle behind, we increment the work more
	 * than enough to prevent repeating work */
	work->blk.nonce += gpu->max_hashes;

	return hashes;
}

static void opencl_thread_shutdown(struct thr_info *thr)
{
	const int thr_id = thr->id;
	_clState *clState = clStates[thr_id];

	clReleaseCommandQueue(clState->commandQueue);
	clReleaseKernel(clState->kernel);
	clReleaseProgram(clState->program);
	clReleaseContext(clState->context);
}

struct device_api opencl_api = {
	.dname = "opencl",
	.name = "OCL",
	.api_detect = opencl_detect,
	.reinit_device = reinit_opencl_device,
#ifdef HAVE_ADL
	.get_statline_before = get_opencl_statline_before,
#endif
	.get_api_extra_device_status = get_opencl_api_extra_device_status,
	.thread_prepare = opencl_thread_prepare,
	.thread_init = opencl_thread_init,
	.free_work = opencl_free_work,
	.prepare_work = opencl_prepare_work,
	.scanhash = opencl_scanhash,
	.thread_shutdown = opencl_thread_shutdown,
};
#endif



