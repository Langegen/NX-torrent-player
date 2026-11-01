#pragma once

#include <cstdint>
#include <functional>
#include <string>

// The app's HTTP calls. Blocking: every caller here already runs them off the
// UI thread (workers::run, brls::async) and syncs the result back.
//
// TLS certificates ARE verified, against a CA store the app ships itself at
// romfs:/cacert.pem -- the console mounts none of its own. A rejected chain on
// device usually means the clock, not an attacker: see verifyTls/tlsAwareErr in
// http.cpp. The tracker announces in engine/torrent.c deliberately stay
// unverified; the comment there says why.
namespace http
{

// Stops every transfer in flight, and any started after, as the app quits.
void abortAll();

// GET `url` into `resp`. False on failure, with a readable reason in `err`.
// `accept` is an optional full header line ("Accept: image/jpeg").
// `timeoutSec` bounds the whole request (TorrServer answers only once it has
// a torrent's metadata, which can take longer than the default). `status`,
// when given, receives the HTTP status code (0 when no answer came); an error
// status still returns true with its body, as it always has.
bool get(const std::string& url, std::string& resp, std::string& err,
         const char* accept = nullptr, long timeoutSec = 25, long* status = nullptr);

// POST `body` as application/json.
bool postJson(const char* url, const std::string& body, std::string& resp,
              std::string& err);

// GET `url` straight to `path`, so a large file never sits in RAM. `progress`
// is called from the transfer thread with (bytes so far, total or 0 if the
// server did not say); returning false from it aborts the download. The file is
// removed on failure, so a half-written one is never left behind.
bool download(const std::string& url, const std::string& path, std::string& err,
              std::function<bool(int64_t, int64_t)> progress = nullptr);

// Fetches a .torrent to `path`, for indexer links (Jackett's /dl/...). Many of
// those answer with a redirect to a magnet: URI instead of a file, which curl
// cannot follow; that case returns true with the URI in `magnet` and no file.
// Otherwise true means the file is on disk and `magnet` is empty.
bool downloadTorrent(const std::string& url, const std::string& path,
                     std::string& magnet, std::string& err);

// Percent-encodes a URL path segment.
std::string urlEncode(const std::string& s);

// Re-reads the proxy from config (Options -> Network). Call after load and
// after every change.
void applyProxy();

// The proxy to use for `url`: the configured one, or "" for a host on the
// local network (Jackett, TorrServer, a NAS) and when none is set.
std::string proxyFor(const std::string& url);

} // namespace http
