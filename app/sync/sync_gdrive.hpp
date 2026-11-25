#pragma once

#include "sync_provider.hpp"
#include <cstdint>
#include <string>

namespace cloud_sync
{

struct DeviceAuthInfo
{
    std::string deviceCode;
    std::string userCode;
    std::string verificationUrl;
    int expiresIn = 1800;
    int interval = 5;
};

class GdriveProvider : public SyncProvider
{
public:
    GdriveProvider() = default;

    std::string id() const override { return "gdrive"; }
    std::string displayName() const override { return "Google Drive"; }
    bool isConfigured() const override;

    bool testConnection(std::string& err) override;
    bool pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err) override;
    bool push(const std::string& libraryJson, const std::string& historyJson, std::string& err) override;

    // OAuth 2.0 Device Flow methods
    bool startDeviceAuth(DeviceAuthInfo& info, std::string& err);
    // Returns: 1 = success, 0 = still pending, -1 = failed/error
    int pollDeviceAuth(const std::string& deviceCode, std::string& err);

    void disconnect();

    static std::string defaultClientId();
    static std::string defaultClientSecret();

private:
    bool ensureAccessToken(std::string& token, std::string& err);
    bool findAppDataFile(const std::string& token, const std::string& filename, std::string& fileId, std::string& err);
    bool downloadAppDataFile(const std::string& token, const std::string& fileId, std::string& content, std::string& err);
    bool uploadAppDataFile(const std::string& token, const std::string& filename, const std::string& content, std::string& err);

    std::string m_cachedAccessToken;
    int64_t m_tokenExpiry = 0;
};

} // namespace cloud_sync
