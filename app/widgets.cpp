#include "widgets.hpp"

#include "i18n.hpp"
#include "stremio.hpp"  // fetchPosterAsync / fetchHqArtAsync: the shared art cache
#include "theme.hpp"

namespace ui
{

namespace
{
constexpr float kRadius = 8.0f;

void makeFocusable(brls::Box* card, std::function<void()> onClick)
{
    card->setFocusable(true);
    card->setHighlightCornerRadius(
        kRadius + brls::Application::getStyle()["brls/highlight/stroke_width"] / 2);
    // "A Open" in the hint bar, as the mock-ups have it (the stock click
    // action says "OK"). A tap fires any A action, so touch still works.
    card->registerAction(
        tr("Open"), brls::BUTTON_A,
        [onClick](brls::View*) {
            if (onClick) onClick();
            return true;
        },
        false, false, brls::SOUND_CLICK);
    card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
}

// The artwork filling a card, cropped to it.
brls::Image* fillImage(float w, float h)
{
    auto* img = new brls::Image();
    img->setDimensions(w, h);
    img->setScalingType(brls::ImageScalingType::FILL);
    img->setClipsToBounds(true);
    img->setCornerRadius(kRadius);
    img->setPositionType(brls::PositionType::ABSOLUTE);
    img->setPositionTop(0.0f);
    img->setPositionLeft(0.0f);
    return img;
}

// A dark band across the bottom of a card with the title on it, so the title
// reads over any artwork.
brls::Box* titleBand(const std::string& title, float w, float h, float fontSize)
{
    auto* band = new ShadeBox(false, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 215), 0.7f);
    band->setWidth(w);
    band->setHeight(h);
    band->setCornerRadius(kRadius);
    band->setPositionType(brls::PositionType::ABSOLUTE);
    band->setPositionBottom(0.0f);
    band->setPositionLeft(0.0f);
    band->setAxis(brls::Axis::COLUMN);
    band->setJustifyContent(brls::JustifyContent::FLEX_END);
    band->setPadding(0.0f, 12.0f, 12.0f, 12.0f);

    auto* name = new brls::Label();
    name->setText(title);
    name->setFontSize(fontSize);
    name->setTextColor(nvgRGBA(255, 255, 255, 245));
    name->setSingleLine(true);
    name->setWidth(w - 24.0f);
    band->addView(name);
    return band;
}
} // namespace

void attachArt(brls::Image* img, const std::string& id, const std::string& url,
               std::shared_ptr<bool> alive, bool hq, const std::string& localPath)
{
    if (!localPath.empty())
    {
        img->setImageFromFile(localPath);
        return;
    }
    if (id.empty() || url.empty()) return;
    auto done = [img, alive](std::string path) {
        if (!*alive || path.empty()) return;
        img->setImageFromFile(path);
    };
    if (hq)
        stremio::fetchHqArtAsync(id, url, done);
    else
        stremio::fetchPosterAsync(id, url, done);
}

brls::Label* heading(const std::string& text, float inset)
{
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(21.0f);
    l->setTextColor(theme::text());
    l->setMargins(14.0f, 0.0f, 4.0f, inset);
    return l;
}

brls::View* strip(brls::Box* row, float height, bool fade)
{
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    // CENTERED: card by card, the focused one kept in view (NATURAL hands focus
    // to the frame and slides a card half out of sight -- see StremioTab).
    if (fade)
    {
        auto* s = new FadeStrip();
        s->setHeight(height);
        s->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        s->setContentView(row);
        return s;
    }
    auto* s = new brls::HScrollingFrame();
    s->setHeight(height);
    s->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    s->setContentView(row);
    return s;
}

brls::Box* posterCard(const std::string& title, const std::string& artId,
                      const std::string& artUrl, std::shared_ptr<bool> alive,
                      std::function<void()> onClick, float w,
                      const std::string& badge)
{
    const float h = w * 1.5f;
    auto* card = new brls::Box();
    card->setWidth(w);
    card->setHeight(h);
    card->setCornerRadius(kRadius);
    card->setBackgroundColor(theme::scrim(20));
    card->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
    makeFocusable(card, std::move(onClick));

    auto* img = fillImage(w, h);
    attachArt(img, artId, artUrl, alive);
    card->addView(img);
    card->addView(titleBand(title, w, h * 0.42f, w < 170.0f ? 15.0f : 17.0f));

    if (!badge.empty())
    {
        auto* b = new brls::Box();
        b->setPositionType(brls::PositionType::ABSOLUTE);
        b->setPositionTop(8.0f);
        b->setPositionLeft(8.0f);
        b->setCornerRadius(5.0f);
        b->setBackgroundColor(nvgRGBA(0, 0, 0, 160));
        b->setPadding(5.0f, 7.0f, 3.0f, 7.0f);
        auto* t = new brls::Label();
        t->setText(badge);
        t->setFontSize(13.0f);
        t->setTextColor(nvgRGBA(255, 255, 255, 240));
        b->addView(t);
        card->addView(b);
    }
    return card;
}

brls::Box* wideCard(const std::string& title, const std::string& artId,
                    const std::string& artUrl, const std::string& localPath,
                    double progress, std::shared_ptr<bool> alive,
                    std::function<void()> onClick, float w)
{
    const float h = w * 9.0f / 16.0f;
    auto* card = new brls::Box();
    card->setWidth(w);
    card->setHeight(h);
    card->setCornerRadius(kRadius);
    card->setBackgroundColor(theme::scrim(20));
    card->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
    makeFocusable(card, std::move(onClick));

    auto* img = fillImage(w, h);
    attachArt(img, artId, artUrl, alive, false, localPath);
    card->addView(img);
    card->addView(titleBand(title, w, h * 0.55f, w < 280.0f ? 16.0f : 18.0f));

    if (progress >= 0.0)
    {
        auto* track = new brls::Box();
        track->setPositionType(brls::PositionType::ABSOLUTE);
        track->setPositionBottom(0.0f);
        track->setPositionLeft(0.0f);
        track->setWidth(w);
        track->setHeight(5.0f);
        track->setBackgroundColor(nvgRGBA(255, 255, 255, 50));
        auto* fill = new theme::AccentBox();
        fill->setHeight(5.0f);
        fill->setWidth(w * (float)(progress > 1.0 ? 1.0 : progress));
        track->addView(fill);
        card->addView(track);
    }
    return card;
}

brls::Box* moreTile(const std::string& text, float w, std::function<void()> onClick)
{
    auto* card = new brls::Box();
    card->setWidth(w);
    card->setHeight(w * 1.5f);
    card->setCornerRadius(kRadius);
    card->setBackgroundColor(theme::scrim(22));
    card->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
    card->setAxis(brls::Axis::COLUMN);
    card->setJustifyContent(brls::JustifyContent::CENTER);
    card->setAlignItems(brls::AlignItems::CENTER);
    makeFocusable(card, std::move(onClick));

    auto* arrow = new brls::Label();
    arrow->setText(">");
    arrow->setFontSize(34.0f);
    arrow->setTextColor(theme::textDim());
    card->addView(arrow);
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(17.0f);
    l->setTextColor(theme::text());
    l->setMarginTop(8.0f);
    l->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    card->addView(l);
    return card;
}

brls::Box* chip(const std::string& text, NVGcolor bg, NVGcolor fg)
{
    auto* c = new brls::Box();
    c->setCornerRadius(6.0f);
    c->setPadding(6.0f, 10.0f, 3.0f, 10.0f);
    c->setMarginRight(10.0f);
    c->setBackgroundColor(bg);
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(17.0f);
    l->setTextColor(fg);
    l->setSingleLine(true);
    c->addView(l);
    return c;
}

brls::Box* chip(const std::string& text, bool strong)
{
    auto* c = new brls::Box();
    c->setCornerRadius(6.0f);
    c->setPadding(6.0f, 10.0f, 3.0f, 10.0f);
    c->setMarginRight(10.0f);
    c->setBackgroundColor(strong ? nvgRGB(0x2f, 0x7d, 0x22) : nvgRGBA(255, 255, 255, 28));
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(17.0f);
    l->setTextColor(nvgRGBA(255, 255, 255, strong ? 255 : 225));
    l->setSingleLine(true);
    c->addView(l);
    return c;
}

brls::Button* actionButton(const std::string& text, bool primary,
                           std::function<void()> onClick, bool onImage)
{
    auto* b = new brls::Button();
    b->setText(text);
    // The mock-ups' pills: smaller than borealis' stock button, the main one
    // in the accent, the others a translucent fill rather than an outline.
    b->setStyle(primary ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERLESS);
    if (!primary)
        b->setBackgroundColor(onImage ? nvgRGBA(255, 255, 255, 34) : theme::scrim(26));
    b->setFontSize(17.0f);
    b->setPadding(9.0f, 20.0f, 9.0f, 20.0f);
    b->setCornerRadius(8.0f);
    b->setHighlightCornerRadius(8.0f +
                                brls::Application::getStyle()["brls/highlight/stroke_width"] / 2);
    b->setMarginRight(12.0f);
    b->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();
        return true;
    });
    return b;
}

void setPillActive(brls::Button* b, bool active, bool onImage)
{
    b->setStyle(active ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERLESS);
    b->setBackgroundColor(active ? nvgRGBA(0, 0, 0, 0)
                                 : (onImage ? nvgRGBA(255, 255, 255, 34) : theme::scrim(26)));
}

} // namespace ui
