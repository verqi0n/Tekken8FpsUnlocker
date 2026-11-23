cmake_minimum_required(VERSION 3.24)
project(Tekken8FpsUnlocker LANGUAGES C CXX RC)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build the ASI with an x64 Visual Studio toolchain.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF)
include(FetchContent)
FetchContent_Declare(minhook
    URL https://codeload.github.com/TsudaKageyu/minhook/zip/c3fcafdc10146beb5919319d0683e44e3c30d537
    URL_HASH SHA256=cdcb160f734d81bd4d235dfea79e3f5a661c8ef0ab74fa814272aa5449069034)
FetchContent_MakeAvailable(minhook)

# Keep the MinHook and bundled disassembler notices with the standalone ASI.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MinHookLicense.rc"
    "101 RCDATA \"${minhook_SOURCE_DIR}/LICENSE.txt\"\n")
add_library(Tekken8FpsUnlocker MODULE Tekken8FpsUnlocker.cpp T8MonitorRefresh.cpp T8VisualTiming.cpp
    T8ActorInterpolation.cpp T8PoseInterpolation.cpp T8FighterTargets.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/MinHookLicense.rc")
target_compile_features(Tekken8FpsUnlocker PRIVATE cxx_std_17)
target_compile_options(Tekken8FpsUnlocker PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
target_link_libraries(Tekken8FpsUnlocker PRIVATE minhook dxgi user32)
set_target_properties(Tekken8FpsUnlocker PROPERTIES PREFIX "" SUFFIX ".asi")
