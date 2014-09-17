#ifndef BFG_WINHACKS_H
#define BFG_WINHACKS_H

#include <winsock2.h>

// wincon.h contains a MOUSE_MOVED that conflicts with curses
#include <wincon.h>
#ifdef MOUSE_MOVED
#	undef MOUSE_MOVED
#endif

#endif
