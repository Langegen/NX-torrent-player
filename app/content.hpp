#pragma once

#include <string>
#include <vector>

// What the catalog screens (home, card) show, independent of where it came
// from. TMDB fills it today; the IMDb id is the bridge to everything that keys
// on it (Stremio addons, Cinemeta).
namespace content
{

enum class Kind
{
    Movie,
    Series,
};

struct Item
{
    Kind kind = Kind::Movie;
    int tmdbId = 0;
    std::string imdbId;         // "tt1234567"; often only known after details
    std::string title;          // in the UI language when TMDB has it
    std::string originalTitle;
    std::string year;           // "2024"; "" when unknown
    std::string date;           // "2024-02-27" (release / first air date)
    std::string overview;
    std::string posterUrl;      // small, for lists
    std::string posterUrlLarge; // for the card
    std::string backdropUrl;    // full width, for the card background
    double rating = 0.0;        // 0..10, 0 = none
    std::vector<std::string> genres;  // names; details only
    std::vector<int> genreIds;        // TMDB genre ids; lists only
    int runtimeMin = 0;               // film length or a typical episode
    int seasonCount = 0;              // series only

    bool isSeries() const { return kind == Kind::Series; }
    // "movie" / "series": Stremio's names for the two.
    const char* stremioType() const { return isSeries() ? "series" : "movie"; }
    // A stable id for caches (posters): "tmdb_movie_123".
    std::string key() const
    {
        return std::string(isSeries() ? "tmdb_tv_" : "tmdb_movie_") +
               std::to_string(tmdbId);
    }
};

struct Season
{
    int number = 0;            // 0 = specials
    std::string name;
    int episodeCount = 0;
    std::string airDate;
    std::string posterUrl;
};

struct Episode
{
    int season = 0;
    int number = 0;
    std::string title;
    std::string overview;
    std::string airDate;
    std::string stillUrl;
    int runtimeMin = 0;

    // Stremio's id for it: "tt1234567:1:3". "" without the show's IMDb id.
    std::string stremioId(const std::string& showImdb) const
    {
        if (showImdb.empty()) return "";
        return showImdb + ":" + std::to_string(season) + ":" + std::to_string(number);
    }
};

// A film or a show with everything the card needs.
struct Details
{
    Item item;
    std::vector<Season> seasons;     // series only, specials last
    std::vector<std::string> cast;   // a few names, top billing first
};

} // namespace content
