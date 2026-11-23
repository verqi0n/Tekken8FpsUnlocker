#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "T8ActorInterpolation.hpp"
#include "T8VisualTiming.hpp"

namespace tekken8 {

void* originalCamera = nullptr;
void* originalActor = nullptr;

namespace {

// Tekken 8 Steam build 26651490 uses double-precision FVector and FRotator.
struct Triple { double x, y, z; };
struct Transform { Triple position, rotation; };
static_assert(sizeof(Transform) == 0x30);

using CameraUpdate = void (*)(void*, float);
using SetActorTransform = bool (*)(void*, const Triple*, const Triple*, bool, void*, std::uint8_t);
using RotationLerp = Triple* (*)(Triple*, const Triple*, const Triple*, float, bool);
using GetActorWorld = void* (*)(void*);
using IsWorldPaused = bool (*)(void*);

std::uintptr_t imageBase() noexcept {
    static const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

template<class Function>
Function gameFunction(std::uintptr_t rva) noexcept {
    return reinterpret_cast<Function>(imageBase() + rva);
}

const std::uint8_t* pointerAt(const void* instance, std::size_t offset) noexcept {
    const std::uint8_t* pointer;
    std::memcpy(&pointer, static_cast<const std::uint8_t*>(instance) + offset, sizeof pointer);
    return pointer;
}

bool finite(const Triple& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Sample once per gameplay tick so render callbacks cannot overwrite the endpoints.
struct History {
    const void* owner = nullptr;
    std::uint64_t scene = 0;
    std::uint64_t tick = 0;
    Transform previous{}, current{};

    Transform sample(const void* object, const VisualFrame& frame, const Transform& source) noexcept {
        if (owner != object || scene != frame.scene || (tick != frame.tick && frame.tick - tick != 1)) {
            owner = object;
            scene = frame.scene;
            tick = frame.tick;
            previous = current = source;
            return source;
        }
        if (tick != frame.tick) {
            previous = current;
            current = source;
            tick = frame.tick;
        }
        if (!std::isfinite(frame.alpha))
            return current;
        const double alpha = std::clamp(frame.alpha, 0.0, 1.0);
        if (alpha == 0.0) return previous;
        if (alpha == 1.0) return current;
        const auto& p = previous.position;
        const auto& q = current.position;
        Transform result{{p.x + (q.x-p.x)*alpha, p.y + (q.y-p.y)*alpha,
                          p.z + (q.z-p.z)*alpha}, current.rotation};
        if (std::memcmp(&previous.rotation, &current.rotation, sizeof(Triple)) != 0) {
            // The last argument to UKismetMathLibrary::RLerp selects the shortest rotation.
            gameFunction<RotationLerp>(0x04FA3940)(&result.rotation, &previous.rotation,
                                                    &current.rotation, static_cast<float>(alpha), true);
        }
        return result;
    }
};

void updateCamera(void* manager, float seconds) noexcept {
    reinterpret_cast<CameraUpdate>(originalCamera)(manager, seconds);
    const auto frame = visualFrame();
    static thread_local History history;
    if (frame.tick == 0 || !manager) {
        history.owner = nullptr;
        return;
    }

    auto* camera = static_cast<std::uint8_t*>(manager);
    // The menu camera also calls DoUpdateCamera. Interpolate only the battle camera.
    if (reinterpret_cast<std::uintptr_t>(pointerAt(camera, 0)) != imageBase() + 0x078C28F0)
        return;
    const auto* controller = pointerAt(camera, 0x298);  // APlayerCameraManager::PCOwner
    if (!controller || pointerAt(controller, 0x350) != camera)
        return;

    // CameraCachePrivate.POV stores location and rotation consecutively.
    Transform source;
    std::memcpy(&source, camera + 0x22D0, sizeof source);
    auto* world = gameFunction<GetActorWorld>(0x04B3F450)(manager);
    std::uint32_t flags;
    std::memcpy(&flags, camera + 0x3320, sizeof flags);
    if (!world || gameFunction<IsWorldPaused>(0x05057A60)(world) ||
        (flags & 0x400) || !finite(source.position) || !finite(source.rotation)) {
        history.owner = nullptr;
        return;
    }

    const Transform view = history.sample(manager, frame, source);
    std::memcpy(camera + 0x22D0, &view, sizeof view);
}

bool setActorTransform(void* actor, const Triple* position, const Triple* rotation,
                       bool sweep, void* hitResult, std::uint8_t teleport) noexcept {
    auto original = reinterpret_cast<SetActorTransform>(originalActor);
    const auto frame = visualFrame();
    static thread_local History fighters[2];
    if (frame.tick == 0) {
        fighters[0].owner = fighters[1].owner = nullptr;
        return original(actor, position, rotation, sweep, hitResult, teleport);
    }
    if (!actor || !position || !rotation || !isFighterActor(actor))
        return original(actor, position, rotation, sweep, hitResult, teleport);

    History* state = nullptr;
    for (auto& candidate : fighters) {
        if (candidate.owner == actor && candidate.scene == frame.scene) {
            state = &candidate;
            break;
        }
        if (!state || candidate.scene != frame.scene || !candidate.owner ||
            (state->scene == frame.scene && candidate.tick < state->tick))
            state = &candidate;
    }
    // Swept moves and ResetPhysics teleports need the exact destination for collision handling.
    if (sweep || teleport == 2 || !finite(*position) || !finite(*rotation)) {
        state->owner = nullptr;
        return original(actor, position, rotation, sweep, hitResult, teleport);
    }
    const Transform result = state->sample(actor, frame, {*position, *rotation});
    return original(actor, &result.position, &result.rotation, sweep, hitResult, teleport);
}

} // namespace

void* cameraHook() noexcept { return reinterpret_cast<void*>(&updateCamera); }
void* actorHook() noexcept { return reinterpret_cast<void*>(&setActorTransform); }

} // namespace tekken8
