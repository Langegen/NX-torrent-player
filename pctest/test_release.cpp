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

    // The episode's own title, for the player's episode list.
    CHECK(release::episodeTitleOfFile(
              "Family.Guy.S08.720p/Family.Guy.S08E16.April.in.Quahog.720p.WEB-DL.DDP5.1.H.264-NTb.mkv") ==
          "April in Quahog");
    CHECK(release::episodeTitleOfFile("Family.Guy.S08E15.Quagmire's.Dad.mkv") == "Quagmire's Dad");
    CHECK(release::episodeTitleOfFile("Show - S01E02 - The Pilot - 1080p.mkv") == "The Pilot");
    CHECK(release::episodeTitleOfFile("show.1x06.spider-man.x264-GRP.mkv") == "spider-man");
    CHECK(release::episodeTitleOfFile("Show.S01E01E02.Two.Parts.[rus].mkv") == "Two Parts");
    CHECK(release::episodeTitleOfFile(numbered[0]) == "Пилот");
    CHECK(release::episodeTitleOfFile(pack[2]).empty());
    CHECK(release::episodeTitleOfFile(russian[1]).empty());
    CHECK(release::episodeTitleOfFile(seasons[0]).empty());
    CHECK(release::episodeTitleOfFile("Show.S02E03.Title.2019.1080p.mkv") == "Title");
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
    // A resolution after a dash is not the end of a range.
    auto d = release::parse("Show S01E05 - 720p");
    CHECK(d.season == 1 && d.seasonTo == 1 && d.episode == 5 && d.episodeTo == 5);
    auto e = release::parse("Show S02 - 480p WEB-DL");
    CHECK(e.season == 2 && e.seasonTo == 2);
    auto f = release::parse("Show S03E10-E08");   // ends before it starts
    CHECK(f.episode == 10 && f.episodeTo == 10);
    // Real ranges still are.
    auto g = release::parse("Show S01E01-E08 1080p");
    CHECK(g.episode == 1 && g.episodeTo == 8);
    auto h = release::parse("Show S01-S03 720p");
    CHECK(h.season == 1 && h.seasonTo == 3);
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

static void test_russian_first()
{
    std::printf("[8/8] Russian UI: rutracker shorthands, Russian audio ranks first...\n");
    auto d = release::parse("Дюна: Часть вторая / Dune: Part Two (2024) BDRip 1080p | D");
    CHECK(hasVoice(d, "Дубляж") && d.voiceRank == 4 && d.russian);
    auto p = release::parse("Фильм / Film (2023) WEB-DL 720p | P");
    CHECK(hasVoice(p, "MVO") && p.voiceRank == 3);
    auto l2 = release::parse("Фильм / Film (2023) WEB-DLRip | L2");
    CHECK(hasVoice(l2, "DVO") && l2.voiceRank == 2);
    auto l1 = release::parse("Фильм / Film (2023) HDRip | L1");
    CHECK(hasVoice(l1, "AVO") && l1.voiceRank == 1);
    auto db = release::parse("Фильм (2023) WEB-DL 1080p | Дб, Ст");
    CHECK(hasVoice(db, "Дубляж"));
    auto pm = release::parse("Фильм (2023) WEB-DL 1080p | ПМ");
    CHECK(hasVoice(pm, "MVO"));
    // Without Cyrillic (or "Rus") the Latin marks mean nothing Russian.
    auto engDub = release::parse("[SubsPlease] Anime Show - 05 (1080p) (Dub)");
    CHECK(engDub.voices.empty() && !engDub.russian);
    auto scene = release::parse("Film.2023.1080p.WEB-DL.DDP5.1.H.264-D");
    CHECK(scene.voices.empty());
    // "D" inside a word is not dubbing.
    CHECK(!hasVoice(release::parse("Фильм / 3D Film (2023) BDRip 1080p"), "Дубляж"));

    release::Want ru;
    ru.preferRussian = true;
    auto eng = release::parse("Film.2023.1080p.AMZN.WEB-DL.DDP5.1.H.264-FLUX");
    auto rus720 = release::parse("Фильм / Film (2023) WEB-DL 720p | P");
    int e = release::score(eng, 200, 6LL << 30, ru);
    int r = release::score(rus720, 5, 3LL << 30, ru);
    std::printf("      English 1080p/200 seeds %d, Russian 720p/5 seeds %d\n", e, r);
    CHECK(r > e);
    // A dead Russian release does not beat a live English one.
    CHECK(release::score(rus720, 0, 3LL << 30, ru) < e);
    // Dubbing over multi-voice, all else equal.
    CHECK(release::score(release::parse("Фильм / Film (2023) WEB-DL 1080p | D"), 50, 0, ru) >
          release::score(release::parse("Фильм / Film (2023) WEB-DL 1080p | P"), 50, 0, ru));
    // "Rus" alone counts.
    CHECK(release::score(release::parse("Film.2023.720p.WEB-DL.Rus.Eng"), 10, 0, ru) > e);
    // Standard unspecified-codec 1080p WEB-DL (H.264 on Russian trackers) with
    // high seeds must outrank a low-seed BDRip-HEVC release.
    auto webdl = release::parse("Закулисье реальности / Backrooms (2026) WEB-DL 1080p | Дубляж");
    auto hevc = release::parse("Закулисье реальности / Backrooms (2026) BDRip-HEVC 1080p | D");
    CHECK(release::score(webdl, 208, 9800LL << 20, ru) > release::score(hevc, 26, 7700LL << 20, ru));
    // An English UI keeps the old order.
    release::Want en;
    CHECK(release::score(eng, 200, 6LL << 30, en) > release::score(rus720, 5, 3LL << 30, en));
    std::printf("      PASSED\n");
}

static void test_jacred_formats_and_profiles()
{
    std::printf("[9/9] Jacred formats, enrichment, diacritics, anti-noise, profiles...\n");

    // 1. All Russian season/episode formats that previously broke.
    auto f1 = release::parse("Фоллаут (2 сезон: 1-8 серии из 8) / Fallout / 2025 / ПМ (Jaskier), СТ / WEB-DL (1080p)");
    CHECK(f1.season == 2 && f1.seasonTo == 2 && f1.episode == 1 && f1.episodeTo == 8 && f1.fullSeason);
    CHECK(f1.hasSeason(2) && !f1.hasSeason(1));

    auto f2 = release::parse("Во все тяжкие (1-5 сезоны: 1-62 серии из 62) / Breaking Bad / 2008-2013 / АП (Пучков)");
    CHECK(f2.season == 1 && f2.seasonTo == 5 && f2.episode == 1 && f2.episodeTo == 62 && f2.fullSeason);
    CHECK(f2.hasSeason(1) && f2.hasSeason(3) && f2.hasSeason(5) && !f2.hasSeason(6));

    auto f3 = release::parse("Во все тяжкие 1-5 сезон (1-62 из 62) / Breaking Bad (2008-2013) BDRip | AV1 | 4К");
    CHECK(f3.season == 1 && f3.seasonTo == 5 && f3.fullSeason && f3.height == 2160 && f3.codec == "AV1");

    auto f4 = release::parse("Во все тяжкие / Breaking Bad / 1 сезон / Серии 1-7 (7) [2008, WEB-DL 1080p]");
    CHECK(f4.season == 1 && f4.seasonTo == 1 && f4.episode == 1 && f4.episodeTo == 7 && f4.fullSeason);

    auto f5 = release::parse("Во все тяжкие (1 сезон: 7 серий из 7) / Breaking Bad / 2008 / ПМ (LostFilm)");
    CHECK(f5.season == 1 && f5.seasonTo == 1 && f5.episode == 1 && f5.episodeTo == 7 && f5.fullSeason);

    auto f6 = release::parse("Fallout / Fallout / 1 сезон 8 серия [2024]");
    CHECK(f6.season == 1 && f6.seasonTo == 1 && f6.episode == 8 && f6.episodeTo == 8 && !f6.fullSeason);

    auto f7 = release::parse("Слово пацана. Кровь на асфальте [01x01-08 из 08] (2023) WEB-DL 1080p");
    CHECK(f7.season == 1 && f7.seasonTo == 1 && f7.episode == 1 && f7.episodeTo == 8 && f7.fullSeason);

    auto f8 = release::parse("Поднятие уровня в одиночку: Восстаньте из тени (ТВ-2) / Ore dake Level Up na Ken");
    CHECK(f8.season == 2 && f8.seasonTo == 2 && f8.fullSeason);

    // 2. Diacritic folding in titleMatches.
    CHECK(release::titleMatches("Сёгун (1 сезон) / Shôgun (2024) WEB-DL 1080p", { "Сёгун", "Shōgun" }));
    CHECK(release::titleMatches("Shogun.2024.S01.1080p.WEB-DL", { "Сёгун", "Shōgun" }));
    CHECK(release::titleMatches("Сегун / Shogun / 1 сезон", { "Сёгун", "Shōgun" }));

    // 3. Jacred metadata enrichment (voices, languages, HDR, seasons without clobbering S01-S05).
    auto bare = release::parse("Фоллаут / Fallout / 2024 / WEB-DL");
    CHECK(!bare.russian && bare.height == 0 && !bare.hasAnySeason());
    release::ExternalMeta m;
    m.quality = 2160;
    m.videotype = "hdr";
    m.voices = { "Red Head Sound", "Субтитры", "Две дорожки" };
    m.seasons = { 1 };
    m.languages = { "rus", "eng" };
    release::enrich(bare, m);
    CHECK(bare.height == 2160 && bare.hdr && bare.russian);
    CHECK(bare.season == 1 && bare.fullSeason && bare.hasSeason(1));
    CHECK(hasVoice(bare, "RHS"));
    CHECK(!hasVoice(bare, "Субтитры") && !hasVoice(bare, "Две дорожки"));

    // Narrow Jacred seasons=[1] must not shrink S01-S05 parsed from title.
    release::ExternalMeta narrow;
    narrow.seasons = { 1 };
    release::enrich(f2, narrow);
    CHECK(f2.hasSeason(1) && f2.hasSeason(5));

    // 4. Anti-noise filter: rejects games/audio/books, keeps video REPACKs & anime FLACs.
    CHECK(release::isNonVideo("Fallout 3 (2009) [Ru/Multi] Repack FitGirl [Game of the Year Edition]"));
    CHECK(release::isNonVideo("The Last of Us Part II (2020) [PS4] [EUR] License"));
    CHECK(release::isNonVideo("Fallout.OST.FLAC"));
    CHECK(release::isNonVideo("Some Movie", { 4000 }, {}));
    CHECK(!release::isNonVideo("Fallout.S01E01.REPACK.1080p.WEB-DL.H264-FLUX", { 5000 }, { "serial" }));
    CHECK(!release::isNonVideo("[Breeze] Solo Leveling S01 v2 [1080p BD FLAC][Dual Audio].mkv"));

    // 5. Series episode logic: season pack > ongoing range > single episode; wrong single episode -1000.
    release::Want wantEp;
    wantEp.series = true;
    wantEp.season = 1;
    wantEp.episode = 1;
    wantEp.preferRussian = true;
    CHECK(release::score(f1, 200, 0, wantEp) < 0); // season 2 rejected when wanting S1
    CHECK(release::score(f6, 200, 0, wantEp) < 0); // single ep 8 rejected when wanting E1
    auto partial = release::parse("Фоллаут (1 сезон: 1-4 серии) / Fallout / 2024 / ПМ (Jaskier) / WEB-DL (1080p)");
    CHECK(release::score(partial, 100, 0, wantEp) > 0);
    auto full1 = release::parse("Фоллаут (1 сезон: 1-8 серии из 8) / Fallout / 2024 / ПМ (Jaskier) / WEB-DL (1080p)");
    auto ep1Only = release::parse("Фоллаут (1 сезон: 1 серия) / Fallout / 2024 / ПМ (Jaskier) / WEB-DL (1080p)");
    CHECK(release::score(full1, 100, 0, wantEp) > release::score(ep1Only, 100, 0, wantEp));

    // 6. Platform profiles (Switch vs PC/Android) and CAMRip/dead handling.
    release::Want sw, pc;
    sw.switchProfile = true;
    pc.switchProfile = false;
    auto r1080 = release::parse("Film.2024.1080p.WEB-DL.H264");
    auto r4k   = release::parse("Film.2024.2160p.WEB-DL.HDR.HEVC.10bit");
    auto r720  = release::parse("Film.2024.720p.WEB-DL.H264");
    CHECK(release::score(r1080, 100, 5LL << 30, sw) > release::score(r4k, 100, 20LL << 30, sw));
    CHECK(release::score(r4k, 100, 20LL << 30, pc) > release::score(r720, 100, 3LL << 30, pc));

    auto cam = release::parse("Film.2026.CAMRip");
    CHECK(cam.cam && release::score(cam, 500, 1LL << 30, pc) == -200);

    // Unknown seeders beats confirmed 0 seeders.
    auto unk = bare;
    unk.seedersUnknown = true;
    CHECK(release::score(unk, 0, 0, sw) > release::score(bare, 0, 0, sw));

    // "Мультиязычная" / "Субтитры" in info.voices without Russian language tag
    // must NOT falsely flag English anime torrents as Russian dubs.
    auto enMulti = release::parse("[ToonsHub] Solo Leveling S01 1080p CR WEB-DL (Multi-Audio, Multi-Subs)");
    release::ExternalMeta enMeta;
    enMeta.quality   = 1080;
    enMeta.voices    = { "\xD0\x9C\xD1\x83\xD0\xBB\xD1\x8C\xD1\x82\xD0\xB8\xD1\x8F\xD0\xB7\xD1\x8B\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F",
                         "\xD0\xA1\xD1\x83\xD0\xB1\xD1\x82\xD0\xB8\xD1\x82\xD1\x80\xD1\x8B",
                         "Multi-Audio",
                         "Dual Audio" };
    release::enrich(enMulti, enMeta);
    CHECK(!enMulti.russian && enMulti.voices.empty());

    // Generic "Дубляж" on foreign trackers without Russian language tag must not
    // mark English dubs ("Eng/Jp Dub" on knaben/nyaa) as Russian, while Russian
    // trackers (rutor) keep it.
    auto knabenDub = release::parse("Solo Leveling - S01 - 2160p HDR Ai Upscale 5.1+2.0 Eng/Jp Dub MKV -Mesc");
    release::ExternalMeta knMeta;
    knMeta.quality  = 2160;
    knMeta.voices   = { "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" };
    knMeta.trackers = { "knaben" };
    release::enrich(knabenDub, knMeta);
    CHECK(!knabenDub.russian && knabenDub.voices.empty());

    // Even when Jacred also passes languages={"eng","rus"} (e.g. softsubs) on a foreign
    // tracker, generic "Дубляж" without Russian title context must not grant voiceRank=4.
    auto knabenSub = release::parse("Solo Leveling - S01 - 2160p HDR Ai Upscale 5.1+2.0 Eng/Jp Dub MKV -Mesc");
    knMeta.languages = { "eng", "rus" };
    release::enrich(knabenSub, knMeta);
    CHECK(knabenSub.voices.empty() && knabenSub.voiceRank == 0);

    auto rutorDub = release::parse("1+1 / Intouchables (2011) BDRip-AVC by msltel");
    release::ExternalMeta rtMeta;
    rtMeta.voices   = { "\xD0\x94\xD1\x83\xD0\xB1\xD0\xBB\xD1\x8F\xD0\xB6" };
    rtMeta.trackers = { "rutor" };
    release::enrich(rutorDub, rtMeta);
    CHECK(rutorDub.russian && rutorDub.voices.size() == 1 && rutorDub.voiceRank == 4);

    // Season pack size is evaluated per-episode, so a 48 GB 8-episode 1080p
    // Russian season pack (~6 GB/ep) with 180 seeds beats a 15 GB 8-ep 30-seed pack on Switch.
    wantEp.switchProfile = true;
    wantEp.preferRussian = true;
    auto bigPack = release::parse("Фоллаут (1 сезон: 1-8 серии из 8) / Fallout / 2024 / ПМ (Jaskier) / WEB-DL (1080p)");
    auto smallPack = release::parse("Фоллаут (1 сезон: 1-8 серии из 8) / Fallout / 2024 / ПМ (Jaskier) / WEB-DL (1080p)");
    CHECK(release::packEpisodeEstimate(bigPack) == 8);
    CHECK(release::score(bigPack, 180, 48LL << 30, wantEp) >
          release::score(smallPack, 30, 15LL << 30, wantEp));

    std::printf("      PASSED\n");
}

static void test_switch_compat_and_av1()
{
    std::printf("[10/10] Switch codec compatibility (AV1, Hi10P), badges, and ranking...\n");

    // 1. Codec parsing: av01 -> AV1; Hi10P without explicit codec -> H.264 10-bit;
    // HEVC Hi10P keeps HEVC.
    auto av01 = release::parse("Show.2024.1080p.WEB-DL.AV01");
    CHECK(av01.codec == "AV1" && !av01.tenBit);
    auto hi10 = release::parse("Anime.S01.1080p.BDRip.Hi10P-Group");
    CHECK(hi10.codec == "H.264" && hi10.tenBit);
    auto hevcHi10 = release::parse("Anime.S01.1080p.HEVC.Hi10P-Group");
    CHECK(hevcHi10.codec == "HEVC" && hevcHi10.tenBit);
    CHECK(!release::isNonVideo("Movie.2024.1080p.AV1.FLAC"));
    CHECK(!release::isNonVideo("Movie.2024.1080p.AV01.FLAC"));

    // 2. Switch hardware decoder compatibility helpers.
    auto av1_10 = release::parse("Сверхъестественное / Supernatural (1-15 сезоны) (2005-2020) "
                                 "BDRip 1080p AV1 10-bit | NewStudio");
    auto h264_8 = release::parse("Film.2024.1080p.WEB-DL.H264");
    auto hevc_8 = release::parse("Film.2024.1080p.BluRay.x265");
    auto unnamed = release::parse("Film.2024.1080p.WEB-DL");
    CHECK(release::isSoftwareDecodedOnSwitch(av01));
    CHECK(release::isSoftwareDecodedOnSwitch(av1_10));
    CHECK(release::isSoftwareDecodedOnSwitch(hi10));
    CHECK(!release::isSoftwareDecodedOnSwitch(h264_8));
    CHECK(!release::isSoftwareDecodedOnSwitch(hevc_8));
    CHECK(!release::isSoftwareDecodedOnSwitch(hevcHi10));
    CHECK(!release::isSoftwareDecodedOnSwitch(unnamed));

    // 3. Wi-Fi heaviness check: 88.2 GB over 15 seasons is light per episode,
    // while 4K, Remux, and oversized movies are heavy.
    CHECK(!release::isHeavyForWifi(av1_10, 94704000000LL, true));
    CHECK(!release::isHeavyForWifi(hevcHi10, 15LL << 30, true));
    CHECK(release::isHeavyForWifi(release::parse("Film.2024.2160p.WEB-DL.HEVC"), 20LL << 30, false));
    CHECK(release::isHeavyForWifi(release::parse("Film.2024.1080p.BluRay.Remux"), 25LL << 30, false));
    CHECK(release::isHeavyForWifi(h264_8, 35LL << 30, false));

    // 4. Real-world Supernatural S05E17 scenario on Switch: AV1 10-bit (1 seed, Russian)
    // must rank below both Russian 720p H.264 and well-seeded 1080p HEVC, and must
    // never be eligible for "best choice".
    release::Want swWant;
    swWant.series        = true;
    swWant.season        = 5;
    swWant.episode       = 17;
    swWant.preferRussian = true;
    swWant.switchProfile = true;

    auto hevcEn = release::parse("Supernatural Season 05 S05 (1080p Bluray x265 HEVC AAC 5.1 Joy)");
    auto ru720  = release::parse("Сверхъестественное (5 сезон) / Supernatural (2009) BDRip 720p | Rus");
    auto hevc10 = release::parse("Аниме (5 сезон) BDRip 1080p HEVC 10-bit | AniLibria");
    auto hi10Ru = release::parse("Аниме (5 сезон) BDRip 1080p Hi10P | AniLibria");

    CHECK(release::score(ru720, 5, 20LL << 30, swWant) >
          release::score(av1_10, 1, 94704000000LL, swWant));
    CHECK(release::score(hevcEn, 209, 7LL << 30, swWant) >
          release::score(av1_10, 1, 94704000000LL, swWant));
    CHECK(release::score(hevc10, 30, 15LL << 30, swWant) >
          release::score(hi10Ru, 30, 15LL << 30, swWant));

    CHECK(!release::isRecommendedEligible(av1_10, 1, swWant));
    CHECK(!release::isRecommendedEligible(hi10Ru, 30, swWant));
    CHECK(!release::isRecommendedEligible(release::parse("Show.2024.1080p.DV.HEVC"), 50, swWant));
    CHECK(!release::isRecommendedEligible(ru720, 0, swWant));
    CHECK(release::isRecommendedEligible(hevcEn, 209, swWant));
    CHECK(release::isRecommendedEligible(hevc10, 30, swWant));

    // 5. PC / Android profile stays unaffected: AV1 is eligible and keeps its priority.
    release::Want pcWant = swWant;
    pcWant.switchProfile = false;
    CHECK(release::isRecommendedEligible(av1_10, 10, pcWant));
    CHECK(release::score(av1_10, 10, 94704000000LL, pcWant) >
          release::score(hevcEn, 209, 7LL << 30, pcWant));

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
    test_russian_first();
    test_jacred_formats_and_profiles();
    test_switch_compat_and_av1();
    std::printf("\nALL TESTS PASSED\n");
    return 0;
}
