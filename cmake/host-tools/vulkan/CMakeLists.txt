# Shim used only by the host-tool build of Slang (cmake/HostTools.cmake):
# Slang's SLANG_OVERRIDE_VULKAN_HEADERS_PATH expects <path>/vulkan/CMakeLists.txt
# defining Vulkan::Headers. This points it at the Vulkan-Headers already
# vendored in third_party/vulkan-headers instead of a second copy.
add_library(lykoware_host_vulkan_headers INTERFACE)
target_include_directories(lykoware_host_vulkan_headers INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/vulkan-headers/include")
add_library(Vulkan::Headers ALIAS lykoware_host_vulkan_headers)
