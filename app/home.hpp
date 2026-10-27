#pragma once

#include <borealis.hpp>

// The Home tab (roadmap mock-up "Главный экран"): a hero banner for the top
// trending title, "Continue watching" from the local history, then TMDB rows
// (trending, popular and top-rated films and series, in cinemas). A opens a
// title's card, "-" searches TMDB.
//
// Without a TMDB key the rows are replaced by a note saying where to enter
// one; Continue watching works either way.
brls::View* buildHomeTab();
