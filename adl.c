#include "miner.h"
#include "adl.h"
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

#include "adl_functions.h"

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

bool adl_active;
#if defined (LINUX)
	static void *hDLL;	// Handle to .so library
#else
	HINSTANCE hDLL;		// Handle to DLL
#endif
static int iNumberAdapters;
static LPAdapterInfo lpInfo = NULL;

void init_adl(int nDevs)
{
	int i, devices = 0, last_adapter = -1;

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

	if (!ADL_Main_Control_Create || !ADL_Main_Control_Destroy ||
		!ADL_Adapter_NumberOfAdapters_Get || !ADL_Adapter_AdapterInfo_Get ||
		!ADL_Adapter_ID_Get || !ADL_Overdrive5_Temperature_Get ||
		!ADL_Adapter_Active_Get || !ADL_Overdrive5_CurrentActivity_Get ||
		!ADL_Overdrive5_ODParameters_Get || !ADL_Overdrive5_FanSpeedInfo_Get ||
		!ADL_Overdrive5_FanSpeed_Get || !ADL_Overdrive5_FanSpeed_Set ||
		!ADL_Overdrive5_ODPerformanceLevels_Get || !ADL_Overdrive5_ODPerformanceLevels_Set ||
		!ADL_Main_Control_Refresh) {
			applog(LOG_INFO, "ATI ADL's API is missing");
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

	for ( i = 0; i < iNumberAdapters; i++ ) {
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
		ga = &gpus[devices - 1].adl;
		ga->iAdapterIndex = iAdapterIndex;
		ga->lpAdapterID = lpAdapterID;
		ga->lpStatus = lpStatus;
		if (ADL_Overdrive5_ODParameters_Get(iAdapterIndex, &ga->lpOdParameters) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Overdrive5_ODParameters_Get");
			continue;
		}

		lev = ga->lpOdParameters.iNumberOfPerformanceLevels - 1;
		/* We're only interested in the top performance level */
		lpOdPerformanceLevels = alloca(sizeof(ADLODPerformanceLevels) + (lev * sizeof(ADLODPerformanceLevel)));
		lpOdPerformanceLevels->iSize = sizeof(ADLODPerformanceLevels) + sizeof(ADLODPerformanceLevel) * lev;
		if (ADL_Overdrive5_ODPerformanceLevels_Get(iAdapterIndex, 0, lpOdPerformanceLevels) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Overdrive5_ODPerformanceLevels_Get");
			continue;
		}
		ga->iEngineClock = lpOdPerformanceLevels->aLevels[lev].iEngineClock;
		ga->iMemoryClock = lpOdPerformanceLevels->aLevels[lev].iMemoryClock;
		ga->iVddc = lpOdPerformanceLevels->aLevels[lev].iVddc;

		if (ADL_Overdrive5_FanSpeedInfo_Get(iAdapterIndex, 0, &ga->lpFanSpeedInfo) != ADL_OK) {
			applog(LOG_INFO, "Failed to ADL_Overdrive5_FanSpeedInfo_Get");
			continue;
		}

		gpus[devices - 1].has_adl = true;
	}

	adl_active = true;
}

float gpu_temp(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_Temperature_Get(ga->iAdapterIndex, 0, &ga->lpTemperature) != ADL_OK)
		return 0;
	return (float)ga->lpTemperature.iTemperature / 1000;
}

int gpu_engineclock(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return ga->lpActivity.iEngineClock / 100;
}

int gpu_memclock(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return ga->lpActivity.iMemoryClock / 100;
}

float gpu_vddc(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return (float)ga->lpActivity.iVddc / 1000;
}

int gpu_activity(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (!ga->lpOdParameters.iActivityReportingSupported)
		return 0;
	if (ADL_Overdrive5_CurrentActivity_Get(ga->iAdapterIndex, &ga->lpActivity) != ADL_OK)
		return 0;
	return ga->lpActivity.iActivityPercent;
}

int gpu_fanspeed(int gpu)
{
	struct gpu_adl *ga;

	if (!gpus[gpu].has_adl || !adl_active)
		return 0;

	ga = &gpus[gpu].adl;
	if (!(ga->lpFanSpeedInfo.iFlags & (ADL_DL_FANCTRL_SUPPORTS_RPM_READ | ADL_DL_FANCTRL_SUPPORTS_PERCENT_READ )))
		return 0;
	if (ADL_Overdrive5_FanSpeed_Get(ga->iAdapterIndex, 0, &ga->lpFanSpeedValue) != ADL_OK)
		return 0;
	return ga->lpFanSpeedValue.iFanSpeed;
}

void clear_adl(void)
{
	if (!adl_active)
		return;

	ADL_Main_Memory_Free ( (void **)&lpInfo );
	ADL_Main_Control_Destroy ();

#if defined (LINUX)
	dlclose(hDLL);
#else
	FreeLibrary(hDLL);
#endif
}
#endif /* HAVE_ADL */
