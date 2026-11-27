#pragma once

#include <cstddef>
#include <cstdint>

namespace tekken8 {

template<class T>
inline T field(const void* object, std::size_t offset) noexcept {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

// UObject::ClassPrivate and UClass::IsChildOf's FStructBaseChain on build 26651490.
inline bool hasClass(const void* object, const void* baseClass) noexcept {
    if (!object || !baseClass)
        return false;
    const auto* cls = field<const void*>(object, 0x10);
    const auto depth = field<std::int32_t>(baseClass, 0x38);
    if (!cls || depth < 0 || depth > field<std::int32_t>(cls, 0x38))
        return false;
    const auto chain = field<const void* const*>(cls, 0x30);
    return chain && chain[depth] == static_cast<const std::uint8_t*>(baseClass) + 0x30;
}

}
