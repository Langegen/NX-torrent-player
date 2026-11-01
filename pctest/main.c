// PC engine test harness: runs the exact Switch torrent engine (torrentfs) on a
// normal PC to measure download behaviour where DHT/µTP/UDP all work, so we can
// tell whether slowness is our algorithm or the Switch's environment.
//
//   Usage: ./enginetest <file.torrent | magnet:?...> [seconds] [mode]
//
//   mode:
//     (none)        download from the start of the file
//     tail          pin the playhead ~8 MB before the end (moov atom test)
//     stream        playhead follows the lowest missing piece
//     play[:KB/s]   a player model: mpv's head/tail probe, then reads at a
//                   fixed bitrate (default 1250 KB/s = 10 Mbit/s) with up to
//                   60 s of read-ahead, feeding the backlog to the engine like
//                   the app does. Reports startup time and stalls.
//
//   Env: ENGINETEST_CACHE  cache path base (default "enginetest_cache")
//        ENGINETEST_SD=1   cache to disk instead of RAM (the app's default is
//                          RAM streaming)
//        ENGINETEST_LOG=1  tracker/DHT log lines on stderr
//
// The last line is a one-line SUMMARY, for comparing runs.

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#include "dhtclient.h"
#include "torrent.h"
#include "torrentfs.h"

static volatile int g_stop = 0;
static void on_sigint(int s) { (void)s; g_stop = 1; }

static void log_line(const char *msg) { fprintf(stderr, "  log: %s\n", msg); }

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

//-----------------------------------------------------------------------------
// Player model
//-----------------------------------------------------------------------------

#define PLAY_CHUNK      (256 * 1024)
#define PLAY_AHEAD_SECS 60.0                 // mpv cache-secs in the app
#define PLAY_AHEAD_MAX  (128LL << 20)        // mpv demuxer-max-bytes
#define PLAY_START_SECS 2.0                  // buffered before the first frame
#define STALL_MIN_SECS  0.5                  // shorter hiccups are not counted

typedef struct {
    torrentfs *t;
    int64_t size;
    double rate;                 // bytes/s of video
    volatile int stop;
    volatile int started;        // first frame reached
    volatile double startup_s;   // open -> first frame
    volatile double stall_s;     // playback time lost waiting
    volatile int stalls;         // stall episodes >= STALL_MIN_SECS
    volatile double played_s;
    volatile double backlog_s;
    volatile int64_t off;        // bytes handed to the "demuxer"
} player;

static char g_buf[PLAY_CHUNK];

static int64_t read_at(player *p, int64_t off, int64_t len) {
    if (off + len > p->size) len = p->size - off;
    if (len <= 0) return 0;
    return torrentfs_read(p->t, off, g_buf, len);
}

static void *player_main(void *arg) {
    player *p = arg;
    double t0 = now_s();

    // mpv's probe: the head, then the tail (an mp4 moov atom), then back.
    if (read_at(p, 0, 64 * 1024) < 0) return NULL;
    int64_t tail = p->size - 2 * 1024 * 1024;
    if (tail > 0 && read_at(p, tail, 64 * 1024) < 0) return NULL;

    // Initial buffer, then the first frame.
    p->off = 0;
    while (!p->stop && p->off < p->size &&
           p->off < (int64_t)(p->rate * PLAY_START_SECS)) {
        int64_t n = read_at(p, p->off, PLAY_CHUNK);
        if (n <= 0) return NULL;
        p->off += n;
    }
    p->startup_s = now_s() - t0;
    p->started = 1;

    double play = 0, last = now_s(), episode = 0;
    while (!p->stop && p->off < p->size) {
        double n = now_s(), dt = n - last;
        last = n;
        double avail = (double)p->off / p->rate;
        double np = play + dt;
        if (np > avail) {            // the player caught up with the data
            double lost = np - avail;
            p->stall_s += lost;
            if (episode < STALL_MIN_SECS && episode + lost >= STALL_MIN_SECS)
                p->stalls++;
            episode += lost;
            np = avail;
        } else {
            episode = 0;
        }
        play = np;
        p->played_s = play;
        double backlog = avail - play;
        p->backlog_s = backlog;
        torrentfs_set_backlog(p->t, (int)(backlog * 1000.0));

        int64_t ahead = p->off - (int64_t)(play * p->rate);
        if (backlog < PLAY_AHEAD_SECS && ahead < PLAY_AHEAD_MAX) {
            int64_t got = read_at(p, p->off, PLAY_CHUNK);
            if (got <= 0) break;
            p->off += got;
        } else {
            usleep(50 * 1000);
        }
    }
    return NULL;
}

//-----------------------------------------------------------------------------

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <file.torrent|magnet:...> [seconds] [tail|stream|play[:KB/s]]\n",
                argv[0]);
        return 2;
    }
    int max_secs = argc >= 3 ? atoi(argv[2]) : 60;
    const char *mode = argc >= 4 ? argv[3] : "";
    signal(SIGINT, on_sigint);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char *cache = getenv("ENGINETEST_CACHE");
    if (!cache || !*cache) cache = "enginetest_cache";
    const char *sd = getenv("ENGINETEST_SD");
    torrentfs_set_ram_stream(sd && *sd == '1' ? 0 : 1);
    const char *lg = getenv("ENGINETEST_LOG");
    if (lg && *lg == '1') {
        torrent_set_log(log_line);
        dht_set_log(log_line);
    }

    char err[256] = {0};
    double t_open = now_s();
    printf("Opening %s ...\n", argv[1]);
    torrentfs *tfs = torrentfs_open(argv[1], cache, err, sizeof(err));
    if (!tfs) {
        fprintf(stderr, "torrentfs_open failed: %s\n", err);
        curl_global_cleanup();
        return 1;
    }
    double open_s = now_s() - t_open;

    int64_t total_bytes = torrentfs_size(tfs);
    printf("File: %.1f MB, piece %lld KB, %d peers at start, open %.1f s\n\n",
           total_bytes / (1024.0 * 1024.0),
           (long long)(torrentfs_piece_len(tfs) / 1024),
           torrentfs_peer_count(tfs), open_s);

    if (strcmp(mode, "tail") == 0) {
        int64_t off = total_bytes - 8 * 1024 * 1024;
        if (off < 0) off = 0;
        torrentfs_set_playhead(tfs, off);
        printf(">>> playhead pinned near the end (offset %.1f MB)\n\n",
               off / (1024.0 * 1024.0));
    }
    int stream_sim = strcmp(mode, "stream") == 0;
    int play_mode = strncmp(mode, "play", 4) == 0;
    int64_t plen = torrentfs_piece_len(tfs);

    player pl;
    memset(&pl, 0, sizeof(pl));
    pthread_t pth;
    int pth_ok = 0;
    if (play_mode) {
        double kbps = mode[4] == ':' ? atof(mode + 5) : 1250.0;
        if (kbps <= 0) kbps = 1250.0;
        pl.t = tfs;
        pl.size = total_bytes;
        pl.rate = kbps * 1024.0;
        pth_ok = pthread_create(&pth, NULL, player_main, &pl) == 0;
        printf(">>> player model at %.0f KB/s\n\n", kbps);
    }

    double t0 = now_s();
    int64_t start_bytes = torrentfs_bytes_recv(tfs);
    int64_t prev_bytes = start_bytes;
    int sec = 0;
    for (sec = 0; sec < max_secs && !g_stop; sec++) {
        sleep(1);

        int64_t done, total, ph;
        torrentfs_stats(tfs, &done, &total, &ph);

        if (stream_sim) {
            int64_t lowest = 0;
            while (lowest < total && torrentfs_piece_done(tfs, lowest)) lowest++;
            torrentfs_set_playhead(tfs, lowest * plen);
        }
        int c[10];
        torrentfs_debug_counts(tfs, c);
        int live = 0, peak = 0, connecting = 0;
        torrentfs_live_peers(tfs, &live, &peak, &connecting);
        int known = 0, ready = 0, dropped = 0;
        torrentfs_pool_stats(tfs, &known, &ready, &dropped);
        char last[128];
        torrentfs_last_err(tfs, last, sizeof(last));

        int64_t bytes = torrentfs_bytes_recv(tfs);
        double mbps = (bytes - prev_bytes) / (1024.0 * 1024.0);
        prev_bytes = bytes;

        printf("[%3ds] %6.2f MB/s %7.1f MB  pieces %lld/%lld ph=%lld | "
               "live %d (peak %d) conn %d | pool %d ready %d drop %d | "
               "ok %d fail %d choked %d",
               sec + 1, mbps, bytes / (1024.0 * 1024.0), (long long)done,
               (long long)total, (long long)ph, live, peak, connecting,
               known, ready, dropped, c[0], c[1], c[3]);
        if (play_mode)
            printf(" | %s buf %.1fs stalls %d (%.1fs)",
                   pl.started ? "play" : "start", pl.backlog_s, pl.stalls,
                   pl.stall_s);
        printf(" | %s\n", last[0] ? last : "-");
        fflush(stdout);
    }
    double elapsed = now_s() - t0;

    int c[10];
    torrentfs_debug_counts(tfs, c);
    int live = 0, peak = 0, connecting = 0;
    torrentfs_live_peers(tfs, &live, &peak, &connecting);
    int known = 0, ready = 0, dropped = 0;
    torrentfs_pool_stats(tfs, &known, &ready, &dropped);
    double avg = (torrentfs_bytes_recv(tfs) - start_bytes) / (1024.0 * 1024.0) /
                 (elapsed > 0 ? elapsed : 1);

    printf("\nStopping...\n");
    pl.stop = 1;
    torrentfs_cancel(tfs);
    if (pth_ok) pthread_join(pth, NULL);

    printf("SUMMARY open=%.1fs", open_s);
    if (play_mode)
        printf(" startup=%s%.1fs stalls=%d stall=%.1fs played=%.0fs",
               pl.started ? "" : ">", pl.started ? pl.startup_s : elapsed,
               pl.stalls, pl.stall_s, pl.played_s);
    printf(" avg=%.2fMB/s peak_live=%d pool=%d dropped=%d conn_ok=%d "
           "conn_fail=%d\n",
           avg, peak, known, dropped, c[0], c[1]);
    fflush(stdout);

    torrentfs_close(tfs);
    curl_global_cleanup();
    return 0;
}
