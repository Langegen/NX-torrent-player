#pragma once

#include <string>
#include <vector>

#include "online_provider.hpp"

namespace online
{

struct NxBalancerDef
{
    std::string id;
    std::string name;
};

struct NxManifest
{
    bool valid = false;
    std::string schema;
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string streamsEndpoint = "/streams";
    std::string resolveEndpoint = "/resolve";
    std::vector<NxBalancerDef> balancers;
};

// Pure helpers (tested in offline tests with -DONLINE_NO_UI).
std::string normalizeNxPluginBase(const std::string& rawUrl);
NxManifest parseNxManifestJson(const std::string& json);
PageResult parseNxStreamsJson(const std::string& json, const std::string& baseUrl);
bool parseNxResolveJson(const std::string& json, const std::string& baseUrl, OnlineEntry& out);

class NxPluginProvider : public IOnlineProvider
{
  public:
    NxPluginProvider(std::string id, std::string name, std::string rawUrl, std::string token);

    std::string id() const override { return pluginId; }
    std::string name() const override { return pluginName; }
    std::string kind() const override { return "nx_plugin"; }

    bool discover(const content::Item& item, const std::string& kpId,
                  std::vector<BalancerInfo>& out, std::string& err) override;

    PageResult fetch(const BalancerInfo& balancer, const std::string& overrideUrl,
                     const content::Item& item, const std::string& kpId,
                     int season, int episode) override;

    bool resolveCall(const std::string& callUrl, OnlineEntry& out, std::string& err) override;

    bool testConnection(std::string& summaryMsg) override;

    const NxManifest& cachedManifest() const { return manifest; }

  private:
    std::string pluginId;
    std::string pluginName;
    std::string base;
    std::string tok;
    NxManifest manifest;

    bool ensureManifest(std::string& err);
};

} // namespace online
