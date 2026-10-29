#include "tmdb.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "http.hpp"
#include "json.hpp"

#ifndef TMDB_NO_UI  // the PC tests build the pure half alone
#include <dirent.h>
#include <sys/stat.h>

#include <chrono>
#include <ctime>
#include <thread>

#include <borealis.hpp>

#include "appdata.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include "workers.hpp"
#endif

#ifndef TMDB_API_KEY
#define TMDB_API_KEY ""
#endif

namespace tmdb
{

// ============================================================================
// Pure
// ============================================================================

std::string languageFor(const std::string& uiLang)
{
    if (uiLang == "ru") return "ru-RU";
    if (uiLang == "fr") return "fr-FR";
    return "en-US";
}

bool isBearerToken(const std::string& key)
{
    // v3 keys are 32 hex digits; v4 tokens are JWTs ("eyJ...", ~200 chars).
    return key.size() > 40 || key.rfind("eyJ", 0) == 0;
}

std::string requestUrl(const Settings& s, const std::string& path,
                       const std::vector<std::pair<std::string, std::string>>& params)
{
    std::string url = s.apiBase;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += path;
    char sep = '?';
    for (const auto& p : params)
    {
        url += sep + p.first + "=" + http::urlEncode(p.second);
        sep = '&';
    }
    url += sep + std::string("language=") + s.language;
    if (!s.apiKey.empty() && !isBearerToken(s.apiKey))
        url += "&api_key=" + http::urlEncode(s.apiKey);
    return url;
}

std::string cacheKey(const std::string& url)
{
    std::string u = url;
    size_t k = u.find("api_key=");
    if (k != std::string::npos)
    {
        size_t end = u.find('&', k);
        // Drop the parameter and the separator in front of it.
        size_t from = (k > 0 && (u[k - 1] == '&' || u[k - 1] == '?')) ? k - 1 : k;
        u.erase(from, end == std::string::npos ? std::string::npos : end - from);
    }
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char c : u)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", (unsigned long long)h);
    return out;
}

std::string imageUrl(const std::string& imageBase, const char* size,
                     const std::string& path)
{
    if (path.empty() || path == "null") return "";
    std::string base = imageBase;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/" + size + (path[0] == '/' ? "" : "/") + path;
}

std::string errorMessage(const std::string& body)
{
    std::string top = json::topLevel(body);
    if (json::boolean(top, "success", true)) return "";
    std::string m = json::str(top, "status_message");
    return m.empty() ? "TMDB error" : m;
}

namespace
{
std::string yearOf(const std::string& date)
{
    return date.size() >= 4 && std::isdigit((unsigned char)date[0]) ? date.substr(0, 4)
                                                                     : "";
}

// The fields every film/series object shares, read off its top level.
void readCommon(const std::string& top, content::Kind kind,
                const std::string& imageBase, content::Item& it)
{
    it.kind   = kind;
    it.tmdbId = (int)json::integer(top, "id", 0);
    bool tv   = kind == content::Kind::Series;
    it.title         = json::str(top, tv ? "name" : "title");
    it.originalTitle = json::str(top, tv ? "original_name" : "original_title");
    if (it.title.empty()) it.title = it.originalTitle;
    it.date     = json::str(top, tv ? "first_air_date" : "release_date");
    it.year     = yearOf(it.date);
    it.overview = json::str(top, "overview");
    std::string poster = json::str(top, "poster_path");
    it.posterUrl      = imageUrl(imageBase, "w185", poster);
    it.posterUrlLarge = imageUrl(imageBase, "w342", poster);
    it.backdropUrl    = imageUrl(imageBase, "w1280", json::str(top, "backdrop_path"));
    it.rating         = json::number(top, "vote_average", 0.0);
    it.originalLanguage = json::str(top, "original_language");
}
} // namespace

std::vector<content::Item> parseList(const std::string& body,
                                     const std::string& imageBase, int kind)
{
    std::vector<content::Item> out;
    for (const auto& obj : json::objects(body, "results"))
    {
        std::string top = json::topLevel(obj);
        content::Kind k;
        if (kind == 0)
            k = content::Kind::Movie;
        else if (kind == 1)
            k = content::Kind::Series;
        else
        {
            std::string mt = json::str(top, "media_type");
            if (mt == "movie") k = content::Kind::Movie;
            else if (mt == "tv") k = content::Kind::Series;
            else continue;  // a person, or something new
        }
        content::Item it;
        readCommon(top, k, imageBase, it);
        for (long g : json::integers(obj, "genre_ids")) it.genreIds.push_back((int)g);
        if (it.tmdbId <= 0 || it.title.empty()) continue;
        out.push_back(std::move(it));
    }
    return out;
}

bool parseDetails(const std::string& body, content::Kind kind,
                  const std::string& imageBase, content::Details& out)
{
    std::string top = json::topLevel(body);
    out = content::Details();
    readCommon(top, kind, imageBase, out.item);
    if (out.item.tmdbId <= 0) return false;

    // A film carries imdb_id itself; a series only inside external_ids.
    out.item.imdbId = json::str(top, "imdb_id");
    if (out.item.imdbId.empty())
        out.item.imdbId = json::str(json::object(body, "external_ids"), "imdb_id");

    for (const auto& g : json::objects(body, "genres"))
    {
        std::string n = json::str(g, "name");
        if (!n.empty()) out.item.genres.push_back(n);
    }

    if (kind == content::Kind::Movie)
        out.item.runtimeMin = (int)json::integer(top, "runtime", 0);
    else
    {
        out.item.seasonCount = (int)json::integer(top, "number_of_seasons", 0);
        // "episode_run_time": [45] -- the first figure, when TMDB has one.
        size_t k = body.find("\"episode_run_time\"");
        if (k != std::string::npos)
        {
            size_t open = body.find('[', k);
            if (open != std::string::npos)
                out.item.runtimeMin = std::atoi(body.c_str() + open + 1);
        }
        std::vector<content::Season> specials;
        for (const auto& s : json::objects(body, "seasons"))
        {
            content::Season se;
            se.number       = (int)json::integer(s, "season_number", -1);
            se.name         = json::str(s, "name");
            se.episodeCount = (int)json::integer(s, "episode_count", 0);
            se.airDate      = json::str(s, "air_date");
            se.posterUrl    = imageUrl(imageBase, "w185", json::str(s, "poster_path"));
            if (se.number < 0 || se.episodeCount <= 0) continue;
            // Specials (season 0) go last: nobody opens a show to watch those.
            (se.number == 0 ? specials : out.seasons).push_back(se);
        }
        for (auto& s : specials) out.seasons.push_back(s);
    }

    std::string credits = json::object(body, "credits");
    for (const auto& c : json::objects(credits, "cast"))
    {
        std::string n = json::str(c, "name");
        if (!n.empty()) out.cast.push_back(n);
        if (out.cast.size() >= 6) break;
    }

    // "recommendations": a list answer of its own, nested. Its titles are of
    // the same kind (TMDB recommends films for a film, shows for a show).
    std::string rec = json::object(body, "recommendations");
    if (!rec.empty())
    {
        out.similar = parseList(rec, imageBase, kind == content::Kind::Series ? 1 : 0);
        if (out.similar.size() > 20) out.similar.resize(20);
    }
    return true;
}

std::vector<content::Episode> parseSeason(const std::string& body,
                                          const std::string& imageBase)
{
    std::vector<content::Episode> out;
    for (const auto& obj : json::objects(body, "episodes"))
    {
        // An episode object nests "crew" and "guest_stars", each with "name"s.
        std::string top = json::topLevel(obj);
        content::Episode e;
        e.season     = (int)json::integer(top, "season_number", 0);
        e.number     = (int)json::integer(top, "episode_number", 0);
        e.title      = json::str(top, "name");
        e.overview   = json::str(top, "overview");
        e.airDate    = json::str(top, "air_date");
        e.stillUrl   = imageUrl(imageBase, "w300", json::str(top, "still_path"));
        e.runtimeMin = (int)json::integer(top, "runtime", 0);
        if (e.number <= 0) continue;
        out.push_back(std::move(e));
    }
    return out;
}

ListQuery discoverQuery(const Discover& d)
{
    bool tv = d.kind == content::Kind::Series;
    ListQuery q;
    q.path = tv ? "/discover/tv" : "/discover/movie";
    q.kind = tv ? 1 : 0;
    const char* date = tv ? "first_air_date" : "primary_release_date";
    if (d.anime)
    {
        // A comma is AND in with_genres: animation and the genre asked for.
        std::string g = std::to_string(kAnimationGenre);
        if (d.genre > 0 && d.genre != kAnimationGenre) g += "," + std::to_string(d.genre);
        q.params.push_back({ "with_genres", g });
        q.params.push_back({ "with_original_language", "ja" });
    }
    else if (d.genre > 0)
        q.params.push_back({ "with_genres", std::to_string(d.genre) });
    if (d.yearFrom > 0)
        q.params.push_back({ std::string(date) + ".gte", std::to_string(d.yearFrom) + "-01-01" });
    if (d.yearTo > 0)
        q.params.push_back({ std::string(date) + ".lte", std::to_string(d.yearTo) + "-12-31" });
    switch (d.sort)
    {
        case Discover::Sort::Rating:
            q.params.push_back({ "sort_by", "vote_average.desc" });
            // Rated by enough people that the top is not a page of unknowns.
            q.params.push_back({ "vote_count.gte", "300" });
            break;
        case Discover::Sort::Newest:
            q.params.push_back({ "sort_by", std::string(date) + ".desc" });
            q.params.push_back({ "vote_count.gte", "20" });
            break;
        default:
            q.params.push_back({ "sort_by", "popularity.desc" });
            break;
    }
    return q;
}

bool isAnime(const content::Item& it)
{
    if (it.originalLanguage != "ja") return false;
    for (int g : it.genreIds)
        if (g == kAnimationGenre) return true;
    for (const auto& g : it.genres)  // details carry names, not ids
        if (g == "Animation" || g == "\xD0\xBC\xD1\x83\xD0\xBB\xD1\x8C\xD1\x82\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C\xD0\xBC")
            return true;  // "мультфильм"
    return false;
}

ListQuery searchQuery(const std::string& text, int kind, int year)
{
    ListQuery q;
    q.kind = kind;
    if (kind < 0)
    {
        q.path   = "/search/multi";
        q.params = { { "query", text }, { "include_adult", "false" } };
        return q;
    }
    bool tv  = kind == 1;
    q.path   = tv ? "/search/tv" : "/search/movie";
    q.params = { { "query", text }, { "include_adult", "false" } };
    if (year > 0)
        q.params.push_back({ tv ? "first_air_date_year" : "primary_release_year",
                             std::to_string(year) });
    return q;
}

const std::vector<std::pair<int, const char*>>& genres(content::Kind kind)
{
    static const std::vector<std::pair<int, const char*>> movie = {
        { 28, "Action" },      { 12, "Adventure" },  { 16, "Animation" },
        { 35, "Comedy" },      { 80, "Crime" },      { 99, "Documentary" },
        { 18, "Drama" },       { 10751, "Family" },  { 14, "Fantasy" },
        { 36, "Historical" },     { 27, "Horror" },     { 10402, "Music" },
        { 9648, "Mystery" },   { 10749, "Romance" }, { 878, "Science fiction" },
        { 53, "Thriller" },    { 10752, "War" },     { 37, "Western" },
    };
    static const std::vector<std::pair<int, const char*>> tv = {
        { 10759, "Action & Adventure" }, { 16, "Animation" },  { 35, "Comedy" },
        { 80, "Crime" },                 { 99, "Documentary" }, { 18, "Drama" },
        { 10751, "Family" },             { 10762, "Kids" },     { 9648, "Mystery" },
        { 10764, "Reality" },            { 10765, "Sci-Fi & Fantasy" },
        { 10768, "War & Politics" },     { 37, "Western" },
    };
    return kind == content::Kind::Series ? tv : movie;
}

// ============================================================================
// The app side
// ============================================================================
#ifndef TMDB_NO_UI

Settings settings()
{
    const config::Config& c = config::get();
    Settings s;
    s.apiKey = c.tmdbApiKey.empty() ? std::string(TMDB_API_KEY) : c.tmdbApiKey;
    if (!c.tmdbApiUrl.empty()) s.apiBase = c.tmdbApiUrl;
    if (!c.tmdbImageUrl.empty()) s.imageBase = c.tmdbImageUrl;
    s.language = languageFor(i18n::lang());
    return s;
}

bool configured() { return !settings().apiKey.empty(); }

std::string rowPath(Row row)
{
    switch (row)
    {
        case Row::Trending:      return "/trending/all/week";
        case Row::PopularMovies: return "/movie/popular";
        case Row::PopularSeries: return "/tv/popular";
        case Row::TopMovies:     return "/movie/top_rated";
        case Row::TopSeries:     return "/tv/top_rated";
        case Row::NowPlaying:    return "/movie/now_playing";
    }
    return "/trending/all/week";
}

int rowKind(Row row)
{
    switch (row)
    {
        case Row::Trending: return -1;
        case Row::PopularSeries:
        case Row::TopSeries: return 1;
        default: return 0;
    }
}

namespace
{
std::string cachePath(const std::string& url)
{
    return std::string(APPDATA_TMDB) + "/" + cacheKey(url) + ".json";
}

bool readFile(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return !out.empty();
}

// Blocking: the answer for `url`, from the SD card when the copy there is
// younger than `ttlSec`, else from TMDB (and stored). A network failure falls
// back to a copy of any age, so the screen still has something to show.
bool fetchCached(const Settings& s, const std::string& url, long ttlSec,
                 std::string& body, std::string& err)
{
    std::string path = cachePath(url);
    struct stat st;
    bool have = stat(path.c_str(), &st) == 0;
    if (have && std::time(nullptr) - st.st_mtime < ttlSec && readFile(path, body))
        return true;

    std::string auth;
    if (isBearerToken(s.apiKey)) auth = "Authorization: Bearer " + s.apiKey;
    std::string resp;
    bool ok = http::get(url, resp, err, auth.empty() ? nullptr : auth.c_str(), 20);
    if (!ok)
    {
        // One more try: a flaky connection (Wi-Fi waking, a VPN or proxy
        // settling) fails the odd request that the next one gets through.
        // This runs on a worker thread, so the pause blocks nothing.
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        resp.clear();
        ok = http::get(url, resp, err, auth.empty() ? nullptr : auth.c_str(), 20);
    }
    if (ok)
    {
        std::string tmdbErr = errorMessage(resp);
        if (!tmdbErr.empty())
        {
            err = "TMDB: " + tmdbErr;
            ok  = false;
        }
    }
    if (ok)
    {
        mkdir(APPDATA_TMDB, 0777);
        // One per thread: two workers can fetch the same URL at once.
        std::string tmp = path + "." +
                          std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()) %
                                         100000) +
                          ".tmp";
        if (FILE* f = std::fopen(tmp.c_str(), "wb"))
        {
            std::fwrite(resp.data(), 1, resp.size(), f);
            std::fclose(f);
            std::remove(path.c_str());
            std::rename(tmp.c_str(), path.c_str());
        }
        body = std::move(resp);
        return true;
    }
    if (have && readFile(path, body))
    {
        brls::Logger::info("[tmdb] offline, using a cached answer ({})", err);
        return true;
    }
    return false;
}

constexpr long kListTtl    = 6 * 3600;       // rows change daily at most
constexpr long kDetailsTtl = 3 * 24 * 3600;  // a new season shows up eventually
constexpr long kSearchTtl  = 3600;

void fetchList(const std::string& path,
               std::vector<std::pair<std::string, std::string>> params, int kind,
               long ttl, std::function<void(ListResult)> done)
{
    Settings s = settings();
    if (s.apiKey.empty())
    {
        ListResult r;
        r.error = "no TMDB key";
        done(r);
        return;
    }
    std::string url = requestUrl(s, path, params);
    workers::run([s, url, kind, ttl, done]() {
        ListResult r;
        std::string body;
        if (fetchCached(s, url, ttl, body, r.error))
        {
            r.items      = parseList(body, s.imageBase, kind);
            r.ok         = true;
            r.page       = (int)json::integer(body, "page", 1);
            r.totalPages = (int)json::integer(body, "total_pages", 1);
            // TMDB serves at most 500 pages, whatever it says the total is.
            if (r.totalPages > 500) r.totalPages = 500;
        }
        brls::sync([done, r]() { done(r); });
    });
}
} // namespace

ListQuery rowQuery(Row row)
{
    ListQuery q;
    q.path = rowPath(row);
    q.kind = rowKind(row);
    return q;
}

ListQuery pathQuery(const std::string& path, content::Kind kind)
{
    ListQuery q;
    q.path = path;
    q.kind = kind == content::Kind::Series ? 1 : 0;
    return q;
}

ListQuery genreQuery(content::Kind kind, int genreId)
{
    Discover d;
    d.kind  = kind;
    d.genre = genreId;
    ListQuery q = discoverQuery(d);
    // Enough votes that the row is not a page of unknowns.
    q.params.push_back({ "vote_count.gte", "150" });
    return q;
}

void fetchQueryAsync(const ListQuery& q, int page, std::function<void(ListResult)> done)
{
    auto params = q.params;
    if (page > 1) params.push_back({ "page", std::to_string(page) });
    bool search = q.path.rfind("/search/", 0) == 0;
    fetchList(q.path, params, q.kind, search ? kSearchTtl : kListTtl, std::move(done));
}

void fetchRowAsync(Row row, std::function<void(ListResult)> done)
{
    fetchList(rowPath(row), {}, rowKind(row), kListTtl, std::move(done));
}

void fetchPathAsync(const std::string& path,
                    const std::vector<std::pair<std::string, std::string>>& params,
                    content::Kind kind, std::function<void(ListResult)> done)
{
    fetchList(path, params, kind == content::Kind::Series ? 1 : 0, kListTtl, std::move(done));
}

void fetchGenreAsync(content::Kind kind, int genreId, std::function<void(ListResult)> done)
{
    bool tv = kind == content::Kind::Series;
    fetchList(tv ? "/discover/tv" : "/discover/movie",
              { { "with_genres", std::to_string(genreId) },
                { "sort_by", "popularity.desc" },
                // Enough votes that the row is not a page of unknowns.
                { "vote_count.gte", "150" } },
              tv ? 1 : 0, kListTtl, std::move(done));
}

void searchAsync(const std::string& query, std::function<void(ListResult)> done)
{
    fetchList("/search/multi", { { "query", query }, { "include_adult", "false" } },
              -1, kSearchTtl, std::move(done));
}

void fetchDetailsAsync(content::Kind kind, int id,
                       std::function<void(DetailsResult)> done)
{
    Settings s = settings();
    std::string path = std::string(kind == content::Kind::Series ? "/tv/" : "/movie/") +
                       std::to_string(id);
    std::string url = requestUrl(s, path, { { "append_to_response", "external_ids,credits,recommendations" } });
    workers::run([s, url, kind, done]() {
        DetailsResult r;
        std::string body;
        if (s.apiKey.empty())
            r.error = "no TMDB key";
        else if (fetchCached(s, url, kDetailsTtl, body, r.error))
        {
            r.ok = parseDetails(body, kind, s.imageBase, r.details);
            if (!r.ok) r.error = "unexpected answer from TMDB";
        }
        brls::sync([done, r]() { done(r); });
    });
}

void fetchSeasonAsync(int tvId, int season, std::function<void(SeasonResult)> done)
{
    Settings s = settings();
    std::string url = requestUrl(s, "/tv/" + std::to_string(tvId) + "/season/" +
                                        std::to_string(season));
    workers::run([s, url, done]() {
        SeasonResult r;
        std::string body;
        if (s.apiKey.empty())
            r.error = "no TMDB key";
        else if (fetchCached(s, url, kDetailsTtl, body, r.error))
        {
            r.episodes = parseSeason(body, s.imageBase);
            r.ok       = true;
        }
        brls::sync([done, r]() { done(r); });
    });
}

int64_t cacheBytes()
{
    int64_t total = 0;
    if (DIR* d = opendir(APPDATA_TMDB))
    {
        struct dirent* e;
        while ((e = readdir(d)))
        {
            std::string p = std::string(APPDATA_TMDB) + "/" + e->d_name;
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) total += st.st_size;
        }
        closedir(d);
    }
    return total;
}

void clearCache()
{
    if (DIR* d = opendir(APPDATA_TMDB))
    {
        struct dirent* e;
        while ((e = readdir(d)))
        {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::remove((std::string(APPDATA_TMDB) + "/" + name).c_str());
        }
        closedir(d);
    }
}

#endif  // TMDB_NO_UI

} // namespace tmdb
