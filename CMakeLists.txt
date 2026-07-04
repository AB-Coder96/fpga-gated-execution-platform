cmake_minimum_required(VERSION 3.20)

project(
    fpga_gated_execution_platform
    VERSION 0.1.0
    DESCRIPTION "C++20 ITCH/OUCH replay, order book, risk, and execution platform with FPGA-style guardrail gating"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Global C++ settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for editors, clangd, and tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Project options
# ---------------------------------------------------------------------------

option(FGEP_BUILD_TESTS "Build unit and integration tests" ON)
option(FGEP_BUILD_APPS "Build demo applications" ON)
option(FGEP_BUILD_BENCHMARKS "Build benchmark executables" OFF)
option(FGEP_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

set(
    FGEP_EXEC_BACKEND
    "kernel_udp"
    CACHE STRING
    "Default execution backend: recording, kernel_udp, afxdp, dpdk"
)

set_property(
    CACHE FGEP_EXEC_BACKEND
    PROPERTY STRINGS
        recording
        kernel_udp
        afxdp
        dpdk
)

# ---------------------------------------------------------------------------
# Optional backend dependency detection
# ---------------------------------------------------------------------------

set(FGEP_HAVE_DPDK OFF)

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(DPDK QUIET IMPORTED_TARGET libdpdk)

    if(DPDK_FOUND)
        set(FGEP_HAVE_DPDK ON)
    endif()
endif()

set(FGEP_HAVE_AFXDP OFF)

set(FGEP_GIT_COMMIT "unknown")

find_package(Git QUIET)

if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE FGEP_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(FGEP_GIT_COMMIT STREQUAL "")
    set(FGEP_GIT_COMMIT "unknown")
endif()
# ---------------------------------------------------------------------------
# Main library target
# ---------------------------------------------------------------------------
#
# This starts as an INTERFACE library so the repository can configure before
# implementation files exist. As the project grows, this can become a normal
# static library with real source files.

add_library(
    fgep
    STATIC
        src/core/time.cpp
        src/core/types.cpp
        src/itch/itch_decode.cpp
        src/itch/itch_encode.cpp
        src/ouch/ouch_decode.cpp
        src/ouch/ouch_encode.cpp
        src/moldudp64/moldudp64_decode.cpp
        src/moldudp64/moldudp64_encode.cpp
        src/replay/moldudp64_itch_replay.cpp
        src/book/symbol_order_book.cpp
        src/book/venue_book.cpp
        src/book/multi_venue_book.cpp
        src/bbo/normalized_bbo.cpp
        src/instrument/instrument_directory.cpp
        src/replay/itch_replay_apply.cpp
        src/execution/order_lifecycle.cpp
        src/risk/risk_supervisor.cpp
        src/execution/simulated_execution_engine.cpp
        src/telemetry/pipeline_telemetry.cpp
        src/telemetry/latency_summary.cpp
        src/bench/order_candidate_generator.cpp
        src/bench/execution_benchmark.cpp
        src/bench/benchmark_report.cpp
        src/reference/symbol_fixture.cpp
        src/reference/venue_fixture.cpp
        src/execution/execution_backend.cpp
        src/execution/encoded_backend_submit.cpp
        src/execution/kernel_udp_backend.cpp
        src/execution/afxdp_backend.cpp
        src/execution/dpdk_backend.cpp
        src/execution/backend_factory.cpp
        src/gate/guardrail_state.cpp
        src/gate/permissive_gate.cpp
        src/gate/guardrail_control.cpp
        src/gate/guardrail_update_benchmark.cpp
        src/bench/backend_benchmark.cpp
        src/bench/system_metadata.cpp
        src/bench/wall_clock_backend_benchmark.cpp
        src/runtime/runtime_phase.cpp
        src/runtime/no_heap_guard.cpp
        src/gen/itch_message_builder.cpp
        src/gen/moldudp64_packet_framer.cpp
        src/net/udp_sender.cpp
)

target_include_directories(
    fgep
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(
    fgep
    PUBLIC
        cxx_std_20
)

target_compile_definitions(
    fgep
    PUBLIC
        FGEP_DEFAULT_EXEC_BACKEND="${FGEP_EXEC_BACKEND}"
        FGEP_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        FGEP_GIT_COMMIT="${FGEP_GIT_COMMIT}"
        FGEP_ENABLE_RUNTIME_NO_HEAP_TESTS=$<BOOL:${FGEP_ENABLE_RUNTIME_NO_HEAP_TESTS}>
)


if(FGEP_ENABLE_PROFILING)
    if(MSVC)
        target_compile_options(fgep PUBLIC /Zi)
    else()
        target_compile_options(fgep PUBLIC -g -fno-omit-frame-pointer)
    endif()
endif()

if(FGEP_ENABLE_THREADS)
    find_package(Threads REQUIRED)
    target_link_libraries(fgep PUBLIC Threads::Threads)
    target_compile_definitions(fgep PUBLIC FGEP_ENABLE_THREADS=1)
else()
    target_compile_definitions(fgep PUBLIC FGEP_ENABLE_THREADS=0)
endif()


if(FGEP_HAVE_DPDK)
    target_compile_definitions(fgep PUBLIC FGEP_HAVE_DPDK=1)
    target_link_libraries(fgep PUBLIC PkgConfig::DPDK)
else()
    target_compile_definitions(fgep PUBLIC FGEP_HAVE_DPDK=0)
endif()

if(FGEP_ENABLE_AFXDP)
    target_compile_definitions(fgep PUBLIC FGEP_ENABLE_AFXDP=1)
else()
    target_compile_definitions(fgep PUBLIC FGEP_ENABLE_AFXDP=0)
endif()

if(FGEP_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(fgep PUBLIC /W4 /permissive-)
    else()
        target_compile_options(
            fgep
            PUBLIC
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
        )
    endif()
endif()




# ---------------------------------------------------------------------------
# Optional subdirectories
# ---------------------------------------------------------------------------
include(cmake/ProjectOptions.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/ThirdParty.cmake)
include(CTest)

if(FGEP_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

if(FGEP_BUILD_APPS AND EXISTS "${PROJECT_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
endif()

if(FGEP_BUILD_BENCHMARKS AND EXISTS "${PROJECT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    add_subdirectory(benchmarks)
endif()

if(FGEP_GOOGLETEST_AVAILABLE)
    include(GoogleTest)

    function(fgep_add_gtest test_name source_file)
        add_executable(
            ${test_name}
            ${source_file}
        )

        target_link_libraries(
            ${test_name}
            PRIVATE
                fgep
                GTest::gtest_main
        )

        target_compile_features(
            ${test_name}
            PRIVATE
                cxx_std_20
        )

        gtest_discover_tests(${test_name})
    endfunction()
endif()
# ---------------------------------------------------------------------------
# Status summary
# ---------------------------------------------------------------------------

message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build tests: ${FGEP_BUILD_TESTS}")
message(STATUS "Build apps: ${FGEP_BUILD_APPS}")
message(STATUS "Build benchmarks: ${FGEP_BUILD_BENCHMARKS}")
message(STATUS "Enable threads: ${FGEP_ENABLE_THREADS}")
message(STATUS "Enable runtime no-heap tests: ${FGEP_ENABLE_RUNTIME_NO_HEAP_TESTS}")
message(STATUS "Enable profiling: ${FGEP_ENABLE_PROFILING}")
message(STATUS "Enable Linux I/O: ${FGEP_ENABLE_LINUX_IO}")
message(STATUS "Enable hardware counters: ${FGEP_ENABLE_HARDWARE_COUNTERS}")
message(STATUS "Enable GoogleTest: ${FGEP_ENABLE_GOOGLETEST}")
message(STATUS "Enable Google Benchmark: ${FGEP_ENABLE_GOOGLE_BENCHMARK}")
message(STATUS "Enable AF_XDP: ${FGEP_ENABLE_AFXDP}")
message(STATUS "Have DPDK: ${FGEP_HAVE_DPDK}")
message(STATUS "Git commit: ${FGEP_GIT_COMMIT}")