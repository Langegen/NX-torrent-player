#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

#include "jackett_diag.hpp"

namespace jackett
{

enum class Category
{
    ALL    = 0,
    MOVIES = 2000,
    SERIES = 5000,
    ANIME  = 5070,
};

enum class SortBy
{
    SEEDERS = 0,
    SIZE    = 1,
    DATE    = 2,
};

struct Item
{
    std::string title;
    std::string tracker;
    int64_t     size = 0;
    int         seeders = 0;
    int         peers = 0;
    std::string publishDate;
    std::string magnetUri;
    std::string link;
    std::string categoryDesc;
};

struct SearchResult
{
    bool ok = false;
    std::string error;
    std::vector<Item> items;
};

// Formats byte size into human-readable string (e.g. "1.4 GB").
std::string formatSize(int64_t bytes);

// Normalizes a download link from Jackett: handles localhost/relative paths
// by substituting the configured Jackett server host.
std::string normalizeDownloadLink(const std::string& link);

// Performs an asynchronous search request to Jackett.
// Callback is invoked on the UI thread (via brls::sync).
void searchAsync(const std::string& query, Category category,
                 std::function<void(SearchResult)> cb);

// Sorts items in-place according to SortBy.
void sortItems(std::vector<Item>& items, SortBy sort);

// What a Diagnosis means and what to do about it, in the UI language, for
// the server at `base`. "" for Problem::None.
std::string explain(const Diagnosis& d, const std::string& base);

// Tests connectivity with the specified URL and API key: a search, so the
// indexers are checked too. Callback is invoked on the UI thread (via
// brls::sync); on success `msg` says how many indexers answered ("Jackett
// works. Indexers: 12"), on failure what is wrong and what to do.
void testConnectionAsync(const std::string& url, const std::string& apiKey,
                         std::function<void(bool ok, const std::string& msg)> cb);

// Downloads a .torrent file from Jackett to `destPath`.
// Callback is invoked on the UI thread with (ok, errorMsg, magnet). When the
// indexer answers with a redirect to a magnet: URI instead of a file, ok is
// true, no file is written and `magnet` carries the URI.
void downloadTorrentAsync(const std::string& link, const std::string& destPath,
                          std::function<void(bool ok, const std::string& err,
                                             const std::string& magnet)> cb);

// Where the .torrent for a result's link is kept for playback (stable per
// link, under APPDATA_JACKETT). Creates the folder.
std::string cachedTorrentPath(const std::string& link);

// Deletes the files in APPDATA_JACKETT that no history entry points at: they
// are only kept so an entry can resume, and nothing else ever removes them.
// Call after history::load(), before anything can start a playback (a file
// fetched for a result being played is not in the history yet).
void pruneCache();

// Saves a torrent item to the local library (downloads .torrent to APPDATA_TORRENTS
// or appends magnet to APPDATA_MAGNETS).
void saveToLocalAsync(const Item& item,
                      std::function<void(bool ok, const std::string& msg)> cb);

} // namespace jackett
