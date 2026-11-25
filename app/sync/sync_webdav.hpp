#pragma once

#include "sync_provider.hpp"
#include <string>

namespace cloud_sync
{

struct WebdavConfig
{
    std::string url;
    std::string user;
    std::string pass;

    bool valid() const { return !url.empty() && !user.empty() && !pass.empty(); }
};

class WebdavProvider : public SyncProvider
{
public:
    WebdavProvider() = default;

    std::string id() const override { return "webdav"; }
    std::string displayName() const override { return "WebDAV"; }
    bool isConfigured() const override;

    bool testConnection(std::string& err) override;
    bool pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err) override;
    bool push(const std::string& libraryJson, const std::string& historyJson, std::string& err) override;

    bool testConnection(const WebdavConfig& cfg, std::string& err);
    bool pull(const WebdavConfig& cfg, std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err);
    bool push(const WebdavConfig& cfg, const std::string& libraryJson, const std::string& historyJson, std::string& err);

    static WebdavConfig snapshotConfig();
    static std::string normalizeUrl(std::string url);

private:
    bool ensureFolder(const std::string& folderUrl, const WebdavConfig& cfg, std::string& err);
    bool pullFile(const std::string& folderUrl, const std::string& filename, const WebdavConfig& cfg,
                  std::string& outJson, std::string& err);
};

} // namespace cloud_sync
