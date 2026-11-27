#pragma once

#include <string>
#include <vector>

#include "online_provider.hpp"

namespace online
{

struct ParsedLampaUrl
{
    std::string baseUrl;
    std::string token;
    bool looksLikeJsScript = false;
};

// Pure helpers (built in offline tests with -DONLINE_NO_UI).
ParsedLampaUrl parseLampaPluginUrl(const std::string& rawUrl, const std::string& explicitToken = "");
std::string resolveRelativeUrl(const std::string& baseUrl, const std::string& href);
std::string ensureLampacParams(const std::string& url, const std::string& token, bool addRjson = true);
std::string parseExternalIdsKp(const std::string& json);

std::vector<BalancerInfo> parseLampacEventsJson(const std::string& json,
                                                const std::string& pluginId,
                                                const std::string& baseUrl,
                                                const std::string& token);

PageResult parseLampacPageJson(const std::string& json,
                               const std::string& baseUrl,
                               const std::string& token,
                               int reqSeason = 0,
                               int reqEpisode = 0,
                               bool isSeries = false);

bool parseLampacCallJson(const std::string& json,
                         const std::string& baseUrl,
                         const std::string& token,
                         OnlineEntry& out);

class LampacProvider : public IOnlineProvider
{
  public:
    LampacProvider(std::string id, std::string name, std::string rawUrl, std::string token);

    std::string id() const override { return pluginId; }
    std::string name() const override { return pluginName; }
    std::string kind() const override { return "lampac"; }

    const std::string& baseUrl() const { return base; }
    const std::string& token() const { return tok; }

    // Best-effort TMDB/IMDb -> Kinopoisk ID lookup via /externalids (non-fatal if 404).
    std::string fetchKpId(const content::Item& item);

    bool discover(const content::Item& item, const std::string& kpId,
                  std::vector<BalancerInfo>& out, std::string& err) override;

    PageResult fetch(const BalancerInfo& balancer, const std::string& overrideUrl,
                     const content::Item& item, const std::string& kpId,
                     int season, int episode) override;

    bool resolveCall(const std::string& callUrl, OnlineEntry& out, std::string& err) override;

    bool testConnection(std::string& summaryMsg) override;

  private:
    std::string pluginId;
    std::string pluginName;
    std::string base;
    std::string tok;

    std::string buildQueryUrl(const std::string& endpointOrUrl,
                              const content::Item& item,
                              const std::string& kpId,
                              int season, int episode) const;
};

} // namespace online
