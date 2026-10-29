#include "collection.hpp"

#include "card.hpp"
#include "i18n.hpp"
#include "library.hpp"
#include "theme.hpp"
#include "widgets.hpp"

namespace
{
constexpr float kInset   = 60.0f;
constexpr float kPosterW = 150.0f;
constexpr float kGap     = 16.0f;
// How close to the end of the scroll the next page is asked for: about one
// row of posters, so it is there before anyone reaches the bottom.
constexpr float kNearBottom = 400.0f;
} // namespace

PosterGrid::PosterGrid()
{
    setAxis(brls::Axis::COLUMN);
    setPadding(6.0f, 0.0f, 24.0f, 0.0f);
    float usable = brls::Application::contentWidth - 2 * kInset + kGap;
    columns = std::max(3, (int)(usable / (kPosterW + kGap)));
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
    shown  = 0;
    page   = 0;
    totalPages = 1;
    loading    = false;
    query  = q;
    scroll = frame;
    next();
}

void PosterGrid::next()
{
    if (loading || page >= totalPages) return;
    loading = true;
    if (!status)
    {
        status = new brls::Label();
        status->setFontSize(17.0f);
        status->setTextColor(theme::textMuted());
        status->setMargins(14.0f, 0.0f, 0.0f, kInset);
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
        for (const auto& it : r.items)
            if (!filter || filter(it)) add(it);
        addView(status);
        if (shown == 0 && page >= totalPages)
            status->setText(tr("Nothing here"));
        else
            status->setVisibility(page >= totalPages ? brls::Visibility::GONE
                                                     : brls::Visibility::VISIBLE);
        // A filter can thin a page to nothing: keep going while the screen
        // is not even full.
        if (shown < columns * 2 && page < totalPages) next();
    });
}

void PosterGrid::add(const content::Item& it)
{
    if (!row || (int)row->getChildren().size() >= columns)
    {
        row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setPaddingLeft(kInset);
        row->setMarginBottom(kGap);
        addView(row);
    }
    row->addView(catalogPoster(it, alive, kPosterW));
    shown++;
}

void PosterGrid::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
                      brls::FrameContext* ctx)
{
    brls::Box::draw(vg, x, y, w, h, style, ctx);
    // borealis' ScrollingFrame says nothing when it scrolls: compare its
    // offset with the content each frame (catches a touch flick too).
    if (!scroll || loading || page >= totalPages) return;
    float visible = scroll->getHeight();
    float content = getHeight();
    if (visible <= 0.0f || content <= 0.0f) return;
    if (scroll->getContentOffsetY() + visible >= content - kNearBottom) next();
}

void openCollection(const std::string& title, const tmdb::ListQuery& q)
{
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setGrow(1.0f);

    auto* head = new brls::Label();
    head->setText(title);
    head->setFontSize(28.0f);
    head->setMargins(16.0f, 0.0f, 6.0f, kInset);
    root->addView(head);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    auto* grid = new PosterGrid();
    scroll->setContentView(grid);
    root->addView(scroll);
    grid->load(q, scroll);

    auto* frame = new brls::AppletFrame(root);
    frame->setTitle("");
    brls::Application::pushActivity(new brls::Activity(frame));
}
