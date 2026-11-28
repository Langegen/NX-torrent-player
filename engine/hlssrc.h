#ifndef HLSSRC_H
#define HLSSRC_H

// In-process HLS (VOD .m3u8) stream reader over libcurl, with zero loopback
// sockets and zero extra threads.
//
// Exists for Nintendo Switch online playback: Horizon OS bsd:u stalls and
// deadlocks on local 127.0.0.1 loopback sockets (hls_proxy), and its Applet
// Mode limits bsd sessions. This reader parses master/media playlists, fetches
// optional init maps and AES-128 keys, decrypts segments (with
// HLS_PROXY_ENABLE_AES), caches segment 0 for instant rewind after FFmpeg's
// tail duration probe, and reuses one persistent keep-alive CURL handle on
// mpv's demuxer thread.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hlssrc hlssrc;

// Process-wide configuration, snapshotted when hlssrc_open_* runs.
void hlssrc_set_ca(const char *ca_path);
void hlssrc_set_proxy(const char *proxy);
void hlssrc_set_headers(const char *const *headers, int count);
void hlssrc_set_quality(int prefer_height, bool hide_4k);

// Opens an HLS stream from `url` (http:// or https://). Aborts immediately if
// `*abort` turns true (outlives the returned hlssrc).
hlssrc *hlssrc_open_abortable(const char *url, const volatile bool *abort,
                              char *err, size_t errlen);

// Opens an HLS stream from an already-fetched manifest body (`eff_url` is its
// post-redirect URL, used for resolving relative URIs).
hlssrc *hlssrc_open_with_manifest(const char *eff_url, const char *manifest,
                                  size_t manifest_len,
                                  const volatile bool *abort,
                                  char *err, size_t errlen);

// Virtual concatenated byte length (init map + segments), or -1 if unknown.
int64_t hlssrc_size(const hlssrc *s);

// Sequential read of up to `n` bytes from the current virtual cursor.
// Returns >0 on data, 0 on EOF, -1 on error or cancel.
int64_t hlssrc_read(hlssrc *s, void *buf, int64_t n);

// Seeks the virtual cursor to `offset`. Returns the resulting offset >= 0, or
// -1 on error / cancel / out of range.
int64_t hlssrc_seek(hlssrc *s, int64_t offset);

// Thread-safe cancellation: unblocks an in-flight curl transfer promptly.
void hlssrc_cancel(hlssrc *s);

void hlssrc_close(hlssrc *s);

const char *hlssrc_last_err(const hlssrc *s);

#ifdef __cplusplus
}
#endif

#endif // HLSSRC_H
