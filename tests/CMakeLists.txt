add_executable(nairoclip_tests
    test_main.cpp
    test_rational.cpp
    test_time.cpp
    test_model.cpp
    test_model_invariants.cpp
    test_thread_safety.cpp
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
    test_capi_timeline.cpp
    test_capi_media.cpp
    test_person_effects.cpp
    test_filters.cpp
    test_ai_task.cpp
    test_background_fill.cpp
    test_transition_library.cpp
    test_ai_advanced.cpp
    test_depth_ai.cpp
    test_super_res_ai.cpp
    test_translate_ai.cpp
    test_matting_ai.cpp
    test_speech_ai.cpp
    test_interpolation_ai.cpp
    test_ai_completion.cpp
    test_roundtrip.cpp
    test_subtitle.cpp
    test_stress.cpp
    test_media_io.cpp
    test_effects.cpp
    test_audio_mixer.cpp
    test_audio_denoise.cpp
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
    test_batch_ops.cpp
    test_mask.cpp
    test_chroma_key.cpp
    test_spatial_effects.cpp
    test_screen_effects.cpp
    test_atmosphere_motion.cpp
    test_ai_engine.cpp
    test_codec_policy.cpp
    test_ai_marking.cpp
    test_reframe.cpp
    test_crypto.cpp
    test_chapter.cpp
    test_export_queue.cpp
    test_smart_movie.cpp
    test_invisible_watermark.cpp
    test_heartbeat.cpp
    test_media_offline.cpp
    test_model_manager.cpp
    test_blend_modes.cpp
    test_shake_effect.cpp
    test_lut.cpp
    test_track_groups.cpp
    test_render_baseline.cpp
    test_freeze_frame.cpp
    test_light_effects.cpp
    test_motion_blur_directional.cpp
    test_capi_errors.cpp
    test_capi_ownership.cpp
    test_bounds_audit.cpp
    test_edit_template.cpp
    test_template_fill.cpp
    test_template_apply.cpp
    test_capi_template.cpp
)

target_link_libraries(nairoclip_tests PRIVATE nairoclip_core)

if(MSVC)
    target_compile_options(nairoclip_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_tests PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 测试素材前置
# ---------------------------------------------------------------------------
# 背景：FFmpeg 真实编解码用例依赖 /tmp/media_gen/test_av.mp4 等素材。
# 这些素材由 tests/fixtures/generate_media.sh 生成，但此前没有任何自动化
# 步骤调用它——新环境首次跑测试必然出现若干失败（或被静默跳过）。
#
# 处置：把生成逻辑挂到测试可执行文件的构建后步骤（POST_BUILD），
# 幂等（脚本内部判断已存在则跳过），失败不阻断构建（无 ffmpeg 的机器
# 仍可编译，只是相关用例会跳过）。
# 用 ffmpeg CLI 是否存在判断（生成脚本本身依赖 CLI，而非 libav*）。
find_program(NAIROCLIP_FFMPEG_CLI ffmpeg)
if(NAIROCLIP_FFMPEG_CLI)
    add_custom_command(TARGET nairoclip_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "准备测试素材 /tmp/media_gen ..."
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/generate_media.sh /tmp/media_gen
        COMMENT "生成 FFmpeg 测试素材（幂等）"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# 定向测试靶
# ---------------------------------------------------------------------------
# 背景：测试框架是自研的 NC_TEST，main() 不做用例过滤，只能整套跑。全量套件
# 需数分钟，验证单个改动（如 B0-5 版本迁移）时反馈太慢，也容易在超时被截断时
# 分不清是"没跑到"还是"失败"。
#
# 处置：按主题拆出小靶，只链接相关用例源文件，用完即弃，不影响主套件。
add_executable(nairoclip_serialization_tests
    test_main.cpp
    test_serialization.cpp
)
target_link_libraries(nairoclip_serialization_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_serialization_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_serialization_tests PRIVATE -Wall -Wextra)
endif()

add_executable(nairoclip_editor_tests
    test_main.cpp
    test_editor.cpp
)
target_link_libraries(nairoclip_editor_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_editor_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_editor_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_blend_modes_tests
    test_main.cpp
    test_blend_modes.cpp
)
target_link_libraries(nairoclip_blend_modes_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_blend_modes_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_blend_modes_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_shake_tests
    test_main.cpp
    test_shake_effect.cpp
)
target_link_libraries(nairoclip_shake_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_shake_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_shake_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_lut_tests
    test_main.cpp
    test_lut.cpp
)
target_link_libraries(nairoclip_lut_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_lut_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_lut_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_track_groups_tests
    test_main.cpp
    test_track_groups.cpp
)
target_link_libraries(nairoclip_track_groups_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_track_groups_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_track_groups_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_render_baseline_tests
    test_main.cpp
    test_render_baseline.cpp
)
target_link_libraries(nairoclip_render_baseline_tests PRIVATE nairoclip_core)
target_include_directories(nairoclip_render_baseline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(nairoclip_render_baseline_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_render_baseline_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_freeze_tests
    test_main.cpp
    test_freeze_frame.cpp
)
target_link_libraries(nairoclip_freeze_tests PRIVATE nairoclip_core)
target_include_directories(nairoclip_freeze_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(nairoclip_freeze_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_freeze_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_light_tests
    test_main.cpp
    test_light_effects.cpp
)
target_link_libraries(nairoclip_light_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_light_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_light_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_motionblur_tests
    test_main.cpp
    test_motion_blur_directional.cpp
)
target_link_libraries(nairoclip_motionblur_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_motionblur_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_motionblur_tests PRIVATE -Wall -Wextra)
endif()
add_executable(nairoclip_capi_errors_tests
    test_main.cpp
    test_capi_errors.cpp
)
target_link_libraries(nairoclip_capi_errors_tests PRIVATE nairoclip_core)
if(MSVC)
    target_compile_options(nairoclip_capi_errors_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_capi_errors_tests PRIVATE -Wall -Wextra)
endif()










add_test(NAME nairoclip_tests COMMAND nairoclip_tests)
# ctest 默认在 build/ 下运行；测试内部已做仓库根解析，但从源目录运行
# 更稳妥（部分用例使用相对路径读取 models/ 与 fixtures）。
set_tests_properties(nairoclip_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)



