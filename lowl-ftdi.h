#ifndef BFG_LOWL_FTDI_H
#define BFG_LOWL_FTDI_H

#include <stdbool.h>
#include <stdint.h>

#include <libusb.h>

#include "lowlevel.h"

enum ft232r_reset_purge {
	FTDI_PURGE_RX   = 1,
	FTDI_PURGE_TX   = 2,
	FTDI_PURGE_BOTH = 3,
};

struct ft232r_device_handle;

extern struct ft232r_device_handle *ft232r_open(const struct lowlevel_device_info *);
extern struct ft232r_device_handle *ft232h_open_mpsse(const struct lowlevel_device_info *);
extern void ft232r_close(struct ft232r_device_handle *);
extern bool ft232r_purge_buffers(struct ft232r_device_handle *, enum ft232r_reset_purge);
extern bool ft232r_set_bitmode(struct ft232r_device_handle *, uint8_t mask, uint8_t mode);
extern ssize_t ft232r_flush(struct ft232r_device_handle *);
extern ssize_t ft232r_write(struct ft232r_device_handle *, const void *data, size_t count);
extern ssize_t ft232r_write_all(struct ft232r_device_handle *, const void *data, size_t count);
extern ssize_t ft232r_read(struct ft232r_device_handle *, void *buf, size_t count);
extern ssize_t ft232r_read_all(struct ft232r_device_handle *, void *data, size_t count);
extern bool ft232r_get_pins(struct ft232r_device_handle *, uint8_t *pins);
extern bool ft232r_set_cbus_bits(struct ft232r_device_handle *dev, bool sc, bool cs);
extern bool ft232r_get_cbus_bits(struct ft232r_device_handle *dev, bool *out_sio0, bool *out_sio1);
extern bool ft232h_mpsse_set_axbus(struct ft232r_device_handle *, uint8_t value, uint8_t directions, bool adbus);
#define ft232h_mpsse_set_acbus(ftdi, val, dir)  ft232h_mpsse_set_axbus(ftdi, val, dir, false)
#define ft232h_mpsse_set_adbus(ftdi, val, dir)  ft232h_mpsse_set_axbus(ftdi, val, dir, true)
extern ssize_t ft232h_mpsse_readwrite_all(struct ft232r_device_handle *, void *read_data, const void *write_data, size_t count);

#endif
