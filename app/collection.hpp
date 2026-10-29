#pragma once

#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>

#include "tmdb.hpp"

// A wrapping grid of posters fed by a TMDB query, one page at a time: the
// next page is asked for as the scroll nears the bottom. A collection's "See
// all" screen is one; the search screen embeds one for its results.
class PosterGrid : public brls::Box
{
  public:
    PosterGrid();
    ~PosterGrid() override;

    // Drops what is shown and starts over on `q` (page 1). `scroll` is the
    // frame the grid sits in, watched for the bottom.
    void load(const tmdb::ListQuery& q, brls::ScrollingFrame* scroll);
    // Applied to each page before it is shown (the search's genre filter).
    std::function<bool(const content::Item&)> filter;
    // Where the focus goes when the grid is emptied under it.
    brls::View* parkTarget = nullptr;
    // Set before load(): when the page comes in, move the focus from
    // `parkTarget` onto the first result (a genre tile was pressed: it is gone).
    bool focusResultsOnce = false;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    tmdb::ListQuery query;
    brls::ScrollingFrame* scroll = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    brls::Box* row  = nullptr;  // the row being filled
    brls::Label* status = nullptr;
    int columns = 6;
    int shown   = 0;
    int page = 0, totalPages = 1;
    bool loading = false;
    brls::View* firstCard = nullptr;  // freed with the grid's rows (load() clears it)

    void next();
    void add(const content::Item& it);
    void focusFirstIfLost();
};

// The full screen of one collection ("Top rated movies" and so on).
void openCollection(const std::string& title, const tmdb::ListQuery& q);
