// Offline tests for the TMDB client's pure half (app/tmdb.cpp built with
// TMDB_NO_UI) and the JSON helpers it relies on. The fixtures are trimmed
// copies of real TMDB answers, keeping the parts that trip a naive reader:
// "name" inside created_by / genres / last_episode_to_air before the show's
// own, people in a multi search, a series' specials season.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "http.hpp"
#include "json.hpp"
#include "tmdb.hpp"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace http
{
bool get(const std::string&, std::string&, std::string& err, const char*, long)
{
    err = "no network in tests";
    return false;
}
std::string urlEncode(const std::string& s)  // same as app/http.cpp
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}
} // namespace http

static const char* IMG = "https://image.tmdb.org/t/p";

static const char* kTrending = R"J({"page":1,"results":[
 {"backdrop_path":"/bd1.jpg","id":693134,"title":"Дюна: Часть вторая","original_title":"Dune: Part Two",
  "overview":"Пол Атрейдес объединяется с фрименами.","poster_path":"/p1.jpg","media_type":"movie",
  "genre_ids":[878,12],"release_date":"2024-02-27","vote_average":8.143},
 {"id":17419,"name":"Kevin Bacon","media_type":"person","known_for":[{"id":1,"title":"x"}]},
 {"backdrop_path":null,"id":106379,"name":"Фоллаут","original_name":"Fallout","overview":"",
  "poster_path":"/p2.jpg","media_type":"tv","first_air_date":"2024-04-10","vote_average":8.3,
  "origin_country":["US"]}
],"total_pages":500})J";

static const char* kTv = R"J({"adult":false,"backdrop_path":"/bd.jpg",
 "created_by":[{"id":1,"name":"Жора Крыжовников","profile_path":null}],
 "episode_run_time":[52],"first_air_date":"2023-11-09",
 "genres":[{"id":18,"name":"драма"},{"id":80,"name":"криминал"}],
 "id":220102,
 "last_episode_to_air":{"id":9,"name":"Асфальт","episode_number":8},
 "name":"Слово пацана. Кровь на асфальте","number_of_seasons":1,
 "original_name":"Слово пацана. Кровь на асфальте","overview":"Казань, конец 1980-х.",
 "poster_path":"/poster.jpg",
 "seasons":[{"air_date":null,"episode_count":2,"id":5,"name":"Спецматериалы","poster_path":null,"season_number":0},
            {"air_date":"2023-11-09","episode_count":8,"id":6,"name":"Сезон 1","poster_path":"/s1.jpg","season_number":1}],
 "vote_average":8.4,
 "credits":{"cast":[{"name":"Леон Кемстач","character":"Андрей"},{"name":"Рузиль Минекаев","character":"Марат"}],"crew":[]},
 "external_ids":{"imdb_id":"tt29632826","tvdb_id":null}})J";

static const char* kMovie = R"J({"backdrop_path":"/bd.jpg","genres":[{"id":878,"name":"фантастика"}],
 "id":693134,"imdb_id":"tt15239678","original_title":"Dune: Part Two","overview":"…",
 "poster_path":"/p.jpg","release_date":"2024-02-27","runtime":166,"title":"Дюна: Часть вторая",
 "vote_average":8.1,"belongs_to_collection":{"id":726871,"name":"Дюна (Коллекция)"},
 "external_ids":{"imdb_id":"tt15239678"}})J";

static const char* kSeason = R"J({"_id":"x","air_date":"2023-11-09","episodes":[
 {"air_date":"2023-11-09","episode_number":1,"name":"Пришиться","overview":"o1","runtime":53,
  "season_number":1,"still_path":"/e1.jpg","crew":[{"name":"Режиссёр"}],"guest_stars":[{"name":"Гость"}]},
 {"air_date":"2023-11-09","episode_number":2,"name":"Отшиться","overview":"","runtime":null,
  "season_number":1,"still_path":null,"crew":[],"guest_stars":[]}
],"name":"Сезон 1","season_number":1})J";

static void test_json_helpers()
{
    std::printf("[1/6] json::topLevel and json::object...\n");
    std::string top = json::topLevel(kTv);
    CHECK(json::str(kTv, "name") == "Жора Крыжовников");      // the naive read
    CHECK(json::str(top, "name") == "Слово пацана. Кровь на асфальте");
    CHECK(json::str(top, "imdb_id").empty());                 // nested only
    CHECK(json::str(json::object(kTv, "external_ids"), "imdb_id") == "tt29632826");
    CHECK(json::object(kTv, "missing").empty());
    // Braces and quotes inside strings are not structure.
    std::string tricky = R"J({"a":"x{\"y\":[1]}","b":{"c":"d"},"e":"f"})J";
    std::string t2 = json::topLevel(tricky);
    CHECK(json::str(t2, "a") == "x{\"y\":[1]}");
    CHECK(json::str(t2, "c").empty());
    CHECK(json::str(t2, "e") == "f");
    std::printf("      PASSED\n");
}

static void test_urls()
{
    std::printf("[2/6] Request URLs, keys, languages, cache keys...\n");
    tmdb::Settings s;
    s.apiKey = "0123456789abcdef0123456789abcdef";
    s.language = tmdb::languageFor("ru");
    CHECK(s.language == "ru-RU");
    CHECK(tmdb::languageFor("xx") == "en-US");
    std::string u = tmdb::requestUrl(s, "/search/multi", { { "query", "Слово пацана" } });
    CHECK(u.rfind("https://api.themoviedb.org/3/search/multi?query=%D0%A1", 0) == 0);
    CHECK(u.find("&language=ru-RU") != std::string::npos);
    CHECK(u.find("&api_key=0123456789abcdef0123456789abcdef") != std::string::npos);

    CHECK(!tmdb::isBearerToken(s.apiKey));
    tmdb::Settings b = s;
    b.apiKey = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIxMjMifQ.signature";
    CHECK(tmdb::isBearerToken(b.apiKey));
    CHECK(tmdb::requestUrl(b, "/movie/popular").find("api_key") == std::string::npos);

    // The key is not part of the cache key; the rest of the URL is.
    tmdb::Settings other = s;
    other.apiKey = "ffffffffffffffffffffffffffffffff";
    CHECK(tmdb::cacheKey(tmdb::requestUrl(s, "/tv/1")) ==
          tmdb::cacheKey(tmdb::requestUrl(other, "/tv/1")));
    CHECK(tmdb::cacheKey(tmdb::requestUrl(s, "/tv/1")) !=
          tmdb::cacheKey(tmdb::requestUrl(s, "/tv/2")));
    CHECK(tmdb::cacheKey("x").size() == 16);

    // A mirror with a trailing slash.
    tmdb::Settings m = s;
    m.apiBase = "https://mirror.example/3/";
    CHECK(tmdb::requestUrl(m, "/tv/1").rfind("https://mirror.example/3/tv/1?", 0) == 0);

    CHECK(tmdb::imageUrl(IMG, "w185", "/a.jpg") == "https://image.tmdb.org/t/p/w185/a.jpg");
    CHECK(tmdb::imageUrl(IMG, "w185", "").empty());
    CHECK(tmdb::imageUrl(IMG, "w185", "null").empty());
    std::printf("      PASSED\n");
}

static void test_list()
{
    std::printf("[3/6] A trending list: films and series, people skipped...\n");
    auto items = tmdb::parseList(kTrending, IMG, -1);
    CHECK(items.size() == 2);
    CHECK(items[0].kind == content::Kind::Movie);
    CHECK(items[0].tmdbId == 693134);
    CHECK(items[0].title == "Дюна: Часть вторая");
    CHECK(items[0].originalTitle == "Dune: Part Two");
    CHECK(items[0].year == "2024");
    CHECK(items[0].posterUrl == "https://image.tmdb.org/t/p/w185/p1.jpg");
    CHECK(items[0].backdropUrl == "https://image.tmdb.org/t/p/w1280/bd1.jpg");
    CHECK(items[0].rating > 8.1 && items[0].rating < 8.2);
    CHECK(items[0].key() == "tmdb_movie_693134");
    CHECK(items[1].kind == content::Kind::Series);
    CHECK(items[1].title == "Фоллаут");
    CHECK(items[1].backdropUrl.empty());
    CHECK(items[1].key() == "tmdb_tv_106379");
    // A forced kind ignores media_type.
    CHECK(tmdb::parseList(R"J({"results":[{"id":5,"name":"S"}]})J", IMG, 1)[0].isSeries());
    std::printf("      PASSED\n");
}

static void test_details()
{
    std::printf("[4/6] Details: a series (nested names, specials last) and a film...\n");
    content::Details d;
    CHECK(tmdb::parseDetails(kTv, content::Kind::Series, IMG, d));
    CHECK(d.item.title == "Слово пацана. Кровь на асфальте");
    CHECK(d.item.imdbId == "tt29632826");
    CHECK(d.item.seasonCount == 1);
    CHECK(d.item.runtimeMin == 52);
    CHECK(d.item.genres.size() == 2 && d.item.genres[1] == "криминал");
    CHECK(d.seasons.size() == 2);
    CHECK(d.seasons[0].number == 1 && d.seasons[0].episodeCount == 8);
    CHECK(d.seasons[1].number == 0);  // specials moved to the end
    CHECK(d.cast.size() == 2 && d.cast[0] == "Леон Кемстач");

    content::Details m;
    CHECK(tmdb::parseDetails(kMovie, content::Kind::Movie, IMG, m));
    CHECK(m.item.title == "Дюна: Часть вторая");   // not the collection's name
    CHECK(m.item.imdbId == "tt15239678");
    CHECK(m.item.runtimeMin == 166);
    CHECK(m.item.posterUrlLarge == "https://image.tmdb.org/t/p/w342/p.jpg");
    CHECK(!tmdb::parseDetails("{}", content::Kind::Movie, IMG, m));
    std::printf("      PASSED\n");
}

static void test_season()
{
    std::printf("[5/6] A season's episodes...\n");
    auto eps = tmdb::parseSeason(kSeason, IMG);
    CHECK(eps.size() == 2);
    CHECK(eps[0].title == "Пришиться");      // not the crew's "name"
    CHECK(eps[0].number == 1 && eps[0].season == 1);
    CHECK(eps[0].runtimeMin == 53);
    CHECK(eps[0].stillUrl == "https://image.tmdb.org/t/p/w300/e1.jpg");
    CHECK(eps[1].stillUrl.empty());
    CHECK(eps[1].stremioId("tt29632826") == "tt29632826:1:2");
    CHECK(eps[1].stremioId("").empty());
    std::printf("      PASSED\n");
}

static void test_errors()
{
    std::printf("[6/6] TMDB error bodies...\n");
    CHECK(tmdb::errorMessage(R"J({"success":false,"status_code":7,"status_message":"Invalid API key: You must be granted a valid key."})J")
              .rfind("Invalid API key", 0) == 0);
    CHECK(tmdb::errorMessage(kMovie).empty());
    CHECK(tmdb::errorMessage(R"J({"success":false})J") == "TMDB error");
    std::printf("      PASSED\n");
}

int main()
{
    std::printf("=========================================\n");
    std::printf(" NX TORRENT PLAYER: TMDB CLIENT          \n");
    std::printf("=========================================\n\n");
    test_json_helpers();
    test_urls();
    test_list();
    test_details();
    test_season();
    test_errors();
    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
