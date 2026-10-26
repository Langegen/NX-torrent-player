#include "jackett.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <set>
#include <sys/stat.h>

#include <borealis.hpp>

#include "appdata.hpp"
#include "config.hpp"
#include "history.hpp"
#include "http.hpp"
#include "i18n.hpp"
#include "json.hpp"

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
    const char* unit[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)bytes;
    int i    = 0;
    while (v >= 1024.0 && i < 4)
    {
        v /= 1024.0;
        i++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), (v < 10.0 && i > 0) ? "%.1f %s" : "%.0f %s",
                  v, unit[i]);
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

void searchAsync(const std::string& query, Category category,
                 std::function<void(SearchResult)> cb)
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
    std::string url  = base + "/api/v2.0/indexers/all/results?";
    if (!cfg.jackettApiKey.empty())
    {
        url += "apikey=" + http::urlEncode(cfg.jackettApiKey) + "&";
    }
    url += "Query=" + http::urlEncode(query);

    if (category != Category::ALL)
    {
        url += "&Category[]=" + std::to_string(static_cast<int>(category));
    }

    // Not the URL itself: it carries the API key, and the log is a plain file
    // on the SD card that people attach to bug reports.
    brls::Logger::info("[jackett] querying {} for \"{}\"", base, query);

    brls::async([url, cb]() {
        std::string resp, err;
        bool ok = http::get(url, resp, err);
        if (!ok)
        {
            brls::Logger::error("[jackett] http request failed: {}", err);
            brls::sync([cb, err]() {
                SearchResult r;
                r.ok    = false;
                r.error = err;
                cb(std::move(r));
            });
            return;
        }

        std::vector<std::string> rawObjects = json::objects(resp, "Results");
        if (rawObjects.empty())
        {
            std::string apiErr = json::str(resp, "error");
            if (apiErr.empty())
                apiErr = json::str(resp, "message");
            if (!apiErr.empty())
            {
                brls::Logger::error("[jackett] api error: {}", apiErr);
                brls::sync([cb, apiErr]() {
                    SearchResult r;
                    r.ok    = false;
                    r.error = apiErr;
                    cb(std::move(r));
                });
                return;
            }

            if (resp.find("\"Results\"") == std::string::npos &&
                resp.find("\"Indexers\"") == std::string::npos)
            {
                brls::Logger::error("[jackett] invalid response: {}", resp.substr(0, 120));
                brls::sync([cb]() {
                    SearchResult r;
                    r.ok    = false;
                    r.error = tr("Invalid response from Jackett server");
                    cb(std::move(r));
                });
                return;
            }
        }

        std::vector<Item> items;
        items.reserve(rawObjects.size());
        for (const auto& obj : rawObjects)
        {
            Item it;
            it.title = json::str(obj, "Title");
            if (it.title.empty()) continue;
            it.tracker = json::str(obj, "Tracker");
            if (it.tracker.empty()) it.tracker = json::str(obj, "TrackerId");
            it.size         = json::integer(obj, "Size");
            it.seeders      = (int)json::integer(obj, "Seeders");
            it.peers        = (int)json::integer(obj, "Peers");
            it.publishDate  = json::str(obj, "PublishDate");
            it.magnetUri    = json::str(obj, "MagnetUri");
            it.link         = json::str(obj, "Link");
            it.categoryDesc = json::str(obj, "CategoryDesc");
            items.push_back(std::move(it));
        }

        brls::Logger::info("[jackett] search returned {} items", items.size());
        sortItems(items, SortBy::SEEDERS);

        brls::sync([cb, items = std::move(items)]() mutable {
            SearchResult r;
            r.ok    = true;
            r.items = std::move(items);
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

    brls::async([testUrl, cb]() {
        std::string resp, err;
        bool ok = http::get(testUrl, resp, err);
        if (!ok)
        {
            brls::sync([cb, err]() { cb(false, err); });
            return;
        }

        std::string apiErr = json::str(resp, "error");
        if (apiErr.empty())
            apiErr = json::str(resp, "message");
        if (!apiErr.empty())
        {
            brls::sync([cb, apiErr]() { cb(false, apiErr); });
            return;
        }

        if (resp.find("\"Results\"") == std::string::npos &&
            resp.find("\"Indexers\"") == std::string::npos)
        {
            brls::sync([cb]() { cb(false, tr("Invalid response from server. Check URL and API key.")); });
            return;
        }

        brls::sync([cb]() { cb(true, ""); });
    });
}

void downloadTorrentAsync(const std::string& link, const std::string& destPath,
                          std::function<void(bool ok, const std::string& err,
                                             const std::string& magnet)> cb)
{
    std::string targetUrl = normalizeDownloadLink(link);
    brls::Logger::info("[jackett] downloading torrent to {}", destPath);

    brls::async([targetUrl, destPath, cb]() {
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
    for (const auto& it : history::all())
    {
        size_t at = it.source.rfind(marker);
        if (at != std::string::npos)
            used.insert(it.source.substr(at + marker.size()));
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

} // namespace jackett
