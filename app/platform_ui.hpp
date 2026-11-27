#pragma once

#include <borealis.hpp>

#include <algorithm>
#include <cstdlib>

#include "android_bridge.hpp"

namespace platform_ui
{

// True on Android TV (Leanback / TELEVISION UI mode) where 10-foot D-pad UX applies.
inline bool isTv()
{
#if defined(__ANDROID__) || defined(ANDROID)
    return androidIsTv();
#else
    return false;
#endif
}

// True on Android phones & tablets (not Android TV), or on PC when NX_MOBILE_UI=1.
inline bool isMobile()
{
#if defined(__ANDROID__) || defined(ANDROID)
    return !androidIsTv();
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

inline bool isTablet()
{
#if defined(__ANDROID__) || defined(ANDROID)
    return isMobile() && androidSmallestWidthDp() >= 600;
#else
    return false;
#endif
}

inline bool isPhone()
{
    return isMobile() && !isTablet();
}

inline bool isPortrait()
{
    return brls::Application::contentHeight > brls::Application::contentWidth;
}

inline void safeInsets(float& top, float& right, float& bottom, float& left)
{
    top = right = bottom = left = 0.0f;
#if defined(__ANDROID__) || defined(ANDROID)
    int t = 0, r = 0, b = 0, l = 0;
    androidSafeInsetsPx(t, r, b, l);
    float scale = brls::Application::windowScale > 0.01f ? brls::Application::windowScale : 1.0f;
    top    = (float)t / scale;
    right  = (float)r / scale;
    bottom = (float)b / scale;
    left   = (float)l / scale;
#endif
}

inline float safeInsetTop()
{
    float t, r, b, l;
    safeInsets(t, r, b, l);
    return t;
}

inline float safeInsetLeft()
{
    float t, r, b, l;
    safeInsets(t, r, b, l);
    return l;
}

inline float safeInsetRight()
{
    float t, r, b, l;
    safeInsets(t, r, b, l);
    return r;
}

inline float safeInsetBottom()
{
    float t, r, b, l;
    safeInsets(t, r, b, l);
    return b;
}

inline float sideInset()
{
    float base = isMobile() ? (isPortrait() ? 18.0f : 32.0f) : 60.0f;
    if (isMobile())
    {
        float t, r, b, l;
        safeInsets(t, r, b, l);
        float cut = std::max(l, r);
        if (cut > 0.0f) base = std::max(base, cut + 8.0f);
    }
    return base;
}

} // namespace platform_ui
