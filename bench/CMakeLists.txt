include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    google_benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
)

FetchContent_MakeAvailable(google_benchmark)

add_executable(datagine_benchmarks
    datagine_benchmarks.cpp
)

target_link_libraries(datagine_benchmarks
    PRIVATE
        datagine::datagine
        benchmark::benchmark_main
        Threads::Threads
)

if(MSVC)
    target_compile_options(datagine_benchmarks PRIVATE /W4)
else()
    target_compile_options(datagine_benchmarks PRIVATE -Wall -Wextra -Wpedantic)
endif()
