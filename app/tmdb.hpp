#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "content.hpp"

// The TMDB (themoviedb.org) catalog: what the home screen's rows and the
// film/show card are built from, in the UI's language.
//
// TMDB needs a key. It comes from Options -> Network, or failing that from the
// build (cmake -DTMDB_API_KEY=...). Both kinds work: a v3 "API key" (32 hex,
// sent as ?api_key=) and a v4 "read access token" (a long JWT, sent as a
// Bearer header). TMDB is blocked in some countries; the API address can point
// at a mirror, and the app's proxy applies.
//
// Every answer is kept on the SD card (APPDATA_TMDB). A fresh copy is used
// without asking the network; when the network fails, any copy at all is --
// so the home screen opens instantly and works offline.
namespace tmdb
{

struct Settings
{
    std::string apiKey;
    std::string apiBase = "https://api.themoviedb.org/3";
    std::string imageBase = "https://image.tmdb.org/t/p";
    std::string language = "en-US";
};

// ---- pure: no network, no UI (tested on PC) ---------------------------------

// The TMDB language for the app's UI language: "ru" -> "ru-RU".
std::string languageFor(const std::string& uiLang);

// A v4 read access token (Bearer) rather than a v3 key.
bool isBearerToken(const std::string& key);

// {apiBase}{path}?{params}&language=..[&api_key=..]. Values are URL-encoded.
std::string requestUrl(const Settings& s, const std::string& path,
                       const std::vector<std::pair<std::string, std::string>>& params = {});

// A cache file name for a request: a hash of the URL without its api_key, so
// changing the key does not throw the cache away. 16 hex digits.
std::string cacheKey(const std::string& url);

// "https://image.tmdb.org/t/p" + "w185" + "/abc.jpg"; "" when there is no path.
std::string imageUrl(const std::string& imageBase, const char* size,
                     const std::string& path);

// TMDB's own error text from an error body ("Invalid API key: ..."), or "".
std::string errorMessage(const std::string& body);

// Items of a list answer ("results": trending, popular, search...). kind -1
// reads each item's media_type (people are skipped); 0 = all films, 1 = all
// series.
std::vector<content::Item> parseList(const std::string& json,
                                     const std::string& imageBase, int kind);

// /movie/{id} or /tv/{id} with append_to_response=external_ids,credits,
// recommendations (the card's "More like this" row).
bool parseDetails(const std::string& json, content::Kind kind,
                  const std::string& imageBase, content::Details& out);

// /tv/{id}/season/{n}.
std::vector<content::Episode> parseSeason(const std::string& json,
                                          const std::string& imageBase);

// A list request that can be asked for page after page: a collection's
// screen, the search's results. kind as parseList takes it.
struct ListQuery
{
    std::string path;
    std::vector<std::pair<std::string, std::string>> params;
    int kind = -1;
};

// Discover: the catalog filtered and sorted.
struct Discover
{
    content::Kind kind = content::Kind::Movie;
    int genre    = 0;  // TMDB genre id, 0 = any
    int yearFrom = 0;  // 0 = no bound
    int yearTo   = 0;
    enum class Sort
    {
        Popular,
        Rating,
        Newest,
    } sort = Sort::Popular;
};
ListQuery discoverQuery(const Discover& d);

// A text search. kind -1 = films and shows (TMDB's multi search, which takes
// no year); 0 / 1 = one kind, optionally of one year.
ListQuery searchQuery(const std::string& text, int kind = -1, int year = 0);

// TMDB's genres for films / shows: id and English name (translate with tr()).
const std::vector<std::pair<int, const char*>>& genres(content::Kind kind);

// ---- the app side -----------------------------------------------------------

// From config (and the build's default key).
Settings settings();
bool configured();  // there is a key to use

// The home screen's rows.
enum class Row
{
    Trending,       // films and series, this week
    PopularMovies,
    PopularSeries,
    TopMovies,
    TopSeries,
    NowPlaying,     // in cinemas
};
std::string rowPath(Row row);
int rowKind(Row row);  // as parseList takes it

struct ListResult
{
    bool ok = false;
    std::vector<content::Item> items;
    std::string error;
    int page       = 1;
    int totalPages = 1;
};

// The query behind one of the home rows (so its "See all" page can page on).
ListQuery rowQuery(Row row);
ListQuery pathQuery(const std::string& path, content::Kind kind);
ListQuery genreQuery(content::Kind kind, int genreId);

// One page (1-based) of a query.
void fetchQueryAsync(const ListQuery& q, int page, std::function<void(ListResult)> done);
struct DetailsResult
{
    bool ok = false;
    content::Details details;
    std::string error;
};
struct SeasonResult
{
    bool ok = false;
    std::vector<content::Episode> episodes;
    std::string error;
};

// All run on a worker thread and call back on the UI thread.
void fetchRowAsync(Row row, std::function<void(ListResult)> done);
// Any list endpoint of one kind ("/movie/upcoming", "/tv/on_the_air").
void fetchPathAsync(const std::string& path,
                    const std::vector<std::pair<std::string, std::string>>& params,
                    content::Kind kind, std::function<void(ListResult)> done);
// The most popular titles of a TMDB genre id (28 action, 18 drama...).
void fetchGenreAsync(content::Kind kind, int genreId, std::function<void(ListResult)> done);
void searchAsync(const std::string& query, std::function<void(ListResult)> done);
void fetchDetailsAsync(content::Kind kind, int id,
                       std::function<void(DetailsResult)> done);
void fetchSeasonAsync(int tvId, int season, std::function<void(SeasonResult)> done);

// Size of the response cache on the SD card, and emptying it.
int64_t cacheBytes();
void clearCache();

} // namespace tmdb
