#include "home.hpp"

#include <memory>
#include <string>
#include <vector>

#include "card.hpp"
#include "history.hpp"
#include "i18n.hpp"
#include "player.hpp"  // startPlaybackPrompt, for Continue watching
#include "theme.hpp"
#include "tmdb.hpp"
#include "widgets.hpp"

namespace
{

// Where every row starts, so headings and first cards line up.
constexpr float kInset = 60.0f;
// Card widths. A poster is 2:3, a Continue card 16:9.
constexpr float kPosterW = 190.0f;
constexpr float kWideW   = 330.0f;
// The strips are taller than their cards: the focus glow is drawn outside the
// card and the frame clips to itself.
constexpr float kGlow = 24.0f;

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

std::string shortText(const std::string& s, size_t max)
{
    if (s.size() <= max) return s;
    size_t cut = s.rfind(' ', max);
    if (cut == std::string::npos) cut = max;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    return s.substr(0, cut) + "...";
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

class HomeRoot : public brls::Box
{
  public:
    HomeRoot()
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(14.0f, 0.0f, 36.0f, 0.0f);
        scroll->setContentView(list);
        addView(scroll);

        hero = new brls::Box();
        hero->setVisibility(brls::Visibility::GONE);
        list->addView(hero);

        addContinueRow();

        if (!tmdb::configured())
        {
            addSetupNote();
            return;
        }

        registerAction(tr("Search"), brls::BUTTON_BACK, [this](brls::View*) {
            promptSearch();
            return true;
        });

        // The trending row also feeds the hero.
        brls::Box* trending = addRow(tr("Popular now"));
        tmdb::fetchRowAsync(tmdb::Row::Trending, [this, trending, a = alive](tmdb::ListResult r) {
            if (!*a) return;
            fillRow(trending, r);
            for (const auto& it : r.items)
                if (!it.backdropUrl.empty())
                {
                    buildHero(it);
                    break;
                }
        });

        const std::vector<std::pair<const char*, tmdb::Row>> rows = {
            { "Popular movies", tmdb::Row::PopularMovies },
            { "Popular shows", tmdb::Row::PopularSeries },
            { "In cinemas", tmdb::Row::NowPlaying },
            { "Top rated movies", tmdb::Row::TopMovies },
            { "Top rated shows", tmdb::Row::TopSeries },
        };
        for (const auto& r : rows)
        {
            brls::Box* row = addRow(tr(r.first));
            tmdb::fetchRowAsync(r.second, [this, row, a = alive](tmdb::ListResult res) {
                if (!*a) return;
                fillRow(row, res);
            });
        }
    }

    ~HomeRoot() override { *alive = false; }

  private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    brls::Box* list = nullptr;
    brls::Box* hero = nullptr;

    // A titled strip, with a "Loading..." placeholder until fillRow.
    brls::Box* addRow(const std::string& title)
    {
        list->addView(ui::heading(title, kInset));
        auto* row = new brls::Box();
        auto* wait = textLabel(tr("Loading..."), 18.0f, theme::textMuted());
        wait->setMarginLeft(kInset);
        row->addView(wait);
        list->addView(ui::strip(row, kPosterW * 1.5f + kGlow));
        return row;
    }

    void fillRow(brls::Box* row, const tmdb::ListResult& r)
    {
        // Only the placeholder is in there: nothing focusable is freed.
        row->clearViews();
        if (!r.ok || r.items.empty())
        {
            auto* l = textLabel(r.ok ? tr("Nothing here") : r.error, 18.0f,
                                r.ok ? theme::textMuted() : theme::textWarn());
            l->setMarginLeft(kInset);
            row->addView(l);
            return;
        }
        for (const auto& it : r.items)
        {
            auto* card = ui::posterCard(it.title, it.key(), it.posterUrl, alive,
                                        [it]() { openContentCard(it); }, kPosterW);
            if (row->getChildren().empty()) card->setMarginLeft(kInset);
            row->addView(card);
        }
    }

    // The mock-up's banner: the backdrop, darkened from the left, with the
    // title, a meta line, two lines of synopsis and the two actions.
    void buildHero(const content::Item& it)
    {
        hero->clearViews();
        hero->setVisibility(brls::Visibility::VISIBLE);
        hero->setHeight(350.0f);
        hero->setMargins(4.0f, kInset, 8.0f, kInset);
        hero->setCornerRadius(16.0f);
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
        bg->setCornerRadius(16.0f);
        ui::attachArt(bg, it.key() + "_bd", it.backdropUrl, alive, true);
        hero->addView(bg);

        auto* shade = new ui::ShadeBox(true, ink(245), ink(30), 0.75f);
        shade->setCornerRadius(16.0f);
        shade->setPositionType(brls::PositionType::ABSOLUTE);
        shade->setPositionTop(0.0f);
        shade->setPositionLeft(0.0f);
        shade->setWidthPercentage(100.0f);
        shade->setHeightPercentage(100.0f);
        hero->addView(shade);

        auto* text = new brls::Box();
        text->setAxis(brls::Axis::COLUMN);
        text->setPadding(0.0f, 36.0f, 30.0f, 36.0f);
        text->setMaxWidth(760.0f);

        auto* title = textLabel(it.title, 42.0f, nvgRGBA(255, 255, 255, 255));
        title->setSingleLine(true);
        text->addView(title);
        auto* meta = textLabel(heroMeta(it), 18.0f, nvgRGBA(255, 255, 255, 200));
        meta->setMarginTop(8.0f);
        text->addView(meta);
        if (!it.overview.empty())
        {
            auto* ov = textLabel(shortText(it.overview, 170), 18.0f,
                                 nvgRGBA(255, 255, 255, 215));
            ov->setMarginTop(12.0f);
            text->addView(ov);
        }

        auto* actions = new brls::Box();
        actions->setAxis(brls::Axis::ROW);
        actions->setMarginTop(18.0f);
        actions->addView(ui::actionButton(std::string("\xE2\x96\xB6  ") + tr("Watch"), true,
                                          [it]() { watchContent(it); }));
        actions->addView(ui::actionButton(tr("Details"), false,
                                          [it]() { openContentCard(it); }));
        text->addView(actions);
        hero->addView(text);
    }

    // History entries that can be resumed, newest first, as 16:9 cards with
    // the progress along the bottom.
    void addContinueRow()
    {
        std::vector<history::Item> items;
        for (const auto& it : history::all())
            if (history::hasValidResume(it)) items.push_back(it);
        if (items.empty()) return;
        if (items.size() > 12) items.resize(12);

        list->addView(ui::heading(tr("Continue watching"), kInset));
        auto* row = new brls::Box();
        for (const auto& it : items)
        {
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
            if (row->getChildren().empty()) card->setMarginLeft(kInset);
            row->addView(card);
        }
        list->addView(ui::strip(row, kWideW * 9.0f / 16.0f + kGlow));
    }

    void addSetupNote()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setMargins(30.0f, kInset, 0.0f, kInset);
        box->setPadding(26.0f, 30.0f, 26.0f, 30.0f);
        box->setCornerRadius(14.0f);
        box->setBackgroundColor(theme::scrim(20));
        box->addView(textLabel(tr("The catalog needs a TMDB key"), 26.0f, theme::text()));
        auto* l = textLabel(
            tr("Get a free API key at themoviedb.org (Settings -> API) and enter it in "
               "Options -> Network. Where TMDB is blocked, set a proxy or a mirror there "
               "too."),
            18.0f, theme::textBody());
        l->setMarginTop(10.0f);
        box->addView(l);
        list->addView(box);
    }

    void promptSearch()
    {
        brls::Application::getImeManager()->openForText(
            [a = alive](std::string q) {
                if (!*a || q.empty()) return;
                openSearchResults(q);
            },
            tr("Search movies & shows..."), "", 128, "");
    }

    // The results on a page of their own: films and series as two strips.
    static void openSearchResults(const std::string& query)
    {
        auto* page  = new SearchPage(query);
        auto* frame = new brls::AppletFrame(page);
        frame->setTitle(tr("Search") + std::string(": ") + query);
        brls::Application::pushActivity(new brls::Activity(frame));
    }

    class SearchPage : public brls::Box
    {
      public:
        explicit SearchPage(const std::string& query)
        {
            setAxis(brls::Axis::COLUMN);
            setGrow(1.0f);
            auto* scroll = new brls::ScrollingFrame();
            scroll->setGrow(1.0f);
            scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
            auto* list = new brls::Box();
            list->setAxis(brls::Axis::COLUMN);
            list->setPadding(10.0f, 0.0f, 30.0f, 0.0f);
            scroll->setContentView(list);
            addView(scroll);

            list->addView(ui::heading(tr("Movies"), kInset));
            auto* movies = new brls::Box();
            list->addView(ui::strip(movies, kPosterW * 1.5f + kGlow));
            list->addView(ui::heading(tr("Shows"), kInset));
            auto* shows = new brls::Box();
            list->addView(ui::strip(shows, kPosterW * 1.5f + kGlow));
            for (brls::Box* r : { movies, shows })
            {
                auto* w = textLabel(tr("Searching..."), 18.0f, theme::textMuted());
                w->setMarginLeft(kInset);
                r->addView(w);
            }

            tmdb::searchAsync(query, [movies, shows, a = alive](tmdb::ListResult r) {
                if (!*a) return;
                movies->clearViews();
                shows->clearViews();
                for (const auto& it : r.items)
                {
                    brls::Box* row = it.isSeries() ? shows : movies;
                    auto* card = ui::posterCard(it.title, it.key(), it.posterUrl, a,
                                                [it]() { openContentCard(it); }, kPosterW);
                    if (row->getChildren().empty()) card->setMarginLeft(kInset);
                    row->addView(card);
                }
                for (brls::Box* row : { movies, shows })
                    if (row->getChildren().empty())
                    {
                        auto* l = textLabel(r.ok ? tr("No results found") : r.error, 18.0f,
                                            theme::textMuted());
                        l->setMarginLeft(kInset);
                        row->addView(l);
                    }
            });
        }
        ~SearchPage() override { *alive = false; }

      private:
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    };
};

} // namespace

brls::View* buildHomeTab()
{
    return new HomeRoot();
}
