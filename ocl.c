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
int opt_platform_id;

char *file_contents(const char *filename, int *length)
{
	char *fullpath = alloca(PATH_MAX);
	void *buffer;
	FILE *f;

	strcpy(fullpath, opt_kernel_path);
	strcat(fullpath, filename);

	/* Try in the optional kernel path or installed prefix first */
	f = fopen(fullpath, "rb");
	if (!f) {
		/* Then try from the path cgminer was called */
		strcpy(fullpath, cgminer_path);
		strcat(fullpath, filename);
		f = fopen(fullpath, "rb");
	}
	/* Finally try opening it directly */
	if (!f)
		f = fopen(filename, "rb");

	if (!f) {
		applog(LOG_ERR, "Unable to open %s or %s for reading", filename, fullpath);
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

int clDevicesNum(void) {
	cl_int status;
	char pbuff[256];
	cl_uint numDevices;
	cl_uint numPlatforms;
	cl_platform_id *platforms;
	cl_platform_id platform = NULL;
	unsigned int most_devices = 0, i;

	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	/* If this fails, assume no GPUs. */
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "clGetPlatformsIDs failed (no OpenCL SDK installed?)");
		return -1;
	}

	if (numPlatforms == 0) {
		applog(LOG_ERR, "clGetPlatformsIDs returned no platforms (no OpenCL SDK installed?)");
		return -1;
	}

	platforms = (cl_platform_id *)alloca(numPlatforms*sizeof(cl_platform_id));
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Getting Platform Ids. (clGetPlatformsIDs)");
		return -1;
	}

	for (i = 0; i < numPlatforms; i++) {
		status = clGetPlatformInfo( platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Getting Platform Info. (clGetPlatformInfo)");
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
			applog(LOG_ERR, "Error: Getting Device IDs (num)");
			return -1;
		}
		applog(LOG_INFO, "Platform %d devices: %d", i, numDevices);
		if (numDevices > most_devices)
			most_devices = numDevices;
	}

	return most_devices;
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
	if (opt_debug) {
		applog(LOG_DEBUG, "Potential OP3 instructions identified: "
			"%i BFE_INT, %i BFE_UINT, %i BYTE_ALIGN",
			count_bfe_int, count_bfe_uint, count_byte_align);
		applog(LOG_DEBUG, "Patched a total of %i BFI_INT instructions", patched);
	}
}

_clState *initCl(unsigned int gpu, char *name, size_t nameSize)
{
	_clState *clState = calloc(1, sizeof(_clState));
	bool patchbfi = false, prog_built = false;
	cl_platform_id platform = NULL;
	cl_platform_id* platforms;
	cl_device_id *devices;
	cl_uint numPlatforms;
	cl_uint numDevices;
	char pbuff[256];
	cl_int status;

	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Getting Platforms. (clGetPlatformsIDs)");
		return NULL;
	}

	platforms = (cl_platform_id *)alloca(numPlatforms*sizeof(cl_platform_id));
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Getting Platform Ids. (clGetPlatformsIDs)");
		return NULL;
	}

	if (opt_platform_id >= numPlatforms) {
		applog(LOG_ERR, "Specified platform that does not exist");
		return NULL;
	}

	status = clGetPlatformInfo(platforms[opt_platform_id], CL_PLATFORM_VENDOR, sizeof(pbuff), pbuff, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Getting Platform Info. (clGetPlatformInfo)");
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
	status = clGetPlatformInfo(platform, CL_PLATFORM_VERSION, sizeof(pbuff), pbuff, NULL);
	if (status == CL_SUCCESS)
		applog(LOG_INFO, "CL Platform version: %s", pbuff);

	status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Getting Device IDs (num)");
		return NULL;
	}

	if (numDevices > 0 ) {
		devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id));

		/* Now, get the device list data */

		status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Getting Device IDs (list)");
			return NULL;
		}

		applog(LOG_INFO, "List of devices:");

		unsigned int i;
		for (i = 0; i < numDevices; i++) {
			status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS) {
				applog(LOG_ERR, "Error: Getting Device Info");
				return NULL;
			}

			applog(LOG_INFO, "\t%i\t%s", i, pbuff);
		}

		if (gpu < numDevices) {
			status = clGetDeviceInfo(devices[gpu], CL_DEVICE_NAME, sizeof(pbuff), pbuff, NULL);
			if (status != CL_SUCCESS) {
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
	if (status != CL_SUCCESS) {
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
		clState->hasBitAlign = true;
		
	/* Check for OpenCL >= 1.0 support, needed for global offset parameter usage. */
	char * devoclver = malloc(1024);
	const char * ocl10 = "OpenCL 1.0";

	status = clGetDeviceInfo(devices[gpu], CL_DEVICE_VERSION, 1024, (void *)devoclver, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Failed to clGetDeviceInfo when trying to get CL_DEVICE_VERSION");
		return NULL;
	}
	find = strstr(devoclver, ocl10);
	if (!find)
		clState->hasOpenCL11plus = true;

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
	 * unless explicitly set on the command line. 79x0 cards perform
	 * better without vectors */
	if (clState->preferred_vwidth > 1) {
		if (strstr(name, "Tahiti"))
			clState->preferred_vwidth = 1;
		else
			clState->preferred_vwidth = 2;
	}

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
	char filename[255];
	char numbuf[10];

	if (chosen_kernel == KL_NONE) {
		if (strstr(name, "Tahiti") || !clState->hasBitAlign)
			clState->chosen_kernel = KL_POCLBM;
		else
			clState->chosen_kernel = KL_PHATK;
	} else
		clState->chosen_kernel = chosen_kernel;

	switch (clState->chosen_kernel) {
		case KL_POCLBM:
			strcpy(filename, POCLBM_KERNNAME".cl");
			strcpy(binaryfilename, POCLBM_KERNNAME);
			break;
		case KL_NONE: /* Shouldn't happen */
		case KL_PHATK:
			strcpy(filename, PHATK_KERNNAME".cl");
			strcpy(binaryfilename, PHATK_KERNNAME);
			break;
		case KL_DIAKGCN:
			strcpy(filename, DIAKGCN_KERNNAME".cl");
			strcpy(binaryfilename, DIAKGCN_KERNNAME);
			break;
		case KL_DIABLO:
			strcpy(filename, DIABLO_KERNNAME".cl");
			strcpy(binaryfilename, DIABLO_KERNNAME);
			break;
	}

	FILE *binaryfile;
	size_t *binary_sizes;
	char **binaries;
	int pl;
	char *source = file_contents(filename, &pl);
	size_t sourceSize[] = {(size_t)pl};

	if (!source)
		return NULL;

	binary_sizes = (size_t *)malloc(sizeof(size_t)*numDevices);
	if (unlikely(!binary_sizes)) {
		applog(LOG_ERR, "Unable to malloc binary_sizes");
		return NULL;
	}
	binaries = (char **)malloc(sizeof(char *)*numDevices);
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
		if (!binary_stat.st_size)
			goto build;

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
			free(binaries[gpu]);
			goto build;
		}

		clState->program = clCreateProgramWithBinary(clState->context, 1, &devices[gpu], &binary_sizes[gpu], (const unsigned char **)&binaries[gpu], &status, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithBinary)");
			fclose(binaryfile);
			free(binaries[gpu]);
			goto build;
		}
		fclose(binaryfile);
		if (opt_debug)
			applog(LOG_DEBUG, "Loaded binary image %s", binaryfilename);

		free(binaries[gpu]);
		goto built;
	}

	/////////////////////////////////////////////////////////////////
	// Load CL file, build CL program object, create CL kernel object
	/////////////////////////////////////////////////////////////////

build:
	clState->program = clCreateProgramWithSource(clState->context, 1, (const char **)&source, sourceSize, &status);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithSource)");
		return NULL;
	}

	clRetainProgram(clState->program);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Retaining Program (clRetainProgram)");
		return NULL;
	}

	/* create a cl program executable for all the devices specified */
	char *CompilerOptions = calloc(1, 256);

	sprintf(CompilerOptions, "-D WORKSIZE=%d -D VECTORS%d",
		(int)clState->work_size, clState->preferred_vwidth);
	if (opt_debug)
		applog(LOG_DEBUG, "Setting worksize to %d", clState->work_size);
	if (clState->preferred_vwidth > 1 && opt_debug)
		applog(LOG_DEBUG, "Patched source to suit %d vectors", clState->preferred_vwidth);

	if (clState->hasBitAlign) {
		strcat(CompilerOptions, " -D BITALIGN");
		if (opt_debug)
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
		    strstr(name, "WinterPark" ) ||
		    strstr(name, "BeaverCreek" ))
			patchbfi = true;
	} else if (opt_debug)
		applog(LOG_DEBUG, "cl_amd_media_ops not found, will not set BITALIGN");

	if (patchbfi) {
		strcat(CompilerOptions, " -D BFI_INT");
		if (opt_debug)
			applog(LOG_DEBUG, "BFI_INT patch requiring device found, patched source with BFI_INT");
	} else if (opt_debug)
		applog(LOG_DEBUG, "BFI_INT patch requiring device not found, will not BFI_INT patch");

	if (opt_debug)
		applog(LOG_DEBUG, "CompilerOptions: %s", CompilerOptions);
	status = clBuildProgram(clState->program, 1, &devices[gpu], CompilerOptions , NULL, NULL);
	free(CompilerOptions);

	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "Error: Building Program (clBuildProgram)");
		size_t logSize;
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

		char *log = malloc(logSize);
		status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
		applog(LOG_INFO, "%s", log);
		return NULL;
	}

	prog_built = true;

	status = clGetProgramInfo( clState->program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t)*numDevices, binary_sizes, NULL );
	if (unlikely(status != CL_SUCCESS)) {
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
	status = clGetProgramInfo( clState->program, CL_PROGRAM_BINARIES, sizeof(char *)*numDevices, binaries, NULL );
	if (unlikely(status != CL_SUCCESS)) {
		applog(LOG_ERR, "Error: Getting program info. (clGetPlatformInfo)");
		return NULL;
	}

	/* Patch the kernel if the hardware supports BFI_INT but it needs to
	 * be hacked in */
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
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Releasing program. (clReleaseProgram)");
			return NULL;
		}

		clState->program = clCreateProgramWithBinary(clState->context, 1, &devices[gpu], &binary_sizes[gpu], (const unsigned char **)&binaries[gpu], &status, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Loading Binary into cl_program (clCreateProgramWithBinary)");
			return NULL;
		}

		clRetainProgram(clState->program);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Retaining Program (clRetainProgram)");
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

	applog(LOG_INFO, "Initialising kernel %s with%s bitalign, %d vectors and worksize %d",
	       filename, clState->hasBitAlign ? "" : "out", clState->preferred_vwidth, clState->work_size);

	if (!prog_built) {
		/* create a cl program executable for all the devices specified */
		status = clBuildProgram(clState->program, 1, &devices[gpu], NULL, NULL, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Building Program (clBuildProgram)");
			size_t logSize;
			status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

			char *log = malloc(logSize);
			status = clGetProgramBuildInfo(clState->program, devices[gpu], CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
			applog(LOG_INFO, "%s", log);
			return NULL;
		}

		clRetainProgram(clState->program);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error: Retaining Program (clRetainProgram)");
			return NULL;
		}
	}

	/* get a kernel object handle for a kernel with the given name */
	clState->kernel = clCreateKernel(clState->program, "search", &status);
	if (status != CL_SUCCESS) {
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
	if (status != CL_SUCCESS) {
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

