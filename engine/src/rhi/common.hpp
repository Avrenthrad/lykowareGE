#pragma once

// Internal to engine/src/rhi. Not installed and not included by public headers.

#include <lykoware/rhi/instance.hpp>

#include <volk.h>

#include <string>
#include <string_view>

namespace lykoware::rhi::detail {

[[nodiscard]] std::string_view result_name(VkResult result) noexcept;

// "<call> failed: VK_ERROR_... (-n)"
[[nodiscard]] std::string driver_failure(std::string_view call, VkResult result);

// Fills error and returns false, for one-line early exits.
bool fail(RhiError& error, RhiFailure failure, std::string message);

// True while an rhi::Device is alive. Defined in device.cpp; the Instance
// destructor uses it to report a Device that outlived its Instance.
[[nodiscard]] bool device_alive() noexcept;

}
