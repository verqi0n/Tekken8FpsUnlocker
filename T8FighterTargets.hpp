#pragma once

#include <cstdint>

namespace tekken8 {

struct FighterTarget {
    unsigned slot;
    std::uint64_t objectId;
};

// A game-thread actor callback supplies a live AActor. A zero ID means it is not a current fighter.
FighterTarget fighterTarget(const void* actor) noexcept;
std::uint64_t fighterMeshIdentity(const void* component) noexcept;

// Called from the game thread while the viewport world and its levels are valid.
void refreshFighterTargets(const std::uint8_t* image, const void* world) noexcept;

}
