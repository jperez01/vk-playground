# Keep all exercise checks in one runner; test the same math implementation as demo.
add_executable(rendering_exercises EXCLUDE_FROM_ALL
    rendering_exercises.cpp
)
set_target_properties(rendering_exercises PROPERTIES
    CXX_STANDARD 20
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(rendering_exercises PRIVATE rendering_math)
add_test(NAME rendering_exercises COMMAND rendering_exercises)
