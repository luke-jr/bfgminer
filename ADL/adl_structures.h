/* The statements-of-fact provided herein are intended to be compatible with
 * AMD ADL's library. AMD is the creator and copyright holder of the ADL
 * library this interface describes, and therefore also defined this interface
 * originally.
 * These free interfaces were created by Luke Dashjr <luke+freeadl@dashjr.org>
 * As interfaces/APIs cannot be copyrighted, there is no license needed in the
 * USA and probably many other jurisdictions.
 * If your jurisdiction rules otherwise, the header is offered by Luke Dashjr
 * under the MIT license, but you are responsible for determining who your
 * jurisdiction considers to be the copyright holder in such a case.
 *
 * THE INFORMATION IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE INFORMATION OR THE USE OR OTHER DEALINGS IN THE
 * INFORMATION.
 */

#ifndef ADL_STRUCTURES_H_
#define ADL_STRUCTURES_H_

#include "adl_defines.h"

typedef struct AdapterInfo {
	int iSize;
	int iAdapterIndex;
	char strUDID[ADL_MAX_PATH];
	int iBusNumber;
	int iDeviceNumber;
	int iFunctionNumber;
	int iVendorID;
	char strAdapterName[ADL_MAX_PATH];
	char strDisplayName[ADL_MAX_PATH];
	int iPresent;

#ifdef WIN32
	int iExist;
	char strDriverPath[ADL_MAX_PATH];
	char strDriverPathExt[ADL_MAX_PATH];
	char strPNPString[ADL_MAX_PATH];
	int iOSDisplayIndex;
#elif !defined(__APPLE__) /* Linux */
	int iXScreenNum;
	int iDrvIndex;
	char strXScreenConfigName[ADL_MAX_PATH];
#endif /* Linux */
} AdapterInfo, *LPAdapterInfo;

#if !(defined(WIN32) || defined(__APPLE__))
typedef struct XScreenInfo {
	int iXScreenNum;
	char strXScreenConfigName[ADL_MAX_PATH];
} XScreenInfo, *LPXScreenInfo;
#endif /* Linux */

typedef struct ADLMemoryInfo {
	long long iMemorySize;
	char strMemoryType[ADL_MAX_PATH];
	long long iMemoryBandwidth;
} ADLMemoryInfo, *LPADLMemoryInfo;

typedef struct ADLDDCInfo {
	int ulSize;
	int ulSupportsDDC;
	int ulManufacturerID;
	int ulProductID;
	char cDisplayName[ADL_MAX_DISPLAY_NAME];
	int ulMaxHResolution;
	int ulMaxVResolution;
	int ulMaxRefresh;
	int ulPTMCx;
	int ulPTMCy;
	int ulPTMRefreshRate;
	int ulDDCInfoFlag;
} ADLDDCInfo, *LPADLDDCInfo;

typedef struct ADLGamma {
	float fRed;
	float fGreen;
	float fBlue;
} ADLGamma, *LPADLGamma;

typedef struct ADLCustomMode {
	int iFlags;
	int iModeWidth;
	int iModeHeight;
	int iBaseModeWidth;
	int iBaseModeHeight;
	int iRefreshRate;
} ADLCustomMode, *LPADLCustomMode;

typedef struct ADLGetClocksOUT {
	long ulHighCoreClock;
	long ulHighMemoryClock;
	long ulHighVddc;
	long ulCoreMin;
	long ulCoreMax;
	long ulMemoryMin;
	long ulMemoryMax;
	long ulActivityPercent;
	long ulCurrentCoreClock;
	long ulCurrentMemoryClock;
	long ulReserved;
} ADLGetClocksOUT;

typedef struct ADLDisplayConfig {
	long ulSize;
	long ulConnectorType;
	long ulDeviceData;
	long ulOverridedDeviceData;
	long ulReserved;
} ADLDisplayConfig;

typedef struct ADLDisplayID {
	int iDisplayLogicalIndex;
	int iDisplayPhysicalIndex;
	int iDisplayLogicalAdapterIndex;
	int iDisplayPhysicalAdapterIndex;
} ADLDisplayID, *LPADLDisplayID;

typedef struct ADLDisplayInfo {
	ADLDisplayID displayID;
	int iDisplayControllerIndex;
	char strDisplayName[ADL_MAX_PATH];
	char strDisplayManufacturerName[ADL_MAX_PATH];
	int iDisplayType;
	int iDisplayOutputType;
	int iDisplayConnector;
	int iDisplayInfoMask;
	int iDisplayInfoValue;
} ADLDisplayInfo, *LPADLDisplayInfo;

typedef struct ADLDisplayMode {
	int iPelsHeight;
	int iPelsWidth;
	int iBitsPerPel;
	int iDisplayFrequency;
} ADLDisplayMode;

typedef struct ADLDetailedTiming {
	int iSize;
	short sTimingFlags;
	short sHTotal;
	short sHDisplay;
	short sHSyncStart;
	short sHSyncWidth;
	short sVTotal;
	short sVDisplay;
	short sVSyncStart;
	short sVSyncWidth;
	short sPixelClock;
	short sHOverscanRight;
	short sHOverscanLeft;
	short sVOverscanBottom;
	short sVOverscanTop;
	short sOverscan8B;
	short sOverscanGR;
} ADLDetailedTiming;

typedef struct ADLDisplayModeInfo {
	int iTimingStandard;
	int iPossibleStandard;
	int iRefreshRate;
	int iPelsWidth;
	int iPelsHeight;
	ADLDetailedTiming sDetailedTiming;
} ADLDisplayModeInfo;

typedef struct ADLDisplayProperty {
	int iSize;
	int iPropertyType;
	int iExpansionMode;
	int iSupport;
	int iCurrent;
	int iDefault;
} ADLDisplayProperty;

typedef struct ADLClockInfo {
	int iCoreClock;
	int iMemoryClock;
} ADLClockInfo, *LPADLClockInfo;

typedef struct ADLI2C {
	int iSize;
	int iLine;
	int iAddress;
	int iOffset;
	int iAction;
	int iSpeed;
	int iDataSize;
	char *pcData;
} ADLI2C;

typedef struct ADLDisplayEDIDData {
	int iSize;
	int iFlag;
	int iEDIDSize;
	int iBlockIndex;
	char cEDIDData[ADL_MAX_EDIDDATA_SIZE];
	int iReserved[4];
} ADLDisplayEDIDData;

typedef struct ADLControllerOverlayInput {
	int iSize;
	int iOverlayAdjust;
	int iValue;
	int iReserved;
} ADLControllerOverlayInput;

typedef struct ADLAdjustmentinfo {
	int iDefault;
	int iMin;
	int iMax;
	int iStep;
} ADLAdjustmentinfo;

typedef struct ADLControllerOverlayInfo {
	int iSize;
	ADLAdjustmentinfo  sOverlayInfo;
	int iReserved[3];
} ADLControllerOverlayInfo;

typedef struct ADLGLSyncModuleID {
	int iModuleID;
	int iGlSyncGPUPort;
	int iFWBootSectorVersion;
	int iFWUserSectorVersion;
} ADLGLSyncModuleID , *LPADLGLSyncModuleID;

typedef struct ADLGLSyncPortCaps {
	int iPortType;
	int iNumOfLEDs;
} ADLGLSyncPortCaps, *LPADLGLSyncPortCaps;

typedef struct ADLGLSyncGenlockConfig {
	int iValidMask;
	int iSyncDelay;
	int iFramelockCntlVector;
	int iSignalSource;
	int iSampleRate;
	int iSyncField;
	int iTriggerEdge;
	int iScanRateCoeff;
} ADLGLSyncGenlockConfig, *LPADLGLSyncGenlockConfig;

typedef struct ADLGlSyncPortInfo {
	int iPortType;
	int iNumOfLEDs;
	int iPortState;
	int iFrequency;
	int iSignalType;
	int iSignalSource;
} ADLGlSyncPortInfo, *LPADLGlSyncPortInfo;

typedef struct ADLGlSyncPortControl {
	int iPortType;
	int iControlVector;
	int iSignalSource;
} ADLGlSyncPortControl;

typedef struct ADLGlSyncMode {
	int iControlVector;
	int iStatusVector;
	int iGLSyncConnectorIndex;
} ADLGlSyncMode, *LPADLGlSyncMode;

typedef struct ADLGlSyncMode2 {
	int iControlVector;
	int iStatusVector;
	int iGLSyncConnectorIndex;
	int iDisplayIndex;
} ADLGlSyncMode2, *LPADLGlSyncMode2;

typedef struct ADLInfoPacket {
	char hb0;
	char hb1;
	char hb2;
	char sb[28];
} ADLInfoPacket;

typedef struct ADLAVIInfoPacket {
	char bPB3_ITC;
	char bPB5;
} ADLAVIInfoPacket;

typedef struct ADLODClockSetting {
	int iDefaultClock;
	int iCurrentClock;
	int iMaxClock;
	int iMinClock;
	int iRequestedClock;
	int iStepClock;
} ADLODClockSetting;

typedef struct ADLAdapterODClockInfo {
	int iSize;
	int iFlags;
	ADLODClockSetting sMemoryClock;
	ADLODClockSetting sEngineClock;
} ADLAdapterODClockInfo;

typedef struct ADLAdapterODClockConfig {
	int iSize;
	int iFlags;
	int iMemoryClock;
	int iEngineClock;
} ADLAdapterODClockConfig;

typedef struct ADLPMActivity {
	int iSize;
	int iEngineClock;
	int iMemoryClock;
	int iVddc;
	int iActivityPercent;
	int iCurrentPerformanceLevel;
	int iCurrentBusSpeed;
	int iCurrentBusLanes;
	int iMaximumBusLanes;
	int iReserved;
} ADLPMActivity;

typedef struct ADLThermalControllerInfo {
	int iSize;
	int iThermalDomain;
	int iDomainIndex;
	int iFlags;
} ADLThermalControllerInfo;

typedef struct ADLTemperature {
	int iSize;
	int iTemperature;
} ADLTemperature;

typedef struct ADLFanSpeedInfo {
	int iSize;
	int iFlags;
	int iMinPercent;
	int iMaxPercent;
	int iMinRPM;
	int iMaxRPM;
} ADLFanSpeedInfo;

typedef struct ADLFanSpeedValue {
	int iSize;
	int iSpeedType;
	int iFanSpeed;
	int iFlags;
} ADLFanSpeedValue;

typedef struct ADLODParameterRange {
	int iMin;
	int iMax;
	int iStep;
} ADLODParameterRange;

typedef struct ADLODParameters {
	int iSize;
	int iNumberOfPerformanceLevels;
	int iActivityReportingSupported;
	int iDiscretePerformanceLevels;
	int iReserved;
	ADLODParameterRange sEngineClock;
	ADLODParameterRange sMemoryClock;
	ADLODParameterRange sVddc;
} ADLODParameters;

typedef struct ADLODPerformanceLevel {
	int iEngineClock;
	int iMemoryClock;
	int iVddc;
} ADLODPerformanceLevel;

typedef struct ADLODPerformanceLevels {
	int iSize;
	int iReserved;
	ADLODPerformanceLevel aLevels[1];
} ADLODPerformanceLevels;

typedef struct ADLCrossfireComb {
	int iNumLinkAdapter;
	int iAdaptLink[3];
} ADLCrossfireComb;

typedef struct ADLCrossfireInfo {
	int iErrorCode;
	int iState;
	int iSupported;
} ADLCrossfireInfo;

typedef struct ADLBiosInfo {
	char strPartNumber[ADL_MAX_PATH];
	char strVersion[ADL_MAX_PATH];
	char strDate[ADL_MAX_PATH];
} ADLBiosInfo, *LPADLBiosInfo;

typedef struct ADLAdapterLocation {
	int iBus;
	int iDevice;
	int iFunction;
} ADLAdapterLocation;

typedef struct ADLMVPUCaps {
	int iSize;
	int iAdapterCount;
	int iPossibleMVPUMasters;
	int iPossibleMVPUSlaves;
	char cAdapterPath[ADL_DL_MAX_MVPU_ADAPTERS][ADL_DL_MAX_REGISTRY_PATH];
} ADLMVPUCaps;

typedef struct ADLMVPUStatus {
	int iSize;
	int iActiveAdapterCount;
	int iStatus;
	ADLAdapterLocation aAdapterLocation[ADL_DL_MAX_MVPU_ADAPTERS];
} ADLMVPUStatus;

typedef struct ADLActivatableSource {
	int iAdapterIndex;
	int iNumActivatableSources;
	int iActivatableSourceMask;
	int iActivatableSourceValue;
} ADLActivatableSource, *LPADLActivatableSource;

typedef struct ADLMode {
	int iAdapterIndex;
	ADLDisplayID displayID;
	int iXPos;
	int iYPos;
	int iXRes;
	int iYRes;
	int iColourDepth;
	float fRefreshRate;
	int iOrientation;
	int iModeFlag;
	int iModeMask;
	int iModeValue;
} ADLMode, *LPADLMode;

typedef struct ADLDisplayTarget {
	ADLDisplayID displayID;
	int iDisplayMapIndex;
	int iDisplayTargetMask;
	int iDisplayTargetValue;
} ADLDisplayTarget, *LPADLDisplayTarget;

typedef struct tagADLBezelTransientMode {
	int iAdapterIndex;
	int iSLSMapIndex;
	int iSLSModeIndex;
	ADLMode displayMode;
	int iNumBezelOffset;
	int iFirstBezelOffsetArrayIndex;
	int iSLSBezelTransientModeMask;
	int iSLSBezelTransientModeValue;
} ADLBezelTransientMode, *LPADLBezelTransientMode;

typedef struct ADLAdapterDisplayCap {
	int iAdapterIndex;
	int iAdapterDisplayCapMask;
	int iAdapterDisplayCapValue;
} ADLAdapterDisplayCap, *LPADLAdapterDisplayCap;

typedef struct ADLDisplayMap {
	int iDisplayMapIndex;
	ADLMode displayMode;
	int iNumDisplayTarget;
	int iFirstDisplayTargetArrayIndex;
	int iDisplayMapMask;
	int iDisplayMapValue;
} ADLDisplayMap, *LPADLDisplayMap;

typedef struct ADLPossibleMap {
	int iIndex;
	int iAdapterIndex;
	int iNumDisplayMap;
	ADLDisplayMap* displayMap;
	int iNumDisplayTarget;
	ADLDisplayTarget* displayTarget;
} ADLPossibleMap, *LPADLPossibleMap;

typedef struct ADLPossibleMapping {
	int iDisplayIndex;
	int iDisplayControllerIndex;
	int iDisplayMannerSupported;
} ADLPossibleMapping, *LPADLPossibleMapping;

typedef struct ADLPossibleMapResult {
	int iIndex;
	int iPossibleMapResultMask;
	int iPossibleMapResultValue;
} ADLPossibleMapResult, *LPADLPossibleMapResult;

typedef struct ADLSLSGrid {
	int iAdapterIndex;
	int iSLSGridIndex;
	int iSLSGridRow;
	int iSLSGridColumn;
	int iSLSGridMask;
	int iSLSGridValue;
} ADLSLSGrid, *LPADLSLSGrid;

typedef struct ADLSLSMap {
	int iAdapterIndex;
	int iSLSMapIndex;
	ADLSLSGrid grid;
	int iSurfaceMapIndex;
	int iOrientation;
	int iNumSLSTarget;
	int iFirstSLSTargetArrayIndex;
	int iNumNativeMode;
	int iFirstNativeModeArrayIndex;
	int iNumBezelMode;
	int iFirstBezelModeArrayIndex;
	int iNumBezelOffset;
	int iFirstBezelOffsetArrayIndex;
	int iSLSMapMask;
	int iSLSMapValue;
} ADLSLSMap, *LPADLSLSMap;

typedef struct ADLSLSOffset {
	int iAdapterIndex;
	int iSLSMapIndex;
	ADLDisplayID displayID;
	int iBezelModeIndex;
	int iBezelOffsetX;
	int iBezelOffsetY;
	int iDisplayWidth;
	int iDisplayHeight;
	int iBezelOffsetMask;
	int iBezelffsetValue;
} ADLSLSOffset, *LPADLSLSOffset;

typedef struct ADLSLSMode {
	int iAdapterIndex;
	int iSLSMapIndex;
	int iSLSModeIndex;
	ADLMode displayMode;
	int iSLSNativeModeMask;
	int iSLSNativeModeValue;
} ADLSLSMode, *LPADLSLSMode;

typedef struct ADLPossibleSLSMap {
	int iSLSMapIndex;
	int iNumSLSMap;
	ADLSLSMap* lpSLSMap;
	int iNumSLSTarget;
	ADLDisplayTarget* lpDisplayTarget;
} ADLPossibleSLSMap, *LPADLPossibleSLSMap;

typedef struct ADLSLSTarget {
	int iAdapterIndex;
	int iSLSMapIndex;
	ADLDisplayTarget displayTarget;
	int iSLSGridPositionX;
	int iSLSGridPositionY;
	ADLMode viewSize;
	int iSLSTargetMask;
	int iSLSTargetValue;
} ADLSLSTarget, *LPADLSLSTarget;

typedef struct ADLBezelOffsetSteppingSize {
	int iAdapterIndex;
	int iSLSMapIndex;
	int iBezelOffsetSteppingSizeX;
	int iBezelOffsetSteppingSizeY;
	int iBezelOffsetSteppingSizeMask;
	int iBezelOffsetSteppingSizeValue;
} ADLBezelOffsetSteppingSize, *LPADLBezelOffsetSteppingSize;

#endif /* ADL_STRUCTURES_H_ */
