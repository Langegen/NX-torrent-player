// Offline tests for the library model (app/library.cpp built with
// LIBRARY_NO_UI): the JSON round trip, what "Continue" offers, and the merge
// an import does.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "library.hpp"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

using library::Entry;
using library::Next;
using library::Status;

static Entry film(int id, const std::string& title)
{
    Entry e;
    e.item.kind   = content::Kind::Movie;
    e.item.tmdbId = id;
    e.item.title  = title;
    return e;
}

static Entry show(int id, const std::string& title)
{
    Entry e = film(id, title);
    e.item.kind = content::Kind::Series;
    return e;
}

static void test_roundtrip()
{
    std::printf("[1/4] JSON round trip, with awkward titles...\n");
    Entry a = film(693134, "Дюна: Часть \"вторая\"\tx");
    a.item.imdbId    = "tt15239678";
    a.item.year      = "2024";
    a.item.posterUrl = "https://image.tmdb.org/t/p/w185/p.jpg";
    a.item.rating    = 8.1;
    a.status    = Status::Planned;
    a.changed   = 1700000000;
    a.pos       = 1234.5;
    a.dur       = 9960;
    a.source    = "sdmc:/switch/NX-torrent-player/jackett/0123.torrent";
    a.fileIndex = 2;
    a.sourceType = "Jackett";

    Entry b = show(220102, "Слово пацана");
    b.status      = Status::Watching;
    b.lastSeason  = 1;
    b.lastEpisode = 5;
    b.episodes[library::episodeKey(1, 4)] = { 3100, 3100, true, 1700000100 };
    b.episodes[library::episodeKey(1, 5)] = { 900, 3200, false, 1700000200 };
    b.packTitle    = "Slovo.patsana.S01.1080p";
    b.packMagnet   = "magnet:?xt=urn:btih:abc";
    b.packSeason   = 1;
    b.packSeasonTo = 1;

    auto back = library::fromJson(library::toJson({ a, b }));
    CHECK(back.size() == 2);
    const Entry& x = back[0];
    CHECK(x.item.title == "Дюна: Часть \"вторая\" x");  // the tab became a space
    CHECK(x.item.imdbId == "tt15239678" && x.item.year == "2024");
    CHECK(x.item.posterUrl == a.item.posterUrl);
    CHECK(x.item.rating > 8.0 && x.item.rating < 8.2);
    CHECK(!x.item.isSeries());
    CHECK(x.status == Status::Planned && x.changed == 1700000000);
    CHECK(x.pos > 1234 && x.pos < 1235 && x.dur > 9959);
    CHECK(x.source == a.source && x.fileIndex == 2 && x.sourceType == "Jackett");
    CHECK(!x.done);  // not confused by the episodes' "done"

    const Entry& y = back[1];
    CHECK(y.item.isSeries() && y.status == Status::Watching);
    CHECK(y.lastSeason == 1 && y.lastEpisode == 5);
    CHECK(y.episodes.size() == 2);
    CHECK(y.episode(1, 4) && y.episode(1, 4)->done);
    CHECK(y.episode(1, 5) && !y.episode(1, 5)->done && y.episode(1, 5)->pos > 899);
    CHECK(y.packMagnet == b.packMagnet && y.packSeason == 1);
    CHECK(library::fromJson("not json").empty());
    std::printf("      PASSED\n");
}

static void test_next()
{
    std::printf("[2/4] What Continue offers...\n");
    Entry f = film(1, "F");
    CHECK(library::nextOf(f).kind == Next::Kind::None);  // never played
    f.pos = 600;
    f.dur = 6000;
    Next n = library::nextOf(f);
    CHECK(n.kind == Next::Kind::Resume && n.progress > 0.09 && n.progress < 0.11);
    f.pos = 5800;  // in the credits
    CHECK(library::nextOf(f).kind == Next::Kind::None);
    f.pos = 600;
    f.status = Status::Dropped;
    CHECK(library::nextOf(f).kind == Next::Kind::None);

    Entry s = show(2, "S");
    s.lastSeason  = 2;
    s.lastEpisode = 3;
    s.episodes[library::episodeKey(2, 3)] = { 1200, 2400, false, 1 };
    n = library::nextOf(s);
    CHECK(n.kind == Next::Kind::Resume && n.season == 2 && n.episode == 3);
    s.episodes[library::episodeKey(2, 3)] = { 2400, 2400, true, 2 };
    n = library::nextOf(s);
    CHECK(n.kind == Next::Kind::NextEp && n.season == 2 && n.episode == 4);
    s.status = Status::Completed;
    CHECK(library::nextOf(s).kind == Next::Kind::None);
    std::printf("      PASSED\n");
}

static void test_merge()
{
    std::printf("[3/4] Import merge: new titles added, newer state wins...\n");
    Entry mine = show(10, "Mine");
    mine.status    = Status::Watching;
    mine.changed   = 100;
    mine.watchedAt = 500;
    mine.episodes[library::episodeKey(1, 1)] = { 10, 20, true, 500 };

    Entry theirs = show(10, "Mine");
    theirs.status    = Status::Completed;
    theirs.changed   = 200;  // newer status
    theirs.watchedAt = 400;  // older progress
    theirs.episodes[library::episodeKey(1, 2)] = { 5, 20, false, 400 };

    std::vector<Entry> into = { mine };
    int added = library::merge(into, { theirs, film(11, "New") });
    CHECK(added == 1);
    CHECK(into.size() == 2);
    CHECK(into[0].status == Status::Completed);           // newer status
    CHECK(into[0].watchedAt == 500);                      // my newer progress kept
    CHECK(into[0].episodes.size() == 2);                  // episodes from both
    CHECK(into[1].item.title == "New");
    std::printf("      PASSED\n");
}

static void test_keys()
{
    std::printf("[4/4] Keys...\n");
    CHECK(film(5, "x").key() == "tmdb_movie_5");
    CHECK(show(5, "x").key() == "tmdb_tv_5");
    CHECK(library::episodeKey(1, 12) == "1:12");
    std::printf("      PASSED\n");
}

int main()
{
    std::printf("=========================================\n");
    std::printf(" NX TORRENT PLAYER: LIBRARY              \n");
    std::printf("=========================================\n\n");
    test_roundtrip();
    test_next();
    test_merge();
    test_keys();
    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
