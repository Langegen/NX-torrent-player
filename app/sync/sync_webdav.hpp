#pragma once

#include "sync_provider.hpp"
#include <string>

namespace sync
{

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

    static std::string normalizeUrl(std::string url);

private:
    bool ensureFolder(const std::string& folderUrl, const std::string& userPwd, std::string& err);
};

} // namespace sync
