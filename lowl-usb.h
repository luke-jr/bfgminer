#ifndef BFG_LOWL_USB_H
#define BFG_LOWL_USB_H

#include <stdbool.h>
#include <stdint.h>

#include <libusb.h>

extern struct device_drv *bfg_claim_usb(struct device_drv * const, const bool verbose, const uint8_t usbbus, const uint8_t usbaddr);
#define bfg_make_devid_libusb(dev)  bfg_make_devid_usb(libusb_get_bus_number(dev), libusb_get_device_address(dev))
#define bfg_claim_libusb(api, verbose, dev)  bfg_claim_usb(api, verbose, libusb_get_bus_number(dev), libusb_get_device_address(dev))

extern void cgpu_copy_libusb_strings(struct cgpu_info *, libusb_device *);

struct lowl_usb_endpoint;

extern struct lowl_usb_endpoint *usb_open_ep(struct libusb_device_handle *, uint8_t epid, int pktsz);
extern struct lowl_usb_endpoint *usb_open_ep_pair(struct libusb_device_handle *, uint8_t epid_r, int pktsz_r, uint8_t epid_w, int pktsz_w);
extern void usb_ep_set_timeouts_ms(struct lowl_usb_endpoint *, unsigned timeout_ms_r, unsigned timeout_ms_w);
extern ssize_t usb_read(struct lowl_usb_endpoint *, void *, size_t);
extern ssize_t usb_write(struct lowl_usb_endpoint *, const void *, size_t);
extern void usb_close_ep(struct lowl_usb_endpoint *);

#endif
