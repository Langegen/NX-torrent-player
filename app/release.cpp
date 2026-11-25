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

// "1-8", "1–8" (an en dash), "1 - 8": the end of a range after a number
// `from`, at most `maxv`. Not a range: "S01E05 - 720p" (a letter right after
// the number -- a resolution, a codec), and an end below its start.
int readRangeEnd(const std::string& s, size_t& i, int from, int maxv)
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
    if (n < 0) return -1;
    if (j < s.size() && std::isalpha((unsigned char)s[j])) return -1;  // 720p, 264
    if (n < from || n > maxv) return -1;
    i = j;
    return n;
}

// Reads a single number ("2 сезон") or range ("1-5 сезоны", "1-8 серии")
// immediately before `pos`.
bool numberRangeBefore(const std::string& s, size_t pos, int maxDigits, int maxVal,
                       int& outFrom, int& outTo)
{
    size_t j = pos;
    while (j > 0 && s[j - 1] == ' ') j--;
    size_t end = j;
    while (j > 0 && std::isdigit((unsigned char)s[j - 1])) j--;
    if (j == end || (int)(end - j) > maxDigits) return false;
    if (j > 0 && (std::isalpha((unsigned char)s[j - 1]) || (unsigned char)s[j - 1] >= 0x80))
        return false;
    int b = std::atoi(s.substr(j, end - j).c_str());
    if (b < 0 || b > maxVal) return false;

    outFrom = outTo = b;
    // Check for an optional "a-" or "a–" range prefix before b.
    size_t d = j;
    while (d > 0 && s[d - 1] == ' ') d--;
    size_t aEnd = std::string::npos;
    if (d > 0 && s[d - 1] == '-')
        aEnd = d - 1;
    else if (d >= 3 && s.compare(d - 3, 3, "\xE2\x80\x93") == 0)
        aEnd = d - 3;
    if (aEnd != std::string::npos)
    {
        while (aEnd > 0 && s[aEnd - 1] == ' ') aEnd--;
        size_t aStart = aEnd;
        while (aStart > 0 && std::isdigit((unsigned char)s[aStart - 1])) aStart--;
        if (aStart < aEnd && (int)(aEnd - aStart) <= maxDigits &&
            (aStart == 0 ||
             (!std::isalpha((unsigned char)s[aStart - 1]) &&
              (unsigned char)s[aStart - 1] < 0x80)))
        {
            int a = std::atoi(s.substr(aStart, aEnd - aStart).c_str());
            if (a >= 0 && a <= b) outFrom = a;
        }
    }
    return true;
}

// Reads "из 8", "of 8", or "(7)" right after an episode token/range.
int readTotalAfter(const std::string& s, size_t j)
{
    size_t scanEnd = std::min(s.size(), j + 16);
    for (size_t p = j; p < scanEnd; p++)
    {
        if (s.compare(p, 5, "\xD0\xB8\xD0\xB7 ") == 0)  // "из "
        {
            size_t t = p + 5;
            while (t < s.size() && s[t] == ' ') t++;
            return readNum(s, t, 3);
        }
        if (s.compare(p, 3, "of ") == 0 &&
            (p == 0 || !std::isalpha((unsigned char)s[p - 1])))
        {
            size_t t = p + 3;
            while (t < s.size() && s[t] == ' ') t++;
            return readNum(s, t, 3);
        }
    }
    size_t p = j;
    while (p < s.size() && s[p] == ' ') p++;
    if (p < s.size() && s[p] == '(')
    {
        size_t t = p + 1;
        while (t < s.size() && s[t] == ' ') t++;
        int tot = readNum(s, t, 3);
        while (t < s.size() && s[t] == ' ') t++;
        if (tot > 0 && t < s.size() && s[t] == ')') return tot;
    }
    return -1;
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
            int to = readRangeEnd(low, j, e, 999);
            if (to >= 0) in.episodeTo = to;
        }
        else
        {
            int to = readRangeEnd(low, j, s, 99);
            if (to >= 0) in.seasonTo = to;
        }
    }

    // 1x06, 01x01-08 из 08
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
            int to = readRangeEnd(low, j, e, 999);
            if (to >= 0) in.episodeTo = to;
            int tot = readTotalAfter(low, j);
            if (tot > 0 && in.episode <= 1 && in.episodeTo >= tot)
                in.fullSeason = true;
            break;
        }

    // "2 сезон", "1-5 сезоны", "сезон 1", "сезоны 1-3", "season 2", "seasons 1-3"
    // Check BEFORE the word first so "(2 сезон: 1-8 серии из 8)" reads 2, not 1-8!
    for (const char* word : { "\xD1\x81\xD0\xB5\xD0\xB7\xD0\xBE\xD0\xBD",  // сезон
                              "season" })
    {
        for (size_t k = low.find(word); k != std::string::npos && in.season < 0;
             k = low.find(word, k + 1))
        {
            size_t j;
            if (!wordAt(low, k, std::strlen(word), j)) continue;
            int s = -1, sto = -1;
            if (numberRangeBefore(low, k, 2, 99, s, sto))
            {
                in.season   = s;
                in.seasonTo = sto;
                // Handle "(1 сезон-5)" where the dash sits immediately after "сезон".
                if (j < low.size() &&
                    (low[j] == '-' || low.compare(j, 3, "\xE2\x80\x93") == 0))
                {
                    int to = readRangeEnd(low, j, s, 99);
                    if (to >= 0) in.seasonTo = to;
                }
            }
            else
            {
                skipSpaces(low, j);
                s = readNum(low, j, 2);
                if (s < 0) continue;
                in.season = in.seasonTo = s;
                int to = readRangeEnd(low, j, s, 99);
                if (to >= 0) in.seasonTo = to;
            }
        }
    }

    // "1-8 серии из 8", "7 серий из 7", "серии 1-7 (7)", "8 серия", "episode 6"
    const char* seria = "\xD1\x81\xD0\xB5\xD1\x80\xD0\xB8";  // сери(я|и|й)
    for (const char* word : { seria, "episode" })
    {
        for (size_t k = low.find(word); k != std::string::npos && in.episode < 0;
             k = low.find(word, k + 1))
        {
            size_t j;
            // "сериал" is not "серия".
            if (!wordAt(low, k, std::strlen(word), j)) continue;
            bool isGenitive = (word == seria) &&
                              low.compare(k, 10, "\xD1\x81\xD0\xB5\xD1\x80\xD0\xB8\xD0\xB9") == 0;
            int e = -1, eto = -1;
            if (numberRangeBefore(low, k, 3, 999, e, eto))
            {
                in.episode   = e;
                in.episodeTo = eto;
                int total = readTotalAfter(low, j);
                if (isGenitive && total > 0 && in.episode == in.episodeTo && in.episode == total)
                {
                    in.episode    = 1;
                    in.episodeTo  = total;
                    in.fullSeason = true;
                }
                else if (total > 0 && in.episode <= 1 && in.episodeTo >= total)
                {
                    in.fullSeason = true;
                }
            }
            else
            {
                skipSpaces(low, j);
                e = readNum(low, j);
                if (e < 0) continue;
                in.episode = in.episodeTo = e;
                int to = readRangeEnd(low, j, e, 999);
                if (to >= 0) in.episodeTo = to;
                int total = readTotalAfter(low, j);
                if (total > 0 && in.episode <= 1 && in.episodeTo >= total)
                    in.fullSeason = true;
            }
        }
    }

    // "(1-16 из 16)" / "(1-62 из 62)" when a season was already named without "серии".
    if (in.season >= 0 && in.episode < 0)
    {
        for (size_t p = 0; p < low.size(); p++)
        {
            if (low[p] != '(' && low[p] != '[') continue;
            size_t j = p + 1;
            while (j < low.size() && low[j] == ' ') j++;
            int a = readNum(low, j, 3);
            if (a < 0) continue;
            int b = readRangeEnd(low, j, a, 999);
            if (b < 0) b = a;
            int tot = readTotalAfter(low, j);
            if (tot > 0 && ((a <= 1 && b >= tot) || (a == b && a == tot)))
            {
                in.episode    = 1;
                in.episodeTo  = tot;
                in.fullSeason = true;
                break;
            }
        }
    }

    // Anime cour/season markers: "(ТВ-1)", "(ТВ-2)", "TV-1", "TV-2".
    if (in.season < 0)
    {
        for (const char* tag : { "\xD1\x82\xD0\xB2-", "tv-" })
        {
            size_t tlen = std::strlen(tag);
            for (size_t k = low.find(tag); k != std::string::npos; k = low.find(tag, k + 1))
            {
                if (k > 0 && (std::isalnum((unsigned char)low[k - 1]) ||
                              (unsigned char)low[k - 1] >= 0x80))
                    continue;
                size_t j = k + tlen;
                int n = readNum(low, j, 2);
                if (n >= 1 && n <= 20 && !isLetterAt(low, j))
                {
                    in.season = in.seasonTo = n;
                    break;
                }
            }
            if (in.season >= 0) break;
        }
    }

    if (in.season >= 0 && in.episode < 0) in.fullSeason = true;
    if (hasWord(words(low), "complete") && in.episode < 0) in.fullSeason = true;

    if (in.season >= 0 && in.seasonTo >= in.season && in.seasonTo - in.season <= 50)
    {
        for (int s = in.season; s <= in.seasonTo; s++)
            in.seasons.push_back(s);
    }
}

struct Voice
{
    const char* match;  // lower-case substring; " x " = a whole word
    const char* label;
    // A short Latin mark ("| D", "| P", "L1", "Dub") that only means a Russian
    // voice-over on a Russian tracker's title: an English "(Dub)" anime release
    // is not one. Only taken when the name has Cyrillic in it.
    bool needsCyrillic = false;
};

const Voice kVoices[] = {
    { "\xD0\xB4\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" },  // дубляж
    { "\xD0\xB4\xD1\x83\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x80", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" },  // дублир(ованный)
    { " dub ", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6", true },
    // Rutracker's shorthands, as Lampa reads them: "| D" dubbing, "| P"
    // multi-voice, "L2" two-voice, "L1" single-voice / amateur, and the
    // Cyrillic ones ("дб", "пм"/"лм", "пд"/"лд", "ло"/"ап").
    { " d ", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6", true },
    { " дб ", "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" },
    { " p ", "MVO", true },
    { " пм ", "MVO" },
    { " лм ", "MVO" },
    { " l2 ", "DVO", true },
    { " пд ", "DVO" },
    { " лд ", "DVO" },
    { "двуголос", "DVO" },
    { " l1 ", "AVO", true },
    { " ло ", "AVO" },
    { " ап ", "AVO" },
    { "любительск", "AVO" },
    { " mvo ", "MVO", true },
    { "\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81", "MVO" },  // многоголос
    { " dvo ", "DVO", true },
    { "\xD0\xB4\xD0\xB2\xD1\x83\xD1\x85\xD0\xB3\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x81", "DVO" },  // двухголос
    { " avo ", "AVO", true },
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
    { " rhs ", "RHS", true },
    { "flarrow films", "Flarrow Films" },
    { "\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xB0\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C\xD0\xBC", "\xD0\x9D\xD0\xB5\xD0\xB2\xD0\xB0\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C\xD0\xBC" },  // невафильм -> Невафильм
    { "\xD0\xBF\xD0\xB8\xD1\x84\xD0\xB0\xD0\xB3\xD0\xBE\xD1\x80", "\xD0\x9F\xD0\xB8\xD1\x84\xD0\xB0\xD0\xB3\xD0\xBE\xD1\x80" },  // пифагор -> Пифагор
    { "anilibria", "AniLibria" },
    { "animevost", "AnimeVost" },
};

void recomputeVoiceRank(Info& in)
{
    in.voiceRank = 0;
    for (const auto& v : in.voices)
    {
        int r = v == "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" ? 4  // Дубляж
              : v == "DVO"                                                 ? 2
              : v == "AVO"                                                 ? 1
                                                                           : 3;  // MVO, a studio
        if (r > in.voiceRank) in.voiceRank = r;
    }
}

void readVoices(const std::string& low, const std::string& w, Info& in)
{
    // A Russian tracker's title (Cyrillic in it) or one that says "Rus".
    bool ruContext = hasWord(w, "rus");
    for (unsigned char c : low)
        if (c == 0xD0 || c == 0xD1)
        {
            ruContext = true;
            break;
        }
    for (const Voice& v : kVoices)
    {
        if (v.needsCyrillic && !ruContext) continue;
        bool hit = v.match[0] == ' ' ? w.find(v.match) != std::string::npos
                                     : low.find(v.match) != std::string::npos;
        if (!hit) continue;
        bool dup = false;
        for (const auto& x : in.voices) dup = dup || x == v.label;
        if (!dup) in.voices.push_back(v.label);
    }
    recomputeVoiceRank(in);
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

    if (has(low, "2160p") || hasWord(w, "4k") || hasWord(w, "4\xD0\xBA") ||
        hasWord(w, "uhd") || has(low, "3840x"))
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
    else if (has(low, "web-dlrip") || has(low, "webdlrip") || has(low, "web-dl-rip") ||
             has(low, "webrip") || has(low, "web-rip"))
        in.source = "WEBRip";
    else if (has(low, "web-dl") || has(low, "webdl") || has(low, "web dl"))
        in.source = "WEB-DL";
    else if (has(low, "hdtv"))
        in.source = "HDTV";
    else if (has(low, "hdrip"))
        in.source = "HDRip";
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
    if (has(low, "hevc") || has(low, "x265") || has(low, "x.265") ||
        has(low, "h265") || has(low, "h.265") || has(w, " h 265 ") || has(w, " x 265 "))
        in.codec = "HEVC";
    else if (has(low, "x264") || has(low, "x.264") || has(low, "h264") || has(low, "h.264") ||
             hasWord(w, "avc") || has(w, " h 264 ") || has(w, " x 264 "))
        in.codec = "H.264";
    else if (hasWord(w, "av1") || hasWord(w, "av01"))
        in.codec = "AV1";
    else if (has(low, "xvid") || has(low, "divx"))
        in.codec = "XviD";

    in.dolbyVision = has(low, "dolby vision") || hasWord(w, "dovi") || hasWord(w, "dv");
    // As words: "hdr" is also the start of "HDRezka".
    in.hdr = hasWord(w, "hdr") || hasWord(w, "hdr10") || hasWord(w, "hdr10plus") ||
             in.dolbyVision;
    in.tenBit      = has(low, "10bit") || has(low, "10-bit") || has(low, "10 bit") ||
                has(low, "hi10");
    if (in.codec.empty() && has(low, "hi10"))
        in.codec = "H.264";

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

namespace
{
std::string normalizeMetaVoice(const std::string& raw, bool& isRuVoice, bool& isUkr)
{
    std::string v = raw;
    while (!v.empty() && std::isspace((unsigned char)v.front())) v.erase(0, 1);
    while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
    if (v.empty()) return "";

    std::string lv = lower(v);
    if (lv == "\xD1\x83\xD0\xBA\xD1\x80\xD0\xB0\xD0\xB8\xD0\xBD\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" || // украинский
        lv == "\xD1\x83\xD0\xBA\xD1\x80" || lv == "ukr" || lv == "ukrainian")
    {
        isUkr = true;
        return "";
    }
    // Non-voice meta tags returned by Jacred in info.voices.
    if (lv == "\xD1\x81\xD1\x83\xD0\xB1\xD1\x82\xD0\xB8\xD1\x82\xD1\x80\xD1\x8B" || // субтитры
        lv == "sub" || lv == "subs" ||
        lv == "\xD0\xBE\xD1\x80\xD0\xB8\xD0\xB3\xD0\xB8\xD0\xBD\xD0\xB0\xD0\xBB" || // оригинал
        lv == "original" ||
        lv == "\xD0\xB4\xD0\xB2\xD0\xB5 \xD0\xB4\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB6\xD0\xBA\xD0\xB8" || // две дорожки
        lv == "\xD0\xBD\xD0\xB5\xD1\x81\xD0\xBA\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD0\xB4\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBA" || // несколько дорожек
        lv == "\xD0\xBC\xD1\x83\xD0\xBB\xD1\x8C\xD1\x82\xD0\xB8\xD1\x8F\xD0\xB7\xD1\x8B\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F" || // мультиязычная
        lv == "\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD1\x8F\xD0\xB7\xD1\x8B\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F" || // многоязычная
        lv == "\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB6\xD0\xB5\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F" || // многодорожечная
        lv == "\xD0\xB1\xD0\xB5\xD0\xB7 \xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0" || // без перевода
        lv == "\xD0\xBD\xD0\xB5\xD0\xB8\xD0\xB7\xD0\xB2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBD\xD0\xBE" || // неизвестно
        lv == "\xD1\x80\xD0\xBE\xD1\x81\xD1\x81\xD0\xB8\xD1\x8F" || // россия
        lv == "russia" || lv == "eng" || lv == "english" || lv == "multi" ||
        lv == "dual audio" || lv == "multi-audio" || lv == "multi audio" || lv == "unknown")
        return "";

    isRuVoice = true;
    if (lv == "red head sound" || lv == "rhs") return "RHS";
    if (lv == "hdrezka" || lv == "hdrezka studio") return "HDRezka";
    if (lv == "\xD0\xBA\xD1\x83\xD0\xB1\xD0\xB8\xD0\xBA \xD0\xB2 \xD0\xBA\xD1\x83\xD0\xB1\xD0\xB5" || // кубик в кубе
        lv == "kubik")
        return "\xD0\x9A\xD1\x83\xD0\xB1\xD0\xB8\xD0\xBA \xD0\xB2 \xD0\x9A\xD1\x83\xD0\xB1\xD0\xB5";
    if (lv == "\xD0\xB4\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" || // дубляж
        lv == "\xD0\xB4\xD1\x83\xD0\xB1\xD0\xBB\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB9" ||
        lv == "dub")
        return "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6";
    if (lv == "lostfilm") return "LostFilm";
    if (lv == "jaskier") return "Jaskier";
    if (lv == "alexfilm") return "AlexFilm";
    if (lv == "newstudio") return "NewStudio";
    if (lv == "baibako") return "BaibaKo";
    if (lv == "coldfilm") return "ColdFilm";
    if (lv == "amedia") return "Amedia";
    if (lv == "tvshows") return "TVShows";
    if (lv == "anilibria") return "AniLibria";
    if (lv == "animevost") return "AnimeVost";
    if (lv == "mvo" || lv == "dvo" || lv == "avo")
    {
        std::string up = lv;
        for (char& c : up) c = (char)std::toupper((unsigned char)c);
        return up;
    }
    return v;
}

bool isForeignTracker(const std::string& raw)
{
    std::string t = lower(raw);
    while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(0, 1);
    while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
    static const char* const kForeign[] = {
        "nyaa", "nyaasi", "knaben", "piratebay", "thepiratebay", "1337x",
        "yts", "eztv", "torrentgalaxy", "rarbg", "limetorrents", "kickass",
        "bitsearch", "solidtorrents", "extto", "anidex", "tokyotosho",
        "sukebei", "glodls", "magnetdl", "torlock", "torrentdownloads",
    };
    for (const char* f : kForeign)
        if (t == f) return true;
    return false;
}
} // namespace

void enrich(Info& in, const ExternalMeta& meta)
{
    in.seedersUnknown = in.seedersUnknown || meta.seedersUnknown;

    if (in.height == 0 && meta.quality > 0)
    {
        if (meta.quality >= 2000) in.height = 2160;
        else if (meta.quality >= 1000) in.height = 1080;
        else if (meta.quality >= 700) in.height = 720;
        else if (meta.quality >= 400) in.height = 480;
    }

    if (!meta.videotype.empty())
    {
        std::string vt = lower(meta.videotype);
        if (vt.find("dv") != std::string::npos || vt.find("dolby") != std::string::npos)
            in.dolbyVision = in.hdr = true;
        else if (vt.find("hdr") != std::string::npos)
            in.hdr = true;
    }

    if (!meta.seasons.empty())
    {
        std::vector<int> ms;
        for (int s : meta.seasons)
            if (s > 0 && std::find(ms.begin(), ms.end(), s) == ms.end())
                ms.push_back(s);
        std::sort(ms.begin(), ms.end());
        if (!ms.empty())
        {
            if (!in.hasAnySeason())
            {
                in.seasons  = ms;
                in.season   = ms.front();
                in.seasonTo = ms.back();
                if (in.episode < 0) in.fullSeason = true;
            }
            else if (ms.size() > in.seasons.size())
            {
                for (int s : ms)
                    if (std::find(in.seasons.begin(), in.seasons.end(), s) == in.seasons.end())
                        in.seasons.push_back(s);
                std::sort(in.seasons.begin(), in.seasons.end());
                in.season   = in.seasons.front();
                in.seasonTo = in.seasons.back();
            }
        }
    }

    const bool parsedRussian = in.russian;
    for (const auto& lang : meta.languages)
    {
        std::string l = lower(lang);
        if (l == "rus" || l == "ru") in.russian = true;
        else if (l == "ukr" || l == "uk" || l == "ua") in.ukrainian = true;
    }

    bool onlyForeignTrackers = !meta.trackers.empty();
    for (const auto& t : meta.trackers)
    {
        if (!isForeignTracker(t))
        {
            onlyForeignTrackers = false;
            break;
        }
    }

    bool addedVoice = false;
    for (const auto& rawV : meta.voices)
    {
        bool isRuVoice = false, isUkr = false;
        std::string norm = normalizeMetaVoice(rawV, isRuVoice, isUkr);
        if (isUkr) in.ukrainian = true;
        if (norm.empty()) continue;
        // Jacred maps English/anime "Dub" on foreign trackers to generic "Дубляж".
        if (onlyForeignTrackers && !parsedRussian &&
            norm == "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6")
            continue;
        std::string nl = lower(norm);
        bool dup = false;
        for (const auto& ex : in.voices)
            if (lower(ex) == nl) { dup = true; break; }
        if (!dup)
        {
            in.voices.push_back(std::move(norm));
            addedVoice = addedVoice || isRuVoice;
        }
    }
    if (addedVoice)
    {
        in.russian = true;
        recomputeVoiceRank(in);
    }
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

int packEpisodeEstimate(const Info& in)
{
    if (in.episode > 0 && in.episodeTo > in.episode)
        return in.episodeTo - in.episode + 1;
    if (in.fullSeason)
    {
        int sc = !in.seasons.empty()
                     ? (int)in.seasons.size()
                     : ((in.season > 0 && in.seasonTo >= in.season)
                            ? (in.seasonTo - in.season + 1)
                            : 1);
        return std::max(1, sc) * 8;
    }
    return 1;
}

bool isSoftwareDecodedOnSwitch(const Info& in)
{
    return in.codec == "AV1" || (in.codec == "H.264" && in.tenBit);
}

bool isHeavyForWifi(const Info& in, int64_t size, bool series)
{
    if (in.height == 2160 || in.source == "Remux") return true;
    if (size <= 0) return false;
    int64_t effSize = size;
    if (series)
    {
        int epDiv = packEpisodeEstimate(in);
        if (epDiv > 1) effSize /= epDiv;
    }
    return effSize > (series ? (10LL << 30) : (30LL << 30));
}

bool isRecommendedEligible(const Info& in, int seeders, const Want& want)
{
    if (in.cam) return false;
    if (seeders == 0 && !in.seedersUnknown) return false;
    if (want.switchProfile)
    {
        if (isSoftwareDecodedOnSwitch(in)) return false;
        if (in.dolbyVision) return false;
    }
    return true;
}

int score(const Info& in, int seeders, int64_t size, const Want& want)
{
    // Wrong season or wrong single episode: filtered out of the visible list.
    if (want.series && want.season > 0 && in.hasAnySeason() && !in.hasSeason(want.season))
        return -1000;
    if (want.series && want.episode > 0 && !in.fullSeason && in.episode >= 0 &&
        in.episode == in.episodeTo && in.episode != want.episode)
        return -1000;
    if (in.cam) return -200;

    // For streaming, judge a series pack by per-episode size rather than whole-pack size.
    double effGb = size / (1024.0 * 1024.0 * 1024.0);
    if (want.series)
    {
        int epDiv = packEpisodeEstimate(in);
        if (epDiv > 1) effGb /= epDiv;
    }

    int s = 0;
    if (want.switchProfile)
    {
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
        // Tegra X1 NVDEC hardware-decodes H.264 8-bit and HEVC 8/10-bit.
        // AV1 (all bit depths) and H.264 10-bit (Hi10P) have no hardware decoder
        // on Switch and fall back to CPU software decode (1080p AV1 10-bit drops
        // to ~5 fps in complex scenes on stock 1.02 GHz Cortex-A57).
        if (in.codec == "H.264")
        {
            if (in.tenBit) s -= (in.height > 0 && in.height <= 720) ? 35 : 60;
            else s += 10;
        }
        else if (in.codec == "HEVC")
        {
            s += 2;
            if (in.tenBit) s -= 4;
        }
        else if (in.codec == "AV1")
        {
            s -= (in.height > 0 && in.height <= 720) ? 75 : 110;
            if (in.tenBit) s -= 20;
        }
        else
        {
            if (in.codec.empty() && in.height <= 1080 && !in.hdr && !in.dolbyVision && !in.tenBit)
                s += 6;
            if (in.tenBit) s -= 4;
        }
        if (in.dolbyVision) s -= 20;  // profile 5 comes out green and purple
        else if (in.hdr) s -= 8;      // washed out without a tone-map

        if (in.source == "WEB-DL") s += 8;
        else if (in.source == "BluRay" || in.source == "BDRip") s += 6;
        else if (in.source == "WEBRip") s += 5;
        else if (in.source == "HDRip") s += 4;
        else if (in.source == "Remux") s -= 15;  // 40-80 GB over Wi-Fi

        if (effGb > (want.series ? 40.0 : 80.0)) s -= 20;
        else if (effGb > (want.series ? 20.0 : 40.0)) s -= 10;
    }
    else
    {
        // PC / Android: 4K, HEVC, 10-bit and HDR play well without heavy penalties.
        switch (in.height)
        {
            case 2160: s += 42; break;
            case 1080: s += 40; break;
            case 720:  s += 24; break;
            case 480:  s += 8; break;
            default:   s += 16; break;
        }
        if (in.codec == "H.264") s += 8;
        else if (in.codec == "HEVC") s += 8;
        else if (in.codec == "AV1") s += 4;
        else if (in.codec.empty() && in.height <= 1080 && !in.hdr && !in.dolbyVision) s += 6;
        if (in.dolbyVision) s -= 8;
        else if (in.hdr) s += 2;

        if (in.source == "WEB-DL") s += 8;
        else if (in.source == "BluRay" || in.source == "BDRip") s += 7;
        else if (in.source == "WEBRip") s += 5;
        else if (in.source == "HDRip") s += 4;
        else if (in.source == "Remux") s -= 4;

        if (effGb > (want.series ? 50.0 : 100.0)) s -= 12;
        else if (effGb > (want.series ? 30.0 : 60.0)) s -= 5;
    }

    const bool dead = (seeders == 0 && !in.seedersUnknown);
    if (dead) s -= 150;
    else if (seeders > 0) s += (int)std::fmin(45.0, 9.0 * std::log2(1.0 + seeders));

    if (want.series)
    {
        if (in.fullSeason)
        {
            bool singleSeason = (in.seasons.size() <= 1) && (in.season < 0 || in.season == in.seasonTo);
            s += singleSeason ? 16 : 12;
        }
        else if (want.episode > 0)
        {
            if (in.episode >= 0 && in.episode <= want.episode &&
                in.episodeTo >= want.episode && in.episodeTo > in.episode)
                s += 8;
            else if (in.episode == want.episode && in.episodeTo == want.episode)
                s += 4;
        }
    }

    if (!want.voice.empty())
        for (const auto& v : in.voices)
            if (v == want.voice) s += 15;
    if (!want.group.empty() && in.group == want.group) s += 10;

    // A Russian UI wants Russian audio: any live release with it ranks above
    // every release without it (+100 is more than quality and seeders can
    // add up to), and within that, dubbing first, then multi-, two-, single-
    // voice. A dead one (0 known seeders) gets nothing: it sinks to the bottom.
    bool hasRussian = in.russian || (want.originalRussian && !in.ukrainian);
    if (want.preferRussian && hasRussian && !dead)
        s += 100 + (in.voiceRank == 4 ? 20 : in.voiceRank == 3 ? 15
                    : in.voiceRank == 2 ? 10 : in.voiceRank == 1 ? 5 : 0);
    // Ukrainian audio only, the UI in Russian: a working release, but rarely
    // the one wanted.
    if (want.preferRussian && in.ukrainian && !hasRussian) s -= 12;
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

char foldLatinCp(uint32_t cp)
{
    if (cp >= 0x00C0 && cp <= 0x00C5) return 'A';
    if (cp == 0x00C7) return 'C';
    if (cp >= 0x00C8 && cp <= 0x00CB) return 'E';
    if (cp >= 0x00CC && cp <= 0x00CF) return 'I';
    if (cp == 0x00D1) return 'N';
    if ((cp >= 0x00D2 && cp <= 0x00D6) || cp == 0x00D8) return 'O';
    if (cp >= 0x00D9 && cp <= 0x00DC) return 'U';
    if (cp == 0x00DD) return 'Y';

    if (cp >= 0x00E0 && cp <= 0x00E5) return 'a';
    if (cp == 0x00E7) return 'c';
    if (cp >= 0x00E8 && cp <= 0x00EB) return 'e';
    if (cp >= 0x00EC && cp <= 0x00EF) return 'i';
    if (cp == 0x00F1) return 'n';
    if ((cp >= 0x00F2 && cp <= 0x00F6) || cp == 0x00F8) return 'o';
    if (cp >= 0x00F9 && cp <= 0x00FC) return 'u';
    if (cp == 0x00FD || cp == 0x00FF) return 'y';

    if (cp >= 0x0100 && cp <= 0x0105) return (cp & 1) ? 'a' : 'A';
    if (cp >= 0x0106 && cp <= 0x010D) return (cp & 1) ? 'c' : 'C';
    if (cp >= 0x010E && cp <= 0x0111) return (cp & 1) ? 'd' : 'D';
    if (cp >= 0x0112 && cp <= 0x011B) return (cp & 1) ? 'e' : 'E';
    if (cp >= 0x011C && cp <= 0x0123) return (cp & 1) ? 'g' : 'G';
    if (cp >= 0x0124 && cp <= 0x0127) return (cp & 1) ? 'h' : 'H';
    if (cp >= 0x0128 && cp <= 0x012F) return (cp & 1) ? 'i' : 'I';
    if (cp == 0x0130) return 'I';
    if (cp == 0x0131) return 'i';
    if (cp >= 0x0134 && cp <= 0x0135) return (cp & 1) ? 'j' : 'J';
    if (cp >= 0x0136 && cp <= 0x0137) return (cp & 1) ? 'k' : 'K';
    if (cp >= 0x0139 && cp <= 0x0142) return (cp & 1) ? 'L' : 'l';
    if (cp >= 0x0143 && cp <= 0x0148) return (cp & 1) ? 'N' : 'n';
    if (cp >= 0x014C && cp <= 0x0151) return (cp & 1) ? 'o' : 'O';
    if (cp >= 0x0154 && cp <= 0x0159) return (cp & 1) ? 'r' : 'R';
    if (cp >= 0x015A && cp <= 0x0161) return (cp & 1) ? 's' : 'S';
    if (cp >= 0x0162 && cp <= 0x0167) return (cp & 1) ? 't' : 'T';
    if (cp >= 0x0168 && cp <= 0x0173) return (cp & 1) ? 'u' : 'U';
    if (cp >= 0x0174 && cp <= 0x0175) return (cp & 1) ? 'w' : 'W';
    if (cp == 0x0176 || cp == 0x0178) return 'Y';
    if (cp == 0x0177) return 'y';
    if (cp >= 0x0179 && cp <= 0x017E) return (cp & 1) ? 'Z' : 'z';
    return 0;
}

// A name for comparing: lower case, folded diacritics and "ё" -> "е",
// separators as single spaces, " padded ", without leading "[group]" or "the".
std::string comparable(std::string s)
{
    while (true)
    {
        while (!s.empty() && s[0] == ' ') s.erase(0, 1);
        if (!s.empty() && s[0] == '[')
        {
            size_t close = s.find(']');
            if (close != std::string::npos)
            {
                s.erase(0, close + 1);
                continue;
            }
        }
        break;
    }
    std::string w = words(lower(foldAscii(s)));
    if (w.compare(0, 5, " the ") == 0) w.erase(0, 4);
    return w;
}

} // namespace

std::string foldAscii(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80)
        {
            out += (char)c;
            continue;
        }
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size() && ((unsigned char)s[i + 1] & 0xC0) == 0x80)
        {
            uint32_t cp = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            if (cp == 0x0401)  // Ё -> Е
            {
                out += (char)0xD0;
                out += (char)0x95;
                i++;
                continue;
            }
            if (cp == 0x0451)  // ё -> е
            {
                out += (char)0xD0;
                out += (char)0xB5;
                i++;
                continue;
            }
            char ascii = foldLatinCp(cp);
            if (ascii)
            {
                out += ascii;
                i++;
                continue;
            }
        }
        out += (char)c;
    }
    return out;
}

bool isNonVideo(const std::string& title,
                const std::vector<int>& categories,
                const std::vector<std::string>& types)
{
    if (!categories.empty())
    {
        bool anyVideo = false, anyNonVideo = false;
        for (int c : categories)
        {
            if (c == 0 || (c >= 2000 && c < 3000) || (c >= 5000 && c < 6000) ||
                (c >= 8000 && c < 9000))
                anyVideo = true;
            else
                anyNonVideo = true;
        }
        if (anyNonVideo && !anyVideo) return true;
    }

    if (!types.empty())
    {
        bool anyVideo = false, anyExplicitNonVideo = false;
        for (const auto& t : types)
        {
            std::string lt = lower(t);
            if (lt == "movie" || lt == "serial" || lt == "anime" ||
                lt == "multfilm" || lt == "multserial" || lt == "documovie" ||
                lt == "docuserial" || lt == "tvshow")
                anyVideo = true;
            else if (lt == "game" || lt == "games" || lt == "music" ||
                     lt == "audio" || lt == "book" || lt == "books" ||
                     lt == "software")
                anyExplicitNonVideo = true;
        }
        if (anyExplicitNonVideo && !anyVideo) return true;
    }

    std::string low = lower(title);
    std::string w   = words(low);

    static const char* const kWords[] = {
        "ps4", "ps5", "ps3", "ps2", "xbox", "xbox360", "x360", "nsw", "nsp",
        "xci", "nsz", "gog", "fitgirl", "dodi", "xatab", "elamigos", "steamrip",
        "chovka", "decepticon", "codex", "skidrow", "plaza", "hoodlum",
        "empress", "rune", "tenoke", "audiobook", "epub", "fb2", "mobi", "djvu",
    };
    for (const char* k : kWords)
        if (hasWord(w, k)) return true;

    static const char* const kSubs[] = {
        "r.g. mechanics", "rg mechanics", "r.g. catalyst", "game of the year",
        "\xD0\xB0\xD1\x83\xD0\xB4\xD0\xB8\xD0\xBE\xD0\xBA\xD0\xBD\xD0\xB8\xD0\xB3\xD0\xB0", // аудиокнига
        "[ru/multi]", "[pc]", "soundtrack", "original score",
    };
    for (const char* s : kSubs)
        if (has(low, s)) return true;

    bool hasVideoTag = has(low, "1080p") || has(low, "720p") || has(low, "2160p") ||
                       has(low, "480p") || has(low, "web-dl") || has(low, "webrip") ||
                       has(low, "bdrip") || has(low, "bluray") || has(low, "remux") ||
                       has(low, "hdtv") || has(low, "dvdrip") || has(low, "hdrip") ||
                       has(low, ".mkv") || has(low, ".mp4") || has(low, ".avi") ||
                       has(low, "h264") || has(low, "h.264") || has(low, "x264") ||
                       has(low, "h265") || has(low, "h.265") || has(low, "x265") ||
                       has(low, "hevc") || hasWord(w, "avc") || hasWord(w, "av1") ||
                       hasWord(w, "av01");
    if (!hasVideoTag)
    {
        if (hasWord(w, "ost") || hasWord(w, "flac") || hasWord(w, "pdf"))
            return true;
        if (hasWord(w, "repack") && (hasWord(w, "license") || hasWord(w, "goty") ||
                                     hasWord(w, "steam") || has(low, "build ")))
            return true;
    }
    return false;
}

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

namespace
{
bool digitsOnly(const std::string& s)
{
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

// Digits at t[i..] (1 to max of them), advancing i; -1 when none.
int digitsAt(const std::string& t, size_t& i, size_t max)
{
    size_t k = 0;
    int n    = 0;
    while (i < t.size() && k < max && std::isdigit((unsigned char)t[i]))
    {
        n = n * 10 + (t[i] - '0');
        i++;
        k++;
    }
    if (k == 0 || (i < t.size() && std::isdigit((unsigned char)t[i]))) return -1;
    return n;
}

// One token (lower case) that names the episode: "s08e16", "s01e01e02",
// "s01e01-e02", "1x06", "e06", "ep06".
bool isEpisodeToken(const std::string& t)
{
    size_t i = 0;
    if (t.size() < 2) return false;
    if (t[0] == 's')
    {
        i = 1;
        if (digitsAt(t, i, 2) < 0 || i >= t.size() || t[i] != 'e') return false;
        i++;
        if (digitsAt(t, i, 3) < 0) return false;
        return i == t.size() || t[i] == 'e' || t[i] == '-';
    }
    if (std::isdigit((unsigned char)t[0]))
    {
        if (digitsAt(t, i, 2) < 0 || i >= t.size() || t[i] != 'x') return false;
        i++;
        if (digitsAt(t, i, 3) < 0) return false;
        return i == t.size() || t[i] == '-';
    }
    if (t[0] == 'e')
    {
        i = (t.size() > 2 && t[1] == 'p') ? 2 : 1;
        return digitsAt(t, i, 3) >= 0 && i == t.size();
    }
    return false;
}

// A release tag: resolution, source, codec, audio, language, scene flags --
// where the title part of a file name ends. `t` is lower case.
bool isTagToken(const std::string& raw, const std::string& next)
{
    std::string t = raw;
    size_t dash   = t.find('-');
    if (dash != std::string::npos && dash > 0) t = t.substr(0, dash);  // "x264-NTb", "web-dl"
    if (t.empty()) return false;

    static const char* const tags[] = {
        "4k", "8k", "uhd", "hdr", "hdr10", "dv", "dovi", "sdr", "10bit", "8bit", "hevc",
        "avc", "xvid", "divx", "x264", "x265", "h264", "h265", "av1", "web", "webrip",
        "webdl", "blu", "bluray", "bdrip", "brrip", "bdremux", "remux", "hdtv", "hdrip",
        "dvdrip", "dvd", "rip", "proper", "repack", "internal", "amzn", "nf", "dsnp",
        "hulu", "atvp", "hmax", "pcok", "multi", "rus", "eng", "ukr", "dub", "mvo", "dvo",
        "avo", "aac", "ac3", "eac3", "dts", "truehd", "atmos", "flac", "opus", "mp3",
    };
    for (const char* k : tags)
        if (t == k) return true;
    // "DDP5.1", "DD5.1", "AAC2.0": the channel count rides on the codec.
    for (const char* k : { "ddp", "dd5", "dd2", "aac", "eac3", "ac3", "dts" })
        if (t.compare(0, std::strlen(k), k) == 0) return true;
    // 720p, 1080p, 2160p, 1080i.
    if (t.size() >= 4 && t.size() <= 5 && (t.back() == 'p' || t.back() == 'i') &&
        digitsOnly(t.substr(0, t.size() - 1)))
        return true;
    // "H.264" split at the dot.
    if ((t == "h" || t == "x") && (next == "264" || next == "265")) return true;
    return false;
}

bool isYear(const std::string& t)
{
    if (t.size() != 4 || !digitsOnly(t)) return false;
    int y = std::atoi(t.c_str());
    return y >= 1900 && y <= 2099;
}
} // namespace

std::string episodeTitleOfFile(const std::string& path)
{
    size_t slash     = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    // The extension: a short run of letters and digits after the last dot.
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && base.size() - dot - 1 >= 2 &&
        base.size() - dot - 1 <= 4)
    {
        bool ext = true;
        for (size_t i = dot + 1; i < base.size(); i++)
            if (!std::isalnum((unsigned char)base[i])) ext = false;
        if (ext) base = base.substr(0, dot);
    }

    std::vector<std::string> tok;
    std::string cur;
    for (char c : base)
    {
        if (c == '.' || c == '_' || c == ' ')
        {
            if (!cur.empty()) tok.push_back(cur);
            cur.clear();
        }
        else
            cur += c;
    }
    if (!cur.empty()) tok.push_back(cur);
    std::vector<std::string> low;
    for (const auto& t : tok) low.push_back(lower(t));

    // Where the title starts: after the episode marker -- "S08E16", "1x06",
    // "E06", "Ep 06", "Серия 6", or a leading number ("06. Title").
    size_t from = 0;
    for (size_t i = 0; i < tok.size(); i++)
    {
        if (isEpisodeToken(low[i]))
        {
            from = i + 1;
            // "S01E16.E17": the rest of a double episode.
            while (from < tok.size() && isEpisodeToken(low[from])) from++;
            break;
        }
        if ((low[i] == "ep" || low[i] == "episode" || low[i] == "серия") &&
            i + 1 < tok.size() && digitsOnly(low[i + 1]) && low[i + 1].size() <= 3)
        {
            from = i + 2;
            break;
        }
    }
    if (from == 0 && !tok.empty() && digitsOnly(low[0]) && low[0].size() <= 3) from = 1;

    std::vector<std::string> out;
    for (size_t i = from; i < tok.size(); i++)
    {
        const std::string& t = low[i];
        const std::string next = i + 1 < tok.size() ? low[i + 1] : "";
        if (t[0] == '[') break;
        if (t[0] == '(')
        {
            std::string in = t.substr(1);
            if (!in.empty() && in.back() == ')') in.pop_back();
            if (isYear(in) || isTagToken(in, "")) break;
        }
        // A year ends it once there is a title before it ("1917" alone is one).
        if (isYear(t) && !out.empty()) break;
        if (isTagToken(t, next)) break;
        out.push_back(tok[i]);
    }
    // Separators left at either end: "Show - S01E02 - Title - 1080p".
    while (!out.empty() && out.front() == "-") out.erase(out.begin());
    while (!out.empty() && out.back() == "-") out.pop_back();

    std::string s;
    for (const auto& w : out)
    {
        if (!s.empty()) s += ' ';
        s += w;
    }
    return s;
}

bool episodeOfFile(const std::string& path, int packSeason, int& season, int& episode)
{
    int s = -1, e = -1;
    fileScope(path, s, e);
    if (e <= 0) return false;
    if (s <= 0) s = packSeason;
    if (s <= 0) return false;
    season  = s;
    episode = e;
    return true;
}

} // namespace release
