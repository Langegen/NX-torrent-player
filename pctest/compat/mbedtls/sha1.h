#ifndef MBEDTLS_SHA1_H
#define MBEDTLS_SHA1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} mbedtls_sha1_context;

void mbedtls_sha1_init(mbedtls_sha1_context *ctx);
void mbedtls_sha1_free(mbedtls_sha1_context *ctx);
int mbedtls_sha1_starts_ret(mbedtls_sha1_context *ctx);
int mbedtls_sha1_update_ret(mbedtls_sha1_context *ctx, const unsigned char *input, size_t ilen);
int mbedtls_sha1_finish_ret(mbedtls_sha1_context *ctx, unsigned char output[20]);
int mbedtls_sha1(const unsigned char *input, size_t ilen, unsigned char output[20]);

// mbedtls 3 names (no _ret), as the ported dhtclient.c uses them.
static inline int mbedtls_sha1_starts(mbedtls_sha1_context *ctx) {
    return mbedtls_sha1_starts_ret(ctx);
}
static inline int mbedtls_sha1_update(mbedtls_sha1_context *ctx,
                                      const unsigned char *in, size_t n) {
    return mbedtls_sha1_update_ret(ctx, in, n);
}
static inline int mbedtls_sha1_finish(mbedtls_sha1_context *ctx,
                                      unsigned char out[20]) {
    return mbedtls_sha1_finish_ret(ctx, out);
}

#ifdef __cplusplus
}
#endif

#endif
