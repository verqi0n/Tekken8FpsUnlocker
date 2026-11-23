#pragma once

#include <cstdint>

namespace tekken8 {

// Called from the game thread while the viewport world and its levels are valid.
void refreshFighterTargets(const std::uint8_t* image, const void* world) noexcept;

}
