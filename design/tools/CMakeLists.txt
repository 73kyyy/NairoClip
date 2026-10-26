# 设计桥（nc_design_harness）：仅用于设计评审与平台层对接参考，不进入产品运行时。
# FFmpeg / ONNX Runtime 通过 nairoclip_core 的 PUBLIC 依赖自动传递；另需 zlib（PNG 编码）。

find_package(ZLIB REQUIRED)

add_executable(nc_design_harness nc_design_harness.cpp)

target_link_libraries(nc_design_harness PRIVATE
    nairoclip_core
    ZLIB::ZLIB
)

if(MSVC)
    target_compile_options(nc_design_harness PRIVATE /W4 /utf-8)
else()
    target_compile_options(nc_design_harness PRIVATE -Wall -Wextra)
endif()
