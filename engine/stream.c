#include "stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpv/stream_cb.h>

#include "httpsrc.h"
#include "hlssrc.h"
#include "torrentfs.h"

// Per-open-stream state: the shared torrent store plus this stream's cursor.
typedef struct {
    torrentfs *tfs;
    int64_t pos;
    volatile bool cancelled;   // cancel_cb: this stream only
} stream_ctx;

static int64_t read_cb(void *cookie, char *buf, uint64_t nbytes) {
    stream_ctx *s = cookie;
    int64_t n = torrentfs_read_cancel(s->tfs, s->pos, buf, (int64_t)nbytes,
                                      &s->cancelled);
    if (n > 0) s->pos += n;
    return n;  // 0 = EOF, -1 = error/cancel
}

static int64_t seek_cb(void *cookie, int64_t offset) {
    stream_ctx *s = cookie;
    s->pos = offset;
    torrentfs_set_playhead(s->tfs, offset);  // reprioritize downloads here
    return offset;
}

static int64_t size_cb(void *cookie) {
    stream_ctx *s = cookie;
    int64_t sz = torrentfs_size(s->tfs);
    return sz > 0 ? sz : MPV_ERROR_UNSUPPORTED;
}

static void close_cb(void *cookie) {
    free(cookie);  // the torrentfs itself is owned by main, not the stream
}

// mpv aborting this stream (stop, quit): a read parked on a missing piece
// returns instead of waiting for it. Per stream, not torrentfs_cancel_reader,
// which would fail the next stream's reads too.
static void cancel_cb(void *cookie) {
    ((stream_ctx *)cookie)->cancelled = true;
}

static int open_cb(void *user_data, char *uri, mpv_stream_cb_info *info) {
    (void)uri;
    stream_ctx *s = calloc(1, sizeof(*s));
    if (!s) return MPV_ERROR_LOADING_FAILED;
    s->tfs = user_data;
    s->pos = 0;

    info->cookie = s;
    info->read_fn = read_cb;
    info->seek_fn = seek_cb;
    info->size_fn = size_cb;
    info->close_fn = close_cb;
    info->cancel_fn = cancel_cb;
    return 0;
}

int stream_register(mpv_handle *mpv, torrentfs *tfs) {
    return mpv_stream_cb_add_ro(mpv, "torrent", tfs, open_cb);
}

//---------------------------------------------------------------------------
// nxhttps://, nxhttp://, nxhls://, nxhlshttp:// -- read through curl
// (httpsrc.c / hlssrc.c), with zero loopback sockets on Switch.
//---------------------------------------------------------------------------

typedef struct {
    httpsrc *src;
    hlssrc *hls;
    int64_t pos;
} http_ctx;

static int64_t http_read_cb(void *cookie, char *buf, uint64_t nbytes) {
    http_ctx *h = cookie;
    int64_t n = h->hls ? hlssrc_read(h->hls, buf, (int64_t)nbytes)
                       : httpsrc_read(h->src, h->pos, buf, (int64_t)nbytes);
    if (n > 0) h->pos += n;
    return n;
}

static int64_t http_seek_cb(void *cookie, int64_t offset) {
    http_ctx *h = cookie;
    if (h->hls) {
        int64_t r = hlssrc_seek(h->hls, offset);
        if (r < 0) return MPV_ERROR_GENERIC;
        h->pos = r;
        return r;
    }
    if (offset < 0 || offset > httpsrc_size(h->src)) return MPV_ERROR_GENERIC;
    h->pos = offset;  // the data is fetched by the next read
    return offset;
}

static int64_t http_size_cb(void *cookie) {
    http_ctx *h = cookie;
    return h->hls ? hlssrc_size(h->hls) : httpsrc_size(h->src);
}

static void http_close_cb(void *cookie) {
    http_ctx *h = cookie;
    if (h->hls) hlssrc_close(h->hls);
    if (h->src) httpsrc_close(h->src);
    free(h);
}

static void http_cancel_cb(void *cookie) {
    http_ctx *h = cookie;
    if (h->hls) hlssrc_cancel(h->hls);
    if (h->src) httpsrc_cancel(h->src);
}

static char *map_custom_url(const char *uri) {
    const char *scheme = NULL;
    const char *rest = NULL;
    if (strncmp(uri, "nxhttps://", 10) == 0) {
        scheme = "https://";
        rest = uri + 10;
    } else if (strncmp(uri, "nxhttp://", 9) == 0) {
        scheme = "http://";
        rest = uri + 9;
    } else if (strncmp(uri, "nxhls://", 8) == 0) {
        scheme = "https://";
        rest = uri + 8;
    } else if (strncmp(uri, "nxhlshttp://", 12) == 0) {
        scheme = "http://";
        rest = uri + 12;
    } else {
        return NULL;
    }
    size_t len = strlen(scheme) + strlen(rest) + 1;
    char *url = malloc(len);
    if (url) snprintf(url, len, "%s%s", scheme, rest);
    return url;
}

static int finish_http_open(mpv_stream_cb_info *info, httpsrc *src, hlssrc *hls) {
    http_ctx *h = calloc(1, sizeof(*h));
    if (!h) {
        if (hls) hlssrc_close(hls);
        if (src) httpsrc_close(src);
        return MPV_ERROR_LOADING_FAILED;
    }
    h->src = src;
    h->hls = hls;

    info->cookie = h;
    info->read_fn = http_read_cb;
    info->seek_fn = http_seek_cb;
    info->size_fn = http_size_cb;
    info->close_fn = http_close_cb;
    info->cancel_fn = http_cancel_cb;
    return 0;
}

static int http_open_cb(void *user_data, char *uri, mpv_stream_cb_info *info) {
    const volatile bool *abort = user_data;
    stream_http_err[0] = 0;   // a previous open's reason is not this one's
    char *url = map_custom_url(uri);
    if (!url) return MPV_ERROR_LOADING_FAILED;

    char err[160] = {0};
    httpsrc_hls_probe probe;
    httpsrc *src = httpsrc_open_ex(url, abort, &probe, err, sizeof(err));
    if (src) {
        free(url);
        return finish_http_open(info, src, NULL);
    }

    if (probe.eff_url) {
        hlssrc *hls = (probe.truncated || !probe.data)
                          ? hlssrc_open_abortable(probe.eff_url, abort, err, sizeof(err))
                          : hlssrc_open_with_manifest(probe.eff_url, probe.data, probe.len,
                                                      abort, err, sizeof(err));
        free(probe.data);
        free(probe.eff_url);
        free(url);
        if (!hls) {
            snprintf(stream_http_err, sizeof(stream_http_err), "%s", err);
            return MPV_ERROR_LOADING_FAILED;
        }
        return finish_http_open(info, NULL, hls);
    }

    free(probe.data);
    free(url);
    snprintf(stream_http_err, sizeof(stream_http_err), "%s", err);
    return MPV_ERROR_LOADING_FAILED;
}

static int hls_open_cb(void *user_data, char *uri, mpv_stream_cb_info *info) {
    const volatile bool *abort = user_data;
    stream_http_err[0] = 0;
    char *url = map_custom_url(uri);
    if (!url) return MPV_ERROR_LOADING_FAILED;

    char err[160] = {0};
    hlssrc *hls = hlssrc_open_abortable(url, abort, err, sizeof(err));
    free(url);
    if (!hls) {
        snprintf(stream_http_err, sizeof(stream_http_err), "%s", err);
        return MPV_ERROR_LOADING_FAILED;
    }
    return finish_http_open(info, NULL, hls);
}

char stream_http_err[160];

int stream_register_http(mpv_handle *mpv, const volatile bool *abort) {
    stream_http_err[0] = 0;
    int r1 = mpv_stream_cb_add_ro(mpv, "nxhttps", (void *)abort, http_open_cb);
    int r2 = mpv_stream_cb_add_ro(mpv, "nxhttp", (void *)abort, http_open_cb);
    return r1 != 0 ? r1 : r2;
}

int stream_register_hls(mpv_handle *mpv, const volatile bool *abort) {
    int r1 = mpv_stream_cb_add_ro(mpv, "nxhls", (void *)abort, hls_open_cb);
    int r2 = mpv_stream_cb_add_ro(mpv, "nxhlshttp", (void *)abort, hls_open_cb);
    return r1 != 0 ? r1 : r2;
}
