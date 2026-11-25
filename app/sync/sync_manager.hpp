#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "sync_provider.hpp"
#include "sync_webdav.hpp"
#include "sync_gdrive.hpp"

namespace cloud_sync
{

enum class State
{
    Idle,
    Syncing,
    Success,
    Error
};

class Manager
{
public:
    static Manager& instance();

    void init();
    void shutdown();

    SyncProvider* activeProvider();
    WebdavProvider* webdavProvider();
    GdriveProvider* gdriveProvider();

    State state() const;
    std::string lastMessage() const;
    int64_t lastSyncTime() const;

    // Initiates synchronization on the workers pool.
    // onDone callback is delivered on the UI thread via brls::sync.
    void syncNow(std::function<void(bool success, const std::string& message)> onDone = nullptr);

    // Checks auto-sync setting and runs sync in background if configured and idle.
    void triggerAutoSync();

    // Runs connection checks on the workers pool and returns result on UI thread.
    void testWebdavAsync(std::function<void(bool ok, const std::string& message)> onDone);
    void testGdriveAsync(std::function<void(bool ok, const std::string& message)> onDone);

private:
    Manager();
    ~Manager() = default;

    std::unique_ptr<WebdavProvider> m_webdav;
    std::unique_ptr<GdriveProvider> m_gdrive;

    State m_state = State::Idle;
    std::string m_lastMessage;
    std::atomic<bool> m_syncing{ false };
};

// Global convenience wrappers
void init();
void shutdown();
void syncNow(std::function<void(bool success, const std::string& message)> onDone = nullptr);
void triggerAutoSync();
void testWebdavAsync(std::function<void(bool ok, const std::string& message)> onDone);
void testGdriveAsync(std::function<void(bool ok, const std::string& message)> onDone);
State state();
std::string lastMessage();
SyncProvider* activeProvider();

} // namespace cloud_sync
