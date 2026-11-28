#include "hlssrc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#ifdef HLS_PROXY_ENABLE_AES
#include <mbedtls/aes.h>
#endif

#include "httpsrc.h"

#define HS_MAX_HEADERS     16
#define HS_MAX_HEADER_LEN  512
#define HS_MAX_M3U8_BYTES  (4 * 1024 * 1024)
#define HS_MAX_INIT_BYTES  (4 * 1024 * 1024)
#define HS_MAX_SEG_BYTES   (32 * 1024 * 1024)
#define HS_RETRIES         2
#define HS_TAIL_PROBE_WIN  (512 * 1024)

static char g_ca[512];
static char g_proxy[512];
static char g_headers[HS_MAX_HEADERS][HS_MAX_HEADER_LEN];
static int g_header_count = 0;
static int g_prefer_height = 0;
static bool g_hide_4k = false;

void hlssrc_set_ca(const char *p) { snprintf(g_ca, sizeof(g_ca), "%s", p ? p : ""); }
void hlssrc_set_proxy(const char *p) { snprintf(g_proxy, sizeof(g_proxy), "%s", p ? p : ""); }
void hlssrc_set_quality(int prefer_height, bool hide_4k) {
    g_prefer_height = prefer_height;
    g_hide_4k = hide_4k;
}

void hlssrc_set_headers(const char *const *headers, int count) {
    g_header_count = 0;
    if (!headers || count <= 0) return;
    for (int i = 0; i < count && g_header_count < HS_MAX_HEADERS; i++) {
        if (!headers[i] || !headers[i][0]) continue;
        snprintf(g_headers[g_header_count], sizeof(g_headers[0]), "%s", headers[i]);
        g_header_count++;
    }
}

typedef struct {
    char *url;
    double duration;
    int64_t range_off;
    int64_t range_len;   // 0 = full resource
    int key_idx;         // -1 = unencrypted
    bool has_iv;
    uint8_t iv[16];
    uint64_t media_seq;
    int64_t voff;
    int64_t vlen;
} hls_seg;

typedef struct {
    char *url;
    bool loaded;
    uint8_t key[16];
} hls_key;

typedef struct {
    char *url;
    int height;
    int64_t bandwidth;
} hls_variant;

struct hlssrc {
    CURL *curl;
    const volatile bool *abort;
    volatile bool cancelled;
    char err[160];

    // Snapshot of global settings taken at open time so mid-playback UI changes
    // cannot race with background demuxer reads.
    char ca[512];
    char proxy[512];
    char headers[HS_MAX_HEADERS][HS_MAX_HEADER_LEN];
    int header_count;
    int prefer_height;
    bool hide_4k;

    char *init_url;
    int64_t init_range_off;
    int64_t init_range_len;
    uint8_t *init_buf;
    int64_t init_len;

    hls_key *keys;
    size_t key_count;
    size_t key_cap;

    hls_seg *segs;
    size_t seg_count;
    size_t seg_cap;

    double est_bps;
    int64_t total_size;

    // Cached segment 0 (stays resident for instant rewind to offset 0 after
    // FFmpeg's initial tail probe).
    uint8_t *seg0_buf;
    int64_t seg0_len;

    // Buffer for currently loaded segment (> 0).
    uint8_t *cur_buf;
    int64_t cur_len;
    size_t cur_cap;
    int64_t cur_loaded;   // segment index in cur_buf, -1 if none

    // Stream read cursor
    bool in_init;
    int64_t init_off;
    size_t cur_seg;
    int64_t cur_off;
    int64_t vpos;
};

static int hs_strcasecmp_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)tolower((unsigned char)a[i]);
        unsigned char cb = (unsigned char)tolower((unsigned char)b[i]);
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg ? msg : "");
}

static bool is_cancelled(const hlssrc *s) {
    return s->cancelled || (s->abort && *s->abort);
}

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

// Resolve `rel` against `base` (http(s), //, /path, ?query, relative).
static void resolve_url(const char *base, const char *rel, char *out, size_t cap) {
    if (!rel || !rel[0] || !cap) {
        if (cap) out[0] = 0;
        return;
    }
    if (hs_strcasecmp_n(rel, "http://", 7) == 0 ||
        hs_strcasecmp_n(rel, "https://", 8) == 0) {
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

    const char *q = strpbrk(base, "?#");
    size_t path_end = q ? (size_t)(q - base) : strlen(base);
    size_t dir_end = origin_len;
    for (size_t i = path_end; i > origin_len; i--) {
        if (base[i - 1] == '/') {
            dir_end = i;
            break;
        }
    }
    if (dir_end == origin_len)
        snprintf(out, cap, "%.*s/%s", (int)origin_len, base, rel);
    else
        snprintf(out, cap, "%.*s%s", (int)dir_end, base, rel);
}

// ---------------------------------------------------------------------------
// Curl HTTP fetch
// ---------------------------------------------------------------------------

typedef struct {
    const hlssrc *s;
    uint8_t *buf;
    size_t len;
    size_t cap;
    size_t max_bytes;
} mem_sink;

static size_t mem_write_cb(char *p, size_t sz, size_t nm, void *ud) {
    mem_sink *m = (mem_sink *)ud;
    if (is_cancelled(m->s)) return 0;
    size_t n = sz * nm;
    if (n == 0) return 0;
    if (m->len + n > m->max_bytes) return 0;
    if (m->len + n + 1 > m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 65536;
        while (ncap < m->len + n + 1) {
            if (ncap > m->max_bytes / 2) {
                ncap = m->max_bytes + 1;
                break;
            }
            ncap *= 2;
        }
        if (ncap > m->max_bytes + 1) ncap = m->max_bytes + 1;
        uint8_t *nb = (uint8_t *)realloc(m->buf, ncap);
        if (!nb) return 0;
        m->buf = nb;
        m->cap = ncap;
    }
    memcpy(m->buf + m->len, p, n);
    m->len += n;
    m->buf[m->len] = 0;
    httpsrc_add_bytes((int64_t)n);
    return n;
}

static int hs_progress_cb(void *ud, curl_off_t dt, curl_off_t dn,
                          curl_off_t ut, curl_off_t un) {
    (void)dt; (void)dn; (void)ut; (void)un;
    return is_cancelled((const hlssrc *)ud) ? 1 : 0;
}

static int fetch_mem(hlssrc *s, const char *url,
                     int64_t range_off, int64_t range_len,
                     bool allow_gzip, size_t max_bytes,
                     uint8_t **out_buf, size_t *out_len,
                     char *eff_url_out, size_t eff_cap) {
    *out_buf = NULL;
    *out_len = 0;
    if (eff_url_out && eff_cap) eff_url_out[0] = 0;

    for (int attempt = 0; attempt <= HS_RETRIES; attempt++) {
        if (is_cancelled(s)) {
            snprintf(s->err, sizeof(s->err), "cancelled");
            return -1;
        }

        CURL *c = s->curl;
        curl_easy_reset(c);

        bool has_ua = false;
        struct curl_slist *hdrs = NULL;
        for (int i = 0; i < s->header_count; i++) {
            if (hs_strcasecmp_n(s->headers[i], "user-agent:", 11) == 0) has_ua = true;
            hdrs = curl_slist_append(hdrs, s->headers[i]);
        }
        if (!has_ua) curl_easy_setopt(c, CURLOPT_USERAGENT, "NX-torrent-player");
        if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        if (s->ca[0]) curl_easy_setopt(c, CURLOPT_CAINFO, s->ca);
        if (s->proxy[0] && !is_local_url(url)) curl_easy_setopt(c, CURLOPT_PROXY, s->proxy);
        if (allow_gzip) curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");

        char range[64];
        if (range_len > 0) {
            snprintf(range, sizeof(range), "%lld-%lld",
                     (long long)range_off, (long long)(range_off + range_len - 1));
            curl_easy_setopt(c, CURLOPT_RANGE, range);
        }

        mem_sink sink = {0};
        sink.s = s;
        sink.max_bytes = max_bytes;

        char eb[CURL_ERROR_SIZE] = {0};
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, hs_progress_cb);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, s);
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, eb);

        CURLcode rc = curl_easy_perform(c);
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        char *eff = NULL;
        curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
        if (eff && eff_url_out && eff_cap)
            snprintf(eff_url_out, eff_cap, "%s", eff);

        if (hdrs) {
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, NULL);
            curl_slist_free_all(hdrs);
        }

        if (is_cancelled(s)) {
            free(sink.buf);
            snprintf(s->err, sizeof(s->err), "cancelled");
            return -1;
        }
        if (rc == CURLE_OK && code < 400) {
            if (!sink.buf) {
                sink.buf = (uint8_t *)malloc(1);
                if (sink.buf) sink.buf[0] = 0;
            }
            *out_buf = sink.buf;
            *out_len = sink.len;
            return 0;
        }

        free(sink.buf);
        if (rc != CURLE_OK) {
            if (rc == CURLE_PEER_FAILED_VERIFICATION) {
                snprintf(s->err, sizeof(s->err),
                         "certificate rejected -- check the console's date and time");
                return -1;
            }
            snprintf(s->err, sizeof(s->err), "%.150s",
                     eb[0] ? eb : curl_easy_strerror(rc));
        } else {
            snprintf(s->err, sizeof(s->err), "HTTP %ld", code);
            // Don't retry 4xx client errors (except 408 / 429).
            if (code >= 400 && code < 500 && code != 408 && code != 429)
                return -1;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// M3U8 parsing
// ---------------------------------------------------------------------------

static bool extract_attr(const char *line, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = line;
    while (*p) {
        if ((p == line || p[-1] == ',' || p[-1] == ':' || p[-1] == ' ') &&
            hs_strcasecmp_n(p, key, klen) == 0 && p[klen] == '=') {
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

static bool parse_byterange(const char *val, int64_t *len_out, int64_t *off_out, bool *has_off) {
    if (!val || !isdigit((unsigned char)*val)) return false;
    char *end = NULL;
    long long l = strtoll(val, &end, 10);
    if (l <= 0) return false;
    *len_out = (int64_t)l;
    if (end && *end == '@' && isdigit((unsigned char)end[1])) {
        *off_out = (int64_t)strtoll(end + 1, NULL, 10);
        *has_off = true;
    } else {
        *off_out = 0;
        *has_off = false;
    }
    return true;
}

#ifdef HLS_PROXY_ENABLE_AES
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
    for (size_t i = 0; i < len; i++) {
        int nib = hex_nibble(s[len - 1 - i]);
        if (nib < 0) return false;
        size_t byte_idx = 15 - (i / 2);
        if (i % 2 == 0) iv[byte_idx] |= (uint8_t)nib;
        else iv[byte_idx] |= (uint8_t)(nib << 4);
    }
    return true;
}
#endif

#ifdef HLS_PROXY_ENABLE_AES
static int intern_key(hlssrc *s, const char *abs_url) {
    for (size_t i = 0; i < s->key_count; i++) {
        if (strcmp(s->keys[i].url, abs_url) == 0) return (int)i;
    }
    if (s->key_count == s->key_cap) {
        size_t ncap = s->key_cap ? s->key_cap * 2 : 8;
        hls_key *nk = (hls_key *)realloc(s->keys, ncap * sizeof(*nk));
        if (!nk) return -1;
        s->keys = nk;
        s->key_cap = ncap;
    }
    hls_key *k = &s->keys[s->key_count];
    memset(k, 0, sizeof(*k));
    k->url = strdup(abs_url);
    if (!k->url) return -1;
    return (int)(s->key_count++);
}
#endif

static int append_seg(hlssrc *s, const hls_seg *seg) {
    if (s->seg_count == s->seg_cap) {
        size_t ncap = s->seg_cap ? s->seg_cap * 2 : 64;
        hls_seg *ns = (hls_seg *)realloc(s->segs, ncap * sizeof(*ns));
        if (!ns) return -1;
        s->segs = ns;
        s->seg_cap = ncap;
    }
    s->segs[s->seg_count++] = *seg;
    return 0;
}

static bool is_master_playlist(const char *src, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && src[eol] != '\n') eol++;
        size_t l = eol - pos;
        const char *p = src + pos;
        while (l > 0 && (*p == ' ' || *p == '\t' || *p == '\xEF' || *p == '\xBB' || *p == '\xBF')) {
            p++; l--;
        }
        if (l >= 18 && hs_strcasecmp_n(p, "#EXT-X-STREAM-INF:", 18) == 0) return true;
        pos = eol < len ? eol + 1 : len;
    }
    return false;
}

static char *pick_master_variant(const hlssrc *s, const char *base_url,
                                 const char *src, size_t len) {
    hls_variant *vars = NULL;
    size_t vcount = 0, vcap = 0;

    bool have_pending = false;
    int pend_h = 0;
    int64_t pend_bw = 0;

    size_t pos = 0;
    if (len >= 3 && (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        pos = 3;

    while (pos < len) {
        size_t eol = pos;
        while (eol < len && src[eol] != '\n') eol++;
        size_t ll = eol - pos;
        if (ll > 0 && src[pos + ll - 1] == '\r') ll--;

        char *line = (char *)malloc(ll + 1);
        if (!line) break;
        memcpy(line, src + pos, ll);
        line[ll] = 0;
        pos = eol < len ? eol + 1 : len;

        char *t = line;
        while (*t == ' ' || *t == '\t') t++;

        if (t[0] == 0) {
            free(line);
            continue;
        }
        if (hs_strcasecmp_n(t, "#EXT-X-STREAM-INF:", 18) == 0) {
            have_pending = true;
            pend_h = 0;
            pend_bw = 0;
            char val[64];
            if (extract_attr(t + 18, "RESOLUTION", val, sizeof(val))) {
                const char *x = strchr(val, 'x');
                if (!x) x = strchr(val, 'X');
                if (x) pend_h = atoi(x + 1);
            }
            if (extract_attr(t + 18, "BANDWIDTH", val, sizeof(val)))
                pend_bw = strtoll(val, NULL, 10);
        } else if (t[0] != '#' && have_pending) {
            have_pending = false;
            char abs_url[2048];
            resolve_url(base_url, t, abs_url, sizeof(abs_url));
            if (vcount == vcap) {
                size_t ncap = vcap ? vcap * 2 : 8;
                hls_variant *nv = (hls_variant *)realloc(vars, ncap * sizeof(*nv));
                if (!nv) { free(line); break; }
                vars = nv;
                vcap = ncap;
            }
            vars[vcount].url = strdup(abs_url);
            vars[vcount].height = pend_h;
            vars[vcount].bandwidth = pend_bw;
            if (vars[vcount].url) vcount++;
        } else if (t[0] != '#') {
            have_pending = false;
        }
        free(line);
    }

    if (vcount == 0) {
        free(vars);
        return NULL;
    }

    // Apply hide_4k filter if at least one non-4K variant exists.
    bool have_non4k = false;
    if (s->hide_4k) {
        for (size_t i = 0; i < vcount; i++) {
            if (vars[i].height <= 1080) { have_non4k = true; break; }
        }
    }

    size_t best = 0;
    bool best_valid = false;
    int pref = s->prefer_height;

    for (size_t i = 0; i < vcount; i++) {
        if (have_non4k && vars[i].height > 1080) continue;
        if (!best_valid) {
            best = i;
            best_valid = true;
            continue;
        }
        int hi = vars[i].height;
        int hb = vars[best].height;
        int64_t bi = vars[i].bandwidth;
        int64_t bb = vars[best].bandwidth;

        if (pref > 0) {
            bool i_le = (hi > 0 && hi <= pref);
            bool b_le = (hb > 0 && hb <= pref);
            if (i_le && !b_le) {
                best = i;
            } else if (i_le && b_le) {
                if (hi > hb || (hi == hb && bi > bb)) best = i;
            } else if (!i_le && !b_le) {
                if (hi > 0 && (hb <= 0 || hi < hb)) best = i;
                else if (hi == hb && bi > bb) best = i;
            }
        } else {
            if (hi > hb || (hi == hb && bi > bb)) best = i;
        }
    }

    char *chosen = vars[best].url;
    vars[best].url = NULL;
    for (size_t i = 0; i < vcount; i++) free(vars[i].url);
    free(vars);
    return chosen;
}

static int parse_media_playlist(hlssrc *s, const char *base_url,
                                const char *src, size_t len) {
    uint64_t media_seq = 0;
    int cur_key_idx = -1;
    bool cur_has_iv = false;
    uint8_t cur_iv[16] = {0};
    double pend_dur = 0.0;
    int64_t pend_br_len = 0;
    int64_t pend_br_off = 0;
    bool pend_has_br = false;
    bool pend_br_has_off = false;

    char last_br_url[2048] = {0};
    int64_t last_br_end = 0;

    size_t pos = 0;
    if (len >= 3 && (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        pos = 3;

    while (pos < len) {
        size_t eol = pos;
        while (eol < len && src[eol] != '\n') eol++;
        size_t ll = eol - pos;
        if (ll > 0 && src[pos + ll - 1] == '\r') ll--;

        char *line = (char *)malloc(ll + 1);
        if (!line) {
            snprintf(s->err, sizeof(s->err), "out of memory");
            return -1;
        }
        memcpy(line, src + pos, ll);
        line[ll] = 0;
        pos = eol < len ? eol + 1 : len;

        char *t = line;
        while (*t == ' ' || *t == '\t') t++;

        if (t[0] == 0) {
            free(line);
            continue;
        }

        if (t[0] == '#') {
            if (hs_strcasecmp_n(t, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
                media_seq = strtoull(t + 22, NULL, 10);
            } else if (hs_strcasecmp_n(t, "#EXTINF:", 8) == 0) {
                pend_dur = strtod(t + 8, NULL);
            } else if (hs_strcasecmp_n(t, "#EXT-X-BYTERANGE:", 17) == 0) {
                pend_has_br = parse_byterange(t + 17, &pend_br_len,
                                              &pend_br_off, &pend_br_has_off);
            } else if (hs_strcasecmp_n(t, "#EXT-X-MAP:", 11) == 0) {
                if (!s->init_url) {
                    char map_uri[2048] = {0};
                    if (extract_attr(t + 11, "URI", map_uri, sizeof(map_uri))) {
                        char abs_map[2048] = {0};
                        resolve_url(base_url, map_uri, abs_map, sizeof(abs_map));
                        s->init_url = strdup(abs_map);
                        char br[64] = {0};
                        if (extract_attr(t + 11, "BYTERANGE", br, sizeof(br))) {
                            int64_t bl = 0, bo = 0;
                            bool bho = false;
                            if (parse_byterange(br, &bl, &bo, &bho)) {
                                s->init_range_len = bl;
                                s->init_range_off = bho ? bo : 0;
                            }
                        }
                    }
                }
            } else if (hs_strcasecmp_n(t, "#EXT-X-KEY:", 11) == 0) {
                char method[64] = {0};
                if (extract_attr(t + 11, "METHOD", method, sizeof(method))) {
                    if (hs_strcasecmp_n(method, "NONE", 4) == 0) {
                        cur_key_idx = -1;
                        cur_has_iv = false;
                    } else if (hs_strcasecmp_n(method, "AES-128", 7) == 0) {
#ifdef HLS_PROXY_ENABLE_AES
                        char key_uri[2048] = {0}, iv_str[128] = {0};
                        if (extract_attr(t + 11, "URI", key_uri, sizeof(key_uri))) {
                            char abs_key[2048] = {0};
                            resolve_url(base_url, key_uri, abs_key, sizeof(abs_key));
                            int kidx = intern_key(s, abs_key);
                            if (kidx < 0) {
                                free(line);
                                snprintf(s->err, sizeof(s->err), "out of memory");
                                return -1;
                            }
                            cur_key_idx = kidx;
                            cur_has_iv = extract_attr(t + 11, "IV", iv_str, sizeof(iv_str)) &&
                                         parse_hex_iv(iv_str, cur_iv);
                        }
#else
                        free(line);
                        snprintf(s->err, sizeof(s->err),
                                 "AES-128 HLS decryption not enabled");
                        return -1;
#endif
                    } else {
                        free(line);
                        snprintf(s->err, sizeof(s->err),
                                 "unsupported HLS encryption: %s", method);
                        return -1;
                    }
                }
            }
        } else {
            char abs_seg[2048] = {0};
            resolve_url(base_url, t, abs_seg, sizeof(abs_seg));

            hls_seg seg;
            memset(&seg, 0, sizeof(seg));
            seg.url = strdup(abs_seg);
            if (!seg.url) {
                free(line);
                snprintf(s->err, sizeof(s->err), "out of memory");
                return -1;
            }
            seg.duration = pend_dur;
            seg.key_idx = cur_key_idx;
            seg.has_iv = cur_has_iv;
            if (cur_has_iv) memcpy(seg.iv, cur_iv, 16);
            seg.media_seq = media_seq;

            if (pend_has_br) {
                seg.range_len = pend_br_len;
                if (pend_br_has_off) {
                    seg.range_off = pend_br_off;
                } else {
                    seg.range_off = (strcmp(last_br_url, abs_seg) == 0) ? last_br_end : 0;
                }
                snprintf(last_br_url, sizeof(last_br_url), "%s", abs_seg);
                last_br_end = seg.range_off + seg.range_len;
            }

            if (append_seg(s, &seg) != 0) {
                free(seg.url);
                free(line);
                snprintf(s->err, sizeof(s->err), "out of memory");
                return -1;
            }

            media_seq++;
            pend_dur = 0.0;
            pend_has_br = false;
            pend_br_len = 0;
            pend_br_off = 0;
            pend_br_has_off = false;
        }
        free(line);
    }

    if (s->seg_count == 0) {
        snprintf(s->err, sizeof(s->err), "HLS playlist contains no segments");
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Segment fetching & AES-128 decryption
// ---------------------------------------------------------------------------

#ifdef HLS_PROXY_ENABLE_AES
static int ensure_key_loaded(hlssrc *s, int key_idx) {
    if (key_idx < 0 || (size_t)key_idx >= s->key_count) return -1;
    hls_key *k = &s->keys[key_idx];
    if (k->loaded) return 0;

    uint8_t *kbuf = NULL;
    size_t klen = 0;
    if (fetch_mem(s, k->url, 0, 0, false, 64, &kbuf, &klen, NULL, 0) != 0)
        return -1;
    if (klen != 16) {
        free(kbuf);
        snprintf(s->err, sizeof(s->err), "invalid HLS AES key size (%zu)", klen);
        return -1;
    }
    memcpy(k->key, kbuf, 16);
    free(kbuf);
    k->loaded = true;
    return 0;
}
#endif

static int fetch_segment(hlssrc *s, size_t idx, uint8_t **out_buf, size_t *out_len) {
    const hls_seg *seg = &s->segs[idx];
    uint8_t *buf = NULL;
    size_t len = 0;
    if (fetch_mem(s, seg->url, seg->range_off, seg->range_len,
                  false, HS_MAX_SEG_BYTES, &buf, &len, NULL, 0) != 0) {
        return -1;
    }

    if (seg->key_idx >= 0) {
#ifdef HLS_PROXY_ENABLE_AES
        if (ensure_key_loaded(s, seg->key_idx) != 0) {
            free(buf);
            return -1;
        }
        if (len == 0 || (len % 16) != 0) {
            free(buf);
            snprintf(s->err, sizeof(s->err), "invalid AES HLS segment size (%zu)", len);
            return -1;
        }
        uint8_t iv[16] = {0};
        if (seg->has_iv) {
            memcpy(iv, seg->iv, 16);
        } else {
            uint64_t seq = seg->media_seq;
            for (int b = 15; b >= 8; b--) {
                iv[b] = (uint8_t)(seq & 0xFF);
                seq >>= 8;
            }
        }
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_dec(&aes, s->keys[seg->key_idx].key, 128);
        mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, buf, buf);
        mbedtls_aes_free(&aes);

        uint8_t pad = buf[len - 1];
        if (pad >= 1 && pad <= 16 && (size_t)pad <= len) {
            bool ok = true;
            for (size_t i = 0; i < (size_t)pad; i++) {
                if (buf[len - 1 - i] != pad) { ok = false; break; }
            }
            if (ok) len -= pad;
        }
#else
        free(buf);
        snprintf(s->err, sizeof(s->err), "AES-128 HLS decryption not enabled");
        return -1;
#endif
    }

    *out_buf = buf;
    *out_len = len;
    return 0;
}

static int ensure_seg_loaded(hlssrc *s, size_t idx,
                             const uint8_t **buf_out, int64_t *len_out) {
    if (idx == 0) {
        *buf_out = s->seg0_buf;
        *len_out = s->seg0_len;
        return 0;
    }
    if (s->cur_loaded != (int64_t)idx) {
        uint8_t *buf = NULL;
        size_t len = 0;
        if (fetch_segment(s, idx, &buf, &len) != 0)
            return -1;
        free(s->cur_buf);
        s->cur_buf = buf;
        s->cur_cap = len + 1;
        s->cur_len = (int64_t)len;
        s->cur_loaded = (int64_t)idx;
    }
    *buf_out = s->cur_buf;
    *len_out = s->cur_len;
    return 0;
}

// ---------------------------------------------------------------------------
// Open & Timeline setup
// ---------------------------------------------------------------------------

static hlssrc *create_hlssrc(const volatile bool *abort) {
    hlssrc *s = (hlssrc *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->abort = abort;
    s->cur_loaded = -1;
    s->curl = curl_easy_init();
    if (!s->curl) {
        free(s);
        return NULL;
    }
    snprintf(s->ca, sizeof(s->ca), "%s", g_ca);
    snprintf(s->proxy, sizeof(s->proxy), "%s", g_proxy);
    s->header_count = g_header_count;
    for (int i = 0; i < g_header_count; i++) {
        memcpy(s->headers[i], g_headers[i], sizeof(s->headers[0]));
        s->headers[i][sizeof(s->headers[0]) - 1] = 0;
    }
    s->prefer_height = g_prefer_height;
    s->hide_4k = g_hide_4k;
    return s;
}

static int finalize_open(hlssrc *s, const char *eff_url,
                         const char *manifest, size_t manifest_len) {
    char *cur_url = strdup(eff_url);
    char *cur_body = (char *)malloc(manifest_len + 1);
    if (!cur_url || !cur_body) {
        free(cur_url);
        free(cur_body);
        snprintf(s->err, sizeof(s->err), "out of memory");
        return -1;
    }
    memcpy(cur_body, manifest, manifest_len);
    cur_body[manifest_len] = 0;
    size_t cur_len = manifest_len;

    // Follow up to 3 master playlist levels.
    for (int depth = 0; depth < 3 && is_master_playlist(cur_body, cur_len); depth++) {
        char *next_url = pick_master_variant(s, cur_url, cur_body, cur_len);
        if (!next_url) {
            free(cur_url);
            free(cur_body);
            snprintf(s->err, sizeof(s->err), "HLS master playlist has no variants");
            return -1;
        }
        free(cur_url);
        free(cur_body);
        cur_url = NULL;
        cur_body = NULL;

        uint8_t *nbuf = NULL;
        size_t nlen = 0;
        char next_eff[2048] = {0};
        if (fetch_mem(s, next_url, 0, 0, true, HS_MAX_M3U8_BYTES,
                      &nbuf, &nlen, next_eff, sizeof(next_eff)) != 0) {
            free(next_url);
            return -1;
        }
        cur_url = strdup(next_eff[0] ? next_eff : next_url);
        free(next_url);
        cur_body = (char *)nbuf;
        cur_len = nlen;
        if (!cur_url) {
            free(cur_body);
            snprintf(s->err, sizeof(s->err), "out of memory");
            return -1;
        }
    }

    int prc = parse_media_playlist(s, cur_url, cur_body, cur_len);
    free(cur_url);
    free(cur_body);
    if (prc != 0) return -1;

    if (s->init_url) {
        uint8_t *ibuf = NULL;
        size_t ilen = 0;
        if (fetch_mem(s, s->init_url, s->init_range_off, s->init_range_len,
                      false, HS_MAX_INIT_BYTES, &ibuf, &ilen, NULL, 0) != 0)
            return -1;
        s->init_buf = ibuf;
        s->init_len = (int64_t)ilen;
    }

    uint8_t *s0 = NULL;
    size_t s0len = 0;
    if (fetch_segment(s, 0, &s0, &s0len) != 0)
        return -1;
    s->seg0_buf = s0;
    s->seg0_len = (int64_t)s0len;

    double bps = 512.0 * 1024.0;
    if (s->segs[0].duration > 0.1 && s->seg0_len > 0) {
        bps = (double)s->seg0_len / s->segs[0].duration;
        if (bps < 8.0 * 1024.0) bps = 8.0 * 1024.0;
        if (bps > 24.0 * 1024.0 * 1024.0) bps = 24.0 * 1024.0 * 1024.0;
    }
    s->est_bps = bps;

    int64_t off = s->init_len;
    for (size_t i = 0; i < s->seg_count; i++) {
        s->segs[i].voff = off;
        if (i == 0) {
            s->segs[i].vlen = s->seg0_len > 0 ? s->seg0_len : 188;
        } else if (s->segs[i].range_len > 0) {
            s->segs[i].vlen = s->segs[i].range_len;
        } else {
            double dur = s->segs[i].duration > 0.1 ? s->segs[i].duration
                       : (s->segs[0].duration > 0.1 ? s->segs[0].duration : 6.0);
            int64_t est = (int64_t)(dur * bps);
            if (s->init_len == 0) {
                est = (est / 188) * 188;
                if (est < 188) est = 188;
            } else if (est < 1024) {
                est = 1024;
            }
            s->segs[i].vlen = est;
        }
        off += s->segs[i].vlen;
    }
    s->total_size = off;

    s->in_init = (s->init_len > 0);
    s->init_off = 0;
    s->cur_seg = 0;
    s->cur_off = 0;
    s->vpos = 0;
    return 0;
}

hlssrc *hlssrc_open_abortable(const char *url, const volatile bool *abort,
                              char *err, size_t errlen) {
    if (!url || !url[0]) {
        set_err(err, errlen, "empty URL");
        return NULL;
    }
    hlssrc *s = create_hlssrc(abort);
    if (!s) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }

    uint8_t *mbuf = NULL;
    size_t mlen = 0;
    char eff[2048] = {0};
    if (fetch_mem(s, url, 0, 0, true, HS_MAX_M3U8_BYTES,
                  &mbuf, &mlen, eff, sizeof(eff)) != 0) {
        set_err(err, errlen, s->err);
        hlssrc_close(s);
        return NULL;
    }

    int rc = finalize_open(s, eff[0] ? eff : url, (const char *)mbuf, mlen);
    free(mbuf);
    if (rc != 0) {
        set_err(err, errlen, s->err);
        hlssrc_close(s);
        return NULL;
    }
    return s;
}

hlssrc *hlssrc_open_with_manifest(const char *eff_url, const char *manifest,
                                  size_t manifest_len,
                                  const volatile bool *abort,
                                  char *err, size_t errlen) {
    if (!eff_url || !eff_url[0] || !manifest) {
        set_err(err, errlen, "invalid HLS manifest");
        return NULL;
    }
    hlssrc *s = create_hlssrc(abort);
    if (!s) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    if (finalize_open(s, eff_url, manifest, manifest_len) != 0) {
        set_err(err, errlen, s->err);
        hlssrc_close(s);
        return NULL;
    }
    return s;
}

int64_t hlssrc_size(const hlssrc *s) {
    if (!s) return -1;
    return s->total_size;
}

int64_t hlssrc_read(hlssrc *s, void *out, int64_t n) {
    if (!s || !out || n < 0) return -1;
    if (n == 0) return 0;
    if (is_cancelled(s)) {
        snprintf(s->err, sizeof(s->err), "cancelled");
        return -1;
    }

    if (s->in_init) {
        int64_t avail = s->init_len - s->init_off;
        if (avail > 0) {
            int64_t take = n < avail ? n : avail;
            memcpy(out, s->init_buf + s->init_off, (size_t)take);
            s->init_off += take;
            s->vpos += take;
            if (s->init_off >= s->init_len) {
                s->in_init = false;
                s->cur_seg = 0;
                s->cur_off = 0;
            }
            return take;
        }
        s->in_init = false;
        s->cur_seg = 0;
        s->cur_off = 0;
    }

    while (s->cur_seg < s->seg_count) {
        if (is_cancelled(s)) {
            snprintf(s->err, sizeof(s->err), "cancelled");
            return -1;
        }
        const uint8_t *buf = NULL;
        int64_t len = 0;
        if (ensure_seg_loaded(s, s->cur_seg, &buf, &len) != 0)
            return -1;

        if (s->cur_off >= len) {
            s->cur_seg++;
            s->cur_off = 0;
            continue;
        }

        int64_t avail = len - s->cur_off;
        int64_t take = n < avail ? n : avail;
        memcpy(out, buf + s->cur_off, (size_t)take);
        s->cur_off += take;
        s->vpos += take;
        if (s->cur_off >= len) {
            s->cur_seg++;
            s->cur_off = 0;
        }
        return take;
    }

    return 0; // EOF
}

static int64_t snap_ts_sync(const uint8_t *buf, int64_t len, int64_t target) {
    if (len <= 0 || target <= 0) return 0;
    if (target >= len) target = len - 1;

    // Forward scan from `target` for 0x47 sync byte spaced by 188 bytes.
    for (int64_t p = target; p < len; p++) {
        if (buf[p] == 0x47 && (p + 188 >= len || buf[p + 188] == 0x47))
            return p;
    }
    // Backward scan fallback if target landed past the last packet start.
    for (int64_t p = target - 1; p >= 0; p--) {
        if (buf[p] == 0x47 && (p + 188 >= len || buf[p + 188] == 0x47))
            return p;
    }
    return 0;
}

int64_t hlssrc_seek(hlssrc *s, int64_t offset) {
    if (!s || offset < 0) return -1;
    if (is_cancelled(s)) {
        snprintf(s->err, sizeof(s->err), "cancelled");
        return -1;
    }

    if (offset == 0) {
        s->in_init = (s->init_len > 0);
        s->init_off = 0;
        s->cur_seg = 0;
        s->cur_off = 0;
        s->vpos = 0;
        return 0;
    }

    if (s->init_len > 0 && offset < s->init_len) {
        s->in_init = true;
        s->init_off = offset;
        s->cur_seg = 0;
        s->cur_off = 0;
        s->vpos = offset;
        return offset;
    }

    if (offset >= s->total_size) {
        s->in_init = false;
        s->cur_seg = s->seg_count;
        s->cur_off = 0;
        s->vpos = s->total_size;
        return s->total_size;
    }

    // Binary search for segment enclosing virtual `offset`.
    size_t lo = 0, hi = s->seg_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s->segs[mid].voff <= offset) lo = mid;
        else hi = mid;
    }
    size_t idx = lo;

    const uint8_t *buf = NULL;
    int64_t seg_len = 0;
    if (ensure_seg_loaded(s, idx, &buf, &seg_len) != 0)
        return -1;

    int64_t new_off = 0;
    if (s->init_len > 0) {
        // fMP4 segments must start at the moof boundary unless exact match in seg 0.
        if (idx == 0) {
            int64_t delta = offset - s->segs[0].voff;
            new_off = (delta >= 0 && delta < seg_len) ? delta : 0;
        } else {
            new_off = 0;
        }
    } else if (seg_len > 0) {
        if (idx == 0) {
            int64_t delta = offset - s->segs[0].voff;
            new_off = (delta >= 0 && delta < seg_len) ? delta : 0;
        } else if (s->segs[idx].vlen > 0) {
            double frac = (double)(offset - s->segs[idx].voff) / (double)s->segs[idx].vlen;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            int64_t rel = (int64_t)(frac * (double)seg_len);
            if (offset >= s->total_size - HS_TAIL_PROBE_WIN && seg_len > 65536 && rel < seg_len - 65536) {
                // Keep tail probes near end of the final segment so FFmpeg parses PCR duration quickly.
                rel = seg_len - 65536;
            }
            new_off = snap_ts_sync(buf, seg_len, rel);
        }
    }

    s->in_init = false;
    s->cur_seg = idx;
    s->cur_off = new_off;
    s->vpos = offset;
    return offset;
}

void hlssrc_cancel(hlssrc *s) {
    if (s) s->cancelled = true;
}

const char *hlssrc_last_err(const hlssrc *s) {
    return s ? s->err : "";
}

void hlssrc_close(hlssrc *s) {
    if (!s) return;
    if (s->curl) curl_easy_cleanup(s->curl);
    free(s->init_url);
    free(s->init_buf);
    for (size_t i = 0; i < s->key_count; i++) free(s->keys[i].url);
    free(s->keys);
    for (size_t i = 0; i < s->seg_count; i++) free(s->segs[i].url);
    free(s->segs);
    free(s->seg0_buf);
    free(s->cur_buf);
    free(s);
}
