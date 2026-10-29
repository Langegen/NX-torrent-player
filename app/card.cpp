#include "card.hpp"

#include <borealis.hpp>

#include <cstdio>
#include <memory>

#include "config.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "sources.hpp"
#include "stremio.hpp"
#include "theme.hpp"
#include "tmdb.hpp"
#include "widgets.hpp"

namespace
{

// Horizon dark, as in the mock-ups: what the backdrop is darkened towards, so
// the white text on it reads in either theme.
const NVGcolor kInk = nvgRGB(7, 7, 26);

NVGcolor ink(int alpha)
{
    NVGcolor c = kInk;
    c.a        = alpha / 255.0f;
    return c;
}

void note(const std::string& msg)
{
    auto* d = new brls::Dialog(msg);
    d->addButton("OK", []() {});
    d->open();
}

std::string runtimeText(int min)
{
    if (min <= 0) return "";
    if (min < 60) return std::to_string(min) + tr(" min");
    return std::to_string(min / 60) + tr(" h ") + std::to_string(min % 60) + tr(" min");
}

std::string ratingText(double r)
{
    char b[16];
    std::snprintf(b, sizeof(b), "TMDB %.1f", r);
    return b;
}

// The sources screen for a film, a whole show (season 0) or one episode. It
// gathers from Jackett and the Stremio addons both; with neither set up there
// is nothing it could show, so say what would make it work instead.
void openSources(const content::Item& it, int season, int episode, int episodesInSeason = 0)
{
    bool jackettOn = !config::get().jackettUrl.empty();
    bool stremioOn = !stremio::loadAuthKey().empty() && !it.imdbId.empty();
    if (!jackettOn && !stremioOn)
    {
        note(tr("To find sources, set up Jackett in Options (or sign in to Stremio)."));
        return;
    }
    openSourcesScreen(it, season, episode, episodesInSeason);
}

// The card's state, shared by the callbacks that fill it in.
struct CardState
{
    content::Details d;
    bool detailsLoaded = false;
    std::string imdbRating;  // "7.9", from Cinemeta; "" until known
    int seasonIdx      = 0;   // into d.seasons
    int seasonReq      = 0;   // bumps on every switch: late answers are dropped
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    brls::Image* backdrop      = nullptr;
    brls::Box* chips           = nullptr;
    brls::Label* overview      = nullptr;
    brls::Label* cast          = nullptr;
    brls::Button* playBtn      = nullptr;
    brls::Button* markBtn      = nullptr;  // the bookmark status
    brls::Box* seasonsSection  = nullptr;
    brls::Box* seasonBar       = nullptr;
    std::vector<brls::Button*> seasonBtns;
    brls::Box* episodes        = nullptr;
    brls::Box* similarSection  = nullptr;  // "More like this"
    brls::Box* similarRow      = nullptr;
};

class CardView : public brls::Box
{
  public:
    explicit CardView(const content::Item& item)
    {
        st = std::make_shared<CardState>();
        st->d.item = item;
        alive      = st->alive;

        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);

        // The backdrop under everything, then the shade that darkens its left
        // side for the text and its bottom for the episode strip.
        st->backdrop = new brls::Image();
        st->backdrop->setPositionType(brls::PositionType::ABSOLUTE);
        st->backdrop->setPositionTop(0.0f);
        st->backdrop->setPositionLeft(0.0f);
        st->backdrop->setWidthPercentage(100.0f);
        st->backdrop->setHeightPercentage(100.0f);
        st->backdrop->setScalingType(brls::ImageScalingType::FILL);
        st->backdrop->setClipsToBounds(true);
        addView(st->backdrop);

        auto* shadeLeft = new ui::ShadeBox(true, ink(250), ink(60), 0.85f);
        absoluteFill(shadeLeft);
        addView(shadeLeft);
        auto* shadeBottom = new ui::ShadeBox(false, ink(0), ink(235), 1.0f);
        absoluteFill(shadeBottom);
        addView(shadeBottom);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        auto* body = new brls::Box();
        body->setAxis(brls::Axis::COLUMN);
        body->setPadding(34.0f, 60.0f, 40.0f, 60.0f);
        buildTop(body);
        buildSeasons(body);
        buildSimilar(body);
        scroll->setContentView(body);
        addView(scroll);

        ui::attachArt(st->backdrop, item.key() + "_bd", item.backdropUrl, alive, true);

        // L / R walk the seasons (the mock-up's hint bar).
        registerAction(tr("Season"), brls::BUTTON_LB, [this](brls::View*) {
            switchSeason(-1);
            return true;
        });
        registerAction(tr("Season"), brls::BUTTON_RB, [this](brls::View*) {
            switchSeason(+1);
            return true;
        });

        loadDetails();
    }

    ~CardView() override { *alive = false; }

  private:
    std::shared_ptr<CardState> st;
    std::shared_ptr<bool> alive;

    static void absoluteFill(brls::View* v)
    {
        v->setPositionType(brls::PositionType::ABSOLUTE);
        v->setPositionTop(0.0f);
        v->setPositionLeft(0.0f);
        v->setWidthPercentage(100.0f);
        v->setHeightPercentage(100.0f);
    }

    static brls::Label* whiteLabel(const std::string& text, float size, int alpha)
    {
        auto* l = new brls::Label();
        l->setText(text);
        l->setFontSize(size);
        l->setTextColor(nvgRGBA(255, 255, 255, alpha));
        return l;
    }

    void buildTop(brls::Box* body)
    {
        const content::Item& it = st->d.item;

        auto* top = new brls::Box();
        top->setAxis(brls::Axis::ROW);
        top->setAlignItems(brls::AlignItems::FLEX_START);

        auto* poster = new brls::Image();
        poster->setDimensions(180.0f, 270.0f);
        poster->setScalingType(brls::ImageScalingType::FILL);
        poster->setClipsToBounds(true);
        poster->setCornerRadius(12.0f);
        poster->setMarginRight(34.0f);
        poster->setShadowType(brls::ShadowType::GENERIC);
        poster->setShadowVisibility(true);
        ui::attachArt(poster, it.key(), it.posterUrlLarge, alive);
        top->addView(poster);

        auto* info = new brls::Box();
        info->setAxis(brls::Axis::COLUMN);
        info->setGrow(1.0f);
        info->setShrink(1.0f);

        info->addView(whiteLabel(it.title, 36.0f, 255));

        st->chips = new brls::Box();
        st->chips->setAxis(brls::Axis::ROW);
        st->chips->setMarginTop(12.0f);
        info->addView(st->chips);
        fillChips();

        st->overview = whiteLabel(trimmed(it.overview), 17.0f, 225);
        st->overview->setMarginTop(18.0f);
        info->addView(st->overview);

        st->cast = whiteLabel("", 16.0f, 165);
        st->cast->setMarginTop(10.0f);
        st->cast->setVisibility(brls::Visibility::GONE);
        info->addView(st->cast);

        auto* actions = new brls::Box();
        actions->setAxis(brls::Axis::ROW);
        actions->setMarginTop(18.0f);

        auto weak = std::weak_ptr<CardState>(st);
        st->playBtn = ui::actionButton(playLabel(), true, [weak]() {
            if (auto s = weak.lock()) watch(*s);
        }, true);
        actions->addView(st->playBtn);

        // Every release of the film, or of the season on screen (packs and
        // single episodes alike).
        actions->addView(ui::actionButton(tr("Torrents"), false, [weak]() {
            auto s = weak.lock();
            if (!s) return;
            int season = 0;
            if (s->d.item.isSeries() && s->seasonIdx < (int)s->d.seasons.size())
                season = s->d.seasons[s->seasonIdx].number;
            openSources(s->d.item, season, 0);
        }, true));

        // Bookmark: a status from the library (Watching / Planned / ...).
        st->markBtn = ui::actionButton(markLabel(), false, [weak]() {
            if (auto s = weak.lock()) pickStatus(s);
        }, true);
        actions->addView(st->markBtn);
        info->addView(actions);

        top->addView(info);
        body->addView(top);
    }

    static std::string trimmed(const std::string& s)
    {
        // A synopsis can run to a page; the card has room for a paragraph.
        const size_t kMax = 300;
        if (s.size() <= kMax) return s;
        size_t cut = s.rfind(' ', kMax);
        if (cut == std::string::npos) cut = kMax;
        // Don't split a UTF-8 sequence.
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        return s.substr(0, cut) + "...";
    }

    // The main button says what it will do: continue where the library left
    // off ("Continue S1 E5"), the episode after a finished one, or the start.
    std::string playLabel() const
    {
        const std::string play = "\xE2\x96\xB6  ";
        const content::Details& d = st->d;
        library::Entry en;
        if (library::find(d.item.key(), en))
        {
            library::Next n = library::nextOf(en);
            if (n.kind == library::Next::Kind::Resume)
                return play + (d.item.isSeries()
                                   ? tr("Continue S") + std::to_string(n.season) + tr(" E") +
                                         std::to_string(n.episode)
                                   : std::string(tr("Continue")));
            if (n.kind == library::Next::Kind::NextEp)
                return play + tr("Watch S") + std::to_string(n.season) + tr(" E") +
                       std::to_string(n.episode);
        }
        if (d.item.isSeries() && !d.seasons.empty())
            return play + tr("Watch S") + std::to_string(d.seasons[0].number) + tr(" E") + "1";
        return play + tr("Watch");
    }

    std::string markLabel() const
    {
        library::Entry en;
        library::Status s = library::find(st->d.item.key(), en) ? en.status
                                                                 : library::Status::None;
        switch (s)
        {
            case library::Status::Watching:  return tr("Watching");
            case library::Status::Planned:   return tr("Planned");
            case library::Status::Completed: return tr("Completed");
            case library::Status::Dropped:   return tr("Dropped");
            default:                         return std::string("+  ") + tr("Bookmark");
        }
    }

    // The status list, as borealis' own selector shows one.
    static void pickStatus(std::shared_ptr<CardState> s)
    {
        static const library::Status kOrder[] = {
            library::Status::Watching, library::Status::Planned, library::Status::Completed,
            library::Status::Dropped, library::Status::None,
        };
        std::vector<std::string> labels = { tr("Watching"), tr("Planned"), tr("Completed"),
                                            tr("Dropped"), tr("Remove from bookmarks") };
        library::Entry en;
        library::Status cur = library::find(s->d.item.key(), en) ? en.status
                                                                  : library::Status::None;
        int sel = 0;
        for (int i = 0; i < 5; i++)
            if (kOrder[i] == cur) sel = i;
        std::weak_ptr<CardState> weak = s;
        auto* dd = new brls::Dropdown(tr("Bookmark"), labels, [weak](int i) {
            auto c = weak.lock();
            if (!c || i < 0 || i >= 5) return;
            library::setStatus(c->d.item, kOrder[i]);
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    // Library changes (a status picked, an episode watched): relabel. Polled:
    // the player writes the library from its own screen, on top of this one.
    uint32_t seenGen = 0;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
              brls::FrameContext* ctx) override
    {
        uint32_t gen = library::generation();
        if (gen != seenGen)
        {
            seenGen = gen;
            if (st->playBtn) st->playBtn->setText(playLabel());
            if (st->markBtn) st->markBtn->setText(markLabel());
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

    void fillChips()
    {
        st->chips->clearViews();
        const content::Details& d = st->d;
        const content::Item& it   = d.item;
        if (it.rating > 0.05) st->chips->addView(ui::chip(ratingText(it.rating), true));
        if (!st->imdbRating.empty())
            st->chips->addView(ui::chip("IMDb " + st->imdbRating, nvgRGB(0xf5, 0xc5, 0x18),
                                        nvgRGB(0x10, 0x10, 0x10)));
        if (!it.year.empty()) st->chips->addView(ui::chip(it.year));
        for (size_t i = 0; i < it.genres.size() && i < 2; i++)
            st->chips->addView(ui::chip(it.genres[i]));
        if (it.isSeries())
        {
            int seasons = 0, eps = 0;
            for (const auto& s : d.seasons)
                if (s.number > 0) { seasons++; eps += s.episodeCount; }
            // "Seasons: 1 · Episodes: 8" rather than "1 season": no plural
            // forms to get wrong in any language.
            if (seasons > 0)
                st->chips->addView(ui::chip(tr("Seasons: ") + std::to_string(seasons) +
                                            " \xC2\xB7 " + tr("Episodes: ") +
                                            std::to_string(eps)));
        }
        else if (it.runtimeMin > 0)
            st->chips->addView(ui::chip(runtimeText(it.runtimeMin)));
    }

    void buildSeasons(brls::Box* body)
    {
        st->seasonsSection = new brls::Box();
        st->seasonsSection->setAxis(brls::Axis::COLUMN);
        st->seasonsSection->setMarginTop(22.0f);
        st->seasonsSection->setVisibility(brls::Visibility::GONE);

        st->seasonBar = new brls::Box();
        st->seasonBar->setAxis(brls::Axis::ROW);
        st->seasonBar->setMarginBottom(14.0f);
        st->seasonsSection->addView(st->seasonBar);

        st->episodes = new brls::Box();
        // 300 wide at 16:9, plus room for the focus glow.
        st->seasonsSection->addView(ui::strip(st->episodes, 236.0f * 9.0f / 16.0f + 22.0f, false));
        body->addView(st->seasonsSection);
    }

    // TMDB's recommendations under everything else, as on the home rows: A
    // opens that title's card on top of this one, Y bookmarks it.
    void buildSimilar(brls::Box* body)
    {
        st->similarSection = new brls::Box();
        st->similarSection->setAxis(brls::Axis::COLUMN);
        st->similarSection->setMarginTop(26.0f);
        st->similarSection->setVisibility(brls::Visibility::GONE);

        auto* h = whiteLabel(tr("More like this"), 20.0f, 235);
        h->setMarginBottom(10.0f);
        st->similarSection->addView(h);

        st->similarRow = new brls::Box();
        st->similarSection->addView(ui::strip(st->similarRow, kSimilarW * 1.5f + 22.0f, false));
        body->addView(st->similarSection);
    }

    static constexpr float kSimilarW = 140.0f;

    void showSimilar()
    {
        if (st->d.similar.empty()) return;
        st->similarRow->clearViews();  // never focused: only filled once
        for (const auto& it : st->d.similar)
            st->similarRow->addView(catalogPoster(it, alive, kSimilarW));
        st->similarSection->setVisibility(brls::Visibility::VISIBLE);
    }

    void loadDetails()
    {
        auto weak = std::weak_ptr<CardState>(st);
        const content::Item& it = st->d.item;
        tmdb::fetchDetailsAsync(it.kind, it.tmdbId, [this, weak](tmdb::DetailsResult r) {
            auto s = weak.lock();
            if (!s || !*s->alive) return;
            if (!r.ok)
            {
                brls::Logger::warning("[card] details: {}", r.error);
                return;
            }
            // Keep what the list had where the details are thinner (a list
            // backdrop when the details lack one, say).
            content::Item before = s->d.item;
            s->d = r.details;
            if (s->d.item.backdropUrl.empty()) s->d.item.backdropUrl = before.backdropUrl;
            if (s->d.item.overview.empty()) s->d.item.overview = before.overview;
            s->detailsLoaded = true;

            fillChips();
            s->overview->setText(trimmed(s->d.item.overview));
            if (!s->d.cast.empty())
            {
                std::string c = tr("Starring: ");
                for (size_t i = 0; i < s->d.cast.size() && i < 4; i++)
                    c += (i ? ", " : "") + s->d.cast[i];
                s->cast->setText(c);
                s->cast->setVisibility(brls::Visibility::VISIBLE);
            }
            s->playBtn->setText(playLabel());
            if (s->d.item.isSeries() && !s->d.seasons.empty()) showSeasons();
            showSimilar();
            loadImdbRating();
        });
    }

    // TMDB has no IMDb rating; Cinemeta (Stremio's catalog, keyed on the IMDb
    // id, no account needed) does. Shown next to TMDB's once it lands.
    void loadImdbRating()
    {
        if (st->d.item.imdbId.empty()) return;
        auto weak = std::weak_ptr<CardState>(st);
        stremio::fetchMetaAsync("https://v3-cinemeta.strem.io", st->d.item.stremioType(),
                                st->d.item.imdbId, [this, weak](stremio::MetaResult r) {
            auto s = weak.lock();
            if (!s || !*s->alive || !r.ok || r.imdbRating.empty()) return;
            s->imdbRating = r.imdbRating;
            fillChips();
        });
    }

    void showSeasons()
    {
        st->seasonsSection->setVisibility(brls::Visibility::VISIBLE);
        st->seasonBar->clearViews();
        st->seasonBtns.clear();
        for (size_t i = 0; i < st->d.seasons.size(); i++)
        {
            const content::Season& s = st->d.seasons[i];
            std::string name = s.number == 0 ? tr("Specials")
                                             : tr("Season ") + std::to_string(s.number);
            int idx = (int)i;
            auto* b = ui::actionButton(name, false, [this, idx]() { selectSeason(idx); }, true);
            st->seasonBar->addView(b);
            st->seasonBtns.push_back(b);
        }
        selectSeason(0);
    }

    void switchSeason(int dir)
    {
        if (st->seasonBtns.empty()) return;
        int n = (int)st->seasonBtns.size();
        int idx = st->seasonIdx + dir;
        if (idx < 0 || idx >= n) return;
        // The focus may be on an episode about to be freed: move it to the
        // new season's button first (a freed focused view crashes borealis).
        brls::Application::giveFocus(st->seasonBtns[idx]);
        selectSeason(idx);
    }

    void selectSeason(int idx)
    {
        if (idx < 0 || idx >= (int)st->d.seasons.size()) return;
        // An episode card can hold the focus when a season button is pressed
        // by touch; park it on that button before the strip is emptied.
        brls::View* f = brls::Application::getCurrentFocus();
        for (brls::View* v = f; v; v = v->getParent())
            if (v == st->episodes)
            {
                brls::Application::giveFocus(st->seasonBtns[idx]);
                break;
            }

        st->seasonIdx = idx;
        for (size_t i = 0; i < st->seasonBtns.size(); i++)
            ui::setPillActive(st->seasonBtns[i], (int)i == idx, true);

        st->episodes->clearViews();
        auto* wait = whiteLabel(tr("Loading..."), 18.0f, 180);
        wait->setMarginLeft(4.0f);
        st->episodes->addView(wait);

        int req    = ++st->seasonReq;
        int number = st->d.seasons[idx].number;
        auto weak  = std::weak_ptr<CardState>(st);
        tmdb::fetchSeasonAsync(st->d.item.tmdbId, number,
                               [this, weak, req](tmdb::SeasonResult r) {
            auto s = weak.lock();
            if (!s || !*s->alive || req != s->seasonReq) return;
            s->episodes->clearViews();  // only the "Loading" label is there
            if (!r.ok || r.episodes.empty())
            {
                s->episodes->addView(whiteLabel(r.ok ? tr("No episodes") : r.error, 18.0f, 180));
                return;
            }
            int count = (int)r.episodes.size();
            library::Entry lib;
            bool haveLib = library::find(s->d.item.key(), lib);
            for (const auto& e : r.episodes)
            {
                // Watched part of it, from the library: a full bar when done.
                double prog = -1.0;
                if (const library::EpisodeProgress* p =
                        haveLib ? lib.episode(e.season, e.number) : nullptr)
                    prog = p->done ? 1.0 : (p->dur > 0 ? p->pos / p->dur : -1.0);
                std::string title = std::to_string(e.number) + ". " +
                                    (e.title.empty() ? tr("Episode ") + std::to_string(e.number)
                                                     : e.title);
                std::string id = s->d.item.key() + "_s" + std::to_string(e.season) +
                                 "e" + std::to_string(e.number);
                int season = e.season, number = e.number;
                auto onClick = [weak, season, number, count]() {
                    if (auto c = weak.lock()) openSources(c->d.item, season, number, count);
                };
                s->episodes->addView(ui::wideCard(title, id, e.stillUrl, "", prog,
                                                  s->alive, onClick, 236.0f));
            }
        });
    }

    // The main button: a film's sources, or a series' first episode of the
    // first season (the details have to be in: that is where the IMDb id and
    // the seasons come from).
    static void watch(CardState& s)
    {
        if (!s.detailsLoaded)
        {
            note(tr("Still loading, try again in a moment."));
            return;
        }
        // Something to continue: straight back to it (the file that was
        // playing, or the next episode).
        library::Entry en;
        if (library::find(s.d.item.key(), en) &&
            library::nextOf(en).kind != library::Next::Kind::None)
        {
            en.item = s.d.item;  // the fresher snapshot (IMDb id, artwork)
            continueContent(en);
            return;
        }
        if (!s.d.item.isSeries())
        {
            openSources(s.d.item, 0, 0);
            return;
        }
        if (s.d.seasons.empty())
        {
            note(tr("No episodes"));
            return;
        }
        openSources(s.d.item, s.d.seasons[0].number, 1, s.d.seasons[0].episodeCount);
    }
};

class CardActivity : public brls::Activity
{
  public:
    explicit CardActivity(content::Item item) : item(std::move(item)) {}

    brls::View* createContentView() override
    {
        auto* frame = new brls::AppletFrame(new CardView(item));
        frame->setTitle("");
        return frame;
    }

  private:
    content::Item item;
};

} // namespace

void openContentCard(const content::Item& item)
{
    brls::Application::pushActivity(new CardActivity(item));
}

brls::Box* catalogPoster(const content::Item& item, std::shared_ptr<bool> alive, float w)
{
    auto* card = ui::posterCard(item.title, item.key(), item.posterUrl, alive,
                                [item]() { openContentCard(item); }, w);
    card->registerAction(tr("Bookmark"), brls::BUTTON_Y, [item](brls::View*) {
        library::Entry e;
        bool marked = library::find(item.key(), e) && e.status != library::Status::None;
        library::setStatus(item, marked ? library::Status::None : library::Status::Planned);
        brls::Application::notify(marked ? tr("Removed from bookmarks")
                                         : tr("Added to bookmarks: Planned"));
        return true;
    });
    return card;
}

void watchContent(const content::Item& item)
{
    if (item.isSeries())
    {
        openContentCard(item);
        return;
    }
    if (!item.imdbId.empty())
    {
        openSources(item, 0, 0);
        return;
    }
    // A list item has no IMDb id yet: the details (usually cached) carry it.
    tmdb::fetchDetailsAsync(item.kind, item.tmdbId, [item](tmdb::DetailsResult r) {
        content::Item it = item;
        if (r.ok) it.imdbId = r.details.item.imdbId;
        openSources(it, 0, 0);
    });
}
