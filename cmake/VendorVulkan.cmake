# Vendored Vulkan-Headers and volk. See third_party/vulkan-headers/PROVENANCE.md
# and third_party/volk/PROVENANCE.md.
#
# Vulkan is never linked at build time. volk opens the system Vulkan loader
# (vulkan-1.dll / libvulkan.so.1) at run time, so a machine without a Vulkan
# driver still builds and runs everything that does not render.

function(lykoware_add_vulkan)
    add_library(lykoware_vulkan_headers INTERFACE)
    target_include_directories(lykoware_vulkan_headers SYSTEM INTERFACE
        "${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/include")
    # Every Vulkan call goes through volk's function pointers.
    target_compile_definitions(lykoware_vulkan_headers INTERFACE VK_NO_PROTOTYPES)

    # Upstream volk.c compiled as-is. Lykoware's warning flags are not applied
    # to vendored code; clang-tidy only runs on targets that opt in.
    add_library(lykoware_volk STATIC "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c")
    target_include_directories(lykoware_volk SYSTEM PUBLIC
        "${CMAKE_SOURCE_DIR}/third_party/volk")
    target_link_libraries(lykoware_volk PUBLIC lykoware_vulkan_headers ${CMAKE_DL_LIBS})
    set_target_properties(lykoware_volk PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)

    # Vulkan Memory Allocator: header-only upstream; the one implementation
    # translation unit is a Lykoware file compiled here without Lykoware's
    # warning flags. See third_party/vma/PROVENANCE.md.
    add_library(lykoware_vma STATIC "${CMAKE_SOURCE_DIR}/engine/src/rhi/vma_impl.cpp")
    target_include_directories(lykoware_vma SYSTEM PUBLIC
        "${CMAKE_SOURCE_DIR}/third_party/vma/include")
    target_compile_definitions(lykoware_vma PUBLIC
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
    target_link_libraries(lykoware_vma PUBLIC lykoware_volk)
    target_compile_features(lykoware_vma PRIVATE cxx_std_20)
    set_target_properties(lykoware_vma PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(LYKOWARE_SANITIZERS)
        foreach(target lykoware_volk lykoware_vma)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
endfunction()
