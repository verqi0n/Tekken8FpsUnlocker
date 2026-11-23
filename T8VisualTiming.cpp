#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include "T8VisualTiming.hpp"
#include "T8ActorInterpolation.hpp"
#include "T8FighterTargets.hpp"
#include "T8PoseInterpolation.hpp"
#include "Tekken8FpsUnlocker.hpp"

namespace tekken8 {
namespace {

SRWLOCK timingLock = SRWLOCK_INIT;
const void* currentWorld = nullptr; // Used only on the game thread.
std::uint64_t scene = 0, tick = 0;
std::int64_t started = 0;
double countsPerStep = 0;

}

VisualFrame visualFrame() noexcept {
    AcquireSRWLockShared(&timingLock);
    VisualFrame result{scene, tick, 0, started != 0};
    const auto stamp = started;
    ReleaseSRWLockShared(&timingLock);
    if (result.active) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        result.alpha = std::clamp(static_cast<double>(now.QuadPart - stamp) / countsPerStep, 0.0, 1.0);
    }
    return result;
}

void updateVisualWorld(const std::uint8_t* image, const void* world) noexcept {
    if (world != currentWorld) {
        AcquireSRWLockExclusive(&timingLock);
        ++scene;
        tick = 0;
        started = 0;
        ReleaseSRWLockExclusive(&timingLock);
        currentWorld = world;
    }
    refreshFighterTargets(image, world);
}

void completeVisualTick(std::int64_t startCounter) noexcept {
    if (!currentWorld)
        return;
    AcquireSRWLockExclusive(&timingLock);
    ++tick;
    started = startCounter;
    ReleaseSRWLockExclusive(&timingLock);
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

    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency))
        return false;
    countsPerStep = static_cast<double>(frequency.QuadPart) * gameplayStep;
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
        MH_Uninitialize();
        return false;
    }
    return true;
}

} // namespace tekken8
