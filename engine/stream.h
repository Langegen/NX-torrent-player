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

// Registers `nxhttps://`: `loadfile nxhttps://host/path` plays
// https://host/path, read through curl with range requests (httpsrc.c). This
// FFmpeg build has no https protocol, so mpv cannot open such a URL itself;
// plain http:// (and HLS over it) needs no help.
int stream_register_http(mpv_handle *mpv);

// Why the last nxhttps:// open failed ("" if it did not), for the player to
// show: mpv itself only reports "loading failed".
extern char stream_http_err[160];

#endif
