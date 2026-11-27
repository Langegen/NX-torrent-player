#include "hls_proxy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET hp_sock_t;
#define HP_BAD_SOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int hp_sock_t;
#define HP_BAD_SOCK (-1)
#endif

#include <pthread.h>
#include <curl/curl.h>

#ifdef HLS_PROXY_ENABLE_AES
#include <mbedtls/aes.h>
#endif

#include "thread_cfg.h"

#define HP_MAX_HEADERS     16
#define HP_MAX_HEADER_LEN  512
#define HP_QUEUE_CAP       32
#define HP_NUM_WORKERS     2
#define HP_MAX_M3U8_BYTES  (2 * 1024 * 1024)
#define HP_MAX_SEG_BYTES   (32 * 1024 * 1024)
#define HP_MAX_KEYS        64

static char g_ca[512];
static char g_proxy[512];

void hls_proxy_set_ca(const char *ca_path) {
    snprintf(g_ca, sizeof(g_ca), "%s", ca_path ? ca_path : "");
}

void hls_proxy_set_proxy(const char *proxy) {
    snprintf(g_proxy, sizeof(g_proxy), "%s", proxy ? proxy : "");
}

// ---------------------------------------------------------------------------
// Portable socket helpers
// ---------------------------------------------------------------------------

static void hp_sock_init(void) {
#ifdef _WIN32
    static int inited = 0;
    if (!inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        inited = 1;
    }
#endif
}

static void hp_sock_close(hp_sock_t s) {
    if (s == HP_BAD_SOCK) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

static void hp_sock_shutdown(hp_sock_t s) {
    if (s == HP_BAD_SOCK) return;
#ifdef _WIN32
    shutdown(s, SD_BOTH);
#else
    shutdown(s, SHUT_RDWR);
#endif
}

static void hp_sock_set_timeout(hp_sock_t s, int sec) {
#ifdef _WIN32
    DWORD ms = (DWORD)(sec * 1000);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static int hp_send_all(hp_sock_t s, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
#ifdef _WIN32
        int chunk = len > 65536 ? 65536 : (int)len;
        int r = send(s, p, chunk, 0);
#else
        ssize_t r = send(s, p, len, 0);
#endif
        if (r <= 0) return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static int hp_strcasecmp_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)tolower((unsigned char)a[i]);
        unsigned char cb = (unsigned char)tolower((unsigned char)b[i]);
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static const char *hp_strcasestr(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return hay;
    for (; *hay; hay++) {
        if (hp_strcasecmp_n(hay, needle, nlen) == 0) return hay;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Base64URL encode/decode (for passing target URLs in /p?u=...)
// ---------------------------------------------------------------------------

static int b64url_encode(const char *in, size_t in_len, char *out, size_t out_cap) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t v = ((uint32_t)(unsigned char)in[i]) << 16;
        int rem = (int)(in_len - i);
        if (rem > 1) v |= ((uint32_t)(unsigned char)in[i + 1]) << 8;
        if (rem > 2) v |= ((uint32_t)(unsigned char)in[i + 2]);
        if (o + 4 >= out_cap) return -1;
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        if (rem > 1) out[o++] = tbl[(v >> 6) & 63];
        if (rem > 2) out[o++] = tbl[v & 63];
    }
    if (o >= out_cap) return -1;
    out[o] = 0;
    return (int)o;
}

static int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}

static int b64url_decode(const char *in, size_t in_len, char *out, size_t out_cap) {
    while (in_len > 0 && in[in_len - 1] == '=') in_len--;
    size_t o = 0;
    uint32_t val = 0;
    int bits = -8;
    for (size_t i = 0; i < in_len; i++) {
        int d = b64_val((unsigned char)in[i]);
        if (d < 0) return -1;
        val = (val << 6) | (uint32_t)d;
        bits += 6;
        if (bits >= 0) {
            if (o + 1 >= out_cap) return -1;
            out[o++] = (char)((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    if (o >= out_cap) return -1;
    out[o] = 0;
    return (int)o;
}

// ---------------------------------------------------------------------------
// Proxy state
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t key[16];
    bool has_iv;
    uint8_t iv[16];
} aes_key_entry;

struct hls_proxy {
    hp_sock_t listen_fd;
    uint16_t port;
    bool stop;

    char headers[HP_MAX_HEADERS][HP_MAX_HEADER_LEN];
    int header_count;

    pthread_t accept_thread;
    pthread_t workers[HP_NUM_WORKERS];
    bool accept_started;
    int workers_started;

    pthread_mutex_t q_lock;
    pthread_cond_t q_cond;
    hp_sock_t queue[HP_QUEUE_CAP];
    int q_head;
    int q_tail;
    int q_count;

    pthread_mutex_t key_lock;
    aes_key_entry keys[HP_MAX_KEYS];
    int key_count;
};

static bool is_local_url(const char *url) {
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    char host[256] = {0};
    size_t i = 0;
    if (*p == '[') {
        p++;
        while (*p && *p != ']' && i + 1 < sizeof(host)) host[i++] = (char)tolower((unsigned char)*p++);
    } else {
        while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':' && i + 1 < sizeof(host))
            host[i++] = (char)tolower((unsigned char)*p++);
    }
    host[i] = 0;
    if (!host[0]) return false;
    if (strcmp(host, "localhost") == 0 || strcmp(host, "::1") == 0) return true;
    int o[4] = {0};
    char tail = 0;
    if (sscanf(host, "%d.%d.%d.%d%c", &o[0], &o[1], &o[2], &o[3], &tail) == 4) {
        if (o[0] == 127 || o[0] == 10 || (o[0] == 192 && o[1] == 168) ||
            (o[0] == 172 && o[1] >= 16 && o[1] <= 31))
            return true;
    }
    return false;
}

static bool url_ends_m3u8(const char *url) {
    const char *q = strpbrk(url, "?#");
    size_t len = q ? (size_t)(q - url) : strlen(url);
    return len >= 5 && hp_strcasecmp_n(url + len - 5, ".m3u8", 5) == 0;
}

// Resolve `rel` against `base` (supports full http(s), //, /path, ?query, relative).
static void resolve_url(const char *base, const char *rel, char *out, size_t cap) {
    if (!rel || !rel[0] || !cap) {
        if (cap) out[0] = 0;
        return;
    }
    if (hp_strcasecmp_n(rel, "http://", 7) == 0 ||
        hp_strcasecmp_n(rel, "https://", 8) == 0) {
        snprintf(out, cap, "%s", rel);
        return;
    }
    const char *sc = strstr(base, "://");
    if (!sc) {
        snprintf(out, cap, "%s", rel);
        return;
    }
    size_t scheme_len = (size_t)(sc - base);
    if (rel[0] == '/' && rel[1] == '/') {
        snprintf(out, cap, "%.*s:%s", (int)scheme_len, base, rel);
        return;
    }
    const char *auth_start = sc + 3;
    const char *path_start = strpbrk(auth_start, "/?#");
    size_t origin_len = path_start ? (size_t)(path_start - base) : strlen(base);

    if (rel[0] == '/') {
        snprintf(out, cap, "%.*s%s", (int)origin_len, base, rel);
        return;
    }
    if (rel[0] == '?') {
        const char *q = strpbrk(base, "?#");
        size_t base_no_q = q ? (size_t)(q - base) : strlen(base);
        snprintf(out, cap, "%.*s%s", (int)base_no_q, base, rel);
        return;
    }

    // Directory-relative: strip filename and query from base path.
    const char *q = strpbrk(base, "?#");
    size_t path_end = q ? (size_t)(q - base) : strlen(base);
    size_t dir_end = origin_len;
    for (size_t i = path_end; i > origin_len; i--) {
        if (base[i - 1] == '/') {
            dir_end = i;
            break;
        }
    }
    if (dir_end == origin_len) {
        snprintf(out, cap, "%.*s/%s", (int)origin_len, base, rel);
    } else {
        snprintf(out, cap, "%.*s%s", (int)dir_end, base, rel);
    }
}

// ---------------------------------------------------------------------------
// Upstream transfer context
// ---------------------------------------------------------------------------

typedef enum {
    MODE_DECIDING = 0,
    MODE_STREAMING,
    MODE_BUFFER_M3U8,
    MODE_BUFFER_AES,
} resp_mode;

typedef struct {
    hls_proxy *proxy;
    hp_sock_t client;
    CURL *curl;
    bool is_head;
    bool url_m3u8;
    bool is_aes;
    resp_mode mode;

    long http_status;
    char content_type[128];
    char content_range[128];
    int64_t content_length;
    bool ct_m3u8;
    bool headers_sent;
    bool send_failed;

    char *buf;
    size_t buf_len;
    size_t buf_cap;
    size_t max_buf;

    char peek[32];
    size_t peek_len;
} xfer_ctx;

static int xfer_progress_cb(void *ud, curl_off_t dt, curl_off_t dn,
                            curl_off_t ut, curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    xfer_ctx *x = (xfer_ctx *)ud;
    return x->proxy->stop ? 1 : 0;
}

static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static size_t xfer_header_cb(char *p, size_t sz, size_t nm, void *ud) {
    xfer_ctx *x = (xfer_ctx *)ud;
    size_t n = sz * nm;
    if (n >= 5 && hp_strcasecmp_n(p, "HTTP/", 5) == 0) {
        // New status line (e.g. after redirect): reset parsed headers.
        const char *sp = memchr(p, ' ', n);
        x->http_status = sp ? strtol(sp + 1, NULL, 10) : 200;
        x->content_type[0] = 0;
        x->content_range[0] = 0;
        x->content_length = -1;
        x->ct_m3u8 = false;
        return n;
    }
    if (n > 13 && hp_strcasecmp_n(p, "content-type:", 13) == 0) {
        const char *v = p + 13;
        size_t vl = n - 13;
        while (vl > 0 && (*v == ' ' || *v == '\t')) { v++; vl--; }
        size_t cp = vl < sizeof(x->content_type) - 1 ? vl : sizeof(x->content_type) - 1;
        memcpy(x->content_type, v, cp);
        x->content_type[cp] = 0;
        trim_crlf(x->content_type);
        if (hp_strcasestr(x->content_type, "mpegurl")) x->ct_m3u8 = true;
    } else if (n > 14 && hp_strcasecmp_n(p, "content-range:", 14) == 0) {
        const char *v = p + 14;
        size_t vl = n - 14;
        while (vl > 0 && (*v == ' ' || *v == '\t')) { v++; vl--; }
        size_t cp = vl < sizeof(x->content_range) - 1 ? vl : sizeof(x->content_range) - 1;
        memcpy(x->content_range, v, cp);
        x->content_range[cp] = 0;
        trim_crlf(x->content_range);
    } else if (n > 15 && hp_strcasecmp_n(p, "content-length:", 15) == 0) {
        const char *v = p + 15;
        while (v < p + n && (*v == ' ' || *v == '\t')) v++;
        if (v < p + n && isdigit((unsigned char)*v))
            x->content_length = strtoll(v, NULL, 10);
    }
    return n;
}

static int send_stream_headers(xfer_ctx *x) {
    if (x->headers_sent) return 0;
    x->headers_sent = true;
    char hdr[1024];
    int pos = 0;
    long code = x->http_status > 0 ? x->http_status : 200;
    const char *reason = (code == 206) ? "Partial Content"
                       : (code >= 400) ? "Error" : "OK";
    pos += snprintf(hdr + pos, sizeof(hdr) - (size_t)pos,
                    "HTTP/1.1 %ld %s\r\n", code, reason);
    if (x->content_type[0])
        pos += snprintf(hdr + pos, sizeof(hdr) - (size_t)pos,
                        "Content-Type: %s\r\n", x->content_type);
    if (x->content_range[0])
        pos += snprintf(hdr + pos, sizeof(hdr) - (size_t)pos,
                        "Content-Range: %s\r\n", x->content_range);
    if (x->content_length >= 0)
        pos += snprintf(hdr + pos, sizeof(hdr) - (size_t)pos,
                        "Content-Length: %lld\r\n", (long long)x->content_length);
    pos += snprintf(hdr + pos, sizeof(hdr) - (size_t)pos,
                    "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n");
    if (hp_send_all(x->client, hdr, (size_t)pos) != 0) {
        x->send_failed = true;
        return -1;
    }
    return 0;
}

static int append_buf(xfer_ctx *x, const char *data, size_t len) {
    if (x->buf_len + len > x->max_buf) return -1;
    if (x->buf_len + len + 1 > x->buf_cap) {
        size_t ncap = x->buf_cap ? x->buf_cap * 2 : 16384;
        while (ncap < x->buf_len + len + 1) ncap *= 2;
        if (ncap > x->max_buf + 1) ncap = x->max_buf + 1;
        char *nb = (char *)realloc(x->buf, ncap);
        if (!nb) return -1;
        x->buf = nb;
        x->buf_cap = ncap;
    }
    memcpy(x->buf + x->buf_len, data, len);
    x->buf_len += len;
    x->buf[x->buf_len] = 0;
    return 0;
}

static bool peek_starts_extm3u(const char *p, size_t n) {
    size_t i = 0;
    if (n >= 3 && (unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        i = 3;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    return (n - i) >= 7 && memcmp(p + i, "#EXTM3U", 7) == 0;
}

static size_t xfer_write_cb(char *p, size_t sz, size_t nm, void *ud) {
    xfer_ctx *x = (xfer_ctx *)ud;
    size_t n = sz * nm;
    if (x->proxy->stop) return 0;
    if (x->is_head) return n;

    if (x->mode == MODE_DECIDING) {
        if (x->http_status >= 400) {
            x->mode = MODE_STREAMING;
        } else if (x->is_aes) {
            x->mode = MODE_BUFFER_AES;
            x->max_buf = HP_MAX_SEG_BYTES;
        } else if (x->url_m3u8 || x->ct_m3u8) {
            x->mode = MODE_BUFFER_M3U8;
            x->max_buf = HP_MAX_M3U8_BYTES;
        } else {
            size_t take = n;
            if (x->peek_len + take > sizeof(x->peek)) take = sizeof(x->peek) - x->peek_len;
            memcpy(x->peek + x->peek_len, p, take);
            x->peek_len += take;
            if (x->peek_len < 16) {
                if (peek_starts_extm3u(x->peek, x->peek_len)) {
                    x->mode = MODE_BUFFER_M3U8;
                    x->max_buf = HP_MAX_M3U8_BYTES;
                    if (append_buf(x, x->peek, x->peek_len) != 0) return 0;
                    if (n > take && append_buf(x, p + take, n - take) != 0) return 0;
                    return n;
                }
                return n; // Wait for more peek bytes or EOF.
            }
            if (peek_starts_extm3u(x->peek, x->peek_len)) {
                x->mode = MODE_BUFFER_M3U8;
                x->max_buf = HP_MAX_M3U8_BYTES;
                if (append_buf(x, x->peek, x->peek_len) != 0) return 0;
                if (n > take && append_buf(x, p + take, n - take) != 0) return 0;
                return n;
            }
            x->mode = MODE_STREAMING;
            if (send_stream_headers(x) != 0) return 0;
            if (hp_send_all(x->client, x->peek, x->peek_len) != 0) {
                x->send_failed = true;
                return 0;
            }
            if (n > take && hp_send_all(x->client, p + take, n - take) != 0) {
                x->send_failed = true;
                return 0;
            }
            return n;
        }
    }

    if (x->mode == MODE_BUFFER_M3U8 || x->mode == MODE_BUFFER_AES) {
        if (append_buf(x, p, n) != 0) return 0;
        return n;
    }

    if (send_stream_headers(x) != 0) return 0;
    if (hp_send_all(x->client, p, n) != 0) {
        x->send_failed = true;
        return 0;
    }
    return n;
}

static void setup_curl(hls_proxy *proxy, CURL *c, const char *url, const char *range_hdr) {
    curl_easy_reset(c);
    bool has_ua = false;
    for (int i = 0; i < proxy->header_count; i++) {
        if (hp_strcasecmp_n(proxy->headers[i], "user-agent:", 11) == 0) {
            has_ua = true;
            break;
        }
    }
    if (!has_ua) curl_easy_setopt(c, CURLOPT_USERAGENT, "NX-torrent-player");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 25L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    if (g_ca[0]) curl_easy_setopt(c, CURLOPT_CAINFO, g_ca);
    if (g_proxy[0] && !is_local_url(url)) curl_easy_setopt(c, CURLOPT_PROXY, g_proxy);
    if (range_hdr && range_hdr[0]) {
        // Strip "Range: " prefix if caller passed raw value; CURLOPT_RANGE takes "a-b"
        if (hp_strcasecmp_n(range_hdr, "bytes=", 6) == 0)
            curl_easy_setopt(c, CURLOPT_RANGE, range_hdr + 6);
        else
            curl_easy_setopt(c, CURLOPT_RANGE, range_hdr);
    }
}

static struct curl_slist *build_curl_headers(const hls_proxy *proxy) {
    struct curl_slist *hdrs = NULL;
    for (int i = 0; i < proxy->header_count; i++)
        hdrs = curl_slist_append(hdrs, proxy->headers[i]);
    return hdrs;
}

#ifdef HLS_PROXY_ENABLE_AES
typedef struct {
    hls_proxy *proxy;
    uint8_t buf[64];
    size_t len;
} key_fetch_ctx;

static size_t key_write_cb(char *p, size_t sz, size_t nm, void *ud) {
    key_fetch_ctx *k = (key_fetch_ctx *)ud;
    size_t n = sz * nm;
    if (k->proxy->stop) return 0;
    if (k->len + n > sizeof(k->buf)) return 0;
    memcpy(k->buf + k->len, p, n);
    k->len += n;
    return n;
}

static int key_progress_cb(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    return ((key_fetch_ctx *)ud)->proxy->stop ? 1 : 0;
}

static int fetch_and_store_aes_key(hls_proxy *proxy, const char *key_url,
                                   bool has_iv, const uint8_t iv[16]) {
    CURL *c = curl_easy_init();
    if (!c) return -1;
    key_fetch_ctx kctx;
    memset(&kctx, 0, sizeof(kctx));
    kctx.proxy = proxy;

    setup_curl(proxy, c, key_url, NULL);
    struct curl_slist *hdrs = build_curl_headers(proxy);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, key_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &kctx);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, key_progress_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &kctx);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK || code >= 400 || kctx.len != 16) return -1;

    pthread_mutex_lock(&proxy->key_lock);
    int idx = -1;
    if (proxy->key_count < HP_MAX_KEYS) {
        idx = proxy->key_count++;
    } else {
        // Evict slot 0 by shifting? Keep simple ring wrap.
        idx = (int)(proxy->key_count % HP_MAX_KEYS);
        proxy->key_count++;
    }
    memcpy(proxy->keys[idx].key, kctx.buf, 16);
    proxy->keys[idx].has_iv = has_iv;
    if (has_iv) memcpy(proxy->keys[idx].iv, iv, 16);
    pthread_mutex_unlock(&proxy->key_lock);
    return idx;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_iv(const char *s, uint8_t iv[16]) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    size_t len = strlen(s);
    if (len == 0 || len > 32) return false;
    memset(iv, 0, 16);
    // Right-align hex digits into 16 bytes
    for (size_t i = 0; i < len; i++) {
        int nib = hex_nibble(s[len - 1 - i]);
        if (nib < 0) return false;
        size_t byte_idx = 15 - (i / 2);
        if (i % 2 == 0) iv[byte_idx] |= (uint8_t)nib;
        else iv[byte_idx] |= (uint8_t)(nib << 4);
    }
    return true;
}
#endif // HLS_PROXY_ENABLE_AES

// ---------------------------------------------------------------------------
// M3U8 rewriter
// ---------------------------------------------------------------------------

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} str_builder;

static int sb_append(str_builder *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 16384;
        while (ncap < sb->len + n + 1) ncap *= 2;
        if (ncap > HP_MAX_M3U8_BYTES * 4) return -1;
        char *nd = (char *)realloc(sb->data, ncap);
        if (!nd) return -1;
        sb->data = nd;
        sb->cap = ncap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = 0;
    return 0;
}

static int sb_str(str_builder *sb, const char *s) {
    return sb_append(sb, s, strlen(s));
}

// Extract attribute value from `TAG:...,KEY="VAL"` or `KEY=VAL`.
static bool extract_attr(const char *line, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = line;
    while (*p) {
        if (hp_strcasecmp_n(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            if (*v == '"') {
                v++;
                const char *e = strchr(v, '"');
                if (!e) return false;
                size_t l = (size_t)(e - v);
                if (l >= cap) l = cap - 1;
                memcpy(out, v, l);
                out[l] = 0;
                return true;
            } else {
                const char *e = strpbrk(v, ", \r\n");
                size_t l = e ? (size_t)(e - v) : strlen(v);
                if (l >= cap) l = cap - 1;
                memcpy(out, v, l);
                out[l] = 0;
                return true;
            }
        }
        p++;
    }
    return false;
}

static int make_proxy_url_with_aes(const hls_proxy *p, const char *target_url,
                                   int key_idx, const uint8_t *iv,
                                   char *out, size_t out_len) {
    if (hls_proxy_make_url(p, target_url, out, out_len) != 0) return -1;
    if (key_idx >= 0 && iv) {
        size_t cur = strlen(out);
        char hex[33];
        for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", iv[i]);
        hex[32] = 0;
        int added = snprintf(out + cur, out_len - cur, "&k=%d&iv=%s", key_idx, hex);
        if (added < 0 || cur + (size_t)added >= out_len) return -1;
    }
    return 0;
}

static int rewrite_tag_uri(hls_proxy *proxy, const char *base_url, const char *line,
                           str_builder *sb) {
    const char *u = hp_strcasestr(line, "URI=\"");
    if (!u) return sb_str(sb, line) || sb_str(sb, "\n");
    const char *val = u + 5;
    const char *end = strchr(val, '"');
    if (!end) return sb_str(sb, line) || sb_str(sb, "\n");

    char rel[2048] = {0};
    size_t rlen = (size_t)(end - val);
    if (rlen >= sizeof(rel)) rlen = sizeof(rel) - 1;
    memcpy(rel, val, rlen);

    char abs_url[2048] = {0};
    resolve_url(base_url, rel, abs_url, sizeof(abs_url));
    char proxied[4096] = {0};
    if (hls_proxy_make_url(proxy, abs_url, proxied, sizeof(proxied)) != 0)
        return sb_str(sb, line) || sb_str(sb, "\n");

    if (sb_append(sb, line, (size_t)(val - line)) != 0) return -1;
    if (sb_str(sb, proxied) != 0) return -1;
    if (sb_str(sb, end) != 0) return -1;
    return sb_str(sb, "\n");
}

static char *rewrite_m3u8(hls_proxy *proxy, const char *base_url,
                          const char *src, size_t src_len, size_t *out_len) {
    str_builder sb = {0};
    uint64_t media_seq = 0;
    int cur_key_idx = -1;
    bool cur_has_iv = false;
    uint8_t cur_iv[16] = {0};

    size_t pos = 0;
    if (src_len >= 3 && (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        pos = 3;

    while (pos < src_len) {
        size_t eol = pos;
        while (eol < src_len && src[eol] != '\n') eol++;
        size_t line_len = eol - pos;
        if (line_len > 0 && src[pos + line_len - 1] == '\r') line_len--;

        char *line = (char *)malloc(line_len + 1);
        if (!line) { free(sb.data); return NULL; }
        memcpy(line, src + pos, line_len);
        line[line_len] = 0;
        pos = (eol < src_len) ? eol + 1 : src_len;

        // Trim leading spaces
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (trimmed[0] == 0) {
            free(line);
            continue;
        }

        if (trimmed[0] == '#') {
            if (hp_strcasecmp_n(trimmed, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
                media_seq = strtoull(trimmed + 22, NULL, 10);
                if (sb_str(&sb, trimmed) != 0 || sb_str(&sb, "\n") != 0) { free(line); free(sb.data); return NULL; }
            } else if (hp_strcasecmp_n(trimmed, "#EXT-X-KEY:", 11) == 0) {
                char method[64] = {0};
                if (extract_attr(trimmed + 11, "METHOD", method, sizeof(method)) &&
                    hp_strcasecmp_n(method, "NONE", 4) == 0) {
                    cur_key_idx = -1;
                    cur_has_iv = false;
                }
#ifdef HLS_PROXY_ENABLE_AES
                else if (hp_strcasecmp_n(method, "AES-128", 7) == 0) {
                    char key_uri[2048] = {0}, iv_str[128] = {0};
                    if (extract_attr(trimmed + 11, "URI", key_uri, sizeof(key_uri))) {
                        char abs_key[2048] = {0};
                        resolve_url(base_url, key_uri, abs_key, sizeof(abs_key));
                        uint8_t parsed_iv[16] = {0};
                        bool has_iv = extract_attr(trimmed + 11, "IV", iv_str, sizeof(iv_str)) &&
                                      parse_hex_iv(iv_str, parsed_iv);
                        int kidx = fetch_and_store_aes_key(proxy, abs_key, has_iv, parsed_iv);
                        if (kidx >= 0) {
                            cur_key_idx = kidx;
                            cur_has_iv = has_iv;
                            if (has_iv) memcpy(cur_iv, parsed_iv, 16);
                            free(line);
                            continue; // Strip #EXT-X-KEY since proxy decrypts segments
                        }
                    }
                    if (rewrite_tag_uri(proxy, base_url, trimmed, &sb) != 0) { free(line); free(sb.data); return NULL; }
                }
#endif
                else {
                    if (rewrite_tag_uri(proxy, base_url, trimmed, &sb) != 0) { free(line); free(sb.data); return NULL; }
                }
            } else if (hp_strcasecmp_n(trimmed, "#EXT-X-MEDIA:", 13) == 0 ||
                       hp_strcasecmp_n(trimmed, "#EXT-X-MAP:", 11) == 0 ||
                       hp_strcasecmp_n(trimmed, "#EXT-X-I-FRAME-STREAM-INF:", 26) == 0) {
                if (rewrite_tag_uri(proxy, base_url, trimmed, &sb) != 0) { free(line); free(sb.data); return NULL; }
            } else {
                if (sb_str(&sb, trimmed) != 0 || sb_str(&sb, "\n") != 0) { free(line); free(sb.data); return NULL; }
            }
        } else {
            char abs_url[2048] = {0};
            resolve_url(base_url, trimmed, abs_url, sizeof(abs_url));
            uint8_t seq_iv[16] = {0};
            const uint8_t *use_iv = NULL;
            if (cur_key_idx >= 0) {
                if (cur_has_iv) {
                    use_iv = cur_iv;
                } else {
                    uint64_t s = media_seq;
                    for (int b = 15; b >= 8; b--) {
                        seq_iv[b] = (uint8_t)(s & 0xFF);
                        s >>= 8;
                    }
                    use_iv = seq_iv;
                }
            }
            char proxied[4096] = {0};
            if (make_proxy_url_with_aes(proxy, abs_url, cur_key_idx, use_iv,
                                        proxied, sizeof(proxied)) == 0) {
                if (sb_str(&sb, proxied) != 0 || sb_str(&sb, "\n") != 0) { free(line); free(sb.data); return NULL; }
            } else {
                if (sb_str(&sb, abs_url) != 0 || sb_str(&sb, "\n") != 0) { free(line); free(sb.data); return NULL; }
            }
            media_seq++;
        }
        free(line);
    }

    *out_len = sb.len;
    return sb.data;
}

// ---------------------------------------------------------------------------
// Request handling
// ---------------------------------------------------------------------------

static void send_error_resp(hp_sock_t s, int code, const char *msg) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                     code, msg);
    hp_send_all(s, buf, (size_t)n);
}

static bool query_param(const char *query, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *amp = strchr(v, '&');
            size_t l = amp ? (size_t)(amp - v) : strlen(v);
            if (l >= cap) l = cap - 1;
            memcpy(out, v, l);
            out[l] = 0;
            return true;
        }
        const char *next = strchr(p, '&');
        p = next ? next + 1 : NULL;
    }
    return false;
}

static void handle_client(hls_proxy *proxy, hp_sock_t client) {
    hp_sock_set_timeout(client, 30);

    char req[8192];
    size_t rlen = 0;
    while (rlen < sizeof(req) - 1) {
#ifdef _WIN32
        int r = recv(client, req + rlen, (int)(sizeof(req) - 1 - rlen), 0);
#else
        ssize_t r = recv(client, req + rlen, sizeof(req) - 1 - rlen, 0);
#endif
        if (r <= 0) break;
        rlen += (size_t)r;
        req[rlen] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (rlen == 0) {
        hp_sock_close(client);
        return;
    }

    char method[16] = {0}, path[4096] = {0};
    if (sscanf(req, "%15s %4095s", method, path) != 2) {
        send_error_resp(client, 400, "Bad Request");
        hp_sock_close(client);
        return;
    }
    bool is_head = (strcmp(method, "HEAD") == 0);
    if (!is_head && strcmp(method, "GET") != 0) {
        send_error_resp(client, 405, "Method Not Allowed");
        hp_sock_close(client);
        return;
    }

    const char *q = strchr(path, '?');
    if (!q || strncmp(path, "/p?", 3) != 0) {
        send_error_resp(client, 404, "Not Found");
        hp_sock_close(client);
        return;
    }

    char u_b64[4096] = {0}, target_url[4096] = {0};
    if (!query_param(q + 1, "u", u_b64, sizeof(u_b64)) ||
        b64url_decode(u_b64, strlen(u_b64), target_url, sizeof(target_url)) <= 0) {
        send_error_resp(client, 400, "Invalid Target");
        hp_sock_close(client);
        return;
    }

    char range_val[128] = {0};
    const char *hdr_end = strstr(req, "\r\n\r\n");
    const char *line = strstr(req, "\r\n");
    while (line && (!hdr_end || line < hdr_end)) {
        line += 2;
        if (hp_strcasecmp_n(line, "range:", 6) == 0) {
            const char *v = line + 6;
            while (*v == ' ' || *v == '\t') v++;
            const char *eol = strstr(v, "\r\n");
            size_t rl = eol ? (size_t)(eol - v) : strlen(v);
            if (rl >= sizeof(range_val)) rl = sizeof(range_val) - 1;
            memcpy(range_val, v, rl);
            range_val[rl] = 0;
            break;
        }
        line = strstr(line, "\r\n");
    }

    bool is_aes = false;
#ifdef HLS_PROXY_ENABLE_AES
    int aes_key_idx = -1;
    uint8_t aes_iv[16] = {0};
    char k_str[32] = {0}, iv_str[64] = {0};
    if (query_param(q + 1, "k", k_str, sizeof(k_str)) &&
        query_param(q + 1, "iv", iv_str, sizeof(iv_str))) {
        aes_key_idx = atoi(k_str);
        if (aes_key_idx >= 0 && aes_key_idx < HP_MAX_KEYS && parse_hex_iv(iv_str, aes_iv)) {
            is_aes = true;
            range_val[0] = 0; // Decrypt full segment cleanly
        }
    }
#endif

    CURL *curl = curl_easy_init();
    if (!curl) {
        send_error_resp(client, 500, "Internal Error");
        hp_sock_close(client);
        return;
    }

    xfer_ctx x;
    memset(&x, 0, sizeof(x));
    x.proxy = proxy;
    x.client = client;
    x.curl = curl;
    x.is_head = is_head;
    x.url_m3u8 = url_ends_m3u8(target_url);
    x.is_aes = is_aes;
    x.content_length = -1;

    setup_curl(proxy, curl, target_url, range_val);
    if (is_head) curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    struct curl_slist *hdrs = build_curl_headers(proxy);
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, xfer_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &x);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, xfer_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &x);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &x);

    CURLcode rc = curl_easy_perform(curl);
    char *eff = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
    char eff_copy[4096];
    snprintf(eff_copy, sizeof(eff_copy), "%s", eff ? eff : target_url);

    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (proxy->stop || x.send_failed) {
        free(x.buf);
        hp_sock_close(client);
        return;
    }

    if (rc != CURLE_OK) {
        if (!x.headers_sent) send_error_resp(client, 502, "Bad Gateway");
        free(x.buf);
        hp_sock_close(client);
        return;
    }

    if (is_head) {
        send_stream_headers(&x);
        free(x.buf);
        hp_sock_close(client);
        return;
    }

    if (x.mode == MODE_DECIDING) {
        // Response was smaller than 16 bytes
        if (peek_starts_extm3u(x.peek, x.peek_len)) {
            x.mode = MODE_BUFFER_M3U8;
            x.max_buf = HP_MAX_M3U8_BYTES;
            append_buf(&x, x.peek, x.peek_len);
        } else {
            if (send_stream_headers(&x) == 0 && x.peek_len > 0)
                hp_send_all(client, x.peek, x.peek_len);
        }
    }

    if (x.mode == MODE_BUFFER_M3U8 && x.buf) {
        size_t out_len = 0;
        char *rewritten = rewrite_m3u8(proxy, eff_copy, x.buf, x.buf_len, &out_len);
        if (!rewritten) {
            send_error_resp(client, 500, "Playlist Rewrite Failed");
        } else {
            char hdr[256];
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/vnd.apple.mpegurl\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              out_len);
            if (hp_send_all(client, hdr, (size_t)hn) == 0)
                hp_send_all(client, rewritten, out_len);
            free(rewritten);
        }
    }
#ifdef HLS_PROXY_ENABLE_AES
    else if (x.mode == MODE_BUFFER_AES && x.buf) {
        if (x.buf_len == 0 || (x.buf_len % 16) != 0) {
            send_error_resp(client, 502, "Invalid AES Segment");
        } else {
            uint8_t key[16];
            pthread_mutex_lock(&proxy->key_lock);
            memcpy(key, proxy->keys[aes_key_idx].key, 16);
            pthread_mutex_unlock(&proxy->key_lock);

            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_dec(&aes, key, 128);
            uint8_t iv_mut[16];
            memcpy(iv_mut, aes_iv, 16);
            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, x.buf_len, iv_mut,
                                  (const uint8_t *)x.buf, (uint8_t *)x.buf);
            mbedtls_aes_free(&aes);

            size_t plain_len = x.buf_len;
            uint8_t pad = (uint8_t)x.buf[x.buf_len - 1];
            if (pad >= 1 && pad <= 16 && (size_t)pad <= plain_len) {
                bool ok = true;
                for (size_t i = 0; i < pad; i++) {
                    if ((uint8_t)x.buf[plain_len - 1 - i] != pad) { ok = false; break; }
                }
                if (ok) plain_len -= pad;
            }
            char hdr[256];
            int hn = snprintf(hdr, sizeof(hdr),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              x.content_type[0] ? x.content_type : "video/mp2t",
                              plain_len);
            if (hp_send_all(client, hdr, (size_t)hn) == 0 && plain_len > 0)
                hp_send_all(client, x.buf, plain_len);
        }
    }
#endif

    free(x.buf);
    hp_sock_close(client);
}

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------

static void *worker_main(void *arg) {
    tsnx_thread_pin_self(TSNX_CORE_NET, TSNX_PRIO_NET_BG);
    hls_proxy *proxy = (hls_proxy *)arg;
    for (;;) {
        pthread_mutex_lock(&proxy->q_lock);
        while (proxy->q_count == 0 && !proxy->stop)
            pthread_cond_wait(&proxy->q_cond, &proxy->q_lock);
        if (proxy->stop && proxy->q_count == 0) {
            pthread_mutex_unlock(&proxy->q_lock);
            break;
        }
        hp_sock_t client = proxy->queue[proxy->q_head];
        proxy->q_head = (proxy->q_head + 1) % HP_QUEUE_CAP;
        proxy->q_count--;
        pthread_mutex_unlock(&proxy->q_lock);

        if (proxy->stop) {
            hp_sock_close(client);
            continue;
        }
        handle_client(proxy, client);
    }
    return NULL;
}

static void *accept_main(void *arg) {
    tsnx_thread_pin_self(TSNX_CORE_NET, TSNX_PRIO_NET_BG);
    hls_proxy *proxy = (hls_proxy *)arg;
    while (!__atomic_load_n(&proxy->stop, __ATOMIC_ACQUIRE)) {
#ifdef _WIN32
        WSAPOLLFD pfd = { proxy->listen_fd, POLLIN, 0 };
        int pr = WSAPoll(&pfd, 1, 200);
#else
        struct pollfd pfd = { proxy->listen_fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, 200);
#endif
        if (__atomic_load_n(&proxy->stop, __ATOMIC_ACQUIRE)) break;
        if (pr <= 0 || !(pfd.revents & (POLLIN | POLLERR | POLLHUP))) continue;
        hp_sock_t c = accept(proxy->listen_fd, NULL, NULL);
        if (c == HP_BAD_SOCK) {
            if (__atomic_load_n(&proxy->stop, __ATOMIC_ACQUIRE)) break;
            continue;
        }
        pthread_mutex_lock(&proxy->q_lock);
        if (__atomic_load_n(&proxy->stop, __ATOMIC_ACQUIRE) ||
            proxy->q_count >= HP_QUEUE_CAP) {
            pthread_mutex_unlock(&proxy->q_lock);
            hp_sock_close(c);
            continue;
        }
        proxy->queue[proxy->q_tail] = c;
        proxy->q_tail = (proxy->q_tail + 1) % HP_QUEUE_CAP;
        proxy->q_count++;
        pthread_cond_signal(&proxy->q_cond);
        pthread_mutex_unlock(&proxy->q_lock);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

hls_proxy *hls_proxy_start(const char *const *headers, int header_count,
                           char *err, size_t errlen) {
    hp_sock_init();
    hls_proxy *p = (hls_proxy *)calloc(1, sizeof(*p));
    if (!p) {
        if (err && errlen) snprintf(err, errlen, "out of memory");
        return NULL;
    }
    p->listen_fd = HP_BAD_SOCK;
    pthread_mutex_init(&p->q_lock, NULL);
    pthread_cond_init(&p->q_cond, NULL);
    pthread_mutex_init(&p->key_lock, NULL);

    if (headers && header_count > 0) {
        for (int i = 0; i < header_count && p->header_count < HP_MAX_HEADERS; i++) {
            if (!headers[i] || !headers[i][0]) continue;
            snprintf(p->headers[p->header_count], HP_MAX_HEADER_LEN, "%s", headers[i]);
            p->header_count++;
        }
    }

    hp_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == HP_BAD_SOCK) {
        if (err && errlen) snprintf(err, errlen, "socket() failed");
        hls_proxy_stop(p);
        return NULL;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 8) != 0) {
        if (err && errlen) snprintf(err, errlen, "bind/listen failed");
        hp_sock_close(fd);
        hls_proxy_stop(p);
        return NULL;
    }

    socklen_t sl = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &sl) != 0) {
        if (err && errlen) snprintf(err, errlen, "getsockname failed");
        hp_sock_close(fd);
        hls_proxy_stop(p);
        return NULL;
    }
    p->listen_fd = fd;
    p->port = ntohs(sa.sin_port);

    for (int i = 0; i < HP_NUM_WORKERS; i++) {
        if (pthread_create(&p->workers[i], NULL, worker_main, p) != 0) {
            if (err && errlen) snprintf(err, errlen, "worker thread failed");
            hls_proxy_stop(p);
            return NULL;
        }
        p->workers_started++;
    }
    if (pthread_create(&p->accept_thread, NULL, accept_main, p) != 0) {
        if (err && errlen) snprintf(err, errlen, "accept thread failed");
        hls_proxy_stop(p);
        return NULL;
    }
    p->accept_started = true;
    return p;
}

uint16_t hls_proxy_port(const hls_proxy *p) { return p ? p->port : 0; }

int hls_proxy_make_url(const hls_proxy *p, const char *target_url,
                       char *out, size_t out_len) {
    if (!p || !p->port || !target_url || !target_url[0] || !out || out_len < 32)
        return -1;
    char b64[4096];
    if (b64url_encode(target_url, strlen(target_url), b64, sizeof(b64)) <= 0)
        return -1;
    int n = snprintf(out, out_len, "http://127.0.0.1:%u/p?u=%s",
                     (unsigned)p->port, b64);
    return (n > 0 && (size_t)n < out_len) ? 0 : -1;
}

void hls_proxy_abort(hls_proxy *p) {
    if (!p) return;
    __atomic_store_n(&p->stop, true, __ATOMIC_RELEASE);
    pthread_mutex_lock(&p->q_lock);
    pthread_cond_broadcast(&p->q_cond);
    pthread_mutex_unlock(&p->q_lock);
}

void hls_proxy_stop(hls_proxy *p) {
    if (!p) return;
    hls_proxy_abort(p);
    if (p->listen_fd != HP_BAD_SOCK) {
        hp_sock_shutdown(p->listen_fd);
        hp_sock_close(p->listen_fd);
        p->listen_fd = HP_BAD_SOCK;
    }
    if (p->accept_started) pthread_join(p->accept_thread, NULL);
    for (int i = 0; i < p->workers_started; i++) pthread_join(p->workers[i], NULL);

    while (p->q_count > 0) {
        hp_sock_close(p->queue[p->q_head]);
        p->q_head = (p->q_head + 1) % HP_QUEUE_CAP;
        p->q_count--;
    }
    pthread_mutex_destroy(&p->q_lock);
    pthread_cond_destroy(&p->q_cond);
    pthread_mutex_destroy(&p->key_lock);
    free(p);
}
