#ifndef _BFG_HTTPSRV_H
#define _BFG_HTTPSRV_H

#include <microhttpd.h>

extern void httpsrv_start(unsigned short port);
extern void httpsrv_prepare_resp(struct MHD_Response *);
extern void httpsrv_stop();

#endif
