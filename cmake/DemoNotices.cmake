include_guard(GLOBAL)

# Offline notice staging for the Demo component. Never distribute whole amalgamated
# headers just to carry their notices. Comment delimiters are deliberately retained
# when extracting embedded notices, so copyright/permission text stays verbatim.
set(_demo_notices_dir "${PROJECT_BINARY_DIR}/demo-licenses")
set(_demo_notice_sources "${PROJECT_SOURCE_DIR}/packaging/licenses")
file(REMOVE_RECURSE "${_demo_notices_dir}")
file(MAKE_DIRECTORY "${_demo_notices_dir}")

function(_demo_notice_require source)
    if(NOT EXISTS "${source}" OR IS_DIRECTORY "${source}")
        message(FATAL_ERROR "Required Demo dependency notice is missing: ${source}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
endfunction()

function(_demo_notice_copy source destination)
    _demo_notice_require("${source}")
    configure_file("${source}" "${_demo_notices_dir}/${destination}" COPYONLY)
endfunction()

# Extract an inclusive, literal-delimited region, failing closed if vendored
# updates move/remove a notice. No line-number assumptions and no synthesized text.
function(_demo_notice_extract source destination first last)
    _demo_notice_require("${source}")
    file(READ "${source}" text)
    string(FIND "${text}" "${first}" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "Required Demo notice marker not found in ${source}: ${first}")
    endif()
    string(SUBSTRING "${text}" ${begin} -1 remainder)
    string(FIND "${remainder}" "${last}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Required Demo notice end not found in ${source}: ${last}")
    endif()
    string(LENGTH "${last}" end_length)
    math(EXPR length "${end} + ${end_length}")
    string(SUBSTRING "${remainder}" 0 ${length} notice)
    if(NOT notice MATCHES "[Cc]opyright" OR NOT notice MATCHES "[Pp]ermission|[Ll]icen[sc]e|[Rr]edistribution")
        message(FATAL_ERROR "Incomplete Demo dependency notice in ${source}")
    endif()
    file(WRITE "${_demo_notices_dir}/${destination}" "${notice}\n")
endfunction()

_demo_notice_copy("${PROJECT_SOURCE_DIR}/third_party/SDL/LICENSE.txt" "SDL2.txt")
_demo_notice_copy("${PROJECT_SOURCE_DIR}/third_party/SDL/src/video/yuv2rgb/LICENSE" "SDL2-yuv2rgb.txt")
# Select HIDAPI's permissive BSD alternative (not its optional GPL alternative).
_demo_notice_copy("${PROJECT_SOURCE_DIR}/third_party/SDL/src/hidapi/LICENSE-bsd.txt" "SDL2-hidapi.txt")
_demo_notice_copy("${PROJECT_SOURCE_DIR}/third_party/fmt/LICENSE.rst" "fmt.txt")
_demo_notice_copy("${PROJECT_SOURCE_DIR}/third_party/fastgltf/LICENSE.md" "fastgltf.txt")
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/SDL/src/libm/e_atan2.c"
    "SDL2-SunPro.txt" "/*" "*/")

_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/vkbootstrap/VkBootstrap.h"
    "vk-bootstrap.txt" "/*" "*/")
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/vkbootstrap/VkBootstrapDispatch.h"
    "vk-bootstrap-dispatch.txt" "/*" "*/")
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/vma/vk_mem_alloc.h"
    "VulkanMemoryAllocator.txt" "// Copyright" "// THE SOFTWARE.")
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/volk/volk.h"
    "volk.txt" "/**\n * Copyright (c) 2018-2019 Arseny Kapoulkine" "*/")
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/glm/glm/ext/scalar_ulp.inl"
    "glm-SunPro.txt" "/// Copyright" "/// is preserved.")

foreach(header IN ITEMS stb_image/stb_image.h imgui/imstb_rectpack.h imgui/imstb_textedit.h imgui/imstb_truetype.h)
    get_filename_component(name "${header}" NAME_WE)
    _demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/${header}"
        "${name}.txt" "ALTERNATIVE A - MIT License" "*/")
endforeach()

_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/fastgltf/deps/simdjson/simdjson.cpp"
    "simdjson-pytorch.txt" "/* From\nhttps://github.com/endorno/pytorch/" "*/")
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/fastgltf/deps/simdjson/simdjson.cpp"
    "simdjson-string-view-lite-attribution.txt" "// Copyright 2017-2020 by Martin Moene" "// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)")
# Keep the Loitsch attribution embedded in the number parser as well as the
# complete Apache license supplied by simdjson's matching upstream release.
_demo_notice_extract("${PROJECT_SOURCE_DIR}/third_party/fastgltf/deps/simdjson/simdjson.cpp"
    "simdjson-number-parser-attribution.txt" "The code is distributed under the MIT license, Copyright (c) 2009 Florian" "*/")

# Missing vendored notices, downloaded once and checked in verbatim. These are
# immutable source URLs, documentation only: configure/install NEVER use network.
# ImGui imgui.h identifies 1.92.2 WIP; imgui.cpp has the same 2014-2025 copyright
# as this 1.92.2 release notice. GLM setup.hpp identifies 0.9.9.7; simdjson.h 3.3.0.
# imgui-LICENSE.txt:
# https://raw.githubusercontent.com/ocornut/imgui/2b24f5fa71d124de108da54d9da49ea3049dc91e/LICENSE.txt
# glm-copying.txt:
# https://raw.githubusercontent.com/g-truc/glm/13724cfae64a8b5313d1cabc9a963d2c9dbeda12/copying.txt
# simdjson-LICENSE.txt:
# https://raw.githubusercontent.com/simdjson/simdjson/1875ed65503a2ea9cc4010581bb0fe2a4c40b5bc/LICENSE
# string-view-lite-LICENSE.txt (Boost 1.0 text referenced by the embedded notice):
# https://raw.githubusercontent.com/martinmoene/string-view-lite/7b1867b7c08f9ac010fb8b9dae79ff8c8359bafb/LICENSE.txt
# proggyclean-LICENSE.txt (original notice mirrored with the unmodified font):
# https://raw.githubusercontent.com/chrissimpkins/codeface/102cca6b53b374ed11e53f8daa0b969c4b5e2859/fonts/proggy-clean/license.txt
# Vulkan-Headers v1.4.350, for SDK header references to otherwise missing licenses:
# https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/8864cdc896bbc2a9b6eb36b3218fc9ef57908d77/LICENSE.md
# https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/8864cdc896bbc2a9b6eb36b3218fc9ef57908d77/LICENSES/Apache-2.0.txt
# https://raw.githubusercontent.com/KhronosGroup/Vulkan-Headers/8864cdc896bbc2a9b6eb36b3218fc9ef57908d77/LICENSES/MIT.txt
# simdjson-grisu-LICENSE.txt (MIT notice for the attributed JSON/Grisu2 code):
# https://raw.githubusercontent.com/nlohmann/json/bc889afb4c5bf1c0d8ee29ef35eaaf4c8bef8a5d/docs/mkdocs/docs/home/license.md
# Scene provenance is recorded in demo-scene-attribution.txt; exact CC0 notice:
# https://raw.githubusercontent.com/anishthite/cc0-models-Polygonal-Mind/56db2d4088512531a070d0bf3eb9d284d077528d/License.md
# Microsoft-runtime.txt identifies the separately licensed release redist.
foreach(notice IN ITEMS imgui-LICENSE.txt glm-copying.txt simdjson-LICENSE.txt simdjson-grisu-LICENSE.txt
        string-view-lite-LICENSE.txt proggyclean-LICENSE.txt
        demo-scene-CC0.txt demo-scene-attribution.txt Microsoft-runtime.txt
        vulkan-headers-LICENSE.txt vulkan-headers-LICENSES-Apache-2.0.txt
        vulkan-headers-LICENSES-MIT.txt)
    _demo_notice_copy("${_demo_notice_sources}/${notice}" "${notice}")
endforeach()

# Preserve the actual SDK's copyright years/SPDX notices, not only the generic
# Apache/MIT texts above. Include vk_video because Vulkan headers include it.
set(_demo_vulkan_includes ${Vulkan_INCLUDE_DIRS} "${Vulkan_INCLUDE_DIR}")
list(REMOVE_DUPLICATES _demo_vulkan_includes)
set(_demo_vulkan_notice_count 0)
foreach(include_dir IN LISTS _demo_vulkan_includes)
    if(NOT EXISTS "${include_dir}/vulkan/vulkan_core.h")
        continue()
    endif()
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        "${include_dir}/vulkan/*.h" "${include_dir}/vulkan/*.hpp"
        "${include_dir}/vk_video/*.h")
    foreach(header IN LISTS headers)
        _demo_notice_require("${header}")
        file(READ "${header}" prefix LIMIT 4096)
        string(FIND "${prefix}" "/*" begin)
        if(begin LESS 0)
            continue()
        endif()
        string(SUBSTRING "${prefix}" ${begin} -1 remainder)
        string(FIND "${remainder}" "*/" end)
        if(end LESS 0)
            continue()
        endif()
        math(EXPR length "${end} + 2")
        string(SUBSTRING "${remainder}" 0 ${length} notice)
        if(notice MATCHES "[Cc]opyright" AND notice MATCHES "SPDX-License-Identifier")
            file(RELATIVE_PATH relative "${include_dir}" "${header}")
            string(REPLACE "/" "-" name "${relative}")
            file(WRITE "${_demo_notices_dir}/VulkanSDK-${name}.txt" "${notice}\n")
            math(EXPR _demo_vulkan_notice_count "${_demo_vulkan_notice_count} + 1")
        endif()
    endforeach()
endforeach()
if(_demo_vulkan_notice_count EQUAL 0)
    message(FATAL_ERROR "Demo notices require Vulkan SDK headers with embedded copyright/SPDX notices")
endif()

# vcpkg's copyright files contain aggregate notices for ports and their bundled
# dependencies. Including the installed triplet's complete set is intentional:
# no dependency DLL can silently lose its notice when the runtime closure grows.
set(_demo_vcpkg_share "")
foreach(root IN ITEMS "${VCPKG_INSTALLED_DIR}" "${_VCPKG_INSTALLED_DIR}")
    if(root AND IS_DIRECTORY "${root}/${VCPKG_TARGET_TRIPLET}/share")
        set(_demo_vcpkg_share "${root}/${VCPKG_TARGET_TRIPLET}/share")
        break()
    endif()
endforeach()
if(NOT _demo_vcpkg_share)
    message(FATAL_ERROR "Demo notices require the active vcpkg triplet's share directory")
endif()
foreach(required_port IN ITEMS assimp nativefiledialog-extended)
    _demo_notice_require("${_demo_vcpkg_share}/${required_port}/copyright")
endforeach()
file(GLOB copyrights CONFIGURE_DEPENDS "${_demo_vcpkg_share}/*/copyright")
foreach(copyright IN LISTS copyrights)
    get_filename_component(port_dir "${copyright}" DIRECTORY)
    get_filename_component(port "${port_dir}" NAME)
    _demo_notice_copy("${copyright}" "vcpkg-${port}.txt")
endforeach()

install(DIRECTORY "${_demo_notices_dir}/" DESTINATION licenses
    CONFIGURATIONS Release RelWithDebInfo COMPONENT Demo)
