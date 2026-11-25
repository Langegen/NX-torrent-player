#include "bookmarks.hpp"

#include <memory>
#include <string>
#include <vector>

#include "appdata.hpp"
#include "card.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "platform_ui.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{

inline float inset() { return platform_ui::sideInset(); }
inline float posterWidth()
{
    return (platform_ui::isMobile() && platform_ui::isPortrait()) ? 156.0f : 190.0f;
}
constexpr float kGlow = 24.0f;

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

        float ins = inset();
        auto* top = new brls::Box();
        top->setPadding(12.0f, ins, 4.0f, ins);

        newFolderBtn = ui::actionButton(tr("+ New folder"), false, [this]() {
            promptCreateFolder();
        });
        top->addView(newFolderBtn);

        manageFoldersBtn = ui::actionButton(tr("Manage folders"), false, [this]() {
            manageFoldersMenu();
        });
        top->addView(manageFoldersBtn);

        exportBtn = ui::actionButton(tr("Export"), false, []() {
            std::string err;
            if (library::exportTo(kExportPath, err))
                note(tr("The library was saved to ") + std::string(kExportPath));
            else
                note(tr("Export failed: ") + err);
        });
        top->addView(exportBtn);

        auto* importBtn = ui::actionButton(tr("Import"), false, []() {
            int added = 0;
            std::string err;
            if (library::importFrom(kExportPath, added, err))
                note(tr("Imported. New titles: ") + std::to_string(added));
            else
                note(tr("Import failed: ") + err + "\n" + tr("Expected file: ") +
                     std::string(kExportPath));
        });
        top->addView(importBtn);

        addView(ui::pillStrip(top, 56.0f));

        // Filter pills strip
        auto* filterStrip = new ui::NearestStrip();
        filterStrip->setScrollingIndicatorVisible(false);
        filterStrip->setHeight(52.0f);
        filterBox = new brls::Box();
        filterBox->setAxis(brls::Axis::ROW);
        filterBox->setAlignItems(brls::AlignItems::CENTER);
        filterBox->setPadding(4.0f, ins, 6.0f, ins);
        filterStrip->setContentView(filterBox);
        addView(filterStrip);

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
    brls::Box* list = nullptr;
    brls::Box* filterBox = nullptr;
    brls::Button* newFolderBtn = nullptr;
    brls::Button* manageFoldersBtn = nullptr;
    brls::Button* exportBtn = nullptr;
    uint32_t seenGen = 0;
    std::string activeFilter = ""; // "" = All, "__watching__", "__planned__", etc., or folder name

    void promptCreateFolder()
    {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(0, 1);
                while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
                if (text.empty()) return;
                if (library::createFolder(text))
                {
                    activeFilter = text;
                    brls::Application::notify(tr("Folder created"));
                    rebuild();
                }
                else
                {
                    note(tr("Folder already exists"));
                }
            },
            tr("New folder"), "", 64, "");
    }

    void promptRenameFolder(const std::string& oldName)
    {
        brls::Application::getImeManager()->openForText(
            [this, oldName](std::string text) {
                while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(0, 1);
                while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
                if (text.empty() || text == oldName) return;
                if (library::renameFolder(oldName, text))
                {
                    if (activeFilter == oldName) activeFilter = text;
                    rebuild();
                }
                else
                {
                    note(tr("Folder already exists"));
                }
            },
            tr("Rename folder"), "", 64, oldName);
    }

    void promptDeleteFolder(const std::string& name)
    {
        auto* d = new brls::Dialog(
            std::string(tr("Delete folder")) + " \"" + name + "\"?\n\n" +
            tr("Titles in this folder will remain in your bookmarks."));
        d->addButton(tr("Cancel"), []() {});
        d->addButton(tr("Delete"), [this, name]() {
            library::deleteFolder(name);
            if (activeFilter == name) activeFilter = "";
            rebuild();
        });
        d->open();
    }

    void manageFoldersMenu()
    {
        auto custom = library::customFolders();
        if (custom.empty()) return;
        auto* dd = new brls::Dropdown(tr("Manage folders"), custom, [this, custom](int i) {
            if (i < 0 || i >= (int)custom.size()) return;
            std::string selected = custom[i];
            auto* d = new brls::Dialog(std::string(tr("Folder: ")) + selected);
            d->addButton(tr("Rename folder"), [this, selected]() {
                promptRenameFolder(selected);
            });
            d->addButton(tr("Delete folder"), [this, selected]() {
                promptDeleteFolder(selected);
            });
            d->addButton(tr("Cancel"), []() {});
            d->open();
        });
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void openItemOptions(const content::Item& it)
    {
        std::vector<std::string> opts = {
            tr("Folders"),
            tr("Bookmark"),
            tr("Remove from bookmarks")
        };
        auto* dd = new brls::Dropdown(it.title, opts, [this, it](int i) {
            if (i == 0)
                openFolderPicker(it);
            else if (i == 1)
                openStatusPicker(it);
            else if (i == 2)
            {
                library::setStatus(it, library::Status::None);
                brls::Application::notify(tr("Removed from bookmarks"));
            }
        });
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void openFolderPicker(const content::Item& item)
    {
        std::vector<std::string> custom = library::customFolders();
        library::Entry en;
        bool hasEntry = library::find(item.key(), en);

        std::vector<std::string> labels;
        for (const auto& f : custom)
        {
            bool inThis = hasEntry && en.inFolder(f);
            labels.push_back((inThis ? "[\xE2\x9C\x93]  " : "[   ]  ") + f);
        }
        labels.push_back(std::string("+  ") + tr("New folder..."));
        if (hasEntry && !en.folders.empty())
            labels.push_back(tr("Remove from all folders"));

        auto* dd = new brls::Dropdown(tr("Folders"), labels, [this, item, custom](int i) {
            if (i < 0) return;
            if (i < (int)custom.size())
            {
                library::toggleFolder(item, custom[i]);
            }
            else if (i == (int)custom.size())
            {
                brls::Application::getImeManager()->openForText(
                    [this, item](std::string text) {
                        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(0, 1);
                        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
                        if (text.empty()) return;
                        library::createFolder(text);
                        library::toggleFolder(item, text);
                        brls::Application::notify(tr("Folder created"));
                    },
                    tr("New folder"), "", 64, "");
            }
            else
            {
                library::setFolders(item, {});
            }
        });
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    void openStatusPicker(const content::Item& item)
    {
        static const library::Status kOrder[] = {
            library::Status::Watching, library::Status::Planned, library::Status::Completed,
            library::Status::Dropped, library::Status::None,
        };
        std::vector<std::string> labels = { tr("Watching"), tr("Planned"), tr("Completed"),
                                            tr("Dropped"), tr("Remove from bookmarks") };
        library::Entry en;
        library::Status cur = library::find(item.key(), en) ? en.status
                                                            : library::Status::None;
        int sel = 0;
        for (int i = 0; i < 5; i++)
            if (kOrder[i] == cur) sel = i;
        auto* dd = new brls::Dropdown(tr("Bookmark"), labels, [item](int i) {
            if (i < 0 || i >= 5) return;
            library::setStatus(item, kOrder[i]);
        }, sel);
        brls::Application::pushActivity(new brls::Activity(dd));
    }

    brls::Box* buildCard(const content::Item& it)
    {
        auto* card = ui::posterCard(it.title, it.key(), it.posterUrl, alive,
                                    [it]() { openContentCard(it); }, posterWidth());
        card->registerAction(tr("Actions"), brls::BUTTON_Y, [this, it](brls::View*) {
            openItemOptions(it);
            return true;
        }, false, false, brls::SOUND_CLICK);
        return card;
    }

    void rebuildFilters()
    {
        filterBox->clearViews();
        std::vector<std::pair<std::string, std::string>> filters = {
            { "", tr("All") },
            { "__watching__", tr("Watching") },
            { "__planned__", tr("Planned") },
            { "__completed__", tr("Completed") },
            { "__dropped__", tr("Dropped") },
        };
        auto custom = library::customFolders();
        for (const auto& f : custom)
            filters.push_back({ f, "\xF0\x9F\x93\x81 " + f });

        for (const auto& item : filters)
        {
            std::string filterKey = item.first;
            bool isActive = (activeFilter == filterKey);
            auto* btn = ui::actionButton(item.second, false, [this, filterKey]() {
                if (activeFilter != filterKey)
                {
                    activeFilter = filterKey;
                    rebuild();
                }
            });
            ui::setPillActive(btn, isActive);
            btn->setMarginRight(8.0f);
            filterBox->addView(btn);
        }
    }

    void rebuild()
    {
        // The focus comes back to the card it was on when a pushed card
        // closes -- and that card is about to be freed. Park it first.
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
            if (v == list || v == filterBox)
            {
                brls::Application::giveFocus(newFolderBtn);
                break;
            }
        *alive = false;
        alive  = std::make_shared<bool>(true);

        auto custom = library::customFolders();
        manageFoldersBtn->setVisibility(custom.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        rebuildFilters();

        list->clearViews();

        const std::pair<library::Status, const char*> statuses[] = {
            { library::Status::Watching, "Watching" },
            { library::Status::Planned, "Planned" },
            { library::Status::Completed, "Completed" },
            { library::Status::Dropped, "Dropped" },
        };

        int shown = 0;

        float ins = inset();
        float pw  = posterWidth();

        if (activeFilter.empty())
        {
            // All: statuses first, then custom folders
            for (const auto& sec : statuses)
            {
                auto entries = library::withStatus(sec.first);
                if (entries.empty()) continue;
                list->addView(ui::heading(std::string(tr(sec.second)) + "  \xC2\xB7  " +
                                              std::to_string(entries.size()),
                                          ins));
                auto* row = new brls::Box();
                for (const auto& e : entries)
                {
                    auto* card = buildCard(e.item);
                    if (row->getChildren().empty()) card->setMarginLeft(ins);
                    row->addView(card);
                }
                list->addView(ui::strip(row, pw * 1.5f + kGlow));
                shown++;
            }

            for (const auto& f : custom)
            {
                auto entries = library::withFolder(f);
                if (entries.empty()) continue;
                list->addView(ui::heading("\xF0\x9F\x93\x81 " + f + "  \xC2\xB7  " +
                                              std::to_string(entries.size()),
                                          ins));
                auto* row = new brls::Box();
                for (const auto& e : entries)
                {
                    auto* card = buildCard(e.item);
                    if (row->getChildren().empty()) card->setMarginLeft(ins);
                    row->addView(card);
                }
                list->addView(ui::strip(row, pw * 1.5f + kGlow));
                shown++;
            }
        }
        else if (activeFilter == "__watching__" || activeFilter == "__planned__" ||
                 activeFilter == "__completed__" || activeFilter == "__dropped__")
        {
            library::Status target = (activeFilter == "__watching__")  ? library::Status::Watching
                                   : (activeFilter == "__planned__")   ? library::Status::Planned
                                   : (activeFilter == "__completed__") ? library::Status::Completed
                                                                       : library::Status::Dropped;
            const char* label = (activeFilter == "__watching__")  ? "Watching"
                              : (activeFilter == "__planned__")   ? "Planned"
                              : (activeFilter == "__completed__") ? "Completed"
                                                                  : "Dropped";
            auto entries = library::withStatus(target);
            if (!entries.empty())
            {
                list->addView(ui::heading(std::string(tr(label)) + "  \xC2\xB7  " +
                                              std::to_string(entries.size()),
                                          ins));
                auto* row = new brls::Box();
                for (const auto& e : entries)
                {
                    auto* card = buildCard(e.item);
                    if (row->getChildren().empty()) card->setMarginLeft(ins);
                    row->addView(card);
                }
                list->addView(ui::strip(row, pw * 1.5f + kGlow));
                shown++;
            }
        }
        else
        {
            // Custom folder
            auto entries = library::withFolder(activeFilter);
            auto* header = new brls::Box();
            header->setAxis(brls::Axis::ROW);
            header->setPadding(4.0f, ins, 12.0f, ins);
            header->setAlignItems(brls::AlignItems::CENTER);

            auto* headingLbl = ui::heading("\xF0\x9F\x93\x81 " + activeFilter + "  \xC2\xB7  " +
                                               std::to_string(entries.size()), 0);
            header->addView(headingLbl);

            auto* renameBtn = ui::actionButton(tr("Rename folder"), false, [this]() {
                promptRenameFolder(activeFilter);
            });
            renameBtn->setMarginLeft(24.0f);
            header->addView(renameBtn);

            auto* delBtn = ui::actionButton(tr("Delete folder"), false, [this]() {
                promptDeleteFolder(activeFilter);
            });
            delBtn->setMarginLeft(10.0f);
            header->addView(delBtn);

            list->addView(header);

            if (!entries.empty())
            {
                auto* row = new brls::Box();
                for (const auto& e : entries)
                {
                    auto* card = buildCard(e.item);
                    if (row->getChildren().empty()) card->setMarginLeft(ins);
                    row->addView(card);
                }
                list->addView(ui::strip(row, pw * 1.5f + kGlow));
                shown++;
            }
            else
            {
                auto* box = new brls::Box();
                box->setAxis(brls::Axis::COLUMN);
                box->setMargins(20.0f, ins, 0.0f, ins);
                box->setPadding(26.0f, 30.0f, 26.0f, 30.0f);
                box->setCornerRadius(14.0f);
                box->setBackgroundColor(theme::scrim(20));
                auto* t = new brls::Label();
                t->setText(tr("Folder is empty"));
                t->setFontSize(26.0f);
                box->addView(t);
                auto* l = new brls::Label();
                l->setText(platform_ui::isMobile()
                               ? tr("Open a film or a show card and press \"Folder\" to add it here.")
                               : tr("Open a film or a show card and press \"Folder\" to add it here, or press (Y) on a bookmark."));
                l->setFontSize(18.0f);
                l->setTextColor(theme::textBody());
                l->setMarginTop(10.0f);
                box->addView(l);
                list->addView(box);
                shown = 1; // Handled empty folder state
            }
        }

        if (shown == 0)
        {
            auto* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setMargins(30.0f, ins, 0.0f, ins);
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
