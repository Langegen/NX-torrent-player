#pragma once

#include <string>

namespace sync
{

class SyncProvider
{
public:
    virtual ~SyncProvider() = default;

    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;
    virtual bool isConfigured() const = 0;

    // Checks whether server is reachable and credentials are valid
    virtual bool testConnection(std::string& err) = 0;

    // Pulls remote library and history JSON strings.
    // If a remote file does not exist yet, returns true with that string empty.
    virtual bool pull(std::string& remoteLibraryJson, std::string& remoteHistoryJson, std::string& err) = 0;

    // Pushes merged library and history JSON strings to the remote storage.
    virtual bool push(const std::string& libraryJson, const std::string& historyJson, std::string& err) = 0;
};

} // namespace sync
