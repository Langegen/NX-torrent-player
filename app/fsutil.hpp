#pragma once

// Small file helpers for the app's own data (config, library, history,
// lists). Header-only: the PC test builds (pctest/run_tests.sh) compile a
// few app files on their own and need nothing extra to link.

#include <cstdio>
#include <functional>
#include <string>

namespace fsutil
{

// The whole file into `out`. False if it cannot be opened.
inline bool readFile(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[8192];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Replaces `path` with `data` so that a power cut or a full SD card never
// leaves a truncated file behind: written to path.tmp and checked (fwrite,
// fflush, fclose), then renamed in. The previous version stays as path.bak.
// False on any failure, with the old file still in place.
inline bool writeAtomic(const std::string& path, const std::string& data)
{
    std::string tmp = path + ".tmp";
    FILE* f         = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok      = std::fflush(f) == 0 && ok;
    ok      = std::fclose(f) == 0 && ok;
    if (!ok)
    {
        std::remove(tmp.c_str());
        return false;
    }
    // FAT cannot rename over an existing file: move the old one aside first.
    // A cut between the two renames leaves path missing and path.tmp whole,
    // which readFirst() then picks up.
    std::string bak = path + ".bak";
    std::remove(bak.c_str());
    bool hadOld = std::rename(path.c_str(), bak.c_str()) == 0;
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        if (hadOld) std::rename(bak.c_str(), path.c_str());
        return false;
    }
    return true;
}

// The first of path, path.tmp (a save cut between its renames) and path.bak
// (the version before the last save) that opens and passes `valid` (when
// given). False if none does.
inline bool readFirst(const std::string& path, std::string& out,
                      const std::function<bool(const std::string&)>& valid = nullptr)
{
    const std::string candidates[] = { path, path + ".tmp", path + ".bak" };
    for (const auto& p : candidates)
        if (readFile(p, out) && (!valid || valid(out))) return true;
    out.clear();
    return false;
}

// A JSON document that was written to the end: its last non-space character
// closes it. A file cut short by a lost write fails this.
inline bool looksComplete(const std::string& body, char last = '}')
{
    for (size_t i = body.size(); i > 0; i--)
    {
        char c = body[i - 1];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        return c == last;
    }
    return false;
}

} // namespace fsutil
