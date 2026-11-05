#pragma once

// Force-included into every engine source on the Switch (CMakeLists.txt). The
// engine ported from TorrentShopNX was written against its build, which
// force-includes a header like this one: these files use htons/poll/getaddrinfo
// without including the headers themselves.

#ifdef __SWITCH__
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>

#ifndef ESHUTDOWN
#define ESHUTDOWN EPIPE
#endif
#endif
