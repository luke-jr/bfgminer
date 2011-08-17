/*
 * Copyright 2011 Con Kolivas
 */
#include "config.h"
#ifdef HAVE_OPENCL

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef WIN32
	#include <winsock2.h>
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netdb.h>
#endif

#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "findnonce.h"
#include "ocl.h"

extern int opt_vectors;
extern int opt_worksize;

char *file_contents(const char *filename, int *length)
{
	FILE *f = fopen(filename, "rb");
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
	/* If this fails, assume no GPUs. */
	if (status != CL_SUCCESS)
	{
		applog(LOG_INFO, "clGetPlatformsIDs failed (no GPU?)");
		return 0;
	}

	if (numPlatforms > 0)
	{
		cl_platform_id* platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		if (status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Getting Platform Ids. (clGetPlatformsIDs)");
			return -1;
		}

		unsigned int i;
		for(i=0; i < numPlatforms; ++i)
		{
			char pbuff[100];
			status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS)
			{
				applog(LOG_ERR, "Error: Getting Platform Info. (clGetPlatformInfo)");
				free(platforms);
				return -1;
			}
			platform = platforms[i];
			if (!strcmp(pbuff, "Advanced Micro Devices, Inc."))
			{
				break;
			}
		}
		free(platforms);
	}

	if (platform == NULL) {
		perror("NULL platform found!\n");
		return -1;
	}

	cl_uint numDevices;
	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Getting Device IDs (num)");
		return -1;
	}

	return numDevices;
}

static int advance(char **area, unsigned *remaining, const char *marker)
{
	char *find = memmem(*area, *remaining, marker, strlen(marker));

	if (!find) {
		if (opt_debug)
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
				*opcode &= 0xfffc1fffffffffffULL;
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
	int patchbfi = 0;
	cl_int status = 0;
	unsigned int i;

	_clState *clState = calloc(1, sizeof(_clState));

	cl_uint numPlatforms;
	cl_platform_id platform = NULL;
	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Getting Platforms. (clGetPlatformsIDs)");
		return NULL;
	}

	if (numPlatforms > 0)
	{
		cl_platform_id* platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id));
		status = clGetPlatformIDs(numPlatforms, platforms, NULL);
		if (status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Getting Platform Ids. (clGetPlatformsIDs)");
			return NULL;
		}

		for(i = 0; i < numPlatforms; ++i)
		{
			char pbuff[100];
			status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS)
			{
				applog(LOG_ERR, "Error: Getting Platform Info. (clGetPlatformInfo)");
				free(platforms);
				return NULL;
			}
			platform = platforms[i];
			if (!strcmp(pbuff, "Advanced Micro Devices, Inc."))
			{
				break;
			}
		}
		free(platforms);
	}

	if (platform == NULL) {
		perror("NULL platform found!\n");
		return NULL;
	}

	size_t nDevices;
	cl_uint numDevices;
	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Getting Device IDs (num)");
		return NULL;
	}

	cl_device_id *devices;
	if (numDevices > 0 ) {
		devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

		/* Now, get the device list data */

		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
		if (status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Getting Device IDs (list)");
			return NULL;
		}

		applog(LOG_INFO, "List of devices:");

		unsigned int i;
		for(i=0; i<numDevices; i++) {
			char pbuff[100];
			status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS)
			{
				applog(LOG_ERR, "Error: Getting Device Info");
				return NULL;
			}

			applog(LOG_INFO, "\t%i\t%s", i, pbuff);
		}

		if (gpu < numDevices) {
			char pbuff[100];
			status = clGetDeviceInfo(devices[gpu], CL_DEVICE_NAME, sizeof(pbuff), pbuff, &nDevices);
			if (status != CL_SUCCESS)
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
	if (status != CL_SUCCESS)
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
		clState->hasBitAlign = patchbfi = 1;

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

	/* For some reason 2 vectors is still better even if the card says
	 * otherwise, and many cards lie about their max so use 256 as max
	 * unless explicitly set on the command line */
	if (clState->preferred_vwidth > 1)
		clState->preferred_vwidth = 2;
	if (opt_vectors)
		clState->preferred_vwidth = opt_vectors;
	if (opt_worksize && opt_worksize <= clState->max_work_size)
		clState->work_size = opt_worksize;
	else
		clState->work_size = (clState->max_work_size <= 256 ? clState->max_work_size : 256) /
				clState->preferred_vwidth;

	/* Create binary filename based on parameters passed to opencl
	 * compiler to ensure we only load a binary that matches what would
	 * have otherwise created. The filename is:
	 * name + kernelname +/i bitalign + v + vectors + w + work_size + sizeof(long) + .bin
	 */
	char binaryfilename[255];
	char numbuf[10];
	char filename[16];

	if (chosen_kernel == KL_NONE) {
		if (clState->hasBitAlign)
			chosen_kernel = KL_PHATK;
		else
			chosen_kernel = KL_POCLBM;
	}

	switch (chosen_kernel) {
		case KL_POCLBM:
			strcpy(filename, "poclbm110717.cl");
			strcpy(binaryfilename, "poclbm110717");
			break;
		case KL_NONE: /* Shouldn't happen */
		case KL_PHATK:
			strcpy(filename, "phatk2_2.cl");
			strcpy(binaryfilename, "phatk2_2");
			break;
	}

	FILE *binaryfile;
	size_t *binary_sizes;
	char **binaries;
	int pl;
	char *source, *rawsource = file_contents(filename, &pl);
	size_t sourceSize[] = {(size_t)pl};

	source = malloc(pl);
	if (!source) {
		applog(LOG_ERR, "Unable to malloc source");
		return NULL;
	}

	binary_sizes = (size_t *)malloc(sizeof(size_t)*nDevices);
	if (unlikely(!binary_sizes)) {
		applog(LOG_ERR, "Unable to malloc binary_sizes");
		return NULL;
	}
	binaries = (char **)malloc(sizeof(char *)*nDevices);
	if (unlikely(!binaries)) {
		applog(LOG_ERR, "Unable to malloc binaries");
		return NULL;
	}

	strcat(binaryfilename, name);
	if (clState->hasBitAlign)
		strcat(binaryfilename, "bitalign");

	strcat(binaryfilename, "v");
	sprintf(numbuf, "%d", clState->preferred_vwidth);
	strcat(binaryfilename, numbuf);
	strcat(binaryfilename, "w");
	sprintf(numbuf, "%d", (int)clState->work_size);
	strcat(binaryfilename, numbuf);
	strcat(binaryfilename, "long");
	sprintf(numbuf, "%d", (int)sizeof(long));
	strcat(binaryfilename, numbuf);
	strcat(binaryfilename, ".bin");

	binaryfile = fopen(binaryfilename, "rb");
	if (!binaryfile) {
		if (opt_debug)
			applog(LOG_DEBUG, "No binary found, generating from source");
	} else {
		struct stat binary_stat;

		if (unlikely(stat(binaryfilename, &binary_stat))) {
			if (opt_debug)
				applog(LOG_DEBUG, "Unable to stat binary, generating from source");
			fclose(binaryfile);
			goto build;
		}
		binary_sizes[gpu] = binary_stat.st_size;
		binaries[gpu] = (char *)malloc(binary_sizes[gpu]);
		if (unlikely(!binaries[gpu])) {
			applog(LOG_ERR, "Unable to malloc binaries");
			fclose(binaryfile);
			return NULL;
		}

		if (fread(binaries[gpu], 1, binary_sizes[gpu], binaryfile) != binary_sizes[gpu]) {
			applog(LOG_ERR, "Unable to fread binaries[gpu]");
			fclose(binaryfile);
			goto build;
		}
		fclose(binaryfile);

		clState->program = clCreateProgramWithBinary(clState->context, 1, &devices[gpu], &binary_sizes[gpu], (const unsigned char **)&binaries[gpu], &status, NULL);
		if (status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithBinary)");
			return NULL;
		}
		if (opt_debug)
			applog(LOG_DEBUG, "Loaded binary image %s", binaryfilename);

		free(binaries[gpu]);
		goto built;
	}

	/////////////////////////////////////////////////////////////////
	// Load CL file, build CL program object, create CL kernel object
	/////////////////////////////////////////////////////////////////

build:
	memcpy(source, rawsource, pl);

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

	/* Patch the source file defining BITALIGN */
	if (clState->hasBitAlign) {
		char *find = strstr(source, "BITALIGNX");

		if (unlikely(!find)) {
			applog(LOG_ERR, "Unable to find BITALIGNX in source");
			return NULL;
		}
		find += 8; // "BITALIGN"
		strncpy(find, " ", 1);
		if (opt_debug)
			applog(LOG_DEBUG, "cl_amd_media_ops found, patched source with BITALIGN");
	} else if (opt_debug)
		applog(LOG_DEBUG, "cl_amd_media_ops not found, will not BITALIGN patch");

	if (patchbfi) {
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

	clState->program = clCreateProgramWithSource(clState->context, 1, (const char **)&source, sourceSize, &status);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithSource)");
		return NULL;
	}

	/* create a cl program executable for all the devices specified */
	char CompilerOptions[256];
	sprintf(CompilerOptions, "%s%i", "-DWORKSIZE=", clState->work_size);
	//int n = 1000;
	//while(n--)
	//	printf("%s", CompilerOptions);
	//return 1;
	status = clBuildProgram(clState->program, 1, &devices[gpu], CompilerOptions , NULL, NULL);

	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Building Program (clBuildProgram)");
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

		char *log = malloc(logSize);
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		applog(LOG_INFO, "%s", log);
		return NULL;
	}

	/* Create the command queue just so we can flush it to try and avoid
	 * zero sized binaries */
	clState->commandQueue = clCreateCommandQueue(clState->context, devices[gpu], 0, &status);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Creating Command Queue. (clCreateCommandQueue)");
		return NULL;
	}
	status = clFinish(clState->commandQueue);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Finishing command queue. (clFinish)");
		return NULL;
	}

	status = clGetProgramInfo( clState->program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t)*nDevices, binary_sizes, NULL );
	if (unlikely(status != CL_SUCCESS))
	{
		applog(LOG_ERR, "Error: Getting program info CL_PROGRAM_BINARY_SIZES. (clGetPlatformInfo)");
		return NULL;
	}

	/* copy over all of the generated binaries. */
	if (opt_debug)
		applog(LOG_DEBUG, "binary size %d : %d", gpu, binary_sizes[gpu]);
	if (!binary_sizes[gpu]) {
		applog(LOG_ERR, "OpenCL compiler generated a zero sized binary, may need to reboot!");
		return NULL;
	}
	binaries[gpu] = (char *)malloc( sizeof(char)*binary_sizes[gpu]);
	status = clGetProgramInfo( clState->program, CL_PROGRAM_BINARIES, sizeof(char *)*nDevices, binaries, NULL );
	if (unlikely(status != CL_SUCCESS))
	{
		applog(LOG_ERR, "Error: Getting program info. (clGetPlatformInfo)");
		return NULL;
	}
	clReleaseCommandQueue(clState->commandQueue);

	/* Patch the kernel if the hardware supports BFI_INT */
	if (patchbfi) {
		unsigned remaining = binary_sizes[gpu];
		char *w = binaries[gpu];
		unsigned int start, length;

		/* Find 2nd incidence of .text, and copy the program's
		* position and length at a fixed offset from that. Then go
		* back and find the 2nd incidence of \x7ELF (rewind by one
		* from ELF) and then patch the opcocdes */
		if (!advance(&w, &remaining, ".text"))
			{patchbfi = 0; goto build;}
		w++; remaining--;
		if (!advance(&w, &remaining, ".text")) {
			/* 32 bit builds only one ELF */
			w--; remaining++;
		}
		memcpy(&start, w + 285, 4);
		memcpy(&length, w + 289, 4);
		w = binaries[gpu]; remaining = binary_sizes[gpu];
		if (!advance(&w, &remaining, "ELF"))
			{patchbfi = 0; goto build;}
		w++; remaining--;
		if (!advance(&w, &remaining, "ELF")) {
			/* 32 bit builds only one ELF */
			w--; remaining++;
		}
		w--; remaining++;
		w += start; remaining -= start;
		if (opt_debug)
			applog(LOG_DEBUG, "At %p (%u rem. bytes), to begin patching",
				w, remaining);
		patch_opcodes(w, length);

		status = clReleaseProgram(clState->program);
		if (status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Releasing program. (clReleaseProgram)");
			return NULL;
		}

		clState->program = clCreateProgramWithBinary(clState->context, 1, &devices[gpu], &binary_sizes[gpu], (const unsigned char **)&binaries[gpu], &status, NULL);
		if (status != CL_SUCCESS)
		{
			applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithBinary)");
			return NULL;
		}
	}

	free(source);
	free(rawsource);

	/* Save the binary to be loaded next time */
	binaryfile = fopen(binaryfilename, "wb");
	if (!binaryfile) {
		/* Not a fatal problem, just means we build it again next time */
		if (opt_debug)
			applog(LOG_DEBUG, "Unable to create file %s", binaryfilename);
	} else {
		if (unlikely(fwrite(binaries[gpu], 1, binary_sizes[gpu], binaryfile) != binary_sizes[gpu])) {
			applog(LOG_ERR, "Unable to fwrite to binaryfile");
			return NULL;
		}
		fclose(binaryfile);
	}
	if (binaries[gpu])
		free(binaries[gpu]);
built:
	free(binaries);
	free(binary_sizes);

	applog(LOG_INFO, "Initialising kernel %s with%s BFI_INT patching, %d vectors and worksize %d",
	       filename, patchbfi ? "" : "out", clState->preferred_vwidth, clState->work_size);

	/* create a cl program executable for all the devices specified */
	status = clBuildProgram(clState->program, 1, &devices[gpu], NULL, NULL, NULL);
	if (status != CL_SUCCESS)
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
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Error: Creating Kernel from program. (clCreateKernel)");
		return NULL;
	}

	/////////////////////////////////////////////////////////////////
	// Create an OpenCL command queue
	/////////////////////////////////////////////////////////////////
	clState->commandQueue = clCreateCommandQueue(clState->context, devices[gpu],
						     CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &status);
	if (status != CL_SUCCESS) /* Try again without OOE enable */
		clState->commandQueue = clCreateCommandQueue(clState->context, devices[gpu], 0 , &status);
	if (status != CL_SUCCESS)
	{
		applog(LOG_ERR, "Creating Command Queue. (clCreateCommandQueue)");
		return NULL;
	}

	clState->outputBuffer = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, BUFFERSIZE, NULL, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: clCreateBuffer (outputBuffer)");
		return NULL;
	}

	return clState;
}
#endif /* HAVE_OPENCL */

