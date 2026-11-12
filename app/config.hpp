#pragma once

#include <string>
#include <vector>

// User settings, persisted to sdmc:/switch/nx-torrent-player/config.json and
// opened with X from the browser. Everything here has a default that matches
// what the app did before it was configurable, so a missing (or corrupt) file
// is not an error -- it just means "defaults".
namespace config
{

enum class Tab
{
    LOCAL   = 0,
    STREMIO = 1,
    JACKETT = 2,
    HISTORY = 3,
    HOME    = 4,  // the TMDB catalog (home.cpp)
    BOOKMARKS = 5,  // the library by status (bookmarks.cpp)
    MOVIES  = 6,  // the catalog's films (home.cpp)
    SHOWS   = 7,  // the catalog's series (home.cpp)
    ANIME   = 8,  // the catalog's anime: Japanese animation (home.cpp)
};

// The default UI sizes, named so the Options screen can mark them in the list.
// Docked defaults smaller than handheld: the same logical size that fills a 6"
// panel leaves most of a TV empty.
constexpr int kDefaultDockedUiWidth   = 1600;
constexpr int kDefaultHandheldUiWidth = 1280;

struct Config
{
    // Which category the browser opens on.
    Tab startupTab = Tab::HOME;

    // Writes the log to APPDATA_LOG. Off by default: it is unbuffered and the
    // engine dumps a [stats] line every 2s, so it writes to the SD card
    // continuously for the whole session. Worth it when diagnosing, not
    // otherwise.
    bool logging = false;

    // Preferred track languages, as ISO-639-1 ("fr") or "auto" -- which means
    // the console's own language, so the app matches the system out of the box.
    std::string audioLang = "auto";
    std::string subLang   = "auto";

    // Show subtitles at all. Off means mpv loads none rather than picking one.
    bool subtitles = true;

    // Lift the loudness of a 5.1 track downmixed to stereo (mpv's dynaudnorm).
    // On by default -- a 5.1 master folded to stereo sits well below a native
    // stereo track, and the console's own speakers have no headroom left to
    // make that up. Only ever applied in handheld for that reason: docked, the
    // TV or receiver does the amplifying and the compression is just a
    // flattened dynamic range nobody asked for. Applied live, so docking
    // mid-film drops it (see MpvView::pumpEvents).
    bool audioBoost = true;

    // The picture in handheld: mpv's video equalizer (brightness, contrast,
    // gamma, saturation), each -100..100 with 0 = untouched. Handheld only,
    // like the boost: docked the TV owns the picture, so the player sends
    // zeros there and docking mid-film drops them (see picture.hpp).
    int picBrightness = 0;
    int picContrast   = 0;
    int picGamma      = 0;
    int picSaturation = 0;

    // Hardware video decoding (nvtegra). On by default. Turn it off to decode in
    // software instead -- slower and it may stutter on 1080p, but it sidesteps the
    // GPU decode path, which helps tell whether the random freezes come from it.
    bool hwDecode = true;

    // Ask GitHub for a newer release at startup. On by default, but it is a
    // network call the user did not ask for -- offline or on a metered
    // connection, being able to turn it off matters.
    bool checkUpdates = true;

    // Hides 4K sources in the Stremio source list. On by default: they are the
    // heaviest streams in the swarm and the Switch outputs 1080p docked, so
    // they cost bandwidth we cannot show.
    bool hide4k = true;

    // Download-rate limiter (torrentfs_set_governor): once the playback buffer
    // is comfortably ahead, cap downloading to a backlog-tied rate instead of
    // bursting at wifi line rate — those bursts saturate the OS network core and
    // can stutter the console. Off by default: it trades download speed for
    // network calm, and it never limits anything while the buffer is under 10 s
    // anyway. Never touches streams that struggle to keep up.
    bool rateGovernor = false;

    // UI size, as the logical width borealis lays its views out in: windowScale
    // is the output width over this, so a *wider* logical space means a
    // *smaller* UI. Stored per mode because the right answer differs -- 1280
    // (borealis' own default) fills a 6" panel but wastes most of a TV.
    int dockedUiWidth   = kDefaultDockedUiWidth;
    int handheldUiWidth = kDefaultHandheldUiWidth;

    // Colour scheme, as a theme::Scheme id. Purple is what the app has always
    // looked like. An id we do not know falls back to it at read time (see
    // theme::current()), so this is not validated here.
    std::string accent = "purple";

    // The UI language, as an i18n::langIds() id: "auto" (the console's own
    // language when we have a table for it, English otherwise -- the default),
    // "en", "fr" or "ru". Read at startup only (see i18n::load()): the strings
    // are taken when a view is built, and the header, tab bar and browser are
    // built once.
    std::string language = "auto";

    // "dark", "light" or "system" (the console's own setting). Dark is what the
    // app has always been. Read at startup only: borealis does not support
    // swapping the variant while running. Anything unrecognised behaves as
    // "dark" (see theme::isLight()).
    std::string themeVariant = "dark";

    // How the Stremio tab draws its movies and shows: "posters" (upright cards
    // in a horizontally scrolling strip, the default), "cards" (a column of
    // rows, poster beside a raised panel with the title, the episode or the
    // year, the position and the progress bar) or "classic" (the flat row the
    // app shipped with). Anything unrecognised behaves as "cards" -- the style
    // is compared against the other two, never validated at read time.
    std::string listStyle = "posters";

    // RAM streaming (torrentfs_set_ram_stream): keep verified pieces in a
    // bounded RAM window instead of writing them to the SD card. On by default
    // -- it removes the per-piece playback stutter (the SD write of a finished
    // piece hammers the filesystem core, the more so the bigger the piece). The
    // trade is that nothing is persisted and seeking far back re-downloads, and
    // it needs a full-RAM launch. Applies to the next video.
    bool ramStream = true;

    // How far ahead playback buffers: 0 standard, 1 large, 2 maximum (see
    // bufferPreset()). Bigger rides out a slow swarm or wifi drop at the cost
    // of RAM and a longer wait before the first frame. Applies to the next video.
    int bufferMode = 0;

    // What the player does when a video plays to its end (EndAction): go on
    // to the next episode (what it always did), back to the menu, put the
    // console to sleep, or close the app. The player's panel sets it too.
    int endAction = 0;

    // Jackett server configuration
    std::string jackettUrl = "";
    std::string jackettApiKey = "";
    // The first-run Jackett setup has been shown (finished or skipped): it is
    // offered once, then only Options -> Jackett.
    bool jackettSetupAsked = false;

    // TorrServer ("http://192.168.1.10:8090"). Set: torrents are streamed by
    // that server and played here as plain HTTP, instead of by the on-device
    // engine. Empty: the engine, as before.
    std::string torrServerUrl = "";

    // Proxy for the app's HTTP requests and https playback, as curl takes it:
    // "http://host:3128", "socks5h://host:1080". Empty: direct. Addresses on
    // the local network (Jackett, TorrServer) never go through it.
    std::string proxyUrl = "";

    // TMDB: the key (v3 key or v4 token; empty = the build's default, if
    // any), and optional mirrors for the API and the images (empty = TMDB's
    // own). See tmdb.hpp.
    std::string tmdbApiKey = "";
    std::string tmdbApiUrl = "";
    std::string tmdbImageUrl = "";
    std::string tmdbProxyUrl = "";
    // The built-in TMDB proxy. Unless the user flipped it in Options
    // (tmdbProxyUserSet), it follows the UI language: on for Russian, where
    // TMDB is often unreachable directly. Only a user's choice is saved.
    bool tmdbProxyEnabled = false;
    bool tmdbProxyUserSet = false;
};

// The numbers behind Config::bufferMode.
struct BufferPreset
{
    int demuxerMaxMiB;     // mpv demuxer-max-bytes
    int readaheadSecs;     // mpv demuxer-readahead-secs
    int cacheSecs;         // mpv cache-secs
    double startSecs;      // buffered before the first frame
    int streamWindowMiB;   // engine read-ahead window (torrentfs)
};
// The preset for the current setting, already capped to what this launch can
// afford (an applet launch has far less RAM than a full one).
BufferPreset bufferPreset();
const std::vector<std::string>& bufferLabels();

// The values of Config::endAction.
enum EndAction
{
    END_NEXT_EPISODE,  // the next episode when there is one, else the menu
    END_MENU,          // back to the menu, even mid-series
    END_SLEEP,         // back to the menu and the console to sleep
    END_QUIT,          // close the app
    END_ACTION_COUNT,
};
const std::vector<std::string>& endActionLabels();

// The UI language with "auto" resolved: "en", "fr" or "ru".
std::string effectiveUiLang(const std::string& setting);

// The live settings. Mutate, then call save().
Config& get();

// The console's language as ISO-639-1 ("fr"), or "en" if it cannot be read.
std::string consoleLang();

// An ISO-639-1 code (or "auto") turned into what mpv's alang/slang want: a
// comma-separated list, because a track's language tag can be 639-1 ("fr") or
// either 639-2 form ("fre" bibliographic / "fra" terminological) depending on
// who muxed the file. "" when there is nothing to prefer.
std::string mpvLangList(const std::string& code);

// The languages the Options screen offers, "auto" first. Codes and labels are
// index-matched.
const std::vector<std::string>& langCodes();
const std::vector<std::string>& langLabels();

// A language tag as somebody else spelled it -- a track's "lang" ("fre"), a
// Stremio subtitle addon's ("fr", "eng", sometimes "French") -- turned into a
// name to show. Unknown tags come back capitalised but otherwise untouched,
// which is the honest answer for the long tail of what addons send.
std::string langLabelFor(const std::string& tag);

// The subtitle language actually preferred right now: the setting, with "auto"
// resolved to the console's own language. ISO-639-1.
std::string preferredSubLang();

// The UI sizes the Options screen offers: logical widths (what dockedUiWidth /
// handheldUiWidth store) and their labels, index-matched. One list for both
// modes -- a label is the size relative to 1280, which does not depend on the
// output it is drawn to.
const std::vector<int>& uiWidths();
const std::vector<std::string>& uiWidthLabels();

// Reads config.json. Called once at startup, before the UI is built. Missing
// file / unreadable keys keep their default.
void load();

// Writes config.json. False if the SD card refused it.
bool save();

} // namespace config
