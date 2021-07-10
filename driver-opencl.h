#ifndef BFG_DRIVER_OPENCL
#define BFG_DRIVER_OPENCL

#include <float.h>
#include <stdbool.h>

#include "CL/cl.h"
#ifdef HAVE_SENSORS
#include <sensors/sensors.h>
#endif

#include "miner.h"

enum opencl_binary_usage {
	OBU_DEFAULT  = 0,
	OBU_LOAD     = 1,
	OBU_SAVE     = 2,
	OBU_LOADSAVE = 3,
	OBU_NONE     = 4,
};

static const float intensity_not_set = FLT_MAX;

struct opencl_kernel_info;
struct _clState;

typedef cl_int (*queue_kernel_parameters_func_t)(const struct opencl_kernel_info *, struct _clState *, struct work *, cl_uint);

struct opencl_kernel_info {
	char *file;
	bool loaded;
	cl_program program;
	cl_kernel kernel;
	bool goffset;
	enum cl_kernels interface;
	size_t wsize;
	queue_kernel_parameters_func_t queue_kernel_parameters;
};

struct opencl_device_data {
	bool mapped;
	int virtual_gpu;
	int virtual_adl;
	unsigned long oclthreads;
	float intensity;
	char *_init_intensity;
	bool dynamic;
	
	enum bfg_tristate use_goffset;
	cl_uint vwidth;
	size_t work_size;
	cl_ulong max_alloc;
	
	struct opencl_kernel_info kernelinfo[POW_ALGORITHM_COUNT];
	
	enum opencl_binary_usage opt_opencl_binaries;
#ifdef USE_SCRYPT
	int lookup_gap;
	size_t thread_concurrency;
	size_t shaders;
#endif
	struct timeval tv_gpustart;
	int intervals;
	
#ifdef HAVE_ADL
	bool has_adl;
	struct gpu_adl adl;
	
	int gpu_engine;
	int min_engine;
	int gpu_fan;
	int min_fan;
	int gpu_memclock;
	int gpu_memdiff;
	int gpu_powertune;
	float gpu_vddc;
#endif
	
#ifdef HAVE_SENSORS
	const sensors_chip_name *sensor;
#endif
};

extern float opencl_proc_get_intensity(struct cgpu_info *, const char **iunit);
extern unsigned long xintensity_to_oclthreads(double xintensity, cl_uint max_compute_units);
extern bool opencl_set_intensity_from_str(struct cgpu_info *, const char *newvalue);

#ifdef USE_SHA256D
struct opencl_work_data {
	cl_uint ctx_a; cl_uint ctx_b; cl_uint ctx_c; cl_uint ctx_d;
	cl_uint ctx_e; cl_uint ctx_f; cl_uint ctx_g; cl_uint ctx_h;
	cl_uint cty_a; cl_uint cty_b; cl_uint cty_c; cl_uint cty_d;
	cl_uint cty_e; cl_uint cty_f; cl_uint cty_g; cl_uint cty_h;
	cl_uint merkle; cl_uint ntime; cl_uint nbits;
	cl_uint fW0; cl_uint fW1; cl_uint fW2; cl_uint fW3; cl_uint fW15;
	cl_uint fW01r; cl_uint fcty_e; cl_uint fcty_e2;
	cl_uint W16; cl_uint W17; cl_uint W2;
	cl_uint PreVal4; cl_uint T1;
	cl_uint C1addK5; cl_uint D1A; cl_uint W2A; cl_uint W17_2;
	cl_uint PreVal4addT1; cl_uint T1substate0;
	cl_uint PreVal4_2;
	cl_uint PreVal0;
	cl_uint PreW18;
	cl_uint PreW19;
	cl_uint PreW31;
	cl_uint PreW32;

	/* For diakgcn */
	cl_uint B1addK6, PreVal0addK7, W16addK16, W17addK17;
	cl_uint zeroA, zeroB;
	cl_uint oneA, twoA, threeA, fourA, fiveA, sixA, sevenA;
};
#endif

extern void opencl_early_init();
extern char *print_ndevs_and_exit(int *ndevs);
extern void *reinit_gpu(void *userdata);
extern char *set_gpu_map(char *arg);
extern const char *set_gpu_engine(char *arg);
extern const char *set_gpu_fan(char *arg);
extern const char *set_gpu_memclock(char *arg);
extern const char *set_gpu_memdiff(char *arg);
extern const char *set_gpu_powertune(char *arg);
extern const char *set_gpu_threads(char *arg);
extern const char *set_gpu_vddc(char *arg);
extern const char *set_temp_overheat(char *arg);
extern const char *set_intensity(char *arg);
extern const char *set_vector(char *arg);
extern const char *set_worksize(char *arg);
#ifdef USE_SCRYPT
extern const char *set_shaders(char *arg);
extern const char *set_lookup_gap(char *arg);
extern const char *set_thread_concurrency(char *arg);
#endif
extern enum cl_kernels select_kernel(const char *);
extern const char *opencl_get_kernel_interface_name(const enum cl_kernels);
extern const char *opencl_get_default_kernel_filename(const enum cl_kernels);
extern const char *set_kernel(char *arg);
extern void write_config_opencl(FILE *);
void manage_gpu(void);
extern void opencl_dynamic_cleanup();
extern void pause_dynamic_threads(int gpu);

extern bool have_opencl;
extern int opt_platform_id;
extern bool opt_opencl_binaries;

extern struct device_drv opencl_api;

#endif /* __DEVICE_GPU_H__ */
