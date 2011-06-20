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
		fprintf(stderr, "Unable to open %s for reading\n", filename);
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
		printf("Error: Getting Platforms. (clGetPlatformsIDs)\n");
		return -1;
	}   

	if(numPlatforms > 0)
	{   
		cl_platform_id* platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		if(status != CL_SUCCESS)
		{   
			printf("Error: Getting Platform Ids. (clGetPlatformsIDs)\n");
			return -1;
		}   

		unsigned int i;
		for(i=0; i < numPlatforms; ++i)
		{   
			char pbuff[100];
			status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{   
				printf("Error: Getting Platform Info. (clGetPlatformInfo)\n");
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
		printf("Error: Getting Device IDs (num)\n");
		return -1;
	}

	return numDevices;
}

void advance(char **area, unsigned *remaining, const char *marker)
{
	char *find = memmem(*area, *remaining, marker, strlen(marker));
	if (!find)
		fprintf(stderr, "Marker \"%s\" not found\n", marker), exit(1);
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
		printf("Potential OP3 instructions identified: "
			"%i BFE_INT, %i BFE_UINT, %i BYTE_ALIGN\n",
			count_bfe_int, count_bfe_uint, count_byte_align);
		printf("Patched a total of %i BFI_INT instructions\n", patched);
	}
}

_clState *initCl(int gpu, char *name, size_t nameSize) {
	cl_int status = 0;

	_clState *clState = malloc(sizeof(_clState));;

	cl_uint numPlatforms;
	cl_platform_id platform = NULL;
	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if(status != CL_SUCCESS)
	{   
		printf("Error: Getting Platforms. (clGetPlatformsIDs)\n");
		return NULL;
	}   

	if(numPlatforms > 0)
	{   
		cl_platform_id* platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		if(status != CL_SUCCESS)
		{   
			printf("Error: Getting Platform Ids. (clGetPlatformsIDs)\n");
			return NULL;
		}   

		unsigned int i;
		for(i=0; i < numPlatforms; ++i)
		{   
			char pbuff[100];
			status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{   
				printf("Error: Getting Platform Info. (clGetPlatformInfo)\n");
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
		printf("Error: Getting Device IDs (num)\n");
		return NULL;
	}

	cl_device_id *devices;
	if(numDevices > 0 ) {
		devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

		/* Now, get the device list data */

		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
		if(status != CL_SUCCESS)
		{
			printf("Error: Getting Device IDs (list)\n");
			return NULL;
		}

		printf("List of devices:\n");

		unsigned int i;
		for(i=0; i<numDevices; i++) {
			char pbuff[100];
			status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{
				printf("Error: Getting Device Info\n");
				return NULL;
			}

			printf("\t%i\t%s\n", i, pbuff);
		}

		if (gpu >= 0 && gpu < numDevices) {
			char pbuff[100];
			status = clGetDeviceInfo(devices[gpu], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if(status != CL_SUCCESS)
			{
				printf("Error: Getting Device Info\n");
				return NULL;
			}

			printf("Selected %i: %s\n", gpu, pbuff);
			strncpy(name, pbuff, nameSize);
		} else {
			printf("Invalid GPU %i\n", gpu);
			return NULL;
		}

	} else return NULL;

	cl_context_properties cps[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };

	clState->context = clCreateContextFromType(cps, CL_DEVICE_TYPE_GPU, NULL, NULL, &status);
	if(status != CL_SUCCESS) 
	{   
		printf("Error: Creating Context. (clCreateContextFromType)\n");
		return NULL; 
	}


	/////////////////////////////////////////////////////////////////
	// Load CL file, build CL program object, create CL kernel object
	/////////////////////////////////////////////////////////////////
	//
	const char * filename  = "oclminer.cl";
	int pl;
	char *source = file_contents(filename, &pl);
	size_t sourceSize[] = {(size_t)pl};

	clState->program = clCreateProgramWithSource(clState->context, 1, (const char **)&source, sourceSize, &status);
	if(status != CL_SUCCESS) 
	{   
		printf("Error: Loading Binary into cl_program (clCreateProgramWithBinary)\n");
		return NULL;
	}

	/* create a cl program executable for all the devices specified */
	status = clBuildProgram(clState->program, 1, &devices[gpu], NULL, NULL, NULL);
	if(status != CL_SUCCESS) 
	{   
		printf("Error: Building Program (clBuildProgram)\n");
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

		char *log = malloc(logSize);
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		printf("%s\n", log);
		return NULL; 
	}

	size_t nDevices;
	size_t * binary_sizes;
	char ** binaries;
	unsigned int i;
	int err;

	/* figure out number of devices and the sizes of the binary for each device. */
	err = clGetProgramInfo( clState->program, CL_PROGRAM_NUM_DEVICES, sizeof(nDevices), &nDevices, NULL );
	binary_sizes = (size_t *)malloc( sizeof(size_t)*nDevices );
	err = clGetProgramInfo( clState->program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t)*nDevices, binary_sizes, NULL );

	/* copy over all of the generated binaries. */
	binaries = (char **)malloc( sizeof(char *)*nDevices );
	for( i = 0; i < nDevices; i++ ) {
		printf("binary size %d : %d\n", i, binary_sizes[i]);
		if( binary_sizes[i] != 0 )
			binaries[i] = (char *)malloc( sizeof(char)*binary_sizes[i] );
		else
			binaries[i] = NULL;
	}
	err = clGetProgramInfo( clState->program, CL_PROGRAM_BINARIES, sizeof(char *)*nDevices, binaries, NULL );
	// all the code should be within the first 83000 bytes or so, but scan
	// a bit more for headroom
	unsigned bytes_to_scan = 93000;
	for (i = 0; i < nDevices; i++) {
		if (!binaries[i])
			continue;

		unsigned remaining = bytes_to_scan;
		char *w = binaries[i];
		int j;

		if (opt_debug)
			printf("At %p (%u rem. bytes), searching outer elf marker\n", w, remaining);
		advance(&w, &remaining, "ELF");
		if (opt_debug)
			printf("At %p (%u rem. bytes), searching inner elf marker\n", w, remaining);
		advance(&w, &remaining, "ELF");
		if (opt_debug)
			printf("At %p (%u rem. bytes), searching first .text marker\n", w, remaining);
		advance(&w, &remaining, ".text");
		if (opt_debug)
			printf("At %p (%u rem. bytes), searching second .text marker\n", w, remaining);
		advance(&w, &remaining, ".text");
		// now we are pointing to the first opcode
		patch_opcodes(w, remaining);
	}

	status = clReleaseProgram(clState->program);
	if(status != CL_SUCCESS)
	{
		printf("Error: Releasing program. (clReleaseProgram)\n");
		return NULL;
	}

	clState->program = clCreateProgramWithBinary(clState->context, numDevices, &devices[gpu], binary_sizes, binaries, &status, NULL);
	if(status != CL_SUCCESS) 
	{   
		printf("Error: Loading Binary into cl_program (clCreateProgramWithBinary)\n");
		return NULL;
	}

	/* create a cl program executable for all the devices specified */
	status = clBuildProgram(clState->program, 1, &devices[gpu], NULL, NULL, NULL);
	if(status != CL_SUCCESS) 
	{   
		printf("Error: Building Program (clBuildProgram)\n");
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

		char *log = malloc(logSize);
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		printf("%s\n", log);
		return NULL; 
	}

	/* get a kernel object handle for a kernel with the given name */
	clState->kernel = clCreateKernel(clState->program, "oclminer", &status);
	if(status != CL_SUCCESS)
	{
		printf("Error: Creating Kernel from program. (clCreateKernel)\n");
		return NULL;
	}

	/////////////////////////////////////////////////////////////////
	// Create an OpenCL command queue
	/////////////////////////////////////////////////////////////////
	clState->commandQueue = clCreateCommandQueue( clState->context, devices[gpu], 0, &status);
	if(status != CL_SUCCESS)
	{
		printf("Creating Command Queue. (clCreateCommandQueue)\n");
		return NULL;
	}

	clState->inputBuffer = clCreateBuffer(clState->context, CL_MEM_READ_ONLY, sizeof(dev_blk_ctx), NULL, &status);
	if(status != CL_SUCCESS) {
		printf("Error: clCreateBuffer (inputBuffer)\n");
		return NULL;
	}

	clState->outputBuffer = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, sizeof(uint32_t) * 128, NULL, &status);
	if(status != CL_SUCCESS) {
		printf("Error: clCreateBuffer (outputBuffer)\n");
		return NULL;
	}

	return clState;
}

