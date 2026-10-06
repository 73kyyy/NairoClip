add_executable(nairoclip_tests
    test_main.cpp
    test_rational.cpp
    test_time.cpp
    test_model.cpp
    test_editor.cpp
    test_keyframe.cpp
    test_transition.cpp
    test_crosstrack.cpp
)

target_link_libraries(nairoclip_tests PRIVATE nairoclip_core)

if(MSVC)
    target_compile_options(nairoclip_tests PRIVATE /W4 /utf-8)
else()
    target_compile_options(nairoclip_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME nairoclip_tests COMMAND nairoclip_tests)
