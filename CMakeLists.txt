cmake_minimum_required(VERSION 3.28)
project(newDirectory)

set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)

if(MSVC)
    # ポリシーの設定
    if(POLICY CMP0091)
        cmake_policy(SET CMP0091 NEW)  # ポリシーをNEWに設定
    endif()
endif()

# MinGWビルドかMSVCビルドかを判別するフラグを定義
if(MINGW)
    add_definitions(-DMINGW_BUILD)
elseif(MSVC)
    add_definitions(-DMSVC_BUILD)
endif()

#if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror -Wshadow -Wstrict-aliasing -Wunreachable-code -Wnull-dereference -Winit-self -Wfloat-equal -Wconversion -Wstrict-overflow=5 -fsanitize=undefined")
#endif()


if (MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "AddressSanitizer (ASan) is enabled for Debug build")

    # AddressSanitizer を有効化
    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO /DEBUG)  # INCREMENTAL LINKING は無効化
endif ()

set(SOURCES
        main.cpp
        lcg.cpp
        lcg.h
        BattleEmulator.cpp
        BattleEmulator.h
        Player.h
        camera.cpp
        camera.h
        debug.cpp
        debug.h
        BattleResult.h
        Equipment.h
        ActionOptimizer.cpp
        ActionOptimizer.h
        Genome.h
        EnhancedCostCalculator.h
        EnhancedCostCalculator.cpp
        EnhancedHashCalculator.h
        EnhancedHeapQueue.h
        EnhancedHeapQueue.cpp
        EnhancedHashCalculator.cpp
        SimpleParameterOptimizer.cpp
        SimpleParameterOptimizer.h
        LinearIdPool.h
        camera/freecam-bact-table.hpp
        camera/freecam_action_mapper.hpp
        camera/freecam_fast_generated.hpp
        camera/freecam_fast_runtime.hpp
        camera/freecam_route.hpp
        camera/freecam_runtime.hpp
        camera/freecam_setup.hpp
        camera/freecam_state.hpp
)

add_executable(gerunikku ${SOURCES})
target_compile_definitions(gerunikku PRIVATE gerunikku=1)


add_executable(gerunikku_gouketu_opt ${SOURCES})
target_compile_definitions(gerunikku_gouketu_opt PRIVATE gerunikku=1 OPTIMIZE_MODE=1)  # `#define MULTITHREADING` を適用

add_executable(erugi1_hayate ${SOURCES})
target_compile_definitions(erugi1_hayate PRIVATE hayate=1)

add_executable(erugi1_gilyumei ${SOURCES})
target_compile_definitions(erugi1_gilyumei PRIVATE gilyumei=1)

foreach(target gerunikku gerunikku_gouketu_opt erugi1_hayate erugi1_gilyumei)
    target_link_libraries(${target} PRIVATE Threads::Threads)
endforeach()

add_executable(freecam_edge_validation
        camera/freecam_edge_validation.cpp
)
target_include_directories(freecam_edge_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP BUILD_TIME "%H:%M:%S" UTC)
add_definitions(-DBUILD_DATE="${BUILD_DATE}" -DBUILD_TIME="${BUILD_TIME}")

if(MINGW)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections -fno-inline-small-functions")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /O2")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    target_compile_options(gerunikku PRIVATE /utf-8)
endif()

# MinGWの最適化フラグに応じたマクロ設定
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_CXX_FLAGS MATCHES "-O3")
        add_definitions(-DOPTIMIZATION_O3_ENABLED)
    elseif(CMAKE_CXX_FLAGS MATCHES "-O2")
        add_definitions(-DOPTIMIZATION_O2_ENABLED)
    else()
        add_definitions(-DNO_OPTIMIZATION)
    endif()
endif()

# MSVCの最適化フラグに応じたマクロ設定
if(MSVC)
    if(CMAKE_CXX_FLAGS_RELEASE MATCHES "/O2")
        add_definitions(-DOPTIMIZATION_O2_ENABLED)
    elseif(CMAKE_CXX_FLAGS_RELEASE MATCHES "/Od")
        add_definitions(-DNO_OPTIMIZATION)
    endif()
endif()