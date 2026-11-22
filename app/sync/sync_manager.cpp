#include "sync_manager.hpp"

#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>
#include <borealis.hpp>

#include "../config.hpp"
#include "../history.hpp"
#include "../library.hpp"

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
    while (m_syncing && waited < 20)
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
    {
        std::lock_guard<std::mutex> guard(g_managerLock);
        if (m_syncing)
        {
            if (onDone)
                brls::async([onDone]() { onDone(false, "Sync already in progress"); });
            return;
        }
        m_syncing = true;
        m_state = State::Syncing;
        m_lastMessage = "Syncing...";
    }

    std::thread([this, onDone]() {
        SyncProvider* p = activeProvider();
        if (!p || !p->isConfigured())
        {
            std::lock_guard<std::mutex> guard(g_managerLock);
            m_syncing = false;
            m_state = State::Error;
            m_lastMessage = "Sync provider is not configured";
            if (onDone)
            {
                std::string msg = m_lastMessage;
                brls::async([onDone, msg]() { onDone(false, msg); });
            }
            return;
        }

        std::string err;
        std::string remoteLib, remoteHist;

        // Step 1: Pull from cloud
        if (!p->pull(remoteLib, remoteHist, err))
        {
            std::lock_guard<std::mutex> guard(g_managerLock);
            m_syncing = false;
            m_state = State::Error;
            m_lastMessage = err;
            auto& cfg = config::get();
            cfg.syncLastError = err;
            config::save();
            if (onDone)
                brls::async([onDone, err]() { onDone(false, err); });
            return;
        }

        // Step 2: Merge into local store
        if (!remoteLib.empty())
        {
            library::mergeJson(remoteLib);
        }
        if (!remoteHist.empty())
        {
            history::mergeJson(remoteHist);
        }

        // Step 3: Snapshot local store to push merged data back to cloud
        std::string mergedLib = library::getJson();
        std::string mergedHist = history::getJson();

        // Step 4: Push to cloud
        if (!p->push(mergedLib, mergedHist, err))
        {
            std::lock_guard<std::mutex> guard(g_managerLock);
            m_syncing = false;
            m_state = State::Error;
            m_lastMessage = err;
            auto& cfg = config::get();
            cfg.syncLastError = err;
            config::save();
            if (onDone)
                brls::async([onDone, err]() { onDone(false, err); });
            return;
        }

        // Step 5: Success
        {
            std::lock_guard<std::mutex> guard(g_managerLock);
            m_syncing = false;
            m_state = State::Success;
            m_lastMessage = "Synchronized";
            auto& cfg = config::get();
            cfg.syncLastTime = std::time(nullptr);
            cfg.syncLastError.clear();
            config::save();
        }

        if (onDone)
        {
            brls::async([onDone]() { onDone(true, "Synchronized"); });
        }
    }).detach();
}

void init() { Manager::instance().init(); }
void shutdown() { Manager::instance().shutdown(); }
void syncNow(std::function<void(bool success, const std::string& message)> onDone) { Manager::instance().syncNow(onDone); }
void triggerAutoSync() { Manager::instance().triggerAutoSync(); }
State state() { return Manager::instance().state(); }
std::string lastMessage() { return Manager::instance().lastMessage(); }
SyncProvider* activeProvider() { return Manager::instance().activeProvider(); }

} // namespace cloud_sync
