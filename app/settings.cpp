#include "settings.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/tab_frame.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>

#include "config.hpp"
#include "jackett.hpp"
#include "picture.hpp"
#include "http.hpp"
#include "torrserver.hpp"
#include "tmdb.hpp"
#include "theme.hpp"
#include "update.hpp"
#include "stremio.hpp"
#include "online/online_manager.hpp"

#include "sync/sync_manager.hpp"
#include "sync/sync_dialog.hpp"
#include "sync/sync_webdav.hpp"
#include "sync/sync_gdrive.hpp"

extern "C" {
#include "torrentfs.h"
}

#include "i18n.hpp"
#include "osk.hpp"
#include "platform_ui.hpp"
#include "widgets.hpp"  // ui::FieldFrame

namespace
{

void note(const std::string& msg)
{
    auto* d = new brls::Dialog(msg);
    d->addButton("OK", []() {});
    d->open();
}

std::function<void()> uiScaleHook;
std::function<void()> openStremioHook;

// A Label an outstanding background job can safely give up on: the Options
// screen is popped long before a slow SD-card scan of the poster cache is
// necessarily over, and the callback would write into a freed view.
class AsyncLabel : public brls::Label
{
  public:
    ~AsyncLabel() override { *alive = false; }
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// And for a button that a background job relabels: the connection tests run
// for up to half a minute, and switching the Options sidebar frees the pane
// (and the button) meanwhile.
class AsyncButton : public brls::Button
{
  public:
    ~AsyncButton() override { *alive = false; }
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// The same guard for a Box that a background job fills with rows -- the Account
// screen's addon list, whose fetch can land after B has popped it.
class AsyncBox : public brls::Box
{
  public:
    ~AsyncBox() override { *alive = false; }
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

brls::View* generalPane();
brls::View* playbackPane();
brls::View* streamingPane();
brls::View* stremioPane();
brls::View* jackettPane();
brls::View* onlinePane();
brls::View* syncPane();
brls::View* networkPane();
brls::View* aboutPane();

// ---------------------------------------------------------------------------
// The panes. One per sidebar tab, each built fresh when its tab is entered
// (TabFrame frees the previous one), so they read config at build time and
// share nothing.

// The scrolling column a pane is. Returns the frame to hand to the tab; `list`
// comes back as the box to fill.
brls::ScrollingFrame* newPane(brls::Box** list)
{
    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    // CENTERED, not the default NATURAL: NATURAL's pixel free-scroll hands focus
    // to the frame and re-grabs the topmost visible cell as you cross a page,
    // which intermittently wedged the cursor and refused to go further.
    // CENTERED moves focus one cell at a time and scrolls to keep it in view.
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    // No margin: setContentView detaches the box and forces its width, so only
    // padding survives. Right padding stays clear of the scrolling indicator,
    // which the frame pins to its own edge.
    float inset = platform_ui::isMobile()
        ? (platform_ui::isPortrait() ? 16.0f : 32.0f)
        : 40.0f;
    box->setPadding(14.0f, inset, 40.0f, inset);
    scroll->setContentView(box);

    *list = box;
    return scroll;
}

// A caption under the cell it belongs to.
brls::Label* caption(const std::string& text)
{
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(15.0f);
    l->setTextColor(theme::textMuted());
    l->setLineHeight(1.4f);
    l->setMargins(10.0f, 20.0f, 18.0f, 20.0f);
    return l;
}

// Index of the stored width in the offered list. load() rejects anything it
// does not offer, so the fallback only matters if the table is ever changed
// without the defaults following it.
int uiIndex(int width)
{
    const auto& ws = config::uiWidths();
    for (size_t i = 0; i < ws.size(); i++)
        if (ws[i] == width) return (int)i;
    return 0;
}

// The list is shared between the two modes, but they do not default to the same
// size -- so mark the default in each cell rather than in the table.
std::vector<std::string> uiLabels(int dflt)
{
    std::vector<std::string> v = config::uiWidthLabels();
    const auto& ws             = config::uiWidths();
    for (size_t i = 0; i < ws.size(); i++)
        if (ws[i] == dflt) v[i] += tr(" (default)");
    return v;
}

// Index of a stored language code in the offered list; "auto" (0) if we do not
// know it -- a config.json edited by hand can say anything.
int langIndex(const std::string& code)
{
    const auto& codes = config::langCodes();
    for (size_t i = 0; i < codes.size(); i++)
        if (codes[i] == code) return (int)i;
    return 0;
}

brls::View* generalPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto* startup = new brls::SelectorCell();
    int curStartupIdx = (cfg.startupTab == config::Tab::STREMIO) ? 1 :
                        (cfg.startupTab == config::Tab::JACKETT) ? 2 :
                        (cfg.startupTab == config::Tab::HISTORY) ? 3 :
                        (cfg.startupTab == config::Tab::HOME)    ? 4 :
                        (cfg.startupTab == config::Tab::BOOKMARKS) ? 5 : 0;
    startup->init(tr("Category on startup"),
                  { tr("Local"), tr("Stremio"), "Jackett", tr("History"), tr("Home"),
                    tr("Bookmarks") },
                  curStartupIdx, [](int sel) {
                      config::get().startupTab =
                          (sel == 1) ? config::Tab::STREMIO :
                          (sel == 2) ? config::Tab::JACKETT :
                          (sel == 3) ? config::Tab::HISTORY :
                          (sel == 4) ? config::Tab::HOME :
                          (sel == 5) ? config::Tab::BOOKMARKS : config::Tab::LOCAL;
                      config::save();
                  });
    list->addView(startup);

    // Same startup-only story as the theme below, for the same reason plus one
    // of its own: a view reads its strings when it is built, and the header,
    // the tab bar and the browser behind this screen were built once at launch.
    auto* language = new brls::SelectorCell();
    language->init(
        tr("Language"), i18n::langLabels(),
        [] {
            const auto& ids = i18n::langIds();
            for (size_t i = 0; i < ids.size(); i++)
                if (ids[i] == config::get().language) return (int)i;
            return 0;
        }(),
        [](int sel) {
            config::get().language = i18n::langIds()[(size_t)sel];
            config::save();
        });
    list->addView(language);
    list->addView(caption(tr("The language applies when you restart the app.")));

    // Startup-only, unlike every other cell here: borealis states the variant is
    // not expected to change while the app runs, and the views already on screen
    // copied their colours out of the theme when they were built. Applying it
    // live would leave the UI half converted, so say so instead.
    static const std::vector<std::string> kVariantIds = { "dark", "light",
                                                          "system" };
    auto* variant = new brls::SelectorCell();
    variant->init(
        tr("Theme"),
        { tr("Dark (default)"), tr("Light"),
#ifdef __SWITCH__
          tr("Follow the console")
#else
          tr("Follow system")
#endif
        },
        [] {
            for (size_t i = 0; i < kVariantIds.size(); i++)
                if (kVariantIds[i] == config::get().themeVariant) return (int)i;
            return 0;
        }(),
        [](int sel) {
            config::get().themeVariant = kVariantIds[sel];
            config::save();
            // Nothing to apply: theme::applyVariant() latched the variant at
            // startup and is the only reader of this setting.
        });
    list->addView(variant);
    list->addView(caption(tr("The theme applies when you restart the app.")));

    auto* accent = new brls::SelectorCell();
    accent->init(
        tr("Accent colour"),
        [] {
            std::vector<std::string> v = theme::schemeLabels();
            if (!v.empty()) v[0] += tr(" (default)");  // schemes() is default-first
            return v;
        }(),
        [] {
            const auto& ids = theme::schemeIds();
            for (size_t i = 0; i < ids.size(); i++)
                if (ids[i] == config::get().accent) return (int)i;
            return 0;
        }(),
        [](int sel) {
            config::get().accent = theme::schemeIds()[sel];
            config::save();
            // The backgrounds follow on the next frame by themselves (they read
            // the scheme as they draw); this repaints the theme colours that
            // views copy out of it.
            theme::applyAccent();
        });
    list->addView(accent);

    auto* builtinKbd = new brls::BooleanCell();
    builtinKbd->init(tr("Built-in keyboard"), config::get().builtinKeyboard, [](bool on) {
        config::get().builtinKeyboard = on;
        config::save();
        osk::applyConfig();
    });
    list->addView(builtinKbd);
    list->addView(caption(tr("Use the app's own on-screen keyboard instead of the system one. Helps if the system keyboard crashes.")));

    auto* sizeHdr = new brls::Header();
    sizeHdr->setTitle(tr("UI size"));
    list->addView(sizeHdr);

#ifdef __SWITCH__
    auto* dockedUi = new brls::SelectorCell();
    dockedUi->init(tr("Docked"), uiLabels(config::kDefaultDockedUiWidth),
                   uiIndex(config::get().dockedUiWidth), [](int sel) {
                       config::get().dockedUiWidth = config::uiWidths()[sel];
                       config::save();
                       brls::sync([] { applyUiScale(); });
                   });
    list->addView(dockedUi);

    auto* handheldUi = new brls::SelectorCell();
    handheldUi->init(tr("Handheld"), uiLabels(config::kDefaultHandheldUiWidth),
                     uiIndex(config::get().handheldUiWidth), [](int sel) {
                         config::get().handheldUiWidth = config::uiWidths()[sel];
                         config::save();
                         brls::sync([] { applyUiScale(); });
                     });
    list->addView(handheldUi);
#else
    auto* scaleCell = new brls::SelectorCell();
    scaleCell->init(tr("Interface scale"), uiLabels(config::defaultHandheldUiWidth()),
                    uiIndex(config::get().handheldUiWidth), [](int sel) {
                        config::get().handheldUiWidth = config::uiWidths()[sel];
                        config::save();
                        brls::sync([] { applyUiScale(); });
                    });
    list->addView(scaleCell);
#endif

    return pane;
}

brls::View* playbackPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto* alang = new brls::SelectorCell();
    alang->init(tr("Audio language"), config::langLabels(),
                langIndex(cfg.audioLang), [](int sel) {
                    config::get().audioLang = config::langCodes()[sel];
                    config::save();
                });
    list->addView(alang);

    auto* slang = new brls::SelectorCell();
    slang->init(tr("Subtitle language"), config::langLabels(),
                langIndex(cfg.subLang), [](int sel) {
                    config::get().subLang = config::langCodes()[sel];
                    config::save();
                });
    list->addView(slang);

    auto* subs = new brls::BooleanCell();
    subs->init(tr("Subtitles"), cfg.subtitles, [](bool on) {
        config::get().subtitles = on;
        config::save();
    });
    list->addView(subs);

    auto* audioBoost = new brls::BooleanCell();
    audioBoost->init(
#ifdef __SWITCH__
        tr("Boost quiet audio in handheld"),
#else
        tr("Boost quiet audio"),
#endif
        cfg.audioBoost, [](bool on) {
        config::get().audioBoost = on;
        config::save();
        // The player re-reads this every frame, so it reaches a video that is
        // already playing as well as the next one.
    });
    list->addView(audioBoost);
    list->addView(caption(
        tr("On by default. Lifts a 5.1 mix folded down to stereo, which plays "
        "much quieter.")));

    auto* hwdec = new brls::BooleanCell();
    hwdec->init(tr("Hardware decoding"), cfg.hwDecode, [](bool on) {
        config::get().hwDecode = on;
        config::save();
    });
    list->addView(hwdec);
    list->addView(caption(
        tr("On by default. Off decodes in software: slower, may stutter on 1080p.")));

    auto* endAction = new brls::SelectorCell();
    endAction->init(tr("When a video ends"), config::endActionLabels(), cfg.endAction,
                    [](int sel) {
                        config::get().endAction = sel;
                        config::save();
                    });
    list->addView(endAction);
    list->addView(caption(
        tr("Also in the player's settings, with the sleep timer.")));

    // The picture on the console's own screen (picture.hpp). The settings
    // themselves are on the calibration screen, next to the patterns they are
    // judged against -- and in the player's panel, against the film.
    auto* picHdr = new brls::Header();
#ifdef __SWITCH__
    picHdr->setTitle(tr("Picture in handheld"));
#else
    picHdr->setTitle(tr("Picture"));
#endif
    list->addView(picHdr);

    auto* pic = new brls::DetailCell();
    pic->setText(tr("Picture and test patterns"));
    auto showValue = [pic]() {
        bool changed = false;
        for (int p = 0; p < picture::PROP_COUNT; p++)
            if (picture::value(p) != 0) changed = true;
        pic->setDetailText(changed ? tr("Adjusted") : tr("Default"));
        pic->setDetailTextColor(changed ? theme::accentText() : theme::textMuted());
    };
    showValue();
    pic->registerClickAction([showValue](brls::View*) {
        picture::openCalibration(showValue);
        return true;
    });
    list->addView(pic);
    list->addView(caption(
#ifdef __SWITCH__
        tr("Brightness, contrast, gamma, saturation and RGB balance of the video on the console's "
           "screen. Docked they are off: the TV sets the picture.")
#else
        tr("Brightness, contrast, gamma, saturation and RGB balance of the video.")
#endif
    ));

    return pane;
}

brls::View* streamingPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    // A SelectorCell rather than a BooleanCell for the two settings whose value
    // needs saying more than "On"/"Off": BooleanCell hardcodes those two strings
    // in a private, non-virtual updateUI() that also re-runs 200ms after every
    // toggle (the end of its scale animation), so its text cannot be overridden
    // from here.
    auto* ramStream = new brls::SelectorCell();
    ramStream->init(
        platform_ui::isMobile() ? tr("Stream to RAM (no disk cache)") : tr("Stream to RAM (no SD cache)"),
        { tr("Off (Not recommended)"), tr("On (Recommended)") },
        cfg.ramStream ? 1 : 0, [](int sel) {
            config::get().ramStream = sel == 1;
            config::save();
            // Latched when the engine opens, so it takes effect on
            // the next video.
        });
    list->addView(ramStream);
    list->addView(caption(
        platform_ui::isMobile()
            ? tr("Keep pieces in memory instead of writing them to storage. Reduces stutter, at the cost of no resume and a limited seek-back range.")
            : tr("Keep pieces in memory instead of writing them to the SD card. Removes "
                 "the stutter on each finished piece, at the cost of no resume and a "
                 "limited seek-back range.")));

    auto* buffer = new brls::SelectorCell();
    buffer->init(tr("Buffer size"), config::bufferLabels(), cfg.bufferMode,
                 [](int sel) {
                     config::get().bufferMode = sel;
                     config::save();
                     // Read when the player opens: the next video.
                 });
    list->addView(buffer);
    list->addView(caption(
        tr("How far ahead the video is loaded. Larger rides out a slow swarm or "
        "a weak wifi, but uses more memory and takes longer to start. Applies to "
        "the next video.")));

    auto* governor = new brls::SelectorCell();
    governor->init(tr("Limit download rate"), { tr("Off (default)"), tr("On") },
                   cfg.rateGovernor ? 1 : 0, [](int sel) {
                       config::get().rateGovernor = sel == 1;
                       config::save();
                       // Takes effect immediately, even for a stream already
                       // playing.
                       torrentfs_set_governor(sel == 1 ? 1 : 0);
                   });
    list->addView(governor);
    list->addView(caption(
        tr("Once the buffer is comfortably ahead, cap the download speed instead "
        "of bursting \xE2\x80\x94 the bursts can stutter the system. Off by default.")));

    return pane;
}

brls::View* stremioPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto* hide4k = new brls::BooleanCell();
    hide4k->init(tr("Hide 4K sources"), cfg.hide4k, [](bool on) {
        config::get().hide4k = on;
        config::save();
    });
    list->addView(hide4k);

    // The account itself is not here: it has its own screen, off the header's
    // profile button (see AccountActivity).

    auto* cacheHdr = new brls::Header();
    cacheHdr->setTitle(tr("Poster cache"));
    list->addView(cacheHdr);

    auto humanMB = [](int64_t b) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
        return std::string(buf);
    };
    auto* cacheLbl = new AsyncLabel();
    cacheLbl->setText(tr("Reading..."));
    cacheLbl->setFontSize(16.0f);
    cacheLbl->setTextColor(theme::textDim());
    cacheLbl->setMargins(10.0f, 20.0f, 6.0f, 20.0f);
    list->addView(cacheLbl);

    // Off the UI thread: the size is a stat() per cached poster, and on a full
    // cache that SD-card walk is most of a second -- long enough that the pane
    // took visibly too long to appear, since nothing is drawn until this whole
    // function returns. The label fills itself in a moment later instead.
    auto readCacheSize = [cacheLbl, humanMB]() {
        auto live = cacheLbl->alive;
        brls::async([cacheLbl, live, humanMB]() {
            int64_t bytes = stremio::posterCacheBytes();
            brls::sync([cacheLbl, live, humanMB, bytes]() {
                if (*live) cacheLbl->setText(humanMB(bytes) + tr(" on the SD card"));
            });
        });
    };
    readCacheSize();

    auto* clearCache = new brls::Button();
    clearCache->setText(tr("Clear poster cache"));
    clearCache->setMargins(4.0f, 20.0f, 18.0f, 20.0f);
    clearCache->registerClickAction([cacheLbl, readCacheSize](brls::View*) {
        auto live = cacheLbl->alive;
        // Deleting them is the same walk, so it goes off the thread too --
        // otherwise the whole UI stops until the card is done.
        cacheLbl->setText(tr("Clearing..."));
        brls::async([live, readCacheSize]() {
            stremio::clearPosterCache();
            brls::sync([live, readCacheSize]() {
                stremio::markLibraryStale();  // library reloads on return
                note(tr("Poster cache cleared. The library reloads when you go "
                     "back to it."));
                if (*live) readCacheSize();
            });
        });
        return true;
    });
    list->addView(clearCache);

    return pane;
}

brls::View* jackettPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto* hdr = new brls::Header();
    hdr->setTitle("Jackett");
    list->addView(hdr);

    auto* urlCell = new brls::DetailCell();
    urlCell->setText(tr("Server URL"));
    urlCell->setDetailText(cfg.jackettUrl.empty() ? tr("Not set") : cfg.jackettUrl);
    urlCell->registerClickAction([urlCell](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [urlCell](std::string out) {
                out = jackett::normalizeUrl(out);  // "192.168.1.5" -> http://...:9117
                config::get().jackettUrl = out;
                config::save();
                urlCell->setDetailText(out.empty() ? tr("Not set") : out);
            },
            tr("Jackett Server URL"), tr("e.g. http://192.168.1.100:9117"), 256,
            config::get().jackettUrl);
        return true;
    });
    list->addView(urlCell);
    list->addView(caption(
        tr("The URL of your Jackett server including port (e.g. http://192.168.1.100:9117).")));

    auto* keyCell = new brls::DetailCell();
    keyCell->setText(tr("API Key"));
    auto maskKey = [](const std::string& k) -> std::string {
        if (k.empty()) return tr("None");
        if (k.size() <= 8) return "********";
        return k.substr(0, 4) + "..." + k.substr(k.size() - 4);
    };
    keyCell->setDetailText(maskKey(cfg.jackettApiKey));
    keyCell->registerClickAction([keyCell, maskKey](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [keyCell, maskKey](std::string out) {
                config::get().jackettApiKey = out;
                config::save();
                keyCell->setDetailText(maskKey(out));
            },
            tr("Jackett API Key (optional)"), tr("Leave empty if disabled in Jackett"), 128,
            config::get().jackettApiKey);
        return true;
    });
    list->addView(keyCell);
    list->addView(caption(
        tr("The API Key shown in the top right corner of the Jackett web interface (leave empty if disabled).")));

    auto* testBtn = new AsyncButton();
    testBtn->setText(tr("Test connection"));
    testBtn->setMargins(14.0f, 20.0f, 18.0f, 20.0f);
    testBtn->registerClickAction([testBtn](brls::View*) {
        if (config::get().jackettUrl.empty())
        {
            note(tr("Please set Server URL first."));
            return true;
        }
        testBtn->setState(brls::ButtonState::DISABLED);
        testBtn->setText(tr("Testing..."));
        jackett::testConnectionAsync(
            config::get().jackettUrl, config::get().jackettApiKey,
            [testBtn, live = testBtn->alive](bool ok, const std::string& msg) {
                if (*live)
                {
                    testBtn->setState(brls::ButtonState::ENABLED);
                    testBtn->setText(tr("Test connection"));
                }
                // Either way the message says what was found or what to fix.
                note(ok ? msg : tr("Connection failed: ") + msg);
            });
        return true;
    });
    list->addView(testBtn);

    return pane;
}

brls::View* onlinePane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);

    auto* modeCell = new brls::SelectorCell();
    int curMode    = (config::get().defaultWatchMode == "online") ? 1
                   : (config::get().defaultWatchMode == "ask")    ? 2
                                                                  : 0;
    modeCell->init(tr("Default watch source"), { tr("Torrents"), tr("Online"), tr("Ask") }, curMode,
                   [](int sel) {
                       config::get().defaultWatchMode = (sel == 1) ? "online"
                                                      : (sel == 2) ? "ask"
                                                                   : "torrents";
                       config::save();
                   });
    list->addView(modeCell);
    list->addView(caption(tr(
        "Which source the Watch button and episode clicks open by default. "
        "Both Torrents and Online buttons remain available on every card.")));

    static const int kQualVals[]     = { 0, 2160, 1080, 720, 480 };
    std::vector<std::string> qLabels = { tr("Auto (best)"), "2160p", "1080p", "720p", "480p" };
    int curQ                         = 2;
    for (int i = 0; i < 5; i++)
        if (kQualVals[i] == config::get().onlinePreferQuality) curQ = i;
    auto* qCell = new brls::SelectorCell();
    qCell->init(tr("Preferred quality"), qLabels, curQ, [](int sel) {
        if (sel < 0 || sel >= 5) return;
        config::get().onlinePreferQuality = kQualVals[sel];
        config::save();
    });
    list->addView(qCell);

    auto* hdr = new brls::Header();
    hdr->setTitle(tr("Online plugins"));
    list->addView(hdr);

    auto* pluginsBox = new AsyncBox();
    pluginsBox->setAxis(brls::Axis::COLUMN);
    list->addView(pluginsBox);

    auto rebuildPlugins = std::make_shared<std::function<void()>>();
    auto live           = pluginsBox->alive;
    std::weak_ptr<std::function<void()>> weakRebuild = rebuildPlugins;

    *rebuildPlugins = [pluginsBox, modeCell, live, weakRebuild]() {
        if (!*live) return;
        auto selfRebuild = weakRebuild.lock();
        if (!selfRebuild) return;
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
        {
            if (v == pluginsBox)
            {
                brls::Application::giveFocus(modeCell);
                break;
            }
        }
        pluginsBox->clearViews();
        const auto& plugins = config::get().onlinePlugins;
        if (plugins.empty())
        {
            pluginsBox->addView(caption(tr(
                "No plugins added yet. Add a Lampa/Lampac server or plugin URL "
                "(e.g. https://app.bwa.ad/online.js or http://IP:9118) or an NX Online Plugin.")));
            return;
        }

        for (size_t i = 0; i < plugins.size(); i++)
        {
            const auto& p = plugins[i];
            auto* cell    = new brls::DetailCell();
            cell->setText(p.name.empty() ? p.url : p.name);
            std::string kindLabel = (p.kind == "nx_plugin") ? "NX Plugin" : "Lampa/Lampac";
            cell->setDetailText(kindLabel + " \xC2\xB7 " +
                                (p.enabled ? tr("Enabled") : tr("Disabled")));
            cell->setDetailTextColor(p.enabled ? theme::textGood() : theme::textMuted());

            cell->registerClickAction([i, live, weakRebuild](brls::View*) {
                if (i >= config::get().onlinePlugins.size()) return true;
                const auto cur                   = config::get().onlinePlugins[i];
                std::vector<std::string> actions = {
                    tr("Test connection"),
                    cur.enabled ? tr("Disable") : tr("Enable"),
                    tr("Edit token"),
                    tr("Rename"),
                    tr("Delete"),
                };
                auto* dd = new brls::Dropdown(
                    cur.name, actions, [](int) {}, -1,
                    [i, live, weakRebuild](int sel) {
                        if (!*live || i >= config::get().onlinePlugins.size()) return;
                        auto triggerRebuild = [weakRebuild]() {
                            if (auto fn = weakRebuild.lock()) (*fn)();
                        };
                        if (sel == 0)
                        {
                            auto cfgCopy = config::get().onlinePlugins[i];
                            brls::Application::notify(tr("Testing..."));
                            online::testPluginAsync(cfgCopy, [](bool ok, const std::string& msg) {
                                std::string txt = ok
                                    ? (std::string(tr("Connected")) + " (" + msg + ")")
                                    : (std::string(tr("Connection failed: ")) + msg);
                                note(txt);
                            });
                        }
                        else if (sel == 1)
                        {
                            config::get().onlinePlugins[i].enabled =
                                !config::get().onlinePlugins[i].enabled;
                            config::save();
                            triggerRebuild();
                        }
                        else if (sel == 2)
                        {
                            std::string curTok = config::get().onlinePlugins[i].token;
                            brls::Application::getImeManager()->openForText(
                                [i, live, triggerRebuild](std::string text) {
                                    if (!*live || i >= config::get().onlinePlugins.size()) return;
                                    config::get().onlinePlugins[i].token = text;
                                    config::save();
                                    triggerRebuild();
                                },
                                tr("Access token (optional)"), "", 128, curTok);
                        }
                        else if (sel == 3)
                        {
                            std::string curName = config::get().onlinePlugins[i].name;
                            brls::Application::getImeManager()->openForText(
                                [i, live, triggerRebuild](std::string text) {
                                    if (!*live || i >= config::get().onlinePlugins.size()) return;
                                    if (!text.empty())
                                    {
                                        config::get().onlinePlugins[i].name = text;
                                        config::save();
                                        triggerRebuild();
                                    }
                                },
                                tr("Plugin name"), "", 64, curName);
                        }
                        else if (sel == 4)
                        {
                            auto& vec = config::get().onlinePlugins;
                            vec.erase(vec.begin() + (ptrdiff_t)i);
                            config::save();
                            triggerRebuild();
                        }
                    });
                brls::Application::pushActivity(new brls::Activity(dd));
                return true;
            });
            pluginsBox->addView(cell);
        }
    };
    (*rebuildPlugins)();

    auto runProbeAndAdd = [live, rebuildPlugins](const std::string& url, const std::string& token) {
        brls::Application::notify(tr("Checking plugin..."));
        online::probePluginAsync(url, token, [live, rebuildPlugins](online::ProbeResult pr) {
            if (!pr.ok)
            {
                note(pr.error);
                return;
            }
            config::get().onlinePlugins.push_back(pr.plugin);
            config::save();
            if (*live) (*rebuildPlugins)();
            note(std::string(tr("Plugin added: ")) + pr.plugin.name + "\n" + pr.summary);
        });
    };

    auto* addBtn = new brls::Button();
    addBtn->setText(tr("+ Add plugin (Lampa / NX)"));
    addBtn->setMargins(10.0f, 0.0f, 12.0f, 0.0f);
    addBtn->registerClickAction([runProbeAndAdd](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [runProbeAndAdd](std::string url) {
                while (!url.empty() && std::isspace((unsigned char)url.front())) url.erase(0, 1);
                while (!url.empty() && std::isspace((unsigned char)url.back())) url.pop_back();
                if (url.empty()) return;

                brls::sync([runProbeAndAdd, url]() {
                    auto* d = new brls::Dialog(tr("Enter an access token if the plugin requires one:"));
                    d->addButton(tr("No token"), [runProbeAndAdd, url]() {
                        runProbeAndAdd(url, "");
                    });
                    d->addButton(tr("Enter token"), [runProbeAndAdd, url]() {
                        brls::Application::getImeManager()->openForText(
                            [runProbeAndAdd, url](std::string tok) {
                                runProbeAndAdd(url, tok);
                            },
                            tr("Access token (optional)"), "", 128, "");
                    });
                    d->open();
                });
            },
            tr("Plugin or server URL"), "https://app.bwa.ad/online.js", 256, "");
        return true;
    });
    list->addView(addBtn);

    list->addView(caption(tr(
        "Supports Lampa/Lampac servers and plugin URLs (e.g. https://app.bwa.ad/online.js, "
        "http://192.168.1.10:9118), as well as custom NX Online Plugin v1 endpoints. "
        "Browser-only DOM scripts (like online_mod.js) are not supported natively.")));
    return pane;
}

brls::View* syncPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto trimWs = [](std::string s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
            s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
            s.erase(0, 1);
        return s;
    };

    auto isDescendant = [](brls::View* v, brls::View* root) {
        for (brls::View* cur = v; cur != nullptr; cur = cur->getParent())
        {
            if (cur == root) return true;
        }
        return false;
    };

    struct SyncPaneCtx
    {
        brls::Box* dynamicBox        = nullptr;
        brls::SelectorCell* provCell = nullptr;
        std::shared_ptr<bool> alive  = std::make_shared<bool>(true);
        bool scheduled               = false;
        bool hasNote                 = false;
        std::string pendingNote;
        std::function<void()> rebuild;
    };

    class SyncBox : public brls::Box
    {
      public:
        explicit SyncBox(std::shared_ptr<SyncPaneCtx> c)
            : brls::Box(brls::Axis::COLUMN), ctx(std::move(c))
        {
        }
        ~SyncBox() override
        {
            if (ctx)
            {
                *ctx->alive  = false;
                ctx->rebuild = nullptr;
            }
        }
        std::shared_ptr<SyncPaneCtx> ctx;
    };

    auto* hdr = new brls::Header();
    hdr->setTitle(tr("Cloud synchronization"));
    list->addView(hdr);

    auto* providerCell = new brls::SelectorCell();
    std::vector<std::string> providerLabels = { tr("Disabled"), "Google Drive", "WebDAV" };
    int curProvIdx = (cfg.syncProvider == "gdrive") ? 1 :
                     (cfg.syncProvider == "webdav") ? 2 : 0;

    auto* autoSync = new brls::BooleanCell();
    autoSync->init(tr("Automatic sync"), cfg.syncAuto, [](bool on) {
        config::get().syncAuto = on;
        config::save();
    });

    auto ctx        = std::make_shared<SyncPaneCtx>();
    auto* dynamicBox = new SyncBox(ctx);
    ctx->dynamicBox = dynamicBox;
    ctx->provCell   = providerCell;
    auto live       = ctx->alive;

    auto scheduleRebuild = [ctx, isDescendant](const std::string& noteMsg) {
        if (!*ctx->alive) return;
        if (!noteMsg.empty())
        {
            ctx->pendingNote = noteMsg;
            ctx->hasNote     = true;
        }
        if (ctx->scheduled) return;
        ctx->scheduled = true;

        std::shared_ptr<std::function<void()>> pump = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weakPump = pump;
        *pump = [ctx, isDescendant, pump, weakPump]() {
            if (!*ctx->alive)
            {
                *pump = nullptr;
                return;
            }
            if (!ui::isOnTop(ctx->dynamicBox))
            {
                brls::delay(30, [weakPump]() {
                    if (auto p = weakPump.lock()) (*p)();
                });
                return;
            }
            ctx->scheduled = false;

            brls::View* cur = brls::Application::getCurrentFocus();
            if (cur && isDescendant(cur, ctx->dynamicBox))
                brls::Application::giveFocus(ctx->provCell);

            ctx->dynamicBox->clearViews();
            if (ctx->rebuild) ctx->rebuild();

            if (ctx->hasNote)
            {
                std::string msg = std::move(ctx->pendingNote);
                ctx->pendingNote.clear();
                ctx->hasNote = false;
                note(msg);
            }
            *pump = nullptr;
        };

        brls::sync([pump]() { (*pump)(); });
    };

    providerCell->init(
        tr("Service"), providerLabels, curProvIdx,
        [scheduleRebuild](int sel) {
            config::get().syncProvider = (sel == 1) ? "gdrive" : (sel == 2) ? "webdav" : "none";
            config::save();
            scheduleRebuild("");
        },
        [scheduleRebuild](int) {
            scheduleRebuild("");
        });

    list->addView(providerCell);
    list->addView(autoSync);
    list->addView(caption(tr("Sync bookmarks and history on startup and when finishing a video.")));
    list->addView(dynamicBox);

    ctx->rebuild = [dynamicBox, live, trimWs, scheduleRebuild]() {
        config::Config& c = config::get();

        if (c.syncProvider == "gdrive")
        {
            auto* ghdr = new brls::Header();
            ghdr->setTitle("Google Drive");
            dynamicBox->addView(ghdr);

            auto maskClientId = [](const std::string& id) -> std::string {
                if (id.empty())
                    return cloud_sync::GdriveProvider::hasBuiltinCredentials()
                               ? tr("Built-in (default)")
                               : tr("Not set");
                if (id.size() <= 16) return id;
                return id.substr(0, 8) + "..." + id.substr(id.size() - 8);
            };
            auto maskClientSecret = [](const std::string& sec) -> std::string {
                if (sec.empty())
                    return cloud_sync::GdriveProvider::hasBuiltinCredentials()
                               ? tr("Built-in (default)")
                               : tr("Not set");
                return "********";
            };

            auto* clientIdCell = new brls::DetailCell();
            clientIdCell->setText(tr("OAuth Client ID"));
            clientIdCell->setDetailText(maskClientId(c.syncGdriveClientId));
            clientIdCell->registerClickAction([clientIdCell, live, trimWs, maskClientId](brls::View*) {
                brls::Application::getImeManager()->openForText(
                    [clientIdCell, live, trimWs, maskClientId](std::string out) {
                        out = trimWs(out);
                        config::get().syncGdriveClientId = out;
                        config::save();
                        if (auto* gd = cloud_sync::Manager::instance().gdriveProvider())
                            gd->clearCachedToken();
                        if (*live)
                            clientIdCell->setDetailText(maskClientId(out));
                    },
                    tr("OAuth Client ID"), "1234567890-xxxx.apps.googleusercontent.com", 256,
                    config::get().syncGdriveClientId);
                return true;
            });
            dynamicBox->addView(clientIdCell);

            auto* clientSecretCell = new brls::DetailCell();
            clientSecretCell->setText(tr("OAuth Client Secret"));
            clientSecretCell->setDetailText(maskClientSecret(c.syncGdriveClientSecret));
            clientSecretCell->registerClickAction([clientSecretCell, live, trimWs, maskClientSecret](brls::View*) {
                brls::Application::getImeManager()->openForText(
                    [clientSecretCell, live, trimWs, maskClientSecret](std::string out) {
                        out = trimWs(out);
                        config::get().syncGdriveClientSecret = out;
                        config::save();
                        if (auto* gd = cloud_sync::Manager::instance().gdriveProvider())
                            gd->clearCachedToken();
                        if (*live)
                            clientSecretCell->setDetailText(maskClientSecret(out));
                    },
                    tr("OAuth Client Secret"), "GOCSPX-...", 128,
                    config::get().syncGdriveClientSecret);
                return true;
            });
            dynamicBox->addView(clientSecretCell);

            dynamicBox->addView(caption(tr(
                "Create a free OAuth 2.0 Client ID (TVs and Limited Input devices) in Google Cloud Console with Google Drive API enabled.")));

            if (!c.syncGdriveRefreshToken.empty())
            {
                auto* accCell = new brls::DetailCell();
                accCell->setText(tr("Account"));
                accCell->setDetailText(c.syncGdriveUserEmail.empty() ? tr("Connected") : c.syncGdriveUserEmail);
                dynamicBox->addView(accCell);

                auto* testBtn = new AsyncButton();
                testBtn->setText(tr("Test connection"));
                testBtn->setMargins(10.0f, 20.0f, 8.0f, 20.0f);
                testBtn->registerClickAction([testBtn](brls::View*) {
                    if (!cloud_sync::GdriveProvider::hasClientCredentials())
                    {
                        note(tr("Enter OAuth Client ID and Client Secret first (Google Cloud Console: TVs and Limited Input devices)."));
                        return true;
                    }
                    testBtn->setState(brls::ButtonState::DISABLED);
                    testBtn->setText(tr("Testing..."));
                    cloud_sync::testGdriveAsync([testBtn, btnLive = testBtn->alive](bool ok, const std::string& err) {
                        if (*btnLive)
                        {
                            testBtn->setState(brls::ButtonState::ENABLED);
                            testBtn->setText(tr("Test connection"));
                        }
                        note(ok ? tr("Google Drive connection successful!") : tr("Connection failed: ") + err);
                    });
                    return true;
                });
                dynamicBox->addView(testBtn);

                auto* logoutBtn = new brls::Button();
                logoutBtn->setText(tr("Sign out"));
                logoutBtn->setMargins(6.0f, 20.0f, 10.0f, 20.0f);
                logoutBtn->registerClickAction([scheduleRebuild](brls::View*) {
                    cloud_sync::Manager::instance().gdriveProvider()->disconnect();
                    scheduleRebuild(tr("Signed out of Google Drive"));
                    return true;
                });
                dynamicBox->addView(logoutBtn);
            }
            else
            {
                auto* loginBtn = new brls::Button();
                loginBtn->setText(tr("Sign in to Google Drive"));
                loginBtn->setMargins(10.0f, 20.0f, 10.0f, 20.0f);
                loginBtn->registerClickAction([live, scheduleRebuild](brls::View*) {
                    if (!cloud_sync::GdriveProvider::hasClientCredentials())
                    {
                        note(tr("Enter OAuth Client ID and Client Secret first (Google Cloud Console: TVs and Limited Input devices)."));
                        return true;
                    }
                    cloud_sync::showGoogleAuthDialog([live, scheduleRebuild](bool ok) {
                        if (!ok) return;
                        if (*live)
                            scheduleRebuild(tr("Google Drive connected successfully!"));
                        else
                            note(tr("Google Drive connected successfully!"));
                        cloud_sync::syncNow([live, scheduleRebuild](bool, const std::string&) {
                            if (*live) scheduleRebuild("");
                        });
                    });
                    return true;
                });
                dynamicBox->addView(loginBtn);
                dynamicBox->addView(caption(tr("Sign in with your phone or PC by scanning the QR code or entering the device code.")));
            }
        }
        else if (c.syncProvider == "webdav")
        {
            auto* whdr = new brls::Header();
            whdr->setTitle("WebDAV");
            dynamicBox->addView(whdr);

            auto* preset  = new brls::SelectorCell();
            auto* urlCell = new brls::DetailCell();

            std::vector<std::string> presets = { tr("Custom"), "Яндекс Диск", "Облако Mail.ru" };
            int curPreset = (c.syncWebdavUrl == "https://webdav.yandex.ru") ? 1 :
                            (c.syncWebdavUrl == "https://webdav.cloud.mail.ru") ? 2 : 0;
            preset->init(tr("Preset"), presets, curPreset, [urlCell, live](int sel) {
                if (sel == 1) config::get().syncWebdavUrl = "https://webdav.yandex.ru";
                else if (sel == 2) config::get().syncWebdavUrl = "https://webdav.cloud.mail.ru";
                config::save();
                if (*live)
                {
                    const std::string& u = config::get().syncWebdavUrl;
                    urlCell->setDetailText(u.empty() ? tr("Not set") : u);
                }
            });
            dynamicBox->addView(preset);

            urlCell->setText(tr("Server URL"));
            urlCell->setDetailText(c.syncWebdavUrl.empty() ? tr("Not set") : c.syncWebdavUrl);
            urlCell->registerClickAction([urlCell, preset, live](brls::View*) {
                brls::Application::getImeManager()->openForText(
                    [urlCell, preset, live](std::string out) {
                        out = cloud_sync::WebdavProvider::normalizeUrl(out);
                        config::get().syncWebdavUrl = out;
                        config::save();
                        if (*live)
                        {
                            urlCell->setDetailText(out.empty() ? tr("Not set") : out);
                            int p = (out == "https://webdav.yandex.ru") ? 1 :
                                    (out == "https://webdav.cloud.mail.ru") ? 2 : 0;
                            preset->setSelection(p, true);
                        }
                    },
                    tr("WebDAV Server URL"), "https://webdav.yandex.ru", 256,
                    config::get().syncWebdavUrl);
                return true;
            });
            dynamicBox->addView(urlCell);

            auto* userCell = new brls::DetailCell();
            userCell->setText(tr("Username / Email"));
            userCell->setDetailText(c.syncWebdavUser.empty() ? tr("Not set") : c.syncWebdavUser);
            userCell->registerClickAction([userCell, live, trimWs](brls::View*) {
                brls::Application::getImeManager()->openForText(
                    [userCell, live, trimWs](std::string out) {
                        out = trimWs(out);
                        config::get().syncWebdavUser = out;
                        config::save();
                        if (*live)
                            userCell->setDetailText(out.empty() ? tr("Not set") : out);
                    },
                    tr("Username / Email"), tr("Your cloud login"), 128,
                    config::get().syncWebdavUser);
                return true;
            });
            dynamicBox->addView(userCell);

            auto* passCell = new brls::DetailCell();
            passCell->setText(tr("App password"));
            passCell->setDetailText(c.syncWebdavPass.empty() ? tr("Not set") : "********");
            passCell->registerClickAction([passCell, live, trimWs](brls::View*) {
                brls::Application::getImeManager()->openForText(
                    [passCell, live, trimWs](std::string out) {
                        out = trimWs(out);
                        config::get().syncWebdavPass = out;
                        config::save();
                        if (*live)
                            passCell->setDetailText(out.empty() ? tr("Not set") : "********");
                    },
                    tr("App password / token"), tr("Generated in cloud security settings"), 128,
                    config::get().syncWebdavPass);
                return true;
            });
            dynamicBox->addView(passCell);
            dynamicBox->addView(caption(tr("For Yandex Disk or Mail.ru, create and use an App Password.")));

            auto* testBtn = new AsyncButton();
            testBtn->setText(tr("Test connection"));
            testBtn->setMargins(12.0f, 20.0f, 12.0f, 20.0f);
            testBtn->registerClickAction([testBtn](brls::View*) {
                if (!cloud_sync::WebdavProvider::snapshotConfig().valid())
                {
                    note(tr("WebDAV credentials not set"));
                    return true;
                }
                testBtn->setState(brls::ButtonState::DISABLED);
                testBtn->setText(tr("Testing..."));
                cloud_sync::testWebdavAsync([testBtn, btnLive = testBtn->alive](bool ok, const std::string& err) {
                    if (*btnLive)
                    {
                        testBtn->setState(brls::ButtonState::ENABLED);
                        testBtn->setText(tr("Test connection"));
                    }
                    note(ok ? tr("WebDAV connection successful!") : tr("Connection failed: ") + err);
                });
                return true;
            });
            dynamicBox->addView(testBtn);
        }

        if (c.syncProvider != "none")
        {
            auto* shdr = new brls::Header();
            shdr->setTitle(tr("Actions & Status"));
            dynamicBox->addView(shdr);

            auto* timeCell = new brls::DetailCell();
            timeCell->setText(tr("Last synced"));
            if (c.syncLastTime > 0)
            {
                char buf[64];
                std::time_t t = (std::time_t)c.syncLastTime;
                if (std::tm* tm = std::localtime(&t))
                {
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
                    timeCell->setDetailText(buf);
                }
                else
                {
                    timeCell->setDetailText(tr("Never"));
                }
            }
            else
            {
                timeCell->setDetailText(tr("Never"));
            }
            dynamicBox->addView(timeCell);

            if (!c.syncLastError.empty())
            {
                auto* errCell = new brls::DetailCell();
                errCell->setText(tr("Last error"));
                errCell->setDetailText(c.syncLastError);
                dynamicBox->addView(errCell);
            }

            auto* syncBtn = new AsyncButton();
            syncBtn->setText(tr("Sync now"));
            syncBtn->setMargins(14.0f, 20.0f, 20.0f, 20.0f);
            syncBtn->registerClickAction([syncBtn, live, scheduleRebuild](brls::View*) {
                syncBtn->setState(brls::ButtonState::DISABLED);
                syncBtn->setText(tr("Syncing..."));
                cloud_sync::syncNow([syncBtn, btnLive = syncBtn->alive, live, scheduleRebuild](bool ok, const std::string& msg) {
                    std::string resultMsg = ok ? tr("Synchronization complete!") : tr("Sync failed: ") + msg;
                    if (*btnLive)
                    {
                        syncBtn->setState(brls::ButtonState::ENABLED);
                        syncBtn->setText(tr("Sync now"));
                    }
                    if (*live)
                        scheduleRebuild(resultMsg);
                    else
                        note(resultMsg);
                });
                return true;
            });
            dynamicBox->addView(syncBtn);
        }
    };

    ctx->rebuild();

    return pane;
}

brls::View* networkPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto* hdr = new brls::Header();
    hdr->setTitle("TorrServer");
    list->addView(hdr);

    auto* tsCell = new brls::DetailCell();
    tsCell->setText(tr("Server URL"));
    tsCell->setDetailText(cfg.torrServerUrl.empty() ? tr("Off") : cfg.torrServerUrl);
    tsCell->registerClickAction([tsCell](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [tsCell](std::string out) {
                config::get().torrServerUrl = torrserver::normalizeBase(out);
                config::save();
                const std::string& v = config::get().torrServerUrl;
                tsCell->setDetailText(v.empty() ? tr("Off") : v);
            },
            tr("TorrServer URL"), tr("e.g. http://192.168.1.10:8090"), 256,
            config::get().torrServerUrl);
        return true;
    });
    list->addView(tsCell);
    list->addView(caption(
        platform_ui::isMobile()
            ? tr("When set, torrents are streamed by your TorrServer and played here as a plain video stream, instead of being downloaded by the device. Leave empty to use the built-in engine.")
            : tr("When set, torrents are streamed by your TorrServer and played here as "
                 "a plain video stream, instead of being downloaded by the console. "
                 "Leave empty to use the built-in engine.")));

    auto* tsTest = new AsyncButton();
    tsTest->setText(tr("Test connection"));
    tsTest->setMargins(14.0f, 20.0f, 18.0f, 20.0f);
    tsTest->registerClickAction([tsTest](brls::View*) {
        if (config::get().torrServerUrl.empty())
        {
            note(tr("Please set Server URL first."));
            return true;
        }
        tsTest->setState(brls::ButtonState::DISABLED);
        tsTest->setText(tr("Testing..."));
        torrserver::testConnectionAsync(
            config::get().torrServerUrl,
            [tsTest, live = tsTest->alive](bool ok, const std::string& msg) {
                if (*live)
                {
                    tsTest->setState(brls::ButtonState::ENABLED);
                    tsTest->setText(tr("Test connection"));
                }
                if (ok)
                    note(tr("Connected to TorrServer ") + msg);
                else
                    note(tr("Connection failed: ") + msg);
            });
        return true;
    });
    list->addView(tsTest);

    auto* phdr = new brls::Header();
    phdr->setTitle(tr("Proxy"));
    list->addView(phdr);

    auto* proxyCell = new brls::DetailCell();
    proxyCell->setText(tr("Proxy"));
    proxyCell->setDetailText(cfg.proxyUrl.empty() ? tr("Off") : cfg.proxyUrl);
    proxyCell->registerClickAction([proxyCell](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [proxyCell](std::string out) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
                config::get().proxyUrl = out;
                config::save();
                http::applyProxy();
                proxyCell->setDetailText(out.empty() ? tr("Off") : out);
            },
            tr("Proxy"), tr("e.g. http://host:3128 or socks5h://host:1080"), 256,
            config::get().proxyUrl);
        return true;
    });
    list->addView(proxyCell);
    list->addView(caption(
        tr("Used for catalogs, addons, posters and https video links. Servers on "
           "your local network (Jackett, TorrServer) are always reached directly.")));

    auto* thdr = new brls::Header();
    thdr->setTitle("TMDB");
    list->addView(thdr);

    auto maskKey = [](const std::string& k) -> std::string {
        if (k.empty()) return tr("Built-in (default)");
        if (k.size() <= 8) return "********";
        return k.substr(0, 4) + "..." + k.substr(k.size() - 4);
    };
    auto* tmdbKey = new brls::DetailCell();
    tmdbKey->setText(tr("API key"));
    tmdbKey->setDetailText(maskKey(cfg.tmdbApiKey));
    tmdbKey->registerClickAction([tmdbKey, maskKey](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [tmdbKey, maskKey](std::string out) {
                while (!out.empty() && out.back() == ' ') out.pop_back();
                config::get().tmdbApiKey = out;
                config::save();
                tmdbKey->setDetailText(maskKey(out));
            },
            tr("TMDB API key"), tr("API key (v3) or read access token (v4)"), 512,
            config::get().tmdbApiKey);
        return true;
    });
    list->addView(tmdbKey);

    auto* tmdbUrl = new brls::DetailCell();
    tmdbUrl->setText(tr("API address"));

    auto updateApiDetail = [tmdbUrl]() {
        // What tmdb:: will actually call, shorn of the scheme and the "/3".
        std::string base = tmdb::settings().apiBase;
        size_t s = base.find("://");
        if (s != std::string::npos) base.erase(0, s + 3);
        if (base.size() >= 2 && base.compare(base.size() - 2, 2, "/3") == 0)
            base.resize(base.size() - 2);
        tmdbUrl->setDetailText(base);
    };

    auto* tmdbProxy = new brls::BooleanCell();
    tmdbProxy->init(tr("TMDB proxy"), cfg.tmdbProxyEnabled, [updateApiDetail](bool on) {
        config::get().tmdbProxyEnabled = on;
        config::get().tmdbProxyUserSet = true;
        config::save();
        tmdb::clearCache();
        updateApiDetail();
    });
    list->addView(tmdbProxy);

    auto* tmdbProxyUrl = new brls::DetailCell();
    tmdbProxyUrl->setText(tr("Proxy address"));
    tmdbProxyUrl->setDetailText(cfg.tmdbProxyUrl.empty() ? tr("Built-in proxy") : cfg.tmdbProxyUrl);
    tmdbProxyUrl->registerClickAction([tmdbProxyUrl, updateApiDetail](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [tmdbProxyUrl, updateApiDetail](std::string out) {
                while (!out.empty() && (out.back() == ' ' || out.back() == '/')) out.pop_back();
                config::get().tmdbProxyUrl = out;
                config::save();
                tmdb::clearCache();
                tmdbProxyUrl->setDetailText(out.empty() ? tr("Built-in proxy") : out);
                updateApiDetail();
            },
            tr("Proxy address"), "https://my-worker.workers.dev", 256,
            config::get().tmdbProxyUrl);
        return true;
    });
    list->addView(tmdbProxyUrl);

    updateApiDetail();
    tmdbUrl->registerClickAction([tmdbUrl, updateApiDetail](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [tmdbUrl, updateApiDetail](std::string out) {
                while (!out.empty() && (out.back() == ' ' || out.back() == '/')) out.pop_back();
                config::get().tmdbApiUrl = out;
                config::save();
                updateApiDetail();
            },
            tr("TMDB API address"), "https://api.themoviedb.org/3", 256,
            config::get().tmdbApiUrl);
        return true;
    });
    list->addView(tmdbUrl);
    list->addView(caption(
        tr("The catalog on the Home tab comes from TMDB. The proxy routes API requests "
           "and posters through Cloudflare Worker or mirror, which allows access in Russia. "
           "Leave empty to use the built-in proxy. Changes apply when you reopen the Home tab.")));

    auto* tmdbTest = new AsyncButton();
    tmdbTest->setText(tr("Test connection"));
    tmdbTest->setMargins(14.0f, 20.0f, 6.0f, 20.0f);
    tmdbTest->registerClickAction([tmdbTest](brls::View*) {
        if (!tmdb::configured())
        {
            note(tr("Enter the TMDB API key first."));
            return true;
        }
        tmdbTest->setState(brls::ButtonState::DISABLED);
        tmdbTest->setText(tr("Testing..."));
        // A cleared cache makes it a real request rather than a stored answer:
        // the request goes out once the clearing is done.
        tmdb::clearCache([tmdbTest, live = tmdbTest->alive]() {
            tmdb::fetchRowAsync(tmdb::Row::Trending, [tmdbTest, live](tmdb::ListResult r) {
                if (*live)
                {
                    tmdbTest->setState(brls::ButtonState::ENABLED);
                    tmdbTest->setText(tr("Test connection"));
                }
                if (r.ok && !r.items.empty())
                    note(tr("Connection successful!"));
                else
                    note(tr("Connection failed: ") +
                         (r.error.empty() ? tr("empty answer") : r.error));
            });
        });
        return true;
    });
    list->addView(tmdbTest);

    auto* tmdbClear = new brls::Button();
    tmdbClear->setText(tr("Clear TMDB cache"));
    tmdbClear->setMargins(6.0f, 20.0f, 18.0f, 20.0f);
    tmdbClear->registerClickAction([](brls::View*) {
        tmdb::clearCache();
        note(tr("TMDB cache cleared."));
        return true;
    });
    list->addView(tmdbClear);

    return pane;
}

brls::View* aboutPane()
{
    brls::Box* list = nullptr;
    auto* pane      = newPane(&list);
    config::Config& cfg = config::get();

    auto* version = new brls::Label();
    version->setText(
        update::hasPending()
            ? tr("Version ") + std::string(APP_VERSION) +
                  tr(" \xE2\x80\x94 an update is installed, restart to use it")
            : tr("Version ") + std::string(APP_VERSION));
    version->setFontSize(20.0f);
    version->setTextColor(theme::text());
    version->setMargins(4.0f, 20.0f, 16.0f, 20.0f);
    list->addView(version);

#ifdef __SWITCH__
    auto* checkUpd = new brls::BooleanCell();
    checkUpd->init(tr("Check for updates on startup"), cfg.checkUpdates, [](bool on) {
        config::get().checkUpdates = on;
        config::save();
    });
    list->addView(checkUpd);
#endif

    auto* changelog = new brls::Button();
    changelog->setText(tr("View changelog"));
    changelog->setMargins(14.0f, 20.0f, 8.0f, 20.0f);
    changelog->registerClickAction([](brls::View*) {
        // showChangelog raises its own blocking spinner while it loads, so the
        // user cannot re-press this or leave Options mid-fetch.
        update::showChangelog(APP_VERSION);
        return true;
    });
    list->addView(changelog);

    auto* checkNow = new AsyncButton();
    checkNow->setText(tr("Check now"));
    checkNow->setMargins(4.0f, 20.0f, 18.0f, 20.0f);
    checkNow->registerClickAction([checkNow](brls::View*) {
        checkNow->setState(brls::ButtonState::DISABLED);
        checkNow->setText(tr("Checking..."));
        update::checkAsync([checkNow, live = checkNow->alive](update::Release r) {
            if (*live)
            {
                checkNow->setState(brls::ButtonState::ENABLED);
                checkNow->setText(tr("Check now"));
            }
            // Asked for explicitly, so unlike the startup check this one says
            // something either way.
            if (!r.ok)
                note(tr("Could not check for updates: ") + r.error);
            else if (!r.newer)
                note(tr("You are on the latest version (") +
                     std::string(APP_VERSION) + ").");
            else if (!r.installable())
            {
#if !defined(__SWITCH__)
                note(tr("Version ") + r.version +
                     tr(" is available on GitHub."));
#else
                // Newer, but nothing we can install: say so rather than claim
                // this is the latest.
                note(tr("Version ") + r.version +
                     tr(" is out, but that release has no .nro to install. "
                        "Get it from GitHub."));
#endif
            }
            else
                update::promptInstall(r);
        });
        return true;
    });
    list->addView(checkNow);

    auto* diagHdr = new brls::Header();
    diagHdr->setTitle(tr("Diagnostics"));
    list->addView(diagHdr);

    auto* logging = new brls::BooleanCell();
    logging->init(tr("Log file"), cfg.logging, [](bool on) {
        config::get().logging = on;
        config::save();
    });
    list->addView(logging);
    list->addView(caption(
        platform_ui::isMobile()
            ? tr("The log is written to app storage continuously. Turn it on to diagnose "
                 "a problem, then restart the app.")
            : tr("The log is written to the SD card continuously. Turn it on to diagnose "
                 "a problem, then restart the app.")));

    return pane;
}

class MobileSettingsView : public brls::Box
{
  public:
    MobileSettingsView()
    {
        setAxis(brls::Axis::COLUMN);
        setGrow(1.0f);

        struct TabDef
        {
            std::string label;
            std::function<brls::View*()> create;
        };
        std::vector<TabDef> defs = {
            { tr("General"),   [] { return generalPane(); } },
            { tr("Playback"),  [] { return playbackPane(); } },
            { tr("Streaming"), [] { return streamingPane(); } },
            { "Stremio",       [] { return stremioPane(); } },
            { "Jackett",       [] { return jackettPane(); } },
            { tr("Online"),    [] { return onlinePane(); } },
            { tr("Sync"),      [] { return syncPane(); } },
            { tr("Network"),   [] { return networkPane(); } },
            { tr("About"),     [] { return aboutPane(); } },
        };

        tabRow = new brls::Box();
        float inset = platform_ui::sideInset();
        tabRow->setPadding(8.0f, inset, 6.0f, inset);

        for (size_t i = 0; i < defs.size(); i++)
        {
            creators.push_back(std::move(defs[i].create));
            int idx = (int)i;
            auto* b = ui::actionButton(defs[i].label, i == 0, [this, idx]() {
                selectTab(idx);
            });
            tabRow->addView(b);
            tabBtns.push_back(b);
        }
        addView(ui::pillStrip(tabRow, 56.0f));

        auto* sep = new brls::Rectangle();
        sep->setHeight(1.0f);
        sep->setColor(theme::scrim(22));
        addView(sep);

        paneHost = new brls::Box();
        paneHost->setAxis(brls::Axis::COLUMN);
        paneHost->setGrow(1.0f);
        addView(paneHost);

        selectTab(0);

        resizeSub = brls::Application::getWindowSizeChangedEvent()->subscribe(
            [this]() { updatePadding(); });
        resizeSubscribed = true;
    }

    ~MobileSettingsView() override
    {
        if (resizeSubscribed)
        {
            brls::Application::getWindowSizeChangedEvent()->unsubscribe(resizeSub);
            resizeSubscribed = false;
        }
    }

  private:
    brls::Box* tabRow = nullptr;
    brls::Box* paneHost = nullptr;
    std::vector<brls::Button*> tabBtns;
    std::vector<std::function<brls::View*()>> creators;
    brls::VoidEvent::Subscription resizeSub;
    bool resizeSubscribed = false;

    void selectTab(int idx)
    {
        if (idx < 0 || idx >= (int)creators.size()) return;
        for (size_t i = 0; i < tabBtns.size(); i++)
            ui::setPillActive(tabBtns[i], (int)i == idx);
        ui::safeClearViews(paneHost, tabBtns[idx]);
        if (brls::View* v = creators[idx]())
        {
            v->setGrow(1.0f);
            paneHost->addView(v);
        }
    }

    void updatePadding()
    {
        float inset = platform_ui::sideInset();
        if (tabRow) tabRow->setPadding(8.0f, inset, 6.0f, inset);
        float paneInset = platform_ui::isPortrait() ? 16.0f : 32.0f;
        if (paneHost && !paneHost->getChildren().empty())
        {
            if (auto* sf = dynamic_cast<brls::ScrollingFrame*>(paneHost->getChildren()[0]))
            {
                if (!sf->getChildren().empty())
                {
                    if (auto* b = dynamic_cast<brls::Box*>(sf->getChildren()[0]))
                        b->setPadding(14.0f, paneInset, 40.0f, paneInset);
                }
            }
        }
    }
};

}  // namespace

void setUiScaleHook(std::function<void()> fn) { uiScaleHook = std::move(fn); }
void setOpenStremioHook(std::function<void()> fn) { openStremioHook = std::move(fn); }

void applyUiScale()
{
    if (platform_ui::isMobile())
    {
        int winW = brls::Application::windowWidth;
        int winH = brls::Application::windowHeight;
        float density = androidDisplayDensity();
        if (density < 0.75f) density = 1.0f;
#if !defined(__ANDROID__) && !defined(ANDROID)
        // Desktop NX_MOBILE_UI=1 test mode has density 1.0; simulate ~2.5x phone density.
        if (winW <= 900 || winH <= 900) density = 2.5f;
#endif
        double scaleFactor = 1280.0 / (double)std::max(640, config::get().handheldUiWidth);
        double widthDp     = (winW > 0) ? (double)winW / density : 400.0;
        double heightDp    = (winH > 0) ? (double)winH / density : 700.0;

        uint32_t w, h;
        if (winH > winW && winW > 0)
        {
            // Portrait phone / tablet: compute logical width from dp so text and
            // touch targets (~44-48dp) match native Android proportions (~35-45% larger
            // than the old 720-800px cap).
            double targetW = platform_ui::isTablet()
                                 ? std::clamp(widthDp * 1.15, 680.0, 840.0)
                                 : std::clamp(widthDp * 1.28, 460.0, 560.0);
            w = (uint32_t)std::clamp(std::lround(targetW / scaleFactor), 360L, 1800L);
            h = (uint32_t)std::ceil((double)w * winH / winW);
        }
        else if (winW > 0 && winH > 0)
        {
            // Landscape phone (e.g. 20:9) or tablet: drive vertical logical pixels
            // from physical height dp rather than forcing 720p onto a 6" phone.
            double targetH = platform_ui::isTablet()
                                 ? std::clamp(heightDp * 1.15, 640.0, 740.0)
                                 : std::clamp(heightDp * 1.35, 500.0, 580.0);
            uint32_t hScaled = (uint32_t)std::clamp(std::lround(targetH / scaleFactor), 380L, 1600L);
            w = (uint32_t)std::clamp((long)std::ceil((double)hScaled * winW / winH), 640L, 3200L);
            h = (uint32_t)std::ceil((double)w * winH / winW);
        }
        else
        {
            w = 1120;
            h = 630;
        }

        if (brls::Application::ORIGINAL_WINDOW_WIDTH == w &&
            brls::Application::ORIGINAL_WINDOW_HEIGHT == h)
            return;

        brls::Application::ORIGINAL_WINDOW_WIDTH  = w;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = h;
    }
#ifdef __SWITCH__
    else if (platform_ui::isTv())
    {
        int winW = brls::Application::windowWidth;
        int winH = brls::Application::windowHeight;
        uint32_t w = (uint32_t)std::clamp(config::get().handheldUiWidth, 960, 1920);
        uint32_t h = (winW > 0 && winH > 0)
                         ? (uint32_t)std::ceil((double)w * winH / winW)
                         : (w * 9 / 16);

        if (brls::Application::ORIGINAL_WINDOW_WIDTH == w &&
            brls::Application::ORIGINAL_WINDOW_HEIGHT == h)
            return;

        brls::Application::ORIGINAL_WINDOW_WIDTH  = w;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = h;
    }
    else
    {
        bool docked = brls::Application::windowWidth >= 1920;
        uint32_t w  = (uint32_t)(docked ? config::get().dockedUiWidth
                                        : config::get().handheldUiWidth);
        uint32_t h  = w * 9 / 16;  // every offered width is 16:9, so this is exact

        if (brls::Application::ORIGINAL_WINDOW_WIDTH == w &&
            brls::Application::ORIGINAL_WINDOW_HEIGHT == h)
            return;

        brls::Application::ORIGINAL_WINDOW_WIDTH  = w;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = h;
    }
#else
    else
    {
        // Android TV / Desktop PC: single clean UI scale setting (handheldUiWidth)
        // preserving display aspect ratio on ultra-wide or non-16:9 monitors.
        int winW = brls::Application::windowWidth;
        int winH = brls::Application::windowHeight;
        uint32_t w = (uint32_t)std::clamp(config::get().handheldUiWidth, 960, 2560);
        uint32_t h = (winW > 0 && winH > 0)
                         ? (uint32_t)std::ceil((double)w * winH / winW)
                         : (w * 9 / 16);

        if (brls::Application::ORIGINAL_WINDOW_WIDTH == w &&
            brls::Application::ORIGINAL_WINDOW_HEIGHT == h)
            return;

        brls::Application::ORIGINAL_WINDOW_WIDTH  = w;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = h;
    }
#endif

    // Recomputes windowScale/contentWidth and relayouts the activity stack.
    // Safe to call from the window-size-changed listener: that event is fired by
    // onWindowResized *after* its own setWindowSize call, and setWindowSize does
    // not fire it again -- so this is one extra layout pass, not a loop, and it
    // lands before the next frame is drawn.
    brls::Application::setWindowSize(brls::Application::windowWidth,
                                     brls::Application::windowHeight);

    // ... and because it does not fire it again, anything that keys off the UI
    // size has to be told here. Editing the setting in Options reaches the
    // header this way and no other.
    if (uiScaleHook) uiScaleHook();
}

SettingsActivity::~SettingsActivity()
{
    if (resizeSubscribed)
    {
        brls::Application::getWindowSizeChangedEvent()->unsubscribe(resizeSub);
        resizeSubscribed = false;
    }
}

brls::View* SettingsActivity::createContentView()
{
    if (platform_ui::isMobile())
    {
        auto* frame = new ui::FieldFrame();
        frame->pushContentView(new MobileSettingsView());
        frame->setTitle(tr("Options"));
        return frame;
    }

    // A sidebar with a pane per category, rather than the one long scroll this
    // used to be. Twenty-odd cells with a prose caption under half of them ran
    // to several screens, and the section headers went past too fast to serve
    // as landmarks -- you had to remember roughly how far down a setting was.
    // The sidebar names the five groups up front and each pane now fits in a
    // screen or close to it.
    //
    // TabFrame keeps only the visible tab alive: every pane below is rebuilt
    // from config each time it is entered, which is also why none of them can
    // hold a pointer into another.
    auto* tabs = new brls::TabFrame();

    tabs->addTab(tr("General"), [] { return generalPane(); });
    tabs->addTab(tr("Playback"), [] { return playbackPane(); });
    tabs->addTab(tr("Streaming"), [] { return streamingPane(); });
    tabs->addTab("Stremio", [] { return stremioPane(); });
    tabs->addTab("Jackett", [] { return jackettPane(); });
    tabs->addTab(tr("Online"), [] { return onlinePane(); });
    tabs->addTab(tr("Sync"), [] { return syncPane(); });
    tabs->addTab(tr("Network"), [] { return networkPane(); });
    tabs->addTab(tr("About"), [] { return aboutPane(); });

    brls::View* sidebar = tabs->getView("brls/tab_frame/sidebar");
    auto updateSidebar = [sidebar]() {
        if (!sidebar) return;
        bool portrait = brls::Application::contentHeight > brls::Application::contentWidth;
        sidebar->setWidth(portrait ? 200.0f : 410.0f);
    };
    updateSidebar();
    resizeSub = brls::Application::getWindowSizeChangedEvent()->subscribe(updateSidebar);
    resizeSubscribed = true;

    auto* frame = new ui::FieldFrame();
    frame->pushContentView(tabs);
    // After pushContentView: it overwrites the title with the content view's.
    frame->setTitle(tr("Options"));
    return frame;
}

brls::View* AccountActivity::createContentView()
{
    // Was a header, a line of text and a button. It is the screen behind the
    // header's profile button, so it should answer the questions you press that
    // button to ask: who is signed in, how much is on the account, and what the
    // addons -- which everything the Stremio side does depends on -- actually
    // provide.
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setGrow(1.0f);
    root->setPadding(24.0f, 0.0f, 0.0f, 0.0f);

    std::string key   = stremio::loadAuthKey();
    std::string email = stremio::loadEmail();

    if (key.empty())
    {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setGrow(1.0f);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->setPadding(0.0f, 28.0f, 0.0f, 28.0f);

        auto* title = new brls::Label();
        title->setText(tr("Not signed in"));
        title->setFontSize(28.0f);
        title->setTextColor(theme::text());
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        box->addView(title);

        auto* sub = new brls::Label();
        sub->setText(platform_ui::isMobile()
                         ? tr("Sign in to Stremio to sync your Stremio library and addons.")
                         : tr("The sign-in form is on the Stremio tab."));
        sub->setFontSize(18.0f);
        sub->setTextColor(theme::textMuted());
        sub->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        sub->setMarginTop(10.0f);
        box->addView(sub);

        auto* goBtn = ui::actionButton(tr("Go to Stremio"), true, []() {
            brls::Application::popActivity();
            if (openStremioHook) brls::sync([]() { openStremioHook(); });
        });
        goBtn->setMarginTop(24.0f);
        goBtn->setMarginRight(0.0f);
        box->addView(goBtn);

        root->addView(box);

        auto* frame = new ui::FieldFrame();
        frame->pushContentView(root);
        frame->setTitle(tr("Account"));
        return frame;
    }

    float inset = platform_ui::sideInset();

    // ---- identity ---------------------------------------------------------
    auto* head = new brls::Box();
    head->setAxis(brls::Axis::ROW);
    head->setAlignItems(brls::AlignItems::CENTER);
    head->setMargins(0.0f, inset, 28.0f, inset);

    // A monogram rather than an avatar: the API gives us no picture, and a
    // letter in the accent reads as an account at a glance where a generic
    // person icon would just be furniture.
    auto* mono = new brls::Box();
    mono->setWidth(84.0f);
    mono->setHeight(84.0f);
    mono->setCornerRadius(42.0f);
    mono->setBackgroundColor(theme::accent());
    mono->setJustifyContent(brls::JustifyContent::CENTER);
    mono->setAlignItems(brls::AlignItems::CENTER);
    mono->setShrink(0.0f);
    {
        auto* l = new brls::Label();
        std::string c = email.empty() ? "?" : std::string(1, email[0]);
        if (!c.empty()) c[0] = (char)std::toupper((unsigned char)c[0]);
        l->setText(c);
        l->setFontSize(40.0f);
        l->setTextColor(nvgRGB(255, 255, 255));
        mono->addView(l);
    }
    head->addView(mono);

    auto* who = new brls::Box();
    who->setAxis(brls::Axis::COLUMN);
    who->setGrow(1.0f);
    who->setMarginLeft(24.0f);
    {
        auto* addr = new brls::Label();
        // The address is only kept so this screen can name the account -- the
        // API has no use for it once there is an authKey.
        addr->setText(email.empty() ? (platform_ui::isMobile() ? tr("this device") : tr("this console")) : email);
        addr->setFontSize(28.0f);
        addr->setTextColor(theme::text());
        addr->setSingleLine(true);
        who->addView(addr);

        auto* sub = new brls::Label();
        sub->setText(tr("Signed in to Stremio"));
        sub->setFontSize(17.0f);
        sub->setTextColor(theme::textMuted());
        sub->setMarginTop(4.0f);
        who->addView(sub);
    }
    head->addView(who);
    root->addView(head);

    // ---- the two figures --------------------------------------------------
    auto* stats = new brls::Box();
    stats->setAxis(brls::Axis::ROW);
    stats->setMargins(0.0f, inset, 26.0f, inset);

    // Returns the value label, so a count that is not known yet can be filled
    // in when it lands.
    auto stat = [&](const std::string& name, const std::string& value) {
        auto* card = new brls::Box();
        card->setAxis(brls::Axis::COLUMN);
        card->setWidth(platform_ui::isMobile() ? 160.0f : 200.0f);
        card->setPadding(18.0f, 20.0f, 18.0f, 22.0f);
        card->setCornerRadius(10.0f);
        card->setBackgroundColor(theme::surface());
        card->setMarginRight(16.0f);

        auto* v = new brls::Label();
        v->setText(value);
        v->setFontSize(34.0f);
        v->setTextColor(theme::text());
        card->addView(v);

        auto* n = new brls::Label();
        n->setText(name);
        n->setFontSize(15.0f);
        n->setTextColor(theme::textMuted());
        n->setMarginTop(2.0f);
        card->addView(n);

        stats->addView(card);
        return v;
    };

    int libN = stremio::libraryCount();
    stat(tr("In library"), libN < 0 ? "\xE2\x80\x94" : std::to_string(libN));
    brls::Label* addonCount = stat(tr("Addons"), "\xE2\x80\x94");
    root->addView(stats);

    // ---- the addons -------------------------------------------------------
    auto* addonHdr = new brls::Header();
    addonHdr->setTitle(tr("Installed addons"));
    addonHdr->setMargins(0.0f, inset, 0.0f, inset);
    root->addView(addonHdr);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    auto* addonList = new AsyncBox();
    addonList->setAxis(brls::Axis::COLUMN);
    addonList->setPadding(6.0f, inset, 20.0f, inset);
    scroll->setContentView(addonList);
    root->addView(scroll);

    auto* pending = new brls::Label();
    pending->setText(tr("Reading the account's addons..."));
    pending->setFontSize(16.0f);
    pending->setTextColor(theme::textMuted());
    pending->setMarginTop(8.0f);
    addonList->addView(pending);

    // Free after the first call of the session, so this is normally instant.
    auto live = addonList->alive;
    stremio::fetchAddonsAsync(key, [addonList, live, addonCount,
                                    pending](stremio::AddonsResult r) {
        if (!*live) return;
        pending->setVisibility(brls::Visibility::GONE);
        if (!r.ok)
        {
            auto* err = new brls::Label();
            err->setText(tr("Could not read them: ") + r.error);
            err->setFontSize(16.0f);
            err->setTextColor(theme::textWarn());
            addonList->addView(err);
            return;
        }
        addonCount->setText(std::to_string(r.addons.size()));

        for (const auto& a : r.addons)
        {
            // Focusable, with nothing to activate: this is a list to read,
            // and a ScrollingFrame scrolls by moving focus through its children
            // -- with none of them focusable there was nothing for the stick to
            // move to and the list could only be dragged by touch.
            auto* row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            row->setAlignItems(brls::AlignItems::CENTER);
            row->setPadding(14.0f, 20.0f, 14.0f, 20.0f);
            row->setMarginBottom(8.0f);
            row->setCornerRadius(8.0f);
            row->setBackgroundColor(theme::scrim(14));
            row->setFocusable(true);
            row->setHighlightCornerRadius(8.0f);

            auto* name = new brls::Label();
            name->setText(a.name);
            name->setFontSize(19.0f);
            name->setTextColor(theme::text());
            name->setSingleLine(true);
            name->setGrow(1.0f);
            name->setMarginRight(20.0f);
            row->addView(name);

            // What it actually serves, which is the only thing about an addon
            // that changes what the app can do with it. An addon on the
            // blocklist keeps whatever else it provides -- only its streams are
            // ignored -- so that is said of the streams, not of the addon.
            bool streamsOff = a.hasStream && stremio::isStreamAddonHidden(a.name);
            std::string what;
            auto add = [&](const char* s) {
                if (!what.empty()) what += "  \xC2\xB7  ";
                what += s;
            };
            if (a.hasMeta) add(tr("Metadata"));
            if (a.hasStream) add(streamsOff ? tr("Streams (disabled)") : tr("Streams"));
            if (a.hasSubtitles) add(tr("Subtitles"));
            if (what.empty()) what = tr("Nothing this app uses");

            bool anyUsable =
                a.hasMeta || a.hasSubtitles || (a.hasStream && !streamsOff);
            if (!anyUsable) name->setTextColor(theme::textDim());

            auto* kind = new brls::Label();
            kind->setText(what);
            kind->setFontSize(16.0f);
            kind->setTextColor(anyUsable ? theme::textDim()
                                         : theme::textFaint());
            kind->setSingleLine(true);
            kind->setShrink(0.0f);
            row->addView(kind);

            addonList->addView(row);
        }

        if (r.addons.empty())
        {
            auto* none = new brls::Label();
            none->setText(tr("None. Install them from Stremio on another device."));
            none->setFontSize(16.0f);
            none->setTextColor(theme::textMuted());
            addonList->addView(none);
        }
    });

    // ---- sign out ---------------------------------------------------------
    auto* logout = new brls::Button();
    logout->setText(tr("Sign out of Stremio"));
    logout->setMargins(4.0f, inset, 24.0f, inset);
    logout->registerClickAction([logout](brls::View*) {
        stremio::clearAuthKey();
        // The addon collection -- and every meta answer cached behind it --
        // belongs to that account.
        stremio::clearAddonCache();
        // The tab holds the key in memory and is only rebuilt when it is
        // re-entered, so say what actually has to happen.
        note(tr("Signed out. Restart the app to get back to the sign-in screen."));
        logout->setState(brls::ButtonState::DISABLED);
        return true;
    });
    root->addView(logout);

    auto* frame = new ui::FieldFrame();
    frame->pushContentView(root);
    frame->setTitle(tr("Account"));
    return frame;
}
