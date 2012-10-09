#ifndef __UTIL_H__
#define __UTIL_H__

#if defined(unix) || defined(__APPLE__)
	#include <errno.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>

	#define SOCKETTYPE int
	#define SOCKETFAIL(a) ((a) < 0)
	#define INVSOCK -1
	#define INVINETADDR -1
	#define CLOSESOCKET close

	#define SOCKERRMSG strerror(errno)
#elif defined WIN32
	#include <ws2tcpip.h>
	#include <winsock2.h>

	#define SOCKETTYPE SOCKET
	#define SOCKETFAIL(a) ((a) == SOCKET_ERROR)
	#define INVSOCK INVALID_SOCKET
	#define INVINETADDR INADDR_NONE
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

	#ifndef SHUT_RDWR
	#define SHUT_RDWR SD_BOTH
	#endif

	#ifndef in_addr_t
	#define in_addr_t uint32_t
	#endif
#endif
struct pool;
bool sock_send(int sock, char *s, ssize_t len);
char *recv_line(SOCKETTYPE sock);
bool parse_method(struct pool *pool, char *s);
bool extract_sockaddr(struct pool *pool, char *url);
bool auth_stratum(struct pool *pool);
bool initiate_stratum(struct pool *pool);

#endif /* __UTIL_H__ */
