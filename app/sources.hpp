#pragma once

#include "content.hpp"
#include "library.hpp"
#include "torrent_play.hpp"  // PlayHint

// The sources screen (roadmap mock-up "Выбор раздачи"): every release found
// for a film or an episode, from Jackett (searched by the title and by the
// original title, with the year for a film) and from the Stremio account's
// stream addons (by IMDb id), in one list.
//
// Each release name is read (release.hpp) into quality, source, codec,
// HDR/10-bit, voices and what part of a series it holds; releases for another
// season or episode are dropped, the rest sorted so the one that will simply
// play on a Switch comes first, marked "best choice". Filters: quality chips;
// X cycles the order (recommended / seeders / size). A plays, Y saves a
// Jackett result to the Local library.
//
// For a series the voice and the group picked are remembered, so the next
// episode's list puts the same dub first.
//
// season / episode: 0 / 0 for a film, or for a whole series ("Torrents" on a
// show's card).
void openSourcesScreen(const content::Item& item, int season, int episode,
                       int episodesInSeason = 0);

// What the player is told for one episode of a catalog show: its title, its
// art, progress against that episode, the next one. For a file picked by hand
// from a pack opened for the whole season (torrent_play.cpp).
PlayHint episodeHint(const content::Item& item, int season, int episode, int nextPop);

// The next episode of a show: from the same season pack when the last episode
// came from one that holds it (no list to pick from), else its sources screen.
void openNextEpisode(const content::Item& item, int season, int episode);

// "Continue" on a library entry (home row, card): back into the file that was
// playing, or the next episode when the last one is done.
void continueContent(const library::Entry& entry);
