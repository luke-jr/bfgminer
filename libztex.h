/**
 *   libztex.h - headers for Ztex 1.15x fpga board support library
 *
 *   Copyright (c) 2012 nelisky.btc@gmail.com
 *
 *   This work is based upon the Java SDK provided by ztex which is
 *   Copyright (C) 2009-2011 ZTEX GmbH.
 *   http://www.ztex.de
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see http://www.gnu.org/licenses/.
**/
#ifndef __LIBZTEX_H__
#define __LIBZTEX_H__

#include <libusb.h>

#define LIBZTEX_MAX_DESCRIPTORS 512
#define LIBZTEX_SNSTRING_LEN 10

#define LIBZTEX_IDVENDOR 0x221A
#define LIBZTEX_IDPRODUCT 0x0100

#define LIBZTEX_MAXMAXERRORRATE 0.05
#define LIBZTEX_ERRORHYSTERESIS 0.1
#define LIBZTEX_OVERHEATTHRESHOLD 0.4

struct libztex_fpgastate {
	bool fpgaConfigured;
	unsigned char fpgaChecksum;
	uint16_t fpgaBytes;
	unsigned char fpgaInitB;
	unsigned char fpgaFlashResult;
	bool fpgaFlashBitSwap;
};

struct libztex_device {
	pthread_mutex_t	mutex;
	struct libztex_device *root;
	int16_t fpgaNum;
	struct libusb_device_descriptor descriptor;
	libusb_device_handle *hndl; 
	unsigned char usbbus;
	unsigned char usbaddress;
	unsigned char snString[LIBZTEX_SNSTRING_LEN+1];
	unsigned char productId[4];
	unsigned char fwVersion;
	unsigned char interfaceVersion;
	unsigned char interfaceCapabilities[6];
	unsigned char moduleReserved[12];
	uint8_t numNonces;
	uint16_t offsNonces;
	double freqM1;	
	uint8_t freqM;
	uint8_t freqMaxM;
	uint8_t freqMDefault;
	char* bitFileName;
	bool suspendSupported;
	double hashesPerClock;
	uint8_t extraSolutions;

	double errorCount[256];
	double errorWeight[256];
	double errorRate[256];
	double maxErrorRate[256];

	int16_t nonceCheckValid;

	int16_t numberOfFpgas;
	int selectedFpga;
	bool parallelConfigSupport;
	
	char repr[20];
};

struct libztex_dev_list { 
	struct libztex_device *dev;
	struct libztex_dev_list *next;
};

struct libztex_hash_data {
	uint32_t goldenNonce[2];
	uint32_t nonce;
	uint32_t hash7;
};

extern int libztex_scanDevices (struct libztex_dev_list ***devs);
extern void libztex_freeDevList (struct libztex_dev_list **devs);
extern int libztex_prepare_device (struct libusb_device *dev, struct libztex_device** ztex);
extern void libztex_destroy_device (struct libztex_device* ztex);
extern int libztex_configureFpga (struct libztex_device *dev);
extern int libztex_setFreq (struct libztex_device *ztex, uint16_t freq);
extern int libztex_sendHashData (struct libztex_device *ztex, unsigned char *sendbuf);
extern int libztex_readHashData (struct libztex_device *ztex, struct libztex_hash_data nonces[]);
extern int libztex_resetFpga (struct libztex_device *ztex);
extern int libztex_selectFpga(struct libztex_device *ztex);
extern int libztex_numberOfFpgas(struct libztex_device *ztex);

#endif /* __LIBZTEX_H__ */
