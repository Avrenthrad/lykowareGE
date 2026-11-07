# Runtime package attributions (ADR 0045). Upstream files remain unmodified.
# Embedded excerpts are generated in the build tree and reviewed again if anchors change.
include_guard(GLOBAL)

function(_lykoware_notice_excerpt source start_marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" contents)
    # CMake text reads normalize CRLF. Make that policy explicit for generated excerpts.
    string(REPLACE "\r\n" "\n" contents "${contents}")
    string(FIND "${contents}" "${start_marker}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Notice start marker changed in ${source}; review attribution before updating")
    endif()
    string(SUBSTRING "${contents}" ${start} -1 tail)
    string(FIND "${tail}" "${end_marker}" length)
    if(length LESS 1)
        message(FATAL_ERROR "Notice end marker changed in ${source}; review attribution before updating")
    endif()
    string(SUBSTRING "${tail}" 0 ${length} notice)
    file(WRITE "${output}" "${notice}")
endfunction()

function(lykoware_install_runtime_notices)
    set(destination "share/lykoware/licenses")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-notices")
    file(MAKE_DIRECTORY "${generated}")
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/RUNTIME_NOTICES.md"
        DESTINATION "${destination}" RENAME README.md COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION "${destination}/lykoware" COMPONENT RuntimeNotices)
    # Keep the existing full JSON install rule untouched, so concurrent root CMake
    # additions (including Wuffs) compose cleanly. This rule is notice-component only.
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/json/LICENSE.MIT"
        "${CMAKE_SOURCE_DIR}/third_party/json/PROVENANCE.md"
        DESTINATION "${destination}/json" COMPONENT RuntimeNotices EXCLUDE_FROM_ALL)
    foreach(dependency IN ITEMS sdl3 jolt volk vma vulkan-headers)
        install(FILES "${CMAKE_SOURCE_DIR}/third_party/${dependency}/PROVENANCE.md"
            DESTINATION "${destination}/${dependency}" COMPONENT RuntimeNotices)
    endforeach()
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/sdl3/LICENSE.txt"
        DESTINATION "${destination}/sdl3" COMPONENT RuntimeNotices)
    install(FILES
        "${CMAKE_SOURCE_DIR}/third_party/sdl3/src/hidapi/LICENSE-bsd.txt"
        "${CMAKE_SOURCE_DIR}/third_party/sdl3/src/hidapi/LICENSE-orig.txt"
        "${CMAKE_SOURCE_DIR}/third_party/sdl3/src/hidapi/AUTHORS.txt"
        DESTINATION "${destination}/sdl3/hidapi" COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/sdl3/src/video/yuv2rgb/LICENSE"
        DESTINATION "${destination}/sdl3/yuv2rgb" COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/jolt/LICENSE"
        DESTINATION "${destination}/jolt" COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/volk/LICENSE.md"
        DESTINATION "${destination}/volk" COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/vma/LICENSE.txt"
        DESTINATION "${destination}/vma" COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/LICENSE.md"
        DESTINATION "${destination}/vulkan-headers" COMPONENT RuntimeNotices)
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/LICENSES/MIT.txt"
        DESTINATION "${destination}/vulkan-headers/LICENSES" COMPONENT RuntimeNotices)
    # SDL's bundled Khronos Vulkan headers elect Apache-2.0, independently of our
    # separately vendored Vulkan-Headers' MIT option. Preserve both accurately.
    install(FILES "${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/LICENSES/Apache-2.0.txt"
        DESTINATION "${destination}/sdl3/khronos" COMPONENT RuntimeNotices)

    _lykoware_notice_excerpt("${CMAKE_SOURCE_DIR}/third_party/sdl3/src/video/stb_image.h"
        "ALTERNATIVE A - MIT License" "ALTERNATIVE B - Public Domain" "${generated}/stb_image-MIT.txt")
    _lykoware_notice_excerpt("${CMAKE_SOURCE_DIR}/third_party/sdl3/src/video/miniz.h"
        "  This is free and unencumbered software released into the public domain." "*/" "${generated}/miniz.txt")
    _lykoware_notice_excerpt("${CMAKE_SOURCE_DIR}/third_party/sdl3/src/libm/e_sqrt.c"
        " * Copyright (C) 1993 by Sun Microsystems" " */" "${generated}/libm.txt")
    install(FILES "${generated}/stb_image-MIT.txt" "${generated}/miniz.txt" "${generated}/libm.txt"
        DESTINATION "${destination}/sdl3/embedded" COMPONENT RuntimeNotices)

    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/sdl3/src/video/khronos/*.h")
    list(SORT headers)
    set(bundle "")
    foreach(header IN LISTS headers)
        _lykoware_notice_excerpt("${header}" "Copyright" "*/" "${generated}/khronos-current.txt")
        file(READ "${generated}/khronos-current.txt" notice)
        file(RELATIVE_PATH relative "${CMAKE_SOURCE_DIR}" "${header}")
        string(APPEND bundle "Source: ${relative}\n${notice}\n\n")
    endforeach()
    file(WRITE "${generated}/khronos.txt" "${bundle}")
    install(FILES "${generated}/khronos.txt"
        DESTINATION "${destination}/sdl3/khronos" COMPONENT RuntimeNotices)

    # Claude's image-decode branch installs these in the full package already.
    # Include its selected dependency in the notice-only component too, when built.
    if(TARGET lykoware_wuffs)
        install(FILES "${CMAKE_SOURCE_DIR}/third_party/wuffs/LICENSE-MIT"
            "${CMAKE_SOURCE_DIR}/third_party/wuffs/PROVENANCE.md"
            DESTINATION "${destination}/wuffs" COMPONENT RuntimeNotices EXCLUDE_FROM_ALL)
    endif()
    if(TARGET lykoware_lua)
        install(FILES "${CMAKE_SOURCE_DIR}/third_party/lua/LICENSE"
            "${CMAKE_SOURCE_DIR}/third_party/lua/PROVENANCE.md"
            DESTINATION "${destination}/lua" COMPONENT RuntimeNotices EXCLUDE_FROM_ALL)
    endif()
endfunction()
