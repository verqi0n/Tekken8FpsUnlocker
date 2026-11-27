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
    int battlePhase = -1;
    bool manualFighter = false;
    bool ghostFighter = false;
    bool replay = false;
    bool spectating = false;
    bool menuOpen = false;

    bool allowsUnlock() const noexcept;
};

// Battle phases from the intro to the last round end, excluding loading, the winner pose and results.
bool isFightPhase(int phase) noexcept;

bool hasManualFighterOnSide(const void* const* entries, std::size_t count, unsigned side) noexcept;

// Queried on the game thread, including after ticks that can change the scene or role.
bool isLocalFight(const std::uint8_t* image, const void* world) noexcept;

}
