#include "picture.hpp"

#include <borealis.hpp>

#include <switch.h>  // appletGetOperationMode

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef ABSOLUTE
#undef ABSOLUTE
#endif
#ifdef RELATIVE
#undef RELATIVE
#endif

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <cstdio>
#include <string>
#include <sys/stat.h>

#include "appdata.hpp"
#include "config.hpp"
#include "fsutil.hpp"
#include "i18n.hpp"
#include "player.hpp"  // renderMpvFrame
#include "player_widgets.hpp"
#include "theme.hpp"

namespace picture
{

namespace
{

const char* const kMpvNames[PROP_COUNT] = { "brightness", "contrast", "gamma",
                                            "saturation" };

} // namespace

std::string label(int prop)
{
    switch (prop)
    {
        case BRIGHTNESS: return tr("Brightness");
        case CONTRAST:   return tr("Contrast");
        case GAMMA:      return tr("Gamma");
        default:         return tr("Saturation");
    }
}

int& value(int prop)
{
    config::Config& c = config::get();
    switch (prop)
    {
        case BRIGHTNESS: return c.picBrightness;
        case CONTRAST:   return c.picContrast;
        case GAMMA:      return c.picGamma;
        default:         return c.picSaturation;
    }
}

std::string valueText(int v)
{
    char b[16];
    if (v == 0)
        std::snprintf(b, sizeof(b), "0");
    else
        std::snprintf(b, sizeof(b), "%+d", v);
    return b;
}

bool step(int prop, int dir)
{
    int& v = value(prop);
    int n  = v + dir;
    if (n < -100 || n > 100) return false;
    v = n;
    return true;
}

bool handheld()
{
    // A cached read of what the applet message loop last heard, not an IPC
    // call (see audioBoostWanted in player.cpp): fine per frame.
    return appletGetOperationMode() == AppletOperationMode_Handheld;
}

namespace
{
// Saturation through a user shader of ours, not mpv's equalizer: that one
// scales only a YCbCr source's chroma while converting it, so on an RGB
// picture -- the patterns, the hardware decoder's frames -- -30 and +30 looked
// the same. The shader works on the final image (the OUTPUT hook: at the
// screen's size, no extra pass over a 4K frame) and is loaded only while the
// value is not 0.
//
// One file per value, with the factor written into it: libmpv renders with
// vo_gpu, which knows no //!PARAM (that is gpu-next), and caches a shader by
// its path for the renderer's life -- rewriting one file would not be seen.
// They are a few hundred bytes, written once and reused.
std::string saturationShader(int v)
{
    char name[48];
    std::snprintf(name, sizeof(name), "/sat_%d.glsl", 100 + v);
    std::string path = std::string(APPDATA_SHADERS) + name;
    std::string have;
    if (fsutil::readFile(path, have) && fsutil::looksComplete(have)) return path;
    char body[640];
    std::snprintf(body, sizeof(body),
                  "// NX Torrent Player: saturation %+d (app/picture.cpp)\n"
                  "//!HOOK OUTPUT\n"
                  "//!BIND HOOKED\n"
                  "//!DESC saturation\n"
                  "vec4 hook()\n"
                  "{\n"
                  "    vec4 c = HOOKED_tex(HOOKED_pos);\n"
                  "    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
                  "    c.rgb = clamp(mix(vec3(l), c.rgb, %.3f), 0.0, 1.0);\n"
                  "    return c;\n"
                  "}\n",
                  v, 1.0 + v / 100.0);
    mkdir(APPDATA_SHADERS, 0777);
    if (!fsutil::writeAtomic(path, body)) return "";
    return path;
}

void applySaturation(mpv_handle* mpv, int want)
{
    // change-list, not a plain set: a set splits the list on ':' (and on the
    // console every path has one, "sdmc:/").
    const char* clr[] = { "change-list", "glsl-shaders", "clr", "", nullptr };
    mpv_command(mpv, clr);
    if (want == 0) return;
    std::string path = saturationShader(want);
    const char* add[] = { "change-list", "glsl-shaders", "append", path.c_str(), nullptr };
    if (path.empty() || mpv_command(mpv, add) < 0)
        brls::Logger::error("[picture] saturation shader did not load ({})", path);
}
} // namespace

void sync(mpv_handle* mpv, Eq& applied)
{
    if (!mpv) return;
    const bool on = handheld();
    for (int p = 0; p < PROP_COUNT; p++)
    {
        int want = on ? value(p) : 0;
        if (want == applied.v[p]) continue;
        if (p == SATURATION)
            applySaturation(mpv, want);
        else
        {
            char s[8];
            std::snprintf(s, sizeof(s), "%d", want);
            mpv_set_property_string(mpv, kMpvNames[p], s);
        }
        applied.v[p] = want;
    }
}

// ---------------------------------------------------------------------------
// The calibration screen.

namespace
{

void* getProcAddress(void*, const char* name)
{
    glfwGetCurrentContext();
    return (void*)glfwGetProcAddress(name);
}

// The test patterns (assets/patterns, made by tools/make_patterns.py). Each is
// 1280x720 -- the handheld screen's own size, so with the panel hidden a
// pattern is shown pixel for pixel, which the gamma chart's 1-px stripes need.
struct Pattern
{
    const char* file;
    const char* name;
    const char* hint;
};

const Pattern kPatterns[] = {
    { APP_RES("patterns/black.png"), "Black level",
      "Bars 1 to 8 are 1% to 8% grey on black. Raise Brightness until bar 2 is "
      "just visible; lower it if the black background turns grey." },
    { APP_RES("patterns/white.png"), "White level",
      "Bars 90 to 99 are on 100% white. Lower Contrast until the bars up to 98 "
      "stand apart from the background." },
    { APP_RES("patterns/greyscale.png"), "Grey scale",
      "Every step should be distinct and a neutral grey. Dark steps merging: "
      "raise Gamma. Light steps merging: lower Contrast." },
    { APP_RES("patterns/gamma.png"), "Gamma",
      "Hide the panel and look from arm's length: the patch that blends into "
      "its stripes is the screen's gamma. Adjust Gamma until it is 2.2." },
    { APP_RES("patterns/colour.png"), "Colour",
      "Bars at 75% and 100%, and skin tones. Set Saturation so faces look "
      "natural, neither orange nor pale." },
};
constexpr int kPatternCount = (int)(sizeof(kPatterns) / sizeof(kPatterns[0]));

constexpr float kPanelWidth = 440.0f;

class CalibrationView : public brls::Box
{
  public:
    CalibrationView();
    ~CalibrationView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    bool startMpv();
    void load(int index);
    void setPanelShown(bool show);
    void updateModeLine();

    mpv_handle* mpv              = nullptr;
    mpv_render_context* rctx     = nullptr;
    Eq applied;
    int pattern                  = 0;
    bool panelShown              = true;
    bool dirty                   = false;  // a setting changed: save on the way out
    int lastMode                 = -1;     // 1 handheld, 0 docked, -1 not shown yet

    brls::Box* catcher           = nullptr;  // holds the focus while the panel is hidden
    brls::Box* panel             = nullptr;
    brls::Label* modeLine        = nullptr;
    brls::Label* hint            = nullptr;
    brls::View* lastFocus        = nullptr;
    player_ui::StepperCell* rows[PROP_COUNT] = {};
};

CalibrationView::CalibrationView()
{
    setAxis(brls::Axis::ROW);
    setJustifyContent(brls::JustifyContent::FLEX_END);
    setGrow(1.0f);

    // The picture's side of the screen. While the panel is hidden it takes the
    // focus, so every button only brings the panel back -- the rows are
    // INVISIBLE then, and a press must not change a value nobody can see.
    catcher = new brls::Box();
    catcher->setGrow(1.0f);
    catcher->setHeightPercentage(100.0f);
    catcher->setHideHighlight(true);
    catcher->setHideHighlightBackground(true);
    catcher->setHideHighlightBorder(true);
    catcher->addGestureRecognizer(new brls::TapGestureRecognizer(
        catcher, [this]() { setPanelShown(!panelShown); }));
    auto back = [this](brls::View*) {
        setPanelShown(true);
        return true;
    };
    for (auto b : { brls::BUTTON_A, brls::BUTTON_B, brls::BUTTON_X, brls::BUTTON_Y,
                    brls::BUTTON_NAV_UP, brls::BUTTON_NAV_DOWN, brls::BUTTON_NAV_LEFT,
                    brls::BUTTON_NAV_RIGHT })
        catcher->registerAction("", b, back, true, false, brls::SOUND_NONE);
    addView(catcher);

    panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidth(kPanelWidth);
    panel->setHeightPercentage(100.0f);
    panel->setPadding(32.0f, 28.0f, 20.0f, 28.0f);
    panel->setBackgroundColor(theme::isLight() ? nvgRGBA(246, 246, 250, 242)
                                               : nvgRGBA(18, 18, 22, 238));
    panel->registerAction(
        tr("Back"), brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        },
        false, false, brls::SOUND_BACK);
    panel->registerAction(
        tr("Hide the panel"), brls::BUTTON_Y,
        [this](brls::View*) {
            setPanelShown(false);
            return true;
        },
        false, false, brls::SOUND_CLICK);
    addView(panel);

    auto* title = new brls::Label();
    title->setText(tr("Picture"));
    title->setFontSize(26.0f);
    title->setTextColor(theme::text());
    title->setMarginBottom(6.0f);
    panel->addView(title);

    modeLine = new brls::Label();
    modeLine->setFontSize(15.0f);
    modeLine->setLineHeight(1.35f);
    modeLine->setTextColor(theme::textMuted());
    modeLine->setMarginBottom(8.0f);
    panel->addView(modeLine);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    auto* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    scroll->setContentView(list);
    panel->addView(scroll);

    list->addView(new player_ui::StepperCell(
        tr("Pattern"), [this]() { return std::string(tr(kPatterns[pattern].name)); },
        [this](int d) {
            int n = pattern + d;
            if (n < 0 || n >= kPatternCount) return false;
            load(n);
            return true;
        }));

    for (int p = 0; p < PROP_COUNT; p++)
    {
        rows[p] = new player_ui::StepperCell(
            label(p), [p]() { return valueText(value(p)); },
            [this, p](int d) {
                if (!step(p, d)) return false;
                dirty = true;
                return true;
            },
            [this, p]() {
                value(p) = 0;
                dirty    = true;
            });
        list->addView(rows[p]);
    }

    hint = new brls::Label();
    hint->setFontSize(15.0f);
    hint->setLineHeight(1.4f);
    hint->setTextColor(theme::textMuted());
    hint->setMargins(10.0f, 12.0f, 10.0f, 12.0f);
    list->addView(hint);

    auto* reset = new brls::DetailCell();
    reset->setText(tr("Reset all"));
    // No value: an empty detail label still claims its maximum width and cut
    // the title down to "R...".
    reset->detail->setVisibility(brls::Visibility::GONE);
    reset->registerClickAction([this](brls::View*) {
        for (int p = 0; p < PROP_COUNT; p++)
        {
            value(p) = 0;
            rows[p]->refresh();
        }
        dirty = true;
        return true;
    });
    list->addView(reset);

    // For a remote, which has no Y: the same as Y, as a row.
    auto* hide = new brls::DetailCell();
    hide->setText(tr("Hide the panel"));
    hide->setDetailText("Y");
    hide->registerClickAction([this](brls::View*) {
        setPanelShown(false);
        return true;
    });
    list->addView(hide);

    auto* keys = new brls::Label();
    keys->setText(tr("\xE2\x86\x90 \xE2\x86\x92 change a value, OK resets it. "
                     "Any button brings the hidden panel back."));
    keys->setFontSize(14.0f);
    keys->setLineHeight(1.4f);
    keys->setTextColor(theme::textMuted());
    keys->setMargins(10.0f, 12.0f, 8.0f, 12.0f);
    list->addView(keys);

    updateModeLine();
    if (startMpv()) load(0);
    else hint->setText(tr("The player could not start."));
}

CalibrationView::~CalibrationView()
{
    if (rctx) mpv_render_context_free(rctx);
    if (mpv) mpv_terminate_destroy(mpv);
    if (dirty) config::save();
}

bool CalibrationView::startMpv()
{
    mpv = mpv_create();
    if (!mpv) return false;
    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_option_string(mpv, "hwdec", "no");  // a still image
    mpv_set_option_string(mpv, "config", "no");
    mpv_set_option_string(mpv, "terminal", "no");
    mpv_set_option_string(mpv, "ao", "null");
    mpv_set_option_string(mpv, "aid", "no");
    mpv_set_option_string(mpv, "sid", "no");
    mpv_set_option_string(mpv, "osd-level", "0");
    // A still shown for good, not a one-frame video that ends.
    mpv_set_option_string(mpv, "image-display-duration", "inf");
    mpv_set_option_string(mpv, "keep-open", "yes");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "opengl-glfinish", "yes");
    if (mpv_initialize(mpv) < 0)
    {
        brls::Logger::error("[picture] mpv_initialize failed");
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        return false;
    }

    mpv_opengl_init_params glInit{ getProcAddress, nullptr };
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };
    if (mpv_render_context_create(&rctx, mpv, params) < 0)
    {
        brls::Logger::error("[picture] mpv_render_context_create failed");
        rctx = nullptr;
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
        return false;
    }
    setPanelShown(true);  // the video margin for the panel
    return true;
}

void CalibrationView::load(int index)
{
    pattern = index;
    hint->setText(tr(kPatterns[index].hint));
    if (!mpv) return;
    const char* cmd[] = { "loadfile", kPatterns[index].file, nullptr };
    mpv_command(mpv, cmd);
    brls::Logger::info("[picture] pattern {}", kPatterns[index].file);
}

void CalibrationView::setPanelShown(bool show)
{
    if (show != panelShown)
    {
        panelShown = show;
        if (show)
        {
            panel->setVisibility(brls::Visibility::VISIBLE);
            if (lastFocus) brls::Application::giveFocus(lastFocus);
            catcher->setFocusable(false);
        }
        else
        {
            lastFocus = brls::Application::getCurrentFocus();
            catcher->setFocusable(true);
            brls::Application::giveFocus(catcher);
            // INVISIBLE, not GONE: the picture keeps its size either way, only
            // the margin below moves it.
            panel->setVisibility(brls::Visibility::INVISIBLE);
        }
    }
    // With the panel up the pattern is fitted into the rest of the screen
    // rather than hidden under it; without, it fills the screen 1:1.
    if (mpv)
    {
        double ratio = show ? kPanelWidth / brls::Application::contentWidth : 0.0;
        char s[16];
        std::snprintf(s, sizeof(s), "%.4f", ratio);
        mpv_set_property_string(mpv, "video-margin-ratio-right", s);
    }
}

void CalibrationView::updateModeLine()
{
    int mode = handheld() ? 1 : 0;
    if (mode == lastMode) return;
    lastMode = mode;
    modeLine->setText(
        mode ? tr("Applies in handheld only. Docked, the TV sets the picture.")
             : tr("Docked: the TV sets the picture and these settings are off. "
                  "The patterns still help to set up the TV."));
}

void CalibrationView::draw(NVGcontext* vg, float x, float y, float width, float height,
                           brls::Style style, brls::FrameContext* ctx)
{
    updateModeLine();
    if (mpv)
    {
        sync(mpv, applied);
        // Drain mpv's events: nothing here needs them, but they queue up.
        while (mpv_wait_event(mpv, 0)->event_id != MPV_EVENT_NONE) {}
    }
    if (rctx) renderMpvFrame(vg, rctx);
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

class CalibrationActivity : public brls::Activity
{
  public:
    brls::View* createContentView() override { return new CalibrationView(); }
    // Opaque, like the player: nothing behind may be composited over mpv.
    bool isTranslucent() override { return false; }
};

} // namespace

void openCalibration() { brls::Application::pushActivity(new CalibrationActivity()); }

} // namespace picture
