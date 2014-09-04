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

#ifdef HAVE_ADL

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_CURSES
// Must be before stdbool, since pdcurses typedefs bool :/
#include <curses.h>
#endif

#include <stdbool.h>

#include "driver-opencl.h"
#include "miner.h"
#include "ADL/adl_sdk.h"
#include "compat.h"

#ifndef WIN32
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#else /* WIN32 */
#include <windows.h>
#include <tchar.h>
#endif
#include "adl_functions.h"

#ifndef __stdcall
#define __stdcall
#endif

#ifndef HAVE_CURSES
#define wlogprint(...)  applog(LOG_WARNING, __VA_ARGS__)
#endif

bool adl_active;
bool opt_reorder = false;

static
const int opt_targettemp = 75;
const int opt_overheattemp = 85;
static pthread_mutex_t adl_lock;

struct gpu_adapters {
	int iAdapterIndex;
	int iBusNumber;
	int virtual_gpu;
	int id;
};

// Memory allocation function
static void * __stdcall ADL_Main_Memory_Alloc(int iSize)
{
	void *lpBuffer = malloc(iSize);

	return lpBuffer;
}

// Optional Memory de-allocation function
static void __stdcall ADL_Main_Memory_Free (void **lpBuffer)
{
	if (*lpBuffer) {
		free (*lpBuffer);
		*lpBuffer = NULL;
	}
}

#ifndef WIN32
// equivalent functions in linux
static void *GetProcAddress(void *pLibrary, const char *name)
{
	return dlsym( pLibrary, name);
}
#endif

static	ADL_MAIN_CONTROL_CREATE		ADL_Main_Control_Create;
static	ADL_MAIN_CONTROL_DESTROY	ADL_Main_Control_Destroy;
static	ADL_ADAPTER_NUMBEROFADAPTERS_GET	ADL_Adapter_NumberOfAdapters_Get;
static	ADL_ADAPTER_ADAPTERINFO_GET	ADL_Adapter_AdapterInfo_Get;
static	ADL_ADAPTER_ID_GET		ADL_Adapter_ID_Get;
static	ADL_OVERDRIVE5_TEMPERATURE_GET	ADL_Overdrive5_Temperature_Get;
static	ADL_OVERDRIVE5_CURRENTACTIVITY_GET	ADL_Overdrive5_CurrentActivity_Get;
static	ADL_OVERDRIVE5_ODPARAMETERS_GET	ADL_Overdrive5_ODParameters_Get;
static	ADL_OVERDRIVE5_FANSPEEDINFO_GET	ADL_Overdrive5_FanSpeedInfo_Get;
static	ADL_OVERDRIVE5_FANSPEED_GET	ADL_Overdrive5_FanSpeed_Get;
static	ADL_OVERDRIVE5_FANSPEED_SET	ADL_Overdrive5_FanSpeed_Set;
static	ADL_OVERDRIVE5_ODPERFORMANCELEVELS_GET	ADL_Overdrive5_ODPerformanceLevels_Get;
static	ADL_OVERDRIVE5_ODPERFORMANCELEVELS_SET	ADL_Overdrive5_ODPerformanceLevels_Set;
static	ADL_MAIN_CONTROL_REFRESH	ADL_Main_Control_Refresh;
static	ADL_OVERDRIVE5_POWERCONTROL_GET	ADL_Overdrive5_PowerControl_Get;
static	ADL_OVERDRIVE5_POWERCONTROL_SET	ADL_Overdrive5_PowerControl_Set;
static	ADL_OVERDRIVE5_FANSPEEDTODEFAULT_SET	ADL_Overdrive5_FanSpeedToDefault_Set;

#ifndef WIN32
	static void *hDLL;	// Handle to .so library
#else /* WIN32 */
	HINSTANCE hDLL;		// Handle to DLL
#endif
static int iNumberAdapters;
static LPAdapterInfo lpInfo = NULL;

int set_fanspeed(int gpu, int iFanSpeed);
static float __gpu_temp(struct gpu_adl *ga);

static inline void lock_adl(void)
{
	mutex_lock(&adl_lock);
}

static inline void unlock_adl(void)
{
	mutex_unlock(&adl_lock);
}

/* This looks for the twin GPU that has the fanspeed control of a non fanspeed
 * control GPU on dual GPU cards */
static bool fanspeed_twin(struct gpu_adl *ga, struct gpu_adl *other_ga)
{
	if (!other_ga->has_fanspeed)
		return false;
	if (abs(ga->iBusNumber - other_ga->iBusNumber) != 1)
		return false;
	if (strcmp(ga->strAdapterName, other_ga->strAdapterName))
		return false;
	return true;
}

static bool prepare_adl(void)
{
	int result;

#if defined(WIN32) || defined(__CYGWIN__)
#	ifdef __CYGWIN__
#		define LoadLibrary(x) dlopen(x, RTLD_LAZY|RTLD_GLOBAL);
#	endif
	hDLL = LoadLibrary("atiadlxx.dll");
	if (hDLL == NULL)
		// A 32 bit calling application on 64 bit OS will fail to LoadLIbrary.
		// Try to load the 32 bit library (atiadlxy.dll) instead
		hDLL = LoadLibrary("atiadlxy.dll");
#else
	hDLL = dlopen( "libatiadlxx.so", RTLD_LAZY|RTLD_GLOBAL);
#endif
	if (hDLL == NULL) {
		applog(LOG_INFO, "Unable to load ati adl library");
		return false;
	}
	ADL_Main_Control_Create = (ADL_MAIN_CONTROL_CREATE) GetProcAddress(hDLL,"ADL_Main_Control_Create");
	ADL_Main_Control_Destroy = (ADL_MAIN_CONTROL_DESTROY) GetProcAddress(hDLL,"ADL_Main_Control_Destroy");
	ADL_Adapter_NumberOfAdapters_Get = (ADL_ADAPTER_NUMBEROFADAPTERS_GET) GetProcAddress(hDLL,"ADL_Adapter_NumberOfAdapters_Get");
	ADL_Adapter_AdapterInfo_Get = (ADL_ADAPTER_ADAPTERINFO_GET) GetProcAddress(hDLL,"ADL_Adapter_AdapterInfo_Get");
	ADL_Adapter_ID_Get = (ADL_ADAPTER_ID_GET) GetProcAddress(hDLL,"ADL_Adapter_ID_Get");
	ADL_Overdrive5_Temperature_Get = (ADL_OVERDRIVE5_TEMPERATURE_GET) GetProcAddress(hDLL,"ADL_Overdrive5_Temperature_Get");
	ADL_Overdrive5_CurrentActivity_Get = (ADL_OVERDRIVE5_CURRENTACTIVITY_GET) GetProcAddress(hDLL, "ADL_Overdrive5_CurrentActivity_Get");
	ADL_Overdrive5_ODParameters_Get = (ADL_OVERDRIVE5_ODPARAMETERS_GET) GetProcAddress(hDLL, "ADL_Overdrive5_ODParameters_Get");
	ADL_Overdrive5_FanSpeedInfo_Get = (ADL_OVERDRIVE5_FANSPEEDINFO_GET) GetProcAddress(hDLL, "ADL_Overdrive5_FanSpeedInfo_Get");
	ADL_Overdrive5_FanSpeed_Get = (ADL_OVERDRIVE5_FANSPEED_GET) GetProcAddress(hDLL, "ADL_Overdrive5_FanSpeed_Get");
	ADL_Overdrive5_FanSpeed_Set = (ADL_OVERDRIVE5_FANSPEED_SET) GetProcAddress(hDLL, "ADL_Overdrive5_FanSpeed_Set");
	ADL_Overdrive5_ODPerformanceLevels_Get = (ADL_OVERDRIVE5_ODPERFORMANCELEVELS_GET) GetProcAddress(hDLL, "ADL_Overdrive5_ODPerformanceLevels_Get");
	ADL_Overdrive5_ODPerformanceLevels_Set = (ADL_OVERDRIVE5_ODPERFORMANCELEVELS_SET) GetProcAddress(hDLL, "ADL_Overdrive5_ODPerformanceLevels_Set");
	ADL_Main_Control_Refresh = (ADL_MAIN_CONTROL_REFRESH) GetProcAddress(hDLL, "ADL_Main_Control_Refresh");
	ADL_Overdrive5_PowerControl_Get = (ADL_OVERDRIVE5_POWERCONTROL_GET) GetProcAddress(hDLL, "ADL_Overdrive5_PowerControl_Get");
	ADL_Overdrive5_PowerControl_Set = (ADL_OVERDRIVE5_POWERCONTROL_SET) GetProcAddress(hDLL, "ADL_Overdrive5_PowerControl_Set");
	ADL_Overdrive5_FanSpeedToDefault_Set = (ADL_OVERDRIVE5_FANSPEEDTODEFAULT_SET) GetProcAddress(hDLL, "ADL_Overdrive5_FanSpeedToDefault_Set");

	if (!ADL_Main_Control_Create || !ADL_Main_Control_Destroy ||
		!ADL_Adapter_NumberOfAdapters_Get || !ADL_Adapter_AdapterInfo_Get ||
		!ADL_Adapter_ID_Get || !ADL_Overdrive5_Temperature_Get ||
		!ADL_Overdrive5_CurrentActivity_Get ||
		!ADL_Overdrive5_ODParameters_Get || !ADL_Overdrive5_FanSpeedInfo_Get ||
		!ADL_Overdrive5_FanSpeed_Get || !ADL_Overdrive5_FanSpeed_Set ||
		!ADL_Overdrive5_ODPerformanceLevels_Get || !ADL_Overdrive5_ODPerformanceLevels_Set ||
		!ADL_Main_Control_Refresh || !ADL_Overdrive5_PowerControl_Get ||
		!ADL_Overdrive5_PowerControl_Set || !ADL_Overdrive5_FanSpeedToDefault_Set) {
			applog(LOG_WARNING, "ATI ADL's API is missing");
		return false;
	}

	// Initialise ADL. The second parameter is 1, which means:
	// retrieve adapter information only for adapters that are physically present and enabled in the system
	result = ADL_Main_Control_Create (ADL_Main_Memory_Alloc, 1);
	if (result != ADL_OK) {
		applog(LOG_INFO, "ADL Initialisation Error! Error %d!", result);
		return false;
	}

	result = ADL_Main_Control_Refresh();
	if (result != ADL_OK) {
		applog(LOG_INFO, "ADL Refresh Error! Error %d!", result);
		return false;
	}

	return true;
}

void init_adl(int nDevs)
{
	int result, i, j, devices = 0, last_adapter = -1, gpu = 0, dummy = 0;
	struct gpu_adapters adapters[MAX_GPUDEVICES], vadapters[MAX_GPUDEVICES];
	bool devs_match = true;

	if (unlikely(pthread_mutex_init(&adl_lock, NULL))) {
		applog(LOG_ERR, "Failed to init adl_lock in init_adl");
		return;
	}

	if (!prepare_adl())
		return;

	// Obtain the number of adapters for the system
	result = ADL_Adapter_NumberOfAdapters_Get (&iNumberAdapters);
	if (result != ADL_OK) {
		applog(LOG_INFO, "Cannot get the number of adapters! Error %d!", result);
		return ;
	}

	if (iNumberAdapters > 0) {
		lpInfo = malloc ( sizeof (AdapterInfo) * iNumberAdapters );
		memset ( lpInfo,'\0', sizeof (AdapterInfo) * iNumberAdapters );

		lpInfo->iSize = sizeof(lpInfo);
		// Get the AdapterInfo structure for all adapters in the system
		result = ADL_Adapter_AdapterInfo_Get (lpInfo, sizeof (AdapterInfo) * iNumberAdapters);
		if (result != ADL_OK) {
			applog(LOG_INFO, "ADL_Adapter_AdapterInfo_Get Error! Error %d", result);
			return ;
		}
	} else {
		applog(LOG_INFO, "No adapters found");
		return;
	}

	/* Iterate over iNumberAdapters and find the lpAdapterID of real devices */
	for (i = 0; i < iNumberAdapters; i++) {
		int iAdapterIndex;
		int lpAdapterID;

		iAdapterIndex = lpInfo[i].iAdapterIndex;
		/* Get unique identifier of the adapter, 0 means not AMD */
		result = ADL_Adapter_ID_Get(iAdapterIndex, &lpAdapterID);
		if (result != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Adapter_ID_Get. Error %d", result);
			if (result == -10)
				applog(LOG_INFO, "This error says the device is not enabled");
		}
		else
		/* Each adapter may have multiple entries */
		if (lpAdapterID == last_adapter)
			continue;
		else
		if (!lpAdapterID)
			applog(LOG_INFO, "Adapter returns ID 0 meaning not AMD. Card order might be confused");
		else
			last_adapter = lpAdapterID;

		applog(LOG_DEBUG, "GPU %d "
		       "iAdapterIndex %d "
		       "strUDID %s "
		       "iBusNumber %d "
		       "iDeviceNumber %d "
		       "iFunctionNumber %d "
		       "iVendorID %d "
		       "strAdapterName  %s ",
		       devices,
		       iAdapterIndex,
		       lpInfo[i].strUDID,
		       lpInfo[i].iBusNumber,
		       lpInfo[i].iDeviceNumber,
		       lpInfo[i].iFunctionNumber,
		       lpInfo[i].iVendorID,
		       lpInfo[i].strAdapterName);

		adapters[devices].iAdapterIndex = iAdapterIndex;
		adapters[devices].iBusNumber = lpInfo[i].iBusNumber;
		adapters[devices].id = i;

		/* We found a truly new adapter instead of a logical
		 * one. Now since there's no way of correlating the
		 * opencl enumerated devices and the ADL enumerated
		 * ones, we have to assume they're in the same order.*/
		if (++devices > nDevs && devs_match) {
			applog(LOG_ERR, "ADL found more devices than opencl!");
			applog(LOG_ERR, "There is possibly at least one GPU that doesn't support OpenCL");
			applog(LOG_ERR, "Use the gpu map feature to reliably map OpenCL to ADL");
			devs_match = false;
		}
	}

	if (devices < nDevs) {
		applog(LOG_ERR, "ADL found less devices than opencl!");
		applog(LOG_ERR, "There is possibly more than one display attached to a GPU");
		applog(LOG_ERR, "Use the gpu map feature to reliably map OpenCL to ADL");
		devs_match = false;
	}

	for (i = 0; i < devices; i++) {
		vadapters[i].virtual_gpu = i;
		vadapters[i].id = adapters[i].id;
	}

	/* Apply manually provided OpenCL to ADL mapping, if any */
	for (i = 0; i < nDevs; i++) {
		struct opencl_device_data * const data = gpus[i].device_data;
		if (data->mapped) {
			vadapters[data->virtual_adl].virtual_gpu = i;
			applog(LOG_INFO, "Mapping OpenCL device %d to ADL device %d", i, data->virtual_adl);
		} else
			data->virtual_adl = i;
	}

	if (!devs_match) {
		applog(LOG_ERR, "WARNING: Number of OpenCL and ADL devices did not match!");
		applog(LOG_ERR, "Hardware monitoring may NOT match up with devices!");
	} else if (opt_reorder) {
		/* Windows has some kind of random ordering for bus number IDs and
		 * ordering the GPUs according to ascending order fixes it. Linux
		 * has usually sequential but decreasing order instead! */
		for (i = 0; i < devices; i++) {
			int j, virtual_gpu;

			virtual_gpu = 0;
			for (j = 0; j < devices; j++) {
				if (i == j)
					continue;
#ifdef WIN32
				if (adapters[j].iBusNumber < adapters[i].iBusNumber)
#else
				if (adapters[j].iBusNumber > adapters[i].iBusNumber)
#endif
					virtual_gpu++;
			}
			if (virtual_gpu != i) {
				applog(LOG_INFO, "Mapping device %d to GPU %d according to Bus Number order",
				       i, virtual_gpu);
				vadapters[virtual_gpu].virtual_gpu = i;
				vadapters[virtual_gpu].id = adapters[i].id;
			}
		}
	}

	if (devices > nDevs)
		devices = nDevs;

	for (gpu = 0; gpu < devices; gpu++) {
		struct cgpu_info * const cgpu = &gpus[gpu];
		struct opencl_device_data * const data = cgpu->device_data;
		struct gpu_adl *ga;
		int iAdapterIndex;
		int lpAdapterID;
		ADLODPerformanceLevels *lpOdPerformanceLevels;
		int lev, adlGpu;

		adlGpu = data->virtual_adl;
		i = vadapters[adlGpu].id;
		iAdapterIndex = lpInfo[i].iAdapterIndex;
		data->virtual_gpu = vadapters[adlGpu].virtual_gpu;

		/* Get unique identifier of the adapter, 0 means not AMD */
		result = ADL_Adapter_ID_Get(iAdapterIndex, &lpAdapterID);
		if (result != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Adapter_ID_Get. Error %d", result);
			lpAdapterID = -1;
		}

		if (gpus[gpu].deven == DEV_DISABLED) {
			data->gpu_engine =
			data->gpu_memclock =
			data->gpu_vddc =
			data->gpu_fan =
			data->gpu_powertune = 0;
			continue;
		}

		applog(LOG_INFO, "GPU %d %s hardware monitoring enabled", gpu, lpInfo[i].strAdapterName);
		if (gpus[gpu].name)
			free((void*)gpus[gpu].name);
		if (lpInfo[i].strAdapterName)
			gpus[gpu].name = trimmed_strdup(lpInfo[i].strAdapterName);
		data->has_adl = true;
		/* Flag adl as active if any card is successfully activated */
		adl_active = true;

		/* From here on we know this device is a discrete device and
		 * should support ADL */
		ga = &data->adl;
		ga->gpu = gpu;
		ga->iAdapterIndex = iAdapterIndex;
		ga->lpAdapterID = lpAdapterID;
		strcpy(ga->strAdapterName, lpInfo[i].strAdapterName);
		ga->DefPerfLev = NULL;
		ga->twin = NULL;

		ga->lpOdParameters.iSize = sizeof(ADLODParameters);
		if (ADL_Overdrive5_ODParameters_Get(iAdapterIndex, &ga->lpOdParameters) != ADL_OK)
			applog(LOG_INFO, "Failed to ADL_Overdrive5_ODParameters_Get");

		lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
		/* We're only interested in the top performance level */
		lpOdPerformanceLevels = malloc(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
		lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;

		/* Get default performance levels first */
		if (ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 1, lpOdPerformanceLevels) != ADL_OK)
			applog(LOG_INFO, "Failed to ADL_Overdrive5_ODPerformanceLevels_Get");
		/* Set the limits we'd use based on default gpu speeds */
		ga->maxspeed = ga->minspeed = lpOdPerformanceLevels->aLevels[lev].iEngineClock;

		ga->lpTemperature.iSize = sizeof(ADLTemperature);
		ga->lpFanSpeedInfo.iSize = sizeof(ADLFanSpeedInfo);
		ga->lpFanSpeedValue.iSize = ga->DefFanSpeedValue.iSize = sizeof(ADLFanSpeedValue);
		/* Now get the current performance levels for any existing overclock */
		ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 0, lpOdPerformanceLevels);
		/* Save these values as the defaults in case we wish to reset to defaults */
		ga->DefPerfLev = lpOdPerformanceLevels;

		if (data->gpu_engine) {
			int setengine = data->gpu_engine * 100;

			/* Lower profiles can't have a higher setting */
			for (j = 0; j < lev; j++) {
				if (lpOdPerformanceLevels->aLevels[j].iEngineClock > setengine)
					lpOdPerformanceLevels->aLevels[j].iEngineClock = setengine;
			}
			lpOdPerformanceLevels->aLevels[lev].iEngineClock = setengine;
			applog(LOG_INFO, "Setting GPU %d engine clock to %d", gpu, data->gpu_engine);
			ADL_Overdrive5_ODPerformanceLevels_Set(iAdapterIndex, lpOdPerformanceLevels);
			ga->maxspeed = setengine;
			if (data->min_engine)
				ga->minspeed = data->min_engine * 100;
			ga->managed = true;
			if (data->gpu_memdiff)
				set_memoryclock(gpu, data->gpu_engine + data->gpu_memdiff);
		}

		if (data->gpu_memclock) {
			int setmem = data->gpu_memclock * 100;

			for (j = 0; j < lev; j++) {
				if (lpOdPerformanceLevels->aLevels[j].iMemoryClock > setmem)
					lpOdPerformanceLevels->aLevels[j].iMemoryClock = setmem;
			}
			lpOdPerformanceLevels->aLevels[lev].iMemoryClock = setmem;
			applog(LOG_INFO, "Setting GPU %d memory clock to %d", gpu, data->gpu_memclock);
			ADL_Overdrive5_ODPerformanceLevels_Set(iAdapterIndex, lpOdPerformanceLevels);
			ga->managed = true;
		}

		if (data->gpu_vddc) {
			int setv = data->gpu_vddc * 1000;

			for (j = 0; j < lev; j++) {
				if (lpOdPerformanceLevels->aLevels[j].iVddc > setv)
					lpOdPerformanceLevels->aLevels[j].iVddc = setv;
			}
			lpOdPerformanceLevels->aLevels[lev].iVddc = setv;
			applog(LOG_INFO, "Setting GPU %d voltage to %.3f", gpu, data->gpu_vddc);
			ADL_Overdrive5_ODPerformanceLevels_Set(iAdapterIndex, lpOdPerformanceLevels);
			ga->managed = true;
		}

		ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 0, lpOdPerformanceLevels);
		ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
		ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
		ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
		ga->iBusNumber = lpInfo[i].iBusNumber;

		if (ADL_Overdrive5_FanSpeedInfo_Get(iAdapterIndex, 0, &ga->lpFanSpeedInfo) != ADL_OK)
			applog(LOG_INFO, "Failed to ADL_Overdrive5_FanSpeedInfo_Get");
		else
			ga->has_fanspeed = true;

		/* Save the fanspeed values as defaults in case we reset later */
		ga->DefFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
		ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->DefFanSpeedValue);
		if (data->gpu_fan)
			set_fanspeed(gpu, data->gpu_fan);
		else
			data->gpu_fan = 85; /* Set a nominal upper limit of 85% */

		/* Not fatal if powercontrol get fails */
		if (ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy) != ADL_OK)
			applog(LOG_INFO, "Failed to ADL_Overdrive5_PowerControl_get");

		if (data->gpu_powertune) {
			ADL_Overdrive5_PowerControl_Set(ga->iAdapterIndex, data->gpu_powertune);
			ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy);
			ga->managed = true;
		}

		/* Set some default temperatures for autotune when enabled */
		if (!gpus[gpu].targettemp)
			gpus[gpu].targettemp = opt_targettemp;
		if (!ga->overtemp)
			ga->overtemp = opt_overheattemp;
		if (!gpus[gpu].cutofftemp)
			gpus[gpu].cutofftemp = opt_cutofftemp;
		if (opt_autofan) {
			/* Set a safe starting default if we're automanaging fan speeds */
			int nominal = 50;

			ga->autofan = true;
			/* Clamp fanspeed values to range provided */
			if (nominal > data->gpu_fan)
				nominal = data->gpu_fan;
			if (nominal < data->min_fan)
				nominal = data->min_fan;
			set_fanspeed(gpu, nominal);
		}
		if (opt_autoengine) {
			ga->autoengine = true;
			ga->managed = true;
		}
		gpus[gpu].temp =
		ga->lasttemp = __gpu_temp(ga);
	}

	for (gpu = 0; gpu < devices; gpu++) {
		struct cgpu_info * const cgpu = &gpus[gpu];
		struct opencl_device_data * const data = cgpu->device_data;
		struct gpu_adl *ga = &data->adl;
		int j;

		for (j = 0; j < devices; j++) {
			struct cgpu_info * const other_cgpu = &gpus[j];
			struct opencl_device_data * const other_data = other_cgpu->device_data;
			struct gpu_adl *other_ga;

			if (j == gpu)
				continue;

			other_ga = &other_data->adl;

			/* Search for twin GPUs on a single card. They will be
			 * separated by one bus id and one will have fanspeed
			 * while the other won't. */
			if (!ga->has_fanspeed) {
				if (fanspeed_twin(ga, other_ga)) {
					applog(LOG_INFO, "Dual GPUs detected: %d and %d",
						ga->gpu, other_ga->gpu);
					ga->twin = other_ga;
					other_ga->twin = ga;
				}
			}
		}
	}
}

static float __gpu_temp(struct gpu_adl *ga)
{
	if (ADL_Overdrive5_Temperature_Get(ga->iAdapterIndex, 0, &ga->lpTemperature) != ADL_OK)
		return -1;
	return (float)ga->lpTemperature.iTemperature / 1000;
}

float gpu_temp(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	float ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	ret = __gpu_temp(ga);
	unlock_adl();
	gpus[gpu].temp = ret;
	return ret;
}

static inline int __gpu_engineclock(struct gpu_adl *ga)
{
	return ga->lpActivity.iEngineClock / 100;
}

int gpu_engineclock(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		goto out;
	ret = __gpu_engineclock(ga);
out:
	unlock_adl();
	return ret;
}

static inline int __gpu_memclock(struct gpu_adl *ga)
{
	return ga->lpActivity.iMemoryClock / 100;
}

int gpu_memclock(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		goto out;
	ret = __gpu_memclock(ga);
out:
	unlock_adl();
	return ret;
}

static inline float __gpu_vddc(struct gpu_adl *ga)
{
	return (float)ga->lpActivity.iVddc / 1000;
}

float gpu_vddc(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	float ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		goto out;
	ret = __gpu_vddc(ga);
out:
	unlock_adl();
	return ret;
}

static inline int __gpu_activity(struct gpu_adl *ga)
{
	if (!ga->lpOdParameters.iActivityReportingSupported)
		return -1;
	return ga->lpActivity.iActivityPercent;
}

int gpu_activity(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	ret = ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity);
	unlock_adl();
	if (ret != ADL_OK)
		return ret;
	if (!ga->lpOdParameters.iActivityReportingSupported)
		return ret;
	return ga->lpActivity.iActivityPercent;
}

static inline int __gpu_fanspeed(struct gpu_adl *ga)
{
	if (!ga->has_fanspeed && ga->twin)
		return __gpu_fanspeed(ga->twin);

	if (!(ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_RPM_READ))
		return -1;
	ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return -1;
	return ga->lpFanSpeedValue.iFanSpeed;
}

int gpu_fanspeed(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	ret = __gpu_fanspeed(ga);
	unlock_adl();
	return ret;
}

static int __gpu_fanpercent(struct gpu_adl *ga)
{
	if (!ga->has_fanspeed && ga->twin)
		return __gpu_fanpercent(ga->twin);

	if (!(ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_PERCENT_READ ))
		return -1;
	ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return -1;
	return ga->lpFanSpeedValue.iFanSpeed;
}

int gpu_fanpercent(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	ret = __gpu_fanpercent(ga);
	unlock_adl();
	return ret;
}

static inline int __gpu_powertune(struct gpu_adl *ga)
{
	int dummy = 0;

	if (ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy) != ADL_OK)
		return -1;
	return ga->iPercentage;
}

int gpu_powertune(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = -1;

	if (!data->has_adl || !adl_active)
		return ret;

	ga = &data->adl;
	lock_adl();
	ret = __gpu_powertune(ga);
	unlock_adl();
	return ret;
}

bool gpu_stats(int gpu, float *temp, int *engineclock, int *memclock, float *vddc,
	       int *activity, int *fanspeed, int *fanpercent, int *powertune)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active)
		return false;

	ga = &data->adl;

	lock_adl();
	gpus[gpu].temp =
	*temp = __gpu_temp(ga);
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK) {
		*engineclock = 0;
		*memclock = 0;
		*vddc = 0;
		*activity = 0;
	} else {
		*engineclock = __gpu_engineclock(ga);
		*memclock = __gpu_memclock(ga);
		*vddc = __gpu_vddc(ga);
		*activity = __gpu_activity(ga);
	}
	*fanspeed = __gpu_fanspeed(ga);
	*fanpercent = __gpu_fanpercent(ga);
	*powertune = __gpu_powertune(ga);
	unlock_adl();

	return true;
}

#ifdef HAVE_CURSES
static void get_enginerange(int gpu, int *imin, int *imax)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Get enginerange not supported\n");
		return;
	}
	ga = &data->adl;
	*imin = ga->lpOdParameters.sEngineClock.iMin / 100;
	*imax = ga->lpOdParameters.sEngineClock.iMax / 100;
}
#endif

int set_engineclock(int gpu, int iEngineClock)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	ADLODPerformanceLevels *lpOdPerformanceLevels;
	int i, lev, ret = 1;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Set engineclock not supported\n");
		return ret;
	}

	iEngineClock *= 100;
	ga = &data->adl;

	/* Keep track of intended engine clock in case the device changes
	 * profile and drops while idle, not taking the new engine clock */
	ga->lastengine = iEngineClock;

	lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
	lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
	lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;

	lock_adl();
	if (ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK)
		goto out;
	for (i = 0; i < lev; i++) {
		if (lpOdPerformanceLevels->aLevels[i].iEngineClock > iEngineClock)
			lpOdPerformanceLevels->aLevels[i].iEngineClock = iEngineClock;
	}
	lpOdPerformanceLevels->aLevels[lev].iEngineClock = iEngineClock;
	ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels);
	ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
	if (lpOdPerformanceLevels->aLevels[lev].iEngineClock == iEngineClock)
		ret = 0;
	ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
	if (ga->iEngineClock > ga->maxspeed)
		ga->maxspeed = ga->iEngineClock;
	if (ga->iEngineClock < ga->minspeed)
		ga->minspeed = ga->iEngineClock;
	ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
	ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
	ga->managed = true;
out:
	unlock_adl();

	if (data->gpu_memdiff)
		set_memoryclock(gpu, iEngineClock / 100 + data->gpu_memdiff);

	return ret;
}

#ifdef HAVE_CURSES
static void get_memoryrange(int gpu, int *imin, int *imax)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Get memoryrange not supported\n");
		return;
	}
	ga = &data->adl;
	*imin = ga->lpOdParameters.sMemoryClock.iMin / 100;
	*imax = ga->lpOdParameters.sMemoryClock.iMax / 100;
}
#endif

int set_memoryclock(int gpu, int iMemoryClock)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	ADLODPerformanceLevels *lpOdPerformanceLevels;
	int i, lev, ret = 1;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Set memoryclock not supported\n");
		return ret;
	}

	data->gpu_memclock = iMemoryClock;

	iMemoryClock *= 100;
	ga = &data->adl;

	lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
	lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
	lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;

	lock_adl();
	if (ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK)
		goto out;
	lpOdPerformanceLevels->aLevels[lev].iMemoryClock = iMemoryClock;
	for (i = 0; i < lev; i++) {
		if (lpOdPerformanceLevels->aLevels[i].iMemoryClock > iMemoryClock)
			lpOdPerformanceLevels->aLevels[i].iMemoryClock = iMemoryClock;
	}
	ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels);
	ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
	if (lpOdPerformanceLevels->aLevels[lev].iMemoryClock == iMemoryClock)
		ret = 0;
	ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
	ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
	ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
	ga->managed = true;
out:
	unlock_adl();
	return ret;
}

#ifdef HAVE_CURSES
static void get_vddcrange(int gpu, float *imin, float *imax)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Get vddcrange not supported\n");
		return;
	}
	ga = &data->adl;
	*imin = (float)ga->lpOdParameters.sVddc.iMin / 1000;
	*imax = (float)ga->lpOdParameters.sVddc.iMax / 1000;
}

static float curses_float(const char *query)
{
	float ret;
	char *cvar;

	cvar = curses_input(query);
	if (unlikely(!cvar))
		return -1;
	ret = atof(cvar);
	free(cvar);
	return ret;
}
#endif

int set_vddc(int gpu, float fVddc)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	ADLODPerformanceLevels *lpOdPerformanceLevels;
	int i, iVddc, lev, ret = 1;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Set vddc not supported\n");
		return ret;
	}

	iVddc = 1000 * fVddc;
	ga = &data->adl;

	lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
	lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
	lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;

	lock_adl();
	if (ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK)
		goto out;
	for (i = 0; i < lev; i++) {
		if (lpOdPerformanceLevels->aLevels[i].iVddc > iVddc)
			lpOdPerformanceLevels->aLevels[i].iVddc = iVddc;
	}
	lpOdPerformanceLevels->aLevels[lev].iVddc = iVddc;
	ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels);
	ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
	if (lpOdPerformanceLevels->aLevels[lev].iVddc == iVddc)
		ret = 0;
	ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
	ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
	ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
	ga->managed = true;
out:
	unlock_adl();
	return ret;
}

static void get_fanrange(int gpu, int *imin, int *imax)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;

	if (!data->has_adl || !adl_active) {
		wlogprint("Get fanrange not supported\n");
		return;
	}
	ga = &data->adl;
	*imin = ga->lpFanSpeedInfo.iMinPercent;
	*imax = ga->lpFanSpeedInfo.iMaxPercent;
}

int set_fanspeed(int gpu, int iFanSpeed)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int ret = 1;

	if (!data->has_adl || !adl_active) {
		wlogprint("Set fanspeed not supported\n");
		return ret;
	}

	ga = &data->adl;
	if (!(ga->lpFanSpeedInfo.iFlags & (ADL_DL_FANCTRL_SUPPORTS_RPM_WRITE | ADL_DL_FANCTRL_SUPPORTS_PERCENT_WRITE ))) {
		applog(LOG_DEBUG, "GPU %d doesn't support rpm or percent write", gpu);
		return ret;
	}

	/* Store what fanspeed we're actually aiming for for re-entrant changes
	 * in case this device does not support fine setting changes */
	ga->targetfan = iFanSpeed;

	lock_adl();
	ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK) {
		applog(LOG_DEBUG, "GPU %d call to fanspeed get failed", gpu);
	}
	if (!(ga->lpFanSpeedValue.iFlags & ADL_DL_FANCTRL_FLAG_USER_DEFINED_SPEED)) {
		/* If user defined is not already specified, set it first */
		ga->lpFanSpeedValue.iFlags |= ADL_DL_FANCTRL_FLAG_USER_DEFINED_SPEED;
		ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue);
	}
	if (!(ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_PERCENT_WRITE)) {
		/* Must convert speed to an RPM */
		iFanSpeed = ga->lpFanSpeedInfo.iMaxRPM * iFanSpeed / 100;
		ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
	} else
		ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
	ga->lpFanSpeedValue.iFanSpeed = iFanSpeed;
	ret = ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue);
	ga->managed = true;
	unlock_adl();

	return ret;
}

#ifdef HAVE_CURSES
static int set_powertune(int gpu, int iPercentage)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	int dummy, ret = 1;

	if (!data->has_adl || !adl_active) {
		wlogprint("Set powertune not supported\n");
		return ret;
	}

	ga = &data->adl;

	lock_adl();
	ADL_Overdrive5_PowerControl_Set(ga->iAdapterIndex, iPercentage);
	ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy);
	if (ga->iPercentage == iPercentage)
		ret = 0;
	ga->managed = true;
	unlock_adl();
	return ret;
}
#endif

/* Returns whether the fanspeed is optimal already or not. The fan_window bool
 * tells us whether the current fanspeed is in the target range for fanspeeds.
 */
static bool fan_autotune(int gpu, int temp, int fanpercent, int lasttemp, bool *fan_window)
{
	struct cgpu_info *cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	int tdiff = round(temp - lasttemp);
	struct gpu_adl *ga = &data->adl;
	const int top = data->gpu_fan;
	const int bot = data->min_fan;
	int newpercent = fanpercent;
	int iMin = 0, iMax = 100;

	get_fanrange(gpu, &iMin, &iMax);
	if (temp > ga->overtemp && fanpercent < iMax) {
		applog(LOG_WARNING, "Overheat detected on GPU %d, increasing fan to 100%%", gpu);
		newpercent = iMax;

		dev_error(cgpu, REASON_DEV_OVER_HEAT);
	} else if (temp > gpus[gpu].targettemp && fanpercent < top && tdiff >= 0) {
		applog(LOG_DEBUG, "Temperature over target, increasing fanspeed");
		if (temp > gpus[gpu].targettemp + opt_hysteresis)
			newpercent = ga->targetfan + 10;
		else
			newpercent = ga->targetfan + 5;
		if (newpercent > top)
			newpercent = top;
	} else if (fanpercent > bot && temp < gpus[gpu].targettemp - opt_hysteresis) {
		/* Detect large swings of 5 degrees or more and change fan by
		 * a proportion more */
		if (tdiff <= 0) {
			applog(LOG_DEBUG, "Temperature %d degrees below target, decreasing fanspeed", opt_hysteresis);
			newpercent = ga->targetfan - 1 + tdiff / 5;
		} else if (tdiff >= 5) {
			applog(LOG_DEBUG, "Temperature climbed %d while below target, increasing fanspeed", tdiff);
			newpercent = ga->targetfan + tdiff / 5;
		}
	} else {

		/* We're in the optimal range, make minor adjustments if the
		 * temp is still drifting */
		if (fanpercent > bot && tdiff < 0 && lasttemp < gpus[gpu].targettemp) {
			applog(LOG_DEBUG, "Temperature dropping while in target range, decreasing fanspeed");
			newpercent = ga->targetfan + tdiff;
		} else if (fanpercent < top && tdiff > 0 && temp > gpus[gpu].targettemp - opt_hysteresis) {
			applog(LOG_DEBUG, "Temperature rising while in target range, increasing fanspeed");
			newpercent = ga->targetfan + tdiff;
		}
	}

	if (newpercent > iMax)
		newpercent = iMax;
	else if (newpercent < iMin)
		newpercent = iMin;

	if (newpercent <= top)
		*fan_window = true;
	else
		*fan_window = false;

	if (newpercent != fanpercent) {
		applog(LOG_INFO, "Setting GPU %d fan percentage to %d", gpu, newpercent);
		set_fanspeed(gpu, newpercent);
		/* If the fanspeed is going down and we're below the top speed,
		 * consider the fan optimal to prevent minute changes in
		 * fanspeed delaying GPU engine speed changes */
		if (newpercent < fanpercent && *fan_window)
			return true;
		return false;
	}
	return true;
}

void gpu_autotune(int gpu, enum dev_enable *denable)
{
	int temp, fanpercent, engine, newengine, twintemp = 0;
	bool fan_optimal = true, fan_window = true;
	struct cgpu_info *cgpu;
	struct gpu_adl *ga;

	cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	ga = &data->adl;

	lock_adl();
	ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity);
	gpus[gpu].temp =
	temp = __gpu_temp(ga);
	if (ga->twin)
		twintemp = __gpu_temp(ga->twin);
	fanpercent = __gpu_fanpercent(ga);
	unlock_adl();

	newengine = engine = gpu_engineclock(gpu) * 100;

	if (temp && fanpercent >= 0 && ga->autofan) {
		if (!ga->twin)
			fan_optimal = fan_autotune(gpu, temp, fanpercent, ga->lasttemp, &fan_window);
		else if (ga->autofan && (ga->has_fanspeed || !ga->twin->autofan)) {
			/* On linked GPUs, we autotune the fan only once, based
			 * on the highest temperature from either GPUs */
			int hightemp, fan_gpu;
			int lasttemp;

			if (twintemp > temp) {
				lasttemp = ga->twin->lasttemp;
				hightemp = twintemp;
			} else {
				lasttemp = ga->lasttemp;
				hightemp = temp;
			}
			if (ga->has_fanspeed)
				fan_gpu = gpu;
			else
				fan_gpu = ga->twin->gpu;
			fan_optimal = fan_autotune(fan_gpu, hightemp, fanpercent, lasttemp, &fan_window);
		}
	}

	if (engine && ga->autoengine) {
		if (temp > cgpu->cutofftemp) {
			// Shutoff and recovery happens back in watchdog_thread
			newengine = ga->minspeed;
		} else if (temp > ga->overtemp && engine > ga->minspeed) {
			applog(LOG_WARNING, "Overheat detected, decreasing GPU %d clock speed", gpu);
			newengine = ga->minspeed;

			dev_error(cgpu, REASON_DEV_OVER_HEAT);
		} else if (temp > gpus[gpu].targettemp + opt_hysteresis && engine > ga->minspeed && fan_optimal) {
			applog(LOG_DEBUG, "Temperature %d degrees over target, decreasing clock speed", opt_hysteresis);
			newengine = engine - ga->lpOdParameters.sEngineClock.iStep;
			/* Only try to tune engine speed up if this GPU is not disabled */
		} else if (temp < gpus[gpu].targettemp && engine < ga->maxspeed && fan_window && *denable == DEV_ENABLED) {
			int iStep = ga->lpOdParameters.sEngineClock.iStep;

			applog(LOG_DEBUG, "Temperature below target, increasing clock speed");
			if (temp < gpus[gpu].targettemp - opt_hysteresis)
				iStep *= 2;
			newengine = engine + iStep;
		}

		if (newengine > ga->maxspeed)
			newengine = ga->maxspeed;
		else if (newengine < ga->minspeed)
			newengine = ga->minspeed;

		/* Adjust engine clock speed if it's lower, or if it's higher
		 * but higher than the last intended value as well as the
		 * current speed, to avoid setting the engine clock speed to
		 * a speed relateive to a lower profile during idle periods. */
		if (newengine < engine || (newengine > engine && newengine > ga->lastengine)) {
			newengine /= 100;
			applog(LOG_INFO, "Setting GPU %d engine clock to %d", gpu, newengine);
			set_engineclock(gpu, newengine);
		}
	}
	ga->lasttemp = temp;
}

void set_defaultfan(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	if (!data->has_adl || !adl_active)
		return;

	ga = &data->adl;
	lock_adl();
	ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->DefFanSpeedValue);
	unlock_adl();
}

void set_defaultengine(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga;
	if (!data->has_adl || !adl_active)
		return;

	ga = &data->adl;
	lock_adl();
	ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, ga->DefPerfLev);
	unlock_adl();
}

#ifdef HAVE_CURSES
void change_autosettings(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga = &data->adl;
	char input;
	int val;

	wlogprint("Target temperature: %d\n", gpus[gpu].targettemp);
	wlogprint("Overheat temperature: %d\n", ga->overtemp);
	wlogprint("Cutoff temperature: %d\n", gpus[gpu].cutofftemp);
	wlogprint("Toggle [F]an auto [G]PU auto\nChange [T]arget [O]verheat [C]utoff\n");
	wlogprint("Or press any other key to continue\n");
	input = getch();
	if (!strncasecmp(&input, "f", 1)) {
		ga->autofan ^= true;
		wlogprint("Fan autotune is now %s\n", ga->autofan ? "enabled" : "disabled");
		if (!ga->autofan) {
			wlogprint("Resetting fan to startup settings\n");
			set_defaultfan(gpu);
		}
	} else if (!strncasecmp(&input, "g", 1)) {
		ga->autoengine ^= true;
		wlogprint("GPU engine clock autotune is now %s\n", ga->autoengine ? "enabled" : "disabled");
		if (!ga->autoengine) {
			wlogprint("Resetting GPU engine clock to startup settings\n");
			set_defaultengine(gpu);
		}
	} else if (!strncasecmp(&input, "t", 1)) {
		val = curses_int("Enter target temperature for this GPU in C (0-200)");
		if (val < 0 || val > 200)
			wlogprint("Invalid temperature");
		else
			gpus[gpu].targettemp = val;
	} else if (!strncasecmp(&input, "o", 1)) {
		wlogprint("Enter overheat temperature for this GPU in C (%d+)", gpus[gpu].targettemp);
		val = curses_int("");
		if (val <= gpus[gpu].targettemp || val > 200)
			wlogprint("Invalid temperature");
		else
			ga->overtemp = val;
	} else if (!strncasecmp(&input, "c", 1)) {
		wlogprint("Enter cutoff temperature for this GPU in C (%d+)", ga->overtemp);
		val = curses_int("");
		if (val <= ga->overtemp || val > 200)
			wlogprint("Invalid temperature");
		else
			gpus[gpu].cutofftemp = val;
	}
}

void change_gpusettings(int gpu)
{
	struct cgpu_info * const cgpu = &gpus[gpu];
	struct opencl_device_data * const data = cgpu->device_data;
	struct gpu_adl *ga = &data->adl;
	float fval, fmin = 0, fmax = 0;
	int val, imin = 0, imax = 0;
	char input;
	int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
	float temp = 0, vddc = 0;

updated:
	if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune))
	wlogprint("Temp: %.1f C\n", temp);
	if (fanpercent >= 0 || fanspeed >= 0) {
		wlogprint("Fan Speed: ");
		if (fanpercent >= 0)
			wlogprint("%d%% ", fanpercent);
		if (fanspeed >= 0)
			wlogprint("(%d RPM)", fanspeed);
		wlogprint("\n");
	}
	wlogprint("Engine Clock: %d MHz\nMemory Clock: %d MHz\nVddc: %.3f V\nActivity: %d%%\nPowertune: %d%%\n",
		engineclock, memclock, vddc, activity, powertune);
	wlogprint("Fan autotune is %s (%d-%d)\n", ga->autofan ? "enabled" : "disabled",
		  data->min_fan, data->gpu_fan);
	wlogprint("GPU engine clock autotune is %s (%d-%d)\n", ga->autoengine ? "enabled" : "disabled",
		ga->minspeed / 100, ga->maxspeed / 100);
	wlogprint("Change [A]utomatic [E]ngine [F]an [M]emory [V]oltage [P]owertune\n");
	wlogprint("Or press any other key to continue\n");
	input = getch();

	if (!strncasecmp(&input, "a", 1)) {
		change_autosettings(gpu);
	} else if (!strncasecmp(&input, "e", 1)) {
		get_enginerange(gpu, &imin, &imax);
		wlogprint("Enter GPU engine clock speed (%d - %d MHz)", imin, imax);
		val = curses_int("");
		if (val < imin || val > imax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_engineclock(gpu, val))
			wlogprint("Driver reports success but check values below\n");
		else
			wlogprint("Failed to modify engine clock speed\n");
	} else if (!strncasecmp(&input, "f", 1)) {
		get_fanrange(gpu, &imin, &imax);
		wlogprint("Enter fan percentage (%d - %d %%)", imin, imax);
		val = curses_int("");
		if (val < imin || val > imax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_fanspeed(gpu, val))
			wlogprint("Driver reports success but check values below\n");
		else
			wlogprint("Failed to modify fan speed\n");
	} else if (!strncasecmp(&input, "m", 1)) {
		get_memoryrange(gpu, &imin, &imax);
		wlogprint("Enter GPU memory clock speed (%d - %d MHz)", imin, imax);
		val = curses_int("");
		if (val < imin || val > imax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_memoryclock(gpu, val))
			wlogprint("Driver reports success but check values below\n");
		else
			wlogprint("Failed to modify memory clock speed\n");
	} else if (!strncasecmp(&input, "v", 1)) {
		get_vddcrange(gpu, &fmin, &fmax);
		wlogprint("Enter GPU voltage (%.3f - %.3f V)", fmin, fmax);
		fval = curses_float("");
		if (fval < fmin || fval > fmax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_vddc(gpu, fval))
			wlogprint("Driver reports success but check values below\n");
		else
			wlogprint("Failed to modify voltage\n");
	} else if (!strncasecmp(&input, "p", 1)) {
		val = curses_int("Enter powertune value (-20 - 20)");
		if (val < -20 || val > 20) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_powertune(gpu, val))
			wlogprint("Driver reports success but check values below\n");
		else
			wlogprint("Failed to modify powertune value\n");
	} else {
		clear_logwin();
		return;
	}
	cgsleep_ms(1000);
	goto updated;
}
#endif

static void free_adl(void)
{
	ADL_Main_Memory_Free ((void **)&lpInfo);
	ADL_Main_Control_Destroy ();
#ifndef WIN32
	dlclose(hDLL);
#else
	FreeLibrary(hDLL);
#endif
}

void clear_adl(int nDevs)
{
	struct gpu_adl *ga;
	int i;

	if (!adl_active)
		return;

	lock_adl();
	/* Try to reset values to their defaults */
	for (i = 0; i < nDevs; i++) {
		struct cgpu_info * const cgpu = &gpus[i];
		struct opencl_device_data * const data = cgpu->device_data;
		ga = &data->adl;
		/*  Only reset the values if we've changed them at any time */
		if (!data->has_adl || !ga->managed)
			continue;
		ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, ga->DefPerfLev);
		free(ga->DefPerfLev);
		ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->DefFanSpeedValue);
		ADL_Overdrive5_FanSpeedToDefault_Set(ga->iAdapterIndex, 0);
	}
	adl_active = false;
	unlock_adl();
	free_adl();
}
#endif /* HAVE_ADL */
