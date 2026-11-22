#include "sync_gdrive.hpp"

#include <ctime>
#include "../config.hpp"
#include "../http.hpp"
#include "../json.hpp"

namespace cloud_sync
{

// Default fallback client ID for open source community testing / device flow.
// Users can provide their own client ID and secret in config if desired.
std::string GdriveProvider::defaultClientId()
{
    const auto& cfg = config::get();
    if (!cfg.syncGdriveClientId.empty())
        return cfg.syncGdriveClientId;
    return "924822003714-3knpl8eb079c65691lqf253v5q29b9eb.apps.googleusercontent.com";
}

std::string GdriveProvider::defaultClientSecret()
{
    const auto& cfg = config::get();
    if (!cfg.syncGdriveClientSecret.empty())
        return cfg.syncGdriveClientSecret;
    return "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";
}

bool GdriveProvider::isConfigured() const
{
    const auto& cfg = config::get();
    return !cfg.syncGdriveRefreshToken.empty();
}

void GdriveProvider::disconnect()
{
    auto& cfg = config::get();
    cfg.syncGdriveRefreshToken.clear();
    cfg.syncGdriveUserEmail.clear();
    config::save();
    m_cachedAccessToken.clear();
    m_tokenExpiry = 0;
}

bool GdriveProvider::startDeviceAuth(DeviceAuthInfo& info, std::string& err)
{
    std::string clientId = defaultClientId();
    std::string url = "https://oauth2.googleapis.com/device/code";

    std::string postData = "client_id=" + http::urlEncode(clientId) +
                           "&scope=" + http::urlEncode("https://www.googleapis.com/auth/drive.appdata email");

    http::RequestOptions opt;
    opt.method = "POST";
    opt.contentType = "application/x-www-form-urlencoded";
    opt.body = postData;
    opt.timeoutSec = 20;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Google Auth error (HTTP " + std::to_string(status) + "): " + (resp.empty() ? err : resp);
        return false;
    }

    info.deviceCode = json::str(resp, "device_code");
    info.userCode = json::str(resp, "user_code");
    info.verificationUrl = json::str(resp, "verification_url");
    info.expiresIn = (int)json::integer(resp, "expires_in", 1800);
    info.interval = (int)json::integer(resp, "interval", 5);

    if (info.verificationUrl.empty())
        info.verificationUrl = "https://www.google.com/device";

    return !info.deviceCode.empty() && !info.userCode.empty();
}

int GdriveProvider::pollDeviceAuth(const std::string& deviceCode, std::string& err)
{
    std::string clientId = defaultClientId();
    std::string clientSecret = defaultClientSecret();
    std::string url = "https://oauth2.googleapis.com/token";

    std::string postData = "client_id=" + http::urlEncode(clientId) +
                           "&client_secret=" + http::urlEncode(clientSecret) +
                           "&device_code=" + http::urlEncode(deviceCode) +
                           "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    http::RequestOptions opt;
    opt.method = "POST";
    opt.contentType = "application/x-www-form-urlencoded";
    opt.body = postData;
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    http::request(url, opt, resp, err, &status);

    if (status == 200)
    {
        std::string refreshToken = json::str(resp, "refresh_token");
        std::string accessToken = json::str(resp, "access_token");
        int expiresIn = (int)json::integer(resp, "expires_in", 3600);

        if (!refreshToken.empty())
        {
            auto& cfg = config::get();
            cfg.syncGdriveRefreshToken = refreshToken;

            // Fetch user email
            if (!accessToken.empty())
            {
                m_cachedAccessToken = accessToken;
                m_tokenExpiry = std::time(nullptr) + expiresIn;

                http::RequestOptions userOpt;
                userOpt.method = "GET";
                userOpt.headers = { "Authorization: Bearer " + accessToken };
                std::string userResp, userErr;
                long userStatus = 0;
                if (http::request("https://www.googleapis.com/oauth2/v2/userinfo", userOpt, userResp, userErr, &userStatus) && userStatus == 200)
                {
                    cfg.syncGdriveUserEmail = json::str(userResp, "email");
                }
            }
            config::save();
            return 1; // Success
        }
    }

    std::string errorType = json::str(resp, "error");
    if (errorType == "authorization_pending" || errorType == "slow_down")
        return 0; // Pending

    err = json::str(resp, "error_description");
    if (err.empty()) err = errorType;
    if (err.empty()) err = "Authorization expired or failed";
    return -1; // Failed
}

bool GdriveProvider::ensureAccessToken(std::string& token, std::string& err)
{
    int64_t now = std::time(nullptr);
    if (!m_cachedAccessToken.empty() && now < m_tokenExpiry - 60)
    {
        token = m_cachedAccessToken;
        return true;
    }

    const auto& cfg = config::get();
    if (cfg.syncGdriveRefreshToken.empty())
    {
        err = "Google Drive is not authorized";
        return false;
    }

    std::string clientId = defaultClientId();
    std::string clientSecret = defaultClientSecret();
    std::string url = "https://oauth2.googleapis.com/token";

    std::string postData = "client_id=" + http::urlEncode(clientId) +
                           "&client_secret=" + http::urlEncode(clientSecret) +
                           "&refresh_token=" + http::urlEncode(cfg.syncGdriveRefreshToken) +
                           "&grant_type=refresh_token";

    http::RequestOptions opt;
    opt.method = "POST";
    opt.contentType = "application/x-www-form-urlencoded";
    opt.body = postData;
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Failed to refresh Google token (HTTP " + std::to_string(status) + "): " + (resp.empty() ? err : resp);
        return false;
    }

    std::string newAccess = json::str(resp, "access_token");
    int expiresIn = (int)json::integer(resp, "expires_in", 3600);
    if (newAccess.empty())
    {
        err = "Invalid token response from Google";
        return false;
    }

    m_cachedAccessToken = newAccess;
    m_tokenExpiry = now + expiresIn;
    token = m_cachedAccessToken;
    return true;
}

bool GdriveProvider::findAppDataFile(const std::string& token, const std::string& filename, std::string& fileId, std::string& err)
{
    fileId.clear();
    std::string url = "https://www.googleapis.com/drive/v3/files?spaces=appDataFolder&fields=files(id,name)&pageSize=100";

    http::RequestOptions opt;
    opt.method = "GET";
    opt.headers = { "Authorization: Bearer " + token };
    opt.timeoutSec = 20;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Failed to query appDataFolder (HTTP " + std::to_string(status) + "): " + err;
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

bool GdriveProvider::downloadAppDataFile(const std::string& token, const std::string& fileId, std::string& content, std::string& err)
{
    std::string url = "https://www.googleapis.com/drive/v3/files/" + fileId + "?alt=media";

    http::RequestOptions opt;
    opt.method = "GET";
    opt.headers = { "Authorization: Bearer " + token };
    opt.timeoutSec = 25;

    long status = 0;
    if (!http::request(url, opt, content, err, &status) || status != 200)
    {
        err = "Failed to download " + fileId + " (HTTP " + std::to_string(status) + "): " + err;
        return false;
    }
    return true;
}

bool GdriveProvider::uploadAppDataFile(const std::string& token, const std::string& filename, const std::string& content, std::string& err)
{
    std::string fileId;
    if (!findAppDataFile(token, filename, fileId, err))
        return false;

    if (!fileId.empty())
    {
        // Update existing file
        std::string url = "https://www.googleapis.com/upload/drive/v3/files/" + fileId + "?uploadType=media";
        http::RequestOptions opt;
        opt.method = "PATCH";
        opt.contentType = "application/json";
        opt.body = content;
        opt.headers = { "Authorization: Bearer " + token };
        opt.timeoutSec = 25;

        std::string resp;
        long status = 0;
        if (!http::request(url, opt, resp, err, &status) || status != 200)
        {
            err = "Failed to update " + filename + " (HTTP " + std::to_string(status) + "): " + err;
            return false;
        }
        return true;
    }
    else
    {
        // Create new file via multipart
        std::string boundary = "NX_SYNC_BOUNDARY_982341";
        std::string url = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";

        std::string metadata = "{\"name\": \"" + filename + "\", \"parents\": [\"appDataFolder\"]}";
        std::string multipartBody = "--" + boundary + "\r\n"
                                    "Content-Type: application/json; charset=UTF-8\r\n\r\n"
                                    + metadata + "\r\n"
                                    "--" + boundary + "\r\n"
                                    "Content-Type: application/json\r\n\r\n"
                                    + content + "\r\n"
                                    "--" + boundary + "--\r\n";

        http::RequestOptions opt;
        opt.method = "POST";
        opt.contentType = "multipart/related; boundary=" + boundary;
        opt.body = multipartBody;
        opt.headers = { "Authorization: Bearer " + token };
        opt.timeoutSec = 25;

        std::string resp;
        long status = 0;
        if (!http::request(url, opt, resp, err, &status) || status != 200)
        {
            err = "Failed to create " + filename + " (HTTP " + std::to_string(status) + "): " + err;
            return false;
        }
        return true;
    }
}

bool GdriveProvider::testConnection(std::string& err)
{
    std::string token;
    if (!ensureAccessToken(token, err))
        return false;

    std::string url = "https://www.googleapis.com/drive/v3/about?fields=user";
    http::RequestOptions opt;
    opt.method = "GET";
    opt.headers = { "Authorization: Bearer " + token };
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    if (!http::request(url, opt, resp, err, &status) || status != 200)
    {
        err = "Google Drive API error (HTTP " + std::to_string(status) + ")";
        return false;
    }
    return true;
}

bool GdriveProvider::pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err)
{
    std::string token;
    if (!ensureAccessToken(token, err))
        return false;

    std::string libId;
    if (!findAppDataFile(token, "nx_library.json", libId, err))
        return false;
    if (!libId.empty())
    {
        if (!downloadAppDataFile(token, libId, remoteLibraryJson, err))
            return false;
    }
    else
    {
        remoteLibraryJson.clear();
    }

    std::string histId;
    if (!findAppDataFile(token, "nx_history.json", histId, err))
        return false;
    if (!histId.empty())
    {
        if (!downloadAppDataFile(token, histId, remoteHistoryJson, err))
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
    std::string token;
    if (!ensureAccessToken(token, err))
        return false;

    if (!uploadAppDataFile(token, "nx_library.json", libraryJson, err))
        return false;

    if (!uploadAppDataFile(token, "nx_history.json", historyJson, err))
        return false;

    return true;
}

} // namespace cloud_sync
