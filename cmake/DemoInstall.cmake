# Only the Demo component is distributable; dependency projects also define SDK installs.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    return()
endif()

install(CODE [[
    if(NOT CMAKE_INSTALL_CONFIG_NAME MATCHES "^(Release|RelWithDebInfo)$")
        message(FATAL_ERROR "Demo packaging requires Release or RelWithDebInfo; Debug runtimes are not distributable.")
    endif()
]] COMPONENT Demo)

# Never install the developer's unlicensed/private scene by accident.
file(READ "${PROJECT_SOURCE_DIR}/packaging/demo-scene.json" _demo_scene_manifest)
string(JSON _demo_scene_sha256 GET "${_demo_scene_manifest}" sha256)
install(CODE "
    file(SHA256 \"${DEMO_SCENE_SOURCE_FILE}\" _demo_source_sha256)
    file(SHA256 \"${DEMO_SCENE_FILE}\" _demo_actual_scene_sha256)
    if(NOT _demo_source_sha256 STREQUAL \"${_demo_scene_sha256}\" OR
       NOT _demo_actual_scene_sha256 STREQUAL \"${DEMO_SCENE_SHA256}\")
        message(FATAL_ERROR \"Demo scene differs from the licensed, pinned asset. Run scripts/package-demo.ps1.\")
    endif()
" COMPONENT Demo)

install(TARGETS demo RUNTIME_DEPENDENCY_SET DemoDependencies
    RUNTIME DESTINATION . COMPONENT Demo)
# CMake's dependency scanner excludes targets built in this project, including SDL.
install(FILES $<TARGET_RUNTIME_DLLS:demo> DESTINATION . COMPONENT Demo)
install(RUNTIME_DEPENDENCY_SET DemoDependencies
    DIRECTORIES "$<TARGET_FILE_DIR:demo>"
    PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
        "^[Vv][Uu][Ll][Kk][Aa][Nn]-1\\.[Dd][Ll][Ll]$"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*"
    RUNTIME DESTINATION . COMPONENT Demo)

# Use only files Microsoft places in its release CRT redistribution directory.
# Select the newest release CRT in the compiler's VS installation. v14 runtimes
# are backward-compatible; never search System32 or debug_nonredist.
string(REGEX REPLACE "/Tools/MSVC/.*$" "/Redist/MSVC" _demo_redist_root "${CMAKE_CXX_COMPILER}")
file(GLOB _demo_crt_directories LIST_DIRECTORIES TRUE
    "${_demo_redist_root}/[0-9]*/x64/Microsoft.VC*.CRT")
list(SORT _demo_crt_directories COMPARE NATURAL ORDER DESCENDING)
if(NOT _demo_crt_directories)
    message(FATAL_ERROR "MSVC x64 release CRT redistribution directory was not found.")
endif()
list(GET _demo_crt_directories 0 _demo_crt_directory)
foreach(required_runtime IN ITEMS msvcp140.dll vcruntime140.dll)
    if(NOT EXISTS "${_demo_crt_directory}/${required_runtime}")
        message(FATAL_ERROR "Missing redistributable runtime: ${_demo_crt_directory}/${required_runtime}")
    endif()
endforeach()
file(GLOB _demo_crt_files "${_demo_crt_directory}/*.dll")
install(PROGRAMS ${_demo_crt_files} DESTINATION . COMPONENT Demo)

install(FILES "${DEMO_SCENE_FILE}" DESTINATION assets RENAME structure.glb COMPONENT Demo)
install(FILES ${SPIRV_BINARY_FILES} DESTINATION spirv COMPONENT Demo)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE.txt" DESTINATION . COMPONENT Demo)
install(FILES "${PROJECT_SOURCE_DIR}/packaging/README.txt" DESTINATION . COMPONENT Demo)
install(FILES "${PROJECT_SOURCE_DIR}/packaging/demo-scene.json"
    DESTINATION licenses COMPONENT Demo)
include("${CMAKE_CURRENT_LIST_DIR}/DemoNotices.cmake")

# The wrapper uses generated target paths, never guesses a generator's config layout.
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/demo-executable-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:demo>")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/demo-shaders.txt"
    CONTENT "$<JOIN:${SPIRV_BINARY_FILES},\n>\n")
