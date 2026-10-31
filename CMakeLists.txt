# CMakeList.txt : CMake project for vulkan_guide, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.21)

project ("vulkan_guide")

find_program(SLANGC NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
if(NOT EXISTS "${SLANGC}")
    message(FATAL_ERROR "Slang compiler does not exist: ${SLANGC}")
endif()

find_package(Vulkan REQUIRED)

add_subdirectory(third_party)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(DEMO_SCENE_FILE "${PROJECT_SOURCE_DIR}/assets/structure.glb" CACHE FILEPATH
    "Default scene staged beside demo; packaging sets this to the checksum-verified scene")
set(DEMO_SCENE_SOURCE_FILE "" CACHE FILEPATH "Checksum-pinned source GLB used for a demo package")
set(DEMO_SCENE_SHA256 "" CACHE STRING "Checksum of the prepared demo-package scene")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

add_subdirectory(shared)

add_subdirectory(src)

file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/spirv")
file(GLOB_RECURSE SLANG_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/shaders/*.slang"
    )

foreach(SLANG ${SLANG_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${SLANG} NAME)

  if(FILE_NAME MATCHES "_comp\\.slang$")
      set(STAGE "compute")
      string(REGEX REPLACE "_comp\\.slang$" ".comp.spv" OUT_NAME ${FILE_NAME})
      set(SPIRV "${PROJECT_SOURCE_DIR}/spirv/${OUT_NAME}")
      message(STATUS "${SLANG} -> ${SPIRV}")
      add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${SLANGC} ${SLANG} -target spirv -matrix-layout-column-major -stage ${STAGE} -entry main -o ${SPIRV}
        DEPENDS ${SLANG}
        VERBATIM
      )
      list(APPEND SPIRV_BINARY_FILES ${SPIRV})
  else()
      file(READ ${SLANG} FILE_CONTENT)
      
      set(HAS_VERTEX FALSE)
      set(HAS_FRAGMENT FALSE)
      
      if(FILE_CONTENT MATCHES "vertexMain")
          set(HAS_VERTEX TRUE)
      endif()
      if(FILE_CONTENT MATCHES "fragmentMain")
          set(HAS_FRAGMENT TRUE)
      endif()
      
      if(HAS_VERTEX)
          string(REGEX REPLACE "\\.slang$" ".vert.spv" OUT_NAME ${FILE_NAME})
          set(SPIRV "${PROJECT_SOURCE_DIR}/spirv/${OUT_NAME}")
          message(STATUS "${SLANG} [vertex] -> ${SPIRV}")
          add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${SLANGC} ${SLANG} -target spirv -matrix-layout-column-major -stage vertex -entry vertexMain -o ${SPIRV}
            DEPENDS ${SLANG}
            VERBATIM
          )
          list(APPEND SPIRV_BINARY_FILES ${SPIRV})
      endif()
      
      if(HAS_FRAGMENT)
          string(REGEX REPLACE "\\.slang$" ".frag.spv" OUT_NAME ${FILE_NAME})
          set(SPIRV "${PROJECT_SOURCE_DIR}/spirv/${OUT_NAME}")
          message(STATUS "${SLANG} [fragment] -> ${SPIRV}")
          add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${SLANGC} ${SLANG} -target spirv -matrix-layout-column-major -stage fragment -entry fragmentMain -o ${SPIRV}
            DEPENDS ${SLANG}
            VERBATIM
          )
          list(APPEND SPIRV_BINARY_FILES ${SPIRV})
      endif()
  endif()
endforeach(SLANG)

# Derive startup checks from the shader outputs rather than duplicating their names.
set(SHADER_RESOURCE_ENTRIES "")
foreach(SPIRV_FILE IN LISTS SPIRV_BINARY_FILES)
    get_filename_component(SPIRV_NAME "${SPIRV_FILE}" NAME)
    string(APPEND SHADER_RESOURCE_ENTRIES "    \"spirv/${SPIRV_NAME}\",\n")
endforeach()
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/generated/runtime_shader_files.h"
    CONTENT "#pragma once\ninline constexpr const char* runtimeShaderFiles[] = {\n@SHADER_RESOURCE_ENTRIES@};\n"
    @ONLY
)
target_include_directories(demo PRIVATE "${PROJECT_BINARY_DIR}/generated")

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )

# Always stage runtime inputs, even when demo itself does not need relinking.
# CMP0112 (NEW since our minimum version) keeps TARGET_FILE_DIR from adding
# a dependency back to demo.
add_custom_target(RuntimeResources
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:demo>/assets" "$<TARGET_FILE_DIR:demo>/spirv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DEMO_SCENE_FILE}" "$<TARGET_FILE_DIR:demo>/assets/structure.glb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SPIRV_BINARY_FILES} "$<TARGET_FILE_DIR:demo>/spirv"
    DEPENDS Shaders
    COMMAND_EXPAND_LISTS
    VERBATIM
)

include(cmake/DemoInstall.cmake)
