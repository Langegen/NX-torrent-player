#include "httpsrc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <curl/curl.h>

// 512 KiB initial probe / post-seek chunk ramps smoothly to 4 MiB for
// sustained sequential playback, cutting first-frame latency without extra
// round-trips on large streams.
#define PROBE_CHUNK (512 * 1024)
#define CHUNK (4 * 1024 * 1024)
#define RETRIES 3

static char g_ca[512];
static char g_proxy[512];
static char g_headers[16][512];
static int g_header_count = 0;
static volatile int64_t g_bytes_total;

void httpsrc_set_ca(const char *p) { snprintf(g_ca, sizeof(g_ca), "%s", p ? p : ""); }
void httpsrc_set_proxy(const char *p) { snprintf(g_proxy, sizeof(g_proxy), "%s", p ? p : ""); }
void httpsrc_set_headers(const char *const *headers, int count) {
    g_header_count = 0;
    if (!headers || count <= 0) return;
    for (int i = 0; i < count && g_header_count < 16; i++) {
        if (!headers[i] || !headers[i][0]) continue;
        snprintf(g_headers[g_header_count], sizeof(g_headers[0]), "%s", headers[i]);
        g_header_count++;
    }
}
int64_t httpsrc_bytes_total(void) { return __atomic_load_n(&g_bytes_total, __ATOMIC_RELAXED); }
void httpsrc_add_bytes(int64_t n) {
    if (n > 0) __atomic_fetch_add(&g_bytes_total, n, __ATOMIC_RELAXED);
}

struct httpsrc {
    CURL *curl;
    char *url;           // original URL
    char *direct;        // where the redirects led (NULL = same as url)
    int64_t size;
    volatile bool cancelled;
    const volatile bool *abort;   // the owner's flag (httpsrc_open_abortable)

    uint8_t *buf;        // the buffered chunk
    int64_t buf_pos;     // its offset in the resource
    int64_t buf_len;
    int64_t expected_pos;// end of previous buffered read (detects sequential reads)
    int seq_reads;       // adaptive ramp 0..3 -> 512 KiB .. 4 MiB

    // State of the transfer in progress.
    int64_t want;        // bytes asked for
    int64_t got;
    int64_t content_range_total;  // from "Content-Range: bytes a-b/TOTAL", -1
    bool ct_m3u8;
    char err[160];
};

static bool is_cancelled(const httpsrc *s);

static bool is_local_url(const char *url) {
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    char host[256] = {0};
    size_t i = 0;
    if (*p == '[') {
        p++;
        while (*p && *p != ']' && i + 1 < sizeof(host))
            host[i++] = (char)tolower((unsigned char)*p++);
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

static bool hs_has_mpegurl(const char *p, size_t n) {
    static const char needle[] = "mpegurl";
    size_t nlen = sizeof(needle) - 1;
    if (n < nlen) return false;
    for (size_t i = 0; i + nlen <= n; i++) {
        if (strncasecmp(p + i, needle, nlen) == 0) return true;
    }
    return false;
}

static bool starts_with_extm3u(const uint8_t *p, int64_t len) {
    int64_t i = 0;
    if (len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) i = 3;
    while (i < len && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) i++;
    return (len - i) >= 7 && memcmp(p + i, "#EXTM3U", 7) == 0;
}

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg);
}

static size_t write_cb(char *p, size_t sz, size_t nm, void *ud) {
    httpsrc *s = ud;
    if (is_cancelled(s)) return 0;
    size_t n = sz * nm;
    size_t room = (size_t)(s->want - s->got);
    // A server that ignores Range sends the whole file: keep what we asked
    // for and stop the transfer there (curl reports a write error, which the
    // caller recognises by got == want).
    size_t take = n < room ? n : room;
    memcpy(s->buf + s->got, p, take);
    s->got += (int64_t)take;
    __atomic_fetch_add(&g_bytes_total, (int64_t)take, __ATOMIC_RELAXED);
    return take == n ? n : 0;
}

static size_t header_cb(char *p, size_t sz, size_t nm, void *ud) {
    httpsrc *s = ud;
    size_t n = sz * nm;
    if (n >= 5 && strncasecmp(p, "HTTP/", 5) == 0) {
        s->content_range_total = -1;
        s->ct_m3u8 = false;
        return n;
    }
    // "Content-Range: bytes 0-0/123456" (case-insensitive name).
    static const char name[] = "content-range:";
    static const char ct_name[] = "content-type:";
    if (n > sizeof(name) && strncasecmp(p, name, sizeof(name) - 1) == 0) {
        const char *slash = memchr(p, '/', n);
        if (slash && slash + 1 < p + n && isdigit((unsigned char)slash[1]))
            s->content_range_total = strtoll(slash + 1, NULL, 10);
    } else if (n > sizeof(ct_name) - 1 && strncasecmp(p, ct_name, sizeof(ct_name) - 1) == 0) {
        if (hs_has_mpegurl(p + sizeof(ct_name) - 1, n - (sizeof(ct_name) - 1)))
            s->ct_m3u8 = true;
    }
    return n;
}

static bool is_cancelled(const httpsrc *s) {
    return s->cancelled || (s->abort && *s->abort);
}

static int progress_cb(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut,
                       curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    return is_cancelled((httpsrc *)ud) ? 1 : 0;
}

static void setup(httpsrc *s, const char *url) {
    CURL *c = s->curl;
    curl_easy_reset(c);
    bool has_ua = false;
    for (int i = 0; i < g_header_count; i++) {
        if (strncasecmp(g_headers[i], "user-agent:", 11) == 0) {
            has_ua = true;
            break;
        }
    }
    if (!has_ua)
        curl_easy_setopt(c, CURLOPT_USERAGENT, "NX-torrent-player");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    // A stalled server: give up after 20 s under 1 KB/s rather than hanging
    // the demuxer forever (the read is retried).
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, s);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, s);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, s);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    if (g_ca[0]) curl_easy_setopt(c, CURLOPT_CAINFO, g_ca);
    if (g_proxy[0] && !is_local_url(url)) curl_easy_setopt(c, CURLOPT_PROXY, g_proxy);
}

// One GET of [pos, pos+len) into s->buf (if len > 0 and pos >= 0; pos < 0 means
// unranged GET up to CHUNK bytes). Returns HTTP status, or -1 with s->err set.
static long fetch(httpsrc *s, const char *url, int64_t pos, int64_t len) {
    setup(s, url);
    char range[64];
    struct curl_slist *hdrs = NULL;
    for (int i = 0; i < g_header_count; i++)
        hdrs = curl_slist_append(hdrs, g_headers[i]);
    if (hdrs)
        curl_easy_setopt(s->curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(s->curl, CURLOPT_URL, url);
    if (pos >= 0 && len > 0) {
        snprintf(range, sizeof(range), "%lld-%lld", (long long)pos,
                 (long long)(pos + len - 1));
        curl_easy_setopt(s->curl, CURLOPT_RANGE, range);
        s->want = len;
    } else {
        s->want = CHUNK;
    }
    s->got = 0;
    s->content_range_total = -1;
    s->ct_m3u8 = false;

    char eb[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(s->curl, CURLOPT_ERRORBUFFER, eb);
    CURLcode rc = curl_easy_perform(s->curl);
    if (hdrs) {
        curl_easy_setopt(s->curl, CURLOPT_HTTPHEADER, NULL);
        curl_slist_free_all(hdrs);
    }
    long code = 0;
    curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &code);

    if (is_cancelled(s)) { snprintf(s->err, sizeof(s->err), "cancelled"); return -1; }
    // The write error we caused ourselves on an over-long body is not one.
    if (rc == CURLE_WRITE_ERROR && s->got == s->want) rc = CURLE_OK;
    if (rc != CURLE_OK) {
        if (rc == CURLE_PEER_FAILED_VERIFICATION)
            snprintf(s->err, sizeof(s->err),
                     "certificate rejected -- check the console's date and time");
        else
            snprintf(s->err, sizeof(s->err), "%.150s", eb[0] ? eb : curl_easy_strerror(rc));
        return -1;
    }
    if (code >= 400) {
        snprintf(s->err, sizeof(s->err), "HTTP %ld", code);
        return code;
    }
    return code;
}

httpsrc *httpsrc_open(const char *url, char *err, size_t errlen) {
    return httpsrc_open_ex(url, NULL, NULL, err, errlen);
}

httpsrc *httpsrc_open_abortable(const char *url, const volatile bool *abort,
                                char *err, size_t errlen) {
    return httpsrc_open_ex(url, abort, NULL, err, errlen);
}

httpsrc *httpsrc_open_ex(const char *url, const volatile bool *abort,
                         httpsrc_hls_probe *hls_out,
                         char *err, size_t errlen) {
    if (hls_out) memset(hls_out, 0, sizeof(*hls_out));
    if (err && errlen) err[0] = 0;

    httpsrc *s = calloc(1, sizeof(*s));
    if (!s) { set_err(err, errlen, "out of memory"); return NULL; }
    s->abort = abort;
    s->url = strdup(url);
    s->buf = malloc(CHUNK);
    s->curl = curl_easy_init();
    s->size = -1;
    if (!s->url || !s->buf || !s->curl) {
        set_err(err, errlen, "out of memory");
        httpsrc_close(s);
        return NULL;
    }

    // Probe with a smaller initial chunk: Content-Range gives the total size,
    // and the bytes satisfy mpv's initial container probe at low latency.
    long code = fetch(s, s->url, 0, PROBE_CHUNK);
    if (hls_out && code == 416) {
        // Some HLS origin servers reject Range requests on small .m3u8 files.
        code = fetch(s, s->url, -1, 0);
    }
    if (code < 0 || code >= 400) {
        set_err(err, errlen, s->err);
        httpsrc_close(s);
        return NULL;
    }

    if (hls_out && (s->ct_m3u8 || starts_with_extm3u(s->buf, s->got))) {
        char *eff = NULL;
        curl_easy_getinfo(s->curl, CURLINFO_EFFECTIVE_URL, &eff);
        hls_out->eff_url = strdup(eff ? eff : s->url);
        hls_out->truncated = (s->got >= PROBE_CHUNK);
        if (!hls_out->truncated && s->got > 0) {
            hls_out->data = (char *)malloc((size_t)s->got + 1);
            if (hls_out->data) {
                memcpy(hls_out->data, s->buf, (size_t)s->got);
                hls_out->data[s->got] = 0;
                hls_out->len = (size_t)s->got;
            }
        }
        httpsrc_close(s);
        return NULL;
    }

    if (code != 206 || s->content_range_total <= 0) {
        // No ranges: playable only from the start, never seekable. Not worth a
        // half-working player -- say why instead.
        set_err(err, errlen, "the server does not support seeking (no HTTP range requests)");
        httpsrc_close(s);
        return NULL;
    }
    s->size = s->content_range_total;
    s->buf_pos = 0;
    s->buf_len = s->got;
    s->expected_pos = s->got;
    s->seq_reads = 0;

    // Later requests go straight to where the redirects ended (a debrid link
    // usually bounces to a CDN), saving a round-trip per chunk.
    char *eff = NULL;
    curl_easy_getinfo(s->curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff && strcmp(eff, s->url) != 0) s->direct = strdup(eff);
    return s;
}

int64_t httpsrc_size(const httpsrc *s) { return s->size; }

int64_t httpsrc_read(httpsrc *s, int64_t pos, void *out, int64_t n) {
    if (is_cancelled(s)) return -1;
    if (pos < 0) return -1;
    if (pos >= s->size) return 0;

    if (!(pos >= s->buf_pos && pos < s->buf_pos + s->buf_len)) {
        if (pos == s->expected_pos) {
            if (s->seq_reads < 3) s->seq_reads++;
        } else {
            s->seq_reads = 0;
        }
        int64_t want_chunk = (int64_t)PROBE_CHUNK << s->seq_reads;
        if (want_chunk > CHUNK) want_chunk = CHUNK;
        int64_t len = s->size - pos < want_chunk ? s->size - pos : want_chunk;
        long code = -1;
        for (int attempt = 0; attempt < RETRIES && !is_cancelled(s); attempt++) {
            // After a failure, go back through the original URL: a CDN link
            // from the redirect may have expired.
            const char *u = (s->direct && attempt == 0) ? s->direct : s->url;
            code = fetch(s, u, pos, len);
            if (code == 206 && s->got > 0) break;
            if (code > 0 && code != 206) {
                snprintf(s->err, sizeof(s->err), "server ignored the range (HTTP %ld)", code);
                code = -1;
            }
        }
        if (code != 206 || s->got <= 0) {
            s->buf_len = 0;
            return -1;
        }
        s->buf_pos = pos;
        s->buf_len = s->got;
        s->expected_pos = pos + s->got;
    }

    int64_t off = pos - s->buf_pos;
    int64_t take = s->buf_len - off;
    if (take > n) take = n;
    memcpy(out, s->buf + off, (size_t)take);
    return take;
}

void httpsrc_cancel(httpsrc *s) { s->cancelled = true; }

const char *httpsrc_last_err(const httpsrc *s) { return s->err; }

void httpsrc_close(httpsrc *s) {
    if (!s) return;
    if (s->curl) curl_easy_cleanup(s->curl);
    free(s->buf);
    free(s->url);
    free(s->direct);
    free(s);
}
