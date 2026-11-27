// Offline tests for engine/hls_proxy.c (loopback HLS/HTTP proxy, M3U8 rewrite,
// custom headers, Range forwarding, and optional AES-128 segment decryption).

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
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int test_sock_t;
#define TEST_BAD_SOCK (-1)
#define test_close close
#endif

#include <curl/curl.h>

#ifdef HLS_PROXY_ENABLE_AES
#include <mbedtls/aes.h>
#endif

#include "hls_proxy.h"

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
    volatile int saw_referer;
    volatile int saw_ua;
    volatile int saw_range;
} mock_srv;

#ifdef HLS_PROXY_ENABLE_AES
static const uint8_t kTestKey[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};
static const char kTestPlain[] = "HELLO_HLS_PLAIN_SEGMENT_PAYLOAD!"; // 32 bytes
#endif

static void send_str(test_sock_t s, const char *str, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int r = send(s, str + sent, (int)(len - sent), 0);
#else
        ssize_t r = send(s, str + sent, len - sent, 0);
#endif
        if (r <= 0) break;
        sent += (size_t)r;
    }
}

static void *mock_thread(void *arg) {
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

        if (strstr(req, "Referer: https://player.test/") != NULL) sv->saw_referer++;
        if (strstr(req, "User-Agent: CustomAgent/2.0") != NULL) sv->saw_ua++;
        if (strstr(req, "Range: bytes=4-9") != NULL) sv->saw_range++;

        char path[512] = {0};
        sscanf(req, "%*s %511s", path);

        char hdr[512];
        if (strcmp(path, "/master.m3u8") == 0) {
            const char *body =
                "#EXTM3U\n"
                "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a1\",NAME=\"Rus\",URI=\"audio/rus.m3u8\"\n"
                "#EXT-X-STREAM-INF:BANDWIDTH=2000000,AUDIO=\"a1\"\n"
                "sub/1080.m3u8\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_str(c, hdr, (size_t)hn);
            send_str(c, body, strlen(body));
        } else if (strcmp(path, "/sub/1080.m3u8") == 0) {
            const char *body =
                "#EXTM3U\n"
                "#EXT-X-TARGETDURATION:10\n"
                "#EXT-X-MEDIA-SEQUENCE:5\n"
                "#EXT-X-MAP:URI=\"init.mp4\"\n"
                "#EXTINF:6.0,\n"
                "seg0.ts\n"
                "#EXTINF:6.0,\n"
                "/abs/seg1.ts\n"
                "#EXT-X-ENDLIST\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_str(c, hdr, (size_t)hn);
            send_str(c, body, strlen(body));
        } else if (strcmp(path, "/sub/seg0.ts") == 0) {
            const char *body = "0123456789ABCDEF";
            if (strstr(req, "Range: bytes=4-9")) {
                const char *part = "456789";
                int hn = snprintf(hdr, sizeof(hdr),
                                  "HTTP/1.1 206 Partial Content\r\n"
                                  "Content-Range: bytes 4-9/16\r\n"
                                  "Content-Length: 6\r\nConnection: close\r\n\r\n");
                send_str(c, hdr, (size_t)hn);
                send_str(c, part, 6);
            } else {
                int hn = snprintf(hdr, sizeof(hdr),
                                  "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n"
                                  "Content-Length: 16\r\nConnection: close\r\n\r\n");
                send_str(c, hdr, (size_t)hn);
                send_str(c, body, 16);
            }
        }
#ifdef HLS_PROXY_ENABLE_AES
        else if (strcmp(path, "/aes/list.m3u8") == 0) {
            const char *body =
                "#EXTM3U\n"
                "#EXT-X-TARGETDURATION:10\n"
                "#EXT-X-MEDIA-SEQUENCE:1\n"
                "#EXT-X-KEY:METHOD=AES-128,URI=\"/aes/key.bin\"\n"
                "#EXTINF:6.0,\n"
                "/aes/seg.bin\n"
                "#EXT-X-ENDLIST\n";
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                              strlen(body));
            send_str(c, hdr, (size_t)hn);
            send_str(c, body, strlen(body));
        } else if (strcmp(path, "/aes/key.bin") == 0) {
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Length: 16\r\nConnection: close\r\n\r\n");
            send_str(c, hdr, (size_t)hn);
            send_str(c, (const char *)kTestKey, 16);
        } else if (strcmp(path, "/aes/seg.bin") == 0) {
            // Encrypt kTestPlain (32 bytes) + PKCS#7 padding (16 bytes of 0x10) = 48 bytes
            uint8_t plain[48], cipher[48];
            memcpy(plain, kTestPlain, 32);
            memset(plain + 32, 16, 16);
            uint8_t iv[16] = {0};
            iv[15] = 1; // media sequence 1
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_enc(&aes, kTestKey, 128);
            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 48, iv, plain, cipher);
            mbedtls_aes_free(&aes);

            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\n"
                              "Content-Length: 48\r\nConnection: close\r\n\r\n");
            send_str(c, hdr, (size_t)hn);
            send_str(c, (const char *)cipher, 48);
        }
#endif
        else {
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            send_str(c, hdr, (size_t)hn);
        }
        test_close(c);
    }
    return NULL;
}

typedef struct {
    char *data;
    size_t len;
} mem_buf;

static size_t mem_write_cb(char *p, size_t sz, size_t nm, void *ud) {
    mem_buf *b = (mem_buf *)ud;
    size_t n = sz * nm;
    char *nd = (char *)realloc(b->data, b->len + n + 1);
    if (!nd) return 0;
    b->data = nd;
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
    return n;
}

static mem_buf curl_get(const char *url, const char *range) {
    mem_buf b = {0};
    CURL *c = curl_easy_init();
    CHECK(c != NULL);
    curl_easy_setopt(c, CURLOPT_URL, url);
    if (range) curl_easy_setopt(c, CURLOPT_RANGE, range);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    CHECK(rc == CURLE_OK);
    CHECK(code >= 200 && code < 300);
    return b;
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    curl_global_init(CURL_GLOBAL_ALL);

    mock_srv sv = {0};
    sv.fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(sv.fd != TEST_BAD_SOCK);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    CHECK(bind(sv.fd, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    CHECK(listen(sv.fd, 8) == 0);
    socklen_t sl = sizeof(sa);
    CHECK(getsockname(sv.fd, (struct sockaddr *)&sa, &sl) == 0);
    sv.port = ntohs(sa.sin_port);
    CHECK(pthread_create(&sv.thread, NULL, mock_thread, &sv) == 0);

    const char *hdrs[] = {
        "Referer: https://player.test/",
        "User-Agent: CustomAgent/2.0",
    };
    char err[128] = {0};
    hls_proxy *proxy = hls_proxy_start(hdrs, 2, err, sizeof(err));
    CHECK(proxy != NULL);
    CHECK(hls_proxy_port(proxy) > 0);

    // 1. Master playlist rewrite (relative sub/1080.m3u8 and audio/rus.m3u8)
    char target[512], pUrl[2048];
    snprintf(target, sizeof(target), "http://127.0.0.1:%u/master.m3u8", (unsigned)sv.port);
    CHECK(hls_proxy_make_url(proxy, target, pUrl, sizeof(pUrl)) == 0);
    mem_buf master = curl_get(pUrl, NULL);
    CHECK(master.data != NULL);
    CHECK(strstr(master.data, "#EXTM3U") != NULL);
    CHECK(strstr(master.data, "http://127.0.0.1:") != NULL);
    CHECK(sv.saw_referer >= 1);
    CHECK(sv.saw_ua >= 1);

    // Extract rewritten sub/1080.m3u8 URL from master playlist
    char *subLine = strstr(master.data, "http://127.0.0.1:");
    // First match is inside #EXT-X-MEDIA URI="..."; find the second match on its own line
    subLine = strstr(subLine + 10, "\nhttp://127.0.0.1:");
    CHECK(subLine != NULL);
    subLine++;
    char *subEnd = strchr(subLine, '\n');
    if (subEnd) *subEnd = 0;

    // 2. Media playlist served with text/plain Content-Type (detected by .m3u8 / #EXTM3U)
    mem_buf media = curl_get(subLine, NULL);
    CHECK(media.data != NULL);
    CHECK(strstr(media.data, "#EXT-X-MAP:URI=\"http://127.0.0.1:") != NULL);

    // Extract rewritten seg0.ts URL
    char *segLine = strstr(media.data, "\nhttp://127.0.0.1:");
    CHECK(segLine != NULL);
    segLine++;
    char *segEnd = strchr(segLine, '\n');
    if (segEnd) *segEnd = 0;

    // 3. Range request forwarding
    mem_buf segPart = curl_get(segLine, "4-9");
    CHECK(segPart.len == 6);
    CHECK(memcmp(segPart.data, "456789", 6) == 0);
    CHECK(sv.saw_range >= 1);

    free(master.data);
    free(media.data);
    free(segPart.data);

#ifdef HLS_PROXY_ENABLE_AES
    // 4. AES-128-CBC playlist + segment decryption
    snprintf(target, sizeof(target), "http://127.0.0.1:%u/aes/list.m3u8", (unsigned)sv.port);
    CHECK(hls_proxy_make_url(proxy, target, pUrl, sizeof(pUrl)) == 0);
    mem_buf aesList = curl_get(pUrl, NULL);
    CHECK(aesList.data != NULL);
    CHECK(strstr(aesList.data, "#EXT-X-KEY") == NULL); // stripped
    char *aesSeg = strstr(aesList.data, "\nhttp://127.0.0.1:");
    CHECK(aesSeg != NULL);
    aesSeg++;
    char *aesEnd = strchr(aesSeg, '\n');
    if (aesEnd) *aesEnd = 0;
    CHECK(strstr(aesSeg, "&k=0&iv=") != NULL);

    mem_buf aesPlain = curl_get(aesSeg, NULL);
    CHECK(aesPlain.len == 32);
    CHECK(memcmp(aesPlain.data, kTestPlain, 32) == 0);
    free(aesList.data);
    free(aesPlain.data);
#endif

    hls_proxy_stop(proxy);
    sv.stop = true;
#ifdef _WIN32
    shutdown(sv.fd, SD_BOTH);
#else
    shutdown(sv.fd, SHUT_RDWR);
#endif
    test_close(sv.fd);
    pthread_join(sv.thread, NULL);
    curl_global_cleanup();
    printf("test_hls_proxy: ALL PASSED\n");
    return 0;
}
