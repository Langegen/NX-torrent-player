#ifndef PC_POLL_H
#define PC_POLL_H

#ifdef _WIN32
#include "sys/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long nfds_t;

#define poll(fds, nfds, timeout) WSAPoll((LPWSAPOLLFD)(fds), (ULONG)(nfds), (INT)(timeout))

#ifdef __cplusplus
}
#endif

#else
#include_next <poll.h>
#endif

#endif
