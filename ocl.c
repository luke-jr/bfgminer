#define _GNU_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include "findnonce.h"
#include "ocl.h"

char *file_contents(const char *filename, int *length)
{
	FILE *f = fopen(filename, "r");
	void *buffer;

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

int clDevicesNum() {
	cl_int status = 0;

	cl_uint numPlatforms;
	cl_platform_id platform = NULL;
	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if(status != CL_SUCCESS)
	{   
		applog(LOG_ERR, "Error: Getting Platforms. (clGetPlatformsIDs)");
		return -1;
	}   

	if(numPlatforms > 0)
	{   
		cl_platform_id* platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		if(status != CL_SUCCESS)
		{   
			applog(LOG_ERR, "Error: Getting Platform Ids. (clGetPlatformsIDs)");
			return -1;
		}   

		unsigned int i;
		for(i=0; i < numPlatforms; ++i)
		{   
			char pbuff[100];
			status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{   
				applog(LOG_ERR, "Error: Getting Platform Info. (clGetPlatformInfo)");
				free(platforms);
				return -1;
			}   
			platform = platforms[i];
			if(!strcmp(pbuff, "Advanced Micro Devices, Inc."))
			{   
				break;
			}  
		}   
		free(platforms);
	}   

	if(platform == NULL) {
		perror("NULL platform found!\n");
		return -1;
	}

	cl_uint numDevices;
	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if(status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Getting Device IDs (num)");
		return -1;
	}

	return numDevices;
}

void advance(char **area, unsigned *remaining, const char *marker)
{
	char *find = memmem(*area, *remaining, marker, strlen(marker));

	if (!find)
		applog(LOG_ERR, "Marker \"%s\" not found", marker), exit(1);
	*remaining -= find - *area;
	*area = find;
}

#define OP3_INST_BFE_UINT	4UL
#define OP3_INST_BFE_INT	5UL
#define OP3_INST_BFI_INT	6UL
#define OP3_INST_BIT_ALIGN_INT	12UL
#define OP3_INST_BYTE_ALIGN_INT	13UL

void patch_opcodes(char *w, unsigned remaining)
{
	uint64_t *opcode = (uint64_t *)w;
	int patched = 0;
	int count_bfe_int = 0;
	int count_bfe_uint = 0;
	int count_byte_align = 0;
	while (42)
	{
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
				*opcode &= 0xfffc1fffffffffffUL;
				*opcode |= OP3_INST_BFI_INT << (32 + 13);
				patched++;
			}
		}
		if (remaining <= 8) {
			break;
		}
		opcode++;
		remaining -= 8;
	}
	if (opt_debug) {
		applog(LOG_DEBUG, "Potential OP3 instructions identified: "
			"%i BFE_INT, %i BFE_UINT, %i BYTE_ALIGN",
			count_bfe_int, count_bfe_uint, count_byte_align);
		applog(LOG_DEBUG, "Patched a total of %i BFI_INT instructions", patched);
	}
}

_clState *initCl(unsigned int gpu, char *name, size_t nameSize)
{
	bool hasBitAlign = false;
	cl_int status = 0;
	unsigned int i;

	_clState *clState = malloc(sizeof(_clState));;

	cl_uint numPlatforms;
	cl_platform_id platform = NULL;
	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if(status != CL_SUCCESS)
	{   
		applog(LOG_ERR, "Error: Getting Platforms. (clGetPlatformsIDs)");
		return NULL;
	}   

	if(numPlatforms > 0)
	{   
		cl_platform_id* platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		if(status != CL_SUCCESS)
		{   
			applog(LOG_ERR, "Error: Getting Platform Ids. (clGetPlatformsIDs)");
			return NULL;
		}   

		for(i = 0; i < numPlatforms; ++i)
		{   
			char pbuff[100];
			status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{   
				applog(LOG_ERR, "Error: Getting Platform Info. (clGetPlatformInfo)");
				free(platforms);
				return NULL;
			}   
			platform = platforms[i];
			if(!strcmp(pbuff, "Advanced Micro Devices, Inc."))
			{   
				break;
			}  
		}   
		free(platforms);
	}   

	if(platform == NULL) {
		perror("NULL platform found!\n");
		return NULL;
	}

	cl_uint numDevices;
	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if(status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Getting Device IDs (num)");
		return NULL;
	}

	cl_device_id *devices;
	if (numDevices > 0 ) {
		devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

		/* Now, get the device list data */

		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
		if(status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Getting Device IDs (list)");
			return NULL;
		}

		applog(LOG_INFO, "List of devices:");

		unsigned int i;
		for(i=0; i<numDevices; i++) {
			char pbuff[100];
			status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{
				applog(LOG_ERR, "Error: Getting Device Info");
				return NULL;
			}

			applog(LOG_INFO, "\t%i\t%s", i, pbuff);
		}

		if (gpu < numDevices) {
			char pbuff[100];
			status = clGetDeviceInfo(devices[gpu], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{
				applog(LOG_ERR, "Error: Getting Device Info");
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
	if(status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Creating Context. (clCreateContextFromType)");
		return NULL;
	}

	/* Check for BFI INT support. Hopefully people don't mix devices with
	 * and without it! */
	char * extensions = malloc(1024);
	const char * camo = "cl_amd_media_ops";
	char *find;

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_EXTENSIONS, 1024, (void *)extensions, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Failed to clGetDeviceInfo when trying to get CL_DEVICE_EXTENSIONS");
		return NULL;
	}
	find = strstr(extensions, camo);
	if (find)
		hasBitAlign = true;

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT, sizeof(cl_uint), (void *)&clState->preferred_vwidth, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Failed to clGetDeviceInfo when trying to get CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT");
		return NULL;
	}
	if (opt_debug)
		applog(LOG_DEBUG, "Preferred vector width reported %d", clState->preferred_vwidth);

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), (void *)&clState->max_work_size, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Failed to clGetDeviceInfo when trying to get CL_DEVICE_MAX_WORK_GROUP_SIZE");
		return NULL;
	}
	if (opt_debug)
		applog(LOG_DEBUG, "Max work group size reported %d", clState->max_work_size);

	/////////////////////////////////////////////////////////////////
	// Load CL file, build CL program object, create CL kernel object
	/////////////////////////////////////////////////////////////////

	/* Load a different kernel depending on whether it supports
	 * cl_amd_media_ops or not */
	char *filename = "poclbm.cl";

	int pl;
	char *source = file_contents(filename, &pl);
	size_t sourceSize[] = {(size_t)pl};

	/* Patch the source file with the preferred_vwidth */
	if (clState->preferred_vwidth > 1) {
		char *find = strstr(source, "VECTORSX");

		if (unlikely(!find)) {
			applog(LOG_ERR, "Unable to find VECTORSX in source");
			return NULL;
		}
		find += 7; // "VECTORS"
		if (clState->preferred_vwidth == 2)
			strncpy(find, "2", 1);
		else
			strncpy(find, "4", 1);
		if (opt_debug)
			applog(LOG_DEBUG, "Patched source to suit %d vectors", clState->preferred_vwidth);
	}

	/* Patch the source file defining BFI_INT */
	if (hasBitAlign == true) {
		char *find = strstr(source, "BFI_INTX");

		if (unlikely(!find)) {
			applog(LOG_ERR, "Unable to find BFI_INTX in source");
			return NULL;
		}
		find += 7; // "BFI_INT"
		strncpy(find, " ", 1);
		if (opt_debug)
			applog(LOG_DEBUG, "cl_amd_media_ops found, patched source with BFI_INT");
	} else if (opt_debug)
		applog(LOG_DEBUG, "cl_amd_media_ops not found, will not BFI_INT patch");

	applog(LOG_INFO, "Initialising kernel with%s BFI_INT patching, %d vectors and worksize %d",
	       hasBitAlign ? "" : "out", clState->preferred_vwidth, clState->max_work_size / clState->preferred_vwidth);

	clState->program = clCreateProgramWithSource(clState->context, 1, (const char **)&source, sourceSize, &status);
	if(status != CL_SUCCESS) 
	{   
		applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithSource)");
		return NULL;
	}

	/* create a cl program executable for all the devices specified */
	status = clBuildProgram(clState->program, 1, &devices[gpu], NULL, NULL, NULL);
	if(status != CL_SUCCESS) 
	{   
		applog(LOG_ERR, "Error: Building Program (clBuildProgram)");
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

		char *log = malloc(logSize);
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		applog(LOG_INFO, "%s", log);
		return NULL; 
	}

	/* Patch the kernel if the hardware supports BFI_INT */
	if (hasBitAlign == true) {
		size_t nDevices;
		size_t * binary_sizes;
		char ** binaries;
		int err;

		/* figure out number of devices and the sizes of the binary for each device. */
		err = clGetProgramInfo( clState->program, CL_PROGRAM_NUM_DEVICES, sizeof(nDevices), &nDevices, NULL );
		binary_sizes = (size_t *)malloc( sizeof(size_t)*nDevices );
		err = clGetProgramInfo( clState->program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t)*nDevices, binary_sizes, NULL );

		/* copy over all of the generated binaries. */
		binaries = (char **)malloc( sizeof(char *)*nDevices );
		for( i = 0; i < nDevices; i++ ) {
			if (opt_debug)
				applog(LOG_DEBUG, "binary size %d : %d", i, binary_sizes[i]);
			if( binary_sizes[i] != 0 )
				binaries[i] = (char *)malloc( sizeof(char)*binary_sizes[i] );
			else
				binaries[i] = NULL;
		}
		err = clGetProgramInfo( clState->program, CL_PROGRAM_BINARIES, sizeof(char *)*nDevices, binaries, NULL );

		for (i = 0; i < nDevices; i++) {
			if (!binaries[i])
				continue;

			unsigned remaining = binary_sizes[i];
			char *w = binaries[i];
			unsigned int start, length;

			/* Find 2nd incidence of .text, and copy the program's
			* position and length at a fixed offset from that. Then go
			* back and find the 2nd incidence of \x7ELF (rewind by one
			* from ELF) and then patch the opcocdes */
			advance(&w, &remaining, ".text");
			w++; remaining--;
			advance(&w, &remaining, ".text");
			memcpy(&start, w + 285, 4);
			memcpy(&length, w + 289, 4);
			w = binaries[i]; remaining = binary_sizes[i];
			advance(&w, &remaining, "ELF");
			w++; remaining--;
			advance(&w, &remaining, "ELF");
			w--; remaining++;
			w += start; remaining -= start;
			if (opt_debug)
				applog(LOG_DEBUG, "At %p (%u rem. bytes), to begin patching",
					w, remaining);
			patch_opcodes(w, length);
		}
		status = clReleaseProgram(clState->program);
		if(status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Releasing program. (clReleaseProgram)");
			return NULL;
		}

		clState->program = clCreateProgramWithBinary(clState->context, numDevices, &devices[gpu], binary_sizes, (const unsigned char **)binaries, &status, NULL);
		if(status != CL_SUCCESS) 
		{   
			applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithBinary)");
			return NULL;
		}
	}

	/* create a cl program executable for all the devices specified */
	status = clBuildProgram(clState->program, 1, &devices[gpu], NULL, NULL, NULL);
	if(status != CL_SUCCESS) 
	{   
		applog(LOG_ERR, "Error: Building Program (clBuildProgram)");
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

		char *log = malloc(logSize);
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		applog(LOG_INFO, "%s", log);
		return NULL; 
	}

	/* get a kernel object handle for a kernel with the given name */
	clState->kernel = clCreateKernel(clState->program, "search", &status);
	if(status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Creating Kernel from program. (clCreateKernel)");
		return NULL;
	}

	/////////////////////////////////////////////////////////////////
	// Create an OpenCL command queue
	/////////////////////////////////////////////////////////////////
	clState->commandQueue = clCreateCommandQueue( clState->context, devices[gpu], 0, &status);
	if(status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Creating Command Queue. (clCreateCommandQueue)");
		return NULL;
	}

	clState->outputBuffer = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, sizeof(uint32_t) * 128, NULL, &status);
	if(status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: clCreateBuffer (outputBuffer)");
		return NULL;
	}

	return clState;
}

