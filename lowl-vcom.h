#ifndef BFG_LOWL_VCOM_H
#define BFG_LOWL_VCOM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "deviceapi.h"

struct device_drv;
struct cgpu_info;

struct detectone_meta_info_t {
	const char *manufacturer;
	const char *product;
	const char *serial;
};

extern struct detectone_meta_info_t *_detectone_meta_info();
#define detectone_meta_info (*_detectone_meta_info())
extern void clear_detectone_meta_info(void);

extern bool vcom_lowl_probe_wrapper(const struct lowlevel_device_info *, detectone_func_t);

extern int _serial_autodetect(detectone_func_t, ...);
#define serial_autodetect(...)  _serial_autodetect(__VA_ARGS__, NULL)

extern struct device_drv *bfg_claim_serial(struct device_drv * const, const bool verbose, const char * const devpath);
#define serial_claim(devpath, drv)    bfg_claim_serial(drv, false, devpath)
#define serial_claim_v(devpath, drv)  bfg_claim_serial(drv, true , devpath)

extern int serial_open(const char *devpath, unsigned long baud, uint8_t timeout, bool purge);
extern ssize_t _serial_read(int fd, char *buf, size_t buflen, char *eol);
#define serial_read(fd, buf, count)  \
	_serial_read(fd, (char*)(buf), count, NULL)
#define serial_read_line(fd, buf, bufsiz, eol)  \
	_serial_read(fd, buf, bufsiz, &eol)
extern int serial_close(int fd);

enum
{
	RTS_LOW = 0,
	RTS_HIGH = 1
};

enum
{
	DTR_LOW = 0,
	DTR_HIGH = 1
};

extern int get_serial_cts(int fd);
extern int set_serial_dtr(int fd, int dtr);
extern int set_serial_rts(int fd, int rts);
extern bool valid_baud(int baud);

#endif
