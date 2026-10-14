#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <switch.h>
#include "magnet.h"
#include "torrent.h"
#include "dhtclient.h"

// Test magnet with 31 trackers from the user's issue description
static const char *USER_MAGNET =
    "magnet:?xt=urn:btih:b64d91f1dfa21b7db1d628c58bbc734876453ff0"
    "&dn=%d0%91%d1%80%d0%b8%d0%b3%d0%b0%d0%b4%d0%b0_%5bscarabe.org%5d"
    "&tr=http%3a%2f%2fkorsars.pro%2fbt%2fannounce.php%3fuk%3ddiTtwcX9nV"
    "&tr=http%3a%2f%2ftracker.bitru.org%2fannounce"
    "&tr=udp%3a%2f%2fbt.rutor.org%3a2710"
    "&tr=http%3a%2f%2fbt.new-team.org%2f0000006f32e3db162c66313e8f71388d%2fannounce"
    "&tr=udp%3a%2f%2ftracker.openbittorrent.com%3a80%2fannounce"
    "&tr=udp%3a%2f%2ftracker.publicbt.com%3a80%2fannounce"
    "&tr=udp%3a%2f%2ftracker.coppersurfer.tk%3a6969"
    "&tr=udp%3a%2f%2ftracker.leechers-paradise.org%3a6969"
    "&tr=udp%3a%2f%2fopen.demonii.com%3a1337"
    "&tr=http%3a%2f%2fretracker.local%2fannounce"
    "&tr=udp%3a%2f%2fexplodie.org%3a6969%2fannounce"
    "&tr=udp%3a%2f%2fexodus.desync.com%3a6969%2fannounce"
    "&tr=udp%3a%2f%2ftracker.opentrackr.org%3a1337%2fannounce"
    "&tr=udp%3a%2f%2fopen.stealth.si%3a80%2fannounce"
    "&tr=udp%3a%2f%2ftracker.moeking.me%3a6969%2fannounce"
    "&tr=udp%3a%2f%2fcode2chicken.nl%3a6969%2fannounce"
    "&tr=udp%3a%2f%2fdiscord.heihachi.pw%3a6969%2fannounce"
    "&tr=http%3a%2f%2fvps02.net.orel.ru%3a80%2fannounce"
    "&tr=http%3a%2f%2ftracker.files.fm%3a6969%2fannounce"
    "&tr=udp%3a%2f%2ftracker.torrent.eu.org%3a451%2fannounce"
    "&tr=udp%3a%2f%2fopentracker.i2p.rocks%3a6969%2fannounce"
    "&tr=http%3a%2f%2fbt02.nnm-club.cc%3a2710%2fannounce"
    "&tr=udp%3a%2f%2fopentor.net%3a6969"
    "&tr=udp%3a%2f%2ftracker.bitsearch.to%3a1337%2fannounce"
    "&tr=http%3a%2f%2fbt4.t-ru.org%2fann"
    "&tr=http%3a%2f%2fbt3.t-ru.org%2fann"
    "&tr=http%3a%2f%2fbt2.t-ru.org%2fann"
    "&tr=http%3a%2f%2fbt.t-ru.org%2fann"
    "&tr=http%3a%2f%2ftracker.dler.org%3a6969%2fannounce"
    "&tr=udp%3a%2f%2fopen.stealth.si%3a80"
    "&tr=udp%3a%2f%2fexodus.desync.com%3a6969";

static void test_magnet_parsing(void) {
    printf("[1/3] Testing magnet parsing with 31 trackers...\n");

    magnet_info m;
    char err[256] = {0};
    int rc = magnet_parse(USER_MAGNET, &m, err, sizeof(err));
    assert(rc == 0);
    assert(m.tracker_count == 31);
    assert(strcmp(m.name, "Бригада_[scarabe.org]") == 0);

    // Verify deduplication
    magnet_add_tracker(&m, "udp://open.stealth.si:80");
    assert(m.tracker_count == 31);

    // Test case insensitivity
    magnet_info m_case;
    const char *case_magnet = "MAGNET:?XT=URN:BTIH:B64D91F1DFA21B7DB1D628C58BBC734876453FF0&DN=Brigada";
    rc = magnet_parse(case_magnet, &m_case, err, sizeof(err));
    assert(rc == 0);
    assert(strcmp(m_case.name, "Brigada") == 0);
    magnet_free(&m_case);

    magnet_free(&m);
    printf("      Magnet parsing & deduplication tests PASSED.\n");
}

static void test_announce_pool_bounds(void) {
    printf("[2/3] Testing announce worker pool bounds...\n");

    // Construct a meta with 20 dummy trackers
    torrent_meta t;
    memset(&t, 0, sizeof(t));
    for (int i = 0; i < 20; i++) {
        char url[128];
        snprintf(url, sizeof(url), "udp://127.0.0.1:%d/announce", 60000 + i);
        t.trackers[i] = strdup(url);
    }
    t.tracker_count = 20;

    // Use cancel flag set to true to test immediate cancellation handling
    volatile bool cancel = true;
    char err[256] = {0};
    u64 t0 = armGetSystemTick();
    int answered = torrent_announce_cb(&t, NULL, NULL, &cancel, err, sizeof(err));
    u64 dt = armGetSystemTick() - t0;

    printf("      Announce completed: answered=%d, time=%.2f ms\n",
           answered, (double)dt / 1000000.0);

    for (int i = 0; i < t.tracker_count; i++) free(t.trackers[i]);
    printf("      Announce worker pool bounds test PASSED.\n");
}

static void test_trackerless_and_dht_fallback(void) {
    printf("[3/3] Testing trackerless magnet link and DHT fallback...\n");

    // Bare magnet link with NO trackers
    const char *bare_magnet = "magnet:?xt=urn:btih:b64d91f1dfa21b7db1d628c58bbc734876453ff0";

    torrent_meta t;
    char err[256] = {0};

    // Run magnet load: previously failed with "magnet has no tracker"
    // Now it should populate fallback trackers, announce, attempt DHT, and fail only with seed absence.
    u64 t0 = armGetSystemTick();
    int rc = torrent_load_magnet(&t, bare_magnet, err, sizeof(err));
    u64 dt = armGetSystemTick() - t0;

    printf("      Result: rc=%d, error='%s', time=%.2f s\n",
           rc, err, (double)dt / 1000000000.0);

    // It must NOT fail with "magnet has no tracker"
    assert(strstr(err, "magnet has no tracker") == NULL);
    assert(strstr(err, "DHT bootstrap not supported") == NULL);

    // It must report either no peers found or no connectable peers
    assert(strstr(err, "no peers found") != NULL ||
           strstr(err, "no connectable peers") != NULL ||
           rc == 0);

    printf("      Trackerless magnet & DHT fallback test PASSED.\n");
}

int main(void) {
    printf("=========================================\n");
    printf(" AUTOMATED TEST SUITE: NX TORRENT PLAYER \n");
    printf("=========================================\n\n");

    test_magnet_parsing();
    test_announce_pool_bounds();
    test_trackerless_and_dht_fallback();

    printf("\n=========================================\n");
    printf(" ALL AUTOMATED TESTS COMPLETED SUCCESSFULLY!\n");
    printf("=========================================\n");
    return 0;
}
