#pragma once

// All files the app writes live under one folder on the SD card instead of the
// root. The directory is created at startup by ensureAppDataDir() in main.cpp.
#ifdef __SWITCH__
#define APPDATA_DIR      "sdmc:/switch/NX-torrent-player"
#define APP_RES(p)       "romfs:/" p
#else
#define APPDATA_DIR      "./appdata"
#define APP_RES(p)       "resources/" p
#endif
#define APPDATA_LOG      APPDATA_DIR "/nx-torrent-player.log"
#define APPDATA_CACHE    APPDATA_DIR "/cache.bin"
// Folder the user drops .torrent files into (scanned for the main menu list).
#define APPDATA_TORRENTS APPDATA_DIR "/torrents"
// One magnet link (or bare info-hash) per line; listed alongside the .torrents.
// Also where "Add magnet" appends what the on-screen keyboard captures.
#define APPDATA_MAGNETS  APPDATA_TORRENTS "/magnet.txt"
// App-managed cache of a magnet's video files, so its file picker works without
// re-fetching metadata: "<hash>\t<index>\t<size>\t<name>" lines (index -1 = a
// resolved magnet with no video inside). Written by the background resolver.
#define APPDATA_MAGNET_FILES APPDATA_DIR "/magnet-files.tsv"
// Cached Stremio artwork, one .jpg per library item. Posters never change, so
// entries are kept forever and a hit never touches the network.
#define APPDATA_POSTERS  APPDATA_DIR "/posters"
// Subtitle files pulled from Stremio's subtitle addons, one per subtitle picked
// in the player. Kept so re-picking the same one costs nothing; they are a few
// tens of KB each.
#define APPDATA_SUBS     APPDATA_DIR "/subtitles"
// .torrent files fetched to play a Jackett result, one per link (named by a
// hash of it). Kept, not overwritten: a history entry points at its file to
// resume later, and a single shared temp file made every entry open whichever
// torrent was played last.
#define APPDATA_JACKETT  APPDATA_DIR "/jackett"
// TMDB answers, one .json per request (see tmdb.cpp): fresh copies replace
// the network, stale ones stand in for it when it is down.
#define APPDATA_TMDB     APPDATA_DIR "/tmdb"
// The library: bookmarks, per-title / per-episode progress (library.cpp).
#define APPDATA_LIBRARY  APPDATA_DIR "/library.json"
// Watch history and saved playback positions.
#define APPDATA_HISTORY  APPDATA_DIR "/history.json"

