/*
    Copyright 2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <borealis/core/font.hpp>

namespace brls
{

// Font loader using the pl system service
class SwitchFontLoader : public FontLoader
{
  public:
    void loadFonts() override;

    // LOCAL PATCH (see VENDORED.md): whether to make the app's bundled
    // romfs:/SpaceGrotesk-Medium.ttf the primary font. Set before
    // Application::init(). The app turns it off for a language the font has
    // no glyphs for (Russian), so the UI is one system font instead of Space
    // Grotesk for Latin and the system font for everything else.
    static bool useBundledFont;
};

} // namespace brls
