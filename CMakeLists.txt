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

# 设计桥：仅桌面/开发构建需要（依赖 zlib 的 PNG 编码与本地 HTTP）。
# 交叉编译（Android/iOS）与 CI 出包时被剔除，因此按目录存在性条件加入，
# 避免「剔除后配置阶段失败」。
option(NAIROCLIP_BUILD_DESIGN_HARNESS "Build design review harness (desktop)" ON)
if(NAIROCLIP_BUILD_DESIGN_HARNESS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/design/tools/CMakeLists.txt")
    add_subdirectory(design/tools)
endif()

if(NAIROCLIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# B0-4：C ABI 所有权标注检查（新增 ABI 必须带标注）。
# 手动运行：cmake --build . --target check_abi_ownership
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(check_abi_ownership
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_abi_ownership.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "检查 capi.h 出参所有权标注"
    )
endif()
