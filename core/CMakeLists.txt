add_library(nairoclip_core STATIC
    src/rational.cpp
    src/time.cpp
    src/clip.cpp
    src/track.cpp
    src/sequence.cpp
    src/project.cpp
    src/commands.cpp
    src/editor.cpp
    src/keyframe.cpp
    src/transition.cpp
    src/speed_curve.cpp
    src/json.cpp
    src/serialization.cpp
    src/recovery.cpp
    src/perf_probe.cpp
    src/thread_pool.cpp
    src/simd.cpp
    src/text_content.cpp
    src/crash_handler.cpp
    src/animation.cpp
    src/export_plan.cpp
    src/capi.cpp
    src/subtitle.cpp
    src/media_io.cpp
    src/effects.cpp
    src/audio_mixer.cpp
    src/preview.cpp
    src/reference_style.cpp
    src/transition_render.cpp
    src/auto_edit.cpp
    src/project_doctor.cpp
    src/scene_detect.cpp
    src/export_executor.cpp
    src/export_runner.cpp
    src/thumbnail.cpp
    src/ai_label.cpp
    src/waveform.cpp
    src/subtitle_io.cpp
    src/media_writer.cpp
    src/frame_fit.cpp
    src/xform.cpp
    src/beat_detect.cpp
    src/auto_duck.cpp
    src/watermark.cpp
    src/entitlement.cpp
    src/ai_check.cpp
    src/proxy.cpp
    src/export_preset.cpp
    src/speech_cut.cpp
    src/mask.cpp
    src/batch_ops.cpp
    src/reframe.cpp
    src/crypto.cpp
    src/chapter.cpp
    src/export_queue.cpp
    src/smart_movie.cpp
    src/invisible_watermark.cpp
    src/heartbeat.cpp
    src/chroma_key.cpp
    src/spatial_effects.cpp
    src/screen_effects.cpp
    src/person_effects.cpp
    src/filters.cpp
    src/ai_task.cpp
    src/background_fill.cpp
    src/depth_ai.cpp
    src/matting_ai.cpp
    src/speech_ai.cpp
    src/interpolation_ai.cpp
    src/stem_ai.cpp
    src/super_res_ai.cpp
    src/translate_ai.cpp
    src/blend_modes.cpp
    src/render_engine.cpp
    src/particle_engine.cpp
    src/atmosphere_effects.cpp
    src/audio_denoise.cpp
    src/motion_effects.cpp
    src/shake_effect.cpp
    src/lut.cpp
    src/ref_style_analyzer.cpp
    src/light_effects.cpp
    src/edit_template.cpp
    src/template_fill.cpp
    src/template_apply.cpp
    src/motion_blur_directional.cpp
    src/ai_engine.cpp
    src/codec_policy.cpp
    src/ai_marking.cpp
    src/media_offline.cpp
    src/model_manager.cpp
)

# Android 平台层通过 libnc_jni.so 链接本静态库，必须 PIC。
set_target_properties(nairoclip_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(nairoclip_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 编解码合规：开发例外开关（见根 CMakeLists 注释与 docs/COMPLIANCE.md）。
if(NAIROCLIP_DEVELOPMENT_GPL_OK)
    target_compile_definitions(nairoclip_core PUBLIC NAIROCLIP_DEVELOPMENT_GPL_OK=1)
    message(STATUS "NairoClip: DEVELOPMENT build — GPL encoder exception ENABLED "
                   "(release builds must set -DNAIROCLIP_DEVELOPMENT_GPL_OK=OFF)")
else()
    message(STATUS "NairoClip: RELEASE compliance mode — GPL encoders denied")
endif()

# ---- FFmpeg 集成 ----
#
# 两条查找路径：
#
#   1) **显式根目录**（Android 交叉编译走这条）
#      -DNAIROCLIP_FFMPEG_ROOT=/path/to/ffmpeg/<abi>
#      目录结构约定：<root>/include/  +  <root>/lib/libav*.so
#      Android 上没有 pkg-config 的 sysroot 配置，且 NDK 构建时
#      `-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON` 会让 pkg-config 直接失效 ——
#      所以必须支持显式路径。
#
#   2) pkg-config（Linux/macOS 桌面构建走这条）
#
# ⚠️ **曾经的问题**：Android CI 里传了
#      -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON
#    于是 pkg-config 分支永远不成立 → NAIROCLIP_HAS_FFMPEG 未定义
#    → APK 里编进去的是 MockMediaReader，**导入真实视频直接失败**。
#    这不是"可选降级"，是功能缺失。
set(NAIROCLIP_FFMPEG_FOUND FALSE)

if(DEFINED NAIROCLIP_FFMPEG_ROOT AND NOT NAIROCLIP_FFMPEG_ROOT STREQUAL "" AND
   NOT NAIROCLIP_FFMPEG_ROOT STREQUAL "OFF")
    set(_ff_inc "${NAIROCLIP_FFMPEG_ROOT}/include")
    if(EXISTS "${_ff_inc}/libavcodec/avcodec.h")
        set(NAIROCLIP_FFMPEG_FOUND TRUE)
        target_sources(nairoclip_core PRIVATE src/media_ffmpeg.cpp src/media_ffmpeg_writer.cpp)
        target_include_directories(nairoclip_core PUBLIC "${_ff_inc}")
        # 显式列出五个库：顺序有意义（avformat 依赖 avcodec/avutil）
        target_link_libraries(nairoclip_core PUBLIC
            "${NAIROCLIP_FFMPEG_ROOT}/lib/libavformat.so"
            "${NAIROCLIP_FFMPEG_ROOT}/lib/libavcodec.so"
            "${NAIROCLIP_FFMPEG_ROOT}/lib/libswscale.so"
            "${NAIROCLIP_FFMPEG_ROOT}/lib/libswresample.so"
            "${NAIROCLIP_FFMPEG_ROOT}/lib/libavutil.so")
        target_compile_definitions(nairoclip_core PUBLIC NAIROCLIP_HAS_FFMPEG=1)
        message(STATUS "NairoClip: FFmpeg (explicit root) — ${NAIROCLIP_FFMPEG_ROOT}")
    else()
        message(FATAL_ERROR
            "NAIROCLIP_FFMPEG_ROOT 指定了但找不到头文件：${_ff_inc}/libavcodec/avcodec.h")
    endif()
endif()

if(NOT NAIROCLIP_FFMPEG_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
            libavformat libavcodec libavutil libswscale libswresample)
        if(FFMPEG_FOUND)
            set(NAIROCLIP_FFMPEG_FOUND TRUE)
            target_sources(nairoclip_core PRIVATE src/media_ffmpeg.cpp src/media_ffmpeg_writer.cpp)
            target_link_libraries(nairoclip_core PUBLIC PkgConfig::FFMPEG)
            target_compile_definitions(nairoclip_core PUBLIC NAIROCLIP_HAS_FFMPEG=1)
            message(STATUS "NairoClip: FFmpeg (pkg-config) — real media decoding enabled")
        endif()
    endif()
endif()

if(NOT NAIROCLIP_FFMPEG_FOUND)
    message(STATUS "NairoClip: FFmpeg not found — using Mock media readers")
endif()

# ---- ONNX Runtime 可选集成（文档第一段「一、技术选型总纲」）----
# 与 FFmpeg 同一模式：构建时探测，有则启用（AI 推理真实可用），无则降级
# （aiEngineAvailable() 返回 false；不伪造推理结果）。
# 探测顺序：显式 -DNAIROCLIP_ORT_ROOT → 环境变量 ONNXRUNTIME_ROOT → 常见路径。
# 说明：显式传入无效路径（如 -DNAIROCLIP_ORT_ROOT=OFF 或 /nonexistent）时
# 不再自动回退常见路径（否则用户无法禁用 ORT——修复记录见 M2.31）。
set(NAIROCLIP_ORT_FOUND FALSE)
set(NAIROCLIP_ORT_ROOT "" CACHE STRING "ONNX Runtime 根目录（含 include/ 与 lib/）")

if(DEFINED NAIROCLIP_ORT_ROOT AND NOT NAIROCLIP_ORT_ROOT STREQUAL "")
    # 显式指定（非空）：严格按该路径（无效则报错提示，不静默回退）
    if(NAIROCLIP_ORT_ROOT STREQUAL "OFF")
        message(STATUS "NairoClip: ONNX Runtime explicitly disabled (AI inference off)")
    elseif(EXISTS "${NAIROCLIP_ORT_ROOT}/include/onnxruntime_cxx_api.h")
        set(NAIROCLIP_ORT_FOUND TRUE)
    else()
        message(STATUS "NairoClip: ONNX Runtime root '${NAIROCLIP_ORT_ROOT}' invalid "
                       "(missing include/onnxruntime_cxx_api.h) — AI inference disabled")
    endif()
elseif(DEFINED ENV{ONNXRUNTIME_ROOT} AND
       EXISTS "$ENV{ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    set(NAIROCLIP_ORT_ROOT "$ENV{ONNXRUNTIME_ROOT}")
    set(NAIROCLIP_ORT_FOUND TRUE)
else()
    # 常见路径探测（开发机/CI 手动放置；沙箱为 /workspace/deps）
    foreach(_candidate
            "/workspace/deps/onnxruntime-linux-aarch64-1.22.0"
            "/workspace/deps/onnxruntime-linux-x64-1.22.0"
            "/usr/local/onnxruntime"
            "/opt/onnxruntime")
        if(EXISTS "${_candidate}/include/onnxruntime_cxx_api.h")
            set(NAIROCLIP_ORT_ROOT "${_candidate}")
            set(NAIROCLIP_ORT_FOUND TRUE)
            break()
        endif()
    endforeach()
endif()

if(NAIROCLIP_ORT_FOUND)
    message(STATUS "NairoClip: ONNX Runtime found at ${NAIROCLIP_ORT_ROOT} — AI inference enabled")
    file(GLOB _ort_libs "${NAIROCLIP_ORT_ROOT}/lib/libonnxruntime.so*")
    target_include_directories(nairoclip_core PRIVATE "${NAIROCLIP_ORT_ROOT}/include")
    # 动态链接（与 FFmpeg 同一策略：宽松协议的动态链接要求）
    target_link_libraries(nairoclip_core PRIVATE "${NAIROCLIP_ORT_ROOT}/lib/libonnxruntime.so")
    target_compile_definitions(nairoclip_core PUBLIC NAIROCLIP_HAS_ONNX_RUNTIME=1)
elseif(NOT NAIROCLIP_ORT_ROOT STREQUAL "OFF")
    message(STATUS "NairoClip: ONNX Runtime not found — AI inference disabled "
                   "(set -DNAIROCLIP_ORT_ROOT=<path> to enable; =OFF to silence)")
endif()

if(MSVC)
    target_compile_options(nairoclip_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_core PRIVATE -Wall -Wextra)
    # clang（NDK）把列表初始化里的窄化默认当**错误**（-Wc++11-narrowing），
    # gcc 只当警告 —— 于是本地 x86_64 编得过的代码，Android 一编就失败。
    # 这类窄化多数是 int64_t → int 的帧率/尺寸字段，语义上安全。
    # 先关掉这条警告让 APK 能出包；逐个补 static_cast 属于清理项，
    # 不阻塞真机验证（已经有 media_ffmpeg_writer.cpp 那 4 处补好了）。
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(nairoclip_core PRIVATE -Wno-c++11-narrowing)
    endif()
endif()
