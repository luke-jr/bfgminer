/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2011-2014 Luke Dashjr
 * Copyright 2014 Nate Woolls
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef HAVE_CURSES
// Must be before stdbool, since pdcurses typedefs bool :/
#include <curses.h>
#endif

#ifndef WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <sys/types.h>

#ifndef WIN32
#include <sys/resource.h>
#endif

#define OMIT_OPENCL_API

#include "compat.h"
#include "miner.h"
#include "deviceapi.h"
#include "driver-opencl.h"
#include "findnonce.h"
#include "ocl.h"
#include "adl.h"
#include "util.h"

/* TODO: cleanup externals ********************/


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
#if defined(__APPLE__)
	void *cl = dlopen("/System/Library/Frameworks/OpenCL.framework/Versions/Current/OpenCL", RTLD_LAZY);
#elif !defined(WIN32)
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


struct opencl_kernel_interface {
	const char *kiname;
	queue_kernel_parameters_func_t queue_kernel_parameters_func;
};


#ifdef HAVE_CURSES
extern WINDOW *mainwin, *statuswin, *logwin;
extern void enable_curses(void);
#endif

extern int mining_threads;
extern int opt_g_threads;
extern bool ping;
extern bool opt_loginput;
extern char *opt_kernel_path;
extern int gpur_thr_id;
extern bool opt_noadl;
extern bool have_opencl;
static _clState *clStates[MAX_GPUDEVICES];
static struct opencl_kernel_interface kernel_interfaces[];



extern void *miner_thread(void *userdata);
extern int dev_from_id(int thr_id);
extern void decay_time(double *f, double fadd);


/**********************************************/

#ifdef HAVE_ADL
extern float gpu_temp(int gpu);
extern int gpu_fanspeed(int gpu);
extern int gpu_fanpercent(int gpu);
#endif


void opencl_early_init()
{
	static struct opencl_device_data dataarray[MAX_GPUDEVICES];
	for (int i = 0; i < MAX_GPUDEVICES; ++i)
	{
		struct opencl_device_data * const data = &dataarray[i];
		*data = (struct opencl_device_data){
			.dynamic = true,
			.use_goffset = BTS_UNKNOWN,
			.intensity = intensity_not_set,
#ifdef USE_SCRYPT
			.lookup_gap = 2,
#endif
		};
		gpus[i] = (struct cgpu_info){
			.device_data = data,
		};
	}
}

static
const char *_set_list(char * const arg, const char * const emsg, bool (*set_func)(struct cgpu_info *, const char *))
{
	int i, device = 0;
	char *nextptr, buf[0x10];

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return emsg;
	if (!set_func(&gpus[device++], nextptr))
		return emsg;
	snprintf(buf, sizeof(buf), "%s", nextptr);

	while ((nextptr = strtok(NULL, ",")) != NULL)
		if (!set_func(&gpus[device++], nextptr))
			return emsg;
	if (device == 1) {
		for (i = device; i < MAX_GPUDEVICES; i++)
			set_func(&gpus[i], buf);
	}

	return NULL;
}

#define _SET_INTERFACE(PNAME)  \
static  \
const char *opencl_init_ ## PNAME (struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)  \
{  \
	if (!_set_ ## PNAME (proc, newvalue))  \
		return "Invalid value for " #PNAME;  \
	return NULL;  \
}  \
// END OF _SET_INTERFACE

#define _SET_INT_LIST2(PNAME, VCHECK, FIELD)  \
static  \
bool _set_ ## PNAME (struct cgpu_info * const cgpu, const char * const _val)  \
{  \
	const int v = atoi(_val);  \
	if (!(VCHECK))  \
		return false;  \
	FIELD = v;  \
	return true;  \
}  \
_SET_INTERFACE(PNAME)  \
const char *set_ ## PNAME(char *arg)  \
{  \
	return _set_list(arg, "Invalid value passed to " #PNAME, _set_ ## PNAME);  \
}  \
// END OF _SET_INT_LIST

#define _SET_INT_LIST(PNAME, VCHECK, FIELD)  \
	_SET_INT_LIST2(PNAME, VCHECK, ((struct opencl_device_data *)cgpu->device_data)->FIELD)

_SET_INT_LIST(vector  , (v == 1 || v == 2 || v == 4), vwidth   )
_SET_INT_LIST(worksize, (v >= 1 && v <= 9999)       , work_size)

#ifdef USE_SCRYPT
_SET_INT_LIST(shaders           , true, shaders)
_SET_INT_LIST(lookup_gap        , true, lookup_gap)
_SET_INT_LIST(thread_concurrency, true, thread_concurrency)
#endif

enum cl_kernels select_kernel(const char * const arg)
{
	for (unsigned i = 1; i < (unsigned)OPENCL_KERNEL_INTERFACE_COUNT; ++i)
		if (!strcasecmp(arg, kernel_interfaces[i].kiname))
			return i;
	return KL_NONE;
}

const char *opencl_get_kernel_interface_name(const enum cl_kernels kern)
{
	struct opencl_kernel_interface *ki = &kernel_interfaces[kern];
	return ki->kiname;
}

static
bool _set_kernel(struct cgpu_info * const cgpu, const char *_val)
{
	struct opencl_device_data * const data = cgpu->device_data;
	
	size_t knamelen = strlen(_val);
	char filename[knamelen + 3 + 1];
	sprintf(filename, "%s.cl", _val);
	
	int dummy_srclen;
	enum cl_kernels interface;
	struct mining_algorithm *malgo;
	char *src = opencl_kernel_source(filename, &dummy_srclen, &interface, &malgo);
	if (!src)
		return false;
	free(src);
	if (!malgo)
		return false;
	
	struct opencl_kernel_info * const kernelinfo = &data->kernelinfo[malgo->algo];
	free(kernelinfo->file);
	kernelinfo->file = strdup(_val);
	
	return true;
}
_SET_INTERFACE(kernel)
const char *set_kernel(char *arg)
{
	return _set_list(arg, "Invalid value passed to set_kernel", _set_kernel);
}

static
const char *opencl_init_binary(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct opencl_device_data * const data = proc->device_data;
	char *end;
	bool nv;
	
	nv = bfg_strtobool(newvalue, &end, 0);
	if (newvalue[0] && !end[0])
		data->opt_opencl_binaries = nv ? OBU_LOADSAVE : OBU_NONE;
	else
	if (!(strcasecmp(newvalue, "load") && strcasecmp(newvalue, "read")))
		data->opt_opencl_binaries = OBU_LOAD;
	else
	if (!(strcasecmp(newvalue, "save") && strcasecmp(newvalue, "write")))
		data->opt_opencl_binaries = OBU_SAVE;
	else
	if (!(strcasecmp(newvalue, "both")))
		data->opt_opencl_binaries = OBU_LOADSAVE;
	else
	if (!(strcasecmp(newvalue, "default")))
		data->opt_opencl_binaries = OBU_DEFAULT;
	else
		return "Invalid value passed to opencl binary";
	
	return NULL;
}

static
const char *opencl_init_goffset(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct opencl_device_data * const data = proc->device_data;
	char *end;
	bool nv = bfg_strtobool(newvalue, &end, 0);
	if (newvalue[0] && !end[0])
		data->use_goffset = nv;
	else
		return "Invalid boolean value";
	return NULL;
}

#ifdef HAVE_ADL
/* This function allows us to map an adl device to an opencl device for when
 * simple enumeration has failed to match them. */
static
const char *opencl_init_gpu_map(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct opencl_device_data * const data = proc->device_data;
	data->virtual_adl = atoi(newvalue);
	data->mapped = true;
	return NULL;
}

char *set_gpu_map(char *arg)
{
	struct opencl_device_data *data;
	int val1 = 0, val2 = 0;
	char *nextptr;

	nextptr = strtok(arg, ",");
	if (nextptr == NULL)
		return "Invalid parameters for set gpu map";
	if (sscanf(arg, "%d:%d", &val1, &val2) != 2)
		return "Invalid description for map pair";
	if (val1 < 0 || val1 > MAX_GPUDEVICES || val2 < 0 || val2 > MAX_GPUDEVICES)
		return "Invalid value passed to set_gpu_map";

	data = gpus[val1].device_data;
	data->virtual_adl = val2;
	data->mapped = true;

	while ((nextptr = strtok(NULL, ",")) != NULL) {
		if (sscanf(nextptr, "%d:%d", &val1, &val2) != 2)
			return "Invalid description for map pair";
		if (val1 < 0 || val1 > MAX_GPUDEVICES || val2 < 0 || val2 > MAX_GPUDEVICES)
			return "Invalid value passed to set_gpu_map";
		data = gpus[val1].device_data;
		data->virtual_adl = val2;
		data->mapped = true;
	}

	return NULL;
}

static
bool _set_gpu_engine(struct cgpu_info * const cgpu, const char * const _val)
{
	int val1, val2;
	get_intrange(_val, &val1, &val2);
	if (val1 < 0 || val1 > 9999 || val2 < 0 || val2 > 9999 || val2 < val1)
		return false;
	
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl * const ga = &data->adl;
	
	data->min_engine = val1;
	data->gpu_engine = val2;
	ga->autoengine = (val1 != val2);
	return true;
}
_SET_INTERFACE(gpu_engine)
const char *set_gpu_engine(char *arg)
{
	return _set_list(arg, "Invalid value passed to set_gpu_engine", _set_gpu_engine);
}
static
const char *opencl_set_gpu_engine(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct opencl_device_data * const data = proc->device_data;
	struct gpu_adl * const ga = &data->adl;
	
	int val1, val2;
	get_intrange(newvalue, &val1, &val2);
	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value for clock";
	
	if (val1 == val2)
	{
		if (set_engineclock(proc->device_id, val1))
			return "Failed to set gpu_engine";
		ga->autoengine = false;
	}
	else
	{
		// Ensure current clock is within range
		if (ga->lastengine < val1)
		{
			if (set_engineclock(proc->device_id, val1))
				return "Failed to set gpu_engine";
		}
		else
		if (ga->lastengine > val2)
			if (set_engineclock(proc->device_id, val2))
				return "Failed to set gpu_engine";
		
		data->min_engine = val1;
		data->gpu_engine = val2;
		ga->autoengine = true;
	}
	
	return NULL;
}

static
bool _set_gpu_fan(struct cgpu_info * const cgpu, const char * const _val)
{
	int val1, val2;
	get_intrange(_val, &val1, &val2);
	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return false;
	
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl * const ga = &data->adl;
	
	data->min_fan = val1;
	data->gpu_fan = val2;
	ga->autofan = (val1 != val2);
	return true;
}
_SET_INTERFACE(gpu_fan)
const char *set_gpu_fan(char *arg)
{
	return _set_list(arg, "Invalid value passed to set_gpu_fan", _set_gpu_fan);
}
static
const char *opencl_set_gpu_fan(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct opencl_device_data * const data = proc->device_data;
	struct gpu_adl * const ga = &data->adl;
	
	int val1, val2;
	get_intrange(newvalue, &val1, &val2);
	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value for fan";
	
	if (val1 == val2)
	{
		if (set_fanspeed(proc->device_id, val1))
			return "Failed to set gpu_fan";
		ga->autofan = false;
	}
	else
	{
		// Ensure current fan is within range
		if (ga->targetfan < val1)
		{
			if (set_fanspeed(proc->device_id, val1))
				return "Failed to set gpu_fan";
		}
		else
		if (ga->targetfan > val2)
			if (set_fanspeed(proc->device_id, val2))
				return "Failed to set gpu_fan";
		
		data->min_fan = val1;
		data->gpu_fan = val2;
		ga->autofan = true;
	}
	
	return NULL;
}

_SET_INT_LIST(gpu_memclock , (v >=     1 && v <  9999), gpu_memclock )
static
const char *opencl_set_gpu_memclock(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	if (set_memoryclock(proc->device_id, atoi(newvalue)))
		return "Failed to set gpu_memclock";
	return NULL;
}

_SET_INT_LIST(gpu_memdiff  , (v >= -9999 && v <= 9999), gpu_memdiff  )
static
const char *opencl_set_gpu_memdiff(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct opencl_device_data * const data = proc->device_data;
	
	if (!_set_gpu_memdiff(proc, newvalue))
		return "Invalid value for gpu_memdiff";
	
	set_engineclock(proc->device_id, data->gpu_engine);
	
	return NULL;
}

_SET_INT_LIST(gpu_powertune, (v >=   -99 && v <=   99), gpu_powertune)
_SET_INT_LIST(gpu_vddc     , (v >=     0 && v <  9999), gpu_vddc     )
static
const char *opencl_set_gpu_vddc(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	if (set_vddc(proc->device_id, atof(newvalue)))
		return "Failed to set gpu_vddc";
	return NULL;
}

_SET_INT_LIST(temp_overheat, (v >=     0 && v <   200), adl.overtemp )
#endif

double oclthreads_to_xintensity(const unsigned long oclthreads, const cl_uint max_compute_units)
{
	return (double)oclthreads / (double)max_compute_units / 64.;
}

unsigned long xintensity_to_oclthreads(const double xintensity, const cl_uint max_compute_units)
{
	return xintensity * max_compute_units * 0x40;
}

static int min_intensity, max_intensity;

// NOTE: This can't be attribute-constructor because then it would race with the mining_algorithms list being populated
static
void opencl_calc_intensity_range()
{
	RUNONCE();
	
	min_intensity = INT_MAX;
	max_intensity = INT_MIN;
	struct mining_algorithm *malgo;
	LL_FOREACH(mining_algorithms, malgo)
	{
		const int malgo_min_intensity = malgo->opencl_oclthreads_to_intensity(malgo->opencl_min_oclthreads);
		const int malgo_max_intensity = malgo->opencl_oclthreads_to_intensity(malgo->opencl_max_oclthreads);
		if (malgo_min_intensity < min_intensity)
			min_intensity = malgo_min_intensity;
		if (malgo_max_intensity > max_intensity)
			max_intensity = malgo_max_intensity;
	}
}

bool opencl_set_intensity_from_str(struct cgpu_info * const cgpu, const char *_val)
{
	struct opencl_device_data * const data = cgpu->device_data;
	unsigned long oclthreads = 0;
	float intensity = intensity_not_set;
	bool dynamic = false;
	
	if (!strncasecmp(_val, "d", 1))
	{
		dynamic = true;
		++_val;
	}
	
	if (!strncasecmp(_val, "x", 1))
	{
		const double v = atof(&_val[1]);
		if (v < 1 || v > 9999)
			return false;
		
		// thr etc won't be initialised here, so avoid dereferencing it
		if (data->oclthreads)
		{
			struct thr_info * const thr = cgpu->thr[0];
			const int thr_id = thr->id;
			_clState * const clState = clStates[thr_id];
			
			oclthreads = xintensity_to_oclthreads(v, clState->max_compute_units);
		}
	}
	else
	{
		char *endptr;
		const double v = strtod(_val, &endptr);
		if (endptr == _val)
		{
			if (!dynamic)
				return false;
		}
		else
		{
			opencl_calc_intensity_range();
			if (v < min_intensity || v > max_intensity)
				return false;
			oclthreads = 1;
			intensity = v;
		}
	}
	
	// Make actual assignments after we know the values are valid
	data->dynamic = dynamic;
	if (data->oclthreads)
	{
		if (oclthreads)
		{
			data->oclthreads = oclthreads;
			data->intensity = intensity;
		}
		pause_dynamic_threads(cgpu->device_id);
	}
	else
	{
		if (unlikely(data->_init_intensity))
			free(data->_init_intensity);
		data->_init_intensity = strdup(_val);
	}
	
	return true;
}
#define _set_intensity opencl_set_intensity_from_str
_SET_INTERFACE(intensity)
const char *set_intensity(char *arg)
{
	return _set_list(arg, "Invalid value passed to intensity", _set_intensity);
}

_SET_INT_LIST2(gpu_threads, (v >= 1 && v <= 10), cgpu->threads)

void write_config_opencl(FILE * const fcfg)
{
#ifdef HAVE_ADL
	if (opt_reorder)
		fprintf(fcfg, ",\n\"gpu-reorder\" : true");
#endif
}


BFG_REGISTER_DRIVER(opencl_api)
static const struct bfg_set_device_definition opencl_set_device_funcs_probe[];
static const struct bfg_set_device_definition opencl_set_device_funcs[];

char *print_ndevs_and_exit(int *ndevs)
{
	opt_log_output = true;
	opencl_api.drv_detect();
	clear_adl(*ndevs);
	applog(LOG_INFO, "%i GPU devices max detected", *ndevs);
	exit(*ndevs);
}


struct cgpu_info gpus[MAX_GPUDEVICES]; /* Maximum number apparently possible */
struct cgpu_info *cpus;


/* In dynamic mode, only the first thread of each device will be in use.
 * This potentially could start a thread that was stopped with the start-stop
 * options if one were to disable dynamic from the menu on a paused GPU */
void pause_dynamic_threads(int gpu)
{
	struct cgpu_info *cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	int i;

	for (i = 1; i < cgpu->threads; i++) {
		struct thr_info *thr;

		thr = cgpu->thr[i];
		if (!thr->pause && data->dynamic) {
			applog(LOG_WARNING, "Disabling extra threads due to dynamic mode.");
		}

		thr->pause = data->dynamic;
		if (!data->dynamic && cgpu->deven != DEV_DISABLED)
			mt_enable(thr);
	}
}


struct device_drv opencl_api;

float opencl_proc_get_intensity(struct cgpu_info * const proc, const char ** const iunit)
{
	struct opencl_device_data * const data = proc->device_data;
	struct thr_info *thr = proc->thr[0];
	const int thr_id = thr->id;
	_clState * const clState = clStates[thr_id];
	float intensity = data->intensity;
	if (intensity == intensity_not_set)
	{
		intensity = oclthreads_to_xintensity(data->oclthreads, clState->max_compute_units);
		*iunit = data->dynamic ? "dx" : "x";
	}
	else
		*iunit = data->dynamic ? "d" : "";
	return intensity;
}

#ifdef HAVE_CURSES
static
void opencl_wlogprint_status(struct cgpu_info *cgpu)
{
	struct opencl_device_data * const data = cgpu->device_data;
	struct thr_info *thr = cgpu->thr[0];
	int i;
	char checkin[40];
	double displayed_rolling;
	bool mhash_base = !(cgpu->rolling < 1);
	char logline[255];
	strcpy(logline, ""); // In case it has no data
	
	{
		const char *iunit;
		float intensity = opencl_proc_get_intensity(cgpu, &iunit);
		tailsprintf(logline, sizeof(logline), "I:%s%g ",
		            iunit,
		            intensity);
	}
#ifdef HAVE_ADL
	if (data->has_adl) {
		int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
		float temp = 0, vddc = 0;

		if (gpu_stats(cgpu->device_id, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune)) {
			if (fanspeed != -1 || fanpercent != -1) {
				tailsprintf(logline, sizeof(logline), "F:");
				if (fanspeed > 9999)
					fanspeed = 9999;
				if (fanpercent != -1)
				{
					tailsprintf(logline, sizeof(logline), "%d%%", fanpercent);
					if (fanspeed != -1)
						tailsprintf(logline, sizeof(logline), "(%dRPM)", fanspeed);
				}
				else
					tailsprintf(logline, sizeof(logline), "%dRPM", fanspeed);
				tailsprintf(logline, sizeof(logline), " ");
			}
			if (engineclock != -1)
				tailsprintf(logline, sizeof(logline), "E:%dMHz ", engineclock);
			if (memclock != -1)
				tailsprintf(logline, sizeof(logline), "M:%dMHz ", memclock);
			if (vddc != -1)
				tailsprintf(logline, sizeof(logline), "V:%.3fV ", vddc);
			if (activity != -1)
				tailsprintf(logline, sizeof(logline), "A:%d%% ", activity);
			if (powertune != -1)
				tailsprintf(logline, sizeof(logline), "P:%d%%", powertune);
		}
	}
#endif
	
	wlogprint("%s\n", logline);
	
	wlogprint("Last initialised: %s\n", cgpu->init);
	
	for (i = 0; i < mining_threads; i++) {
		thr = get_thread(i);
		if (thr->cgpu != cgpu)
			continue;
		
		get_datestamp(checkin, sizeof(checkin), time(NULL) - timer_elapsed(&thr->last, NULL));
		displayed_rolling = thr->rolling;
		if (!mhash_base)
			displayed_rolling *= 1000;
		snprintf(logline, sizeof(logline), "Thread %d: %.1f %sh/s %s ", i, displayed_rolling, mhash_base ? "M" : "K" , cgpu->deven != DEV_DISABLED ? "Enabled" : "Disabled");
		switch (cgpu->status) {
			default:
			case LIFE_WELL:
				tailsprintf(logline, sizeof(logline), "ALIVE");
				break;
			case LIFE_SICK:
				tailsprintf(logline, sizeof(logline), "SICK reported in %s", checkin);
				break;
			case LIFE_DEAD:
				tailsprintf(logline, sizeof(logline), "DEAD reported in %s", checkin);
				break;
			case LIFE_INIT:
			case LIFE_NOSTART:
				tailsprintf(logline, sizeof(logline), "Never started");
				break;
		}
		if (thr->pause)
			tailsprintf(logline, sizeof(logline), " paused");
		wlogprint("%s\n", logline);
	}
}

static
void opencl_tui_wlogprint_choices(struct cgpu_info *cgpu)
{
	wlogprint("[I]ntensity [R]estart GPU ");
#ifdef HAVE_ADL
	struct opencl_device_data * const data = cgpu->device_data;
	if (data->has_adl)
		wlogprint("[C]hange settings ");
#endif
}

static
const char *opencl_tui_handle_choice(struct cgpu_info *cgpu, int input)
{
	switch (input)
	{
		case 'i': case 'I':
		{
			char promptbuf[0x40];
			char *intvar;

			opencl_calc_intensity_range();
			snprintf(promptbuf, sizeof(promptbuf), "Set GPU scan intensity (d or %d -> %d)", min_intensity, max_intensity);
			intvar = curses_input(promptbuf);
			if (!intvar)
				return "Invalid intensity\n";
			if (!_set_intensity(cgpu, intvar))
			{
				free(intvar);
				return "Invalid intensity (out of range)\n";
			}
			free(intvar);
			return "Intensity changed\n";
		}
		case 'r': case 'R':
			reinit_device(cgpu);
			return "Attempting to restart\n";
		case 'c': case 'C':
		{
			char logline[256];
			
			clear_logwin();
			get_statline3(logline, sizeof(logline), cgpu, true, true);
			wattron(logwin, A_BOLD);
			wlogprint("%s", logline);
			wattroff(logwin, A_BOLD);
			wlogprint("\n");
			
			change_gpusettings(cgpu->device_id);
			return "";  // Force refresh
		}
	}
	return NULL;
}

#endif


#define CL_SET_BLKARG(blkvar) status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->blkvar)
#define CL_SET_ARG(var) status |= clSetKernelArg(*kernel, num++, sizeof(var), (void *)&var)
#define CL_SET_VARG(args, var) status |= clSetKernelArg(*kernel, num++, args * sizeof(uint), (void *)var)

#ifdef USE_SHA256D
static
void *_opencl_work_data_dup(struct work * const work)
{
	struct opencl_work_data *p = malloc(sizeof(*p));
	memcpy(p, work->device_data, sizeof(*p));
	return p;
}

static
void _opencl_work_data_free(struct work * const work)
{
	free(work->device_data);
}

static
struct opencl_work_data *_opencl_work_data(struct work * const work)
{
	if (!work->device_data)
	{
		work->device_data = calloc(1, sizeof(struct opencl_work_data));
		work->device_data_dup_func = _opencl_work_data_dup;
		work->device_data_free_func = _opencl_work_data_free;
	}
	return work->device_data;
}

static
cl_int queue_poclbm_kernel(const struct opencl_kernel_info * const kinfo, _clState * const clState, struct work * const work, const cl_uint threads)
{
	struct opencl_work_data * const blk = _opencl_work_data(work);
	const cl_kernel * const kernel = &kinfo->kernel;
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

	if (!kinfo->goffset)
	{
		cl_uint vwidth = clState->vwidth;
		uint *nonces = alloca(sizeof(uint) * vwidth);
		unsigned int i;

		for (i = 0; i < vwidth; i++)
			nonces[i] = work->blk.nonce + (i * threads);
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

static
cl_int queue_phatk_kernel(const struct opencl_kernel_info * const kinfo, _clState * const clState, struct work * const work, __maybe_unused const cl_uint threads)
{
	struct opencl_work_data * const blk = _opencl_work_data(work);
	const cl_kernel * const kernel = &kinfo->kernel;
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
		nonces[i] = work->blk.nonce + i;
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

static
cl_int queue_diakgcn_kernel(const struct opencl_kernel_info * const kinfo, _clState * const clState, struct work * const work, __maybe_unused const cl_uint threads)
{
	struct opencl_work_data * const blk = _opencl_work_data(work);
	const cl_kernel * const kernel = &kinfo->kernel;
	unsigned int num = 0;
	cl_int status = 0;

	if (!kinfo->goffset) {
		cl_uint vwidth = clState->vwidth;
		uint *nonces = alloca(sizeof(uint) * vwidth);
		unsigned int i;
		for (i = 0; i < vwidth; i++)
			nonces[i] = work->blk.nonce + i;
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

static
cl_int queue_diablo_kernel(const struct opencl_kernel_info * const kinfo, _clState * const clState, struct work * const work, const cl_uint threads)
{
	struct opencl_work_data * const blk = _opencl_work_data(work);
	const cl_kernel * const kernel = &kinfo->kernel;
	unsigned int num = 0;
	cl_int status = 0;

	if (!kinfo->goffset) {
		cl_uint vwidth = clState->vwidth;
		uint *nonces = alloca(sizeof(uint) * vwidth);
		unsigned int i;

		for (i = 0; i < vwidth; i++)
			nonces[i] = work->blk.nonce + (i * threads);
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
#endif

#ifdef USE_SCRYPT
static
cl_int queue_scrypt_kernel(const struct opencl_kernel_info * const kinfo, _clState * const clState, struct work * const work, __maybe_unused const cl_uint threads)
{
	unsigned char *midstate = work->midstate;
	const cl_kernel * const kernel = &kinfo->kernel;
	unsigned int num = 0;
	cl_uint le_target;
	cl_int status = 0;
	
	if (!kinfo->goffset)
	{
		cl_uint nonce_base = work->blk.nonce;
		CL_SET_ARG(nonce_base);
	}

	le_target = *(cl_uint *)(work->target + 28);
	clState->cldata = work->data;
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

#ifdef USE_OPENCL_FULLHEADER
static
cl_int queue_fullheader_kernel(const struct opencl_kernel_info * const kinfo, _clState * const clState, struct work * const work, __maybe_unused const cl_uint threads)
{
	const struct mining_algorithm * const malgo = work_mining_algorithm(work);
	const cl_kernel * const kernel = &kinfo->kernel;
	unsigned int num = 0;
	cl_int status = 0;
	uint8_t blkheader[80];
	
	work->nonce_diff = malgo->opencl_min_nonce_diff;
	
	if (!kinfo->goffset)
	{
		cl_uint nonce_base = work->blk.nonce;
		CL_SET_ARG(nonce_base);
	}
	
	swap32yes(blkheader, work->data, 80/4);
	status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, CL_TRUE, 0, sizeof(blkheader), blkheader, 0, NULL, NULL);
	
	CL_SET_ARG(clState->CLbuffer0);
	CL_SET_ARG(clState->outputBuffer);
	
	return status;
}
#endif


static
struct opencl_kernel_interface kernel_interfaces[] = {
	{NULL},
#ifdef USE_SHA256D
	{"poclbm",  queue_poclbm_kernel },
	{"phatk",   queue_phatk_kernel  },
	{"diakgcn", queue_diakgcn_kernel},
	{"diablo",  queue_diablo_kernel },
#endif
#ifdef USE_OPENCL_FULLHEADER
	{"fullheader", queue_fullheader_kernel },
#endif
#ifdef USE_SCRYPT
	{"scrypt",  queue_scrypt_kernel },
#endif
};


/* We have only one thread that ever re-initialises GPUs, thus if any GPU
 * init command fails due to a completely wedged GPU, the thread will never
 * return, unable to harm other GPUs. If it does return, it means we only had
 * a soft failure and then the reinit_gpu thread is ready to tackle another
 * GPU */
void *reinit_gpu(void *userdata)
{
	struct thr_info *mythr = userdata;
	struct cgpu_info *cgpu, *sel_cgpu;
	struct thr_info *thr;
	char name[256];
	int thr_id;
	int i;

	pthread_detach(pthread_self());
	RenameThread("reinit_gpu");

select_cgpu:
	sel_cgpu =
	cgpu = tq_pop(mythr->q);
	if (!cgpu)
		goto out;
	
	struct opencl_device_data * const data = cgpu->device_data;

	if (clDevicesNum() != nDevs) {
		applog(LOG_WARNING, "Hardware not reporting same number of active devices, will not attempt to restart GPU");
		goto out;
	}

	for (i = 0; i < cgpu->threads; ++i)
	{
		thr = cgpu->thr[i];
		thr_id = thr->id;

		thr->rolling = thr->cgpu->rolling = 0;
		/* Reports the last time we tried to revive a sick GPU */
		cgtime(&thr->sick);
		if (!pthread_cancel(thr->pth)) {
			applog(LOG_WARNING, "Thread %d still exists, killing it off", thr_id);
		} else
			applog(LOG_WARNING, "Thread %d no longer exists", thr_id);
	}

	for (i = 0; i < cgpu->threads; ++i)
	{
		int virtual_gpu;

		thr = cgpu->thr[i];
		thr_id = thr->id;

		virtual_gpu = data->virtual_gpu;
		/* Lose this ram cause we may get stuck here! */
		//tq_freeze(thr->q);

		thr->q = tq_new();
		if (!thr->q)
			quithere(1, "Failed to tq_new");

		/* Lose this ram cause we may dereference in the dying thread! */
		//free(clState);

		applog(LOG_INFO, "Reinit GPU thread %d", thr_id);
		clStates[thr_id] = opencl_create_clState(virtual_gpu, name, sizeof(name));
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

	get_now_datestamp(sel_cgpu->init, sizeof(sel_cgpu->init));

	proc_enable(cgpu);

	goto select_cgpu;
out:
	return NULL;
}

struct device_drv opencl_api;

static int opencl_autodetect()
{
	RUNONCE(0);
	
#ifndef WIN32
	if (!getenv("DISPLAY")) {
		applog(LOG_DEBUG, "DISPLAY not set, setting :0 just in case");
		setenv("DISPLAY", ":0", 1);
	}
#endif

	if (!load_opencl_symbols()) {
		nDevs = 0;
		return 0;
	}


	int i;

	nDevs = clDevicesNum();
	if (nDevs < 0) {
		applog(LOG_ERR, "clDevicesNum returned error, no GPUs usable");
		nDevs = 0;
	}

	if (!nDevs)
		return 0;

	if (opt_g_threads == -1) {
		// NOTE: This should ideally default to 2 for non-scrypt
		opt_g_threads = 1;
	}

#ifdef HAVE_SENSORS
	const sensors_chip_name *cn;
	int c = 0;
	
	sensors_init(NULL);
	sensors_chip_name cnm;
	if (sensors_parse_chip_name("radeon-*", &cnm))
		c = -1;
#endif

	for (i = 0; i < nDevs; ++i) {
		struct cgpu_info *cgpu;

		cgpu = &gpus[i];
		struct opencl_device_data * const data = cgpu->device_data;
		
		cgpu->deven = DEV_ENABLED;
		cgpu->drv = &opencl_api;
		cgpu->device_id = i;
		if (cgpu->threads == 0)
			cgpu->threads = opt_g_threads;
		data->virtual_gpu = i;
		
#ifdef HAVE_SENSORS
		cn = (c == -1) ? NULL : sensors_get_detected_chips(&cnm, &c);
		data->sensor = cn;
#endif
		
		cgpu->set_device_funcs = opencl_set_device_funcs_probe;
		cgpu_set_defaults(cgpu);
		cgpu->set_device_funcs = opencl_set_device_funcs;
		
		add_cgpu(cgpu);
	}

	if (!opt_noadl)
		init_adl(nDevs);
	
	return nDevs;
}

static void opencl_detect()
{
	int flags = GDF_DEFAULT_NOAUTO;
	struct mining_goal_info *goal, *tmpgoal;
	HASH_ITER(hh, mining_goals, goal, tmpgoal)
	{
		if (!goal->malgo->opencl_nodefault)
			flags &= ~GDF_DEFAULT_NOAUTO;
	}
	generic_detect(&opencl_api, NULL, opencl_autodetect, flags);
}

static void reinit_opencl_device(struct cgpu_info *gpu)
{
#ifdef HAVE_ADL
	struct opencl_device_data * const data = gpu->device_data;
	if (adl_active && data->has_adl && gpu_activity(gpu->device_id) > 50)
	{
		applogr(, LOG_ERR, "%s: Still showing activity (suggests a hard hang); cancelling reinitialise.",
		        gpu->dev_repr);
	}
#endif
	
	tq_push(control_thr[gpur_thr_id].q, gpu);
}

static
bool opencl_get_stats(struct cgpu_info * const gpu)
{
	__maybe_unused struct opencl_device_data * const data = gpu->device_data;
#ifdef HAVE_SENSORS
	if (data->sensor)
	{
		const sensors_chip_name *cn = data->sensor;
		const sensors_feature *feat;
		for (int f = 0; (feat = sensors_get_features(cn, &f)); )
		{
			const sensors_subfeature *subf;
			subf = sensors_get_subfeature(cn, feat, SENSORS_SUBFEATURE_TEMP_INPUT);
			if (!(subf && subf->flags & SENSORS_MODE_R))
				continue;
			
			double val;
			int rc = sensors_get_value(cn, subf->number, &val);
			if (rc)
				continue;
			
			gpu->temp = val;
			return true;
		}
	}
#endif
#ifdef HAVE_ADL
	if (data->has_adl) {
		int gpuid = gpu->device_id;
		gpu_temp(gpuid);
		gpu_fanspeed(gpuid);
	}
#endif
	return true;
}

static
void opencl_watchdog(struct cgpu_info * const cgpu, __maybe_unused const struct timeval * const tv_now)
{
#ifdef HAVE_ADL
	struct opencl_device_data * const data = cgpu->device_data;
	const int gpu = cgpu->device_id;
	enum dev_enable *denable = &cgpu->deven;
	
	if (adl_active && data->has_adl)
		gpu_autotune(gpu, denable);
	if (opt_debug && data->has_adl) {
		int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
		float temp = 0, vddc = 0;

		if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune))
			applog(LOG_DEBUG, "%.1f C  F: %d%%(%dRPM)  E: %dMHz  M: %dMHz  V: %.3fV  A: %d%%  P: %d%%",
			temp, fanpercent, fanspeed, engineclock, memclock, vddc, activity, powertune);
	}
#endif
}

static struct api_data*
get_opencl_api_extra_device_status(struct cgpu_info *gpu)
{
	struct opencl_device_data * const data = gpu->device_data;
	struct thr_info * const thr = gpu->thr[0];
	const int thr_id = thr->id;
	_clState * const clState = clStates[thr_id];
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
	uint32_t oclthreads;
	double intensityf = data->intensity;
	// FIXME: Some way to express intensities malgo-neutral?
	struct mining_goal_info * const goal = get_mining_goal("default");
	struct mining_algorithm * const malgo = goal->malgo;
	if (data->intensity == intensity_not_set)
	{
		oclthreads = data->oclthreads;
		intensityf = malgo->opencl_oclthreads_to_intensity(oclthreads);
	}
	else
		oclthreads = malgo->opencl_intensity_to_oclthreads(intensityf);
	double xintensity = oclthreads_to_xintensity(oclthreads, clState->max_compute_units);
	if (data->dynamic)
		strcpy(intensity, "D");
	else
		sprintf(intensity, "%g", intensityf);
	root = api_add_string(root, "Intensity", intensity, true);
	root = api_add_uint32(root, "OCLThreads", &oclthreads, true);
	root = api_add_double(root, "CIntensity", &intensityf, true);
	root = api_add_double(root, "XIntensity", &xintensity, true);

	return root;
}

struct opencl_thread_data {
	uint32_t *res;
};

static uint32_t *blank_res;

static bool opencl_thread_prepare(struct thr_info *thr)
{
	char name[256];
	struct cgpu_info *cgpu = thr->cgpu;
	struct opencl_device_data * const data = cgpu->device_data;
	int gpu = cgpu->device_id;
	int virtual_gpu = data->virtual_gpu;
	int i = thr->id;
	static bool failmessage = false;
	int buffersize = OPENCL_MAX_BUFFERSIZE;

	if (!blank_res)
		blank_res = calloc(buffersize, 1);
	if (!blank_res) {
		applog(LOG_ERR, "Failed to calloc in opencl_thread_init");
		return false;
	}

	strcpy(name, "");
	applog(LOG_INFO, "Init GPU thread %i GPU %i virtual GPU %i", i, gpu, virtual_gpu);
	clStates[i] = opencl_create_clState(virtual_gpu, name, sizeof(name));
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

		dev_error(cgpu, REASON_DEV_NOSTART);

		return false;
	}
	if (!cgpu->name)
		cgpu->name = trimmed_strdup(name);
	applog(LOG_INFO, "initCl() finished. Found %s", name);
	get_now_datestamp(cgpu->init, sizeof(cgpu->init));

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
	int buffersize = OPENCL_MAX_BUFFERSIZE;

	if (!thrdata) {
		applog(LOG_ERR, "Failed to calloc in opencl_thread_init");
		return false;
	}

	thrdata->res = calloc(buffersize, 1);

	if (!thrdata->res) {
		free(thrdata);
		applog(LOG_ERR, "Failed to calloc in opencl_thread_init");
		return false;
	}

	status |= clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0,
				       buffersize, blank_res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed.");
		return false;
	}

	gpu->status = LIFE_WELL;

	gpu->device_last_well = time(NULL);

	return true;
}

static
float opencl_min_nonce_diff(struct cgpu_info * const proc, const struct mining_algorithm * const malgo)
{
	return malgo->opencl_min_nonce_diff ?: -1.;
}

#ifdef USE_SHA256D
static bool opencl_prepare_work(struct thr_info __maybe_unused *thr, struct work *work)
{
	const struct mining_algorithm * const malgo = work_mining_algorithm(work);
	if (malgo->algo == POW_SHA256D)
	{
		struct opencl_work_data * const blk = _opencl_work_data(work);
		precalc_hash(blk, (uint32_t *)(work->midstate), (uint32_t *)(work->data + 64));
	}
	return true;
}
#endif

extern int opt_dynamic_interval;

const struct opencl_kernel_info *opencl_scanhash_get_kernel(struct cgpu_info * const cgpu, _clState * const clState, const struct mining_algorithm * const malgo)
{
	struct opencl_device_data * const data = cgpu->device_data;
	struct opencl_kernel_info *kernelinfo = NULL;
	kernelinfo = &data->kernelinfo[malgo->algo];
	if (!kernelinfo->file)
	{
		kernelinfo->file = malgo->opencl_get_default_kernel_file(malgo, cgpu, clState);
		if (!kernelinfo->file)
			applogr(NULL, LOG_ERR, "%s: Unsupported mining algorithm", cgpu->dev_repr);
	}
	if (!kernelinfo->loaded)
	{
		if (!opencl_load_kernel(cgpu, clState, cgpu->name, kernelinfo, kernelinfo->file, malgo))
			applogr(NULL, LOG_ERR, "%s: Failed to load kernel", cgpu->dev_repr);
		
		kernelinfo->queue_kernel_parameters = kernel_interfaces[kernelinfo->interface].queue_kernel_parameters_func;
	}
	return kernelinfo;
}

static int64_t opencl_scanhash(struct thr_info *thr, struct work *work,
				int64_t __maybe_unused max_nonce)
{
	const int thr_id = thr->id;
	struct opencl_thread_data *thrdata = thr->cgpu_data;
	struct cgpu_info *gpu = thr->cgpu;
	struct opencl_device_data * const data = gpu->device_data;
	_clState *clState = clStates[thr_id];
	const struct mining_algorithm * const malgo = work_mining_algorithm(work);
	const struct opencl_kernel_info *kinfo = opencl_scanhash_get_kernel(gpu, clState, malgo);
	if (!kinfo)
		return -1;
	const cl_kernel * const kernel = &kinfo->kernel;
	const int dynamic_us = opt_dynamic_interval * 1000;

	cl_int status;
	size_t globalThreads[1];
	size_t localThreads[1] = { kinfo->wsize };
	int64_t hashes;
	int found = FOUND;
	int buffersize = BUFFERSIZE;
#ifdef USE_SCRYPT
	if (malgo->algo == POW_SCRYPT)
	{
		found = SCRYPT_FOUND;
		buffersize = SCRYPT_BUFFERSIZE;
	}
#endif
	if (data->intensity != intensity_not_set)
		data->oclthreads = malgo->opencl_intensity_to_oclthreads(data->intensity);

	/* Windows' timer resolution is only 15ms so oversample 5x */
	if (data->dynamic && (++data->intervals * dynamic_us) > 70000) {
		struct timeval tv_gpuend;
		double gpu_us;

		cgtime(&tv_gpuend);
		gpu_us = us_tdiff(&tv_gpuend, &data->tv_gpustart) / data->intervals;
		if (gpu_us > dynamic_us) {
			const unsigned long min_oclthreads = malgo->opencl_min_oclthreads;
			data->oclthreads /= 2;
			if (data->oclthreads < min_oclthreads)
				data->oclthreads = min_oclthreads;
		} else if (gpu_us < dynamic_us / 2) {
			const unsigned long max_oclthreads = malgo->opencl_max_oclthreads;
			data->oclthreads *= 2;
			if (data->oclthreads > max_oclthreads)
				data->oclthreads = max_oclthreads;
		}
		if (data->intensity != intensity_not_set)
			data->intensity = malgo->opencl_oclthreads_to_intensity(data->oclthreads);
		memcpy(&(data->tv_gpustart), &tv_gpuend, sizeof(struct timeval));
		data->intervals = 0;
	}

	if (data->oclthreads < localThreads[0])
		data->oclthreads = localThreads[0];
	globalThreads[0] = data->oclthreads;
	hashes = globalThreads[0];
	hashes *= clState->vwidth;
	
	if (hashes > gpu->max_hashes)
		gpu->max_hashes = hashes;

	status = kinfo->queue_kernel_parameters(kinfo, clState, work, globalThreads[0]);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: clSetKernelArg of all params failed.");
		return -1;
	}

	if (kinfo->goffset)
	{
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
				     buffersize, thrdata->res, 0, NULL, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: clEnqueueReadBuffer failed error %d. (clEnqueueReadBuffer)", status);
		return -1;
	}

	/* The amount of work scanned can fluctuate when intensity changes
	 * and since we do this one cycle behind, we increment the work more
	 * than enough to prevent repeating work */
	work->blk.nonce += gpu->max_hashes;

	/* This finish flushes the readbuffer set with CL_FALSE in clEnqueueReadBuffer */
	clFinish(clState->commandQueue);

	/* FOUND entry is used as a counter to say how many nonces exist */
	if (thrdata->res[found]) {
		/* Clear the buffer again */
		status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0,
					      buffersize, blank_res, 0, NULL, NULL);
		if (unlikely(status != CL_SUCCESS)) {
			applog(LOG_ERR, "Error: clEnqueueWriteBuffer failed.");
			return -1;
		}
		applog(LOG_DEBUG, "GPU %d found something?", gpu->device_id);
		postcalc_hash_async(thr, work, thrdata->res, kinfo->interface);
		memset(thrdata->res, 0, buffersize);
		/* This finish flushes the writebuffer set with CL_FALSE in clEnqueueWriteBuffer */
		clFinish(clState->commandQueue);
	}

	return hashes;
}

static
void opencl_clean_kernel_info(struct opencl_kernel_info * const kinfo)
{
	clReleaseKernel(kinfo->kernel);
	clReleaseProgram(kinfo->program);
}

static void opencl_thread_shutdown(struct thr_info *thr)
{
	struct cgpu_info * const cgpu = thr->cgpu;
	struct opencl_device_data * const data = cgpu->device_data;
	const int thr_id = thr->id;
	_clState *clState = clStates[thr_id];

	for (unsigned i = 0; i < (unsigned)POW_ALGORITHM_COUNT; ++i)
	{
		opencl_clean_kernel_info(&data->kernelinfo[i]);
	}
	clReleaseCommandQueue(clState->commandQueue);
	clReleaseContext(clState->context);
}

static
const char *opencl_cannot_set(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	return "Cannot set at runtime (use --set-device on commandline)";
}

static const struct bfg_set_device_definition opencl_set_device_funcs_probe[] = {
	{"intensity", opencl_init_intensity},
	{"kernel", opencl_init_kernel},
	{"threads", opencl_init_gpu_threads},
	{"vector", opencl_init_vector},
	{"work_size", opencl_init_worksize},
	{"binary", opencl_init_binary},
	{"goffset", opencl_init_goffset},
#ifdef HAVE_ADL
	{"adl_mapping", opencl_init_gpu_map},
	{"clock", opencl_init_gpu_engine},
	{"fan", opencl_init_gpu_fan},
	{"memclock", opencl_init_gpu_memclock},
	{"memdiff", opencl_init_gpu_memdiff},
	{"powertune", opencl_init_gpu_powertune},
	{"temp_overheat", opencl_init_temp_overheat},
	{"voltage", opencl_init_gpu_vddc},
#endif
#ifdef USE_SCRYPT
	{"shaders", opencl_init_shaders},
	{"lookup_gap", opencl_init_lookup_gap},
	{"thread_concurrency", opencl_init_thread_concurrency},
#endif
	{NULL}
};

static const struct bfg_set_device_definition opencl_set_device_funcs[] = {
	{"intensity", opencl_init_intensity, "Intensity of GPU scanning (d, -10 -> 31, or x1 to x9999)"},
	{"kernel", opencl_cannot_set, "Mining kernel code to use"},
	{"threads", opencl_cannot_set, "Number of threads"},
	{"vector", opencl_cannot_set, ""},
	{"work_size", opencl_cannot_set, ""},
	{"binary", opencl_cannot_set, ""},
	{"goffset", opencl_cannot_set, ""},
#ifdef HAVE_ADL
	{"adl_mapping", opencl_cannot_set, "Map to ADL device"},
	{"clock", opencl_set_gpu_engine, "GPU engine clock"},
	{"fan", opencl_set_gpu_fan, "GPU fan percentage range"},
	{"memclock", opencl_set_gpu_memclock, "GPU memory clock"},
	{"memdiff", opencl_set_gpu_memdiff, "Clock speed difference between GPU and memory (auto-gpu mode only)"},
	{"powertune", opencl_cannot_set, "GPU powertune percentage"},
	{"temp_overheat", opencl_init_temp_overheat, "Overheat temperature when automatically managing fan and GPU speeds"},
	{"voltage", opencl_set_gpu_vddc, "GPU voltage"},
#endif
#ifdef USE_SCRYPT
	{"shaders", opencl_cannot_set, "GPU shaders per card (scrypt only)"},
	{"lookup_gap", opencl_cannot_set, "GPU lookup gap (scrypt only)"},
	{"thread_concurrency", opencl_cannot_set, "GPU thread concurrency (scrypt only)"},
#endif
	{NULL}
};

struct device_drv opencl_api = {
	.dname = "opencl",
	.name = "OCL",
	.probe_priority = 110,
	.drv_min_nonce_diff = opencl_min_nonce_diff,
	.drv_detect = opencl_detect,
	.reinit_device = reinit_opencl_device,
	.watchdog = opencl_watchdog,
	.get_stats = opencl_get_stats,
#ifdef HAVE_CURSES
	.proc_wlogprint_status = opencl_wlogprint_status,
	.proc_tui_wlogprint_choices = opencl_tui_wlogprint_choices,
	.proc_tui_handle_choice = opencl_tui_handle_choice,
#endif
	.get_api_extra_device_status = get_opencl_api_extra_device_status,
	.thread_prepare = opencl_thread_prepare,
	.thread_init = opencl_thread_init,
#ifdef USE_SHA256D
	.prepare_work = opencl_prepare_work,
#endif
	.scanhash = opencl_scanhash,
	.thread_shutdown = opencl_thread_shutdown,
};
