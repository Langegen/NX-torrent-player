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

} // namespace player_ui
