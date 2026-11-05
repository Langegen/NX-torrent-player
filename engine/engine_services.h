#ifndef ENGINE_SERVICES_H
#define ENGINE_SERVICES_H

// The process-wide half of the torrent engine: what TorrentShopNX's engine.c
// did, minus its multi-torrent registry (the player opens one torrentfs at a
// time). Started once at app launch so the DHT routing table is warm and the
// listen port is open by the time the user picks a release.

#include <stdbool.h>
#include <stdint.h>

#include "torrentfs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the DHT service (bootstrapped from its node cache), the incoming-peer
// listener + UPnP mapping, and the uTP socket. `log_path` non-NULL also opens
// the engine's own async log there. Safe to call once; later calls no-op.
void engine_services_start(const char *log_path);

// Stop everything started above (saves the DHT node cache). App exit only.
void engine_services_stop(void);

// Incoming peers are routed to a registered torrent by its info-hash. The
// player registers right after torrentfs_open and unregisters BEFORE
// torrentfs_close: the listener hands sockets over under the same lock.
void engine_register_torrent(torrentfs *t);
void engine_unregister_torrent(torrentfs *t);

#ifdef __cplusplus
}
#endif

#endif
