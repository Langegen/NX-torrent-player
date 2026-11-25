#pragma once

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

    State state() const { return m_state; }
    std::string lastMessage() const { return m_lastMessage; }
    int64_t lastSyncTime() const;

    // Initiates synchronization in the background.
    // onDone callback is delivered (guaranteed off the sync thread or via brls::async) with result.
    void syncNow(std::function<void(bool success, const std::string& message)> onDone = nullptr);

    // Checks auto-sync setting and runs sync in background if configured and idle
    void triggerAutoSync();

private:
    Manager();
    ~Manager() = default;

    std::unique_ptr<WebdavProvider> m_webdav;
    std::unique_ptr<GdriveProvider> m_gdrive;

    State m_state = State::Idle;
    std::string m_lastMessage;
    bool m_syncing = false;
};

// Global convenience wrappers
void init();
void shutdown();
void syncNow(std::function<void(bool success, const std::string& message)> onDone = nullptr);
void triggerAutoSync();
State state();
std::string lastMessage();
SyncProvider* activeProvider();

} // namespace cloud_sync
