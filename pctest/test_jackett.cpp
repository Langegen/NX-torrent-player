// Offline tests for app/jackett_diag.cpp: what the app tells people when
// Jackett does not answer the way it should, and how a typed address is
// cleaned up.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "jackett_diag.hpp"
#include "json.hpp"

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

int main()
{
    test_transport();
    test_answers();
    test_indexers();
    test_urls();
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
