#include "card.hpp"

#include <borealis.hpp>

#include <cstdio>
#include <memory>

#include "browse.hpp"       // openLibraryItem / openEpisodeById: Stremio sources
#include "config.hpp"
#include "i18n.hpp"
#include "jackett.hpp"      // jackett::Category
#include "jackett_tab.hpp"  // openJackettSearch
#include "player.hpp"       // PlayerArt
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

std::string twoDigits(int n)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%02d", n);
    return b;
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

// Where an episode or a film is looked for. Stremio's addons first: they key
// on the IMDb id and answer with ready sources. Jackett otherwise, searched by
// name. Neither set up: say what would make this work.
void openSources(const content::Item& it, int season, int episode)
{
    std::string authKey = stremio::loadAuthKey();
    if (!authKey.empty() && !it.imdbId.empty())
    {
        if (!it.isSeries())
        {
            stremio::LibItem li;
            li.id     = it.imdbId;
            li.name   = it.title;
            li.type   = "movie";
            li.poster = it.posterUrlLarge;
            li.year   = it.year;
            openLibraryItem(authKey, li);
            return;
        }
        if (season > 0 || episode > 0)
        {
            PlayerArt art;
            art.posterPath = stremio::cachedPosterPath(it.key());
            art.bgId       = it.key() + "_bd";
            art.bgUrl      = it.backdropUrl;
            std::string vid = it.imdbId + ":" + std::to_string(season) + ":" +
                              std::to_string(episode);
            openEpisodeById(authKey, it.imdbId, vid, art);
            return;
        }
    }

    if (!config::get().jackettUrl.empty())
    {
        std::string q = it.title;
        if (!it.isSeries() && !it.year.empty()) q += " " + it.year;
        if (it.isSeries() && season > 0 && episode > 0)
            q += " S" + twoDigits(season) + "E" + twoDigits(episode);
        openJackettSearch(q, (int)(it.isSeries() ? jackett::Category::SERIES
                                                 : jackett::Category::MOVIES));
        return;
    }

    note(tr("To find sources, sign in to Stremio or set up Jackett in Options."));
}

// The card's state, shared by the callbacks that fill it in.
struct CardState
{
    content::Details d;
    bool detailsLoaded = false;
    int seasonIdx      = 0;   // into d.seasons
    int seasonReq      = 0;   // bumps on every switch: late answers are dropped
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    brls::Image* backdrop      = nullptr;
    brls::Box* chips           = nullptr;
    brls::Label* overview      = nullptr;
    brls::Label* cast          = nullptr;
    brls::Button* playBtn      = nullptr;
    brls::Box* seasonsSection  = nullptr;
    brls::Box* seasonBar       = nullptr;
    std::vector<brls::Button*> seasonBtns;
    brls::Box* episodes        = nullptr;
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
        poster->setDimensions(220.0f, 330.0f);
        poster->setScalingType(brls::ImageScalingType::FILL);
        poster->setClipsToBounds(true);
        poster->setCornerRadius(12.0f);
        poster->setMarginRight(40.0f);
        poster->setShadowType(brls::ShadowType::GENERIC);
        poster->setShadowVisibility(true);
        ui::attachArt(poster, it.key(), it.posterUrlLarge, alive);
        top->addView(poster);

        auto* info = new brls::Box();
        info->setAxis(brls::Axis::COLUMN);
        info->setGrow(1.0f);
        info->setShrink(1.0f);

        info->addView(whiteLabel(it.title, 40.0f, 255));

        st->chips = new brls::Box();
        st->chips->setAxis(brls::Axis::ROW);
        st->chips->setMarginTop(16.0f);
        info->addView(st->chips);
        fillChips();

        st->overview = whiteLabel(trimmed(it.overview), 19.0f, 225);
        st->overview->setMarginTop(18.0f);
        info->addView(st->overview);

        st->cast = whiteLabel("", 16.0f, 165);
        st->cast->setMarginTop(10.0f);
        st->cast->setVisibility(brls::Visibility::GONE);
        info->addView(st->cast);

        auto* actions = new brls::Box();
        actions->setAxis(brls::Axis::ROW);
        actions->setMarginTop(24.0f);

        auto weak = std::weak_ptr<CardState>(st);
        st->playBtn = ui::actionButton(playLabel(), true, [weak]() {
            if (auto s = weak.lock()) watch(*s);
        });
        actions->addView(st->playBtn);

        actions->addView(ui::actionButton(tr("Torrents"), false, [weak]() {
            auto s = weak.lock();
            if (!s) return;
            if (config::get().jackettUrl.empty())
            {
                note(tr("Set up Jackett in Options to search torrents."));
                return;
            }
            content::Item i = s->d.item;
            std::string q = i.title;
            if (!i.isSeries() && !i.year.empty()) q += " " + i.year;
            openJackettSearch(q, (int)(i.isSeries() ? jackett::Category::SERIES
                                                    : jackett::Category::MOVIES));
        }));
        info->addView(actions);

        top->addView(info);
        body->addView(top);
    }

    static std::string trimmed(const std::string& s)
    {
        // A synopsis can run to a page; the card has room for a paragraph.
        const size_t kMax = 420;
        if (s.size() <= kMax) return s;
        size_t cut = s.rfind(' ', kMax);
        if (cut == std::string::npos) cut = kMax;
        // Don't split a UTF-8 sequence.
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        return s.substr(0, cut) + "...";
    }

    std::string playLabel() const
    {
        const content::Details& d = st->d;
        if (d.item.isSeries() && !d.seasons.empty())
        {
            const content::Season& s = d.seasons[0];
            return std::string("\xE2\x96\xB6  ") + tr("Watch S") + std::to_string(s.number) +
                   tr(" E") + "1";
        }
        return std::string("\xE2\x96\xB6  ") + tr("Watch");
    }

    void fillChips()
    {
        st->chips->clearViews();
        const content::Details& d = st->d;
        const content::Item& it   = d.item;
        if (it.rating > 0.05) st->chips->addView(ui::chip(ratingText(it.rating), true));
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
        st->seasonsSection->setMarginTop(30.0f);
        st->seasonsSection->setVisibility(brls::Visibility::GONE);

        st->seasonBar = new brls::Box();
        st->seasonBar->setAxis(brls::Axis::ROW);
        st->seasonBar->setMarginBottom(14.0f);
        st->seasonsSection->addView(st->seasonBar);

        st->episodes = new brls::Box();
        // 300 wide at 16:9, plus room for the focus glow.
        st->seasonsSection->addView(ui::strip(st->episodes, 169.0f + 24.0f, false));
        body->addView(st->seasonsSection);
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
            auto* b = ui::actionButton(name, false, [this, idx]() { selectSeason(idx); });
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
            st->seasonBtns[i]->setStyle((int)i == idx ? &brls::BUTTONSTYLE_PRIMARY
                                                      : &brls::BUTTONSTYLE_BORDERED);

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
            for (const auto& e : r.episodes)
            {
                std::string title = std::to_string(e.number) + ". " +
                                    (e.title.empty() ? tr("Episode ") + std::to_string(e.number)
                                                     : e.title);
                std::string id = s->d.item.key() + "_s" + std::to_string(e.season) +
                                 "e" + std::to_string(e.number);
                int season = e.season, number = e.number;
                auto onClick = [weak, season, number]() {
                    if (auto c = weak.lock()) openSources(c->d.item, season, number);
                };
                s->episodes->addView(ui::wideCard(title, id, e.stillUrl, "", -1.0,
                                                  s->alive, onClick, 300.0f));
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
        openSources(s.d.item, s.d.seasons[0].number, 1);
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
