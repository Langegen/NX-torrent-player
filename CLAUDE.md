# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

NX Torrent Player — Nintendo Switch homebrew that *streams* torrents: an
on-device BitTorrent engine feeds mpv through a custom devoptab, so playback
starts on the first pieces instead of waiting for a download. The UI is borealis
(Horizon style), and a Stremio account can be signed in to browse its library,
addons and sources.

## Build

devkitPro is not installed locally: build through the Docker image, with the
build tree kept in a named volume (`st_build`) so it survives between runs.

```sh
docker run --rm -v st_build:/build -v "$PWD":/project -w /build \
    devkitpro/devkita64:latest \
    sh -c "cmake --build . --target NX-torrent-player.nro"

# copy the result out of the volume
docker run --rm -v st_build:/build -v "$PWD":/project \
    devkitpro/devkita64:latest sh -c "cp /build/NX-torrent-player.nro /project/"
```

Adding or removing a source file needs a configure pass (the sources are
globbed): `cmake .` in the same container, before the build. A first build in a
fresh volume needs `cmake /project` instead.

Produces `NX-torrent-player.nro`, which runs from the Homebrew Menu on a CFW
Switch. On Windows, prefix docker commands with `MSYS_NO_PATHCONV=1` or Git Bash
rewrites the container paths.

## Architecture

- `app/` — the UI (C++, borealis): `main` (browser/tabs), `player` (mpv view +
  loading screen + the Lampa-style control panel + the episode list, which is
  the video files of a season pack; torrents, direct http(s) links,
  TorrServer), `player_widgets` (the panel's nanovg buttons, timeline and
  shades), `stremio`
  (API, library, artwork), `browse` (the Stremio drill-down), `settings`,
  `config` (config.json), `http` (curl + proxy), `torrserver`, `i18n` (+
  `i18n_ru.cpp`, one table per language), `home` (the TMDB home tab), `card`
  (film/series card), `tmdb` (client + SD cache), `content` (the model the
  catalog screens share), `widgets` (strips, poster/wide cards, chips, shades),
  `sources` (the release list for a film/episode: Jackett + Stremio addons),
  `release` (release-name parsing, ranking, episode-in-pack matching),
  `torrent_play` (open a Jackett result or magnet and pick the file to play),
  `library` (bookmarks + per-title/per-episode progress, library.json),
  `bookmarks` (the Bookmarks tab), `picture` (handheld-only brightness /
  contrast / gamma / saturation through mpv's equalizer, and the calibration
  screen with the test patterns in `assets/patterns`, made by
  `tools/make_patterns.py`)
- `engine/` — the torrent engine (C), ported from TorrentShopNX (same
  lineage; the `TSNX_*` names are kept so the files stay diffable):
  `torrentfs` (block-level scheduler: several peers per piece, hedged
  requests, endgame, slow-peer isolation, PEX, calm mode; RAM window; the
  streaming read), `bt_peer` (wire protocol, BEP 9), `torrent_meta` (.torrent,
  trackers, the pipelined magnet fetch, the metadata + peer caches),
  `udp_tracker`, `dhtclient`/`dht` (one background DHT with a node cache),
  `listen` + `upnp` (incoming peers), `utp_nb` (µTP), `engine_services` (the
  process-wide part, started in `main()`), `magnet`, `stream` (the mpv
  `stream_cb` protocols: `torrent://` and `nxhttps://`), `httpsrc` (https read
  through curl range requests), plus vendored libutp (`utp*.cpp`).
  `torrent.h`/`peer.h` are shims over `torrent_meta.h`/`bt_peer.h`;
  `engine_compat.h` is force-included on the Switch
- `include/` — libutp headers, paired with `engine/utp*.cpp`
- `assets/` — app icon, logos, list icons (copied into the nro's romfs)
- `library/borealis/` — vendored UI framework, locally patched: see its
  `VENDORED.md` (upstream commit + the ENOBUFS patch, re-apply if re-cloned)
- `pctest/` — builds the engine for a PC, without the Switch or the UI;
  `test_stream` streams from mock seeders on 127.0.0.x the way mpv reads

The entry point is `int main()` in `app/main.cpp`.

## Constraints worth knowing before changing things

- **nanovg is batched**: every `nvg*` call flushes at `nvgEndFrame`, *after*
  `MpvView::draw()` runs — mpv renders immediately. Drawing anything opaque in
  the player without splitting the frame (`nvgEndFrame` / `nvgBeginFrame` around
  the mpv render) paints over the video. That was the "black video, working
  audio" bug.
- **libnx caps concurrent *blocking* socket calls** (`num_bsd_sessions`, 16
  here). The peer loop is a single `poll()`; the blocking ones are tracker
  announces (3), metadata workers (5), the DHT, UPnP and curl — this is *not*
  what limits peer count (see the socket-pool patch in
  `library/borealis/VENDORED.md`).
- **`torrentfs_read` must never return 0 before EOF**: mpv takes it as the end
  of the file and the player closes (TorrentShopNX's stall timeout did that).
- The PC app's `compat/sys/socket.h` converts `SO_RCVTIMEO` timevals to
  Winsock's milliseconds: unconverted, a 250 ms timeout became "forever".
- **FAT32 caps a file at 4 GB**: the piece cache is chunked for this reason.
- The Switch mounts **no CA store**, so the app ships one: `assets/cacert.pem`
  is copied into the romfs and `app/http.cpp` points `CURLOPT_CAINFO` at
  `romfs:/cacert.pem` with verification on. curl here is 7.69/mbedTLS, so
  `CURLOPT_CAINFO_BLOB` does not exist — the bundle has to be a real file. A
  rejected chain on device is usually **the console clock**, not an attacker.
  The tracker announces in `engine/torrent.c` stay unverified on purpose.
- `ScrollingFrame::setContentView()` detaches the view and forces its width, so
  **margins on a content view are ignored** — put the inset on the rows.
- A **GONE box's children stay focusable** (`Box::getDefaultFocus()` recurses
  without checking the box's own visibility), and `giveFocus()` on a
  non-focusable view is a **no-op** — which strands `Application::currentFocus`
  on whatever it was, including freed views. See `setSubtreeFocusable()` and the
  focus parking in `StremioTab::onAuthenticated`.
- The **player's control panel does not use borealis focus**: `MpvView` keeps
  the focus and moves its own cursor (`Btn` / `Zone`), every D-pad direction is
  an action on it, and the `BUTTON_NAV_*` twins are swallowed. Every input
  goes through `MpvView::command(Cmd)`, and the whole player works with the
  arrows + OK + Back alone (a remote): on the console a **keyboard's or USB
  remote's arrows arrive only as `BUTTON_NAV_*`** (Enter/Esc as A/B), so a
  NAV press with no D-pad twin held and no stick pushed is a command
  (`navFromKeys`). Back leaves only on a second press; OK ×3 unlocks. Its icons are
  drawn with nanovg (`player_widgets`): the Material glyphs a player needs sit
  in U+E000-E152, which the Nintendo shared font claims first.
- Magnet metadata is cached in `APPDATA_META` (`torrent_set_meta_cache`): a
  magnet opened again loads its `.torrent` from there, with no peers handed
  on — the engine's own discovery finds them.
- The Switch outputs stereo: mpv is forced to `audio-channels=stereo` or 5.1
  dialogue (centre channel) is lost.
- The portlibs **FFmpeg has no `https` protocol** (only file, http, ftp, tcp,
  udp, rtmp, tls, httpproxy; `tls` is libnx's ssl service). mpv opens `http://`
  and HLS over it itself; an `https://` URL is played as `nxhttps://` through
  `engine/httpsrc.c` (curl, our CA bundle, Range requests). HLS over https is
  therefore not supported yet. No `crypto` protocol either (AES-128 HLS).
- **Space Grotesk (the primary UI font) has no Cyrillic**: in Russian the app
  turns it off before `Application::init()` (`SwitchFontLoader::useBundledFont`,
  a local borealis patch) so the UI is the system font throughout.

## Debugging on device

The log is **off by default** (Options → Log file) because it is unbuffered and
the engine dumps a `[stats]` line every 2s. Turn it on, restart, reproduce.

ZR in the player opens a live panel of every engine counter. `fail: N sock/local`
climbing with `last err: socket(): No buffer space available` means the socket
buffer pool is exhausted — see `library/borealis/VENDORED.md` (and lower
`MAX_SESS` in `engine/torrentfs.c`). `pool` shows addresses ready to dial,
dropped because the pool was full, and received over PEX; `rotated out` the
sessions closed by peer turnover; `hedged` / `wasted` what re-requesting the
hot piece's tail from a second peer cost.

## Measuring the engine on a PC

`pctest/main.c` runs `engine/` without the Switch or the UI; `sh
pctest/build.sh` builds it on Linux or in the devkitPro MSYS2 shell (mingw64).
`enginetest <file.torrent|magnet> 120 play` models the player (mpv's head/tail
probe, then a 10 Mbit/s read with 60 s of read-ahead, the backlog fed to the
engine) and ends with a `SUMMARY` line: startup time, stalls, live peers, pool.
Network noise between runs is large — compare two builds by alternating them on
the same torrents, never against an older run.

For a crash, Atmosphère writes a report to `sdmc:/atmosphere/crash_reports/`.
The ELF is linked at vaddr 0, so the offsets map directly:

```sh
aarch64-none-elf-addr2line -Cfe /build/NX-torrent-player.elf <offset>
```
