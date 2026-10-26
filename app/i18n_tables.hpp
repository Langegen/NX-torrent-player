#pragma once

#include <cstddef>

// Translation tables kept in their own files (i18n_ru.cpp, ...), so a language
// is one self-contained list. See i18n.hpp for how they are used.
namespace i18n_tables
{

struct Pair
{
    const char* en;  // the English source string, as written at the tr() call
    const char* tr;  // its translation
};

extern const Pair kRu[];
extern const size_t kRuCount;

} // namespace i18n_tables
