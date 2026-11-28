#ifndef STREAM_H
#define STREAM_H

#include <mpv/client.h>

#include "torrentfs.h"

// Registers a custom mpv protocol so `loadfile torrent://...` reads through our
// callbacks, backed by the streaming torrent store `tfs`. mpv issues blocking
// read/seek calls; the torrentfs layer downloads on demand around the playhead.
//
// Returns 0 on success. `tfs` must outlive the mpv instance.
int stream_register(mpv_handle *mpv, torrentfs *tfs);

// Registers `nxhttps://` and `nxhttp://`: `loadfile nxhttps://host/path` plays
// https://host/path through curl with range requests (httpsrc.c), and
// transparently upgrades to in-process HLS (hlssrc.c) if the server returns
// an #EXTM3U playlist.
// `abort` (may be NULL) abandons an open in progress when it turns true: set
// it before mpv_terminate_destroy, which otherwise waits that open out. It
// must outlive the mpv instance.
int stream_register_http(mpv_handle *mpv, const volatile bool *abort);

// Registers `nxhls://` (-> https://) and `nxhlshttp://` (-> http://) through
// our in-process zero-loopback HLS VOD reader (hlssrc.c).
int stream_register_hls(mpv_handle *mpv, const volatile bool *abort);

// Why the last nxhttps:// open failed ("" if it did not), for the player to
// show: mpv itself only reports "loading failed".
extern char stream_http_err[160];

#endif
