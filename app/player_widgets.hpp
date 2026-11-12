#pragma once

// The pieces of the player's control panel (the Lampa-style bar: a timeline
// over a row of round buttons, a title bar on top). All drawn with nanovg
// directly: the Material glyphs a player needs (skip, replay, subtitles,
// playlist) sit in U+E000-E152, which the Nintendo shared font claims ahead of
// Material in the fallback chain, so as Label glyphs they came out as the
// wrong symbols.

#include <borealis.hpp>

#include <functional>
#include <string>

namespace player_ui
{

enum class Icon
{
    Play,
    Pause,
    Prev,
    Next,
    Rewind,
    Forward,
    Episodes,
    Audio,
    Subtitles,
    Settings,
    LockOpen,
    LockClosed,
};

// Draws `icon` centred on (cx, cy), sized for a button `size` px across.
void drawIcon(NVGcontext* vg, Icon icon, float cx, float cy, float size, NVGcolor color);

// A round icon button of the panel. Focus is the player's own (it keeps the
// borealis focus itself, see MpvView): `focused` is set from outside and
// paints the white disc Lampa marks the current button with, `caption` is
// shown over it while focused.
class Button : public brls::Box
{
  public:
    Button(Icon icon, std::string caption, float size);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    Icon icon;
    std::string caption;
    bool focused     = false;
    bool showCaption = false;
};

// A one-line action pill that pops up over a panel button (the subtitles
// chip). The pill is sized to the text and centred in the view, which is the
// touch area; `focused` paints it white, like a focused button.
class Chip : public brls::Box
{
  public:
    Chip();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    std::string text;
    bool focused = false;
};

// The timeline: played / buffered / the rest, a knob, and -- while a seek is
// being aimed -- the target time in a bubble over the cursor. Times are
// seconds; set them every frame the panel is up.
class Timeline : public brls::Box
{
  public:
    Timeline();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    double duration = 0.0;
    double position = 0.0;  // what has been played
    double buffered = 0.0;  // read ahead up to here
    double cursor   = 0.0;  // where a seek is aimed (shown while seeking)
    double playing  = 0.0;  // what is on screen now: the jump is cursor - this
    bool seeking    = false;
    bool focused    = false;

    // 0..1 along the track for an x in window coordinates (touch).
    double fractionAt(float x) const;
};

// A box that paints a vertical black gradient behind its children: opaque at
// the edge of the screen it hugs (top or bottom), clear towards the middle.
class Shade : public brls::Box
{
  public:
    explicit Shade(bool fromTop, int alpha);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    bool fromTop;
    int alpha;
};

// The console's battery, drawn as a battery: an outline with a nub, filled to
// `level` (0..100) -- red when low -- and a bolt over it while charging. The
// percentage is a Label beside it (MpvView), in the panel's own font.
class Battery : public brls::Box
{
  public:
    Battery();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    int level     = 100;
    bool charging = false;
};

// A row whose value changes in place: left/right step it (held, they repeat)
// and OK does `onOk` -- by default the next value, wrapping round. A dropdown
// cost three presses a notch and hid the video behind its list; this is one
// press, with the picture still in view. Used by the player's settings panel
// and the picture calibration screen.
class StepperCell : public brls::DetailCell
{
  public:
    // `step(dir)` changes the value and says whether it could (false at an
    // end); `value()` is how it reads now.
    StepperCell(const std::string& titleText, std::function<std::string()> valueFn,
                std::function<bool(int)> stepFn, std::function<void()> onOk = nullptr);

    void refresh();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    static constexpr float kChevronRoom = 30.0f;
    std::function<std::string()> value;
    std::function<bool(int)> step;
};

} // namespace player_ui
