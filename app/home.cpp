#include "home.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "card.hpp"
#include "collection.hpp"
#include "history.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "player.hpp"   // startPlaybackPrompt, for files from the history
#include "sources.hpp"  // continueContent
#include "stremio.hpp"  // Cinemeta, for the IMDb rating
#include "theme.hpp"
#include "tmdb.hpp"
#include "widgets.hpp"

// Laid out after the roadmap's mock-up "Главный экран": a compact hero (title,
// one meta line, two pills), "Continue watching" right under it as 16:9 cards,
// then rows of 2:3 posters -- so the first screen shows all three kinds.

namespace
{

// Where every row starts, so headings and first cards line up.
constexpr float kInset = 60.0f;
// Card widths. A poster is 2:3, a Continue card 16:9.
constexpr float kPosterW = 150.0f;
constexpr float kWideW   = 260.0f;
constexpr float kHeroH   = 236.0f;
// The strips are taller than their cards: the focus glow is drawn outside the
// card and the frame clips to itself.
constexpr float kGlow = 22.0f;

const NVGcolor kInk = nvgRGB(7, 7, 26);

NVGcolor ink(int alpha)
{
    NVGcolor c = kInk;
    c.a        = alpha / 255.0f;
    return c;
}

brls::Label* textLabel(const std::string& text, float size, NVGcolor color)
{
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    return l;
}

// "2024 · Film · TMDB 8.1", the hero's meta line.
std::string heroMeta(const content::Item& it)
{
    std::string m = it.year;
    auto add = [&m](const std::string& s) {
        if (s.empty()) return;
        if (!m.empty()) m += "  \xC2\xB7  ";
        m += s;
    };
    add(it.isSeries() ? tr("Show") : tr("Movie"));
    if (it.rating > 0.05)
    {
        char b[16];
        std::snprintf(b, sizeof(b), "TMDB %.1f", it.rating);
        add(b);
    }
    return m;
}

// A backdrop at a card's size: TMDB serves every width off the same path.
std::string smallBackdrop(std::string url)
{
    size_t k = url.find("/w1280/");
    if (k != std::string::npos) url.replace(k, 7, "/w780/");
    return url;
}

// Y on a poster: in the bookmarks as Planned, or out again.
void toggleBookmark(const content::Item& it)
{
    library::Entry e;
    bool marked = library::find(it.key(), e) && e.status != library::Status::None;
    library::setStatus(it, marked ? library::Status::None : library::Status::Planned);
    brls::Application::notify(marked ? tr("Removed from bookmarks")
                                     : tr("Added to bookmarks: Planned"));
}

brls::Box* catalogCard(const content::Item& it, std::shared_ptr<bool> alive)
{
    auto* card = ui::posterCard(it.title, it.key(), it.posterUrl, alive,
                                [it]() { openContentCard(it); }, kPosterW);
    card->registerAction(tr("Bookmark"), brls::BUTTON_Y, [it](brls::View*) {
        toggleBookmark(it);
        return true;
    });
    return card;
}

// The rows of each view: what the header says and where the titles come from.
struct RowSpec
{
    const char* title;
    tmdb::ListQuery query;  // also what its "See all" screen pages through
};

RowSpec rowOf(const char* title, tmdb::Row row) { return { title, tmdb::rowQuery(row) }; }

RowSpec pathOf(const char* title, const char* path, content::Kind kind)
{
    return { title, tmdb::pathQuery(path, kind) };
}

RowSpec genreOf(const char* title, content::Kind kind, int genre)
{
    return { title, tmdb::genreQuery(kind, genre) };
}


class HomeRoot : public brls::Box
{
  public:
    enum class View
    {
        Home = 0,
        Movies,
        Shows,
    };

    explicit HomeRoot(View v) : view(v)
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(14.0f, 0.0f, 30.0f, 0.0f);
        scroll->setContentView(list);
        addView(scroll);

        registerAction(tr("Search"), brls::BUTTON_BACK, [](brls::View*) {
            openCatalogSearch();
            return true;
        });

        build();
    }

    ~HomeRoot() override { *alive = false; }

    // The library and the history change behind this tab (a film watched, an
    // episode finished): keep Continue current. Polled, as nothing signals it.
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
              brls::FrameContext* ctx) override
    {
        uint32_t gen = library::generation() * 7919u + history::generation();
        if (gen != seenGen)
        {
            seenGen = gen;
            if (continueRow) fillContinue();
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

  private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    View view;
    brls::Box* list = nullptr;
    brls::Box* hero = nullptr;
    brls::Button* heroWatch = nullptr;   // somewhere safe to park the focus
    brls::Box* firstRow = nullptr;       // ... or here
    brls::View* continueSection[2] = { nullptr, nullptr };  // heading, strip
    brls::Box* continueRow = nullptr;
    uint32_t seenGen = 0;

    void build()
    {
        using content::Kind;
        if (view == View::Home)
        {
            hero = new brls::Box();
            hero->setVisibility(brls::Visibility::GONE);
            list->addView(hero);
            addContinueSection();
        }
        if (!tmdb::configured())
        {
            addSetupNote();
            return;
        }

        std::vector<RowSpec> rows;
        if (view == View::Home)
        {
            // The trending row also feeds the hero.
            RowSpec trend = rowOf("Popular now", tmdb::Row::Trending);
            brls::Box* trending = addRow(trend);
            tmdb::fetchQueryAsync(trend.query, 1, [this, trending, trend, a = alive](tmdb::ListResult r) {
                if (!*a) return;
                fillRow(trending, r, trend);
                for (const auto& it : r.items)
                    if (!it.backdropUrl.empty())
                    {
                        buildHero(it);
                        break;
                    }
            });
            rows = { rowOf("Popular movies", tmdb::Row::PopularMovies),
                     rowOf("Popular shows", tmdb::Row::PopularSeries),
                     rowOf("In cinemas", tmdb::Row::NowPlaying),
                     rowOf("Top rated movies", tmdb::Row::TopMovies),
                     rowOf("Top rated shows", tmdb::Row::TopSeries) };
        }
        else if (view == View::Movies)
            rows = { rowOf("Popular movies", tmdb::Row::PopularMovies),
                     rowOf("In cinemas", tmdb::Row::NowPlaying),
                     pathOf("Coming soon", "/movie/upcoming", Kind::Movie),
                     rowOf("Top rated movies", tmdb::Row::TopMovies),
                     genreOf("Action", Kind::Movie, 28),
                     genreOf("Comedy", Kind::Movie, 35),
                     genreOf("Drama", Kind::Movie, 18),
                     genreOf("Science fiction", Kind::Movie, 878),
                     genreOf("Thriller", Kind::Movie, 53),
                     genreOf("Horror", Kind::Movie, 27),
                     genreOf("Animation", Kind::Movie, 16) };
        else
            rows = { rowOf("Popular shows", tmdb::Row::PopularSeries),
                     pathOf("On the air", "/tv/on_the_air", Kind::Series),
                     rowOf("Top rated shows", tmdb::Row::TopSeries),
                     genreOf("Drama", Kind::Series, 18),
                     genreOf("Comedy", Kind::Series, 35),
                     genreOf("Crime", Kind::Series, 80),
                     genreOf("Sci-Fi & Fantasy", Kind::Series, 10765),
                     genreOf("Animation", Kind::Series, 16),
                     genreOf("Documentary", Kind::Series, 99) };

        for (const auto& spec : rows)
        {
            brls::Box* row = addRow(spec);
            tmdb::fetchQueryAsync(spec.query, 1, [this, row, spec, a = alive](tmdb::ListResult res) {
                if (!*a) return;
                fillRow(row, res, spec);
            });
        }
    }

    // A titled strip, with a "Loading..." placeholder until fillRow. The
    // heading opens the whole collection on a touch ("See all" at the row's
    // end does it from the controller).
    brls::Box* addRow(const RowSpec& spec)
    {
        std::string title = tr(spec.title);
        tmdb::ListQuery q = spec.query;

        auto* head = new brls::Box();
        head->setAxis(brls::Axis::ROW);
        head->setAlignItems(brls::AlignItems::FLEX_END);
        auto* h = ui::heading(title, kInset);
        head->addView(h);
        auto* more = textLabel(std::string(tr("See all")) + "  >", 15.0f, theme::textMuted());
        more->setMargins(0.0f, 0.0f, 6.0f, 14.0f);
        head->addView(more);
        head->addGestureRecognizer(new brls::TapGestureRecognizer(
            head, [title, q]() { openCollection(title, q); }));
        list->addView(head);

        auto* row = new brls::Box();
        auto* wait = textLabel(tr("Loading..."), 17.0f, theme::textMuted());
        wait->setMarginLeft(kInset);
        row->addView(wait);
        list->addView(ui::strip(row, kPosterW * 1.5f + kGlow));
        if (!firstRow) firstRow = row;
        return row;
    }

    void fillRow(brls::Box* row, const tmdb::ListResult& r, const RowSpec& spec)
    {
        // Only the placeholder is in there: nothing focusable is freed.
        row->clearViews();
        if (!r.ok || r.items.empty())
        {
            auto* l = textLabel(r.ok ? tr("Nothing here") : r.error, 17.0f,
                                r.ok ? theme::textMuted() : theme::textWarn());
            l->setMarginLeft(kInset);
            row->addView(l);
            return;
        }
        for (const auto& it : r.items)
        {
            auto* card = catalogCard(it, alive);
            if (row->getChildren().empty()) card->setMarginLeft(kInset);
            row->addView(card);
        }
        // The rest of the collection, one press past the last poster.
        std::string title = tr(spec.title);
        tmdb::ListQuery q = spec.query;
        row->addView(ui::moreTile(tr("See all"), kPosterW,
                                  [title, q]() { openCollection(title, q); }));
    }

    // The mock-up's banner: the backdrop darkened from the left, the title,
    // one meta line and the two pills.
    void buildHero(const content::Item& it)
    {
        if (!hero) return;
        hero->setVisibility(brls::Visibility::VISIBLE);
        hero->setHeight(kHeroH);
        hero->setMargins(0.0f, kInset, 6.0f, kInset);
        hero->setCornerRadius(14.0f);
        hero->setAxis(brls::Axis::COLUMN);
        hero->setJustifyContent(brls::JustifyContent::FLEX_END);

        auto* bg = new brls::Image();
        bg->setPositionType(brls::PositionType::ABSOLUTE);
        bg->setPositionTop(0.0f);
        bg->setPositionLeft(0.0f);
        bg->setWidthPercentage(100.0f);
        bg->setHeightPercentage(100.0f);
        bg->setScalingType(brls::ImageScalingType::FILL);
        bg->setClipsToBounds(true);
        bg->setCornerRadius(14.0f);
        ui::attachArt(bg, it.key() + "_bd", it.backdropUrl, alive, true);
        hero->addView(bg);

        auto* shade = new ui::ShadeBox(true, ink(242), ink(40), 0.62f);
        shade->setCornerRadius(14.0f);
        shade->setPositionType(brls::PositionType::ABSOLUTE);
        shade->setPositionTop(0.0f);
        shade->setPositionLeft(0.0f);
        shade->setWidthPercentage(100.0f);
        shade->setHeightPercentage(100.0f);
        hero->addView(shade);

        auto* text = new brls::Box();
        text->setAxis(brls::Axis::COLUMN);
        text->setPadding(0.0f, 28.0f, 24.0f, 28.0f);
        text->setMaxWidth(620.0f);

        auto* title = textLabel(it.title, 34.0f, nvgRGBA(255, 255, 255, 255));
        title->setSingleLine(true);
        text->addView(title);
        auto* meta = textLabel(heroMeta(it), 16.0f, nvgRGBA(255, 255, 255, 200));
        meta->setMarginTop(6.0f);
        text->addView(meta);
        // The IMDb rating joins the line once known: the details give the IMDb
        // id (cached), Cinemeta the rating.
        std::string line = heroMeta(it);
        auto a = alive;
        tmdb::fetchDetailsAsync(it.kind, it.tmdbId, [meta, line, a](tmdb::DetailsResult d) {
            if (!*a || !d.ok || d.details.item.imdbId.empty()) return;
            const content::Item& di = d.details.item;
            stremio::fetchMetaAsync("https://v3-cinemeta.strem.io", di.stremioType(), di.imdbId,
                                    [meta, line, a](stremio::MetaResult m) {
                if (!*a || !m.ok || m.imdbRating.empty()) return;
                meta->setText(line + "  \xC2\xB7  IMDb " + m.imdbRating);
            });
        });

        auto* actions = new brls::Box();
        actions->setAxis(brls::Axis::ROW);
        actions->setMarginTop(16.0f);
        heroWatch = ui::actionButton(std::string("\xE2\x96\xB6  ") + tr("Watch"), true,
                                     [it]() { watchContent(it); }, true);
        actions->addView(heroWatch);
        actions->addView(ui::actionButton(tr("Details"), false,
                                          [it]() { openContentCard(it); }, true));
        text->addView(actions);
        hero->addView(text);
    }

    // ---- Continue watching ---------------------------------------------------

    void addContinueSection()
    {
        auto* h = ui::heading(tr("Continue watching"), kInset);
        list->addView(h);
        continueRow = new brls::Box();
        brls::View* strip = ui::strip(continueRow, kWideW * 9.0f / 16.0f + kGlow);
        list->addView(strip);
        continueSection[0] = h;
        continueSection[1] = strip;
        fillContinue();
    }

    bool focusInside(brls::View* root)
    {
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
            if (v == root) return true;
        return false;
    }

    // Moves the focus off the Continue row before it is rebuilt (a freed
    // focused view crashes borealis): the hero, else the first poster row.
    void parkFocus()
    {
        if (!focusInside(continueRow)) return;
        if (heroWatch)
            brls::Application::giveFocus(heroWatch);
        else if (firstRow && !firstRow->getChildren().empty() &&
                 firstRow->getChildren()[0]->isFocusable())
            brls::Application::giveFocus(firstRow->getChildren()[0]);
    }

    // The library's titles first (one press back into the film, or on to the
    // next episode), then files from the history that are not those titles
    // (a local .torrent, a magnet).
    void fillContinue()
    {
        parkFocus();
        continueRow->clearViews();

        std::set<std::string> libSources;
        int n = 0;
        for (const auto& e : library::continueWatching())
        {
            if (n >= 12) break;
            library::Next next = library::nextOf(e);
            std::string title = e.item.title;
            if (e.item.isSeries())
                title += "  \xC2\xB7  S" + std::to_string(next.season) + "E" +
                         std::to_string(next.episode);
            if (!e.source.empty()) libSources.insert(e.source);
            library::Entry copy = e;
            const content::Item& it = e.item;
            std::string art = !it.backdropUrl.empty() ? smallBackdrop(it.backdropUrl)
                                                      : it.posterUrlLarge;
            auto* card = ui::wideCard(title, it.key() + "_bdw", art, "",
                                      next.kind == library::Next::Kind::Resume ? next.progress
                                                                               : -1.0,
                                      alive, [copy]() { continueContent(copy); }, kWideW);
            std::string key = e.key();
            card->registerAction(tr("Remove"), brls::BUTTON_X, [key](brls::View*) {
                library::dismissContinue(key);
                return true;
            });
            if (continueRow->getChildren().empty()) card->setMarginLeft(kInset);
            continueRow->addView(card);
            n++;
        }

        for (const auto& it : history::all())
        {
            if (n >= 12) break;
            if (!history::hasValidResume(it) || libSources.count(it.source)) continue;
            double prog = it.durationSec > 0 ? it.positionSec / it.durationSec : -1.0;
            history::Item copy = it;
            auto* card = ui::wideCard(it.title, "", "", it.posterPath, prog, alive,
                                      [copy]() {
                                          PlayerArt art;
                                          art.posterPath = copy.posterPath;
                                          WatchInfo w;
                                          w.authKey      = copy.stremioAuthKey;
                                          w.itemId       = copy.stremioItemId;
                                          w.videoId      = copy.stremioVideoId;
                                          w.type         = copy.stremioType;
                                          w.displayTitle = copy.title;
                                          startPlaybackPrompt(copy.source, art, copy.title,
                                                              copy.fileIndex, w,
                                                              copy.sourceType);
                                      },
                                      kWideW);
            std::string id = it.id;
            card->registerAction(tr("Remove"), brls::BUTTON_X, [id](brls::View*) {
                history::remove(id);
                return true;
            });
            if (continueRow->getChildren().empty()) card->setMarginLeft(kInset);
            continueRow->addView(card);
            n++;
        }

        auto vis = n > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
        for (brls::View* v : continueSection)
            if (v) v->setVisibility(vis);
    }

    void addSetupNote()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setMargins(24.0f, kInset, 0.0f, kInset);
        box->setPadding(24.0f, 28.0f, 24.0f, 28.0f);
        box->setCornerRadius(14.0f);
        box->setBackgroundColor(theme::scrim(20));
        box->addView(textLabel(tr("The catalog needs a TMDB key"), 24.0f, theme::text()));
        auto* l = textLabel(
            tr("Get a free API key at themoviedb.org (Settings -> API) and enter it in "
               "Options -> Network. Where TMDB is blocked, set a proxy or a mirror there "
               "too."),
            17.0f, theme::textBody());
        l->setMarginTop(10.0f);
        box->addView(l);
        list->addView(box);
    }
};
} // namespace

brls::View* buildHomeTab(int view)
{
    return new HomeRoot((HomeRoot::View)(view < 0 || view > 2 ? 0 : view));
}
