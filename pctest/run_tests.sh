#!/bin/sh
# Builds and runs the offline test suites:
#   test_suite   -- the torrent engine (pctest/test_suite.c)
#   test_httpsrc -- HTTP range reads for https playback (pctest/test_httpsrc.c)
#   test_net     -- TorrServer URLs and the proxy bypass (pctest/test_net.cpp)
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

"$out/test_suite"
echo
"$out/test_httpsrc"
echo
"$out/test_net"
