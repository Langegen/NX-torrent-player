#include "history_tab.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "card.hpp"
#include "history.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "player.hpp"
#include "theme.hpp"
#include "widgets.hpp"

// The History tab: what was watched, as the things themselves rather than the
// files. Films and series (played from their card) come first as posters --
// A opens the card, where "Continue from 42:10" and each episode's progress
// are. Below them, everything played without a card (a magnet typed in, a
// local file, a Jackett-tab result, Stremio) as before: the file, its
// progress, A to play it again.

namespace
{

constexpr float kInset   = 60.0f;
constexpr float kPosterW = 190.0f;
constexpr float kWideW   = 300.0f;
constexpr float kGlow    = 24.0f;

// True if `v` is `ancestor` or sits under it.
bool isUnder(brls::View* v, brls::View* ancestor)
{
    for (; v; v = v->getParent())
        if (v == ancestor) return true;
    return false;
}

std::string fileKey(const std::string& source, int fileIndex)
{
    return source + "#" + std::to_string(fileIndex);
}

// The badge on a title's poster: where it stands.
std::string badgeFor(const library::Entry& e)
{
    library::Next n = library::nextOf(e);
    if (e.item.isSeries())
    {
        int s = n.kind != library::Next::Kind::None ? n.season : e.lastSeason;
        int ep = n.kind != library::Next::Kind::None ? n.episode : e.lastEpisode;
        if (s > 0 && ep > 0) return "S" + std::to_string(s) + "E" + std::to_string(ep);
        return "";
    }
    if (n.kind == library::Next::Kind::Resume) return fmtTime(e.pos);
    if (e.done) return tr("Watched");
    return "";
}

class HistoryTabRoot : public brls::Box
{
  public:
    HistoryTabRoot()
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setGrow(1.0f);
        seenHist = history::generation();
        seenLib  = library::generation();
        rebuild();
    }

    ~HistoryTabRoot() override { *alive = false; }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        // The lists change under us when the player records progress or a
        // dialog removes an entry. Rebuild only once we are the top activity
        // again: while a dialog (or the player, or a card) sits above us,
        // borealis' focus stack holds a raw pointer to the view that opened
        // it, and freeing that view would crash when it closes and hands the
        // focus back.
        bool stale = history::generation() != seenHist || library::generation() != seenLib;
        if (stale && !rebuildQueued && isOnTop())
        {
            rebuildQueued = true;
            auto live     = alive;
            brls::sync([this, live]() {
                if (!*live) return;  // the tab was switched away meanwhile
                rebuildQueued = false;
                if (!isOnTop()) return;  // draw() retries next frame
                seenHist = history::generation();
                seenLib  = library::generation();
                rebuild();
            });
        }
    }

  private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    // The two card rows, for putting the focus back after a rebuild.
    brls::Box* titleRow = nullptr;
    brls::Box* fileRow  = nullptr;
    bool rebuildQueued  = false;
    uint32_t seenHist = 0, seenLib = 0;

    bool isOnTop()
    {
        auto stack = brls::Application::getActivitiesStack();
        return !stack.empty() && stack.back() == this->getParentActivity();
    }

    void rebuild()
    {
        // clearViews() frees the cards, and the focused one with them: park
        // the focus on ourselves first (hidden highlight), then hand it to
        // the card at the same place in the new rows.
        int focusRow = -1, focusIdx = 0;
        brls::View* cur = brls::Application::getCurrentFocus();
        if (cur && isUnder(cur, this))
        {
            brls::Box* rows[2] = { titleRow, fileRow };
            for (int r = 0; r < 2; r++)
                if (rows[r])
                {
                    const auto& kids = rows[r]->getChildren();
                    for (size_t i = 0; i < kids.size(); i++)
                        if (isUnder(cur, kids[i]))
                        {
                            focusRow = r;
                            focusIdx = (int)i;
                        }
                }
            if (focusRow < 0) focusRow = 0;
            this->setFocusable(true);
            this->setHideHighlight(true);
            brls::Application::giveFocus(this);
        }

        *alive = false;
        alive  = std::make_shared<bool>(true);
        this->clearViews();
        titleRow = fileRow = nullptr;

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        auto* list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(4.0f, 0.0f, 36.0f, 0.0f);
        scroll->setContentView(list);

        // ---- films and series ------------------------------------------
        std::vector<library::Entry> titles = library::watched();
        // The files those titles played from: not shown again below.
        std::set<std::string> titleFiles;
        for (const auto& e : titles)
        {
            if (!e.source.empty()) titleFiles.insert(fileKey(e.source, e.fileIndex));
            for (const auto& kv : e.episodes)
                if (!kv.second.source.empty())
                    titleFiles.insert(fileKey(kv.second.source, kv.second.fileIndex));
        }
        if (!titles.empty())
        {
            list->addView(ui::heading(tr("Films and series"), kInset));
            titleRow = new brls::Box();
            for (const auto& e : titles)
            {
                content::Item it = e.item;
                std::string key  = e.key();
                auto* card = ui::posterCard(it.title, it.key(), it.posterUrl, alive,
                                            [it]() { openContentCard(it); }, kPosterW,
                                            badgeFor(e));
                std::string name = it.title;
                card->registerAction(
                    tr("Remove"), brls::BUTTON_Y,
                    [key, name](brls::View*) {
                        auto* d = new brls::Dialog(
                            std::string(tr("Remove this item from watch history?")) + "\n\n" +
                            name);
                        d->addButton(tr("Cancel"), []() {});
                        d->addButton(tr("Remove"), [key]() { library::forgetWatched(key); });
                        d->open();
                        return true;
                    },
                    false, false, brls::SOUND_CLICK);
                if (titleRow->getChildren().empty()) card->setMarginLeft(kInset);
                titleRow->addView(card);
            }
            list->addView(ui::strip(titleRow, kPosterW * 1.5f + kGlow));
        }

        // ---- other files ------------------------------------------------
        std::vector<history::Item> files;
        for (const auto& it : history::all())
            if (it.tmdbId <= 0 && !titleFiles.count(fileKey(it.source, it.fileIndex)))
                files.push_back(it);
        if (!files.empty())
        {
            list->addView(ui::heading(tr("Other files"), kInset));
            fileRow = new brls::Box();
            for (const auto& it : files) fileRow->addView(fileCard(it));
            fileRow->getChildren()[0]->setMarginLeft(kInset);
            list->addView(ui::strip(fileRow, kWideW * 9.0f / 16.0f + kGlow));
        }

        if (titles.empty() && files.empty())
        {
            auto* emptyBox = new brls::Box();
            emptyBox->setAxis(brls::Axis::COLUMN);
            emptyBox->setJustifyContent(brls::JustifyContent::CENTER);
            emptyBox->setAlignItems(brls::AlignItems::CENTER);
            emptyBox->setGrow(1.0f);
            auto* label = new brls::Label();
            label->setText(tr("No watch history yet"));
            label->setFontSize(22.0f);
            label->setTextColor(theme::textMuted());
            emptyBox->addView(label);
            this->addView(emptyBox);
            delete scroll;
            // Nothing focusable is left: if we parked the focus on ourselves,
            // it stays here (B still returns to the tab bar), otherwise the
            // tab is not focusable at all.
            if (focusRow < 0) this->setFocusable(false);
            return;
        }
        this->addView(scroll);

        this->setFocusable(false);
        if (focusRow >= 0)
        {
            brls::Box* rows[2] = { titleRow, fileRow };
            brls::Box* row     = rows[focusRow] ? rows[focusRow] : (titleRow ? titleRow : fileRow);
            int n              = (int)row->getChildren().size();
            brls::Application::giveFocus(row->getChildren()[std::min(focusIdx, n - 1)]);
        }
    }

    // A file played without a card: its name and progress. A plays it again
    // (the player offers the saved position), Y removes it, X clears all.
    brls::Box* fileCard(const history::Item& item)
    {
        double prog = item.completed ? 1.0
                      : item.durationSec > 0.0 ? item.positionSec / item.durationSec
                                               : -1.0;
        std::string title = item.title.empty() ? item.source : item.title;
        if (!item.completed && item.positionSec >= 15.0)
            title += "  \xC2\xB7  " + fmtTime(item.positionSec);
        history::Item copy = item;
        auto* card = ui::wideCard(title, "", "", item.posterPath, prog, alive,
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
                                                          copy.fileIndex, w, copy.sourceType);
                                  },
                                  kWideW);

        card->registerAction(
            tr("Remove"), brls::BUTTON_Y,
            [copy](brls::View*) {
                std::string msg = std::string(tr("Remove this item from watch history?")) +
                                  "\n\n" + (copy.title.empty() ? copy.source : copy.title);
                auto* d = new brls::Dialog(msg);
                d->addButton(tr("Cancel"), []() {});
                d->addButton(tr("Remove"), [copy]() { history::remove(copy.id); });
                d->open();
                return true;
            },
            false, false, brls::SOUND_CLICK);

        card->registerAction(
            tr("Clear all history"), brls::BUTTON_X,
            [](brls::View*) {
                auto* d = new brls::Dialog(
                    tr("Are you sure you want to clear your entire watch history?"));
                d->addButton(tr("Cancel"), []() {});
                d->addButton(tr("Clear"), []() { history::clear(); });
                d->open();
                return true;
            },
            false, false, brls::SOUND_CLICK);
        return card;
    }
};

} // namespace

brls::View* buildHistoryTab()
{
    return new HistoryTabRoot();
}
