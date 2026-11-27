#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace online
{

struct OnlineQuality
{
    std::string label;  // "1080p", "720p", "Auto", ...
    std::string url;
    int height = 0;     // 2160, 1080, 720, 480, 360, or 0 if unknown
};

struct OnlineSubtitle
{
    std::string label;
    std::string lang;
    std::string url;
};

enum class OnlineMethod
{
    Play,
    Call,
    Link,
};

struct OnlineEntry
{
    OnlineMethod method = OnlineMethod::Play;
    std::string title;
    std::string details;
    std::string voiceName;
    int season  = 0;
    int episode = 0;
    std::string url;
    std::string maxQuality;
    std::vector<OnlineQuality> qualities;
    std::vector<OnlineSubtitle> subtitles;
    // Formatted as "Header-Name: value" for curl / mpv.
    std::vector<std::string> headers;
};

struct BalancerInfo
{
    std::string pluginId;
    std::string id;
    std::string name;
    std::string url;
    bool available   = true;
    bool accountOnly = false;
    bool bound       = false;
    bool avoid       = false;
    std::string status;
};

struct VoiceOption
{
    std::string name;
    std::string url;
    bool active = false;
};

struct SeasonOption
{
    int number       = 0;
    int episodeCount = 0;
    std::string name;
    std::string url;
    bool active = false;
};

enum class PageType
{
    Empty,
    Movie,
    Seasons,
    Episodes,
    Similar,
};

struct PageResult
{
    bool ok = false;
    std::string error;
    PageType type       = PageType::Empty;
    // True when a balancer (such as BWA) returns voice streams for a single
    // requested (season, episode) rather than a list of episodes.
    bool perEpisodeMode = false;
    std::vector<VoiceOption> voices;
    std::vector<SeasonOption> seasons;
    std::vector<OnlineEntry> entries;
};

struct OnlineRef
{
    std::string pluginId;
    std::string balancerId;
    std::string voiceName;
};

inline int parseQualityHeight(const std::string& label)
{
    std::string low;
    for (char c : label) low += (char)std::tolower((unsigned char)c);
    if (low.find("2160") != std::string::npos || low.find("4k") != std::string::npos ||
        low.find("uhd") != std::string::npos)
        return 2160;
    if (low.find("1440") != std::string::npos || low.find("2k") != std::string::npos)
        return 1440;
    if (low.find("1080") != std::string::npos || low.find("fhd") != std::string::npos)
        return 1080;
    if (low.find("720") != std::string::npos || low.find("hd") != std::string::npos)
        return 720;
    if (low.find("480") != std::string::npos) return 480;
    if (low.find("360") != std::string::npos) return 360;
    if (low.find("240") != std::string::npos) return 240;
    // Extract first multi-digit number
    for (size_t i = 0; i < low.size(); i++)
    {
        if (low[i] >= '1' && low[i] <= '9')
        {
            int v = 0;
            while (i < low.size() && low[i] >= '0' && low[i] <= '9')
            {
                v = v * 10 + (low[i] - '0');
                i++;
            }
            if (v >= 144 && v <= 4320) return v;
        }
    }
    return 0;
}

inline void sortQualitiesDesc(std::vector<OnlineQuality>& qs)
{
    std::stable_sort(qs.begin(), qs.end(), [](const OnlineQuality& a, const OnlineQuality& b) {
        return a.height > b.height;
    });
}

inline int pickBestQualityIndex(const std::vector<OnlineQuality>& qs, int preferHeight, bool hide4k)
{
    if (qs.empty()) return -1;
    std::vector<int> candidates;
    for (int i = 0; i < (int)qs.size(); i++)
    {
        if (hide4k && qs[(size_t)i].height >= 2160) continue;
        candidates.push_back(i);
    }
    if (candidates.empty())
    {
        for (int i = 0; i < (int)qs.size(); i++) candidates.push_back(i);
    }

    if (preferHeight <= 0)
    {
        int best = candidates[0];
        for (int idx : candidates)
            if (qs[(size_t)idx].height > qs[(size_t)best].height) best = idx;
        return best;
    }

    int bestLe = -1;
    for (int idx : candidates)
    {
        int h = qs[(size_t)idx].height;
        if (h > 0 && h <= preferHeight)
        {
            if (bestLe < 0 || h > qs[(size_t)bestLe].height) bestLe = idx;
        }
    }
    if (bestLe >= 0) return bestLe;

    int bestMin = candidates[0];
    for (int idx : candidates)
    {
        int h  = qs[(size_t)idx].height;
        int bh = qs[(size_t)bestMin].height;
        if (bh == 0 || (h > 0 && h < bh)) bestMin = idx;
    }
    return bestMin;
}

inline std::string pickBestQuality(const OnlineEntry& entry, int preferHeight, bool hide4k,
                                   std::string* chosenLabelOut = nullptr)
{
    if (!entry.qualities.empty())
    {
        int idx = pickBestQualityIndex(entry.qualities, preferHeight, hide4k);
        if (idx >= 0 && idx < (int)entry.qualities.size())
        {
            if (chosenLabelOut) *chosenLabelOut = entry.qualities[(size_t)idx].label;
            if (!entry.qualities[(size_t)idx].url.empty())
                return entry.qualities[(size_t)idx].url;
        }
    }
    if (chosenLabelOut) *chosenLabelOut = entry.maxQuality;
    return entry.url;
}

inline std::string encodePct(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

inline std::string decodePct(const std::string& s)
{
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        else if (s[i] == '+')
        {
            out += ' ';
            continue;
        }
        out += s[i];
    }
    return out;
}

inline std::string makeOnlineSourceUri(const OnlineRef& ref)
{
    std::string u = "online://" + encodePct(ref.pluginId) + "/" + encodePct(ref.balancerId);
    if (!ref.voiceName.empty()) u += "?voice=" + encodePct(ref.voiceName);
    return u;
}

inline bool parseOnlineSourceUri(const std::string& uri, OnlineRef& out)
{
    static const char prefix[] = "online://";
    if (uri.rfind(prefix, 0) != 0) return false;
    out = OnlineRef();
    std::string rest = uri.substr(sizeof(prefix) - 1);
    size_t q = rest.find('?');
    std::string path = (q == std::string::npos) ? rest : rest.substr(0, q);
    std::string query = (q == std::string::npos) ? "" : rest.substr(q + 1);

    size_t slash = path.find('/');
    if (slash == std::string::npos)
    {
        out.pluginId = decodePct(path);
    }
    else
    {
        out.pluginId   = decodePct(path.substr(0, slash));
        out.balancerId = decodePct(path.substr(slash + 1));
    }

    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == "voice")
            out.voiceName = decodePct(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return !out.pluginId.empty();
}

} // namespace online
