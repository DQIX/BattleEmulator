cmake_minimum_required(VERSION 3.28)
project(newDirectory)

set(CMAKE_CXX_STANDARD 17)

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
)

add_executable(gilyumei2_rubii ${SOURCES})
target_compile_definitions(gilyumei2_rubii PRIVATE RUBII=1)


add_executable(gilyumei2_rubii_opt ${SOURCES})
target_compile_definitions(gilyumei2_rubii_opt PRIVATE GOUKETU=1 OPTIMIZE_MODE=1)  # `#define MULTITHREADING` を適用

add_executable(gilyumei2_gouketu ${SOURCES})
target_compile_definitions(gilyumei2_gouketu PRIVATE GOUKETU=1)


string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP BUILD_TIME "%H:%M:%S" UTC)
add_definitions(-DBUILD_DATE="${BUILD_DATE}" -DBUILD_TIME="${BUILD_TIME}")

if(MINGW)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections -fno-inline-small-functions")
    set(CMAKE_EXE_LINKER_FLAGS "-static -Wl,--gc-sections")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /O2")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    target_compile_options(newDirectory PRIVATE /utf-8)
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