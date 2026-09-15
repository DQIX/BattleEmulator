cmake_minimum_required(VERSION 3.16)
project(BilyoumaEvaluation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(BILYOUMA_SANITIZE "Build ASan + UBSan" OFF)
set(COMMON ../BilyoumaSearch.cpp ../ActionOptimizer.cpp ../BattleEmulator.cpp
    ../EnhancedCostCalculator.cpp ../EnhancedHashCalculator.cpp ../EnhancedHeapQueue.cpp
    ../InputBuilder.cpp ../lcg.cpp ../camera.cpp ../debug.cpp)
foreach(PROFILE 19 13 15)
    add_executable(bilyouma_bench_lv${PROFILE} Benchmark.cpp BaselineMeasured.cpp ${COMMON})
    target_include_directories(bilyouma_bench_lv${PROFILE} PRIVATE ..)
    target_compile_definitions(bilyouma_bench_lv${PROFILE} PRIVATE
        BattleEmulatorLV${PROFILE}=1 NO_MULTITHREADING=1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Input main.cpp contains integral-valued floating literals in aggregate
        # initializers. Accept those without changing any profile or world source.
        target_compile_options(bilyouma_bench_lv${PROFILE} PRIVATE -Wno-narrowing -Wno-attributes)
        if(BILYOUMA_SANITIZE)
            target_compile_options(bilyouma_bench_lv${PROFILE} PRIVATE -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(bilyouma_bench_lv${PROFILE} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endforeach()
