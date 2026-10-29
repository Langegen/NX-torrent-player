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

// The releases of a search answer, most seeded first.
std::vector<Item> parseResults(const std::string& resp)
{
    std::vector<Item> items;
    for (const auto& obj : json::objects(resp, "Results"))
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
    sortItems(items, SortBy::SEEDERS);
    return items;
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

    brls::async([url, cb, base]() {
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
                items = parseResults(resp);
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

    brls::async([testUrl, cb, base]() {
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
