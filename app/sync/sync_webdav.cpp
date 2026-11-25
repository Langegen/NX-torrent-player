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

WebdavConfig WebdavProvider::snapshotConfig()
{
    const auto& cfg = config::get();
    WebdavConfig out;
    out.url  = cfg.syncWebdavUrl;
    out.user = cfg.syncWebdavUser;
    out.pass = cfg.syncWebdavPass;
    return out;
}

bool WebdavProvider::isConfigured() const
{
    return snapshotConfig().valid();
}

bool WebdavProvider::ensureFolder(const std::string& folderUrl, const WebdavConfig& cfg, std::string& err)
{
    err.clear();
    http::RequestOptions opt;
    opt.method     = "PROPFIND";
    opt.headers    = { "Depth: 0" };
    opt.username   = cfg.user;
    opt.password   = cfg.pass;
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    bool reqOk = http::request(folderUrl, opt, resp, err, &status);
    if (reqOk && (status == 200 || status == 207))
        return true;

    if (status == 404)
    {
        err.clear();
        http::RequestOptions mkcol;
        mkcol.method     = "MKCOL";
        mkcol.username   = cfg.user;
        mkcol.password   = cfg.pass;
        mkcol.timeoutSec = 15;
        std::string mkResp;
        long mkStatus = 0;
        if (http::request(folderUrl, mkcol, mkResp, err, &mkStatus))
        {
            if (mkStatus == 200 || mkStatus == 201 || mkStatus == 204 || mkStatus == 405)
                return true;
        }
        if (mkStatus == 401)
            err = "Authentication failed: invalid username or app password";
        else if (mkStatus == 403)
            err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
        else if (mkStatus != 0)
            err = "Failed to create remote directory (HTTP " + std::to_string(mkStatus) + ")";
        else if (err.empty())
            err = "Failed to create remote directory";
        return false;
    }

    if (status == 401)
    {
        err = "Authentication failed: invalid username or app password";
        return false;
    }
    if (status == 403)
    {
        err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
        return false;
    }

    if (status != 0)
        err = "Server check failed (HTTP " + std::to_string(status) + ")" + (err.empty() ? "" : ": " + err);
    else if (err.empty())
        err = "Server check failed";
    return false;
}

bool WebdavProvider::testConnection(std::string& err)
{
    return testConnection(snapshotConfig(), err);
}

bool WebdavProvider::testConnection(const WebdavConfig& cfg, std::string& err)
{
    err.clear();
    if (!cfg.valid())
    {
        err = "WebDAV credentials not set";
        return false;
    }

    std::string baseUrl = normalizeUrl(cfg.url);

    http::RequestOptions opt;
    opt.method     = "PROPFIND";
    opt.headers    = { "Depth: 0" };
    opt.username   = cfg.user;
    opt.password   = cfg.pass;
    opt.timeoutSec = 15;

    std::string resp;
    long status = 0;
    if (!http::request(baseUrl, opt, resp, err, &status))
    {
        if (status == 401)
            err = "Authentication failed: check username and password";
        else if (status == 403)
            err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
        return false;
    }

    if (status == 200 || status == 207)
        return true;

    if (status == 401)
    {
        err = "Authentication failed: check username and password";
        return false;
    }
    if (status == 403)
    {
        err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
        return false;
    }

    err = "Server returned HTTP " + std::to_string(status);
    return false;
}

bool WebdavProvider::pullFile(const std::string& folderUrl, const std::string& filename,
                              const WebdavConfig& cfg, std::string& outJson, std::string& err)
{
    err.clear();
    outJson.clear();

    http::RequestOptions opt;
    opt.method     = "GET";
    opt.username   = cfg.user;
    opt.password   = cfg.pass;
    opt.timeoutSec = 20;

    long status = 0;
    std::string resp;
    bool reqOk = http::request(folderUrl + "/" + filename, opt, resp, err, &status);
    if (!reqOk)
    {
        if (status == 401)
            err = "Authentication failed: check username and app password";
        else if (status == 403)
            err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
        else if (status != 0 && err.empty())
            err = "Failed to download " + filename + " (HTTP " + std::to_string(status) + ")";
        return false;
    }

    if (status == 200)
    {
        outJson = std::move(resp);
        return true;
    }
    if (status == 404)
    {
        outJson.clear();
        return true;
    }
    if (status == 401)
    {
        err = "Authentication failed: check username and app password";
        return false;
    }
    if (status == 403)
    {
        err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
        return false;
    }

    err = "Failed to download " + filename + " (HTTP " + std::to_string(status) + ")";
    return false;
}

bool WebdavProvider::pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err)
{
    return pull(snapshotConfig(), remoteLibraryJson, remoteHistoryJson, err);
}

bool WebdavProvider::pull(const WebdavConfig& cfg, std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err)
{
    err.clear();
    if (!cfg.valid())
    {
        err = "WebDAV credentials not configured";
        return false;
    }

    std::string baseUrl   = normalizeUrl(cfg.url);
    std::string folderUrl = baseUrl + "/NX-torrent-player";

    if (!pullFile(folderUrl, "library.json", cfg, remoteLibraryJson, err))
        return false;
    if (!pullFile(folderUrl, "history.json", cfg, remoteHistoryJson, err))
        return false;

    return true;
}

bool WebdavProvider::push(const std::string& libraryJson, const std::string& historyJson, std::string& err)
{
    return push(snapshotConfig(), libraryJson, historyJson, err);
}

bool WebdavProvider::push(const WebdavConfig& cfg, const std::string& libraryJson, const std::string& historyJson, std::string& err)
{
    err.clear();
    if (!cfg.valid())
    {
        err = "WebDAV credentials not configured";
        return false;
    }

    std::string baseUrl   = normalizeUrl(cfg.url);
    std::string folderUrl = baseUrl + "/NX-torrent-player";

    if (!ensureFolder(folderUrl, cfg, err))
        return false;

    // Push library.json
    {
        err.clear();
        http::RequestOptions opt;
        opt.method      = "PUT";
        opt.contentType = "application/json";
        opt.body        = libraryJson;
        opt.username    = cfg.user;
        opt.password    = cfg.pass;
        opt.timeoutSec  = 25;
        long status = 0;
        std::string resp;
        if (!http::request(folderUrl + "/library.json", opt, resp, err, &status) ||
            (status != 200 && status != 201 && status != 204))
        {
            if (status == 401)
                err = "Authentication failed: check username and app password";
            else if (status == 403)
                err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
            else
                err = "Failed to upload library.json (HTTP " + std::to_string(status) + ")" + (err.empty() ? "" : ": " + err);
            return false;
        }
    }

    // Push history.json
    {
        err.clear();
        http::RequestOptions opt;
        opt.method      = "PUT";
        opt.contentType = "application/json";
        opt.body        = historyJson;
        opt.username    = cfg.user;
        opt.password    = cfg.pass;
        opt.timeoutSec  = 25;
        long status = 0;
        std::string resp;
        if (!http::request(folderUrl + "/history.json", opt, resp, err, &status) ||
            (status != 200 && status != 201 && status != 204))
        {
            if (status == 401)
                err = "Authentication failed: check username and app password";
            else if (status == 403)
                err = "Access forbidden (HTTP 403): check WebDAV permissions or app password";
            else
                err = "Failed to upload history.json (HTTP " + std::to_string(status) + ")" + (err.empty() ? "" : ": " + err);
            return false;
        }
    }

    return true;
}

} // namespace cloud_sync
