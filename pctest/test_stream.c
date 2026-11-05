// Streaming loopback test for the torrent engine: what the player sees.
//
// Builds a synthetic single-file torrent, serves it from mock seeders on
// 127.0.0.10, .11, ... (one address each: the engine caps sessions per IP),
// each with its own rate limit and latency, and hands their addresses out
// through a mock UDP tracker -- the torrent's only announce URL, so the test
// needs no engine API beyond what the player uses and builds against the old
// engine too (for before/after numbers). Then it reads like mpv: a probe at
// the head, one at the tail (moov / cues), the whole file front to back, and a
// seek. Every byte is checked.
//
//   test_stream [--size MB] [--piece MB] [--seeds R1,R2,...] [--latency MS]
//               [--label TEXT] [--limit SECS]
//   R = seeder rate in KB/s, 0 = unlimited.
//
// Exit code 0 = all data correct and no read timed out.
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <mbedtls/sha1.h>

#include "torrentfs.h"

#define BLOCK      16384
#define MAX_SEEDS  8

static int64_t g_size;
static int64_t g_piece;
static int64_t g_npieces;
static uint8_t g_info_hash[20];
static volatile bool g_stop;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

// The payload: byte i of the file, cheap and position-dependent.
static inline uint8_t pat(int64_t i) {
    return (uint8_t)(((uint64_t)i * 2654435761ULL) >> 24) ^ (uint8_t)(i >> 20);
}

static void fill(uint8_t *dst, int64_t off, int64_t n) {
    for (int64_t k = 0; k < n; k++) dst[k] = pat(off + k);
}

//-----------------------------------------------------------------------------
// Mock seeder
//-----------------------------------------------------------------------------

typedef struct {
    int idx;
    int listen_fd;
    uint32_t ip;          // network order, 127.0.0.(10+idx)
    uint16_t port;
    int rate_kbs;         // 0 = unlimited
    int latency_ms;
    int64_t sent;         // payload bytes served
} seeder;

typedef struct {
    seeder *sd;
    int fd;
} conn_arg;

typedef struct { uint32_t idx, begin, len; double due; } req;

static bool read_full(int fd, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        ssize_t r = recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

static bool send_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    while (n) {
        ssize_t r = send(fd, p, n, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

static void *conn_main(void *arg) {
    conn_arg ca = *(conn_arg *)arg;
    free(arg);
    seeder *sd = ca.sd;
    int fd = ca.fd;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    uint8_t hs[68];
    if (!read_full(fd, hs, 68) || hs[0] != 19 || memcmp(hs + 28, g_info_hash, 20) != 0) {
        close(fd);
        return NULL;
    }
    uint8_t my[68];
    my[0] = 19;
    memcpy(my + 1, "BitTorrent protocol", 19);
    memset(my + 20, 0, 8);
    memcpy(my + 28, g_info_hash, 20);
    snprintf((char *)my + 48, 21, "-MK0001-%012d", sd->idx);
    if (!send_full(fd, my, 68)) { close(fd); return NULL; }

    // Bitfield (a full seed), then unchoke straight away.
    size_t bfl = (size_t)((g_npieces + 7) / 8);
    uint8_t *bf = calloc(1, 5 + bfl);
    uint32_t l = htonl((uint32_t)(1 + bfl));
    memcpy(bf, &l, 4);
    bf[4] = 5;
    for (int64_t i = 0; i < g_npieces; i++) bf[5 + i / 8] |= (uint8_t)(0x80 >> (i % 8));
    bool ok = send_full(fd, bf, 5 + bfl);
    free(bf);
    uint8_t unchoke[5] = { 0, 0, 0, 1, 1 };
    if (!ok || !send_full(fd, unchoke, 5)) { close(fd); return NULL; }

    uint8_t *rx = malloc(1 << 16);
    size_t rxn = 0;
    req *q = malloc(sizeof(req) * 4096);
    int qn = 0;
    uint8_t *msg = malloc(13 + BLOCK);
    double tokens = 64 * 1024, last = now_s();
    const double rate = sd->rate_kbs > 0 ? sd->rate_kbs * 1024.0 : 0.0;

    while (!g_stop) {
        struct pollfd p = { fd, POLLIN, 0 };
        int pr = poll(&p, 1, qn ? 1 : 20);
        if (pr < 0 && errno != EINTR) break;
        if (pr > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t r = recv(fd, rx + rxn, (1 << 16) - rxn, 0);
            if (r <= 0) break;
            rxn += (size_t)r;
            size_t off = 0;
            while (rxn - off >= 4) {
                uint32_t ml;
                memcpy(&ml, rx + off, 4);
                ml = ntohl(ml);
                if (ml > 60000) goto out;
                if (rxn - off < 4 + ml) break;
                if (ml >= 13 && rx[off + 4] == 6 && qn < 4096) {   // request
                    uint32_t v[3];
                    memcpy(v, rx + off + 5, 12);
                    q[qn].idx   = ntohl(v[0]);
                    q[qn].begin = ntohl(v[1]);
                    q[qn].len   = ntohl(v[2]);
                    q[qn].due   = now_s() + sd->latency_ms / 1000.0;
                    qn++;
                } else if (ml >= 13 && rx[off + 4] == 8) {         // cancel
                    uint32_t v[3];
                    memcpy(v, rx + off + 5, 12);
                    for (int k = 0; k < qn; k++)
                        if (q[k].idx == ntohl(v[0]) && q[k].begin == ntohl(v[1])) {
                            memmove(q + k, q + k + 1, sizeof(req) * (size_t)(qn - k - 1));
                            qn--;
                            break;
                        }
                }
                off += 4 + ml;
            }
            memmove(rx, rx + off, rxn - off);
            rxn -= off;
        }
        // Serve what is due, within the rate budget.
        double t = now_s();
        if (rate > 0) {
            tokens += (t - last) * rate;
            if (tokens > 64 * 1024) tokens = 64 * 1024;
        }
        last = t;
        while (qn > 0 && q[0].due <= t && (rate == 0 || tokens >= q[0].len)) {
            req r = q[0];
            memmove(q, q + 1, sizeof(req) * (size_t)(qn - 1));
            qn--;
            int64_t abs = (int64_t)r.idx * g_piece + r.begin;
            if (r.len > BLOCK || abs + r.len > g_size) goto out;
            uint32_t v = htonl(9 + r.len);
            memcpy(msg, &v, 4);
            msg[4] = 7;
            v = htonl(r.idx);   memcpy(msg + 5, &v, 4);
            v = htonl(r.begin); memcpy(msg + 9, &v, 4);
            fill(msg + 13, abs, r.len);
            if (!send_full(fd, msg, 13 + r.len)) goto out;
            __atomic_add_fetch(&sd->sent, (int64_t)r.len, __ATOMIC_RELAXED);
            if (rate > 0) tokens -= r.len;
        }
    }
out:
    free(rx);
    free(q);
    free(msg);
    close(fd);
    return NULL;
}

static void *seeder_main(void *arg) {
    seeder *sd = arg;
    while (!g_stop) {
        struct pollfd p = { sd->listen_fd, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) continue;
        int fd = accept(sd->listen_fd, NULL, NULL);
        if (fd < 0) continue;
        conn_arg *ca = malloc(sizeof(*ca));
        ca->sd = sd;
        ca->fd = fd;
        pthread_t th;
        if (pthread_create(&th, NULL, conn_main, ca) == 0) pthread_detach(th);
        else { close(fd); free(ca); }
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// Mock UDP tracker (BEP 15): hands out every seeder.
//-----------------------------------------------------------------------------

static seeder g_seeds[MAX_SEEDS];
static int g_nseeds;
static int g_trk_fd;

static void *tracker_main(void *arg) {
    (void)arg;
    uint8_t b[2048];
    while (!g_stop) {
        struct pollfd p = { g_trk_fd, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) continue;
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(g_trk_fd, b, sizeof(b), 0, (struct sockaddr *)&from, &fl);
        if (n < 16) continue;
        uint32_t action, txid;
        memcpy(&action, b + 8, 4);
        memcpy(&txid, b + 12, 4);
        action = ntohl(action);
        uint8_t out[20 + 6 * MAX_SEEDS];
        size_t ol;
        if (action == 0) {                  // connect
            uint32_t a = htonl(0);
            memcpy(out, &a, 4);
            memcpy(out + 4, &txid, 4);
            memset(out + 8, 0x42, 8);       // connection id
            ol = 16;
        } else if (action == 1 && n >= 98) { // announce
            uint32_t v = htonl(1);
            memcpy(out, &v, 4);
            memcpy(out + 4, &txid, 4);
            v = htonl(1800); memcpy(out + 8, &v, 4);
            v = htonl(0);    memcpy(out + 12, &v, 4);
            v = htonl((uint32_t)g_nseeds); memcpy(out + 16, &v, 4);
            for (int i = 0; i < g_nseeds; i++) {
                memcpy(out + 20 + 6 * i, &g_seeds[i].ip, 4);
                uint16_t pt = htons(g_seeds[i].port);
                memcpy(out + 24 + 6 * i, &pt, 2);
            }
            ol = 20 + 6 * (size_t)g_nseeds;
        } else {
            continue;
        }
        sendto(g_trk_fd, out, ol, 0, (struct sockaddr *)&from, fl);
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// Torrent file
//-----------------------------------------------------------------------------

static bool write_torrent(const char *path, int trk_port) {
    char head[256];
    int hl = snprintf(head, sizeof(head),
                      "d6:lengthi%llde4:name8:test.mkv12:piece lengthi%llde6:pieces%lld:",
                      (long long)g_size, (long long)g_piece, (long long)(g_npieces * 20));
    size_t info_len = (size_t)hl + (size_t)g_npieces * 20 + 1;
    uint8_t *info = malloc(info_len);
    memcpy(info, head, (size_t)hl);
    uint8_t *buf = malloc((size_t)g_piece);
    for (int64_t i = 0; i < g_npieces; i++) {
        int64_t off = i * g_piece;
        int64_t n = off + g_piece > g_size ? g_size - off : g_piece;
        fill(buf, off, n);
        mbedtls_sha1(buf, (size_t)n, info + hl + i * 20);
    }
    free(buf);
    info[info_len - 1] = 'e';
    mbedtls_sha1(info, info_len, g_info_hash);

    char ann[64];
    int al = snprintf(ann, sizeof(ann), "udp://127.0.0.1:%d/announce", trk_port);
    FILE *f = fopen(path, "wb");
    if (!f) { free(info); return false; }
    fprintf(f, "d8:announce%d:%s4:info", al, ann);
    fwrite(info, 1, info_len, f);
    fputc('e', f);
    fclose(f);
    free(info);
    return true;
}

//-----------------------------------------------------------------------------
// The reader (mpv's side)
//-----------------------------------------------------------------------------

static torrentfs *g_tfs;
static double g_deadline;   // absolute: cancel the engine past this

static void *watchdog_main(void *arg) {
    (void)arg;
    while (!g_stop) {
        if (now_s() > g_deadline && g_tfs) {
            torrentfs_cancel(g_tfs);
            return NULL;
        }
        usleep(50000);
    }
    return NULL;
}

static long g_bad;

// Reads [off, off+n) fully (the engine returns short reads). False on error.
static bool read_at(int64_t off, int64_t n, uint8_t *buf) {
    int64_t got = 0;
    while (got < n) {
        int64_t r = torrentfs_read(g_tfs, off + got, (char *)buf + got, n - got);
        if (r <= 0) return false;
        got += r;
    }
    for (int64_t k = 0; k < n; k++)
        if (buf[k] != pat(off + k)) g_bad++;
    return true;
}

// The engine keeps files relative to the working directory on a PC (the DHT
// node cache under ./appdata): run in a scratch directory and remove it.
#define SCRATCH_DIR "test_stream_tmp"
static void scratch_cleanup(void) {
    remove("appdata/cache/dht/dht_cache.bin");
    rmdir("appdata/cache/dht");
    rmdir("appdata/cache/meta");
    rmdir("appdata/cache");
    rmdir("appdata");
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    mkdir(SCRATCH_DIR, 0777);
    if (chdir(SCRATCH_DIR) != 0) { fprintf(stderr, "chdir failed\n"); return 2; }
    int size_mb = 96, piece_mb = 4, latency = 0, limit = 120;
    const char *seeds = "0,0,0";
    const char *label = "";
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--size")) size_mb = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--piece")) piece_mb = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seeds")) seeds = argv[i + 1];
        else if (!strcmp(argv[i], "--latency")) latency = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--label")) label = argv[i + 1];
        else if (!strcmp(argv[i], "--limit")) limit = atoi(argv[i + 1]);
    }
    // A short last piece on purpose: + 123 KB.
    g_size = (int64_t)size_mb * 1024 * 1024 + 123 * 1024;
    g_piece = (int64_t)piece_mb * 1024 * 1024;
    g_npieces = (g_size + g_piece - 1) / g_piece;

    // Seeders.
    const char *p = seeds;
    while (*p && g_nseeds < MAX_SEEDS) {
        seeder *sd = &g_seeds[g_nseeds];
        sd->idx = g_nseeds;
        sd->rate_kbs = atoi(p);
        sd->latency_ms = latency;
        sd->ip = htonl(0x7F00000A + (uint32_t)g_nseeds);   // 127.0.0.10 + i
        sd->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(sd->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in sa = { 0 };
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = sd->ip;
        sa.sin_port = 0;
        if (bind(sd->listen_fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
            listen(sd->listen_fd, 16) != 0) {
            fprintf(stderr, "seeder %d: bind failed (%s)\n", g_nseeds, strerror(errno));
            return 2;
        }
        socklen_t sl = sizeof(sa);
        getsockname(sd->listen_fd, (struct sockaddr *)&sa, &sl);
        sd->port = ntohs(sa.sin_port);
        g_nseeds++;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }

    // Tracker.
    g_trk_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in ta = { 0 };
    ta.sin_family = AF_INET;
    ta.sin_addr.s_addr = htonl(0x7F000001);
    bind(g_trk_fd, (struct sockaddr *)&ta, sizeof(ta));
    socklen_t tl = sizeof(ta);
    getsockname(g_trk_fd, (struct sockaddr *)&ta, &tl);

    char tpath[64], cpath[64];
    snprintf(tpath, sizeof(tpath), "test_stream_%d.torrent", (int)getpid());
    snprintf(cpath, sizeof(cpath), "test_stream_%d.cache", (int)getpid());
    if (!write_torrent(tpath, ntohs(ta.sin_port))) { fprintf(stderr, "torrent write failed\n"); return 2; }

    pthread_t th;
    pthread_create(&th, NULL, tracker_main, NULL);
    pthread_detach(th);
    for (int i = 0; i < g_nseeds; i++) {
        pthread_create(&th, NULL, seeder_main, &g_seeds[i]);
        pthread_detach(th);
    }

    // The player's settings: RAM streaming, the standard 32 MB window.
    torrentfs_set_ram_stream(1);
    torrentfs_set_stream_window(32LL * 1024 * 1024);

    char err[256] = { 0 };
    double t0 = now_s();
    g_deadline = t0 + limit;
    g_tfs = torrentfs_open(tpath, cpath, err, sizeof(err));
    if (!g_tfs) { fprintf(stderr, "torrentfs_open: %s\n", err); return 2; }
    pthread_create(&th, NULL, watchdog_main, NULL);
    pthread_detach(th);

    uint8_t *buf = malloc(1 << 20);
    bool ok = true;
    double t_head = -1, t_tail = -1, t_8mb = -1, t_full = -1, t_seek = -1;

    // mpv's probe: the head, then the index at the tail.
    ok = ok && read_at(0, 32 * 1024, buf);
    t_head = now_s() - t0;
    int64_t tail_off = g_size - 1024 * 1024;
    ok = ok && read_at(tail_off, 64 * 1024, buf);
    t_tail = now_s() - t0;

    // Then front to back, as fast as the engine delivers.
    double t_seq0 = now_s();
    for (int64_t off = 0; ok && off < g_size; off += 512 * 1024) {
        int64_t n = g_size - off < 512 * 1024 ? g_size - off : 512 * 1024;
        ok = read_at(off, n, buf);
        if (ok && t_8mb < 0 && off + n >= 8LL * 1024 * 1024) t_8mb = now_s() - t0;
    }
    if (ok) t_full = now_s() - t_seq0;

    // A seek back into the middle (in RAM mode: possibly evicted, re-fetched).
    if (ok) {
        double ts = now_s();
        ok = read_at(g_size * 6 / 10, 256 * 1024, buf);
        if (ok) t_seek = now_s() - ts;
    }

    int64_t sent = 0;
    for (int i = 0; i < g_nseeds; i++) sent += g_seeds[i].sent;
    double mbs = t_full > 0 ? (double)g_size / (1024.0 * 1024.0) / t_full : 0;
    printf("%-34s head %5.2fs  tail %5.2fs  first8MB %5.2fs  full %5.1fs (%6.1f MB/s)  "
           "seek %5.2fs  served/size %.2f  data %s%s\n",
           label, t_head, t_tail, t_8mb, t_full, mbs, t_seek,
           (double)sent / (double)g_size, g_bad ? "BAD" : "ok",
           ok ? "" : "  TIMEOUT/ERROR");
    fflush(stdout);

    g_stop = true;
    torrentfs_cancel(g_tfs);
    torrentfs_close(g_tfs);
    free(buf);
    remove(tpath);
    scratch_cleanup();
    if (chdir("..") == 0) rmdir(SCRATCH_DIR);
    return (ok && g_bad == 0) ? 0 : 1;
}
