#include "release.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace release
{

namespace
{

// ASCII and Cyrillic to lower case (UTF-8): "СЕЗОН" and "Сезон" read the same.
std::string lower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80)
        {
            out += (char)std::tolower(c);
            continue;
        }
        if (c == 0xD0 && i + 1 < s.size())
        {
            unsigned char d = (unsigned char)s[i + 1];
            if (d >= 0x90 && d <= 0x9F)  // А..П -> а..п
            {
                out += (char)0xD0;
                out += (char)(d + 0x20);
                i++;
                continue;
            }
            if (d >= 0xA0 && d <= 0xAF)  // Р..Я -> р..я
            {
                out += (char)0xD1;
                out += (char)(d - 0x20);
                i++;
                continue;
            }
            if (d == 0x81)  // Ё -> ё
            {
                out += (char)0xD1;
                out += (char)0x91;
                i++;
                continue;
            }
        }
        out += (char)c;
    }
    return out;
}

// Separators turned into spaces, padded: word lookups become " word ".
std::string words(const std::string& low)
{
    std::string out = " ";
    for (char c : low)
    {
        if (std::strchr("._-[](){}|/\\,+:;!?\"'*", c) || c == ' ' || c == '\t')
        {
            if (out.back() != ' ') out += ' ';
        }
        else
            out += c;
    }
    if (out.back() != ' ') out += ' ';
    return out;
}

bool hasWord(const std::string& w, const char* word)
{
    return w.find(std::string(" ") + word + " ") != std::string::npos;
}

bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Digits at s[i..], at most `max` of them; -1 when none. Advances i.
int readNum(const std::string& s, size_t& i, int max = 3)
{
    int n = 0, k = 0;
    while (i < s.size() && k < max && std::isdigit((unsigned char)s[i]))
    {
        n = n * 10 + (s[i] - '0');
        i++;
        k++;
    }
    if (k == 0) return -1;
    // More digits follow: a year or a resolution, not a season number.
    if (i < s.size() && std::isdigit((unsigned char)s[i])) return -1;
    return n;
}

void skipSpaces(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == ':' || s[i] == '.' || s[i] == '_'))
        i++;
}

// "1-8", "1–8" (an en dash), "1 - 8": the end of a range after a number.
int readRangeEnd(const std::string& s, size_t& i)
{
    size_t j = i;
    while (j < s.size() && s[j] == ' ') j++;
    if (j < s.size() && s[j] == '-')
        j++;
    else if (s.compare(j, 3, "\xE2\x80\x93") == 0)  // –
        j += 3;
    else
        return -1;
    while (j < s.size() && s[j] == ' ') j++;
    if (j < s.size() && (s[j] == 'e' || s[j] == 's')) j++;  // "E01-E08", "S01-S03"
    int n = readNum(s, j);
    if (n >= 0) i = j;
    return n;
}

// The number just before `pos` ("1 сезон"), or -1.
int numberBefore(const std::string& s, size_t pos)
{
    size_t j = pos;
    while (j > 0 && s[j - 1] == ' ') j--;
    size_t end = j;
    while (j > 0 && std::isdigit((unsigned char)s[j - 1])) j--;
    if (j == end || end - j > 2) return -1;
    return std::atoi(s.substr(j, end - j).c_str());
}

bool isLetterAt(const std::string& s, size_t i)
{
    return i < s.size() &&
           (std::isalpha((unsigned char)s[i]) || (unsigned char)s[i] >= 0xD0);
}

// The word stem at s[k..k+len), as a whole word: no letter before it, at most
// one ending letter after it ("сезоны", "серии", "seasons"), then no more.
// `end` is set past the ending.
bool wordAt(const std::string& s, size_t k, size_t len, size_t& end)
{
    if (k > 0 && (std::isalpha((unsigned char)s[k - 1]) || (unsigned char)s[k - 1] >= 0x80))
        return false;
    size_t j = k + len;
    if (j < s.size() && std::isalpha((unsigned char)s[j]))
        j++;                                        // "s"
    else if (j + 1 < s.size() && (unsigned char)s[j] >= 0xD0)
        j += 2;                                     // one Cyrillic letter
    if (isLetterAt(s, j)) return false;
    end = j;
    return true;
}

// Seasons and episodes, in the spellings trackers use.
void readScope(const std::string& low, Info& in)
{
    // SxxEyy, SxxEyy-Ezz, Sxx-Syy, Sxx
    for (size_t i = 0; i + 1 < low.size() && in.season < 0; i++)
    {
        if (low[i] != 's' || !std::isdigit((unsigned char)low[i + 1])) continue;
        if (i > 0 && std::isalnum((unsigned char)low[i - 1])) continue;
        size_t j = i + 1;
        int s = readNum(low, j, 2);
        if (s < 0) continue;
        in.season = in.seasonTo = s;
        if (j < low.size() && low[j] == 'e' && j + 1 < low.size() &&
            std::isdigit((unsigned char)low[j + 1]))
        {
            j++;
            int e = readNum(low, j);
            in.episode = in.episodeTo = e;
            int to = readRangeEnd(low, j);
            if (to >= 0) in.episodeTo = to;
        }
        else
        {
            int to = readRangeEnd(low, j);
            if (to >= 0) in.seasonTo = to;
        }
    }

    // 1x06
    if (in.season < 0)
        for (size_t i = 1; i + 2 < low.size(); i++)
        {
            if (low[i] != 'x' || !std::isdigit((unsigned char)low[i - 1]) ||
                !std::isdigit((unsigned char)low[i + 1]))
                continue;
            size_t a = i;
            while (a > 0 && std::isdigit((unsigned char)low[a - 1])) a--;
            if (i - a > 2 || (a > 0 && std::isalnum((unsigned char)low[a - 1]))) continue;
            size_t j = i + 1;
            int e = readNum(low, j, 3);
            if (e < 0 || j - (i + 1) < 2) continue;  // "2x2" is not an episode
            in.season = in.seasonTo = std::atoi(low.substr(a, i - a).c_str());
            in.episode = in.episodeTo = e;
            break;
        }

    // "сезон 1", "1 сезон", "сезоны 1-3", "season 2", "seasons 1-3"
    // "сезон 1", "1 сезон", "сезоны 1-3", "season 2", "seasons 1-3"
    for (const char* word : { "\xD1\x81\xD0\xB5\xD0\xB7\xD0\xBE\xD0\xBD",  // сезон
                              "season" })
    {
        for (size_t k = low.find(word); k != std::string::npos && in.season < 0;
             k = low.find(word, k + 1))
        {
            size_t j;
            if (!wordAt(low, k, std::strlen(word), j)) continue;
            skipSpaces(low, j);
            int s = readNum(low, j, 2);
            if (s < 0) s = numberBefore(low, k);
            if (s < 0) continue;
            in.season = in.seasonTo = s;
            int to = readRangeEnd(low, j);
            if (to >= 0) in.seasonTo = to;
        }
    }

    // "серии 1-8 из 8", "серия 6", "6 серия", "episode 6", "episodes 1-8"
    const char* seria = "\xD1\x81\xD0\xB5\xD1\x80\xD0\xB8";  // сери(я|и|й)
    for (const char* word : { seria, "episode" })
    {
        for (size_t k = low.find(word); k != std::string::npos && in.episode < 0;
             k = low.find(word, k + 1))
        {
            size_t j;
            // "сериал" is not "серия".
            if (!wordAt(low, k, std::strlen(word), j)) continue;
            skipSpaces(low, j);
            int e = readNum(low, j);
            if (e < 0) e = numberBefore(low, k);
            if (e < 0) continue;
            in.episode = in.episodeTo = e;
            int to = readRangeEnd(low, j);
            if (to >= 0) in.episodeTo = to;
            // "из 8" / "of 8": the whole season when the range reaches it.
            size_t of = low.find("\xD0\xB8\xD0\xB7 ", j);  // "из "
            if (of == std::string::npos) of = low.find("of ", j);
            if (of != std::string::npos && of < j + 6)
            {
                size_t t = low.find(' ', of) + 1;
                int total = readNum(low, t);
                if (total > 0 && in.episode <= 1 && in.episodeTo >= total)
                    in.fullSeason = true;
            }
        }
    }

    if (in.season >= 0 && in.episode < 0) in.fullSeason = true;
    if (hasWord(words(low), "complete") && in.episode < 0) in.fullSeason = true;
}

struct Voice
{
    const char* match;  // lower-case substring
    const char* label;
};

const Voice kVoices[] = {
    { "\xD0\xB4\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" },  // дубляж
    { "\xD0\xB4\xD1\x83\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x80", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" },  // дублир(ованный)
    { " dub ", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" },
    { " mvo ", "MVO" },
    { "\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81", "MVO" },  // многоголос
    { " dvo ", "DVO" },
    { "\xD0\xB4\xD0\xB2\xD1\x83\xD1\x85\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81", "DVO" },  // двухголос
    { " avo ", "AVO" },
    { "\xD0\xB0\xD0\xB2\xD1\x82\xD0\xBE\xD1\x80\xD1\x81\xD0\xBA", "AVO" },  // авторск
    { "lostfilm", "LostFilm" },
    { "hdrezka", "HDRezka" },
    { "newstudio", "NewStudio" },
    { "kubik", "\xD0\x9A\xD1\x83\xD0\xB1\xD0\xB8\xD0\xBA \xD0\xB2 \xD0\x9A\xD1\x83\xD0\xB1\xD0\xB5" },
    { "\xD0\xBA\xD1\x83\xD0\xB1\xD0\xB8\xD0\xBA \xD0\xB2 \xD0\xBA\xD1\x83\xD0\xB1\xD0\xB5",
      "\xD0\x9A\xD1\x83\xD0\xB1\xD0\xB8\xD0\xBA \xD0\xB2 \xD0\x9A\xD1\x83\xD0\xB1\xD0\xB5" },  // Кубик в Кубе
    { "alexfilm", "AlexFilm" },
    { "jaskier", "Jaskier" },
    { "tvshows", "TVShows" },
    { "baibako", "BaibaKo" },
    { "amedia", "Amedia" },
    { "coldfilm", "ColdFilm" },
    { "red head sound", "RHS" },
    { "anilibria", "AniLibria" },
    { "animevost", "AnimeVost" },
};

void readVoices(const std::string& low, const std::string& w, Info& in)
{
    for (const Voice& v : kVoices)
    {
        bool hit = v.match[0] == ' ' ? w.find(v.match) != std::string::npos
                                     : low.find(v.match) != std::string::npos;
        if (!hit) continue;
        bool dup = false;
        for (const auto& x : in.voices) dup = dup || x == v.label;
        if (!dup) in.voices.push_back(v.label);
    }
}

// "...H264-NTb.mkv" -> "NTb"; "[HDRezka]" at the end -> "HDRezka".
std::string readGroup(const std::string& title)
{
    std::string t = title;
    size_t dot = t.find_last_of('.');
    if (dot != std::string::npos && t.size() - dot <= 5) t = t.substr(0, dot);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (!t.empty() && t.back() == ']')
    {
        size_t open = t.rfind('[');
        if (open != std::string::npos && t.size() - open - 2 <= 24)
            return t.substr(open + 1, t.size() - open - 2);
    }
    size_t dash = t.rfind('-');
    if (dash == std::string::npos || dash + 1 >= t.size()) return "";
    std::string g = t.substr(dash + 1);
    if (g.size() > 16) return "";
    for (char c : g)
        if (!std::isalnum((unsigned char)c)) return "";
    // "...-2024" or "-1080p" is not a group.
    bool digits = true;
    for (char c : g) digits = digits && std::isdigit((unsigned char)c);
    if (digits || g == "DL" || g == "dl") return "";
    return g;
}

} // namespace

Info parse(const std::string& title)
{
    Info in;
    std::string low = lower(title);
    std::string w   = words(low);

    if (has(low, "2160p") || hasWord(w, "4k") || hasWord(w, "uhd") || has(low, "3840x"))
        in.height = 2160;
    else if (has(low, "1080p") || has(low, "1080i") || has(low, "1920x"))
        in.height = 1080;
    else if (has(low, "720p") || has(low, "1280x"))
        in.height = 720;
    else if (has(low, "480p") || has(low, "576p") || hasWord(w, "dvdrip"))
        in.height = 480;

    if (has(low, "remux"))
        in.source = "Remux";
    else if (has(low, "bdrip") || has(low, "bd-rip") || has(low, "brrip"))
        in.source = "BDRip";
    else if (has(low, "bluray") || has(low, "blu-ray"))
        in.source = "BluRay";
    else if (has(low, "web-dl") || has(low, "webdl") || has(low, "web dl"))
        in.source = "WEB-DL";
    else if (has(low, "webrip") || has(low, "web-rip"))
        in.source = "WEBRip";
    else if (has(low, "hdtv"))
        in.source = "HDTV";
    else if (has(low, "dvdrip") || hasWord(w, "dvd5") || hasWord(w, "dvd9"))
        in.source = "DVDRip";
    if (has(low, "camrip") || hasWord(w, "cam") || hasWord(w, "ts") || has(low, "telesync") ||
        hasWord(w, "hdts") || hasWord(w, "tc"))
    {
        in.source = "CAMRip";
        in.cam    = true;
    }

    // Also spelled with a space: "DDP5 1 H 264-NTb" (an index that turned
    // the dots into spaces).
    if (has(low, "hevc") || has(low, "x265") || has(low, "h265") || has(low, "h.265") ||
        has(w, " h 265 ") || has(w, " x 265 "))
        in.codec = "HEVC";
    else if (has(low, "x264") || has(low, "h264") || has(low, "h.264") || hasWord(w, "avc") ||
             has(w, " h 264 ") || has(w, " x 264 "))
        in.codec = "H.264";
    else if (hasWord(w, "av1"))
        in.codec = "AV1";
    else if (has(low, "xvid") || has(low, "divx"))
        in.codec = "XviD";

    in.dolbyVision = has(low, "dolby vision") || hasWord(w, "dovi") || hasWord(w, "dv");
    // As words: "hdr" is also the start of "HDRezka".
    in.hdr = hasWord(w, "hdr") || hasWord(w, "hdr10") || hasWord(w, "hdr10plus") ||
             in.dolbyVision;
    in.tenBit      = has(low, "10bit") || has(low, "10-bit") || has(low, "10 bit") ||
                has(low, "hi10");

    readScope(low, in);
    readVoices(low, w, in);
    in.group = readGroup(title);

    // The audio languages a name owns up to ("Rus | Ukr | Eng", "4xUkr/Eng",
    // "Русский"): a Ukrainian-only release is not the pick for a Russian UI.
    in.russian = !in.voices.empty() || hasWord(w, "rus") || has(low, "\xD1\x80\xD1\x83\xD1\x81") ||
                 has(low, "russian");
    in.ukrainian = has(low, "ukr") || has(low, "\xD1\x83\xD0\xBA\xD1\x80");  // укр
    return in;
}

std::vector<std::string> tags(const Info& in)
{
    std::vector<std::string> t;
    if (!in.source.empty()) t.push_back(in.source);
    if (!in.codec.empty()) t.push_back(in.codec + (in.tenBit ? " 10-bit" : ""));
    if (in.dolbyVision)
        t.push_back("Dolby Vision");
    else if (in.hdr)
        t.push_back("HDR");
    for (const auto& v : in.voices) t.push_back(v);
    return t;
}

int score(const Info& in, int seeders, int64_t size, const Want& want)
{
    // What it holds first: the wrong season or episode is not a choice at all.
    if (want.series && want.season > 0 && in.season >= 0 &&
        (want.season < in.season || want.season > in.seasonTo))
        return -1000;
    if (want.series && want.episode > 0 && in.episode >= 0 && !in.fullSeason &&
        (want.episode < in.episode || want.episode > in.episodeTo))
        return -1000;
    if (in.cam) return -500;

    int s = 0;
    // Quality for a Switch: 1080p plays everywhere; 4K is downscaled by the
    // dock anyway (it outputs 1080p) and costs four times the bandwidth.
    switch (in.height)
    {
        case 1080: s += 40; break;
        case 720:  s += 28; break;
        case 2160: s += 18; break;
        case 480:  s += 10; break;
        default:   s += 16; break;
    }
    // H.264 decodes in hardware everywhere; HEVC too, but 10-bit and HDR
    // releases are the heavy ones; AV1 has no hardware decoder on the X1.
    if (in.codec == "H.264") s += 10;
    else if (in.codec == "HEVC") s += 5;
    else if (in.codec == "AV1") s -= 20;
    if (in.tenBit) s -= 4;
    if (in.dolbyVision) s -= 20;  // profile 5 comes out green and purple
    else if (in.hdr) s -= 8;      // washed out without a tone-map

    if (in.source == "WEB-DL") s += 8;
    else if (in.source == "BluRay" || in.source == "BDRip") s += 6;
    else if (in.source == "WEBRip") s += 5;
    else if (in.source == "Remux") s -= 15;  // 40-80 GB over Wi-Fi

    if (seeders == 0) s -= 30;
    else if (seeders > 0) s += (int)std::fmin(30.0, 8.0 * std::log2(1.0 + seeders));

    const double gb = size / (1024.0 * 1024.0 * 1024.0);
    if (gb > 80) s -= 20;
    else if (gb > 40) s -= 10;

    if (want.series && want.episode > 0)
    {
        if (in.episode == want.episode && in.episodeTo == want.episode) s += 4;
        else if (in.fullSeason) s += 2;
    }

    if (!want.voice.empty())
        for (const auto& v : in.voices)
            if (v == want.voice) s += 15;
    if (!want.group.empty() && in.group == want.group) s += 10;
    if (want.preferRussian && !in.voices.empty()) s += 8;
    // Ukrainian audio only, the UI in Russian: a working release, but rarely
    // the one wanted.
    if (want.preferRussian && in.ukrainian && !in.russian) s -= 12;
    return s;
}

namespace
{
// Season and episode of one file: from its name, then its folders.
void fileScope(const std::string& path, int& season, int& episode)
{
    size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string dir  = slash == std::string::npos ? "" : path.substr(0, slash);

    Info b = parse(base);
    season  = b.season;
    episode = b.episode;
    std::string low = lower(base);
    if (episode < 0)
    {
        // "E06", "Ep06", "Ep.06" with no season in front.
        for (size_t i = 0; i + 1 < low.size(); i++)
        {
            if (low[i] != 'e' || (i > 0 && std::isalpha((unsigned char)low[i - 1]))) continue;
            size_t j = i + 1;
            if (j < low.size() && low[j] == 'p') j++;
            if (j < low.size() && low[j] == '.') j++;
            int e = readNum(low, j);
            if (e >= 0) { episode = e; break; }
        }
    }
    if (episode < 0)
    {
        // A leading number: "06. Title.mkv", "06 - Title.mkv", "06.mkv".
        size_t j = 0;
        int e = readNum(low, j);
        if (e >= 0 && j < low.size() && std::strchr(" ._-", low[j])) episode = e;
    }
    if (season < 0 && !dir.empty())
    {
        Info d = parse(dir.substr(dir.find_last_of("/\\") == std::string::npos
                                      ? 0
                                      : dir.find_last_of("/\\") + 1));
        season = d.season;
    }
}
} // namespace

namespace
{

// A name for comparing: lower case, "ё" as "е", separators as single spaces,
// " padded ", without a leading "[group]" or "the".
std::string comparable(std::string s)
{
    while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    if (!s.empty() && s[0] == '[')
    {
        size_t close = s.find(']');
        if (close != std::string::npos) s.erase(0, close + 1);
    }
    std::string low = lower(s);
    std::string e;
    for (size_t i = 0; i < low.size(); i++)
    {
        if ((unsigned char)low[i] == 0xD1 && i + 1 < low.size() &&
            (unsigned char)low[i + 1] == 0x91)
        {
            e += (char)0xD0;
            e += (char)0xB5;
            i++;
        }
        else
            e += low[i];
    }
    std::string w = words(e);
    if (w.compare(0, 5, " the ") == 0) w.erase(0, 4);
    return w;
}

} // namespace

bool titleMatches(const std::string& name, const std::vector<std::string>& titles)
{
    std::vector<std::string> want;
    for (const auto& t : titles)
    {
        std::string c = comparable(t);
        if (c.size() > 2 && std::find(want.begin(), want.end(), c) == want.end())
            want.push_back(c);
    }
    if (want.empty()) return true;  // nothing to compare with

    // The whole name and each of its " / " and " | " parts.
    std::vector<std::string> parts = { name };
    size_t from = 0;
    for (size_t i = 0; i <= name.size(); i++)
        if (i == name.size() || name[i] == '/' || name[i] == '|')
        {
            if (i > from) parts.push_back(name.substr(from, i - from));
            from = i + 1;
        }
    for (const auto& p : parts)
    {
        std::string c = comparable(p);
        for (const auto& w : want)
            if (c.compare(0, w.size(), w) == 0) return true;
    }
    return false;
}

std::vector<int> yearsIn(const std::string& name)
{
    std::vector<int> out;
    for (size_t i = 0; i + 4 <= name.size(); i++)
    {
        if (!std::isdigit((unsigned char)name[i])) continue;
        size_t j = i;
        while (j < name.size() && std::isdigit((unsigned char)name[j])) j++;
        if (j - i == 4 && (i == 0 || !std::isalnum((unsigned char)name[i - 1])) &&
            (j == name.size() || !std::isalpha((unsigned char)name[j])))
        {
            int y = std::atoi(name.substr(i, 4).c_str());
            if (y >= 1900 && y <= 2039) out.push_back(y);
        }
        i = j;
    }
    return out;
}

int matchEpisode(const std::vector<std::string>& paths, int season, int episode)
{
    int best = -1, bestRank = 0;
    bool tie = false;
    for (size_t i = 0; i < paths.size(); i++)
    {
        int s = -1, e = -1;
        fileScope(paths[i], s, e);
        if (e != episode) continue;
        if (s >= 0 && s != season) continue;
        int rank = s == season ? 2 : 1;  // a stated season beats an unstated one
        if (rank > bestRank)
        {
            best = (int)i;
            bestRank = rank;
            tie = false;
        }
        else if (rank == bestRank)
            tie = true;
    }
    return tie ? -1 : best;
}

} // namespace release
