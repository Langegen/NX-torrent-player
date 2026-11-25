#include "collection.hpp"

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
    return (platform_ui::isMobile() && platform_ui::isPortrait()) ? 156.0f : 150.0f;
}
inline float gapWidth()
{
    return (platform_ui::isMobile() && platform_ui::isPortrait()) ? 14.0f : 16.0f;
}
// How close to the end of the scroll the next page is asked for: about one
// row of posters, so it is there before anyone reaches the bottom.
constexpr float kNearBottom = 400.0f;
} // namespace

PosterGrid::PosterGrid()
{
    setAxis(brls::Axis::COLUMN);
    setPadding(6.0f, 0.0f, 24.0f, 0.0f);
    float gap    = gapWidth();
    float usable = brls::Application::contentWidth - 2.0f * inset() + gap;
    columns      = std::max(3, (int)(usable / (posterWidth() + gap)));
}

PosterGrid::~PosterGrid() { *alive = false; }

void PosterGrid::load(const tmdb::ListQuery& q, brls::ScrollingFrame* frame)
{
    // A card about to be freed may hold the focus (a new search started from
    // the keyboard leaves it where it was).
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
        if (v == this)
        {
            if (parkTarget) brls::Application::giveFocus(parkTarget);
            break;
        }
    *alive = false;
    alive  = std::make_shared<bool>(true);
    clearViews();
    row    = nullptr;
    status = nullptr;
    firstCard = nullptr;
    shown  = 0;
    page   = 0;
    totalPages = 1;
    loading    = false;
    query  = q;
    scroll = frame;
    next();
}

void PosterGrid::loadPersonCredits(int personId, bool isDirector, brls::ScrollingFrame* frame)
{
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent())
        if (v == this)
        {
            if (parkTarget) brls::Application::giveFocus(parkTarget);
            break;
        }
    *alive = false;
    alive  = std::make_shared<bool>(true);
    clearViews();
    row    = nullptr;
    status = nullptr;
    firstCard = nullptr;
    shown  = 0;
    page   = 0;
    totalPages = 1;
    loading    = true;
    query  = tmdb::ListQuery();
    scroll = frame;

    status = new brls::Label();
    status->setFontSize(17.0f);
    status->setTextColor(theme::textMuted());
    status->setMargins(14.0f, 0.0f, 0.0f, inset());
    status->setText(tr("Loading..."));
    addView(status);

    tmdb::fetchPersonCreditsAsync(personId, isDirector, [this, a = alive](tmdb::ListResult r) {
        if (!*a) return;
        loading = false;
        if (!r.ok)
        {
            status->setText(r.error);
            totalPages = page;
            return;
        }
        page = 1;
        totalPages = 1;
        removeView(status, false);
        for (const auto& it : r.items)
            if (!filter || filter(it)) add(it);
        addView(status);
        if (shown > 0) focusFirstIfLost();
        if (shown == 0)
            status->setText(tr("Nothing here"));
        else
            status->setVisibility(brls::Visibility::GONE);
    });
}

void PosterGrid::next()
{
    if (loading || exhausted()) return;
    loading = true;
    if (!status)
    {
        status = new brls::Label();
        status->setFontSize(17.0f);
        status->setTextColor(theme::textMuted());
        status->setMargins(14.0f, 0.0f, 0.0f, inset());
        addView(status);
    }
    status->setText(tr("Loading..."));
    status->setVisibility(brls::Visibility::VISIBLE);

    tmdb::fetchQueryAsync(query, page + 1, [this, a = alive](tmdb::ListResult r) {
        if (!*a) return;
        loading = false;
        if (!r.ok)
        {
            status->setText(r.error);
            totalPages = page;  // stop asking
            return;
        }
        page       = r.page;
        totalPages = r.totalPages;
        // The status line stays the last child: take it out, add the page,
        // put it back.
        removeView(status, false);
        int before = shown;
        for (const auto& it : r.items)
            if (!filter || filter(it)) add(it);
        addView(status);
        if (before == 0 && shown > 0) focusFirstIfLost();
        if (shown == 0 && exhausted())
            status->setText(tr("Nothing here"));
        else
            status->setVisibility(exhausted() ? brls::Visibility::GONE
                                                     : brls::Visibility::VISIBLE);
        // A filter can thin a page to nothing: keep going while the screen
        // is not even full.
        if (shown < columns * 2 && !exhausted()) next();
    });
}

// The screen opened before its posters came in, with nothing to focus: the
// focus stayed behind on the previous screen (the "See all" tile), and A did
// nothing. Hand it to the first poster -- unless it already sits somewhere on
// this screen (the search bar, a filter), where the person put it.
void PosterGrid::focusFirstIfLost()
{
    brls::Activity* mine = getParentActivity();
    auto stack           = brls::Application::getActivitiesStack();
    if (!mine || stack.empty() || stack.back() != mine) return;
    brls::View* f = brls::Application::getCurrentFocus();
    bool take = focusResultsOnce && f == parkTarget;
    focusResultsOnce = false;
    if (f && f->getParentActivity() == mine && !take) return;
    if (firstCard) brls::Application::giveFocus(firstCard);
}

void PosterGrid::add(const content::Item& it)
{
    float gap = gapWidth();
    if (!row || (int)row->getChildren().size() >= columns)
    {
        row = new ui::NearestRow();
        row->setPaddingLeft(inset());
        row->setMarginBottom(gap);
        addView(row);
    }
    brls::Box* card = catalogPoster(it, alive, posterWidth());
    if (!firstCard) firstCard = card;
    row->addView(card);
    shown++;
}

void PosterGrid::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
                      brls::FrameContext* ctx)
{
    brls::Box::draw(vg, x, y, w, h, style, ctx);
    // borealis' ScrollingFrame says nothing when it scrolls: compare its
    // offset with the content each frame (catches a touch flick too).
    if (!scroll || loading || exhausted()) return;
    // Where this grid's bottom is against the frame's, both on screen: right
    // for a grid that is the whole content and for one section among others
    // (the search's "All"), whose own height says nothing about the offset.
    float visible = scroll->getHeight();
    if (visible <= 0.0f || h <= 0.0f) return;
    if (y + h <= scroll->getY() + visible + kNearBottom) next();
}

void openCollection(const std::string& title, const tmdb::ListQuery& q)
{
    bool mobile = platform_ui::isMobile();
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setGrow(1.0f);

    if (!mobile)
    {
        auto* head = new brls::Label();
        head->setText(title);
        head->setFontSize(28.0f);
        head->setMargins(34.0f, 0.0f, 6.0f, inset());
        root->addView(head);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    auto* grid = new PosterGrid();
    scroll->setContentView(grid);
    root->addView(scroll);
    grid->load(q, scroll);

    auto* frame = new ui::FieldFrame(root);
    if (mobile)
    {
        frame->setTitle(title);
    }
    else
    {
        frame->setTitle("");
        // The heading is the collection's name: no empty title bar above it.
        frame->setHeaderVisibility(brls::Visibility::GONE);
    }
    brls::Application::pushActivity(new brls::Activity(frame));
}

void openPersonCredits(const std::string& name, const std::string& role, int personId, bool isDirector)
{
    bool mobile = platform_ui::isMobile();
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setGrow(1.0f);

    if (!mobile)
    {
        auto* headerBox = new brls::Box();
        headerBox->setAxis(brls::Axis::ROW);
        headerBox->setAlignItems(brls::AlignItems::BASELINE);
        headerBox->setMargins(34.0f, 0.0f, 6.0f, inset());

        auto* head = new brls::Label();
        head->setText(name);
        head->setFontSize(28.0f);
        headerBox->addView(head);

        if (!role.empty())
        {
            auto* sub = new brls::Label();
            sub->setText("  \xC2\xB7  " + role);
            sub->setFontSize(18.0f);
            sub->setTextColor(theme::textMuted());
            headerBox->addView(sub);
        }
        root->addView(headerBox);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    auto* grid = new PosterGrid();
    scroll->setContentView(grid);
    root->addView(scroll);
    grid->loadPersonCredits(personId, isDirector, scroll);

    auto* frame = new ui::FieldFrame(root);
    if (mobile)
    {
        frame->setTitle(role.empty() ? name : (name + "  \xC2\xB7  " + role));
    }
    else
    {
        frame->setTitle("");
        frame->setHeaderVisibility(brls::Visibility::GONE);
    }
    brls::Application::pushActivity(new brls::Activity(frame));
}
