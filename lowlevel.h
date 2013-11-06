#ifndef _BFG_LOWLEVEL_H
#define _BFG_LOWLEVEL_H

#include <stdbool.h>
#include <stdint.h>

#include <uthash.h>

struct lowlevel_device_info;

typedef bool (*lowl_found_devinfo_func_t)(struct lowlevel_device_info *, void *);

struct lowlevel_driver {
	const char *dname;
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
	UT_hash_handle hh;
};

extern void lowlevel_scan();
extern int _lowlevel_detect(lowl_found_devinfo_func_t, const char *serial, const char **product_needles, void *);
#define lowlevel_detect(func, ...)  _lowlevel_detect(func, NULL, (const char *[]){__VA_ARGS__, NULL}, NULL)
#define lowlevel_detect_serial(func, serial)  _lowlevel_detect(func, serial, (const char *[]){NULL}, NULL)
extern int lowlevel_detect_id(lowl_found_devinfo_func_t, void *, const struct lowlevel_driver *, int32_t vid, int32_t pid);
extern void lowlevel_scan_free();
extern void lowlevel_devinfo_semicpy(struct lowlevel_device_info *dst, const struct lowlevel_device_info *src);
extern void lowlevel_devinfo_free(struct lowlevel_device_info *);

#ifdef USE_X6500
extern struct lowlevel_driver lowl_ft232r;
#endif
#ifdef NEED_BFG_LOWL_HID
extern struct lowlevel_driver lowl_hid;
#endif
#ifdef USE_NANOFURY
extern struct lowlevel_driver lowl_mcp2210;
#endif
#ifdef HAVE_LIBUSB
extern struct lowlevel_driver lowl_usb;
#else
// Dummy definition for the various "don't warn if just a lower-level interface" checks
static struct lowlevel_driver lowl_usb;
#endif
#ifdef HAVE_FPGAUTILS
extern struct lowlevel_driver lowl_vcom;
#endif

#endif
