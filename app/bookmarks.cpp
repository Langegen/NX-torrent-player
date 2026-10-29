#include "bookmarks.hpp"

#include <memory>
#include <string>

#include "appdata.hpp"
#include "card.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{

constexpr float kInset   = 60.0f;
constexpr float kPosterW = 190.0f;
constexpr float kGlow    = 24.0f;

// Where an export goes and an import is read from: a fixed name, so it is
// easy to find on the SD card and to copy to another console.
const char* kExportPath = APPDATA_DIR "/library-export.json";

void note(const std::string& msg)
{
    auto* d = new brls::Dialog(msg);
    d->addButton("OK", []() {});
    d->open();
}

class BookmarksRoot : public brls::Box
{
  public:
    BookmarksRoot()
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);

        auto* top = new brls::Box();
        top->setAxis(brls::Axis::ROW);
        top->setPadding(18.0f, kInset, 4.0f, kInset);
        exportBtn = ui::actionButton(tr("Export"), false, []() {
            std::string err;
            if (library::exportTo(kExportPath, err))
                note(tr("The library was saved to ") + std::string(kExportPath));
            else
                note(tr("Export failed: ") + err);
        });
        top->addView(exportBtn);
        top->addView(ui::actionButton(tr("Import"), false, []() {
            int added = 0;
            std::string err;
            if (library::importFrom(kExportPath, added, err))
                note(tr("Imported. New titles: ") + std::to_string(added));
            else
                note(tr("Import failed: ") + err + "\n" + tr("Expected file: ") +
                     std::string(kExportPath));
        }));
        addView(top);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        list = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setPadding(4.0f, 0.0f, 36.0f, 0.0f);
        scroll->setContentView(list);
        addView(scroll);

        seenGen = library::generation();
        rebuild();
    }

    ~BookmarksRoot() override { *alive = false; }

    // A status changed on a card pushed over this tab: rebuild on return.
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
              brls::FrameContext* ctx) override
    {
        uint32_t gen = library::generation();
        if (gen != seenGen)
        {
            seenGen = gen;
            rebuild();
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

  private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    brls::Box* list        = nullptr;
    brls::Button* exportBtn = nullptr;
    uint32_t seenGen = 0;

    void rebuild()
    {
        // The focus comes back to the card it was on when a pushed card
        // closes -- and that card is about to be freed. Park it first.
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
            if (v == list)
            {
                brls::Application::giveFocus(exportBtn);
                break;
            }
        *alive = false;
        alive  = std::make_shared<bool>(true);
        list->clearViews();

        const std::pair<library::Status, const char*> sections[] = {
            { library::Status::Watching, "Watching" },
            { library::Status::Planned, "Planned" },
            { library::Status::Completed, "Completed" },
            { library::Status::Dropped, "Dropped" },
        };
        int shown = 0;
        for (const auto& sec : sections)
        {
            auto entries = library::withStatus(sec.first);
            if (entries.empty()) continue;
            list->addView(ui::heading(std::string(tr(sec.second)) + "  \xC2\xB7  " +
                                          std::to_string(entries.size()),
                                      kInset));
            auto* row = new brls::Box();
            for (const auto& e : entries)
            {
                content::Item it = e.item;
                auto* card = ui::posterCard(it.title, it.key(), it.posterUrl, alive,
                                            [it]() { openContentCard(it); }, kPosterW);
                if (row->getChildren().empty()) card->setMarginLeft(kInset);
                row->addView(card);
            }
            list->addView(ui::strip(row, kPosterW * 1.5f + kGlow));
            shown++;
        }

        if (shown == 0)
        {
            auto* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setMargins(30.0f, kInset, 0.0f, kInset);
            box->setPadding(26.0f, 30.0f, 26.0f, 30.0f);
            box->setCornerRadius(14.0f);
            box->setBackgroundColor(theme::scrim(20));
            auto* t = new brls::Label();
            t->setText(tr("No bookmarks yet"));
            t->setFontSize(26.0f);
            box->addView(t);
            auto* l = new brls::Label();
            l->setText(tr("Open a film or a show and press \"Bookmark\" to keep it here, "
                          "sorted by what you are watching, planning, have finished or "
                          "dropped."));
            l->setFontSize(18.0f);
            l->setTextColor(theme::textBody());
            l->setMarginTop(10.0f);
            box->addView(l);
            list->addView(box);
        }
    }
};

} // namespace

brls::View* buildBookmarksTab()
{
    return new BookmarksRoot();
}
