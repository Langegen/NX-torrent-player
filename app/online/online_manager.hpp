#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "content.hpp"
#include "online_provider.hpp"
#include "online_types.hpp"

namespace online
{

struct ProbeResult
{
    bool ok = false;
    std::string error;
    config::OnlinePluginConfig plugin;
    std::string summary;
};

struct DiscoverResult
{
    std::string kpId;
    std::vector<BalancerInfo> balancers;
    std::vector<std::string> errors;
};

struct ResolveResult
{
    bool ok = false;
    std::string error;
    OnlineEntry entry;
};

// Returns true when at least one online plugin is configured and enabled.
bool hasEnabledPlugins();

// Looks up a plugin config by id, or nullptr if not found.
const config::OnlinePluginConfig* findPluginConfig(const std::string& id);

// Instantiates a provider for `cfg` (LampacProvider or NxPluginProvider).
std::unique_ptr<IOnlineProvider> createProvider(const config::OnlinePluginConfig& cfg);

// Generates a unique plugin id for a newly added plugin.
std::string makePluginId(const std::string& nameOrUrl);

// Persistent choice memory (last balancer & voice per content item).
bool loadChoice(const std::string& contentKey, OnlineRef& out);
void saveChoice(const std::string& contentKey, const OnlineRef& ref);

// Probes a user-entered URL (+ optional token), detects NxPlugin vs Lampac/Lampa,
// checks connectivity, and returns a populated OnlinePluginConfig.
void probePluginAsync(const std::string& rawUrl, const std::string& token,
                      std::function<void(ProbeResult)> cb);

// Tests an already-configured plugin.
void testPluginAsync(const config::OnlinePluginConfig& cfg,
                     std::function<void(bool ok, const std::string& msg)> cb);

// Discovers all balancers across all enabled plugins for `item`.
void discoverAllAsync(const content::Item& item, std::function<void(DiscoverResult)> cb);

// Fetches a balancer page (or follows overrideUrl).
void fetchPageAsync(const BalancerInfo& balancer, const std::string& overrideUrl,
                    const content::Item& item, const std::string& kpId,
                    int season, int episode,
                    std::function<void(PageResult)> cb);

// Resolves a deferred OnlineMethod::Call URL.
void resolveCallAsync(const std::string& pluginId, const std::string& callUrl,
                      const OnlineEntry& hint,
                      std::function<void(ResolveResult)> cb);

// Builds a BWA/per-episode call URL for `(season, episode)` on `balancer`.
std::string buildEpisodeCallUrl(const BalancerInfo& balancer, const content::Item& item,
                                const std::string& kpId, int season, int episode);

// Plays `entry` in MpvView (resolving OnlineMethod::Call first if needed).
// `seasonEpisodes` provides the full episode playlist for Next/Prev/ZL inside the player.
void playOnlineEntry(const content::Item& item,
                     const OnlineRef& ref,
                     const OnlineEntry& entry,
                     const std::vector<OnlineEntry>& seasonEpisodes,
                     int episodeIdx,
                     int nextPop,
                     const std::string& preferredQualityUrl = "",
                     const std::string& preferredQualityLabel = "",
                     bool promptResume = true);

// Resumes or auto-plays `(season, episode)` using a remembered `OnlineRef`
// (fetches fresh stream URL from the same plugin/balancer/voice). Falls back
// to openOnlineScreen() if the stream cannot be auto-resolved.
void resumeOnlineAsync(const content::Item& item,
                       const OnlineRef& ref,
                       int season, int episode,
                       double resumeSec,
                       int episodesInSeason = 0);

} // namespace online
