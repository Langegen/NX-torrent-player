#pragma once

#include "content.hpp"

// The film / series card (roadmap mock-up "Карточка"): the backdrop across the
// screen darkened on the left, the poster, the title, a row of chips (TMDB
// rating, year, genres, length or seasons), the synopsis and the actions; for
// a series a season switcher (L/R) and that season's episodes as stills.
//
// Opens at once with what the list already knew and fills in the rest
// (genres, runtime, seasons, the IMDb id) when the details arrive.
void openContentCard(const content::Item& item);

// "Watch" without the card: the hero's button on the home screen. A film goes
// straight to its sources; a series opens the card, since an episode has to
// be picked first.
void watchContent(const content::Item& item);
