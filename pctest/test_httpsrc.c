// Offline tests for engine/httpsrc.c (HTTP range reads through curl).
//
// A mock HTTP/1.1 server on 127.0.0.1 serves a generated 10 MiB file, with or
// without Range support, so chunk boundaries, seeks, redirects and failures
// are exercised without the network. Plain http: TLS is curl's business, the
// chunking logic is ours.

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <curl/curl.h>

#include "httpsrc.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

#define FILE_SIZE (10 * 1024 * 1024 + 12345)

static uint8_t byte_at(int64_t i) { return (uint8_t)((i * 31) ^ (i >> 9)); }

//---------------------------------------------------------------------------
// Mock server
//---------------------------------------------------------------------------

typedef struct {
    int fd;
    uint16_t port;
    pthread_t thread;
    bool ranges;          // honour Range (206) or always send the whole file
    bool redirect;        // "/start" answers 302 to "/file"
    int fail_next;        // answer this many requests with 503 first
    volatile int requests;
    volatile int redirects_served;
} server;

static int send_all(int fd, const void *b, size_t n) {
    const char *p = b;
    while (n) {
        ssize_t r = send(fd, p, n, 0);
        if (r <= 0) return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static void serve(server *sv, int fd) {
    char req[4096];
    size_t len = 0;
    while (len < sizeof(req) - 1) {
        ssize_t r = recv(fd, req + len, sizeof(req) - 1 - len, 0);
        if (r <= 0) return;
        len += (size_t)r;
        req[len] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    sv->requests++;

    char hdr[512];
    if (sv->fail_next > 0) {
        sv->fail_next--;
        int n = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        send_all(fd, hdr, (size_t)n);
        return;
    }
    if (sv->redirect && strncmp(req, "GET /start", 10) == 0) {
        sv->redirects_served++;
        int n = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 302 Found\r\nLocation: /file\r\nContent-Length: 0\r\n"
                         "Connection: close\r\n\r\n");
        send_all(fd, hdr, (size_t)n);
        return;
    }

    long long a = 0, b = FILE_SIZE - 1;
    bool ranged = false;
    char *rg = strstr(req, "\r\nRange: bytes=");   // curl spells it so
    if (sv->ranges && rg && sscanf(rg + 15, "%lld-%lld", &a, &b) >= 1) {
        ranged = true;
        if (b >= FILE_SIZE) b = FILE_SIZE - 1;
    }
    int n;
    if (ranged)
        n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 206 Partial Content\r\nContent-Length: %lld\r\n"
                     "Content-Range: bytes %lld-%lld/%d\r\nConnection: close\r\n\r\n",
                     b - a + 1, a, b, FILE_SIZE);
    else
        n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                     FILE_SIZE);
    if (send_all(fd, hdr, (size_t)n) != 0) return;

    uint8_t blk[16384];
    for (long long i = a; i <= b;) {
        size_t m = 0;
        while (m < sizeof(blk) && i <= b) blk[m++] = byte_at(i++);
        if (send_all(fd, blk, m) != 0) return;   // client stopped reading
    }
}

static void *server_main(void *arg) {
    server *sv = arg;
    for (;;) {
        int c = accept(sv->fd, NULL, NULL);
        if (c < 0) return NULL;
        serve(sv, c);
        shutdown(c, SHUT_WR);
        close(c);
    }
}

static void server_start(server *sv, bool ranges) {
    memset(sv, 0, sizeof(*sv));
    sv->ranges = ranges;
    sv->fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(sv->fd >= 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(sv->fd, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    CHECK(listen(sv->fd, 16) == 0);
    socklen_t sl = sizeof(sa);
    getsockname(sv->fd, (struct sockaddr *)&sa, &sl);
    sv->port = ntohs(sa.sin_port);
    pthread_create(&sv->thread, NULL, server_main, sv);
}

static void server_stop(server *sv) {
    shutdown(sv->fd, SHUT_RDWR);
    close(sv->fd);
    pthread_join(sv->thread, NULL);
}

static void url_of(char *out, size_t n, const server *sv, const char *path) {
    snprintf(out, n, "http://127.0.0.1:%u%s", sv->port, path);
}

static bool matches(const uint8_t *b, int64_t pos, int64_t n) {
    for (int64_t i = 0; i < n; i++)
        if (b[i] != byte_at(pos + i)) return false;
    return true;
}

//---------------------------------------------------------------------------
// Tests
//---------------------------------------------------------------------------

static void test_sequential_read(void) {
    printf("[1/6] Size, then a sequential read of the whole file...\n");
    server sv;
    server_start(&sv, true);
    char url[128], err[160] = {0};
    url_of(url, sizeof(url), &sv, "/file");

    httpsrc *s = httpsrc_open(url, err, sizeof(err));
    CHECK(s != NULL);
    CHECK(httpsrc_size(s) == FILE_SIZE);

    // Odd-sized reads, so they straddle the 4 MiB chunk boundaries.
    uint8_t *buf = malloc(100000);
    int64_t pos = 0;
    while (pos < FILE_SIZE) {
        int64_t n = httpsrc_read(s, pos, buf, 99991);
        CHECK(n > 0);
        CHECK(matches(buf, pos, n));
        pos += n;
    }
    CHECK(httpsrc_read(s, pos, buf, 10) == 0);   // EOF
    printf("      %d requests for %d bytes\n", sv.requests, FILE_SIZE);
    CHECK(sv.requests == 3);                     // 4 + 4 + 2 MiB
    free(buf);
    httpsrc_close(s);
    server_stop(&sv);
    printf("      PASSED\n");
}

static void test_seek(void) {
    printf("[2/6] Seeks: far ahead, back to the start, inside the chunk...\n");
    server sv;
    server_start(&sv, true);
    char url[128], err[160] = {0};
    url_of(url, sizeof(url), &sv, "/file");
    httpsrc *s = httpsrc_open(url, err, sizeof(err));
    CHECK(s != NULL);

    uint8_t buf[4096];
    int64_t at[] = { FILE_SIZE - 100, 0, 9000000, 9000000 + 4000, 5 };
    for (size_t i = 0; i < sizeof(at) / sizeof(*at); i++) {
        int64_t n = httpsrc_read(s, at[i], buf, sizeof(buf));
        CHECK(n > 0);
        CHECK(matches(buf, at[i], n));
    }
    // One chunk is kept: the open, then tail, start, 9000000 (+4000 is a hit
    // in that chunk) and 5 (the start chunk was replaced) -- 5 requests.
    printf("      %d requests\n", sv.requests);
    CHECK(sv.requests == 5);
    CHECK(httpsrc_read(s, FILE_SIZE, buf, 1) == 0);
    httpsrc_close(s);
    server_stop(&sv);
    printf("      PASSED\n");
}

static void test_no_ranges(void) {
    printf("[3/6] A server without Range support is refused with a reason...\n");
    server sv;
    server_start(&sv, false);
    char url[128], err[160] = {0};
    url_of(url, sizeof(url), &sv, "/file");
    httpsrc *s = httpsrc_open(url, err, sizeof(err));
    printf("      err='%s'\n", err);
    CHECK(s == NULL);
    CHECK(strstr(err, "range") != NULL);
    server_stop(&sv);
    printf("      PASSED\n");
}

static void test_redirect(void) {
    printf("[4/6] A redirect is followed once, then read directly...\n");
    server sv;
    server_start(&sv, true);
    sv.redirect = true;
    char url[128], err[160] = {0};
    url_of(url, sizeof(url), &sv, "/start");
    httpsrc *s = httpsrc_open(url, err, sizeof(err));
    CHECK(s != NULL);
    uint8_t buf[512];
    CHECK(httpsrc_read(s, 6000000, buf, sizeof(buf)) == sizeof(buf));
    CHECK(matches(buf, 6000000, sizeof(buf)));
    printf("      redirects served: %d\n", sv.redirects_served);
    CHECK(sv.redirects_served == 1);
    httpsrc_close(s);
    server_stop(&sv);
    printf("      PASSED\n");
}

static void test_retry(void) {
    printf("[5/6] A failed chunk request is retried...\n");
    server sv;
    server_start(&sv, true);
    char url[128], err[160] = {0};
    url_of(url, sizeof(url), &sv, "/file");
    httpsrc *s = httpsrc_open(url, err, sizeof(err));
    CHECK(s != NULL);
    sv.fail_next = 2;   // two 503s, the third attempt succeeds
    uint8_t buf[512];
    CHECK(httpsrc_read(s, 7000000, buf, sizeof(buf)) == sizeof(buf));
    CHECK(matches(buf, 7000000, sizeof(buf)));
    sv.fail_next = 10;  // more than the retries: the read fails, with a reason
    CHECK(httpsrc_read(s, 1000, buf, sizeof(buf)) == -1);
    printf("      err='%s'\n", httpsrc_last_err(s));
    CHECK(strstr(httpsrc_last_err(s), "503") != NULL);
    httpsrc_close(s);
    server_stop(&sv);
    printf("      PASSED\n");
}

static void test_cancel(void) {
    printf("[6/6] After cancel every read fails at once...\n");
    server sv;
    server_start(&sv, true);
    char url[128], err[160] = {0};
    url_of(url, sizeof(url), &sv, "/file");
    httpsrc *s = httpsrc_open(url, err, sizeof(err));
    CHECK(s != NULL);
    int before = sv.requests;
    httpsrc_cancel(s);
    uint8_t buf[16];
    CHECK(httpsrc_read(s, 0, buf, sizeof(buf)) == -1);
    CHECK(httpsrc_read(s, 8000000, buf, sizeof(buf)) == -1);
    CHECK(sv.requests == before);   // nothing was sent
    httpsrc_close(s);
    server_stop(&sv);
    printf("      PASSED\n");
}

int main(void) {
    printf("=========================================\n");
    printf(" NX TORRENT PLAYER: HTTP SOURCE TESTS    \n");
    printf("=========================================\n\n");
    curl_global_init(CURL_GLOBAL_DEFAULT);

    test_sequential_read();
    test_seek();
    test_no_ranges();
    test_redirect();
    test_retry();
    test_cancel();

    curl_global_cleanup();
    printf("\nALL TESTS PASSED\n");
    return 0;
}
