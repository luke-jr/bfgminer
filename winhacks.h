#ifndef BFG_WINHACKS_H
#define BFG_WINHACKS_H

#include <winsock2.h>

// wincon.h contains a KEY_EVENT that conflicts with ncurses
#include <wincon.h>
#ifdef KEY_EVENT
#	undef KEY_EVENT
#endif
// wincon.h contains a MOUSE_MOVED that conflicts with curses
#ifdef MOUSE_MOVED
#	undef MOUSE_MOVED
#endif

#endif
