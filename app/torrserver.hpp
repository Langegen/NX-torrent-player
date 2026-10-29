#pragma once

#include <functional>
#include <string>

// TorrServer (github.com/YouROK/TorrServer): a torrent client on another
// machine that serves a torrent's file over HTTP while it downloads. With its
// address in Options, the player hands it the magnet and plays the HTTP stream
// instead of running the on-device engine -- a PC or NAS has far more
// bandwidth, peers and storage to stream from than the console.
//
// API used: GET /echo (version, for "Test connection"),
//   /stream?link=<magnet>&stat          -> JSON, with "file_stats":[{id,path,length}]
//   /stream?link=<magnet>&index=<id>&play -> the file itself, range requests ok
// File ids are 1-based, in the torrent's own file order -- the order the
// engine's file index counts in, so engine index i is TorrServer id i+1.
namespace torrserver
{

// "192.168.1.10:8090/" -> "http://192.168.1.10:8090". "" stays "".
std::string normalizeBase(std::string url);

// The configured server, normalized; "" when TorrServer is off.
std::string base();

std::string statUrl(const std::string& base, const std::string& link);
std::string playUrl(const std::string& base, const std::string& link, int id);

// The TorrServer file id to play. `fileIndex` >= 0 (engine order) maps to
// fileIndex + 1 without looking at the list; -1 picks the largest video file
// in `statJson` (else the largest file). -1 when there is nothing to pick.
int pickFile(const std::string& statJson, int fileIndex);

// Blocking (call off the UI thread): the URL to play `link` (a magnet) through
// the server. With fileIndex -1 this asks the server for the file list first,
// which waits until it has the torrent's metadata.
bool resolve(const std::string& base, const std::string& link, int fileIndex,
             std::string& url, std::string& err);

// GET /echo; the callback runs on the UI thread with the server's version.
void testConnectionAsync(const std::string& base,
                         std::function<void(bool ok, const std::string& msg)> cb);

} // namespace torrserver
