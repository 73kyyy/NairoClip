cmake_minimum_required(VERSION 3.16)
project(NairoClip VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NAIROCLIP_BUILD_TESTS "Build NairoClip tests" ON)

add_subdirectory(core)

if(NAIROCLIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
