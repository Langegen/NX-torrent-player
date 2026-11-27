#include "jackett_tab.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <strings.h>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "appdata.hpp"
#include "browse.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include "jackett.hpp"
#include "platform_ui.hpp"
#include "player.hpp"
#include "release.hpp"
#include "torrent_play.hpp"
#include "theme.hpp"
#include "widgets.hpp"  // ui::FieldFrame, ui::actionButton, ui::pillStrip

extern "C" {
#include "torrent.h"
}

namespace
{

class JackettTabRoot : public brls::Box
{
  public:
    JackettTabRoot()
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);
    }

    ~JackettTabRoot() override
    {
        *alive = false;
    }

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    // The controller behind this tab. Held here so it dies with the tab --
    // it used to be `new`ed and never freed, one leak per visit to the tab.
    std::shared_ptr<void> controller;
};

class JackettView
{
  public:
    JackettView(JackettTabRoot* root)
        : root(root), alive(root->alive)
    {
        const std::string& al = config::get().audioLang;
        want.preferRussian    = i18n::lang() == "ru" || al == "ru" ||
                                (al == "auto" && config::consoleLang() == "ru");
#ifdef __SWITCH__
        want.switchProfile = true;
#else
        want.switchProfile = false;
#endif
        setupLayout();
        refresh();
    }

  private:
    struct ParsedRow
    {
        jackett::Item item;
        release::Info info;
        std::vector<std::string> trackers;
        std::string trackerLabel;
        int score = 0;
    };

    enum class SortMode
    {
        RECOMMENDED = 0,
        SEEDERS     = 1,
        SIZE        = 2,
        DATE        = 3,
    };

    enum class Facet
    {
        Voice,
        Tracker,
    };

    JackettTabRoot* root = nullptr;
    std::shared_ptr<bool> alive;

    brls::Box* controlsBox = nullptr;
    brls::Box* searchBar   = nullptr;
    brls::Label* barText   = nullptr;
    brls::Box* categoryRow = nullptr;
    brls::Button* catAllBtn    = nullptr;
    brls::Button* catMovieBtn  = nullptr;
    brls::Button* catSeriesBtn = nullptr;
    brls::Button* catAnimeBtn  = nullptr;
    brls::Button* sortBtn      = nullptr;

    brls::Box* filterRow       = nullptr;
    std::vector<std::pair<brls::Button*, int>> heightBtns;
    brls::Button* aliveBtn     = nullptr;
    brls::Button* voiceBtn     = nullptr;
    brls::Button* trackerBtn   = nullptr;

    brls::Box* contentContainer = nullptr;

    std::string searchQuery;
    jackett::Category currentCategory = jackett::Category::ALL;
    SortMode currentSort              = SortMode::RECOMMENDED;
    release::Want want;
    int filterHeight                  = 0;
    bool filterAliveOnly              = false;
    std::string filterVoice;
    std::string filterTracker;
    std::vector<ParsedRow> searchResults;
    bool isSearching = false;
    std::string searchError;

    void setupLayout()
    {
        bool mobile = platform_ui::isMobile();
        float inset = platform_ui::sideInset();

        // Top controls: search bar + categories + sort + release filters
        controlsBox = new brls::Box();
        controlsBox->setAxis(brls::Axis::COLUMN);
        controlsBox->setPadding(mobile ? 16.0f : 20.0f, mobile ? 0.0f : 60.0f, 6.0f,
                                mobile ? 0.0f : 60.0f);

        // 1. Search Bar
        searchBar = new brls::Box();
        searchBar->setAxis(brls::Axis::ROW);
        searchBar->setAlignItems(brls::AlignItems::CENTER);
        searchBar->setHeight(mobile ? 58.0f : 64.0f);
        if (mobile) searchBar->setMargins(0.0f, inset, 0.0f, inset);
        searchBar->setPaddingLeft(20.0f);
        searchBar->setPaddingRight(24.0f);
        searchBar->setCornerRadius(mobile ? 10.0f : 6.0f);
        searchBar->setBackgroundColor(theme::scrim(20));
        searchBar->setFocusable(true);
        searchBar->addGestureRecognizer(new brls::TapGestureRecognizer(searchBar));
        searchBar->registerClickAction([this](brls::View*) {
            promptSearch();
            return true;
        });

        auto* icon = new brls::Label();
        icon->setText("");
        icon->setFontSize(30.0f);
        icon->setMargins(4.0f, 14.0f, 0.0f, 0.0f);
        searchBar->addView(icon);

        barText = new brls::Label();
        barText->setText(tr("Search Jackett..."));
        barText->setFontSize(mobile ? 20.0f : 22.0f);
        barText->setTextColor(theme::textMuted());
        barText->setSingleLine(true);
        searchBar->addView(barText);

        controlsBox->addView(searchBar);

        // 2. Categories + Sort bar
        categoryRow = new brls::Box();
        categoryRow->setAxis(brls::Axis::ROW);
        categoryRow->setAlignItems(brls::AlignItems::CENTER);
        if (mobile)
            categoryRow->setPadding(4.0f, inset, 4.0f, inset);
        else
            categoryRow->setMarginTop(12.0f);

        catAllBtn    = makeCategoryButton(tr("All"), jackett::Category::ALL);
        catMovieBtn  = makeCategoryButton(tr("Movies"), jackett::Category::MOVIES);
        catSeriesBtn = makeCategoryButton(tr("Series"), jackett::Category::SERIES);
        catAnimeBtn  = makeCategoryButton(tr("Anime"), jackett::Category::ANIME);

        categoryRow->addView(catAllBtn);
        categoryRow->addView(catMovieBtn);
        categoryRow->addView(catSeriesBtn);
        categoryRow->addView(catAnimeBtn);

        if (!mobile)
        {
            auto* spacer = new brls::Box();
            spacer->setGrow(1.0f);
            categoryRow->addView(spacer);
        }

        sortBtn = new brls::Button();
        sortBtn->setShrink(0.0f);
        if (auto* l = dynamic_cast<brls::Label*>(sortBtn->getView("brls/button/label")))
            l->setSingleLine(true);
        sortBtn->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        updateSortButtonLabel();
        sortBtn->registerClickAction([this](brls::View*) {
            cycleSort();
            return true;
        });
        categoryRow->addView(sortBtn);

        if (mobile)
        {
            auto* strip = ui::pillStrip(categoryRow, 54.0f);
            strip->setMarginTop(8.0f);
            controlsBox->addView(strip);
        }
        else
            controlsBox->addView(categoryRow);

        // 3. Quality, alive-only, voice and tracker filters
        filterRow = new brls::Box();
        filterRow->setAxis(brls::Axis::ROW);
        if (mobile) filterRow->setPadding(4.0f, inset, 4.0f, inset);
        const std::pair<const char*, int> qualities[] = {
            { "All", 0 }, { "2160p", 2160 }, { "1080p", 1080 }, { "720p", 720 }
        };
        for (const auto& q : qualities)
        {
            int height = q.second;
            auto* b    = ui::actionButton(q.second ? q.first : tr(q.first), false, [this, height]() {
                filterHeight = height;
                updateFilterStyles();
                if (!searchResults.empty()) refresh();
            });
            heightBtns.push_back({ b, height });
            filterRow->addView(b);
        }
        aliveBtn = ui::actionButton(tr("Alive (>0)"), false, [this]() {
            filterAliveOnly = !filterAliveOnly;
            updateFilterStyles();
            if (!searchResults.empty()) refresh();
        });
        voiceBtn   = ui::actionButton("", false, [this]() { pickFacet(Facet::Voice); });
        trackerBtn = ui::actionButton("", false, [this]() { pickFacet(Facet::Tracker); });
        filterRow->addView(aliveBtn);
        filterRow->addView(voiceBtn);
        filterRow->addView(trackerBtn);

        auto* filterStrip = ui::pillStrip(filterRow, 50.0f);
        filterStrip->setMarginTop(mobile ? 4.0f : 8.0f);
        controlsBox->addView(filterStrip);

        root->addView(controlsBox);

        // 4. Content container
        contentContainer = new brls::Box();
        contentContainer->setAxis(brls::Axis::COLUMN);
        contentContainer->setGrow(1.0f);
        root->addView(contentContainer);

        updateCategoryButtonStyles();
        updateFilterStyles();
    }

    brls::Button* makeCategoryButton(const std::string& label, jackett::Category cat)
    {
        auto* btn = new brls::Button();
        btn->setShrink(0.0f);
        btn->setText(label);
        if (auto* l = dynamic_cast<brls::Label*>(btn->getView("brls/button/label")))
            l->setSingleLine(true);
        btn->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        btn->setMarginRight(8.0f);
        btn->registerClickAction([this, cat](brls::View*) {
            if (currentCategory != cat)
            {
                currentCategory = cat;
                updateCategoryButtonStyles();
                if (!searchQuery.empty())
                    runSearch();
            }
            return true;
        });
        return btn;
    }

    void updateCategoryButtonStyles()
    {
        catAllBtn->setStyle(currentCategory == jackett::Category::ALL
                                ? &brls::BUTTONSTYLE_PRIMARY
                                : &brls::BUTTONSTYLE_BORDERLESS);
        catMovieBtn->setStyle(currentCategory == jackett::Category::MOVIES
                                  ? &brls::BUTTONSTYLE_PRIMARY
                                  : &brls::BUTTONSTYLE_BORDERLESS);
        catSeriesBtn->setStyle(currentCategory == jackett::Category::SERIES
                                   ? &brls::BUTTONSTYLE_PRIMARY
                                   : &brls::BUTTONSTYLE_BORDERLESS);
        catAnimeBtn->setStyle(currentCategory == jackett::Category::ANIME
                                  ? &brls::BUTTONSTYLE_PRIMARY
                                  : &brls::BUTTONSTYLE_BORDERLESS);
    }

    void updateSortButtonLabel()
    {
        switch (currentSort)
        {
            case SortMode::RECOMMENDED:
                sortBtn->setText(tr("Sort: Recommended"));
                break;
            case SortMode::SEEDERS:
                sortBtn->setText(tr("Sort: Seeders"));
                break;
            case SortMode::SIZE:
                sortBtn->setText(tr("Sort: Size"));
                break;
            case SortMode::DATE:
                sortBtn->setText(tr("Sort: Date"));
                break;
        }
    }

    void updateFilterStyles()
    {
        for (auto& hb : heightBtns)
            ui::setPillActive(hb.first, hb.second == filterHeight);
        if (aliveBtn) ui::setPillActive(aliveBtn, filterAliveOnly);
        if (voiceBtn)
        {
            voiceBtn->setText(filterVoice.empty() ? std::string(tr("Voice-over")) : filterVoice);
            ui::setPillActive(voiceBtn, !filterVoice.empty());
        }
        if (trackerBtn)
        {
            trackerBtn->setText(filterTracker.empty() ? std::string(tr("Tracker")) : filterTracker);
            ui::setPillActive(trackerBtn, !filterTracker.empty());
        }
    }

    std::vector<std::string> facetValues(Facet f) const
    {
        std::map<std::string, int> count;
        for (const auto& r : searchResults)
        {
            if (f == Facet::Voice)
                for (const auto& v : r.info.voices) count[v]++;
            else if (f == Facet::Tracker)
            {
                if (!r.trackers.empty())
                    for (const auto& trk : r.trackers) count[trk]++;
                else if (!r.trackerLabel.empty())
                    count[r.trackerLabel]++;
            }
        }
        std::vector<std::pair<std::string, int>> v(count.begin(), count.end());
        std::stable_sort(v.begin(), v.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<std::string> out;
        for (const auto& p : v) out.push_back(p.first);
        return out;
    }

    void pickFacet(Facet f)
    {
        if (searchResults.empty()) return;
        std::vector<std::string> labels, values;
        labels.push_back(f == Facet::Voice ? tr("Any voice-over") : tr("Any tracker"));
        values.push_back("");
        for (const auto& v : facetValues(f))
        {
            labels.push_back(v);
            values.push_back(v);
        }
        const std::string& cur = (f == Facet::Voice) ? filterVoice : filterTracker;
        int sel = 0;
        for (size_t i = 0; i < values.size(); i++)
            if (values[i] == cur) sel = (int)i;
        auto live = alive;
        auto* dd = new brls::Dropdown(tr(f == Facet::Voice ? "Voice-over" : "Tracker"),
                                      labels, [this, live, f, values](int i) {
            if (!*live) return;
            if (i < 0 || i >= (int)values.size()) return;
            if (f == Facet::Voice) filterVoice = values[i];
            else filterTracker = values[i];
            updateFilterStyles();
            refresh();
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void sortParsedResults()
    {
        std::stable_sort(searchResults.begin(), searchResults.end(),
                         [this](const ParsedRow& a, const ParsedRow& b) {
            switch (currentSort)
            {
                case SortMode::SEEDERS:
                    if (a.item.seeders != b.item.seeders) return a.item.seeders > b.item.seeders;
                    return a.item.size > b.item.size;
                case SortMode::SIZE:
                    if (a.item.size != b.item.size) return a.item.size > b.item.size;
                    return a.item.seeders > b.item.seeders;
                case SortMode::DATE:
                    if (a.item.publishDate != b.item.publishDate)
                        return a.item.publishDate > b.item.publishDate;
                    return a.item.seeders > b.item.seeders;
                case SortMode::RECOMMENDED:
                default:
                    if (a.score != b.score) return a.score > b.score;
                    return a.item.seeders > b.item.seeders;
            }
        });
    }

    void cycleSort()
    {
        if (currentSort == SortMode::RECOMMENDED)
            currentSort = SortMode::SEEDERS;
        else if (currentSort == SortMode::SEEDERS)
            currentSort = SortMode::SIZE;
        else if (currentSort == SortMode::SIZE)
            currentSort = SortMode::DATE;
        else
            currentSort = SortMode::RECOMMENDED;

        updateSortButtonLabel();
        if (!searchResults.empty())
        {
            sortParsedResults();
            refresh();
        }
    }

  public:
    // Starts on a query of someone else's choosing (the film card's
    // "Torrents"): fills the search bar and runs it, so the results are the
    // first thing on screen and the bar is still there to refine the query.
    void startWith(const std::string& query, jackett::Category cat)
    {
        if (query.empty()) return;
        searchQuery     = query;
        currentCategory = cat;
        barText->setText(query);
        barText->setTextColor(theme::text());
        updateCategoryButtonStyles();
        runSearch();
    }

  private:
    void promptSearch()
    {
        brls::Application::getImeManager()->openForText(
            [this, live = alive](std::string text) {
                if (!*live || text.empty()) return;
                searchQuery = text;
                barText->setText(text);
                barText->setTextColor(theme::text());
                runSearch();
            },
            tr("Search Jackett"), "", 128, searchQuery);
    }

    void promptSetup()
    {
        brls::Application::getImeManager()->openForText(
            [this, live = alive](std::string url) {
                if (!*live || url.empty()) return;
                config::get().jackettUrl = url;
                config::save();

                brls::Application::getImeManager()->openForText(
                    [this, live](std::string key) {
                        if (!*live) return;
                        config::get().jackettApiKey = key;
                        config::save();
                        refresh();
                    },
                    tr("Jackett API Key (optional)"), tr("Leave empty if disabled in Jackett"), 128,
                    config::get().jackettApiKey);
            },
            tr("Jackett Server URL"), tr("e.g. http://192.168.1.100:9117"), 256,
            config::get().jackettUrl);
    }

    void buildResultsFromItems(std::vector<jackett::Item>&& items)
    {
        want.series = (currentCategory == jackett::Category::SERIES ||
                       currentCategory == jackett::Category::ANIME);
        searchResults.clear();
        std::map<std::string, size_t> seen;

        for (const auto& it : items)
        {
            std::string key = !it.infoHash.empty()
                                  ? ("ih:" + it.infoHash)
                                  : (it.title + "|" + std::to_string(it.size / (1024 * 1024)));
            ParsedRow row;
            row.item     = it;
            row.trackers = it.trackers;
            if (row.trackers.empty() && !it.tracker.empty())
                row.trackers = jackett::splitTrackers(it.tracker);
            row.trackerLabel = row.trackers.empty() ? it.tracker
                                                    : jackett::compactTrackers(row.trackers);
            row.info         = release::parse(it.title);
            if (it.hasJacredInfo || !it.languages.empty())
            {
                release::ExternalMeta meta;
                meta.quality        = it.infoQuality;
                meta.videotype      = it.infoVideotype;
                meta.voices         = it.infoVoices;
                meta.seasons        = it.infoSeasons;
                meta.languages      = it.languages;
                meta.trackers       = row.trackers;
                meta.seedersUnknown = it.seedersUnknown;
                release::enrich(row.info, meta);
            }
            row.score = release::score(row.info, it.seeders, it.size, want);

            auto found = seen.find(key);
            if (found != seen.end())
            {
                ParsedRow& ex = searchResults[found->second];
                if (row.item.seeders > ex.item.seeders)
                {
                    ex.item.seeders        = row.item.seeders;
                    ex.info.seedersUnknown = row.info.seedersUnknown;
                }
                if (ex.item.size <= 0 && row.item.size > 0) ex.item.size = row.item.size;
                if (ex.item.publishDate.empty() && !row.item.publishDate.empty())
                    ex.item.publishDate = row.item.publishDate;
                for (const auto& trk : row.trackers)
                {
                    std::string tl = release::foldAscii(trk);
                    for (char& c : tl) c = (char)std::tolower((unsigned char)c);
                    bool dup = false;
                    for (const auto& et : ex.trackers)
                    {
                        std::string el = release::foldAscii(et);
                        for (char& c : el) c = (char)std::tolower((unsigned char)c);
                        if (el == tl) { dup = true; break; }
                    }
                    if (!dup) ex.trackers.push_back(trk);
                }
                if (!ex.trackers.empty())
                    ex.trackerLabel = jackett::compactTrackers(ex.trackers);
                release::ExternalMeta m;
                m.quality        = row.info.height;
                m.videotype      = row.info.dolbyVision ? "dv" : (row.info.hdr ? "hdr" : "");
                m.voices         = row.info.voices;
                m.seasons        = row.info.seasons;
                m.trackers       = ex.trackers;
                m.seedersUnknown = ex.info.seedersUnknown && row.info.seedersUnknown;
                if (row.info.russian) m.languages.push_back("rus");
                if (row.info.ukrainian) m.languages.push_back("ukr");
                release::enrich(ex.info, m);
                ex.score = release::score(ex.info, ex.item.seeders, ex.item.size, want);
                continue;
            }
            seen[key] = searchResults.size();
            searchResults.push_back(std::move(row));
        }
        sortParsedResults();
    }

    void runSearch()
    {
        if (searchQuery.empty()) return;

        isSearching = true;
        searchError.clear();
        searchResults.clear();
        filterVoice.clear();
        filterTracker.clear();
        updateFilterStyles();
        refresh();

        jackett::searchAsync(
            searchQuery, currentCategory,
            [this, live = alive, query = searchQuery, cat = currentCategory](jackett::SearchResult r) {
                if (!*live) return;
                if (searchQuery != query || currentCategory != cat) return;

                isSearching = false;
                if (!r.ok)
                {
                    searchError = r.error;
                }
                else
                {
                    buildResultsFromItems(std::move(r.items));
                }
                refresh();
            });
    }

    void refresh()
    {
        // Retry / Configure live inside contentContainer and call back into
        // here: clearViews() would free the very button holding the focus,
        // and the next input would dereference it. Park it on the search bar.
        brls::View* cur = brls::Application::getCurrentFocus();
        for (brls::View* v = cur; v; v = v->getParent())
        {
            if (v == contentContainer)
            {
                brls::Application::giveFocus(searchBar);
                break;
            }
        }
        contentContainer->clearViews();

        const auto& cfg = config::get();
        if (cfg.jackettUrl.empty())
        {
            renderUnconfigured();
            return;
        }

        if (isSearching)
        {
            renderLoading();
            return;
        }

        if (!searchError.empty())
        {
            renderError();
            return;
        }

        if (searchQuery.empty())
        {
            renderIdle();
            return;
        }

        if (searchResults.empty())
        {
            renderEmpty();
            return;
        }

        renderResults();
    }

    void renderUnconfigured()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setGrow(1.0f);
        float pad = platform_ui::isMobile() ? 28.0f : 80.0f;
        box->setPadding(0, pad, 0, pad);

        auto* title = new brls::Label();
        title->setText(tr("Jackett is not configured"));
        title->setFontSize(26.0f);
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        title->setMargins(0, 0, 14, 0);
        box->addView(title);

        auto* hint = new brls::Label();
        hint->setText(tr("Set your Jackett Server URL in Options, or click below to configure now."));
        hint->setFontSize(20.0f);
        hint->setTextColor(theme::textMuted());
        hint->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        hint->setMargins(0, 0, 24, 0);
        box->addView(hint);

        auto* btn = new brls::Button();
        btn->setText(tr("Configure Jackett"));
        btn->registerClickAction([this](brls::View*) {
            promptSetup();
            return true;
        });
        box->addView(btn);

        contentContainer->addView(box);
    }

    void renderIdle()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setGrow(1.0f);
        float pad = platform_ui::isMobile() ? 28.0f : 80.0f;
        box->setPadding(0, pad, 0, pad);

        auto* icon = new brls::Label();
        icon->setText("");
        icon->setFontSize(48.0f);
        icon->setTextColor(theme::textDim());
        icon->setMargins(0, 0, 16, 0);
        box->addView(icon);

        auto* hint = new brls::Label();
        hint->setText(tr("Enter a search query to search torrents via Jackett"));
        hint->setFontSize(22.0f);
        hint->setTextColor(theme::textMuted());
        hint->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        box->addView(hint);

        contentContainer->addView(box);
    }

    void renderLoading()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setGrow(1.0f);

        auto* spinner = new brls::ProgressSpinner();
        box->addView(spinner);

        auto* lbl = new brls::Label();
        lbl->setText(tr("Searching Jackett..."));
        lbl->setFontSize(22.0f);
        lbl->setTextColor(theme::textMuted());
        lbl->setMarginTop(18.0f);
        box->addView(lbl);

        contentContainer->addView(box);
    }

    void renderError()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setGrow(1.0f);
        float pad = platform_ui::isMobile() ? 28.0f : 80.0f;
        box->setPadding(0, pad, 0, pad);

        auto* title = new brls::Label();
        title->setText(tr("Could not search Jackett"));
        title->setFontSize(24.0f);
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        title->setMargins(0, 0, 12, 0);
        box->addView(title);

        auto* desc = new brls::Label();
        desc->setText(searchError);
        desc->setFontSize(18.0f);
        desc->setTextColor(theme::textWarn());
        desc->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        desc->setMargins(0, 0, 20, 0);
        box->addView(desc);

        auto* retryBtn = new brls::Button();
        retryBtn->setText(tr("Retry"));
        retryBtn->registerClickAction([this](brls::View*) {
            runSearch();
            return true;
        });
        box->addView(retryBtn);

        contentContainer->addView(box);
    }

    void renderEmpty()
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setGrow(1.0f);
        float pad = platform_ui::isMobile() ? 28.0f : 80.0f;
        box->setPadding(0, pad, 0, pad);

        auto* title = new brls::Label();
        title->setText(tr("No results found"));
        title->setFontSize(24.0f);
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        title->setMargins(0, 0, 10, 0);
        box->addView(title);

        auto* desc = new brls::Label();
        desc->setText(tr("No torrents matched \"") + searchQuery + "\"");
        desc->setFontSize(18.0f);
        desc->setTextColor(theme::textMuted());
        desc->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        box->addView(desc);

        contentContainer->addView(box);
    }

    bool rowPasses(const ParsedRow& r) const
    {
        if (filterHeight && r.info.height != filterHeight) return false;
        if (filterAliveOnly && r.item.seeders == 0 && !r.info.seedersUnknown) return false;
        if (!filterVoice.empty() &&
            std::find(r.info.voices.begin(), r.info.voices.end(), filterVoice) ==
                r.info.voices.end())
            return false;
        if (!filterTracker.empty())
        {
            bool match = !r.trackers.empty()
                             ? std::find(r.trackers.begin(), r.trackers.end(), filterTracker) !=
                                   r.trackers.end()
                             : r.trackerLabel == filterTracker;
            if (!match) return false;
        }
        return true;
    }

    void renderResults()
    {
        std::vector<const ParsedRow*> rows;
        for (const auto& r : searchResults)
            if (rowPasses(r)) rows.push_back(&r);

        if (rows.empty())
        {
            auto* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setJustifyContent(brls::JustifyContent::CENTER);
            box->setAlignItems(brls::AlignItems::CENTER);
            box->setGrow(1.0f);
            auto* lbl = new brls::Label();
            lbl->setText(tr("Nothing matches this filter."));
            lbl->setFontSize(22.0f);
            lbl->setTextColor(theme::textMuted());
            box->addView(lbl);
            contentContainer->addView(box);
            return;
        }

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        auto* list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        float ins = platform_ui::sideInset();
        list->setPadding(10.0f, ins, 40.0f, ins);
        scroll->setContentView(list);

        for (const ParsedRow* r : rows)
        {
            auto* row = buildItemRow(*r);
            list->addView(row);
        }

        contentContainer->addView(scroll);
    }

    brls::Box* buildItemRow(const ParsedRow& r)
    {
        const jackett::Item& item = r.item;

        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setFocusable(true);
        row->setCornerRadius(10.0f);
        row->setHighlightCornerRadius(10.0f);
        row->setPadding(12.0f, 18.0f, 12.0f, 18.0f);
        row->setMarginBottom(10.0f);
        row->setBackgroundColor(theme::scrim(14));
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        // Left: Quality
        auto* q = new brls::Label();
        q->setText(r.info.height > 0 ? std::to_string(r.info.height) + "p" : "\xE2\x80\x94");
        q->setFontSize(24.0f);
        q->setWidth(96.0f);
        q->setSingleLine(true);
        row->addView(q);

        // Middle: Title + release tags + tracker
        auto* mid = new brls::Box();
        mid->setAxis(brls::Axis::COLUMN);
        mid->setGrow(1.0f);
        mid->setShrink(1.0f);
        mid->setMarginRight(16.0f);

        auto* titleLbl = new brls::Label();
        titleLbl->setText(item.title);
        titleLbl->setFontSize(19.0f);
        titleLbl->setSingleLine(true);
        mid->addView(titleLbl);

        auto* tags = new brls::Box();
        tags->setAxis(brls::Axis::ROW);
        tags->setMarginTop(8.0f);
        auto tag = [tags](const std::string& text, NVGcolor color) {
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
#ifdef __SWITCH__
        const bool swCodec = release::isSoftwareDecodedOnSwitch(r.info);
        const std::string codecTag =
            r.info.codec.empty() ? "" : r.info.codec + (r.info.tenBit ? " 10-bit" : "");
#endif
        if (r.info.cam) tag("CAMRip", theme::textWarn());
        for (const auto& x : release::tags(r.info))
        {
            if (r.info.cam && x == "CAMRip") continue;
            bool warn = false;
#ifdef __SWITCH__
            if ((swCodec && x == codecTag) || x == "Dolby Vision") warn = true;
#endif
            tag(x, warn ? theme::textWarn() : theme::textDim());
        }
        if (r.info.fullSeason && r.info.season > 0)
            tag(r.info.seasonTo > r.info.season ? tr("several seasons") : tr("whole season"),
                theme::textDim());
#ifdef __SWITCH__
        if (swCodec)
            tag(tr("no HW decode"), theme::textWarn());
        const bool isSeries = want.series || r.info.hasAnySeason() || r.info.fullSeason;
        if (release::isHeavyForWifi(r.info, item.size, isSeries))
            tag(tr("heavy for Wi-Fi"), theme::textWarn());
#endif
        if (!r.trackerLabel.empty()) tag(r.trackerLabel, theme::textMuted());
        if (!item.categoryDesc.empty() && r.info.source.empty())
            tag(item.categoryDesc, theme::textDim());
        mid->addView(tags);
        row->addView(mid);

        // Right: Size, Seeders, Date
        auto* right = new brls::Box();
        right->setAxis(brls::Axis::COLUMN);
        right->setAlignItems(brls::AlignItems::FLEX_END);
        right->setWidth(140.0f);
        if (item.size > 0)
        {
            auto* sz = new brls::Label();
            sz->setText(jackett::formatSize(item.size));
            sz->setFontSize(17.0f);
            sz->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            right->addView(sz);
        }
        auto* sp = new brls::Label();
        bool unk = (item.seeders == 0 && r.info.seedersUnknown);
        sp->setText(unk ? std::string(tr("seeds ?"))
                        : tr("seeds ") + std::to_string(item.seeders));
        sp->setFontSize(15.0f);
        sp->setTextColor((item.seeders > 0 || unk) ? theme::textMuted() : theme::textWarn());
        sp->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        sp->setMarginTop(4.0f);
        right->addView(sp);
        if (item.publishDate.size() >= 10)
        {
            auto* dt = new brls::Label();
            dt->setText(item.publishDate.substr(0, 10));
            dt->setFontSize(13.0f);
            dt->setTextColor(theme::textDim());
            dt->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
            dt->setMarginTop(3.0f);
            right->addView(dt);
        }
        row->addView(right);

        // Action A: Play
        jackett::Item itemCopy = item;
        row->registerClickAction([this, itemCopy](brls::View*) {
            PlayHint hint;
            hint.title = itemCopy.title;
            playJackettItem(itemCopy, hint);
            return true;
        });

        // Action Y: Save to Local
        row->registerAction(
            tr("Save to Local"), brls::BUTTON_Y,
            [this, itemCopy](brls::View*) {
                saveItem(itemCopy);
                return true;
            },
            false, false, brls::SOUND_CLICK);

        return row;
    }

    void saveItem(const jackett::Item& item)
    {
        jackett::saveToLocalAsync(item, [](bool ok, const std::string& msg) {
            auto* d = new brls::Dialog(ok ? tr("Saved to Local library") : msg);
            d->addButton("OK", []() {});
            d->open();
        });
    }
};

} // namespace

brls::View* buildJackettTab()
{
    auto* root       = new JackettTabRoot();
    root->controller = std::make_shared<JackettView>(root);
    return root;
}

void openJackettSearch(const std::string& query, int category)
{
    auto* root       = new JackettTabRoot();
    auto view        = std::make_shared<JackettView>(root);
    root->controller = view;

    auto* frame = new ui::FieldFrame(root);
    frame->setTitle("Jackett");
    brls::Application::pushActivity(new brls::Activity(frame));
    // After the push: a search left unconfigured shows its setup prompt
    // instead, which is the right answer there too.
    if (!config::get().jackettUrl.empty())
        view->startWith(query, (jackett::Category)category);
}
