#include "lampac_provider.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>

#include "json.hpp"

#ifndef ONLINE_NO_UI
#include "http.hpp"
#endif

namespace online
{
namespace
{

std::string trimStr(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string toLowerStr(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += (char)std::tolower((unsigned char)c);
    return out;
}

bool isLocalHostText(const std::string& host)
{
    std::string low = toLowerStr(host);
    if (low == "localhost" || low == "127.0.0.1" || low.rfind("192.168.", 0) == 0 ||
        low.rfind("10.", 0) == 0 || low.rfind("172.", 0) == 0)
        return true;
    return low.find('.') == std::string::npos;
}

std::string pctEncode(const std::string& s)
{
    return encodePct(s);
}

bool hasQueryParam(const std::string& url, const std::string& key)
{
    size_t q = url.find('?');
    if (q == std::string::npos) return false;
    std::string query = url.substr(q + 1);
    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = part.find('=');
        std::string k = (eq == std::string::npos) ? part : part.substr(0, eq);
        if (k == key) return true;
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return false;
}

std::string appendQueryParam(const std::string& url, const std::string& key, const std::string& val)
{
    if (hasQueryParam(url, key)) return url;
    char sep = (url.find('?') == std::string::npos) ? '?' : '&';
    return url + sep + key + "=" + pctEncode(val);
}

int readIntOrStr(const std::string& obj, const char* key, int dflt = 0)
{
    long long iv = json::integer(obj, key, LLONG_MIN);
    if (iv != LLONG_MIN) return (int)iv;
    std::string sv = json::str(obj, key);
    if (!sv.empty())
    {
        char* end = nullptr;
        long v = std::strtol(sv.c_str(), &end, 10);
        if (end != sv.c_str()) return (int)v;
    }
    return dflt;
}

std::vector<OnlineSubtitle> parseSubtitlesArray(const std::string& obj, const std::string& baseUrl)
{
    std::vector<OnlineSubtitle> out;
    for (const auto& sObj : json::objects(obj, "subtitles"))
    {
        OnlineSubtitle sub;
        sub.url   = resolveRelativeUrl(baseUrl, json::str(sObj, "url"));
        sub.label = json::str(sObj, "label");
        sub.lang  = json::str(sObj, "lang");
        if (sub.lang.empty()) sub.lang = sub.label;
        if (sub.label.empty()) sub.label = sub.lang;
        if (!sub.url.empty()) out.push_back(std::move(sub));
    }
    return out;
}

std::vector<std::string> parseHeadersObj(const std::string& obj)
{
    std::vector<std::string> out;
    std::string hObj = json::object(obj, "headers");
    if (hObj.empty()) return out;
    for (const auto& kv : json::stringMap(hObj))
    {
        if (!kv.first.empty() && !kv.second.empty())
            out.push_back(kv.first + ": " + kv.second);
    }
    return out;
}

// Parses [[1,25],[2,12]] from BWA's "src_seasons".
std::vector<SeasonOption> parseSrcSeasons(const std::string& body)
{
    std::vector<SeasonOption> out;
    size_t k = body.find("\"src_seasons\"");
    if (k == std::string::npos) return out;
    size_t colon = body.find(':', k + 13);
    if (colon == std::string::npos) return out;
    size_t open = colon + 1;
    while (open < body.size() && std::isspace((unsigned char)body[open])) open++;
    if (open >= body.size() || body[open] != '[') return out;

    int depth = 0;
    for (size_t i = open + 1; i < body.size(); i++)
    {
        char c = body[i];
        if (c == '[')
        {
            if (depth == 0)
            {
                // Parse inner pair [season, count]
                size_t close = body.find(']', i + 1);
                if (close == std::string::npos) break;
                std::string inner = body.substr(i + 1, close - i - 1);
                char* end1 = nullptr;
                long sNum = std::strtol(inner.c_str(), &end1, 10);
                if (end1 != inner.c_str() && sNum >= 0)
                {
                    int epCnt = 0;
                    const char* comma = std::strchr(end1, ',');
                    if (comma) epCnt = (int)std::strtol(comma + 1, nullptr, 10);
                    SeasonOption so;
                    so.number       = (int)sNum;
                    so.episodeCount = epCnt;
                    so.name         = std::to_string((int)sNum);
                    out.push_back(std::move(so));
                }
                i = close;
                continue;
            }
            depth++;
        }
        else if (c == ']')
        {
            if (depth == 0) break;
            depth--;
        }
    }
    return out;
}

std::vector<OnlineEntry> groupBwaStreams(const std::vector<std::string>& streamObjs,
                                         const std::string& baseUrl,
                                         int reqSeason, int reqEpisode)
{
    std::vector<OnlineEntry> entries;
    std::map<std::string, size_t> voiceIdx;

    for (const auto& sObj : streamObjs)
    {
        std::string url = resolveRelativeUrl(baseUrl, json::str(sObj, "url"));
        if (url.empty()) continue;

        std::string voice = trimStr(json::str(sObj, "voice"));
        if (voice.empty()) voice = "Original";
        std::string qLabel = trimStr(json::str(sObj, "quality"));
        if (qLabel.empty()) qLabel = "Auto";
        int qHeight = parseQualityHeight(qLabel);

        auto it = voiceIdx.find(voice);
        if (it == voiceIdx.end())
        {
            OnlineEntry e;
            e.method    = OnlineMethod::Play;
            e.voiceName = voice;
            e.title     = voice;
            e.season    = reqSeason;
            e.episode   = reqEpisode;
            e.headers   = parseHeadersObj(sObj);
            e.subtitles = parseSubtitlesArray(sObj, baseUrl);
            voiceIdx[voice] = entries.size();
            entries.push_back(std::move(e));
            it = voiceIdx.find(voice);
        }

        OnlineEntry& entry = entries[it->second];
        bool dup = false;
        for (const auto& ex : entry.qualities)
        {
            if (ex.url == url) { dup = true; break; }
        }
        if (!dup) entry.qualities.push_back({ qLabel, url, qHeight });
    }

    for (auto& e : entries)
    {
        sortQualitiesDesc(e.qualities);
        if (!e.qualities.empty())
        {
            e.url        = e.qualities[0].url;
            e.maxQuality = e.qualities[0].label;
        }
    }
    return entries;
}

} // namespace

ParsedLampaUrl parseLampaPluginUrl(const std::string& rawUrl, const std::string& explicitToken)
{
    ParsedLampaUrl res;
    std::string u = trimStr(rawUrl);
    if (u.empty()) return res;

    if (u.find("://") == std::string::npos)
    {
        size_t slash = u.find('/');
        std::string host = (slash == std::string::npos) ? u : u.substr(0, slash);
        size_t colon = host.find(':');
        if (colon != std::string::npos) host = host.substr(0, colon);
        u = std::string(isLocalHostText(host) ? "http://" : "https://") + u;
    }

    // Extract query params (token / uid)
    size_t qPos = u.find('?');
    std::string noQuery = (qPos == std::string::npos) ? u : u.substr(0, qPos);
    std::string query   = (qPos == std::string::npos) ? "" : u.substr(qPos + 1);

    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos)
        {
            std::string k = toLowerStr(pair.substr(0, eq));
            std::string v = decodePct(pair.substr(eq + 1));
            if ((k == "token" || k == "uid") && res.token.empty() && !v.empty())
                res.token = v;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    if (!trimStr(explicitToken).empty()) res.token = trimStr(explicitToken);

    while (!noQuery.empty() && noQuery.back() == '/') noQuery.pop_back();

    std::string low = toLowerStr(noQuery);
    size_t schemeEnd = noQuery.find("://");
    size_t pathStart = (schemeEnd == std::string::npos) ? 0 : noQuery.find('/', schemeEnd + 3);

    // Check /online/js/<token> or /online/js
    size_t ojs = low.find("/online/js");
    if (ojs != std::string::npos && (pathStart == std::string::npos || ojs >= pathStart))
    {
        res.looksLikeJsScript = true;
        std::string after = noQuery.substr(ojs + 10);
        if (!after.empty() && after[0] == '/')
        {
            std::string tok = after.substr(1);
            size_t sl = tok.find('/');
            if (sl != std::string::npos) tok = tok.substr(0, sl);
            if (!tok.empty() && res.token.empty()) res.token = tok;
        }
        noQuery = noQuery.substr(0, ojs);
    }
    else if (low.size() >= 3 && low.compare(low.size() - 3, 3, ".js") == 0)
    {
        res.looksLikeJsScript = true;
        size_t lastSlash = noQuery.rfind('/');
        if (lastSlash != std::string::npos &&
            (schemeEnd == std::string::npos || lastSlash > schemeEnd + 2))
        {
            noQuery = noQuery.substr(0, lastSlash);
        }
    }
    else if (pathStart != std::string::npos)
    {
        std::string path = low.substr(pathStart);
        if (path == "/o" || path == "/rc" || path == "/lite" || path == "/online")
            noQuery = noQuery.substr(0, pathStart);
    }

    while (!noQuery.empty() && noQuery.back() == '/') noQuery.pop_back();
    res.baseUrl = noQuery;
    return res;
}

std::string resolveRelativeUrl(const std::string& baseUrl, const std::string& href)
{
    std::string h = trimStr(href);
    if (h.empty()) return "";
    if (h.rfind("http://", 0) == 0 || h.rfind("https://", 0) == 0) return h;

    size_t sc = baseUrl.find("://");
    if (sc == std::string::npos) return h;
    std::string scheme = baseUrl.substr(0, sc);
    if (h.rfind("//", 0) == 0) return scheme + ":" + h;

    size_t pathPos = baseUrl.find('/', sc + 3);
    std::string origin = (pathPos == std::string::npos) ? baseUrl : baseUrl.substr(0, pathPos);
    if (h[0] == '/') return origin + h;
    if (h[0] == '?') return baseUrl + h;
    return baseUrl + "/" + h;
}

std::string ensureLampacParams(const std::string& url, const std::string& token, bool addRjson)
{
    if (url.empty()) return "";
    std::string out = url;
    if (addRjson && !hasQueryParam(out, "rjson"))
        out = appendQueryParam(out, "rjson", "true");
    if (!token.empty() && !hasQueryParam(out, "token"))
        out = appendQueryParam(out, "token", token);
    return out;
}

std::string parseExternalIdsKp(const std::string& body)
{
    long long kpInt = json::integer(body, "kinopoisk_id", 0);
    if (kpInt > 0) return std::to_string(kpInt);
    std::string kpStr = json::str(body, "kinopoisk_id");
    if (!kpStr.empty() && kpStr != "0") return kpStr;
    return "";
}

std::vector<BalancerInfo> parseLampacEventsJson(const std::string& body,
                                                const std::string& pluginId,
                                                const std::string& baseUrl,
                                                const std::string& token)
{
    std::vector<BalancerInfo> out;
    std::vector<std::string> items = json::arrayObjects(body);
    if (items.empty()) items = json::objects(body, "balansers");
    if (items.empty()) items = json::objects(body, "data");
    if (items.empty()) items = json::objects(body, "results");

    for (const auto& obj : items)
    {
        if (!json::boolean(obj, "show", true)) continue;

        bool accountOnly = json::boolean(obj, "account_only", false);
        bool bound       = json::boolean(obj, "bound", false);
        if (accountOnly && !bound && token.empty()) continue;

        BalancerInfo b;
        b.pluginId    = pluginId;
        b.id          = json::str(obj, "balanser");
        if (b.id.empty()) b.id = json::str(obj, "id");
        b.name        = json::str(obj, "name");
        if (b.name.empty()) b.name = b.id;
        b.url         = resolveRelativeUrl(baseUrl, json::str(obj, "url"));
        b.accountOnly = accountOnly;
        b.bound       = bound;
        b.avoid       = json::boolean(obj, "avoid", false);
        b.available   = true;

        std::string lowId = toLowerStr(b.id);
        if (lowId == "torrserver" || lowId == "jacred" || lowId == "torrents") continue;
        if (b.id.empty() && b.url.empty()) continue;
        if (b.id.empty()) b.id = b.name;
        if (!b.url.empty()) b.url = ensureLampacParams(b.url, token, true);
        out.push_back(std::move(b));
    }

    std::stable_sort(out.begin(), out.end(), [](const BalancerInfo& a, const BalancerInfo& b) {
        return !a.avoid && b.avoid;
    });
    return out;
}

PageResult parseLampacPageJson(const std::string& body,
                               const std::string& baseUrl,
                               const std::string& token,
                               int reqSeason,
                               int reqEpisode,
                               bool isSeries)
{
    PageResult res;
    if (trimStr(body).empty())
    {
        res.error = "Empty response";
        return res;
    }
    std::string err = json::str(body, "error");
    if (!err.empty() && json::objects(body, "data").empty() &&
        json::objects(body, "streams").empty())
    {
        res.error = err;
        return res;
    }

    std::vector<std::string> bwaStreams = json::objects(body, "streams");
    std::vector<std::string> dataObjs   = json::objects(body, "data");

    // Format A: BWA JSON ({ "streams": [...], "src_seasons": [[1,25],...] })
    if (dataObjs.empty() && (!bwaStreams.empty() || body.find("\"streams\"") != std::string::npos))
    {
        res.ok             = true;
        res.seasons        = parseSrcSeasons(body);
        res.perEpisodeMode = isSeries;
        res.entries        = groupBwaStreams(bwaStreams, baseUrl, reqSeason, reqEpisode);
        for (const auto& e : res.entries)
        {
            VoiceOption v;
            v.name   = e.voiceName;
            v.active = false;
            res.voices.push_back(std::move(v));
        }
        if (res.entries.empty())
            res.type = PageType::Empty;
        else
            res.type = isSeries ? PageType::Episodes : PageType::Movie;
        return res;
    }

    // Format B: Classic Lampac rjson=true ({ "type": "...", "voice": [...], "data": [...] })
    res.ok = true;
    std::string typeStr = toLowerStr(json::str(body, "type"));
    if (typeStr == "movie") res.type = PageType::Movie;
    else if (typeStr == "season") res.type = PageType::Seasons;
    else if (typeStr == "episode") res.type = PageType::Episodes;
    else if (typeStr == "similar") res.type = PageType::Similar;

    for (const auto& vObj : json::objects(body, "voice"))
    {
        VoiceOption v;
        v.name   = json::str(vObj, "name");
        v.url    = resolveRelativeUrl(baseUrl, json::str(vObj, "url"));
        if (!v.url.empty()) v.url = ensureLampacParams(v.url, token, true);
        v.active = json::boolean(vObj, "active", false);
        if (!v.name.empty()) res.voices.push_back(std::move(v));
    }

    std::string activeVoice;
    for (const auto& v : res.voices)
        if (v.active) { activeVoice = v.name; break; }

    for (const auto& dObj : dataObjs)
    {
        std::string top = json::topLevel(dObj);
        OnlineEntry e;

        std::string m = toLowerStr(json::str(top, "method"));
        if (m == "call") e.method = OnlineMethod::Call;
        else if (m == "link") e.method = OnlineMethod::Link;
        else if (m == "play") e.method = OnlineMethod::Play;
        else
            e.method = (res.type == PageType::Seasons || res.type == PageType::Similar)
                           ? OnlineMethod::Link
                           : OnlineMethod::Play;

        e.title = json::str(top, "title");
        if (e.title.empty()) e.title = json::str(top, "name");
        e.details   = json::str(top, "details");
        e.voiceName = json::str(top, "translate");
        if (e.voiceName.empty()) e.voiceName = json::str(top, "voice");
        if (e.voiceName.empty()) e.voiceName = activeVoice;
        if (e.title.empty()) e.title = e.voiceName;

        e.season  = readIntOrStr(top, "s", reqSeason);
        e.episode = readIntOrStr(top, "e", 0);
        if (e.season <= 0) e.season = readIntOrStr(top, "season", reqSeason);
        if (e.episode <= 0) e.episode = readIntOrStr(top, "episode", 0);

        std::string rawHref = json::str(top, "url");
        if (rawHref.empty()) rawHref = json::str(top, "link");
        e.url = resolveRelativeUrl(baseUrl, rawHref);
        if (!e.url.empty() && e.method != OnlineMethod::Play)
            e.url = ensureLampacParams(e.url, token, true);

        e.maxQuality = json::str(top, "maxquality");
        e.headers    = parseHeadersObj(dObj);
        e.subtitles  = parseSubtitlesArray(dObj, baseUrl);

        std::string qObj = json::object(dObj, "quality");
        if (!qObj.empty())
        {
            for (const auto& kv : json::stringMap(qObj))
            {
                std::string qu = resolveRelativeUrl(baseUrl, kv.second);
                if (!qu.empty())
                    e.qualities.push_back({ kv.first, qu, parseQualityHeight(kv.first) });
            }
            sortQualitiesDesc(e.qualities);
        }
        if (e.qualities.empty() && e.method == OnlineMethod::Play && !e.url.empty())
        {
            std::string ql = e.maxQuality.empty() ? "Auto" : e.maxQuality;
            e.qualities.push_back({ ql, e.url, parseQualityHeight(ql) });
        }
        if (e.url.empty() && !e.qualities.empty()) e.url = e.qualities[0].url;
        if (e.maxQuality.empty() && !e.qualities.empty()) e.maxQuality = e.qualities[0].label;

        if (res.type == PageType::Seasons)
        {
            SeasonOption so;
            so.number = e.season > 0 ? e.season : readIntOrStr(top, "id", 0);
            so.name   = e.title.empty() ? std::to_string(so.number) : e.title;
            so.url    = e.url;
            res.seasons.push_back(std::move(so));
        }

        if (!e.url.empty() || !e.qualities.empty()) res.entries.push_back(std::move(e));
    }

    if (res.entries.empty()) res.type = PageType::Empty;
    else if (res.type == PageType::Empty)
        res.type = isSeries ? PageType::Episodes : PageType::Movie;
    return res;
}

bool parseLampacCallJson(const std::string& body,
                         const std::string& baseUrl,
                         const std::string& token,
                         OnlineEntry& out)
{
    if (trimStr(body).empty()) return false;

    // BWA Format A fallback (e.g. next episode call returning streams[])
    std::vector<std::string> bwaStreams = json::objects(body, "streams");
    if (!bwaStreams.empty())
    {
        auto grouped = groupBwaStreams(bwaStreams, baseUrl, out.season, out.episode);
        if (grouped.empty()) return false;
        size_t pick = 0;
        if (!out.voiceName.empty())
        {
            std::string wantLow = toLowerStr(out.voiceName);
            for (size_t i = 0; i < grouped.size(); i++)
            {
                if (grouped[i].voiceName == out.voiceName) { pick = i; break; }
                std::string gotLow = toLowerStr(grouped[i].voiceName);
                if (gotLow.find(wantLow) != std::string::npos ||
                    wantLow.find(gotLow) != std::string::npos)
                    pick = i;
            }
        }
        out.qualities = grouped[pick].qualities;
        out.url       = grouped[pick].url;
        out.maxQuality = grouped[pick].maxQuality;
        if (!grouped[pick].headers.empty()) out.headers = grouped[pick].headers;
        if (!grouped[pick].subtitles.empty()) out.subtitles = grouped[pick].subtitles;
        out.method = OnlineMethod::Play;
        return !out.url.empty();
    }

    // Classic Lampac call response
    std::string top = json::topLevel(body);
    std::string u   = resolveRelativeUrl(baseUrl, json::str(top, "url"));
    std::vector<OnlineQuality> qs;
    std::string qObj = json::object(body, "quality");
    if (!qObj.empty())
    {
        for (const auto& kv : json::stringMap(qObj))
        {
            std::string qu = resolveRelativeUrl(baseUrl, kv.second);
            if (!qu.empty()) qs.push_back({ kv.first, qu, parseQualityHeight(kv.first) });
        }
        sortQualitiesDesc(qs);
    }
    if (!qs.empty())
    {
        out.qualities = std::move(qs);
        if (u.empty()) u = out.qualities[0].url;
        out.maxQuality = out.qualities[0].label;
    }
    else if (!u.empty())
    {
        std::string ql = json::str(top, "maxquality");
        if (ql.empty()) ql = out.maxQuality.empty() ? "Auto" : out.maxQuality;
        out.qualities.clear();
        out.qualities.push_back({ ql, u, parseQualityHeight(ql) });
        out.maxQuality = ql;
    }

    // If response was nested inside data[0]
    if (u.empty())
    {
        PageResult pr = parseLampacPageJson(body, baseUrl, token, out.season, out.episode, false);
        if (pr.ok && !pr.entries.empty())
        {
            out.url        = pr.entries[0].url;
            out.qualities  = pr.entries[0].qualities;
            out.maxQuality = pr.entries[0].maxQuality;
            if (!pr.entries[0].subtitles.empty()) out.subtitles = pr.entries[0].subtitles;
            if (!pr.entries[0].headers.empty()) out.headers = pr.entries[0].headers;
            out.method = OnlineMethod::Play;
            return !out.url.empty();
        }
        return false;
    }

    auto subs = parseSubtitlesArray(body, baseUrl);
    if (!subs.empty()) out.subtitles = std::move(subs);
    auto hdrs = parseHeadersObj(body);
    if (!hdrs.empty()) out.headers = std::move(hdrs);

    out.url    = u;
    out.method = OnlineMethod::Play;
    return true;
}

LampacProvider::LampacProvider(std::string id, std::string name, std::string rawUrl, std::string token)
    : pluginId(std::move(id)), pluginName(std::move(name))
{
    ParsedLampaUrl parsed = parseLampaPluginUrl(rawUrl, token);
    base                  = std::move(parsed.baseUrl);
    tok                   = std::move(parsed.token);
}

std::string LampacProvider::buildQueryUrl(const std::string& endpointOrUrl,
                                          const content::Item& item,
                                          const std::string& kpId,
                                          int season, int episode) const
{
    std::string u = resolveRelativeUrl(base, endpointOrUrl);
    if (item.tmdbId > 0) u = appendQueryParam(u, "id", std::to_string(item.tmdbId));
    if (!item.imdbId.empty()) u = appendQueryParam(u, "imdb_id", item.imdbId);
    if (!kpId.empty()) u = appendQueryParam(u, "kinopoisk_id", kpId);
    if (!item.title.empty()) u = appendQueryParam(u, "title", item.title);
    std::string orig = !item.originalTitle.empty() ? item.originalTitle : item.englishTitle;
    if (!orig.empty()) u = appendQueryParam(u, "original_title", orig);
    if (!item.year.empty()) u = appendQueryParam(u, "year", item.year);
    u = appendQueryParam(u, "serial", item.isSeries() ? "1" : "0");
    if (item.isSeries() && season > 0)
    {
        u = appendQueryParam(u, "s", std::to_string(season));
        u = appendQueryParam(u, "season", std::to_string(season));
        if (episode > 0)
        {
            u = appendQueryParam(u, "e", std::to_string(episode));
            u = appendQueryParam(u, "episode", std::to_string(episode));
        }
    }
    return ensureLampacParams(u, tok, true);
}

#ifndef ONLINE_NO_UI

std::string LampacProvider::fetchKpId(const content::Item& item)
{
    if (base.empty() || (item.tmdbId <= 0 && item.imdbId.empty())) return "";
    std::string u = base + "/externalids?serial=" + std::string(item.isSeries() ? "1" : "0");
    if (item.tmdbId > 0) u += "&id=" + std::to_string(item.tmdbId);
    if (!item.imdbId.empty()) u += "&imdb_id=" + pctEncode(item.imdbId);
    if (!tok.empty()) u += "&token=" + pctEncode(tok);

    std::string resp, err;
    long status = 0;
    if (!http::get(u, resp, err, "Accept: application/json", 8, &status)) return "";
    if (status < 200 || status >= 300) return "";
    return parseExternalIdsKp(resp);
}

bool LampacProvider::discover(const content::Item& item, const std::string& kpId,
                              std::vector<BalancerInfo>& out, std::string& err)
{
    out.clear();
    if (base.empty()) { err = "Empty server URL"; return false; }

    std::string u = buildQueryUrl("/lite/events", item, kpId, 0, 0);
    u = appendQueryParam(u, "life", "true");

    std::string resp;
    long status = 0;
    if (!http::get(u, resp, err, "Accept: application/json", 15, &status)) return false;
    if (status < 200 || status >= 300)
    {
        err = "HTTP " + std::to_string(status);
        return false;
    }
    out = parseLampacEventsJson(resp, pluginId, base, tok);
    if (out.empty())
    {
        std::string apiErr = json::str(resp, "error");
        if (!apiErr.empty()) { err = apiErr; return false; }
    }
    return true;
}

PageResult LampacProvider::fetch(const BalancerInfo& balancer, const std::string& overrideUrl,
                                 const content::Item& item, const std::string& kpId,
                                 int season, int episode)
{
    PageResult pr;
    std::string target = overrideUrl.empty()
                             ? buildQueryUrl(balancer.url, item, kpId, season, episode)
                             : ensureLampacParams(resolveRelativeUrl(base, overrideUrl), tok, true);
    if (target.empty())
    {
        pr.error = "Empty balancer URL";
        return pr;
    }

    std::string resp, err;
    long status = 0;
    if (!http::get(target, resp, err, "Accept: application/json", 25, &status))
    {
        pr.error = err;
        return pr;
    }
    if (status < 200 || status >= 300)
    {
        pr.error = "HTTP " + std::to_string(status);
        return pr;
    }
    return parseLampacPageJson(resp, base, tok, season, episode, item.isSeries());
}

bool LampacProvider::resolveCall(const std::string& callUrl, OnlineEntry& out, std::string& err)
{
    std::string target = ensureLampacParams(resolveRelativeUrl(base, callUrl), tok, true);
    if (target.empty()) { err = "Empty call URL"; return false; }

    std::string resp;
    long status = 0;
    if (!http::get(target, resp, err, "Accept: application/json", 25, &status)) return false;
    if (status < 200 || status >= 300)
    {
        err = "HTTP " + std::to_string(status);
        return false;
    }
    if (!parseLampacCallJson(resp, base, tok, out))
    {
        std::string apiErr = json::str(resp, "error");
        err = apiErr.empty() ? "Stream link not returned" : apiErr;
        return false;
    }
    return true;
}

bool LampacProvider::testConnection(std::string& summaryMsg)
{
    if (base.empty()) { summaryMsg = "Invalid URL"; return false; }
    std::string u = base + "/lite/events?id=550&imdb_id=tt0137523&title=Fight%20Club&year=1999&serial=0&rjson=true";
    if (!tok.empty()) u += "&token=" + pctEncode(tok);

    std::string resp, err;
    long status = 0;
    if (!http::get(u, resp, err, "Accept: application/json", 12, &status))
    {
        summaryMsg = err;
        return false;
    }
    if (status < 200 || status >= 300)
    {
        summaryMsg = "HTTP " + std::to_string(status);
        return false;
    }
    auto balancers = parseLampacEventsJson(resp, pluginId, base, tok);
    if (balancers.empty())
    {
        std::string apiErr = json::str(resp, "error");
        summaryMsg = apiErr.empty() ? "No balancers returned" : apiErr;
        return false;
    }
    summaryMsg = std::to_string(balancers.size());
    return true;
}

#else

std::string LampacProvider::fetchKpId(const content::Item&) { return ""; }
bool LampacProvider::discover(const content::Item&, const std::string&, std::vector<BalancerInfo>&, std::string& err)
{
    err = "ONLINE_NO_UI";
    return false;
}
PageResult LampacProvider::fetch(const BalancerInfo&, const std::string&, const content::Item&, const std::string&, int, int)
{
    return {};
}
bool LampacProvider::resolveCall(const std::string&, OnlineEntry&, std::string& err)
{
    err = "ONLINE_NO_UI";
    return false;
}
bool LampacProvider::testConnection(std::string& summaryMsg)
{
    summaryMsg = "ONLINE_NO_UI";
    return false;
}

#endif

} // namespace online
