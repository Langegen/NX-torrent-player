#ifndef PC_NETINET_TCP_H
#define PC_NETINET_TCP_H

#ifdef _WIN32
#include "sys/socket.h"
#ifndef TCP_NODELAY
#define TCP_NODELAY 0x0001
#endif
#else
#include_next <netinet/tcp.h>
#endif

#endif
