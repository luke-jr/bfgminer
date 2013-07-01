#ifndef FPGAUTILS_H
#define FPGAUTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

struct device_drv;
struct cgpu_info;

typedef bool(*detectone_func_t)(const char*);
typedef int(*autoscan_func_t)();

extern int _serial_detect(struct device_drv *api, detectone_func_t, autoscan_func_t, int flags);
#define serial_detect_fauto(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 1)
#define serial_detect_auto(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 0)
#define serial_detect_auto_byname(api, detectone, autoscan)  \
	_serial_detect(api, detectone, autoscan, 2)
#define serial_detect(api, detectone)  \
	_serial_detect(api, detectone,     NULL, 0)
#define serial_detect_byname(api, detectone)  \
	_serial_detect(api, detectone,     NULL, 2)
#define noserial_detect(api, autoscan)  \
	_serial_detect(api, NULL     , autoscan, 0)
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
#define serial_close(fd)  close(fd)

extern FILE *open_bitstream(const char *dname, const char *filename);
extern FILE *open_xilinx_bitstream(const char *dname, const char *repr, const char *fwfile, unsigned long *out_len);

extern int get_serial_cts(int fd);

#endif
