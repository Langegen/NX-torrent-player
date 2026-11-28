#ifndef HLS_PROXY_H
#define HLS_PROXY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hls_proxy hls_proxy;

// Process-wide CA bundle and proxy configuration (read on requests).
// Empty/NULL unsets. Localhost upstream URLs automatically bypass the proxy.
void hls_proxy_set_ca(const char *ca_path);
void hls_proxy_set_proxy(const char *proxy);

// Starts a per-player loopback HTTP server (127.0.0.1:<ephemeral_port>) that
// proxies HTTP(S) requests through libcurl, rewrites HLS (.m3u8) playlists,
// forwards Range & custom HTTP headers, and (when built with
// HLS_PROXY_ENABLE_AES) decrypts AES-128-CBC HLS segments via mbedTLS.
// Returns NULL on failure with a message in `err`.
hls_proxy *hls_proxy_start(const char *const *headers, int header_count,
                           char *err, size_t errlen);

uint16_t hls_proxy_port(const hls_proxy *p);

// Formats a local proxy URL "http://127.0.0.1:<port>/p?u=<base64url>" for
// `target_url`. Returns 0 on success, -1 if buffer is too small or invalid.
int hls_proxy_make_url(const hls_proxy *p, const char *target_url,
                       char *out, size_t out_len);

// Immediately flags the proxy to abort any in-flight curl transfers. Call this
// BEFORE mpv_terminate_destroy() so mpv doesn't block waiting on a slow socket.
void hls_proxy_abort(hls_proxy *p);

// Flags every currently running hls_proxy instance to abort (e.g. app exit).
void hls_proxy_abort_all(void);

// Aborts, shuts down the listening socket, joins worker threads, and frees `p`.
void hls_proxy_stop(hls_proxy *p);

#ifdef __cplusplus
}
#endif

#endif // HLS_PROXY_H
