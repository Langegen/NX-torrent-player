#ifndef PC_NETINET_IN_H
#define PC_NETINET_IN_H

#ifdef _WIN32
#include "sys/socket.h"
#else
#include_next <netinet/in.h>
#endif

#endif
