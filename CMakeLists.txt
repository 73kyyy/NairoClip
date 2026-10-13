cmake_minimum_required(VERSION 3.16)
project(NairoClip VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NAIROCLIP_BUILD_TESTS "Build NairoClip tests" ON)
# 编解码合规：开发例外开关（默认 ON = 沙箱/CI 仅有 libx264 时测试可跑）。
# 发布构建必须使用 -DNAIROCLIP_DEVELOPMENT_GPL_OK=OFF（拒绝 GPL 编码器，
# 强制硬件编码器或 OpenH264 兜底；见 docs/COMPLIANCE.md）。
option(NAIROCLIP_DEVELOPMENT_GPL_OK
       "Allow GPL encoders (libx264) for DEVELOPMENT builds only" ON)

add_subdirectory(core)
add_subdirectory(tools)

if(NAIROCLIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
