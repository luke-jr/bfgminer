#ifndef __ADL_H__
#define __ADL_H__
bool adl_active;
#ifdef HAVE_ADL
int opt_hysteresis;
int opt_targettemp;
int opt_overheattemp;
void init_adl(int nDevs);
float gpu_temp(int gpu);
int gpu_engineclock(int gpu);
int gpu_memclock(int gpu);
float gpu_vddc(int gpu);
int gpu_activity(int gpu);
int gpu_fanspeed(int gpu);
int gpu_fanpercent(int gpu);
void change_gpusettings(int gpu);
void gpu_autotune(int gpu);
void clear_adl(int nDevs);
#else /* HAVE_ADL */
void init_adl(int nDevs) {}
void change_gpusettings(int gpu) { }
void gpu_autotune(int gpu) { }
void clear_adl(int nDevs) {}
#endif
#endif
