#ifndef FPGAUTILS_H
#define FPGAUTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifdef HAVE_LIBUSB
#include <libusb.h>
#endif

#include "deviceapi.h"

struct device_drv;
struct cgpu_info;

struct detectone_meta_info_t {
	const char *manufacturer;
	const char *product;
	const char *serial;
};

// NOTE: Should detectone become run multithreaded, this will become a threadsafe #define
extern struct detectone_meta_info_t detectone_meta_info;
extern void clear_detectone_meta_info(void);

extern int _serial_autodetect(detectone_func_t, ...);
#define serial_autodetect(...)  _serial_autodetect(__VA_ARGS__, NULL)

extern struct device_drv *bfg_claim_serial(struct device_drv * const, const bool verbose, const char * const devpath);
#define serial_claim(devpath, drv)    bfg_claim_serial(drv, false, devpath)
#define serial_claim_v(devpath, drv)  bfg_claim_serial(drv, true , devpath)
extern struct device_drv *bfg_claim_usb(struct device_drv * const, const bool verbose, const uint8_t usbbus, const uint8_t usbaddr);
#define bfg_claim_libusb(api, verbose, dev)  bfg_claim_usb(api, verbose, libusb_get_bus_number(dev), libusb_get_device_address(dev))

#ifdef HAVE_LIBUSB
extern void cgpu_copy_libusb_strings(struct cgpu_info *, libusb_device *);
#endif

extern int serial_open(const char *devpath, unsigned long baud, uint8_t timeout, bool purge);
extern ssize_t _serial_read(int fd, char *buf, size_t buflen, char *eol);
#define serial_read(fd, buf, count)  \
	_serial_read(fd, (char*)(buf), count, NULL)
#define serial_read_line(fd, buf, bufsiz, eol)  \
	_serial_read(fd, buf, bufsiz, &eol)
#define serial_close(fd)  close(fd)

extern void _bitstream_not_found(const char *repr, const char *fn);
extern FILE *open_xilinx_bitstream(const char *dname, const char *repr, const char *fwfile, unsigned long *out_len);
extern bool load_bitstream_intelhex(bytes_t *out, const char *dname, const char *repr, const char *fn);
extern bool load_bitstream_bytes(bytes_t *out, const char *dname, const char *repr, const char *fileprefix);

extern int get_serial_cts(int fd);
extern bool valid_baud(int baud);

#endif
