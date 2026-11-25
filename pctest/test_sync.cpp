#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <functional>
#include <string>
#include <vector>

#include "history.hpp"
#include "library.hpp"
#include "config.hpp"
#include "http.hpp"
#include "sync/qrcodegen.hpp"
#include "sync/sync_gdrive.hpp"
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
static RequestOptions g_lastOpt;
static std::function<bool(const std::string&, const RequestOptions&, std::string&, std::string&, long*)> g_mockHandler;

bool request(const std::string& url, const RequestOptions& opt, std::string& resp,
             std::string& err, long* status)
{
    g_lastUrl    = url;
    g_lastMethod = opt.method;
    g_lastOpt    = opt;
    if (status) *status = 0;
    resp.clear();

    if (g_mockHandler)
        return g_mockHandler(url, opt, resp, err, status);

    resp = g_mockResp;
    if (status) *status = g_mockStatus;
    if (g_mockStatus >= 400 && g_mockStatus != 404 && g_mockStatus != 405)
    {
        err = "HTTP " + std::to_string(g_mockStatus);
        return false;
    }
    return true;
}

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
    std::printf("[1/9] History serialization & deserialization...\n");

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
    CHECK(parsed[0].id == "stremio:tt0903747:1:1");
    CHECK(parsed[0].completed == true);
    CHECK(parsed[1].id == "movie1");
    CHECK(parsed[1].positionSec == 3600.5);

    std::printf("      PASSED\n");
}

static void test_history_merge()
{
    std::printf("[2/9] History conflict resolution & merge...\n");

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
    history::Item a_new;
    a_new.id = "file_a";
    a_new.title = "Local Film";
    a_new.positionSec = 2500.0;
    a_new.lastWatched = 3000;
    remote.push_back(a_new);

    history::Item b_old;
    b_old.id = "file_b";
    b_old.title = "Older Remote Film";
    b_old.positionSec = 600.0;
    b_old.lastWatched = 1500;
    remote.push_back(b_old);

    history::Item c;
    c.id = "file_c";
    c.title = "New Film from Cloud";
    c.positionSec = 100.0;
    c.lastWatched = 2500;
    remote.push_back(c);

    int changed = history::merge(local, remote);
    CHECK(changed == 2);
    CHECK(local.size() == 3);
    CHECK(local[0].id == "file_a" && local[0].positionSec == 2500.0);
    CHECK(local[1].id == "file_c" && local[1].positionSec == 100.0);
    CHECK(local[2].id == "file_b" && local[2].positionSec == 1200.0);

    std::printf("      PASSED\n");
}

static void test_webdav_url_normalize()
{
    std::printf("[3/9] WebDAV URL normalization...\n");

    CHECK(cloud_sync::WebdavProvider::normalizeUrl("webdav.yandex.ru") == "https://webdav.yandex.ru");
    CHECK(cloud_sync::WebdavProvider::normalizeUrl("http://nas.local:8080/") == "http://nas.local:8080");
    CHECK(cloud_sync::WebdavProvider::normalizeUrl("  https://webdav.cloud.mail.ru/  ") == "https://webdav.cloud.mail.ru");

    std::printf("      PASSED\n");
}

static void test_qrcodegen()
{
    std::printf("[4/9] QR Code generator (v1, v2 alignment & ISO format bits)...\n");

    // Short text -> Version 1 (21x21)
    qrcodegen::QrCode qr1 = qrcodegen::QrCode::encodeText("HELLO", qrcodegen::Ecc::LOW);
    CHECK(qr1.size() == 21);
    CHECK(qr1.getModule(3, 3) == true);
    CHECK(qr1.getModule(7, 0) == false);

    // Google Device Flow URL (29 bytes) -> Version 2 (25x25)
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("https://www.google.com/device", qrcodegen::Ecc::LOW);
    CHECK(qr.size() == 25);

    // Finder patterns & separators
    CHECK(qr.getModule(3, 3) == true);
    CHECK(qr.getModule(21, 3) == true);
    CHECK(qr.getModule(3, 21) == true);
    CHECK(qr.getModule(7, 0) == false);
    CHECK(qr.getModule(17, 0) == false);

    // Alignment pattern at (18, 18) for Version 2
    CHECK(qr.getModule(18, 18) == true);
    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            int dist = std::max(std::abs(dx), std::abs(dy));
            CHECK(qr.getModule(18 + dx, 18 + dy) == (dist != 1));
        }
    }

    // Dark module (8, size - 8) = (8, 17)
    CHECK(qr.getModule(8, 17) == true);

    // Timing patterns
    for (int i = 8; i <= 16; i++)
    {
        CHECK(qr.getModule(i, 6) == (i % 2 == 0));
        CHECK(qr.getModule(6, i) == (i % 2 == 0));
    }

    // Format info: both copies must match bit-for-bit and pass BCH check
    int fmt1 = 0;
    for (int i = 0; i <= 5; i++)
        if (qr.getModule(8, i)) fmt1 |= (1 << i);
    if (qr.getModule(8, 7)) fmt1 |= (1 << 6);
    if (qr.getModule(8, 8)) fmt1 |= (1 << 7);
    if (qr.getModule(7, 8)) fmt1 |= (1 << 8);
    for (int i = 9; i < 15; i++)
        if (qr.getModule(14 - i, 8)) fmt1 |= (1 << i);

    int fmt2 = 0;
    for (int i = 0; i < 8; i++)
        if (qr.getModule(24 - i, 8)) fmt2 |= (1 << i);
    for (int i = 8; i < 15; i++)
        if (qr.getModule(8, 25 - 15 + i)) fmt2 |= (1 << i);

    CHECK(fmt1 == fmt2);
    int unmasked = fmt1 ^ 0x5412;
    int eclBits  = (unmasked >> 13) & 0x3;
    CHECK(eclBits == 1); // LOW = 01

    // Long URL (Version >= 7 with version info)
    qrcodegen::QrCode qrLong = qrcodegen::QrCode::encodeText(
        "https://www.google.com/device?user_code=ABCD-EFGH&utm_source=nx_torrent_player_long_url_test_1234567890_abcdefghijklmnopqrstuvwxyz",
        qrcodegen::Ecc::MEDIUM);
    CHECK(qrLong.size() >= 45);

    std::printf("      PASSED\n");
}

static void test_library_merge_sync()
{
    std::printf("[5/9] Library cloud merge...\n");

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
    e1_remote.status = library::Status::Completed;
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

static void test_library_merge_updates_existing()
{
    std::printf("[6/9] Library merge updates existing entries & preserves newer episode...\n");

    std::vector<library::Entry> local;
    library::Entry s;
    s.item.kind   = content::Kind::Series;
    s.item.tmdbId = 555;
    s.item.title  = "Series 555";
    s.status      = library::Status::Watching;
    s.changed     = 1000;
    s.watchedAt   = 1500;
    s.lastSeason  = 1;
    s.lastEpisode = 1;
    s.episodes["1:1"] = { 300.0, 2400.0, false, 3000, "src", 0, "Jackett" }; // Newer local S01E01!
    local.push_back(s);

    std::vector<library::Entry> remote;
    library::Entry sr;
    sr.item.kind   = content::Kind::Series;
    sr.item.tmdbId = 555;
    sr.item.title  = "Series 555";
    sr.status      = library::Status::Watching;
    sr.changed     = 1000;
    sr.watchedAt   = 2500; // Newer overall watch (for S01E02)
    sr.lastSeason  = 1;
    sr.lastEpisode = 2;
    sr.episodes["1:1"] = { 100.0, 2400.0, false, 1200, "src", 0, "Jackett" }; // Older S01E01
    sr.episodes["1:2"] = { 900.0, 2400.0, false, 2500, "src", 1, "Jackett" }; // Newer S01E02
    remote.push_back(sr);

    std::string before = library::toJson(local);
    int added = library::merge(local, remote);
    std::string after = library::toJson(local);

    CHECK(added == 0);
    CHECK(before != after); // Status/progress update changed JSON even with added == 0
    CHECK(local[0].lastEpisode == 2);
    CHECK(local[0].episodes["1:1"].pos == 300.0); // Newer local episode kept!
    CHECK(local[0].episodes["1:2"].pos == 900.0); // Remote episode added!

    std::printf("      PASSED\n");
}

static void test_webdav_mock_cycle()
{
    std::printf("[7/9] WebDAV mock pull & push cycle...\n");

    http::g_mockHandler = nullptr;
    config::get().syncWebdavUrl  = "https://webdav.yandex.ru";
    config::get().syncWebdavUser = "tester";
    config::get().syncWebdavPass = "pass:with:colons";

    cloud_sync::WebdavProvider prov;
    CHECK(prov.isConfigured() == true);

    std::string err;
    http::g_mockStatus = 200;
    CHECK(prov.testConnection(err) == true);
    CHECK(http::g_lastMethod == "PROPFIND");
    CHECK(http::g_lastOpt.username == "tester");
    CHECK(http::g_lastOpt.password == "pass:with:colons");

    http::g_mockResp = "{\"history\":[]}";
    std::string remoteLib, remoteHist;
    CHECK(prov.pull(remoteLib, remoteHist, err) == true);
    CHECK(remoteLib == "{\"history\":[]}");

    CHECK(prov.push("{\"entries\":[]}", "{\"history\":[]}", err) == true);
    CHECK(http::g_lastMethod == "PUT");

    std::printf("      PASSED\n");
}

static void test_webdav_error_statuses()
{
    std::printf("[8/9] WebDAV pull rejects HTTP 401/403/500 & handles 404...\n");

    cloud_sync::WebdavConfig wcfg{ "https://webdav.yandex.ru", "user", "pass" };
    cloud_sync::WebdavProvider prov;
    std::string libJson, histJson, err;

    // Simulate curl returning true with HTTP 401/403/500
    for (long code : { 401L, 403L, 500L })
    {
        http::g_mockHandler = [code](const std::string&, const http::RequestOptions&, std::string& resp,
                                     std::string&, long* status) {
            if (status) *status = code;
            resp = "error";
            return true;
        };
        CHECK(prov.pull(wcfg, libJson, histJson, err) == false);
        CHECK(!err.empty());
    }

    // 404 means remote file does not exist yet -> returns true with empty strings
    http::g_mockHandler = [](const std::string&, const http::RequestOptions&, std::string&,
                             std::string&, long* status) {
        if (status) *status = 404;
        return true;
    };
    CHECK(prov.pull(wcfg, libJson, histJson, err) == true);
    CHECK(libJson.empty());
    CHECK(histJson.empty());

    http::g_mockHandler = nullptr;
    std::printf("      PASSED\n");
}

static void test_gdrive_oauth_and_api()
{
    std::printf("[9/9] Google Drive OAuth Device Flow & Drive API...\n");

    config::get().syncGdriveClientId.clear();
    config::get().syncGdriveClientSecret.clear();
    config::get().syncGdriveRefreshToken.clear();
    config::get().syncGdriveUserEmail.clear();

    cloud_sync::GdriveProvider prov;
    if (!cloud_sync::GdriveProvider::hasBuiltinCredentials())
    {
        CHECK(cloud_sync::GdriveProvider::hasClientCredentials() == false);
        cloud_sync::DeviceAuthInfo info;
        std::string err;
        CHECK(prov.startDeviceAuth(info, err) == false);
        CHECK(err.find("Client ID") != std::string::npos);
    }

    config::get().syncGdriveClientId     = "12345.apps.googleusercontent.com";
    config::get().syncGdriveClientSecret = "GOCSPX-secret";
    CHECK(cloud_sync::GdriveProvider::hasClientCredentials() == true);

    // 1. startDeviceAuth success (using verification_uri fallback)
    http::g_mockHandler = [](const std::string& url, const http::RequestOptions& opt,
                             std::string& resp, std::string&, long* status) {
        CHECK(url == "https://oauth2.googleapis.com/device/code");
        CHECK(opt.method == "POST");
        CHECK(opt.body.find("drive.appdata") != std::string::npos);
        if (status) *status = 200;
        resp = "{\"device_code\":\"dev123\",\"user_code\":\"ABCD-EFGH\",\"verification_uri\":\"https://www.google.com/device\",\"expires_in\":1800,\"interval\":5}";
        return true;
    };
    cloud_sync::DeviceAuthInfo info;
    std::string err;
    CHECK(prov.startDeviceAuth(info, err) == true);
    CHECK(info.deviceCode == "dev123");
    CHECK(info.userCode == "ABCD-EFGH");
    CHECK(info.verificationUrl == "https://www.google.com/device");

    // 2. startDeviceAuth invalid_client error message
    http::g_mockHandler = [](const std::string&, const http::RequestOptions&,
                             std::string& resp, std::string&, long* status) {
        if (status) *status = 401;
        resp = "{\"error\":\"invalid_client\",\"error_description\":\"The OAuth client was not found.\"}";
        return true;
    };
    CHECK(prov.startDeviceAuth(info, err) == false);
    CHECK(err.find("invalid_client") != std::string::npos);

    // 3. pollDeviceAuth: pending, slow_down, transient error, approved + userinfo
    cloud_sync::GdriveTokens tokens;
    http::g_mockHandler = [](const std::string&, const http::RequestOptions&,
                             std::string& resp, std::string&, long* status) {
        if (status) *status = 428;
        resp = "{\"error\":\"authorization_pending\"}";
        return true;
    };
    CHECK(prov.pollDeviceAuth("cid", "csec", "dev123", tokens, err) == cloud_sync::PollStatus::Pending);

    http::g_mockHandler = [](const std::string&, const http::RequestOptions&,
                             std::string& resp, std::string&, long* status) {
        if (status) *status = 400;
        resp = "{\"error\":\"slow_down\"}";
        return true;
    };
    CHECK(prov.pollDeviceAuth("cid", "csec", "dev123", tokens, err) == cloud_sync::PollStatus::SlowDown);

    http::g_mockHandler = [](const std::string&, const http::RequestOptions&,
                             std::string&, std::string& netErr, long* status) {
        if (status) *status = 0;
        netErr = "Timeout";
        return false;
    };
    CHECK(prov.pollDeviceAuth("cid", "csec", "dev123", tokens, err) == cloud_sync::PollStatus::TransientError);

    http::g_mockHandler = [](const std::string& url, const http::RequestOptions&,
                             std::string& resp, std::string&, long* status) {
        if (status) *status = 200;
        if (url.find("/token") != std::string::npos)
            resp = "{\"access_token\":\"acc123\",\"refresh_token\":\"ref123\",\"expires_in\":3600}";
        else if (url.find("userinfo") != std::string::npos)
            resp = "{\"email\":\"user@example.com\"}";
        return true;
    };
    CHECK(prov.pollDeviceAuth("cid", "csec", "dev123", tokens, err) == cloud_sync::PollStatus::Approved);
    CHECK(tokens.refreshToken == "ref123");
    CHECK(tokens.userEmail == "user@example.com");

    // 4. testConnection checks appDataFolder files endpoint (not /about)
    cloud_sync::GdriveCredentials creds{ "cid", "csec", "ref123" };
    http::g_mockHandler = [](const std::string& url, const http::RequestOptions&,
                             std::string& resp, std::string&, long* status) {
        if (status) *status = 200;
        if (url.find("/token") != std::string::npos)
            resp = "{\"access_token\":\"acc999\",\"expires_in\":3600}";
        else
        {
            CHECK(url.find("spaces=appDataFolder") != std::string::npos);
            CHECK(url.find("/about") == std::string::npos);
            resp = "{\"files\":[]}";
        }
        return true;
    };
    CHECK(prov.testConnection(creds, err) == true);

    // 5. pull and push cycle
    http::g_mockHandler = [](const std::string& url, const http::RequestOptions& opt,
                             std::string& resp, std::string&, long* status) {
        if (status) *status = 200;
        if (url.find("alt=media") != std::string::npos)
            resp = "{\"entries\":[]}";
        else if (opt.method == "GET")
            resp = "{\"files\":[{\"id\":\"fid1\",\"name\":\"nx_library.json\"}]}";
        else
            resp = "{\"id\":\"fid_new\"}";
        return true;
    };
    std::string rLib, rHist;
    CHECK(prov.pull(creds, rLib, rHist, err) == true);
    CHECK(rLib == "{\"entries\":[]}");
    CHECK(rHist.empty());
    CHECK(prov.push(creds, "{\"entries\":[]}", "{\"history\":[]}", err) == true);

    http::g_mockHandler = nullptr;
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
    test_library_merge_updates_existing();
    test_webdav_mock_cycle();
    test_webdav_error_statuses();
    test_gdrive_oauth_and_api();

    std::printf("\nALL CLOUD SYNC TESTS PASSED\n");
    return 0;
}
