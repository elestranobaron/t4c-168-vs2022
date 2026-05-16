#pragma once

/**
 * Équivalence minimale Winsock (Windows) ↔ sockets BSD / POSIX (Linux).
 * À inclure à la place de <winsock2.h> dans les unités de traduction portées.
 *
 * Sur Windows, on délègue à winsock2.h. Sur Linux, on fournit les types et
 * helpers habituels du code client (SOCKET, INVALID_SOCKET, WSAStartup, etc.).
 */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#else /* POSIX (Linux, etc.) */

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SOCKET
typedef int SOCKET;
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET static_cast<SOCKET>(-1)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#ifndef closesocket
#define closesocket(s) ::close(static_cast<int>(s))
#endif

#ifndef WSAGetLastError
#define WSAGetLastError() (errno)
#endif

#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK EWOULDBLOCK
#endif

#ifndef WSAEINTR
#define WSAEINTR EINTR
#endif

#ifndef WSAECONNABORTED
#define WSAECONNABORTED ECONNABORTED
#endif
#ifndef WSAEHOSTUNREACH
#define WSAEHOSTUNREACH EHOSTUNREACH
#endif
#ifndef WSAECONNRESET
#define WSAECONNRESET ECONNRESET
#endif
#ifndef WSAEADDRNOTAVAIL
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#endif
#ifndef WSAEAFNOSUPPORT
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#endif
#ifndef WSAETIMEDOUT
#define WSAETIMEDOUT ETIMEDOUT
#endif
#ifndef WSAENETUNREACH
#define WSAENETUNREACH ENETUNREACH
#endif

#ifndef LPSOCKADDR
typedef struct sockaddr *LPSOCKADDR;
#endif

#ifndef SOCKADDR_IN
typedef struct sockaddr_in SOCKADDR_IN;
#endif

typedef struct WSAData {
    unsigned short wVersion;
    unsigned short wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA, *LPWSADATA;

inline int WSAStartup(unsigned short wVersionRequested, LPWSADATA lpWSAData) {
    if (lpWSAData) {
        std::memset(lpWSAData, 0, sizeof(WSADATA));
        lpWSAData->wVersion = wVersionRequested;
        lpWSAData->wHighVersion = wVersionRequested;
        lpWSAData->iMaxSockets = 256;
        lpWSAData->iMaxUdpDg = 65507; /* DirectSocket.cpp lit ce champ */
    }
    return 0;
}

inline int WSACleanup(void) { return 0; }

#ifndef MAKEWORD
#define MAKEWORD(low, high) \
    ((unsigned short)(((unsigned char)((unsigned int)(low) & 0xff))) | \
     ((unsigned short)((unsigned char)((unsigned int)(high) & 0xff))) << 8)
#endif

/**
 * ioctlsocket Windows : FIONBIO + u_long*. Le client passe parfois NULL pour
 * repasser en bloquant (DirectSocket.cpp).
 */
inline int ioctlsocket(SOCKET s, long cmd, void *argp) {
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return SOCKET_ERROR;
    }
    if (cmd == FIONBIO) {
        int fd = static_cast<int>(s);
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            return SOCKET_ERROR;
        }
        bool nonblock = true;
        if (argp != nullptr) {
            const auto *mode = static_cast<const unsigned long *>(argp);
            nonblock = (*mode != 0u);
        } else {
            nonblock = false;
        }
        if (nonblock) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~static_cast<int>(O_NONBLOCK);
        }
        if (fcntl(fd, F_SETFL, flags) < 0) {
            return SOCKET_ERROR;
        }
        return 0;
    }
    errno = EINVAL;
    return SOCKET_ERROR;
}

#endif /* !_WIN32 */
