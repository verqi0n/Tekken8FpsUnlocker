#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>
#include "T8PoseInterpolation.hpp"
#include "T8VisualTiming.hpp"

namespace tekken8 {
void* originalPose = nullptr;

namespace {

// UE5 FTransform3d has three 32-byte fields (rotation, translation, scale).
struct alignas(16) Transform {
    double lanes[12];
};
static_assert(sizeof(Transform) == 0x60);

struct PoseArray {
    Transform* data;
    std::int32_t count;
    std::int32_t capacity;
};
static_assert(sizeof(PoseArray) == 16);

using ProcessPose = void (*)(void*, void*, void*, bool, PoseArray*, void*, void*, void*, void*);
// Verified against execTLerp's call at 0x5007DB1. RCX is the caller's return
// buffer, followed by A, B, alpha in XMM3 and the interpolation mode on the stack.
using BlendTransform = Transform* (*)(Transform*, const Transform*, const Transform*, float, std::uint8_t);
BlendTransform blendTransform = nullptr;

struct PoseHistory {
    const void* component = nullptr;
    const void* mesh = nullptr;
    const void* animation = nullptr;
    int lod = 0;
    std::uint64_t scene = 0;
    std::uint64_t tick = 0;
    std::uint64_t used = 0;
    bool hasPair = false;
    std::vector<Transform> previous;
    std::vector<Transform> current;
};

std::array<PoseHistory, 2> histories;
std::mutex historyMutex;
std::uint64_t lastUsed = 0;

PoseHistory& historyFor(const void* component, std::uint64_t scene) noexcept {
    for (auto& h : histories) {
        if (h.component == component && h.scene == scene)
            return h;
    }
    return histories[histories[0].used <= histories[1].used ? 0 : 1];
}

void interpolatePose(const void* component, const void* mesh, const void* animation,
                     bool evaluated, PoseArray& pose, const VisualFrame& frame) {
    // GetPredictedLODLevel reads this int in the supported build (0x4B16E65).
    const int lod = *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(component) + 0x790);
    const auto count = static_cast<std::size_t>(pose.count);
    const auto bytes = count * sizeof(Transform);
    std::lock_guard<std::mutex> lock(historyMutex);
    auto& h = historyFor(component, frame.scene);
    const bool sameMesh = h.component == component && h.scene == frame.scene && h.mesh == mesh &&
                          h.animation == animation && h.lod == lod && h.current.size() == count;
    if (sameMesh && frame.tick < h.tick)
        return;

    if (!sameMesh || frame.tick - h.tick > 1) {
        // Blending requires two consecutive poses from the same mesh and LOD.
        if (!evaluated)
            return;
        h.previous.resize(count);
        h.current.assign(pose.data, pose.data + count);
        h.mesh = mesh;
        h.component = component;
        h.animation = animation;
        h.lod = lod;
        h.scene = frame.scene;
        h.tick = frame.tick;
        h.used = ++lastUsed;
        h.hasPair = false;
        return;
    }

    if (frame.tick != h.tick) {
        if (!evaluated)
            return;
        h.previous.swap(h.current);
        std::memcpy(h.current.data(), pose.data, bytes);
        h.tick = frame.tick;
        h.hasPair = true;
    }
    h.used = ++lastUsed;
    if (!h.hasPair)
        return;

    const auto alpha = static_cast<float>(std::clamp(frame.alpha, 0.0, 1.0));
    if (alpha == 0.0f || alpha == 1.0f) {
        std::memcpy(pose.data, alpha == 0.0f ? h.previous.data() : h.current.data(), bytes);
        return;
    }
    for (std::size_t i = 0; i < count; ++i)
        blendTransform(&pose.data[i], &h.previous[i], &h.current[i], alpha, 0);
}

void processPose(void* component, void* mesh, void* animation, bool evaluate, PoseArray* spaceBases,
                 void* boneSpace, void* rootTranslation, void* curves, void* attributes) {
    const VisualFrame start = visualFrame();
    reinterpret_cast<ProcessPose>(originalPose)(component, mesh, animation, evaluate, spaceBases,
                                               boneSpace, rootTranslation, curves, attributes);
    if (!start.active || !component || !mesh || !spaceBases || !spaceBases->data ||
        spaceBases->count <= 0 || spaceBases->count > spaceBases->capacity || !isFighterMesh(component))
        return;
    const VisualFrame end = visualFrame();
    if (!end.active || start.scene != end.scene || start.tick != end.tick)
        return;
    try {
        interpolatePose(component, mesh, animation, evaluate, *spaceBases, end);
    } catch (const std::bad_alloc&) {
        // The output still contains the native pose if allocating history fails.
    }
}

} // namespace

void* poseHook() noexcept {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base)
        return nullptr;
    constexpr std::uint8_t prologue[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0xF3, 0x0F, 0x11, 0x58, 0x20, 0x55
    };
    auto* target = base + 0x4FAAA80;
    if (std::memcmp(target, prologue, sizeof prologue))
        return nullptr;
    blendTransform = reinterpret_cast<BlendTransform>(target);
    return reinterpret_cast<void*>(&processPose);
}

} // namespace tekken8
