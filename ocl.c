/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"
#ifdef HAVE_OPENCL

#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define OMIT_OPENCL_API

#include "deviceapi.h"
#include "driver-opencl.h"
#include "findnonce.h"
#include "logging.h"
#include "miner.h"
#include "ocl.h"
#include "sha2.h"
#include "util.h"

/* Platform API */
extern
CL_API_ENTRY cl_int CL_API_CALL
(*clGetPlatformIDs)(cl_uint          /* num_entries */,
                 cl_platform_id * /* platforms */,
                 cl_uint *        /* num_platforms */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clGetPlatformInfo)(cl_platform_id   /* platform */,
                  cl_platform_info /* param_name */,
                  size_t           /* param_value_size */,
                  void *           /* param_value */,
                  size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Device APIs */
extern
CL_API_ENTRY cl_int CL_API_CALL
(*clGetDeviceIDs)(cl_platform_id   /* platform */,
               cl_device_type   /* device_type */,
               cl_uint          /* num_entries */,
               cl_device_id *   /* devices */,
               cl_uint *        /* num_devices */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clGetDeviceInfo)(cl_device_id    /* device */,
                cl_device_info  /* param_name */,
                size_t          /* param_value_size */,
                void *          /* param_value */,
                size_t *        /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Context APIs  */
extern
CL_API_ENTRY cl_context CL_API_CALL
(*clCreateContextFromType)(const cl_context_properties * /* properties */,
                        cl_device_type          /* device_type */,
                        void (CL_CALLBACK *     /* pfn_notify*/ )(const char *, const void *, size_t, void *),
                        void *                  /* user_data */,
                        cl_int *                /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseContext)(cl_context /* context */) CL_API_SUFFIX__VERSION_1_0;

/* Command Queue APIs */
extern
CL_API_ENTRY cl_command_queue CL_API_CALL
(*clCreateCommandQueue)(cl_context                     /* context */,
                     cl_device_id                   /* device */,
                     cl_command_queue_properties    /* properties */,
                     cl_int *                       /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseCommandQueue)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

/* Memory Object APIs */
extern
CL_API_ENTRY cl_mem CL_API_CALL
(*clCreateBuffer)(cl_context   /* context */,
               cl_mem_flags /* flags */,
               size_t       /* size */,
               void *       /* host_ptr */,
               cl_int *     /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Program Object APIs  */
extern
CL_API_ENTRY cl_program CL_API_CALL
(*clCreateProgramWithSource)(cl_context        /* context */,
                          cl_uint           /* count */,
                          const char **     /* strings */,
                          const size_t *    /* lengths */,
                          cl_int *          /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_program CL_API_CALL
(*clCreateProgramWithBinary)(cl_context                     /* context */,
                          cl_uint                        /* num_devices */,
                          const cl_device_id *           /* device_list */,
                          const size_t *                 /* lengths */,
                          const unsigned char **         /* binaries */,
                          cl_int *                       /* binary_status */,
                          cl_int *                       /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseProgram)(cl_program /* program */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clBuildProgram)(cl_program           /* program */,
               cl_uint              /* num_devices */,
               const cl_device_id * /* device_list */,
               const char *         /* options */,
               void (CL_CALLBACK *  /* pfn_notify */)(cl_program /* program */, void * /* user_data */),
               void *               /* user_data */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clGetProgramInfo)(cl_program         /* program */,
                 cl_program_info    /* param_name */,
                 size_t             /* param_value_size */,
                 void *             /* param_value */,
                 size_t *           /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clGetProgramBuildInfo)(cl_program            /* program */,
                      cl_device_id          /* device */,
                      cl_program_build_info /* param_name */,
                      size_t                /* param_value_size */,
                      void *                /* param_value */,
                      size_t *              /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Kernel Object APIs */
extern
CL_API_ENTRY cl_kernel CL_API_CALL
(*clCreateKernel)(cl_program      /* program */,
               const char *    /* kernel_name */,
               cl_int *        /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clReleaseKernel)(cl_kernel   /* kernel */) CL_API_SUFFIX__VERSION_1_0;

extern
CL_API_ENTRY cl_int CL_API_CALL
(*clSetKernelArg)(cl_kernel    /* kernel */,
               cl_uint      /* arg_index */,
               size_t       /* arg_size */,
               const void * /* arg_value */) CL_API_SUFFIX__VERSION_1_0;

/* Flush and Finish APIs */
extern
CL_API_ENTRY cl_int CL_API_CALL
(*clFinish)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

/* Enqueued Commands APIs */
extern
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

extern
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

extern
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

int opt_platform_id = -1;

FILE *opencl_open_kernel(const char * const filename)
{
	char *fullpath = alloca(PATH_MAX);
	FILE *f;

	/* Try in the optional kernel path or installed prefix first */
	f = open_bitstream("opencl", filename);
	if (!f) {
		/* Then try from the path BFGMiner was called */
		strcpy(fullpath, cgminer_path);
		strcat(fullpath, filename);
		f = fopen(fullpath, "rb");
	}
	/* Finally try opening it directly */
	if (!f)
		f = fopen(filename, "rb");
	
	return f;
}

char *file_contents(const char *filename, int *length)
{
	void *buffer;
	FILE *f;

	f = opencl_open_kernel(filename);

	if (!f) {
		applog(LOG_ERR, "Unable to open %s for reading", filename);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	*length = ftell(f);
	fseek(f, 0, SEEK_SET);

	buffer = malloc(*length+1);
	*length = fread(buffer, 1, *length, f);
	fclose(f);
	((char*)buffer)[*length] = '\0';

	return (char*)buffer;
}

extern int opt_g_threads;

int clDevicesNum(void) {
	cl_int status;
	char pbuff[256];
	cl_uint numDevices;
	cl_uint numPlatforms;
	int most_devices = -1;
	cl_platform_id *platforms;
	cl_platform_id platform = NULL;
	unsigned int i, mdplatform = 0;
	bool mdmesa = false;

	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	/* If this fails, assume no GPUs. */
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: clGetPlatformsIDs failed (no OpenCL SDK installed?)", status);
		return -1;
	}

	if (numPlatforms == 0) {
		applog(LOG_ERR, "clGetPlatformsIDs returned no platforms (no OpenCL SDK installed?)");
		return -1;
	}

	platforms = (cl_platform_id *)alloca(numPlatforms*sizeof(cl_platform_id));
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platform Ids. (clGetPlatformsIDs)", status);
		return -1;
	}

	for (i = 0; i < numPlatforms; i++) {
		if (opt_platform_id >= 0 && (int)i != opt_platform_id)
			continue;

		status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: Getting Platform Info. (clGetPlatformInfo)", status);
			return -1;
		}
		platform = platforms[i];
		applog(LOG_INFO, "CL Platform %d vendor: %s", i, pbuff);
		status = clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(pbuff), pbuff, NULL);
		if (status == CL_SUCCESS)
			applog(LOG_INFO, "CL Platform %d name: %s", i, pbuff);
		status = clGetPlatformInfo(platform, CL_PLATFORM_VERSION, sizeof(pbuff), pbuff, NULL);
		if (status == CL_SUCCESS)
			applog(LOG_INFO, "CL Platform %d version: %s", i, pbuff);
		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
		if (status != CL_SUCCESS) {
			applog(LOG_INFO, "Error %d: Getting Device IDs (num)", status);
			continue;
		}
		applog(LOG_INFO, "Platform %d devices: %d", i, numDevices);
		if ((int)numDevices > most_devices) {
			most_devices = numDevices;
			mdplatform = i;
			mdmesa = strstr(pbuff, "MESA");
		}
		if (numDevices) {
			unsigned int j;
			cl_device_id *devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

			clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
			for (j = 0; j < numDevices; j++) {
				clGetDeviceInfo(devices[j], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
				applog(LOG_INFO, "\t%i\t%s", j, pbuff);
			}
			free(devices);
		}
	}

	if (opt_platform_id < 0)
		opt_platform_id = mdplatform;
	if (mdmesa && opt_g_threads == -1)
		opt_g_threads = 1;

	return most_devices;
}

cl_int bfg_clBuildProgram(_clState * const clState, const cl_device_id devid, const char * const CompilerOptions)
{
	cl_int status;
	
	status = clBuildProgram(clState->program, 1, &devid, CompilerOptions, NULL, NULL);
	
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error %d: Building Program (clBuildProgram)", status);
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devid, CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);
		
		char *log = malloc(logSize ?: 1);
		status = clGetProgramBuildInfo(clState->program, devid, CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		if (logSize > 0 && log[0])
			applog(LOG_ERR, "%s", log);
		free(log);
	}
	
	return status;
}

static int advance(char **area, unsigned *remaining, const char *marker)
{
	char *find = memmem(*area, *remaining, marker, strlen(marker));

	if (!find) {
		applog(LOG_DEBUG, "Marker \"%s\" not found", marker);
		return 0;
	}
	*remaining -= find - *area;
	*area = find;
	return 1;
}

#define OP3_INST_BFE_UINT	4ULL
#define OP3_INST_BFE_INT	5ULL
#define OP3_INST_BFI_INT	6ULL
#define OP3_INST_BIT_ALIGN_INT	12ULL
#define OP3_INST_BYTE_ALIGN_INT	13ULL

void patch_opcodes(char *w, unsigned remaining)
{
	uint64_t *opcode = (uint64_t *)w;
	int patched = 0;
	int count_bfe_int = 0;
	int count_bfe_uint = 0;
	int count_byte_align = 0;
	while (42) {
		int clamp = (*opcode >> (32 + 31)) & 0x1;
		int dest_rel = (*opcode >> (32 + 28)) & 0x1;
		int alu_inst = (*opcode >> (32 + 13)) & 0x1f;
		int s2_neg = (*opcode >> (32 + 12)) & 0x1;
		int s2_rel = (*opcode >> (32 + 9)) & 0x1;
		int pred_sel = (*opcode >> 29) & 0x3;
		if (!clamp && !dest_rel && !s2_neg && !s2_rel && !pred_sel) {
			if (alu_inst == OP3_INST_BFE_INT) {
				count_bfe_int++;
			} else if (alu_inst == OP3_INST_BFE_UINT) {
				count_bfe_uint++;
			} else if (alu_inst == OP3_INST_BYTE_ALIGN_INT) {
				count_byte_align++;
				// patch this instruction to BFI_INT
				*opcode &= 0xfffc1fffffffffffULL;
				*opcode |= OP3_INST_BFI_INT << (32 + 13);
				patched++;
			}
		}
		if (remaining <= 8)
			break;
		opcode++;
		remaining -= 8;
	}
	applog(LOG_DEBUG, "Potential OP3 instructions identified: "
		"%i BFE_INT, %i BFE_UINT, %i BYTE_ALIGN",
		count_bfe_int, count_bfe_uint, count_byte_align);
	applog(LOG_DEBUG, "Patched a total of %i BFI_INT instructions", patched);
}

_clState *initCl(unsigned int gpu, char *name, size_t nameSize)
{
	_clState *clState = calloc(1, sizeof(_clState));
	bool patchbfi = false, prog_built = false;
	bool ismesa = false;
	struct cgpu_info *cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	cl_platform_id platform = NULL;
	char pbuff[256], vbuff[255];
	char *s, *q;
	cl_platform_id* platforms;
	cl_uint preferred_vwidth;
	cl_device_id *devices;
	cl_uint numPlatforms;
	cl_uint numDevices;
	cl_int status;

	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platforms. (clGetPlatformsIDs)", status);
		return NULL;
	}

	platforms = (cl_platform_id *)alloca(numPlatforms*sizeof(cl_platform_id));
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platform Ids. (clGetPlatformsIDs)", status);
		return NULL;
	}

	if (opt_platform_id >= (int)numPlatforms) {
		applog(LOG_ERR, "Specified platform that does not exist");
		return NULL;
	}

	status = clGetPlatformInfo(platforms[opt_platform_id], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platform Info. (clGetPlatformInfo)", status);
		return NULL;
	}
	platform = platforms[opt_platform_id];

	if (platform == NULL) {
		perror("NULL platform found!\n");
		return NULL;
	}

	applog(LOG_INFO, "CL Platform vendor: %s", pbuff);
	status = clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(pbuff), pbuff, NULL);
	if (status == CL_SUCCESS)
		applog(LOG_INFO, "CL Platform name: %s", pbuff);
	status = clGetPlatformInfo(platform, CL_PLATFORM_VERSION, sizeof(vbuff), vbuff, NULL);
	if (status == CL_SUCCESS)
		applog(LOG_INFO, "CL Platform version: %s", vbuff);

	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Device IDs (num)", status);
		return NULL;
	}

	if (numDevices > 0 ) {
		devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

		/* Now, get the device list data */

		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: Getting Device IDs (list)", status);
			return NULL;
		}

		applog(LOG_INFO, "List of devices:");

		unsigned int i;
		for (i = 0; i < numDevices; i++) {
			status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS) {
				applog(LOG_ERR, "Error %d: Getting Device Info", status);
				return NULL;
			}

			applog(LOG_INFO, "\t%i\t%s", i, pbuff);
		}

		if (gpu < numDevices) {
			status = clGetDeviceInfo(devices[gpu], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS) {
				applog(LOG_ERR, "Error %d: Getting Device Info", status);
				return NULL;
			}

			applog(LOG_INFO, "Selected %i: %s", gpu, pbuff);
			strncpy(name, pbuff, nameSize);
		} else {
			applog(LOG_ERR, "Invalid GPU %i", gpu);
			return NULL;
		}

	} else return NULL;

	cl_context_properties cps[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };

	clState->context = clCreateContextFromType(cps, CL_DEVICE_TYPE_GPU, NULL, NULL, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Creating Context. (clCreateContextFromType)", status);
		return NULL;
	}

	/////////////////////////////////////////////////////////////////
	// Create an OpenCL command queue
	/////////////////////////////////////////////////////////////////
	clState->commandQueue = clCreateCommandQueue(clState->context, devices[gpu],
						     CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &status);
	if (status != CL_SUCCESS) /* Try again without OOE enable */
		clState->commandQueue = clCreateCommandQueue(clState->context, devices[gpu], 0 , &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Creating Command Queue. (clCreateCommandQueue)", status);
		return NULL;
	}

	/* Check for BFI INT support. Hopefully people don't mix devices with
	 * and without it! */
	char * extensions = malloc(1024);
	const char * camo = "cl_amd_media_ops";
	char *find;

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_EXTENSIONS, 1024, (void *)extensions, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_EXTENSIONS", status);
		return NULL;
	}
	find = strstr(extensions, camo);
	if (find)
		clState->hasBitAlign = true;
	free(extensions);

	/* Check for OpenCL >= 1.0 support, needed for global offset parameter usage. */
	char * devoclver = malloc(1024);
	const char * ocl10 = "OpenCL 1.0";

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_VERSION, 1024, (void *)devoclver, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_VERSION", status);
		return NULL;
	}
	find = strstr(devoclver, ocl10);
	if (!find)
		clState->hasOpenCL11plus = true;
	free(devoclver);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT, sizeof(cl_uint), (void *)&preferred_vwidth, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT", status);
		return NULL;
	}
	applog(LOG_DEBUG, "Preferred vector width reported %d", preferred_vwidth);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), (void *)&clState->max_work_size, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_WORK_GROUP_SIZE", status);
		return NULL;
	}
	applog(LOG_DEBUG, "Max work group size reported %"PRId64, (int64_t)clState->max_work_size);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(clState->max_compute_units), (void *)&clState->max_compute_units, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_COMPUTE_UNITS", status);
		return NULL;
	}
	if (data->_init_intensity)
	{
		data->oclthreads = 1;  // Needed to ensure we don't just try to re-save the string (which would free before strduping and segfault anyway)
		opencl_set_intensity_from_str(cgpu, data->_init_intensity);
	}
	else
		data->oclthreads = intensity_to_oclthreads(MIN_INTENSITY, !opt_scrypt);
	applog(LOG_DEBUG, "Max compute units reported %u", (unsigned)clState->max_compute_units);
	
	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_MEM_ALLOC_SIZE , sizeof(cl_ulong), (void *)&data->max_alloc, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_MEM_ALLOC_SIZE", status);
		return NULL;
	}
	applog(LOG_DEBUG, "Max mem alloc size is %lu", (unsigned long)data->max_alloc);
	
	find = strstr(vbuff, "MESA");
	if (find)
	{
		long int major = strtol(&find[4], &s, 10), minor = 0;
		if (!major)
		{} // No version number at all
		else
		if (s[0] == '.')
			minor = strtol(&s[1], NULL, 10);
		if (major < 10 || (major == 10 && minor < 1))
		{
			if (data->opt_opencl_binaries == OBU_DEFAULT)
			{
				applog(LOG_DEBUG, "Mesa OpenCL platform detected (v%ld.%ld), disabling OpenCL kernel binaries and bitalign", major, minor);
				data->opt_opencl_binaries = OBU_NONE;
			}
			else
				applog(LOG_DEBUG, "Mesa OpenCL platform detected (v%ld.%ld), disabling bitalign", major, minor);
			clState->hasBitAlign = false;
		}
		else
			applog(LOG_DEBUG, "Mesa OpenCL platform detected (v%ld.%ld)", major, minor);
		ismesa = true;
	}
	
	if (data->opt_opencl_binaries == OBU_DEFAULT)
	{
#ifdef __APPLE__
		// Apple OpenCL doesn't like using binaries this way
		data->opt_opencl_binaries = OBU_NONE;
#else
		data->opt_opencl_binaries = OBU_LOADSAVE;
#endif
	}

	/* Create binary filename based on parameters passed to opencl
	 * compiler to ensure we only load a binary that matches what would
	 * have otherwise created. The filename is:
	 * kernelname + name +/- g(offset) + v + vectors + w + work_size + l + sizeof(long) + p + platform version + .bin
	 * For scrypt the filename is:
	 * kernelname + name + g + lg + lookup_gap + tc + thread_concurrency + w + work_size + l + sizeof(long) + p + platform version + .bin
	 */
	char binaryfilename[255];
	char filename[255];
	char numbuf[32];

	if (!data->kernel_file)
	{
		if (opt_scrypt) {
			applog(LOG_INFO, "Selecting scrypt kernel");
			clState->chosen_kernel = KL_SCRYPT;
		}
		else if (ismesa)
		{
			applog(LOG_INFO, "Selecting phatk kernel for Mesa");
			clState->chosen_kernel = KL_PHATK;
		} else if (!strstr(name, "Tahiti") &&
			/* Detect all 2.6 SDKs not with Tahiti and use diablo kernel */
			(strstr(vbuff, "844.4") ||  // Linux 64 bit ATI 2.6 SDK
			 strstr(vbuff, "851.4") ||  // Windows 64 bit ""
			 strstr(vbuff, "831.4") ||
			 strstr(vbuff, "898.1") ||  // 12.2 driver SDK 
			 strstr(vbuff, "923.1") ||  // 12.4
			 strstr(vbuff, "938.2") ||  // SDK 2.7
			 strstr(vbuff, "1113.2"))) {// SDK 2.8
				applog(LOG_INFO, "Selecting diablo kernel");
				clState->chosen_kernel = KL_DIABLO;
		/* Detect all 7970s, older ATI and NVIDIA and use poclbm */
		} else if (strstr(name, "Tahiti") || !clState->hasBitAlign) {
			applog(LOG_INFO, "Selecting poclbm kernel");
			clState->chosen_kernel = KL_POCLBM;
		/* Use phatk for the rest R5xxx R6xxx */
		} else {
			applog(LOG_INFO, "Selecting phatk kernel");
			clState->chosen_kernel = KL_PHATK;
		}
		data->kernel_file = strdup(opencl_get_kernel_interface_name(clState->chosen_kernel));
	}
	
	snprintf(filename, sizeof(filename), "%s.cl", data->kernel_file);
	snprintf(binaryfilename, sizeof(filename), "%s", data->kernel_file);
	int pl;
	char *source = file_contents(filename, &pl);
	if (!source)
		return NULL;
	{
		uint8_t hash[0x20];
		char hashhex[7];
		sha256((void*)source, pl, hash);
		bin2hex(hashhex, hash, 3);
		tailsprintf(binaryfilename, sizeof(binaryfilename), "-%s", hashhex);
	}
	s = strstr(source, "kernel-interface:");
	if (s)
	{
		for (s = &s[17]; s[0] && isspace(s[0]); ++s)
			if (s[0] == '\n' || s[0] == '\r')
				break;
		for (q = s; q[0] && !isspace(q[0]); ++q)
		{}  // Find end of string
		const size_t kinamelen = q - s;
		char kiname[kinamelen + 1];
		memcpy(kiname, s, kinamelen);
		kiname[kinamelen] = '\0';
		clState->chosen_kernel = select_kernel(kiname);
	}
	else
	if (opt_scrypt)
		clState->chosen_kernel = KL_SCRYPT;
	switch (clState->chosen_kernel) {
		case KL_NONE:
			applog(LOG_ERR, "%s: Failed to identify kernel interface for %s",
			       cgpu->dev_repr, data->kernel_file);
			free(source);
			return NULL;
		case KL_PHATK:
			if ((strstr(vbuff, "844.4") || strstr(vbuff, "851.4") ||
			     strstr(vbuff, "831.4") || strstr(vbuff, "898.1") ||
			     strstr(vbuff, "923.1") || strstr(vbuff, "938.2") ||
			     strstr(vbuff, "1113.2"))) {
				applog(LOG_WARNING, "WARNING: You have selected the phatk kernel.");
				applog(LOG_WARNING, "You are running SDK 2.6+ which performs poorly with this kernel.");
				applog(LOG_WARNING, "Downgrade your SDK and delete any .bin files before starting again.");
				applog(LOG_WARNING, "Or allow BFGMiner to automatically choose a more suitable kernel.");
			}
		default:
			;
	}
	applog(LOG_DEBUG, "%s: Using kernel %s with interface %s",
	       cgpu->dev_repr, data->kernel_file,
	       opencl_get_kernel_interface_name(clState->chosen_kernel));

	/* For some reason 2 vectors is still better even if the card says
	 * otherwise, and many cards lie about their max so use 256 as max
	 * unless explicitly set on the command line. Tahiti prefers 1 */
	if (strstr(name, "Tahiti"))
		preferred_vwidth = 1;
	else if (preferred_vwidth > 2)
		preferred_vwidth = 2;

	if (data->vwidth)
		clState->vwidth = data->vwidth;
	else {
		clState->vwidth = preferred_vwidth;
		data->vwidth = preferred_vwidth;
	}

	if (((clState->chosen_kernel == KL_POCLBM || clState->chosen_kernel == KL_DIABLO || clState->chosen_kernel == KL_DIAKGCN) &&
		clState->vwidth == 1 && clState->hasOpenCL11plus) || opt_scrypt)
			clState->goffset = true;

	if (data->work_size && data->work_size <= clState->max_work_size)
		clState->wsize = data->work_size;
	else if (opt_scrypt)
		clState->wsize = 256;
	else if (strstr(name, "Tahiti"))
		clState->wsize = 64;
	else
		clState->wsize = (clState->max_work_size <= 256 ? clState->max_work_size : 256) / clState->vwidth;
	data->work_size = clState->wsize;

#ifdef USE_SCRYPT
	if (opt_scrypt) {
		if (!data->opt_lg) {
			applog(LOG_DEBUG, "GPU %d: selecting lookup gap of 2", gpu);
			data->lookup_gap = 2;
		} else
			data->lookup_gap = data->opt_lg;

		if (!data->opt_tc) {
			unsigned int sixtyfours;

			sixtyfours =  data->max_alloc / 131072 / 64 - 1;
			data->thread_concurrency = sixtyfours * 64;
			if (data->shaders && data->thread_concurrency > data->shaders) {
				data->thread_concurrency -= data->thread_concurrency % data->shaders;
				if (data->thread_concurrency > data->shaders * 5)
					data->thread_concurrency = data->shaders * 5;
			}
			applog(LOG_DEBUG, "GPU %u: selecting thread concurrency of %lu", gpu,  (unsigned long)data->thread_concurrency);
		} else
			data->thread_concurrency = data->opt_tc;
	}
#endif

	FILE *binaryfile;
	size_t *binary_sizes;
	char **binaries;
	size_t sourceSize[] = {(size_t)pl};
	cl_uint slot, cpnd;

	slot = cpnd = 0;

	binary_sizes = calloc(sizeof(size_t) * MAX_GPUDEVICES * 4, 1);
	if (unlikely(!binary_sizes)) {
		applog(LOG_ERR, "Unable to calloc binary_sizes");
		return NULL;
	}
	binaries = calloc(sizeof(char *) * MAX_GPUDEVICES * 4, 1);
	if (unlikely(!binaries)) {
		applog(LOG_ERR, "Unable to calloc binaries");
		return NULL;
	}

	strcat(binaryfilename, name);
	if (clState->goffset)
		strcat(binaryfilename, "g");
	if (opt_scrypt) {
#ifdef USE_SCRYPT
		sprintf(numbuf, "lg%utc%u", data->lookup_gap, (unsigned int)data->thread_concurrency);
		strcat(binaryfilename, numbuf);
#endif
	} else {
		sprintf(numbuf, "v%d", clState->vwidth);
		strcat(binaryfilename, numbuf);
	}
	sprintf(numbuf, "w%d", (int)clState->wsize);
	strcat(binaryfilename, numbuf);
	sprintf(numbuf, "l%d", (int)sizeof(long));
	strcat(binaryfilename, numbuf);
	strcat(binaryfilename, "p");
	strcat(binaryfilename, vbuff);
	sanestr(binaryfilename, binaryfilename);
	applog(LOG_DEBUG, "OCL%2u: Configured OpenCL kernel name: %s", gpu, binaryfilename);
	strcat(binaryfilename, ".bin");
	
	if (!(data->opt_opencl_binaries & OBU_LOAD))
		goto build;

	binaryfile = fopen(binaryfilename, "rb");
	if (!binaryfile) {
		applog(LOG_DEBUG, "No binary found, generating from source");
	} else {
		struct stat binary_stat;

		if (unlikely(stat(binaryfilename, &binary_stat))) {
			applog(LOG_DEBUG, "Unable to stat binary, generating from source");
			fclose(binaryfile);
			goto build;
		}
		if (!binary_stat.st_size)
			goto build;

		binary_sizes[slot] = binary_stat.st_size;
		binaries[slot] = (char *)calloc(binary_sizes[slot], 1);
		if (unlikely(!binaries[slot])) {
			applog(LOG_ERR, "Unable to calloc binaries");
			fclose(binaryfile);
			return NULL;
		}

		if (fread(binaries[slot], 1, binary_sizes[slot], binaryfile) != binary_sizes[slot]) {
			applog(LOG_ERR, "Unable to fread binaries");
			fclose(binaryfile);
			free(binaries[slot]);
			goto build;
		}

		clState->program = clCreateProgramWithBinary(clState->context, 1, &devices[gpu], &binary_sizes[slot], (const unsigned char **)binaries, &status, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: Loading Binary into cl_program (clCreateProgramWithBinary)", status);
			fclose(binaryfile);
			free(binaries[slot]);
			goto build;
		}

		fclose(binaryfile);
		applog(LOG_DEBUG, "Loaded binary image %s", binaryfilename);

		goto built;
	}

	/////////////////////////////////////////////////////////////////
	// Load CL file, build CL program object, create CL kernel object
	/////////////////////////////////////////////////////////////////

build:
	clState->program = clCreateProgramWithSource(clState->context, 1, (const char **)&source, sourceSize, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Loading Binary into cl_program (clCreateProgramWithSource)", status);
		return NULL;
	}

	/* create a cl program executable for all the devices specified */
	char *CompilerOptions = calloc(1, 256);

#ifdef USE_SCRYPT
	if (opt_scrypt)
		sprintf(CompilerOptions, "-D LOOKUP_GAP=%d -D CONCURRENT_THREADS=%d -D WORKSIZE=%d",
			data->lookup_gap, (unsigned int)data->thread_concurrency, (int)clState->wsize);
	else
#endif
	{
		sprintf(CompilerOptions, "-D WORKSIZE=%d -D VECTORS%d -D WORKVEC=%d",
			(int)clState->wsize, clState->vwidth, (int)clState->wsize * clState->vwidth);
	}
	applog(LOG_DEBUG, "Setting worksize to %"PRId64, (int64_t)clState->wsize);
	if (clState->vwidth > 1)
		applog(LOG_DEBUG, "Patched source to suit %d vectors", clState->vwidth);

	if (clState->hasBitAlign) {
		strcat(CompilerOptions, " -D BITALIGN");
		applog(LOG_DEBUG, "cl_amd_media_ops found, setting BITALIGN");
		if (strstr(name, "Cedar") ||
		    strstr(name, "Redwood") ||
		    strstr(name, "Juniper") ||
		    strstr(name, "Cypress" ) ||
		    strstr(name, "Hemlock" ) ||
		    strstr(name, "Caicos" ) ||
		    strstr(name, "Turks" ) ||
		    strstr(name, "Barts" ) ||
		    strstr(name, "Cayman" ) ||
		    strstr(name, "Antilles" ) ||
		    strstr(name, "Wrestler" ) ||
		    strstr(name, "Zacate" ) ||
		    strstr(name, "WinterPark" ))
		{
			// BFI_INT patching only works with AMD-APP up to 1084
			if (strstr(vbuff, "ATI-Stream"))
				patchbfi = true;
			else
			if ((s = strstr(vbuff, "AMD-APP")) && (s = strchr(s, '(')) && atoi(&s[1]) < 1085)
				patchbfi = true;
		}
	} else
		applog(LOG_DEBUG, "cl_amd_media_ops not found, will not set BITALIGN");

	if (patchbfi) {
		if (data->opt_opencl_binaries == OBU_LOADSAVE)
		{
			strcat(CompilerOptions, " -D BFI_INT");
			applog(LOG_DEBUG, "BFI_INT patch requiring device found, patched source with BFI_INT");
		}
		else
		{
			patchbfi = false;
			applog(LOG_WARNING, "BFI_INT patch requiring device found, but OpenCL binary usage disabled; cannot BFI_INT patch");
		}
	} else
		applog(LOG_DEBUG, "BFI_INT patch requiring device not found, will not BFI_INT patch");

	if (clState->goffset)
		strcat(CompilerOptions, " -D GOFFSET");

	if (!clState->hasOpenCL11plus)
		strcat(CompilerOptions, " -D OCL1");

	applog(LOG_DEBUG, "CompilerOptions: %s", CompilerOptions);
	status = bfg_clBuildProgram(clState, devices[gpu], CompilerOptions);
	free(CompilerOptions);

	if (status != CL_SUCCESS)
		return NULL;

	prog_built = true;
	
	if (!(data->opt_opencl_binaries & OBU_SAVE))
		goto built;

	status = clGetProgramInfo(clState->program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &cpnd, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error %d: Getting program info CL_PROGRAM_NUM_DEVICES. (clGetProgramInfo)", status);
		return NULL;
	}

	status = clGetProgramInfo(clState->program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t)*cpnd, binary_sizes, NULL);
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error %d: Getting program info CL_PROGRAM_BINARY_SIZES. (clGetProgramInfo)", status);
		return NULL;
	}

	/* The actual compiled binary ends up in a RANDOM slot! Grr, so we have
	 * to iterate over all the binary slots and find where the real program
	 * is. What the heck is this!? */
	for (slot = 0; slot < cpnd; slot++)
		if (binary_sizes[slot])
			break;

	/* copy over all of the generated binaries. */
	applog(LOG_DEBUG, "Binary size for gpu %u found in binary slot %u: %"PRId64,
	       gpu, (unsigned)slot, (int64_t)binary_sizes[slot]);
	if (!binary_sizes[slot]) {
		applog(LOG_ERR, "OpenCL compiler generated a zero sized binary, FAIL!");
		return NULL;
	}
	binaries[slot] = calloc(sizeof(char) * binary_sizes[slot], 1);
	status = clGetProgramInfo(clState->program, CL_PROGRAM_BINARIES, sizeof(char *) * cpnd, binaries, NULL );
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error %d: Getting program info. CL_PROGRAM_BINARIES (clGetProgramInfo)", status);
		return NULL;
	}

	/* Patch the kernel if the hardware supports BFI_INT but it needs to
	 * be hacked in */
	if (patchbfi) {
		unsigned remaining = binary_sizes[slot];
		char *w = binaries[slot];
		unsigned int start, length;

		/* Find 2nd incidence of .text, and copy the program's
		* position and length at a fixed offset from that. Then go
		* back and find the 2nd incidence of \x7ELF (rewind by one
		* from ELF) and then patch the opcocdes */
		if (!advance(&w, &remaining, ".text"))
			goto build;
		w++; remaining--;
		if (!advance(&w, &remaining, ".text")) {
			/* 32 bit builds only one ELF */
			w--; remaining++;
		}
		memcpy(&start, w + 285, 4);
		memcpy(&length, w + 289, 4);
		w = binaries[slot]; remaining = binary_sizes[slot];
		if (!advance(&w, &remaining, "ELF"))
			goto build;
		w++; remaining--;
		if (!advance(&w, &remaining, "ELF")) {
			/* 32 bit builds only one ELF */
			w--; remaining++;
		}
		w--; remaining++;
		w += start; remaining -= start;
		applog(LOG_DEBUG, "At %p (%u rem. bytes), to begin patching",
			w, remaining);
		patch_opcodes(w, length);

		status = clReleaseProgram(clState->program);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: Releasing program. (clReleaseProgram)", status);
			return NULL;
		}

		clState->program = clCreateProgramWithBinary(clState->context, 1, &devices[gpu], &binary_sizes[slot], (const unsigned char **)&binaries[slot], &status, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: Loading Binary into cl_program (clCreateProgramWithBinary)", status);
			return NULL;
		}

		/* Program needs to be rebuilt */
		prog_built = false;
	}

	free(source);

	/* Save the binary to be loaded next time */
	binaryfile = fopen(binaryfilename, "wb");
	if (!binaryfile) {
		/* Not a fatal problem, just means we build it again next time */
		applog(LOG_DEBUG, "Unable to create file %s", binaryfilename);
	} else {
		if (unlikely(fwrite(binaries[slot], 1, binary_sizes[slot], binaryfile) != binary_sizes[slot])) {
			applog(LOG_ERR, "Unable to fwrite to binaryfile");
			return NULL;
		}
		fclose(binaryfile);
	}
built:
	if (binaries[slot])
		free(binaries[slot]);
	free(binaries);
	free(binary_sizes);

	applog(LOG_INFO, "Initialising kernel %s with%s bitalign, %"PRId64" vectors and worksize %"PRIu64,
	       filename, clState->hasBitAlign ? "" : "out", (int64_t)clState->vwidth, (uint64_t)clState->wsize);

	if (!prog_built) {
		/* create a cl program executable for all the devices specified */
		status = bfg_clBuildProgram(clState, devices[gpu], NULL);
		if (status != CL_SUCCESS)
			return NULL;
	}

	/* get a kernel object handle for a kernel with the given name */
	clState->kernel = clCreateKernel(clState->program, "search", &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Creating Kernel from program. (clCreateKernel)", status);
		return NULL;
	}
	
	free((void*)cgpu->kname);
	cgpu->kname = strdup(data->kernel_file);

#ifdef USE_SCRYPT
	if (opt_scrypt) {
		size_t ipt = (1024 / data->lookup_gap + (1024 % data->lookup_gap > 0));
		size_t bufsize = 128 * ipt * data->thread_concurrency;

		/* Use the max alloc value which has been rounded to a power of
		 * 2 greater >= required amount earlier */
		if (bufsize > data->max_alloc) {
			applog(LOG_WARNING, "Maximum buffer memory device %d supports says %lu", gpu, (unsigned long)data->max_alloc);
			applog(LOG_WARNING, "Your scrypt settings come to %lu", (unsigned long)bufsize);
		}
		applog(LOG_DEBUG, "Creating scrypt buffer sized %lu", (unsigned long)bufsize);
		clState->padbufsize = bufsize;

		/* This buffer is weird and might work to some degree even if
		 * the create buffer call has apparently failed, so check if we
		 * get anything back before we call it a failure. */
		clState->padbuffer8 = NULL;
		clState->padbuffer8 = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, bufsize, NULL, &status);
		if (status != CL_SUCCESS && !clState->padbuffer8) {
			applog(LOG_ERR, "Error %d: clCreateBuffer (padbuffer8), decrease TC or increase LG", status);
			return NULL;
		}

		clState->CLbuffer0 = clCreateBuffer(clState->context, CL_MEM_READ_ONLY, 128, NULL, &status);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: clCreateBuffer (CLbuffer0)", status);
			return NULL;
		}
		clState->outputBuffer = clCreateBuffer(clState->context, CL_MEM_WRITE_ONLY, SCRYPT_BUFFERSIZE, NULL, &status);
	} else
#endif
	clState->outputBuffer = clCreateBuffer(clState->context, CL_MEM_WRITE_ONLY, BUFFERSIZE, NULL, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: clCreateBuffer (outputBuffer)", status);
		return NULL;
	}

	return clState;
}
#endif /* HAVE_OPENCL */

