#!/bin/sh
# Builds and runs the offline test suites:
#   test_suite   -- the torrent engine: magnet metadata (pctest/test_suite.c)
#   test_stream  -- the torrent engine streaming from mock seeders, read the
#                   way mpv reads (pctest/test_stream.c), several swarm shapes
#   test_httpsrc -- HTTP range reads for https playback (pctest/test_httpsrc.c)
#   test_net     -- TorrServer URLs and the proxy bypass (pctest/test_net.cpp)
#   test_tmdb    -- TMDB answers and the JSON helpers (pctest/test_tmdb.cpp)
#   test_release -- release names, ranking, episode files (pctest/test_release.cpp)
#   test_library -- the library's JSON, Continue, import merge (pctest/test_library.cpp)
#   test_jackett -- Jackett errors explained, typed addresses (pctest/test_jackett.cpp)
#
# Needs a C compiler and libcurl headers (Debian/Ubuntu: build-essential
# libcurl4-openssl-dev; MSYS2/Cygwin: gcc libcurl-devel). No mbedtls: SHA-1
# comes from pctest/compat. No network: the trackers and the DHT are mocked in
# test_suite.c itself (which is why udp_tracker.c, dht.c and dhtclient.c are
# left out), and the suites talk to mock servers on 127.0.0.1 or mock http.
# test_stream links the real tracker client and DHT: its torrent announces to a
# mock UDP tracker on 127.0.0.1, and the DHT finding nothing is harmless.
# µTP and MSE are stubbed on the PC (pctest/nbstub.c, utpstub.c).
set -e
cd "$(dirname "$0")/.."

out="${OUT:-./pctest}"
cc="${CC:-gcc}"

$cc -O1 -g -Wall \
    -Ipctest/compat -Iengine -Iinclude \
    engine/bencode.c \
    engine/torrent_meta.c \
    engine/bt_peer.c \
    engine/magnet.c \
    engine/engine_log.c \
    pctest/compat/sha1.c \
    pctest/utpstub.c \
    pctest/nbstub.c \
    pctest/test_suite.c \
    -o "$out/test_suite" \
    -lpthread -lcurl

$cc -O2 -g -Wall \
    -Ipctest/compat -Iengine -Iinclude \
    engine/bencode.c \
    engine/torrent_meta.c \
    engine/torrentfs.c \
    engine/bt_peer.c \
    engine/magnet.c \
    engine/udp_tracker.c \
    engine/dht.c \
    engine/dhtclient.c \
    engine/listen.c \
    engine/engine_log.c \
    pctest/compat/sha1.c \
    pctest/utpstub.c \
    pctest/nbstub.c \
    pctest/test_stream.c \
    -o "$out/test_stream" \
    -lpthread -lcurl

$cc -O1 -g -Wall \
    -Iengine \
    engine/httpsrc.c \
    pctest/test_httpsrc.c \
    -o "$out/test_httpsrc" \
    -lpthread -lcurl

${CXX:-g++} -O1 -g -Wall -std=c++17 \
    -DTORRSERVER_NO_UI -Iapp \
    app/torrserver.cpp \
    app/json.cpp \
    pctest/test_net.cpp \
    -o "$out/test_net"

${CXX:-g++} -O1 -g -Wall -std=c++17 \
    -DTMDB_NO_UI -Iapp \
    app/tmdb.cpp \
    app/json.cpp \
    pctest/test_tmdb.cpp \
    -o "$out/test_tmdb"

${CXX:-g++} -O1 -g -Wall -std=c++17 \
    -Iapp \
    app/release.cpp \
    pctest/test_release.cpp \
    -o "$out/test_release"

${CXX:-g++} -O1 -g -Wall -std=c++17 \
    -DLIBRARY_NO_UI -Iapp \
    app/library.cpp \
    app/json.cpp \
    pctest/test_library.cpp \
    -o "$out/test_library"

${CXX:-g++} -O1 -g -Wall -std=c++17 \
    -Iapp \
    app/jackett_diag.cpp \
    app/json.cpp \
    pctest/test_jackett.cpp \
    -o "$out/test_jackett"

"$out/test_suite"
echo
# Streaming: head/tail = mpv's probe (container header, then the index at the
# end), first8MB ~ playback can start, full = the whole file front to back.
# Rates in KB/s (0 = unlimited); slow peers must not hold up the fast one.
"$out/test_stream" --size 64 --piece 4 --seeds 0,0,0 --label "4MB pieces, 3 fast seeds"
"$out/test_stream" --size 64 --piece 16 --seeds 4096,150,150,150 --latency 60 \
    --label "16MB pieces, 1 fast + 3 slow"
"$out/test_stream" --size 64 --piece 2 --seeds 800,800,800,800,800,800 --latency 80 \
    --label "2MB pieces, 6 x 800 KB/s"
"$out/test_stream" --size 400 --piece 8 --seeds 0,0,0,0 \
    --label "8MB pieces, 400 MB (RAM window evicts)"
echo
"$out/test_httpsrc"
echo
"$out/test_net"
echo
"$out/test_tmdb"
echo
"$out/test_release"
echo
"$out/test_library"
echo
"$out/test_jackett"
