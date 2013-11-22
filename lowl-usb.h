#ifndef BFG_LOWL_USB_H
#define BFG_LOWL_USB_H

#include <libusb.h>

extern struct device_drv *bfg_claim_usb(struct device_drv * const, const bool verbose, const uint8_t usbbus, const uint8_t usbaddr);
#define bfg_make_devid_libusb(dev)  bfg_make_devid_usb(libusb_get_bus_number(dev), libusb_get_device_address(dev))
#define bfg_claim_libusb(api, verbose, dev)  bfg_claim_usb(api, verbose, libusb_get_bus_number(dev), libusb_get_device_address(dev))

extern void cgpu_copy_libusb_strings(struct cgpu_info *, libusb_device *);

#endif
