// Offline tests for app/jackett_diag.cpp: what the app tells people when
// Jackett does not answer the way it should, and how a typed address is
// cleaned up.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "http.hpp"
#include "jackett.hpp"
#include "jackett_diag.hpp"
#include "json.hpp"

namespace http
{
std::string urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
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

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("      FAILED: %s (line %d)\n", #cond, __LINE__);      \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

using jackett::Problem;

static void test_transport()
{
    std::printf("[1/4] curl errors, by what to do about them...\n");
    using jackett::diagnoseTransport;
    CHECK(diagnoseTransport("Couldn't connect to server").problem == Problem::Unreachable);
    CHECK(diagnoseTransport("Failed to connect to 192.168.1.5 port 9117: Connection refused")
              .problem == Problem::Unreachable);
    CHECK(diagnoseTransport("Could not resolve host: jacket.local").problem == Problem::NoHost);
    CHECK(diagnoseTransport("Timeout was reached").problem == Problem::Timeout);
    CHECK(diagnoseTransport("Operation timed out after 25001 milliseconds").problem ==
          Problem::Timeout);
    CHECK(diagnoseTransport("could not verify the site's certificate -- check the console's "
                            "date and time")
              .problem == Problem::Certificate);
    CHECK(diagnoseTransport("URL using bad/illegal format or missing URL").problem ==
          Problem::BadUrl);
    CHECK(diagnoseTransport("Unsupported protocol").problem == Problem::BadUrl);
    jackett::Diagnosis o = jackett::diagnoseTransport("Something odd");
    CHECK(o.problem == Problem::Other && o.detail == "Something odd");
    std::printf("      PASSED\n");
}

static void test_answers()
{
    std::printf("[2/4] Answers: key, not Jackett, server errors...\n");
    using jackett::diagnoseAnswer;
    CHECK(diagnoseAnswer(401, "", false).problem == Problem::BadKey);
    CHECK(diagnoseAnswer(403, "{}", true).problem == Problem::BadKey);
    CHECK(diagnoseAnswer(400, R"({"error":"Invalid API Key"})", false).problem ==
          Problem::BadKey);
    CHECK(diagnoseAnswer(200, "<!DOCTYPE html><html><title>Login</title>", false).problem ==
          Problem::NotJackett);
    CHECK(diagnoseAnswer(404, "Not Found", true).problem == Problem::NotJackett);
    jackett::Diagnosis e = diagnoseAnswer(500, "oops", true);
    CHECK(e.problem == Problem::Other && e.detail == "HTTP 500");
    jackett::Diagnosis m = diagnoseAnswer(400, R"({"message":"Indexer is not configured"})", true);
    CHECK(m.problem == Problem::Other && m.detail == "Indexer is not configured");
    std::printf("      PASSED\n");
}

static void test_indexers()
{
    std::printf("[3/4] Indexers: none, all failing, some fine...\n");
    using jackett::diagnoseAnswer;
    const char* none = R"({"Results":[],"Indexers":[]})";
    CHECK(diagnoseAnswer(200, none, false).problem == Problem::NoIndexers);
    // A search that finds nothing is just that.
    CHECK(diagnoseAnswer(200, none, true).problem == Problem::None);

    const char* allBad = R"({"Results":[],"Indexers":[
        {"ID":"rutor","Name":"Rutor","Status":1,"Results":0,"Error":"Request timed out"},
        {"ID":"nnm","Name":"NNM-Club","Status":1,"Results":0,"Error":"Login failed"}]})";
    jackett::Diagnosis b = diagnoseAnswer(200, allBad, true);
    CHECK(b.problem == Problem::IndexersFailed);
    CHECK(b.indexers == 2 && b.failed == 2);
    CHECK(b.detail == "Rutor: Request timed out");

    const char* someOk = R"({"Results":[{"Title":"Dune.2021.1080p","Size":1}],"Indexers":[
        {"ID":"rutor","Name":"Rutor","Status":2,"Results":1,"Error":null},
        {"ID":"nnm","Name":"NNM-Club","Status":1,"Results":0,"Error":"Login failed"}]})";
    jackett::Diagnosis s = diagnoseAnswer(200, someOk, true);
    CHECK(s.problem == Problem::None);
    CHECK(s.indexers == 2 && s.failed == 1);
    CHECK(s.hasIndexerList);

    // Jacred (ru.jac.black): results, no "Indexers" -- a working server.
    const char* jacred = R"({"Results":[{"Tracker":"rutracker","Title":"Дюна / Dune [2021, WEB-DL 1080p] Dub","Size":1,"Seeders":5,"MagnetUri":"magnet:?xt=urn:btih:00&tr=x"}],"jacred":true})";
    jackett::Diagnosis j = diagnoseAnswer(200, jacred, false);
    CHECK(j.problem == Problem::None);
    CHECK(!j.hasIndexerList && j.indexers == 0);
    CHECK(diagnoseAnswer(200, R"({"Results":[],"jacred":true})", false).problem == Problem::None);
    // nginx in front of Jacred refusing a request (no User-Agent): not the API.
    CHECK(diagnoseAnswer(403, "<html><title>403 Forbidden</title></html>", false).problem ==
          Problem::Forbidden);
    // A size past 2 GB (Jacred's own answer): no wrap on a 32-bit `long`.
    CHECK(json::integer(R"({"Size":6184752906})", "Size") == 6184752906LL);
    std::printf("      PASSED\n");
}

static void test_urls()
{
    std::printf("[4/4] Typed addresses...\n");
    using jackett::normalizeUrl;
    CHECK(normalizeUrl("  http://192.168.1.5:9117/ ") == "http://192.168.1.5:9117");
    CHECK(normalizeUrl("192.168.1.5") == "http://192.168.1.5:9117");
    CHECK(normalizeUrl("192.168.1.5:8080") == "http://192.168.1.5:8080");
    CHECK(normalizeUrl("nas.local/jackett") == "http://nas.local:9117/jackett");
    // A scheme typed: taken as is, no port guessed (a reverse proxy).
    CHECK(normalizeUrl("https://jackett.example.com") == "https://jackett.example.com");
    CHECK(normalizeUrl("[::1]") == "http://[::1]:9117");
    CHECK(normalizeUrl("[::1]:9000") == "http://[::1]:9000");
    CHECK(normalizeUrl("   ").empty());
    // A public Jacred mirror: standard port, nothing added but the scheme.
    CHECK(normalizeUrl("ru.jac.black") == "http://ru.jac.black");
    CHECK(normalizeUrl("jac.red/") == "http://jac.red");
    CHECK(normalizeUrl("jackett") == "http://jackett:9117");
    CHECK(normalizeUrl("NAS.lan") == "http://NAS.lan:9117");
    CHECK(normalizeUrl("localhost") == "http://localhost:9117");
    std::printf("      PASSED\n");
}

static void test_jacred_results_and_urls()
{
    std::printf("[5/5] Jacred JSON metadata, trackers, btih and search URLs...\n");
    const char* resp = R"J({"Results":[
      {
        "Tracker": "rutracker, bitru, kinozal, rutor",
        "Title": "\u0424\u043E\u043B\u043B\u0430\u0443\u0442 (2 \u0441\u0435\u0437\u043E\u043D: 1-8 \u0441\u0435\u0440\u0438\u0438 \u0438\u0437 8) / Fallout / 2025",
        "Size": 31933081845,
        "PublishDate": "2026-05-09T09:57:00",
        "Category": [5000],
        "Seeders": 277,
        "Peers": 27,
        "MagnetUri": "magnet:?xt=urn:btih:B4F8206402E6DAAE109D8A487A3E21B21A495DE5&dn=Fallout",
        "languages": ["eng", "rus"],
        "info": {
          "quality": 1080,
          "videotype": "hdr",
          "voices": ["\u0414\u0443\u0431\u043B\u044F\u0436", "Jaskier"],
          "seasons": [2],
          "types": ["serial"],
          "name": "\u0424\u043E\u043B\u043B\u0430\u0443\u0442",
          "originalname": "Fallout",
          "relased": 2025,
          "imdb": "tt12637874",
          "seedersLive": true,
          "seedersUnknown": false
        }
      },
      {
        "Tracker": "nyaa",
        "Title": "Anime.Movie.2001.1080p",
        "Size": 2147483648,
        "Category": [5070],
        "Seeders": 0,
        "MagnetUri": "magnet:?xt=urn:btih:MFRGGZDFMZTWQ2LKNNWG23TPOBZGS43U",
        "info": {
          "quality": 1080,
          "types": ["anime"],
          "seedersUnknown": true
        }
      }
    ],"jacred":true})J";

    auto items = jackett::parseResultsJson(resp);
    CHECK(items.size() == 2);
    const auto& f = items[0];
    CHECK(f.title.find("Фоллаут") == 0);
    CHECK(f.size == 31933081845LL);
    CHECK(f.seeders == 277);
    CHECK(f.trackers.size() == 4 && f.trackers[0] == "rutracker" && f.trackers[3] == "rutor");
    CHECK(jackett::compactTrackers(f.trackers) == "rutracker +3");
    CHECK(f.categories.size() == 1 && f.categories[0] == 5000);
    CHECK(f.languages.size() == 2 && f.languages[0] == "eng" && f.languages[1] == "rus");
    CHECK(f.infoHash == "b4f8206402e6daae109d8a487a3e21b21a495de5");
    CHECK(f.hasJacredInfo && f.infoQuality == 1080 && f.infoVideotype == "hdr");
    CHECK(f.infoVoices.size() == 2 && f.infoVoices[0] == "Дубляж" && f.infoVoices[1] == "Jaskier");
    CHECK(f.infoSeasons.size() == 1 && f.infoSeasons[0] == 2);
    CHECK(f.infoTypes.size() == 1 && f.infoTypes[0] == "serial");
    CHECK(f.infoName == "Фоллаут" && f.infoOriginalName == "Fallout");
    CHECK(f.infoYear == 2025 && f.infoImdb == "tt12637874");
    CHECK(f.seedersLive && !f.seedersUnknown);

    // Base32 btih decodes to 40-char lowercase hex.
    CHECK(items[1].infoHash.size() == 40);
    CHECK(items[1].seedersUnknown);
    CHECK(jackett::compactTrackers(items[1].trackers) == "nyaa");
    CHECK(jackett::splitTrackers("rutor, RuTor , kinozal").size() == 2);

    jackett::TitleHint animeMovie;
    animeMovie.title = "Унесённые призраками";
    animeMovie.titleOriginal = "Spirited Away";
    animeMovie.year = "2001";
    animeMovie.series = false;
    animeMovie.anime = true;
    std::string u1 = jackett::buildSearchUrl("https://ru.jac.black/", "", "Spirited Away 2001",
                                             jackett::Category::MOVIES, &animeMovie);
    CHECK(u1.find("is_serial=0") != std::string::npos);
    CHECK(u1.find("Category[]=2000") != std::string::npos);
    CHECK(u1.find("Category[]=5070") != std::string::npos);

    std::string u2 = jackett::buildSearchUrl("https://ru.jac.black", "", "Фоллаут",
                                             jackett::Category::ALL, nullptr);
    CHECK(u2 == "https://ru.jac.black/api/v2.0/indexers/all/results?Query=%D0%A4%D0%BE%D0%BB%D0%BB%D0%B0%D1%83%D1%82");
    std::printf("      PASSED\n");
}

int main()
{
    test_transport();
    test_answers();
    test_indexers();
    test_urls();
    test_jacred_results_and_urls();
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
