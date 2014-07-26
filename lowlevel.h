#ifndef BFG_LOWLEVEL_H
#define BFG_LOWLEVEL_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include <uthash.h>

#include "miner.h"

struct lowlevel_device_info;

typedef bool (*lowl_found_devinfo_func_t)(struct lowlevel_device_info *, void *);

struct lowlevel_driver {
	const char *dname;
	bool exclude_from_all;
	
	struct lowlevel_device_info *(*devinfo_scan)();
	void (*devinfo_free)(struct lowlevel_device_info *);
};

struct lowlevel_device_info {
	char *manufacturer;
	char *product;
	char *serial;
	char *path;
	char *devid;
	uint16_t vid;
	uint16_t pid;
	
	struct lowlevel_driver *lowl;
	void *lowl_data;
	
	struct lowlevel_device_info *next;
	struct lowlevel_device_info *same_devid_next;
	UT_hash_handle hh;
	pthread_t probe_pth;
	int ref;
};

extern char *bfg_make_devid_usb(uint8_t usbbus, uint8_t usbaddr);

extern struct lowlevel_device_info *lowlevel_scan();
extern bool _lowlevel_match_product(const struct lowlevel_device_info *, const char **);
#define lowlevel_match_product(info, ...)  \
	_lowlevel_match_product(info, (const char *[]){__VA_ARGS__, NULL})
#define lowlevel_match_lowlproduct(info, matchlowl, ...)  \
	(matchlowl == info->lowl && _lowlevel_match_product(info, (const char *[]){__VA_ARGS__, NULL}))
extern bool lowlevel_match_id(const struct lowlevel_device_info *, const struct lowlevel_driver *, int32_t vid, int32_t pid);
extern int _lowlevel_detect(lowl_found_devinfo_func_t, const char *serial, const char **product_needles, void *);
#define lowlevel_detect(func, ...)  _lowlevel_detect(func, NULL, (const char *[]){__VA_ARGS__, NULL}, NULL)
#define lowlevel_detect_serial(func, serial)  _lowlevel_detect(func, serial, (const char *[]){NULL}, NULL)
extern int lowlevel_detect_id(lowl_found_devinfo_func_t, void *, const struct lowlevel_driver *, int32_t vid, int32_t pid);
extern void lowlevel_scan_free();

extern struct lowlevel_device_info *lowlevel_ref(const struct lowlevel_device_info *);
#define lowlevel_claim(drv, verbose, info)  \
	bfg_claim_any(drv, (verbose) ? ((info)->path ?: "") : NULL, (info)->devid)
extern void lowlevel_devinfo_semicpy(struct lowlevel_device_info *dst, const struct lowlevel_device_info *src);
extern void lowlevel_devinfo_free(struct lowlevel_device_info *);

#ifdef NEED_BFG_LOWL_FTDI
extern struct lowlevel_driver lowl_ft232r;
#endif
#ifdef NEED_BFG_LOWL_HID
extern struct lowlevel_driver lowl_hid;
#endif
#ifdef USE_NANOFURY
extern struct lowlevel_driver lowl_mcp2210;
#endif
#ifdef NEED_BFG_LOWL_MSWIN
extern struct lowlevel_driver lowl_mswin;
#endif
#ifdef NEED_BFG_LOWL_PCI
extern struct lowlevel_driver lowl_pci;
#endif
#ifdef HAVE_LIBUSB
extern struct lowlevel_driver lowl_usb;
#else
// Dummy definition for the various "don't warn if just a lower-level interface" checks
static __maybe_unused struct lowlevel_driver lowl_usb;
#endif
#ifdef NEED_BFG_LOWL_VCOM
extern struct lowlevel_driver lowl_vcom;
#endif

extern struct device_drv *bfg_claim_any(struct device_drv *, const char *verbose, const char *devpath);
extern struct device_drv *bfg_claim_any2(struct device_drv *, const char *verbose, const char *llname, const char *path);

#endif
