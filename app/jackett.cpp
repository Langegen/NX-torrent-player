#include "jackett.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "http.hpp"
#include "json.hpp"

#ifndef JACKETT_NO_UI
#include <dirent.h>
#include <set>
#include <sys/stat.h>

#include <borealis.hpp>

#include "appdata.hpp"
#include "config.hpp"
#include "history.hpp"
#include "library.hpp"
#include "i18n.hpp"
#include "workers.hpp"
#endif

namespace jackett
{

namespace
{

std::string cleanBaseUrl(std::string url)
{
    while (!url.empty() && (url.back() == ' ' || url.back() == '\t' ||
                            url.back() == '\r' || url.back() == '\n'))
        url.pop_back();
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

std::string lowerAscii(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trimSpaces(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Converts a 32-char RFC 4648 base32 btih to 40-char lowercase hex so magnets
// from different indexers deduplicate against each other.
std::string base32ToHex(const std::string& b32)
{
    if (b32.size() != 32) return "";
    uint8_t bytes[20] = {};
    int bits = 0, val = 0, outIdx = 0;
    for (char ch : b32)
    {
        char c = (char)std::toupper((unsigned char)ch);
        int d = -1;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= '2' && c <= '7') d = 26 + (c - '2');
        else return "";
        val = (val << 5) | d;
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            if (outIdx < 20) bytes[outIdx++] = (uint8_t)((val >> bits) & 0xFF);
        }
    }
    if (outIdx != 20) return "";
    static const char* const kHex = "0123456789abcdef";
    std::string hex;
    hex.reserve(40);
    for (int i = 0; i < 20; i++)
    {
        hex += kHex[bytes[i] >> 4];
        hex += kHex[bytes[i] & 0xF];
    }
    return hex;
}

} // namespace

std::vector<std::string> splitTrackers(const std::string& tracker)
{
    std::vector<std::string> out;
    std::vector<std::string> seenLow;
    size_t from = 0;
    while (from <= tracker.size())
    {
        size_t comma = tracker.find(',', from);
        std::string part = trimSpaces(
            comma == std::string::npos ? tracker.substr(from) : tracker.substr(from, comma - from));
        if (!part.empty())
        {
            std::string low = lowerAscii(part);
            if (std::find(seenLow.begin(), seenLow.end(), low) == seenLow.end())
            {
                seenLow.push_back(std::move(low));
                out.push_back(std::move(part));
            }
        }
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    return out;
}

std::string compactTrackers(const std::vector<std::string>& trackers)
{
    if (trackers.empty()) return "";
    if (trackers.size() == 1) return trackers[0];
    return trackers[0] + " +" + std::to_string(trackers.size() - 1);
}

std::string extractInfoHash(const std::string& uri)
{
    if (uri.empty()) return "";
    std::string low = lowerAscii(uri);
    size_t p = low.find("btih:");
    if (p == std::string::npos) return "";
    p += 5;
    size_t end = p;
    while (end < low.size() && std::isalnum((unsigned char)low[end])) end++;
    std::string raw = low.substr(p, end - p);
    if (raw.size() == 40)
    {
        for (char c : raw)
            if (!std::isxdigit((unsigned char)c)) return "";
        return raw;
    }
    if (raw.size() == 32) return base32ToHex(raw);
    return "";
}

std::vector<Item> parseResultsJson(const std::string& resp)
{
    std::vector<Item> items;
    for (const auto& obj : json::objects(resp, "Results"))
    {
        std::string top = json::topLevel(obj);
        Item it;
        it.title = json::str(top, "Title");
        if (it.title.empty()) continue;
        it.tracker = json::str(top, "Tracker");
        if (it.tracker.empty()) it.tracker = json::str(top, "TrackerId");
        it.trackers     = splitTrackers(it.tracker);
        it.size         = json::integer(top, "Size");
        it.seeders      = (int)json::integer(top, "Seeders");
        it.peers        = (int)json::integer(top, "Peers");
        it.publishDate  = json::str(top, "PublishDate");
        it.magnetUri    = json::str(top, "MagnetUri");
        it.link         = json::str(top, "Link");
        it.categoryDesc = json::str(top, "CategoryDesc");

        for (long c : json::integers(obj, "Category"))
            it.categories.push_back((int)c);
        it.languages = json::strings(obj, "languages");

        it.infoHash = extractInfoHash(it.magnetUri);
        if (it.infoHash.empty() && it.link.rfind("magnet:", 0) == 0)
            it.infoHash = extractInfoHash(it.link);

        std::string infoObj = json::object(obj, "info");
        if (!infoObj.empty())
        {
            it.hasJacredInfo    = true;
            it.infoQuality      = (int)json::integer(infoObj, "quality", 0);
            it.infoVideotype    = json::str(infoObj, "videotype");
            it.infoVoices       = json::strings(infoObj, "voices");
            for (long s : json::integers(infoObj, "seasons"))
                if (s > 0 && s < 100) it.infoSeasons.push_back((int)s);
            it.infoTypes        = json::strings(infoObj, "types");
            it.infoName         = json::str(infoObj, "name");
            it.infoOriginalName = json::str(infoObj, "originalname");
            it.infoYear         = (int)json::integer(infoObj, "relased", 0);
            if (it.infoYear <= 0) it.infoYear = (int)json::integer(infoObj, "released", 0);
            it.infoImdb         = json::str(infoObj, "imdb");
            it.seedersUnknown   = json::boolean(infoObj, "seedersUnknown", false);
            it.seedersLive      = json::boolean(infoObj, "seedersLive", false);
        }

        items.push_back(std::move(it));
    }
    sortItems(items, SortBy::SEEDERS);
    return items;
}

std::string buildSearchUrl(const std::string& baseUrl, const std::string& apiKey,
                           const std::string& query, Category category,
                           const TitleHint* hint)
{
    std::string base = cleanBaseUrl(baseUrl);
    std::string url  = base + "/api/v2.0/indexers/all/results?";
    if (!apiKey.empty())
    {
        url += "apikey=" + http::urlEncode(apiKey) + "&";
    }
    url += "Query=" + http::urlEncode(query);

    if (category != Category::ALL)
    {
        url += "&Category[]=" + std::to_string(static_cast<int>(category));
        if (hint && hint->anime && category != Category::ANIME)
            url += "&Category[]=" + std::to_string(static_cast<int>(Category::ANIME));
    }
    // Lampa's extra parameters: a Jacred server then matches the title and
    // year itself (a "Fallout" search stops returning game repacks).
    if (hint)
    {
        if (!hint->title.empty()) url += "&title=" + http::urlEncode(hint->title);
        if (!hint->titleOriginal.empty())
            url += "&title_original=" + http::urlEncode(hint->titleOriginal);
        if (!hint->year.empty()) url += "&year=" + http::urlEncode(hint->year);
        const char* isSerial = hint->series ? "2" : (hint->anime ? "0" : "1");
        url += std::string("&is_serial=") + isSerial;
    }
    return url;
}

void sortItems(std::vector<Item>& items, SortBy sort)
{
    switch (sort)
    {
        case SortBy::SEEDERS:
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                if (a.seeders != b.seeders) return a.seeders > b.seeders;
                return a.size > b.size;
            });
            break;

        case SortBy::SIZE:
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                if (a.size != b.size) return a.size > b.size;
                return a.seeders > b.seeders;
            });
            break;

        case SortBy::DATE:
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                if (a.publishDate != b.publishDate) return a.publishDate > b.publishDate;
                return a.seeders > b.seeders;
            });
            break;
    }
}

#ifndef JACKETT_NO_UI

namespace
{

std::string sanitizeFilename(const std::string& name)
{
    std::string out;
    for (char c : name)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c < 32)
            out += '_';
        else
            out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    if (out.empty()) out = "torrent";
    return out;
}

bool appendMagnetToFile(const std::string& path, const std::string& magnet)
{
    FILE* f = std::fopen(path.c_str(), "a+");
    if (!f) return false;
    std::fprintf(f, "\n%s\n", magnet.c_str());
    std::fclose(f);
    return true;
}

} // namespace

std::string formatSize(int64_t bytes)
{
    if (bytes <= 0) return "";
    // In the UI language ("ГБ"): the size sits next to Russian text.
    const char* unit[] = { tr("B"), tr("KB"), tr("MB"), tr("GB"), tr("TB") };
    double v = (double)bytes;
    int i    = 0;
    while (v >= 1024.0 && i < 4)
    {
        v /= 1024.0;
        i++;
    }
    // A decimal up to 100 of a unit: 14.8 GB and 15.4 GB are different
    // choices, "15 GB" both. MB and KB need none.
    char buf[32];
    std::snprintf(buf, sizeof(buf), (v < 100.0 && i >= 3) ? "%.1f %s" : "%.0f %s", v,
                  unit[i]);
    return buf;
}

std::string normalizeDownloadLink(const std::string& link)
{
    if (link.empty() || link.rfind("magnet:", 0) == 0)
        return link;

    std::string base = cleanBaseUrl(config::get().jackettUrl);
    std::string out  = link;

    if (!base.empty())
    {
        if (out[0] == '/')
        {
            out = base + out;
        }
        else if (out.rfind("http://localhost:", 0) == 0 ||
                 out.rfind("http://127.0.0.1:", 0) == 0)
        {
            size_t slash = out.find('/', 17);
            if (slash != std::string::npos)
                out = base + out.substr(slash);
        }
    }

    const std::string& apiKey = config::get().jackettApiKey;
    if (!apiKey.empty() && out.find("jackett_apikey=") == std::string::npos)
    {
        char sep = (out.find('?') == std::string::npos) ? '?' : '&';
        out += sep;
        out += "jackett_apikey=" + http::urlEncode(apiKey);
    }

    return out;
}

void searchAsync(const std::string& query, Category category,
                 std::function<void(SearchResult)> cb, const TitleHint* hint)
{
    const auto& cfg = config::get();
    if (cfg.jackettUrl.empty())
    {
        SearchResult res;
        res.ok    = false;
        res.error = tr("Jackett is not configured. Set URL in Options.");
        cb(std::move(res));
        return;
    }

    std::string base = cleanBaseUrl(cfg.jackettUrl);
    std::string url  = buildSearchUrl(base, cfg.jackettApiKey, query, category, hint);

    // Not the URL itself: it carries the API key, and the log is a plain file
    // on the SD card that people attach to bug reports.
    brls::Logger::info("[jackett] querying {} for \"{}\"", base, query);

    workers::run([url, cb, base]() {
        std::string resp, err;
        long status = 0;
        Diagnosis d;
        std::vector<Item> items;
        if (!http::get(url, resp, err, nullptr, 25, &status))
        {
            brls::Logger::error("[jackett] http request failed: {}", err);
            d = diagnoseTransport(err);
        }
        else
        {
            d = diagnoseAnswer(status, resp, true);
            if (d.problem != Problem::None)
                brls::Logger::error("[jackett] bad answer (HTTP {}): {}", status,
                                    resp.substr(0, 160));
            else
                items = parseResultsJson(resp);
            if (d.failed > 0)
                brls::Logger::warning("[jackett] {} of {} indexers failed, first: {}",
                                      d.failed, d.indexers, d.detail);
        }
        brls::Logger::info("[jackett] search returned {} items", items.size());

        brls::sync([cb, d, base, items = std::move(items)]() mutable {
            SearchResult r;
            r.ok = d.problem == Problem::None;
            if (r.ok)
                r.items = std::move(items);
            else
                r.error = explain(d, base);
            cb(std::move(r));
        });
    });
}

void testConnectionAsync(const std::string& url, const std::string& apiKey,
                         std::function<void(bool ok, const std::string& msg)> cb)
{
    std::string base    = cleanBaseUrl(url);
    std::string testUrl = base + "/api/v2.0/indexers/all/results?";
    if (!apiKey.empty())
    {
        testUrl += "apikey=" + http::urlEncode(apiKey) + "&";
    }
    testUrl += "Query=test";

    brls::Logger::info("[jackett] testing connection to {}", base);

    workers::run([testUrl, cb, base]() {
        std::string resp, err;
        long status = 0;
        Diagnosis d = http::get(testUrl, resp, err, nullptr, 25, &status)
                          ? diagnoseAnswer(status, resp, false)
                          : diagnoseTransport(err);
        brls::sync([cb, d, base]() {
            if (d.problem != Problem::None)
            {
                cb(false, explain(d, base));
                return;
            }
            // Working: say how many indexers it has, and how many of them
            // failed this time (a tracker down, a login expired).
            // A Jacred mirror lists no indexers: it searches its own index.
            if (!d.hasIndexerList)
            {
                cb(true, tr("The server works."));
                return;
            }
            std::string msg = tr("Jackett works. Indexers: ") + std::to_string(d.indexers);
            if (d.failed > 0)
                msg += tr(", with errors: ") + std::to_string(d.failed) + "\n" + d.detail;
            cb(true, msg);
        });
    });
}

std::string explain(const Diagnosis& d, const std::string& base)
{
    switch (d.problem)
    {
        case Problem::None:
            return "";
        case Problem::BadUrl:
            return tr("The Jackett address is not a valid URL. Enter it like "
                      "http://192.168.1.100:9117.");
        case Problem::NoHost:
            return tr("The Jackett address does not resolve. Check its spelling, or use "
                      "the computer's IP address (like http://192.168.1.100:9117).");
        case Problem::Unreachable:
            return tr("Nothing answers at ") + base +
                   tr(". Check that Jackett is running, that the console is on the same "
                      "network, and the port (9117 by default).");
        case Problem::Timeout:
            return tr("Jackett did not answer in time. Indexers can be slow: try again, "
                      "and if it keeps happening, check the network.");
        case Problem::Certificate:
            return tr("Secure connection to Jackett failed: ") + d.detail;
        case Problem::BadKey:
            return tr("Jackett rejected the API key. Copy it from the top right corner of "
                      "the Jackett web page into Options, Jackett.");
        case Problem::Forbidden:
            return tr("The server refused the request (HTTP 403). A public server can block "
                      "some networks: try another address, or your own Jackett.");
        case Problem::NotJackett:
            return tr("Something answers at this address, but not Jackett's API. Check "
                      "the port (9117 by default) and the API key.");
        case Problem::NoIndexers:
            return tr("Jackett works, but has no indexers. Add trackers with \"Add "
                      "indexer\" on the Jackett web page.");
        case Problem::IndexersFailed:
            return tr("Every Jackett indexer returned an error. Check them on the Jackett "
                      "web page.") + std::string("\n") + d.detail;
        case Problem::Other:
            break;
    }
    return d.detail.empty() ? std::string(tr("Invalid response from Jackett server"))
                            : d.detail;
}

void downloadTorrentAsync(const std::string& link, const std::string& destPath,
                          std::function<void(bool ok, const std::string& err,
                                             const std::string& magnet)> cb)
{
    std::string targetUrl = normalizeDownloadLink(link);
    brls::Logger::info("[jackett] downloading torrent to {}", destPath);

    workers::run([targetUrl, destPath, cb]() {
        std::string err, magnet;
        bool ok = http::downloadTorrent(targetUrl, destPath, magnet, err);
        if (ok && !magnet.empty())
            brls::Logger::info("[jackett] link redirected to a magnet");
        brls::sync([cb, ok, err, magnet]() { cb(ok, err, magnet); });
    });
}

std::string cachedTorrentPath(const std::string& link)
{
    // FNV-1a over the link: stable across runs, which is all a file name needs.
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : link)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.torrent", (unsigned long long)h);
    mkdir(APPDATA_JACKETT, 0777);
    return std::string(APPDATA_JACKETT) + "/" + name;
}

void pruneCache()
{
    DIR* d = opendir(APPDATA_JACKETT);
    if (!d) return;

    // Matched by file name: the names are hashes, so the folder part of a
    // stored path ("sdmc:/..." or not) does not matter.
    static const std::string marker = "/jackett/";
    std::set<std::string> used;
    auto keep = [&](const std::string& source) {
        size_t at = source.rfind(marker);
        if (at != std::string::npos) used.insert(source.substr(at + marker.size()));
    };
    for (const auto& it : history::all()) keep(it.source);
    // The library resumes episodes and films from their own files, long after
    // the history may have dropped them (it keeps 100).
    for (const auto& e : library::all())
    {
        keep(e.source);
        for (const auto& kv : e.episodes) keep(kv.second.source);
    }

    int removed = 0;
    struct dirent* e;
    while ((e = readdir(d)))
    {
        std::string name = e->d_name;
        if (name == "." || name == ".." || used.count(name)) continue;
        if (std::remove((std::string(APPDATA_JACKETT) + "/" + name).c_str()) == 0)
            removed++;
    }
    closedir(d);
    if (removed)
        brls::Logger::info("[jackett] pruned {} cached torrent(s)", removed);
}

void saveToLocalAsync(const Item& item,
                      std::function<void(bool ok, const std::string& msg)> cb)
{
    mkdir(APPDATA_TORRENTS, 0777);

    std::string link = item.link;
    std::string magnet = (!link.empty() && link.rfind("magnet:", 0) == 0) ? link : item.magnetUri;
    bool isHttp = !link.empty() && link.rfind("magnet:", 0) != 0;

    if (isHttp)
    {
        std::string filename = sanitizeFilename(item.title) + ".torrent";
        std::string destPath = std::string(APPDATA_TORRENTS) + "/" + filename;

        downloadTorrentAsync(item.link, destPath, [item, destPath, magnet, cb](bool ok, const std::string& err,
                                                                               const std::string& redirected) {
            if (ok && !redirected.empty())
            {
                if (appendMagnetToFile(APPDATA_MAGNETS, redirected))
                    cb(true, tr("Saved magnet link to Local library"));
                else
                    cb(false, tr("Failed to save magnet link"));
                return;
            }
            if (ok)
            {
                brls::Logger::info("[jackett] saved torrent to {}", destPath);
                cb(true, tr("Saved to Local library"));
                return;
            }

            // If .torrent download failed, fallback to saving magnetUri if present
            if (!magnet.empty() &&
                appendMagnetToFile(APPDATA_MAGNETS, magnet))
            {
                brls::Logger::info("[jackett] fallback saved magnet to {}", APPDATA_MAGNETS);
                cb(true, tr("Saved magnet link to Local library"));
            }
            else
            {
                cb(false, err.empty() ? tr("Failed to save torrent") : err);
            }
        });
        return;
    }

    if (!magnet.empty())
    {
        if (appendMagnetToFile(APPDATA_MAGNETS, magnet))
        {
            brls::Logger::info("[jackett] saved magnet to {}", APPDATA_MAGNETS);
            cb(true, tr("Saved magnet link to Local library"));
        }
        else
        {
            cb(false, tr("Failed to save magnet link"));
        }
        return;
    }

    cb(false, tr("No playable link or magnet URI found for this torrent."));
}

#endif // JACKETT_NO_UI

} // namespace jackett
