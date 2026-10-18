cmake_minimum_required(VERSION 3.16)
project(perfscope VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 standard across all targets
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Define platform-specific sources and libraries
set(SAMPLER_SOURCES
    src/sampler/sampler_factory.cpp
)

if(WIN32)
    list(APPEND SAMPLER_SOURCES src/sampler/windows_sampler.cpp)
    # Windows requires linking against psapi and advapi32 for process/memory info
    set(PLATFORM_LIBS psapi advapi32)
else()
    list(APPEND SAMPLER_SOURCES src/sampler/linux_sampler.cpp)
    set(PLATFORM_LIBS pthread)
endif()

# Core static library
add_library(perfscope_lib STATIC
    ${SAMPLER_SOURCES}
    src/stats/stats_calculator.cpp
    src/anomaly/anomaly_detector.cpp
    src/csv/csv_io.cpp
    src/bench/benchmark_runner.cpp
    src/cli/cli_parser.cpp
)

target_include_directories(perfscope_lib PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(perfscope_lib PUBLIC ${PLATFORM_LIBS})

# Main executable
add_executable(perfscope src/main.cpp)
target_link_libraries(perfscope PRIVATE perfscope_lib)

# Testing
enable_testing()

add_executable(perfscope_tests
    tests/test_main.cpp
    tests/test_stats.cpp
    tests/test_csv.cpp
    tests/test_anomaly.cpp
)

target_include_directories(perfscope_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests
)

target_link_libraries(perfscope_tests PRIVATE perfscope_lib)

add_test(NAME AllTests COMMAND perfscope_tests)
