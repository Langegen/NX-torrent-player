#pragma once

#include "sync_provider.hpp"
#include <cstdint>
#include <mutex>
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

struct GdriveCredentials
{
    std::string clientId;
    std::string clientSecret;
    std::string refreshToken;

    bool hasClient() const { return !clientId.empty() && !clientSecret.empty(); }
    bool valid() const { return hasClient() && !refreshToken.empty(); }
};

struct GdriveTokens
{
    std::string refreshToken;
    std::string accessToken;
    int expiresIn = 3600;
    std::string userEmail;
};

enum class PollStatus
{
    Pending = 0,
    Approved = 1,
    SlowDown = 2,
    TransientError = 3,
    Failed = -1,
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

    // Thread-safe snapshot variants (capture config on UI thread, run on worker)
    bool testConnection(const GdriveCredentials& creds, std::string& err);
    bool pull(const GdriveCredentials& creds, std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err);
    bool push(const GdriveCredentials& creds, const std::string& libraryJson, const std::string& historyJson, std::string& err);

    // OAuth 2.0 Device Flow methods (do not mutate config::get() from worker threads)
    bool startDeviceAuth(const std::string& clientId, DeviceAuthInfo& info, std::string& err);
    bool startDeviceAuth(DeviceAuthInfo& info, std::string& err);
    PollStatus pollDeviceAuth(const std::string& clientId, const std::string& clientSecret,
                              const std::string& deviceCode, GdriveTokens& tokens, std::string& err);

    void disconnect();
    void clearCachedToken();
    void setCachedToken(const std::string& accessToken, int expiresIn, const std::string& cacheKey = "");

    static std::string builtinClientId();
    static std::string builtinClientSecret();
    static bool hasBuiltinCredentials();
    static std::string defaultClientId();
    static std::string defaultClientSecret();
    static bool hasClientCredentials();
    static GdriveCredentials snapshotCredentials();

private:
    bool ensureAccessToken(const GdriveCredentials& creds, std::string& token, std::string& err);
    bool findSyncFolder(const std::string& token, std::string& folderId, std::string& err);
    bool ensureSyncFolder(const std::string& token, std::string& folderId, std::string& err);
    bool findSyncFile(const std::string& token, const std::string& folderId, const std::string& filename, std::string& fileId, std::string& err);
    bool downloadSyncFile(const std::string& token, const std::string& fileId, std::string& content, std::string& err);
    bool uploadSyncFile(const std::string& token, const std::string& folderId, const std::string& filename, const std::string& content, std::string& err);

    std::mutex m_tokenMutex;
    std::string m_cachedAccessToken;
    std::string m_cachedForKey;
    int64_t m_tokenExpiry = 0;
};

} // namespace cloud_sync
