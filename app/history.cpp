#include "history.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <borealis.hpp>

#include "appdata.hpp"
#include "json.hpp"

namespace history
{
namespace
{

std::mutex g_lock;
std::vector<Item> g_items;
bool g_loaded = false;
uint32_t g_generation = 0;

// Serialises the file writes themselves: a background save and a forced one
// (the player closing) must not interleave on history.json.tmp.
std::mutex g_saveLock;
// A background save is already queued; it snapshots the list when it runs, so
// a second request in the meantime has nothing to add.
std::atomic<bool> g_savePending{ false };

constexpr size_t kMaxItems = 100;

} // namespace

uint32_t generation()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_generation;
}

std::string makeId(const std::string& source, int fileIndex, const std::string& stremioVideoId)
{
    if (!stremioVideoId.empty())
        return "stremio:" + stremioVideoId;
    if (fileIndex >= 0)
        return source + "#" + std::to_string(fileIndex);
    return source;
}

void load()
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_items.clear();
    g_loaded = true;

    // save() removes the old file before renaming the new one into place (FAT
    // will not rename over an existing file); a crash between the two leaves
    // only the .tmp, which is complete -- read that rather than lose everything.
    std::string tmp = std::string(APPDATA_HISTORY) + ".tmp";
    FILE* f = std::fopen(APPDATA_HISTORY, "rb");
    if (!f) f = std::fopen(tmp.c_str(), "rb");
    if (!f) return;

    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0)
    {
        std::fclose(f);
        return;
    }

    std::string body;
    body.resize(sz);
    size_t rd = std::fread(&body[0], 1, sz, f);
    std::fclose(f);
    body.resize(rd);

    std::vector<std::string> objs = json::objects(body, "history");
    g_items.reserve(objs.size());

    for (const auto& obj : objs)
    {
        Item it;
        it.id = json::str(obj, "id");
        it.source = json::str(obj, "source");
        it.fileIndex = (int)json::integer(obj, "fileIndex", -1);
        it.title = json::str(obj, "title");
        it.sourceType = json::str(obj, "sourceType");
        it.posterPath = json::str(obj, "posterPath");
        it.positionSec = json::number(obj, "positionSec", 0.0);
        it.durationSec = json::number(obj, "durationSec", 0.0);
        it.lastWatched = json::integer(obj, "lastWatched", 0);
        it.completed = json::boolean(obj, "completed", false);
        it.stremioAuthKey = json::str(obj, "stremioAuthKey");
        it.stremioItemId = json::str(obj, "stremioItemId");
        it.stremioVideoId = json::str(obj, "stremioVideoId");
        it.stremioType = json::str(obj, "stremioType");

        if (it.id.empty())
            it.id = makeId(it.source, it.fileIndex, it.stremioVideoId);

        g_items.push_back(std::move(it));
    }

    std::sort(g_items.begin(), g_items.end(), [](const Item& a, const Item& b) {
        return a.lastWatched > b.lastWatched;
    });

    if (g_items.size() > kMaxItems)
        g_items.resize(kMaxItems);
}

void save()
{
    std::lock_guard<std::mutex> saveGuard(g_saveLock);
    std::vector<Item> copy;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        copy = g_items;
    }

    std::string out = "{\n  \"history\": [\n";
    for (size_t i = 0; i < copy.size(); i++)
    {
        const auto& it = copy[i];
        out += "    {\n";
        out += "      \"id\": \"" + json::escape(it.id) + "\",\n";
        out += "      \"source\": \"" + json::escape(it.source) + "\",\n";
        out += "      \"fileIndex\": " + std::to_string(it.fileIndex) + ",\n";
        out += "      \"title\": \"" + json::escape(it.title) + "\",\n";
        out += "      \"sourceType\": \"" + json::escape(it.sourceType) + "\",\n";
        out += "      \"posterPath\": \"" + json::escape(it.posterPath) + "\",\n";
        out += "      \"positionSec\": " + std::to_string(it.positionSec) + ",\n";
        out += "      \"durationSec\": " + std::to_string(it.durationSec) + ",\n";
        out += "      \"lastWatched\": " + std::to_string(it.lastWatched) + ",\n";
        out += "      \"completed\": " + std::string(it.completed ? "true" : "false") + ",\n";
        out += "      \"stremioAuthKey\": \"" + json::escape(it.stremioAuthKey) + "\",\n";
        out += "      \"stremioItemId\": \"" + json::escape(it.stremioItemId) + "\",\n";
        out += "      \"stremioVideoId\": \"" + json::escape(it.stremioVideoId) + "\",\n";
        out += "      \"stremioType\": \"" + json::escape(it.stremioType) + "\"\n";
        out += "    }" + std::string(i + 1 < copy.size() ? "," : "") + "\n";
    }
    out += "  ]\n}\n";

    std::string tmp = std::string(APPDATA_HISTORY) + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f)
    {
        brls::Logger::error("[history] failed to open {} for writing", tmp);
        return;
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fflush(f);
    std::fclose(f);

    std::remove(APPDATA_HISTORY);
    std::rename(tmp.c_str(), APPDATA_HISTORY);
}

std::vector<Item> all()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_items;
}

bool find(const std::string& source, int fileIndex, const std::string& stremioVideoId,
          Item& out)
{
    std::lock_guard<std::mutex> guard(g_lock);
    std::string targetId = makeId(source, fileIndex, stremioVideoId);
    for (const auto& it : g_items)
    {
        if (it.id == targetId ||
            (!stremioVideoId.empty() && it.stremioVideoId == stremioVideoId) ||
            (it.source == source && it.fileIndex == fileIndex))
        {
            out = it;
            return true;
        }
    }
    return false;
}

void record(const Item& item, bool saveNow)
{
    std::string id = item.id.empty() ? makeId(item.source, item.fileIndex, item.stremioVideoId) : item.id;
    int64_t now = item.lastWatched != 0 ? item.lastWatched : (int64_t)std::time(nullptr);

    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = std::find_if(g_items.begin(), g_items.end(), [&](const Item& x) {
            return x.id == id;
        });

        if (it != g_items.end())
        {
            it->positionSec = item.positionSec;
            it->durationSec = item.durationSec;
            it->lastWatched = now;
            it->completed = item.completed;
            if (!item.title.empty()) it->title = item.title;
            if (!item.posterPath.empty()) it->posterPath = item.posterPath;
            if (!item.sourceType.empty()) it->sourceType = item.sourceType;
            if (!item.source.empty()) it->source = item.source;
            if (item.fileIndex >= 0) it->fileIndex = item.fileIndex;
            if (!item.stremioAuthKey.empty()) it->stremioAuthKey = item.stremioAuthKey;
            if (!item.stremioItemId.empty()) it->stremioItemId = item.stremioItemId;
            if (!item.stremioVideoId.empty()) it->stremioVideoId = item.stremioVideoId;
            if (!item.stremioType.empty()) it->stremioType = item.stremioType;
        }
        else
        {
            Item newItem = item;
            newItem.id = id;
            newItem.lastWatched = now;
            g_items.push_back(std::move(newItem));
        }

        std::sort(g_items.begin(), g_items.end(), [](const Item& a, const Item& b) {
            return a.lastWatched > b.lastWatched;
        });

        if (g_items.size() > kMaxItems)
            g_items.resize(kMaxItems);
        g_generation++;
    }

    if (saveNow)
    {
        save();
        return;
    }
    if (!g_savePending.exchange(true))
    {
        brls::async([]() {
            g_savePending = false;  // before the snapshot: a later change re-queues
            save();
        });
    }
}

void remove(const std::string& id)
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = std::find_if(g_items.begin(), g_items.end(), [&](const Item& x) {
            return x.id == id;
        });
        if (it != g_items.end())
        {
            g_items.erase(it);
            g_generation++;
        }
    }
    save();
}

void clear()
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_items.clear();
        g_generation++;
    }
    save();
}

bool hasValidResume(const Item& item)
{
    if (item.completed)
        return false;
    if (item.positionSec < 15.0)
        return false;
    if (item.durationSec > 0.0 && item.positionSec >= item.durationSec * 0.95)
        return false;
    return true;
}

} // namespace history
