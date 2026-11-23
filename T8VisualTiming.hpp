#pragma once

#include <cstdint>

namespace tekken8 {

struct VisualFrame {
    std::uint64_t scene;
    std::uint64_t tick; // Zero until the first completed tick in this scene.
    double alpha;
};

VisualFrame visualFrame() noexcept;
bool isFighterActor(const void* actor) noexcept;
bool isFighterMesh(const void* component) noexcept;

bool installVisualInterpolation(std::uint8_t* image) noexcept;
void uninstallVisualInterpolation() noexcept;
void updateVisualWorld(const std::uint8_t* image, const void* world) noexcept;
void completeVisualTick(std::int64_t startCounter) noexcept;

}
