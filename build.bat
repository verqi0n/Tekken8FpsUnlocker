@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" || exit /b 1

where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo Add CMake 3.24 or later to PATH and run this script again. 1>&2
    goto :failed
)

cmake -S . -B build -A x64
if errorlevel 1 goto :failed

cmake --build build --config Release --target Tekken8FpsUnlocker --parallel
if errorlevel 1 goto :failed

cmake -E copy_if_different "build\Release\Tekken8FpsUnlocker.asi" "Tekken8FpsUnlocker.asi"
if errorlevel 1 goto :failed

echo Built "%CD%\Tekken8FpsUnlocker.asi"
popd
exit /b 0

:failed
echo ASI build failed. See the error above. 1>&2
popd
exit /b 1
