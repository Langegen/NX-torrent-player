#pragma once

#include <borealis.hpp>

// The catalog tabs (roadmap mock-up "Главный экран"). view 0 = Home: a hero
// for the top trending title, "Continue watching" (the library, then files
// from the history), then TMDB rows. 1 = Movies, 2 = Shows: rows of that
// kind, by list and by genre. A opens a card, Y bookmarks a poster, X takes a
// title out of Continue, "-" searches.
//
// Without a TMDB key the rows are replaced by a note saying where to enter
// one; Continue watching works either way.
brls::View* buildHomeTab(int view = 0);

// The TMDB search: keyboard, then films and shows on a results page. The
// header's "Search" button and "-" on the catalog tabs.
void openCatalogSearch();
