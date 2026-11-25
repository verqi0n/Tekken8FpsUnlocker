#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "T8Config.hpp"
#include <cerrno>
#include <cwchar>
#include <filesystem>

namespace tekken8 {

float readTargetFps(HMODULE module) {
    constexpr float defaultFps = 300.0f;
    wchar_t modulePath[32768];
    const DWORD length = GetModuleFileNameW(module, modulePath, _countof(modulePath));
    if (!length || length >= _countof(modulePath))
        return defaultFps;

    std::filesystem::path iniPath(modulePath);
    iniPath.replace_extension(L".ini");
    wchar_t setting[32];
    const DWORD count = GetPrivateProfileStringW(L"FPS", L"TargetFPS", L"", setting,
                                                 _countof(setting), iniPath.c_str());
    if (!count || count == _countof(setting) - 1)
        return defaultFps;

    wchar_t* end = nullptr;
    errno = 0;
    const long fps = std::wcstol(setting, &end, 10);
    if (end == setting || *end != L'\0' || errno == ERANGE || fps < 0)
        return defaultFps;
    return static_cast<float>(fps);
}

} // namespace tekken8
