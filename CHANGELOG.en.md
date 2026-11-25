# What's new

[Русский](CHANGELOG.md) • **English**

The text under a version heading is the GitHub release description, which the
app also shows in About → View changelog. So a section holds plain text and
lists only, no headings or emphasis.

## 1.3 — 25 November 2026

Overhauled torrent search and Switch codec compatibility ranking (AV1 and Hi10P protection), playback freeze and artifact fixes, and double-tap seeking.

Torrents, Search & Switch Compatibility
- Fixed Russian season and episode title parsing (e.g. "5 сезон: 1-22 серии" no longer misparses the season) and added Jacred metadata enrichment (seasons, voice-overs, languages, trackers, btih).
- Added hybrid 2-request search, per-episode pack sizing for ranking, non-video filtering, and false foreign dub filtering.
- Added new filters and sorting in Sources and Jackett: Season selector, Alive (>0) filter, Custom search, and Sort by Date.
- Protected Switch playback from ~5 fps software-decode stutters: AV1 (8/10-bit) and H.264 10-bit (Hi10P) releases lacking Tegra X1 NVDEC hardware decoding are heavily deprioritized on Switch, excluded from "best choice", and tagged "no HW decode", while "heavy for Wi-Fi" now reflects real per-episode/film size, 4K, and Remux.

Player & Picture
- Player now displays a warning toast on Switch when falling back to software decoding, automatically switches to lightweight bilinear scaling, and shows the active video codec and pixel format in the ZR diagnostics panel.
- Fixed grey frames, chroma corruption, and playback freezes: enabled strict SHA-1 verification in RAM streaming, applet-mode RAM protection, cache-pause rebuffering on slow swarms, non-blocking libmpv rendering, 3-thread software decode cap on Switch, and a live hardware decoding toggle in the player Video settings.
- Atomic color shader replacement (saturation and RGB calibration no longer stack) and blocked background seeking while settings overlays are open.
- Added double-tap-to-seek (-/+10 s on left and right screen zones with rapid tap chaining).

## 1.2 — 20 November 2026

Folders and categories in bookmarks, actor and director filtering, RGB color balance in screen settings, L/R tab navigation, expandable synopsis, and major stability fixes for docking and long playback sessions.

Interface & Navigation
- Folders and categories in bookmarks: create, rename, and delete custom folders in bookmarks to easily organize your movies and TV shows.
- Creator and cast filtering (Lampa style): movie and TV cards now feature clickable rows of directors, creators, and cast members with direct access to their filmographies.
- L/R tab switching: quickly navigate between main menu sections using the L and R shoulder buttons on the gamepad.
- Expandable synopsis: long overviews and descriptions for films and series can now be expanded and collapsed.
- Improved gamepad navigation to the "See all" section on catalog tabs, along with grid navigation and focus preservation in search and Stremio.
- Fixed encoding issues and missing glyphs in movie titles without Russian translations.

Picture & Player
- RGB balance: added independent Red, Green, and Blue adjustment sliders in handheld picture settings and within the player.
- Fixed subtitle toggle pop-up hint clipping off-screen.

Stability & Performance
- Fixed crashes and stutter when docking and undocking the console: eliminated 100ms debounce resize delay, synchronized graphics buffer state, and switched to bilinear filtering in docked mode (1080p).
- Fixed video artifacts (gray screen) and cumulative audio desync during long playback sessions: increased NVDEC hardware surface pool and synchronized audio clock with hardware Audren (48 kHz).
- Fixed freezes and memory issues when scrolling long movie catalogs: implemented off-screen culling, background poster decoding in worker threads, and rate-limited GPU texture uploads.

## 1.1 — 14 November 2026

Instant rewind, sleep timer, quick subtitle toggle, side panels for audio and subtitles, battery indicator in player, and unified search.

Player
- Instant rewind: mpv keeps 128 MB of back cache, and the torrent engine fetches and retains a sliding window of up to 2.5 minutes behind the playhead in RAM (up to 384 MB). Back pieces are requested with low priority once ahead buffer is sufficient, making rewind instantaneous.
- Quick subtitle toggle: hovering over the Subtitles button reveals a pop-up button to turn subtitles off or back on with a single click or tap.
- Battery indicator: exact percentage and battery status icon in the top bar beside the clock (turns red when low, lightning bolt when charging).
- Side track lists: audio and subtitle selection now use smooth side panels instead of dropdowns, eliminating hangs on releases with dozens of tracks.
- Sleep timer: automatic sleep after 15, 30, 45, 60, 90, 120 minutes or at the end of the video. Displays a warning 1 minute before sleeping, pauses playback with position saved, and puts the console to sleep.
- Playback end action: choose between "Next episode", "Return to menu", "Sleep mode", or "Exit application" (in the player settings and general playback options).
- Handheld saturation: color adjustment now uses a screen-space shader, properly working with the hardware video decoder and test patterns.

Catalog and search
- Search all categories: "All" is now the default search category, neatly separating Movies, TV Shows, and Anime into distinct sections.

Stability and fixes
- Fixed on-screen keyboard freeze when entering long server addresses (Jackett, TorrServer).
- Fixed Borealis RecyclerFrame list width caching bug across instances.

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
