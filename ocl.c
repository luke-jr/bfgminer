/*
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2012-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <limits.h>
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

static
void extract_word(char * const buf, const size_t bufsz, const char ** const endptr, const char *s)
{
	const char *q;
	for ( ; s[0] && isspace(s[0]); ++s)
		if (s[0] == '\n' || s[0] == '\r')
			break;
	for (q = s; q[0] && !isspace(q[0]); ++q)
	{}  // Find end of string
	size_t len = q - s;
	if (len >= bufsz)
		len = bufsz - 1;
	memcpy(buf, s, len);
	buf[len] = '\0';
	if (endptr)
		*endptr = q;
}

char *opencl_kernel_source(const char * const filename, int * const out_sourcelen, enum cl_kernels * const out_kinterface, struct mining_algorithm ** const out_malgo)
{
	char *source = file_contents(filename, out_sourcelen);
	if (!source)
		return NULL;
	char *s = strstr(source, "kernel-interface:");
	if (s)
	{
		const char *q;
		char buf[0x20];
		extract_word(buf, sizeof(buf), &q, &s[17]);
		*out_kinterface = select_kernel(buf);
		
		if (out_malgo && (q[0] == '\t' || q[0] == ' '))
		{
			extract_word(buf, sizeof(buf), &q, q);
			*out_malgo = mining_algorithm_by_alias(buf);
		}
	}
	else
		*out_kinterface = KL_NONE;
	return source;
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

cl_int bfg_clBuildProgram(cl_program * const program, const cl_device_id devid, const char * const CompilerOptions)
{
	cl_int status;
	
	status = clBuildProgram(*program, 1, &devid, CompilerOptions, NULL, NULL);
	
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error %d: Building Program (clBuildProgram)", status);
		size_t logSize;
		status = clGetProgramBuildInfo(*program, devid, CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);
		
		char *log = malloc(logSize ?: 1);
		status = clGetProgramBuildInfo(*program, devid, CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
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

_clState *opencl_create_clState(unsigned int gpu, char *name, size_t nameSize)
{
	_clState *clState = calloc(1, sizeof(_clState));
	struct cgpu_info *cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	cl_platform_id platform = NULL;
	char pbuff[256], vbuff[255];
	char *s;
	cl_platform_id* platforms;
	cl_device_id *devices;
	cl_uint numPlatforms;
	cl_uint numDevices;
	cl_int status;

	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platforms. (clGetPlatformsIDs)", status);
err:
		free(clState);
		return NULL;
	}

	platforms = (cl_platform_id *)alloca(numPlatforms*sizeof(cl_platform_id));
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platform Ids. (clGetPlatformsIDs)", status);
		goto err;
	}

	if (opt_platform_id >= (int)numPlatforms) {
		applog(LOG_ERR, "Specified platform that does not exist");
		goto err;
	}

	status = clGetPlatformInfo(platforms[opt_platform_id], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Platform Info. (clGetPlatformInfo)", status);
		goto err;
	}
	platform = platforms[opt_platform_id];

	if (platform == NULL) {
		perror("NULL platform found!\n");
		goto err;
	}

	applog(LOG_INFO, "CL Platform vendor: %s", pbuff);
	status = clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(pbuff), pbuff, NULL);
	if (status == CL_SUCCESS)
		applog(LOG_INFO, "CL Platform name: %s", pbuff);
	status = clGetPlatformInfo(platform, CL_PLATFORM_VERSION, sizeof(vbuff), vbuff, NULL);
	if (status == CL_SUCCESS)
		applog(LOG_INFO, "CL Platform version: %s", vbuff);
	clState->platform_ver_str = strdup(vbuff);

	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Getting Device IDs (num)", status);
		goto err;
	}

	if (numDevices <= 0)
		goto err;
	
	{
		devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

		/* Now, get the device list data */

		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d: Getting Device IDs (list)", status);
err2:
			free(devices);
			goto err;
		}

		applog(LOG_INFO, "List of devices:");

		unsigned int i;
		for (i = 0; i < numDevices; i++) {
			status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS) {
				applog(LOG_ERR, "Error %d: Getting Device Info", status);
				goto err2;
			}

			applog(LOG_INFO, "\t%i\t%s", i, pbuff);
		}

		if (gpu < numDevices) {
			status = clGetDeviceInfo(devices[gpu], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS) {
				applog(LOG_ERR, "Error %d: Getting Device Info", status);
				goto err2;
			}

			applog(LOG_INFO, "Selected %i: %s", gpu, pbuff);
			strncpy(name, pbuff, nameSize);
		} else {
			applog(LOG_ERR, "Invalid GPU %i", gpu);
			goto err2;
		}
	}

	cl_context_properties cps[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };

	clState->context = clCreateContextFromType(cps, CL_DEVICE_TYPE_GPU, NULL, NULL, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Creating Context. (clCreateContextFromType)", status);
		goto err2;
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
		goto err2;
	}

	/* Check for BFI INT support. Hopefully people don't mix devices with
	 * and without it! */
	char * extensions = malloc(1024);
	const char * camo = "cl_amd_media_ops";
	char *find;

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_EXTENSIONS, 1024, (void *)extensions, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_EXTENSIONS", status);
		free(extensions);
		goto err2;
	}
	find = strstr(extensions, camo);
	if (find)
		clState->hasBitAlign = true;
	free(extensions);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT, sizeof(cl_uint), (void *)&clState->preferred_vwidth, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT", status);
		goto err2;
	}
	applog(LOG_DEBUG, "Preferred vector width reported %d", clState->preferred_vwidth);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), (void *)&clState->max_work_size, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_WORK_GROUP_SIZE", status);
		goto err2;
	}
	applog(LOG_DEBUG, "Max work group size reported %"PRId64, (int64_t)clState->max_work_size);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(clState->max_compute_units), (void *)&clState->max_compute_units, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_COMPUTE_UNITS", status);
		goto err2;
	}
	if (data->_init_intensity)
	{
		data->oclthreads = 1;  // Needed to ensure we don't just try to re-save the string (which would free before strduping and segfault anyway)
		opencl_set_intensity_from_str(cgpu, data->_init_intensity);
	}
	else
	{
		data->oclthreads = 1;
		data->intensity = INT_MIN;
	}
	applog(LOG_DEBUG, "Max compute units reported %u", (unsigned)clState->max_compute_units);
	
	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_MEM_ALLOC_SIZE , sizeof(cl_ulong), (void *)&data->max_alloc, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_MEM_ALLOC_SIZE", status);
		goto err2;
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
		clState->is_mesa = true;
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
	
	clState->devid = devices[gpu];
	free(devices);
	
	/* For some reason 2 vectors is still better even if the card says
	 * otherwise, and many cards lie about their max so use 256 as max
	 * unless explicitly set on the command line. Tahiti prefers 1 */
	if (strstr(name, "Tahiti"))
		clState->preferred_vwidth = 1;
	else
	if (clState->preferred_vwidth > 2)
		clState->preferred_vwidth = 2;

	if (data->vwidth)
		clState->vwidth = data->vwidth;
	else {
		clState->vwidth = clState->preferred_vwidth;
		data->vwidth = clState->preferred_vwidth;
	}

	clState->outputBuffer = clCreateBuffer(clState->context, 0, OPENCL_MAX_BUFFERSIZE, NULL, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: clCreateBuffer (outputBuffer)", status);
		// NOTE: devices is freed here, but still assigned
		goto err;
	}
	
	return clState;
}

static
bool opencl_load_kernel_binary(struct cgpu_info * const cgpu, _clState * const clState, struct opencl_kernel_info * const kernelinfo, const char * const binaryfilename, bytes_t * const b)
{
	cl_int status;
	
	FILE * const binaryfile = fopen(binaryfilename, "rb");
	if (!binaryfile)
		return false;
	
	struct stat binary_stat;
	if (unlikely(stat(binaryfilename, &binary_stat)))
	{
		applog(LOG_DEBUG, "Unable to stat binary, generating from source");
		fclose(binaryfile);
		return false;
	}
	if (!binary_stat.st_size)
	{
		fclose(binaryfile);
		return false;
	}
	
	const size_t binsz = binary_stat.st_size;
	bytes_resize(b, binsz);
	if (fread(bytes_buf(b), 1, binsz, binaryfile) != binsz)
	{
		applog(LOG_ERR, "Unable to fread binaries");
		fclose(binaryfile);
		return false;
	}
	fclose(binaryfile);
	
	kernelinfo->program = clCreateProgramWithBinary(clState->context, 1, &clState->devid, &binsz, (void*)&bytes_buf(b), &status, NULL);
	if (status != CL_SUCCESS)
		applogr(false, LOG_ERR, "Error %d: Loading Binary into cl_program (clCreateProgramWithBinary)", status);
	
	status = bfg_clBuildProgram(&kernelinfo->program, clState->devid, NULL);
	if (status != CL_SUCCESS)
		return false;
	
	applog(LOG_DEBUG, "Loaded binary image %s", binaryfilename);
	return true;
}

static
bool opencl_should_patch_bfi_int(struct cgpu_info * const cgpu, _clState * const clState, struct opencl_kernel_info * const kernelinfo)
{
#ifdef USE_SHA256D
	struct opencl_device_data * const data = cgpu->device_data;
	const char * const name = cgpu->name;
	const char * const vbuff = clState->platform_ver_str;
	char *s;
	
	if (!clState->hasBitAlign)
		return false;
	
	if (!(strstr(name, "Cedar") ||
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
	      strstr(name, "WinterPark" )))
		return false;
	
	// BFI_INT patching only works with AMD-APP up to 1084
	if (strstr(vbuff, "ATI-Stream"))
	{}
	else
	if ((s = strstr(vbuff, "AMD-APP")) && (s = strchr(s, '(')) && atoi(&s[1]) < 1085)
	{}
	else
		return false;
	
	switch (kernelinfo->interface)
	{
		case KL_DIABLO: case KL_DIAKGCN: case KL_PHATK: case KL_POCLBM:
			// Okay, these actually use BFI_INT hacking
			break;
		default:
			// Anything else has never needed it
			return false;
			break;
	}
	
	if (data->opt_opencl_binaries != OBU_LOADSAVE)
		applogr(false, LOG_WARNING, "BFI_INT patch requiring device found, but OpenCL binary usage disabled; cannot BFI_INT patch");
	
	applog(LOG_DEBUG, "BFI_INT patch requiring device found, will patch source with BFI_INT");
	return true;
#else
	return false;
#endif
}

static
bool opencl_build_kernel(struct cgpu_info * const cgpu, _clState * const clState, struct opencl_kernel_info * const kernelinfo, const char *source, const size_t source_len, const bool patchbfi)
{
	struct opencl_device_data * const data = cgpu->device_data;
	cl_int status;
	
	kernelinfo->program = clCreateProgramWithSource(clState->context, 1, &source, &source_len, &status);
	if (status != CL_SUCCESS)
		applogr(false, LOG_ERR, "Error %d: Loading Binary into cl_program (clCreateProgramWithSource)", status);

	/* create a cl program executable for all the devices specified */
	char *CompilerOptions = calloc(1, 256);

#ifdef USE_SCRYPT
	if (kernelinfo->interface == KL_SCRYPT)
		sprintf(CompilerOptions, "-D LOOKUP_GAP=%d -D CONCURRENT_THREADS=%d -D WORKSIZE=%d",
			data->lookup_gap, (unsigned int)data->thread_concurrency, (int)kernelinfo->wsize);
	else
#endif
	{
		sprintf(CompilerOptions, "-D WORKSIZE=%d -D VECTORS%d -D WORKVEC=%d",
			(int)kernelinfo->wsize, clState->vwidth, (int)kernelinfo->wsize * clState->vwidth);
	}
	applog(LOG_DEBUG, "Setting worksize to %"PRId64, (int64_t)kernelinfo->wsize);
	if (clState->vwidth > 1)
		applog(LOG_DEBUG, "Patched source to suit %d vectors", clState->vwidth);

	if (clState->hasBitAlign)
	{
		strcat(CompilerOptions, " -D BITALIGN");
		applog(LOG_DEBUG, "cl_amd_media_ops found, setting BITALIGN");
	}
	else
		applog(LOG_DEBUG, "cl_amd_media_ops not found, will not set BITALIGN");

#ifdef USE_SHA256D
	if (patchbfi)
		strcat(CompilerOptions, " -D BFI_INT");
#endif

	if (kernelinfo->goffset)
		strcat(CompilerOptions, " -D GOFFSET");

	applog(LOG_DEBUG, "CompilerOptions: %s", CompilerOptions);
	status = bfg_clBuildProgram(&kernelinfo->program, clState->devid, CompilerOptions);
	free(CompilerOptions);

	if (status != CL_SUCCESS)
		return false;
	
	return true;
}

static
bool opencl_get_kernel_binary(struct cgpu_info * const cgpu, _clState * const clState, struct opencl_kernel_info * const kernelinfo, bytes_t * const b)
{
	cl_int status;
	cl_uint slot, cpnd;
	
	status = clGetProgramInfo(kernelinfo->program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &cpnd, NULL);
	if (unlikely(status != CL_SUCCESS))
		applogr(false, LOG_ERR, "Error %d: Getting program info CL_PROGRAM_NUM_DEVICES. (clGetProgramInfo)", status);
	
	if (!cpnd)
		return false;

	size_t binary_sizes[cpnd];
	status = clGetProgramInfo(kernelinfo->program, CL_PROGRAM_BINARY_SIZES, sizeof(binary_sizes), binary_sizes, NULL);
	if (unlikely(status != CL_SUCCESS))
		applogr(false, LOG_ERR, "Error %d: Getting program info CL_PROGRAM_BINARY_SIZES. (clGetProgramInfo)", status);
	
	uint8_t **binaries = malloc(sizeof(*binaries) * cpnd);
	for (slot = 0; slot < cpnd; ++slot)
		binaries[slot] = malloc(binary_sizes[slot] + 1);

	/* The actual compiled binary ends up in a RANDOM slot! Grr, so we have
	 * to iterate over all the binary slots and find where the real program
	 * is. What the heck is this!? */
	for (slot = 0; slot < cpnd; slot++)
		if (binary_sizes[slot])
			break;

	/* copy over all of the generated binaries. */
	applog(LOG_DEBUG, "%s: Binary size found in binary slot %u: %"PRId64, cgpu->dev_repr, (unsigned)slot, (int64_t)binary_sizes[slot]);
	if (!binary_sizes[slot])
		applogr(false, LOG_ERR, "OpenCL compiler generated a zero sized binary, FAIL!");
	status = clGetProgramInfo(kernelinfo->program, CL_PROGRAM_BINARIES, sizeof(binaries), binaries, NULL);
	if (unlikely(status != CL_SUCCESS))
		applogr(false, LOG_ERR, "Error %d: Getting program info. CL_PROGRAM_BINARIES (clGetProgramInfo)", status);
	
	bytes_resize(b, binary_sizes[slot]);
	memcpy(bytes_buf(b), binaries[slot], bytes_len(b));
	
	for (slot = 0; slot < cpnd; ++slot)
		free(binaries[slot]);
	free(binaries);
	
	return true;
}

#ifdef USE_SHA256D
	/* Patch the kernel if the hardware supports BFI_INT but it needs to
	 * be hacked in */
static
bool opencl_patch_kernel_binary(bytes_t * const b)
{
	unsigned remaining = bytes_len(b);
	char *w = (void*)bytes_buf(b);
	unsigned int start, length;

	/* Find 2nd incidence of .text, and copy the program's
	* position and length at a fixed offset from that. Then go
	* back and find the 2nd incidence of \x7ELF (rewind by one
	* from ELF) and then patch the opcocdes */
	if (!advance(&w, &remaining, ".text"))
		return false;
	w++; remaining--;
	if (!advance(&w, &remaining, ".text")) {
		/* 32 bit builds only one ELF */
		w--; remaining++;
	}
	memcpy(&start, w + 285, 4);
	memcpy(&length, w + 289, 4);
	w = (void*)bytes_buf(b);
	remaining = bytes_len(b);
	if (!advance(&w, &remaining, "ELF"))
		return false;
	w++; remaining--;
	if (!advance(&w, &remaining, "ELF")) {
		/* 32 bit builds only one ELF */
		w--; remaining++;
	}
	w--; remaining++;
	w += start; remaining -= start;
	applog(LOG_DEBUG, "At %p (%u rem. bytes), to begin patching", w, remaining);
	patch_opcodes(w, length);
	return true;
}

static
bool opencl_replace_binary_kernel(struct cgpu_info * const cgpu, _clState * const clState, struct opencl_kernel_info * const kernelinfo, bytes_t * const b)
{
	cl_int status;
	
	status = clReleaseProgram(kernelinfo->program);
	if (status != CL_SUCCESS)
		applogr(false, LOG_ERR, "Error %d: Releasing program. (clReleaseProgram)", status);
	
	const size_t binsz = bytes_len(b);
	kernelinfo->program = clCreateProgramWithBinary(clState->context, 1, &clState->devid, &binsz, (void*)&bytes_buf(b), &status, NULL);
	if (status != CL_SUCCESS)
		applogr(false, LOG_ERR, "Error %d: Loading Binary into cl_program (clCreateProgramWithBinary)", status);
	
	status = bfg_clBuildProgram(&kernelinfo->program, clState->devid, NULL);
	if (status != CL_SUCCESS)
		return false;
	
	return true;
}
#endif

static
bool opencl_save_kernel_binary(const char * const binaryfilename, bytes_t * const b)
{
	FILE *binaryfile;
	
	/* Save the binary to be loaded next time */
	binaryfile = fopen(binaryfilename, "wb");
	if (!binaryfile)
		return false;
	
	// FIXME: Failure here results in a bad file; better to write and move-replace (but unlink before replacing for Windows)
	if (unlikely(fwrite(bytes_buf(b), 1, bytes_len(b), binaryfile) != bytes_len(b)))
	{
		fclose(binaryfile);
		return false;
	}
	
	fclose(binaryfile);
	return true;
}

static
bool opencl_test_goffset(_clState * const clState)
{
	if (sizeof(size_t) < sizeof(uint32_t))
		return false;
	
	const char *source = "__kernel __attribute__((reqd_work_group_size(64, 1, 1))) void runtest(volatile __global uint *out) { *out = get_global_id(0); }";
	const size_t source_len = strlen(source);
	cl_int status;
	cl_program program = clCreateProgramWithSource(clState->context, 1, &source, &source_len, &status);
	if (status != CL_SUCCESS)
		applogr(false, LOG_ERR, "Error %d: Loading %s code into cl_program (clCreateProgramWithSource)", status, "goffset test");
	status = bfg_clBuildProgram(&program, clState->devid, "");
	if (status != CL_SUCCESS)
	{
fail:
		clReleaseProgram(program);
		return false;
	}
	cl_kernel kernel = clCreateKernel(program, "runtest", &status);
	if (status != CL_SUCCESS)
		return_via_applog(fail, , LOG_ERR, "Error %d: Creating kernel from %s program (clCreateKernel)", status, "goffset test");
	static const uint32_t cleardata = 0;
	status = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_FALSE, 0, sizeof(cleardata), &cleardata, 0, NULL, NULL);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error %d: Clearing output buffer for %s kernel (clEnqueueWriteBuffer)", status, "goffset test");
fail2:
		clReleaseKernel(kernel);
		goto fail;
	}
	status = clSetKernelArg(kernel, 0, sizeof(clState->outputBuffer), &clState->outputBuffer);
	if (status != CL_SUCCESS)
		return_via_applog(fail2, , LOG_ERR, "Error %d: Setting kernel argument for %s kernel (clSetKernelArg)", status, "goffset test");
	const size_t size_t_one = 1, test_goffset = 0xfabd0bf9;
	status = clEnqueueNDRangeKernel(clState->commandQueue, kernel, 1, &test_goffset, &size_t_one, &size_t_one, 0,  NULL, NULL);
	if (status != CL_SUCCESS)
		return_via_applog(fail2, , LOG_DEBUG, "Error %d: Running %s kernel (clEnqueueNDRangeKernel)", status, "goffset test");
	uint32_t resultdata;
	status = clEnqueueReadBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0, sizeof(resultdata), &resultdata, 0, NULL, NULL);
	if (status != CL_SUCCESS)
		return_via_applog(fail2, , LOG_DEBUG, "Error %d: Reading result from %s kernel (clEnqueueReadBuffer)", status, "goffset test");
	applog(LOG_DEBUG, "%s kernel returned 0x%08lx for goffset 0x%08lx", "goffset test", (unsigned long)resultdata, (unsigned long)test_goffset);
	return (resultdata == test_goffset);
}

bool opencl_load_kernel(struct cgpu_info * const cgpu, _clState * const clState, const char * const name, struct opencl_kernel_info * const kernelinfo, const char * const kernel_file, __maybe_unused const struct mining_algorithm * const malgo)
{
	const int gpu = cgpu->device_id;
	struct opencl_device_data * const data = cgpu->device_data;
	const char * const vbuff = clState->platform_ver_str;
	cl_int status;
	
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

	snprintf(filename, sizeof(filename), "%s.cl", kernel_file);
	snprintf(binaryfilename, sizeof(filename), "%s", kernel_file);
	int pl;
	char *source = opencl_kernel_source(filename, &pl, &kernelinfo->interface, NULL);
	if (!source)
		return false;
	{
		uint8_t hash[0x20];
		char hashhex[7];
		sha256((void*)source, pl, hash);
		bin2hex(hashhex, hash, 3);
		tailsprintf(binaryfilename, sizeof(binaryfilename), "-%s", hashhex);
	}
	switch (kernelinfo->interface)
	{
		case KL_NONE:
			applog(LOG_ERR, "%s: Failed to identify kernel interface for %s",
			       cgpu->dev_repr, kernel_file);
			free(source);
			return false;
#ifdef USE_SHA256D
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
#endif
		default:
			;
	}
	applog(LOG_DEBUG, "%s: Using kernel %s with interface %s",
	       cgpu->dev_repr, kernel_file,
	       opencl_get_kernel_interface_name(kernelinfo->interface));

	{
		int kernel_goffset_support = 0;  // 0 = none; 1 = optional; 2 = required
		if (strstr(source, "def GOFFSET"))
			kernel_goffset_support = 1;
		else
		if (strstr(source, " base,"))
			kernel_goffset_support = 0;
		else
			kernel_goffset_support = 2;
		bool device_goffset_support = false;
		switch (data->use_goffset)
		{
			case BTS_TRUE:
				device_goffset_support = true;
				break;
			case BTS_FALSE:
				// if the kernel requires goffset, don't allow the user to disable it
				if (kernel_goffset_support == 2)
				{
					if (opencl_test_goffset(clState))
						device_goffset_support = true;
				}
				break;
			case BTS_UNKNOWN:
				data->use_goffset = opencl_test_goffset(clState);
				if (data->use_goffset)
					device_goffset_support = true;
				break;
		}
		applog(LOG_DEBUG, "%s: goffset support: device=%s kernel=%s", cgpu->dev_repr, device_goffset_support ? "yes" : "no", (kernel_goffset_support == 2) ? "required" : ((kernel_goffset_support == 1) ? "optional" : "none"));
		if (device_goffset_support)
		{
			if (kernel_goffset_support)
				kernelinfo->goffset = true;
		}
		else
		if (kernel_goffset_support == 2)
		{
			// FIXME: Determine this before min_nonce_diff returns positive
			applog(LOG_ERR, "%s: Need goffset support!", cgpu->dev_repr);
			return false;
		}
	}

	if (data->work_size && data->work_size <= clState->max_work_size)
		kernelinfo->wsize = data->work_size;
	else
#ifdef USE_SCRYPT
	if (malgo->algo == POW_SCRYPT)
		kernelinfo->wsize = 256;
	else
#endif
	if (strstr(name, "Tahiti"))
		kernelinfo->wsize = 64;
	else
		kernelinfo->wsize = (clState->max_work_size <= 256 ? clState->max_work_size : 256) / clState->vwidth;

#ifdef USE_SCRYPT
	if (kernelinfo->interface == KL_SCRYPT)
	{
		if (!data->thread_concurrency)
		{
			unsigned int sixtyfours;

			sixtyfours =  data->max_alloc / 131072 / 64 - 1;
			data->thread_concurrency = sixtyfours * 64;
			if (data->shaders && data->thread_concurrency > data->shaders) {
				data->thread_concurrency -= data->thread_concurrency % data->shaders;
				if (data->thread_concurrency > data->shaders * 5)
					data->thread_concurrency = data->shaders * 5;
			}
			applog(LOG_DEBUG, "GPU %u: selecting thread concurrency of %lu", gpu,  (unsigned long)data->thread_concurrency);
		}
	}
#endif

	strcat(binaryfilename, name);
	if (kernelinfo->goffset)
		strcat(binaryfilename, "g");
#ifdef USE_SCRYPT
	if (kernelinfo->interface == KL_SCRYPT)
	{
		sprintf(numbuf, "lg%utc%u", data->lookup_gap, (unsigned int)data->thread_concurrency);
		strcat(binaryfilename, numbuf);
	}
	else
#endif
	{
		sprintf(numbuf, "v%d", clState->vwidth);
		strcat(binaryfilename, numbuf);
	}
	sprintf(numbuf, "w%d", (int)kernelinfo->wsize);
	strcat(binaryfilename, numbuf);
	sprintf(numbuf, "l%d", (int)sizeof(long));
	strcat(binaryfilename, numbuf);
	strcat(binaryfilename, "p");
	strcat(binaryfilename, vbuff);
	sanestr(binaryfilename, binaryfilename);
	applog(LOG_DEBUG, "OCL%2u: Configured OpenCL kernel name: %s", gpu, binaryfilename);
	strcat(binaryfilename, ".bin");
	
	bool patchbfi = opencl_should_patch_bfi_int(cgpu, clState, kernelinfo);
	
	bytes_t binary_bytes = BYTES_INIT;
	bool loaded_kernel = false;
	if (data->opt_opencl_binaries & OBU_LOAD)
	{
		if (opencl_load_kernel_binary(cgpu, clState, kernelinfo, binaryfilename, &binary_bytes))
			loaded_kernel = true;
		else
		{
			bytes_free(&binary_bytes);
			applog(LOG_DEBUG, "No usable binary found, generating from source");
		}
	}
	
	if (!loaded_kernel)
	{
build:
		if (!opencl_build_kernel(cgpu, clState, kernelinfo, source, pl, patchbfi))
		{
			free(source);
			return false;
		}
		
		if ((patchbfi || (data->opt_opencl_binaries & OBU_SAVE)) && !bytes_len(&binary_bytes))
		{
			if (!opencl_get_kernel_binary(cgpu, clState, kernelinfo, &binary_bytes))
			{
				bytes_free(&binary_bytes);
				applog(LOG_DEBUG, "%s: Failed to get compiled kernel binary from OpenCL (cannot save it)", cgpu->dev_repr);
				// NOTE: empty binary_bytes will fail BFI_INT patch on its own
			}
		}
		
#ifdef USE_SHA256D
		if (patchbfi)
		{
			if (!(opencl_patch_kernel_binary(&binary_bytes)) && opencl_replace_binary_kernel(cgpu, clState, kernelinfo, &binary_bytes))
			{
				applog(LOG_DEBUG, "%s: BFI_INT patching failed, rebuilding without it", cgpu->dev_repr);
				patchbfi = false;
				bytes_free(&binary_bytes);
				goto build;
			}
		}
#endif
		
		if (data->opt_opencl_binaries & OBU_SAVE)
		{
			if (!opencl_save_kernel_binary(binaryfilename, &binary_bytes))
				applog(LOG_DEBUG, "Unable to save file %s", binaryfilename);
		}
	}
	
	free(source);
	bytes_free(&binary_bytes);
	
	applog(LOG_INFO, "Initialising kernel %s with%s bitalign, %"PRId64" vectors and worksize %"PRIu64,
	       filename, clState->hasBitAlign ? "" : "out", (int64_t)clState->vwidth, (uint64_t)kernelinfo->wsize);

	/* get a kernel object handle for a kernel with the given name */
	kernelinfo->kernel = clCreateKernel(kernelinfo->program, "search", &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d: Creating Kernel from program. (clCreateKernel)", status);
		return false;
	}
	
	free((void*)cgpu->kname);
	cgpu->kname = strdup(kernel_file);

#ifdef MAX_CLBUFFER0_SZ
	switch (kernelinfo->interface)
	{
#ifdef USE_SCRYPT
		case KL_SCRYPT:
			if (!clState->padbufsize)
			{
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
					return false;
				}
			}
			// NOTE: fallthru
#endif
#ifdef USE_OPENCL_FULLHEADER
		case KL_FULLHEADER:
#endif
			if (!clState->CLbuffer0)
			{
				clState->CLbuffer0 = clCreateBuffer(clState->context, CL_MEM_READ_ONLY, MAX_CLBUFFER0_SZ, NULL, &status);
				if (status != CL_SUCCESS) {
					applog(LOG_ERR, "Error %d: clCreateBuffer (CLbuffer0)", status);
					return false;
				}
			}
			break;
		default:
			break;
	}
#endif

	kernelinfo->loaded = true;
	return true;
}
