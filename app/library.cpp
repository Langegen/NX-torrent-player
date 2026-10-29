#include "library.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

#include "json.hpp"

#ifndef LIBRARY_NO_UI
#include <borealis.hpp>
#include "appdata.hpp"
#endif

namespace library
{

std::string episodeKey(int season, int episode)
{
    return std::to_string(season) + ":" + std::to_string(episode);
}

const EpisodeProgress* Entry::episode(int s, int e) const
{
    auto it = episodes.find(episodeKey(s, e));
    return it == episodes.end() ? nullptr : &it->second;
}

// ============================================================================
// JSON
// ============================================================================

namespace
{
std::string num(double v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

std::string str(const std::string& s) { return "\"" + json::escape(s) + "\""; }

// json::escape covers quotes and backslashes; a title can also hold a newline
// or a tab, which a JSON string may not carry raw.
std::string clean(std::string s)
{
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}

const char* statusName(Status s)
{
    switch (s)
    {
        case Status::Watching:  return "watching";
        case Status::Planned:   return "planned";
        case Status::Completed: return "completed";
        case Status::Dropped:   return "dropped";
        default:                return "";
    }
}

Status statusOf(const std::string& s)
{
    if (s == "watching") return Status::Watching;
    if (s == "planned") return Status::Planned;
    if (s == "completed") return Status::Completed;
    if (s == "dropped") return Status::Dropped;
    return Status::None;
}
} // namespace

std::string toJson(const std::vector<Entry>& entries)
{
    std::string o = "{\"version\":1,\"entries\":[\n";
    for (size_t i = 0; i < entries.size(); i++)
    {
        const Entry& e = entries[i];
        const content::Item& it = e.item;
        o += "{\"item\":{";
        o += "\"kind\":" + str(it.isSeries() ? "series" : "movie");
        o += ",\"tmdbId\":" + std::to_string(it.tmdbId);
        o += ",\"imdbId\":" + str(clean(it.imdbId));
        o += ",\"title\":" + str(clean(it.title));
        o += ",\"originalTitle\":" + str(clean(it.originalTitle));
        o += ",\"year\":" + str(clean(it.year));
        o += ",\"poster\":" + str(clean(it.posterUrl));
        o += ",\"posterLarge\":" + str(clean(it.posterUrlLarge));
        o += ",\"backdrop\":" + str(clean(it.backdropUrl));
        o += ",\"rating\":" + num(it.rating);
        o += "}";
        o += ",\"status\":" + str(statusName(e.status));
        o += ",\"added\":" + std::to_string(e.added);
        o += ",\"changed\":" + std::to_string(e.changed);
        o += ",\"pos\":" + num(e.pos) + ",\"dur\":" + num(e.dur);
        o += ",\"done\":" + std::string(e.done ? "true" : "false");
        o += ",\"lastSeason\":" + std::to_string(e.lastSeason);
        o += ",\"lastEpisode\":" + std::to_string(e.lastEpisode);
        o += ",\"watchedAt\":" + std::to_string(e.watchedAt);
        o += ",\"source\":" + str(clean(e.source));
        o += ",\"fileIndex\":" + std::to_string(e.fileIndex);
        o += ",\"sourceType\":" + str(clean(e.sourceType));
        o += ",\"packTitle\":" + str(clean(e.packTitle));
        o += ",\"packLink\":" + str(clean(e.packLink));
        o += ",\"packMagnet\":" + str(clean(e.packMagnet));
        o += ",\"packSeason\":" + std::to_string(e.packSeason);
        o += ",\"packSeasonTo\":" + std::to_string(e.packSeasonTo);
        o += ",\"episodes\":[";
        bool first = true;
        for (const auto& kv : e.episodes)
        {
            size_t c = kv.first.find(':');
            if (c == std::string::npos) continue;
            if (!first) o += ",";
            first = false;
            o += "{\"s\":" + kv.first.substr(0, c) + ",\"e\":" + kv.first.substr(c + 1) +
                 ",\"pos\":" + num(kv.second.pos) + ",\"dur\":" + num(kv.second.dur) +
                 ",\"done\":" + (kv.second.done ? "true" : "false") +
                 ",\"at\":" + std::to_string(kv.second.at) + "}";
        }
        o += "]}";
        o += i + 1 < entries.size() ? ",\n" : "\n";
    }
    o += "]}\n";
    return o;
}

std::vector<Entry> fromJson(const std::string& body)
{
    std::vector<Entry> out;
    for (const auto& obj : json::objects(body, "entries"))
    {
        // The entry's own keys only: "pos" and "done" also appear in each of
        // its episodes, "title" in its item.
        std::string top = json::topLevel(obj);
        std::string itj = json::object(obj, "item");
        Entry e;
        content::Item& it = e.item;
        it.kind     = json::str(itj, "kind") == "series" ? content::Kind::Series
                                                          : content::Kind::Movie;
        it.tmdbId   = (int)json::integer(itj, "tmdbId", 0);
        it.imdbId   = json::str(itj, "imdbId");
        it.title    = json::str(itj, "title");
        it.originalTitle = json::str(itj, "originalTitle");
        it.year     = json::str(itj, "year");
        it.posterUrl      = json::str(itj, "poster");
        it.posterUrlLarge = json::str(itj, "posterLarge");
        it.backdropUrl    = json::str(itj, "backdrop");
        it.rating   = json::number(itj, "rating", 0);
        if (it.tmdbId <= 0) continue;

        e.status      = statusOf(json::str(top, "status"));
        e.added       = json::integer(top, "added", 0);
        e.changed     = json::integer(top, "changed", 0);
        e.pos         = json::number(top, "pos", 0);
        e.dur         = json::number(top, "dur", 0);
        e.done        = json::boolean(top, "done", false);
        e.lastSeason  = (int)json::integer(top, "lastSeason", 0);
        e.lastEpisode = (int)json::integer(top, "lastEpisode", 0);
        e.watchedAt   = json::integer(top, "watchedAt", 0);
        e.source      = json::str(top, "source");
        e.fileIndex   = (int)json::integer(top, "fileIndex", -1);
        e.sourceType  = json::str(top, "sourceType");
        e.packTitle   = json::str(top, "packTitle");
        e.packLink    = json::str(top, "packLink");
        e.packMagnet  = json::str(top, "packMagnet");
        e.packSeason  = (int)json::integer(top, "packSeason", 0);
        e.packSeasonTo = (int)json::integer(top, "packSeasonTo", 0);
        for (const auto& ep : json::objects(obj, "episodes"))
        {
            EpisodeProgress p;
            p.pos  = json::number(ep, "pos", 0);
            p.dur  = json::number(ep, "dur", 0);
            p.done = json::boolean(ep, "done", false);
            p.at   = json::integer(ep, "at", 0);
            e.episodes[episodeKey((int)json::integer(ep, "s", 0),
                                  (int)json::integer(ep, "e", 0))] = p;
        }
        out.push_back(std::move(e));
    }
    return out;
}

// ============================================================================
// Continue
// ============================================================================

namespace
{
// Started (15 s in) and not yet over (95% counts as over: the credits).
bool partWay(double pos, double dur, bool done)
{
    if (done || pos < 15.0) return false;
    return dur <= 0.0 || pos < dur * 0.95;
}
} // namespace

Next nextOf(const Entry& e)
{
    Next n;
    if (e.status == Status::Dropped || e.status == Status::Completed) return n;
    if (!e.item.isSeries())
    {
        if (partWay(e.pos, e.dur, e.done))
        {
            n.kind     = Next::Kind::Resume;
            n.progress = e.dur > 0 ? e.pos / e.dur : -1;
        }
        return n;
    }
    if (e.lastEpisode <= 0) return n;
    const EpisodeProgress* p = e.episode(e.lastSeason, e.lastEpisode);
    if (p && partWay(p->pos, p->dur, p->done))
    {
        n.kind     = Next::Kind::Resume;
        n.season   = e.lastSeason;
        n.episode  = e.lastEpisode;
        n.progress = p->dur > 0 ? p->pos / p->dur : -1;
        return n;
    }
    if (p && (p->done || (p->dur > 0 && p->pos >= p->dur * 0.95)))
    {
        n.kind    = Next::Kind::NextEp;
        n.season  = e.lastSeason;
        n.episode = e.lastEpisode + 1;
    }
    return n;
}

int merge(std::vector<Entry>& into, const std::vector<Entry>& incoming)
{
    int added = 0;
    for (const auto& in : incoming)
    {
        auto it = std::find_if(into.begin(), into.end(),
                               [&](const Entry& e) { return e.key() == in.key(); });
        if (it == into.end())
        {
            into.push_back(in);
            added++;
            continue;
        }
        if (in.changed > it->changed)
        {
            it->status  = in.status;
            it->changed = in.changed;
        }
        if (in.watchedAt > it->watchedAt)
        {
            Status keep = it->status;
            int64_t ch  = it->changed;
            auto eps    = it->episodes;
            *it         = in;
            it->status  = keep;
            it->changed = ch;
            for (const auto& kv : eps)
                if (!it->episodes.count(kv.first)) it->episodes[kv.first] = kv.second;
        }
        else
            for (const auto& kv : in.episodes)
            {
                auto mine = it->episodes.find(kv.first);
                if (mine == it->episodes.end() || kv.second.at > mine->second.at)
                    it->episodes[kv.first] = kv.second;
            }
    }
    return added;
}

// ============================================================================
// The store
// ============================================================================
#ifndef LIBRARY_NO_UI

namespace
{
std::mutex g_lock;
std::vector<Entry> g_entries;
uint32_t g_gen = 0;

int64_t now() { return (int64_t)std::time(nullptr); }

bool readFile(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[8192];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& data)
{
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) return false;
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// Off the UI thread: the player records every 15 s, and an SD write in the
// frame is a stutter. Snapshot under the lock, write without it.
void saveAsync()
{
    std::string data;
    {
        std::lock_guard<std::mutex> g(g_lock);
        data = toJson(g_entries);
        g_gen++;
    }
    brls::async([data]() {
        static std::mutex writeLock;  // two saves never interleave on disk
        std::lock_guard<std::mutex> g(writeLock);
        if (!writeFile(APPDATA_LIBRARY, data))
            brls::Logger::warning("[library] could not write {}", APPDATA_LIBRARY);
    });
}

Entry& ensure(const content::Item& item)
{
    for (auto& e : g_entries)
        if (e.key() == item.key())
        {
            // Keep the snapshot fresh (an IMDb id learned since, a new poster).
            if (!item.title.empty()) e.item.title = item.title;
            if (!item.imdbId.empty()) e.item.imdbId = item.imdbId;
            if (!item.posterUrl.empty()) e.item.posterUrl = item.posterUrl;
            if (!item.posterUrlLarge.empty()) e.item.posterUrlLarge = item.posterUrlLarge;
            if (!item.backdropUrl.empty()) e.item.backdropUrl = item.backdropUrl;
            return e;
        }
    Entry e;
    e.item  = item;
    e.added = now();
    g_entries.push_back(e);
    return g_entries.back();
}
} // namespace

void load()
{
    std::string body;
    bool ok = readFile(APPDATA_LIBRARY, body) ||
              readFile(std::string(APPDATA_LIBRARY) + ".tmp", body);  // a cut-off save
    std::lock_guard<std::mutex> g(g_lock);
    g_entries = ok ? fromJson(body) : std::vector<Entry>();
    brls::Logger::info("[library] {} titles", g_entries.size());
}

uint32_t generation()
{
    std::lock_guard<std::mutex> g(g_lock);
    return g_gen;
}

bool find(const std::string& key, Entry& out)
{
    std::lock_guard<std::mutex> g(g_lock);
    for (const auto& e : g_entries)
        if (e.key() == key)
        {
            out = e;
            return true;
        }
    return false;
}

std::vector<Entry> all()
{
    std::lock_guard<std::mutex> g(g_lock);
    return g_entries;
}

std::vector<Entry> withStatus(Status s)
{
    std::vector<Entry> out;
    {
        std::lock_guard<std::mutex> g(g_lock);
        for (const auto& e : g_entries)
            if (e.status == s) out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return std::max(a.changed, a.watchedAt) > std::max(b.changed, b.watchedAt);
    });
    return out;
}

std::vector<Entry> continueWatching()
{
    std::vector<Entry> out;
    {
        std::lock_guard<std::mutex> g(g_lock);
        for (const auto& e : g_entries)
            if (nextOf(e).kind != Next::Kind::None) out.push_back(e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.watchedAt > b.watchedAt; });
    return out;
}

void setStatus(const content::Item& item, Status s)
{
    {
        std::lock_guard<std::mutex> g(g_lock);
        Entry& e  = ensure(item);
        e.status  = s;
        e.changed = now();
    }
    saveAsync();
}

void dismissContinue(const std::string& key)
{
    {
        std::lock_guard<std::mutex> g(g_lock);
        for (auto& e : g_entries)
            if (e.key() == key)
            {
                e.pos = e.dur = 0;
                e.lastSeason = e.lastEpisode = 0;
            }
    }
    saveAsync();
}

void recordProgress(const content::Item& item, int season, int episode, double pos,
                    double dur, bool finished, const std::string& source, int fileIndex,
                    const std::string& sourceType)
{
    {
        std::lock_guard<std::mutex> g(g_lock);
        Entry& e    = ensure(item);
        e.watchedAt = now();
        e.source    = source;
        e.fileIndex = fileIndex;
        e.sourceType = sourceType;
        if (item.isSeries() && episode > 0)
        {
            EpisodeProgress& p = e.episodes[episodeKey(season, episode)];
            p.dur  = dur;
            p.pos  = finished ? dur : pos;
            p.done = p.done || finished;
            p.at   = e.watchedAt;
            e.lastSeason  = season;
            e.lastEpisode = episode;
        }
        else
        {
            e.pos  = finished ? dur : pos;
            e.dur  = dur;
            e.done = finished;
        }
        // Started a planned title: it is being watched now. A film played to
        // the end is watched.
        if (e.status == Status::Planned || e.status == Status::None)
        {
            if (!item.isSeries() && finished)
                e.status = Status::Completed;
            else if (e.status == Status::Planned)
                e.status = Status::Watching;
            if (e.status != Status::None) e.changed = e.watchedAt;
        }
    }
    saveAsync();
}

void rememberPack(const content::Item& item, const std::string& title,
                  const std::string& link, const std::string& magnet, int seasonFrom,
                  int seasonTo)
{
    {
        std::lock_guard<std::mutex> g(g_lock);
        Entry& e       = ensure(item);
        e.packTitle    = title;
        e.packLink     = link;
        e.packMagnet   = magnet;
        e.packSeason   = seasonFrom;
        e.packSeasonTo = seasonTo < seasonFrom ? seasonFrom : seasonTo;
    }
    saveAsync();
}

bool exportTo(const std::string& path, std::string& err)
{
    std::string data;
    {
        std::lock_guard<std::mutex> g(g_lock);
        data = toJson(g_entries);
    }
    if (!writeFile(path, data))
    {
        err = "cannot write " + path;
        return false;
    }
    return true;
}

bool importFrom(const std::string& path, int& added, std::string& err)
{
    std::string body;
    if (!readFile(path, body))
    {
        err = "cannot read " + path;
        return false;
    }
    std::vector<Entry> in = fromJson(body);
    if (in.empty() && body.find("\"entries\"") == std::string::npos)
    {
        err = "not a library file";
        return false;
    }
    {
        std::lock_guard<std::mutex> g(g_lock);
        added = merge(g_entries, in);
    }
    saveAsync();
    return true;
}

#endif  // LIBRARY_NO_UI

} // namespace library
