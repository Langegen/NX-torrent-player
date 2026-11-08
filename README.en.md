<div align="center">
  <img src="assets/NX-torrent-player-banner.png" alt="NX Torrent Player" width="640">

  # NX Torrent Player

  **A Nintendo Switch video player that streams torrents instead of downloading them**

  *TMDB catalog, Stremio, Jackett, TorrServer, bookmarks and watch history*

  [![Release](https://img.shields.io/github/v/release/Langegen/NX-torrent-player)](https://github.com/Langegen/NX-torrent-player/releases)
  [![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
  [![Platform](https://img.shields.io/badge/Platform-Nintendo_Switch-red.svg)](https://www.nintendo.com/)
  [![Upstream](https://img.shields.io/badge/Forked_From-shodowlo%2FNX--torrent--player-green.svg)](https://github.com/shodowlo/NX-torrent-player)

  [Русский](README.md) • **English** • [What's new](CHANGELOG.en.md)
</div>

---

> [!NOTE]
> ### ℹ️ About this fork, and credit to the original author
> This project continues [**NX-torrent-player**](https://github.com/shodowlo/NX-torrent-player), created by [**shodow** (@shodowlo)](https://github.com/shodowlo).
>
> Many thanks to **shodow** for the idea, the built-in BitTorrent streaming engine and the Switch-style interface. The fork is developed by [**Langegen**](https://github.com/Langegen): a TMDB catalog, Jackett, bookmarks and history, a new torrent engine, a Lampa-style player and more — with care for the original code.

---

## 📖 Overview

**NX Torrent Player** is a homebrew video player for a Nintendo Switch running custom firmware (Atmosphère). It does not wait for a file to download: the built-in BitTorrent engine fetches pieces in the order the player reads them, so a video starts within seconds.

Films and shows can be picked from a TMDB catalog, Lampa-style, with releases found through **Jackett** and **Stremio** addons. A Stremio library, local `.torrent` files and magnet links, direct `http(s)` links and **TorrServer** all work too. The `borealis` interface looks like the Switch's own system and supports the touch screen; the player can be driven with a remote.

---

## ✨ Features

### 🏠 Catalog and library
- **Lampa-style home** — a large banner, Continue watching, and rows of trending, popular and top-rated films and shows from TMDB in your language. Separate Movies, Series and Anime tabs, and search.
- **Title card** — backdrop, rating, genres, overview, cast, seasons and episodes with progress, "More like this". TMDB answers are cached on the SD card, so the home screen opens instantly and offline.
- **TMDB where it is blocked** — a TMDB proxy is on by default for the Russian UI (**Options → Network → TMDB**), and a built-in API key is included. You can run your own proxy on Cloudflare Workers: see [`tools/tmdb-worker`](tools/tmdb-worker/README.md).
- **Bookmarks** — Watching, Plan to watch, Completed and Dropped, on the **Bookmarks** tab. Progress is kept per film and per episode and tied to the title, not the file, so it survives switching releases. The library exports and imports as one file.
- **History** — posters of what you watched, opening the card; files without a card sit in an "Other files" row. "Continue S1 E2 from 22:10" resumes from the same release.

### 🎯 Releases
- **One release list** — Jackett (searched by the local and the original title, Jacred servers supported) and your Stremio addons together.
- **Release names, understood** — quality, source, codec, HDR / 10-bit, voice-over (including rutracker marks: `| D`, `| P`, `L1`…), season and episode. Releases with Russian audio rank first, dubbing highest; the one that plays best on a Switch comes first, marked "best pick".
- **Season packs** — open straight on the right episode; the chosen voice-over is remembered per show.
- **Filters** — by quality, X changes the order; 4K releases are hidden by default (the Switch outputs at most 1080p) and can be shown in **Options → Stremio**.

### 🎬 Player
- **Lampa-style control panel** — title, episode, clock and end time at the top; a timeline and buttons at the bottom: previous / next episode, rewind, pause, episodes, audio, subtitles, settings, lock.
- **Season pack episodes** — an episode list (ZL) with readable names ("Episode 16 · April in Quahog") and Watched / where-you-stopped marks; the next episode starts on its own.
- **Remote-friendly** — the whole player works with the arrows, OK and Back: a keyboard or a USB remote in the dock will do. The sticks act as the D-pad, and on the timeline as smooth analog scrubbing.
- **Settings in place** — volume, speed, video size (fill, stretch, zoom), subtitle size and delay change with the arrows right in the panel; external subtitles from Stremio addons.
- **Handheld picture** — brightness, contrast, gamma and saturation, with a calibration screen and test patterns (**Options → Playback**). Docked, the TV owns the picture.
- **Audio** — a proper 5.1-to-stereo downmix (dialogue is not lost) and a boost for quiet audio in handheld.
- **4K** — hardware decoding and lighter scaling for video above 1080p.

### ⚡ Torrent engine
- **TorrentShopNX engine** — several peers per piece, re-requesting the hot piece from a second peer, endgame, peer exchange (PEX), incoming connections (UPnP), uTP, a persistent background DHT.
- **Fast magnets** — trackers and DHT are asked at once, metadata and peers are cached: opening a magnet again does not wait for the network.
- **Adaptive start** — playback begins as soon as the rate covers the bitrate; the buffer size is configurable (**Options → Streaming → Buffer size**).
- **Stats panel (ZR)** — peers, speed, the address pool, the buffer and the startup stages, live.

### 🌐 Other sources and network
- **Stremio** — sign in, library, catalogs and addons.
- **Local torrents and magnets** — from a folder on the SD card or the on-screen keyboard.
- **Direct links** — `http(s)` addon sources (debrid services and the like) with seeking; `https` goes through the app's own curl with certificate checks.
- **TorrServer** — set its address and every torrent is downloaded by the server, while the console gets a plain video stream.
- **Proxy** — HTTP or SOCKS for catalogs, addons, posters and `https` video; local-network servers are always reached directly.

### 🛡️ Reliability
- Saved data (settings, library, history) is written atomically with a backup — a power cut does not corrupt it.
- Stuck requests, a slow magnet and quitting the app are cut short at once, without hangs.
- Updates are downloaded and checked in the app.

### 🌍 Languages
Russian, English and French. The console's language is used by default; change it in **Options → General → Language**.

---

## 📸 Screenshots

<table>
  <tr>
    <td width="50%"><img src="assets/image1.webp" alt="Local tab" width="100%"><br><em>Local torrents and magnet links</em></td>
    <td width="50%"><img src="assets/image2.webp" alt="Stremio library" width="100%"><br><em>Stremio catalog and library</em></td>
  </tr>
  <tr>
    <td width="50%"><img src="assets/image4.webp" alt="Episode picker" width="100%"><br><em>Picking a file or episode from a release</em></td>
    <td width="50%"><img src="assets/image3.webp" alt="Player" width="100%"><br><em>Player with audio and subtitle selection</em></td>
  </tr>
</table>

---

## 📥 Installation

### Requirements
- A Nintendo Switch running **Atmosphère** custom firmware.
- Free space on the microSD card for the video cache.
- An internet connection (Wi-Fi or LAN) and **the console's date and time set correctly** — otherwise HTTPS certificate checks fail.

### Steps
1. Download the latest `NX-torrent-player.nro` from [**Releases**](https://github.com/Langegen/NX-torrent-player/releases).
2. Copy it to the microSD card:
   ```text
   sdmc:/switch/NX-torrent-player/NX-torrent-player.nro
   ```
3. Open the **Homebrew Menu through a game** (Title Override: hold **R** while launching any installed game) so the app gets the full memory. From the Album (applet mode) the app refuses to start: there is too little memory there for video.

> [!TIP]
> **Updates**: the app checks for new versions on startup (**Options → About → Check for updates on startup**), downloads them and replaces the `.nro`. The changelog can be viewed there too.

---

## 🎮 Controls

### Menus
| Button | Where | Action |
| :--- | :--- | :--- |
| **A** | Everywhere | Select / open / play |
| **B** | Everywhere | Back / close |
| **Y** | Title card | Bookmark |
| **X** | Title card | All releases for an episode |
| **X** | Release list | Change the order |
| **Y** | Jackett result | Save to Local |
| **X** | Home, Continue row | Remove from the row |
| **Y / X** | History | Remove an entry / clear the history |
| **Y** | Local | Delete a `.torrent` or magnet |
| **L / R** | Series card | Previous / next season |
| **L / R** | Stremio | Switch view |
| **Touch screen** | Everywhere | Tap to select, swipe to scroll |

### Player
| Button | Action |
| :--- | :--- |
| **D-pad / stick** | With the panel: left / right through the buttons, up to the timeline, down back. Without it: left / right seek 10 s (faster when held), up / down show the panel |
| **Stick on the timeline** | Smooth scrubbing: the further the tilt, the faster |
| **A** | Press the selected button; without the panel, pause / resume |
| **B** | Hide the panel; while playing, leave on a second press |
| **X / +** | Settings: audio, subtitles, speed, video size, picture |
| **ZL** | Episode list |
| **L / R** | Playback speed |
| **Y** | Lock the controls — unlocked only by **Y** or a tap on the padlock |
| **ZR / −** | Engine and stream statistics |
| **Touch screen** | On-screen buttons, tap or drag on the timeline |

From a keyboard or USB remote: arrows, Enter (OK), Esc (Back), Space to pause, Page Up / Page Down for the previous / next episode.

---

## 🧭 User guide

### 1. 🏠 Catalog
Home and the Movies, Series and Anime tabs show the TMDB catalog. Open a title and press **Watch** (or an episode in the list): the app gathers releases from Jackett and your Stremio addons and suggests the best one. If the catalog does not load, check **Options → Network → TMDB**.

### 2. 🔍 Jackett
In **Options → Jackett** (or at first launch) set:
- **Server URL** — for example `http://192.168.1.100:9117`; Jacred servers work too.
- **API key** — if Jackett has authentication on.

Jackett results then show up in every title's release list. **Sources → Jackett** searches the trackers directly, with All / Movies / Series / Anime filters and sorting by seeders, size or date.

### 3. 📺 Stremio
**Sources → Stremio**: sign in with your e-mail and password. The library, catalogs, seasons and episodes are there; releases come from your addons (Torrentio, for example), direct links from debrid addons. Stremio addons also feed the release lists on title cards.

### 4. 📁 Local torrents and magnet links
- Put `.torrent` files in `sdmc:/switch/NX-torrent-player/torrents/` (subfolders are read too).
- Magnet links: one per line in `torrents/magnet.txt`, or the **+ Add magnet** button on the Local tab.
- A magnet's metadata is resolved in the background and cached.

### 5. 🖥️ TorrServer and proxy
**Options → Network**: a TorrServer address sends torrents through it; an HTTP/SOCKS proxy is used for catalogs, addons and `https` video.

### 6. 🩺 Troubleshooting
- **HTTPS / certificate errors** — check the console's date and time.
- **Video stutters** — open the statistics (**ZR**): with few peers and a rate below the bitrate, pick a release with more seeders or a larger buffer.
- **A log is needed** — turn on **Options → About → Log file**, restart the app and reproduce; the log is `sdmc:/switch/NX-torrent-player/nx-torrent-player.log`.

---

## 🛠️ Building from source

### Switch .nro
The simplest way is the official devkitPro Docker image (no local devkitA64 needed):

```sh
# 1. A volume for the build cache, and configure (once)
docker run --rm -v st_build:/build -v "$PWD":/project -w /build \
    devkitpro/devkita64:latest sh -c "cmake /project"

# 2. Build NX-torrent-player.nro
docker run --rm -v st_build:/build -v "$PWD":/project -w /build \
    devkitpro/devkita64:latest \
    sh -c "cmake --build . --target NX-torrent-player.nro"

# 3. Copy the .nro into the project folder
docker run --rm -v st_build:/build -v "$PWD":/project \
    devkitpro/devkita64:latest sh -c "cp /build/NX-torrent-player.nro /project/"
```

Without Docker you need devkitPro with `switch-libmpv switch-curl switch-libwebp switch-mbedtls switch-glfw switch-mesa`, then `cmake -B build -DPLATFORM_SWITCH=ON` and `cmake --build build --target NX-torrent-player.nro`. CI builds the same way: on every pull request, and a draft release on a `vX.Y.Z` tag.

> [!NOTE]
> On Windows (Git Bash or MSYS2), prefix the docker commands with `MSYS_NO_PATHCONV=1`, or the paths inside the container get rewritten.

### PC tests
The offline tests need only gcc/g++ and the libcurl headers (Linux, macOS, WSL or MSYS2). Trackers, DHT and peers are mocked on loopback, so no network is needed. CI runs them on every pull request:
```sh
sh pctest/run_tests.sh
```

`enginetest` runs the real engine on a live swarm with a model of the player, to measure startup and stalls (it also needs libmbedtls and zlib):
```sh
sh pctest/build.sh
pctest/enginetest <file.torrent | magnet:?...> 120 play
```

---

## 👏 Credits

- **[shodow (@shodowlo)](https://github.com/shodowlo)** — author of the original [NX Torrent Player](https://github.com/shodowlo/NX-torrent-player).
- **TorrentShopNX** — the related torrent engine whose core was ported.
- **[Lampa](https://github.com/yumata/lampa)** — the model for the home screen, title cards and player panel.
- **[borealis](https://github.com/xfangfang/borealis)** by @xfangfang — the Horizon-style UI framework.
- **[mpv](https://mpv.io/)** and **[FFmpeg](https://ffmpeg.org/)** — video playback.
- **[libutp](https://github.com/bittorrent/libutp)** — the uTP protocol.
- **[TMDB](https://www.themoviedb.org/)** — the catalog and descriptions (*this product uses the TMDB API but is not endorsed or certified by TMDB*).
- **[Stremio](https://www.stremio.com/)** and **[Jackett](https://github.com/Jackett/Jackett)** — release sources (*this project is not affiliated with them*).
- **[curl](https://curl.se/) and Mozilla's CA certificates** — HTTP/TLS and certificate checks.
- **[devkitPro](https://devkitpro.org/) and [libnx](https://github.com/switchbrew/libnx)** — the Switch toolchain and system libraries.

---

## 📜 License

**Apache License 2.0**.

- Original source code © 2024–2026 shodowlo.
- Modifications and additions © 2026 Langegen and contributors.

See [LICENSE](LICENSE) for details.
