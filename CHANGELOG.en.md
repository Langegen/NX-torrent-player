# What's new

[Русский](CHANGELOG.md) • **English**

The text under a version heading is the GitHub release description, which the
app also shows in About → View changelog. So a section holds plain text and
lists only, no headings or emphasis.

## 1.0.0 — 8 November 2026

The first major version: a new torrent engine, a Lampa-style player and a lot of stability work.

Player
- A Lampa-style control panel: title, episode, clock and end time at the top; a timeline and buttons at the bottom — previous / next episode, rewind, pause, episodes, audio, subtitles, settings, lock.
- A season pack's episodes work as a playlist: the episode list (ZL), previous and next, and the next episode starting on its own.
- The episode list scrolls with the cursor and shows readable names: "Episode 16 · April in Quahog" instead of a cut-off file name.
- The whole player works with the arrows, OK and Back — a keyboard or a USB remote in the dock will do. While a video plays, Back leaves on a second press.
- The sticks act as the D-pad, and on the timeline as smooth scrubbing.
- Volume, speed, video size (fill, stretch, zoom), subtitle size and delay change with the arrows right in the settings panel.
- Handheld picture: brightness, contrast, gamma and saturation in a Picture submenu, and a calibration screen with test patterns. Docked, the TV owns the picture.
- The lock comes off only with Y or a tap on the padlock.
- 4K plays more smoothly: the heavy scaling filters are off for video above 1080p.
- A direct link that drops mid-film reconnects where it stopped.

Torrent engine
- A new core from TorrentShopNX: several peers per piece, re-requesting the needed piece from a second peer, endgame, peer exchange (PEX), incoming connections (UPnP), uTP, a persistent background DHT.
- A scored pool of 1024 addresses and up to 40 connections at once.
- A magnet opens in one go: trackers and DHT are asked at once, metadata and peers are cached.
- Adaptive start: playback begins after 3 seconds of buffer when the rate covers the bitrate. The buffer size is in Options → Streaming.
- DHT now starts on networks where the main bootstrap nodes do not answer.

Catalog, history and releases
- History as posters that open the title card; "Continue S1 E2 from 22:10" from the same release; files without a card in an "Other files" row.
- Releases with Russian audio rank first, dubbing highest; rutracker marks are understood (| D, | P, L1/L2, ДБ, ПМ). Jacred queries carry the title, original title and year.
- TMDB proxy: a switch in the options, on by default for the Russian UI; a built-in TMDB key.
- The UI language follows the console by default.
- Russian text uses the console's system font, without the wide letter spacing.
- A long season list scrolls instead of being squeezed.

Stability
- The app no longer closes on its first network request (such as "Test connection").
- Settings, library and history are saved with a backup: a power cut no longer wipes them.
- No more hangs while a magnet loads, when leaving the player during a load, or when quitting the app.
- Crashes and memory leaks in the engine fixed; the app uses noticeably less memory.
- Artwork is decoded in the background — menus no longer stutter, and posters load faster.
- Server errors (502, 500) are no longer cached in place of good data.
- A downloaded update is checked before it is installed: a broken file cannot replace a working one.

Earlier versions are on the [Releases](https://github.com/Langegen/NX-torrent-player/releases) page.
