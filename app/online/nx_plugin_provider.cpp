#include "nx_plugin_provider.hpp"

#include <cctype>
#include <cstdlib>

#include "json.hpp"
#include "lampac_provider.hpp"  // resolveRelativeUrl

#ifndef ONLINE_NO_UI
#include "http.hpp"
#endif

namespace online
{
namespace
{

std::string trimNx(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string lowerNx(const std::string& s)
{
    std::string out;
    for (char c : s) out += (char)std::tolower((unsigned char)c);
    return out;
}

std::vector<OnlineSubtitle> parseNxSubtitles(const std::string& obj, const std::string& baseUrl)
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

std::vector<std::string> parseNxHeaders(const std::string& obj)
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

} // namespace

std::string normalizeNxPluginBase(const std::string& rawUrl)
{
    std::string u = trimNx(rawUrl);
    if (u.empty()) return "";
    if (u.find("://") == std::string::npos)
        u = "https://" + u;
    size_t q = u.find('?');
    if (q != std::string::npos) u = u.substr(0, q);
    while (!u.empty() && u.back() == '/') u.pop_back();
    const std::string kManifest = "/manifest.json";
    if (u.size() > kManifest.size() &&
        lowerNx(u.substr(u.size() - kManifest.size())) == kManifest)
    {
        u = u.substr(0, u.size() - kManifest.size());
    }
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

NxManifest parseNxManifestJson(const std::string& body)
{
    NxManifest m;
    if (trimNx(body).empty()) return m;
    m.schema = json::str(body, "schema");
    if (m.schema != "nx-online/v1") return m;

    m.valid       = true;
    m.id          = json::str(body, "id");
    m.name        = json::str(body, "name");
    m.version     = json::str(body, "version");
    m.description = json::str(body, "description");
    if (m.name.empty()) m.name = m.id.empty() ? "NX Plugin" : m.id;

    std::string eps = json::object(body, "endpoints");
    if (!eps.empty())
    {
        std::string st = json::str(eps, "streams");
        std::string rs = json::str(eps, "resolve");
        if (!st.empty()) m.streamsEndpoint = st;
        if (!rs.empty()) m.resolveEndpoint = rs;
    }

    for (const auto& bObj : json::objects(body, "balancers"))
    {
        NxBalancerDef b;
        b.id   = json::str(bObj, "id");
        b.name = json::str(bObj, "name");
        if (b.name.empty()) b.name = b.id;
        if (!b.id.empty()) m.balancers.push_back(std::move(b));
    }
    if (m.balancers.empty())
        m.balancers.push_back({ "default", m.name });

    return m;
}

PageResult parseNxStreamsJson(const std::string& body, const std::string& baseUrl)
{
    PageResult res;
    if (trimNx(body).empty()) { res.error = "Empty response"; return res; }
    std::string err = json::str(body, "error");
    if (!err.empty() && json::objects(body, "items").empty())
    {
        res.error = err;
        return res;
    }

    res.ok = true;
    std::string t = lowerNx(json::str(body, "type"));
    if (t == "movie") res.type = PageType::Movie;
    else if (t == "seasons") res.type = PageType::Seasons;
    else if (t == "episodes") res.type = PageType::Episodes;
    else if (t == "similar") res.type = PageType::Similar;

    for (const auto& vObj : json::objects(body, "voices"))
    {
        VoiceOption v;
        v.name   = json::str(vObj, "name");
        v.url    = resolveRelativeUrl(baseUrl, json::str(vObj, "url"));
        v.active = json::boolean(vObj, "active", false);
        if (!v.name.empty()) res.voices.push_back(std::move(v));
    }

    for (const auto& sObj : json::objects(body, "seasons"))
    {
        SeasonOption s;
        s.number       = (int)json::integer(sObj, "number", 0);
        s.episodeCount = (int)json::integer(sObj, "episodeCount", 0);
        s.name         = json::str(sObj, "name");
        s.url          = resolveRelativeUrl(baseUrl, json::str(sObj, "url"));
        s.active       = json::boolean(sObj, "active", false);
        if (s.name.empty()) s.name = std::to_string(s.number);
        res.seasons.push_back(std::move(s));
    }

    for (const auto& itemObj : json::objects(body, "items"))
    {
        std::string top = json::topLevel(itemObj);
        OnlineEntry e;
        std::string m = lowerNx(json::str(top, "method"));
        if (m == "call") e.method = OnlineMethod::Call;
        else if (m == "link") e.method = OnlineMethod::Link;
        else e.method = OnlineMethod::Play;

        e.title     = json::str(top, "title");
        e.details   = json::str(top, "details");
        e.voiceName = json::str(top, "voice");
        e.season    = (int)json::integer(top, "season", 0);
        e.episode   = (int)json::integer(top, "episode", 0);
        e.url       = resolveRelativeUrl(baseUrl, json::str(top, "url"));
        e.headers   = parseNxHeaders(itemObj);
        e.subtitles = parseNxSubtitles(itemObj, baseUrl);

        std::string qObj = json::object(itemObj, "qualities");
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
            std::string ql = json::str(top, "quality");
            if (ql.empty()) ql = "Auto";
            e.qualities.push_back({ ql, e.url, parseQualityHeight(ql) });
        }
        if (e.url.empty() && !e.qualities.empty()) e.url = e.qualities[0].url;
        if (!e.qualities.empty()) e.maxQuality = e.qualities[0].label;

        if (!e.url.empty() || !e.qualities.empty())
            res.entries.push_back(std::move(e));
    }

    if (res.entries.empty()) res.type = PageType::Empty;
    else if (res.type == PageType::Empty) res.type = PageType::Movie;
    return res;
}

bool parseNxResolveJson(const std::string& body, const std::string& baseUrl, OnlineEntry& out)
{
    if (trimNx(body).empty()) return false;
    std::string top = json::topLevel(body);
    std::string u   = resolveRelativeUrl(baseUrl, json::str(top, "url"));

    std::vector<OnlineQuality> qs;
    std::string qObj = json::object(body, "qualities");
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
        std::string ql = json::str(top, "quality");
        if (ql.empty()) ql = "Auto";
        out.qualities.clear();
        out.qualities.push_back({ ql, u, parseQualityHeight(ql) });
        out.maxQuality = ql;
    }
    if (u.empty()) return false;

    auto subs = parseNxSubtitles(body, baseUrl);
    if (!subs.empty()) out.subtitles = std::move(subs);
    auto hdrs = parseNxHeaders(body);
    if (!hdrs.empty()) out.headers = std::move(hdrs);

    out.url    = u;
    out.method = OnlineMethod::Play;
    return true;
}

NxPluginProvider::NxPluginProvider(std::string id, std::string name, std::string rawUrl, std::string token)
    : pluginId(std::move(id)), pluginName(std::move(name)), base(normalizeNxPluginBase(rawUrl)), tok(std::move(token))
{
}

#ifndef ONLINE_NO_UI

bool NxPluginProvider::ensureManifest(std::string& err)
{
    if (manifest.valid) return true;
    if (base.empty()) { err = "Invalid plugin URL"; return false; }

    http::RequestOptions opt;
    opt.timeoutSec = 10;
    opt.headers    = { "Accept: application/json" };
    if (!tok.empty()) opt.headers.push_back("Authorization: Bearer " + tok);

    std::string url = base + "/manifest.json";
    if (!tok.empty()) url += "?token=" + http::urlEncode(tok);

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status)) return false;
    if (status < 200 || status >= 300)
    {
        err = "HTTP " + std::to_string(status);
        return false;
    }
    manifest = parseNxManifestJson(resp);
    if (!manifest.valid)
    {
        err = "Invalid NX plugin manifest (expected schema nx-online/v1)";
        return false;
    }
    return true;
}

bool NxPluginProvider::discover(const content::Item&, const std::string&,
                                std::vector<BalancerInfo>& out, std::string& err)
{
    out.clear();
    if (!ensureManifest(err)) return false;
    for (const auto& b : manifest.balancers)
    {
        BalancerInfo bi;
        bi.pluginId  = pluginId;
        bi.id        = b.id;
        bi.name      = b.name;
        bi.url       = resolveRelativeUrl(base, manifest.streamsEndpoint);
        bi.available = true;
        out.push_back(std::move(bi));
    }
    return true;
}

PageResult NxPluginProvider::fetch(const BalancerInfo& balancer, const std::string& overrideUrl,
                                   const content::Item& item, const std::string& kpId,
                                   int season, int episode)
{
    PageResult pr;
    std::string err;
    if (!ensureManifest(err)) { pr.error = err; return pr; }

    std::string url = overrideUrl.empty()
                          ? resolveRelativeUrl(base, manifest.streamsEndpoint)
                          : resolveRelativeUrl(base, overrideUrl);
    auto addP = [&](const std::string& k, const std::string& v) {
        if (v.empty()) return;
        url += (url.find('?') == std::string::npos ? "?" : "&") + k + "=" + http::urlEncode(v);
    };
    if (overrideUrl.empty())
    {
        addP("balancer", balancer.id);
        if (item.tmdbId > 0) addP("tmdb_id", std::to_string(item.tmdbId));
        addP("imdb_id", item.imdbId);
        addP("kp_id", kpId);
        addP("type", item.isSeries() ? "series" : "movie");
        addP("title", item.title);
        addP("original_title", !item.originalTitle.empty() ? item.originalTitle : item.englishTitle);
        addP("year", item.year);
        if (item.isSeries() && season > 0) addP("season", std::to_string(season));
        if (item.isSeries() && episode > 0) addP("episode", std::to_string(episode));
    }
    if (!tok.empty() && url.find("token=") == std::string::npos) addP("token", tok);

    http::RequestOptions opt;
    opt.timeoutSec = 25;
    opt.headers    = { "Accept: application/json" };
    if (!tok.empty()) opt.headers.push_back("Authorization: Bearer " + tok);

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, pr.error, &status)) return pr;
    if (status < 200 || status >= 300)
    {
        pr.error = "HTTP " + std::to_string(status);
        return pr;
    }
    return parseNxStreamsJson(resp, base);
}

bool NxPluginProvider::resolveCall(const std::string& callUrl, OnlineEntry& out, std::string& err)
{
    if (!ensureManifest(err)) return false;
    std::string url = resolveRelativeUrl(base, callUrl);
    if (url.empty()) url = resolveRelativeUrl(base, manifest.resolveEndpoint);
    if (!tok.empty() && url.find("token=") == std::string::npos)
        url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("token=") + http::urlEncode(tok);

    http::RequestOptions opt;
    opt.timeoutSec = 25;
    opt.headers    = { "Accept: application/json" };
    if (!tok.empty()) opt.headers.push_back("Authorization: Bearer " + tok);

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status)) return false;
    if (status < 200 || status >= 300)
    {
        err = "HTTP " + std::to_string(status);
        return false;
    }
    if (!parseNxResolveJson(resp, base, out))
    {
        std::string apiErr = json::str(resp, "error");
        err = apiErr.empty() ? "Resolve failed" : apiErr;
        return false;
    }
    return true;
}

bool NxPluginProvider::testConnection(std::string& summaryMsg)
{
    manifest.valid = false;
    std::string err;
    if (!ensureManifest(err))
    {
        summaryMsg = err;
        return false;
    }
    summaryMsg = manifest.name + " (" + std::to_string(manifest.balancers.size()) + ")";
    return true;
}

#else

bool NxPluginProvider::ensureManifest(std::string& err) { err = "ONLINE_NO_UI"; return false; }
bool NxPluginProvider::discover(const content::Item&, const std::string&, std::vector<BalancerInfo>&, std::string& err)
{
    err = "ONLINE_NO_UI";
    return false;
}
PageResult NxPluginProvider::fetch(const BalancerInfo&, const std::string&, const content::Item&, const std::string&, int, int)
{
    return {};
}
bool NxPluginProvider::resolveCall(const std::string&, OnlineEntry&, std::string& err)
{
    err = "ONLINE_NO_UI";
    return false;
}
bool NxPluginProvider::testConnection(std::string& summaryMsg)
{
    summaryMsg = "ONLINE_NO_UI";
    return false;
}

#endif

} // namespace online
