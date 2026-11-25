#pragma once

#include <borealis.hpp>

#include <cstdlib>

namespace platform_ui
{

// True on Android builds, or on PC desktop when NX_MOBILE_UI=1 is set for visual debugging.
inline bool isMobile()
{
#if defined(__ANDROID__) || defined(ANDROID)
    return true;
#else
    static int cached = -1;
    if (cached < 0)
    {
        const char* e = std::getenv("NX_MOBILE_UI");
        cached = (e && e[0] == '1') ? 1 : 0;
    }
    return cached == 1;
#endif
}

inline bool isPortrait()
{
    return brls::Application::contentHeight > brls::Application::contentWidth;
}

inline float sideInset()
{
    return isMobile() ? (isPortrait() ? 20.0f : 36.0f) : 60.0f;
}

} // namespace platform_ui
