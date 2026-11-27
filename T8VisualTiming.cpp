#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <immintrin.h>
#include "T8VisualTiming.hpp"
#include "T8ActorInterpolation.hpp"
#include "T8FighterTargets.hpp"
#include "T8PoseInterpolation.hpp"

namespace tekken8 {
namespace {

// Single writer (the game thread), many readers (render and animation threads).
// The sequence is odd while the game thread publishes a new frame.
alignas(64) std::atomic<std::uint64_t> sequence{0};
std::atomic<std::uint64_t> scene{0}, tick{0};
std::atomic<double> alpha{1.0};
const void* currentWorld = nullptr; // Used only on the game thread.

template<class Update>
void publish(Update update) noexcept {
    const auto next = sequence.load(std::memory_order_relaxed) + 1;
    sequence.store(next, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    update();
    sequence.store(next + 1, std::memory_order_release);
}

}

VisualFrame visualFrame() noexcept {
    for (;;) {
        const auto before = sequence.load(std::memory_order_acquire);
        if (before & 1) {
            _mm_pause();
            continue;
        }
        const VisualFrame result{scene.load(std::memory_order_relaxed), tick.load(std::memory_order_relaxed),
                                 alpha.load(std::memory_order_relaxed)};
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence.load(std::memory_order_relaxed) == before)
            return result;
    }
}

void updateVisualWorld(const std::uint8_t* image, const void* world) noexcept {
    if (world != currentWorld) {
        publish([] {
            scene.store(scene.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
            tick.store(0, std::memory_order_relaxed);
            alpha.store(1.0, std::memory_order_relaxed);
        });
        currentWorld = world;
    }
    refreshFighterTargets(image, world);
}

void beginVisualTicks() noexcept {
    // Transforms written during a tick belong to its end state.
    if (currentWorld)
        publish([] { alpha.store(1.0, std::memory_order_relaxed); });
}

void completeVisualTick() noexcept {
    if (currentWorld)
        publish([] { tick.store(tick.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); });
}

void setVisualPhase(double phase) noexcept {
    if (!currentWorld)
        return;
    phase = phase > 0.0 ? (phase < 1.0 ? phase : 1.0) : 0.0; // Also maps NaN to 0.
    publish([phase] { alpha.store(phase, std::memory_order_relaxed); });
}

bool installVisualInterpolation(std::uint8_t* image) noexcept {
    constexpr std::uint8_t cameraPrologue[]{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x48,0x81,0xEC,0x50,0x08,0x00,0x00};
    constexpr std::uint8_t actorPrologue[]{0x48,0x81,0xEC,0x68,0x01,0x00,0x00,0x48,0x8B,0x89,0xA0,0x01,0x00,0x00};
    constexpr std::uint8_t posePrologue[]{0x48,0x85,0xD2,0x0F,0x84,0x49,0x07,0x00,0x00,0x55,0x53,0x57,0x41,0x55,0x41,0x57};
    auto* camera = image + 0x05222260;
    auto* actor = image + 0x04B483E0;
    auto* pose = image + 0x04DD42E0;
    if (std::memcmp(camera, cameraPrologue, sizeof cameraPrologue) ||
        std::memcmp(actor, actorPrologue, sizeof actorPrologue) ||
        std::memcmp(pose, posePrologue, sizeof posePrologue))
        return false;

    struct Hook { void* entry; void* replacement; void** original; };
    const Hook hooks[]{{camera, cameraHook(), &originalCamera}, {actor, actorHook(), &originalActor},
                       {pose, poseHook(), &originalPose}};
    if (!hooks[2].replacement || MH_Initialize() != MH_OK)
        return false;
    for (const auto& hook : hooks) {
        if (MH_CreateHook(hook.entry, hook.replacement, hook.original) != MH_OK ||
            MH_QueueEnableHook(hook.entry) != MH_OK) {
            MH_Uninitialize();
            return false;
        }
    }
    if (MH_ApplyQueued() != MH_OK) {
        // Earlier entries in the batch may already have run on another thread.
        uninstallVisualInterpolation();
        return false;
    }
    return true;
}

void uninstallVisualInterpolation() noexcept {
    // Installation rollback runs before the tick hook publishes its first frame.
    // MinHook can restore the entry points but cannot join active detours. Keep
    // their original-call trampolines alive with this pinned ASI, even if an
    // entry point cannot be restored and must keep calling through.
    MH_DisableHook(MH_ALL_HOOKS);
}

} // namespace tekken8
