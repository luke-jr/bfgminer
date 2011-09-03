/*******************************************************************************

 * This program reads HW information from your ATI Radeon card and displays them
 * You can also change frequencies and voltages.

 * THIS PROGRAM MAY DAMAGE YOUR VIDEO CARD, IF YOU APPLY NONSENSIAL VALUES.
 * e.g. INCREASING THE VOLTAGES AND FREQUENCIES IN CONJUNCTION WITH LOWERING THE
 *      FAN SPEED IS NOT ADVISABLE!

 * Copyright(C) Thorsten Gilling (tgilling@web.de)

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*******************************************************************************/

// ------------------------------------------------------------------------------------------------------------
// AMD ADL function types from Version 3.0
// ------------------------------------------------------------------------------------------------------------

#if defined (linux)
 #include <dlfcn.h>	//dyopen, dlsym, dlclose
 #include <stdlib.h>
 #include <string.h>	//memeset
#else
 #include <windows.h>
 #include <tchar.h>
#endif

#include "ADL_SDK/adl_sdk.h"

// Definitions of the used function pointers. Add more if you use other ADL APIs

// ------------------------------------------------------------------------------------------------------------

// ADL Main
typedef int ( *ADL_MAIN_CONTROL_CREATE ) (ADL_MAIN_MALLOC_CALLBACK callback, int iEnumConnectedAdapters);
typedef int ( *ADL_MAIN_CONTROL_REFRESH ) ();
typedef int ( *ADL_MAIN_CONTROL_DESTROY ) ();
typedef int ( *ADL_GRAPHICS_PLATFORM_GET ) (int *lpPlatForm);

// ------------------------------------------------------------------------------------------------------------

// ADL Adapter/General
typedef int ( *ADL_ADAPTER_ACTIVE_GET ) (int iAdapterIndex, int *lpStatus);
typedef int ( *ADL_ADAPTER_NUMBEROFADAPTERS_GET ) (int *lpNumAdapters);
typedef int ( *ADL_ADAPTER_ADAPTERINFO_GET ) (LPAdapterInfo lpInfo, int iInputSize);
typedef int ( *ADL_ADAPTER_ASICFAMILYTYPE_GET ) (int iAdapterIndex, int *lpAsicTypes, int *lpValids);
typedef int ( *ADL_ADAPTER_SPEED_CAPS )	(int iAdapterIndex, int *lpCaps, int *lpValid);
typedef int ( *ADL_ADAPTER_SPEED_GET ) (int iAdapterIndex, int *lpCurrent, int *lpDefault);
typedef int ( *ADL_ADAPTER_SPEED_SET ) (int iAdapterIndex, int iSpeed);
typedef int ( *ADL_ADAPTER_ACCESSIBILITY_GET ) (int iAdapterIndex, int *lpAccessibility);
typedef int ( *ADL_ADAPTER_VIDEOBIOSINFO_GET ) (int iAdapterIndex, ADLBiosInfo *lpBiosInfo);
typedef int ( *ADL_ADAPTER_ID_GET ) (int iAdapterIndex, int *lpAdapterID);

// ADL Adapter/CrossDisplay
typedef int ( *ADL_ADAPTER_CROSSDISPLAYADAPTERROLE_CAPS ) (int iAdapterIndex, int *lpCrossDisplaySupport, int *lpAdapterRole, int *lpNumPossDisplayAdapters, int **lppPossDisplayAdapters, int *lpNnumPosRenderingAdapters, int **lppPosRenderingAdapters, int *lpErrorStatus);
typedef int ( *ADL_ADAPTER_CROSSDISPLAYINFO_GET ) (int iAdapterIndex, int *lpAdapterRole, int *lpCrossdisplayMode, int *lpNumDisplayAdapters, int **lppDisplayAdapters, int *lpNumRenderingAdapters, int **lppRenderingAdapters, int *lpErrorCodeStatus);
typedef int ( *ADL_ADAPTER_CROSSDISPLAYINFO_SET ) (int iAdapterIndex, int iDisplayAdapterIndex, int iRenderingAdapterIndex, int crossdisplayMode, int *lpErrorCode);

// ADL Adapter/CrossFire
typedef int ( *ADL_ADAPTER_CROSSFIRE_CAPS ) (int iAdapterIndex, int *lpPreferred, int *lpNumComb, ADLCrossfireComb **ppCrossfireComb);
typedef int ( *ADL_ADAPTER_CROSSFIRE_GET ) (int iAdapterIndex, ADLCrossfireComb *lpCrossfireComb, ADLCrossfireInfo *lpCrossfireInfo);
typedef int ( *ADL_ADAPTER_CROSSFIRE_SET ) (int iAdapterIndex, ADLCrossfireComb *lpCrossfireComb);

// ------------------------------------------------------------------------------------------------------------

// ADL Display/Misc

typedef int ( *ADL_DISPLAY_DISPLAYINFO_GET ) (int iAdapterIndex, int *lpNumDisplays, ADLDisplayInfo **lppInfo, int iForceDetect);
typedef int ( *ADL_DISPLAY_NUMBEROFDISPLAYS_GET ) (int iAdapterIndex, int *lpNumDisplays);
typedef int ( *ADL_DISPLAY_PRESERVEDASPECTRATIO_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport, int *lpCurrent, int *lpDefault);
typedef int ( *ADL_DISPLAY_PRESERVEDASPECTRATIO_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);
typedef int ( *ADL_DISPLAY_IMAGEEXPANSION_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport, int *lpCurrent, int *lpDefault);
typedef int ( *ADL_DISPLAY_IMAGEEXPANSION_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);
typedef int ( *ADL_DISPLAY_POSITION_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpX, int *lpY, int *lpXDefault, int *lpYDefault, int *lpMinX, int *lpMinY, int *lpMaxX, int *lpMaxY, int *lpStepX, int *lpStepY);
typedef int ( *ADL_DISPLAY_POSITION_SET ) (int iAdapterIndex, int iDisplayIndex, int iX, int iY);
typedef int ( *ADL_DISPLAY_SIZE_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpWidth, int *lpHeight, int *lpDefaultWidth, int *lpDefaultHeight, int *lpMinWidth, int *lpMinHeight, int *lpMaxWidth, int *lpMaxHeight, int *lpStepWidth, int *lpStepHeight);
typedef int ( *ADL_DISPLAY_SIZE_SET ) (int iAdapterIndex, int iDisplayIndex, int iWidth, int iHeight);
typedef int ( *ADL_DISPLAY_ADJUSTCAPS_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpInfo);
typedef int ( *ADL_DISPLAY_CAPABILITIES_GET ) (int iAdapterIndex, int *lpNumberOfControlers, int *lpNumberOfDisplays);
typedef int ( *ADL_DISPLAY_CONNECTEDDISPLAYS_GET ) (int iAdapterIndex, int *lpConnections);
typedef int ( *ADL_DISPLAY_DEVICECONFIG_GET ) (int iAdapterIndex, int iDisplayIndex, ADLDisplayConfig *lpDisplayConfig);
typedef int ( *ADL_DISPLAY_PROPERTY_GET ) (int iAdapterIndex, int iDisplayIndex, ADLDisplayProperty *lpDisplayProperty);
typedef int ( *ADL_DISPLAY_PROPERTY_SET ) (int iAdapterIndex, int iDisplayIndex, ADLDisplayProperty *lpDisplayProperty);
typedef int ( *ADL_DISPLAY_SWITCHINGCAPABILITY_GET ) (int iAdapterIndex, int *lpResult);
typedef int ( *ADL_DISPLAY_DITHERSTATE_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpDitherState);
typedef int ( *ADL_DISPLAY_DITHERSTATE_SET ) (int iAdapterIndex, int iDisplayIndex, int iDitherState);
typedef int ( *ADL_DISPLAY_SUPPORTEDPIXELFORMAT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpPixelFormat);
typedef int ( *ADL_DISPLAY_PIXELFORMAT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpPixelFormat);
typedef int ( *ADL_DISPLAY_PIXELFORMAT_SET ) (int iAdapterIndex, int iDisplayIndex, int iPixelFormat);
typedef int ( *ADL_DISPLAY_ODCLOCKINFO_GET ) (int iAdapterIndex, ADLAdapterODClockInfo *lpOdClockInfo);
typedef int ( *ADL_DISPLAY_ODCLOCKCONFIG_SET ) (int iAdapterIndex, ADLAdapterODClockConfig *lpOdClockConfig);
typedef int ( *ADL_DISPLAY_ADJUSTMENTCOHERENT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpAdjustmentCoherentCurrent, int *lpAdjustmentCoherentDefault);
typedef int ( *ADL_DISPLAY_ADJUSTMENTCOHERENT_SET ) (int iAdapterIndex, int iDisplayIndex, int iAdjustmentCoherent);
typedef int ( *ADL_DISPLAY_REDUCEDBLANKING_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpReducedBlankingCurrent, int *lpReducedBlankingDefault);
typedef int ( *ADL_DISPLAY_REDUCEDBLANKING_SET ) (int iAdapterIndex, int iDisplayIndex, int iReducedBlanking);
typedef int ( *ADL_DISPLAY_FORMATSOVERRIDE_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSettingsSupported, int *lpSettingsSupportedEx, int *lpCurSettings);
typedef int ( *ADL_DISPLAY_FORMATSOVERRIDE_SET ) (int iAdapterIndex, int iDisplayIndex, int iOverrideSettings);
typedef int ( *ADL_DISPLAY_MVPUCAPS_GET ) (int iAdapterIndex, ADLMVPUCaps *lpMvpuCaps);
typedef int ( *ADL_DISPLAY_MVPUSTATUS_GET ) (int iAdapterIndex, ADLMVPUStatus *lpMvpuStatus);

// ADL Display/Eyefinity
typedef int ( *ADL_ADAPTER_ACTIVE_SET ) (int iAdapterIndex, int iStatus, int *lpNewlyActivate);
typedef int ( *ADL_ADAPTER_ACTIVE_SETPREFER ) (int iAdapterIndex, int iStatus, int iNumPreferTarget, ADLDisplayTarget *lpPreferTarget, int *lpNewlyActivate);
typedef int ( *ADL_ADAPTER_PRIMARY_GET ) (int *lpPrimaryAdapterIndex);
typedef int ( *ADL_ADAPTER_PRIMARY_SET ) (int iAdapterIndex);
typedef int ( *ADL_ADAPTER_MODESWITCH ) (int iAdapterIndex);
typedef int ( *ADL_DISPLAY_MODES_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpNumModes, ADLMode **lppModes);
typedef int ( *ADL_DISPLAY_MODES_SET ) (int iAdapterIndex, int iDisplayIndex, int iNumModes, ADLMode *lpModes);
typedef int ( *ADL_DISPLAY_POSSIBLEMODE_GET ) (int iAdapterIndex, int *lpNumModes, ADLMode **lppModes);
typedef int ( *ADL_DISPLAY_FORCIBLEDISPLAY_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpStatus);
typedef int ( *ADL_DISPLAY_FORCIBLEDISPLAY_SET ) (int iAdapterIndex, int iDisplayIndex, int iStatus);
typedef int ( *ADL_ADAPTER_NUMBEROFACTIVATABLESOURCES_GET ) (int iAdapterIndex, int *lpNumSources, ADLActivatableSource **lppSources);
typedef int ( *ADL_ADAPTER_DISPLAY_CAPS ) (int iAdapterIndex, int *lpNumDisplayCaps, ADLAdapterDisplayCap **lppAdapterDisplayCaps);
typedef int ( *ADL_DISPLAY_DISPLAYMAPCONFIG_GET ) (int iAdapterIndex, int *lpNumDisplayMap, ADLDisplayMap **lppDisplayMap, int *lpNumDisplayTarget, ADLDisplayTarget **lppDisplayTarget, int iOptions);
typedef int ( *ADL_DISPLAY_DISPLAYMAPCONFIG_SET ) (int iAdapterIndex, int iNumDisplayMap, ADLDisplayMap *lpDisplayMap, int iNumDisplayTarget, ADLDisplayTarget *lpDisplayTarget);
typedef int ( *ADL_DISPLAY_POSSIBLEMAPPING_GET ) (int iAdapterIndex, int iNumberOfPresetMapping, ADLPossibleMapping *lpPresetMappings, int iEnquiryControllerIndex, int *lpNumberOfEnquiryPossibleMappings, ADLPossibleMapping **lppEnquiryPossibleMappings);
typedef int ( *ADL_DISPLAY_DISPLAYMAPCONFIG_VALIDATE ) (int iAdapterIndex, int iNumPossibleMap, ADLPossibleMap *lpPossibleMaps, int *lpNumPossibleMapResult, ADLPossibleMapResult **lppPossibleMapResult);
typedef int ( *ADL_DISPLAY_DISPLAYMAPCONFIG_POSSIBLEADDANDREMOVE ) (int iAdapterIndex, int iNumDisplayMap, ADLDisplayMap *lpDisplayMap, int iNumDisplayTarget, ADLDisplayTarget *lpDisplayTarget, int *lpNumPossibleAddTarget, ADLDisplayTarget **lppPossibleAddTarget, int *lpNumPossibleRemoveTarget, ADLDisplayTarget **lppPossibleRemoveTarget);
typedef int ( *ADL_DISPLAY_SLSGRID_CAPS ) (int iAdapterIndex, int *lpNumSLSGrid, ADLSLSGrid **lppSLSGrid, int iOption);
typedef int ( *ADL_DISPLAY_SLSMAPINDEXLIST_GET ) (int iAdapterIndex, int *lpNumSLSMapIndexList, int **lppSLSMapIndexList, int iOptions);
typedef int ( *ADL_DISPLAY_SLSMAPINDEX_GET ) (int iAdapterIndex, int iADLNumDisplayTarget, ADLDisplayTarget *lpDisplayTarget, int *lpSLSMapIndex);
typedef int ( *ADL_DISPLAY_SLSMAPCONFIG_GET ) (int iAdapterIndex, int iSLSMapIndex, ADLSLSMap *lpSLSMap, int *lpNumSLSTarget, ADLSLSTarget **lppSLSTarget, int *lpNumNativeMode, ADLSLSMode **lppNativeMode, int *lpNumBezelMode, ADLBezelTransientMode **lppBezelMode, int *lpNumTransientMode, ADLBezelTransientMode **lppTransientMode, int *lpNumSLSOffset, ADLSLSOffset **lppSLSOffset, int iOption);
typedef int ( *ADL_DISPLAY_SLSMAPCONFIG_CREATE ) (int iAdapterIndex, ADLSLSMap SLSMap, int iNumTargetTarget, ADLSLSTarget *lpSLSTarget, int iBezelModePercent, int *lpSLSMapIndex, int iOption);
typedef int ( *ADL_DISPLAY_SLSMAPCONFIG_DELETE ) (int iAdapterIndex, int iSLSMapIndex);
typedef int ( *ADL_DISPLAY_SLSMAPCONFIG_SETSTATE ) (int iAdapterIndex, int iSLSMapIndex, int iState);
typedef int ( *ADL_DISPLAY_SLSMAPCONFIG_REARRANGE ) (int iAdapterIndex, int iSLSMapIndex, int iNumDisplayTarget, ADLSLSTarget *lpSLSTarget, ADLSLSMap slsMap, int iOption);
typedef int ( *ADL_DISPLAY_POSSIBLEMODE_WINXP_GET ) (int iAdapterIndex, int iNumDisplayTargets, ADLDisplayTarget *lpDisplayTargets, int iLargeDesktopSupportedType, int iDevicePanningControl, int *lpNumModes, ADLMode **lppModes);
typedef int ( *ADL_DISPLAY_BEZELOFFSETSTEPPINGSIZE_GET ) (int iAdapterIndex, int *lpNumBezelOffsetSteppingSize, ADLBezelOffsetSteppingSize **lppBezelOffsetSteppingSize);
typedef int ( *ADL_DISPLAY_BEZELOFFSET_SET ) (int iAdapterIndex, int iSLSMapIndex, int iNumBezelOffset, LPADLSLSOffset lpBezelOffset, ADLSLSMap SLSMap, int iOption);
typedef int ( *ADL_DISPLAY_BEZELSUPPORTED_VALIDATE ) (int iAdapterIndex, int iNumPossibleSLSMap, LPADLPossibleSLSMap lpPossibleSLSMaps, int *lpNumPossibleSLSMapResult, LPADLPossibleMapResult *lppPossibleMapResult);

// ADL Display/Color
typedef int ( *ADL_DISPLAY_COLORCAPS_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpCaps, int *lpValids);
typedef int ( *ADL_DISPLAY_COLOR_SET ) (int iAdapterIndex, int iDisplayIndex, int iColorType, int iCurrent);
typedef int ( *ADL_DISPLAY_COLOR_GET ) (int iAdapterIndex, int iDisplayIndex, int iColorType, int *lpCurrent, int *lpDefault, int *lpMin, int *lpMax, int *lpStep);
typedef int ( *ADL_DISPLAY_COLORTEMPERATURESOURCE_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpTempSource);
typedef int ( *ADL_DISPLAY_COLORTEMPERATURESOURCE_SET ) (int iAdapterIndex, int iDisplayIndex, int iTempSource);

// ADL Display/Timing
typedef int ( *ADL_DISPLAY_MODETIMINGOVERRIDE_GET ) (int iAdapterIndex, int iDisplayIndex, ADLDisplayMode *lpModeIn, ADLDisplayModeInfo *lpModeInfoOut);
typedef int ( *ADL_DISPLAY_MODETIMINGOVERRIDE_SET ) (int iAdapterIndex, int iDisplayIndex, ADLDisplayModeInfo *lpMode, int iForceUpdate);
typedef int ( *ADL_DISPLAY_MODETIMINGOVERRIDELIST_GET ) (int iAdapterIndex, int iDisplayIndex, int iMaxNumOfOverrides, ADLDisplayModeInfo *lpModeInfoList, int *lpNumOfOverrides);

// ADL Display/Customize
typedef int ( *ADL_DISPLAY_CUSTOMIZEDMODELISTNUM_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpListNum);
typedef int ( *ADL_DISPLAY_CUSTOMIZEDMODELIST_GET ) (int iAdapterIndex, int iDisplayIndex, ADLCustomMode *lpCustomModeList, int iBuffSize);
typedef int ( *ADL_DISPLAY_CUSTOMIZEDMODE_ADD ) (int iAdapterIndex, int iDisplayIndex, ADLCustomMode customMode);
typedef int ( *ADL_DISPLAY_CUSTOMIZEDMODE_DELETE ) (int iAdapterIndex, int iDisplayIndex, int iIndex);
typedef int ( *ADL_DISPLAY_CUSTOMIZEDMODE_VALIDATE ) (int iAdapterIndex, int iDisplayIndex, ADLCustomMode customMode, int *lpValid);

// ADL Display/Over-Underscan
typedef int ( *ADL_DISPLAY_UNDERSCAN_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);
typedef int ( *ADL_DISPLAY_UNDERSCAN_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpCurrent, int *lpDefault, int *lpMin, int *lpMax, int *lpStep);
typedef int ( *ADL_DISPLAY_OVERSCAN_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);
typedef int ( *ADL_DISPLAY_OVERSCAN_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpCurrent, int *lpDefualt, int *lpMin, int *lpMax, int *lpStep);

// ADL Display/Overlay
typedef int ( *ADL_DISPLAY_CONTROLLEROVERLAYADJUSTMENTCAPS_GET ) (int iAdapterIndex, ADLControllerOverlayInput *lpOverlayInput, ADLControllerOverlayInfo *lpCapsInfo);
typedef int ( *ADL_DISPLAY_CONTROLLEROVERLAYADJUSTMENTDATA_GET ) (int iAdapterIndex, ADLControllerOverlayInput *lpOverlay);
typedef int ( *ADL_DISPLAY_CONTROLLEROVERLAYADJUSTMENTDATA_SET ) (int iAdapterIndex, ADLControllerOverlayInput *lpOverlay);

// ADL Display/PowerXpress
typedef int ( *ADL_DISPLAY_POWERXPRESSVERSION_GET ) (int iAdapterIndex, int *lpVersion);
typedef int ( *ADL_DISPLAY_POWERXPRESSACTIVEGPU_GET ) (int iAdapterIndex, int *lpActiveGPU);
typedef int ( *ADL_DISPLAY_POWERXPRESSACTIVEGPU_SET ) (int iAdapterIndex, int iActiveGPU, int *lpOperationResult);
typedef int ( *ADL_DISPLAY_POWERXPRESS_AUTOSWITCHCONFIG_GET ) (int iAdapterIndex, int *lpAutoSwitchOnACDCEvent, int *lpAutoSwitchOnDCACEvent);
typedef int ( *ADL_DISPLAY_POWERXPRESS_AUTOSWITCHCONFIG_SET ) (int iAdapterIndex, int iAutoSwitchOnACDCEvent, int iAutoSwitchOnDCACEvent);

// ------------------------------------------------------------------------------------------------------------

// ADL DFP
typedef int ( *ADL_DFP_BASEAUDIOSUPPORT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport);
typedef int ( *ADL_DFP_HDMISUPPORT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport);
typedef int ( *ADL_DFP_MVPUANALOGSUPPORT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport);
typedef int ( *ADL_DFP_PIXELFORMAT_CAPS ) (int iAdapterIndex, int iDisplayIndex, int *lpValidBits, int *lpValidCaps);
typedef int ( *ADL_DFP_PIXELFORMAT_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpCurState, int *lpDefault);
typedef int ( *ADL_DFP_PIXELFORMAT_SET ) (int iAdapterIndex, int iDisplayIndex, int iState);
typedef int ( *ADL_DFP_GPUSCALINGENABLE_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport, int *lpCurrent, int *lpDefault);
typedef int ( *ADL_DFP_GPUSCALINGENABLE_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);
typedef int ( *ADL_DFP_ALLOWONLYCETIMINGS_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpSupport, int *lpCurrent, int *lpDefault);
typedef int ( *ADL_DFP_ALLOWONLYCETIMINGS_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);

// ADl TV
typedef int ( *ADL_DISPLAY_TVCAPS_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpcaps);
typedef int ( *ADL_TV_STANDARD_SET ) (int iAdapterIndex, int iDisplayIndex, int iCurrent);
typedef int ( *ADL_TV_STANDARD_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpCurrent, int *lpDefault, int *lpSupportedStandards);

// ADL Component Video
typedef int ( *ADL_CV_DONGLESETTINGS_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpDongleSetting, int *lpOverrideSettingsSupported, int *lpCurOverrideSettings);
typedef int ( *ADL_CV_DONGLESETTINGS_SET ) (int iAdapterIndex, int iDisplayIndex, int iOverrideSettings);
typedef int ( *ADL_CV_DONGLESETTINGS_RESET ) (int iAdapterIndex, int iDisplayIndex);

// ------------------------------------------------------------------------------------------------------------

// ADL Overdrive 5
typedef int ( *ADL_OVERDRIVE5_CURRENTACTIVITY_GET ) (int iAdapterIndex, ADLPMActivity *lpActivity);
typedef int ( *ADL_OVERDRIVE5_THERMALDEVICES_ENUM ) (int iAdapterIndex, int iThermalControllerIndex, ADLThermalControllerInfo *lpThermalControllerInfo);
typedef int ( *ADL_OVERDRIVE5_TEMPERATURE_GET ) (int iAdapterIndex, int iThermalControllerIndex, ADLTemperature *lpTemperature);
typedef int ( *ADL_OVERDRIVE5_FANSPEEDINFO_GET ) (int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedInfo *lpFanSpeedInfo);
typedef int ( *ADL_OVERDRIVE5_FANSPEED_GET ) (int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedValue *lpFanSpeedValue);
typedef int ( *ADL_OVERDRIVE5_FANSPEED_SET ) (int iAdapterIndex, int iThermalControllerIndex, ADLFanSpeedValue *lpFanSpeedValue);
typedef int ( *ADL_OVERDRIVE5_FANSPEEDTODEFAULT_SET ) (int iAdapterIndex, int iThermalControllerIndex);
typedef int ( *ADL_OVERDRIVE5_ODPARAMETERS_GET ) (int iAdapterIndex, ADLODParameters *lpOdParameters);
typedef int ( *ADL_OVERDRIVE5_ODPERFORMANCELEVELS_GET ) (int iAdapterIndex, int iDefault, ADLODPerformanceLevels *lpOdPerformanceLevels);
typedef int ( *ADL_OVERDRIVE5_ODPERFORMANCELEVELS_SET ) (int iAdapterIndex, ADLODPerformanceLevels *lpOdPerformanceLevels);

// ------------------------------------------------------------------------------------------------------------

// ADL I2C
typedef int ( *ADL_DISPLAY_WRITEANDREADI2CREV_GET ) (int iAdapterIndex, int *lpMajor, int *lpMinor);
typedef int ( *ADL_DISPLAY_WRITEANDREADI2C ) (int iAdapterIndex, ADLI2C *plI2C);
typedef int ( *ADL_DISPLAY_DDCBLOCKACCESS_GET ) (int iAdapterIndex, int iDisplayIndex, int iOption, int iCommandIndex, int iSendMsgLen, char *lpucSendMsgBuf, int *lpulRecvMsgLen, char *lpucRecvMsgBuf);
typedef int ( *ADL_DISPLAY_DDCINFO_GET ) (int iAdapterIndex, int iDisplayIndex, ADLDDCInfo *lpInfo);
typedef int ( *ADL_DISPLAY_EDIDDATA_GET ) (int iAdapterIndex, int iDisplayIndex, ADLDisplayEDIDData *lpEDIDData);

// ------------------------------------------------------------------------------------------------------------

// ADL Workstation
typedef int ( *ADL_WORKSTATION_CAPS ) (int iAdapterIndex, int *lpValidBits, int *lpCaps);
typedef int ( *ADL_WORKSTATION_STEREO_GET ) (int iAdapterIndex, int *lpDefState, int *lpCurState);
typedef int ( *ADL_WORKSTATION_STEREO_SET ) (int iAdapterIndex, int iCurState);
typedef int ( *ADL_WORKSTATION_ADAPTERNUMOFGLSYNCCONNECTORS_GET ) (int iAdapterIndex, int *lpNumOfGLSyncConnectors);
typedef int ( *ADL_WORKSTATION_DISPLAYGENLOCKCAPABLE_GET ) (int iAdapterIndex, int iDisplayIndex, int *lpCanGenlock);
typedef int ( *ADL_WORKSTATION_GLSYNCMODULEDETECT_GET ) (int iAdapterIndex, int iGlSyncConnector, ADLGLSyncModuleID *lpGlSyncModuleID);
typedef int ( *ADL_WORKSTATION_GLSYNCMODULEINFO_GET ) (int iAdapterIndex, int iGlSyncConnector, int *lpNumGLSyncGPUPorts, int *lpNumGlSyncPorts, int *lpMaxSyncDelay, int *lpMaxSampleRate, ADLGLSyncPortCaps **ppGlSyncPorts);
typedef int ( *ADL_WORKSTATION_GLSYNCGENLOCKCONFIGURATION_GET ) (int iAdapterIndex, int iGlSyncConnector, int iGlValidMask, ADLGLSyncGenlockConfig *lpGlSyncGenlockConfig);
typedef int ( *ADL_WORKSTATION_GLSYNCGENLOCKCONFIGURATION_SET ) (int iAdapterIndex, int iGlSyncConnector, ADLGLSyncGenlockConfig glSyncGenlockConfig);
typedef int ( *ADL_WORKSTATION_GLSYNCPORTSTATE_GET ) (int iAdapterIndex, int iGlSyncConnector, int iGlSyncPortType, int iNumLEDs, ADLGlSyncPortInfo *lpGlSyncPortInfo, int **ppGlSyncLEDs);
typedef int ( *ADL_WORKSTATION_GLSYNCPORTSTATE_SET ) (int iAdapterIndex, int iGlSyncConnector, ADLGlSyncPortControl glSyncPortControl);
typedef int ( *ADL_WORKSTATION_DISPLAYGLSYNCMODE_GET ) (int iAdapterIndex, int iDisplayIndex, ADLGlSyncMode *lpGlSyncMode);
typedef int ( *ADL_WORKSTATION_DISPLAYGLSYNCMODE_SET ) (int iAdapterIndex, int iDisplayIndex, ADLGlSyncMode glSyncMode);
typedef int ( *ADL_WORKSTATION_GLSYNCSUPPORTEDTOPOLOGY_GET ) (int iAdapterIndex, int iNumSyncModes, ADLGlSyncMode2 *glSyncModes, int *iNumSugSyncModes, ADLGlSyncMode2 **glSugSyncModes);
typedef int ( *ADL_WORKSTATION_LOADBALANCING_GET ) (int *lpResultMask, int *lpCurResultValue, int *lpDefResultValue);
typedef int ( *ADL_WORKSTATION_LOADBALANCING_SET ) (int iCurState);
typedef int ( *ADL_WORKSTATION_LOADBALANCING_CAPS ) (int iAdapterIndex, int *lpResultMask, int *lpResultValue);

// ------------------------------------------------------------------------------------------------------------

#ifdef LINUX
// ADL Linux
typedef int ( *ADL_ADAPTER_MEMORYINFO_GET ) (int iAdapterIndex, ADLMemoryInfo *lpMemoryInfo);
typedef int ( *ADL_CONTROLLER_COLOR_SET ) (int iAdapterIndex, int iControllerIndex, ADLGamma adlGamma);
typedef int ( *ADL_CONTROLLER_COLOR_GET ) (int iAdapterIndex, int iControllerIndex, ADLGamma *lpGammaCurrent, ADLGamma *lpGammaDefault, ADLGamma *lpGammaMin, ADLGamma *lpGammaMax);
typedef int ( *ADL_DESKTOPCONFIG_GET ) (int iAdapterIndex, int *lpDesktopConfig);
typedef int ( *ADL_DESKTOPCONFIG_SET ) (int iAdapterIndex, int iDesktopConfig);
typedef int ( *ADL_NUMBEROFDISPLAYENABLE_GET ) (int iAdapterIndex, int *lpNumberOfDisplays);
typedef int ( *ADL_DISPLAYENABLE_SET ) (int iAdapterIndex, int *lpDisplayIndexList, int iDisplayListSize, int bPersistOnly);
typedef int ( *ADL_DISPLAY_IDENTIFYDISPLAY ) (int iAdapterIndex, int iDisplayIndex, int iDisplayControllerIndex, int iShow, int iDisplayNum, int iPosX, int iPosY);
typedef int ( *ADL_DISPLAY_LUTCOLOR_SET ) (int iAdapterIndex, int iDisplayIndex, ADLGamma adlGamma);
typedef int ( *ADL_DISPLAY_LUTCOLOR_GET ) (int iAdapterIndex, int iDisplayIndex, ADLGamma *lpGammaCurrent, ADLGamma *lpGammaDefault, ADLGamma *lpGammaMin, ADLGamma *lpGammaMax);
typedef int ( *ADL_ADAPTER_XSCREENINFO_GET ) (LPXScreenInfo lpXScreenInfo, int iInputSize);
typedef int ( *ADL_DISPLAY_XRANDRDISPLAYNAME_GET ) (int iAdapterIndex, int iDisplayIndex, char *lpXrandrDisplayName, int iBuffSize);
#endif
// ------------------------------------------------------------------------------------------------------------


// experimental undocumented
typedef int ( *ADL_OVERDRIVE5_POWERCONTROL_GET ) (int iAdapterIndex, int* iPercentage, int* whatever);
typedef int ( *ADL_OVERDRIVE5_POWERCONTROL_SET ) (int iAdapterIndex, int iPercentage);
//typedef int ( *ADL_OVERDRIVE5_POWERCONTROL_CAPS ) (int iAdapterIndex, int* lpCaps, int* lpValid);
//typedef int ( *ADL_OVERDRIVE5_POWERCONTROLINFO_GET) (int iAdapterIndex, ...)