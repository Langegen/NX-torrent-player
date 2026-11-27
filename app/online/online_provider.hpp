#pragma once

#include <string>
#include <vector>

#include "content.hpp"
#include "online_types.hpp"

namespace online
{

class IOnlineProvider
{
  public:
    virtual ~IOnlineProvider() = default;

    virtual std::string id() const   = 0;
    virtual std::string name() const = 0;
    virtual std::string kind() const = 0;

    // Discover balancers available for `item` (using optional `kpId`).
    virtual bool discover(const content::Item& item, const std::string& kpId,
                          std::vector<BalancerInfo>& out, std::string& err) = 0;

    // Fetch streams / voices / seasons / episodes from `balancer`, or follow
    // `overrideUrl` (for voice/season/link navigation).
    virtual PageResult fetch(const BalancerInfo& balancer, const std::string& overrideUrl,
                             const content::Item& item, const std::string& kpId,
                             int season, int episode) = 0;

    // Resolve a deferred `OnlineMethod::Call` URL into a playable entry.
    // `out.voiceName` may be pre-populated as a preferred voice hint (for BWA).
    virtual bool resolveCall(const std::string& callUrl, OnlineEntry& out, std::string& err) = 0;

    // Test plugin connectivity for Settings UI.
    virtual bool testConnection(std::string& summaryMsg) = 0;
};

} // namespace online
