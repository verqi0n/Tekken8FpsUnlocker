#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include "Tekken8FpsUnlocker.hpp"
#include "T8MonitorRefresh.hpp"
#include "T8VisualTiming.hpp"

static_assert(sizeof(void*) == 8, "Build for x64");
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
// Steam build 26651490. RVAs are relative to Polaris-Win64-Shipping.exe.
constexpr DWORD imageTimestamp = 0x6AF749A0;
constexpr DWORD imageSize = 0x0A79B000;
constexpr DWORD tickWrapper = 0x02E542C0;
constexpr DWORD dispatchReturn = 0x00A62F05;
constexpr DWORD tickSlot = 0x085A0388;
constexpr DWORD tickImplementation = 0x05BB57B0;
constexpr DWORD gameplayInstance = 0x09948C68;
constexpr DWORD engineInstance = 0x09BB6670;
constexpr DWORD maxFpsStorage = 0x09BB6B60;
constexpr DWORD namePool = 0x099BC380;
constexpr unsigned fixedFrameRateFlags = 0x9DC;
constexpr unsigned fixedFrameRateValue = 0x9E0;

using GameTick = void (*)(void*, double);
std::uint8_t* image = nullptr;
GameTick originalTick = nullptr;

bool supportedImage(const std::uint8_t* base) noexcept {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        pe->FileHeader.TimeDateStamp != imageTimestamp || pe->OptionalHeader.SizeOfImage != imageSize)
        return false;

    // The wrapper tail-calls slot 2 of the gameplay singleton. The dispatcher
    // passes seconds as a double and leaves dispatchReturn on the stack.
    constexpr std::uint8_t wrapper[] = {
        0x48, 0x8B, 0x0D, 0xA1, 0x49, 0xAF, 0x06, 0x48, 0x85, 0xC9, 0x74, 0x0A,
        0x48, 0x8B, 0x01, 0x0F, 0x28, 0xC8, 0x48, 0xFF, 0x60, 0x10, 0xC3
    };
    constexpr std::uint8_t dispatch[] = {0x48, 0x8B, 0x0C, 0x03, 0xFF, 0xD1};
    return std::memcmp(base + tickWrapper, wrapper, sizeof wrapper) == 0 &&
           std::memcmp(base + dispatchReturn - sizeof dispatch, dispatch, sizeof dispatch) == 0;
}

// The game thread reads t.MaxFPS[0], as seen in the overlay's FPSUnlockerGateway::apply (RVA 0x29580).
tekken8::FrameRateFields frameRateFields() noexcept {
    auto* engine = *reinterpret_cast<std::uint8_t**>(image + engineInstance);
    auto* maxFps = *reinterpret_cast<float**>(image + maxFpsStorage);
    return {engine ? engine + fixedFrameRateFlags : nullptr,
            engine ? reinterpret_cast<float*>(engine + fixedFrameRateValue) : nullptr, maxFps};
}

const std::uint8_t* viewportWorld() noexcept {
    const auto* engine = *reinterpret_cast<const std::uint8_t* const*>(image + engineInstance);
    if (!engine)
        return nullptr;
    const auto* viewport = *reinterpret_cast<const std::uint8_t* const*>(engine + 0x9B8);
    return viewport ? *reinterpret_cast<const std::uint8_t* const*>(viewport + 0x78) : nullptr;
}

// The menu world can already be loaded during Boot. The overlay waits for the viewport to switch maps (RVA 0x3A606).
bool viewportHasLeftBoot(const std::uint8_t* world) noexcept {
    if (!world)
        return false;
    const auto* level = *reinterpret_cast<const std::uint8_t* const*>(world + 0x30);
    if (!level)
        return false;
    const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(level + 0x20);
    if (!owner)
        return false;

    // FNameEntryId is a block index and a two-byte offset within that block.
    const auto id = *reinterpret_cast<const std::uint32_t*>(owner + 0x18);
    const auto* pool = image + namePool;
    if (!id || (id >> 16) > *reinterpret_cast<const std::uint32_t*>(pool + 8))
        return false;
    const auto* block = reinterpret_cast<const std::uint8_t* const*>(pool + 0x10)[id >> 16];
    if (!block)
        return false;
    const auto* entry = block + 2 * (id & 0xFFFF);
    const auto header = *reinterpret_cast<const std::uint16_t*>(entry);
    const auto length = static_cast<std::size_t>(header >> 6);
    if (header & 1)
        return tekken8::isPlayableMap<wchar_t>({reinterpret_cast<const wchar_t*>(entry + 2), length}, L"Boot");
    return tekken8::isPlayableMap<char>({reinterpret_cast<const char*>(entry + 2), length}, "Boot");
}

void __declspec(noinline) pacedTick(void* gameplay, double seconds) noexcept {
    auto* const instance = reinterpret_cast<void* volatile*>(image + gameplayInstance);
    if (_ReturnAddress() != image + dispatchReturn || gameplay != *instance) {
        originalTick(gameplay, seconds);
        return;
    }

    static const void* activeWorld = nullptr;
    static void* activeGameplay = nullptr;
    static tekken8::GameplayClock clock;
    static tekken8::FrameRateOverride frameRate;
    const auto leaveWorld = [&] {
        clock = {};
        activeWorld = nullptr;
        activeGameplay = nullptr;
        tekken8::updateVisualWorld(image, nullptr);
        frameRate.restore(frameRateFields());
    };

    const auto* world = viewportWorld();
    if (!viewportHasLeftBoot(world)) {
        if (activeWorld)
            leaveWorld();
        originalTick(gameplay, seconds);
        return;
    }

    if (world != activeWorld || gameplay != activeGameplay) {
        if (activeWorld)
            leaveWorld();
        activeWorld = world;
        activeGameplay = gameplay;
        // Loading time belongs to the previous scene.
        if (std::isfinite(seconds) && seconds > tekken8::gameplayStep)
            seconds = tekken8::gameplayStep;
    }
    tekken8::updateVisualWorld(image, world);
    clock.addFrame(seconds);
    while (clock.takeStep()) {
        LARGE_INTEGER start{};
        QueryPerformanceCounter(&start);
        originalTick(gameplay, tekken8::gameplayStep);
        if (gameplay != *instance || world != viewportWorld()) {
            leaveWorld();
            return;
        }
        // Publish the completed tick using its start time, as the overlay does.
        tekken8::completeVisualTick(start.QuadPart);
    }
    // Tekken reapplies its cap on scene changes. Refresh even on a zero-tick frame.
    frameRate.apply(frameRateFields(), tekken8::monitorRefreshLimit());
}

BOOL CALLBACK install(PINIT_ONCE, PVOID, PVOID*) {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"Polaris-Win64-Shipping.exe"));
    if (!base || base != reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr)) || !supportedImage(base))
        return TRUE;

    auto** slot = reinterpret_cast<void**>(base + tickSlot);
    auto* expected = base + tickImplementation;
    if (*slot != expected)
        return TRUE;

    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&__ImageBase), &self))
        return TRUE;
    image = base;
    originalTick = reinterpret_cast<GameTick>(expected);
    DWORD protection;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &protection))
        return TRUE;
    if (tekken8::installVisualInterpolation(base)) {
        const auto replaced = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(&pacedTick), expected);
        if (replaced != expected)
            tekken8::uninstallVisualInterpolation();
    }
    DWORD ignored;
    VirtualProtect(slot, sizeof *slot, protection, &ignored);
    return TRUE;
}
} // namespace

extern "C" __declspec(dllexport) void InitializeASI() {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, install, nullptr, nullptr);
}
