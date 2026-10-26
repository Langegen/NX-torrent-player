#include "torrserver.hpp"

#include <cctype>
#include <cstdint>

#include "http.hpp"
#include "json.hpp"

#ifndef TORRSERVER_NO_UI  // the PC tests build the pure functions alone
#include <borealis.hpp>
#include "config.hpp"
#endif

namespace torrserver
{

namespace
{
bool hasVideoExt(const std::string& path)
{
    static const char* exts[] = { ".mkv", ".mp4", ".avi", ".m4v", ".mov",
                                  ".ts",  ".m2ts", ".webm", ".wmv", ".mpg",
                                  ".mpeg" };
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    for (const char* x : exts)
        if (e == x) return true;
    return false;
}
} // namespace

std::string normalizeBase(std::string url)
{
    while (!url.empty() && std::isspace((unsigned char)url.back())) url.pop_back();
    size_t lead = 0;
    while (lead < url.size() && std::isspace((unsigned char)url[lead])) lead++;
    url = url.substr(lead);
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.empty()) return url;
    if (url.find("://") == std::string::npos) url = "http://" + url;
    return url;
}

std::string statUrl(const std::string& base, const std::string& link)
{
    return base + "/stream?link=" + http::urlEncode(link) + "&stat";
}

std::string playUrl(const std::string& base, const std::string& link, int id)
{
    return base + "/stream?link=" + http::urlEncode(link) +
           "&index=" + std::to_string(id) + "&play";
}

int pickFile(const std::string& statJson, int fileIndex)
{
    if (fileIndex >= 0) return fileIndex + 1;

    int bestVideo = -1, bestAny = -1;
    double videoLen = -1, anyLen = -1;
    for (const auto& f : json::objects(statJson, "file_stats"))
    {
        int id = (int)json::integer(f, "id", -1);
        double len = json::number(f, "length", 0);
        if (id <= 0) continue;
        if (len > anyLen) { anyLen = len; bestAny = id; }
        if (hasVideoExt(json::str(f, "path")) && len > videoLen)
        {
            videoLen = len;
            bestVideo = id;
        }
    }
    return bestVideo > 0 ? bestVideo : bestAny;
}

bool resolve(const std::string& base, const std::string& link, int fileIndex,
             std::string& url, std::string& err)
{
    int id = fileIndex >= 0 ? fileIndex + 1 : -1;
    if (id < 0)
    {
        std::string resp;
        // The server answers once it has the metadata: give it the time a
        // slow swarm needs, the on-device engine would wait as long.
        if (!http::get(statUrl(base, link), resp, err, nullptr, 120))
        {
            err = "TorrServer: " + err;
            return false;
        }
        id = pickFile(resp, -1);
        if (id < 0)
        {
            err = "TorrServer returned no files for this torrent";
            return false;
        }
    }
    url = playUrl(base, link, id);
    return true;
}

#ifndef TORRSERVER_NO_UI
std::string base() { return normalizeBase(config::get().torrServerUrl); }

void testConnectionAsync(const std::string& base,
                         std::function<void(bool ok, const std::string& msg)> cb)
{
    std::string url = normalizeBase(base) + "/echo";
    brls::async([url, cb]() {
        std::string resp, err;
        bool ok = http::get(url, resp, err, nullptr, 10);
        while (!resp.empty() && std::isspace((unsigned char)resp.back())) resp.pop_back();
        if (ok && resp.empty()) ok = false, err = "empty answer";
        // Anything long is a web page, not TorrServer's "MatriX.135".
        if (ok && resp.size() > 64) ok = false, err = "not a TorrServer";
        brls::sync([cb, ok, resp, err]() { cb(ok, ok ? resp : err); });
    });
}
#endif

} // namespace torrserver
