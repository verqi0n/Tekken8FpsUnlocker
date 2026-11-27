#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "T8Config.hpp"
#include <cerrno>
#include <cwchar>
#include <filesystem>

namespace tekken8 {
namespace {

// Returns -1 if the key is missing or is not a non-negative whole number.
long readWholeNumber(const wchar_t* key, const wchar_t* iniPath) {
    wchar_t setting[32];
    const DWORD count = GetPrivateProfileStringW(L"FPS", key, L"", setting, _countof(setting), iniPath);
    if (!count || count == _countof(setting) - 1)
        return -1;

    wchar_t* end = nullptr;
    errno = 0;
    const long value = std::wcstol(setting, &end, 10);
    if (end == setting || *end != L'\0' || errno == ERANGE || value < 0)
        return -1;
    return value;
}

} // namespace

Settings readSettings(HMODULE module) {
    Settings settings;
    wchar_t modulePath[32768];
    const DWORD length = GetModuleFileNameW(module, modulePath, _countof(modulePath));
    if (!length || length >= _countof(modulePath))
        return settings;

    std::filesystem::path iniPath(modulePath);
    iniPath.replace_extension(L".ini");
    const long fps = readWholeNumber(L"TargetFPS", iniPath.c_str());
    if (fps >= 0)
        settings.targetFps = static_cast<float>(fps);
    // Only 1 enables the option. Any other value keeps the default.
    settings.unlockEverywhere = readWholeNumber(L"UnlockEverywhere", iniPath.c_str()) == 1;
    return settings;
}

} // namespace tekken8
