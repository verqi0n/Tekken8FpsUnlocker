#pragma once

#include <windows.h>

namespace tekken8 {

struct Settings {
    float targetFps = 300.0f;
    // Apply TargetFPS in menus, replays and every other scene after the startup map.
    bool unlockEverywhere = false;
};

Settings readSettings(HMODULE module);

} // namespace tekken8
