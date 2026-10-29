// Offline tests for app/release.cpp: release names as Russian and English
// trackers actually write them, the recommended order, and finding an episode
// inside a season pack.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "release.hpp"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "      FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

static bool hasVoice(const release::Info& in, const std::string& v)
{
    for (const auto& x : in.voices)
        if (x == v) return true;
    return false;
}

static void test_scene_names()
{
    std::printf("[1/6] Scene names: quality, source, codec, HDR, scope, group...\n");
    auto a = release::parse("Slovo.patsana.S01.1080p.WEB-DL.H264-NTb");
    CHECK(a.height == 1080 && a.source == "WEB-DL" && a.codec == "H.264");
    CHECK(a.season == 1 && a.episode == -1 && a.fullSeason);
    CHECK(a.group == "NTb");

    auto b = release::parse("Slovo.patsana.S01E06.720p.WEBRip.x264");
    CHECK(b.height == 720 && b.source == "WEBRip" && b.codec == "H.264");
    CHECK(b.season == 1 && b.episode == 6 && b.episodeTo == 6 && !b.fullSeason);

    auto c = release::parse("Dune.Part.Two.2024.2160p.UHD.BluRay.REMUX.DV.HDR.HEVC.TrueHD-FGT");
    CHECK(c.height == 2160 && c.source == "Remux" && c.codec == "HEVC");
    CHECK(c.dolbyVision && c.hdr);
    CHECK(c.season == -1 && c.episode == -1);  // 2024 is a year, not a season

    auto d = release::parse("The.Show.S02E01-E08.1080p.10bit.x265");
    CHECK(d.season == 2 && d.episode == 1 && d.episodeTo == 8 && d.tenBit);

    auto e = release::parse("Show.S01-S03.Complete.720p");
    CHECK(e.season == 1 && e.seasonTo == 3 && e.fullSeason);

    auto f = release::parse("show.1x06.hdtv.xvid");
    CHECK(f.season == 1 && f.episode == 6 && f.source == "HDTV" && f.codec == "XviD");

    auto g = release::parse("Movie.2023.1920x1080.WEB-DL");
    CHECK(g.height == 1080 && g.season == -1 && g.episode == -1);
    std::printf("      PASSED\n");
}

static void test_russian_names()
{
    std::printf("[2/6] Russian tracker titles: seasons, episodes, voices...\n");
    auto a = release::parse("Слово пацана. Кровь на асфальте [01x01-08 из 08] (2023) "
                            "WEB-DL 1080p | Оригинал");
    CHECK(a.height == 1080 && a.source == "WEB-DL");
    CHECK(a.season == 1);

    auto b = release::parse("Фоллаут / Fallout [Сезон: 1, Серии: 1-8 из 8] (2024) WEB-DL "
                            "1080p | LostFilm, HDRezka Studio");
    CHECK(b.season == 1 && b.episode == 1 && b.episodeTo == 8 && b.fullSeason);
    CHECK(hasVoice(b, "LostFilm") && hasVoice(b, "HDRezka"));
    CHECK(!b.hdr);  // "HDRezka" is not HDR

    auto c = release::parse("Дюна: Часть вторая / Dune: Part Two (2024) BDRip 1080p | "
                            "Дублированный");
    CHECK(c.height == 1080 && c.source == "BDRip");
    CHECK(hasVoice(c, "Дубляж"));
    CHECK(c.season == -1);

    auto d = release::parse("Сериал (3 сезон, 5 серия) WEBRip 720p | MVO (Кубик в Кубе)");
    CHECK(d.season == 3 && d.episode == 5 && !d.fullSeason);
    CHECK(hasVoice(d, "MVO") && hasVoice(d, "Кубик в Кубе"));

    auto e = release::parse("ШОУ (СЕЗОНЫ 1-2) HDTVRip | Профессиональный (многоголосый)");
    CHECK(e.season == 1 && e.seasonTo == 2 && e.fullSeason && hasVoice(e, "MVO"));

    auto f = release::parse("Новый фильм (2024) CAMRip");
    CHECK(f.cam);
    std::printf("      PASSED\n");
}

static void test_tags()
{
    std::printf("[3/6] Tags for the UI...\n");
    auto t = release::tags(release::parse("Show.S01.2160p.WEB-DL.HDR.HEVC.10bit | LostFilm"));
    CHECK(t.size() == 4);
    CHECK(t[0] == "WEB-DL" && t[1] == "HEVC 10-bit" && t[2] == "HDR" && t[3] == "LostFilm");
    std::printf("      PASSED\n");
}

static void test_order()
{
    std::printf("[4/6] Recommended order: 1080p H.264 over 4K HEVC HDR, seeds, scope...\n");
    release::Want film;
    int hd = release::score(release::parse("Film.2024.1080p.WEB-DL.H264"), 200, 6LL << 30, film);
    int uhd = release::score(release::parse("Film.2024.2160p.WEB-DL.HDR.HEVC.10bit"), 200,
                             20LL << 30, film);
    int remux = release::score(release::parse("Film.2024.1080p.BluRay.REMUX.AVC"), 200,
                               35LL << 30, film);
    int dead = release::score(release::parse("Film.2024.1080p.WEB-DL.H264"), 0, 6LL << 30, film);
    int cam = release::score(release::parse("Film.2024.CAMRip"), 900, 1LL << 30, film);
    std::printf("      1080p %d, 4K HDR %d, remux %d, dead %d, cam %d\n", hd, uhd, remux, dead,
                cam);
    CHECK(hd > uhd);
    CHECK(hd > remux);
    CHECK(hd > dead);
    CHECK(cam < 0);

    release::Want ep;
    ep.series  = true;
    ep.season  = 1;
    ep.episode = 6;
    CHECK(release::score(release::parse("Show.S02.1080p"), 50, 0, ep) < 0);       // other season
    CHECK(release::score(release::parse("Show.S01E05.1080p"), 50, 0, ep) < 0);    // other episode
    CHECK(release::score(release::parse("Show.S01E01-E08.1080p"), 50, 0, ep) > 0);
    CHECK(release::score(release::parse("Show.S01.1080p"), 50, 0, ep) > 0);       // the season
    CHECK(release::score(release::parse("Show.S01-S03.1080p"), 50, 0, ep) > 0);
    // The voice picked last time wins over an otherwise equal release.
    release::Want again = ep;
    again.voice = "LostFilm";
    CHECK(release::score(release::parse("Show.S01.1080p | LostFilm"), 50, 0, again) >
          release::score(release::parse("Show.S01.1080p | HDRezka"), 50, 0, again));
    std::printf("      PASSED\n");
}

static void test_match_episode()
{
    std::printf("[5/6] Finding an episode in a season pack...\n");
    std::vector<std::string> pack = {
        "Slovo.patsana.S01.1080p/Slovo.patsana.S01E01.1080p.mkv",
        "Slovo.patsana.S01.1080p/Slovo.patsana.S01E02.1080p.mkv",
        "Slovo.patsana.S01.1080p/Slovo.patsana.S01E06.1080p.mkv",
        "Slovo.patsana.S01.1080p/Sample/sample.mkv",
    };
    CHECK(release::matchEpisode(pack, 1, 6) == 2);
    CHECK(release::matchEpisode(pack, 1, 3) == -1);
    CHECK(release::matchEpisode(pack, 2, 6) == -1);

    std::vector<std::string> numbered = { "Сериал/01. Пилот.mkv", "Сериал/02. Вторая.mkv",
                                          "Сериал/10. Десятая.mkv" };
    CHECK(release::matchEpisode(numbered, 1, 2) == 1);
    CHECK(release::matchEpisode(numbered, 1, 10) == 2);

    std::vector<std::string> seasons = { "Season 1/E05.mkv", "Season 2/E05.mkv" };
    CHECK(release::matchEpisode(seasons, 2, 5) == 1);

    std::vector<std::string> russian = { "Сезон 1/Серия 1.avi", "Сезон 1/Серия 2.avi" };
    CHECK(release::matchEpisode(russian, 1, 2) == 1);

    std::vector<std::string> ambiguous = { "a/E05.mkv", "b/E05.mkv" };
    CHECK(release::matchEpisode(ambiguous, 1, 5) == -1);

    // A file picked by hand: which episode it is.
    int s = 0, e = 0;
    CHECK(release::episodeOfFile(pack[2], 0, s, e) && s == 1 && e == 6);
    CHECK(release::episodeOfFile(seasons[1], 0, s, e) && s == 2 && e == 5);
    CHECK(release::episodeOfFile(russian[1], 0, s, e) && s == 1 && e == 2);
    // No season in the name: the pack's own.
    CHECK(release::episodeOfFile(numbered[2], 3, s, e) && s == 3 && e == 10);
    CHECK(!release::episodeOfFile(numbered[2], 0, s, e));
    CHECK(!release::episodeOfFile(pack[3], 1, s, e));  // a sample, no episode
    std::printf("      PASSED\n");
}

static void test_edges()
{
    std::printf("[6/6] Edge cases: empty, years, resolutions...\n");
    auto a = release::parse("");
    CHECK(a.height == 0 && a.season == -1 && a.voices.empty());
    auto b = release::parse("Blade Runner 2049 (2017) 1080p");
    CHECK(b.season == -1 && b.episode == -1 && b.height == 1080);
    auto c = release::parse("S1E1");
    CHECK(c.season == 1 && c.episode == 1);
    std::printf("      PASSED\n");
}

static void test_title_match()
{
    std::printf("[7/7] Is it the title at all: Fallout is not Mission: Impossible...\n");
    const std::vector<std::string> fallout = { "Фоллаут", "Fallout" };
    using release::titleMatches;
    // Real names from a search for "Fallout" (ru.jac.black).
    CHECK(!titleMatches("Mission.Impossible.Fallout.2018.1080p.WEB-DL.DD5.1.H264-CMRG", fallout));
    CHECK(!titleMatches("Mission: Impossible - Fallout (2018) 1080p BluRay x264", fallout));
    CHECK(titleMatches("Фолаут (1 сезон) / Fallout (Season 1) (2024) WEB-DL 1080p H.265", fallout));
    CHECK(titleMatches("Fallout.S01E01.1080p.WEB.h264-ETHEL", fallout));
    CHECK(titleMatches("Фоллаут / Fallout / Сезон: 1 / Серии: 1-8 из 8 [2024, WEB-DL 1080p]",
                       fallout));
    CHECK(titleMatches("[LostFilm] Fallout - S01E03", fallout));
    CHECK(!titleMatches("Falloutboy.Live.2010", fallout));  // whole words only
    // Case, punctuation, "ё", "The".
    CHECK(titleMatches("ДЮНА: ЧАСТЬ ВТОРАЯ / Dune: Part Two (2024) BDRip",
                       { "Дюна: Часть вторая", "Dune: Part Two" }));
    CHECK(titleMatches("Елки 12 (2026) WEB-DL", { "Ёлки 12" }));
    CHECK(titleMatches("Bear.S03E01.1080p", { "Медведь", "The Bear" }));
    CHECK(titleMatches("anything", {}));  // nothing to compare: no opinion

    // Dots turned into spaces by the index: the codec still reads.
    CHECK(release::parse("Fallout S01E01 The End 1080p AMZN WEB-DL DDP5 1 H 264-NTb").codec ==
          "H.264");
    CHECK(release::parse("Show S01 2160p x 265").codec == "HEVC");
    // A Russian UI: the Ukrainian-only season pack no longer beats the
    // episode itself (both real, from the same search).
    release::Want ep;
    ep.series = true;
    ep.season = 1;
    ep.episode = 1;
    ep.preferRussian = true;
    auto ukr = release::parse("Фолаут (1 сезон) / Fallout (Season 1) (2024) WEB-DL 1080p H.265 "
                              "4xUkr/Eng | sub Ukr/Eng");
    auto flux = release::parse("Fallout S01E01 The End 1080p AMZN WEB-DL DDP5 1 Atmos H 264-FLUX");
    CHECK(ukr.ukrainian && !ukr.russian);
    CHECK(release::score(flux, 62, 0, ep) > release::score(ukr, 42, 0, ep));
    auto both = release::parse("Fallout.S01.1080p.WEB-DL.Rus.Ukr.Eng");
    CHECK(both.russian && both.ukrainian);

    auto y = release::yearsIn("1917 (2019) / Blade Runner 2049 (2017) 1080p");
    CHECK(y.size() == 3 && y[0] == 1917 && y[1] == 2019 && y[2] == 2017);
    CHECK(release::yearsIn("Dune.2021.2160p").size() == 1);
    CHECK(release::yearsIn("[2026, США, WEB-DL 1080p]")[0] == 2026);
    CHECK(release::yearsIn("x264 1080p 5.1 10bit").empty());
    std::printf("      PASSED\n");
}

int main()
{
    std::printf("=========================================\n");
    std::printf(" NX TORRENT PLAYER: RELEASE NAMES        \n");
    std::printf("=========================================\n\n");
    test_scene_names();
    test_russian_names();
    test_tags();
    test_order();
    test_match_episode();
    test_edges();
    test_title_match();
    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
