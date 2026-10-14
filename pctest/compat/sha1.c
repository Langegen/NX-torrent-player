#include "mbedtls/sha1.h"
#include <string.h>

#define ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

#define BLK0(i) (block->l[i] = (ROL(block->l[i], 24) & 0xFF00FF00) | (ROL(block->l[i], 8) & 0x00FF00FF))
#define BLK(i) (block->l[i & 15] = ROL(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] ^ block->l[(i + 2) & 15] ^ block->l[i & 15], 1))

#define R0(v,w,x,y,z,i) z += ((w & (x ^ y)) ^ y) + BLK0(i) + 0x5A827999 + ROL(v, 5); w = ROL(w, 30);
#define R1(v,w,x,y,z,i) z += ((w & (x ^ y)) ^ y) + BLK(i) + 0x5A827999 + ROL(v, 5); w = ROL(w, 30);
#define R2(v,w,x,y,z,i) z += (w ^ x ^ y) + BLK(i) + 0x6ED9EBA1 + ROL(v, 5); w = ROL(w, 30);
#define R3(v,w,x,y,z,i) z += (((w | x) & y) | (w & x)) + BLK(i) + 0x8F1BBCDC + ROL(v, 5); w = ROL(w, 30);
#define R4(v,w,x,y,z,i) z += (w ^ x ^ y) + BLK(i) + 0xCA62C1D6 + ROL(v, 5); w = ROL(w, 30);

typedef union {
    uint8_t c[64];
    uint32_t l[16];
} CHAR64LONG16;

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    CHAR64LONG16 block[1];
    memcpy(block, buffer, 64);

    R0(a, b, c, d, e, 0);  R0(e, a, b, c, d, 1);  R0(d, e, a, b, c, 2);  R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4);  R0(a, b, c, d, e, 5);  R0(e, a, b, c, d, 6);  R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8);  R0(b, c, d, e, a, 9);  R0(a, b, c, d, e, 10); R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12); R0(c, d, e, a, b, 13); R0(b, c, d, e, a, 14); R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20); R2(e, a, b, c, d, 21); R2(d, e, a, b, c, 22); R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24); R2(a, b, c, d, e, 25); R2(e, a, b, c, d, 26); R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28); R2(b, c, d, e, a, 29); R2(a, b, c, d, e, 30); R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32); R2(c, d, e, a, b, 33); R2(b, c, d, e, a, 34); R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36); R2(d, e, a, b, c, 37); R2(c, d, e, a, b, 38); R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40); R3(e, a, b, c, d, 41); R3(d, e, a, b, c, 42); R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44); R3(a, b, c, d, e, 45); R3(e, a, b, c, d, 46); R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48); R3(b, c, d, e, a, 49); R3(a, b, c, d, e, 50); R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52); R3(c, d, e, a, b, 53); R3(b, c, d, e, a, 54); R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56); R3(d, e, a, b, c, 57); R3(c, d, e, a, b, 58); R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60); R4(e, a, b, c, d, 61); R4(d, e, a, b, c, 62); R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64); R4(a, b, c, d, e, 65); R4(e, a, b, c, d, 66); R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68); R4(b, c, d, e, a, 69); R4(a, b, c, d, e, 70); R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72); R4(c, d, e, a, b, 73); R4(b, c, d, e, a, 74); R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76); R4(d, e, a, b, c, 77); R4(c, d, e, a, b, 78); R4(b, c, d, e, a, 79);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void mbedtls_sha1_init(mbedtls_sha1_context *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_sha1_free(mbedtls_sha1_context *ctx) {
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

int mbedtls_sha1_starts_ret(mbedtls_sha1_context *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
    return 0;
}

int mbedtls_sha1_update_ret(mbedtls_sha1_context *ctx, const unsigned char *input, size_t ilen) {
    size_t left = (size_t)((ctx->count >> 3) & 0x3F);
    size_t fill = 64 - left;
    ctx->count += (uint64_t)ilen << 3;

    if (left && ilen >= fill) {
        memcpy(ctx->buffer + left, input, fill);
        sha1_transform(ctx->state, ctx->buffer);
        input += fill;
        ilen -= fill;
        left = 0;
    }

    while (ilen >= 64) {
        sha1_transform(ctx->state, input);
        input += 64;
        ilen -= 64;
    }

    if (ilen > 0) {
        memcpy(ctx->buffer + left, input, ilen);
    }
    return 0;
}

int mbedtls_sha1_finish_ret(mbedtls_sha1_context *ctx, unsigned char output[20]) {
    uint8_t final_count[8];
    for (int i = 0; i < 8; i++) {
        final_count[i] = (uint8_t)((ctx->count >> ((7 - i) * 8)) & 0xFF);
    }

    uint8_t c = 0x80;
    mbedtls_sha1_update_ret(ctx, &c, 1);
    while ((ctx->count & 504) != 448) {
        c = 0;
        mbedtls_sha1_update_ret(ctx, &c, 1);
    }
    mbedtls_sha1_update_ret(ctx, final_count, 8);

    for (int i = 0; i < 5; i++) {
        output[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        output[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        output[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        output[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFF);
    }
    return 0;
}

int mbedtls_sha1(const unsigned char *input, size_t ilen, unsigned char output[20]) {
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts_ret(&ctx);
    mbedtls_sha1_update_ret(&ctx, input, ilen);
    mbedtls_sha1_finish_ret(&ctx, output);
    mbedtls_sha1_free(&ctx);
    return 0;
}
