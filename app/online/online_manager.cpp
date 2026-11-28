#include "online_manager.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

#include "appdata.hpp"
#include "fsutil.hpp"
#include "http.hpp"
#include "i18n.hpp"
#include "lampac_provider.hpp"
#include "nx_plugin_provider.hpp"
#include "online_view.hpp"
#include "player.hpp"
#include "sources.hpp"
#include "stremio.hpp"
#include "workers.hpp"

namespace online
{
namespace
{

const char* kChoicesPath = APPDATA_DIR "/online_choices.tsv";  // key \t plugin \t balancer \t voice
const char* kKpCachePath = APPDATA_DIR "/kp_ids.tsv";          // tmdbId \t kpId

std::string toLower(const std::string& s)
{
    std::string out;
    for (char c : s) out += (char)std::tolower((unsigned char)c);
    return out;
}

std::string sanitizeTab(std::string s)
{
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

std::string loadCachedKp(int tmdbId)
{
    if (tmdbId <= 0) return "";
    std::string key = std::to_string(tmdbId);
    FILE* f = std::fopen(kKpCachePath, "rb");
    if (!f) return "";
    char line[256];
    std::string found;
    while (std::fgets(line, sizeof(line), f))
    {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t tab = l.find('\t');
        if (tab != std::string::npos && l.substr(0, tab) == key)
            found = l.substr(tab + 1);
    }
    std::fclose(f);
    return found;
}

void saveCachedKp(int tmdbId, const std::string& kpId)
{
    if (tmdbId <= 0 || kpId.empty()) return;
    std::string key = std::to_string(tmdbId);
    std::vector<std::string> lines;
    if (FILE* f = std::fopen(kKpCachePath, "rb"))
    {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
        {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (!l.empty() && l.compare(0, key.size() + 1, key + "\t") != 0)
                lines.push_back(l);
        }
        std::fclose(f);
    }
    lines.push_back(key + "\t" + sanitizeTab(kpId));
    if (lines.size() > 500) lines.erase(lines.begin(), lines.end() - 500);
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    fsutil::writeAtomic(kKpCachePath, out);
}

std::string hostOfUrl(const std::string& url)
{
    size_t b = url.find("://");
    b = (b == std::string::npos) ? 0 : b + 3;
    size_t e = url.find_first_of("/?#", b);
    return url.substr(b, e == std::string::npos ? std::string::npos : e - b);
}

std::vector<PlayerOnlineQuality> toPlayerQualities(const std::vector<OnlineQuality>& qs)
{
    std::vector<PlayerOnlineQuality> out;
    out.reserve(qs.size());
    for (const auto& q : qs) out.push_back({ q.label, q.url, q.height });
    return out;
}

std::vector<PlayerOnlineSubtitle> toPlayerSubtitles(const std::vector<OnlineSubtitle>& subs)
{
    std::vector<PlayerOnlineSubtitle> out;
    out.reserve(subs.size());
    for (const auto& s : subs) out.push_back({ s.label, s.lang, s.url });
    return out;
}

std::vector<PlayerOnlineEpisode> toPlayerEpisodes(const std::vector<OnlineEntry>& eps)
{
    std::vector<PlayerOnlineEpisode> out;
    out.reserve(eps.size());
    for (const auto& e : eps)
    {
        PlayerOnlineEpisode pe;
        pe.season    = e.season;
        pe.episode   = e.episode;
        pe.title     = e.title;
        pe.voiceName = e.voiceName;
        pe.url       = e.url;
        pe.isCall    = (e.method == OnlineMethod::Call);
        pe.qualities = toPlayerQualities(e.qualities);
        pe.subtitles = toPlayerSubtitles(e.subtitles);
        pe.headers   = e.headers;
        out.push_back(std::move(pe));
    }
    return out;
}

void launchPlayerWithEntry(const content::Item& item,
                           const OnlineRef& ref,
                           const OnlineEntry& entry,
                           const std::vector<OnlineEntry>& seasonEpisodes,
                           int episodeIdx,
                           int nextPop,
                           const std::string& preferredQualityUrl,
                           const std::string& preferredQualityLabel,
                           bool promptResume,
                           double forcedResumeSec = -1.0)
{
    const auto& cfg = config::get();
    std::string chosenLabel = preferredQualityLabel;
    std::string chosenUrl   = preferredQualityUrl;
    if (chosenUrl.empty())
        chosenUrl = pickBestQuality(entry, cfg.onlinePreferQuality, cfg.hide4k, &chosenLabel);

    if (chosenUrl.empty())
    {
        auto* d = new brls::Dialog(tr("No playable stream URL returned."));
        d->addButton("OK", []() {});
        d->open();
        return;
    }

    int s = entry.season;
    int e = entry.episode;
    PlayHint hint = episodeHint(item, s, e, nextPop);
    hint.sourceType = "Online";
    if (forcedResumeSec >= 0.0) hint.watch.resumeSec = forcedResumeSec;

    hint.watch.online.active            = true;
    hint.watch.online.pluginId          = ref.pluginId;
    hint.watch.online.balancerId        = ref.balancerId;
    hint.watch.online.voiceName         = !entry.voiceName.empty() ? entry.voiceName : ref.voiceName;
    hint.watch.online.currentQuality    = chosenLabel;
    hint.watch.online.httpHeaders       = entry.headers;
    hint.watch.online.qualities         = toPlayerQualities(entry.qualities);
    hint.watch.online.subtitles         = toPlayerSubtitles(entry.subtitles);
    hint.watch.online.episodes          = toPlayerEpisodes(seasonEpisodes);
    hint.watch.online.currentEpisodeIdx = episodeIdx;

    if (hint.watch.online.episodes.size() >= 2 && episodeIdx >= 0 &&
        episodeIdx + 1 < (int)hint.watch.online.episodes.size())
    {
        int nextEp = hint.watch.online.episodes[(size_t)(episodeIdx + 1)].episode;
        int nextS  = hint.watch.online.episodes[(size_t)(episodeIdx + 1)].season;
        if (nextS <= 0) nextS = s;
        if (nextEp > 0)
            hint.watch.nextVideoId = item.key() + ":" + std::to_string(nextS) + ":" + std::to_string(nextEp);
    }

    if (promptResume)
        startPlaybackPrompt(chosenUrl, hint.art, hint.title, -1, hint.watch, "Online");
    else
        brls::Application::pushActivity(
            new PlayerActivity(chosenUrl, hint.art, hint.title, -1, hint.watch, "Online"));
}

} // namespace

bool hasEnabledPlugins()
{
    for (const auto& p : config::get().onlinePlugins)
        if (p.enabled && !p.url.empty()) return true;
    return false;
}

const config::OnlinePluginConfig* findPluginConfig(const std::string& id)
{
    for (const auto& p : config::get().onlinePlugins)
        if (p.id == id) return &p;
    return nullptr;
}

std::unique_ptr<IOnlineProvider> createProvider(const config::OnlinePluginConfig& cfg)
{
    if (cfg.kind == "nx_plugin")
        return std::make_unique<NxPluginProvider>(cfg.id, cfg.name, cfg.url, cfg.token);
    return std::make_unique<LampacProvider>(cfg.id, cfg.name, cfg.url, cfg.token);
}

std::string makePluginId(const std::string& nameOrUrl)
{
    std::string h = hostOfUrl(nameOrUrl);
    if (h.empty()) h = "plugin";
    std::string clean;
    for (char c : h)
    {
        unsigned char u = (unsigned char)c;
        if (std::isalnum(u)) clean += (char)std::tolower(u);
        else if (c == '.' || c == '-' || c == '_') clean += '_';
    }
    if (clean.empty()) clean = "plugin";

    std::string candidate = clean;
    int suffix = 2;
    while (findPluginConfig(candidate) != nullptr)
        candidate = clean + "_" + std::to_string(suffix++);
    return candidate;
}

bool loadChoice(const std::string& contentKey, OnlineRef& out)
{
    FILE* f = std::fopen(kChoicesPath, "rb");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (std::fgets(line, sizeof(line), f))
    {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t a = l.find('\t');
        if (a == std::string::npos || l.substr(0, a) != contentKey) continue;
        size_t b = l.find('\t', a + 1);
        if (b == std::string::npos) continue;
        size_t c = l.find('\t', b + 1);
        out.pluginId   = l.substr(a + 1, b - a - 1);
        out.balancerId = l.substr(b + 1, c == std::string::npos ? std::string::npos : c - b - 1);
        out.voiceName  = (c == std::string::npos) ? "" : l.substr(c + 1);
        found          = !out.pluginId.empty();
    }
    std::fclose(f);
    return found;
}

void saveChoice(const std::string& contentKey, const OnlineRef& ref)
{
    if (contentKey.empty() || ref.pluginId.empty() || ref.balancerId.empty()) return;
    std::vector<std::string> lines;
    if (FILE* f = std::fopen(kChoicesPath, "rb"))
    {
        char line[512];
        while (std::fgets(line, sizeof(line), f))
        {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (!l.empty() && l.compare(0, contentKey.size() + 1, contentKey + "\t") != 0)
                lines.push_back(l);
        }
        std::fclose(f);
    }
    lines.push_back(contentKey + "\t" + sanitizeTab(ref.pluginId) + "\t" +
                    sanitizeTab(ref.balancerId) + "\t" + sanitizeTab(ref.voiceName));
    if (lines.size() > 300) lines.erase(lines.begin(), lines.end() - 300);
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    fsutil::writeAtomic(kChoicesPath, out);
}

void probePluginAsync(const std::string& rawUrl, const std::string& token,
                      std::function<void(ProbeResult)> cb)
{
    workers::run([rawUrl, token, cb = std::move(cb)]() {
        if (http::isStopping()) return;
        ProbeResult res;
        std::string trimmed = rawUrl;
        while (!trimmed.empty() && std::isspace((unsigned char)trimmed.front())) trimmed.erase(0, 1);
        while (!trimmed.empty() && std::isspace((unsigned char)trimmed.back())) trimmed.pop_back();
        if (trimmed.empty())
        {
            res.error = tr("Empty URL");
            if (!http::isStopping()) brls::sync([cb, res]() { cb(res); });
            return;
        }

        // 1. Check NX Online Plugin v1 manifest
        std::string nxBase = normalizeNxPluginBase(trimmed);
        NxPluginProvider nxProbe("probe", "probe", nxBase, token);
        std::string nxMsg;
        if (nxProbe.testConnection(nxMsg))
        {
            if (http::isStopping()) return;
            res.ok             = true;
            res.plugin.id      = makePluginId(nxBase);
            res.plugin.name    = nxProbe.cachedManifest().name;
            res.plugin.kind    = "nx_plugin";
            res.plugin.url     = nxBase;
            res.plugin.token   = token;
            res.plugin.enabled = true;
            res.summary        = "NX Plugin: " + nxMsg;
            brls::sync([cb, res]() { cb(res); });
            return;
        }
        if (http::isStopping()) return;

        // 2. Check Lampa / Lampac / BWA protocol
        ParsedLampaUrl parsed = parseLampaPluginUrl(trimmed, token);
        LampacProvider lampacProbe("probe", "probe", trimmed, token);
        std::string lampacMsg;
        if (lampacProbe.testConnection(lampacMsg))
        {
            if (http::isStopping()) return;
            res.ok             = true;
            res.plugin.id      = makePluginId(parsed.baseUrl);
            res.plugin.name    = hostOfUrl(parsed.baseUrl);
            if (res.plugin.name.empty()) res.plugin.name = "Lampac";
            res.plugin.kind    = "lampac";
            res.plugin.url     = parsed.baseUrl;
            res.plugin.token   = parsed.token;
            res.plugin.enabled = true;
            res.summary        = "Lampa/Lampac (" + lampacMsg + " " + tr("balancers") + ")";
            brls::sync([cb, res]() { cb(res); });
            return;
        }
        if (http::isStopping()) return;

        if (parsed.looksLikeJsScript)
        {
            res.error = tr("This .js script has no Lampac/BWA server API (client-only browser plugins like online_mod.js are not supported). Use a Lampac/BWA URL (e.g. app.bwa.ad/online.js) or NX Online Plugin.");
        }
        else
        {
            res.error = tr("Connection failed: ") + lampacMsg;
        }
        brls::sync([cb, res]() { cb(res); });
    });
}

void testPluginAsync(const config::OnlinePluginConfig& cfg,
                     std::function<void(bool ok, const std::string& msg)> cb)
{
    workers::run([cfg, cb = std::move(cb)]() {
        if (http::isStopping()) return;
        auto prov = createProvider(cfg);
        std::string msg;
        bool ok = prov->testConnection(msg);
        if (!http::isStopping()) brls::sync([cb, ok, msg]() { cb(ok, msg); });
    });
}

void discoverAllAsync(const content::Item& item, std::function<void(DiscoverResult)> cb)
{
    std::vector<config::OnlinePluginConfig> plugins;
    for (const auto& p : config::get().onlinePlugins)
        if (p.enabled && !p.url.empty()) plugins.push_back(p);

    workers::run([item, plugins = std::move(plugins), cb = std::move(cb)]() {
        if (http::isStopping()) return;
        DiscoverResult res;
        res.kpId = loadCachedKp(item.tmdbId);

        // Try fetching KP ID from the first Lampac plugin if not cached yet.
        if (res.kpId.empty())
        {
            for (const auto& p : plugins)
            {
                if (http::isStopping()) return;
                if (p.kind == "lampac")
                {
                    LampacProvider lp(p.id, p.name, p.url, p.token);
                    std::string kp = lp.fetchKpId(item);
                    if (!kp.empty())
                    {
                        res.kpId = kp;
                        saveCachedKp(item.tmdbId, kp);
                        break;
                    }
                }
            }
        }

        for (const auto& p : plugins)
        {
            if (http::isStopping()) return;
            auto prov = createProvider(p);
            std::vector<BalancerInfo> found;
            std::string err;
            if (prov->discover(item, res.kpId, found, err))
            {
                for (auto& b : found) res.balancers.push_back(std::move(b));
            }
            else if (!err.empty())
            {
                res.errors.push_back(p.name + ": " + err);
            }
        }

        if (!http::isStopping()) brls::sync([cb, res = std::move(res)]() { cb(res); });
    });
}

void fetchPageAsync(const BalancerInfo& balancer, const std::string& overrideUrl,
                    const content::Item& item, const std::string& kpId,
                    int season, int episode,
                    std::function<void(PageResult)> cb)
{
    const config::OnlinePluginConfig* cfg = findPluginConfig(balancer.pluginId);
    if (!cfg)
    {
        PageResult pr;
        pr.error = tr("Plugin not found");
        cb(pr);
        return;
    }
    config::OnlinePluginConfig copyCfg = *cfg;
    workers::run([copyCfg, balancer, overrideUrl, item, kpId, season, episode,
                  cb = std::move(cb)]() {
        if (http::isStopping()) return;
        auto prov = createProvider(copyCfg);
        PageResult pr = prov->fetch(balancer, overrideUrl, item, kpId, season, episode);
        if (!http::isStopping()) brls::sync([cb, pr = std::move(pr)]() { cb(pr); });
    });
}

void resolveCallAsync(const std::string& pluginId, const std::string& callUrl,
                      const OnlineEntry& hint,
                      std::function<void(ResolveResult)> cb)
{
    const config::OnlinePluginConfig* cfg = findPluginConfig(pluginId);
    if (!cfg)
    {
        ResolveResult rr;
        rr.error = tr("Plugin not found");
        cb(rr);
        return;
    }
    config::OnlinePluginConfig copyCfg = *cfg;
    workers::run([copyCfg, callUrl, hint, cb = std::move(cb)]() {
        if (http::isStopping()) return;
        ResolveResult rr;
        rr.entry = hint;
        auto prov = createProvider(copyCfg);
        rr.ok = prov->resolveCall(callUrl, rr.entry, rr.error);
        if (!http::isStopping()) brls::sync([cb, rr = std::move(rr)]() { cb(rr); });
    });
}

std::string buildEpisodeCallUrl(const BalancerInfo& balancer, const content::Item& item,
                                const std::string& kpId, int season, int episode)
{
    const config::OnlinePluginConfig* cfg = findPluginConfig(balancer.pluginId);
    std::string u = balancer.url;
    auto addP = [&](const std::string& k, const std::string& v) {
        if (v.empty()) return;
        std::string pat1 = "?" + k + "=";
        std::string pat2 = "&" + k + "=";
        if (u.find(pat1) != std::string::npos || u.find(pat2) != std::string::npos) return;
        u += (u.find('?') == std::string::npos ? "?" : "&") + k + "=" + encodePct(v);
    };
    if (cfg && cfg->kind == "nx_plugin")
    {
        addP("balancer", balancer.id);
        if (item.tmdbId > 0) addP("tmdb_id", std::to_string(item.tmdbId));
        addP("imdb_id", item.imdbId);
        addP("kp_id", kpId);
        addP("type", item.isSeries() ? "series" : "movie");
        addP("title", item.title);
        addP("original_title", !item.originalTitle.empty() ? item.originalTitle : item.englishTitle);
        addP("year", item.year);
        if (season > 0) addP("season", std::to_string(season));
        if (episode > 0) addP("episode", std::to_string(episode));
        if (!cfg->token.empty()) addP("token", cfg->token);
        return u;
    }

    if (item.tmdbId > 0) addP("id", std::to_string(item.tmdbId));
    addP("imdb_id", item.imdbId);
    addP("kinopoisk_id", kpId);
    addP("title", item.title);
    addP("original_title", !item.originalTitle.empty() ? item.originalTitle : item.englishTitle);
    addP("year", item.year);
    addP("serial", item.isSeries() ? "1" : "0");
    if (season > 0)
    {
        addP("s", std::to_string(season));
        addP("season", std::to_string(season));
    }
    if (episode > 0)
    {
        addP("e", std::to_string(episode));
        addP("episode", std::to_string(episode));
    }
    addP("rjson", "true");
    if (cfg && !cfg->token.empty()) addP("token", cfg->token);
    return u;
}

void playOnlineEntry(const content::Item& item,
                     const OnlineRef& ref,
                     const OnlineEntry& entry,
                     const std::vector<OnlineEntry>& seasonEpisodes,
                     int episodeIdx,
                     int nextPop,
                     const std::string& preferredQualityUrl,
                     const std::string& preferredQualityLabel,
                     bool promptResume,
                     std::shared_ptr<bool> aliveGuard,
                     std::function<void()> onDone)
{
    if (entry.method == OnlineMethod::Call)
    {
        brls::Application::notify(tr("Fetching stream link..."));
        resolveCallAsync(ref.pluginId, entry.url, entry,
                         [item, ref, seasonEpisodes, episodeIdx, nextPop,
                          preferredQualityUrl, preferredQualityLabel, promptResume,
                          aliveGuard, onDone = std::move(onDone)](ResolveResult rr) {
                             if (onDone) onDone();
                             if (http::isStopping() || (aliveGuard && !*aliveGuard)) return;
                             if (!rr.ok)
                             {
                                 auto* d = new brls::Dialog(tr("Failed to get stream: ") + rr.error);
                                 d->addButton("OK", []() {});
                                 d->open();
                                 return;
                             }
                             launchPlayerWithEntry(item, ref, rr.entry, seasonEpisodes, episodeIdx,
                                                   nextPop, preferredQualityUrl, preferredQualityLabel,
                                                   promptResume);
                         });
        return;
    }

    if (onDone) onDone();
    launchPlayerWithEntry(item, ref, entry, seasonEpisodes, episodeIdx, nextPop,
                          preferredQualityUrl, preferredQualityLabel, promptResume);
}

void resumeOnlineAsync(const content::Item& item,
                       const OnlineRef& ref,
                       int season, int episode,
                       double resumeSec,
                       int episodesInSeason)
{
    const config::OnlinePluginConfig* cfgPtr = findPluginConfig(ref.pluginId);
    if (!cfgPtr || !cfgPtr->enabled)
    {
        openOnlineScreen(item, season, episode, episodesInSeason);
        return;
    }
    config::OnlinePluginConfig cfg = *cfgPtr;
    brls::Application::notify(tr("Connecting to online source..."));

    workers::run([cfg, item, ref, season, episode, resumeSec, episodesInSeason]() {
        if (http::isStopping()) return;
        auto prov = createProvider(cfg);
        std::string kpId = loadCachedKp(item.tmdbId);
        if (kpId.empty() && cfg.kind == "lampac")
        {
            kpId = static_cast<LampacProvider*>(prov.get())->fetchKpId(item);
            if (!kpId.empty()) saveCachedKp(item.tmdbId, kpId);
        }
        if (http::isStopping()) return;

        std::vector<BalancerInfo> balancers;
        std::string err;
        BalancerInfo targetBal;
        bool foundBal = false;
        if (prov->discover(item, kpId, balancers, err))
        {
            for (const auto& b : balancers)
            {
                if (b.id == ref.balancerId) { targetBal = b; foundBal = true; break; }
            }
        }
        if (http::isStopping()) return;
        if (!foundBal)
        {
            brls::sync([item, season, episode, episodesInSeason]() {
                openOnlineScreen(item, season, episode, episodesInSeason);
            });
            return;
        }

        PageResult pr = prov->fetch(targetBal, "", item, kpId, season, episode);
        if (http::isStopping()) return;
        if (!pr.ok || pr.entries.empty())
        {
            brls::sync([item, season, episode, episodesInSeason]() {
                openOnlineScreen(item, season, episode, episodesInSeason);
            });
            return;
        }

        // If classic Lampac returned Seasons first, follow the matching season.
        if (pr.type == PageType::Seasons && season > 0)
        {
            std::string seasonUrl;
            for (const auto& s : pr.seasons)
                if (s.number == season && !s.url.empty()) { seasonUrl = s.url; break; }
            if (!seasonUrl.empty())
            {
                pr = prov->fetch(targetBal, seasonUrl, item, kpId, season, episode);
                if (http::isStopping()) return;
            }
        }

        // Match remembered voice if classic Lampac offers voices.
        if (!ref.voiceName.empty() && !pr.voices.empty() && !pr.perEpisodeMode)
        {
            std::string vUrl;
            for (const auto& v : pr.voices)
            {
                if (!v.active && !v.url.empty() && toLower(v.name) == toLower(ref.voiceName))
                {
                    vUrl = v.url;
                    break;
                }
            }
            if (!vUrl.empty())
            {
                PageResult vp = prov->fetch(targetBal, vUrl, item, kpId, season, episode);
                if (http::isStopping()) return;
                if (vp.ok && !vp.entries.empty()) pr = std::move(vp);
            }
        }

        OnlineEntry chosen;
        bool matched = false;
        std::vector<OnlineEntry> playlistEps;
        int epIdx = -1;

        if (pr.perEpisodeMode && item.isSeries())
        {
            int targetS = season > 0 ? season : 1;
            int targetE = episode > 0 ? episode : 1;
            int pickVoice = 0;
            if (!ref.voiceName.empty())
            {
                std::string wl = toLower(ref.voiceName);
                for (size_t i = 0; i < pr.entries.size(); i++)
                {
                    std::string gl = toLower(pr.entries[i].voiceName);
                    if (gl == wl || gl.find(wl) != std::string::npos || wl.find(gl) != std::string::npos)
                    {
                        pickVoice = (int)i;
                        break;
                    }
                }
            }
            chosen = pr.entries[(size_t)pickVoice];
            chosen.season  = targetS;
            chosen.episode = targetE;
            matched = true;

            int epCount = episodesInSeason;
            for (const auto& sOpt : pr.seasons)
                if (sOpt.number == targetS && sOpt.episodeCount > 0) epCount = sOpt.episodeCount;
            if (epCount < targetE) epCount = targetE;
            if (epCount <= 0) epCount = 24;

            for (int k = 1; k <= epCount; k++)
            {
                if (k == targetE)
                {
                    epIdx = (int)playlistEps.size();
                    playlistEps.push_back(chosen);
                }
                else
                {
                    OnlineEntry ce;
                    ce.method    = OnlineMethod::Call;
                    ce.season    = targetS;
                    ce.episode   = k;
                    ce.title     = tr("Episode ") + std::to_string(k);
                    ce.voiceName = chosen.voiceName;
                    ce.url       = buildEpisodeCallUrl(targetBal, item, kpId, targetS, k);
                    playlistEps.push_back(std::move(ce));
                }
            }
        }
        else if (item.isSeries() && episode > 0)
        {
            for (size_t i = 0; i < pr.entries.size(); i++)
            {
                if (pr.entries[i].episode == episode &&
                    (season <= 0 || pr.entries[i].season <= 0 || pr.entries[i].season == season))
                {
                    chosen  = pr.entries[i];
                    epIdx   = (int)i;
                    matched = true;
                    break;
                }
            }
            if (matched) playlistEps = pr.entries;
        }
        else
        {
            // Movie: match voice if possible
            size_t pick = 0;
            if (!ref.voiceName.empty())
            {
                std::string wl = toLower(ref.voiceName);
                for (size_t i = 0; i < pr.entries.size(); i++)
                {
                    std::string gl = toLower(pr.entries[i].voiceName + " " + pr.entries[i].title);
                    if (gl.find(wl) != std::string::npos) { pick = i; break; }
                }
            }
            chosen  = pr.entries[pick];
            matched = true;
        }

        if (!matched)
        {
            brls::sync([item, season, episode, episodesInSeason]() {
                openOnlineScreen(item, season, episode, episodesInSeason);
            });
            return;
        }

        if (chosen.method == OnlineMethod::Call)
        {
            std::string callErr;
            if (!prov->resolveCall(chosen.url, chosen, callErr))
            {
                if (http::isStopping()) return;
                brls::sync([item, season, episode, episodesInSeason]() {
                    openOnlineScreen(item, season, episode, episodesInSeason);
                });
                return;
            }
            if (http::isStopping()) return;
        }

        if (chosen.method == OnlineMethod::Link)
        {
            if (!http::isStopping())
                brls::sync([item, season, episode, episodesInSeason]() {
                    openOnlineScreen(item, season, episode, episodesInSeason);
                });
            return;
        }

        if (!http::isStopping())
            brls::sync([item, ref, chosen, playlistEps, epIdx, resumeSec]() {
                launchPlayerWithEntry(item, ref, chosen, playlistEps, epIdx, 1, "", "",
                                      resumeSec < 0.0, resumeSec);
            });
    });
}

} // namespace online
