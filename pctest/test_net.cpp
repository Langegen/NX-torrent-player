// Offline tests for the URL logic of TorrServer playback and the proxy bypass:
// app/torrserver.cpp (built with TORRSERVER_NO_UI, without borealis) and
// app/netutil.hpp. http::get is replaced below, so nothing leaves the process.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "http.hpp"
#include "netutil.hpp"
#include "torrserver.hpp"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

// --- http stand-ins ---------------------------------------------------------
static std::string g_lastUrl, g_answer;
static long g_lastTimeout;
static int g_gets;

namespace http
{
bool get(const std::string& url, std::string& resp, std::string& err,
         const char*, long timeoutSec)
{
    g_gets++;
    g_lastUrl = url;
    g_lastTimeout = timeoutSec;
    if (g_answer.empty()) { err = "connection refused"; return false; }
    resp = g_answer;
    return true;
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

// --- tests ------------------------------------------------------------------
static const char* kStat = R"({"title":"Show","hash":"abc","file_stats":[
 {"id":1,"path":"Show/Sample/sample.mkv","length":52428800},
 {"id":2,"path":"Show/Show.S01E01.mkv","length":1500000000},
 {"id":3,"path":"Show/Show.S01E02.MKV","length":1600000000},
 {"id":4,"path":"Show/extras.zip","length":9000000000},
 {"id":5,"path":"Show/Show.nfo","length":2000}]})";

static void test_normalize()
{
    std::printf("[1/5] Server address normalisation...\n");
    CHECK(torrserver::normalizeBase("192.168.1.10:8090/") == "http://192.168.1.10:8090");
    CHECK(torrserver::normalizeBase("  https://ts.example.com//  ") == "https://ts.example.com");
    CHECK(torrserver::normalizeBase("") == "");
    CHECK(torrserver::normalizeBase("   ") == "");
    std::printf("      PASSED\n");
}

static void test_pick()
{
    std::printf("[2/5] File choice: given index, largest video, fallbacks...\n");
    CHECK(torrserver::pickFile(kStat, 0) == 1);   // engine index -> 1-based id
    CHECK(torrserver::pickFile(kStat, 4) == 5);
    CHECK(torrserver::pickFile(kStat, -1) == 3);  // largest *video*, not the zip
    CHECK(torrserver::pickFile(R"({"file_stats":[{"id":7,"path":"a.iso","length":5}]})", -1) == 7);
    CHECK(torrserver::pickFile(R"({"file_stats":[]})", -1) == -1);
    CHECK(torrserver::pickFile("not json", -1) == -1);
    std::printf("      PASSED\n");
}

static void test_resolve()
{
    std::printf("[3/5] Resolve: index known -> no request; unknown -> stat first...\n");
    const std::string base = "http://nas:8090";
    const std::string magnet = "magnet:?xt=urn:btih:abc&dn=Show S01&tr=udp://t:1";
    std::string url, err;

    g_gets = 0;
    CHECK(torrserver::resolve(base, magnet, 2, url, err));
    CHECK(g_gets == 0);
    CHECK(url == "http://nas:8090/stream?link=magnet%3A%3Fxt%3Durn%3Abtih%3Aabc%26dn%3DShow%20S01"
                 "%26tr%3Dudp%3A%2F%2Ft%3A1&index=3&play");

    g_answer = kStat;
    CHECK(torrserver::resolve(base, magnet, -1, url, err));
    CHECK(g_gets == 1);
    CHECK(g_lastUrl.find("&stat") != std::string::npos);
    CHECK(g_lastTimeout >= 60);  // waits for the metadata
    CHECK(url.size() > 11 && url.compare(url.size() - 13, 13, "&index=3&play") == 0);
    std::printf("      PASSED\n");
}

static void test_resolve_errors()
{
    std::printf("[4/5] Resolve failures carry a reason...\n");
    std::string url, err;
    g_answer.clear();
    CHECK(!torrserver::resolve("http://nas:8090", "magnet:?xt=urn:btih:abc", -1, url, err));
    CHECK(err.find("TorrServer") != std::string::npos);
    CHECK(err.find("refused") != std::string::npos);
    g_answer = R"({"file_stats":[]})";
    CHECK(!torrserver::resolve("http://nas:8090", "magnet:?xt=urn:btih:abc", -1, url, err));
    CHECK(err.find("no files") != std::string::npos);
    std::printf("      PASSED\n");
}

static void test_local_hosts()
{
    std::printf("[5/5] Proxy bypass: which hosts count as local...\n");
    using netutil::hostOf;
    using netutil::isLocalHost;
    CHECK(hostOf("http://user:pw@NAS.Lan:8090/stream?x") == "nas.lan");
    CHECK(hostOf("https://[::1]:443/") == "::1");
    CHECK(hostOf("http://192.168.1.10:9117") == "192.168.1.10");

    const char* local[] = { "localhost", "127.0.0.1", "10.0.0.5", "192.168.1.10",
                            "172.16.0.1", "172.31.255.255", "169.254.3.4", "nas",
                            "nas.local", "media.lan", "::1" };
    for (const char* h : local)
    {
        if (!isLocalHost(h)) std::fprintf(stderr, "      not local: %s\n", h);
        CHECK(isLocalHost(h));
    }
    const char* remote[] = { "api.strem.io", "8.8.8.8", "172.32.0.1", "172.15.0.1",
                             "192.169.1.1", "1.2.3.4.5", "256.1.1.1", "" };
    for (const char* h : remote)
    {
        if (isLocalHost(h)) std::fprintf(stderr, "      local: %s\n", h);
        CHECK(!isLocalHost(h));
    }
    std::printf("      PASSED\n");
}

int main()
{
    std::printf("=========================================\n");
    std::printf(" NX TORRENT PLAYER: TORRSERVER / NETWORK \n");
    std::printf("=========================================\n\n");
    test_normalize();
    test_pick();
    test_resolve();
    test_resolve_errors();
    test_local_hosts();
    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
