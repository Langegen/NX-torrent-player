#ifndef HTTPSRC_H
#define HTTPSRC_H

// Random-access reads of one HTTP(S) resource, through curl Range requests.
//
// Exists because the Switch's FFmpeg is built without the https protocol
// (only http, tcp, tls...): mpv cannot open an https:// URL by itself. Reading
// it through curl also means the app's own CA bundle and proxy apply. The mpv
// side is in stream.c (stream_register_http).
//
// Reads are served from one buffered chunk; a read outside it fetches the next
// chunk starting there (one request per CHUNK, on a kept-alive connection).
// mpv's demuxer cache reads ahead on its own thread, so this does not need a
// prefetcher of its own.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct httpsrc httpsrc;

// Returned by httpsrc_open_ex when a URL turned out to be an HLS playlist
// (#EXTM3U or mpegurl Content-Type), so stream.c can hand it straight to hlssrc.
typedef struct {
    char *data;       // heap-allocated manifest body (caller frees), or NULL
    size_t len;
    char *eff_url;    // heap-allocated effective URL after redirects (caller frees)
    bool truncated;   // manifest exceeded the initial probe chunk -> re-fetch full URL
} httpsrc_hls_probe;

// Process-wide settings, read at every open. NULL/"" = unset.
// `ca_path`: CA bundle file for TLS verification (the Switch mounts none, the
// app ships romfs:/cacert.pem). Unset = curl's default store.
void httpsrc_set_ca(const char *ca_path);
// `proxy`: a curl proxy string ("http://host:3128", "socks5h://host:1080").
void httpsrc_set_proxy(const char *proxy);
// Optional HTTP headers ("Referer: ...", "User-Agent: ..."), up to 16 lines.
void httpsrc_set_headers(const char *const *headers, int count);

// Opens `url` (http:// or https://): one request to learn the size and that
// the server honours ranges. NULL on failure, with a reason in err.
httpsrc *httpsrc_open(const char *url, char *err, size_t errlen);
// The same, abandoned (NULL, "cancelled") as soon as *abort turns true --
// mpv has no cancel_fn for a stream until its open returns, and the open is
// a connect plus a first chunk, up to tens of seconds on a bad link. *abort
// is also polled by every later read, so it must outlive the httpsrc.
httpsrc *httpsrc_open_abortable(const char *url, const volatile bool *abort,
                                char *err, size_t errlen);

// Like httpsrc_open_abortable, except if `hls_out` is non-NULL and the
// server replies with an HLS playlist (#EXTM3U / mpegurl Content-Type), populates
// `*hls_out` (with `err[0] = 0`) and returns NULL so the caller can hand off
// without a duplicate probe request.
httpsrc *httpsrc_open_ex(const char *url, const volatile bool *abort,
                         httpsrc_hls_probe *hls_out,
                         char *err, size_t errlen);

// Adds `n` bytes to the shared httpsrc_bytes_total() counter (used by hlssrc).
void httpsrc_add_bytes(int64_t n);

// Total size in bytes.
int64_t httpsrc_size(const httpsrc *s);

// Reads up to `n` bytes at `pos`. Returns the count (>0), 0 at end of file, or
// -1 on error or after httpsrc_cancel.
int64_t httpsrc_read(httpsrc *s, int64_t pos, void *buf, int64_t n);

// Makes an in-flight read (and every later one) fail at once. Safe from any
// thread; the only call that is.
void httpsrc_cancel(httpsrc *s);

void httpsrc_close(httpsrc *s);

// Last error of a failed read, for the log / the info panel.
const char *httpsrc_last_err(const httpsrc *s);

// Bytes received by every httpsrc so far (for a download-speed readout).
int64_t httpsrc_bytes_total(void);

#endif
