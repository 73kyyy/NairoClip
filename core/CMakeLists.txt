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
)
target_include_directories(nairoclip_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---- FFmpeg 可选集成 ----
# 通过 pkg-config 检测；未安装 FFmpeg 时功能自动降级（Mock 路径仍可用）。
find_package(PkgConfig QUIET)
set(NAIROCLIP_FFMPEG_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample)
    if(FFMPEG_FOUND)
        set(NAIROCLIP_FFMPEG_FOUND TRUE)
        target_sources(nairoclip_core PRIVATE src/media_ffmpeg.cpp)
        target_link_libraries(nairoclip_core PUBLIC PkgConfig::FFMPEG)
        target_compile_definitions(nairoclip_core PUBLIC NAIROCLIP_HAS_FFMPEG=1)
        message(STATUS "NairoClip: FFmpeg found — real media decoding enabled")
    endif()
endif()
if(NOT NAIROCLIP_FFMPEG_FOUND)
    message(STATUS "NairoClip: FFmpeg not found — using Mock media readers")
endif()

if(MSVC)
    target_compile_options(nairoclip_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_core PRIVATE -Wall -Wextra)
endif()
