#include <lykoware/rhi/buffer.hpp>

#include "common.hpp"
#include "device_access.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <atomic>
#include <cstring>
#include <string>
#include <utility>

namespace lykoware::rhi {

using detail::DeviceAccess;
using detail::driver_failure;
using detail::fail;

namespace {
// Every Buffer alive in the process (ADR 0088).
std::atomic<std::size_t> g_live_buffers{0};
}

std::size_t live_buffer_count() noexcept
{
    return g_live_buffers.load(std::memory_order_relaxed);
}

struct Buffer::Impl {
    ~Impl()
    {
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(DeviceAccess::allocator(*device), buffer, allocation);
        }
    }

    const Device* device = nullptr;
    BufferUsage usage = BufferUsage::Vertex;
    std::size_t size = 0;
    std::uint32_t max_index = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

Buffer::Buffer(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
    g_live_buffers.fetch_add(1, std::memory_order_relaxed);
}

Buffer::~Buffer()
{
    g_live_buffers.fetch_sub(1, std::memory_order_relaxed);
}

std::unique_ptr<Buffer> Buffer::create(
    const Device& device, BufferUsage usage, std::span<const std::byte> data, RhiError& error)
{
    error = {};
    if (data.empty() || data.size() > buffer_max_bytes) {
        fail(error, RhiFailure::InvalidArgument,
             "buffer data must be 1.." + std::to_string(buffer_max_bytes) + " bytes");
        return nullptr;
    }
    if (usage == BufferUsage::Index && data.size() % sizeof(std::uint32_t) != 0) {
        fail(error, RhiFailure::InvalidArgument, "index data must be a whole number of 32-bit indices");
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->usage = usage;
    impl->size = data.size();
    if (usage == BufferUsage::Index) {
        for (std::size_t offset = 0; offset < data.size(); offset += sizeof(std::uint32_t)) {
            std::uint32_t index = 0;
            std::memcpy(&index, data.data() + offset, sizeof(index));
            impl->max_index = index > impl->max_index ? index : impl->max_index;
        }
    }

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = data.size();
    info.usage = usage == BufferUsage::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    const VkResult result = vmaCreateBuffer(DeviceAccess::allocator(device), &info, &allocation_info, &impl->buffer,
                                            &impl->allocation, nullptr);
    if (result != VK_SUCCESS) {
        impl->buffer = VK_NULL_HANDLE;
        fail(error, RhiFailure::DriverError, driver_failure("vmaCreateBuffer", result));
        return nullptr;
    }
    // Copies and flushes, so the data is visible to the GPU on every memory type.
    const VkResult copied =
        vmaCopyMemoryToAllocation(DeviceAccess::allocator(device), data.data(), impl->allocation, 0, data.size());
    if (copied != VK_SUCCESS) {
        fail(error, RhiFailure::DriverError, driver_failure("vmaCopyMemoryToAllocation", copied));
        return nullptr;
    }
    return std::unique_ptr<Buffer>(new Buffer(std::move(impl)));
}

BufferUsage Buffer::usage() const
{
    return impl_->usage;
}

std::size_t Buffer::size_bytes() const
{
    return impl_->size;
}

std::uint32_t Buffer::max_index() const
{
    return impl_->max_index;
}

void* Buffer::native_handle() const
{
    return static_cast<void*>(impl_->buffer);
}

}
