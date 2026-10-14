#include "history_tab.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
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
    std::tm* tm = std::localtime(&t);
    if (!tm) return "";
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
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

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        if (history::generation() != seenGen)
        {
            seenGen = history::generation();
            brls::sync([this]() { rebuild(); });
        }
    }

    void rebuild()
    {
        this->clearViews();

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
            return;
        }

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);

        auto* list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(24.0f, 40.0f, 24.0f, 40.0f);

        for (const auto& it : items)
        {
            list->addView(createItemRow(it));
        }

        scroll->setContentView(list);
        this->addView(scroll);
    }

  private:
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
        srcBadge->setTextColor(theme::accent());
        srcBadge->setMarginRight(14.0f);
        meta->addView(srcBadge);

        // Progress or completed status
        auto* statusLbl = new brls::Label();
        if (item.completed)
        {
            statusLbl->setText(tr("Completed"));
            statusLbl->setTextColor(theme::accent());
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
