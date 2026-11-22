#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <string>
#include <vector>

#include "history.hpp"
#include "library.hpp"
#include "config.hpp"
#include "http.hpp"
#include "sync/qrcodegen.hpp"
#include "sync/sync_webdav.hpp"

namespace config
{
static Config g_testCfg;
Config& get() { return g_testCfg; }
bool save() { return true; }
}

namespace http
{
static std::string g_mockResp = "";
static long g_mockStatus = 200;
static std::string g_lastUrl = "";
static std::string g_lastMethod = "";

bool request(const std::string& url, const RequestOptions& opt, std::string& resp,
             std::string& err, long* status)
{
    g_lastUrl = url;
    g_lastMethod = opt.method;
    resp = g_mockResp;
    if (status) *status = g_mockStatus;
    if (g_mockStatus >= 400 && g_mockStatus != 404 && g_mockStatus != 405)
    {
        err = "HTTP " + std::to_string(g_mockStatus);
        return false;
    }
    return true;
}

std::string urlEncode(const std::string& s) { return s; }
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

static void test_history_roundtrip()
{
    std::printf("[1/5] History serialization & deserialization...\n");

    std::vector<history::Item> items;
    history::Item it1;
    it1.id = "movie1";
    it1.title = "Dune: Part Two";
    it1.source = "magnet:?xt=urn:btih:abc1";
    it1.fileIndex = 0;
    it1.positionSec = 3600.5;
    it1.durationSec = 7200.0;
    it1.lastWatched = 1710000000;
    it1.completed = false;
    items.push_back(it1);

    history::Item it2;
    it2.id = "stremio:tt0903747:1:1";
    it2.title = "Breaking Bad S01E01";
    it2.stremioVideoId = "tt0903747:1:1";
    it2.positionSec = 2800.0;
    it2.durationSec = 3000.0;
    it2.lastWatched = 1710000500;
    it2.completed = true;
    items.push_back(it2);

    std::string jsonStr = history::toJson(items);
    CHECK(jsonStr.find("Dune: Part Two") != std::string::npos);
    CHECK(jsonStr.find("Breaking Bad S01E01") != std::string::npos);

    std::vector<history::Item> parsed = history::fromJson(jsonStr);
    CHECK(parsed.size() == 2);
    // Should be sorted by lastWatched descending: it2 (1710000500) then it1 (1710000000)
    CHECK(parsed[0].id == "stremio:tt0903747:1:1");
    CHECK(parsed[0].completed == true);
    CHECK(parsed[1].id == "movie1");
    CHECK(parsed[1].positionSec == 3600.5);

    std::printf("      PASSED\n");
}

static void test_history_merge()
{
    std::printf("[2/5] History conflict resolution & merge...\n");

    std::vector<history::Item> local;
    history::Item a;
    a.id = "file_a";
    a.title = "Local Film";
    a.positionSec = 500.0;
    a.lastWatched = 1000;
    local.push_back(a);

    history::Item b;
    b.id = "file_b";
    b.title = "Older Remote Film";
    b.positionSec = 1200.0;
    b.lastWatched = 2000;
    local.push_back(b);

    std::vector<history::Item> remote;
    // remote has newer progress for file_a
    history::Item a_new;
    a_new.id = "file_a";
    a_new.title = "Local Film";
    a_new.positionSec = 2500.0;
    a_new.lastWatched = 3000; // newer!
    remote.push_back(a_new);

    // remote has older progress for file_b
    history::Item b_old;
    b_old.id = "file_b";
    b_old.title = "Older Remote Film";
    b_old.positionSec = 600.0;
    b_old.lastWatched = 1500; // older than local (2000)!
    remote.push_back(b_old);

    // remote has a completely new file_c
    history::Item c;
    c.id = "file_c";
    c.title = "New Film from Cloud";
    c.positionSec = 100.0;
    c.lastWatched = 2500;
    remote.push_back(c);

    int changed = history::merge(local, remote);
    CHECK(changed == 2); // a updated, c added. b kept as local!
    CHECK(local.size() == 3);

    // Sorted descending by lastWatched:
    // 1st: file_a (3000)
    // 2nd: file_c (2500)
    // 3rd: file_b (2000)
    CHECK(local[0].id == "file_a" && local[0].positionSec == 2500.0);
    CHECK(local[1].id == "file_c" && local[1].positionSec == 100.0);
    CHECK(local[2].id == "file_b" && local[2].positionSec == 1200.0);

    std::printf("      PASSED\n");
}

static void test_webdav_url_normalize()
{
    std::printf("[3/5] WebDAV URL normalization...\n");

    CHECK(cloud_sync::WebdavProvider::normalizeUrl("webdav.yandex.ru") == "https://webdav.yandex.ru");
    CHECK(cloud_sync::WebdavProvider::normalizeUrl("http://nas.local:8080/") == "http://nas.local:8080");
    CHECK(cloud_sync::WebdavProvider::normalizeUrl("  https://webdav.cloud.mail.ru/  ") == "https://webdav.cloud.mail.ru");

    std::printf("      PASSED\n");
}

static void test_qrcodegen()
{
    std::printf("[4/5] QR Code generator...\n");

    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("https://www.google.com/device", qrcodegen::Ecc::LOW);
    CHECK(qr.size() >= 21); // Version 1 is 21x21
    // Top-left finder pattern center should be dark
    CHECK(qr.getModule(3, 3) == true);
    // Border around finder pattern (module 7, 0) should be light
    CHECK(qr.getModule(7, 0) == false);

    std::printf("      PASSED\n");
}

static void test_library_merge_sync()
{
    std::printf("[5/5] Library cloud merge...\n");

    std::vector<library::Entry> local;
    library::Entry e1;
    e1.item.tmdbId = 100;
    e1.item.title = "Film 100";
    e1.status = library::Status::Watching;
    e1.changed = 1000;
    local.push_back(e1);

    std::vector<library::Entry> remote;
    library::Entry e1_remote;
    e1_remote.item.tmdbId = 100;
    e1_remote.item.title = "Film 100";
    e1_remote.status = library::Status::Completed; // watched on phone!
    e1_remote.changed = 2000;
    remote.push_back(e1_remote);

    library::Entry e2_remote;
    e2_remote.item.tmdbId = 200;
    e2_remote.item.title = "Film 200";
    e2_remote.status = library::Status::Planned;
    e2_remote.changed = 1500;
    remote.push_back(e2_remote);

    int added = library::merge(local, remote);
    CHECK(added == 1);
    CHECK(local.size() == 2);
    CHECK(local[0].status == library::Status::Completed);
    CHECK(local[1].item.tmdbId == 200);

    std::printf("      PASSED\n");
}

static void test_webdav_mock_cycle()
{
    std::printf("[6/6] WebDAV mock pull & push cycle...\n");

    config::get().syncWebdavUrl = "https://webdav.yandex.ru";
    config::get().syncWebdavUser = "tester";
    config::get().syncWebdavPass = "pass123";

    cloud_sync::WebdavProvider prov;
    CHECK(prov.isConfigured() == true);

    std::string err;
    http::g_mockStatus = 200;
    CHECK(prov.testConnection(err) == true);
    CHECK(http::g_lastMethod == "PROPFIND");

    // Pull mock
    http::g_mockResp = "{\"history\":[]}";
    std::string remoteLib, remoteHist;
    CHECK(prov.pull(remoteLib, remoteHist, err) == true);
    CHECK(remoteLib == "{\"history\":[]}");

    // Push mock
    CHECK(prov.push("{\"entries\":[]}", "{\"history\":[]}", err) == true);
    CHECK(http::g_lastMethod == "PUT");

    std::printf("      PASSED\n");
}

int main()
{
    std::printf("=========================================\n");
    std::printf(" NX TORRENT PLAYER: CLOUD SYNC TESTS     \n");
    std::printf("=========================================\n\n");

    test_history_roundtrip();
    test_history_merge();
    test_webdav_url_normalize();
    test_qrcodegen();
    test_library_merge_sync();
    test_webdav_mock_cycle();

    std::printf("\nALL CLOUD SYNC TESTS PASSED\n");
    return 0;
}
