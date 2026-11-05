#include "engine_services.h"

#include <signal.h>
#include <string.h>

#include <switch.h>

#include "dhtclient.h"
#include "engine_log.h"
#include "listen.h"
#include "torrent_meta.h"
#include "upnp.h"
#include "utp_nb.h"

// The default BitTorrent range is filtered by some ISPs; TorrentShopNX uses
// 6882 for the same reason. The listener falls back to an ephemeral port.
#define LISTEN_PORT 6882
#define MAX_REG     4

static bool s_started;
static Mutex s_reg_lock;
static torrentfs *s_reg[MAX_REG];

static void dht_log_cb(const char *msg) { engine_log(ENGINE_LOG_INFO, "[dht] %s", msg); }
static void meta_log_cb(const char *msg) { engine_log(ENGINE_LOG_INFO, "[meta] %s", msg); }

// Listener thread: an accepted peer (handshake read) for info_hash. The push
// happens under s_reg_lock, so the torrent cannot be closed in between.
static bool route_incoming(const uint8_t info_hash[20], int fd,
                           const uint8_t hs68[68]) {
    bool ok = false;
    mutexLock(&s_reg_lock);
    for (int i = 0; i < MAX_REG && !ok; i++) {
        torrentfs *t = s_reg[i];
        if (t && memcmp(torrentfs_info_hash(t), info_hash, 20) == 0) {
            torrentfs_incoming_push(t, fd, hs68);   // takes ownership of fd
            ok = true;
        }
    }
    mutexUnlock(&s_reg_lock);
    return ok;
}

void engine_services_start(const char *log_path) {
    if (s_started) return;
    s_started = true;
#if !defined(__SWITCH__) && defined(SIGPIPE)
    // A write to a socket the peer just reset raises SIGPIPE on a PC; the
    // console's bsd service never delivers it (and MinGW has no SIGPIPE).
    signal(SIGPIPE, SIG_IGN);
#endif
    mutexInit(&s_reg_lock);
    if (log_path) {
        engine_log_init(log_path);
        engine_log(ENGINE_LOG_INFO, "engine services start");
    }
    dht_set_log(dht_log_cb);
    torrent_set_log(meta_log_cb);
    // The lazily-initialised mutexes, before any thread can race on them.
    torrent_announce_mutex_init();
    dht_bg_init_early();
    utp_nb_init();

    if (torrent_listener_start(LISTEN_PORT, route_incoming) == 0) {
        torrent_set_announce_port(torrent_listener_port());
        upnp_start(torrent_listener_port());
    } else {
        engine_log(ENGINE_LOG_WARN, "[engine] listener failed: no incoming peers");
    }
    dht_warmup();
}

void engine_services_stop(void) {
    if (!s_started) return;
    torrent_listener_stop();
    upnp_stop();
    dht_stop();   // saves the node cache for the next launch
    utp_nb_exit();
    engine_log_close();
    s_started = false;
}

void engine_register_torrent(torrentfs *t) {
    if (!t || !s_started) return;
    mutexLock(&s_reg_lock);
    for (int i = 0; i < MAX_REG; i++)
        if (!s_reg[i]) { s_reg[i] = t; break; }
    mutexUnlock(&s_reg_lock);
}

void engine_unregister_torrent(torrentfs *t) {
    if (!t || !s_started) return;
    mutexLock(&s_reg_lock);
    for (int i = 0; i < MAX_REG; i++)
        if (s_reg[i] == t) s_reg[i] = NULL;
    mutexUnlock(&s_reg_lock);
}
