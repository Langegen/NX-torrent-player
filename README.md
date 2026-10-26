<div align="center">
  <img src="assets/NX-torrent-player-banner.png" alt="NX Torrent Player" width="640">

  # NX Torrent Player

  **A modern BitTorrent streaming video player for Nintendo Switch**
  
  *Featuring Stremio, Jackett, Playback History, and Local Torrents*

  [![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
  [![Platform](https://img.shields.io/badge/Platform-Nintendo_Switch-red.svg)](https://www.nintendo.com/)
  [![Upstream](https://img.shields.io/badge/Forked_From-shodowlo%2FNX--torrent--player-green.svg)](https://github.com/shodowlo/NX-torrent-player)

  [English](README.md) • [Русский](README.ru.md)
</div>

---

> [!NOTE]
> ### ℹ️ Attribution & Upstream Acknowledgement
> This repository is an independent continuation and fork of the original [**NX-torrent-player**](https://github.com/shodowlo/NX-torrent-player) created by [**shodow** (@shodowlo)](https://github.com/shodowlo).
>
> Sincere thanks and full credit go to **shodow** for designing and implementing the foundational architecture, custom BitTorrent streaming engine, and sleek Horizon-style interface. This fork is maintained by [**Langegen**](https://github.com/Langegen) to introduce additional features (such as Jackett integration, watch history, improved multi-file management, enhanced tracker pooling, and PC testing) while honoring and preserving the quality of the original project.

---

## 📖 Overview

**NX Torrent Player** is a homebrew video player for Nintendo Switch running custom firmware (Atmosphère). Unlike traditional downloaders, it incorporates an on-device BitTorrent streaming engine that requests and buffers pieces sequentially: playback begins within seconds, continuously downloading ahead of the playhead.

Whether you stream through **Stremio** catalogs and addons, search across dozens of trackers with **Jackett**, or play local `.torrent` files and magnet links from your SD card, NX Torrent Player delivers a smooth, native console experience powered by `mpv` and the `borealis` UI framework.

---

## ✨ Features

### 🌟 Added in this Fork
- 🔍 **Jackett Search & Streaming** — Search across all your configured Jackett trackers directly from your Switch. Filter by category (*All, Movies, Series, Anime*), sort by seeders/size/date, stream instantly, or save `.torrent` files to your SD card.
- 🕒 **Watch History & Resume Playback** — Dedicated **History** tab tracking your recently watched videos across all sources (Local, Jackett, Stremio). Automatically remembers your playback position so you can resume right where you left off.
- 📂 **Multi-File Selection & Natural Sorting** — Smart file picker for torrents and magnets containing multiple videos with natural alphanumeric episode sorting (e.g., Episode 1, 2, ..., 10 instead of 1, 10, 2).
- ⚡ **Enhanced Engine & Tracker Pooling** — Background announce worker pool supporting concurrent announces across 30+ trackers, asynchronous metadata resolution with cancelable progress dialogs, and robust DHT peer search fallback.
- 🔗 **Direct Video Links** — Stremio addon streams that are plain `http(s)` URLs (debrid services and the like) now play, with seeking. `https` goes through the app's own curl with certificate checks, since the Switch's FFmpeg has no `https` of its own.
- 🖥️ **TorrServer** — Set your TorrServer address in **Options → Network** and every torrent (Local, Jackett, Stremio) is streamed by that server and played on the console as a regular video stream.
- 🌐 **Proxy** — An HTTP or SOCKS proxy for catalogs, addons, posters and `https` video links (**Options → Network**); servers on your local network are always reached directly.
- 🇷🇺 **Russian Interface** — A full Russian translation, switchable in **Options → General → Language**.
- 🧪 **Desktop Test Suite** — Offline PC tests (`pctest/`) for the torrent engine, `https` range reads and TorrServer links, with mock trackers, peers and servers on loopback — no physical Switch, devkitPro toolchain or network needed.

### 🎬 Core Features
- **True Sequential Streaming** — Starts playing almost immediately; the engine streams ahead of the playhead and automatically re-centers when you seek.
- **Stremio Integration** — Sign in with your Stremio account to browse your library, catalogs, addons, Cinemeta descriptions, and BitTorrent streams.
- **Local Torrents & Magnets** — Drop `.torrent` files into `/switch/NX-torrent-player/torrents/` or add magnet links via the on-screen keyboard or `magnet.txt`.
- **Advanced Player** — Built on `mpv` with hardware acceleration, audio/subtitle track selection (including Stremio external subtitles), analog scrubbing (thumbsticks), playback speed controls, and handheld audio boost.
- **Console-Native UI** — Built with `borealis`, providing a fast, responsive Nintendo Switch Horizon-style interface with full controller and touchscreen support.

---

## 📸 Screenshots

<table>
  <tr>
    <td width="50%"><img src="assets/image1.webp" alt="Local torrents tab" width="100%"><br><em>Local Torrents & Magnets</em></td>
    <td width="50%"><img src="assets/image2.webp" alt="Stremio library" width="100%"><br><em>Stremio Library & Catalogs</em></td>
  </tr>
  <tr>
    <td width="50%"><img src="assets/image4.webp" alt="Episode picker" width="100%"><br><em>Episode & File Picker</em></td>
    <td width="50%"><img src="assets/image3.webp" alt="Player" width="100%"><br><em>Video Player with Audio/Subtitles</em></td>
  </tr>
</table>

---

## 📥 Installation

### Requirements
- A Nintendo Switch running custom firmware (**Atmosphère**).
- Sufficient free space on your microSD card for stream caching.
- Internet connection (Wi-Fi or LAN) with correct console date and time.

### Steps
1. Download the latest `NX-torrent-player.nro` from the [**Releases**](https://github.com/Langegen/NX-torrent-player/releases) page.
2. Place the file onto your microSD card into:
   ```text
   sdmc:/switch/NX-torrent-player/NX-torrent-player.nro
   ```
3. Insert the SD card into your console and launch the **Homebrew Menu**.
4. **Important**: Launch the Homebrew Menu via **Title Override** (hold **R** while launching any installed game) to allocate full RAM to the application. Running in applet mode (via the Album) has strict memory limits and is deliberately blocked to prevent crashes.

> [!TIP]
> **In-App Updates**: Updating can be done directly from within the app (**Options → Check on startup**). When a new release is available, it downloads and replaces the `.nro` automatically upon exiting.

---

## 🎮 Controls

| Button / Input | Context | Action |
| :--- | :--- | :--- |
| **A** | Menus / Lists | Select / Play video |
| **B** | Menus / Dialogs | Back / Cancel / Dismiss dialog |
| **X** | Stremio | Toggle library membership / Clear "Continue Watching" tile |
| **Y** | Local / History | Delete `.torrent` or remove entry from list / history |
| **Y** | Jackett | Save `.torrent` or magnet to Local library |
| **Y** | Player | Lock controls (prevent accidental inputs during playback) |
| **L / R** | Browser | Cycle Stremio view / filters |
| **L / R** | Player | Adjust playback speed (or adjust subtitle delay in track menu) |
| **Left / Right Stick** | Player | Smooth analog scrubbing (seek backward / forward) |
| **Touchscreen** | Anywhere | Tap to select, drag to scroll, on-screen player controls |
| **ZR** | Player | Toggle real-time engine statistics and diagnostic HUD |

---

## 🧭 Usage Guide

### 1. 🕒 Watch History
The **History** tab keeps track of your recently watched media across all sources (Local, Jackett, Stremio).
- Each item shows title, source type, watched duration, and progress bar.
- Selecting an item (**A**) automatically resumes playback where you left off (valid for items watched for more than 15 seconds and under 95% completion).
- Press **Y** on any history item to remove it, or use the **Clear History** button at the top.

### 2. 🔍 Jackett Integration
In the **Jackett** tab (or via **Options → Jackett**), set up your Jackett server:
- **Server URL**: Enter your Jackett URL (e.g. `http://192.168.1.100:9117`).
- **API Key**: Enter your Jackett API key (optional; leave blank if your Jackett instance does not require one).
- **Features**:
  - Search across all configured trackers with instant category filtering (**All**, **Movies**, **Series**, **Anime**).
  - Sort results by **Seeders**, **Size**, or **Date**.
  - Press **A** to stream directly. For multi-file releases, an episode picker is displayed with naturally sorted files.
  - Press **Y** to download and save the `.torrent` file to your local library (`/switch/NX-torrent-player/torrents/`).

### 3. 📺 Stremio Integration
In the **Stremio** tab, log in with your Stremio email and password:
- Your Stremio session persists across restarts (sign out anytime from Options).
- Browse your library, discover popular media, and browse seasons and episodes.
- Sources provided by your configured BitTorrent addons will be displayed.
- **4K Filtering**: 4K streams are hidden by default because the Switch outputs up to 1080p docked (saving significant bandwidth). You can re-enable 4K streams in Options if desired.
- Supports external subtitles provided by Stremio subtitle addons alongside embedded subtitles.

### 4. 📁 Local Torrents & Magnets
- Place `.torrent` files into `sdmc:/switch/NX-torrent-player/torrents/` (nested subdirectories are supported).
- Add magnet links by editing `sdmc:/switch/NX-torrent-player/torrents/magnet.txt` (one magnet link or info-hash per line) or press **Add magnet** in the Local tab to type/paste using the on-screen keyboard.
- Magnet metadata (name and file structure) is resolved in the background and cached for instantaneous access next time.
- Press **Y** on any entry to delete the `.torrent` file from the SD card or remove the magnet link.

---

## 🛠️ Building from Source

### Nintendo Switch (.nro)
The build environment runs inside the official devkitPro Docker container, so local toolchain installation is not required.

```sh
# 1. Create a persistent named volume and configure (run once)
docker run --rm -v st_build:/build -v "$PWD":/project -w /build \
    devkitpro/devkita64:latest sh -c "cmake /project"

# 2. Build the NX-torrent-player.nro binary
docker run --rm -v st_build:/build -v "$PWD":/project -w /build \
    devkitpro/devkita64:latest \
    sh -c "cmake --build . --target NX-torrent-player.nro"

# 3. Copy the compiled .nro back to your project directory
docker run --rm -v st_build:/build -v "$PWD":/project \
    devkitpro/devkita64:latest sh -c "cp /build/NX-torrent-player.nro /project/"
```

> [!NOTE]
> On Windows (using Git Bash or MSYS2), prefix docker commands with `MSYS_NO_PATHCONV=1` to prevent path conversions.

### Desktop Engine Tests (PC)
The offline test suites need only gcc/g++ and libcurl headers (Linux, macOS, WSL or
MSYS2). Trackers and the DHT are mocked and a mock peer serves the metadata on
loopback, so they need no network; CI runs them on every pull request:
```sh
sh pctest/run_tests.sh
```

`enginetest` runs the real engine against a live swarm, for measuring download
behaviour (needs libmbedtls and zlib as well):
```sh
cd pctest
./build.sh
./enginetest <file.torrent | magnet:?...> [seconds]
```

---

## 👏 Credits & Acknowledgements

This project would not be possible without the remarkable work of the open-source community:

- **[shodow (@shodowlo)](https://github.com/shodowlo)** — Original creator and primary architect of [NX Torrent Player](https://github.com/shodowlo/NX-torrent-player).
- **[borealis](https://github.com/xfangfang/borealis)** by @xfangfang — Hardware-accelerated Switch Horizon UI framework.
- **[mpv](https://mpv.io/)** — Fast, versatile video playback engine.
- **[libutp](https://github.com/bittorrent/libutp)** — BitTorrent uTP transport protocol implementation.
- **[Stremio](https://www.stremio.com/)** — Media catalog, library, and addon protocols (*NX Torrent Player is not affiliated with or endorsed by Stremio*).
- **[Jackett](https://github.com/Jackett/Jackett)** — Torrent indexer proxy API (*NX Torrent Player is not affiliated with or endorsed by Jackett*).
- **[curl](https://curl.se/) & Mozilla CA Store** — HTTP/TLS engine and root certificate verification bundle.
- **[devkitPro](https://devkitpro.org/) & [libnx](https://github.com/switchbrew/libnx)** — Nintendo Switch homebrew toolchain and runtime libraries.

---

## 📜 License

This project is licensed under the **Apache License 2.0**.

- Original work Copyright © 2024–2026 shodowlo.
- Modifications and additions Copyright © 2026 Langegen and contributors.

See the [LICENSE](LICENSE) file for complete details.
