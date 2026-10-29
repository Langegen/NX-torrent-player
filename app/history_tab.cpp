#include "history_tab.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

#include <borealis.hpp>

#include "history.hpp"
#include "i18n.hpp"
#include "player.hpp"
#include "theme.hpp"

namespace
{

class HistoryProgressBar : public brls::Box
{
  public:
    explicit HistoryProgressBar(float progress) : progress(std::clamp(progress, 0.0f, 1.0f))
    {
        this->setHeight(4.0f);
        this->setCornerRadius(2.0f);
        this->setBackgroundColor(theme::scrim(30));
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (progress > 0.0f)
        {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w * progress, h, 2.0f);
            nvgFillColor(vg, theme::accent());
            nvgFill(vg);
        }
    }

  private:
    float progress = 0.0f;
};

std::string formatDate(int64_t timestamp)
{
    if (timestamp <= 0) return "";
    std::time_t t = (std::time_t)timestamp;
    // Local time, not UTC: libnx's __libnx_init_time() reads the console's
    // time zone (timeToCalendarTimeWithMyRule) at startup and sets TZ from it.
    std::tm* tm = std::localtime(&t);
    if (!tm) return "";
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
}

// True if `v` is `ancestor` or sits under it.
bool isUnder(brls::View* v, brls::View* ancestor)
{
    for (; v; v = v->getParent())
        if (v == ancestor) return true;
    return false;
}

class HistoryTabRoot : public brls::Box
{
  public:
    HistoryTabRoot()
    {
        this->setAxis(brls::Axis::COLUMN);
        this->setGrow(1.0f);
        this->seenGen = history::generation();
        rebuild();
    }

    ~HistoryTabRoot() override
    {
        *alive = false;
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        // The list changes under us when the player records progress or a
        // dialog removes an entry. Rebuild only once we are the top activity
        // again: while a dialog (or the player) sits above us, borealis'
        // focus stack holds a raw pointer to the row that opened it, and
        // freeing that row would crash when the dialog closes and hands the
        // focus back.
        if (history::generation() != seenGen && !rebuildQueued && isOnTop())
        {
            rebuildQueued = true;
            auto live     = alive;
            brls::sync([this, live]() {
                if (!*live) return;  // the tab was switched away meanwhile
                rebuildQueued = false;
                if (!isOnTop()) return;  // draw() retries next frame
                seenGen = history::generation();
                rebuild();
            });
        }
    }

    void rebuild()
    {
        // clearViews() frees the rows, and the focused one with them: park the
        // focus on ourselves first (hidden highlight), then hand it to the row
        // at the same place in the new list. Leaving it on a freed row is an
        // immediate crash on the next input (see StremioTab::parkFocusOffList).
        int focusIdx       = -1;
        brls::View* cur    = brls::Application::getCurrentFocus();
        if (cur && isUnder(cur, this))
        {
            focusIdx = 0;
            for (size_t i = 0; i < rows.size(); i++)
                if (isUnder(cur, rows[i])) focusIdx = (int)i;
            this->setFocusable(true);
            this->setHideHighlight(true);
            brls::Application::giveFocus(this);
        }

        this->clearViews();
        rows.clear();

        std::vector<history::Item> items = history::all();
        if (items.empty())
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
            // Nothing focusable is left: if we parked the focus on ourselves,
            // it stays here (B still returns to the tab bar), otherwise the
            // tab is not focusable at all.
            if (focusIdx < 0) this->setFocusable(false);
            return;
        }

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);

        auto* list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(24.0f, 40.0f, 24.0f, 40.0f);

        for (const auto& it : items)
        {
            brls::Box* row = createItemRow(it);
            rows.push_back(row);
            list->addView(row);
        }

        scroll->setContentView(list);
        this->addView(scroll);

        if (focusIdx >= 0)
        {
            int idx = std::min(focusIdx, (int)rows.size() - 1);
            this->setFocusable(false);
            brls::Application::giveFocus(rows[idx]);
        }
        else
        {
            this->setFocusable(false);
        }
    }

  private:
    bool isOnTop()
    {
        auto stack = brls::Application::getActivitiesStack();
        return !stack.empty() && stack.back() == this->getParentActivity();
    }

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::vector<brls::Box*> rows;  // owned by the list; cleared with it
    bool rebuildQueued          = false;

    brls::Box* createItemRow(const history::Item& item)
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
        titleLbl->setText(item.title.empty() ? item.source : item.title);
        titleLbl->setFontSize(20.0f);
        titleLbl->setSingleLine(true);
        row->addView(titleLbl);

        // Line 2: Meta (Source badge, Progress / Status, Date)
        auto* meta = new brls::Box();
        meta->setAxis(brls::Axis::ROW);
        meta->setAlignItems(brls::AlignItems::CENTER);
        meta->setMarginTop(8.0f);

        // Source badge
        std::string srcType = item.sourceType.empty() ? "Local" : item.sourceType;
        auto* srcBadge = new brls::Label();
        srcBadge->setText("[" + srcType + "]");
        srcBadge->setFontSize(15.0f);
        srcBadge->setTextColor(theme::accentText());
        srcBadge->setMarginRight(14.0f);
        meta->addView(srcBadge);

        // Progress or completed status
        auto* statusLbl = new brls::Label();
        if (item.completed)
        {
            statusLbl->setText(tr("Completed"));
            statusLbl->setTextColor(theme::accentText());
        }
        else
        {
            std::string posStr = fmtTime(item.positionSec);
            std::string durStr = item.durationSec > 0.0 ? (" / " + fmtTime(item.durationSec)) : "";
            std::string pctStr;
            if (item.durationSec > 0.0)
            {
                int pct = (int)((item.positionSec / item.durationSec) * 100.0);
                pctStr = " (" + std::to_string(std::clamp(pct, 0, 100)) + "%)";
            }
            statusLbl->setText(posStr + durStr + pctStr);
            statusLbl->setTextColor(theme::textBody());
        }
        statusLbl->setFontSize(15.0f);
        statusLbl->setMarginRight(16.0f);
        meta->addView(statusLbl);

        // Date
        std::string dtStr = formatDate(item.lastWatched);
        if (!dtStr.empty())
        {
            auto* dtLbl = new brls::Label();
            dtLbl->setText(dtStr);
            dtLbl->setFontSize(14.0f);
            dtLbl->setTextColor(theme::textMuted());
            meta->addView(dtLbl);
        }

        row->addView(meta);

        // Line 3: Progress bar if not completed and duration known
        if (!item.completed && item.durationSec > 0.0 && item.positionSec > 0.0)
        {
            float prog = (float)(item.positionSec / item.durationSec);
            auto* pb = new HistoryProgressBar(prog);
            pb->setMarginTop(10.0f);
            row->addView(pb);
        }

        // Action A: Play / Resume
        history::Item itemCopy = item;
        row->registerClickAction([itemCopy](brls::View*) {
            PlayerArt art;
            art.posterPath = itemCopy.posterPath;
            WatchInfo w;
            w.authKey = itemCopy.stremioAuthKey;
            w.itemId = itemCopy.stremioItemId;
            w.videoId = itemCopy.stremioVideoId;
            w.type = itemCopy.stremioType;
            w.displayTitle = itemCopy.title;
            startPlaybackPrompt(itemCopy.source, art, itemCopy.title, itemCopy.fileIndex, w, itemCopy.sourceType);
            return true;
        });

        // Action Y: Remove item
        row->registerAction(
            tr("Remove"), brls::BUTTON_Y,
            [itemCopy](brls::View*) {
                std::string msg = std::string(tr("Remove this item from watch history?")) + "\n\n" +
                                  (itemCopy.title.empty() ? itemCopy.source : itemCopy.title);
                auto* d = new brls::Dialog(msg);
                d->addButton(tr("Cancel"), []() {});
                d->addButton(tr("Remove"), [itemCopy]() {
                    history::remove(itemCopy.id);
                });
                d->open();
                return true;
            },
            false, false, brls::SOUND_CLICK);

        // Action X: Clear all history
        row->registerAction(
            tr("Clear all history"), brls::BUTTON_X,
            [](brls::View*) {
                auto* d = new brls::Dialog(tr("Are you sure you want to clear your entire watch history?"));
                d->addButton(tr("Cancel"), []() {});
                d->addButton(tr("Clear"), []() {
                    history::clear();
                });
                d->open();
                return true;
            },
            false, false, brls::SOUND_CLICK);

        return row;
    }

    uint32_t seenGen = 0;
};

} // namespace

brls::View* buildHistoryTab()
{
    return new HistoryTabRoot();
}
