#include "T8FighterTargets.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>

namespace tekken8 {
namespace {

// Native PolarisCharacterActor class and the chunked GUObjectArray, build 26651490.
constexpr std::uintptr_t fighterClassRva = 0x9935C10;
constexpr std::uintptr_t objectArrayRva = 0x9A62430;
constexpr std::uint32_t excludedFlags = 0x10 | 0x8000 | 0x10000; // CDO, BeginDestroyed, FinishDestroyed

template<class T>
T field(const void* object, std::size_t offset) noexcept {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

struct ObjectArray {
    const void* const* data;
    std::int32_t size;
    std::int32_t capacity;

    std::int32_t count() const noexcept {
        return data && size >= 0 && size <= capacity ? size : 0;
    }

    const void* at(std::int32_t index) const noexcept {
        return index >= 0 && index < count() ? data[index] : nullptr;
    }
};

struct Fighter {
    const void* actor = nullptr;
    std::int32_t levelIndex = -1;
    std::int32_t actorIndex = -1;
    std::uint64_t objectId = 0;
};

// Only the game thread reads UE objects. Animation workers compare addresses.
std::atomic<const void*> activeMeshes[2]{};
std::atomic<std::uint64_t> meshIds[2]{};
Fighter fighters[2];
const void* lastWorld = nullptr;
const std::uint8_t* objectImage = nullptr;
thread_local bool isTargetThread = false;
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

// FUObjectItem::SerialNumber changes when an object-array slot is recycled.
// Read only after obtaining the actor from its live level or actor callback.
std::uint64_t objectId(const std::uint8_t* image, const void* actor) noexcept {
    const auto* objects = image + objectArrayRva;
    const auto index = field<std::int32_t>(actor, 0xC);
    const auto chunks = field<const std::uint8_t* const*>(objects, 0);
    if (!chunks || index < 0 || index >= field<std::int32_t>(objects, 0x14))
        return 0;
    const auto* chunk = chunks[index >> 16];
    if (!chunk)
        return 0;
    const auto* item = chunk + 24 * (index & 0xFFFF);
    if (field<const void*>(item, 0) != actor)
        return 0;
    const auto serial = field<std::uint32_t>(item, 0x10);
    return (static_cast<std::uint64_t>(serial) << 32) | (static_cast<std::uint32_t>(index) + 1u);
}

bool actorUnavailable(const void* actor) noexcept {
    // AActor::bActorIsBeingDestroyed is set before UObject::BeginDestroy.
    return (field<std::uint32_t>(actor, 8) & excludedFlags) || (field<std::uint8_t>(actor, 0x5D) & 2);
}

void findFighters(const std::uint8_t* image, ObjectArray levels, const void* characterClass) noexcept {
    for (std::int32_t levelIndex = 0; levelIndex < levels.count(); ++levelIndex) {
        const auto* level = levels.at(levelIndex);
        if (!level)
            continue;
        const auto actors = field<ObjectArray>(level, 0x98);
        for (std::int32_t actorIndex = 0; actorIndex < actors.count(); ++actorIndex) {
            const auto* actor = actors.at(actorIndex);
            if (!actor || !isCharacterClass(actor, characterClass) || actorUnavailable(actor))
                continue;
            const auto player = field<std::uint32_t>(actor, 0x7AC);
            if (player < 2 && !fighters[player].actor) {
                const auto id = objectId(image, actor);
                if (id)
                    fighters[player] = {actor, levelIndex, actorIndex, id};
            }
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

FighterTarget fighterTarget(const void* actor) noexcept {
    if (isTargetThread && actor && objectImage) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto& fighter = fighters[i];
            if (actor == fighter.actor && !actorUnavailable(actor) && fighter.objectId == objectId(objectImage, actor))
                return {i, fighter.objectId};
        }
    }
    return {};
}

std::uint64_t fighterMeshIdentity(const void* component) noexcept {
    if (component) {
        for (unsigned i = 0; i < 2; ++i) {
            if (activeMeshes[i].load() == component) {
                const auto id = meshIds[i].load();
                if (activeMeshes[i].load() == component)
                    return id;
            }
        }
    }
    return 0;
}

void refreshFighterTargets(const std::uint8_t* image, const void* world) noexcept {
    isTargetThread = true;
    objectImage = image;
    const auto* characterClass = image ? field<const void*>(image, fighterClassRva) : nullptr;
    const auto levels = world && characterClass ? field<ObjectArray>(world, 0x170) : ObjectArray{};
    if (lastWorld != world)
        nextSearch = {};
    for (unsigned i = 0; i < 2; ++i) {
        const auto& cached = fighters[i];
        const auto* level = lastWorld == world ? levels.at(cached.levelIndex) : nullptr;
        const auto* actor = level ? field<ObjectArray>(level, 0x98).at(cached.actorIndex) : nullptr;
        if (!actor || actor != cached.actor || objectId(image, actor) != cached.objectId ||
            !isCharacterClass(actor, characterClass) || actorUnavailable(actor) || field<std::uint32_t>(actor, 0x7AC) != i) {
            if (cached.actor) {
                activeMeshes[i].store(nullptr);
                nextSearch = {};
            }
            fighters[i] = {};
        }
    }
    lastWorld = world;
    if (levels.data && (!fighters[0].actor || !fighters[1].actor)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextSearch) {
            nextSearch = now + std::chrono::milliseconds(200);
            findFighters(image, levels, characterClass);
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        const auto* actor = fighters[i].actor;
        if (actor) {
            const auto* mesh = mainMesh(actor);
            const auto meshId = mesh ? objectId(image, mesh) : 0;
            if (activeMeshes[i].load() != mesh || meshIds[i].load() != meshId) {
                activeMeshes[i].store(nullptr);
                meshIds[i].store(meshId);
                activeMeshes[i].store(mesh);
            }
        }
    }
}

} // namespace tekken8
