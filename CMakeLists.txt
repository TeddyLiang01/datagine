cmake_minimum_required(VERSION 3.20)

project(datagine VERSION 0.1.0 LANGUAGES CXX)

option(DATAGINE_BUILD_BENCHMARKS "Build Datagine benchmark harnesses" OFF)

find_package(Threads REQUIRED)

add_library(datagine
    src/anomaly/anomaly_detector.cpp
    src/core/market_event.cpp
    src/book/limit_order_book.cpp
    src/book/pooled_limit_order_book.cpp
    src/feed/csv_parser.cpp
    src/replay/replay_engine.cpp
)

add_library(datagine::datagine ALIAS datagine)

target_include_directories(datagine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(datagine PUBLIC cxx_std_20)

set_target_properties(datagine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(datagine PRIVATE /W4)
else()
    target_compile_options(datagine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(datagine_replay
    tools/datagine_replay.cpp
)

target_link_libraries(datagine_replay
    PRIVATE
        datagine::datagine
)

if(MSVC)
    target_compile_options(datagine_replay PRIVATE /W4)
else()
    target_compile_options(datagine_replay PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(DATAGINE_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
