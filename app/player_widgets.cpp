#include "player_widgets.hpp"

#include <cmath>

#include "player.hpp"  // fmtTime

namespace player_ui
{

namespace
{
void tri(NVGcontext* vg, float x1, float y1, float x2, float y2, float x3, float y3)
{
    nvgMoveTo(vg, x1, y1);
    nvgLineTo(vg, x2, y2);
    nvgLineTo(vg, x3, y3);
    nvgClosePath(vg);
}
} // namespace

// Every icon is laid out on a grid where u = 1/64 of the button: the glyph
// itself spans ~24u, the rest is the disc around it.
void drawIcon(NVGcontext* vg, Icon icon, float cx, float cy, float size, NVGcolor c)
{
    const float u = size / 64.0f;
    nvgFillColor(vg, c);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, 2.4f * u);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);

    switch (icon)
    {
        case Icon::Play:
            nvgBeginPath(vg);
            tri(vg, cx - 7 * u, cy - 12 * u, cx + 12 * u, cy, cx - 7 * u, cy + 12 * u);
            nvgFill(vg);
            break;

        case Icon::Pause:
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 10 * u, cy - 11 * u, 6.5f * u, 22 * u, 1.5f * u);
            nvgRoundedRect(vg, cx + 3.5f * u, cy - 11 * u, 6.5f * u, 22 * u, 1.5f * u);
            nvgFill(vg);
            break;

        case Icon::Prev:
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 11 * u, cy - 10 * u, 4 * u, 20 * u, 1 * u);
            tri(vg, cx + 10 * u, cy - 10 * u, cx - 6 * u, cy, cx + 10 * u, cy + 10 * u);
            nvgFill(vg);
            break;

        case Icon::Next:
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx + 7 * u, cy - 10 * u, 4 * u, 20 * u, 1 * u);
            tri(vg, cx - 10 * u, cy - 10 * u, cx + 6 * u, cy, cx - 10 * u, cy + 10 * u);
            nvgFill(vg);
            break;

        case Icon::Rewind:
            nvgBeginPath(vg);
            tri(vg, cx, cy - 9 * u, cx - 12 * u, cy, cx, cy + 9 * u);
            tri(vg, cx + 12 * u, cy - 9 * u, cx, cy, cx + 12 * u, cy + 9 * u);
            nvgFill(vg);
            break;

        case Icon::Forward:
            nvgBeginPath(vg);
            tri(vg, cx, cy - 9 * u, cx + 12 * u, cy, cx, cy + 9 * u);
            tri(vg, cx - 12 * u, cy - 9 * u, cx, cy, cx - 12 * u, cy + 9 * u);
            nvgFill(vg);
            break;

        case Icon::Episodes:
            // Three lines of a list, a play mark against the short last one.
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 12 * u, cy - 10 * u, 24 * u, 3.2f * u, 1.2f * u);
            nvgRoundedRect(vg, cx - 12 * u, cy - 3 * u, 24 * u, 3.2f * u, 1.2f * u);
            nvgRoundedRect(vg, cx - 12 * u, cy + 4 * u, 12 * u, 3.2f * u, 1.2f * u);
            tri(vg, cx + 4 * u, cy + 2 * u, cx + 12 * u, cy + 6.5f * u, cx + 4 * u, cy + 11 * u);
            nvgFill(vg);
            break;

        case Icon::Audio:
            // A speaker and two sound waves.
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - 12 * u, cy - 4 * u);
            nvgLineTo(vg, cx - 7 * u, cy - 4 * u);
            nvgLineTo(vg, cx, cy - 10 * u);
            nvgLineTo(vg, cx, cy + 10 * u);
            nvgLineTo(vg, cx - 7 * u, cy + 4 * u);
            nvgLineTo(vg, cx - 12 * u, cy + 4 * u);
            nvgClosePath(vg);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgArc(vg, cx + 1 * u, cy, 5 * u, -0.8f, 0.8f, NVG_CW);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgArc(vg, cx + 1 * u, cy, 10.5f * u, -0.8f, 0.8f, NVG_CW);
            nvgStroke(vg);
            break;

        case Icon::Subtitles:
            // A frame with two lines of text in it.
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 13 * u, cy - 9.5f * u, 26 * u, 19 * u, 3 * u);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgRect(vg, cx - 8 * u, cy - 2.5f * u, 9 * u, 2.6f * u);
            nvgRect(vg, cx + 3 * u, cy - 2.5f * u, 5 * u, 2.6f * u);
            nvgRect(vg, cx - 8 * u, cy + 2.5f * u, 4 * u, 2.6f * u);
            nvgRect(vg, cx - 2 * u, cy + 2.5f * u, 10 * u, 2.6f * u);
            nvgFill(vg);
            break;

        case Icon::Settings:
        {
            // A gear: eight teeth round a ring with a hole in it.
            nvgBeginPath(vg);
            for (int k = 0; k < 8; k++)
            {
                nvgSave(vg);
                nvgTranslate(vg, cx, cy);
                nvgRotate(vg, (float)k * NVG_PI / 4.0f);
                nvgRect(vg, -2.8f * u, -12.5f * u, 5.6f * u, 6 * u);
                nvgRestore(vg);
            }
            nvgCircle(vg, cx, cy, 9 * u);
            nvgCircle(vg, cx, cy, 3.8f * u);
            nvgPathWinding(vg, NVG_HOLE);
            nvgFill(vg);
            break;
        }

        case Icon::LockOpen:
        case Icon::LockClosed:
        {
            bool open = icon == Icon::LockOpen;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 9 * u, cy - 2 * u, 18 * u, 14 * u, 2.5f * u);
            nvgFill(vg);
            // The shackle: closed, both legs reach the body; open, it is lifted
            // and the left leg hangs free.
            float lift = open ? 4 * u : 0.0f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx + 5.5f * u, cy - 2 * u - lift);
            nvgLineTo(vg, cx + 5.5f * u, cy - 6 * u - lift);
            nvgArc(vg, cx, cy - 6 * u - lift, 5.5f * u, 0.0f, NVG_PI, NVG_CCW);
            nvgLineTo(vg, cx - 5.5f * u, open ? cy - 5 * u - lift : cy - 2 * u);
            nvgStroke(vg);
            break;
        }
    }
}

// ---------------------------------------------------------------------------

Button::Button(Icon icon, std::string caption, float size)
    : icon(icon), caption(std::move(caption))
{
    setDimensions(size, size);
    setMargins(0.0f, 6.0f, 0.0f, 6.0f);
}

void Button::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
                  brls::FrameContext* ctx)
{
    float cx = x + w / 2.0f, cy = y + h / 2.0f, r = w / 2.0f;
    if (focused)
    {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 240));
        nvgFill(vg);
    }
    // A soft shadow under a bare icon: over a bright frame (snow, a white
    // wall) a white glyph on its own all but disappears.
    if (!focused)
        drawIcon(vg, icon, cx, cy + 1.5f, w, nvgRGBA(0, 0, 0, 120));
    drawIcon(vg, icon, cx, cy, w,
             focused ? nvgRGB(18, 18, 22) : nvgRGBA(255, 255, 255, 235));

    // What the button does, over it, while it has the focus: the icons alone
    // leave "Episodes" and "Audio" to guesswork the first time.
    if (focused && showCaption && !caption.empty())
    {
        int font = brls::Application::getFont(brls::FONT_REGULAR);
        if (font != brls::FONT_INVALID)
        {
            nvgFontFaceId(vg, font);
            nvgFontSize(vg, 17.0f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            float b[4];
            nvgTextBounds(vg, 0, 0, caption.c_str(), nullptr, b);
            float tw = b[2] - b[0];
            float ph = 28.0f, pw = tw + 22.0f;
            float py = y - ph - 10.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - pw / 2.0f, py, pw, ph, 6.0f);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 190));
            nvgFill(vg);
            nvgFillColor(vg, nvgRGB(255, 255, 255));
            nvgText(vg, cx, py + ph / 2.0f, caption.c_str(), nullptr);
        }
    }
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

// ---------------------------------------------------------------------------

Timeline::Timeline()
{
    setHeight(36.0f);
    setGrow(1.0f);
}

double Timeline::fractionAt(float px) const
{
    float w = const_cast<Timeline*>(this)->getWidth();
    if (w <= 0.0f) return 0.0;
    double f = (px - const_cast<Timeline*>(this)->getX()) / w;
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

void Timeline::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
                    brls::FrameContext* ctx)
{
    auto frac = [this](double t) {
        if (duration <= 0.0) return 0.0f;
        double f = t / duration;
        return (float)(f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f));
    };
    NVGcolor accent = brls::Application::getTheme().getColor("brls/accent");

    float th = focused ? 8.0f : 5.0f;
    float ty = y + h / 2.0f - th / 2.0f;
    float rr = th / 2.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, ty, w, th, rr);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 50));
    nvgFill(vg);

    float bw = w * frac(buffered);
    if (bw > 0.5f)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, ty, bw, th, rr);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 80));
        nvgFill(vg);
    }

    float pw = w * frac(position);
    if (pw > 0.5f)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, ty, pw, th, rr);
        nvgFillColor(vg, accent);
        nvgFill(vg);
    }

    // The knob rides the played edge, or the aim while a seek is set up.
    float kx = x + w * frac(seeking ? cursor : position);
    float kr = focused || seeking ? 10.0f : 6.5f;
    nvgBeginPath(vg);
    nvgCircle(vg, kx, y + h / 2.0f, kr);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgFill(vg);

    if (seeking)
    {
        // Where it will land, and how far that is from what is on screen:
        // "1:23:45  +0:30". With a remote's arrows the jump is the number that
        // matters -- it tells how many more presses to go.
        std::string t = fmtTime(cursor);
        double d      = cursor - playing;
        std::string j;
        if (d >= 1.0 || d <= -1.0) j = (d > 0 ? "+" : "-") + fmtTime(d > 0 ? d : -d);
        int font = brls::Application::getFont(brls::FONT_REGULAR);
        if (font != brls::FONT_INVALID)
        {
            nvgFontFaceId(vg, font);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            float b[4];
            nvgFontSize(vg, 21.0f);
            float tw = nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, b);
            float jw = 0.0f, gap = 0.0f;
            if (!j.empty())
            {
                nvgFontSize(vg, 18.0f);
                jw  = nvgTextBounds(vg, 0, 0, j.c_str(), nullptr, b);
                gap = 10.0f;
            }
            float bwid = tw + gap + jw + 26.0f, bh = 34.0f;
            float bx   = kx - bwid / 2.0f;
            if (bx < x) bx = x;
            if (bx + bwid > x + w) bx = x + w - bwid;
            float by = y + h / 2.0f - kr - bh - 8.0f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, bx, by, bwid, bh, 7.0f);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 240));
            nvgFill(vg);
            float tx = bx + 13.0f, ty = by + bh / 2.0f;
            nvgFontSize(vg, 21.0f);
            nvgFillColor(vg, nvgRGB(18, 18, 22));
            nvgText(vg, tx, ty, t.c_str(), nullptr);
            if (!j.empty())
            {
                nvgFontSize(vg, 18.0f);
                nvgFillColor(vg, accent);
                nvgText(vg, tx + tw + gap, ty, j.c_str(), nullptr);
            }
        }
    }
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

// ---------------------------------------------------------------------------

Shade::Shade(bool fromTop, int alpha) : fromTop(fromTop), alpha(alpha) {}

void Shade::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
                 brls::FrameContext* ctx)
{
    NVGcolor dark  = nvgRGBA(0, 0, 0, (unsigned char)alpha);
    NVGcolor clear = nvgRGBA(0, 0, 0, 0);
    NVGpaint p     = fromTop ? nvgLinearGradient(vg, x, y, x, y + h, dark, clear)
                             : nvgLinearGradient(vg, x, y, x, y + h, clear, dark);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, p);
    nvgFill(vg);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

} // namespace player_ui
