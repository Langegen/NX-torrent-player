#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Release names, read the way a person scanning a tracker reads them:
// "Slovo.patsana.S01.1080p.WEB-DL.H264" is a whole first season in 1080p from
// a web source; "Дюна: Часть вторая / Dune: Part Two (2024) BDRip 1080p | D"
// is a film with Russian dubbing. The sources screen shows this as tags and
// sorts on it, so the release that will simply play on a Switch comes first.
//
// Pure functions: tested on PC (pctest/test_release.cpp).
namespace release
{

struct Info
{
    int height = 0;         // 2160, 1080, 720, 480; 0 = not said
    std::string source;     // "WEB-DL", "WEBRip", "BluRay", "BDRip", "Remux",
                            // "HDTV", "DVDRip", "CAMRip"; "" = not said
    std::string codec;      // "HEVC", "H.264", "AV1", "XviD"; "" = not said
    bool hdr         = false;
    bool dolbyVision = false;
    bool tenBit      = false;
    bool cam         = false;  // a cinema recording

    // What part of a series it holds. -1 = not said.
    int season    = -1;
    int seasonTo  = -1;  // last season of a range ("S01-S03"); = season otherwise
    int episode   = -1;
    int episodeTo = -1;  // last episode of a range ("E01-E08", "серии 1-8")
    // A whole season (or several) rather than single episodes: a season
    // named with no episode, or an episode range that covers the season
    // ("серии 1-8 из 8").
    bool fullSeason = false;

    // Voice-overs it names: "Дубляж", "MVO", "DVO", "AVO", "LostFilm"...
    std::vector<std::string> voices;
    // The best kind of Russian voice-over named: 4 dubbing, 3 multi-voice (or
    // a studio, which is nearly always one), 2 two-voice, 1 single-voice /
    // amateur; 0 none.
    int voiceRank = 0;
    // The release group: "-NTb" at the end of a scene name, "[HDRezka]". "".
    std::string group;
    // Audio languages the name mentions: Russian (a voice-over, "Rus",
    // "русский") and Ukrainian ("Ukr", "4xUkr", "укр").
    bool russian   = false;
    bool ukrainian = false;
};

Info parse(const std::string& title);

// Short labels for the UI, most telling first: source, codec, HDR / DV,
// 10-bit, "season" scope, voices.
std::vector<std::string> tags(const Info& info);

// What the user is looking for.
struct Want
{
    bool series = false;
    int season  = 0;   // 0 = any (a film, or a whole show)
    int episode = 0;   // 0 = any
    // A voice / group picked last time for this show: favoured.
    std::string voice;
    std::string group;
    bool preferRussian = false;  // the UI is in Russian
};

// How good a choice this is for a Switch, higher is better. A release that
// cannot hold what is wanted (another season, another episode, a cam) comes
// back negative, and the screen hides it behind "All". `seeders` < 0 = not
// known; `size` 0 = not known.
int score(const Info& info, int seeders, int64_t size, const Want& want);

// Whether a release is of the title at all: the search for "Fallout" also
// finds "Mission.Impossible.Fallout.2018". True when one of `titles` (the
// localized and the original name) starts the name, or one of its " / " or
// " | " parts ("Фоллаут / Fallout (Season 1)"), whole words, case and
// punctuation aside, a leading "[group]" and "The" ignored.
bool titleMatches(const std::string& name, const std::vector<std::string>& titles);

// The years a name gives (1900..2039, four digits on their own): "1917
// (2019)" -> {1917, 2019}; a title's own number can be among them, so a
// caller checks whether ANY is the year it wants.
std::vector<int> yearsIn(const std::string& name);

// Which of a torrent's files is season `season`, episode `episode`, by name
// (S01E06, 1x06, "Серия 6", a leading "06." ...). Paths may carry folders
// ("Season 1/..."). -1 when none, or when two files match equally.
int matchEpisode(const std::vector<std::string>& paths, int season, int episode);

// The other way round: which episode a torrent file is, by its name (same
// forms as above), for a file picked by hand from a pack. `packSeason` stands
// in when neither the name nor its folders give a season (0 = unknown).
// false when no episode is named, or no season is known.
bool episodeOfFile(const std::string& path, int packSeason, int& season, int& episode);

// The episode's own title out of a file name, for a list of a pack's files:
// "Family.Guy.S08E16.April.in.Quahog.720p.WEB-DL.mkv" -> "April in Quahog",
// "06. Пилот.mkv" -> "Пилот". What follows the episode marker, up to the first
// release tag. A name with no marker gives its words up to the tags ("" when
// nothing is left, as in "Серия 2.avi" or "S01E02.1080p.mkv").
std::string episodeTitleOfFile(const std::string& path);

} // namespace release
