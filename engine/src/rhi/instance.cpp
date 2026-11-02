#include <lykoware/rhi/instance.hpp>

#include <lykoware/log.hpp>

#include <volk.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lykoware::rhi {
namespace {

constexpr const char* validation_layer = "VK_LAYER_KHRONOS_validation";

// volk keeps one process-wide function table, so only one instance may own it.
std::atomic<bool> g_instance_alive{false};

std::string_view result_name(VkResult result) noexcept
{
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "VkResult error";
    }
}

std::string driver_failure(std::string_view call, VkResult result)
{
    std::string message{call};
    message += " failed: ";
    message += result_name(result);
    message += " (";
    message += std::to_string(static_cast<int>(result));
    message += ')';
    return message;
}

bool fail(RhiError& error, RhiFailure failure, std::string message)
{
    error.failure = failure;
    error.message = std::move(message);
    return false;
}

std::vector<std::string> available_extensions(const char* layer)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(layer, &count, nullptr) != VK_SUCCESS) {
        return {};
    }
    std::vector<VkExtensionProperties> properties(count);
    if (vkEnumerateInstanceExtensionProperties(layer, &count, properties.data()) != VK_SUCCESS) {
        return {};
    }
    std::vector<std::string> names;
    names.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        names.emplace_back(properties[index].extensionName);
    }
    return names;
}

bool layer_available(std::string_view name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& layer) {
        return name == layer.layerName;
    });
}

bool contains(const std::vector<std::string>& names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

void add_unique(std::vector<std::string>& names, std::string_view name)
{
    if (!contains(names, name)) {
        names.emplace_back(name);
    }
}

}

struct Instance::Impl {
    ~Impl()
    {
        if (messenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
            messenger = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        if (loader_open) {
            volkFinalize();
            loader_open = false;
        }
        if (owns_slot) {
            g_instance_alive.store(false);
            owns_slot = false;
        }
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL on_message(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT /*types*/,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* user)
    {
        auto* self = static_cast<Impl*>(user);
        const bool is_error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
        if (self != nullptr) {
            (is_error ? self->errors : self->warnings).fetch_add(1);
        }
        try {
            std::string line = "Vulkan validation: ";
            line += (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "(no message)";
            log(is_error ? LogLevel::error : LogLevel::warning, line);
        } catch (...) {
            // A message that cannot be formatted is still counted above.
        }
        // VK_FALSE: never abort the call that triggered the message.
        return VK_FALSE;
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    bool owns_slot = false;
    bool loader_open = false;
    bool validation = false;
    ApiVersion version{};
    std::vector<std::string> extensions;
    std::atomic<std::uint64_t> warnings{0};
    std::atomic<std::uint64_t> errors{0};
};

Instance::Instance(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Instance::~Instance() = default;

std::unique_ptr<Instance> Instance::create(const InstanceConfig& config, RhiError& error)
{
    error = {};
    auto impl = std::make_unique<Impl>();

    bool expected = false;
    if (!g_instance_alive.compare_exchange_strong(expected, true)) {
        fail(error, RhiFailure::AlreadyExists, "an rhi::Instance already exists in this process");
        return nullptr;
    }
    impl->owns_slot = true;

    if (volkInitialize() != VK_SUCCESS) {
        fail(error, RhiFailure::NoLoader, "no Vulkan loader could be opened; install a Vulkan driver");
        return nullptr;
    }
    impl->loader_open = true;

    const std::uint32_t loader_version = volkGetInstanceVersion();
    impl->version = {VK_API_VERSION_MAJOR(loader_version), VK_API_VERSION_MINOR(loader_version),
                     VK_API_VERSION_PATCH(loader_version)};
    if (loader_version < VK_MAKE_API_VERSION(0, required_api_major, required_api_minor, 0)) {
        fail(error, RhiFailure::VersionTooOld,
             "Vulkan loader reports " + std::to_string(impl->version.major) + "."
                 + std::to_string(impl->version.minor) + "; "
                 + std::to_string(required_api_major) + "." + std::to_string(required_api_minor)
                 + " is required");
        return nullptr;
    }

    std::vector<const char*> layers;
    if (config.validation != Validation::Off) {
        if (layer_available(validation_layer)) {
            layers.push_back(validation_layer);
            impl->validation = true;
        } else if (config.validation == Validation::Required) {
            fail(error, RhiFailure::MissingLayer,
                 std::string{validation_layer} + " is not installed");
            return nullptr;
        }
    }

    std::vector<std::string> available = available_extensions(nullptr);
    if (impl->validation) {
        for (std::string& name : available_extensions(validation_layer)) {
            add_unique(available, name);
        }
    }

    for (const std::string& name : config.extensions) {
        if (!contains(available, name)) {
            fail(error, RhiFailure::MissingExtension, "instance extension " + name + " is not available");
            return nullptr;
        }
        add_unique(impl->extensions, name);
    }
    if (impl->validation) {
        if (!contains(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            fail(error, RhiFailure::MissingExtension,
                 std::string{VK_EXT_DEBUG_UTILS_EXTENSION_NAME} + " is required for validation messages");
            return nullptr;
        }
        add_unique(impl->extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> extension_names;
    extension_names.reserve(impl->extensions.size());
    for (const std::string& name : impl->extensions) {
        extension_names.push_back(name.c_str());
    }

    VkDebugUtilsMessengerCreateInfoEXT messenger_info{};
    messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messenger_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger_info.pfnUserCallback = &Impl::on_message;
    messenger_info.pUserData = impl.get();

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = config.application_name.c_str();
    application.applicationVersion = 0;
    application.pEngineName = "Lykoware";
    application.engineVersion = 0;
    application.apiVersion = VK_MAKE_API_VERSION(0, required_api_major, required_api_minor, 0);

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    // Chaining the messenger also reports problems in create/destroy itself.
    create_info.pNext = impl->validation ? &messenger_info : nullptr;
    create_info.pApplicationInfo = &application;
    create_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();
    create_info.enabledExtensionCount = static_cast<std::uint32_t>(extension_names.size());
    create_info.ppEnabledExtensionNames = extension_names.data();

    const VkResult created = vkCreateInstance(&create_info, nullptr, &impl->instance);
    if (created != VK_SUCCESS) {
        impl->instance = VK_NULL_HANDLE;
        // The loader returns INCOMPATIBLE_DRIVER when it found no ICD at all,
        // or none supporting the requested API version.
        const RhiFailure failure =
            created == VK_ERROR_INCOMPATIBLE_DRIVER ? RhiFailure::NoDriver : RhiFailure::DriverError;
        std::string message = driver_failure("vkCreateInstance", created);
        if (failure == RhiFailure::NoDriver) {
            message += "; no Vulkan driver supporting " + std::to_string(required_api_major) + "."
                + std::to_string(required_api_minor) + " is installed";
        }
        fail(error, failure, std::move(message));
        return nullptr;
    }
    volkLoadInstanceOnly(impl->instance);

    if (impl->validation) {
        const VkResult messenger =
            vkCreateDebugUtilsMessengerEXT(impl->instance, &messenger_info, nullptr, &impl->messenger);
        if (messenger != VK_SUCCESS) {
            impl->messenger = VK_NULL_HANDLE;
            fail(error, RhiFailure::DriverError, driver_failure("vkCreateDebugUtilsMessengerEXT", messenger));
            return nullptr;
        }
    }

    return std::unique_ptr<Instance>(new Instance(std::move(impl)));
}

ApiVersion Instance::api_version() const
{
    return impl_->version;
}

bool Instance::validation_enabled() const
{
    return impl_->validation;
}

std::span<const std::string> Instance::enabled_extensions() const
{
    return impl_->extensions;
}

std::uint64_t Instance::validation_warning_count() const
{
    return impl_->warnings.load();
}

std::uint64_t Instance::validation_error_count() const
{
    return impl_->errors.load();
}

void* Instance::native_handle() const
{
    return static_cast<void*>(impl_->instance);
}

}
