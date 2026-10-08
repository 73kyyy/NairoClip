add_executable(nairoclip_tests
    test_main.cpp
    test_rational.cpp
    test_time.cpp
    test_model.cpp
    test_editor.cpp
    test_keyframe.cpp
    test_transition.cpp
    test_crosstrack.cpp
    test_speed_curve.cpp
    test_json.cpp
    test_serialization.cpp
    test_recovery.cpp
    test_animation.cpp
    test_export_plan.cpp
    test_capi.cpp
    test_roundtrip.cpp
    test_subtitle.cpp
    test_stress.cpp
    test_media_io.cpp
    test_effects.cpp
    test_audio_mixer.cpp
    test_preview.cpp
    test_reference_style.cpp
    test_transition_render.cpp
    test_auto_edit.cpp
    test_boundary.cpp
    test_scene_detect.cpp
    test_e2e_workflow.cpp
    test_media_ffmpeg.cpp
    test_media_writer.cpp
    test_export_runner.cpp
    test_preview_ffmpeg.cpp
    test_thumbnail.cpp
    test_ai_label.cpp
    test_waveform.cpp
    test_reference_workflow_ffmpeg.cpp
    test_export_stress.cpp
    test_subtitle_io.cpp
    test_project_doctor.cpp
    test_export_executor.cpp
    test_ripple.cpp
    test_frame_fit.cpp
    test_beat_detect.cpp
    test_auto_duck.cpp
    test_compound.cpp
    test_mixer_console.cpp
    test_watermark.cpp
    test_entitlement.cpp
    test_ai_check.cpp
    test_proxy.cpp
    test_export_preset.cpp
    test_speech_cut.cpp
    test_reframe.cpp
)

target_link_libraries(nairoclip_tests PRIVATE nairoclip_core)

if(MSVC)
    target_compile_options(nairoclip_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME nairoclip_tests COMMAND nairoclip_tests)
