/*
 * Copyright 2011-2012 Andrew Smith
 * Copyright 2012-2013 Luke Dashjr
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "compat.h"

#ifndef WIN32
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>

	#define SOCKETTYPE int
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define CLOSESOCKET close

	#define SOCKETINIT do{}while(0)

	#define SOCKERRMSG strerror(errno)
#else
	#include <winsock2.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define CLOSESOCKET closesocket

	static char WSAbuf[1024];

	struct WSAERRORS {
		int id;
		char *code;
	} WSAErrors[] = {
		{ 0,			"No error" },
		{ WSAEINTR,		"Interrupted system call" },
		{ WSAEBADF,		"Bad file number" },
		{ WSAEACCES,		"Permission denied" },
		{ WSAEFAULT,		"Bad address" },
		{ WSAEINVAL,		"Invalid argument" },
		{ WSAEMFILE,		"Too many open sockets" },
		{ WSAEWOULDBLOCK,	"Operation would block" },
		{ WSAEINPROGRESS,	"Operation now in progress" },
		{ WSAEALREADY,		"Operation already in progress" },
		{ WSAENOTSOCK,		"Socket operation on non-socket" },
		{ WSAEDESTADDRREQ,	"Destination address required" },
		{ WSAEMSGSIZE,		"Message too long" },
		{ WSAEPROTOTYPE,	"Protocol wrong type for socket" },
		{ WSAENOPROTOOPT,	"Bad protocol option" },
		{ WSAEPROTONOSUPPORT,	"Protocol not supported" },
		{ WSAESOCKTNOSUPPORT,	"Socket type not supported" },
		{ WSAEOPNOTSUPP,	"Operation not supported on socket" },
		{ WSAEPFNOSUPPORT,	"Protocol family not supported" },
		{ WSAEAFNOSUPPORT,	"Address family not supported" },
		{ WSAEADDRINUSE,	"Address already in use" },
		{ WSAEADDRNOTAVAIL,	"Can't assign requested address" },
		{ WSAENETDOWN,		"Network is down" },
		{ WSAENETUNREACH,	"Network is unreachable" },
		{ WSAENETRESET,		"Net connection reset" },
		{ WSAECONNABORTED,	"Software caused connection abort" },
		{ WSAECONNRESET,	"Connection reset by peer" },
		{ WSAENOBUFS,		"No buffer space available" },
		{ WSAEISCONN,		"Socket is already connected" },
		{ WSAENOTCONN,		"Socket is not connected" },
		{ WSAESHUTDOWN,		"Can't send after socket shutdown" },
		{ WSAETOOMANYREFS,	"Too many references, can't splice" },
		{ WSAETIMEDOUT,		"Connection timed out" },
		{ WSAECONNREFUSED,	"Connection refused" },
		{ WSAELOOP,		"Too many levels of symbolic links" },
		{ WSAENAMETOOLONG,	"File name too long" },
		{ WSAEHOSTDOWN,		"Host is down" },
		{ WSAEHOSTUNREACH,	"No route to host" },
		{ WSAENOTEMPTY,		"Directory not empty" },
		{ WSAEPROCLIM,		"Too many processes" },
		{ WSAEUSERS,		"Too many users" },
		{ WSAEDQUOT,		"Disc quota exceeded" },
		{ WSAESTALE,		"Stale NFS file handle" },
		{ WSAEREMOTE,		"Too many levels of remote in path" },
		{ WSASYSNOTREADY,	"Network system is unavailable" },
		{ WSAVERNOTSUPPORTED,	"Winsock version out of range" },
		{ WSANOTINITIALISED,	"WSAStartup not yet called" },
		{ WSAEDISCON,		"Graceful shutdown in progress" },
		{ WSAHOST_NOT_FOUND,	"Host not found" },
		{ WSANO_DATA,		"No host data of that type was found" },
		{ -1,			"Unknown error code" }
	};

	static char *WSAErrorMsg()
	{
		int i;
		int id = WSAGetLastError();

		/* Assume none of them are actually -1 */
		for (i = 0; WSAErrors[i].id != -1; i++)
			if (WSAErrors[i].id == id)
				break;

		sprintf(WSAbuf, "Socket Error: (%d) %s", id, WSAErrors[i].code);

		return &(WSAbuf[0]);
	}

	#define SOCKERRMSG WSAErrorMsg()

	static WSADATA WSA_Data;

	#define SOCKETINIT	do {  \
		int wsa; \
				if ( (wsa = WSAStartup(0x0202, &WSA_Data)) ) { \
					printf("Socket startup failed: %d\n", wsa); \
					return 1; \
		}  \
	} while (0)

	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif
#endif

#define RECVSIZE 65500

static const char SEPARATOR = '|';
static const char COMMA = ',';
static const char EQ = '=';
static int ONLY;

void display(char *buf)
{
	char *nextobj, *item, *nextitem, *eq;
	int itemcount;

	while (buf != NULL) {
		nextobj = strchr(buf, SEPARATOR);
		if (nextobj != NULL)
			*(nextobj++) = '\0';

		if (*buf) {
			item = buf;
			itemcount = 0;
			while (item != NULL) {
				nextitem = strchr(item, COMMA);
				if (nextitem != NULL)
					*(nextitem++) = '\0';

				if (*item) {
					eq = strchr(item, EQ);
					if (eq != NULL)
						*(eq++) = '\0';

					if (itemcount == 0)
						printf("[%s%s] =>\n(\n", item, (eq != NULL && isdigit(*eq)) ? eq : "");

					if (eq != NULL)
						printf("   [%s] => %s\n", item, eq);
					else
						printf("   [%d] => %s\n", itemcount, item);
				}

				item = nextitem;
				itemcount++;
			}
			if (itemcount > 0)
				puts(")");
		}

		buf = nextobj;
	}
}

int callapi(char *command, char *host, short int port)
{
	size_t bufsz = RECVSIZE;
	char *buf = malloc(bufsz+1);
	struct hostent *ip;
	struct sockaddr_in serv;
	SOCKETTYPE sock;
	int ret = 0;
	int n, p;

	assert(buf);
	SOCKETINIT;

	ip = gethostbyname(host);
	if (!ip)
	{
		printf("Failed to resolve host %s\n", host);
		return 1;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVSOCK) {
		printf("Socket initialisation failed: %s\n", SOCKERRMSG);
		return 1;
	}

	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr = *((struct in_addr *)ip->h_addr);
	serv.sin_port = htons(port);

	if (SOCKETFAIL(connect(sock, (struct sockaddr *)&serv, sizeof(struct sockaddr)))) {
		printf("Socket connect failed: %s\n", SOCKERRMSG);
		return 1;
	}

	n = send(sock, command, strlen(command), 0);
	if (SOCKETFAIL(n)) {
		printf("Send failed: %s\n", SOCKERRMSG);
		ret = 1;
	}
	else {
		p = 0;
		buf[0] = '\0';
		while (true)
		{
			if (bufsz < RECVSIZE + p)
			{
				bufsz *= 2;
				buf = realloc(buf, bufsz);
				assert(buf);
			}
			
			n = recv(sock, &buf[p], RECVSIZE, 0);

			if (SOCKETFAIL(n)) {
				printf("Recv failed: %s\n", SOCKERRMSG);
				ret = 1;
				break;
			}

			if (n == 0)
				break;

			p += n;
			buf[p] = '\0';
		}

		if (!ONLY)
			printf("Reply was '%s'\n", buf);
		else
			printf("%s\n", buf);

		if (!ONLY)
			display(buf);
	}

	CLOSESOCKET(sock);

	return ret;
}

static char *trim(char *str)
{
	char *ptr;

	while (isspace(*str))
		str++;

	ptr = strchr(str, '\0');
	while (ptr-- > str) {
		if (isspace(*ptr))
			*ptr = '\0';
	}

	return str;
}

int main(int argc, char *argv[])
{
	char *command = "summary";
	char *host = "127.0.0.1";
	short int port = 4028;
	char *ptr;
	int i = 1;

	if (argc > 1)
		if (strcmp(argv[1], "-?") == 0
		||  strcmp(argv[1], "-h") == 0
		||  strcmp(argv[1], "--help") == 0) {
			fprintf(stderr, "Usage: %s [command [ip/host [port]]]\n", argv[0]);
			return 1;
		}

	if (argc > 1)
		if (strcmp(argv[1], "-o") == 0) {
			ONLY = 1;
			i = 2;
		}

	if (argc > i) {
		ptr = trim(argv[i++]);
		if (strlen(ptr) > 0)
			command = ptr;
	}

	if (argc > i) {
		ptr = trim(argv[i++]);
		if (strlen(ptr) > 0)
			host = ptr;
	}

	if (argc > i) {
		ptr = trim(argv[i]);
		if (strlen(ptr) > 0)
			port = atoi(ptr);
	}

	return callapi(command, host, port);
}
