#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <new>
#include <vector>
#include "T8MonitorRefresh.hpp"

namespace tekken8 {
namespace {

using Microsoft::WRL::ComPtr;
std::atomic<float> publishedHz{0};
std::atomic<ULONGLONG> nextPoll{0};

HWND gameWindow() noexcept {
    HWND best = nullptr;
    long long largestArea = 0;
    const DWORD pid = GetCurrentProcessId();
    for (HWND w = nullptr; (w = FindWindowExW(nullptr, w, L"UnrealWindow", nullptr)) != nullptr;) {
        DWORD owner = 0;
        GetWindowThreadProcessId(w, &owner);
        RECT client{};
        if (owner == pid && IsWindowVisible(w) && !GetWindow(w, GW_OWNER) && GetClientRect(w, &client)) {
            const auto area = static_cast<long long>(client.right) * client.bottom;
            if (!best || area > largestArea) {
                largestArea = area;
                best = w;
            }
        }
    }
    return best;
}

struct Display {
    HMONITOR monitor = nullptr;
    MONITORINFOEXW info{};
    DEVMODEW mode{};
    std::uint64_t panelIdentity = 14695981039346656037ULL;
    unsigned panelCount = 0;
    int desktopMonitorCount = 0;

    bool matches(const Display& other) const noexcept {
        return monitor == other.monitor && std::wcscmp(info.szDevice, other.info.szDevice) == 0 &&
               desktopMonitorCount == other.desktopMonitorCount && panelCount == other.panelCount &&
               panelIdentity == other.panelIdentity &&
               mode.dmPelsWidth == other.mode.dmPelsWidth && mode.dmPelsHeight == other.mode.dmPelsHeight &&
               mode.dmDisplayFrequency == other.mode.dmDisplayFrequency && mode.dmBitsPerPel == other.mode.dmBitsPerPel &&
               mode.dmDisplayOrientation == other.mode.dmDisplayOrientation && mode.dmDisplayFlags == other.mode.dmDisplayFlags;
    }
};

bool readDisplay(Display& display) noexcept {
    const HWND window = gameWindow();
    if (!window)
        return false;
    display.monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    display.info.cbSize = sizeof display.info;
    display.mode.dmSize = sizeof display.mode;
    if (!display.monitor || !GetMonitorInfoW(display.monitor, &display.info) ||
        !EnumDisplaySettingsW(display.info.szDevice, ENUM_CURRENT_SETTINGS, &display.mode) ||
        !display.mode.dmPelsWidth || !display.mode.dmPelsHeight)
        return false;

    // Switching extended displays to a virtual clone can leave this GDI source unchanged.
    display.desktopMonitorCount = GetSystemMetrics(SM_CMONITORS);
    // A hardware clone can add or replace a panel on the same source.
    for (DWORD i = 0; ; ++i) {
        DISPLAY_DEVICEW child{};
        child.cb = sizeof child;
        if (!EnumDisplayDevicesW(display.info.szDevice, i, &child, EDD_GET_DEVICE_INTERFACE_NAME))
            break;
        if (!(child.StateFlags & DISPLAY_DEVICE_ACTIVE))
            continue;
        ++display.panelCount;
        for (const wchar_t unit : child.DeviceID) {
            display.panelIdentity = (display.panelIdentity ^ unit) * 1099511628211ULL;
            if (!unit)
                break;
        }
    }
    return true;
}

std::vector<DXGI_MODE_DESC1> displayModes(IDXGIOutput1* output, DXGI_FORMAT format) {
    constexpr UINT flags = DXGI_ENUM_MODES_SCALING; // Excludes interlaced and stereo modes.
    for (int attempt = 0; attempt != 2; ++attempt) {
        UINT count = 0;
        if (FAILED(output->GetDisplayModeList1(format, flags, &count, nullptr)) || !count)
            break;
        std::vector<DXGI_MODE_DESC1> modes(count);
        const auto result = output->GetDisplayModeList1(format, flags, &count, modes.data());
        if (result == DXGI_ERROR_MORE_DATA || count > modes.size())
            continue; // The display configuration changed between the two calls.
        if (FAILED(result))
            break;
        modes.resize(count);
        return modes;
    }
    return {};
}

std::vector<float> outputRates(IDXGIOutput1* output, const Display& display, DXGI_MODE_ROTATION rotation) {
    const bool rotated = rotation == DXGI_MODE_ROTATION_ROTATE90 || rotation == DXGI_MODE_ROTATION_ROTATE270;
    std::vector<float> rates;
    for (auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM}) {
        for (const auto& m : displayModes(output, format)) {
            const auto width = display.mode.dmPelsWidth, height = display.mode.dmPelsHeight;
            const bool sizeMatches = (m.Width == width && m.Height == height) ||
                                     (rotated && m.Width == height && m.Height == width);
            if (sizeMatches && !m.Stereo && m.RefreshRate.Denominator &&
                (m.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED ||
                 m.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE)) {
                const auto hz = static_cast<float>(static_cast<double>(m.RefreshRate.Numerator) / m.RefreshRate.Denominator);
                if (hz > 1.0f)
                    rates.push_back(hz);
            }
        }
    }
    std::sort(rates.begin(), rates.end());
    rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
    return rates;
}

bool belongsToView(const DXGI_OUTPUT_DESC& output, const Display& display) noexcept {
    if (!output.AttachedToDesktop || !output.Monitor)
        return false;
    if (output.Monitor == display.monitor)
        return true;
    // Virtual clones can have different source IDs but exactly the same desktop rectangle.
    MONITORINFO info{};
    info.cbSize = sizeof info;
    return GetMonitorInfoW(output.Monitor, &info) && EqualRect(&info.rcMonitor, &display.info.rcMonitor);
}

float activeRate(const DEVMODEW& mode) noexcept {
    return !(mode.dmDisplayFlags & DM_INTERLACED) && mode.dmDisplayFrequency > 1
        ? static_cast<float>(mode.dmDisplayFrequency) : 0.0f;
}

struct ViewRates {
    bool cloned;
    float active;

    float limit(float precise, float logicalMax) const noexcept {
        if (cloned)
            return precise > 0 ? precise : active;
        // GDI rounds to whole Hz. Preserve e.g. 143.856 when GDI reports 144.
        if (precise > 0 && logicalMax <= std::ceil(precise))
            return precise;
        return logicalMax > 0 ? logicalMax : active;
    }
};

float dxgiRate(const Display& display, float logicalMax, ViewRates& view) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()))))
        return 0;
    std::vector<float> common;
    unsigned outputs = 0;
    for (UINT a = 0; ; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(a, adapter.GetAddressOf())))
            break;
        for (UINT o = 0; ; ++o) {
            ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(o, output.GetAddressOf())))
                break;
            DXGI_OUTPUT_DESC desc{};
            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output->GetDesc(&desc)) || !belongsToView(desc, display))
                continue;
            ++outputs;
            if (outputs > 1 || desc.Monitor != display.monitor)
                view.cloned = true;
            if (desc.Monitor != display.monitor && desc.DeviceName[0]) {
                DEVMODEW active{};
                active.dmSize = sizeof active;
                if (EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &active))
                    view.active = (std::min)(view.active, activeRate(active));
            }
            if (FAILED(output.As(&output1))) {
                common.clear();
                continue; // Check the remaining outputs for clones and their active modes.
            }
            auto rates = outputRates(output1.Get(), display, desc.Rotation);
            if (!rates.empty())
                view.active = (std::min)(view.active, rates.back());
            if (outputs == 1)
                common.swap(rates);
            else
                common.erase(std::remove_if(common.begin(), common.end(), [&](float hz) {
                    return std::none_of(rates.begin(), rates.end(), [hz](float candidate) {
                        return std::abs(hz - candidate) < 0.002f;
                    });
                }), common.end());
        }
    }
    // A hardware clone may expose only one DXGI output. Respect the modes
    // Windows validates for its shared source; GDI reports rates as whole Hz.
    if (display.panelCount > outputs && logicalMax <= 0)
        return 0;
    for (auto it = common.rbegin(); it != common.rend(); ++it)
        if (!view.cloned || logicalMax <= 0 || *it < logicalMax + 1.0f)
            return *it;
    return 0;
}

float gdiRate(const Display& display) noexcept {
    DWORD best = 0;
    DEVMODEW mode{};
    mode.dmSize = sizeof mode;
    for (DWORD i = 0; EnumDisplaySettingsExW(display.info.szDevice, i, &mode, 0); ++i) {
        if (mode.dmPelsWidth == display.mode.dmPelsWidth && mode.dmPelsHeight == display.mode.dmPelsHeight &&
            mode.dmBitsPerPel == display.mode.dmBitsPerPel && !(mode.dmDisplayFlags & DM_INTERLACED) &&
            mode.dmDisplayFrequency > 1)
            best = (std::max)(best, mode.dmDisplayFrequency);
    }
    return static_cast<float>(best);
}

void CALLBACK poll(PTP_CALLBACK_INSTANCE, void*) noexcept {
    // The ASI is pinned before its tick hook is installed. nextPoll serializes workers.
    static Display last;
    static ULONGLONG recheckAt = 0;
    Display current;
    float hz = publishedHz.load(std::memory_order_relaxed);
    const auto now = GetTickCount64();
    if (!readDisplay(current)) {
        last = {};
        hz = 0;
    } else if (!current.matches(last) || now >= recheckAt) {
        last = current;
        const auto fallback = gdiRate(current);
        float precise = 0;
        ViewRates view{current.panelCount > 1, activeRate(current.mode)};
        try {
            precise = dxgiRate(current, fallback, view);
        } catch (const std::bad_alloc&) {
            // Use the active clone timing or the single display's Windows mode list.
        }
        // A virtual clone's GDI list can belong to only one panel.
        hz = view.limit(precise, fallback);
        // Newly advertised modes can appear without changing the active mode or monitor identity.
        recheckAt = now + (precise > 0 ? 60000 : 5000);
    }
    publishedHz.store(hz, std::memory_order_relaxed);
    nextPoll.store(GetTickCount64() + 1000, std::memory_order_release);
}

} // namespace

float monitorRefreshLimit() noexcept {
    const auto now = GetTickCount64();
    auto next = nextPoll.load(std::memory_order_acquire);
    if (now >= next && nextPoll.compare_exchange_strong(next, MAXULONGLONG, std::memory_order_acq_rel) &&
        !TrySubmitThreadpoolCallback(poll, nullptr, nullptr))
        nextPoll.store(now + 1000, std::memory_order_release);
    return publishedHz.load(std::memory_order_relaxed);
}

} // namespace tekken8
