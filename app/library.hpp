#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "content.hpp"

// The user's library of films and shows (roadmap phase 4), kept on the SD card
// as one JSON file (APPDATA_LIBRARY):
//   - a bookmark status: Watching / Planned / Completed / Dropped;
//   - watch progress per film and per episode -- tied to the title, not to a
//     file, so it survives picking another release;
//   - what played last (to resume in one press) and the season pack it came
//     from (to open the next episode from the same torrent).
//
// The model and the JSON are pure (tested on PC, pctest/test_library.cpp);
// the store below them is the app's.
namespace library
{

enum class Status
{
    None = 0,
    Watching,
    Planned,
    Completed,
    Dropped,
};

struct EpisodeProgress
{
    double pos = 0, dur = 0;
    bool done   = false;
    int64_t at  = 0;  // unix seconds
};

struct Entry
{
    content::Item item;  // snapshot: enough to show it and reopen its card
    Status status   = Status::None;
    int64_t added   = 0;  // first seen
    int64_t changed = 0;  // status last set

    // Film progress.
    double pos = 0, dur = 0;
    bool done = false;
    // Series progress: "season:episode" -> progress, and the last one played.
    std::map<std::string, EpisodeProgress> episodes;
    int lastSeason = 0, lastEpisode = 0;
    int64_t watchedAt = 0;  // last playback, for "Continue watching" order

    // What played last: a path / magnet / URL with its file index.
    std::string source;
    int fileIndex = -1;
    std::string sourceType;

    // The season pack the last episode came from (a Jackett result or a
    // magnet) and the seasons it holds.
    std::string packTitle, packLink, packMagnet;
    int packSeason = 0, packSeasonTo = 0;

    std::string key() const { return item.key(); }
    const EpisodeProgress* episode(int s, int e) const;
};

std::string episodeKey(int season, int episode);

// ---- pure -------------------------------------------------------------------

std::string toJson(const std::vector<Entry>& entries);
std::vector<Entry> fromJson(const std::string& json);

// What "Continue" means for an entry right now.
struct Next
{
    enum class Kind
    {
        None,     // nothing to continue (not started, finished, dropped)
        Resume,   // the film / episode in `source` is part-way through
        NextEp,   // the last episode is done: the one after it
    } kind = Kind::None;
    int season = 0, episode = 0;  // for a series
    double progress = -1;         // 0..1 of what Resume would resume
};
Next nextOf(const Entry& e);

// Merges `incoming` into `into` (an import): new titles added, for a title
// both have the newer status and progress win. Returns how many were added.
int merge(std::vector<Entry>& into, const std::vector<Entry>& incoming);

// ---- the store (UI thread; saves run in the background) ---------------------

void load();
uint32_t generation();  // bumps on every change, for screens to refresh

bool find(const std::string& key, Entry& out);
std::vector<Entry> all();
std::vector<Entry> withStatus(Status s);
// Entries with something to continue, most recently watched first.
std::vector<Entry> continueWatching();

void setStatus(const content::Item& item, Status s);

// Takes a title out of "Continue watching" (X on its card) without touching
// its bookmark or the per-episode marks: the film's position, or which
// episode was last, is forgotten.
void dismissContinue(const std::string& key);

// Called by the player while a title plays: progress, and what is playing.
// `item` is only used to create the entry if it is not there yet.
void recordProgress(const content::Item& item, int season, int episode, double pos,
                    double dur, bool finished, const std::string& source, int fileIndex,
                    const std::string& sourceType);

// The season pack an episode is being played from.
void rememberPack(const content::Item& item, const std::string& title,
                  const std::string& link, const std::string& magnet, int seasonFrom,
                  int seasonTo);

// One file holding the whole library, for a backup or another console.
bool exportTo(const std::string& path, std::string& err);
bool importFrom(const std::string& path, int& added, std::string& err);

} // namespace library
