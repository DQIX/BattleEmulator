cmake_minimum_required(VERSION 3.28)
project(newDirectory)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES main.cpp lcg.cpp BattleEmulator.cpp camera.cpp debug.cpp)

# Existing CLion configuration name remains usable. Both executables contain
# only the Slime.dst core; no battle-path search implementation is linked.
add_executable(gerunikku ${SOURCES})
add_executable(slime ${SOURCES})
foreach(target gerunikku slime)
    target_compile_definitions(${target} PRIVATE gerunikku=1)
    if(MINGW)
        target_compile_definitions(${target} PRIVATE MINGW_BUILD)
        target_compile_options(${target} PRIVATE -O3 -g -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections)
        target_link_options(${target} PRIVATE -Wl,--gc-sections)
    elseif(MSVC)
        target_compile_definitions(${target} PRIVATE MSVC_BUILD)
        target_compile_options(${target} PRIVATE /O2 /Zi /utf-8)
        target_link_options(${target} PRIVATE /DEBUG)
    endif()
endforeach()

# Existing camera diagnostic target is retained, not run as a test framework.
add_executable(freecam_edge_validation camera/freecam_edge_validation.cpp)
target_include_directories(freecam_edge_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP BUILD_TIME "%H:%M:%S" UTC)
add_definitions(-DBUILD_DATE="${BUILD_DATE}" -DBUILD_TIME="${BUILD_TIME}")
