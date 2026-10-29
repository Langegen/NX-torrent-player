#pragma once

// Views and builders shared by the browse screens: the Stremio tab and the
// TMDB home / card screens. Drawn in the app's Horizon style: the theme's
// field, the accent for what is active, cards that fade at a strip's edges.

#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>

// The tab's background colour at a screen point (defined in stremio.cpp, where
// the field is painted): what a fade has to blend into.
NVGcolor tabBgAt(float px, float py);

// A strip that fades its cards out at whichever edge still has content beyond
// it, so they dissolve into the background instead of being cut off -- the same
// treatment the episode and addon scrollers get. The scrim is the background
// colour sampled AT the edge, fading to transparent inward; an edge with
// nothing past it is left alone, so the first card at rest is never dimmed.
class FadeStrip : public brls::HScrollingFrame
{
  public:
    void setContentView(brls::View* view)
    {
        content = view;
        brls::HScrollingFrame::setContentView(view);
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override
    {
        brls::HScrollingFrame::draw(vg, x, y, w, h, style, ctx);

        const float fade = 64.0f;
        float offset     = getContentOffsetX();
        float rightLimit = content ? content->getWidth() - w : 0.0f;
        float midY       = y + h * 0.5f;

        if (offset > 2.0f)  // scrolled: cards run off the left
        {
            NVGcolor edge = tabBgAt(x, midY);
            NVGcolor gone = edge;
            gone.a        = 0.0f;
            nvgBeginPath(vg);
            nvgRect(vg, x, y, fade, h);
            nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x + fade, y, edge, gone));
            nvgFill(vg);
        }
        if (offset < rightLimit - 2.0f)  // more cards off the right
        {
            NVGcolor edge = tabBgAt(x + w, midY);
            NVGcolor gone = edge;
            gone.a        = 0.0f;
            nvgBeginPath(vg);
            nvgRect(vg, x + w - fade, y, fade, h);
            nvgFillPaint(vg,
                         nvgLinearGradient(vg, x + w - fade, y, x + w, y, gone, edge));
            nvgFill(vg);
        }
    }

  private:
    brls::View* content = nullptr;
};

// A poster with only its top corners rounded: it meets the card's footer flush,
// and a rounded bottom edge cut two notches of card background out of the
// artwork. brls::Image rounds all four corners, so this repeats its draw with a
// varying-radius rect -- everything it needs is protected, not private.
class TopRoundedImage : public brls::Image
{
  public:
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override
    {
        if (this->texture == 0) return;

        float ix = x + this->imageX;
        float iy = y + this->imageY;
        this->paint.xform[4] = ix;
        this->paint.xform[5] = iy;

        float r = this->getCornerRadius();
        nvgBeginPath(vg);
        if (this->getClipsToBounds())
            nvgRoundedRectVarying(vg, x, y, w, h, r, r, 0.0f, 0.0f);
        else
            nvgRoundedRectVarying(vg, ix, iy, this->imageWidth,
                                  this->imageHeight, r, r, 0.0f, 0.0f);
        nvgFillPaint(vg, a(this->paint));
        nvgFill(vg);
    }
};

namespace ui
{

// A box that paints a linear gradient behind its children: `from` at the
// start edge to `to` at `stop` (0..1) of the way across, `to` after that.
// Horizontal runs left to right, vertical top to bottom. Used to darken art so
// text on it stays readable: the card's backdrop, a poster's title band.
class ShadeBox : public brls::Box
{
  public:
    ShadeBox(bool horizontal, NVGcolor from, NVGcolor to, float stop = 1.0f)
        : horizontal(horizontal), from(from), to(to), stop(stop)
    {
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override
    {
        float ex = horizontal ? x + w * stop : x;
        float ey = horizontal ? y : y + h * stop;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, getCornerRadius());
        nvgFillPaint(vg, nvgLinearGradient(vg, x, y, ex, ey, from, to));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

  private:
    bool horizontal;
    NVGcolor from, to;
    float stop;
};

// Fills `img` once the artwork for `id` lands (thumbnail size, or full size
// with `hq`). `alive` guards a fetch that lands after the view is gone.
// `localPath`, when set, is used as is (history keeps posters on the SD card).
void attachArt(brls::Image* img, const std::string& id, const std::string& url,
               std::shared_ptr<bool> alive, bool hq = false,
               const std::string& localPath = "");

// A section heading over a strip ("Popular now"), inset to line up with the
// first card.
brls::Label* heading(const std::string& text, float inset);

// A horizontally scrolling strip holding `row`'s cards. `height` should leave
// room for the focus glow above and below the cards. `fade` dissolves the
// edges into the tab's background -- only right where that background is
// behind the strip (not over a card's backdrop picture).
brls::View* strip(brls::Box* row, float height, bool fade = true);

// An upright poster card, as on the home screen: the artwork fills it, the
// title sits on a dark band across the bottom. `badge` ("4K") goes top-left.
brls::Box* posterCard(const std::string& title, const std::string& artId,
                      const std::string& artUrl, std::shared_ptr<bool> alive,
                      std::function<void()> onClick, float w = 190.0f,
                      const std::string& badge = "");

// A wide 16:9 card: image, title on a dark band, and a progress bar along the
// bottom edge when `progress` is in 0..1 (Continue watching, episodes).
brls::Box* wideCard(const std::string& title, const std::string& artId,
                    const std::string& artUrl, const std::string& localPath,
                    double progress, std::shared_ptr<bool> alive,
                    std::function<void()> onClick, float w = 320.0f);

// The tile at the end of a row that opens its whole collection: a poster's
// size, the label centred on the plain card fill.
brls::Box* moreTile(const std::string& text, float w, std::function<void()> onClick);

// A small rounded label: the meta line on the card ("2024", "Drama").
// `strong` uses the accent-free green the rating chip wants.
brls::Box* chip(const std::string& text, bool strong = false);
// The same with its own colours (IMDb's yellow with dark text).
brls::Box* chip(const std::string& text, NVGcolor bg, NVGcolor fg);

// A pill button (the mock-ups' "▶ Watch", "Details", season chips). `primary`
// takes the accent; the others are a translucent fill -- white when they sit on
// a picture (`onImage`), the theme's scrim otherwise.
brls::Button* actionButton(const std::string& text, bool primary,
                           std::function<void()> onClick, bool onImage = false);

// Switches a pill between the accent (selected) and the plain fill: season
// chips, filters, the Home / Movies / Shows switch.
void setPillActive(brls::Button* b, bool active, bool onImage = false);

} // namespace ui
