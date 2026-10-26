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

// Process-wide settings, read at every open. NULL/"" = unset.
// `ca_path`: CA bundle file for TLS verification (the Switch mounts none, the
// app ships romfs:/cacert.pem). Unset = curl's default store.
void httpsrc_set_ca(const char *ca_path);
// `proxy`: a curl proxy string ("http://host:3128", "socks5h://host:1080").
void httpsrc_set_proxy(const char *proxy);

// Opens `url` (http:// or https://): one request to learn the size and that
// the server honours ranges. NULL on failure, with a reason in err.
httpsrc *httpsrc_open(const char *url, char *err, size_t errlen);

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
