#ifndef PC_NETDB_H
#define PC_NETDB_H

#ifdef _WIN32
#include "sys/socket.h"
#else
#include_next <netdb.h>
#endif

#endif
