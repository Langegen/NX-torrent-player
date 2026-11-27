#pragma once

#include <string>

// Platform bootstrap for Android:
//  - changes working directory to SDL internal writable storage
//  - extracts cacert.pem, subfont.ttf, and test pattern PNGs from romfs
//  - caches JNI class/method refs on main thread & sets JavaVM on FFmpeg
void androidInitPlatform();

// Returns writable internal storage path (or ".").
const char* androidStoragePath();

// Returns and clears any pending launch magnet/file URI captured by MainActivity.
std::string androidConsumePendingIntentUri();

// Android device form-factor & display helpers (safe defaults on non-Android).
bool androidIsTv();
float androidDisplayDensity();
int androidSmallestWidthDp();
void androidSafeInsetsPx(int& top, int& right, int& bottom, int& left);
