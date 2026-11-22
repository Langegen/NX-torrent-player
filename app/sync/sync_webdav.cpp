#include "sync_webdav.hpp"

#include "../config.hpp"
#include "../http.hpp"

namespace cloud_sync
{

std::string WebdavProvider::normalizeUrl(std::string url)
{
    while (!url.empty() && (url.back() == ' ' || url.back() == '\t' || url.back() == '/'))
        url.pop_back();
    while (!url.empty() && (url.front() == ' ' || url.front() == '\t'))
        url.erase(0, 1);
    if (!url.empty() && url.find("://") == std::string::npos)
        url = "https://" + url;
    return url;
}

bool WebdavProvider::isConfigured() const
{
    const auto& cfg = config::get();
    return !cfg.syncWebdavUrl.empty() && !cfg.syncWebdavUser.empty() && !cfg.syncWebdavPass.empty();
}

bool WebdavProvider::ensureFolder(const std::string& folderUrl, const std::string& userPwd, std::string& err)
{
    http::RequestOptions opt;
    opt.method = "PROPFIND";
    opt.headers = { "Depth: 0" };
    opt.userPwd = userPwd;
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    if (http::request(folderUrl, opt, resp, err, &status))
    {
        if (status == 200 || status == 207)
            return true;
    }

    if (status == 404)
    {
        http::RequestOptions mkcol;
        mkcol.method = "MKCOL";
        mkcol.userPwd = userPwd;
        mkcol.timeoutSec = 15;
        std::string mkResp;
        long mkStatus = 0;
        if (http::request(folderUrl, mkcol, mkResp, err, &mkStatus))
        {
            if (mkStatus == 201 || mkStatus == 200 || mkStatus == 405)
                return true;
        }
        err = "Failed to create remote directory (HTTP " + std::to_string(mkStatus) + ")";
        return false;
    }

    if (status == 401)
    {
        err = "Authentication failed: invalid username or app password";
        return false;
    }

    err = "Server check failed (HTTP " + std::to_string(status) + "): " + err;
    return false;
}

bool WebdavProvider::testConnection(std::string& err)
{
    if (!isConfigured())
    {
        err = "WebDAV credentials not set";
        return false;
    }

    const auto& cfg = config::get();
    std::string baseUrl = normalizeUrl(cfg.syncWebdavUrl);
    std::string userPwd = cfg.syncWebdavUser + ":" + cfg.syncWebdavPass;

    http::RequestOptions opt;
    opt.method = "PROPFIND";
    opt.headers = { "Depth: 0" };
    opt.userPwd = userPwd;
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    if (!http::request(baseUrl, opt, resp, err, &status))
    {
        return false;
    }

    if (status == 200 || status == 207)
        return true;

    if (status == 401)
    {
        err = "Authentication failed: check username and password";
        return false;
    }

    err = "Server returned HTTP " + std::to_string(status);
    return false;
}

bool WebdavProvider::pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err)
{
    if (!isConfigured())
    {
        err = "WebDAV credentials not configured";
        return false;
    }

    const auto& cfg = config::get();
    std::string baseUrl = normalizeUrl(cfg.syncWebdavUrl);
    std::string userPwd = cfg.syncWebdavUser + ":" + cfg.syncWebdavPass;
    std::string folderUrl = baseUrl + "/NX-torrent-player";

    // Pull library.json
    {
        http::RequestOptions opt;
        opt.method = "GET";
        opt.userPwd = userPwd;
        opt.timeoutSec = 20;
        long status = 0;
        std::string resp;
        if (!http::request(folderUrl + "/library.json", opt, resp, err, &status))
        {
            if (status != 404)
                return false;
        }
        if (status == 200)
            remoteLibraryJson = resp;
        else
            remoteLibraryJson.clear();
    }

    // Pull history.json
    {
        http::RequestOptions opt;
        opt.method = "GET";
        opt.userPwd = userPwd;
        opt.timeoutSec = 20;
        long status = 0;
        std::string resp;
        if (!http::request(folderUrl + "/history.json", opt, resp, err, &status))
        {
            if (status != 404)
                return false;
        }
        if (status == 200)
            remoteHistoryJson = resp;
        else
            remoteHistoryJson.clear();
    }

    return true;
}

bool WebdavProvider::push(const std::string& libraryJson, const std::string& historyJson, std::string& err)
{
    if (!isConfigured())
    {
        err = "WebDAV credentials not configured";
        return false;
    }

    const auto& cfg = config::get();
    std::string baseUrl = normalizeUrl(cfg.syncWebdavUrl);
    std::string userPwd = cfg.syncWebdavUser + ":" + cfg.syncWebdavPass;
    std::string folderUrl = baseUrl + "/NX-torrent-player";

    if (!ensureFolder(folderUrl, userPwd, err))
        return false;

    // Push library.json
    {
        http::RequestOptions opt;
        opt.method = "PUT";
        opt.contentType = "application/json";
        opt.body = libraryJson;
        opt.userPwd = userPwd;
        opt.timeoutSec = 25;
        long status = 0;
        std::string resp;
        if (!http::request(folderUrl + "/library.json", opt, resp, err, &status) || (status != 200 && status != 201 && status != 204))
        {
            err = "Failed to upload library.json (HTTP " + std::to_string(status) + "): " + err;
            return false;
        }
    }

    // Push history.json
    {
        http::RequestOptions opt;
        opt.method = "PUT";
        opt.contentType = "application/json";
        opt.body = historyJson;
        opt.userPwd = userPwd;
        opt.timeoutSec = 25;
        long status = 0;
        std::string resp;
        if (!http::request(folderUrl + "/history.json", opt, resp, err, &status) || (status != 200 && status != 201 && status != 204))
        {
            err = "Failed to upload history.json (HTTP " + std::to_string(status) + "): " + err;
            return false;
        }
    }

    return true;
}

} // namespace cloud_sync
