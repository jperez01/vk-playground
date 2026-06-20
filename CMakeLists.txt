# CMakeList.txt : CMake project for vulkan_guide, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("vulkan_guide")

find_package(Vulkan REQUIRED)

add_subdirectory(third_party)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

add_subdirectory(shared)

add_subdirectory(src)
find_program(SLANGC slangc)

file(GLOB_RECURSE SLANG_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/shaders/*.slang"
    )

foreach(SLANG ${SLANG_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${SLANG} NAME)

  if(FILE_NAME MATCHES "_frag\\.slang$")
      set(STAGE "fragment")
      string(REGEX REPLACE "_frag\\.slang$" ".frag.spv" OUT_NAME ${FILE_NAME})
  elseif(FILE_NAME MATCHES "_vert\\.slang$")
      set(STAGE "vertex")
      string(REGEX REPLACE "_vert\\.slang$" ".vert.spv" OUT_NAME ${FILE_NAME})
  elseif(FILE_NAME MATCHES "_comp\\.slang$")
      set(STAGE "compute")
      string(REGEX REPLACE "_comp\\.slang$" ".comp.spv" OUT_NAME ${FILE_NAME})
  else()
      continue()
      set(STAGE "fragment")
      string(REGEX REPLACE "\\.slang$" ".spv" OUT_NAME ${FILE_NAME})
  endif()
  
  set(SPIRV "${PROJECT_SOURCE_DIR}/spirv/${OUT_NAME}")
  message(STATUS "${SLANG} -> ${SPIRV}")
  
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${SLANGC} ${SLANG} -target spirv -matrix-layout-column-major -stage ${STAGE} -entry main -o ${SPIRV}
    DEPENDS ${SLANG}
    VERBATIM
  )
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(SLANG)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )
