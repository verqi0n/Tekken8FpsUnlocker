cmake_minimum_required(VERSION 3.24)
project(Tekken8FpsUnlocker LANGUAGES C CXX RC)
if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "Build the ASI with an x64 Visual Studio toolchain.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# MinHook's CMake project predates CMP0077. Make its option honor this setting.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
include(FetchContent)
FetchContent_Declare(minhook
    URL https://codeload.github.com/TsudaKageyu/minhook/zip/c3fcafdc10146beb5919319d0683e44e3c30d537
    URL_HASH SHA256=cdcb160f734d81bd4d235dfea79e3f5a661c8ef0ab74fa814272aa5449069034)
FetchContent_MakeAvailable(minhook)

# Keep the MinHook and bundled disassembler notices with the standalone ASI.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/MinHookLicense.rc"
    CONTENT "101 RCDATA \"@minhook_SOURCE_DIR@/LICENSE.txt\"\n" @ONLY)
add_library(Tekken8FpsUnlocker MODULE Tekken8FpsUnlocker.cpp T8Config.cpp T8VisualTiming.cpp
    T8ActorInterpolation.cpp T8PoseInterpolation.cpp T8FighterTargets.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/MinHookLicense.rc")
target_compile_features(Tekken8FpsUnlocker PRIVATE cxx_std_17)
target_compile_options(Tekken8FpsUnlocker PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
target_link_libraries(Tekken8FpsUnlocker PRIVATE minhook)
set_target_properties(Tekken8FpsUnlocker PROPERTIES PREFIX "" SUFFIX ".asi")
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:Tekken8FpsUnlocker>/Tekken8FpsUnlocker.ini"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/Tekken8FpsUnlocker.ini")

include(CTest)
if(BUILD_TESTING)
    add_executable(Tekken8CoreTests tests/CoreTests.cpp)
    target_compile_features(Tekken8CoreTests PRIVATE cxx_std_17)
    target_compile_options(Tekken8CoreTests PRIVATE /W4 /EHsc)
    target_include_directories(Tekken8CoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME Tekken8CoreTests COMMAND Tekken8CoreTests)
endif()
