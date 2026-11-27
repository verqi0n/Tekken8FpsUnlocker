#pragma once

#include <cstddef>
#include <cstdint>

namespace tekken8 {

struct FightActivity {
    int matchMode = -1;
    int networkMode = -1;
    int side = -1;
    int sideRole = -1;
    int playerKind = -1;
    bool battleMain = false;
    bool manualFighter = false;
    bool ghostFighter = false;
    bool replay = false;
    bool spectating = false;
    bool menuOpen = false;

    bool allowsUnlock() const noexcept;
};

bool hasManualFighterOnSide(const void* const* entries, std::size_t count, unsigned side) noexcept;

// Queried on the game thread, including after ticks that can change the scene or role.
bool isLocalFight(const std::uint8_t* image, const void* world) noexcept;

}
