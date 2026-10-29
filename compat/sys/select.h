#ifndef PC_SYS_SELECT_H
#define PC_SYS_SELECT_H

#ifdef _WIN32
#include "sys/socket.h"
#else
#include_next <sys/select.h>
#endif

#endif
