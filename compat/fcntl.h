#ifndef PC_FCNTL_H
#define PC_FCNTL_H

#include_next <fcntl.h>

#ifdef _WIN32
#include "sys/socket.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif

static inline int pc_fcntl(int fd, int cmd, ...) {
    if (cmd == F_GETFL) {
        return 0;
    }
    if (cmd == F_SETFL) {
        va_list args;
        va_start(args, cmd);
        int arg = va_arg(args, int);
        va_end(args);
        u_long mode = (arg & O_NONBLOCK) ? 1 : 0;
        return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
    }
    return -1;
}
#define fcntl pc_fcntl

#ifdef __cplusplus
}
#endif

#endif

#endif
