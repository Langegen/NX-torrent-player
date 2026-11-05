#ifndef NXTP_ENGINE_APP_PATHS_H
#define NXTP_ENGINE_APP_PATHS_H

// Where the engine keeps its own files. The engine was ported from
// TorrentShopNX, whose sources name these paths TSNX_*; the names are kept so
// the ported files stay diffable against their origin, the values are ours
// (the same folder as app/appdata.hpp).

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define TSNX_MKDIR(p) _mkdir(p)
#else
#define TSNX_MKDIR(p) mkdir((p), 0777)
#endif

#ifdef __SWITCH__
#define TSNX_BASE_DIR "sdmc:/switch/NX-torrent-player"
#else
#define TSNX_BASE_DIR "./appdata"
#endif

#define TSNX_CACHE_DIR      TSNX_BASE_DIR "/cache"
// Fetched magnet metadata (info dicts), one file per info-hash.
#define TSNX_CACHE_META     TSNX_CACHE_DIR "/meta"
#define TSNX_CACHE_DHT      TSNX_CACHE_DIR "/dht"
// DHT routing-table nodes, so the next start does not bootstrap from nothing.
#define TSNX_DHT_CACHE_FILE TSNX_CACHE_DHT "/dht_cache.bin"
#define TSNX_ENGINE_LOG     TSNX_BASE_DIR "/engine.log"

// mkdir -p, tolerating "sdmc:/" and backslashes.
static inline void tsnx_ensure_dir(const char *dir_path) {
    if (!dir_path || !*dir_path) return;
    char dir[1024];
    size_t len = strlen(dir_path);
    if (len >= sizeof(dir)) return;
    memcpy(dir, dir_path, len + 1);
    for (size_t i = 0; i < len; ++i)
        if (dir[i] == '\\') dir[i] = '/';
    while (len > 0 && dir[len - 1] == '/') dir[--len] = '\0';
    if (len == 0) return;

    char cur[1024];
    size_t pos = 0;
    cur[0] = '\0';
    if (strncmp(dir, "sdmc:/", 6) == 0) {
        snprintf(cur, sizeof(cur), "sdmc:");
        pos = 6;
    }
    while (pos <= len) {
        size_t next = pos;
        while (dir[next] && dir[next] != '/') next++;
        if (next > pos) {
            size_t clen = strlen(cur);
            if (clen && cur[clen - 1] != '/') cur[clen++] = '/';
            if (clen + (next - pos) + 1 <= sizeof(cur)) {
                memcpy(cur + clen, dir + pos, next - pos);
                cur[clen + (next - pos)] = '\0';
                TSNX_MKDIR(cur);
            }
        }
        if (!dir[next]) break;
        pos = next + 1;
    }
}

// mkdir -p of the directory holding `path`.
static inline void tsnx_ensure_parent_dirs(const char *path) {
    if (!path || !*path) return;
    char dir[1024];
    size_t len = strlen(path);
    if (len >= sizeof(dir)) return;
    memcpy(dir, path, len + 1);
    for (size_t i = 0; i < len; ++i)
        if (dir[i] == '\\') dir[i] = '/';
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';
    tsnx_ensure_dir(dir);
}

#endif
