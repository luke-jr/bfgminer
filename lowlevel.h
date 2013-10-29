#ifndef _BFG_LOWLEVEL_H
#define _BFG_LOWLEVEL_H

#include <stdbool.h>

struct lowlevel_device_info;

typedef bool (*lowl_found_devinfo_func_t)(struct lowlevel_device_info *);

struct lowlevel_driver {
	struct lowlevel_device_info *(*devinfo_scan)();
	void (*devinfo_free)(struct lowlevel_device_info *);
};

struct lowlevel_device_info {
	char *product;
	char *serial;
	char *path;
	
	struct lowlevel_driver *lowl;
	void *lowl_data;
	
	struct lowlevel_device_info *next;
};

extern void lowlevel_scan();
extern int _lowlevel_detect(lowl_found_devinfo_func_t, const char *serial, const char **product_needles);
#define lowlevel_detect(func, ...)  _lowlevel_detect(func, NULL, (const char *[]){__VA_ARGS__, NULL})
#define lowlevel_detect_serial(func, serial)  _lowlevel_detect(func, serial, (const char *[]){NULL})
extern void lowlevel_scan_free();
extern void lowlevel_devinfo_free(struct lowlevel_device_info *);

#ifdef USE_X6500
extern struct lowlevel_driver lowl_ft232r;
#endif
#ifdef USE_NANOFURY
extern struct lowlevel_driver lowl_mcp2210;
#endif

#endif
