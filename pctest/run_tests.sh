#!/bin/sh
# Builds and runs the offline engine test suite (pctest/test_suite.c).
#
# Needs a C compiler and libcurl headers (Debian/Ubuntu: build-essential
# libcurl4-openssl-dev; MSYS2/Cygwin: gcc libcurl-devel). No mbedtls: SHA-1
# comes from pctest/compat. No network: the trackers and the DHT are mocked in
# the test file itself, which is why udp_tracker.c, dht.c and dhtclient.c are
# left out here.
set -e
cd "$(dirname "$0")/.."

out="${OUT:-./pctest/test_suite}"
${CC:-gcc} -O1 -g -Wall \
    -Ipctest/compat -Iengine \
    engine/bencode.c \
    engine/torrent.c \
    engine/peer.c \
    engine/magnet.c \
    pctest/compat/sha1.c \
    pctest/utpstub.c \
    pctest/test_suite.c \
    -o "$out" \
    -lpthread -lcurl

"$out"
