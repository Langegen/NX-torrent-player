#include "config.hpp"

#include <borealis.hpp>

#include <switch.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "appdata.hpp"
#include "fsutil.hpp"
#include "i18n.hpp"

namespace config
{
namespace
{

constexpr const char* kPath = APPDATA_DIR "/config.json";

Config cfg;

// The file is ours: a flat object of bools and one string, written by save()
// below. That is small enough that finding "key": <value> beats pulling a JSON
// parser into the build (same reasoning as app/stremio.cpp).
bool readBool(const std::string& body, const char* key, bool dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t k        = body.find(pat);
    if (k == std::string::npos) return dflt;
    size_t colon = body.find(':', k + pat.size());
    if (colon == std::string::npos) return dflt;
    size_t v = colon + 1;
    while (v < body.size() && (body[v] == ' ' || body[v] == '\t')) v++;
    if (body.compare(v, 4, "true") == 0) return true;
    if (body.compare(v, 5, "false") == 0) return false;
    return dflt;
}

int readInt(const std::string& body, const char* key, int dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t k        = body.find(pat);
    if (k == std::string::npos) return dflt;
    size_t colon = body.find(':', k + pat.size());
    if (colon == std::string::npos) return dflt;
    // strtol skips leading space itself, and yields 0 on anything that is not a
    // number -- which every caller has to reject anyway, since 0 is never a
    // valid value for the settings stored this way.
    return (int)std::strtol(body.c_str() + colon + 1, nullptr, 10);
}

std::string readStr(const std::string& body, const char* key,
                    const std::string& dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t k        = body.find(pat);
    if (k == std::string::npos) return dflt;
    size_t q1 = body.find('"', body.find(':', k + pat.size()));
    if (q1 == std::string::npos) return dflt;
    // What jsonStr() escapes: \" \\ and \u00XX. Anything else stays as
    // written, so a value saved before escaping existed reads back the same.
    std::string out;
    for (size_t i = q1 + 1; i < body.size(); i++)
    {
        char c = body[i];
        if (c == '"') return out;
        if (c == '\\' && i + 1 < body.size())
        {
            char e = body[i + 1];
            if (e == '"' || e == '\\')
            {
                out += e;
                i++;
                continue;
            }
            if (e == 'u' && i + 5 < body.size() && body.compare(i + 2, 2, "00") == 0)
            {
                char* end = nullptr;
                std::string hex = body.substr(i + 4, 2);
                long v = std::strtol(hex.c_str(), &end, 16);
                if (end && *end == 0)
                {
                    out += (char)v;
                    i += 5;
                    continue;
                }
            }
        }
        out += c;
    }
    return dflt;  // no closing quote
}

// `s` as a JSON string: quotes, backslashes and control characters escaped
// (a value with a quote in it used to break the whole file).
std::string jsonStr(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
        {
            o += '\\';
            o += (char)c;
        }
        else if (c < 0x20)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            o += buf;
        }
        else
            o += (char)c;
    }
    return o + "\"";
}

// The languages offered, and what each maps to for mpv. Both 639-2 spellings
// are listed because a track tag can carry either: "fre" (bibliographic) or
// "fra" (terminological) for French, and mpv matches the tag as written.
struct Lang
{
    const char* code;   // what we store
    const char* label;  // what Options shows
    const char* mpv;    // what mpv is given
};

const Lang kLangs[] = {
    { "auto", "Console language", "" },  // resolved via consoleLang()
    { "en", "English", "en,eng" },
    { "fr", "French", "fr,fre,fra" },
    { "es", "Spanish", "es,spa" },
    { "de", "German", "de,ger,deu" },
    { "it", "Italian", "it,ita" },
    { "pt", "Portuguese", "pt,por" },
    { "nl", "Dutch", "nl,dut,nld" },
    { "ru", "Russian", "ru,rus" },
    { "ja", "Japanese", "ja,jpn" },
    { "ko", "Korean", "ko,kor" },
    { "zh", "Chinese", "zh,chi,zho" },
};

// The UI sizes offered, as the logical width borealis lays out in. The label is
// that width relative to 1280 (the stock logical width), because that is what
// is actually perceived: a 1600-wide logical space draws everything at 80% of
// its stock size. Deliberately expressed against 1280 rather than against the
// output, so one list serves both docked and handheld.
struct UiScale
{
    int width;
    const char* label;
};

// No size above 100%: the header does not fit at 111% (the centred view bar
// runs into the category buttons), and nothing else gained from it.
const UiScale kUiScales[] = {
    { 1280, "100%" },
    { 1440, "89%" },
    { 1600, "80%" },
    { 1760, "73%" },
    { 1920, "67%" },
};

// A stored width, rejected unless we actually offer it: these go straight into
// the layout engine, so a hand-edited (or truncated) config.json must not be
// able to leave the UI unusable -- and the setting therefore unreachable.
int knownWidth(int w, int dflt)
{
    for (const auto& s : kUiScales)
        if (s.width == w) return w;
    return dflt;
}

} // namespace

Config& get() { return cfg; }

std::string consoleLang()
{
#ifdef __SWITCH__
    // setGetSystemLanguage packs the code as chars in a u64 ("fr", "en-US"),
    // NOT as a SetLanguage enum -- the first two bytes are the ISO-639-1 part,
    // which is all we need. borealis already calls setInitialize().
    u64 code = 0;
    if (R_FAILED(setGetSystemLanguage(&code))) return "en";
    char buf[9] = { 0 };
    std::memcpy(buf, &code, 8);
    if (!buf[0] || !buf[1]) return "en";
    return std::string(buf, 2);
#else
    const char* l = std::getenv("LANG");
    if (l && std::strlen(l) >= 2) return std::string(l, 2);
    return "ru";
#endif
}

std::string effectiveUiLang(const std::string& setting)
{
    std::string l = setting == "auto" || setting.empty() ? consoleLang() : setting;
    // Only the languages we have a table for; anything else is English.
    if (l == "ru" || l == "fr" || l == "en") return l;
    return "en";
}

namespace
{
// Follows the UI language unless the user chose: on for Russian.
void defaultTmdbProxy()
{
    cfg.tmdbProxyEnabled = effectiveUiLang(cfg.language) == "ru";
    cfg.tmdbProxyUserSet = false;
}

const BufferPreset kBufferPresets[] = {
    { 128, 30, 60, 15.0, 32 },    // standard: what the app always did
    { 256, 90, 180, 30.0, 64 },   // large
    { 512, 180, 300, 45.0, 128 }, // maximum
};
} // namespace

BufferPreset bufferPreset()
{
    int m = cfg.bufferMode;
    if (m < 0 || m > 2) m = 0;
#ifdef __SWITCH__
    // An applet launch (album, or a title held with R) gets a few hundred MB
    // in all: the maximum preset would not fit next to mpv and the engine.
    if (m > 1 && appletGetAppletType() != AppletType_Application &&
        appletGetAppletType() != AppletType_SystemApplication)
        m = 1;
#endif
    return kBufferPresets[m];
}

const std::vector<std::string>& bufferLabels()
{
    static std::vector<std::string> v = { tr("Standard"), tr("Large"),
                                          tr("Maximum") };
    return v;
}

std::string mpvLangList(const std::string& code)
{
    std::string c = code == "auto" ? consoleLang() : code;
    for (const auto& l : kLangs)
        if (c == l.code) return l.mpv;
    // A console language we do not have a row for: hand mpv the bare code, it
    // still matches tracks tagged with the 639-1 form.
    return c.empty() ? "" : c;
}

std::string langLabelFor(const std::string& tag)
{
    if (tag.empty()) return "";
    std::string t;
    for (char c : tag) t += (char)std::tolower((unsigned char)c);

    for (const auto& l : kLangs)
    {
        if (!std::strcmp(l.code, "auto")) continue;
        // The mpv column is exactly the set of spellings that mean this
        // language ("fr,fre,fra"), which is the same set an addon picks from.
        std::string list = l.mpv;
        for (size_t a = 0; a <= list.size();)
        {
            size_t b = list.find(',', a);
            if (b == std::string::npos) b = list.size();
            if (list.compare(a, b - a, t) == 0) return l.label;
            a = b + 1;
        }
        // Some addons send the English name instead of a code.
        std::string lbl;
        for (const char* p = l.label; *p; p++)
            lbl += (char)std::tolower((unsigned char)*p);
        if (lbl == t) return l.label;
    }

    // Not one of ours: hand it back capitalised rather than inventing a name.
    t[0] = (char)std::toupper((unsigned char)t[0]);
    return t;
}

std::string preferredSubLang()
{
    const std::string& s = cfg.subLang;
    return s == "auto" || s.empty() ? consoleLang() : s;
}

const std::vector<std::string>& langCodes()
{
    static std::vector<std::string> v = [] {
        std::vector<std::string> o;
        for (const auto& l : kLangs) o.push_back(l.code);
        return o;
    }();
    return v;
}

const std::vector<std::string>& langLabels()
{
    static std::vector<std::string> v = [] {
        std::vector<std::string> o;
        // Translated here, not in kLangs: that table is built before main(),
        // where the language is not known yet. This runs on first use.
        for (const auto& l : kLangs) o.push_back(tr(l.label));
        return o;
    }();
    return v;
}

const std::vector<int>& uiWidths()
{
    static std::vector<int> v = [] {
        std::vector<int> o;
        for (const auto& s : kUiScales) o.push_back(s.width);
        return o;
    }();
    return v;
}

const std::vector<std::string>& uiWidthLabels()
{
    static std::vector<std::string> v = [] {
        std::vector<std::string> o;
        for (const auto& s : kUiScales) o.push_back(s.label);
        return o;
    }();
    return v;
}

void load()
{
    // config.json, or after a cut save its .tmp / the version before.
    std::string body;
    if (!fsutil::readFirst(kPath, body,
                           [](const std::string& b) { return fsutil::looksComplete(b); }))
    {
        brls::Logger::info("[config] no config.json, using defaults");
        defaultTmdbProxy();
        return;
    }

    std::string sTab = readStr(body, "startupTab", "home");
    cfg.startupTab = (sTab == "stremio") ? Tab::STREMIO :
                     (sTab == "jackett") ? Tab::JACKETT :
                     (sTab == "history") ? Tab::HISTORY :
                     (sTab == "home")    ? Tab::HOME :
                     (sTab == "bookmarks") ? Tab::BOOKMARKS : Tab::LOCAL;
    cfg.jackettUrl    = readStr(body, "jackettUrl", cfg.jackettUrl);
    cfg.jackettApiKey = readStr(body, "jackettApiKey", cfg.jackettApiKey);
    cfg.jackettSetupAsked = readBool(body, "jackettSetupAsked", cfg.jackettSetupAsked);
    cfg.torrServerUrl = readStr(body, "torrServerUrl", cfg.torrServerUrl);
    cfg.proxyUrl      = readStr(body, "proxyUrl", cfg.proxyUrl);
    if (!cfg.proxyUrl.empty() && (cfg.proxyUrl.find("tmdb") != std::string::npos || cfg.proxyUrl.find("/3") != std::string::npos || cfg.proxyUrl.find("rootu.top") != std::string::npos))
    {
        cfg.proxyUrl.clear();
    }
    cfg.tmdbApiKey    = readStr(body, "tmdbApiKey", cfg.tmdbApiKey);
    cfg.tmdbApiUrl    = readStr(body, "tmdbApiUrl", cfg.tmdbApiUrl);
    cfg.tmdbImageUrl  = readStr(body, "tmdbImageUrl", cfg.tmdbImageUrl);
    cfg.tmdbProxyUrl  = readStr(body, "tmdbProxyUrl", cfg.tmdbProxyUrl);
    if (!cfg.tmdbProxyUrl.empty() && cfg.tmdbProxyUrl.find("rootu.top") != std::string::npos)
    {
        cfg.tmdbProxyUrl.clear();
    }
    cfg.logging = readBool(body, "logging", cfg.logging);
    cfg.hide4k  = readBool(body, "hide4k", cfg.hide4k);
    cfg.rateGovernor = readBool(body, "rateGovernor", cfg.rateGovernor);
    cfg.ramStream    = readBool(body, "ramStream", cfg.ramStream);
    cfg.checkUpdates = readBool(body, "checkUpdates", cfg.checkUpdates);
    cfg.audioLang    = readStr(body, "audioLang", cfg.audioLang);
    cfg.subLang      = readStr(body, "subLang", cfg.subLang);
    cfg.subtitles    = readBool(body, "subtitles", cfg.subtitles);
    cfg.hwDecode     = readBool(body, "hwDecode", cfg.hwDecode);
    cfg.audioBoost   = readBool(body, "audioBoost", cfg.audioBoost);
    // mpv's own range; a hand-edited value outside it is pulled back in.
    auto eq = [&](const char* key, int dflt) {
        int v = readInt(body, key, dflt);
        return v < -100 ? -100 : v > 100 ? 100 : v;
    };
    cfg.picBrightness = eq("picBrightness", cfg.picBrightness);
    cfg.picContrast   = eq("picContrast", cfg.picContrast);
    cfg.picGamma      = eq("picGamma", cfg.picGamma);
    cfg.picSaturation = eq("picSaturation", cfg.picSaturation);

    cfg.language     = readStr(body, "language", cfg.language);
    cfg.accent       = readStr(body, "accent", cfg.accent);
    cfg.themeVariant = readStr(body, "themeVariant", cfg.themeVariant);
    cfg.listStyle    = readStr(body, "listStyle", cfg.listStyle);
    cfg.bufferMode   = readInt(body, "bufferMode", cfg.bufferMode);
    if (cfg.bufferMode < 0 || cfg.bufferMode > 2) cfg.bufferMode = 0;

    // After the language, which the default depends on. Version 1 files always
    // carried the key (whatever the user did), so only a version 2 file's key
    // is a choice; older ones get the language default.
    const int version = readInt(body, "configVersion", 1);
    // Version 1 had no "auto" and wrote "en" for everyone who never touched
    // the setting: those follow the console now.
    if (version < 2 && cfg.language == "en") cfg.language = "auto";
    if (version >= 2 && body.find("\"tmdbProxyEnabled\"") != std::string::npos)
    {
        cfg.tmdbProxyEnabled = readBool(body, "tmdbProxyEnabled", cfg.tmdbProxyEnabled);
        cfg.tmdbProxyUserSet = true;
    }
    else
        defaultTmdbProxy();

    cfg.dockedUiWidth = knownWidth(readInt(body, "dockedUiWidth", cfg.dockedUiWidth),
                                   cfg.dockedUiWidth);
    cfg.handheldUiWidth =
        knownWidth(readInt(body, "handheldUiWidth", cfg.handheldUiWidth),
                   cfg.handheldUiWidth);

    brls::Logger::info(
        "[config] startupTab={} logging={} hide4k={} checkUpdates={}",
        cfg.startupTab == Tab::STREMIO ? "stremio" :
        (cfg.startupTab == Tab::JACKETT ? "jackett" : "local"), cfg.logging,
        cfg.hide4k, cfg.checkUpdates);
}

bool save()
{
    const char* sTabStr = (cfg.startupTab == Tab::STREMIO) ? "stremio" :
                          (cfg.startupTab == Tab::JACKETT) ? "jackett" :
                          (cfg.startupTab == Tab::HISTORY) ? "history" :
                          (cfg.startupTab == Tab::HOME)    ? "home" :
                          (cfg.startupTab == Tab::BOOKMARKS) ? "bookmarks" : "local";
    std::string o = "{\n";
    auto kv = [&o](const char* key, const std::string& value) {
        o += "  \"";
        o += key;
        o += "\": ";
        o += value;
        o += ",\n";
    };
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    auto n = [](int v) { return std::to_string(v); };
    kv("configVersion", "2");
    kv("bufferMode", n(cfg.bufferMode));
    kv("startupTab", jsonStr(sTabStr));
    kv("jackettUrl", jsonStr(cfg.jackettUrl));
    kv("jackettApiKey", jsonStr(cfg.jackettApiKey));
    kv("jackettSetupAsked", b(cfg.jackettSetupAsked));
    kv("torrServerUrl", jsonStr(cfg.torrServerUrl));
    kv("proxyUrl", jsonStr(cfg.proxyUrl));
    kv("tmdbApiKey", jsonStr(cfg.tmdbApiKey));
    kv("tmdbApiUrl", jsonStr(cfg.tmdbApiUrl));
    kv("tmdbImageUrl", jsonStr(cfg.tmdbImageUrl));
    kv("tmdbProxyUrl", jsonStr(cfg.tmdbProxyUrl));
    // Only a choice the user made is kept: otherwise the proxy keeps following
    // the UI language.
    if (cfg.tmdbProxyUserSet) kv("tmdbProxyEnabled", b(cfg.tmdbProxyEnabled));
    kv("logging", b(cfg.logging));
    kv("hide4k", b(cfg.hide4k));
    kv("rateGovernor", b(cfg.rateGovernor));
    kv("ramStream", b(cfg.ramStream));
    kv("checkUpdates", b(cfg.checkUpdates));
    kv("audioLang", jsonStr(cfg.audioLang));
    kv("subLang", jsonStr(cfg.subLang));
    kv("subtitles", b(cfg.subtitles));
    kv("hwDecode", b(cfg.hwDecode));
    kv("audioBoost", b(cfg.audioBoost));
    kv("picBrightness", n(cfg.picBrightness));
    kv("picContrast", n(cfg.picContrast));
    kv("picGamma", n(cfg.picGamma));
    kv("picSaturation", n(cfg.picSaturation));
    kv("dockedUiWidth", n(cfg.dockedUiWidth));
    kv("handheldUiWidth", n(cfg.handheldUiWidth));
    kv("language", jsonStr(cfg.language));
    kv("accent", jsonStr(cfg.accent));
    kv("themeVariant", jsonStr(cfg.themeVariant));
    o += "  \"listStyle\": " + jsonStr(cfg.listStyle) + "\n}\n";

    // Atomic: rewritten in place, a power cut mid-save left an empty file --
    // the Jackett address and key, the TMDB key and every setting gone.
    if (!fsutil::writeAtomic(kPath, o))
    {
        brls::Logger::warning("[config] cannot write {}", kPath);
        return false;
    }
    return true;
}

} // namespace config
