#include "sync_manager.hpp"

#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <borealis.hpp>

#include "../config.hpp"
#include "../history.hpp"
#include "../library.hpp"
#include "../workers.hpp"

namespace cloud_sync
{

namespace
{
std::mutex g_managerLock;
}

Manager::Manager()
    : m_webdav(std::make_unique<WebdavProvider>()),
      m_gdrive(std::make_unique<GdriveProvider>())
{
}

Manager& Manager::instance()
{
    static Manager mgr;
    return mgr;
}

void Manager::init()
{
    // Providers are initialized in constructor
}

void Manager::shutdown()
{
    // Wait briefly if sync in flight
    int waited = 0;
    while (m_syncing.load() && waited < 40)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited++;
    }
}

SyncProvider* Manager::activeProvider()
{
    const auto& cfg = config::get();
    if (cfg.syncProvider == "gdrive")
        return m_gdrive.get();
    if (cfg.syncProvider == "webdav")
        return m_webdav.get();
    return nullptr;
}

WebdavProvider* Manager::webdavProvider()
{
    return m_webdav.get();
}

GdriveProvider* Manager::gdriveProvider()
{
    return m_gdrive.get();
}

State Manager::state() const
{
    std::lock_guard<std::mutex> guard(g_managerLock);
    return m_state;
}

std::string Manager::lastMessage() const
{
    std::lock_guard<std::mutex> guard(g_managerLock);
    return m_lastMessage;
}

int64_t Manager::lastSyncTime() const
{
    return config::get().syncLastTime;
}

void Manager::triggerAutoSync()
{
    const auto& cfg = config::get();
    if (!cfg.syncAuto)
        return;

    SyncProvider* p = activeProvider();
    if (!p || !p->isConfigured())
        return;

    syncNow(nullptr);
}

void Manager::syncNow(std::function<void(bool success, const std::string& message)> onDone)
{
    std::string provider = config::get().syncProvider;
    WebdavConfig webdavCfg;
    GdriveCredentials gdriveCreds;
    std::string notConfiguredErr;

    if (provider == "gdrive")
    {
        gdriveCreds = GdriveProvider::snapshotCredentials();
        if (!gdriveCreds.hasClient())
            notConfiguredErr = "Google OAuth Client ID/Secret not configured";
        else if (gdriveCreds.refreshToken.empty())
            notConfiguredErr = "Google Drive is not authorized";
    }
    else if (provider == "webdav")
    {
        webdavCfg = WebdavProvider::snapshotConfig();
        if (!webdavCfg.valid())
            notConfiguredErr = "WebDAV credentials not configured";
    }
    else
    {
        notConfiguredErr = "Sync provider is not configured";
    }

    if (!notConfiguredErr.empty())
    {
        {
            std::lock_guard<std::mutex> guard(g_managerLock);
            m_state       = State::Error;
            m_lastMessage = notConfiguredErr;
        }
        if (onDone)
            brls::sync([onDone, notConfiguredErr]() { onDone(false, notConfiguredErr); });
        return;
    }

    {
        std::lock_guard<std::mutex> guard(g_managerLock);
        if (m_syncing.load())
        {
            if (onDone)
                brls::sync([onDone]() { onDone(false, "Sync already in progress"); });
            return;
        }
        m_syncing.store(true);
        m_state       = State::Syncing;
        m_lastMessage = "Syncing...";
    }

    workers::run([this, provider, webdavCfg, gdriveCreds, onDone]() {
        std::string err;
        std::string remoteLib, remoteHist;

        auto pullFn = [&](std::string& libOut, std::string& histOut, std::string& errOut) -> bool {
            if (provider == "gdrive")
                return m_gdrive->pull(gdriveCreds, libOut, histOut, errOut);
            return m_webdav->pull(webdavCfg, libOut, histOut, errOut);
        };
        auto pushFn = [&](const std::string& libIn, const std::string& histIn, std::string& errOut) -> bool {
            if (provider == "gdrive")
                return m_gdrive->push(gdriveCreds, libIn, histIn, errOut);
            return m_webdav->push(webdavCfg, libIn, histIn, errOut);
        };

        auto finishError = [&](const std::string& failErr) {
            {
                std::lock_guard<std::mutex> guard(g_managerLock);
                m_syncing.store(false);
                m_state       = State::Error;
                m_lastMessage = failErr;
            }
            brls::sync([failErr, onDone]() {
                config::get().syncLastError = failErr;
                config::save();
                if (onDone) onDone(false, failErr);
            });
        };

        // Step 1: Pull from cloud
        if (!pullFn(remoteLib, remoteHist, err))
        {
            finishError(err.empty() ? "Pull failed" : err);
            return;
        }

        // Step 2: Merge into local store
        if (!remoteLib.empty())
            library::mergeJson(remoteLib);
        if (!remoteHist.empty())
            history::mergeJson(remoteHist);

        // Step 3: Snapshot local store to push merged data back to cloud
        std::string mergedLib  = library::getJson();
        std::string mergedHist = history::getJson();

        // Step 4: Push to cloud
        if (!pushFn(mergedLib, mergedHist, err))
        {
            finishError(err.empty() ? "Push failed" : err);
            return;
        }

        // Step 5: Success
        int64_t syncTs = (int64_t)std::time(nullptr);
        {
            std::lock_guard<std::mutex> guard(g_managerLock);
            m_syncing.store(false);
            m_state       = State::Success;
            m_lastMessage = "Synchronized";
        }

        brls::sync([syncTs, onDone]() {
            auto& cfg        = config::get();
            cfg.syncLastTime = syncTs;
            cfg.syncLastError.clear();
            config::save();
            if (onDone) onDone(true, "Synchronized");
        });
    }, workers::Priority::High);
}

void Manager::testWebdavAsync(std::function<void(bool ok, const std::string& message)> onDone)
{
    WebdavConfig cfg = WebdavProvider::snapshotConfig();
    workers::run([this, cfg, onDone]() {
        std::string err;
        bool ok = m_webdav->testConnection(cfg, err);
        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    }, workers::Priority::High);
}

void Manager::testGdriveAsync(std::function<void(bool ok, const std::string& message)> onDone)
{
    GdriveCredentials creds = GdriveProvider::snapshotCredentials();
    workers::run([this, creds, onDone]() {
        std::string err;
        bool ok = m_gdrive->testConnection(creds, err);
        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    }, workers::Priority::High);
}

void init() { Manager::instance().init(); }
void shutdown() { Manager::instance().shutdown(); }
void syncNow(std::function<void(bool success, const std::string& message)> onDone) { Manager::instance().syncNow(onDone); }
void triggerAutoSync() { Manager::instance().triggerAutoSync(); }
void testWebdavAsync(std::function<void(bool ok, const std::string& message)> onDone) { Manager::instance().testWebdavAsync(onDone); }
void testGdriveAsync(std::function<void(bool ok, const std::string& message)> onDone) { Manager::instance().testGdriveAsync(onDone); }
State state() { return Manager::instance().state(); }
std::string lastMessage() { return Manager::instance().lastMessage(); }
SyncProvider* activeProvider() { return Manager::instance().activeProvider(); }

} // namespace cloud_sync
