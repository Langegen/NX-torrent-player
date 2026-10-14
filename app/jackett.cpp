#include "jackett.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>

#include <borealis.hpp>

#include "appdata.hpp"
#include "config.hpp"
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
    if (cfg.jackettUrl.empty() || cfg.jackettApiKey.empty())
    {
        SearchResult res;
        res.ok    = false;
        res.error = tr("Jackett is not configured. Set URL and API key in Options.");
        cb(std::move(res));
        return;
    }

    std::string base = cleanBaseUrl(cfg.jackettUrl);
    std::string url  = base + "/api/v2.0/indexers/all/results?apikey=" +
                      http::urlEncode(cfg.jackettApiKey) +
                      "&Query=" + http::urlEncode(query);

    if (category != Category::ALL)
    {
        url += "&Category[]=" + std::to_string(static_cast<int>(category));
    }

    brls::Logger::info("[jackett] querying {}", url);

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
    std::string testUrl = base + "/api/v2.0/indexers/all/results?apikey=" +
                          http::urlEncode(apiKey) + "&Query=test";

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
        if (!apiErr.empty())
        {
            brls::sync([cb, apiErr]() { cb(false, apiErr); });
            return;
        }

        brls::sync([cb]() { cb(true, ""); });
    });
}

void downloadTorrentAsync(const std::string& link, const std::string& destPath,
                          std::function<void(bool ok, const std::string& err)> cb)
{
    std::string targetUrl = normalizeDownloadLink(link);
    brls::Logger::info("[jackett] downloading torrent from {} to {}", targetUrl, destPath);

    brls::async([targetUrl, destPath, cb]() {
        std::string err;
        bool ok = http::download(targetUrl, destPath, err);
        brls::sync([cb, ok, err]() { cb(ok, err); });
    });
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

        downloadTorrentAsync(item.link, destPath, [item, destPath, magnet, cb](bool ok, const std::string& err) {
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
