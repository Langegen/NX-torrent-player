#ifndef PC_SYS_SOCKET_H
#define PC_SYS_SOCKET_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif
#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#endif
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#endif
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif

static inline void pc_winsock_init(void) {
    static int init = 0;
    if (!init) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        init = 1;
    }
}

static inline int pc_close(int fd) {
    if (fd < 0) return -1;
    int optval = 0;
    int optlen = sizeof(optval);
    if (getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char*)&optval, &optlen) == 0) {
        return closesocket((SOCKET)fd);
    }
    return _close(fd);
}
#define close(fd) pc_close(fd)

static inline int pc_socket(int af, int type, int protocol) {
    pc_winsock_init();
    SOCKET s = socket(af, type, protocol);
    if (s == INVALID_SOCKET) {
        errno = WSAGetLastError();
        return -1;
    }
    return (int)s;
}
#define socket pc_socket

static inline int pc_connect(int s, const struct sockaddr *name, int namelen) {
    int r = connect((SOCKET)s, name, namelen);
    if (r < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            errno = EINPROGRESS;
        } else {
            errno = err;
        }
    }
    return r;
}
#define connect pc_connect

static inline int pc_bind(int s, const struct sockaddr *name, int namelen) {
    int r = bind((SOCKET)s, name, namelen);
    if (r < 0) errno = WSAGetLastError();
    return r;
}
#define bind pc_bind

static inline int pc_listen(int s, int backlog) {
    int r = listen((SOCKET)s, backlog);
    if (r < 0) errno = WSAGetLastError();
    return r;
}
#define listen pc_listen

static inline int pc_accept(int s, struct sockaddr *addr, int *addrlen) {
    SOCKET res = accept((SOCKET)s, addr, addrlen);
    if (res == INVALID_SOCKET) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) errno = EWOULDBLOCK;
        else errno = err;
        return -1;
    }
    return (int)res;
}
#define accept pc_accept

static inline int pc_recv(int s, void *buf, int len, int flags) {
    int r = recv((SOCKET)s, (char*)buf, len, flags);
    if (r < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) errno = EWOULDBLOCK;
        else if (err == WSAEINTR) errno = EINTR;
        else errno = err;
    }
    return r;
}
#define recv pc_recv

static inline int pc_send(int s, const void *buf, int len, int flags) {
    int r = send((SOCKET)s, (const char*)buf, len, flags);
    if (r < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) errno = EWOULDBLOCK;
        else if (err == WSAEINTR) errno = EINTR;
        else errno = err;
    }
    return r;
}
#define send pc_send

static inline int pc_recvfrom(int s, void *buf, int len, int flags, struct sockaddr *from, int *fromlen) {
    int r = recvfrom((SOCKET)s, (char*)buf, len, flags, from, fromlen);
    if (r < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) errno = EWOULDBLOCK;
        else if (err == WSAEINTR) errno = EINTR;
        else errno = err;
    }
    return r;
}
#define recvfrom(s, buf, len, flags, from, fromlen) pc_recvfrom((int)(s), (void*)(buf), (int)(len), (int)(flags), (struct sockaddr*)(from), (int*)(fromlen))

static inline int pc_sendto(int s, const void *buf, int len, int flags, const struct sockaddr *to, int tolen) {
    int r = sendto((SOCKET)s, (const char*)buf, len, flags, to, tolen);
    if (r < 0) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) errno = EWOULDBLOCK;
        else if (err == WSAEINTR) errno = EINTR;
        else errno = err;
    }
    return r;
}
#define sendto(s, buf, len, flags, to, tolen) pc_sendto((int)(s), (const void*)(buf), (int)(len), (int)(flags), (const struct sockaddr*)(to), (int)(tolen))

static inline int pc_setsockopt(int s, int level, int optname, const void *optval, int optlen) {
    return setsockopt((SOCKET)s, level, optname, (const char*)optval, optlen);
}
#define setsockopt(s, level, optname, optval, optlen) pc_setsockopt((int)(s), (int)(level), (int)(optname), (const void*)(optval), (int)(optlen))

static inline int pc_getsockopt(int s, int level, int optname, void *optval, int *optlen) {
    return getsockopt((SOCKET)s, level, optname, (char*)optval, optlen);
}
#define getsockopt(s, level, optname, optval, optlen) pc_getsockopt((int)(s), (int)(level), (int)(optname), (void*)(optval), (int*)(optlen))

#ifdef __cplusplus
}
#endif

#else
#include_next <sys/socket.h>
#endif

#endif // PC_SYS_SOCKET_H
