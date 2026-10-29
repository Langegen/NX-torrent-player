#!/bin/sh
# Builds and runs the offline test suites:
#   test_suite   -- the torrent engine (pctest/test_suite.c)
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
set -e
cd "$(dirname "$0")/.."

out="${OUT:-./pctest}"
cc="${CC:-gcc}"

$cc -O1 -g -Wall \
    -Ipctest/compat -Iengine \
    engine/bencode.c \
    engine/torrent.c \
    engine/peer.c \
    engine/magnet.c \
    pctest/compat/sha1.c \
    pctest/utpstub.c \
    pctest/test_suite.c \
    -o "$out/test_suite" \
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
