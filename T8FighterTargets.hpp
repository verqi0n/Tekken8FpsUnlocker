#pragma once

#include <cstdint>

namespace tekken8 {

struct FighterTarget {
    unsigned slot;
    std::uint64_t objectId;
};

// A game-thread actor callback supplies a live AActor. A zero ID means it is not a current fighter.
FighterTarget fighterTarget(const void* actor) noexcept;
// Safe on any thread. A zero ID means the component is not a current fighter's main mesh.
FighterTarget fighterMesh(const void* component) noexcept;

// Called from the game thread while the viewport world and its levels are valid.
void refreshFighterTargets(const std::uint8_t* image, const void* world) noexcept;

}
