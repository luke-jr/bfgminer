#include "miner.h"
#include "adl.h"

bool adl_active;

#ifdef HAVE_ADL
#if defined (__linux)
 #include "ADL_SDK/adl_sdk.h"
 #include <dlfcn.h>
 #include <stdlib.h>
 #include <unistd.h>
#else
 #include <windows.h>
 #include <tchar.h>
 #include "ADL_SDK/adl_sdk.h"
#endif

#include <stdio.h>
#include <curses.h>

#include "adl_functions.h"

int opt_hysteresis = 3;
int opt_targettemp = 75;
int opt_overheattemp = 85;

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

#if defined (LINUX)
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
static	ADL_ADAPTER_ACTIVE_GET		ADL_Adapter_Active_Get;
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

#if defined (LINUX)
	static void *hDLL;	// Handle to .so library
#else
	HINSTANCE hDLL;		// Handle to DLL
#endif
static int iNumberAdapters;
static LPAdapterInfo lpInfo = NULL;

void init_adl(int nDevs)
{
	int i, devices = 0, last_adapter = -1, gpu = 0, dummy = 0;

#if defined (LINUX)
	hDLL = dlopen( "libatiadlxx.so", RTLD_LAZY|RTLD_GLOBAL);
#else
	hDLL = LoadLibrary("atiadlxx.dll");
	if (hDLL == NULL)
		// A 32 bit calling application on 64 bit OS will fail to LoadLIbrary.
		// Try to load the 32 bit library (atiadlxy.dll) instead
		hDLL = LoadLibrary("atiadlxy.dll");
#endif
	if (hDLL == NULL) {
		applog(LOG_INFO, "Unable to load ati adl library");
		return;
	}

	ADL_Main_Control_Create = (ADL_MAIN_CONTROL_CREATE) GetProcAddress(hDLL,"ADL_Main_Control_Create");
	ADL_Main_Control_Destroy = (ADL_MAIN_CONTROL_DESTROY) GetProcAddress(hDLL,"ADL_Main_Control_Destroy");
	ADL_Adapter_NumberOfAdapters_Get = (ADL_ADAPTER_NUMBEROFADAPTERS_GET) GetProcAddress(hDLL,"ADL_Adapter_NumberOfAdapters_Get");
	ADL_Adapter_AdapterInfo_Get = (ADL_ADAPTER_ADAPTERINFO_GET) GetProcAddress(hDLL,"ADL_Adapter_AdapterInfo_Get");
	ADL_Adapter_ID_Get = (ADL_ADAPTER_ID_GET) GetProcAddress(hDLL,"ADL_Adapter_ID_Get");
	ADL_Overdrive5_Temperature_Get = (ADL_OVERDRIVE5_TEMPERATURE_GET) GetProcAddress(hDLL,"ADL_Overdrive5_Temperature_Get");
	ADL_Adapter_Active_Get = (ADL_ADAPTER_ACTIVE_GET) GetProcAddress(hDLL, "ADL_Adapter_Active_Get");
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

	if (!ADL_Main_Control_Create || !ADL_Main_Control_Destroy ||
		!ADL_Adapter_NumberOfAdapters_Get || !ADL_Adapter_AdapterInfo_Get ||
		!ADL_Adapter_ID_Get || !ADL_Overdrive5_Temperature_Get ||
		!ADL_Adapter_Active_Get || !ADL_Overdrive5_CurrentActivity_Get ||
		!ADL_Overdrive5_ODParameters_Get || !ADL_Overdrive5_FanSpeedInfo_Get ||
		!ADL_Overdrive5_FanSpeed_Get || !ADL_Overdrive5_FanSpeed_Set ||
		!ADL_Overdrive5_ODPerformanceLevels_Get || !ADL_Overdrive5_ODPerformanceLevels_Set ||
		!ADL_Main_Control_Refresh || !ADL_Overdrive5_PowerControl_Get ||
		!ADL_Overdrive5_PowerControl_Set) {
			applog(LOG_WARNING, "ATI ADL's API is missing");
		return;
	}

	// Initialise ADL. The second parameter is 1, which means:
	// retrieve adapter information only for adapters that are physically present and enabled in the system
	if (ADL_Main_Control_Create (ADL_Main_Memory_Alloc, 1) != ADL_OK) {
		applog(LOG_INFO, "ADL Initialisation Error!");
		return ;
	}

	if (ADL_Main_Control_Refresh() != ADL_OK) {
		applog(LOG_INFO, "ADL Refresh Error!");
		return ;
	}

	// Obtain the number of adapters for the system
	if (ADL_Adapter_NumberOfAdapters_Get ( &iNumberAdapters ) != ADL_OK) {
		applog(LOG_INFO, "Cannot get the number of adapters!\n");
		return ;
	}

	if (iNumberAdapters > 0) {
		lpInfo = malloc ( sizeof (AdapterInfo) * iNumberAdapters );
		memset ( lpInfo,'\0', sizeof (AdapterInfo) * iNumberAdapters );

		// Get the AdapterInfo structure for all adapters in the system
		if (ADL_Adapter_AdapterInfo_Get (lpInfo, sizeof (AdapterInfo) * iNumberAdapters) != ADL_OK) {
			applog(LOG_INFO, "ADL_Adapter_AdapterInfo_Get Error!");
			return ;
		}
	} else {
		applog(LOG_INFO, "No adapters found");
		return;
	}

	for (i = 0; i < iNumberAdapters; i++) {
		struct gpu_adl *ga;
		int iAdapterIndex;
		int lpAdapterID;
		int lpStatus;
		ADLODPerformanceLevels *lpOdPerformanceLevels;
		int lev;

		iAdapterIndex = lpInfo[i].iAdapterIndex;
		/* Get unique identifier of the adapter */
		if (ADL_Adapter_ID_Get(iAdapterIndex, &lpAdapterID) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Adapter_ID_Get");
			continue;
		}
		if (!lpAdapterID)
			continue;

		if (lpAdapterID != last_adapter) {
			/* We found a truly new adapter instead of a logical
			 * one. Now since there's no way of correlating the
			 * opencl enumerated devices and the ADL enumerated
			 * ones, we have to assume they're in the same order.*/
			if (++devices > nDevs) {
				applog(LOG_ERR, "ADL found more devices than opencl");
				return;
			}
			gpu = devices - 1;
			last_adapter = lpAdapterID;
		}

		/* See if the adapter is an AMD device with ADL active */
		if (ADL_Adapter_Active_Get(iAdapterIndex, &lpStatus) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Adapter_Active_Get");
			continue;
		}
		if (!lpStatus)
			continue;

		/* From here on we know this device is a discrete device and
		 * should support ADL */
		ga = &gpus[gpu].adl;
		ga->iAdapterIndex = iAdapterIndex;
		ga->lpAdapterID = lpAdapterID;
		ga->lpStatus = lpStatus;
		ga->DefPerfLev = NULL;

		if (ADL_Overdrive5_ODParameters_Get(iAdapterIndex, &ga->lpOdParameters) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Overdrive5_ODParameters_Get");
			continue;
		}

		lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
		/* We're only interested in the top performance level */
		lpOdPerformanceLevels = malloc(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
		lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;

		/* Get default performance levels first */
		if (ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 1, lpOdPerformanceLevels) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Overdrive5_ODPerformanceLevels_Get");
			continue;
		}
		/* Set the limits we'd use based on default gpu speeds */
		ga->maxspeed = ga->minspeed = lpOdPerformanceLevels->aLevels[lev].iEngineClock;

		/* Now get the current performance levels for any existing overclock */
		ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 0, lpOdPerformanceLevels);
		/* Save these values as the defaults in case we wish to reset to defaults */
		ga->DefPerfLev = lpOdPerformanceLevels;

		if (gpus[gpu].gpu_engine) {
			lpOdPerformanceLevels->aLevels[lev].iEngineClock = gpus[gpu].gpu_engine * 100;
			applog(LOG_INFO, "Setting GPU %d engine clock to %d", gpu, gpus[gpu].gpu_engine);
			ADL_Overdrive5_ODPerformanceLevels_Set(iAdapterIndex, lpOdPerformanceLevels);
		}
		if (gpus[gpu].gpu_memclock) {
			lpOdPerformanceLevels->aLevels[lev].iMemoryClock = gpus[gpu].gpu_memclock * 100;
			applog(LOG_INFO, "Setting GPU %d memory clock to %d", gpu, gpus[gpu].gpu_memclock);
			ADL_Overdrive5_ODPerformanceLevels_Set(iAdapterIndex, lpOdPerformanceLevels);
		}
		if (gpus[gpu].gpu_vddc) {
			lpOdPerformanceLevels->aLevels[lev].iVddc = gpus[gpu].gpu_vddc * 1000;
			applog(LOG_INFO, "Setting GPU %d voltage to %.3f", gpu, gpus[gpu].gpu_vddc);
			ADL_Overdrive5_ODPerformanceLevels_Set(iAdapterIndex, lpOdPerformanceLevels);
		}
		ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 0, lpOdPerformanceLevels);
		ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
		ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
		ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;

		if (ga->iEngineClock < ga->minspeed)
			ga->minspeed = ga->iEngineClock;
		if (ga->iEngineClock > ga->maxspeed)
			ga->maxspeed = ga->iEngineClock;

		if (ADL_Overdrive5_FanSpeedInfo_Get(iAdapterIndex, 0, &ga->lpFanSpeedInfo) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Overdrive5_FanSpeedInfo_Get");
			continue;
		}

		/* Save the fanspeed values as defaults in case we reset later */
		ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->DefFanSpeedValue);
		if (gpus[gpu].gpu_fan) {
			ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue);
			ga->lpFanSpeedValue.iFanSpeed = gpus[gpu].gpu_fan;
			ga->lpFanSpeedValue.iFlags = ADL_DL_FANCTRL_FLAG_USER_DEFINED_SPEED;
			ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
			applog(LOG_INFO, "Setting GPU %d fan speed to %d%%", gpu, gpus[gpu].gpu_fan);
			ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue);
		}

		/* Not fatal if powercontrol get fails */
		if (ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy) != ADL_OK)
			applog(LOG_INFO, "Failed to ADL_Overdrive5_PowerControl_get");

		if (gpus[gpu].gpu_powertune) {
			ADL_Overdrive5_PowerControl_Set(ga->iAdapterIndex, gpus[gpu].gpu_powertune);
			ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy);
		}

		/* Set some default temperatures for autotune when enabled */
		ga->targettemp = opt_targettemp;
		ga->overtemp = opt_overheattemp;
		if (opt_autofan)
			ga->autofan = true;
		if (opt_autoengine)
			ga->autoengine = true;

		gpus[gpu].has_adl = true;
	}

	adl_active = true;
}

static inline float __gpu_temp(struct gpu_adl *ga)
{
	if (ADL_Overdrive5_Temperature_Get(ga->iAdapterIndex, 0, &ga->lpTemperature) != ADL_OK)
		return 0;
	return (float)ga->lpTemperature.iTemperature / 1000;
}

float gpu_temp(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	return __gpu_temp(ga);
}

static inline int __gpu_engineclock(struct gpu_adl *ga)
{
	return ga->lpActivity.iEngineClock / 100;
}

int gpu_engineclock(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return __gpu_engineclock(ga);
}

static inline int __gpu_memclock(struct gpu_adl *ga)
{
	return ga->lpActivity.iMemoryClock / 100;
}

int gpu_memclock(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return __gpu_memclock(ga);
}

static inline float __gpu_vddc(struct gpu_adl *ga)
{
	return (float)ga->lpActivity.iVddc / 1000;
}

float gpu_vddc(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return __gpu_vddc(ga);
}

static inline int __gpu_activity(struct gpu_adl *ga)
{
	if (!ga->lpOdParameters.iActivityReportingSupported)
		return 0;
	return ga->lpActivity.iActivityPercent;
}

int gpu_activity(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return __gpu_activity(ga);
}

static inline int __gpu_fanspeed(struct gpu_adl *ga)
{
	if (!(ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_RPM_READ))
		return 0;
	ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return 0;
	return ga->lpFanSpeedValue.iFanSpeed;
}

int gpu_fanspeed(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	return __gpu_fanspeed(ga);
}

static inline int __gpu_fanpercent(struct gpu_adl *ga)
{
	if (!(ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_PERCENT_READ ))
		return -1;
	ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return -1;
	return ga->lpFanSpeedValue.iFanSpeed;
}

int gpu_fanpercent(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return -1;

	ga = &gpus[gpu].adl;
	return __gpu_fanpercent(ga);
}

static inline int __gpu_powertune(struct gpu_adl *ga)
{
	int dummy = 0;

	if (ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy) != ADL_OK)
		return 0;
	return ga->iPercentage;
}

int gpu_powertune(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return -1;

	ga = &gpus[gpu].adl;
	return __gpu_powertune(ga);
}

bool gpu_stats(int gpu, float *temp, int *engineclock, int *memclock, float *vddc,
	       int *activity, int *fanspeed, int *fanpercent, int *powertune)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return false;

	ga = &gpus[gpu].adl;
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

	return true;
}

static void get_enginerange(int gpu, int *imin, int *imax)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Get enginerange not supported\n");
		return;
	}
	ga = &gpus[gpu].adl;
	*imin = ga->lpOdParameters.sEngineClock.iMin / 100;
	*imax = ga->lpOdParameters.sEngineClock.iMax / 100;
}

static int set_engineclock(int gpu, int iEngineClock)
{
	ADLODPerformanceLevels *lpOdPerformanceLevels;
	struct gpu_adl *ga;
	int lev;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Set engineclock not supported\n");
		return 1;
	}

	iEngineClock *= 100;
	ga = &gpus[gpu].adl;
	if (iEngineClock > ga->lpOdParameters.sEngineClock.iMax ||
		iEngineClock < ga->lpOdParameters.sEngineClock.iMin)
			return 1;

	lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
	lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
	lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;
	if (ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK)
		return 1;
	lpOdPerformanceLevels->aLevels[lev].iEngineClock = iEngineClock;
	if (ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels) != ADL_OK)
		return 1;
	ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
	/* Reset to old value if it fails! */
	if (lpOdPerformanceLevels->aLevels[lev].iEngineClock != iEngineClock) {
		/* Set all the parameters in case they're linked somehow */
		lpOdPerformanceLevels->aLevels[lev].iEngineClock = ga->iEngineClock;
		lpOdPerformanceLevels->aLevels[lev].iMemoryClock = ga->iMemoryClock;
		lpOdPerformanceLevels->aLevels[lev].iVddc = ga->iVddc;
		ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels);
		ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
		return 1;
	}
	ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
	if (ga->iEngineClock > ga->maxspeed)
		ga->maxspeed = ga->iEngineClock;
	if (ga->iEngineClock < ga->minspeed)
		ga->minspeed = ga->iEngineClock;
	ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
	ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
	return 0;
}

static void get_memoryrange(int gpu, int *imin, int *imax)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Get memoryrange not supported\n");
		return;
	}
	ga = &gpus[gpu].adl;
	*imin = ga->lpOdParameters.sMemoryClock.iMin / 100;
	*imax = ga->lpOdParameters.sMemoryClock.iMax / 100;
}

static int set_memoryclock(int gpu, int iMemoryClock)
{
	ADLODPerformanceLevels *lpOdPerformanceLevels;
	struct gpu_adl *ga;
	int lev;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Set memoryclock not supported\n");
		return 1;
	}

	iMemoryClock *= 100;
	ga = &gpus[gpu].adl;
	if (iMemoryClock > ga->lpOdParameters.sMemoryClock.iMax ||
		iMemoryClock < ga->lpOdParameters.sMemoryClock.iMin)
			return 1;

	lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
	lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
	lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;
	if (ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK)
		return 1;
	lpOdPerformanceLevels->aLevels[lev].iMemoryClock = iMemoryClock;
	if (ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels) != ADL_OK)
		return 1;
	ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
	/* Reset to old value if it fails! */
	if (lpOdPerformanceLevels->aLevels[lev].iMemoryClock != iMemoryClock) {
		/* Set all the parameters in case they're linked somehow */
		lpOdPerformanceLevels->aLevels[lev].iMemoryClock = ga->iEngineClock;
		lpOdPerformanceLevels->aLevels[lev].iMemoryClock = ga->iMemoryClock;
		lpOdPerformanceLevels->aLevels[lev].iVddc = ga->iVddc;
		ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels);
		ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
		return 1;
	}
	ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
	ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
	ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
	return 0;
}

static void get_vddcrange(int gpu, float *imin, float *imax)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Get vddcrange not supported\n");
		return;
	}
	ga = &gpus[gpu].adl;
	*imin = (float)ga->lpOdParameters.sVddc.iMin / 1000;
	*imax = (float)ga->lpOdParameters.sVddc.iMax / 1000;
}

static float curses_float(const char *query)
{
	float ret;
	char *cvar;

	cvar = curses_input(query);
	ret = atof(cvar);
	free(cvar);
	return ret;
}

static int set_vddc(int gpu, float fVddc)
{
	ADLODPerformanceLevels *lpOdPerformanceLevels;
	struct gpu_adl *ga;
	int iVddc, lev;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Set vddc not supported\n");
		return 1;
	}

	iVddc = 1000 * fVddc;
	ga = &gpus[gpu].adl;
	if (iVddc > ga->lpOdParameters.sVddc.iMax ||
		iVddc < ga->lpOdParameters.sVddc.iMin)
			return 1;

	lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
	lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
	lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;
	if (ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK)
		return 1;
	lpOdPerformanceLevels->aLevels[lev].iVddc = iVddc;
	if (ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels) != ADL_OK)
		return 1;
	ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
	/* Reset to old value if it fails! */
	if (lpOdPerformanceLevels->aLevels[lev].iVddc != iVddc) {
		/* Set all the parameters in case they're linked somehow */
		lpOdPerformanceLevels->aLevels[lev].iEngineClock = ga->iEngineClock;
		lpOdPerformanceLevels->aLevels[lev].iMemoryClock = ga->iMemoryClock;
		lpOdPerformanceLevels->aLevels[lev].iVddc = ga->iVddc;
		ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, lpOdPerformanceLevels);
		ADL_Overdrive5_ODPerformanceLevels_Get(ga->iAdapterIndex, 0, lpOdPerformanceLevels);
		return 1;
	}
	ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
	ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
	ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;
	return 0;
}

static void get_fanrange(int gpu, int *imin, int *imax)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Get fanrange not supported\n");
		return;
	}
	ga = &gpus[gpu].adl;
	*imin = ga->lpFanSpeedInfo.iMinPercent;
	*imax = ga->lpFanSpeedInfo.iMaxPercent;
}

static int set_fanspeed(int gpu, int iFanSpeed)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Set fanspeed not supported\n");
		return 1;
	}

	ga = &gpus[gpu].adl;
	if (!(ga->lpFanSpeedInfo.iFlags & (ADL_DL_FANCTRL_SUPPORTS_RPM_WRITE | ADL_DL_FANCTRL_SUPPORTS_PERCENT_WRITE )))
		return 1;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return 1;
	ga->lpFanSpeedValue.iFlags = ADL_DL_FANCTRL_FLAG_USER_DEFINED_SPEED;
	if ((ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_RPM_WRITE) &&
		!(ga->lpFanSpeedInfo.iFlags & ADL_DL_FANCTRL_SUPPORTS_PERCENT_WRITE)) {
		/* Must convert speed to an RPM */
		iFanSpeed = ga->lpFanSpeedInfo.iMaxRPM * iFanSpeed / 100;
		ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_RPM;
	} else
		ga->lpFanSpeedValue.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
	ga->lpFanSpeedValue.iFanSpeed = iFanSpeed;
	if (ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return 1;
	return 0;
}

static int set_powertune(int gpu, int iPercentage)
{
	int oldPercentage, dummy;
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active) {
		wlogprint("Set powertune not supported\n");
		return 1;
	}

	ga = &gpus[gpu].adl;
	oldPercentage = ga->iPercentage;
	if (ADL_Overdrive5_PowerControl_Set(ga->iAdapterIndex, iPercentage) != ADL_OK) {
		ADL_Overdrive5_PowerControl_Set(ga->iAdapterIndex, ga->iPercentage);
		return 1;
	}
	ADL_Overdrive5_PowerControl_Get(ga->iAdapterIndex, &ga->iPercentage, &dummy);
	return 0;
}

void gpu_autotune(int gpu)
{
	int temp, fanpercent, engine, newpercent, newengine;
	bool fan_optimal = true;
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return;

	ga = &gpus[gpu].adl;
	temp = __gpu_temp(ga);
	newpercent = fanpercent = __gpu_fanpercent(ga);
	newengine = engine = gpu_engineclock(gpu) * 100;

	if (temp && fanpercent >= 0 && ga->autofan) {
		if (temp > ga->overtemp && fanpercent < 100) {
			applog(LOG_WARNING, "Overheat detected, increasing fan to 100%");
			newpercent = 100;
		} else if (temp > ga->targettemp && fanpercent < 85) {
			if (opt_debug)
				applog(LOG_DEBUG, "Temperature over target, increasing fanspeed");
			newpercent = fanpercent + 5;
			if (newpercent > 85)
				newpercent = 85;
		} else if (fanpercent && temp < ga->targettemp - opt_hysteresis) {
			if (opt_debug)
				applog(LOG_DEBUG, "Temperature %d degrees below target, decreasing fanspeed", opt_hysteresis);
			newpercent = fanpercent - 1;
		}

		if (newpercent > 100)
			newpercent = 100;
		else if (newpercent < 0)
			newpercent = 0;
		if (newpercent != fanpercent) {
			fan_optimal = false;
			applog(LOG_INFO, "Setting GPU %d fan percentage to %d", gpu, newpercent);
			set_fanspeed(gpu, newpercent);
		}
	}

	if (engine && ga->autoengine) {
		if (temp > ga->overtemp && engine > ga->minspeed) {
			applog(LOG_WARNING, "Overheat detected, decreasing GPU clock speed");
			newengine = ga->minspeed;
		} else if (temp > ga->targettemp + opt_hysteresis && engine > ga->minspeed && fan_optimal) {
			if (opt_debug)
				applog(LOG_DEBUG, "Temperature %d degrees over target, decreasing clock speed", opt_hysteresis);
			newengine = engine - ga->lpOdParameters.sEngineClock.iStep;
		} else if (temp < ga->targettemp && engine < ga->maxspeed) {
			if (opt_debug)
				applog(LOG_DEBUG, "Temperature below target, increasing clock speed");
			newengine = engine + ga->lpOdParameters.sEngineClock.iStep;
		}

		if (newengine > ga->maxspeed)
			newengine = ga->maxspeed;
		else if (newengine < ga->minspeed)
			newengine = ga->minspeed;
		if (newengine != engine) {
			newengine /= 100;
			applog(LOG_INFO, "Setting GPU %d engine clock to %d", gpu, newengine);
			set_engineclock(gpu, newengine);
		}
	}
}

void set_defaultfan(int gpu)
{
	struct gpu_adl *ga;
	if (!gpus[gpu].has_adl || !adl_active)
		return;

	ga = &gpus[gpu].adl;
	ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->DefFanSpeedValue);
}

void set_defaultengine(int gpu)
{
	struct gpu_adl *ga;
	if (!gpus[gpu].has_adl || !adl_active)
		return;

	ga = &gpus[gpu].adl;
	ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, ga->DefPerfLev);
}

void change_autosettings(int gpu)
{
	struct gpu_adl *ga = &gpus[gpu].adl;
	char input;
	int val;

	wlogprint("Target temperature: %d\n", ga->targettemp);
	wlogprint("Overheat temperature: %d\n", ga->overtemp);
	wlogprint("Hysteresis differece: %d\n", opt_hysteresis);
	wlogprint("Toggle [F]an auto [G]PU auto\nChange [T]arget [O]verheat [H]ysteresis\n");
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
		val = curses_int("Enter target temperature for this GPU in °C (0-100)");
		if (val < 0 || val > 100)
			wlogprint("Invalid temperature");
		else
			ga->targettemp = val;
	} else if (!strncasecmp(&input, "o", 1)) {
		wlogprint("Enter oveheat temperature for this GPU in °C (%d-100)", ga->targettemp);
		val = curses_int("");
		if (val <= ga->targettemp || val > 100)
			wlogprint("Invalid temperature");
		else
			ga->overtemp = val;
	} else if (!strncasecmp(&input, "h", 1)) {
		val = curses_int("Enter hysteresis temperature difference (0-10)");
		if (val < 1 || val > 10)
			wlogprint("Invalid value");
		else
			opt_hysteresis = val;
	}
}

void change_gpusettings(int gpu)
{
	struct gpu_adl *ga = &gpus[gpu].adl;
	float fval, fmin = 0, fmax = 0;
	int val, imin = 0, imax = 0;
	char input;
	int engineclock = 0, memclock = 0, activity = 0, fanspeed = 0, fanpercent = 0, powertune = 0;
	float temp = 0, vddc = 0;

	if (gpu_stats(gpu, &temp, &engineclock, &memclock, &vddc, &activity, &fanspeed, &fanpercent, &powertune))
	wlogprint("Temp: %.1f °C\nFan Speed: %d%% (%d RPM)\nEngine Clock: %d MHz\n"
		"Memory Clock: %d Mhz\nVddc: %.3f V\nActivity: %d%%\nPowertune: %d%%\n",
		temp, fanpercent, fanspeed, engineclock, memclock, vddc, activity, powertune);
	wlogprint("Fan autotune is %s\n", ga->autofan ? "enabled" : "disabled");
	wlogprint("GPU engine clock autotune is %s\n", ga->autoengine ? "enabled" : "disabled");
	wlogprint("Change [A]utomatic [E]ngine [F]an [M]emory [V]oltage [P]owertune\n");
	wlogprint("Or press any other key to continue\n");
	input = getch();

	if (!strncasecmp(&input, "a", 1)) {
		change_autosettings(gpu);
	} else if (!strncasecmp(&input, "e", 1)) {
		get_enginerange(gpu, &imin, &imax);
		wlogprint("Enter GPU engine clock speed (%d - %d Mhz)", imin, imax);
		val = curses_int("");
		if (val < imin || val > imax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_engineclock(gpu, val))
			wlogprint("Successfully modified engine clock speed\n");
		else
			wlogprint("Failed to modify engine clock speed\n");
	} else if (!strncasecmp(&input, "f", 1)) {
		get_fanrange(gpu, &imin, &imax);
		wlogprint("Enter fan percentage (%d - %d %)", imin, imax);
		val = curses_int("");
		if (val < imin || val > imax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_fanspeed(gpu, val))
			wlogprint("Successfully modified fan speed\n");
		else
			wlogprint("Failed to modify fan speed\n");
	} else if (!strncasecmp(&input, "m", 1)) {
		get_memoryrange(gpu, &imin, &imax);
		wlogprint("Enter GPU memory clock speed (%d - %d Mhz)", imin, imax);
		val = curses_int("");
		if (val < imin || val > imax) {
			wlogprint("Value is outside safe range, are you sure?\n");
			input = getch();
			if (strncasecmp(&input, "y", 1))
				return;
		}
		if (!set_memoryclock(gpu, val))
			wlogprint("Successfully modified memory clock speed\n");
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
			wlogprint("Successfully modified voltage\n");
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
			wlogprint("Successfully modified powertune value\n");
		else
			wlogprint("Failed to modify powertune value\n");
	}
	wlogprint("Press any key to continue\n");
	input = getch();
}

void clear_adl(nDevs)
{
	struct gpu_adl *ga;
	int i;

	if (!adl_active)
		return;

	/* Try to reset values to their defaults */
	for (i = 0; i < nDevs; i++) {
		ga = &gpus[i].adl;
		if (!gpus[i].has_adl)
			continue;
		ADL_Overdrive5_ODPerformanceLevels_Set(ga->iAdapterIndex, ga->DefPerfLev);
		free(ga->DefPerfLev);
		ADL_Overdrive5_FanSpeed_Set(ga->iAdapterIndex, 0, &ga->DefFanSpeedValue);
	}

	ADL_Main_Memory_Free ( (void **)&lpInfo );
	ADL_Main_Control_Destroy ();

#if defined (LINUX)
	dlclose(hDLL);
#else
	FreeLibrary(hDLL);
#endif
}
#endif /* HAVE_ADL */
