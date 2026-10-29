#include "jackett_tab.hpp"

#include <algorithm>
#include <cstring>
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
#include "player.hpp"
#include "torrent_play.hpp"
#include "theme.hpp"
#include "widgets.hpp"  // ui::FieldFrame

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
        setupLayout();
        refresh();
    }

  private:
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

    brls::Box* contentContainer = nullptr;

    std::string searchQuery;
    jackett::Category currentCategory = jackett::Category::ALL;
    jackett::SortBy currentSort       = jackett::SortBy::SEEDERS;
    std::vector<jackett::Item> searchResults;
    bool isSearching = false;
    std::string searchError;

    void setupLayout()
    {
        // Top controls: search bar + categories + sort
        controlsBox = new brls::Box();
        controlsBox->setAxis(brls::Axis::COLUMN);
        controlsBox->setPadding(20.0f, 60.0f, 10.0f, 60.0f);

        // 1. Search Bar
        searchBar = new brls::Box();
        searchBar->setAxis(brls::Axis::ROW);
        searchBar->setAlignItems(brls::AlignItems::CENTER);
        searchBar->setHeight(64.0f);
        searchBar->setPaddingLeft(20.0f);
        searchBar->setPaddingRight(24.0f);
        searchBar->setCornerRadius(6.0f);
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
        barText->setFontSize(22.0f);
        barText->setTextColor(theme::textMuted());
        barText->setSingleLine(true);
        searchBar->addView(barText);

        controlsBox->addView(searchBar);

        // 2. Categories + Sort bar
        categoryRow = new brls::Box();
        categoryRow->setAxis(brls::Axis::ROW);
        categoryRow->setAlignItems(brls::AlignItems::CENTER);
        categoryRow->setMarginTop(12.0f);

        catAllBtn    = makeCategoryButton(tr("All"), jackett::Category::ALL);
        catMovieBtn  = makeCategoryButton(tr("Movies"), jackett::Category::MOVIES);
        catSeriesBtn = makeCategoryButton(tr("Series"), jackett::Category::SERIES);
        catAnimeBtn  = makeCategoryButton(tr("Anime"), jackett::Category::ANIME);

        categoryRow->addView(catAllBtn);
        categoryRow->addView(catMovieBtn);
        categoryRow->addView(catSeriesBtn);
        categoryRow->addView(catAnimeBtn);

        auto* spacer = new brls::Box();
        spacer->setGrow(1.0f);
        categoryRow->addView(spacer);

        sortBtn = new brls::Button();
        sortBtn->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        updateSortButtonLabel();
        sortBtn->registerClickAction([this](brls::View*) {
            cycleSort();
            return true;
        });
        categoryRow->addView(sortBtn);

        controlsBox->addView(categoryRow);
        root->addView(controlsBox);

        // 3. Content container
        contentContainer = new brls::Box();
        contentContainer->setAxis(brls::Axis::COLUMN);
        contentContainer->setGrow(1.0f);
        root->addView(contentContainer);

        updateCategoryButtonStyles();
    }

    brls::Button* makeCategoryButton(const std::string& label, jackett::Category cat)
    {
        auto* btn = new brls::Button();
        btn->setText(label);
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
            case jackett::SortBy::SEEDERS:
                sortBtn->setText(tr("Sort: Seeders"));
                break;
            case jackett::SortBy::SIZE:
                sortBtn->setText(tr("Sort: Size"));
                break;
            case jackett::SortBy::DATE:
                sortBtn->setText(tr("Sort: Date"));
                break;
        }
    }

    void cycleSort()
    {
        if (currentSort == jackett::SortBy::SEEDERS)
            currentSort = jackett::SortBy::SIZE;
        else if (currentSort == jackett::SortBy::SIZE)
            currentSort = jackett::SortBy::DATE;
        else
            currentSort = jackett::SortBy::SEEDERS;

        updateSortButtonLabel();
        if (!searchResults.empty())
        {
            jackett::sortItems(searchResults, currentSort);
            renderResults();
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

    void runSearch()
    {
        if (searchQuery.empty()) return;

        isSearching = true;
        searchError.clear();
        searchResults.clear();
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
                    searchResults = std::move(r.items);
                    jackett::sortItems(searchResults, currentSort);
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
        box->setPadding(0, 80, 0, 80);

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
        box->setPadding(0, 80, 0, 80);

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
        box->setPadding(0, 80, 0, 80);

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
        box->setPadding(0, 80, 0, 80);

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

    void renderResults()
    {
        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

        auto* list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(10.0f, 60.0f, 40.0f, 60.0f);
        scroll->setContentView(list);

        for (const auto& item : searchResults)
        {
            auto* row = buildItemRow(item);
            list->addView(row);
        }

        contentContainer->addView(scroll);
    }

    brls::Box* buildItemRow(const jackett::Item& item)
    {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::COLUMN);
        row->setFocusable(true);
        row->setCornerRadius(6.0f);
        row->setPadding(14.0f, 20.0f, 14.0f, 20.0f);
        row->setMarginBottom(10.0f);
        row->setBackgroundColor(theme::scrim(15));
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

        // Line 1: Title
        auto* titleLbl = new brls::Label();
        titleLbl->setText(item.title);
        titleLbl->setFontSize(20.0f);
        titleLbl->setSingleLine(true);
        row->addView(titleLbl);

        // Line 2: Meta (Tracker, Size, Seeds/Peers, Category, Date)
        auto* meta = new brls::Box();
        meta->setAxis(brls::Axis::ROW);
        meta->setAlignItems(brls::AlignItems::CENTER);
        meta->setMarginTop(8.0f);

        // Tracker badge
        if (!item.tracker.empty())
        {
            auto* trk = new brls::Label();
            trk->setText("[" + item.tracker + "]");
            trk->setFontSize(16.0f);
            trk->setTextColor(theme::accentText());
            trk->setMarginRight(14.0f);
            meta->addView(trk);
        }

        // Size
        if (item.size > 0)
        {
            auto* sz = new brls::Label();
            sz->setText(jackett::formatSize(item.size));
            sz->setFontSize(16.0f);
            sz->setTextColor(theme::text());
            sz->setMarginRight(16.0f);
            meta->addView(sz);
        }

        // Seeds & Peers
        auto* sp = new brls::Label();
        std::string spText = "S: " + std::to_string(item.seeders) +
                             "  P: " + std::to_string(item.peers);
        sp->setText(spText);
        sp->setFontSize(16.0f);
        sp->setTextColor(item.seeders > 0 ? theme::textBody() : theme::textDim());
        sp->setMarginRight(16.0f);
        meta->addView(sp);

        // Category
        if (!item.categoryDesc.empty())
        {
            auto* cat = new brls::Label();
            cat->setText(item.categoryDesc);
            cat->setFontSize(15.0f);
            cat->setTextColor(theme::textDim());
            cat->setMarginRight(16.0f);
            meta->addView(cat);
        }

        // Date
        if (!item.publishDate.empty())
        {
            std::string dateStr = item.publishDate;
            if (dateStr.size() >= 10) dateStr = dateStr.substr(0, 10);
            auto* dt = new brls::Label();
            dt->setText(dateStr);
            dt->setFontSize(15.0f);
            dt->setTextColor(theme::textMuted());
            meta->addView(dt);
        }

        row->addView(meta);

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
