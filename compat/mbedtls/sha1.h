#ifndef COMPAT_MBEDTLS_SHA1_H
#define COMPAT_MBEDTLS_SHA1_H

#include_next <mbedtls/sha1.h>

#ifndef mbedtls_sha1_starts_ret
#define mbedtls_sha1_starts_ret mbedtls_sha1_starts
#endif
#ifndef mbedtls_sha1_update_ret
#define mbedtls_sha1_update_ret mbedtls_sha1_update
#endif
#ifndef mbedtls_sha1_finish_ret
#define mbedtls_sha1_finish_ret mbedtls_sha1_finish
#endif

#endif
