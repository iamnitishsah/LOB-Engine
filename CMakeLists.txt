cmake_minimum_required(VERSION 3.16)
project(lob_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimization and warning flags
if (MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-variadic-macro-arguments-omitted)
    endif()
endif()

# Core library target
add_library(lob_core STATIC
    src/strategies/market_maker.cpp
    src/strategies/momentum.cpp
)

target_include_directories(lob_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(lob_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Executables
add_executable(feed_generator src/feed_generator.cpp)
target_link_libraries(feed_generator PRIVATE lob_core)

add_executable(replay src/main_replay.cpp)
add_executable(async_replay src/main_async_replay.cpp)
target_link_libraries(async_replay PRIVATE lob_core)

target_link_libraries(replay PRIVATE lob_core)

add_executable(backtest src/main_backtest.cpp)
target_link_libraries(backtest PRIVATE lob_core)

add_executable(itch_converter src/itch_converter.cpp)
target_link_libraries(itch_converter PRIVATE lob_core)

# Third-party dependencies via FetchContent
include(FetchContent)

# GoogleTest
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google Benchmark
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Tests
enable_testing()

add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE lob_core GTest::gtest_main)
add_test(NAME test_order_book COMMAND test_order_book)

add_executable(test_matching tests/test_matching.cpp)
target_link_libraries(test_matching PRIVATE lob_core GTest::gtest_main)
add_test(NAME test_matching COMMAND test_matching)
add_executable(test_spsc_queue tests/test_spsc_queue.cpp)
target_link_libraries(test_spsc_queue PRIVATE lob_core GTest::gtest_main)
add_test(NAME test_spsc_queue COMMAND test_spsc_queue)

add_executable(test_itch_parser tests/test_itch_parser.cpp)
target_link_libraries(test_itch_parser PRIVATE lob_core GTest::gtest_main)
add_test(NAME test_itch_parser COMMAND test_itch_parser)

add_executable(test_replay_determinism tests/test_replay_determinism.cpp)
target_link_libraries(test_replay_determinism PRIVATE lob_core GTest::gtest_main)
add_test(NAME test_replay_determinism COMMAND test_replay_determinism)

# Benchmarks
add_executable(bench_add_cancel bench/bench_add_cancel.cpp)
target_link_libraries(bench_add_cancel PRIVATE lob_core benchmark::benchmark)

add_executable(bench_match bench/bench_match.cpp)
target_link_libraries(bench_match PRIVATE lob_core benchmark::benchmark)
add_executable(bench_spsc_queue bench/bench_spsc_queue.cpp)
target_link_libraries(bench_spsc_queue PRIVATE lob_core benchmark::benchmark)


add_executable(bench_book_impls bench/bench_book_impls.cpp)
target_link_libraries(bench_book_impls PRIVATE lob_core benchmark::benchmark)
