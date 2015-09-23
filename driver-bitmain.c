/*
 * Copyright 2012-2013 Lingchao Xu <lingchao.xu@bitmaintech.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#ifndef WIN32
  #include <sys/select.h>
  #include <termios.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #ifndef O_CLOEXEC
    #define O_CLOEXEC 0
  #endif
#else
  #include "compat.h"
  #include <windows.h>
  #include <io.h>
#endif

#include "elist.h"
#include "miner.h"
#include "usbutils.h"
#include "driver-bitmain.h"
#include "hexdump.c"
#include "util.h"

#define BITMAIN_CALC_DIFF1	1

#ifdef WIN32
#define BITMAIN_TEST
#endif

#define BITMAIN_TEST_PRINT_WORK 0
#ifdef BITMAIN_TEST
#define BITMAIN_TEST_NUM 19
#define BITMAIN_TEST_USENUM 1
int g_test_index = 0;
const char btm_work_test_data[BITMAIN_TEST_NUM][256] = {
		"00000002ddc1ce5579dbec17f17fbb8f31ae218a814b2a0c1900f0d90000000100000000b58aa6ca86546b07a5a46698f736c7ca9c0eedc756d8f28ac33c20cc24d792675276f879190afc85b6888022000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000eb2d45233c5b02de50ddcb9049ba16040e0ba00e9750a474eec75891571d925b52dfda4a190266667145b02f000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b19000000000000000090c7d3743e0b0562e4f56d3dd35cece3c5e8275d0abb21bf7e503cb72bd7ed3b52dfda4a190266667bbb58d7000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b1900000000000000006e0561da06022bfbb42c5ecd74a46bfd91934f201b777e9155cc6c3674724ec652dfda4a19026666a0cd827b000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b1900000000000000000312f42ce4964cc23f2d8c039f106f25ddd58e10a1faed21b3bba4b0e621807b52dfda4a1902666629c9497d000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b19000000000000000033093a6540dbe8f7f3d19e3d2af05585ac58dafad890fa9a942e977334a23d6e52dfda4a190266665ae95079000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000bd7893057d06e69705bddf9a89c7bac6b40c5b32f15e2295fc8c5edf491ea24952dfda4a190266664b89b4d3000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b19000000000000000075e66f533e53837d14236a793ee4e493985642bc39e016b9e63adf14a584a2aa52dfda4a19026666ab5d638d000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000d936f90c5db5f0fe1d017344443854fbf9e40a07a9b7e74fedc8661c23162bff52dfda4a19026666338e79cb000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000d2c1a7d279a4355b017bc0a4b0a9425707786729f21ee18add3fda4252a31a4152dfda4a190266669bc90806000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000ad36d19f33d04ca779942843890bc3b083cec83a4b60b6c45cf7d21fc187746552dfda4a1902666675d81ab7000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b19000000000000000093b809cf82b76082eacb55bc35b79f31882ed0976fd102ef54783cd24341319b52dfda4a1902666642ab4e42000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b1900000000000000007411ff315430a7bbf41de8a685d457e82d5177c05640d6a4436a40f39e99667852dfda4a190266662affa4b5000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b1900000000000000001ad0db5b9e1e2b57c8d3654c160f5a51067521eab7e340a270639d97f00a3fa252dfda4a1902666601a47bb6000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b19000000000000000022e055c442c46bbe16df68603a26891f6e4cf85b90102b39fd7cadb602b4e34552dfda4a1902666695d33cea000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b1900000000000000009c8baf5a8a1e16de2d6ae949d5fec3ed751f10dcd4c99810f2ce08040fb9e31d52dfda4a19026666fe78849d000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000e5655532b414887f35eb4652bc7b11ebac12891f65bc08cbe0ce5b277b9e795152dfda4a19026666fcc0d1d1000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000f272c5508704e2b62dd1c30ea970372c40bf00f9203f9bf69d456b4a7fbfffe352dfda4a19026666c03d4399000000800000000000000000000000000000000000000000000000000000000000000000",
		"0000000256ccc4c8aeae2b1e41490bc352893605f284e4be043f7b190000000000000000fca3b4531ba627ad9b0e23cdd84c888952c23810df196e9c6db0bcecba6a830952dfda4a19026666c14009cb000000800000000000000000000000000000000000000000000000000000000000000000"
};
const char btm_work_test_midstate[BITMAIN_TEST_NUM][256] = {
		"2d8738e7f5bcf76dcb8316fec772e20e240cd58c88d47f2d3f5a6a9547ed0a35",
		"d31b6ce09c0bfc2af6f3fe3a03475ebefa5aa191fa70a327a354b2c22f9692f1",
		"84a8c8224b80d36caeb42eff2a100f634e1ff873e83fd02ef1306a34abef9dbe",
		"059882159439b9b32968c79a93c5521e769dbea9d840f56c2a17b9ad87e530b8",
		"17fa435d05012574f8f1da26994cc87b6cb9660b5e82072dc6a0881cec150a0d",
		"92a28cc5ec4ba6a2688471dfe2032b5fe97c805ca286c503e447d6749796c6af",
		"1677a03516d6e9509ac37e273d2482da9af6e077abe8392cdca6a30e916a7ae9",
		"50bbe09f1b8ac18c97aeb745d5d2c3b5d669b6ac7803e646f65ac7b763a392d1",
		"e46a0022ebdc303a7fb1a0ebfa82b523946c312e745e5b8a116b17ae6b4ce981",
		"8f2f61e7f5b4d76d854e6d266acfff4d40347548216838ccc4ef3b9e43d3c9ea",
		"0a450588ae99f75d676a08d0326e1ea874a3497f696722c78a80c7b6ee961ea6",
		"3c4c0fc2cf040b806c51b46de9ec0dcc678a7cc5cf3eff11c6c03de3bc7818cc",
		"f6c7c785ab5daddb8f98e5f854f2cb41879fcaf47289eb2b4196fefc1b28316f",
		"005312351ccb0d0794779f5023e4335b5cad221accf0dfa3da7b881266fa9f5a",
		"7b26d189c6bba7add54143179aadbba7ccaeff6887bd8d5bec9597d5716126e6",
		"a4718f4c801e7ddf913a9474eb71774993525684ffea1915f767ab16e05e6889",
		"6b6226a8c18919d0e55684638d33a6892a00d22492cc2f5906ca7a4ac21c74a7",
		"383114dccd1cb824b869158aa2984d157fcb02f46234ceca65943e919329e697",
		"d4d478df3016852b27cb1ae9e1e98d98617f8d0943bf9dc1217f47f817236222"
};
#endif

char opt_bitmain_dev[256] = {0};
bool opt_bitmain_hwerror = false;
bool opt_bitmain_checkall = false;
bool opt_bitmain_checkn2diff = false;
bool opt_bitmain_dev_usb = true;
bool opt_bitmain_nobeeper = false;
bool opt_bitmain_notempoverctrl = false;
bool opt_bitmain_homemode = false;
int opt_bitmain_temp = BITMAIN_TEMP_TARGET;
int opt_bitmain_overheat = BITMAIN_TEMP_OVERHEAT;
int opt_bitmain_fan_min = BITMAIN_DEFAULT_FAN_MIN_PWM;
int opt_bitmain_fan_max = BITMAIN_DEFAULT_FAN_MAX_PWM;
int opt_bitmain_freq_min = BITMAIN_MIN_FREQUENCY;
int opt_bitmain_freq_max = BITMAIN_MAX_FREQUENCY;
bool opt_bitmain_auto;

static int option_offset = -1;

// --------------------------------------------------------------
//      CRC16 check table
// --------------------------------------------------------------
const uint8_t chCRCHTalbe[] =                                 // CRC high byte table
{
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
 0x00, 0xC1, 0x81, 0x40
};

const uint8_t chCRCLTalbe[] =                                 // CRC low byte table
{
 0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7,
 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E,
 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09, 0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9,
 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC,
 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
 0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32,
 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D,
 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A, 0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38,
 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF,
 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
 0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1,
 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4,
 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F, 0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB,
 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA,
 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
 0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0,
 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97,
 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C, 0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E,
 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89,
 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
 0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83,
 0x41, 0x81, 0x80, 0x40
};

static uint16_t CRC16(const uint8_t* p_data, uint16_t w_len)
{
	uint8_t chCRCHi = 0xFF; // CRC high byte initialize
	uint8_t chCRCLo = 0xFF; // CRC low byte initialize
	uint16_t wIndex = 0;    // CRC cycling index

	while (w_len--) {
		wIndex = chCRCLo ^ *p_data++;
		chCRCLo = chCRCHi ^ chCRCHTalbe[wIndex];
		chCRCHi = chCRCLTalbe[wIndex];
	}
	return ((chCRCHi << 8) | chCRCLo);
}

static uint32_t num2bit(int num) {
	switch(num) {
	case 0:  return 0x80000000;
	case 1:  return 0x40000000;
	case 2:  return 0x20000000;
	case 3:  return 0x10000000;
	case 4:  return 0x08000000;
	case 5:  return 0x04000000;
	case 6:  return 0x02000000;
	case 7:  return 0x01000000;
	case 8:  return 0x00800000;
	case 9:  return 0x00400000;
	case 10: return 0x00200000;
	case 11: return 0x00100000;
	case 12: return 0x00080000;
	case 13: return 0x00040000;
	case 14: return 0x00020000;
	case 15: return 0x00010000;
	case 16: return 0x00008000;
	case 17: return 0x00004000;
	case 18: return 0x00002000;
	case 19: return 0x00001000;
	case 20: return 0x00000800;
	case 21: return 0x00000400;
	case 22: return 0x00000200;
	case 23: return 0x00000100;
	case 24: return 0x00000080;
	case 25: return 0x00000040;
	case 26: return 0x00000020;
	case 27: return 0x00000010;
	case 28: return 0x00000008;
	case 29: return 0x00000004;
	case 30: return 0x00000002;
	case 31: return 0x00000001;
	default: return 0x00000000;
	}
}

static bool get_options(int this_option_offset, int *baud, int *chain_num,
			int *asic_num, int *timeout, int *frequency, char * frequency_t, uint8_t * reg_data, uint8_t * voltage, char * voltage_t)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2, *colon3, *colon4, *colon5, *colon6;
	size_t max;
	int i, tmp;

	if (opt_bitmain_options == NULL)
		buf[0] = '\0';
	else {
		ptr = opt_bitmain_options;
		for (i = 0; i < this_option_offset; i++) {
			comma = strchr(ptr, ',');
			if (comma == NULL)
				break;
			ptr = comma + 1;
		}

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	if (!(*buf))
		return false;

	colon = strchr(buf, ':');
	if (colon)
		*(colon++) = '\0';

	tmp = atoi(buf);
	switch (tmp) {
	case 115200:
		*baud = 115200;
		break;
	case 57600:
		*baud = 57600;
		break;
	case 38400:
		*baud = 38400;
		break;
	case 19200:
		*baud = 19200;
		break;
	default:
		quit(1, "Invalid bitmain-options for baud (%s) "
			"must be 115200, 57600, 38400 or 19200", buf);
	}

	if (colon && *colon) {
		colon2 = strchr(colon, ':');
		if (colon2)
			*(colon2++) = '\0';

		if (*colon) {
			tmp = atoi(colon);
			if (tmp > 0) {
				*chain_num = tmp;
			} else {
				quit(1, "Invalid bitmain-options for "
					"chain_num (%s) must be 1 ~ %d",
					colon, BITMAIN_DEFAULT_CHAIN_NUM);
			}
		}

		if (colon2 && *colon2) {
			colon3 = strchr(colon2, ':');
			if (colon3)
				*(colon3++) = '\0';

			tmp = atoi(colon2);
			if (tmp > 0 && tmp <= BITMAIN_DEFAULT_ASIC_NUM)
				*asic_num = tmp;
			else {
				quit(1, "Invalid bitmain-options for "
					"asic_num (%s) must be 1 ~ %d",
					colon2, BITMAIN_DEFAULT_ASIC_NUM);
			}

			if (colon3 && *colon3) {
				colon4 = strchr(colon3, ':');
				if (colon4)
					*(colon4++) = '\0';

				tmp = atoi(colon3);
				if (tmp > 0 && tmp <= 0xff)
					*timeout = tmp;
				else {
					quit(1, "Invalid bitmain-options for "
						"timeout (%s) must be 1 ~ %d",
						colon3, 0xff);
				}
				if (colon4 && *colon4) {
					colon5 = strchr(colon4, ':');
					if(colon5)
						*(colon5++) = '\0';

					tmp = atoi(colon4);
					if (tmp < BITMAIN_MIN_FREQUENCY || tmp > BITMAIN_MAX_FREQUENCY) {
						quit(1, "Invalid bitmain-options for frequency, must be %d <= frequency <= %d",
						     BITMAIN_MIN_FREQUENCY, BITMAIN_MAX_FREQUENCY);
					} else {
						*frequency = tmp;
						strcpy(frequency_t, colon4);
					}
					if (colon5 && *colon5) {
						colon6 = strchr(colon5, ':');
						if(colon6)
							*(colon6++) = '\0';

						if(strlen(colon5) > 8 || strlen(colon5)%2 != 0 || strlen(colon5)/2 == 0) {
							quit(1, "Invalid bitmain-options for reg data, must be hex now: %s",
									colon5);
						}
						memset(reg_data, 0, 4);
						if(!hex2bin(reg_data, colon5, strlen(colon5)/2)) {
							quit(1, "Invalid bitmain-options for reg data, hex2bin error now: %s",
									colon5);
						}

						if (colon6 && *colon6) {
							if(strlen(colon6) > 4 || strlen(colon6)%2 != 0 || strlen(colon6)/2 == 0) {
								quit(1, "Invalid bitmain-options for voltage data, must be hex now: %s",
									colon6);
							}
							memset(voltage, 0, 2);
							if(!hex2bin(voltage, colon6, strlen(colon6)/2)) {
								quit(1, "Invalid bitmain-options for voltage data, hex2bin error now: %s",
									colon5);
							} else {
								sprintf(voltage_t, "%02x%02x", voltage[0], voltage[1]);
								voltage_t[5] = 0;
								voltage_t[4] = voltage_t[3];
								voltage_t[3] = voltage_t[2];
								voltage_t[2] = voltage_t[1];
								voltage_t[1] = '.';
							}
						}
					}
				}
			}
		}
	}
	return true;
}

static bool get_option_freq(int *timeout, int *frequency, char * frequency_t, uint8_t * reg_data)
{
	char buf[BUFSIZ+1];
	char *ptr, *comma, *colon, *colon2;
	size_t max;
	int i, tmp;

	if (opt_bitmain_freq == NULL)
		return true;
	else {
		ptr = opt_bitmain_freq;

		comma = strchr(ptr, ',');
		if (comma == NULL)
			max = strlen(ptr);
		else
			max = comma - ptr;

		if (max > BUFSIZ)
			max = BUFSIZ;
		strncpy(buf, ptr, max);
		buf[max] = '\0';
	}

	if (!(*buf))
		return false;

	colon = strchr(buf, ':');
	if (colon)
		*(colon++) = '\0';

	tmp = atoi(buf);
	if (tmp > 0 && tmp <= 0xff)
		*timeout = tmp;
	else {
		quit(1, "Invalid bitmain-freq for "
			"timeout (%s) must be 1 ~ %d",
			buf, 0xff);
	}

	if (colon && *colon) {
		colon2 = strchr(colon, ':');
		if (colon2)
			*(colon2++) = '\0';

		tmp = atoi(colon);
		if (tmp < BITMAIN_MIN_FREQUENCY || tmp > BITMAIN_MAX_FREQUENCY) {
			quit(1, "Invalid bitmain-freq for frequency, must be %d <= frequency <= %d",
					BITMAIN_MIN_FREQUENCY, BITMAIN_MAX_FREQUENCY);
		} else {
			*frequency = tmp;
			strcpy(frequency_t, colon);
		}

		if (colon2 && *colon2) {
			if(strlen(colon2) > 8 || strlen(colon2)%2 != 0 || strlen(colon2)/2 == 0) {
				quit(1, "Invalid bitmain-freq for reg data, must be hex now: %s",
						colon2);
			}
			memset(reg_data, 0, 4);
			if(!hex2bin(reg_data, colon2, strlen(colon2)/2)) {
				quit(1, "Invalid bitmain-freq for reg data, hex2bin error now: %s",
						colon2);
			}
		}
	}
	return true;
}

static bool get_option_voltage(uint8_t * voltage, char * voltage_t)
{
	if(opt_bitmain_voltage) {
		if(strlen(opt_bitmain_voltage) > 4 || strlen(opt_bitmain_voltage)%2 != 0 || strlen(opt_bitmain_voltage)/2 == 0) {
			applog(LOG_ERR, "Invalid bitmain-voltage for voltage data, must be hex now: %s,set default_volttage",
				opt_bitmain_voltage);
			return false;
		}
		memset(voltage, 0, 2);
		if(!hex2bin(voltage, opt_bitmain_voltage, strlen(opt_bitmain_voltage)/2)) {
			quit(1, "Invalid bitmain-voltage for voltage data, hex2bin error now: %s",
					opt_bitmain_voltage);
		} else {
			sprintf(voltage_t, "%02x%02x", voltage[0], voltage[1]);
			voltage_t[5] = 0;
			voltage_t[4] = voltage_t[3];
			voltage_t[3] = voltage_t[2];
			voltage_t[2] = voltage_t[1];
			voltage_t[1] = '.';
		}
	}
	return true;
}

static int bitmain_set_txconfig(struct bitmain_txconfig_token *bm,
			    uint8_t reset, uint8_t fan_eft, uint8_t timeout_eft, uint8_t frequency_eft,
			    uint8_t voltage_eft, uint8_t chain_check_time_eft, uint8_t chip_config_eft, uint8_t hw_error_eft,
			    uint8_t beeper_ctrl, uint8_t temp_over_ctrl,uint8_t fan_home_mode,
			    uint8_t chain_num, uint8_t asic_num, uint8_t fan_pwm_data, uint8_t timeout_data,
			    uint16_t frequency, uint8_t * voltage, uint8_t chain_check_time,
			    uint8_t chip_address, uint8_t reg_address, uint8_t * reg_data)
{
	uint16_t crc = 0;
	int datalen = 0;
	uint8_t version = 0;
	uint8_t * sendbuf = (uint8_t *)bm;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_txconfig bitmain_txconfig_token is null");
		return -1;
	}

	if (unlikely(timeout_data <= 0 || asic_num <= 0 || chain_num <= 0)) {
		applog(LOG_WARNING, "bitmain_set_txconfig parameter invalid timeout_data(%d) asic_num(%d) chain_num(%d)",
				timeout_data, asic_num, chain_num);
		return -1;
	}

	datalen = sizeof(struct bitmain_txconfig_token);
	memset(bm, 0, datalen);

	bm->token_type = BITMAIN_TOKEN_TYPE_TXCONFIG;
	bm->version = version;
	bm->length = datalen-4;
	bm->length = htole16(bm->length);

	bm->reset = reset;
	bm->fan_eft = fan_eft;
	bm->timeout_eft = timeout_eft;
	bm->frequency_eft = frequency_eft;
	bm->voltage_eft = voltage_eft;
	bm->chain_check_time_eft = chain_check_time_eft;
	bm->chip_config_eft = chip_config_eft;
	bm->hw_error_eft = hw_error_eft;
	bm->beeper_ctrl = beeper_ctrl;
	bm->temp_over_ctrl = temp_over_ctrl;
	bm->fan_home_mode = fan_home_mode;

	sendbuf[4] = htole8(sendbuf[4]);
	sendbuf[5] = htole8(sendbuf[5]);

	bm->chain_num = chain_num;
	bm->asic_num = asic_num;
	bm->fan_pwm_data = fan_pwm_data;
	bm->timeout_data = timeout_data;

	bm->frequency = htole16(frequency);
	memcpy(bm->voltage, voltage, 2);
	bm->chain_check_time = chain_check_time;

	memcpy(bm->reg_data, reg_data, 4);
	bm->chip_address = chip_address;
	bm->reg_address = reg_address;

	crc = CRC16((uint8_t *)bm, datalen-2);
	bm->crc = htole16(crc);

	applog(LOG_ERR, "BTM TxConfigToken:v(%d) reset(%d) fan_e(%d) tout_e(%d) fq_e(%d) vt_e(%d) chainc_e(%d) chipc_e(%d) hw_e(%d) b_c(%d) t_c(%d) f_m(%d) mnum(%d) anum(%d) fanpwmdata(%d) toutdata(%d) freq(%d) volt(%02x%02x) chainctime(%d) regdata(%02x%02x%02x%02x) chipaddr(%02x) regaddr(%02x) crc(%04x)",
					version, reset, fan_eft, timeout_eft, frequency_eft, voltage_eft,
					chain_check_time_eft, chip_config_eft, hw_error_eft, beeper_ctrl, temp_over_ctrl,fan_home_mode,chain_num, asic_num,
					fan_pwm_data, timeout_data, frequency, voltage[0], voltage[1],
					chain_check_time, reg_data[0], reg_data[1], reg_data[2], reg_data[3], chip_address, reg_address, crc);

	return datalen;
}

static int bitmain_set_txtask(uint8_t * sendbuf,
			    unsigned int * last_work_block, struct work **works, int work_array_size, int work_array, int sendworkcount, int * sendcount)
{
	uint16_t crc = 0;
	uint32_t work_id = 0;
	uint8_t version = 0;
	int datalen = 0;
	int i = 0;
	int index = work_array;
	uint8_t new_block= 0;
	char * ob_hex = NULL;
	struct bitmain_txtask_token *bm = (struct bitmain_txtask_token *)sendbuf;
	*sendcount = 0;
	int cursendcount = 0;
	int diff = 0;
	unsigned int difftmp = 0;
	unsigned int pooldiff = 0;
	uint64_t netdifftmp = 0;
	int netdiff = 0;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_txtask bitmain_txtask_token is null");
		return -1;
	}
	if (unlikely(!works)) {
		applog(LOG_WARNING, "bitmain_set_txtask work is null");
		return -1;
	}
	memset(bm, 0, sizeof(struct bitmain_txtask_token));

	bm->token_type = BITMAIN_TOKEN_TYPE_TXTASK;
	bm->version = version;

	datalen = 10;
	applog(LOG_DEBUG, "BTM send work count %d -----", sendworkcount);
	for(i = 0; i < sendworkcount; i++) {
		if(index > work_array_size) {
			index = 0;
		}
		if(works[index]) {
			if(works[index]->work_block > *last_work_block) {
				applog(LOG_ERR, "BTM send task new block %d old(%d)", works[index]->work_block, *last_work_block);
				new_block = 1;
				*last_work_block = works[index]->work_block;
			}
#ifdef BITMAIN_TEST
			if(!hex2bin(works[index]->data, btm_work_test_data[g_test_index], 128)) {
				applog(LOG_DEBUG, "BTM send task set test data error");
			}
			if(!hex2bin(works[index]->midstate, btm_work_test_midstate[g_test_index], 32)) {
				applog(LOG_DEBUG, "BTM send task set test midstate error");
			}
			g_test_index++;
			if(g_test_index >= BITMAIN_TEST_USENUM) {
				g_test_index = 0;
			}
			applog(LOG_DEBUG, "BTM test index = %d", g_test_index);
#endif
			work_id = works[index]->id;
			bm->works[cursendcount].work_id = htole32(work_id);
			applog(LOG_DEBUG, "BTM send task work id:%d %d", bm->works[cursendcount].work_id, work_id);
			memcpy(bm->works[cursendcount].midstate, works[index]->midstate, 32);
			memcpy(bm->works[cursendcount].data2, works[index]->data + 64, 12);

			if(cursendcount == 0) {
				pooldiff = (unsigned int)(works[index]->sdiff);
				difftmp = pooldiff;
				while(1) {
					difftmp = difftmp >> 1;
					if(difftmp > 0) {
						diff++;
						if(diff >= 255) {
							break;
						}
					} else {
						break;
					}
				}
			}

			if(BITMAIN_TEST_PRINT_WORK) {
				ob_hex = bin2hex(works[index]->data, 76);
				applog(LOG_ERR, "work %d data: %s", works[index]->id, ob_hex);
				free(ob_hex);
			}

			cursendcount++;
		}
		index++;
	}
	if(cursendcount <= 0) {
		applog(LOG_ERR, "BTM send work count %d", cursendcount);
		return 0;
	}
	
	netdifftmp = current_diff;
	while(netdifftmp > 0) {
		netdifftmp = netdifftmp >> 1;
		netdiff++;
	}
	datalen += 48*cursendcount;

	bm->length = datalen-4;
	bm->length = htole16(bm->length);
	//len = datalen-3;
	//len = htole16(len);
	//memcpy(sendbuf+1, &len, 2);
	bm->new_block = new_block;
	bm->diff = diff;
	bm->net_diff = htole16(netdiff);

	sendbuf[4] = htole8(sendbuf[4]);

	applog(LOG_DEBUG, "BitMain TxTask Token: %d %d %02x%02x%02x%02x%02x%02x",
				datalen, bm->length, sendbuf[0],sendbuf[1],sendbuf[2],sendbuf[3],sendbuf[4],sendbuf[5]);

	*sendcount = cursendcount;

	crc = CRC16(sendbuf, datalen-2);
	crc = htole16(crc);
	memcpy(sendbuf+datalen-2, &crc, 2);

	applog(LOG_DEBUG, "BitMain TxTask Token: v(%d) new_block(%d) diff(%d pool:%d net:%d) work_num(%d) crc(%04x)",
						version, new_block, diff, pooldiff,netdiff, cursendcount, crc);
	applog(LOG_DEBUG, "BitMain TxTask Token: %d %d %02x%02x%02x%02x%02x%02x",
			datalen, bm->length, sendbuf[0],sendbuf[1],sendbuf[2],sendbuf[3],sendbuf[4],sendbuf[5]);

	return datalen;
}

static int bitmain_set_rxstatus(struct bitmain_rxstatus_token *bm,
		uint8_t chip_status_eft, uint8_t detect_get, uint8_t chip_address, uint8_t reg_address)
{
	uint16_t crc = 0;
	uint8_t version = 0;
	int datalen = 0;
	uint8_t * sendbuf = (uint8_t *)bm;

	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_set_rxstatus bitmain_rxstatus_token is null");
		return -1;
	}

	datalen = sizeof(struct bitmain_rxstatus_token);
	memset(bm, 0, datalen);

	bm->token_type = BITMAIN_TOKEN_TYPE_RXSTATUS;
	bm->version = version;
	bm->length = datalen-4;
	bm->length = htole16(bm->length);

	bm->chip_status_eft = chip_status_eft;
	bm->detect_get = detect_get;

	sendbuf[4] = htole8(sendbuf[4]);

	bm->chip_address = chip_address;
	bm->reg_address = reg_address;

	crc = CRC16((uint8_t *)bm, datalen-2);
	bm->crc = htole16(crc);

	applog(LOG_ERR, "BitMain RxStatus Token: v(%d) chip_status_eft(%d) detect_get(%d) chip_address(%02x) reg_address(%02x) crc(%04x)",
				version, chip_status_eft, detect_get, chip_address, reg_address, crc);

	return datalen;
}

static int bitmain_parse_rxstatus(const uint8_t * data, int datalen, struct bitmain_rxstatus_data *bm)
{
	uint16_t crc = 0;
	uint8_t version = 0;
	int i = 0, j = 0;
	int asic_num = 0;
	int dataindex = 0;
	uint8_t tmp = 0x01;
	if (unlikely(!bm)) {
		applog(LOG_WARNING, "bitmain_parse_rxstatus bitmain_rxstatus_data is null");
		return -1;
	}
	if (unlikely(!data || datalen <= 0)) {
		applog(LOG_WARNING, "bitmain_parse_rxstatus parameter invalid data is null or datalen(%d) error", datalen);
		return -1;
	}
	memset(bm, 0, sizeof(struct bitmain_rxstatus_data));
	memcpy(bm, data, 28);
	if (bm->data_type != BITMAIN_DATA_TYPE_RXSTATUS) {
		applog(LOG_ERR, "bitmain_parse_rxstatus datatype(%02x) error", bm->data_type);
		return -1;
	}
	if (bm->version != version) {
		applog(LOG_ERR, "bitmain_parse_rxstatus version(%02x) error", bm->version);
		return -1;
	}
	bm->length = htole16(bm->length);
	if (bm->length+4 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxstatus length(%d) datalen(%d) error", bm->length, datalen);
		return -1;
	}
	crc = CRC16(data, datalen-2);
	memcpy(&(bm->crc), data+datalen-2, 2);
	bm->crc = htole16(bm->crc);
	if(crc != bm->crc) {
		applog(LOG_ERR, "bitmain_parse_rxstatus check crc(%d) != bm crc(%d) datalen(%d)", crc, bm->crc, datalen);
		return -1;
	}
	bm->fifo_space = htole16(bm->fifo_space);
	bm->fan_exist = htole16(bm->fan_exist);
	bm->temp_exist = htole32(bm->temp_exist);
	bm->nonce_error = htole32(bm->nonce_error);
	if(bm->chain_num > BITMAIN_MAX_CHAIN_NUM) {
		applog(LOG_ERR, "bitmain_parse_rxstatus chain_num=%d error", bm->chain_num);
		return -1;
	}
	dataindex = 28;
	if(bm->chain_num > 0) {
		memcpy(bm->chain_asic_num, data+datalen-2-bm->chain_num-bm->temp_num-bm->fan_num, bm->chain_num);
	}
	for(i = 0; i < bm->chain_num; i++) {
		asic_num = bm->chain_asic_num[i];
		if(asic_num <= 0) {
			asic_num = 1;
		} else {
			if(asic_num % 32 == 0) {
				asic_num = asic_num / 32;
			} else {
				asic_num = asic_num / 32 + 1;
			}
		}
		memcpy((uint8_t *)bm->chain_asic_exist+i*32, data+dataindex, asic_num*4);
		dataindex += asic_num*4;
	}
	for(i = 0; i < bm->chain_num; i++) {
		asic_num = bm->chain_asic_num[i];
		if(asic_num <= 0) {
			asic_num = 1;
		} else {
			if(asic_num % 32 == 0) {
				asic_num = asic_num / 32;
			} else {
				asic_num = asic_num / 32 + 1;
			}
		}
		memcpy((uint8_t *)bm->chain_asic_status+i*32, data+dataindex, asic_num*4);
		dataindex += asic_num*4;
	}
	dataindex += bm->chain_num;
	if(dataindex + bm->temp_num + bm->fan_num + 2 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxstatus dataindex(%d) chain_num(%d) temp_num(%d) fan_num(%d) not match datalen(%d)",
				dataindex, bm->chain_num, bm->temp_num, bm->fan_num, datalen);
		return -1;
	}
	for(i = 0; i < bm->chain_num; i++) {
		//bm->chain_asic_status[i] = swab32(bm->chain_asic_status[i]);
		for(j = 0; j < 8; j++) {
			bm->chain_asic_exist[i*8+j] = htole32(bm->chain_asic_exist[i*8+j]);
			bm->chain_asic_status[i*8+j] = htole32(bm->chain_asic_status[i*8+j]);
		}
	}
	if(bm->temp_num > 0) {
		memcpy(bm->temp, data+dataindex, bm->temp_num);
		dataindex += bm->temp_num;
	}
	if(bm->fan_num > 0) {
		memcpy(bm->fan, data+dataindex, bm->fan_num);
		dataindex += bm->fan_num;
	}
	if(!opt_bitmain_checkall){
		if(tmp != htole8(tmp)){
			applog(LOG_ERR, "BitMain RxStatus   byte4 0x%02x chip_value_eft %d reserved %d get_blk_num %d ",*((uint8_t* )bm +4),bm->chip_value_eft,bm->reserved1,bm->get_blk_num);
			memcpy(&tmp,data+4,1);
			bm->chip_value_eft = tmp >>7;
			bm->get_blk_num = tmp >> 4;
			bm->reserved1 = ((tmp << 4) & 0xff) >> 5;
		}	
		found_blocks = bm->get_blk_num;	
		applog(LOG_ERR, "BitMain RxStatus tmp :0x%02x  byte4 0x%02x chip_value_eft %d reserved %d get_blk_num %d ",tmp,*((uint8_t* )bm +4),bm->chip_value_eft,bm->reserved1,bm->get_blk_num);
	}
	applog(LOG_DEBUG, "BitMain RxStatusData: chipv_e(%d) chainnum(%d) fifos(%d) v1(%d) v2(%d) v3(%d) v4(%d) fann(%d) tempn(%d) fanet(%04x) tempet(%08x) ne(%d) regvalue(%d) crc(%04x)",
			bm->chip_value_eft, bm->chain_num, bm->fifo_space, bm->hw_version[0], bm->hw_version[1], bm->hw_version[2], bm->hw_version[3], bm->fan_num, bm->temp_num, bm->fan_exist, bm->temp_exist, bm->nonce_error, bm->reg_value, bm->crc);
	applog(LOG_DEBUG, "BitMain RxStatus Data chain info:");
	for(i = 0; i < bm->chain_num; i++) {
		applog(LOG_DEBUG, "BitMain RxStatus Data chain(%d) asic num=%d asic_exist=%08x asic_status=%08x", i+1, bm->chain_asic_num[i], bm->chain_asic_exist[i*8], bm->chain_asic_status[i*8]);
	}
	applog(LOG_DEBUG, "BitMain RxStatus Data temp info:");
	for(i = 0; i < bm->temp_num; i++) {
		applog(LOG_DEBUG, "BitMain RxStatus Data temp(%d) temp=%d", i+1, bm->temp[i]);
	}
	applog(LOG_DEBUG, "BitMain RxStatus Data fan info:");
	for(i = 0; i < bm->fan_num; i++) {
		applog(LOG_DEBUG, "BitMain RxStatus Data fan(%d) fan=%d", i+1, bm->fan[i]);
	}
	return 0;
}

static int bitmain_parse_rxnonce(const uint8_t * data, int datalen, struct bitmain_rxnonce_data *bm, int * nonce_num)
{
	int i = 0;
	uint16_t crc = 0;
	uint8_t version = 0;
	int curnoncenum = 0;
	if (unlikely(!bm)) {
		applog(LOG_ERR, "bitmain_parse_rxnonce bitmain_rxstatus_data null");
		return -1;
	}
	if (unlikely(!data || datalen <= 0)) {
		applog(LOG_ERR, "bitmain_parse_rxnonce data null or datalen(%d) error", datalen);
		return -1;
	}
	memcpy(bm, data, sizeof(struct bitmain_rxnonce_data));
	if (bm->data_type != BITMAIN_DATA_TYPE_RXNONCE) {
		applog(LOG_ERR, "bitmain_parse_rxnonce datatype(%02x) error", bm->data_type);
		return -1;
	}
	if (bm->version != version) {
		applog(LOG_ERR, "bitmain_parse_rxnonce version(%02x) error", bm->version);
		return -1;
	}
	bm->length = htole16(bm->length);
	if (bm->length+4 != datalen) {
		applog(LOG_ERR, "bitmain_parse_rxnonce length(%d) error", bm->length);
		return -1;
	}
	crc = CRC16(data, datalen-2);
	memcpy(&(bm->crc), data+datalen-2, 2);
	bm->crc = htole16(bm->crc);
	if(crc != bm->crc) {
		applog(LOG_ERR, "bitmain_parse_rxnonce check crc(%d) != bm crc(%d) datalen(%d)", crc, bm->crc, datalen);
		return -1;
	}
	bm->fifo_space = htole16(bm->fifo_space);
	bm->diff = htole16(bm->diff);
	bm->total_nonce_num = htole64(bm->total_nonce_num);
	curnoncenum = (datalen-14)/8;
	applog(LOG_DEBUG, "BitMain RxNonce Data: nonce_num(%d) fifo_space(%d) diff(%d) tnn(%lld)", curnoncenum, bm->fifo_space, bm->diff, bm->total_nonce_num);
	for(i = 0; i < curnoncenum; i++) {
		bm->nonces[i].work_id = htole32(bm->nonces[i].work_id);
		bm->nonces[i].nonce = htole32(bm->nonces[i].nonce);

		applog(LOG_DEBUG, "BitMain RxNonce Data %d: work_id(%d) nonce(%08x)(%d)",
				i, bm->nonces[i].work_id, bm->nonces[i].nonce, bm->nonces[i].nonce);
	}
	*nonce_num = curnoncenum;
	return 0;
}

static int bitmain_read(struct cgpu_info *bitmain, unsigned char *buf,
		       size_t bufsize, int timeout, int ep)
{
	int err = 0, readlen = 0;
	size_t total = 0;

	if(bitmain == NULL || buf == NULL || bufsize <= 0) {
		applog(LOG_WARNING, "bitmain_read parameter error bufsize(%d)", bufsize);
		return -1;
	}
	if(opt_bitmain_dev_usb) {
#ifdef WIN32
		char readbuf[BITMAIN_READBUF_SIZE];
		int ofs = 2, cp = 0;

		err = usb_read_once_timeout(bitmain, readbuf, bufsize, &readlen, timeout, ep);
		applog(LOG_DEBUG, "%s%i: Get bitmain read got readlen %d err %d",
			bitmain->drv->name, bitmain->device_id, readlen, err);

		if (readlen < 2)
			goto out;

		while (readlen > 2) {
			cp = readlen - 2;
			if (cp > 62)
				cp = 62;
			memcpy(&buf[total], &readbuf[ofs], cp);
			total += cp;
			readlen -= cp + 2;
			ofs += 64;
		}
#else
		err = usb_read_once_timeout(bitmain, buf, bufsize, &readlen, timeout, ep);
		applog(LOG_DEBUG, "%s%i: Get bitmain read got readlen %d err %d",
			bitmain->drv->name, bitmain->device_id, readlen, err);
		total = readlen;
#endif
	} else {
		err = btm_read(bitmain, buf, bufsize);
		total = err;
	}
out:
	return total;
}

static int bitmain_write(struct cgpu_info *bitmain, char *buf, ssize_t len, int ep)
{
	int err, amount;
	if(opt_bitmain_dev_usb) {
		err = usb_write(bitmain, buf, len, &amount, ep);
		applog(LOG_DEBUG, "%s%i: usb_write got err %d", bitmain->drv->name,
				bitmain->device_id, err);

		if (unlikely(err != 0)) {
			applog(LOG_ERR, "usb_write error on bitmain_write err=%d", err);
			return BTM_SEND_ERROR;
		}
		if (amount != len) {
			applog(LOG_ERR, "usb_write length mismatch on bitmain_write amount=%d len=%d", amount, len);
			return BTM_SEND_ERROR;
		}
	} else {
		int havelen = 0;
		while(havelen < len) {
			err = btm_write(bitmain, buf+havelen, len-havelen);
			if(err < 0) {
				applog(LOG_DEBUG, "%s%i: btm_write got err %d", bitmain->drv->name,
						bitmain->device_id, err);
				applog(LOG_WARNING, "usb_write error on bitmain_write");
				return BTM_SEND_ERROR;
			} else {
				havelen += err;
			}
		}
	}
	return BTM_SEND_OK;
}

static int bitmain_send_data(const uint8_t * data, int datalen, struct cgpu_info *bitmain)
{
	int delay, ret, ep = C_BITMAIN_SEND;
	struct bitmain_info *info = NULL;
	cgtimer_t ts_start;

	if(datalen <= 0) {
		return 0;
	}

	if(data[0] == BITMAIN_TOKEN_TYPE_TXCONFIG) {
		ep = C_BITMAIN_TOKEN_TXCONFIG;
	} else if(data[0] == BITMAIN_TOKEN_TYPE_TXTASK) {
		ep = C_BITMAIN_TOKEN_TXTASK;
	} else if(data[0] == BITMAIN_TOKEN_TYPE_RXSTATUS) {
		ep = C_BITMAIN_TOKEN_RXSTATUS;
	}

	info = bitmain->device_data;
	//delay = datalen * 10 * 1000000;
	//delay = delay / info->baud;
	//delay += 4000;

	if(opt_debug) {
		applog(LOG_DEBUG, "BitMain: Sent(%d):", datalen);
		hexdump(data, datalen);
	}

	//cgsleep_prepare_r(&ts_start);
	//applog(LOG_DEBUG, "----bitmain_send_data  start");
	ret = bitmain_write(bitmain, (char *)data, datalen, ep);
	applog(LOG_DEBUG, "----bitmain_send_data  stop ret=%d datalen=%d", ret, datalen);
	//cgsleep_us_r(&ts_start, delay);

	//applog(LOG_DEBUG, "BitMain: Sent: Buffer delay: %dus", delay);

	return ret;
}

static bool bitmain_decode_nonce(struct thr_info *thr, struct cgpu_info *bitmain,
				struct bitmain_info *info, uint32_t nonce, struct work *work)
{
	info = bitmain->device_data;
	//info->matching_work[work->subid]++;
	if(opt_bitmain_hwerror) {
		applog(LOG_DEBUG, "BitMain: submit direct nonce = %08x", nonce);
		if(opt_bitmain_checkall) {
			applog(LOG_DEBUG, "BitMain check all");
			return submit_nonce(thr, work, nonce);
		} else {
			if(opt_bitmain_checkn2diff) {
				int diff = 0;
				diff = work->sdiff;
				if(diff&&(diff&(diff-1))) {
					applog(LOG_DEBUG, "BitMain %d not diff 2 submit_nonce", diff);
					return submit_nonce(thr, work, nonce);
				} else {
					applog(LOG_DEBUG, "BitMain %d diff 2 submit_nonce_direct", diff);
					return submit_nonce_direct(thr, work, nonce);
				}
			} else {
				return submit_nonce_direct(thr, work, nonce);
			}
		}
	} else {
		applog(LOG_DEBUG, "BitMain: submit nonce = %08x", nonce);
		return submit_nonce(thr, work, nonce);
	}
}

static void bitmain_inc_nvw(struct bitmain_info *info, struct thr_info *thr)
{
	applog(LOG_INFO, "%s%d: No matching work - HW error",
	       thr->cgpu->drv->name, thr->cgpu->device_id);

	inc_hw_errors(thr);
	info->no_matching_work++;
}

static inline void record_temp_fan(struct bitmain_info *info, struct bitmain_rxstatus_data *bm, double *temp_avg)
{
	int i = 0;
	int maxfan = 0, maxtemp = 0;
	*temp_avg = 0;

	info->fan_num = bm->fan_num;
	for(i = 0; i < bm->fan_num; i++) {
		info->fan[i] = bm->fan[i] * BITMAIN_FAN_FACTOR;

		if(info->fan[i] > maxfan)
			maxfan = info->fan[i];
	}
	info->temp_num = bm->temp_num;
	for(i = 0; i < bm->temp_num; i++) {
		info->temp[i] = bm->temp[i];
		/*
		if(bm->temp[i] & 0x80) {
			bm->temp[i] &= 0x7f;
			info->temp[i] = 0 - ((~bm->temp[i] & 0x7f) + 1);
		}*/
		*temp_avg += info->temp[i];

		if(info->temp[i] > info->temp_max) {
			info->temp_max = info->temp[i];
		}
		if(info->temp[i] > maxtemp)
			maxtemp = info->temp[i];
	}

	if(bm->temp_num > 0) {
		*temp_avg = *temp_avg / bm->temp_num;
		info->temp_avg = *temp_avg;
	}

	inc_dev_status(maxfan, maxtemp);
}

static void bitmain_update_temps(struct cgpu_info *bitmain, struct bitmain_info *info,
				struct bitmain_rxstatus_data *bm)
{
	char tmp[64] = {0};
	char msg[10240] = {0};
	int i = 0;
	record_temp_fan(info, bm, &(bitmain->temp));

	strcpy(msg, "BitMain: ");
	for(i = 0; i < bm->fan_num; i++) {
		if(i != 0) {
			strcat(msg, ", ");
		}
		sprintf(tmp, "Fan%d: %d/m", i+1, info->fan[i]);
		strcat(msg, tmp);
	}
	strcat(msg, "\t");
	for(i = 0; i < bm->temp_num; i++) {
		if(i != 0) {
			strcat(msg, ", ");
		}
		sprintf(tmp, "Temp%d: %dC", i+1, info->temp[i]);
		strcat(msg, tmp);
	}
	sprintf(tmp, ", TempMAX: %dC", info->temp_max);
	strcat(msg, tmp);
	applog(LOG_INFO, msg);
	info->temp_history_index++;
	info->temp_sum += bitmain->temp;
	applog(LOG_DEBUG, "BitMain: temp_index: %d, temp_count: %d, temp_old: %d",
		info->temp_history_index, info->temp_history_count, info->temp_old);
	if (info->temp_history_index == info->temp_history_count) {
		info->temp_history_index = 0;
		info->temp_sum = 0;
	}
	if (unlikely(info->temp_old >= opt_bitmain_overheat)) {
		applog(LOG_WARNING, "BTM%d overheat! Idling", bitmain->device_id);
		info->overheat = true;
	} else if (info->overheat && info->temp_old <= opt_bitmain_temp) {
		applog(LOG_WARNING, "BTM%d cooled, restarting", bitmain->device_id);
		info->overheat = false;
	}
}

extern void cg_logwork_uint32(struct work *work, uint32_t nonce, bool ok);

static void bitmain_parse_results(struct cgpu_info *bitmain, struct bitmain_info *info,
				 struct thr_info *thr, uint8_t *buf, int *offset)
{
	int i, j, n, m, r, errordiff, spare = BITMAIN_READ_SIZE;
	uint32_t checkbit = 0x00000000;
	bool found = false;
	struct work *work = NULL;
	char * ob_hex = NULL;
	struct bitmain_packet_head packethead;
	int asicnum = 0;
	int idiff = 0;
	int mod = 0,tmp = 0;

	for (i = 0; i <= spare; i++) {
		if(buf[i] == 0xa1) {
			struct bitmain_rxstatus_data rxstatusdata;
			applog(LOG_DEBUG, "bitmain_parse_results RxStatus Data");
			if(*offset < 4) {
				return;
			}
			memcpy(&packethead, buf+i, sizeof(struct bitmain_packet_head));
			packethead.length = htole16(packethead.length);
			if(packethead.length > 1130) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxstatus datalen=%d error", packethead.length+4);
				continue;
			}
			if(*offset < packethead.length + 4) {
				return;
			}
			if(bitmain_parse_rxstatus(buf+i, packethead.length+4, &rxstatusdata) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxstatus error len=%d", packethead.length+4);
			} else {
				mutex_lock(&info->qlock);
				info->chain_num = rxstatusdata.chain_num;
				info->fifo_space = rxstatusdata.fifo_space;
				info->hw_version[0] = rxstatusdata.hw_version[0];
				info->hw_version[1] = rxstatusdata.hw_version[1];
				info->hw_version[2] = rxstatusdata.hw_version[2];
				info->hw_version[3] = rxstatusdata.hw_version[3];
				info->nonce_error = rxstatusdata.nonce_error;
				errordiff = info->nonce_error-info->last_nonce_error;
				//sprintf(g_miner_version, "%d.%d.%d.%d", info->hw_version[0], info->hw_version[1], info->hw_version[2], info->hw_version[3]);
				applog(LOG_ERR, "bitmain_parse_results v=%d chain=%d fifo=%d hwv1=%d hwv2=%d hwv3=%d hwv4=%d nerr=%d-%d freq=%d chain info:",
						rxstatusdata.version, info->chain_num, info->fifo_space, info->hw_version[0], info->hw_version[1], info->hw_version[2], info->hw_version[3],
						info->last_nonce_error, info->nonce_error, info->frequency);
				memcpy(info->chain_asic_exist, rxstatusdata.chain_asic_exist, BITMAIN_MAX_CHAIN_NUM*32);
				memcpy(info->chain_asic_status, rxstatusdata.chain_asic_status, BITMAIN_MAX_CHAIN_NUM*32);
				for(n = 0; n < rxstatusdata.chain_num; n++) {
					info->chain_asic_num[n] = rxstatusdata.chain_asic_num[n];
					memset(info->chain_asic_status_t[n], 0, 320);
					j = 0;

					mod = 0;
					if(info->chain_asic_num[n] <= 0) {
						asicnum = 0;
					} else {
						mod = info->chain_asic_num[n] % 32;
						if(mod == 0) {
							asicnum = info->chain_asic_num[n] / 32;
						} else {
							asicnum = info->chain_asic_num[n] / 32 + 1;
						}
					}
					if(asicnum > 0) {
							for(m = asicnum-1; m >= 0; m--) {
							tmp = mod ? (32-mod): 0;
							for(r = tmp;r < 32;r++){
								if((r-tmp)%8 == 0 && (r-tmp) !=0){
											info->chain_asic_status_t[n][j] = ' ';
											j++;
										}
										checkbit = num2bit(r);
										if(rxstatusdata.chain_asic_exist[n*8+m] & checkbit) {
											if(rxstatusdata.chain_asic_status[n*8+m] & checkbit) {
												info->chain_asic_status_t[n][j] = 'o';
											} else {
												info->chain_asic_status_t[n][j] = 'x';
											}
										} else {
											info->chain_asic_status_t[n][j] = '-';
										}
										j++;
									}
									info->chain_asic_status_t[n][j] = ' ';
									j++;
								mod = 0;
						}
					}
					applog(LOG_DEBUG, "bitmain_parse_results chain(%d) asic_num=%d asic_exist=%08x%08x%08x%08x%08x%08x%08x%08x asic_status=%08x%08x%08x%08x%08x%08x%08x%08x",
						n, info->chain_asic_num[n],
						info->chain_asic_exist[n*8+0], info->chain_asic_exist[n*8+1], info->chain_asic_exist[n*8+2], info->chain_asic_exist[n*8+3], info->chain_asic_exist[n*8+4], info->chain_asic_exist[n*8+5], info->chain_asic_exist[n*8+6], info->chain_asic_exist[n*8+7],
						info->chain_asic_status[n*8+0], info->chain_asic_status[n*8+1], info->chain_asic_status[n*8+2], info->chain_asic_status[n*8+3], info->chain_asic_status[n*8+4], info->chain_asic_status[n*8+5], info->chain_asic_status[n*8+6], info->chain_asic_status[n*8+7]);
					applog(LOG_ERR, "bitmain_parse_results chain(%d) asic_num=%d asic_status=%s", n, info->chain_asic_num[n], info->chain_asic_status_t[n]);
				}
				mutex_unlock(&info->qlock);

				if(errordiff > 0) {
					for(j = 0; j < errordiff; j++) {
						bitmain_inc_nvw(info, thr);
					}
					mutex_lock(&info->qlock);
					info->last_nonce_error += errordiff;
					mutex_unlock(&info->qlock);
				}
				bitmain_update_temps(bitmain, info, &rxstatusdata);
			}

			found = true;
			spare = packethead.length + 4 + i;
			if(spare > *offset) {
				applog(LOG_ERR, "bitmain_parse_rxresults space(%d) > offset(%d)", spare, *offset);
				spare = *offset;
			}
			break;
		} else if(buf[i] == 0xa2) {
			struct bitmain_rxnonce_data rxnoncedata;
			int nonce_num = 0;
			applog(LOG_DEBUG, "bitmain_parse_results RxNonce Data");
			if(*offset < 4) {
				return;
			}
			memcpy(&packethead, buf+i, sizeof(struct bitmain_packet_head));
			packethead.length = htole16(packethead.length);
			if(packethead.length > 1030) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce datalen=%d error", packethead.length+4);
				continue;
			}
			if(*offset < packethead.length + 4) {
				return;
			}
			if(bitmain_parse_rxnonce(buf+i, packethead.length+4, &rxnoncedata, &nonce_num) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce error len=%d", packethead.length+4);
			} else {
				struct pool * pool = NULL;
				for(j = 0; j < nonce_num; j++) {
					work = clone_queued_work_byid(bitmain, rxnoncedata.nonces[j].work_id);
					if(work) {
						pool = work->pool;
						if(BITMAIN_TEST_PRINT_WORK) {
							applog(LOG_ERR, "bitmain_parse_results nonce find work(%d-%d)(%08x)", work->id, rxnoncedata.nonces[j].work_id, rxnoncedata.nonces[j].nonce);

							ob_hex = bin2hex(work->midstate, 32);
							applog(LOG_ERR, "work %d midstate: %s", work->id, ob_hex);
							free(ob_hex);

							ob_hex = bin2hex(work->data+64, 12);
							applog(LOG_ERR, "work %d data2: %s", work->id, ob_hex);
							free(ob_hex);
						}

						if(work->work_block < info->last_work_block) {
							applog(LOG_ERR, "BitMain: bitmain_parse_rxnonce work(%d) nonce stale", rxnoncedata.nonces[j].work_id);
						} else {
							if (bitmain_decode_nonce(thr, bitmain, info, rxnoncedata.nonces[j].nonce, work)) {
								cg_logwork_uint32(work, rxnoncedata.nonces[j].nonce, true);
								if(opt_bitmain_hwerror) {
#ifndef BITMAIN_CALC_DIFF1
									mutex_lock(&info->qlock);
									idiff = (int)work->sdiff;
									info->nonces+=idiff;
									info->auto_nonces+=idiff;
									mutex_unlock(&info->qlock);
									inc_work_status(thr, pool, idiff);
#endif
								} else {
									mutex_lock(&info->qlock);
									info->nonces++;
									info->auto_nonces++;
									mutex_unlock(&info->qlock);
								}
						 	} else {
						 		//bitmain_inc_nvw(info, thr);
						 		applog(LOG_ERR, "BitMain: bitmain_decode_nonce error work(%d)", rxnoncedata.nonces[j].work_id);
						 	}
						}
					 	free_work(work);
					} else {
						//bitmain_inc_nvw(info, thr);
						applog(LOG_ERR, "BitMain: Nonce not find work(%d)", rxnoncedata.nonces[j].work_id);
					}
				}
#ifdef BITMAIN_CALC_DIFF1
				if(opt_bitmain_hwerror) {
					int difftmp = 0;
					difftmp = rxnoncedata.diff;
					idiff = 1;
					while(difftmp > 0) {
						difftmp--;
						idiff = idiff << 1;
					}
					mutex_lock(&info->qlock);
					difftmp = idiff*(rxnoncedata.total_nonce_num-info->total_nonce_num);
					if(difftmp < 0)
						difftmp = 0;

					info->nonces = info->nonces+difftmp;
					info->auto_nonces = info->auto_nonces+difftmp;
					info->total_nonce_num = rxnoncedata.total_nonce_num;
					info->fifo_space = rxnoncedata.fifo_space;
					mutex_unlock(&info->qlock);
					inc_work_stats(thr, pool, difftmp);

					applog(LOG_DEBUG, "bitmain_parse_rxnonce fifo space=%d diff=%d rxtnn=%lld tnn=%lld", info->fifo_space, idiff, rxnoncedata.total_nonce_num, info->total_nonce_num);
				} else {
					mutex_lock(&info->qlock);
					info->fifo_space = rxnoncedata.fifo_space;
					mutex_unlock(&info->qlock);
					applog(LOG_DEBUG, "bitmain_parse_rxnonce fifo space=%d", info->fifo_space);
				}
#else
				mutex_lock(&info->qlock);
				info->fifo_space = rxnoncedata.fifo_space;
				mutex_unlock(&info->qlock);
				applog(LOG_DEBUG, "bitmain_parse_rxnonce fifo space=%d", info->fifo_space);
#endif

#ifndef WIN32
				if(nonce_num < BITMAIN_MAX_NONCE_NUM)
					cgsleep_ms(5);
#endif
			}

 			found = true;
 			spare = packethead.length + 4 + i;
 			if(spare > *offset) {
 				applog(LOG_ERR, "bitmain_parse_rxnonce space(%d) > offset(%d)", spare, *offset);
 				spare = *offset;
 			}
 			break;
		} else {
			applog(LOG_ERR, "bitmain_parse_results data type error=%02x", buf[i]);
		}
	}
	if (!found) {
		spare = *offset - BITMAIN_READ_SIZE;
		/* We are buffering and haven't accumulated one more corrupt
		 * work result. */
		if (spare < (int)BITMAIN_READ_SIZE)
			return;
		bitmain_inc_nvw(info, thr);
	}

	*offset -= spare;
	memmove(buf, buf + spare, *offset);
}

static void bitmain_running_reset(struct cgpu_info *bitmain, struct bitmain_info *info)
{
	bitmain->results = 0;
	info->reset = false;
}

static void *bitmain_get_results(void *userdata)
{
	struct cgpu_info *bitmain = (struct cgpu_info *)userdata;
	struct bitmain_info *info = bitmain->device_data;
	int offset = 0, read_delay = 0, ret = 0;
	const int rsize = BITMAIN_FTDI_READSIZE;
	char readbuf[BITMAIN_READBUF_SIZE];
	struct thr_info *thr = info->thr;
	char threadname[24];
	int errorcount = 0;

	snprintf(threadname, 24, "btm_recv/%d", bitmain->device_id);
	RenameThread(threadname);

	while (likely(!bitmain->shutdown)) {
		unsigned char buf[rsize];

		//applog(LOG_DEBUG, "+++++++bitmain_get_results offset=%d", offset);

		if (offset >= (int)BITMAIN_READ_SIZE) {
			//applog(LOG_DEBUG, "======start bitmain_get_results ");
			bitmain_parse_results(bitmain, info, thr, readbuf, &offset);
			//applog(LOG_DEBUG, "======stop bitmain_get_results ");
		}

		if (unlikely(offset + rsize >= BITMAIN_READBUF_SIZE)) {
			/* This should never happen */
			applog(LOG_DEBUG, "BitMain readbuf overflow, resetting buffer");
			offset = 0;
		}

		if (unlikely(info->reset)) {
			bitmain_running_reset(bitmain, info);
			/* Discard anything in the buffer */
			offset = 0;
		}

		/* As the usb read returns after just 1ms, sleep long enough
		 * to leave the interface idle for writes to occur, but do not
		 * sleep if we have been receiving data as more may be coming. */
		//if (offset == 0) {
		//	cgsleep_ms_r(&ts_start, BITMAIN_READ_TIMEOUT);
		//}

		//cgsleep_prepare_r(&ts_start);
		//applog(LOG_DEBUG, "======start bitmain_get_results bitmain_read");
		ret = bitmain_read(bitmain, buf, rsize, BITMAIN_READ_TIMEOUT, C_BITMAIN_READ);
		//applog(LOG_DEBUG, "======stop bitmain_get_results bitmain_read=%d", ret);

		if ((ret < 1) || (ret == 18)) {
			errorcount++;
#ifdef WIN32
			if(errorcount > 200) {
				//applog(LOG_ERR, "bitmain_read errorcount ret=%d", ret);
				cgsleep_ms(20);
				errorcount = 0;
			}
#else
			if(errorcount > 3) {
				//applog(LOG_ERR, "bitmain_read errorcount ret=%d", ret);
				cgsleep_ms(20);
				errorcount = 0;
			}
#endif
			if(ret < 1)
				continue;
		}

		if (opt_debug) {
			applog(LOG_DEBUG, "BitMain: get:");
			hexdump((uint8_t *)buf, ret);
		}

		memcpy(readbuf+offset, buf, ret);
		offset += ret;
	}
	return NULL;
}

static void bitmain_set_timeout(struct bitmain_info *info)
{
	info->timeout = BITMAIN_TIMEOUT_FACTOR / info->frequency;
}

static void *bitmain_send_tasks(void *userdata)
{
	return NULL;
}

static void bitmain_init(struct cgpu_info *bitmain)
{
	applog(LOG_INFO, "BitMain: Opened on %s", bitmain->device_path);
}

static bool bitmain_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	free(bitmain->works);
	bitmain->works = calloc(BITMAIN_MAX_WORK_NUM * sizeof(struct work *),
			       BITMAIN_ARRAY_SIZE);
	if (!bitmain->works)
		quit(1, "Failed to calloc bitmain works in bitmain_prepare");

	info->thr = thr;
	mutex_init(&info->lock);
	mutex_init(&info->qlock);
	if (unlikely(pthread_cond_init(&info->qcond, NULL)))
		quit(1, "Failed to pthread_cond_init bitmain qcond");
	cgsem_init(&info->write_sem);

	if (pthread_create(&info->read_thr, NULL, bitmain_get_results, (void *)bitmain))
		quit(1, "Failed to create bitmain read_thr");

	//if (pthread_create(&info->write_thr, NULL, bitmain_send_tasks, (void *)bitmain))
	//	quit(1, "Failed to create bitmain write_thr");

	bitmain_init(bitmain);

	return true;
}

static int bitmain_initialize(struct cgpu_info *bitmain)
{
	uint8_t data[BITMAIN_READBUF_SIZE];
	struct bitmain_info *info = NULL;
	int ret = 0, spare = 0;
	uint8_t sendbuf[BITMAIN_SENDBUF_SIZE];
	int readlen = 0;
	int sendlen = 0;
	int trycount = 3;
	struct timespec p;
	struct bitmain_rxstatus_data rxstatusdata;
	int i = 0, j = 0, m = 0, r = 0, statusok = 0;
	uint32_t checkbit = 0x00000000;
	int hwerror_eft = 0;
	int beeper_ctrl = 1;
	int tempover_ctrl = 1;
	int home_mode = 0;
	struct bitmain_packet_head packethead;
	int asicnum = 0;
	int mod = 0,tmp = 0;

	/* Send reset, then check for result */
	if(!bitmain) {
		applog(LOG_WARNING, "bitmain_initialize cgpu_info is null");
		return -1;
	}
	info = bitmain->device_data;

	/* clear read buf */
	ret = bitmain_read(bitmain, data, BITMAIN_READBUF_SIZE,
				  BITMAIN_RESET_TIMEOUT, C_BITMAIN_READ);
	if(ret > 0) {
		if (opt_debug) {
			applog(LOG_DEBUG, "BTM%d Clear Read(%d):", bitmain->device_id, ret);
			hexdump(data, ret);
		}
	}

	sendlen = bitmain_set_rxstatus((struct bitmain_rxstatus_token *)sendbuf, 0, 1, 0, 0);
	if(sendlen <= 0) {
		applog(LOG_ERR, "bitmain_initialize bitmain_set_rxstatus error(%d)", sendlen);
		return -1;
	}

	ret = bitmain_send_data(sendbuf, sendlen, bitmain);
	if (unlikely(ret == BTM_SEND_ERROR)) {
		applog(LOG_ERR, "bitmain_initialize bitmain_send_data error");
		return -1;
	}
	while(trycount >= 0) {
		ret = bitmain_read(bitmain, data+readlen, BITMAIN_READBUF_SIZE, BITMAIN_RESET_TIMEOUT, C_BITMAIN_DATA_RXSTATUS);
		if(ret > 0) {
			readlen += ret;
			if(readlen > BITMAIN_READ_SIZE) {
				for(i = 0; i < readlen; i++) {
					if(data[i] == 0xa1) {
						if (opt_debug) {
							applog(LOG_DEBUG, "%s%d initset: get:", bitmain->drv->name, bitmain->device_id);
							hexdump(data, readlen);
						}
						memcpy(&packethead, data+i, sizeof(struct bitmain_packet_head));
						packethead.length = htole16(packethead.length);

						if(packethead.length > 1130) {
							applog(LOG_ERR, "bitmain_initialize rxstatus datalen=%d error", packethead.length+4);
							continue;
						}
						if(readlen-i < packethead.length+4) {
							applog(LOG_ERR, "bitmain_initialize rxstatus datalen=%d<%d low", readlen-i, packethead.length+4);
							continue;
						}
						if (bitmain_parse_rxstatus(data+i, packethead.length+4, &rxstatusdata) != 0) {
							applog(LOG_ERR, "bitmain_initialize bitmain_parse_rxstatus error");
							continue;
						}
						info->chain_num = rxstatusdata.chain_num;
						info->fifo_space = rxstatusdata.fifo_space;
						info->hw_version[0] = rxstatusdata.hw_version[0];
						info->hw_version[1] = rxstatusdata.hw_version[1];
						info->hw_version[2] = rxstatusdata.hw_version[2];
						info->hw_version[3] = rxstatusdata.hw_version[3];
						info->nonce_error = 0;
						info->last_nonce_error = 0;
						sprintf(g_miner_version, "%d.%d.%d.%d", info->hw_version[0], info->hw_version[1], info->hw_version[2], info->hw_version[3]);
						applog(LOG_ERR, "bitmain_initialize rxstatus v(%d) chain(%d) fifo(%d) hwv1(%d) hwv2(%d) hwv3(%d) hwv4(%d) nerr(%d) freq=%d",
								rxstatusdata.version, info->chain_num, info->fifo_space, info->hw_version[0], info->hw_version[1], info->hw_version[2], info->hw_version[3],
								rxstatusdata.nonce_error, info->frequency);

						memcpy(info->chain_asic_exist, rxstatusdata.chain_asic_exist, BITMAIN_MAX_CHAIN_NUM*32);
						memcpy(info->chain_asic_status, rxstatusdata.chain_asic_status, BITMAIN_MAX_CHAIN_NUM*32);
						for(i = 0; i < rxstatusdata.chain_num; i++) {
							info->chain_asic_num[i] = rxstatusdata.chain_asic_num[i];
							memset(info->chain_asic_status_t[i], 0, 320);
							j = 0;
							mod = 0;

							if(info->chain_asic_num[i] <= 0) {
								asicnum = 0;
							} else {
								mod = info->chain_asic_num[i] % 32;
								if(mod == 0) {
									asicnum = info->chain_asic_num[i] / 32;
								} else {
									asicnum = info->chain_asic_num[i] / 32 + 1;
								}
							}
							if(asicnum > 0) {
									for(m = asicnum-1; m >= 0; m--) {
									tmp = mod ? (32-mod):0;
									for(r = tmp;r < 32;r++){
										if((r-tmp)%8 == 0 && (r-tmp) !=0){
													info->chain_asic_status_t[i][j] = ' ';
													j++;
												}
												checkbit = num2bit(r);
												if(rxstatusdata.chain_asic_exist[i*8+m] & checkbit) {
													if(rxstatusdata.chain_asic_status[i*8+m] & checkbit) {
														info->chain_asic_status_t[i][j] = 'o';
													} else {
														info->chain_asic_status_t[i][j] = 'x';
													}
												} else {
													info->chain_asic_status_t[i][j] = '-';
												}
												j++;
											}
											info->chain_asic_status_t[i][j] = ' ';
											j++;
										mod = 0;
								}
							}
							applog(LOG_DEBUG, "bitmain_initialize chain(%d) asic_num=%d asic_exist=%08x%08x%08x%08x%08x%08x%08x%08x asic_status=%08x%08x%08x%08x%08x%08x%08x%08x",
									i, info->chain_asic_num[i],
									info->chain_asic_exist[i*8+0], info->chain_asic_exist[i*8+1], info->chain_asic_exist[i*8+2], info->chain_asic_exist[i*8+3], info->chain_asic_exist[i*8+4], info->chain_asic_exist[i*8+5], info->chain_asic_exist[i*8+6], info->chain_asic_exist[i*8+7],
									info->chain_asic_status[i*8+0], info->chain_asic_status[i*8+1], info->chain_asic_status[i*8+2], info->chain_asic_status[i*8+3], info->chain_asic_status[i*8+4], info->chain_asic_status[i*8+5], info->chain_asic_status[i*8+6], info->chain_asic_status[i*8+7]);
							applog(LOG_ERR, "bitmain_initialize chain(%d) asic_num=%d asic_status=%s", i, info->chain_asic_num[i], info->chain_asic_status_t[i]);
						}
						bitmain_update_temps(bitmain, info, &rxstatusdata);
						statusok = 1;
						break;
					}
				}
				if(statusok) {
					break;
				}
			}
		}
		trycount--;
		p.tv_sec = 0;
		p.tv_nsec = BITMAIN_RESET_PITCH;
		nanosleep(&p, NULL);
	}

	p.tv_sec = 0;
	p.tv_nsec = BITMAIN_RESET_PITCH;
	nanosleep(&p, NULL);

	cgtime(&info->last_status_time);

	if(statusok) {
		applog(LOG_ERR, "bitmain_initialize start send txconfig");
		if(opt_bitmain_hwerror)
			hwerror_eft = 1;
		else
			hwerror_eft = 0;
		if(opt_bitmain_nobeeper)
			beeper_ctrl = 0;
		else
			beeper_ctrl = 1;
		if(opt_bitmain_notempoverctrl)
			tempover_ctrl = 0;
		else
			tempover_ctrl = 1;
		if(opt_bitmain_homemode)
			home_mode= 1;
		else
			home_mode= 0;
		sendlen = bitmain_set_txconfig((struct bitmain_txconfig_token *)sendbuf, 1, 1, 1, 1, 1, 0, 1, hwerror_eft, beeper_ctrl, tempover_ctrl,home_mode,
				info->chain_num, info->asic_num, BITMAIN_DEFAULT_FAN_MAX_PWM, info->timeout,
				info->frequency, info->voltage, 0, 0, 0x04, info->reg_data);
		if(sendlen <= 0) {
			applog(LOG_ERR, "bitmain_initialize bitmain_set_txconfig error(%d)", sendlen);
			return -1;
		}

		ret = bitmain_send_data(sendbuf, sendlen, bitmain);
		if (unlikely(ret == BTM_SEND_ERROR)) {
			applog(LOG_ERR, "bitmain_initialize bitmain_send_data error");
			return -1;
		}
		applog(LOG_WARNING, "BMM%d: InitSet succeeded", bitmain->device_id);
	} else {
		applog(LOG_WARNING, "BMS%d: InitSet error", bitmain->device_id);
		return -1;
	}
	return 0;
}

static void bitmain_usb_init(struct cgpu_info *bitmain)
{
	int err, interface;

#ifndef WIN32
	return;
#endif

	if (bitmain->usbinfo.nodev)
		return;

	interface = usb_interface(bitmain);

	// Reset
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_RESET,
		FTDI_VALUE_RESET, interface, C_RESET);

	applog(LOG_DEBUG, "%s%i: reset got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set latency
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_LATENCY,
		BITMAIN_LATENCY, interface, C_LATENCY);

	applog(LOG_DEBUG, "%s%i: latency got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set data
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_DATA,
				FTDI_VALUE_DATA_BTM, interface, C_SETDATA);

	applog(LOG_DEBUG, "%s%i: data got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set the baud
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_BAUD, FTDI_VALUE_BAUD_BTM,
				(FTDI_INDEX_BAUD_BTM & 0xff00) | interface,
				C_SETBAUD);

	applog(LOG_DEBUG, "%s%i: setbaud got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set Modem Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
		FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
		FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	/* BitMain repeats the following */
	// Set Modem Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_MODEM,
		FTDI_VALUE_MODEM, interface, C_SETMODEM);

	applog(LOG_DEBUG, "%s%i: setmodemctrl 2 got err %d",
		bitmain->drv->name, bitmain->device_id, err);

	if (bitmain->usbinfo.nodev)
		return;

	// Set Flow Control
	err = usb_transfer(bitmain, FTDI_TYPE_OUT, FTDI_REQUEST_FLOW,
		FTDI_VALUE_FLOW, interface, C_SETFLOW);

	applog(LOG_DEBUG, "%s%i: setflowctrl 2 got err %d",
		bitmain->drv->name, bitmain->device_id, err);
}

static struct cgpu_info * bitmain_usb_detect_one(libusb_device *dev, struct usb_find_devices *found)
{
	int baud, chain_num, asic_num, timeout, frequency = 0;
	char frequency_t[256] = {0};
	uint8_t reg_data[4] = {0};
	uint8_t voltage[2] = {0};
	char voltage_t[8] = {0};
	int this_option_offset = ++option_offset;
	struct bitmain_info *info;
	struct cgpu_info *bitmain;
	bool configured;
	int ret;

	if (opt_bitmain_options == NULL)
		return NULL;

	bitmain = usb_alloc_cgpu(&bitmain_drv, BITMAIN_MINER_THREADS);

	baud = BITMAIN_IO_SPEED;
	chain_num = BITMAIN_DEFAULT_CHAIN_NUM;
	asic_num = BITMAIN_DEFAULT_ASIC_NUM;
	timeout = BITMAIN_DEFAULT_TIMEOUT;
	frequency = BITMAIN_DEFAULT_FREQUENCY;

	if (!usb_init(bitmain, dev, found))
		goto shin;

	configured = get_options(this_option_offset, &baud, &chain_num,
				 &asic_num, &timeout, &frequency, frequency_t, reg_data, voltage, voltage_t);
	get_option_freq(&timeout, &frequency, frequency_t, reg_data);
	get_option_voltage(voltage, voltage_t);

	/* Even though this is an FTDI type chip, we want to do the parsing
	 * all ourselves so set it to std usb type */
	bitmain->usbdev->usb_type = USB_TYPE_STD;

	/* We have a real BitMain! */
	bitmain_usb_init(bitmain);

	bitmain->device_data = calloc(sizeof(struct bitmain_info), 1);
	if (unlikely(!(bitmain->device_data)))
		quit(1, "Failed to calloc bitmain_info data");
	info = bitmain->device_data;

	if (configured) {
		info->baud = baud;
		info->chain_num = chain_num;
		info->asic_num = asic_num;
		info->timeout = timeout;
		info->frequency = frequency;
		strcpy(info->frequency_t, frequency_t);
		memcpy(info->reg_data, reg_data, 4);
		memcpy(info->voltage, voltage, 2);
		strcpy(info->voltage_t, voltage_t);
	} else {
		info->baud = BITMAIN_IO_SPEED;
		info->chain_num = BITMAIN_DEFAULT_CHAIN_NUM;
		info->asic_num = BITMAIN_DEFAULT_ASIC_NUM;
		info->timeout = BITMAIN_DEFAULT_TIMEOUT;
		info->frequency = BITMAIN_DEFAULT_FREQUENCY;
		sprintf(info->frequency_t, "%d", BITMAIN_DEFAULT_FREQUENCY);
		memset(info->reg_data, 0, 4);
		info->voltage[0] = BITMAIN_DEFAULT_VOLTAGE0;
		info->voltage[1] = BITMAIN_DEFAULT_VOLTAGE1;
		strcpy(info->voltage_t, BITMAIN_DEFAULT_VOLTAGE_T);
	}

	info->fan_pwm = BITMAIN_DEFAULT_FAN_MIN_PWM;
	info->temp_max = 0;
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;

	if (!add_cgpu(bitmain))
		goto unshin;

	applog(LOG_ERR, "------bitmain usb detect one------");
	ret = bitmain_initialize(bitmain);
	if (ret && !configured)
		goto unshin;

	update_usb_stats(bitmain);

	info->errorcount = 0;

	applog(LOG_DEBUG, "BitMain Detected: %s "
	       "(chain_num=%d asic_num=%d timeout=%d frequency=%d)",
	       bitmain->device_path, info->chain_num, info->asic_num, info->timeout,
	       info->frequency);

	return bitmain;

unshin:

	usb_uninit(bitmain);

shin:

	free(bitmain->device_data);
	bitmain->device_data = NULL;

	bitmain = usb_free_cgpu(bitmain);

	return NULL;
}

static bool bitmain_detect_one(const char * devpath)
{
	int baud, chain_num, asic_num, timeout, frequency = 0;
	char frequency_t[256] = {0};
	uint8_t reg_data[4] = {0};
	uint8_t voltage[2] = {0};
	char voltage_t[8] = {0};
	int this_option_offset = ++option_offset;
	struct bitmain_info *info;
	struct cgpu_info *bitmain;
	bool configured;
	int ret;

	if (opt_bitmain_options == NULL)
		return false;

	bitmain = btm_alloc_cgpu(&bitmain_drv, BITMAIN_MINER_THREADS);

	configured = get_options(this_option_offset, &baud, &chain_num,
				 &asic_num, &timeout, &frequency, frequency_t, reg_data, voltage, voltage_t);
	get_option_freq(&timeout, &frequency, frequency_t, reg_data);
	get_option_voltage(voltage, voltage_t);

	if (!btm_init(bitmain, opt_bitmain_dev))
		goto shin;
	applog(LOG_ERR, "bitmain_detect_one btm init ok");

	bitmain->device_data = calloc(sizeof(struct bitmain_info), 1);
	/*	make sure initialize successfully*/
	memset(bitmain->device_data,0,sizeof(struct bitmain_info));	
	if (unlikely(!(bitmain->device_data)))
		quit(1, "Failed to calloc bitmain_info data");
	info = bitmain->device_data;

	if (configured) {
		info->baud = baud;
		info->chain_num = chain_num;
		info->asic_num = asic_num;
		info->timeout = timeout;
		info->frequency = frequency;
		strcpy(info->frequency_t, frequency_t);
		memcpy(info->reg_data, reg_data, 4);
		memcpy(info->voltage, voltage, 2);
		strcpy(info->voltage_t, voltage_t);
	} else {
		info->baud = BITMAIN_IO_SPEED;
		info->chain_num = BITMAIN_DEFAULT_CHAIN_NUM;
		info->asic_num = BITMAIN_DEFAULT_ASIC_NUM;
		info->timeout = BITMAIN_DEFAULT_TIMEOUT;
		info->frequency = BITMAIN_DEFAULT_FREQUENCY;
		sprintf(info->frequency_t, "%d", BITMAIN_DEFAULT_FREQUENCY);
		memset(info->reg_data, 0, 4);
		info->voltage[0] = BITMAIN_DEFAULT_VOLTAGE0;
		info->voltage[1] = BITMAIN_DEFAULT_VOLTAGE1;
		strcpy(info->voltage_t, BITMAIN_DEFAULT_VOLTAGE_T);
	}

	info->fan_pwm = BITMAIN_DEFAULT_FAN_MIN_PWM;
	info->temp_max = 0;
	/* This is for check the temp/fan every 3~4s */
	info->temp_history_count = (4 / (float)((float)info->timeout * ((float)1.67/0x32))) + 1;
	if (info->temp_history_count <= 0)
		info->temp_history_count = 1;

	info->temp_history_index = 0;
	info->temp_sum = 0;
	info->temp_old = 0;

	if (!add_cgpu(bitmain))
		goto unshin;

	ret = bitmain_initialize(bitmain);
	applog(LOG_ERR, "bitmain_detect_one stop bitmain_initialize %d", ret);
	if (ret && !configured)
		goto unshin;

	info->errorcount = 0;

	applog(LOG_ERR, "BitMain Detected: %s "
	       "(chain_num=%d asic_num=%d timeout=%d freq=%d-%s volt=%02x%02x-%s)",
	       bitmain->device_path, info->chain_num, info->asic_num, info->timeout,
	       info->frequency, info->frequency_t, info->voltage[0], info->voltage[1], info->voltage_t);

	return true;

unshin:
	btm_uninit(bitmain);

shin:
	free(bitmain->device_data);
	bitmain->device_data = NULL;

	bitmain = usb_free_cgpu(bitmain);

	return false;
}

static void bitmain_detect(bool __maybe_unused hotplug)
{
	applog(LOG_DEBUG, "BTM detect dev: %s", opt_bitmain_dev);
	if(strlen(opt_bitmain_dev) <= 0) {
		opt_bitmain_dev_usb = true;
	} else {
		opt_bitmain_dev_usb = false;
	}
	if(opt_bitmain_dev_usb) {
		usb_detect(&bitmain_drv, bitmain_usb_detect_one);
	} else {
		btm_detect(&bitmain_drv, bitmain_detect_one);
	}
}

static void do_bitmain_close(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	pthread_join(info->read_thr, NULL);
	pthread_join(info->write_thr, NULL);
	bitmain_running_reset(bitmain, info);

	info->no_matching_work = 0;

	cgsem_destroy(&info->write_sem);
}

static void get_bitmain_statline_before(char *buf, size_t bufsiz, struct cgpu_info *bitmain)
{
	struct bitmain_info *info = bitmain->device_data;
	int lowfan = 10000;
	int i = 0;

	/* Find the lowest fan speed of the ASIC cooling fans. */
	for(i = 0; i < info->fan_num; i++) {
		if (info->fan[i] >= 0 && info->fan[i] < lowfan)
			lowfan = info->fan[i];
	}

	tailsprintf(buf, bufsiz, "%2d/%3dC %04dR | ", info->temp_avg, info->temp_max, lowfan);
}

/* We use a replacement algorithm to only remove references to work done from
 * the buffer when we need the extra space for new work. */
static bool bitmain_fill(struct cgpu_info *bitmain)
{
	struct bitmain_info *info = bitmain->device_data;
	int subid, slot;
	struct work *work;
	bool ret = true;
	int sendret = 0, sendcount = 0, neednum = 0, queuednum = 0, sendnum = 0, sendlen = 0;
	uint8_t sendbuf[BITMAIN_SENDBUF_SIZE];
	cgtimer_t ts_start;
	int senderror = 0;
	struct timeval now;
	int timediff = 0;

	//applog(LOG_DEBUG, "BTM bitmain_fill start--------");
	mutex_lock(&info->qlock);
	if(info->fifo_space <= 0) {
		//applog(LOG_DEBUG, "BTM bitmain_fill fifo space empty--------");
		ret = true;
		goto out_unlock;
	}
	if (bitmain->queued >= BITMAIN_MAX_WORK_QUEUE_NUM) {
		ret = true;
	} else {
		ret = false;
	}
	while(info->fifo_space > 0) {
		neednum = info->fifo_space<BITMAIN_MAX_WORK_NUM?info->fifo_space:BITMAIN_MAX_WORK_NUM;
		queuednum = bitmain->queued;
		applog(LOG_DEBUG, "BTM: Work task queued(%d) fifo space(%d) needsend(%d)", queuednum, info->fifo_space, neednum);
		if(queuednum < neednum) {
			while(true) {
				work = get_queued(bitmain);
				if (unlikely(!work)) {
					break;
				} else {
					applog(LOG_DEBUG, "BTM get work queued number:%d neednum:%d", queuednum, neednum);
					subid = bitmain->queued++;
					work->subid = subid;
					slot = bitmain->work_array + subid;
					if (slot > BITMAIN_ARRAY_SIZE) {
						applog(LOG_DEBUG, "bitmain_fill array cyc %d", BITMAIN_ARRAY_SIZE);
						slot = 0;
					}
					if (likely(bitmain->works[slot])) {
						applog(LOG_DEBUG, "bitmain_fill work_completed %d", slot);
						work_completed(bitmain, bitmain->works[slot]);
					}
					bitmain->works[slot] = work;
					queuednum++;
					if(queuednum >= neednum) {
						break;
					}
				}
			}
		}
		if(queuednum < BITMAIN_MAX_DEAL_QUEUE_NUM) {
			if(queuednum < neednum) {
				applog(LOG_DEBUG, "BTM: No enough work to send, queue num=%d", queuednum);
				break;
			}
		}
		sendnum = queuednum < neednum ? queuednum : neednum;
		sendlen = bitmain_set_txtask(sendbuf, &(info->last_work_block), bitmain->works, BITMAIN_ARRAY_SIZE, bitmain->work_array, sendnum, &sendcount);
		bitmain->queued -= sendnum;
		info->send_full_space += sendnum;
		if (bitmain->queued < 0)
			bitmain->queued = 0;
		if (bitmain->work_array + sendnum > BITMAIN_ARRAY_SIZE) {
			bitmain->work_array = bitmain->work_array + sendnum-BITMAIN_ARRAY_SIZE;
		} else {
			bitmain->work_array += sendnum;
		}
		applog(LOG_DEBUG, "BTM: Send work array %d", bitmain->work_array);
		if (sendlen > 0) {
			info->fifo_space -= sendcount;
			if (info->fifo_space < 0)
				info->fifo_space = 0;
			sendret = bitmain_send_data(sendbuf, sendlen, bitmain);
			if (unlikely(sendret == BTM_SEND_ERROR)) {
				applog(LOG_ERR, "BTM%i: Comms error(buffer)", bitmain->device_id);
				//dev_error(bitmain, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				info->errorcount++;
				senderror = 1;
				if (info->errorcount > 1000) {
					info->errorcount = 0;
					applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread", bitmain->drv->name, bitmain->device_id);
					bitmain->shutdown = true;
				}
				break;
			} else {
				applog(LOG_DEBUG, "bitmain_send_data send ret=%d", sendret);
				info->errorcount = 0;
			}
		} else {
			applog(LOG_DEBUG, "BTM: Send work bitmain_set_txtask error: %d", sendlen);
			break;
		}
	}

out_unlock:
	cgtime(&now);
	timediff = now.tv_sec - info->last_status_time.tv_sec;
	if(timediff < 0) timediff = -timediff;
	if (timediff > BITMAIN_SEND_STATUS_TIME) {
		applog(LOG_DEBUG, "BTM: Send RX Status Token fifo_space(%d) timediff(%d)", info->fifo_space, timediff);
		copy_time(&(info->last_status_time), &now);

		sendlen = bitmain_set_rxstatus((struct bitmain_rxstatus_token *) sendbuf, 0, 0, 0, 0);
		if (sendlen > 0) {
			sendret = bitmain_send_data(sendbuf, sendlen, bitmain);
			if (unlikely(sendret == BTM_SEND_ERROR)) {
				applog(LOG_ERR, "BTM%i: Comms error(buffer)", bitmain->device_id);
				//dev_error(bitmain, REASON_DEV_COMMS_ERROR);
				info->reset = true;
				info->errorcount++;
				senderror = 1;
				if (info->errorcount > 1000) {
					info->errorcount = 0;
					applog(LOG_ERR, "%s%d: Device disappeared, shutting down thread", bitmain->drv->name, bitmain->device_id);
					bitmain->shutdown = true;
				}
			} else {
				info->errorcount = 0;
				if (info->fifo_space <= 0) {
					senderror = 1;
				}
			}
		}
	}

	if(info->send_full_space > BITMAIN_SEND_FULL_SPACE) {
		info->send_full_space = 0;
		ret = true;
		cgsleep_ms(1);
	}
	mutex_unlock(&info->qlock);
	if(senderror) {
		ret = true;
		applog(LOG_DEBUG, "bitmain_fill send task sleep");
		//cgsleep_ms(1);
	}
	return ret;
}

static int64_t bitmain_scanhash(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;
	const int chain_num = info->chain_num;
	struct timeval now, then, tdiff;
	int64_t hash_count, us_timeout;
	struct timespec abstime;
	int ret;

	/* Half nonce range */
	us_timeout = 0x80000000ll / info->asic_num / info->frequency;
	tdiff.tv_sec = us_timeout / 1000000;
	tdiff.tv_usec = us_timeout - (tdiff.tv_sec * 1000000);
	cgtime(&now);
	timeradd(&now, &tdiff, &then);
	abstime.tv_sec = then.tv_sec;
	abstime.tv_nsec = then.tv_usec * 1000;

	//applog(LOG_DEBUG, "bitmain_scanhash info->qlock start");
	mutex_lock(&info->qlock);
	hash_count = 0xffffffffull * (uint64_t)info->nonces;
	bitmain->results += info->nonces + info->idle;
	if (bitmain->results > chain_num)
		bitmain->results = chain_num;
	if (!info->reset)
		bitmain->results--;
	info->nonces = info->idle = 0;
	mutex_unlock(&info->qlock);
	//applog(LOG_DEBUG, "bitmain_scanhash info->qlock stop");

	/* Check for nothing but consecutive bad results or consistently less
	 * results than we should be getting and reset the FPGA if necessary */
	//if (bitmain->results < -chain_num && !info->reset) {
	//	applog(LOG_ERR, "BTM%d: Result return rate low, resetting!",
	//		bitmain->device_id);
	//	info->reset = true;
	//}

	if (unlikely(bitmain->usbinfo.nodev)) {
		applog(LOG_ERR, "BTM%d: Device disappeared, shutting down thread",
		       bitmain->device_id);
		bitmain->shutdown = true;
	}

	/* This hashmeter is just a utility counter based on returned shares */
	return hash_count;
}

static void bitmain_flush_work(struct cgpu_info *bitmain)
{
	struct bitmain_info *info = bitmain->device_data;
	int i = 0;

	mutex_lock(&info->qlock);
	/* Will overwrite any work queued */
	applog(LOG_ERR, "bitmain_flush_work queued=%d array=%d", bitmain->queued, bitmain->work_array);
	if(bitmain->queued > 0) {
		if (bitmain->work_array + bitmain->queued > BITMAIN_ARRAY_SIZE) {
			bitmain->work_array = bitmain->work_array + bitmain->queued-BITMAIN_ARRAY_SIZE;
		} else {
			bitmain->work_array += bitmain->queued;
		}
	}
	bitmain->queued = 0;
	//bitmain->work_array = 0;
	//for(i = 0; i < BITMAIN_ARRAY_SIZE; i++) {
	//	bitmain->works[i] = NULL;
	//}
	//pthread_cond_signal(&info->qcond);
	mutex_unlock(&info->qlock);
}

static struct api_data *bitmain_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct bitmain_info *info = cgpu->device_data;
	char buf[64];
	int i = 0;
	double hwp = (cgpu->hw_errors + cgpu->diff1) ?
			(double)(cgpu->hw_errors) / (double)(cgpu->hw_errors + cgpu->diff1) : 0;

	root = api_add_int(root, "baud", &(info->baud), false);
	root = api_add_int(root, "miner_count", &(info->chain_num), false);
	root = api_add_int(root, "asic_count", &(info->asic_num), false);
	root = api_add_int(root, "timeout", &(info->timeout), false);
	root = api_add_string(root, "frequency", info->frequency_t, false);
	root = api_add_string(root, "voltage", info->voltage_t, false);
	root = api_add_int(root, "hwv1", &(info->hw_version[0]), false);
	root = api_add_int(root, "hwv2", &(info->hw_version[1]), false);
	root = api_add_int(root, "hwv3", &(info->hw_version[2]), false);
	root = api_add_int(root, "hwv4", &(info->hw_version[3]), false);

	root = api_add_int(root, "fan_num", &(info->fan_num), false);
	root = api_add_int(root, "fan1", &(info->fan[0]), false);
	root = api_add_int(root, "fan2", &(info->fan[1]), false);
	root = api_add_int(root, "fan3", &(info->fan[2]), false);
	root = api_add_int(root, "fan4", &(info->fan[3]), false);
	root = api_add_int(root, "fan5", &(info->fan[4]), false);
	root = api_add_int(root, "fan6", &(info->fan[5]), false);
	root = api_add_int(root, "fan7", &(info->fan[6]), false);
	root = api_add_int(root, "fan8", &(info->fan[7]), false);
	root = api_add_int(root, "fan9", &(info->fan[8]), false);
	root = api_add_int(root, "fan10", &(info->fan[9]), false);
	root = api_add_int(root, "fan11", &(info->fan[10]), false);
	root = api_add_int(root, "fan12", &(info->fan[11]), false);
	root = api_add_int(root, "fan13", &(info->fan[12]), false);
	root = api_add_int(root, "fan14", &(info->fan[13]), false);
	root = api_add_int(root, "fan15", &(info->fan[14]), false);
	root = api_add_int(root, "fan16", &(info->fan[15]), false);

	root = api_add_int(root, "temp_num", &(info->temp_num), false);
	root = api_add_int(root, "temp1", &(info->temp[0]), false);
	root = api_add_int(root, "temp2", &(info->temp[1]), false);
	root = api_add_int(root, "temp3", &(info->temp[2]), false);
	root = api_add_int(root, "temp4", &(info->temp[3]), false);
	root = api_add_int(root, "temp5", &(info->temp[4]), false);
	root = api_add_int(root, "temp6", &(info->temp[5]), false);
	root = api_add_int(root, "temp7", &(info->temp[6]), false);
	root = api_add_int(root, "temp8", &(info->temp[7]), false);
	root = api_add_int(root, "temp9", &(info->temp[8]), false);
	root = api_add_int(root, "temp10", &(info->temp[9]), false);
	root = api_add_int(root, "temp11", &(info->temp[10]), false);
	root = api_add_int(root, "temp12", &(info->temp[11]), false);
	root = api_add_int(root, "temp13", &(info->temp[12]), false);
	root = api_add_int(root, "temp14", &(info->temp[13]), false);
	root = api_add_int(root, "temp15", &(info->temp[14]), false);
	root = api_add_int(root, "temp16", &(info->temp[15]), false);
	root = api_add_int(root, "temp_avg", &(info->temp_avg), false);
	root = api_add_int(root, "temp_max", &(info->temp_max), false);
	root = api_add_percent(root, "Device Hardware%", &hwp, true);
	root = api_add_int(root, "no_matching_work", &(info->no_matching_work), false);
	/*
	for (i = 0; i < info->chain_num; i++) {
		char mcw[24];

		sprintf(mcw, "match_work_count%d", i + 1);
		root = api_add_int(root, mcw, &(info->matching_work[i]), false);
	}*/

	root = api_add_int(root, "chain_acn1", &(info->chain_asic_num[0]), false);
	root = api_add_int(root, "chain_acn2", &(info->chain_asic_num[1]), false);
	root = api_add_int(root, "chain_acn3", &(info->chain_asic_num[2]), false);
	root = api_add_int(root, "chain_acn4", &(info->chain_asic_num[3]), false);
	root = api_add_int(root, "chain_acn5", &(info->chain_asic_num[4]), false);
	root = api_add_int(root, "chain_acn6", &(info->chain_asic_num[5]), false);
	root = api_add_int(root, "chain_acn7", &(info->chain_asic_num[6]), false);
	root = api_add_int(root, "chain_acn8", &(info->chain_asic_num[7]), false);
	root = api_add_int(root, "chain_acn9", &(info->chain_asic_num[8]), false);
	root = api_add_int(root, "chain_acn10", &(info->chain_asic_num[9]), false);
	root = api_add_int(root, "chain_acn11", &(info->chain_asic_num[10]), false);
	root = api_add_int(root, "chain_acn12", &(info->chain_asic_num[11]), false);
	root = api_add_int(root, "chain_acn13", &(info->chain_asic_num[12]), false);
	root = api_add_int(root, "chain_acn14", &(info->chain_asic_num[13]), false);
	root = api_add_int(root, "chain_acn15", &(info->chain_asic_num[14]), false);
	root = api_add_int(root, "chain_acn16", &(info->chain_asic_num[15]), false);

	//applog(LOG_ERR, "chain asic status:%s", info->chain_asic_status_t[0]);
	root = api_add_string(root, "chain_acs1", info->chain_asic_status_t[0], false);
	root = api_add_string(root, "chain_acs2", info->chain_asic_status_t[1], false);
	root = api_add_string(root, "chain_acs3", info->chain_asic_status_t[2], false);
	root = api_add_string(root, "chain_acs4", info->chain_asic_status_t[3], false);
	root = api_add_string(root, "chain_acs5", info->chain_asic_status_t[4], false);
	root = api_add_string(root, "chain_acs6", info->chain_asic_status_t[5], false);
	root = api_add_string(root, "chain_acs7", info->chain_asic_status_t[6], false);
	root = api_add_string(root, "chain_acs8", info->chain_asic_status_t[7], false);
	root = api_add_string(root, "chain_acs9", info->chain_asic_status_t[8], false);
	root = api_add_string(root, "chain_acs10", info->chain_asic_status_t[9], false);
	root = api_add_string(root, "chain_acs11", info->chain_asic_status_t[10], false);
	root = api_add_string(root, "chain_acs12", info->chain_asic_status_t[11], false);
	root = api_add_string(root, "chain_acs13", info->chain_asic_status_t[12], false);
	root = api_add_string(root, "chain_acs14", info->chain_asic_status_t[13], false);
	root = api_add_string(root, "chain_acs15", info->chain_asic_status_t[14], false);
	root = api_add_string(root, "chain_acs16", info->chain_asic_status_t[15], false);

	//root = api_add_int(root, "chain_acs1", &(info->chain_asic_status[0]), false);
	//root = api_add_int(root, "chain_acs2", &(info->chain_asic_status[1]), false);
	//root = api_add_int(root, "chain_acs3", &(info->chain_asic_status[2]), false);
	//root = api_add_int(root, "chain_acs4", &(info->chain_asic_status[3]), false);

	return root;
}

static void bitmain_shutdown(struct thr_info *thr)
{
	do_bitmain_close(thr);
}

char *set_bitmain_dev(char *arg)
{
	if(arg == NULL || strlen(arg) <= 0) {
		memcpy(opt_bitmain_dev, 0, 256);
	} else {
		strncpy(opt_bitmain_dev, arg, 256);
	}
	applog(LOG_DEBUG, "BTM set device: %s", opt_bitmain_dev);
	return NULL;
}

char *set_bitmain_fan(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to bitmain-fan";
	if (ret == 1)
		val2 = val1;

	if (val1 < 0 || val1 > 100 || val2 < 0 || val2 > 100 || val2 < val1)
		return "Invalid value passed to bitmain-fan";

	opt_bitmain_fan_min = val1 * BITMAIN_PWM_MAX / 100;
	opt_bitmain_fan_max = val2 * BITMAIN_PWM_MAX / 100;

	return NULL;
}

char *set_bitmain_freq(char *arg)
{
	int val1, val2, ret;

	ret = sscanf(arg, "%d-%d", &val1, &val2);
	if (ret < 1)
		return "No values passed to bitmain-freq";
	if (ret == 1)
		val2 = val1;

	if (val1 < BITMAIN_MIN_FREQUENCY || val1 > BITMAIN_MAX_FREQUENCY ||
	    val2 < BITMAIN_MIN_FREQUENCY || val2 > BITMAIN_MAX_FREQUENCY ||
	    val2 < val1)
		return "Invalid value passed to bitmain-freq";

	opt_bitmain_freq_min = val1;
	opt_bitmain_freq_max = val2;

	return NULL;
}

struct device_drv bitmain_drv = {
	.drv_id = DRIVER_bitmain,
	.dname = "Bitmain",
	.name = "BTM",
	.drv_detect = bitmain_detect,
	.thread_prepare = bitmain_prepare,
	.hash_work = hash_queued_work,
	.queue_full = bitmain_fill,
	.scanwork = bitmain_scanhash,
	.flush_work = bitmain_flush_work,
	.get_api_stats = bitmain_api_stats,
	.get_statline_before = get_bitmain_statline_before,
	.reinit_device = bitmain_init,
	.thread_shutdown = bitmain_shutdown,
};
