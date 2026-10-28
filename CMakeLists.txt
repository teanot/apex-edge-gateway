cmake_minimum_required(VERSION 3.20)
project(apex_edge_gateway
    VERSION 1.0.0
    DESCRIPTION "High-Performance Edge IoT Telemetry Gateway & Anomaly Detector"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization and diagnostics
if (MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

include_directories(include)

# Core library sources
set(APEX_CORE_SOURCES
    src/anomaly_detector.cpp
    src/sensor_source.cpp
    src/persistence_journal.cpp
    src/mqtt_dispatcher.cpp
    src/gateway_engine.cpp
)

# Core static library
add_library(apex_core STATIC ${APEX_CORE_SOURCES})
target_include_directories(apex_core PUBLIC include)

# Platform socket libraries
if (WIN32)
    target_link_libraries(apex_core PUBLIC ws2_32)
endif()

# Main Gateway executable
add_executable(apex_edge src/main.cpp)
target_link_libraries(apex_edge PRIVATE apex_core)

# Unit Testing
enable_testing()
add_subdirectory(tests)
