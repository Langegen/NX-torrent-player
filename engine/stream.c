#include "stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpv/stream_cb.h>

#include "httpsrc.h"
#include "torrentfs.h"

// Per-open-stream state: the shared torrent store plus this stream's cursor.
typedef struct {
    torrentfs *tfs;
    int64_t pos;
} stream_ctx;

static int64_t read_cb(void *cookie, char *buf, uint64_t nbytes) {
    stream_ctx *s = cookie;
    int64_t n = torrentfs_read(s->tfs, s->pos, buf, (int64_t)nbytes);
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
    return 0;
}

int stream_register(mpv_handle *mpv, torrentfs *tfs) {
    return mpv_stream_cb_add_ro(mpv, "torrent", tfs, open_cb);
}

//---------------------------------------------------------------------------
// nxhttps:// -- an https:// URL read through curl (httpsrc.c), because this
// FFmpeg has no https protocol of its own.
//---------------------------------------------------------------------------

typedef struct {
    httpsrc *src;
    int64_t pos;
} http_ctx;

static int64_t http_read_cb(void *cookie, char *buf, uint64_t nbytes) {
    http_ctx *h = cookie;
    int64_t n = httpsrc_read(h->src, h->pos, buf, (int64_t)nbytes);
    if (n > 0) h->pos += n;
    return n;
}

static int64_t http_seek_cb(void *cookie, int64_t offset) {
    http_ctx *h = cookie;
    if (offset < 0 || offset > httpsrc_size(h->src)) return MPV_ERROR_GENERIC;
    h->pos = offset;  // the data is fetched by the next read
    return offset;
}

static int64_t http_size_cb(void *cookie) {
    return httpsrc_size(((http_ctx *)cookie)->src);
}

static void http_close_cb(void *cookie) {
    http_ctx *h = cookie;
    httpsrc_close(h->src);
    free(h);
}

static void http_cancel_cb(void *cookie) {
    httpsrc_cancel(((http_ctx *)cookie)->src);
}

static int http_open_cb(void *user_data, char *uri, mpv_stream_cb_info *info) {
    (void)user_data;
    // "nxhttps://host/path" -> "https://host/path"
    static const char prefix[] = "nxhttps://";
    if (strncmp(uri, prefix, sizeof(prefix) - 1) != 0) return MPV_ERROR_LOADING_FAILED;
    size_t rest = strlen(uri) - (sizeof(prefix) - 1);
    char *url = malloc(rest + sizeof("https://"));
    if (!url) return MPV_ERROR_LOADING_FAILED;
    snprintf(url, rest + sizeof("https://"), "https://%s", uri + sizeof(prefix) - 1);

    char err[160] = {0};
    httpsrc *src = httpsrc_open(url, err, sizeof(err));
    free(url);
    if (!src) {
        snprintf(stream_http_err, sizeof(stream_http_err), "%s", err);
        return MPV_ERROR_LOADING_FAILED;
    }
    http_ctx *h = calloc(1, sizeof(*h));
    if (!h) { httpsrc_close(src); return MPV_ERROR_LOADING_FAILED; }
    h->src = src;

    info->cookie = h;
    info->read_fn = http_read_cb;
    info->seek_fn = http_seek_cb;
    info->size_fn = http_size_cb;
    info->close_fn = http_close_cb;
    info->cancel_fn = http_cancel_cb;
    return 0;
}

char stream_http_err[160];

int stream_register_http(mpv_handle *mpv) {
    stream_http_err[0] = 0;
    return mpv_stream_cb_add_ro(mpv, "nxhttps", NULL, http_open_cb);
}
