
# Add source to this project's executable.
add_executable (demo
  main.cpp
  vk_engine.cpp
  vk_engine.h

  vk_loader.h
  vk_loader.cpp
  utils/util.cpp
  utils/util.h
  utils/util.h
  model/model.cpp
  model/model.h
        app/application.cpp
        app/application.h
        app/ui.cpp
        app/ui.h
)

find_package(assimp CONFIG REQUIRED)

set_property(TARGET demo PROPERTY CXX_STANDARD 20)
set_property(TARGET demo PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:demo>)

target_include_directories(demo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(demo vkguide_shared vkbootstrap imgui fastgltf::fastgltf assimp::assimp)

add_custom_command(TARGET demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:demo> $<TARGET_FILE_DIR:demo>
  COMMAND_EXPAND_LISTS
  )