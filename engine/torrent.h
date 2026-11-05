#ifndef TORRENT_H
#define TORRENT_H
// The engine API moved to torrent_meta.h with the TorrentShopNX port; this
// name stays so the app and the tests keep their includes.
#include "torrent_meta.h"

// The old engine's name for the metadata cache folder (one
// "<info hash>.torrent" per fetched magnet).
static inline void torrent_set_meta_cache(const char *dir) {
    torrent_meta_cache_set_dir(dir);
}
#endif
