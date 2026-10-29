#ifndef PC_SYS_STAT_H
#define PC_SYS_STAT_H

#include_next <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>

#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif

#endif

#endif
