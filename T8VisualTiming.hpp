#pragma once

#include <cstdint>

namespace tekken8 {

struct VisualFrame {
    std::uint64_t scene;
    std::uint64_t tick; // Zero until the first completed tick in this scene.
    double alpha;       // Position between the last two ticks, fixed for the whole render frame.
};

VisualFrame visualFrame() noexcept;

bool installVisualInterpolation(std::uint8_t* image) noexcept;
void uninstallVisualInterpolation() noexcept;

// Game thread only.
void updateVisualWorld(const std::uint8_t* image, const void* world) noexcept;
void beginVisualTicks() noexcept;
void completeVisualTick() noexcept;
void setVisualPhase(double phase) noexcept;

}
