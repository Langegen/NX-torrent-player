#pragma once

// The picture on the console's own screen: brightness, contrast, gamma and
// saturation, through mpv's video equalizer. Handheld only -- docked, the TV
// has its own picture settings and is the one to calibrate, so the player
// sends neutral values there and swaps them live when the console goes in or
// out of the dock. The values live in config (picBrightness...).

#include <string>

struct mpv_handle;

namespace picture
{

enum Prop
{
    BRIGHTNESS,
    CONTRAST,
    GAMMA,
    SATURATION,
    PROP_COUNT,
};

// One value per Prop, as mpv takes them: -100..100, 0 untouched.
struct Eq
{
    int v[PROP_COUNT] = {};
};

// The name shown for a setting (translated).
std::string label(int prop);
// The stored setting.
int& value(int prop);
// "0", "+5", "-12".
std::string valueText(int v);

// Steps a setting by `dir` within -100..100. False at an end.
bool step(int prop, int dir);

// The console is in handheld mode (the settings apply).
bool handheld();

// Brings mpv to what it should show right now -- the settings in handheld,
// zeros docked -- touching only what differs from `applied`, which it then
// updates. Cheap enough to call every frame.
void sync(mpv_handle* mpv, Eq& applied);

// The calibration screen: test patterns drawn by mpv, so the settings act on
// them exactly as they act on a film, with the settings beside them.
void openCalibration();

} // namespace picture
