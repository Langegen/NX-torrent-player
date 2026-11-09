/*
    Copyright 2019  WerWolv
    Copyright 2019  p-sam
    Copyright 2023 xfangfang

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

#include <switch.h>

#include <borealis/platforms/switch/switch_ime.hpp>
#include <cstring>
#include <iostream>
#include <vector>

namespace brls
{

static SwkbdConfig createSwkbdBaseConfig(std::string headerText, std::string subText, int maxStringLength, std::string initialText)
{
    SwkbdConfig config;

    swkbdCreate(&config, 0);

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config, headerText.c_str());
    swkbdConfigSetSubText(&config, subText.c_str());
    swkbdConfigSetStringLenMax(&config, maxStringLength);
    swkbdConfigSetInitialText(&config, initialText.c_str());
    swkbdConfigSetBlurBackground(&config, true);

    return config;
}

int getSwkbdKeyDisableBitmask(int borealis_key)
{
    // translate brls to Switch libnx values
    int ret = 0;
    if (borealis_key == brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_NONE)
        return 0;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_SPACE)
        // Disable space-bar
        ret |= SwkbdKeyDisableBitmask_Space;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_AT)
        // Disable '@'.
        ret |= SwkbdKeyDisableBitmask_At;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_PERCENT)
        // Disable '%'.
        ret |= SwkbdKeyDisableBitmask_Percent;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_FORWSLASH)
        // Disable '/'.
        ret |= SwkbdKeyDisableBitmask_ForwardSlash;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_BACKSLASH)
        // Disable '\'.
        ret |= SwkbdKeyDisableBitmask_Backslash;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_NUMBERS)
        // Disable numbers.
        ret |= SwkbdKeyDisableBitmask_Numbers;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_DOWNLOADCODE)
        // Used for swkbdConfigMakePresetDownloadCode.
        ret |= SwkbdKeyDisableBitmask_DownloadCode;

    if (borealis_key & brls::KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_USERNAME)
        // Used for swkbdConfigMakePresetUserName. Disables '@', '%', and '\'.
        ret |= SwkbdKeyDisableBitmask_UserName;

    return ret;
}

bool SwitchImeManager::openForText(std::function<void(std::string)> f, std::string headerText,
    std::string subText, int maxStringLength, std::string initialText,
    int kbdDisableBitmask)
{
    // LOCAL PATCH (NX-torrent-player). The stock version froze the console on
    // the server-address fields -- the only calls passing a sub text, and the
    // only ones that hung. Above 32 characters swkbd draws the input as a box
    // and shows no header or sub text anyway (libnx, SwkbdTextDrawType), so
    // the hint goes in as the guide text, shown in the empty field: the way
    // the app's own magnet prompt has always called it. Also: a failed
    // swkbdCreate is no longer used, and the answer has room for a full
    // maxStringLength of UTF-8 (it was a fixed 256 bytes).
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return false;

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, SwkbdType_All);  // Cyrillic in a search
    swkbdConfigSetHeaderText(&config, headerText.c_str());
    if (maxStringLength > 0 && maxStringLength <= 32)
        swkbdConfigSetSubText(&config, subText.c_str());
    swkbdConfigSetGuideText(&config, (subText.empty() ? headerText : subText).c_str());
    if (maxStringLength > 0)
        swkbdConfigSetStringLenMax(&config, maxStringLength);
    if (!initialText.empty())
        swkbdConfigSetInitialText(&config, initialText.c_str());
    swkbdConfigSetKeySetDisableBitmask(&config, getSwkbdKeyDisableBitmask(kbdDisableBitmask));

    std::vector<char> buffer((maxStringLength > 64 ? maxStringLength : 64) * 4 + 1, 0);
    Result rc = swkbdShow(&config, buffer.data(), buffer.size());
    swkbdClose(&config);

    if (R_FAILED(rc))
        return false;

    f(std::string(buffer.data()));
    return true;
}

bool SwitchImeManager::openForNumber(std::function<void(long)> f, std::string headerText,
    std::string subText, int maxStringLength, std::string initialText,
    std::string leftButton, std::string rightButton,
    int kbdDisableBitmask)
{
    SwkbdConfig config = createSwkbdBaseConfig(headerText, subText, maxStringLength, initialText);

    swkbdConfigSetType(&config, SwkbdType_NumPad);
    swkbdConfigSetLeftOptionalSymbolKey(&config, leftButton.c_str());
    swkbdConfigSetRightOptionalSymbolKey(&config, rightButton.c_str());
    swkbdConfigSetKeySetDisableBitmask(&config, getSwkbdKeyDisableBitmask(kbdDisableBitmask));

    char buffer[0x100];

    if (R_SUCCEEDED(swkbdShow(&config, buffer, 0x100)) && strcmp(buffer, "") != 0)
    {
        f(std::stoll(buffer));

        swkbdClose(&config);
        return true;
    }

    swkbdClose(&config);

    return false;
}

};