#ifndef __ADL_H__
#define __ADL_H__
#ifdef HAVE_ADL
bool adl_active;
int opt_hysteresis;
const int opt_targettemp;
const int opt_overheattemp;
const int opt_cutofftemp;
void init_adl(int nDevs);
float gpu_temp(int gpu);
int gpu_engineclock(int gpu);
int gpu_memclock(int gpu);
float gpu_vddc(int gpu);
int gpu_activity(int gpu);
int gpu_fanspeed(int gpu);
int gpu_fanpercent(int gpu);
bool gpu_stats(int gpu, float *temp, int *engineclock, int *memclock, float *vddc,
	       int *activity, int *fanspeed, int *fanpercent, int *powertune);
void change_gpusettings(int gpu);
void gpu_autotune(int gpu, bool *enable);
void clear_adl(int nDevs);
#else /* HAVE_ADL */
#define adl_active (0)
static inline void init_adl(int nDevs) {}
static inline void change_gpusettings(int gpu) { }
static inline void clear_adl(int nDevs) {}
#endif
#endif
