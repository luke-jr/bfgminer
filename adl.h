#ifndef BFG_ADL_H
#define BFG_ADL_H
#ifdef HAVE_ADL

#include <stdbool.h>

bool adl_active;
bool opt_reorder;
const int opt_targettemp;
const int opt_overheattemp;
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
void gpu_autotune(int gpu, enum dev_enable *denable);
void clear_adl(int nDevs);
#else /* HAVE_ADL */
#define adl_active (0)
static inline void init_adl(__maybe_unused int nDevs) {}
static inline void change_gpusettings(__maybe_unused int gpu) { }
static inline void clear_adl(__maybe_unused int nDevs) {}
#endif
#endif
