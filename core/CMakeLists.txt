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
    src/export_executor.cpp
)

target_include_directories(nairoclip_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(nairoclip_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_core PRIVATE -Wall -Wextra)
endif()
