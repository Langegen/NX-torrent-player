// Offline test suite for the torrent engine.
//
// Nothing here touches the internet. The trackers and the DHT are replaced at
// link time: this file defines udp_announce() and dht_find_peers(), so
// engine/udp_tracker.c, engine/dht.c and engine/dhtclient.c are NOT linked (see
// run_tests.sh). The peer side is real: a mock peer listens on 127.0.0.1 and
// serves the metadata over BEP 10 / BEP 9, so engine/peer.c's handshake and
// metadata fetch run exactly as on the console.
//
//   ./pctest/run_tests.sh

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <switch.h>
#include <mbedtls/sha1.h>

#include "dhtclient.h"
#include "magnet.h"
#include "torrent.h"
#include "udp_tracker.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static peer_addr loopback(uint16_t port) {
    peer_addr a = { htonl(INADDR_LOOPBACK), port };
    return a;
}

//---------------------------------------------------------------------------
// Mock trackers: udp_announce() answers from a table set up by each test.
// Any tracker not in the table -- the engine's DEFAULT_TRACKERS -- fails at
// once, as an unreachable one would, minus the timeout.
//---------------------------------------------------------------------------

#define MOCK_MAX 8
typedef struct {
    const char *url;
    peer_addr peers[80];
    int n;
    bool slow;     // never answers: waits on the cancel flag (or 10 s)
    int delay_ms;  // answers, but only after this long
} mock_tracker;

static mock_tracker g_trackers[MOCK_MAX];
static int g_tracker_count;
static int g_announce_calls;       // every call, mocked or not
static int g_slow_cancelled;       // slow trackers that saw the cancel flag
static pthread_mutex_t g_mock_lock = PTHREAD_MUTEX_INITIALIZER;

static void trackers_reset(void) {
    memset(g_trackers, 0, sizeof(g_trackers));
    g_tracker_count = 0;
    g_announce_calls = 0;
    g_slow_cancelled = 0;
}

static mock_tracker *tracker_add(const char *url) {
    assert(g_tracker_count < MOCK_MAX);
    mock_tracker *t = &g_trackers[g_tracker_count++];
    t->url = url;
    return t;
}

int udp_announce(const char *url, const uint8_t info_hash[20],
                 const uint8_t peer_id[20], int64_t left,
                 peer_addr *peers, int max_peers,
                 const volatile bool *cancel, char *err, size_t errlen) {
    (void)info_hash; (void)peer_id; (void)left;
    pthread_mutex_lock(&g_mock_lock);
    g_announce_calls++;
    pthread_mutex_unlock(&g_mock_lock);

    for (int i = 0; i < g_tracker_count; i++) {
        mock_tracker *t = &g_trackers[i];
        if (strcmp(t->url, url) != 0) continue;
        if (t->slow) {
            double end = now_s() + 10.0;
            while (now_s() < end) {
                if (cancel && *cancel) {
                    pthread_mutex_lock(&g_mock_lock);
                    g_slow_cancelled++;
                    pthread_mutex_unlock(&g_mock_lock);
                    break;
                }
                usleep(5000);
            }
            snprintf(err, errlen, "timeout");
            return -1;
        }
        if (t->delay_ms) usleep((useconds_t)t->delay_ms * 1000);
        int n = t->n < max_peers ? t->n : max_peers;
        memcpy(peers, t->peers, (size_t)n * sizeof(*peers));
        return n;
    }
    snprintf(err, errlen, "unreachable (mock)");
    return -1;
}

//---------------------------------------------------------------------------
// Mock DHT
//---------------------------------------------------------------------------

static peer_addr g_dht_peers[16];
static int g_dht_n;
static int g_dht_calls;

int dht_find_peers(const uint8_t info_hash[20], int target_peers, int budget_ms,
                   dht_peer_cb cb, void *ctx, const volatile bool *cancel,
                   char *err, size_t errlen) {
    (void)info_hash; (void)target_peers; (void)budget_ms; (void)cancel;
    g_dht_calls++;
    if (g_dht_n == 0) {
        snprintf(err, errlen, "no DHT peers (mock)");
        return 0;
    }
    cb(ctx, g_dht_peers, g_dht_n);
    return g_dht_n;
}

void dht_set_log(void (*fn)(const char *msg)) { (void)fn; }

//---------------------------------------------------------------------------
// The torrent the mock peer serves: two files, 1000 pieces, so the info dict
// (~20 KB) spans two 16 KiB metadata pieces.
//---------------------------------------------------------------------------

#define PIECE_LEN   16384
#define PIECES      1000
#define FILE1_LEN   ((int64_t)PIECE_LEN * 600 + 123)
#define FILE2_LEN   ((int64_t)PIECE_LEN * PIECES - FILE1_LEN - 5000)

static char    *g_info;        // bencoded info dict
static size_t   g_info_len;
static uint8_t  g_info_hash[20];
static char     g_hash_hex[41];

static void build_info(void) {
    char head[256];
    int hn = snprintf(head, sizeof(head),
                      "d5:filesl"
                      "d6:lengthi%llde4:pathl8:ep01.mkvee"
                      "d6:lengthi%llde4:pathl6:Extras8:ep02.mkvee"
                      "e4:name7:Brigada12:piece lengthi%de6:pieces%d:",
                      (long long)FILE1_LEN, (long long)FILE2_LEN, PIECE_LEN,
                      PIECES * 20);
    g_info_len = (size_t)hn + PIECES * 20 + 1;
    g_info = malloc(g_info_len);
    memcpy(g_info, head, (size_t)hn);
    for (int i = 0; i < PIECES * 20; i++) g_info[hn + i] = (char)(i * 7 + 3);
    g_info[g_info_len - 1] = 'e';

    mbedtls_sha1((const unsigned char *)g_info, g_info_len, g_info_hash);
    for (int i = 0; i < 20; i++) sprintf(g_hash_hex + i * 2, "%02x", g_info_hash[i]);
}

//---------------------------------------------------------------------------
// Mock peer: BitTorrent handshake, BEP 10 extended handshake, BEP 9 data.
//---------------------------------------------------------------------------

#define MOCK_UT_ID 3   // our ut_metadata id; the engine advertises 1 for its own

typedef struct {
    int listen_fd;
    uint16_t port;
    pthread_t thread;
    volatile int served;     // connections that got the whole metadata
    volatile int accepted;
} mock_peer;

static int read_all(int fd, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, (char *)buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = send(fd, (const char *)buf + sent, n - sent, 0);
        if (r <= 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

static int send_ext(int fd, uint8_t sub, const void *a, size_t an,
                    const void *b, size_t bn) {
    uint32_t len = htonl((uint32_t)(2 + an + bn));
    uint8_t hdr[6];
    memcpy(hdr, &len, 4);
    hdr[4] = 20;   // extended
    hdr[5] = sub;
    if (write_all(fd, hdr, 6) != 0 || write_all(fd, a, an) != 0) return -1;
    return bn ? write_all(fd, b, bn) : 0;
}

static void serve_one(mock_peer *mp, int fd) {
    uint8_t hs[68];
    if (read_all(fd, hs, 68) != 0 || hs[0] != 19 ||
        memcmp(hs + 28, g_info_hash, 20) != 0)
        return;
    hs[25] |= 0x10;                       // we speak the extension protocol
    memcpy(hs + 48, "-MOCK00-abcdefghijkl", 20);
    if (write_all(fd, hs, 68) != 0) return;

    char ehs[96];
    int en = snprintf(ehs, sizeof(ehs), "d1:md11:ut_metadatai%dee13:metadata_sizei%zuee",
                      MOCK_UT_ID, g_info_len);
    if (send_ext(fd, 0, ehs, (size_t)en, NULL, 0) != 0) return;

    int npieces = (int)((g_info_len + 16383) / 16384);
    int sent = 0;
    while (sent < npieces) {
        uint32_t len;
        if (read_all(fd, &len, 4) != 0) return;
        len = ntohl(len);
        if (len == 0) continue;           // keep-alive
        if (len > 1 << 20) return;
        uint8_t *msg = malloc(len);
        if (read_all(fd, msg, len) != 0) { free(msg); return; }
        int piece = -1;
        if (len > 2 && msg[0] == 20 && msg[1] == MOCK_UT_ID) {
            const char *p = strstr((const char *)msg + 2, "5:piecei");
            if (p) piece = atoi(p + 8);
        }
        free(msg);
        if (piece < 0 || piece >= npieces) continue;

        size_t off = (size_t)piece * 16384;
        size_t n = g_info_len - off < 16384 ? g_info_len - off : 16384;
        char d[96];
        int dn = snprintf(d, sizeof(d), "d8:msg_typei1e5:piecei%de10:total_sizei%zuee",
                          piece, g_info_len);
        // Sub-id 1: the id the engine advertised for its ut_metadata.
        if (send_ext(fd, 1, d, (size_t)dn, g_info + off, n) != 0) return;
        sent++;
    }
    mp->served++;
}

static void *peer_main(void *arg) {
    mock_peer *mp = arg;
    for (;;) {
        int fd = accept(mp->listen_fd, NULL, NULL);
        if (fd < 0) return NULL;   // listen socket closed: test over
        mp->accepted++;
        serve_one(mp, fd);
        close(fd);
    }
}

static void peer_start(mock_peer *mp) {
    memset(mp, 0, sizeof(*mp));
    mp->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(mp->listen_fd >= 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(mp->listen_fd, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    CHECK(listen(mp->listen_fd, 8) == 0);
    socklen_t sl = sizeof(sa);
    getsockname(mp->listen_fd, (struct sockaddr *)&sa, &sl);
    mp->port = ntohs(sa.sin_port);
    pthread_create(&mp->thread, NULL, peer_main, mp);
}

static void peer_stop(mock_peer *mp) {
    shutdown(mp->listen_fd, SHUT_RDWR);
    close(mp->listen_fd);
    pthread_join(mp->thread, NULL);
}

// A loopback port nobody listens on: connecting is refused at once, which is
// what most of a stale swarm looks like (minus the timeouts).
static uint16_t dead_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&sa, sizeof(sa));
    socklen_t sl = sizeof(sa);
    getsockname(fd, (struct sockaddr *)&sa, &sl);
    close(fd);
    return ntohs(sa.sin_port);
}

static void magnet_for(char *out, size_t n, const char *tracker) {
    snprintf(out, n, "magnet:?xt=urn:btih:%s&dn=Brigada%s%s", g_hash_hex,
             tracker ? "&tr=" : "", tracker ? tracker : "");
}

//---------------------------------------------------------------------------
// Tests
//---------------------------------------------------------------------------

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
    printf("[1/6] Magnet parsing: 31 trackers, dedup, case...\n");

    magnet_info m;
    char err[256] = {0};
    CHECK(magnet_parse(USER_MAGNET, &m, err, sizeof(err)) == 0);
    CHECK(m.tracker_count == 31);
    CHECK(strcmp(m.name, "Бригада_[scarabe.org]") == 0);

    magnet_add_tracker(&m, "udp://open.stealth.si:80");
    CHECK(m.tracker_count == 31);
    magnet_free(&m);

    magnet_info mc;
    const char *upper = "MAGNET:?XT=URN:BTIH:B64D91F1DFA21B7DB1D628C58BBC734876453FF0&DN=Brigada";
    CHECK(magnet_parse(upper, &mc, err, sizeof(err)) == 0);
    CHECK(strcmp(mc.name, "Brigada") == 0);
    magnet_free(&mc);
    printf("      PASSED\n");
}

static void test_announce_cancel(void) {
    printf("[2/6] Announce: a pre-set cancel flag returns at once...\n");
    trackers_reset();

    torrent_meta t;
    memset(&t, 0, sizeof(t));
    for (int i = 0; i < 20; i++) {
        char url[64];
        snprintf(url, sizeof(url), "udp://slow%d.test:1/announce", i);
        t.trackers[i] = strdup(url);
        // Only MOCK_MAX of them are in the table; the rest fail at once.
        if (i < MOCK_MAX) tracker_add(t.trackers[i])->slow = true;
    }
    t.tracker_count = 20;

    volatile bool cancel = true;
    char err[256] = {0};
    double t0 = now_s();
    int answered = torrent_announce_cb(&t, NULL, NULL, &cancel, err, sizeof(err));
    double dt = now_s() - t0;
    printf("      answered=%d in %.0f ms\n", answered, dt * 1000);
    CHECK(answered == 0);
    CHECK(dt < 1.0);

    for (int i = 0; i < t.tracker_count; i++) free(t.trackers[i]);
    printf("      PASSED\n");
}

static void test_announce_stops_when_full(void) {
    printf("[3/6] Announce: stops waiting once the peer list is full...\n");
    trackers_reset();

    torrent_meta t;
    memset(&t, 0, sizeof(t));
    // The slow one goes first, so it is already waiting when the list fills.
    t.trackers[0] = "udp://slow.test:1/announce";
    t.trackers[1] = "udp://full.test:1/announce";
    t.tracker_count = 2;
    tracker_add(t.trackers[0])->slow = true;
    mock_tracker *full = tracker_add(t.trackers[1]);
    for (int i = 0; i < 80; i++) full->peers[i] = loopback((uint16_t)(20000 + i));
    full->n = 80;
    full->delay_ms = 100;

    peer_addr peers[80];
    char err[256] = {0};
    double t0 = now_s();
    int n = torrent_announce(&t, peers, 80, err, sizeof(err));
    double dt = now_s() - t0;
    printf("      %d peers in %.0f ms (slow tracker cancelled: %d)\n",
           n, dt * 1000, g_slow_cancelled);
    CHECK(n == 80);
    CHECK(dt < 2.0);          // not the slow tracker's 10 s
    CHECK(g_slow_cancelled == 1);
    printf("      PASSED\n");
}

static void check_loaded(const torrent_meta *t) {
    CHECK(strcmp(t->name, "Brigada") == 0);
    CHECK(t->piece_len == PIECE_LEN);
    CHECK(t->piece_count == PIECES);
    CHECK(t->file_count == 2);
    CHECK(t->files[0].length == FILE1_LEN);
    CHECK(t->files[1].length == FILE2_LEN);
    CHECK(t->files[1].offset == FILE1_LEN);
    CHECK(t->total_len == FILE1_LEN + FILE2_LEN);
    CHECK(strstr(t->files[1].path, "ep02.mkv") != NULL);
    CHECK(memcmp(t->info_hash, g_info_hash, 20) == 0);
}

static void test_magnet_metadata(void) {
    printf("[4/6] Magnet: metadata from a peer the tracker lists...\n");
    trackers_reset();
    g_dht_n = 0;
    g_dht_calls = 0;

    mock_peer mp;
    peer_start(&mp);
    mock_tracker *tr = tracker_add("udp://mock.test:1/announce");
    tr->peers[0] = loopback(dead_port());   // a stale peer first
    tr->peers[1] = loopback(mp.port);
    tr->n = 2;

    char magnet[256];
    magnet_for(magnet, sizeof(magnet), "udp%3a%2f%2fmock.test%3a1%2fannounce");

    torrent_meta t;
    peer_addr out[16];
    int out_n = 0;
    char err[256] = {0};
    int rc = torrent_load_magnet_peers(&t, magnet, out, 16, &out_n, err, sizeof(err));
    printf("      rc=%d err='%s' served=%d peers handed on=%d\n",
           rc, err, mp.served, out_n);
    CHECK(rc == 0);
    CHECK(mp.served == 1);
    CHECK(out_n == 2);
    CHECK(g_dht_calls == 0);       // the trackers were enough
    check_loaded(&t);
    // The magnet's tracker plus the defaults, so the download has them too.
    CHECK(t.tracker_count > 1);
    torrent_unload(&t);
    peer_stop(&mp);
    printf("      PASSED\n");
}

static void test_dht_after_stale_peers(void) {
    printf("[5/6] Magnet: stale tracker peers, metadata through the DHT (upstream #3)...\n");
    trackers_reset();

    mock_peer mp;
    peer_start(&mp);
    mock_tracker *tr = tracker_add("udp://mock.test:1/announce");
    for (int i = 0; i < 5; i++) tr->peers[i] = loopback(dead_port());
    tr->n = 5;
    g_dht_peers[0] = tr->peers[0];          // already tried: must be skipped
    g_dht_peers[1] = loopback(mp.port);
    g_dht_n = 2;
    g_dht_calls = 0;

    char magnet[256];
    magnet_for(magnet, sizeof(magnet), "udp%3a%2f%2fmock.test%3a1%2fannounce");

    torrent_meta t;
    peer_addr out[16];
    int out_n = 0;
    char err[256] = {0};
    int rc = torrent_load_magnet_peers(&t, magnet, out, 16, &out_n, err, sizeof(err));
    printf("      rc=%d err='%s' dht calls=%d peers handed on=%d\n",
           rc, err, g_dht_calls, out_n);
    CHECK(rc == 0);
    CHECK(g_dht_calls == 1);
    CHECK(mp.served == 1);
    CHECK(out_n == 1);                      // only the DHT peer that answered
    CHECK(out[0].port == mp.port);
    check_loaded(&t);
    torrent_unload(&t);
    peer_stop(&mp);
    printf("      PASSED\n");
}

static void test_trackerless_no_peers(void) {
    printf("[6/6] Magnet: no tracker, nobody anywhere -> clean failure...\n");
    trackers_reset();
    g_dht_n = 0;
    g_dht_calls = 0;

    char magnet[256];
    magnet_for(magnet, sizeof(magnet), NULL);

    torrent_meta t;
    char err[256] = {0};
    int rc = torrent_load_magnet(&t, magnet, err, sizeof(err));
    printf("      rc=%d err='%s' announces=%d dht calls=%d\n",
           rc, err, g_announce_calls, g_dht_calls);
    CHECK(rc != 0);
    CHECK(g_announce_calls > 0);            // the default trackers were asked
    CHECK(g_dht_calls == 1);                // then the DHT
    CHECK(strstr(err, "no peers found") != NULL);
    CHECK(torrent_meta_state == META_FAIL);
    printf("      PASSED\n");
}

int main(void) {
    printf("=========================================\n");
    printf(" NX TORRENT PLAYER: OFFLINE ENGINE TESTS \n");
    printf("=========================================\n\n");

    build_info();
    printf("mock torrent: %s, info dict %zu bytes\n\n", g_hash_hex, g_info_len);

    test_magnet_parsing();
    test_announce_cancel();
    test_announce_stops_when_full();
    test_magnet_metadata();
    test_dht_after_stale_peers();
    test_trackerless_no_peers();

    printf("\nALL TESTS PASSED\n");
    free(g_info);
    return 0;
}
