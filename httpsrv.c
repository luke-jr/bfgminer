/*
 * Copyright 2013-2014 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#ifdef WIN32
#include <winsock2.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include <microhttpd.h>

#include "logging.h"
#include "miner.h"
#include "util.h"

static struct MHD_Daemon *httpsrv;

extern int handle_getwork(struct MHD_Connection *, bytes_t *);

void httpsrv_prepare_resp(struct MHD_Response *resp)
{
	MHD_add_response_header(resp, MHD_HTTP_HEADER_SERVER, bfgminer_name_slash_ver);
}

static
int httpsrv_handle_req(struct MHD_Connection *conn, const char *url, const char *method, bytes_t *upbuf)
{
	return handle_getwork(conn, upbuf);
}

static
int httpsrv_handle_access(void *cls, struct MHD_Connection *conn, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls)
{
	bytes_t *upbuf;
	
	if (!*con_cls)
	{
		*con_cls = upbuf = malloc(sizeof(bytes_t));
		bytes_init(upbuf);
		return MHD_YES;
	}
	
	upbuf = *con_cls;
	if (*upload_data_size)
	{
		bytes_append(upbuf, upload_data, *upload_data_size);
		*upload_data_size = 0;
		return MHD_YES;
	}
	return httpsrv_handle_req(conn, url, method, *con_cls);
}

static
void httpsrv_cleanup_request(void *cls, struct MHD_Connection *conn, void **con_cls, enum MHD_RequestTerminationCode toe)
{
	if (*con_cls)
	{
		bytes_t *upbuf = *con_cls;
		bytes_free(upbuf);
		free(upbuf);
		*con_cls = NULL;
	}
}

static
void httpsrv_log(void *arg, const char *fmt, va_list ap)
{
	if (!opt_debug)
		return;
	
	char tmp42[LOGBUFSIZ] = "HTTPSrv: ";
	vsnprintf(&tmp42[9], sizeof(tmp42)-9, fmt, ap);
	_applog(LOG_DEBUG, tmp42);
}

void httpsrv_start(unsigned short port)
{
	httpsrv = MHD_start_daemon(
		MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG,
		port, NULL, NULL,
		&httpsrv_handle_access, NULL,
		MHD_OPTION_NOTIFY_COMPLETED, &httpsrv_cleanup_request, NULL,
		MHD_OPTION_EXTERNAL_LOGGER, &httpsrv_log, NULL,
	MHD_OPTION_END);
	if (httpsrv)
		applog(LOG_NOTICE, "HTTP server listening on port %d", (int)port);
	else
		applog(LOG_ERR, "Failed to start HTTP server on port %d", (int)port);
}

void httpsrv_stop()
{
	if (!httpsrv)
		return;
	
	applog(LOG_DEBUG, "Stopping HTTP server");
	MHD_stop_daemon(httpsrv);
	httpsrv = NULL;
}
