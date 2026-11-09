#include "home.hpp"  // openCatalogSearch

#include <memory>
#include <string>
#include <vector>

#include "collection.hpp"  // PosterGrid
#include "i18n.hpp"
#include "theme.hpp"
#include "tmdb.hpp"
#include "widgets.hpp"

// The catalog search screen: a search bar on top, the filters under it (kind,
// genre, year, order), and below either the genre tiles -- where it opens --
// or the results as a growing grid.
//
// No text: TMDB's discover, which takes every filter. With text: TMDB's
// search for that kind, which takes a single year only, so a decade and the
// genre are applied to its results here.

namespace
{

constexpr float kInset = 60.0f;
constexpr float kTileW = 230.0f;
constexpr float kTileH = 96.0f;
constexpr float kGap   = 16.0f;

// The year choices: "any", recent years one by one, then decades.
struct YearChoice
{
    const char* label;  // shown as is (digits), or translated when a word
    int from, to;       // 0 = no bound
    bool word;
};
const std::vector<YearChoice>& yearChoices()
{
    static const std::vector<YearChoice> v = {
        { "Any year", 0, 0, true },  { "2026", 2026, 2026, false },
        { "2025", 2025, 2025, false }, { "2024", 2024, 2024, false },
        { "2023", 2023, 2023, false }, { "2022", 2022, 2022, false },
        { "2021", 2021, 2021, false }, { "2020", 2020, 2020, false },
        { "2010s", 2010, 2019, true }, { "2000s", 2000, 2009, true },
        { "1990s", 1990, 1999, true }, { "1980s", 1980, 1989, true },
        { "Before 1980", 0, 1979, true },
    };
    return v;
}

// One colour per genre tile, from a fixed set: the tiles read as a mosaic.
NVGcolor tileColor(int i)
{
    static const NVGcolor c[] = {
        nvgRGB(0x45, 0x2a, 0x86), nvgRGB(0x1d, 0x5b, 0x8c), nvgRGB(0x8a, 0x2e, 0x46),
        nvgRGB(0x2f, 0x6d, 0x4f), nvgRGB(0x8a, 0x5a, 0x1f), nvgRGB(0x3a, 0x3f, 0x8f),
        nvgRGB(0x6b, 0x2a, 0x6e), nvgRGB(0x1f, 0x6e, 0x75),
    };
    return c[i % (sizeof(c) / sizeof(c[0]))];
}

class SearchScreen : public brls::Box
{
  public:
    SearchScreen()
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);

        // The search bar.
        bar = new brls::Box();
        bar->setAxis(brls::Axis::ROW);
        bar->setAlignItems(brls::AlignItems::CENTER);
        bar->setHeight(58.0f);
        bar->setMargins(14.0f, kInset, 0.0f, kInset);
        bar->setPadding(0.0f, 22.0f, 0.0f, 22.0f);
        bar->setCornerRadius(10.0f);
        bar->setBackgroundColor(theme::scrim(22));
        bar->setFocusable(true);
        bar->setHighlightCornerRadius(10.0f);
        barText = new brls::Label();
        barText->setFontSize(20.0f);
        barText->setSingleLine(true);
        bar->addView(barText);
        bar->registerAction(tr("Search"), brls::BUTTON_A, [this](brls::View*) {
            prompt();
            return true;
        }, false, false, brls::SOUND_CLICK);
        bar->addGestureRecognizer(new brls::TapGestureRecognizer(bar));
        addView(bar);

        // The filters.
        auto* filters = new brls::Box();
        filters->setAxis(brls::Axis::ROW);
        filters->setMargins(12.0f, kInset, 6.0f, kInset);
        kindBtn  = ui::actionButton("", false, [this]() { pickKind(); });
        genreBtn = ui::actionButton("", false, [this]() { pickGenre(); });
        yearBtn  = ui::actionButton("", false, [this]() { pickYear(); });
        sortBtn  = ui::actionButton("", false, [this]() { pickSort(); });
        filters->addView(kindBtn);
        filters->addView(genreBtn);
        filters->addView(yearBtn);
        filters->addView(sortBtn);
        filters->addView(ui::actionButton(tr("Reset"), false, [this]() { reset(); }));
        addView(filters);

        scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        body = new brls::Box();
        body->setAxis(brls::Axis::COLUMN);
        scroll->setContentView(body);
        addView(scroll);

        grid = new PosterGrid();
        grid->parkTarget = bar;
        grid->filter = [this](const content::Item& it) { return passes(it); };

        refreshLabels();
        showTiles();
    }

    void openKeyboard() { prompt(); }

  private:
    std::string query;
    content::Kind kind = content::Kind::Movie;
    // The third type: Japanese animation, films and series both (kind is
    // Series then, for the genre list).
    bool anime = false;
    // The default: films, shows and anime at once, as three sections one
    // under another (runAll). The genres are the films' then.
    bool all = true;
    int genre    = 0;
    int yearIdx  = 0;
    tmdb::Discover::Sort sort = tmdb::Discover::Sort::Popular;

    brls::Box* bar = nullptr;
    brls::Label* barText = nullptr;
    brls::Button *kindBtn = nullptr, *genreBtn = nullptr, *yearBtn = nullptr,
                 *sortBtn = nullptr;
    brls::ScrollingFrame* scroll = nullptr;
    brls::Box* body = nullptr;
    brls::Box* tiles = nullptr;
    brls::Box* sections = nullptr;  // "All": a heading and a grid per type
    PosterGrid* grid = nullptr;
    bool gridShown = false;

    std::string genreName() const
    {
        for (const auto& g : tmdb::genres(kind))
            if (g.first == genre) return tr(g.second);
        return tr("All genres");
    }

    void refreshLabels()
    {
        if (query.empty())
        {
            barText->setText(tr("Search movies & shows..."));
            barText->setTextColor(theme::textMuted());
        }
        else
        {
            barText->setText(query);
            barText->setTextColor(theme::text());
        }
        kindBtn->setText(all ? tr("All")
                         : anime ? tr("Anime")
                         : kind == content::Kind::Series ? tr("Shows") : tr("Movies"));
        genreBtn->setText(genreName());
        const YearChoice& y = yearChoices()[yearIdx];
        yearBtn->setText(y.word ? std::string(tr(y.label)) : std::string(y.label));
        sortBtn->setText(sort == tmdb::Discover::Sort::Rating   ? tr("By rating")
                         : sort == tmdb::Discover::Sort::Newest ? tr("Newest")
                                                                : tr("Popular"));
        // The order only applies to browsing; a search comes ranked by TMDB.
        sortBtn->setVisibility(query.empty() ? brls::Visibility::VISIBLE
                                             : brls::Visibility::GONE);
        sortBtn->setFocusable(query.empty());
        ui::setPillActive(genreBtn, genre != 0);
        ui::setPillActive(yearBtn, yearIdx != 0);
    }

    // The focus may be on something about to go: give it to the search bar.
    void parkFocus()
    {
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
            if (v == body)
            {
                brls::Application::giveFocus(bar);
                return;
            }
    }

    void dropSections()
    {
        if (!sections) return;
        body->removeView(sections);
        sections = nullptr;
    }

    void showTiles()
    {
        parkFocus();
        if (gridShown)
        {
            body->removeView(grid, false);  // kept for the next search
            gridShown = false;
        }
        dropSections();
        if (tiles)
        {
            body->removeView(tiles);
            tiles = nullptr;
        }
        tiles = new brls::Box();
        tiles->setAxis(brls::Axis::COLUMN);
        tiles->setPadding(8.0f, 0.0f, 24.0f, 0.0f);
        auto* head = new brls::Label();
        head->setText(tr("Genres"));
        head->setFontSize(21.0f);
        head->setMargins(6.0f, 0.0f, 10.0f, kInset);
        tiles->addView(head);

        float usable = brls::Application::contentWidth - 2 * kInset + kGap;
        int cols     = std::max(2, (int)(usable / (kTileW + kGap)));
        brls::Box* row = nullptr;
        int i = 0;
        for (const auto& g : tmdb::genres(kind))
        {
            if (!row || (int)row->getChildren().size() >= cols)
            {
                row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                row->setPaddingLeft(kInset);
                row->setMarginBottom(kGap);
                tiles->addView(row);
            }
            int id = g.first;
            row->addView(genreTile(tr(g.second), tileColor(i++), [this, id]() {
                genre = id;
                refreshLabels();
                grid->focusResultsOnce = true;  // the tile pressed is about to go
                run();
            }));
        }
        body->addView(tiles);
    }

    brls::Box* genreTile(const std::string& name, NVGcolor color, std::function<void()> onClick)
    {
        NVGcolor dark = color;
        dark.r *= 0.55f;
        dark.g *= 0.55f;
        dark.b *= 0.55f;
        auto* t = new ui::ShadeBox(true, color, dark, 1.0f);
        t->setWidth(kTileW);
        t->setHeight(kTileH);
        t->setCornerRadius(10.0f);
        t->setMarginRight(kGap);
        t->setAxis(brls::Axis::COLUMN);
        t->setJustifyContent(brls::JustifyContent::FLEX_END);
        t->setPadding(0.0f, 16.0f, 14.0f, 16.0f);
        t->setFocusable(true);
        t->setHighlightCornerRadius(10.0f);
        t->registerAction(tr("Open"), brls::BUTTON_A, [onClick](brls::View*) {
            onClick();
            return true;
        }, false, false, brls::SOUND_CLICK);
        t->addGestureRecognizer(new brls::TapGestureRecognizer(t));
        auto* l = new brls::Label();
        l->setText(name);
        l->setFontSize(20.0f);
        l->setTextColor(nvgRGB(255, 255, 255));
        l->setSingleLine(true);
        t->addView(l);
        return t;
    }

    // Whether a search result passes the filters TMDB's search does not
    // take: the genre, and a year range (a single year it does take).
    bool passes(const content::Item& it) const
    {
        if (query.empty()) return true;  // discover filtered it already
        if (anime && !tmdb::isAnime(it)) return false;
        return hasGenre(it, genre) && inYears(it);
    }

    static bool hasGenre(const content::Item& it, int id)
    {
        if (!id) return true;
        for (int g : it.genreIds)
            if (g == id) return true;
        return false;
    }

    bool inYears(const content::Item& it) const
    {
        const YearChoice& y = yearChoices()[yearIdx];
        if (!y.from && !y.to) return true;
        int year = it.year.empty() ? 0 : std::atoi(it.year.c_str());
        if (!year) return false;
        if (y.from && year < y.from) return false;
        if (y.to && year > y.to) return false;
        return true;
    }

    // A film genre's counterpart among TMDB's TV genres (0: there is none --
    // no horror or romance shows, say). The TV list folds some together.
    static int tvGenreOf(int movieGenre)
    {
        switch (movieGenre)
        {
            case 28: case 12: return 10759;   // Action & Adventure
            case 878: case 14: return 10765;  // Sci-Fi & Fantasy
            case 10752: return 10768;         // War & Politics
            case 16: case 35: case 80: case 99: case 18:
            case 10751: case 9648: case 37:
                return movieGenre;            // the same id on both
            default: return 0;
        }
    }

    // "All": films, shows and anime one under another, each under its heading
    // with a line between them. The anime are kept out of the other two, so
    // nothing is listed twice; a few pages each, then the next section.
    void runAll()
    {
        parkFocus();
        if (tiles)
        {
            body->removeView(tiles);
            tiles = nullptr;
        }
        if (gridShown)
        {
            body->removeView(grid, false);  // kept for a single type
            gridShown = false;
        }
        dropSections();
        // A genre tile was pressed and is gone: the first section takes the
        // focus once its posters land.
        bool focusOnce         = grid->focusResultsOnce;
        grid->focusResultsOnce = false;

        sections = new brls::Box();
        sections->setAxis(brls::Axis::COLUMN);
        body->addView(sections);

        const YearChoice& y = yearChoices()[yearIdx];
        const int single    = (y.from && y.from == y.to) ? y.from : 0;
        const bool text     = !query.empty();
        const int tvGenre   = genre ? tvGenreOf(genre) : 0;
        // A film genre with no TV counterpart: films only.
        const bool tvToo    = !genre || tvGenre;

        auto discover = [this, &y](content::Kind k, int g, bool a) {
            tmdb::Discover d;
            d.kind     = k;
            d.genre    = g;
            d.yearFrom = y.from;
            d.yearTo   = y.to;
            d.sort     = sort;
            d.anime    = a;
            return tmdb::discoverQuery(d);
        };
        auto section = [this, &focusOnce](const std::string& title, const tmdb::ListQuery& q,
                                          std::function<bool(const content::Item&)> f,
                                          int pages) {
            if (!sections->getChildren().empty())
            {
                auto* line = new brls::Box();
                line->setHeight(2.0f);
                line->setMargins(4.0f, kInset, 18.0f, kInset);
                line->setBackgroundColor(theme::scrim(60));
                sections->addView(line);
            }
            auto* head = ui::heading(title, kInset);
            head->setMarginTop(8.0f);
            sections->addView(head);
            auto* g       = new PosterGrid();
            g->parkTarget = bar;
            g->filter     = std::move(f);
            g->maxPages   = pages;
            g->focusResultsOnce = focusOnce;
            focusOnce     = false;
            sections->addView(g);
            g->load(q, scroll);
        };

        if (text)
        {
            int g = genre;
            section(tr("Movies"), tmdb::searchQuery(query, 0, single),
                    [this, g](const content::Item& it) {
                        return !tmdb::isAnime(it) && hasGenre(it, g) && inYears(it);
                    }, 2);
            if (tvToo)
            {
                section(tr("Shows"), tmdb::searchQuery(query, 1, single),
                        [this, tvGenre](const content::Item& it) {
                            return !tmdb::isAnime(it) && hasGenre(it, tvGenre) && inYears(it);
                        }, 2);
                // Films and shows both: the multi search, which takes no year.
                section(tr("Anime"), tmdb::searchQuery(query, -1),
                        [this, g, tvGenre](const content::Item& it) {
                            return tmdb::isAnime(it) &&
                                   (hasGenre(it, g) || hasGenre(it, tvGenre)) && inYears(it);
                        }, 3);
            }
        }
        else
        {
            // Anime films stay with the films (the anime list is the shows);
            // anime shows go to their own section only.
            section(tr("Movies"), discover(content::Kind::Movie, genre, false), nullptr, 2);
            if (tvToo)
            {
                section(tr("Shows"), discover(content::Kind::Series, tvGenre, false),
                        [](const content::Item& it) { return !tmdb::isAnime(it); }, 2);
                section(tr("Anime"), discover(content::Kind::Series, tvGenre, true), nullptr,
                        2);
            }
        }
    }

    void run()
    {
        // Anime has its own list straight away; the others start on the tiles.
        if (query.empty() && genre == 0 && yearIdx == 0 && !anime)
        {
            showTiles();
            return;
        }
        if (all)
        {
            runAll();
            return;
        }
        parkFocus();
        if (tiles)
        {
            body->removeView(tiles);
            tiles = nullptr;
        }
        dropSections();
        if (!gridShown)
        {
            body->addView(grid);
            gridShown = true;
        }
        const YearChoice& y = yearChoices()[yearIdx];
        tmdb::ListQuery q;
        if (query.empty())
        {
            tmdb::Discover d;
            d.kind     = kind;
            d.genre    = genre;
            d.yearFrom = y.from;
            d.yearTo   = y.to;
            d.sort     = sort;
            d.anime    = anime;
            q = tmdb::discoverQuery(d);
        }
        else
        {
            int single = (y.from && y.from == y.to) ? y.from : 0;
            // Anime: films and shows both (the multi search, which takes no
            // year -- passes() checks it), kept to anime by passes().
            q = anime ? tmdb::searchQuery(query, -1)
                      : tmdb::searchQuery(query, kind == content::Kind::Series ? 1 : 0, single);
        }
        grid->load(q, scroll);
    }

    void prompt()
    {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                while (!text.empty() && text.back() == ' ') text.pop_back();
                query = text;
                refreshLabels();
                run();
            },
            tr("Search movies & shows..."), "", 128, query);
    }

    void dropdown(const std::string& title, const std::vector<std::string>& values,
                  int selected, std::function<void(int)> picked)
    {
        auto* dd = new brls::Dropdown(title, values, [picked](int i) {
            if (i >= 0) picked(i);
        }, selected);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void pickKind()
    {
        dropdown(tr("Type"), { tr("All"), tr("Movies"), tr("Shows"), tr("Anime") },
                 all ? 0 : anime ? 3 : kind == content::Kind::Series ? 2 : 1, [this](int i) {
                     // "All" lists by the films' genres.
                     content::Kind k = i <= 1 ? content::Kind::Movie : content::Kind::Series;
                     bool a          = i == 3;
                     bool everything = i == 0;
                     if (k == kind && a == anime && everything == all) return;
                     kind  = k;
                     anime = a;
                     all   = everything;
                     genre = 0;  // the kinds have different genre lists
                     refreshLabels();
                     run();
                 });
    }

    void pickGenre()
    {
        std::vector<std::string> names = { tr("All genres") };
        std::vector<int> ids = { 0 };
        int sel = 0;
        for (const auto& g : tmdb::genres(kind))
        {
            if (g.first == genre) sel = (int)ids.size();
            names.push_back(tr(g.second));
            ids.push_back(g.first);
        }
        dropdown(tr("Genre"), names, sel, [this, ids](int i) {
            genre = ids[i];
            refreshLabels();
            run();
        });
    }

    void pickYear()
    {
        std::vector<std::string> names;
        for (const auto& y : yearChoices())
            names.push_back(y.word ? std::string(tr(y.label)) : std::string(y.label));
        dropdown(tr("Year"), names, yearIdx, [this](int i) {
            yearIdx = i;
            refreshLabels();
            run();
        });
    }

    void pickSort()
    {
        dropdown(tr("Order"), { tr("Popular"), tr("By rating"), tr("Newest") },
                 (int)sort, [this](int i) {
                     sort = (tmdb::Discover::Sort)i;
                     refreshLabels();
                     run();
                 });
    }

    void reset()
    {
        query.clear();
        genre   = 0;
        yearIdx = 0;
        sort    = tmdb::Discover::Sort::Popular;
        refreshLabels();
        showTiles();
    }

  public:
    ~SearchScreen() override
    {
        // Not in the tree while the tiles are up: free it here then.
        if (!gridShown) delete grid;
    }
};

} // namespace

void openCatalogSearch()
{
    if (!tmdb::configured())
    {
        auto* d = new brls::Dialog(tr("The catalog needs a TMDB key"));
        d->addButton("OK", []() {});
        d->open();
        return;
    }
    auto* screen = new SearchScreen();
    auto* frame  = new ui::FieldFrame(screen);
    frame->setTitle(tr("Search"));
    brls::Application::pushActivity(new brls::Activity(frame));
}
