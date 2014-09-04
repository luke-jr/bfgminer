#ifndef BFG_LOWL_MSWIN_H
#define BFG_LOWL_MSWIN_H

#include <stdbool.h>

#include <rpc.h>

static const GUID WIN_GUID_DEVINTERFACE_MonarchKMDF = { 0xdcdb8d6f, 0x98b0, 0x4d1c, {0xa2, 0x77, 0x71, 0x17, 0x69, 0x70, 0x54, 0x31} };

extern bool lowl_mswin_match_guid(const struct lowlevel_device_info *, const GUID *);

#endif
