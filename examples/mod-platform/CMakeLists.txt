# ANNAExampleMod.dll: an FH3 Mod Platform mod that adds entries to ANNA's menu.
#
#   git clone https://github.com/Levilente/FH3_ModPlatform platform      (or point FH3_PLATFORM_DIR at a checkout)
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=platform/cmake/mingw-w64-x86_64.cmake
#   cmake --build build
#
# Install: build/ANNAExampleMod.dll into fh3-mods next to azimuth.exe.
cmake_minimum_required(VERSION 3.20)
project(anna_example_mod VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(FH3_PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform" CACHE PATH "FH3 Mod Platform checkout")
set(FH3ANNA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include" CACHE PATH "folder with fh3anna.h")
file(STRINGS "${FH3_PLATFORM_DIR}/CMakeLists.txt" _pv REGEX "^project\\(fh3_mod_platform VERSION")
string(REGEX REPLACE ".*VERSION ([0-9.]+).*" "\\1" FH3_PLATFORM_VERSION "${_pv}")

add_library(anna_example_mod SHARED anna_example_mod.cpp)
target_include_directories(anna_example_mod PRIVATE "${FH3_PLATFORM_DIR}/include" "${FH3ANNA_INCLUDE_DIR}")
target_compile_definitions(anna_example_mod PRIVATE FH3MOD_PLATFORM_VERSION="${FH3_PLATFORM_VERSION}"
                                                    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(NOT MSVC)
    target_link_options(anna_example_mod PRIVATE -static -s)   # no libc++/libunwind DLLs to ship
endif()
set_target_properties(anna_example_mod PROPERTIES OUTPUT_NAME ANNAExampleMod PREFIX "")
