#include "history.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifndef HISTORY_NO_UI
#include <borealis.hpp>
#endif

#include "appdata.hpp"
#include "fsutil.hpp"
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

std::string toJson(const std::vector<Item>& items)
{
    std::string out = "{\n  \"history\": [\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        const auto& it = items[i];
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
        out += "      \"stremioType\": \"" + json::escape(it.stremioType) + "\",\n";
        out += "      \"tmdbId\": " + std::to_string(it.tmdbId) + ",\n";
        out += "      \"series\": " + std::string(it.series ? "true" : "false") + ",\n";
        out += "      \"season\": " + std::to_string(it.season) + ",\n";
        out += "      \"episode\": " + std::to_string(it.episode) + "\n";
        out += "    }" + std::string(i + 1 < items.size() ? "," : "") + "\n";
    }
    out += "  ]\n}\n";
    return out;
}

std::vector<Item> fromJson(const std::string& body)
{
    std::vector<Item> items;
    std::vector<std::string> objs = json::objects(body, "history");
    items.reserve(objs.size());

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
        it.tmdbId = (int)json::integer(obj, "tmdbId", 0);
        it.series = json::boolean(obj, "series", false);
        it.season = (int)json::integer(obj, "season", 0);
        it.episode = (int)json::integer(obj, "episode", 0);

        if (it.id.empty())
            it.id = makeId(it.source, it.fileIndex, it.stremioVideoId);

        items.push_back(std::move(it));
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.lastWatched > b.lastWatched;
    });

    if (items.size() > kMaxItems)
        items.resize(kMaxItems);

    return items;
}

int merge(std::vector<Item>& target, const std::vector<Item>& incoming)
{
    int changedCount = 0;
    for (const auto& in : incoming)
    {
        std::string inId = in.id.empty() ? makeId(in.source, in.fileIndex, in.stremioVideoId) : in.id;
        auto it = std::find_if(target.begin(), target.end(), [&](const Item& x) {
            if (x.id == inId) return true;
            if (!in.stremioVideoId.empty() && x.stremioVideoId == in.stremioVideoId) return true;
            if (!in.source.empty() && x.source == in.source && x.fileIndex == in.fileIndex) return true;
            return false;
        });

        if (it == target.end())
        {
            Item newItem = in;
            newItem.id = inId;
            target.push_back(std::move(newItem));
            changedCount++;
        }
        else
        {
            if (in.lastWatched > it->lastWatched)
            {
                *it = in;
                it->id = inId;
                changedCount++;
            }
        }
    }

    std::sort(target.begin(), target.end(), [](const Item& a, const Item& b) {
        return a.lastWatched > b.lastWatched;
    });

    if (target.size() > kMaxItems)
        target.resize(kMaxItems);

    return changedCount;
}

void load()
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_items.clear();
    g_loaded = true;

    // The saved file, or (cut short, or missing after a save cut between its
    // renames) the .tmp or the version before -- see fsutil::writeAtomic.
    std::string body;
    if (!fsutil::readFirst(APPDATA_HISTORY, body, [](const std::string& b) {
            return fsutil::looksComplete(b);
        }))
        return;

    g_items = fromJson(body);
}

void save()
{
    std::lock_guard<std::mutex> saveGuard(g_saveLock);
    std::vector<Item> copy;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        copy = g_items;
    }

    std::string out = toJson(copy);

    // Checked and atomic: an unchecked write on a full card used to replace
    // the good file with a truncated one.
#ifndef HISTORY_NO_UI
    if (!fsutil::writeAtomic(APPDATA_HISTORY, out))
        brls::Logger::error("[history] could not write {}", APPDATA_HISTORY);
#endif
}

std::string getJson()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return toJson(g_items);
}

int mergeJson(const std::string& json)
{
    std::vector<Item> incoming = fromJson(json);
    if (incoming.empty() && json.find("\"history\"") == std::string::npos)
        return 0;

    int changed = 0;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        changed = merge(g_items, incoming);
        if (changed > 0)
            g_generation++;
    }
    if (changed > 0)
        save();
    return changed;
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
#ifndef HISTORY_NO_UI
    if (!g_savePending.exchange(true))
    {
        brls::async([]() {
            g_savePending = false;  // before the snapshot: a later change re-queues
            save();
        });
    }
#else
    save();
#endif
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
