#pragma once

#include <string>

#include "jackett.hpp"
#include "player.hpp"  // PlayerArt, WatchInfo

// Playing a torrent that is not open yet: a Jackett result (a link to a
// .torrent, or a magnet) or a bare magnet. A "Preparing torrent..." dialog
// (cancellable) covers the download / metadata fetch; then the file to play
// is picked:
//   - the hint names an episode and one file matches it -> that file, no
//     question asked (a season pack opened from the card's episode);
//   - one video only -> that one;
//   - otherwise the file picker.
struct PlayHint
{
    std::string title;       // shown while loading, and in the picker
    int season  = 0;         // > 0 with episode: pick that file in a pack
    int episode = 0;
    PlayerArt art;
    WatchInfo watch;
    std::string sourceType = "Jackett";
};

void playJackettItem(const jackett::Item& item, const PlayHint& hint);
void playMagnet(const std::string& magnet, const PlayHint& hint);
