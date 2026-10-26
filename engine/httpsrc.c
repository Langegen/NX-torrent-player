#include "httpsrc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <curl/curl.h>

// 4 MiB per request: at 10 MB/s that is 0.4 s of transfer for one round-trip
// of setup, and a seek costs at most this much wasted download.
#define CHUNK (4 * 1024 * 1024)
#define RETRIES 3

static char g_ca[512];
static char g_proxy[512];
static volatile int64_t g_bytes_total;

void httpsrc_set_ca(const char *p) { snprintf(g_ca, sizeof(g_ca), "%s", p ? p : ""); }
void httpsrc_set_proxy(const char *p) { snprintf(g_proxy, sizeof(g_proxy), "%s", p ? p : ""); }
int64_t httpsrc_bytes_total(void) { return __atomic_load_n(&g_bytes_total, __ATOMIC_RELAXED); }

struct httpsrc {
    CURL *curl;
    char *url;           // original URL
    char *direct;        // where the redirects led (NULL = same as url)
    int64_t size;
    volatile bool cancelled;

    uint8_t *buf;        // the buffered chunk
    int64_t buf_pos;     // its offset in the resource
    int64_t buf_len;

    // State of the transfer in progress.
    int64_t want;        // bytes asked for
    int64_t got;
    int64_t content_range_total;  // from "Content-Range: bytes a-b/TOTAL", -1
    char err[160];
};

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg);
}

static size_t write_cb(char *p, size_t sz, size_t nm, void *ud) {
    httpsrc *s = ud;
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
    // "Content-Range: bytes 0-0/123456" (case-insensitive name).
    static const char name[] = "content-range:";
    if (n > sizeof(name) && strncasecmp(p, name, sizeof(name) - 1) == 0) {
        const char *slash = memchr(p, '/', n);
        if (slash && slash + 1 < p + n && isdigit((unsigned char)slash[1]))
            s->content_range_total = strtoll(slash + 1, NULL, 10);
    }
    return n;
}

static int progress_cb(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut,
                       curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    return ((httpsrc *)ud)->cancelled ? 1 : 0;
}

static void setup(httpsrc *s) {
    CURL *c = s->curl;
    curl_easy_reset(c);
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
    if (g_proxy[0]) curl_easy_setopt(c, CURLOPT_PROXY, g_proxy);
}

// One ranged GET of [pos, pos+len) into s->buf. Returns the HTTP status (206
// on success), or -1 with s->err set.
static long fetch(httpsrc *s, const char *url, int64_t pos, int64_t len) {
    setup(s);
    char range[64];
    snprintf(range, sizeof(range), "%lld-%lld", (long long)pos,
             (long long)(pos + len - 1));
    curl_easy_setopt(s->curl, CURLOPT_URL, url);
    curl_easy_setopt(s->curl, CURLOPT_RANGE, range);
    s->want = len;
    s->got = 0;
    s->content_range_total = -1;

    char eb[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(s->curl, CURLOPT_ERRORBUFFER, eb);
    CURLcode rc = curl_easy_perform(s->curl);
    long code = 0;
    curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &code);

    if (s->cancelled) { snprintf(s->err, sizeof(s->err), "cancelled"); return -1; }
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
        return -1;
    }
    return code;
}

httpsrc *httpsrc_open(const char *url, char *err, size_t errlen) {
    httpsrc *s = calloc(1, sizeof(*s));
    if (!s) { set_err(err, errlen, "out of memory"); return NULL; }
    s->url = strdup(url);
    s->buf = malloc(CHUNK);
    s->curl = curl_easy_init();
    s->size = -1;
    if (!s->url || !s->buf || !s->curl) {
        set_err(err, errlen, "out of memory");
        httpsrc_close(s);
        return NULL;
    }

    // Probe with the first chunk itself: the size comes from Content-Range,
    // and the data is the start of the file mpv is about to ask for anyway.
    long code = fetch(s, s->url, 0, CHUNK);
    if (code < 0) {
        set_err(err, errlen, s->err);
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

    // Later requests go straight to where the redirects ended (a debrid link
    // usually bounces to a CDN), saving a round-trip per chunk.
    char *eff = NULL;
    curl_easy_getinfo(s->curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff && strcmp(eff, s->url) != 0) s->direct = strdup(eff);
    return s;
}

int64_t httpsrc_size(const httpsrc *s) { return s->size; }

int64_t httpsrc_read(httpsrc *s, int64_t pos, void *out, int64_t n) {
    if (s->cancelled) return -1;
    if (pos < 0) return -1;
    if (pos >= s->size) return 0;

    if (!(pos >= s->buf_pos && pos < s->buf_pos + s->buf_len)) {
        int64_t len = s->size - pos < CHUNK ? s->size - pos : CHUNK;
        long code = -1;
        for (int attempt = 0; attempt < RETRIES && !s->cancelled; attempt++) {
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
