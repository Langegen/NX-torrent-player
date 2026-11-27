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
#ifdef BOREALIS_USE_OPENGL
#include <glad/glad.h>
#endif

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
    // LOCAL PATCH (NX-torrent-player). Zero-init SwkbdConfig, clamp to HOS limit
    // (500), disable BlurBackground (avoids live OpenGL framebuffer snapshot),
    // skip header/sub text above 32 chars (swkbd uses box mode), and flush GL
    // before swkbdShow() to avoid 4IFIR/SaltyNX nwindow/nvdrv deadlocks/aborts.
    SwkbdConfig config = {};
    if (R_FAILED(swkbdCreate(&config, 0)))
        return false;

    if (maxStringLength <= 0)
        maxStringLength = 64;
    if (maxStringLength > 500)
        maxStringLength = 500;

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, SwkbdType_All);  // Cyrillic in a search
    swkbdConfigSetBlurBackground(&config, false);
    if (maxStringLength <= 32)
    {
        if (!headerText.empty())
            swkbdConfigSetHeaderText(&config, headerText.c_str());
        if (!subText.empty())
            swkbdConfigSetSubText(&config, subText.c_str());
    }
    swkbdConfigSetGuideText(&config, (subText.empty() ? headerText : subText).c_str());
    swkbdConfigSetStringLenMax(&config, maxStringLength);
    if (!initialText.empty())
        swkbdConfigSetInitialText(&config, initialText.c_str());
    swkbdConfigSetKeySetDisableBitmask(&config, getSwkbdKeyDisableBitmask(kbdDisableBitmask));

#ifdef BOREALIS_USE_OPENGL
    glFinish();
#endif

    std::vector<char> buffer((size_t)maxStringLength * 4 + 1, 0);
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
    // LOCAL PATCH (NX-torrent-player): safe init, unchecked swkbdCreate guard,
    // disabled background blur, and glFinish() before swkbdShow().
    SwkbdConfig config = {};
    if (R_FAILED(swkbdCreate(&config, 0)))
        return false;

    if (maxStringLength <= 0)
        maxStringLength = 18;
    if (maxStringLength > 20)
        maxStringLength = 20;

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, SwkbdType_NumPad);
    swkbdConfigSetBlurBackground(&config, false);
    if (!headerText.empty())
        swkbdConfigSetHeaderText(&config, headerText.c_str());
    if (!subText.empty())
        swkbdConfigSetSubText(&config, subText.c_str());
    swkbdConfigSetGuideText(&config, (subText.empty() ? headerText : subText).c_str());
    swkbdConfigSetStringLenMax(&config, maxStringLength);
    if (!initialText.empty())
        swkbdConfigSetInitialText(&config, initialText.c_str());
    swkbdConfigSetLeftOptionalSymbolKey(&config, leftButton.c_str());
    swkbdConfigSetRightOptionalSymbolKey(&config, rightButton.c_str());
    swkbdConfigSetKeySetDisableBitmask(&config, getSwkbdKeyDisableBitmask(kbdDisableBitmask));

#ifdef BOREALIS_USE_OPENGL
    glFinish();
#endif

    char buffer[0x100] = { 0 };
    Result rc          = swkbdShow(&config, buffer, sizeof(buffer));
    swkbdClose(&config);

    if (R_SUCCEEDED(rc) && buffer[0] != '\0')
    {
        try
        {
            f(std::stoll(buffer));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    return false;
}

};