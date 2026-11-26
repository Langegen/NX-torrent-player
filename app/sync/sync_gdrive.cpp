#include "sync_gdrive.hpp"

#include <ctime>
#include "../config.hpp"
#include "../http.hpp"
#include "../json.hpp"

#ifndef GDRIVE_CLIENT_ID
#define GDRIVE_CLIENT_ID ""
#endif

#ifndef GDRIVE_CLIENT_SECRET
#define GDRIVE_CLIENT_SECRET ""
#endif

namespace cloud_sync
{

namespace
{

static constexpr const char* kSyncFolderName = "NX-Torrent-Player";

std::string extractGoogleError(const std::string& resp, const std::string& netErr, long status)
{
    if (!resp.empty())
    {
        std::string desc = json::str(resp, "error_description");
        if (!desc.empty())
        {
            std::string code = json::str(resp, "error");
            return code.empty() ? desc : (code + ": " + desc);
        }
        std::string msg = json::str(resp, "message");
        if (!msg.empty()) return msg;
        std::string code = json::str(resp, "error");
        if (!code.empty()) return code;
    }
    if (!netErr.empty()) return netErr;
    if (status != 0) return "HTTP " + std::to_string(status);
    return "Network error";
}

} // namespace

std::string GdriveProvider::builtinClientId()
{
    return GDRIVE_CLIENT_ID;
}

std::string GdriveProvider::builtinClientSecret()
{
    return GDRIVE_CLIENT_SECRET;
}

bool GdriveProvider::hasBuiltinCredentials()
{
    return !builtinClientId().empty() && !builtinClientSecret().empty();
}

std::string GdriveProvider::defaultClientId()
{
    const auto& cfg = config::get();
    if (!cfg.syncGdriveClientId.empty())
        return cfg.syncGdriveClientId;
    return builtinClientId();
}

std::string GdriveProvider::defaultClientSecret()
{
    const auto& cfg = config::get();
    if (!cfg.syncGdriveClientSecret.empty())
        return cfg.syncGdriveClientSecret;
    return builtinClientSecret();
}

bool GdriveProvider::hasClientCredentials()
{
    return !defaultClientId().empty() && !defaultClientSecret().empty();
}

GdriveCredentials GdriveProvider::snapshotCredentials()
{
    GdriveCredentials creds;
    creds.clientId     = defaultClientId();
    creds.clientSecret = defaultClientSecret();
    creds.refreshToken = config::get().syncGdriveRefreshToken;
    return creds;
}

bool GdriveProvider::isConfigured() const
{
    const auto& cfg = config::get();
    return hasClientCredentials() && !cfg.syncGdriveRefreshToken.empty();
}

void GdriveProvider::clearCachedToken()
{
    std::lock_guard<std::mutex> lock(m_tokenMutex);
    m_cachedAccessToken.clear();
    m_cachedForKey.clear();
    m_tokenExpiry = 0;
}

void GdriveProvider::setCachedToken(const std::string& accessToken, int expiresIn, const std::string& cacheKey)
{
    if (accessToken.empty()) return;
    std::lock_guard<std::mutex> lock(m_tokenMutex);
    m_cachedAccessToken = accessToken;
    m_cachedForKey      = cacheKey;
    m_tokenExpiry       = (int64_t)std::time(nullptr) + (expiresIn > 0 ? expiresIn : 3600);
}

void GdriveProvider::disconnect()
{
    auto& cfg = config::get();
    cfg.syncGdriveRefreshToken.clear();
    cfg.syncGdriveUserEmail.clear();
    config::save();
    clearCachedToken();
}

bool GdriveProvider::startDeviceAuth(DeviceAuthInfo& info, std::string& err)
{
    return startDeviceAuth(defaultClientId(), info, err);
}

bool GdriveProvider::startDeviceAuth(const std::string& clientId, DeviceAuthInfo& info, std::string& err)
{
    err.clear();
    if (clientId.empty())
    {
        err = "Google OAuth Client ID is not configured";
        return false;
    }

    std::string url = "https://oauth2.googleapis.com/device/code";
    std::string postData = "client_id=" + http::urlEncode(clientId) +
                           "&scope=" + http::urlEncode("https://www.googleapis.com/auth/drive.file email");

    http::RequestOptions opt;
    opt.method      = "POST";
    opt.contentType = "application/x-www-form-urlencoded";
    opt.body        = postData;
    opt.timeoutSec  = 20;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        std::string detail = extractGoogleError(resp, err, status);
        if (json::str(resp, "error") == "invalid_client")
            detail += " (check OAuth Client ID and ensure type is 'TVs and Limited Input devices')";
        err = "Google Auth error (HTTP " + std::to_string(status) + "): " + detail;
        return false;
    }

    info.deviceCode      = json::str(resp, "device_code");
    info.userCode        = json::str(resp, "user_code");
    info.verificationUrl = json::str(resp, "verification_url");
    if (info.verificationUrl.empty())
        info.verificationUrl = json::str(resp, "verification_uri");
    if (info.verificationUrl.empty())
        info.verificationUrl = "https://www.google.com/device";

    info.expiresIn = (int)json::integer(resp, "expires_in", 1800);
    info.interval  = (int)json::integer(resp, "interval", 5);
    if (info.interval <= 0) info.interval = 5;

    if (info.deviceCode.empty() || info.userCode.empty())
    {
        err = "Invalid response from Google device code endpoint";
        return false;
    }
    return true;
}

PollStatus GdriveProvider::pollDeviceAuth(const std::string& clientId, const std::string& clientSecret,
                                          const std::string& deviceCode, GdriveTokens& tokens, std::string& err)
{
    err.clear();
    tokens = GdriveTokens{};

    if (clientId.empty() || clientSecret.empty())
    {
        err = "Google OAuth Client ID/Secret not configured";
        return PollStatus::Failed;
    }

    std::string url = "https://oauth2.googleapis.com/token";
    std::string postData = "client_id=" + http::urlEncode(clientId) +
                           "&client_secret=" + http::urlEncode(clientSecret) +
                           "&device_code=" + http::urlEncode(deviceCode) +
                           "&grant_type=" + http::urlEncode("urn:ietf:params:oauth:grant-type:device_code");

    http::RequestOptions opt;
    opt.method      = "POST";
    opt.contentType = "application/x-www-form-urlencoded";
    opt.body        = postData;
    opt.timeoutSec  = 15;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) && status == 0)
    {
        if (err.empty()) err = "Network error";
        return PollStatus::TransientError;
    }

    if (status == 200)
    {
        tokens.refreshToken = json::str(resp, "refresh_token");
        tokens.accessToken  = json::str(resp, "access_token");
        tokens.expiresIn    = (int)json::integer(resp, "expires_in", 3600);

        if (tokens.refreshToken.empty())
        {
            err = "Google did not return a refresh token; revoke app access in your Google Account and try again";
            return PollStatus::Failed;
        }

        if (!tokens.accessToken.empty())
        {
            setCachedToken(tokens.accessToken, tokens.expiresIn, clientId + "\n" + tokens.refreshToken);

            http::RequestOptions userOpt;
            userOpt.method     = "GET";
            userOpt.headers    = { "Authorization: Bearer " + tokens.accessToken };
            userOpt.timeoutSec = 10;

            std::string userResp, userErr;
            long userStatus = 0;
            if (http::request("https://www.googleapis.com/oauth2/v3/userinfo", userOpt, userResp, userErr, &userStatus) && userStatus == 200)
                tokens.userEmail = json::str(userResp, "email");

            if (tokens.userEmail.empty())
            {
                userResp.clear();
                userErr.clear();
                userStatus = 0;
                if (http::request("https://www.googleapis.com/oauth2/v2/userinfo", userOpt, userResp, userErr, &userStatus) && userStatus == 200)
                    tokens.userEmail = json::str(userResp, "email");
            }
        }
        return PollStatus::Approved;
    }

    std::string errorType = json::str(resp, "error");
    if (errorType == "authorization_pending")
        return PollStatus::Pending;
    if (errorType == "slow_down")
        return PollStatus::SlowDown;
    if (errorType == "access_denied")
    {
        err = "Authorization denied by user";
        return PollStatus::Failed;
    }
    if (errorType == "expired_token")
    {
        err = "Device code expired. Please try again.";
        return PollStatus::Failed;
    }

    err = extractGoogleError(resp, err, status);
    return PollStatus::Failed;
}

bool GdriveProvider::ensureAccessToken(const GdriveCredentials& creds, std::string& token, std::string& err)
{
    err.clear();
    if (!creds.hasClient())
    {
        err = "Google OAuth Client ID/Secret not configured";
        return false;
    }
    if (creds.refreshToken.empty())
    {
        err = "Google Drive is not authorized";
        return false;
    }

    const std::string cacheKey = creds.clientId + "\n" + creds.refreshToken;
    int64_t now = (int64_t)std::time(nullptr);
    {
        std::lock_guard<std::mutex> lock(m_tokenMutex);
        if (!m_cachedAccessToken.empty() && m_cachedForKey == cacheKey && now < m_tokenExpiry - 60)
        {
            token = m_cachedAccessToken;
            return true;
        }
    }

    std::string url = "https://oauth2.googleapis.com/token";
    std::string postData = "client_id=" + http::urlEncode(creds.clientId) +
                           "&client_secret=" + http::urlEncode(creds.clientSecret) +
                           "&refresh_token=" + http::urlEncode(creds.refreshToken) +
                           "&grant_type=refresh_token";

    http::RequestOptions opt;
    opt.method      = "POST";
    opt.contentType = "application/x-www-form-urlencoded";
    opt.body        = postData;
    opt.timeoutSec  = 15;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        if (json::str(resp, "error") == "invalid_grant")
        {
            clearCachedToken();
            err = "Google session expired or revoked. Please sign in to Google Drive again.";
        }
        else
        {
            err = "Failed to refresh Google token (HTTP " + std::to_string(status) + "): " +
                  extractGoogleError(resp, err, status);
        }
        return false;
    }

    std::string newAccess = json::str(resp, "access_token");
    int expiresIn = (int)json::integer(resp, "expires_in", 3600);
    if (newAccess.empty())
    {
        err = "Invalid token response from Google";
        return false;
    }

    setCachedToken(newAccess, expiresIn, cacheKey);
    token = newAccess;
    return true;
}

bool GdriveProvider::findSyncFolder(const std::string& token, std::string& folderId, std::string& err)
{
    folderId.clear();
    err.clear();
    std::string q = std::string("mimeType='application/vnd.google-apps.folder' and name='") +
                    kSyncFolderName + "' and trashed=false";
    std::string url = "https://www.googleapis.com/drive/v3/files?spaces=drive&q=" +
                      http::urlEncode(q) + "&fields=files(id,name)&pageSize=10";

    http::RequestOptions opt;
    opt.method     = "GET";
    opt.headers    = { "Authorization: Bearer " + token };
    opt.timeoutSec = 20;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Failed to query Drive folder (HTTP " + std::to_string(status) + "): " +
              extractGoogleError(resp, err, status);
        return false;
    }

    std::vector<std::string> files = json::objects(resp, "files");
    for (const auto& f : files)
    {
        if (json::str(f, "name") == kSyncFolderName)
        {
            folderId = json::str(f, "id");
            break;
        }
    }
    return true;
}

bool GdriveProvider::ensureSyncFolder(const std::string& token, std::string& folderId, std::string& err)
{
    if (!findSyncFolder(token, folderId, err))
        return false;
    if (!folderId.empty())
        return true;

    http::RequestOptions opt;
    opt.method      = "POST";
    opt.contentType = "application/json";
    opt.body        = std::string("{\"name\":\"") + kSyncFolderName +
                      "\",\"mimeType\":\"application/vnd.google-apps.folder\"}";
    opt.headers     = { "Authorization: Bearer " + token };
    opt.timeoutSec  = 20;

    std::string resp;
    long status = 0;
    if (!http::request("https://www.googleapis.com/drive/v3/files?fields=id", opt, resp, err, &status) ||
        (status != 200 && status != 201))
    {
        err = "Failed to create Drive folder (HTTP " + std::to_string(status) + "): " +
              extractGoogleError(resp, err, status);
        return false;
    }

    folderId = json::str(resp, "id");
    if (folderId.empty())
    {
        err = "Google Drive did not return folder ID";
        return false;
    }
    return true;
}

bool GdriveProvider::findSyncFile(const std::string& token, const std::string& folderId,
                                  const std::string& filename, std::string& fileId, std::string& err)
{
    fileId.clear();
    err.clear();
    std::string q = "name='" + filename + "' and '" + folderId + "' in parents and trashed=false";
    std::string url = "https://www.googleapis.com/drive/v3/files?spaces=drive&q=" +
                      http::urlEncode(q) + "&fields=files(id,name)&pageSize=10";

    http::RequestOptions opt;
    opt.method     = "GET";
    opt.headers    = { "Authorization: Bearer " + token };
    opt.timeoutSec = 20;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Failed to query Drive files (HTTP " + std::to_string(status) + "): " +
              extractGoogleError(resp, err, status);
        return false;
    }

    std::vector<std::string> files = json::objects(resp, "files");
    for (const auto& f : files)
    {
        if (json::str(f, "name") == filename)
        {
            fileId = json::str(f, "id");
            break;
        }
    }
    return true;
}

bool GdriveProvider::downloadSyncFile(const std::string& token, const std::string& fileId, std::string& content, std::string& err)
{
    content.clear();
    err.clear();
    std::string url = "https://www.googleapis.com/drive/v3/files/" + fileId + "?alt=media";

    http::RequestOptions opt;
    opt.method     = "GET";
    opt.headers    = { "Authorization: Bearer " + token };
    opt.timeoutSec = 25;

    long status = 0;
    if (!http::request(url, opt, content, err, &status) || status != 200)
    {
        err = "Failed to download " + fileId + " (HTTP " + std::to_string(status) + "): " +
              extractGoogleError(content, err, status);
        content.clear();
        return false;
    }
    return true;
}

bool GdriveProvider::uploadSyncFile(const std::string& token, const std::string& folderId,
                                    const std::string& filename, const std::string& content, std::string& err)
{
    std::string fileId;
    if (!findSyncFile(token, folderId, filename, fileId, err))
        return false;

    if (!fileId.empty())
    {
        // Update existing file
        std::string url = "https://www.googleapis.com/upload/drive/v3/files/" + fileId + "?uploadType=media";
        http::RequestOptions opt;
        opt.method      = "PATCH";
        opt.contentType = "application/json";
        opt.body        = content;
        opt.headers     = { "Authorization: Bearer " + token };
        opt.timeoutSec  = 25;

        std::string resp;
        long status = 0;
        if (!http::request(url, opt, resp, err, &status) || (status != 200 && status != 201))
        {
            err = "Failed to update " + filename + " (HTTP " + std::to_string(status) + "): " +
                  extractGoogleError(resp, err, status);
            return false;
        }
        return true;
    }
    else
    {
        // Create new file via multipart inside NX-Torrent-Player folder
        std::string boundary = "NX_SYNC_BOUNDARY_982341";
        std::string url = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";

        std::string metadata = "{\"name\": \"" + filename + "\", \"parents\": [\"" + folderId + "\"]}";
        std::string multipartBody = "--" + boundary + "\r\n"
                                    "Content-Type: application/json; charset=UTF-8\r\n\r\n"
                                    + metadata + "\r\n"
                                    "--" + boundary + "\r\n"
                                    "Content-Type: application/json\r\n\r\n"
                                    + content + "\r\n"
                                    "--" + boundary + "--\r\n";

        http::RequestOptions opt;
        opt.method      = "POST";
        opt.contentType = "multipart/related; boundary=" + boundary;
        opt.body        = multipartBody;
        opt.headers     = { "Authorization: Bearer " + token };
        opt.timeoutSec  = 25;

        std::string resp;
        long status = 0;
        if (!http::request(url, opt, resp, err, &status) || (status != 200 && status != 201))
        {
            err = "Failed to create " + filename + " (HTTP " + std::to_string(status) + "): " +
                  extractGoogleError(resp, err, status);
            return false;
        }
        return true;
    }
}

bool GdriveProvider::testConnection(std::string& err)
{
    return testConnection(snapshotCredentials(), err);
}

bool GdriveProvider::testConnection(const GdriveCredentials& creds, std::string& err)
{
    std::string token;
    if (!ensureAccessToken(creds, token, err))
        return false;

    std::string url = "https://www.googleapis.com/drive/v3/files?spaces=drive&pageSize=1&fields=files(id)";
    http::RequestOptions opt;
    opt.method     = "GET";
    opt.headers    = { "Authorization: Bearer " + token };
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Google Drive API error (HTTP " + std::to_string(status) + "): " +
              extractGoogleError(resp, err, status);
        return false;
    }
    return true;
}

bool GdriveProvider::pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err)
{
    return pull(snapshotCredentials(), remoteLibraryJson, remoteHistoryJson, err);
}

bool GdriveProvider::pull(const GdriveCredentials& creds, std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err)
{
    std::string token;
    if (!ensureAccessToken(creds, token, err))
        return false;

    std::string folderId;
    if (!findSyncFolder(token, folderId, err))
        return false;

    if (folderId.empty())
    {
        remoteLibraryJson.clear();
        remoteHistoryJson.clear();
        return true;
    }

    std::string libId;
    if (!findSyncFile(token, folderId, "nx_library.json", libId, err))
        return false;
    if (!libId.empty())
    {
        if (!downloadSyncFile(token, libId, remoteLibraryJson, err))
            return false;
    }
    else
    {
        remoteLibraryJson.clear();
    }

    std::string histId;
    if (!findSyncFile(token, folderId, "nx_history.json", histId, err))
        return false;
    if (!histId.empty())
    {
        if (!downloadSyncFile(token, histId, remoteHistoryJson, err))
            return false;
    }
    else
    {
        remoteHistoryJson.clear();
    }

    return true;
}

bool GdriveProvider::push(const std::string& libraryJson, const std::string& historyJson, std::string& err)
{
    return push(snapshotCredentials(), libraryJson, historyJson, err);
}

bool GdriveProvider::push(const GdriveCredentials& creds, const std::string& libraryJson, const std::string& historyJson, std::string& err)
{
    std::string token;
    if (!ensureAccessToken(creds, token, err))
        return false;

    std::string folderId;
    if (!ensureSyncFolder(token, folderId, err))
        return false;

    if (!uploadSyncFile(token, folderId, "nx_library.json", libraryJson, err))
        return false;

    if (!uploadSyncFile(token, folderId, "nx_history.json", historyJson, err))
        return false;

    return true;
}

} // namespace cloud_sync
