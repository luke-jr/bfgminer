/*
 * Copyright 2012-2014 Lingchao Xu
 * Copyright 2015 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
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

#include <curl/curl.h>
#include <uthash.h>

#include "deviceapi.h"
#include "miner.h"
#include "driver-bitmain.h"
#include "lowl-vcom.h"
#include "util.h"

const bool opt_bitmain_hwerror = true;
const unsigned bitmain_poll_interval_us = 10000;

BFG_REGISTER_DRIVER(bitmain_drv)
static const struct bfg_set_device_definition bitmain_set_device_funcs_init[];

#define htole8(x) (x)

#define BITMAIN_USING_CURL  -2

static
struct cgpu_info *btm_alloc_cgpu(struct device_drv *drv, int threads)
{
	struct cgpu_info *cgpu = calloc(1, sizeof(*cgpu));

	if (unlikely(!cgpu))
		quit(1, "Failed to calloc cgpu for %s in usb_alloc_cgpu", drv->dname);

	cgpu->drv = drv;
	cgpu->deven = DEV_ENABLED;
	cgpu->threads = threads;

	cgpu->device_fd = -1;

	struct bitmain_info *info = malloc(sizeof(*info));
	if (unlikely(!info))
		quit(1, "Failed to calloc bitmain_info data");
	cgpu->device_data = info;
	
	*info = (struct bitmain_info){
		.chain_num = BITMAIN_DEFAULT_CHAIN_NUM,
		.asic_num = BITMAIN_DEFAULT_ASIC_NUM,
		.timeout = BITMAIN_DEFAULT_TIMEOUT,
		.frequency = BITMAIN_DEFAULT_FREQUENCY,
		.voltage[0] = BITMAIN_DEFAULT_VOLTAGE0,
		.voltage[1] = BITMAIN_DEFAULT_VOLTAGE1,
		
		.packet_max_nonce = BITMAIN_MAX_PACKET_MAX_NONCE,
		
		.diff = 255,
		.lowest_goal_diff = 255,
		.work_restart = true,
	};
	sprintf(info->frequency_t, "%d", BITMAIN_DEFAULT_FREQUENCY),
	strcpy(info->voltage_t, BITMAIN_DEFAULT_VOLTAGE_T);
	
	return cgpu;
}

static curl_socket_t bitmain_grab_socket_opensocket_cb(void *clientp, __maybe_unused curlsocktype purpose, struct curl_sockaddr *addr)
{
	struct bitmain_info * const info = clientp;
	curl_socket_t sck = bfg_socket(addr->family, addr->socktype, addr->protocol);
	info->curl_sock = sck;
	return sck;
}

static
bool btm_init(struct cgpu_info *cgpu, const char * devpath)
{
	applog(LOG_DEBUG, "btm_init cgpu->device_fd=%d", cgpu->device_fd);
	int fd = -1;
	if(cgpu->device_fd >= 0) {
		return false;
	}
	struct bitmain_info *info = cgpu->device_data;
	if (!strncmp(devpath, "ip:", 3)) {
		CURL *curl = curl_easy_init();
		if (!curl)
			applogr(false, LOG_ERR, "%s: curl_easy_init failed", cgpu->drv->dname);
		
		// CURLINFO_LASTSOCKET is broken on Win64 (which has a wider SOCKET type than curl_easy_getinfo returns), so we use this hack for now
		info->curl_sock = -1;
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, bitmain_grab_socket_opensocket_cb);
		curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, info);
		
		curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
		curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
		curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1);
		curl_easy_setopt(curl, CURLOPT_URL, &devpath[3]);
		if (curl_easy_perform(curl)) {
			curl_easy_cleanup(curl);
			applogr(false, LOG_ERR, "%s: curl_easy_perform failed for %s", cgpu->drv->dname, &devpath[3]);
		}
		cgpu->device_path = strdup(devpath);
		cgpu->device_fd = BITMAIN_USING_CURL;
		info->device_curl = curl;
		return true;
	}
	fd = serial_open(devpath, 0, 1, false);
	if(fd == -1) {
		applog(LOG_DEBUG, "%s open %s error %d",
				cgpu->drv->dname, devpath, errno);
		return false;
	}
	cgpu->device_path = strdup(devpath);
	cgpu->device_fd = fd;
	applog(LOG_DEBUG, "btm_init open device fd = %d", cgpu->device_fd);
	return true;
}

static
void btm_uninit(struct cgpu_info *cgpu)
{
	struct bitmain_info * const info = cgpu->device_data;
	
	applog(LOG_DEBUG, "BTM uninit %s%i", cgpu->drv->name, cgpu->device_fd);

	// May have happened already during a failed initialisation
	//  if release_cgpu() was called due to a USB NODEV(err)
	if (cgpu->device_fd >= 0) {
		serial_close(cgpu->device_fd);
		cgpu->device_fd = -1;
	}
	if (info->device_curl) {
		curl_easy_cleanup(info->device_curl);
		info->device_curl = NULL;
	}
	if(cgpu->device_path) {
		free((char*)cgpu->device_path);
		cgpu->device_path = NULL;
	}
}

bool bitmain_curl_all(const bool is_recv, const int fd, CURL * const curl, void *p, size_t remsz)
{
	CURLcode (* const func)(CURL *, void *, size_t, size_t *) = is_recv ? (void*)curl_easy_recv : (void*)curl_easy_send;
	CURLcode r;
	size_t sz;
	while (remsz) {
		fd_set otherfds, thisfds;
		FD_ZERO(&otherfds);
		FD_ZERO(&thisfds);
		FD_SET(fd, &thisfds);
		select(fd + 1, is_recv ? &thisfds : &otherfds, is_recv ? &otherfds : &thisfds, &thisfds, NULL);
		r = func(curl, p, remsz, &sz);
		switch (r) {
			case CURLE_OK:
				remsz -= sz;
				p += sz;
				break;
			case CURLE_AGAIN:
				break;
			default:
				return false;
		}
	}
	return true;
}

static
int btm_read(struct cgpu_info * const cgpu, void * const buf, const size_t bufsize)
{
	int err = 0;
	//applog(LOG_DEBUG, "btm_read ----- %d -----", bufsize);
	if (unlikely(cgpu->device_fd == BITMAIN_USING_CURL)) {
		struct bitmain_info * const info = cgpu->device_data;
		uint8_t headbuf[5];
		headbuf[0] = 0;
		pk_u32be(headbuf, 1, bufsize);
		if (!bitmain_curl_all(false, info->curl_sock, info->device_curl, headbuf, sizeof(headbuf)))
			return -1;
		if (!bitmain_curl_all( true, info->curl_sock, info->device_curl, headbuf, 4))
			return -1;
		if (headbuf[0] == 0xff && headbuf[1] == 0xff && headbuf[2] == 0xff && headbuf[3] == 0xff)
			return -1;
		size_t sz = upk_u32be(headbuf, 0);
		if (!bitmain_curl_all( true, info->curl_sock, info->device_curl, buf, sz))
			return -1;
		return sz;
	}
	err = read(cgpu->device_fd, buf, bufsize);
	return err;
}

static
int btm_write(struct cgpu_info * const cgpu, void * const buf, const size_t bufsize)
{
	int err = 0;
	//applog(LOG_DEBUG, "btm_write ----- %d -----", bufsize);
	if (unlikely(cgpu->device_fd == BITMAIN_USING_CURL)) {
		struct bitmain_info * const info = cgpu->device_data;
		uint8_t headbuf[5];
		headbuf[0] = 1;
		pk_u32be(headbuf, 1, bufsize);
		if (!bitmain_curl_all(false, info->curl_sock, info->device_curl, headbuf, sizeof(headbuf)))
			return -1;
		if (!bitmain_curl_all(false, info->curl_sock, info->device_curl, buf, bufsize))
			return -1;
		if (!bitmain_curl_all( true, info->curl_sock, info->device_curl, headbuf, 4))
			return -1;
		if (headbuf[0] == 0xff && headbuf[1] == 0xff && headbuf[2] == 0xff && headbuf[3] == 0xff)
			return -1;
		return upk_u32be(headbuf, 0);
	}
	err = write(cgpu->device_fd, buf, bufsize);
	return err;
}

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

bool opt_bitmain_checkall = false;
bool opt_bitmain_nobeeper = false;
bool opt_bitmain_notempoverctrl = false;
bool opt_bitmain_homemode = false;
bool opt_bitmain_auto;

// --------------------------------------------------------------
//      CRC16 check table
// --------------------------------------------------------------
static
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

static
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
	return 1L << (31 - num);
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
	applog(LOG_DEBUG, "BitMain RxNonce Data: nonce_num(%d) fifo_space(%d) diff(%d) tnn(%"PRIu64")", curnoncenum, bm->fifo_space, bm->diff, bm->total_nonce_num);
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
		       size_t bufsize, int timeout)
{
	int err = 0;
	size_t total = 0;

	if(bitmain == NULL || buf == NULL || bufsize <= 0) {
		applog(LOG_WARNING, "bitmain_read parameter error bufsize(%"PRIu64")", (uint64_t)bufsize);
		return -1;
	}
	{
		err = btm_read(bitmain, buf, bufsize);
		total = err;
	}
	return total;
}

static int bitmain_write(struct cgpu_info *bitmain, char *buf, ssize_t len)
{
	int err;
	{
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
	int ret;

	if(datalen <= 0) {
		return 0;
	}

	//struct bitmain_info *info = bitmain->device_data;
	//int delay;
	//delay = datalen * 10 * 1000000;
	//delay = delay / info->baud;
	//delay += 4000;

	if(opt_debug) {
		char hex[(datalen * 2) + 1];
		bin2hex(hex, data, datalen);
		applog(LOG_DEBUG, "BitMain: Sent(%d): %s", datalen, hex);
	}

	//cgtimer_t ts_start;
	//cgsleep_prepare_r(&ts_start);
	//applog(LOG_DEBUG, "----bitmain_send_data  start");
	ret = bitmain_write(bitmain, (char *)data, datalen);
	applog(LOG_DEBUG, "----bitmain_send_data  stop ret=%d datalen=%d", ret, datalen);
	//cgsleep_us_r(&ts_start, delay);

	//applog(LOG_DEBUG, "BitMain: Sent: Buffer delay: %dus", delay);

	return ret;
}

static void bitmain_inc_nvw(struct bitmain_info *info, struct thr_info *thr)
{
	applog(LOG_INFO, "%s%d: No matching work - HW error",
	       thr->cgpu->drv->name, thr->cgpu->device_id);

	inc_hw_errors_only(thr);
	info->no_matching_work++;
}

static inline void record_temp_fan(struct bitmain_info *info, struct bitmain_rxstatus_data *bm, float *temp)
{
	int i = 0;
	int maxfan = 0, maxtemp = 0;
	int temp_avg = 0;

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
		temp_avg += info->temp[i];

		if(info->temp[i] > info->temp_max) {
			info->temp_max = info->temp[i];
		}
		if(info->temp[i] > maxtemp)
			maxtemp = info->temp[i];
	}

	if(bm->temp_num > 0) {
		temp_avg /= bm->temp_num;
		info->temp_avg = temp_avg;
	}

	*temp = maxtemp;
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
	applog(LOG_INFO, "%s", msg);
	info->temp_history_index++;
	info->temp_sum += bitmain->temp;
	applog(LOG_DEBUG, "BitMain: temp_index: %d, temp_count: %d, temp_old: %d",
		info->temp_history_index, info->temp_history_count, info->temp_old);
	if (info->temp_history_index == info->temp_history_count) {
		info->temp_history_index = 0;
		info->temp_sum = 0;
	}
}

static void bitmain_set_fifo_space(struct cgpu_info * const dev, const int fifo_space)
{
	struct thr_info * const master_thr = dev->thr[0];
	struct bitmain_info * const info = dev->device_data;
	
	if (unlikely(fifo_space > info->max_fifo_space))
		info->max_fifo_space = fifo_space;
	
	info->fifo_space = fifo_space;
	master_thr->queue_full = !fifo_space;
}

static void bitmain_parse_results(struct cgpu_info *bitmain, struct bitmain_info *info,
				 struct thr_info *thr, uint8_t *buf, int *offset)
{
	int i, j, n, m, r, errordiff, spare = BITMAIN_READ_SIZE;
	uint32_t checkbit = 0x00000000;
	bool found = false;
	struct work *work = NULL;
	struct bitmain_packet_head packethead;
	int asicnum = 0;
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
				bitmain_set_fifo_space(bitmain, rxstatusdata.fifo_space);
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
			if(packethead.length > 1038) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce datalen=%d error", packethead.length+4);
				continue;
			}
			if(*offset < packethead.length + 4) {
				return;
			}
			if(bitmain_parse_rxnonce(buf+i, packethead.length+4, &rxnoncedata, &nonce_num) != 0) {
				applog(LOG_ERR, "bitmain_parse_results bitmain_parse_rxnonce error len=%d", packethead.length+4);
			} else {
				const float nonce_diff = 1 << rxnoncedata.diff;
				for(j = 0; j < nonce_num; j++) {
					const work_device_id_t work_id = rxnoncedata.nonces[j].work_id;
					HASH_FIND(hh, thr->work_list, &work_id, sizeof(work_id), work);
					if(work) {
						if(BITMAIN_TEST_PRINT_WORK) {
							applog(LOG_ERR, "bitmain_parse_results nonce find work(%d-%d)(%08x)", work->id, rxnoncedata.nonces[j].work_id, rxnoncedata.nonces[j].nonce);

							char ob_hex[(32 * 2) + 1];
							
							bin2hex(ob_hex, work->midstate, 32);
							applog(LOG_ERR, "work %d midstate: %s", work->id, ob_hex);

							bin2hex(ob_hex, &work->data[64], 12);
							applog(LOG_ERR, "work %d data2: %s", work->id, ob_hex);
						}

						{
							const uint32_t nonce = rxnoncedata.nonces[j].nonce;
							applog(LOG_DEBUG, "BitMain: submit nonce = %08lx", (unsigned long)nonce);
							work->nonce_diff = nonce_diff;
							if (submit_nonce(thr, work, nonce)) {
								mutex_lock(&info->qlock);
								hashes_done2(thr, 0x100000000 * work->nonce_diff, NULL);
								mutex_unlock(&info->qlock);
						 	} else {
						 		applog(LOG_ERR, "BitMain: bitmain_decode_nonce error work(%d)", rxnoncedata.nonces[j].work_id);
						 	}
						}
					} else {
						bitmain_inc_nvw(info, thr);
						applog(LOG_ERR, "BitMain: Nonce not find work(%d)", rxnoncedata.nonces[j].work_id);
					}
				}
				mutex_lock(&info->qlock);
				bitmain_set_fifo_space(bitmain, rxnoncedata.fifo_space);
				mutex_unlock(&info->qlock);
				applog(LOG_DEBUG, "bitmain_parse_rxnonce fifo space=%d", info->fifo_space);

#ifndef WIN32
				if(nonce_num < info->packet_max_nonce)
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
	info->reset = false;
}

static void bitmain_prune_old_work(struct cgpu_info * const dev)
{
	struct thr_info * const master_thr = dev->thr[0];
	struct bitmain_info * const info = dev->device_data;
	
	const size_t retain_work_items = info->max_fifo_space * 2;
	const size_t queued_work_items = HASH_COUNT(master_thr->work_list);
	if (queued_work_items > retain_work_items) {
		size_t remove_work_items = queued_work_items - retain_work_items;
		while (remove_work_items--) {
			// Deletes the first item insertion-order
			struct work * const work = master_thr->work_list;
			HASH_DEL(master_thr->work_list, work);
			free_work(work);
		}
	}
}

static void bitmain_poll(struct thr_info * const thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;
	int offset = info->readbuf_offset, ret = 0;
	const int rsize = BITMAIN_FTDI_READSIZE;
	uint8_t * const readbuf = info->readbuf;

	{
		unsigned char buf[rsize];

		if (unlikely(info->reset)) {
			bitmain_running_reset(bitmain, info);
			/* Discard anything in the buffer */
			offset = 0;
		}

		//cgsleep_prepare_r(&ts_start);
		//applog(LOG_DEBUG, "======start bitmain_get_results bitmain_read");
		ret = bitmain_read(bitmain, buf, rsize, BITMAIN_READ_TIMEOUT);
		//applog(LOG_DEBUG, "======stop bitmain_get_results bitmain_read=%d", ret);

		if ((ret < 1) || (ret == 18)) {
			++info->errorcount2;
#ifdef WIN32
			if(info->errorcount2 > 200) {
				//applog(LOG_ERR, "bitmain_read errorcount ret=%d", ret);
				cgsleep_ms(20);
				info->errorcount2 = 0;
			}
#else
			if(info->errorcount2 > 3) {
				//applog(LOG_ERR, "bitmain_read errorcount ret=%d", ret);
				cgsleep_ms(20);
				info->errorcount2 = 0;
			}
#endif
			if(ret < 1)
				return;
		}

		if (opt_debug) {
			char hex[(ret * 2) + 1];
			bin2hex(hex, buf, ret);
			applog(LOG_DEBUG, "BitMain: get: %s", hex);
		}

		memcpy(readbuf+offset, buf, ret);
		offset += ret;

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

		/* As the usb read returns after just 1ms, sleep long enough
		 * to leave the interface idle for writes to occur, but do not
		 * sleep if we have been receiving data as more may be coming. */
		//if (offset == 0) {
		//	cgsleep_ms_r(&ts_start, BITMAIN_READ_TIMEOUT);
		//}
	}
	
	info->readbuf_offset = offset;
	
	bitmain_prune_old_work(bitmain);
	
	timer_set_delay_from_now(&thr->tv_poll, bitmain_poll_interval_us);
}

static void bitmain_init(struct cgpu_info *bitmain)
{
	applog(LOG_INFO, "BitMain: Opened on %s", bitmain->device_path);
}

static bool bitmain_prepare(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	mutex_init(&info->qlock);

	// To initialise queue_full
	bitmain_set_fifo_space(bitmain, info->fifo_space);
	
	bitmain_init(bitmain);

	timer_set_now(&thr->tv_poll);
	
	return true;
}

static int bitmain_initialize(struct cgpu_info *bitmain)
{
	uint8_t data[BITMAIN_READBUF_SIZE];
	struct bitmain_info *info = NULL;
	int ret = 0;
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
				  BITMAIN_RESET_TIMEOUT);
	if(ret > 0) {
		if (opt_debug) {
			char hex[(ret * 2) + 1];
			bin2hex(hex, data, ret);
			applog(LOG_DEBUG, "BTM%d Clear Read(%d): %s", bitmain->device_id, ret, hex);
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
		ret = bitmain_read(bitmain, data+readlen, BITMAIN_READBUF_SIZE, BITMAIN_RESET_TIMEOUT);
		if(ret > 0) {
			readlen += ret;
			if(readlen > BITMAIN_READ_SIZE) {
				for(i = 0; i < readlen; i++) {
					if(data[i] == 0xa1) {
						if (opt_debug) {
							char hex[(readlen * 2) + 1];
							bin2hex(hex, data, readlen);
							applog(LOG_DEBUG, "%s%d initset: get: %s", bitmain->drv->name, bitmain->device_id, hex);
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
						// NOTE: This is before thr_info is allocated, so we cannot use bitmain_set_fifo_space (bitmain_prepare will re-set it for us)
						info->fifo_space = rxstatusdata.fifo_space;
						info->hw_version[0] = rxstatusdata.hw_version[0];
						info->hw_version[1] = rxstatusdata.hw_version[1];
						info->hw_version[2] = rxstatusdata.hw_version[2];
						info->hw_version[3] = rxstatusdata.hw_version[3];
						info->nonce_error = 0;
						info->last_nonce_error = 0;
						sprintf(info->g_miner_version, "%d.%d.%d.%d", info->hw_version[0], info->hw_version[1], info->hw_version[2], info->hw_version[3]);
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

static bool bitmain_detect_one(const char * devpath)
{
	struct bitmain_info *info;
	struct cgpu_info *bitmain;
	int ret;

	bitmain = btm_alloc_cgpu(&bitmain_drv, BITMAIN_MINER_THREADS);
	info = bitmain->device_data;

	drv_set_defaults(&bitmain_drv, bitmain_set_device_funcs_init, info, devpath, NULL, 1);
	
	if (!info->packet_max_work)
		return_via_applog(shin, , LOG_ERR, "%s: Device not configured (did you forget --set bitmain:model=S5 ?)", bitmain_drv.dname);
	
	if (!(upk_u32be(info->reg_data, 0))) {
		switch (info->chip_type) {
			case BMC_BM1382:
			case BMC_BM1384:
				if (bm1382_freq_to_reg_data(info->reg_data, info->frequency))
					break;
				// fall thru if it failed
			default:
				return_via_applog(shin, , LOG_ERR, "%s: Device not configured (did you forget --set bitmain:reg_data=x???? ?)", bitmain_drv.dname);
		}
	}

	if (!btm_init(bitmain, devpath))
		goto shin;
	applog(LOG_ERR, "bitmain_detect_one btm init ok");

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
	if (ret)
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

	free(bitmain);

	return false;
}

static int bitmain_detect_auto(void)
{
	const char * const auto_bitmain_dev = "/dev/bitmain-asic";
	applog(LOG_DEBUG, "BTM detect dev: %s", auto_bitmain_dev);
	return bitmain_detect_one(auto_bitmain_dev) ? 1 : 0;
}

static void bitmain_detect()
{
	generic_detect(&bitmain_drv, bitmain_detect_one, bitmain_detect_auto, GDF_REQUIRE_DNAME | GDF_DEFAULT_NOAUTO);
}

static void do_bitmain_close(struct thr_info *thr)
{
	struct cgpu_info *bitmain = thr->cgpu;
	struct bitmain_info *info = bitmain->device_data;

	bitmain_running_reset(bitmain, info);

	info->no_matching_work = 0;
}

static uint8_t diff_to_bitmain(float diff)
{
	uint8_t res = 0;
	if (diff > UINT64_MAX)
		diff = UINT64_MAX;
	for (uint64_t tmp = diff; tmp >>= 1; ) {
		if (++res == UINT8_MAX)
			break;
	}
	return res;
}

static bool bitmain_queue_append(struct thr_info * const thr, struct work * const work)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct thr_info * const master_thr = dev->thr[0];
	struct bitmain_info * const info = dev->device_data;
	const struct pool * const pool = work->pool;
	const struct mining_goal_info * const goal = pool->goal;
	
	applog(LOG_DEBUG, "%s: %s with fifo_space=%d (max=%d) work_restart=%d", dev->dev_repr, __func__, info->fifo_space, info->max_fifo_space, (int)info->work_restart);
	
	if (info->work_restart) {
		info->work_restart = false;
		info->ready_to_queue = 0;
		bitmain_set_fifo_space(dev, info->max_fifo_space);
		info->queuebuf[4] = 1;  // clear work queues
	}
	
	if (!info->fifo_space) {
		thr->queue_full = true;
		return false;
	}
	
	uint8_t * const wbuf = &info->queuebuf[BITMAIN_TASK_HEADER_SIZE + (BITMAIN_WORK_SIZE * info->ready_to_queue)];
	const int work_nonce_bmdiff = diff_to_bitmain(work->nonce_diff);
	if (work_nonce_bmdiff < info->diff)
		info->diff = work_nonce_bmdiff;
	if (goal->current_diff < info->lowest_goal_diff)
		info->lowest_goal_diff = goal->current_diff;
	
	work->device_id = info->next_work_id++;
	pk_u32le(wbuf, 0, work->device_id);
	memcpy(&wbuf[4], work->midstate, 0x20);
	memcpy(&wbuf[0x24], &work->data[0x40], 0xc);
	
	HASH_ADD(hh, master_thr->work_list, device_id, sizeof(work->device_id), work);
	++info->ready_to_queue;
	
	if (!(info->ready_to_queue >= info->packet_max_work || info->fifo_space == info->ready_to_queue || info->fifo_space == info->max_fifo_space)) {
		applog(LOG_DEBUG, "%s: %s now has ready_to_queue=%d; deferring send", dev->dev_repr, __func__, info->ready_to_queue);
		return true;
	}
	
	applog(LOG_DEBUG, "%s: %s now has ready_to_queue=%d; sending to device", dev->dev_repr, __func__, info->ready_to_queue);
	
	uint8_t * const buf = info->queuebuf;
	const size_t buflen = BITMAIN_TASK_HEADER_SIZE + (info->ready_to_queue * BITMAIN_WORK_SIZE) + BITMAIN_TASK_FOOTER_SIZE;
	
	buf[0] = BITMAIN_TOKEN_TYPE_TXTASK;
	buf[1] = 0;  // packet version
	pk_u16le(buf, 2, buflen - 4);  // length of data after this field (including CRC)
	// buf[4] is set to 1 to clear work queues, when the first work item is added, and reset to 0 after we send
	buf[5] = info->diff;
	pk_u16le(buf, 6, diff_to_bitmain(info->lowest_goal_diff));
	
	pk_u16le(buf, buflen - 2, CRC16(buf, buflen - 2));
	
	int sendret = bitmain_send_data(buf, buflen, proc);
	if (unlikely(sendret == BTM_SEND_ERROR)) {
		applog(LOG_ERR, "%s: Comms error(buffer)", dev->dev_repr);
		//dev_error(bitmain, REASON_DEV_COMMS_ERROR);
		info->reset = true;
		info->errorcount++;
		if (info->errorcount > 1000) {
			info->errorcount = 0;
			applog(LOG_ERR, "%s: Device disappeared, shutting down thread", dev->dev_repr);
			dev->shutdown = true;
		}
		// The work is in the queuebuf already, so we're okay-ish for that...
		return true;
	} else {
		applog(LOG_DEBUG, "bitmain_send_data send ret=%d", sendret);
		info->errorcount = 0;
	}
	buf[4] = 0;
	info->fifo_space -= info->ready_to_queue;
	info->ready_to_queue = 0;
	
	struct timeval tv_now;
	timer_set_now(&tv_now);
	if (timer_passed(&master_thr->tv_poll, &tv_now)) {
		bitmain_poll(master_thr);
	}
	
	return true;
}

static void bitmain_queue_flush(struct thr_info * const thr)
{
	struct cgpu_info * const proc = thr->cgpu;
	struct cgpu_info * const dev = proc->device;
	struct bitmain_info * const info = dev->device_data;
	
	// Can't use thr->work_restart as that merely triggers this function in minerloop_queue
	info->work_restart = true;
	thr->queue_full = false;
}

static struct api_data *bitmain_api_stats(struct cgpu_info *cgpu)
{
	struct api_data *root = NULL;
	struct bitmain_info *info = cgpu->device_data;
	double hwp = (cgpu->hw_errors + cgpu->diff1) ?
			(double)(cgpu->hw_errors) / (double)(cgpu->hw_errors + cgpu->diff1) : 0;

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
	for (int i = 0; i < info->chain_num; ++i) {
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

static
const char *bitmain_set_layout(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct bitmain_info *info = proc->device_data;
	char *endptr, *next_field;
	const long int n_chains = strtol(newvalue, &endptr, 0);
	if (endptr == newvalue || n_chains < 1)
		return "Missing chain count";
	long int n_asics = 0;
	if (endptr[0] == ':' || endptr[1] == ',')
	{
		next_field = &endptr[1];
		n_asics = strtol(next_field, &endptr, 0);
	}
	if (n_asics < 1)
		return "Missing ASIC count";
	if (n_asics > BITMAIN_DEFAULT_ASIC_NUM)
		return "ASIC count too high";
	info->chain_num = n_chains;
	info->asic_num = n_asics;
	return NULL;
}

static
const char *bitmain_set_timeout(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct bitmain_info *info = proc->device_data;
	const int timeout = atoi(newvalue);
	if (timeout < 0 || timeout > 0xff)
		return "Invalid timeout setting";
	info->timeout = timeout;
	return NULL;
}

static
const char *bitmain_set_clock(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct bitmain_info *info = proc->device_data;
	const int freq = atoi(newvalue);
	if (freq < BITMAIN_MIN_FREQUENCY || freq > BITMAIN_MAX_FREQUENCY)
		return "Invalid clock frequency";
	info->frequency = freq;
	sprintf(info->frequency_t, "%d", freq);
	return NULL;
}

static
const char *bitmain_set_reg_data(struct cgpu_info * const proc, const char * const optname, const char *newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct bitmain_info *info = proc->device_data;
	uint8_t reg_data[4] = {0};
	
	if (newvalue[0] == 'x')
		++newvalue;
	
	size_t nvlen = strlen(newvalue);
	if (nvlen > (sizeof(reg_data) * 2) || !nvlen || nvlen % 2)
		return "reg_data must be a hex string of 2-8 digits (1-4 bytes)";
	
	if (!hex2bin(reg_data, newvalue, nvlen / 2))
		return "Invalid reg data hex";
	
	memcpy(info->reg_data, reg_data, sizeof(reg_data));
	
	return NULL;
}

static
const char *bitmain_set_voltage(struct cgpu_info * const proc, const char * const optname, const char *newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct bitmain_info *info = proc->device_data;
	uint8_t voltage_data[2] = {0};
	
	if (newvalue[0] == 'x')
		++newvalue;
	else
voltage_usage:
		return "voltage must be 'x' followed by a hex string of 1-4 digits (1-2 bytes)";
	
	size_t nvlen = strlen(newvalue);
	if (nvlen > (sizeof(voltage_data) * 2) || !nvlen || nvlen % 2)
		goto voltage_usage;
	
	if (!hex2bin(voltage_data, newvalue, nvlen / 2))
		return "Invalid voltage data hex";
	
	memcpy(info->voltage, voltage_data, sizeof(voltage_data));
	bin2hex(info->voltage_t, voltage_data, 2);
	info->voltage_t[5] = 0;
	info->voltage_t[4] = info->voltage_t[3];
	info->voltage_t[3] = info->voltage_t[2];
	info->voltage_t[2] = info->voltage_t[1];
	info->voltage_t[1] = '.';
	
	return NULL;
}

static bool bitmain_set_packet_max_work(struct cgpu_info * const dev, const unsigned i)
{
	struct bitmain_info * const info = dev->device_data;
	uint8_t * const new_queuebuf = realloc(info->queuebuf, BITMAIN_TASK_HEADER_SIZE + (i * BITMAIN_WORK_SIZE) + BITMAIN_TASK_FOOTER_SIZE);
	if (!new_queuebuf)
		return false;
	info->packet_max_work = i;
	info->queuebuf = new_queuebuf;
	return true;
}

static const char *bitmain_set_packet_max_work_opt(struct cgpu_info * const proc, const char * const optname, const char *newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	const int i = atoi(newvalue);
	if (i < 1)
		return "Invalid setting";
	if (!bitmain_set_packet_max_work(proc->device, i))
		return "realloc failure";
	return NULL;
}

static
const char *bitmain_set_packet_max_nonce_opt(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct bitmain_info *info = proc->device_data;
	const int i = atoi(newvalue);
	if (i < 0 || i > BITMAIN_MAX_PACKET_MAX_NONCE)
		return "Invalid setting";
	info->packet_max_nonce = i;
	return NULL;
}

static const char *bitmain_set_model(struct cgpu_info * const proc, const char * const optname, const char * const newvalue, char * const replybuf, enum bfg_set_device_replytype * const out_success)
{
	struct cgpu_info * const dev = proc->device;
	struct bitmain_info * const info = dev->device_data;
	
	if (toupper(newvalue[0]) != 'S') {
unknown_model:
		return "Unknown model";
	}
	char *endptr;
	long Sn = strtol(&newvalue[1], &endptr, 10);
	if (Sn < 1 || Sn > 5)
		goto unknown_model;
	if (Sn == 5 && endptr[0] == '+')
		++endptr;
	if (endptr[0] && !isspace(endptr[0]))
		goto unknown_model;
	
	info->chip_type = BMC_UNKNOWN;
	switch (Sn) {
		case 1:
			info->chip_type = BMC_BM1380;
			bitmain_set_packet_max_work(dev, 8);
			info->packet_max_nonce = 8;
			break;
		case 2:
			info->chip_type = BMC_BM1380;
			bitmain_set_packet_max_work(dev, 0x40);
			info->packet_max_nonce = 0x80;
			break;
		case 3:
			info->chip_type = BMC_BM1382;
			bitmain_set_packet_max_work(dev, 8);
			info->packet_max_nonce = 0x80;
			break;
		case 4:
			info->chip_type = BMC_BM1382;
			bitmain_set_packet_max_work(dev, 0x40);
			info->packet_max_nonce = 0x80;
			break;
		case 5:
			info->chip_type = BMC_BM1384;
			bitmain_set_packet_max_work(dev, 0x40);
			info->packet_max_nonce = 0x80;
			break;
	}
	return NULL;
}

static const struct bfg_set_device_definition bitmain_set_device_funcs_init[] = {
	{"model", bitmain_set_model, "model of unit (S1-S5)"},
	{"layout", bitmain_set_layout, "number of chains ':' number of ASICs per chain (eg: 32:8)"},
	{"timeout", bitmain_set_timeout, "timeout"},
	{"clock", bitmain_set_clock, "clock frequency"},
	{"reg_data", bitmain_set_reg_data, "reg_data (eg: x0d82)"},
	{"voltage", bitmain_set_voltage, "voltage (must be specified as 'x' and hex data; eg: x0725)"},
	{"packet_max_work", bitmain_set_packet_max_work_opt, NULL},
	{"packet_max_nonce", bitmain_set_packet_max_nonce_opt, NULL},
	{NULL},
};

struct device_drv bitmain_drv = {
	.dname = "bitmain",
	.name = "BTM",
	.drv_detect = bitmain_detect,
	.thread_prepare = bitmain_prepare,
	
	.minerloop = minerloop_queue,
	.queue_append = bitmain_queue_append,
	.queue_flush = bitmain_queue_flush,
	.poll = bitmain_poll,
	
	.get_api_stats = bitmain_api_stats,
	.reinit_device = bitmain_init,
	.thread_shutdown = bitmain_shutdown,
};
