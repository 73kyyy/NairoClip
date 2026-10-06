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
    test_export_executor.cpp
)

target_link_libraries(nairoclip_tests PRIVATE nairoclip_core)

if(MSVC)
    target_compile_options(nairoclip_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME nairoclip_tests COMMAND nairoclip_tests)
