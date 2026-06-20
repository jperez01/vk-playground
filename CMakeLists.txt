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

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
    )
