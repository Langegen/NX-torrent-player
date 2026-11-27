#pragma once

#include "content.hpp"

// Full-screen online sources browser (Lampa/Lampac & NX Online Plugin v1):
// lets the user pick a balancer, voice-over, season, episode, and quality
// without torrents.
void openOnlineScreen(const content::Item& item, int season = 0, int episode = 0,
                      int episodesInSeason = 0);
