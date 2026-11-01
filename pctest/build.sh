#!/bin/sh
# Builds the PC engine test (pctest/main.c, see its header for the modes).
#
# Linux: build-essential libmbedtls-dev libcurl4-openssl-dev zlib1g-dev.
# Windows: the devkitPro MSYS2 shell with its mingw64 toolchain (mbedtls and
# curl included), e.g.
#   C:\devkitPro\msys2\usr\bin\bash.exe -lc "PATH=/mingw64/bin:\$PATH sh pctest/build.sh"
# The Windows build uses the desktop app's compat/ layer (winsock) instead of
# pctest/compat, and needs /mingw64/bin on PATH to run.
set -e
cd "$(dirname "$0")/.."

case "$(uname -s)" in
    MINGW*|MSYS*) compat=compat; out=pctest/enginetest.exe; extra=-lws2_32 ;;
    *)            compat=pctest/compat; out=pctest/enginetest; extra= ;;
esac

gcc -O2 -g -Wall \
    -I$compat -Iengine \
    engine/bencode.c \
    engine/torrent.c \
    engine/udp_tracker.c \
    engine/peer.c \
    engine/torrentfs.c \
    engine/magnet.c \
    engine/dht.c \
    engine/dhtclient.c \
    pctest/utpstub.c \
    pctest/main.c \
    -o $out \
    -lpthread -lmbedcrypto -lmbedtls -lmbedx509 -lcurl -lz $extra

echo "OK -> $out"
