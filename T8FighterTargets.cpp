#include "T8FighterTargets.hpp"
#include "T8VisualTiming.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>

namespace tekken8 {
namespace {

// Native PolarisCharacterActor::GetPrivateStaticClass, build 26651490.
constexpr std::uintptr_t fighterClassRva = 0x9935C10;
constexpr std::uint32_t excludedFlags = 0x10 | 0x8000 | 0x10000; // CDO, BeginDestroyed, FinishDestroyed

template<class T>
T field(const void* object, std::size_t offset) noexcept {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

struct ObjectArray {
    const void* const* data;
    std::int32_t size;
    std::int32_t capacity;

    const void* at(std::int32_t index) const noexcept {
        return data && index >= 0 && index < size && size <= capacity ? data[index] : nullptr;
    }
};

struct Fighter {
    const void* actor = nullptr;
    std::int32_t levelIndex = -1;
    std::int32_t actorIndex = -1;
};

// Only the game thread reads UE objects. Animation workers compare addresses.
std::atomic<const void*> activeActors[2]{};
std::atomic<const void*> activeMeshes[2]{};
Fighter fighters[2];
const void* lastWorld = nullptr;
std::chrono::steady_clock::time_point nextSearch{};

bool isCharacterClass(const void* actor, const void* characterClass) noexcept {
    // UClass::IsChildOf uses FStructBaseChain in this build.
    const auto* cls = field<const void*>(actor, 0x10);
    const auto depth = field<std::int32_t>(characterClass, 0x38);
    if (!cls || depth < 0 || depth > field<std::int32_t>(cls, 0x38))
        return false;
    const auto chain = field<const void* const*>(cls, 0x30);
    return chain && chain[depth] == static_cast<const std::uint8_t*>(characterClass) + 0x30;
}

void findFighters(ObjectArray levels, const void* characterClass) noexcept {
    for (std::int32_t levelIndex = 0; levelIndex < levels.size; ++levelIndex) {
        const auto* level = levels.at(levelIndex);
        if (!level)
            continue;
        const auto actors = field<ObjectArray>(level, 0x98);
        for (std::int32_t actorIndex = 0; actorIndex < actors.size; ++actorIndex) {
            const auto* actor = actors.at(actorIndex);
            if (!actor || (field<std::uint32_t>(actor, 8) & excludedFlags) ||
                !isCharacterClass(actor, characterClass))
                continue;
            const auto player = field<std::uint32_t>(actor, 0x7AC);
            if (player < 2 && !fighters[player].actor)
                fighters[player] = {actor, levelIndex, actorIndex};
            if (fighters[0].actor && fighters[1].actor)
                return;
        }
    }
}

const void* mainMesh(const void* actor) noexcept {
    // PolarisCharacterActor::GetMeshComponent uses this vtable slot to retrieve the main mesh.
    using GetMesh = const void* (*)(const void*);
    const auto* vtable = field<const void*>(actor, 0);
    const auto* mesh = field<GetMesh>(vtable, 0x8D0)(actor);
    return mesh && !(field<std::uint32_t>(mesh, 8) & excludedFlags) &&
        field<const void*>(mesh, 0x90) == actor ? mesh : nullptr;
}

} // namespace

bool isFighterActor(const void* actor) noexcept {
    return actor && (actor == activeActors[0].load() || actor == activeActors[1].load());
}

bool isFighterMesh(const void* component) noexcept {
    return component && (component == activeMeshes[0].load() || component == activeMeshes[1].load());
}

void refreshFighterTargets(const std::uint8_t* image, const void* world) noexcept {
    const auto* characterClass = image ? field<const void*>(image, fighterClassRva) : nullptr;
    const auto levels = world && characterClass ? field<ObjectArray>(world, 0x170) : ObjectArray{};
    if (lastWorld != world)
        nextSearch = {};
    for (unsigned i = 0; i < 2; ++i) {
        const auto& cached = fighters[i];
        const auto* level = lastWorld == world ? levels.at(cached.levelIndex) : nullptr;
        const auto* actor = level ? field<ObjectArray>(level, 0x98).at(cached.actorIndex) : nullptr;
        if (!actor || actor != cached.actor || (field<std::uint32_t>(actor, 8) & excludedFlags) ||
            field<std::uint32_t>(actor, 0x7AC) != i) {
            if (cached.actor) {
                activeMeshes[i].store(nullptr);
                activeActors[i].store(nullptr);
            }
            fighters[i] = {};
        }
    }
    lastWorld = world;
    if (levels.data && (!fighters[0].actor || !fighters[1].actor)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextSearch) {
            nextSearch = now + std::chrono::milliseconds(200);
            findFighters(levels, characterClass);
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        const auto* actor = fighters[i].actor;
        if (actor) {
            const auto* mesh = mainMesh(actor);
            if (activeMeshes[i].load() != mesh)
                activeMeshes[i].store(mesh);
            if (activeActors[i].load() != actor)
                activeActors[i].store(actor);
        }
    }
}

} // namespace tekken8
