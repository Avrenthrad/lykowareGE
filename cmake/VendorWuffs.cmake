# Vendored Wuffs v0.4.0-alpha.12 (PNG and JPEG decoding). See third_party/wuffs/PROVENANCE.md
# and ADR 0036.
#
# The one implementation translation unit is a Lykoware file compiled here without Lykoware's
# warning flags or clang-tidy, like VMA's. The module list is a PUBLIC definition so the
# engine's wrapper sees exactly the API that was compiled.

function(lykoware_add_wuffs)
    add_library(lykoware_wuffs STATIC "${CMAKE_SOURCE_DIR}/engine/src/image/wuffs_impl.cpp")
    target_include_directories(lykoware_wuffs SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/wuffs")
    target_compile_definitions(lykoware_wuffs PUBLIC
        WUFFS_CONFIG__MODULES
        WUFFS_CONFIG__MODULE__BASE
        WUFFS_CONFIG__MODULE__AUX__BASE
        WUFFS_CONFIG__MODULE__AUX__IMAGE
        WUFFS_CONFIG__MODULE__ADLER32
        WUFFS_CONFIG__MODULE__CRC32
        WUFFS_CONFIG__MODULE__DEFLATE
        WUFFS_CONFIG__MODULE__ZLIB
        WUFFS_CONFIG__MODULE__PNG
        WUFFS_CONFIG__MODULE__JPEG)
    target_compile_features(lykoware_wuffs PRIVATE cxx_std_20)
    set_target_properties(lykoware_wuffs PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(LYKOWARE_SANITIZERS)
        target_compile_options(lykoware_wuffs PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(lykoware_wuffs PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
