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
#include "theme.hpp"

extern "C" {
#include "torrent.h"
}

namespace
{

struct VideoFileInfo
{
    int index;
    std::string name;
    int64_t length;
};

bool isVideoExtension(const std::string& path)
{
    static const char* kExt[] = { ".mkv", ".mp4", ".avi", ".m4v",  ".mov",
                                  ".webm", ".ts", ".m2ts", ".mpg", ".mpeg",
                                  ".wmv", ".flv", ".ogv" };
    for (const char* e : kExt)
    {
        size_t n = std::strlen(e);
        if (path.size() > n &&
            strcasecmp(path.c_str() + path.size() - n, e) == 0)
            return true;
    }
    return false;
}

// The video files of a loaded torrent, with picker labels (unsorted).
std::vector<VideoFileInfo> videosOf(const torrent_meta* m)
{
    std::vector<int> idx;
    std::vector<std::string> paths;
    for (int i = 0; i < m->file_count; i++)
    {
        if (!isVideoExtension(m->files[i].path)) continue;
        idx.push_back(i);
        paths.push_back(m->files[i].path);
    }
    std::vector<std::string> names = pickerNames(paths);
    std::vector<VideoFileInfo> out;
    for (size_t k = 0; k < idx.size(); k++)
        out.push_back({ idx[k], names[k], m->files[idx[k]].length });
    return out;
}

std::vector<VideoFileInfo> parseTorrentVideos(const std::string& path)
{
    std::vector<VideoFileInfo> out;
    torrent_meta* m = (torrent_meta*)std::calloc(1, sizeof(torrent_meta));
    if (!m) return out;
    char err[128] = { 0 };
    if (torrent_load(m, path.c_str(), err, sizeof(err)) == 0)
    {
        out = videosOf(m);
        torrent_unload(m);
    }
    std::free(m);
    std::sort(out.begin(), out.end(), [](const VideoFileInfo& a, const VideoFileInfo& b) {
        return naturalCompare(a.name, b.name);
    });
    return out;
}

class LoadingBox : public brls::Box
{
  public:
    std::shared_ptr<bool> cancelled;
    LoadingBox(std::shared_ptr<bool> c) : cancelled(std::move(c)) {}
    ~LoadingBox() override
    {
        if (cancelled) *cancelled = true;
    }
};

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
            trk->setTextColor(theme::accent());
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
            playItem(itemCopy);
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

    void playWithVideos(const std::string& source, const std::string& title,
                        const std::vector<VideoFileInfo>& vids)
    {
        if (vids.empty())
        {
            auto* d = new brls::Dialog(tr("This torrent has no video file to stream."));
            d->addButton("OK", []() {});
            d->open();
            return;
        }

        if (vids.size() == 1)
        {
            startPlaybackPrompt(source, {}, vids[0].name, vids[0].index, {}, "Jackett");
            return;
        }

        std::vector<Row> rows;
        for (const auto& v : vids)
        {
            Row r;
            r.label  = v.name;
            r.detail = jackett::formatSize(v.length);
            rows.push_back(r);
        }
        brls::Application::pushActivity(new ListActivity(
            title, tr("Pick a file"), rows, [source, vids](int i) {
                startPlaybackPrompt(source, {}, vids[i].name, vids[i].index, {}, "Jackett");
            }));
    }

    void resolveAndPlayMagnet(const std::string& magnet, const std::string& title,
                              brls::Dialog* loading, LoadingBox* box,
                              std::shared_ptr<bool> cancelled)
    {
        brls::async([magnet, title, loading, box, cancelled, this, live = alive]() {
            torrent_meta* m = (torrent_meta*)std::calloc(1, sizeof(torrent_meta));
            char err[128] = { 0 };
            int rc = -1;
            std::vector<VideoFileInfo> vids;
            std::string resolvedName;
            if (m)
            {
                rc = torrent_load_magnet(m, magnet.c_str(), err, sizeof(err));
                if (rc == 0)
                {
                    resolvedName = m->name;
                    vids         = videosOf(m);
                    std::sort(vids.begin(), vids.end(), [](const VideoFileInfo& a, const VideoFileInfo& b) {
                        return naturalCompare(a.name, b.name);
                    });
                    torrent_unload(m);
                }
                std::free(m);
            }
            std::string e = err;

            brls::sync([this, live, magnet, title, resolvedName, vids, rc, e, loading, box, cancelled]() {
                if (*cancelled || !*live) return;
                box->cancelled = nullptr;
                loading->close([this, magnet, title, resolvedName, vids, rc, e]() {
                    if (rc != 0)
                    {
                        auto* d = new brls::Dialog(tr("Could not resolve torrent metadata: ") + e);
                        d->addButton("OK", []() {});
                        d->open();
                        return;
                    }
                    std::string playTitle = !resolvedName.empty() ? resolvedName : title;
                    playWithVideos(magnet, playTitle, vids);
                });
            });
        });
    }

    void playItem(const jackett::Item& item)
    {
        std::string link = item.link;
        std::string magnet = (!link.empty() && link.rfind("magnet:", 0) == 0)
                                 ? link
                                 : item.magnetUri;
        bool isHttp = !link.empty() && link.rfind("magnet:", 0) != 0;

        if (!isHttp && magnet.empty())
        {
            auto* d = new brls::Dialog(tr("No playable link or magnet URI found for this torrent."));
            d->addButton("OK", []() {});
            d->open();
            return;
        }

        auto cancelled = std::make_shared<bool>(false);
        auto* box = new LoadingBox(cancelled);
        box->setAxis(brls::Axis::COLUMN);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setPadding(24.0f);

        auto* spinner = new brls::ProgressSpinner();
        box->addView(spinner);

        auto* lbl = new brls::Label();
        lbl->setText(tr("Preparing torrent..."));
        lbl->setFontSize(20.0f);
        lbl->setMarginTop(20.0f);
        box->addView(lbl);

        auto* loading = new brls::Dialog(box);
        loading->addButton(tr("Cancel"), [cancelled]() { *cancelled = true; });
        loading->setCancelable(true);
        loading->open();

        if (isHttp)
        {
            // One file per link: history entries resume from this path later,
            // so it must not be overwritten by the next result played.
            std::string tempPath = jackett::cachedTorrentPath(link);
            jackett::downloadTorrentAsync(link, tempPath, [this, live = alive, item, tempPath, magnet, loading, box, cancelled](
                                                              bool ok, const std::string& err, const std::string& redirected) {
                if (*cancelled || !*live) return;
                // The indexer pointed at a magnet instead of a file (common for
                // public trackers): resolve that, it is the same torrent.
                if (ok && !redirected.empty())
                {
                    resolveAndPlayMagnet(redirected, item.title, loading, box, cancelled);
                    return;
                }
                if (ok)
                {
                    auto vids = parseTorrentVideos(tempPath);
                    if (!vids.empty())
                    {
                        box->cancelled = nullptr;
                        loading->close([this, item, tempPath, vids]() {
                            playWithVideos(tempPath, item.title, vids);
                        });
                        return;
                    }
                }
                // If .torrent download failed or had no video, fall back to magnet if available
                if (!magnet.empty())
                {
                    resolveAndPlayMagnet(magnet, item.title, loading, box, cancelled);
                }
                else
                {
                    box->cancelled = nullptr;
                    loading->close([err, ok]() {
                        auto* d = new brls::Dialog(ok ? tr("This torrent has no video file to stream.")
                                                      : tr("Could not download torrent: ") + err);
                        d->addButton("OK", []() {});
                        d->open();
                    });
                }
            });
        }
        else
        {
            resolveAndPlayMagnet(magnet, item.title, loading, box, cancelled);
        }
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

    auto* frame = new brls::AppletFrame(root);
    frame->setTitle("Jackett");
    brls::Application::pushActivity(new brls::Activity(frame));
    // After the push: a search left unconfigured shows its setup prompt
    // instead, which is the right answer there too.
    if (!config::get().jackettUrl.empty())
        view->startWith(query, (jackett::Category)category);
}
