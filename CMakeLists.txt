cmake_minimum_required(VERSION 3.20)

project(UnifiedEngineeringWorkbench
    VERSION 0.1.0
    DESCRIPTION "PASS Science Works internship workbench (RA2311003011475)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WB_BUILD_TESTS "Build the unit test executables" ON)
option(WB_STATIC_RUNTIME "Link the C/C++ runtime statically (no runtime DLLs to ship)" ON)

find_package(Threads REQUIRED)

# Warnings and runtime linkage, applied per target through this interface lib.
add_library(wb_options INTERFACE)
if(MSVC)
    target_compile_options(wb_options INTERFACE /W4 /permissive- /utf-8)
    if(WB_STATIC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    target_compile_options(wb_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        # Keep this machine's absolute paths out of the binaries (__FILE__ in test messages).
        -ffile-prefix-map=${CMAKE_SOURCE_DIR}/=)
    if(MINGW AND WB_STATIC_RUNTIME)
        # Pulls libstdc++, libgcc and winpthread into the .exe so a clean
        # Windows machine needs no MinGW DLLs.
        target_link_options(wb_options INTERFACE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# Recorded in benchmark reports (Task D asks for compiler and optimisation flags).
string(TOUPPER "${CMAKE_BUILD_TYPE}" _wb_build_type_upper)
set(WB_BUILD_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_wb_build_type_upper}}")
string(STRIP "${WB_BUILD_FLAGS}" WB_BUILD_FLAGS)

# ---------------------------------------------------------------------------
# Core library: all numerics, parsing, concurrency and device mocks.
# Pure C++17, no UI and no transport code.
# ---------------------------------------------------------------------------
add_library(wb_core STATIC
    src/core/common/Errors.cpp
    src/core/fea/ThermalRod1D.cpp
    src/core/batch/BatchGenerator.cpp
    src/core/batch/BatchRunner.cpp
    src/core/batch/Benchmark.cpp
    src/core/ml/Backprop.cpp
    src/core/platform/SystemInfo.cpp
    src/core/io/TextFile.cpp
    src/core/io/StlReader.cpp
    src/core/io/NodalCsvReader.cpp
    src/core/hardware/MockAdapters.cpp
    src/core/hardware/DeviceManager.cpp
    src/core/report/Exports.cpp
)
target_include_directories(wb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wb_core PUBLIC Threads::Threads PRIVATE wb_options)
target_compile_definitions(wb_core PRIVATE
    WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    WB_BUILD_FLAGS="${WB_BUILD_FLAGS}")
if(WIN32)
    target_link_libraries(wb_core PRIVATE advapi32)  # RegGetValueA in SystemInfo
endif()

# ---------------------------------------------------------------------------
# Vendored header-only libraries (see docs/DEPENDENCIES.md for versions).
# Marked SYSTEM so their warnings don't drown ours.
# ---------------------------------------------------------------------------
add_library(wb_thirdparty INTERFACE)
target_include_directories(wb_thirdparty SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)  # <nlohmann/json.hpp>

# ---------------------------------------------------------------------------
# Launch: local web server + browser UI. Transport/presentation only - all
# computation is in wb_core.
# ---------------------------------------------------------------------------
add_executable(Launch
    src/app/main.cpp
    src/app/Platform.cpp
    src/app/JobManager.cpp
    src/app/StaticFiles.cpp
    src/app/routes/SystemRoutes.cpp
    src/app/routes/GeometryRoutes.cpp
    src/app/routes/ThermalRoutes.cpp
    src/app/routes/PerformanceRoutes.cpp
    src/app/routes/HardwareRoutes.cpp
    src/app/routes/BackpropRoutes.cpp
)
target_include_directories(Launch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Launch PRIVATE wb_core wb_thirdparty wb_options)
if(WIN32)
    # Windows 10+ APIs (httplib requires them); sockets and ShellExecute.
    target_compile_definitions(Launch PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(Launch PRIVATE ws2_32 shell32)
endif()

# Developer tools: example-data and results generators.
add_executable(wb_make_sample_data src/tools/make_sample_data.cpp)
target_link_libraries(wb_make_sample_data PRIVATE wb_core wb_options)
add_executable(wb_results src/tools/generate_results.cpp)
target_link_libraries(wb_results PRIVATE wb_core wb_options)

# ---------------------------------------------------------------------------
# Unit tests: one executable, run through CTest.
# ---------------------------------------------------------------------------
if(WB_BUILD_TESTS)
    enable_testing()
    add_executable(wb_tests
        tests/test_main.cpp
        tests/test_fea.cpp
        tests/test_backprop.cpp
        tests/test_batch.cpp
        tests/test_io.cpp
        tests/test_hardware.cpp
    )
    target_link_libraries(wb_tests PRIVATE wb_core wb_options)
    add_test(NAME wb_tests COMMAND wb_tests)
endif()
