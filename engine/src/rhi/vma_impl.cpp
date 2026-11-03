// The single compiled copy of Vulkan Memory Allocator (third_party/vma).
// Built as the static target lykoware_vma without Lykoware's warning flags,
// because the code below the include is upstream code.
//
// Vulkan functions come from volk at run time. The lykoware_vma target defines
// VMA_STATIC_VULKAN_FUNCTIONS=0 and VMA_DYNAMIC_VULKAN_FUNCTIONS=1; VMA fetches
// what it needs through the vkGetInstanceProcAddr / vkGetDeviceProcAddr passed
// in VmaVulkanFunctions (see Device::create).

#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
