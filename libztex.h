#ifndef BFG_LIBZTEX_H
#define BFG_LIBZTEX_H

#include <stdbool.h>
#include <stdint.h>

#include <libusb.h>

#include "dynclock.h"

#define LIBZTEX_MAX_DESCRIPTORS 512
#define LIBZTEX_SNSTRING_LEN 10

#define LIBZTEX_IDVENDOR 0x221A
#define LIBZTEX_IDPRODUCT 0x0100

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
	struct libusb_device_descriptor descriptor;
	libusb_device_handle *hndl; 
	unsigned char usbbus;
	unsigned char usbaddress;
	char *dev_manufacturer;
	char *dev_product;
	unsigned char snString[LIBZTEX_SNSTRING_LEN+1];
	unsigned char productId[4];
	unsigned char fwVersion;
	unsigned char interfaceVersion;
	unsigned char interfaceCapabilities[6];
	unsigned char moduleReserved[12];
	uint8_t numNonces;
	uint16_t offsNonces;
	double freqM1;	
	char* bitFileName;
	bool suspendSupported;
	double hashesPerClock;
	uint8_t extraSolutions;

	struct dclk_data dclk;

	int16_t numberOfFpgas;
	int handles;
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

enum ztex_check_result
{
	CHECK_ERROR,
	CHECK_IS_NOT_ZTEX,
	CHECK_OK,
	CHECK_RESCAN,
};

extern int libztex_scanDevices (struct libztex_dev_list ***devs);
extern void libztex_freeDevList (struct libztex_dev_list **devs);
extern enum ztex_check_result libztex_checkDevice(struct libusb_device *);
extern struct libztex_device *libztex_prepare_device2(struct libusb_device *);
extern void libztex_destroy_device (struct libztex_device* ztex);
extern int libztex_configureFpga (struct libztex_device *dev, const char *repr);
extern int libztex_setFreq (struct libztex_device *ztex, uint16_t freq, const char *repr);
extern int libztex_sendHashData (struct libztex_device *ztex, unsigned char *sendbuf);
extern int libztex_readHashData (struct libztex_device *ztex, struct libztex_hash_data nonces[]);
extern int libztex_resetFpga (struct libztex_device *ztex);
extern int libztex_selectFpga(struct libztex_device *ztex, int16_t fpgaNum);
extern int libztex_numberOfFpgas(struct libztex_device *ztex);

#endif /* __LIBZTEX_H__ */
