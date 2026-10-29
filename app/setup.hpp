#pragma once

// The first-run setup: Jackett, the app's main source of releases. A screen
// with the server address, the API key and a check that also counts the
// indexers. Offered once, at startup, while no Jackett address is set; after
// that (finished or skipped) it lives in Options -> Jackett. The TMDB key
// ships in the build and the proxy is optional, so neither is asked here.
void maybeOfferJackettSetup();

// The same screen on demand.
void openJackettSetup();
