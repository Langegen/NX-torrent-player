// Offline unit tests for engine/hlssrc.c and httpsrc_open_ex HLS auto-upgrade.

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET test_sock_t;
#define TEST_BAD_SOCK INVALID_SOCKET
#define test_close closesocket
#define test_sleep_ms(ms) Sleep(ms)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int test_sock_t;
#define TEST_BAD_SOCK (-1)
#define test_close close
#define test_sleep_ms(ms) usleep((ms) * 1000)
#endif

#include <curl/curl.h>

#ifdef HLS_PROXY_ENABLE_AES
#include <mbedtls/aes.h>
#endif

#include "hlssrc.h"
#include "httpsrc.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

typedef struct {
    test_sock_t fd;
    uint16_t port;
    pthread_t thread;
    volatile bool stop;
    volatile int seg0_hits;
    volatile int saw_byterange;
    volatile int saw_playlist_range;
    volatile int slow_mode;
} mock_srv;

static uint8_t g_ts_seg0[188 * 64];
static uint8_t g_ts_seg1[188 * 64];
static uint8_t g_ts_seg2[188 * 64];
static uint8_t g_fmp4_blob[4096];

static void init_fixtures(void) {
    for (int i = 0; i < 64; i++) {
        memset(g_ts_seg0 + i * 188, (uint8_t)(0x10 + (i & 0x0F)), 188);
        g_ts_seg0[i * 188] = 0x47;
        memset(g_ts_seg1 + i * 188, (uint8_t)(0x20 + (i & 0x0F)), 188);
        g_ts_seg1[i * 188] = 0x47;
        memset(g_ts_seg2 + i * 188, (uint8_t)(0x30 + (i & 0x0F)), 188);
        g_ts_seg2[i * 188] = 0x47;
    }
    memcpy(g_fmp4_blob, "FTYP_MOOV_INIT_HDR__", 20);
    for (size_t i = 20; i < 512; i++) g_fmp4_blob[i] = (uint8_t)(i & 0xFF);
    memcpy(g_fmp4_blob + 512, "MOOF_SEG_ONE_BYTES__", 20);
    for (size_t i = 532; i < 1536; i++) g_fmp4_blob[i] = (uint8_t)((i * 3) & 0xFF);
    memcpy(g_fmp4_blob + 1536, "MOOF_SEG_TWO_BYTES__", 20);
    for (size_t i = 1556; i < 2560; i++) g_fmp4_blob[i] = (uint8_t)((i * 7) & 0xFF);
}

static void send_all(test_sock_t s, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int r = send(s, p + sent, (int)(len - sent), 0);
#else
        ssize_t r = send(s, p + sent, len - sent, 0);
#endif
        if (r <= 0) break;
        sent += (size_t)r;
    }
}

static void *srv_thread(void *arg) {
    mock_srv *sv = (mock_srv *)arg;
    while (!sv->stop) {
        test_sock_t c = accept(sv->fd, NULL, NULL);
        if (c == TEST_BAD_SOCK) break;

        char req[4096];
        size_t rlen = 0;
        while (rlen < sizeof(req) - 1) {
#ifdef _WIN32
            int r = recv(c, req + rlen, (int)(sizeof(req) - 1 - rlen), 0);
#else
            ssize_t r = recv(c, req + rlen, sizeof(req) - 1 - rlen, 0);
#endif
            if (r <= 0) break;
            rlen += (size_t)r;
            req[rlen] = 0;
            if (strstr(req, "\r\n\r\n")) break;
        }
        if (rlen == 0) { test_close(c); continue; }

        char path[512] = {0};
        sscanf(req, "%*s %511s", path);
        bool has_range = (strstr(req, "Range:") != NULL || strstr(req, "range:") != NULL);

        char hdr[512];
        if (sv->slow_mode && strcmp(path, "/slow.m3u8") == 0) {
            const char *partial = "#EXTM3U\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: 4096\r\n\r\n");
            send_all(c, hdr, (size_t)hn);
            send_all(c, partial, strlen(partial));
            for (int i = 0; i < 40 && !sv->stop; i++) test_sleep_ms(50);
            test_close(c);
            continue;
        }

        if (strcmp(path, "/master.m3u8") == 0) {
            if (has_range) sv->saw_playlist_range++;
            const char *body =
                "#EXTM3U\n"
                "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n"
                "v360/index.m3u8\n"
                "#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1280x720\n"
                "v720/index.m3u8\n"
                "#EXT-X-STREAM-INF:BANDWIDTH=15000000,RESOLUTION=3840x2160\n"
                "v2160/index.m3u8\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_all(c, hdr, (size_t)hn);
            send_all(c, body, strlen(body));
        } else if (strcmp(path, "/v360/index.m3u8") == 0) {
            const char *body = "#EXTM3U\n#EXTINF:5.0,\n../ts/seg0.ts\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_all(c, hdr, (size_t)hn);
            send_all(c, body, strlen(body));
        } else if (strcmp(path, "/v720/index.m3u8") == 0) {
            if (has_range) sv->saw_playlist_range++;
            const char *body =
                "#EXTM3U\n"
                "#EXT-X-TARGETDURATION:6\n"
                "#EXT-X-MEDIA-SEQUENCE:0\n"
                "#EXTINF:1.0,\n../ts/seg0.ts\n"
                "#EXTINF:1.0,\n../ts/seg1.ts\n"
                "#EXTINF:1.0,\n../ts/seg2.ts\n"
                "#EXT-X-ENDLIST\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_all(c, hdr, (size_t)hn);
            send_all(c, body, strlen(body));
        } else if (strcmp(path, "/v2160/index.m3u8") == 0) {
            const char *body =
                "#EXTM3U\n"
                "#EXTINF:5.0,\n../ts/seg2.ts\n"
                "#EXT-X-ENDLIST\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_all(c, hdr, (size_t)hn);
            send_all(c, body, strlen(body));
        } else if (strcmp(path, "/ts/seg0.ts") == 0) {
            sv->seg0_hits++;
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              sizeof(g_ts_seg0));
            send_all(c, hdr, (size_t)hn);
            send_all(c, g_ts_seg0, sizeof(g_ts_seg0));
        } else if (strcmp(path, "/ts/seg1.ts") == 0) {
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              sizeof(g_ts_seg1));
            send_all(c, hdr, (size_t)hn);
            send_all(c, g_ts_seg1, sizeof(g_ts_seg1));
        } else if (strcmp(path, "/ts/seg2.ts") == 0) {
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              sizeof(g_ts_seg2));
            send_all(c, hdr, (size_t)hn);
            send_all(c, g_ts_seg2, sizeof(g_ts_seg2));
        } else if (strcmp(path, "/fmp4.m3u8") == 0) {
            const char *body =
                "#EXTM3U\n"
                "#EXT-X-MAP:URI=\"media.mp4\",BYTERANGE=\"512@0\"\n"
                "#EXTINF:4.0,\n#EXT-X-BYTERANGE:1024@512\nmedia.mp4\n"
                "#EXTINF:4.0,\n#EXT-X-BYTERANGE:1024\nmedia.mp4\n"
                "#EXT-X-ENDLIST\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_all(c, hdr, (size_t)hn);
            send_all(c, body, strlen(body));
        } else if (strcmp(path, "/media.mp4") == 0) {
            long long rs = 0, re = 0;
            const char *rp = strstr(req, "Range: bytes=");
            if (rp && sscanf(rp + 13, "%lld-%lld", &rs, &re) == 2 &&
                rs >= 0 && rs < (long long)sizeof(g_fmp4_blob) && re >= rs) {
                if (re >= (long long)sizeof(g_fmp4_blob))
                    re = (long long)sizeof(g_fmp4_blob) - 1;
                sv->saw_byterange++;
                size_t slice = (size_t)(re - rs + 1);
                int hn = snprintf(hdr, sizeof(hdr),
                                  "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
                                  "Content-Range: bytes %lld-%lld/%zu\r\nContent-Length: %zu\r\n"
                                  "Connection: close\r\n\r\n",
                                  rs, re, sizeof(g_fmp4_blob), slice);
                send_all(c, hdr, (size_t)hn);
                send_all(c, g_fmp4_blob + rs, slice);
            } else {
                send_all(c, "HTTP/1.1 400 Bad\r\nContent-Length: 0\r\n\r\n", 38);
            }
        } else if (strcmp(path, "/play/123") == 0) {
            const char *body =
                "#EXTM3U\n"
                "#EXTINF:5.0,\n/ts/seg1.ts\n"
                "#EXTINF:5.0,\n/ts/seg2.ts\n"
                "#EXT-X-ENDLIST\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_all(c, hdr, (size_t)hn);
            send_all(c, body, strlen(body));
        } else {
            send_all(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 44);
        }
        test_close(c);
    }
    return NULL;
}

static void start_srv(mock_srv *sv) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    memset(sv, 0, sizeof(*sv));
    sv->fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(sv->fd != TEST_BAD_SOCK);
    int one = 1;
    setsockopt(sv->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(sv->fd, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    CHECK(listen(sv->fd, 16) == 0);
    socklen_t sl = sizeof(sa);
    CHECK(getsockname(sv->fd, (struct sockaddr *)&sa, &sl) == 0);
    sv->port = ntohs(sa.sin_port);
    CHECK(pthread_create(&sv->thread, NULL, srv_thread, sv) == 0);
}

static void stop_srv(mock_srv *sv) {
    sv->stop = true;
    test_close(sv->fd);
    pthread_join(sv->thread, NULL);
}

static void read_exact(hlssrc *s, uint8_t *dst, size_t len) {
    size_t off = 0;
    while (off < len) {
        int64_t r = hlssrc_read(s, dst + off, (int64_t)(len - off));
        CHECK(r > 0);
        off += (size_t)r;
    }
}

typedef struct {
    char url[256];
    volatile bool abort_flag;
    hlssrc *res;
    char err[160];
} open_job;

static void *open_worker(void *arg) {
    open_job *j = (open_job *)arg;
    j->res = hlssrc_open_abortable(j->url, &j->abort_flag, j->err, sizeof(j->err));
    return NULL;
}

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    init_fixtures();

    mock_srv sv;
    start_srv(&sv);
    printf("test_hlssrc:\n");

    // 1. Master playlist selection (prefer 720p), sequential lossless concat,
    // seg0 cache on seek(0), and TS-aligned mid/tail seek.
    {
        sv.seg0_hits = 0;
        sv.saw_playlist_range = 0;
        int64_t bytes_before = httpsrc_bytes_total();
        hlssrc_set_quality(720, false);

        char url[256], err[160] = {0};
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/master.m3u8", (unsigned)sv.port);
        hlssrc *s = hlssrc_open_abortable(url, NULL, err, sizeof(err));
        CHECK(s != NULL);
        CHECK(sv.saw_playlist_range == 0);
        CHECK(sv.seg0_hits == 1);

        int64_t sz = hlssrc_size(s);
        CHECK(sz == (int64_t)(sizeof(g_ts_seg0) * 3));

        static uint8_t all[188 * 64 * 3];
        read_exact(s, all, sizeof(all));
        uint8_t eof_byte = 0;
        CHECK(hlssrc_read(s, &eof_byte, 1) == 0); // EOF

        CHECK(memcmp(all, g_ts_seg0, sizeof(g_ts_seg0)) == 0);
        CHECK(memcmp(all + sizeof(g_ts_seg0), g_ts_seg1, sizeof(g_ts_seg1)) == 0);
        CHECK(memcmp(all + sizeof(g_ts_seg0) * 2, g_ts_seg2, sizeof(g_ts_seg2)) == 0);
        CHECK(httpsrc_bytes_total() > bytes_before);

        // Tail probe seek then rewind to 0 must NOT re-download seg0!
        CHECK(hlssrc_seek(s, sz - 376) == sz - 376);
        uint8_t tail[188];
        CHECK(hlssrc_read(s, tail, sizeof(tail)) > 0);
        CHECK(tail[0] == 0x47);

        CHECK(hlssrc_seek(s, 0) == 0);
        uint8_t head[188];
        read_exact(s, head, sizeof(head));
        CHECK(memcmp(head, g_ts_seg0, sizeof(head)) == 0);
        CHECK(sv.seg0_hits == 1);

        hlssrc_close(s);
        printf("  ok: master 720p + sequential TS + seg0 cache + TS-aligned seek\n");
    }

    // 2. hide_4k skips 2160p when Auto is selected; !hide_4k picks 2160p.
    {
        hlssrc_set_quality(0, true);
        char url[256], err[160] = {0};
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/master.m3u8", (unsigned)sv.port);
        hlssrc *s = hlssrc_open_abortable(url, NULL, err, sizeof(err));
        CHECK(s != NULL);
        // Should pick 720p (3 segments) rather than 2160p (1 segment = seg2)
        uint8_t b[16];
        read_exact(s, b, sizeof(b));
        CHECK(b[0] == 0x47 && b[1] == 0x10);
        hlssrc_close(s);

        hlssrc_set_quality(0, false);
        s = hlssrc_open_abortable(url, NULL, err, sizeof(err));
        CHECK(s != NULL);
        read_exact(s, b, sizeof(b));
        CHECK(b[0] == 0x47 && b[1] == 0x30); // seg2
        hlssrc_close(s);
        printf("  ok: hide_4k / 4k variant selection\n");
    }

    // 3. #EXT-X-MAP + #EXT-X-BYTERANGE concatenation.
    {
        sv.saw_byterange = 0;
        char url[256], err[160] = {0};
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/fmp4.m3u8", (unsigned)sv.port);
        hlssrc *s = hlssrc_open_abortable(url, NULL, err, sizeof(err));
        CHECK(s != NULL);
        CHECK(hlssrc_size(s) == 2560);
        uint8_t got[2560];
        read_exact(s, got, sizeof(got));
        uint8_t dummy = 0;
        CHECK(hlssrc_read(s, &dummy, 1) == 0);
        CHECK(memcmp(got, g_fmp4_blob, 2560) == 0);
        CHECK(sv.saw_byterange == 3);
        hlssrc_close(s);
        printf("  ok: EXT-X-MAP + EXT-X-BYTERANGE fMP4 concat\n");
    }

    // 4. Extensionless URL auto-upgrade via httpsrc_open_ex.
    {
        char url[256], err[160] = {0};
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/play/123", (unsigned)sv.port);
        httpsrc_hls_probe probe;
        httpsrc *hs = httpsrc_open_ex(url, NULL, &probe, err, sizeof(err));
        CHECK(hs == NULL);
        CHECK(err[0] == 0);
        CHECK(probe.eff_url != NULL);
        CHECK(!probe.truncated && probe.data != NULL);

        hlssrc *s = hlssrc_open_with_manifest(probe.eff_url, probe.data,
                                              probe.len, NULL, err, sizeof(err));
        free(probe.data);
        free(probe.eff_url);
        CHECK(s != NULL);
        static uint8_t buf[188 * 64 * 2];
        read_exact(s, buf, sizeof(buf));
        CHECK(memcmp(buf, g_ts_seg1, sizeof(g_ts_seg1)) == 0);
        CHECK(memcmp(buf + sizeof(g_ts_seg1), g_ts_seg2, sizeof(g_ts_seg2)) == 0);
        hlssrc_close(s);
        printf("  ok: extensionless URL auto-upgrade to hlssrc\n");
    }

    // 5. Abort flag unblocks open promptly.
    {
        sv.slow_mode = 1;
        open_job job;
        memset(&job, 0, sizeof(job));
        snprintf(job.url, sizeof(job.url), "http://127.0.0.1:%u/slow.m3u8", (unsigned)sv.port);
        pthread_t t;
        CHECK(pthread_create(&t, NULL, open_worker, &job) == 0);
        test_sleep_ms(120);
        job.abort_flag = true;
        pthread_join(t, NULL);
        CHECK(job.res == NULL);
        CHECK(strcmp(job.err, "cancelled") == 0);
        printf("  ok: abort_flag cancels open immediately\n");
    }

    // 6. Direct httpsrc_open_abortable range reads and non-ranged rejection.
    {
        char url[256], err[160] = {0};
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/media.mp4", (unsigned)sv.port);
        httpsrc *hs = httpsrc_open_abortable(url, NULL, err, sizeof(err));
        CHECK(hs != NULL);
        CHECK(httpsrc_size(hs) == (int64_t)sizeof(g_fmp4_blob));
        uint8_t got[sizeof(g_fmp4_blob)];
        size_t off = 0;
        while (off < sizeof(got)) {
            int64_t r = httpsrc_read(hs, (int64_t)off, got + off, (int64_t)(sizeof(got) - off));
            CHECK(r > 0);
            off += (size_t)r;
        }
        CHECK(memcmp(got, g_fmp4_blob, sizeof(g_fmp4_blob)) == 0);
        httpsrc_close(hs);

        snprintf(url, sizeof(url), "http://127.0.0.1:%u/ts/seg0.ts", (unsigned)sv.port);
        hs = httpsrc_open_abortable(url, NULL, err, sizeof(err));
        CHECK(hs == NULL);
        CHECK(err[0] != 0);
        printf("  ok: httpsrc range reads & non-range rejection\n");
    }

    stop_srv(&sv);
    curl_global_cleanup();
    printf("test_hlssrc passed!\n");
    return 0;
}
