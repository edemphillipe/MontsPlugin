cmake_minimum_required(VERSION 3.24)

project(GuitarStudioPro VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # Universal binary: Apple Silicon (M1..M4+) and Intel
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GSP_BUILD_APP   "Build the standalone application" ON)
option(GSP_BUILD_TESTS "Build the test suite" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(src/core)

if(GSP_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(GSP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
