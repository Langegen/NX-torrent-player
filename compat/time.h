#ifndef COMPAT_TIME_H
#define COMPAT_TIME_H

#include_next <time.h>

#ifdef _WIN32
#ifndef gmtime_r
static inline struct tm* pc_gmtime_r(const time_t *t, struct tm *r) {
    gmtime_s(r, t);
    return r;
}
#define gmtime_r pc_gmtime_r
#endif

#ifndef localtime_r
static inline struct tm* pc_localtime_r(const time_t *t, struct tm *r) {
    localtime_s(r, t);
    return r;
}
#define localtime_r pc_localtime_r
#endif
#endif

#endif
