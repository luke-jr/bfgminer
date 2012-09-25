/**
 *   libztex.c - Ztex 1.15x fpga board support library
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

#include <stdio.h>
#include <unistd.h>

#include "miner.h"
#include "fpgautils.h"
#include "libztex.h"

#define BUFSIZE 256

//* Capability index for EEPROM support.
#define CAPABILITY_EEPROM 0,0
//* Capability index for FPGA configuration support. 
#define CAPABILITY_FPGA 0,1
//* Capability index for FLASH memory support.
#define CAPABILITY_FLASH 0,2
//* Capability index for DEBUG helper support.
#define CAPABILITY_DEBUG 0,3
//* Capability index for AVR XMEGA support.
#define CAPABILITY_XMEGA 0,4
//* Capability index for AVR XMEGA support.
#define CAPABILITY_HS_FPGA 0,5
//* Capability index for AVR XMEGA support.
#define CAPABILITY_MAC_EEPROM 0,6
//* Capability index for multi FPGA support.
#define CAPABILITY_MULTI_FPGA 0,7


static bool libztex_checkDevice(struct libusb_device *dev)
{
	struct libusb_device_descriptor desc;
	int err;

	err = libusb_get_device_descriptor(dev, &desc);
	if (unlikely(err != 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to open read descriptor with error %d", err);
		return false;
	}
	if (!(desc.idVendor == LIBZTEX_IDVENDOR && desc.idProduct == LIBZTEX_IDPRODUCT)) {
		applog(LOG_DEBUG, "Not a ZTEX device %0.4x:%0.4x", desc.idVendor, desc.idProduct);
		return false;
	}
	return true;
}

static bool libztex_checkCapability(struct libztex_device *ztex, int i, int j)
{
	if (!((i >= 0) && (i <= 5) && (j >= 0) && (j < 8) &&
	     (((ztex->interfaceCapabilities[i] & 255) & (1 << j)) != 0))) {
		applog(LOG_ERR, "%s: capability missing: %d %d", ztex->repr, i, j);
		return false;
	}
	return true;
}

static int libztex_detectBitstreamBitOrder(const unsigned char *buf, int size)
{
	int i;

	for (i = 0; i < size - 4; i++) {
		if (((buf[i] & 255) == 0xaa) && ((buf[i + 1] & 255) == 0x99) && ((buf[i + 2] & 255) == 0x55) && ((buf[i + 3] & 255) == 0x66))
			return 1;
		if (((buf[i] & 255) == 0x55) && ((buf[i + 1] & 255) == 0x99) && ((buf[i + 2] & 255) == 0xaa) && ((buf[i + 3] & 255) == 0x66))
			return 0;
	} 
	applog(LOG_WARNING, "Unable to determine bitstream bit order: no signature found");
	return 0;
}

static void libztex_swapBits(unsigned char *buf, int size)
{
	unsigned char c;
	int i;

	for (i = 0; i < size; i++) {
		c = buf[i];
		buf[i] = ((c & 128) >> 7) |
		         ((c & 64) >> 5) |
		         ((c & 32) >> 3) |
		         ((c & 16) >> 1) |
		         ((c & 8) << 1) |
		         ((c & 4) << 3) |
		         ((c & 2) << 5) |
		         ((c & 1) << 7);
	}
}

static int libztex_getFpgaState(struct libztex_device *ztex, struct libztex_fpgastate *state)
{
	unsigned char buf[9];
	int cnt;

	if (!libztex_checkCapability(ztex, CAPABILITY_FPGA))
		return -1;
	cnt = libusb_control_transfer(ztex->hndl, 0xc0, 0x30, 0, 0, buf, 9, 1000);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "%s: Failed getFpgaState with err %d", ztex->repr, cnt);
		return cnt;
	}
	state->fpgaConfigured = (buf[0] == 0);
	state->fpgaChecksum = buf[1] & 0xff;
	state->fpgaBytes = ((buf[5] & 0xff) << 24) | ((buf[4] & 0xff) << 16) | ((buf[3] & 0xff) << 8) | (buf[2] & 0xff);
	state->fpgaInitB = buf[6] & 0xff;
	state->fpgaFlashResult = buf[7];
	state->fpgaFlashBitSwap = (buf[8] != 0);
	return 0;
}

static int libztex_configureFpgaHS(struct libztex_device *ztex, const char* firmware, bool force, char bs)
{
	struct libztex_fpgastate state;
	const int transactionBytes = 65536;
	unsigned char buf[transactionBytes], settings[2];
	int tries, cnt, buf_p, i;
	ssize_t pos = 0;
	FILE *fp;

	if (!libztex_checkCapability(ztex, CAPABILITY_HS_FPGA))
		return -1;
	libztex_getFpgaState(ztex, &state);
	if (!force && state.fpgaConfigured) {
		applog(LOG_INFO, "Bitstream already configured");
		return 1;
	}
	cnt = libusb_control_transfer(ztex->hndl, 0xc0, 0x33, 0, 0, settings, 2, 1000);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "%s: Failed getHSFpgaSettings with err %d", ztex->repr, cnt);
		return cnt;
	}

	libusb_claim_interface(ztex->hndl, settings[1]);

	for (tries = 3; tries > 0; tries--) {
		fp = open_bitstream("ztex", firmware);
		if (!fp) {
			applog(LOG_ERR, "%s: failed to read firmware '%s'", ztex->repr, firmware);
			return -2;
		}

		while (pos < transactionBytes && !feof(fp)) {
			buf[pos++] = getc(fp);
		}

		if (feof(fp))
			pos--;

		if (bs != 0 && bs != 1)
			bs = libztex_detectBitstreamBitOrder(buf, transactionBytes < pos? transactionBytes: pos);


		if (bs == 1)
			libztex_swapBits(buf, pos);
	 
		libusb_control_transfer(ztex->hndl, 0x40, 0x34, 0, 0, NULL, 0, 1000);
		// 0x34 - initHSFPGAConfiguration

		buf_p = pos;
		while (1) {
			i = 0;
			while (i < buf_p) {
				if (libusb_bulk_transfer(ztex->hndl,
				                                  settings[0],
				                                  &buf[i],
				                                  buf_p - i,
				                                  &cnt, 1000) != 0) {
					applog(LOG_ERR, "%s: Failed send hs fpga data", ztex->repr);
					break;
				}
				usleep(500);
				i += cnt;
			}
			if (i < buf_p || buf_p < transactionBytes)
				break;
			buf_p = 0;
			while (buf_p < transactionBytes && !feof(fp)) {
				buf[buf_p++] = getc(fp);
			}
			if (feof(fp))
				buf_p--;
			pos += buf_p;
			if (buf_p == 0)
				break;
			if (bs == 1)
				libztex_swapBits(buf, buf_p);
		}

		libusb_control_transfer(ztex->hndl, 0x40, 0x35, 0, 0, NULL, 0, 1000);
		// 0x35 - finishHSFPGAConfiguration
		if (cnt >= 0)
			tries = 0;

		fclose(fp);

		libztex_getFpgaState(ztex, &state);
		if (!state.fpgaConfigured) {
			applog(LOG_ERR, "%s: HS FPGA configuration failed: DONE pin does not go high", ztex->repr);
			return -3;
		}

	}

	libusb_release_interface(ztex->hndl, settings[1]);

	usleep(200000);
	applog(LOG_INFO, "%s: HS FPGA configuration done", ztex->repr);
	return 0;

}

static int libztex_configureFpgaLS(struct libztex_device *ztex, const char* firmware, bool force, char bs)
{
	struct libztex_fpgastate state;
	const int transactionBytes = 2048;
	unsigned char buf[transactionBytes], cs;
	int tries, cnt, buf_p, i;
	ssize_t pos = 0;
	FILE *fp;

	if (!libztex_checkCapability(ztex, CAPABILITY_FPGA))
		return -1;

	libztex_getFpgaState(ztex, &state);
	if (!force && state.fpgaConfigured) {
		applog(LOG_DEBUG, "Bitstream already configured");
		return 1;
	}

	for (tries = 10; tries > 0; tries--) {
		fp = open_bitstream("ztex", firmware);
		if (!fp) {
			applog(LOG_ERR, "%s: failed to read firmware '%s'", ztex->repr, firmware);
			return -2;
		}

		cs = 0;
		while (pos < transactionBytes && !feof(fp)) {
			buf[pos] = getc(fp);
			cs += buf[pos++];
		}

		if (feof(fp))
			pos--;

		if (bs != 0 && bs != 1)
			bs = libztex_detectBitstreamBitOrder(buf, transactionBytes < pos? transactionBytes: pos);

		//* Reset fpga
		cnt = libztex_resetFpga(ztex);
		if (unlikely(cnt < 0)) {
			applog(LOG_ERR, "%s: Failed reset fpga with err %d", ztex->repr, cnt);
			continue;
		}

		if (bs == 1)
			libztex_swapBits(buf, pos);
	 
		buf_p = pos;
		while (1) {
			i = 0;
			while (i < buf_p) {
				cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x32, 0, 0, &buf[i], buf_p - i, 5000);
				if (unlikely(cnt < 0)) {
					applog(LOG_ERR, "%s: Failed send fpga data with err %d", ztex->repr, cnt);
					break;
				}
				i += cnt;
			}
			if (i < buf_p || buf_p < transactionBytes)
				break;
			buf_p = 0;
			while (buf_p < transactionBytes && !feof(fp)) {
				buf[buf_p] = getc(fp);
				cs += buf[buf_p++];
			}
			if (feof(fp))
				buf_p--;
			pos += buf_p;
			if (buf_p == 0)
				break;
			if (bs == 1)
				libztex_swapBits(buf, buf_p);
		}
		if (cnt >= 0)
			tries = 0;

		fclose(fp);
	}
	libztex_getFpgaState(ztex, &state);
	if (!state.fpgaConfigured) {
		applog(LOG_ERR, "%s: FPGA configuration failed: DONE pin does not go high", ztex->repr);
		return 3;
	}
	usleep(200000);
	applog(LOG_INFO, "%s: FPGA configuration done", ztex->repr);
	return 0;
}

int libztex_configureFpga(struct libztex_device *ztex)
{
	char buf[256];
	int rv;

	strcpy(buf, ztex->bitFileName);
	strcat(buf, ".bit");
	rv = libztex_configureFpgaHS(ztex, buf, true, 2); 
	if (rv != 0)
		rv = libztex_configureFpgaLS(ztex, buf, true, 2); 
	return rv;
}

int libztex_numberOfFpgas(struct libztex_device *ztex) {
	int cnt;
	unsigned char buf[3];
	if (ztex->numberOfFpgas < 0) {
		if (libztex_checkCapability(ztex, CAPABILITY_MULTI_FPGA)) {
			cnt = libusb_control_transfer(ztex->hndl, 0xc0, 0x50, 0, 0, buf, 3, 1000);
			if (unlikely(cnt < 0)) {
				applog(LOG_ERR, "%s: Failed getMultiFpgaInfo with err %d", ztex->repr, cnt);
				return cnt;
			}
			ztex->numberOfFpgas = buf[0] + 1;
			ztex->selectedFpga = -1;//buf[1];
			ztex->parallelConfigSupport = (buf[2] == 1);
		} else {
			ztex->numberOfFpgas = 1;
			ztex->selectedFpga = -1;//0;
			ztex->parallelConfigSupport = false;
		}
	}
	return ztex->numberOfFpgas;
}

int libztex_selectFpga(struct libztex_device *ztex) {
	int cnt, fpgacnt = libztex_numberOfFpgas(ztex->root);
	int number = ztex->fpgaNum;
	if (number < 0 || number >= fpgacnt) {
		applog(LOG_WARNING, "%s: Trying to select wrong fpga (%d in %d)", ztex->repr, number, fpgacnt);
		return 1;
	}
	if (ztex->root->selectedFpga != number && libztex_checkCapability(ztex->root, CAPABILITY_MULTI_FPGA)) {
		cnt = libusb_control_transfer(ztex->root->hndl, 0x40, 0x51, number, 0, NULL, 0, 500);
		if (unlikely(cnt < 0)) {
			applog(LOG_ERR, "Ztex check device: Failed to set fpga with err %d", cnt);
			return cnt;
		}
		ztex->root->selectedFpga = number;
	}
	return 0;
}

int libztex_setFreq(struct libztex_device *ztex, uint16_t freq) {
	int cnt;
	uint16_t oldfreq = ztex->freqM;

	if (freq > ztex->freqMaxM)
		freq = ztex->freqMaxM;

	cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x83, freq, 0, NULL, 0, 500);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to set frequency with err %d", cnt);
		return cnt;
	}
	ztex->freqM = freq;
	if (oldfreq > ztex->freqMaxM) 
		applog(LOG_WARNING, "%s: Frequency set to %0.2f Mhz",
		       ztex->repr, ztex->freqM1 * (ztex->freqM + 1));
	else
		applog(LOG_WARNING, "%s: Frequency change from %0.2f to %0.2f Mhz",
		       ztex->repr, ztex->freqM1 * (oldfreq + 1), ztex->freqM1 * (ztex->freqM + 1));

	return 0;
}

int libztex_resetFpga(struct libztex_device *ztex)
{
	return libusb_control_transfer(ztex->hndl, 0x40, 0x31, 0, 0, NULL, 0, 1000);
}

int libztex_suspend(struct libztex_device *ztex) {
	if (ztex->suspendSupported) {
		return libusb_control_transfer(ztex->hndl, 0x40, 0x84, 0, 0, NULL, 0, 1000);
	} else {
		return 0;
	}
}

int libztex_prepare_device(struct libusb_device *dev, struct libztex_device** ztex) {
	struct libztex_device *newdev;
	int i, cnt, err;
	unsigned char buf[64];

	newdev = malloc(sizeof(struct libztex_device));
	newdev->bitFileName = NULL;
	newdev->numberOfFpgas = -1;
	newdev->valid = false;
	newdev->hndl = NULL;
	*ztex = newdev;

	err = libusb_get_device_descriptor(dev, &newdev->descriptor);
	if (unlikely(err != 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to open read descriptor with error %d", err);
		return err;
	}

	// Check vendorId and productId
	if (!(newdev->descriptor.idVendor == LIBZTEX_IDVENDOR &&
	    newdev->descriptor.idProduct == LIBZTEX_IDPRODUCT)) {
		applog(LOG_ERR, "Not a ztex device? %0.4X, %0.4X", newdev->descriptor.idVendor, newdev->descriptor.idProduct);
		return 1;
	}

	err = libusb_open(dev, &newdev->hndl);
	if (unlikely(err != 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to open handle with error %d", err);
		return err;
	}

	cnt = libusb_get_string_descriptor_ascii (newdev->hndl, newdev->descriptor.iSerialNumber, newdev->snString,
	                                          LIBZTEX_SNSTRING_LEN + 1);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to read device snString with err %d", cnt);
		return cnt;
	}

	cnt = libusb_control_transfer(newdev->hndl, 0xc0, 0x22, 0, 0, buf, 40, 500);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to read ztex descriptor with err %d", cnt);
		return cnt;
	}
	
	if (buf[0] != 40 || buf[1] != 1 || buf[2] != 'Z' || buf[3] != 'T' || buf[4] != 'E' || buf[5] != 'X') {
		applog(LOG_ERR, "Ztex check device: Error reading ztex descriptor");
		return 2;
	}

	newdev->productId[0] = buf[6];
	newdev->productId[1] = buf[7];
	newdev->productId[2] = buf[8];
	newdev->productId[3] = buf[9];
	newdev->fwVersion = buf[10];
	newdev->interfaceVersion = buf[11];
	newdev->interfaceCapabilities[0] = buf[12];
	newdev->interfaceCapabilities[1] = buf[13];
	newdev->interfaceCapabilities[2] = buf[14];
	newdev->interfaceCapabilities[3] = buf[15];
	newdev->interfaceCapabilities[4] = buf[16];
	newdev->interfaceCapabilities[5] = buf[17];
	newdev->moduleReserved[0] = buf[18];
	newdev->moduleReserved[1] = buf[19];
	newdev->moduleReserved[2] = buf[20];
	newdev->moduleReserved[3] = buf[21];
	newdev->moduleReserved[4] = buf[22];
	newdev->moduleReserved[5] = buf[23];
	newdev->moduleReserved[6] = buf[24];
	newdev->moduleReserved[7] = buf[25];
	newdev->moduleReserved[8] = buf[26];
	newdev->moduleReserved[9] = buf[27];
	newdev->moduleReserved[10] = buf[28];
	newdev->moduleReserved[11] = buf[29];


	cnt = libusb_control_transfer(newdev->hndl, 0xc0, 0x82, 0, 0, buf, 64, 500);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "Ztex check device: Failed to read ztex descriptor with err %d", cnt);
		return cnt;
	}

	if (unlikely(buf[0] != 5)) {
		if (unlikely(buf[0] != 2 && buf[0] != 4)) {
			applog(LOG_ERR, "Invalid BTCMiner descriptor version. Firmware must be updated (%d).", buf[0]);
			return 3;
		}
		applog(LOG_WARNING, "Firmware out of date (%d).", buf[0]);
	}

	i = buf[0] > 4? 11: (buf[0] > 2? 10: 8);

	while (cnt < 64 && buf[cnt] != 0)
		cnt++;
	if (cnt < i + 1) {
		applog(LOG_ERR, "Invalid bitstream file name .");
		return 4;
	}

	newdev->bitFileName = malloc(sizeof(char) * (cnt + 1));
	memcpy(newdev->bitFileName, &buf[i], cnt);
	newdev->bitFileName[cnt] = 0;	

	newdev->numNonces = buf[1] + 1;
	newdev->offsNonces = ((buf[2] & 255) | ((buf[3] & 255) << 8)) - 10000;
	newdev->freqM1 = ((buf[4] & 255) | ((buf[5] & 255) << 8) ) * 0.01;
	newdev->freqMaxM = (buf[7] & 255);
	newdev->freqM = (buf[6] & 255);
	newdev->freqMDefault = newdev->freqM;
	newdev->suspendSupported = (buf[0] == 5);
	newdev->hashesPerClock = buf[0] > 2? (((buf[8] & 255) | ((buf[9] & 255) << 8)) + 1) / 128.0: 1.0;
	newdev->extraSolutions = buf[0] > 4? buf[10]: 0;
	
	applog(LOG_DEBUG, "PID: %d numNonces: %d offsNonces: %d freqM1: %f freqMaxM: %d freqM: %d suspendSupported: %s hashesPerClock: %f extraSolutions: %d",
	                 buf[0], newdev->numNonces, newdev->offsNonces, newdev->freqM1, newdev->freqMaxM, newdev->freqM, newdev->suspendSupported ? "T": "F", 
	                 newdev->hashesPerClock, newdev->extraSolutions);

	if (buf[0] < 4) {
		if (strncmp(newdev->bitFileName, "ztex_ufm1_15b", 13) != 0)
			newdev->hashesPerClock = 0.5;
		applog(LOG_WARNING, "HASHES_PER_CLOCK not defined, assuming %0.2f", newdev->hashesPerClock);
	}

	for (cnt=0; cnt < 255; cnt++) {
		newdev->errorCount[cnt] = 0;
		newdev->errorWeight[cnt] = 0;
		newdev->errorRate[cnt] = 0;
		newdev->maxErrorRate[cnt] = 0;
	}

	newdev->usbbus = libusb_get_bus_number(dev);
	newdev->usbaddress = libusb_get_device_address(dev);
	sprintf(newdev->repr, "ZTEX %s-1", newdev->snString);
	newdev->valid = true;
	return 0;
}

void libztex_destroy_device(struct libztex_device* ztex)
{
	if (ztex->hndl != NULL) {
		libusb_close(ztex->hndl);
		ztex->hndl = NULL;
	}
	if (ztex->bitFileName != NULL) {
		free(ztex->bitFileName);
		ztex->bitFileName = NULL;
	}
	free(ztex);
}

int libztex_scanDevices(struct libztex_dev_list*** devs_p)
{
	int usbdevices[LIBZTEX_MAX_DESCRIPTORS];
	struct libztex_dev_list **devs;
	struct libztex_device *ztex;
	int found = 0, pos = 0, err;
	libusb_device **list;
	ssize_t cnt, i = 0;

	cnt = libusb_get_device_list(NULL, &list);
	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "Ztex scan devices: Failed to list usb devices with err %d", cnt);
		return 0;
	}

	for (i = 0; i < cnt; i++) {
		if (libztex_checkDevice(list[i])) {
			// Got one!
			usbdevices[found] = i;
			found++;
		}
	}

	devs = malloc(sizeof(struct libztex_dev_list *) * found);
	if (devs == NULL) {
		applog(LOG_ERR, "Ztex scan devices: Failed to allocate memory");
		return 0;
	}

	for (i = 0; i < found; i++) {
		err = libztex_prepare_device(list[usbdevices[i]], &ztex);
		if (unlikely(err != 0))
			applog(LOG_ERR, "prepare device: %d", err);
		// check if valid
		if (!ztex->valid) {
			libztex_destroy_device(ztex);
			continue;
		}
		devs[pos] = malloc(sizeof(struct libztex_dev_list));
		devs[pos]->dev = ztex;
		devs[pos]->next = NULL;
		if (pos > 0)
			devs[pos - 1]->next = devs[pos];
		pos++;
	}

	libusb_free_device_list(list, 1);
	*devs_p = devs;
	return pos;
}

int libztex_sendHashData(struct libztex_device *ztex, unsigned char *sendbuf)
{
	int cnt, ret, len;

	if (ztex == NULL || ztex->hndl == NULL)
		return 0;
	ret = 44; len = 0;
	while (ret > 0) {
		cnt = libusb_control_transfer(ztex->hndl, 0x40, 0x80, 0, 0, sendbuf + len, ret, 1000);
		if (cnt >= 0) {
			ret -= cnt;
			len += cnt;
		} else
			break;
	}
	if (unlikely(cnt < 0))
		applog(LOG_ERR, "%s: Failed sendHashData with err %d", ztex->repr, cnt);
	
	return cnt;
}

int libztex_readHashData(struct libztex_device *ztex, struct libztex_hash_data nonces[]) {
	int bufsize = 12 + ztex->extraSolutions * 4;
	int cnt = 0, i, j, ret, len;
	unsigned char *rbuf;

	if (ztex->hndl == NULL)
		return 0;
	
	rbuf = malloc(sizeof(unsigned char) * (ztex->numNonces * bufsize));
	if (rbuf == NULL) {
		applog(LOG_ERR, "%s: Failed to allocate memory for reading nonces", ztex->repr);
		return 0;
	}
	ret = bufsize * ztex->numNonces; len = 0;
	while (ret > 0) {
		cnt = libusb_control_transfer(ztex->hndl, 0xc0, 0x81, 0, 0, rbuf + len, ret, 1000);
		if (cnt >= 0) {
			ret -= cnt;
			len += cnt;
		} else
			break;
	}

	if (unlikely(cnt < 0)) {
		applog(LOG_ERR, "%s: Failed readHashData with err %d", ztex->repr, cnt);
		free(rbuf);
		return cnt;
	}

	for (i=0; i<ztex->numNonces; i++) {
		memcpy((char*)&nonces[i].goldenNonce[0], &rbuf[i*bufsize], 4);
		nonces[i].goldenNonce[0] -= ztex->offsNonces;
		//applog(LOG_DEBUG, "W %d:0 %0.8x", i, nonces[i].goldenNonce[0]);
		
		memcpy((char*)&nonces[i].nonce, &rbuf[(i*bufsize)+4], 4);
		nonces[i].nonce -= ztex->offsNonces;
		memcpy((char*)&nonces[i].hash7, &rbuf[(i*bufsize)+8], 4);

		for (j=0; j<ztex->extraSolutions; j++) {
			memcpy((char*)&nonces[i].goldenNonce[j+1], &rbuf[(i*bufsize)+12+(j*4)], 4);
			nonces[i].goldenNonce[j+1] -= ztex->offsNonces;
			//applog(LOG_DEBUG, "W %d:%d %0.8x", i, j+1, nonces[i].goldenNonce[j+1]);
		}
	}

	free(rbuf);
	return cnt;
}

void libztex_freeDevList(struct libztex_dev_list **devs)
{
	bool done = false;
	ssize_t cnt = 0;

	while (!done) {
		if (devs[cnt]->next == NULL)
			done = true;
		free(devs[cnt++]);
	}
	free(devs);
}
