#include "online_view.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "online/online_manager.hpp"
#include "platform_ui.hpp"
#include "player.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{

std::string lowerStr(const std::string& s)
{
    std::string out;
    for (char c : s) out += (char)std::tolower((unsigned char)c);
    return out;
}

class OnlineView : public brls::Box
{
  public:
    OnlineView(const content::Item& item, int season, int episode, int episodesInSeason)
        : item(item), reqSeason(season), reqEpisode(episode),
          episodesInSeason(episodesInSeason),
          selectedSeason(season > 0 ? season : (item.isSeries() ? 1 : 0)),
          selectedEpisode(episode > 0 ? episode : (item.isSeries() ? 1 : 0)),
          preferQuality(config::get().onlinePreferQuality)
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);
        bool mobile = platform_ui::isMobile();
        float inset = mobile ? platform_ui::sideInset() : 0.0f;
        setPadding(mobile ? 10.0f : 34.0f, mobile ? 0.0f : 60.0f, 10.0f,
                   mobile ? 0.0f : 60.0f);

        // Header
        auto* head = new brls::Box();
        head->setAxis(brls::Axis::ROW);
        head->setAlignItems(brls::AlignItems::FLEX_END);
        if (mobile) head->setMargins(0.0f, inset, 4.0f, inset);
        if (!mobile)
        {
            auto* h = new brls::Label();
            h->setText(tr("Online"));
            h->setFontSize(30.0f);
            head->addView(h);
        }
        subtitle = new brls::Label();
        subtitle->setFontSize(18.0f);
        subtitle->setTextColor(theme::textMuted());
        subtitle->setMarginLeft(mobile ? 0.0f : 20.0f);
        subtitle->setSingleLine(true);
        head->addView(subtitle);
        addView(head);

        // Row 1: Balancers pill strip
        balancersBox = new brls::Box();
        balancersBox->setAxis(brls::Axis::ROW);
        if (mobile) balancersBox->setPadding(4.0f, inset, 4.0f, inset);
        auto* s1 = ui::pillStrip(balancersBox, 50.0f);
        s1->setMargins(mobile ? 8.0f : 12.0f, 0.0f, 6.0f, 0.0f);
        addView(s1);

        // Row 2: Voice, Season, Episode (BWA series), Quality, Back link
        auto* controls = new brls::Box();
        controls->setAxis(brls::Axis::ROW);
        if (mobile) controls->setPadding(4.0f, inset, 4.0f, inset);

        backLevelBtn = ui::actionButton(tr("Back"), false, [this]() { popNav(); });
        backLevelBtn->setVisibility(brls::Visibility::GONE);
        controls->addView(backLevelBtn);

        voiceBtn = ui::actionButton(tr("Voice-over"), false, [this]() { pickVoice(); });
        controls->addView(voiceBtn);

        if (item.isSeries())
        {
            seasonBtn = ui::actionButton(tr("Season"), false, [this]() { pickSeason(); });
            controls->addView(seasonBtn);

            episodeBtn = ui::actionButton(tr("Episode"), false, [this]() { pickEpisode(); });
            episodeBtn->setVisibility(brls::Visibility::GONE);
            controls->addView(episodeBtn);
        }

        qualityBtn = ui::actionButton("", false, [this]() { pickDefaultQuality(); });
        controls->addView(qualityBtn);

        auto* s2 = ui::pillStrip(controls, 50.0f);
        s2->setMargins(0.0f, 0.0f, mobile ? 6.0f : 8.0f, 0.0f);
        addView(s2);

        status = new brls::Label();
        status->setFontSize(20.0f);
        status->setTextColor(theme::textMuted());
        status->setMargins(mobile ? 12.0f : 20.0f, inset, 0.0f, inset);
        status->setText(tr("Searching online sources..."));
        addView(status);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(6.0f, mobile ? inset : 4.0f, 20.0f, mobile ? inset : 4.0f);
        scroll->setContentView(list);
        addView(scroll);

        updateHeaderAndControls();
        discover();
    }

    ~OnlineView() override { *alive = false; }

  private:
    content::Item item;
    int reqSeason, reqEpisode, episodesInSeason;
    int selectedSeason;
    int selectedEpisode;
    int preferQuality;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    std::string kpId;
    online::OnlineRef remembered;
    bool hasRemembered = false;

    std::vector<online::BalancerInfo> balancers;
    std::vector<brls::Button*> balancerBtns;
    int activeBalancer = -1;
    int fetchGen       = 0;
    bool loadingPage   = false;
    online::PageResult currentPage;
    std::vector<std::string> navStack;  // override URLs for Link drilldown
    std::string voiceFilter;            // client-side voice filter when page has voices

    bool dirty         = false;
    bool rebuildQueued = false;

    brls::Label* subtitle     = nullptr;
    brls::Label* status       = nullptr;
    brls::Box* balancersBox   = nullptr;
    brls::Box* list           = nullptr;
    brls::Button* backLevelBtn = nullptr;
    brls::Button* voiceBtn     = nullptr;
    brls::Button* seasonBtn    = nullptr;
    brls::Button* episodeBtn   = nullptr;
    brls::Button* qualityBtn   = nullptr;

    bool isOnTop()
    {
        auto stack = brls::Application::getActivitiesStack();
        return !stack.empty() && stack.back() == getParentActivity();
    }

    void refreshList()
    {
        if (isOnTop())
        {
            dirty = false;
            rebuildList();
        }
        else
        {
            dirty = true;
        }
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
              brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (dirty && !rebuildQueued && isOnTop())
        {
            rebuildQueued = true;
            auto live     = alive;
            brls::sync([this, live]() {
                if (!*live) return;
                rebuildQueued = false;
                if (dirty) refreshList();
            });
        }
    }

    std::string qualityLabel(int q) const
    {
        if (q <= 0) return tr("Quality: Auto");
        return tr("Quality: ") + std::to_string(q) + "p";
    }

    int episodeCountForSelectedSeason() const
    {
        for (const auto& s : currentPage.seasons)
            if (s.number == selectedSeason && s.episodeCount > 0) return s.episodeCount;
        if (selectedSeason == reqSeason && episodesInSeason > 0) return episodesInSeason;
        if (selectedEpisode > 24) return selectedEpisode;
        return 24;
    }

    void updateHeaderAndControls()
    {
        std::string s = item.title;
        if (item.isSeries() && selectedSeason > 0)
        {
            s += std::string("  \xC2\xB7  ") + tr("Season ") + std::to_string(selectedSeason);
            if (currentPage.perEpisodeMode && selectedEpisode > 0)
                s += std::string("  \xC2\xB7  ") + tr("Episode ") + std::to_string(selectedEpisode);
        }
        else if (!item.year.empty())
        {
            s += " (" + item.year + ")";
        }
        if (activeBalancer >= 0 && activeBalancer < (int)balancers.size())
            s += "  \xC2\xB7  " + balancers[(size_t)activeBalancer].name;
        subtitle->setText(s);

        if (backLevelBtn)
            backLevelBtn->setVisibility(navStack.empty() ? brls::Visibility::GONE
                                                         : brls::Visibility::VISIBLE);

        std::string activeVoiceLabel;
        for (const auto& v : currentPage.voices)
            if (v.active) { activeVoiceLabel = v.name; break; }
        if (activeVoiceLabel.empty()) activeVoiceLabel = voiceFilter;
        voiceBtn->setText(activeVoiceLabel.empty() ? std::string(tr("Voice-over")) : activeVoiceLabel);
        ui::setPillActive(voiceBtn, !activeVoiceLabel.empty());

        if (seasonBtn)
        {
            seasonBtn->setText(selectedSeason > 0 ? tr("Season ") + std::to_string(selectedSeason)
                                                  : std::string(tr("Season")));
            ui::setPillActive(seasonBtn, selectedSeason > 0);
        }
        if (episodeBtn)
        {
            bool showEp = item.isSeries() && currentPage.perEpisodeMode;
            episodeBtn->setVisibility(showEp ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
            episodeBtn->setText(tr("Episode ") + std::to_string(selectedEpisode));
            ui::setPillActive(episodeBtn, showEp);
        }
        if (qualityBtn)
        {
            qualityBtn->setText(qualityLabel(preferQuality));
            ui::setPillActive(qualityBtn, preferQuality > 0);
        }
    }

    void discover()
    {
        hasRemembered = online::loadChoice(item.key(), remembered);
        auto live     = alive;
        online::discoverAllAsync(item, [this, live](online::DiscoverResult r) {
            if (!*live) return;
            kpId      = r.kpId;
            balancers = std::move(r.balancers);
            buildBalancerPills();

            if (balancers.empty())
            {
                std::string msg = tr("No online balancers available.");
                if (!r.errors.empty()) msg += "\n" + r.errors[0];
                status->setText(msg);
                status->setVisibility(brls::Visibility::VISIBLE);
                return;
            }

            int pick = 0;
            if (hasRemembered)
            {
                for (int i = 0; i < (int)balancers.size(); i++)
                {
                    if (balancers[(size_t)i].pluginId == remembered.pluginId &&
                        balancers[(size_t)i].id == remembered.balancerId)
                    {
                        pick = i;
                        break;
                    }
                }
            }
            selectBalancer(pick);
        });
    }

    void buildBalancerPills()
    {
        ui::safeClearViews(balancersBox, voiceBtn);
        balancerBtns.clear();
        auto live = alive;
        for (int i = 0; i < (int)balancers.size(); i++)
        {
            auto* b = ui::actionButton(balancers[(size_t)i].name, false,
                                       [this, live, i]() {
                                           if (*live) selectBalancer(i);
                                       });
            balancerBtns.push_back(b);
            balancersBox->addView(b);
        }
    }

    void restyleBalancerPills()
    {
        for (int i = 0; i < (int)balancerBtns.size(); i++)
            ui::setPillActive(balancerBtns[(size_t)i], i == activeBalancer);
    }

    void selectBalancer(int idx)
    {
        if (idx < 0 || idx >= (int)balancers.size()) return;
        activeBalancer = idx;
        navStack.clear();
        voiceFilter.clear();
        restyleBalancerPills();
        fetchCurrent("");
    }

    void pushNav(const std::string& overrideUrl)
    {
        navStack.push_back(overrideUrl);
        fetchCurrent(overrideUrl);
    }

    void popNav()
    {
        if (navStack.empty()) return;
        navStack.pop_back();
        fetchCurrent(navStack.empty() ? "" : navStack.back());
    }

    void fetchCurrent(const std::string& overrideUrl)
    {
        if (activeBalancer < 0 || activeBalancer >= (int)balancers.size()) return;
        fetchGen++;
        int gen     = fetchGen;
        loadingPage = true;
        currentPage = online::PageResult();
        updateHeaderAndControls();
        ui::safeClearViews(list, voiceBtn);
        status->setText(tr("Loading..."));
        status->setVisibility(brls::Visibility::VISIBLE);

        const online::BalancerInfo bal = balancers[(size_t)activeBalancer];
        auto live = alive;
        online::fetchPageAsync(
            bal, overrideUrl, item, kpId, selectedSeason, selectedEpisode,
            [this, live, gen, overrideUrl](online::PageResult pr) {
                if (!*live || gen != fetchGen) return;
                loadingPage = false;
                currentPage = std::move(pr);

                // If classic Lampac returned Seasons list and we want a specific season,
                // auto-enter that season once when opening from a season/episode context.
                if (overrideUrl.empty() && currentPage.ok &&
                    currentPage.type == online::PageType::Seasons && selectedSeason > 0)
                {
                    for (const auto& s : currentPage.seasons)
                    {
                        if (s.number == selectedSeason && !s.url.empty())
                        {
                            pushNav(s.url);
                            return;
                        }
                    }
                }

                // Auto-apply remembered voice if available
                if (hasRemembered && !remembered.voiceName.empty() &&
                    activeBalancer >= 0 &&
                    balancers[(size_t)activeBalancer].id == remembered.balancerId)
                {
                    if (!currentPage.perEpisodeMode)
                    {
                        for (const auto& v : currentPage.voices)
                        {
                            if (!v.active && !v.url.empty() &&
                                lowerStr(v.name) == lowerStr(remembered.voiceName) &&
                                overrideUrl != v.url)
                            {
                                fetchCurrent(v.url);
                                return;
                            }
                        }
                    }
                }

                updateHeaderAndControls();
                refreshList();
            });
    }

    void pickVoice()
    {
        if (currentPage.voices.empty()) return;
        std::vector<std::string> labels;
        std::vector<std::string> urls;
        int sel = 0;
        if (currentPage.perEpisodeMode)
        {
            labels.push_back(tr("Any voice-over"));
            urls.push_back("");
        }
        for (size_t i = 0; i < currentPage.voices.size(); i++)
        {
            const auto& v = currentPage.voices[i];
            if (currentPage.perEpisodeMode)
            {
                if (v.name == voiceFilter) sel = (int)labels.size();
            }
            else if (v.active)
            {
                sel = (int)labels.size();
            }
            labels.push_back(v.name);
            urls.push_back(v.url);
        }
        auto live = alive;
        auto* dd = new brls::Dropdown(tr("Voice-over"), labels, [this, live, labels, urls](int i) {
            if (!*live || i < 0 || i >= (int)labels.size()) return;
            if (currentPage.perEpisodeMode)
            {
                voiceFilter = (i == 0) ? "" : labels[(size_t)i];
                updateHeaderAndControls();
                refreshList();
            }
            else if (!urls[(size_t)i].empty())
            {
                navStack.clear();
                fetchCurrent(urls[(size_t)i]);
            }
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void pickSeason()
    {
        std::vector<std::string> labels;
        std::vector<online::SeasonOption> opts = currentPage.seasons;
        if (opts.empty())
        {
            int maxS = item.seasonCount > 0 ? item.seasonCount : std::max(1, selectedSeason);
            for (int s = 1; s <= maxS; s++)
            {
                online::SeasonOption o;
                o.number = s;
                o.name   = tr("Season ") + std::to_string(s);
                opts.push_back(std::move(o));
            }
        }
        int sel = 0;
        for (size_t i = 0; i < opts.size(); i++)
        {
            if (opts[i].number == selectedSeason) sel = (int)i;
            std::string l = opts[i].name;
            if (l == std::to_string(opts[i].number))
                l = tr("Season ") + std::to_string(opts[i].number);
            if (opts[i].episodeCount > 0)
                l += " (" + std::to_string(opts[i].episodeCount) + ")";
            labels.push_back(l);
        }
        auto live = alive;
        auto* dd = new brls::Dropdown(tr("Season"), labels, [this, live, opts](int i) {
            if (!*live || i < 0 || i >= (int)opts.size()) return;
            selectedSeason  = opts[(size_t)i].number;
            selectedEpisode = 1;
            navStack.clear();
            if (!opts[(size_t)i].url.empty())
                pushNav(opts[(size_t)i].url);
            else
                fetchCurrent("");
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void pickEpisode()
    {
        int count = episodeCountForSelectedSeason();
        std::vector<std::string> labels;
        int sel = 0;
        for (int e = 1; e <= count; e++)
        {
            if (e == selectedEpisode) sel = e - 1;
            labels.push_back(tr("Episode ") + std::to_string(e));
        }
        auto live = alive;
        auto* dd = new brls::Dropdown(tr("Episode"), labels, [this, live](int i) {
            if (!*live || i < 0) return;
            selectedEpisode = i + 1;
            fetchCurrent("");
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void pickDefaultQuality()
    {
        static const int kVals[] = { 0, 2160, 1080, 720, 480 };
        std::vector<std::string> labels = { tr("Auto (best)"), "2160p", "1080p", "720p", "480p" };
        int sel = 0;
        for (int i = 0; i < 5; i++)
            if (kVals[i] == preferQuality) sel = i;
        auto live = alive;
        auto* dd = new brls::Dropdown(tr("Quality"), labels, [this, live](int i) {
            if (!*live || i < 0 || i >= 5) return;
            preferQuality = kVals[i];
            config::get().onlinePreferQuality = preferQuality;
            config::save();
            updateHeaderAndControls();
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void rebuildList()
    {
        ui::safeClearViews(list, voiceBtn);
        if (loadingPage) return;

        if (!currentPage.ok || currentPage.entries.empty())
        {
            std::string msg = !currentPage.error.empty() ? currentPage.error
                                                         : tr("No online streams found.");
            status->setText(msg);
            status->setVisibility(brls::Visibility::VISIBLE);
            return;
        }

        std::vector<const online::OnlineEntry*> shown;
        for (const auto& e : currentPage.entries)
        {
            if (currentPage.perEpisodeMode && !voiceFilter.empty() && e.voiceName != voiceFilter)
                continue;
            shown.push_back(&e);
        }
        if (shown.empty())
        {
            status->setText(tr("Nothing matches this filter."));
            status->setVisibility(brls::Visibility::VISIBLE);
            return;
        }
        status->setVisibility(brls::Visibility::GONE);

        library::Entry lib;
        bool haveLib = library::find(item.key(), lib);
        brls::View* focusRow = nullptr;

        for (size_t i = 0; i < shown.size(); i++)
        {
            const online::OnlineEntry& e = *shown[i];
            brls::Box* row = buildRow(e, haveLib ? &lib : nullptr);
            list->addView(row);

            if (!focusRow)
            {
                if (reqEpisode > 0 && e.episode == reqEpisode) focusRow = row;
                else if (hasRemembered && !remembered.voiceName.empty() &&
                         lowerStr(e.voiceName) == lowerStr(remembered.voiceName))
                    focusRow = row;
            }
        }
        if (!focusRow && !list->getChildren().empty())
            focusRow = list->getChildren()[0];
        if (focusRow && isOnTop()) brls::Application::giveFocus(focusRow);
    }

    brls::Box* buildRow(const online::OnlineEntry& e, const library::Entry* lib)
    {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(12.0f, 18.0f, 12.0f, 18.0f);
        row->setMarginBottom(10.0f);
        row->setCornerRadius(10.0f);
        row->setBackgroundColor(theme::scrim(14));
        row->setFocusable(true);
        row->setHighlightCornerRadius(10.0f);

        std::string qText = e.maxQuality.empty() ? "\xE2\x80\x94" : e.maxQuality;
        auto* q = new brls::Label();
        q->setText(qText);
        q->setFontSize(22.0f);
        q->setWidth(100.0f);
        q->setSingleLine(true);
        row->addView(q);

        auto* mid = new brls::Box();
        mid->setAxis(brls::Axis::COLUMN);
        mid->setGrow(1.0f);
        mid->setShrink(1.0f);
        mid->setMarginRight(16.0f);

        std::string titleText = e.title;
        if (e.episode > 0 && !currentPage.perEpisodeMode)
        {
            std::string prefix = tr("Episode ") + std::to_string(e.episode);
            if (titleText.empty()) titleText = prefix;
            else if (titleText.find(std::to_string(e.episode)) == std::string::npos)
                titleText = prefix + " \xC2\xB7 " + titleText;
        }
        if (titleText.empty()) titleText = item.title;

        auto* t = new brls::Label();
        t->setText(titleText);
        t->setFontSize(19.0f);
        t->setSingleLine(true);
        mid->addView(t);

        auto* tags = new brls::Box();
        tags->setAxis(brls::Axis::ROW);
        tags->setMarginTop(8.0f);
        auto addTag = [tags](const std::string& text, NVGcolor color) {
            if (text.empty()) return;
            auto* b = new brls::Box();
            b->setCornerRadius(5.0f);
            b->setPadding(4.0f, 8.0f, 2.0f, 8.0f);
            b->setMarginRight(8.0f);
            b->setBorderColor(color);
            b->setBorderThickness(1.0f);
            auto* l = new brls::Label();
            l->setText(text);
            l->setFontSize(14.0f);
            l->setTextColor(color);
            l->setSingleLine(true);
            b->addView(l);
            tags->addView(b);
        };

        if (!e.voiceName.empty() && e.voiceName != titleText)
            addTag(e.voiceName, theme::textDim());
        if (!e.details.empty())
            addTag(e.details, theme::textDim());
        if (!e.subtitles.empty())
            addTag(tr("Subtitles") + std::string(": ") + std::to_string(e.subtitles.size()),
                   theme::textMuted());
        if (e.qualities.size() > 1)
        {
            std::string qSummary;
            for (size_t i = 0; i < e.qualities.size() && i < 3; i++)
            {
                if (!qSummary.empty()) qSummary += "/";
                qSummary += e.qualities[i].label;
            }
            addTag(qSummary, theme::textMuted());
        }
        if (activeBalancer >= 0 && activeBalancer < (int)balancers.size())
            addTag(balancers[(size_t)activeBalancer].name, theme::textMuted());

        mid->addView(tags);
        row->addView(mid);

        // Right column: watch progress / status
        if (lib && item.isSeries() && e.episode > 0)
        {
            int s = e.season > 0 ? e.season : selectedSeason;
            if (const library::EpisodeProgress* p = lib->episode(s, e.episode))
            {
                std::string progTxt;
                NVGcolor progCol = theme::textMuted();
                if (p->done)
                {
                    progTxt = tr("Watched");
                    progCol = theme::textGood();
                }
                else if (p->pos >= 15.0)
                {
                    progTxt = fmtTime(p->pos);
                    progCol = theme::accentText();
                }
                if (!progTxt.empty())
                {
                    auto* prLbl = new brls::Label();
                    prLbl->setText(progTxt);
                    prLbl->setFontSize(15.0f);
                    prLbl->setTextColor(progCol);
                    prLbl->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
                    row->addView(prLbl);
                }
            }
        }

        online::OnlineEntry copy = e;
        auto live = alive;
        row->registerClickAction([this, live, copy](brls::View*) {
            if (*live) activateEntry(copy, "", "");
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        if (copy.qualities.size() > 1)
        {
            row->registerAction(tr("Quality"), brls::BUTTON_X, [this, live, copy](brls::View*) {
                std::vector<std::string> qLabels;
                for (const auto& qOpt : copy.qualities) qLabels.push_back(qOpt.label);
                auto* dd = new brls::Dropdown(
                    tr("Quality"), qLabels, [](int) {}, -1,
                    [this, live, copy](int qi) {
                        if (!*live || qi < 0 || qi >= (int)copy.qualities.size()) return;
                        activateEntry(copy, copy.qualities[(size_t)qi].url,
                                      copy.qualities[(size_t)qi].label);
                    });
                brls::Application::pushActivity(new brls::Activity(dd));
                return true;
            });
        }
        return row;
    }

    void activateEntry(const online::OnlineEntry& e,
                       const std::string& overrideQUrl,
                       const std::string& overrideQLabel)
    {
        if (e.method == online::OnlineMethod::Link)
        {
            pushNav(e.url);
            return;
        }
        if (activeBalancer < 0 || activeBalancer >= (int)balancers.size()) return;
        const online::BalancerInfo& bal = balancers[(size_t)activeBalancer];

        online::OnlineRef ref;
        ref.pluginId   = bal.pluginId;
        ref.balancerId = bal.id;
        ref.voiceName  = e.voiceName;
        online::saveChoice(item.key(), ref);

        online::OnlineEntry playItem = e;
        if (item.isSeries())
        {
            if (playItem.season <= 0) playItem.season = selectedSeason;
            if (playItem.episode <= 0 && currentPage.perEpisodeMode)
                playItem.episode = selectedEpisode;
        }

        std::vector<online::OnlineEntry> seasonEps;
        int epIdx = -1;

        if (item.isSeries() && currentPage.perEpisodeMode && playItem.episode > 0)
        {
            int count = episodeCountForSelectedSeason();
            for (int k = 1; k <= count; k++)
            {
                if (k == playItem.episode)
                {
                    epIdx = (int)seasonEps.size();
                    seasonEps.push_back(playItem);
                }
                else
                {
                    online::OnlineEntry ce;
                    ce.method    = online::OnlineMethod::Call;
                    ce.season    = playItem.season;
                    ce.episode   = k;
                    ce.title     = tr("Episode ") + std::to_string(k);
                    ce.voiceName = playItem.voiceName;
                    ce.url       = online::buildEpisodeCallUrl(bal, item, kpId, playItem.season, k);
                    seasonEps.push_back(std::move(ce));
                }
            }
        }
        else if (item.isSeries() && currentPage.type == online::PageType::Episodes)
        {
            for (size_t i = 0; i < currentPage.entries.size(); i++)
            {
                const auto& cur = currentPage.entries[i];
                if (cur.method == online::OnlineMethod::Link) continue;
                if (cur.episode == playItem.episode && cur.url == playItem.url)
                    epIdx = (int)seasonEps.size();
                seasonEps.push_back(cur);
            }
        }

        online::playOnlineEntry(item, ref, playItem, seasonEps, epIdx, 2,
                                overrideQUrl, overrideQLabel, true);
    }
};

} // namespace

void openOnlineScreen(const content::Item& item, int season, int episode, int episodesInSeason)
{
    auto* frame = new ui::FieldFrame(new OnlineView(item, season, episode, episodesInSeason));
    if (platform_ui::isMobile())
    {
        frame->setTitle(tr("Online"));
    }
    else
    {
        frame->setTitle("");
        frame->setHeaderVisibility(brls::Visibility::GONE);
    }
    brls::Application::pushActivity(new brls::Activity(frame));
}
