#ifndef PC_ARPA_INET_H
#define PC_ARPA_INET_H

#ifdef _WIN32
#include "sys/socket.h"
#else
#include_next <arpa/inet.h>
#endif

#endif
