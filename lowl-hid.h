#ifndef BFG_LOWL_HID_H
#define BFG_LOWL_HID_H

#include <hidapi.h>

#ifdef WIN32
#define HID_API_EXPORT __declspec(dllexport)
#else
#define HID_API_EXPORT /* */
#endif
extern struct hid_device_info HID_API_EXPORT *(*dlsym_hid_enumerate)(unsigned short, unsigned short);
extern void HID_API_EXPORT (*dlsym_hid_free_enumeration)(struct hid_device_info *);
extern hid_device * HID_API_EXPORT (*dlsym_hid_open_path)(const char *);
extern void HID_API_EXPORT (*dlsym_hid_close)(hid_device *);
extern int HID_API_EXPORT (*dlsym_hid_read)(hid_device *, unsigned char *, size_t);
extern int HID_API_EXPORT (*dlsym_hid_write)(hid_device *, const unsigned char *, size_t);

#define hid_enumerate dlsym_hid_enumerate
#define hid_free_enumeration dlsym_hid_free_enumeration
#define hid_open_path dlsym_hid_open_path
#define hid_close dlsym_hid_close
#define hid_read dlsym_hid_read
#define hid_write dlsym_hid_write

#endif
