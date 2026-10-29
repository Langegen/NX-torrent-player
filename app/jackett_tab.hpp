#pragma once

#include <borealis.hpp>

#include <string>

// Builds the Jackett search tab root view.
brls::View* buildJackettTab();

// Opens Jackett search as its own screen, already searching `query` in
// `category` (a jackett::Category). B returns to the caller.
void openJackettSearch(const std::string& query, int category = 0);
