cmake_minimum_required(VERSION 3.28)
project(newDirectory)

set(CMAKE_CXX_STANDARD 17)

option(multithreading "Enable multithreading" OFF)

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


if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "AddressSanitizer (ASan) is enabled for Debug build")

    # AddressSanitizer を有効化
    add_compile_options(/fsanitize=address /Zi /Od)
    add_link_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO /DEBUG)  # INCREMENTAL LINKING は無効化
endif()
#if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror -Wshadow -Wstrict-aliasing -Wunreachable-code -Wnull-dereference -Winit-self -Wfloat-equal -Wconversion -Wstrict-overflow=5 -fsanitize=undefined")
#endif()

# 共通のソースファイル
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
        InputBuilder.cpp
        InputBuilder.h
        ResultStructure.h
        setting.h
        ActionBruteForcer.cpp
        ActionBruteForcer.h
)

# シングルスレッド版（multithreading = OFF）
add_executable(kbattle_v2 ${SOURCES})
target_compile_definitions(kbattle_v2 PRIVATE MULTITHREADING kbattle_v2_Normal)

string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP BUILD_TIME "%H:%M:%S" UTC)
add_definitions(-DBUILD_DATE="${BUILD_DATE}" -DBUILD_TIME="${BUILD_TIME}")

if(MINGW)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=x86-64 -mtune=generic -ffunction-sections -fdata-sections") #-fno-inline-small-functions
    set(CMAKE_EXE_LINKER_FLAGS "-static -Wl,--gc-sections")
elseif(MSVC)
    #set_property(TARGET kbattle_v2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(kbattle_v2 PRIVATE /W4 /MP)
    target_link_options(kbattle_v2 PRIVATE /INCREMENTAL:NO /DEBUG)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /O2")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
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