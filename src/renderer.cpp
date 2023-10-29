#include "nkgt/renderer.hpp"

#include <fstream>
#include <print>

#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifndef NDEBUG
static void glfw_error_callback(int code, const char* message) {
    std::print("GLFW error {}: {}\n", code, message);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_error_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*)
{
    std::print("{}\n", callback_data->pMessage);
    return VK_FALSE;
}

constexpr VkDebugUtilsMessengerCreateInfoEXT dbg_create_info{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = vk_error_callback,
};

static inline VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT* dbg_messenger)
{
    auto f = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance,
        "vkCreateDebugUtilsMessengerEXT"
    );

    if (f != nullptr) {
        return f(instance, &dbg_create_info, nullptr, dbg_messenger);
    }
    else {
        std::print("Failed to fetch vkCreateDebugUtilsMessengerEXT.\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

}

static inline void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT dbg_messenger)
{
    auto f = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance,
        "vkDestroyDebugUtilsMessengerEXT"
    );

    if (f != nullptr) f(instance, dbg_messenger, nullptr);
}
#endif

template<typename T, std::size_t N>
static consteval std::uint32_t array_size(const T(&)[N]) {
    return N;
}

static auto is_complete(Motorino::queue_indices indices) -> bool {
    return indices.graphics.has_value() &&
           indices.present.has_value();
}

static auto find_queue_indices(
    VkPhysicalDevice device,
    VkSurfaceKHR surface
) -> Motorino::queue_indices {
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);

    VkQueueFamilyProperties *properties = new VkQueueFamilyProperties[family_count];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, properties);

    Motorino::queue_indices indices;

    for (std::uint32_t i = 0; i < family_count; ++i) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

        if (present_support) {
            indices.present = i;
        }

        if (is_complete(indices)) break;
    }

    delete[] properties;
    return indices;
}

Motorino::Engine::Engine(
    std::uint32_t width,
    std::uint32_t height,
    const char* name
) : _width{ width },
    _height{ height },
    _name{ name },
    _instance{ VK_NULL_HANDLE },
    _surface{ VK_NULL_HANDLE },
    _physical_device{ VK_NULL_HANDLE },
    _device{ VK_NULL_HANDLE },
    _graphics_queue{ VK_NULL_HANDLE },
    _present_queue{ VK_NULL_HANDLE },
    _swapchain{ VK_NULL_HANDLE },
    _pipeline_layout{ VK_NULL_HANDLE }
#ifndef NDEBUG
    , _dbg_messenger{ VK_NULL_HANDLE }
#endif
{
#ifndef NDEBUG
    glfwSetErrorCallback(glfw_error_callback);
#endif
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    _handle = glfwCreateWindow(_width, _height, _name, nullptr, nullptr);

    std::print("GLFW window created.\n");
}

auto Motorino::Engine::init_vulkan() -> std::expected<void, Error> {
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = _name,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Motorino",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    const char* extensions[] = {
        "VK_KHR_surface",
        "VK_KHR_win32_surface",
#ifndef NDEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
    };

    VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = array_size(extensions),
        .ppEnabledExtensionNames = extensions,
    };

#ifndef NDEBUG
    const char* validation_layers[] = { "VK_LAYER_KHRONOS_validation" };

    instance_info.pNext = &dbg_create_info;
    instance_info.enabledLayerCount = array_size(validation_layers);
    instance_info.ppEnabledLayerNames = validation_layers;
#endif

    if (vkCreateInstance(&instance_info, nullptr, &_instance) != VK_SUCCESS) {
        std::print("Error while creating Vulkan instance\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Vulkan instance created.\n");

#ifndef NDEBUG
    if (CreateDebugUtilsMessengerEXT(_instance, &_dbg_messenger) != VK_SUCCESS) {
        std::print("Failed to initialize Vulkan debug messenger.\n");
        return std::unexpected(Error::vulkan);
    }
#endif

    VkWin32SurfaceCreateInfoKHR surface_info{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = glfwGetWin32Window(_handle),
    };

    if (vkCreateWin32SurfaceKHR(_instance, &surface_info, nullptr, &_surface) != VK_SUCCESS) {
        std::print("Failed to create Vulkan surface.\n");
        return std::unexpected(Error::vulkan);
    }

    std::uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(_instance, &device_count, nullptr);

    if (device_count == 0) {
        std::print("No Vulkan physical devices available.\n");
        return std::unexpected(Error::vulkan);
    }

    std::vector<VkPhysicalDevice> available_devices;
    available_devices.resize(device_count);

    if (vkEnumeratePhysicalDevices(_instance, &device_count, available_devices.data()) != VK_SUCCESS) {
        std::print("Failed to initialize Vulkan physical device.\n");
        return std::unexpected(Error::vulkan);
    }

    _physical_device = available_devices[0];

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(_physical_device, &properties);

    std::print("Selected device: {}.\n", properties.deviceName);

    _indices = find_queue_indices(_physical_device, _surface);

    if (!is_complete(_indices)) {
        std::print("Failed to query required queue families.\n");
        return std::unexpected(Error::vulkan);
    }

    float priorities = 1.f;
    VkDeviceQueueCreateInfo queue_infos[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = _indices.graphics.value(),
            .queueCount = 1,
            .pQueuePriorities = &priorities,
        },
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = _indices.present.value(),
            .queueCount = 1,
            .pQueuePriorities = &priorities,
        }
    };

    std::uint32_t queue_count = 1;

    if (_indices.graphics != _indices.present) {
        queue_count = 2;
    }

    std::print(
        "Surfaces count: {} (graphics: {}, present: {})\n",
        queue_count,
        *_indices.graphics,
        *_indices.present
    );

    VkPhysicalDeviceFeatures device_features{};
    
    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = array_size(device_extensions),
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &device_features,
    };

    if (vkCreateDevice(_physical_device, &device_info, nullptr, &_device) != VK_SUCCESS) {
        std::print("Failed to create Vulkan logical device.\n");
        return std::unexpected(Error::vulkan);
    }

    vkGetDeviceQueue(_device, _indices.graphics.value(), 0, &_graphics_queue);
    vkGetDeviceQueue(_device, _indices.present.value(), 0, &_present_queue);

    std::print("Created Vulkan logical device.\n");

    VkSurfaceCapabilitiesKHR surface_capabilities;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        _physical_device,
        _surface,
        &surface_capabilities
    );

    bool should_clamp = surface_capabilities.maxImageCount > 0 &&
        surface_capabilities.minImageCount + 1 > surface_capabilities.maxImageCount;

    std::uint32_t image_count = should_clamp ? surface_capabilities.maxImageCount :
                                               surface_capabilities.minImageCount + 1;

    std::uint32_t index_values[] = { *_indices.graphics, *_indices.present };

    VkSwapchainCreateInfoKHR swapchain_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = _surface,
        .minImageCount = image_count,
        .imageFormat = VK_FORMAT_R8G8B8A8_SRGB,
        .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = surface_capabilities.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    if (index_values[0] != index_values[1]) {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_info.queueFamilyIndexCount = 2;
        swapchain_info.pQueueFamilyIndices = index_values;
    }

    if (vkCreateSwapchainKHR(_device, &swapchain_info, nullptr, &_swapchain) != VK_SUCCESS) {
        std::print("Failed to create Vulkan swapchain.\n");
        return std::unexpected(Error::vulkan);
    }

    vkGetSwapchainImagesKHR(_device, _swapchain, &image_count, nullptr);
    
    _images.resize(image_count);
    vkGetSwapchainImagesKHR(_device, _swapchain, &image_count, _images.data());

    std::print(
        "Created Vulkan swapchain ({},{},{}).\n",
        surface_capabilities.currentExtent.width,
        surface_capabilities.currentExtent.height,
        image_count
    );

    _image_views.resize(_images.size());

    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY },
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };

    for (std::size_t i = 0; i < _images.size(); ++i) {
        view_info.image = _images[i];

        if (vkCreateImageView(_device, &view_info, nullptr, &_image_views[i]) != VK_SUCCESS) {
            std::print("Failed to obtain Vulkan image view.\n");
            return std::unexpected(Error::vulkan);
        }
    }

    constexpr VkAttachmentDescription color_attachment{
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    constexpr VkAttachmentReference color_attachment_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref
    };

    constexpr VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    };

    VkRenderPassCreateInfo render_pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };

    if (vkCreateRenderPass(_device, &render_pass_info, nullptr, &_render_pass) != VK_SUCCESS) {
        std::print("Failed to create Vulkan render pass.\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Created Vulkan render pass.\n");

    _framebuffers.resize(_images.size());

    for (std::size_t i = 0; i < _image_views.size(); ++i) {
        VkFramebufferCreateInfo framebuffer_info{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = _render_pass,
            .attachmentCount = 1,
            .pAttachments = &_image_views[i],
            .width = _width,
            .height = _height,
            .layers = 1
        };

        if (vkCreateFramebuffer(_device, &framebuffer_info, nullptr, &_framebuffers[i]) != VK_SUCCESS) {
            std::print("Failed to create Vulkan framebuffer.\n");
            return std::unexpected(Error::vulkan);
        }
    }

    std::print("Created {} framebuffers.\n", _framebuffers.size());

    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = _indices.graphics.value(),
    };

    if (vkCreateCommandPool(_device, &pool_info, nullptr, &_command_pool) != VK_SUCCESS) {
        std::print("Failed to create command pool.\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Created command pool.\n");

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = _command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(_device, &alloc_info, &_command_buffer) != VK_SUCCESS) {
        std::print("Failed to allocate command buffer.\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Allocated command buffers.\n");

    constexpr VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    constexpr VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    if (vkCreateSemaphore(_device, &semaphore_info, nullptr, &_image_available_semaphore) != VK_SUCCESS) {
        std::print("Failed to create image_available semaphore.\n");
        return std::unexpected(Error::vulkan);
    }

    if (vkCreateSemaphore(_device, &semaphore_info, nullptr, &_render_finished_semaphore) != VK_SUCCESS) {
        std::print("Failed to create render_finished semaphore.\n");
        return std::unexpected(Error::vulkan);
    }

    if (vkCreateFence(_device, &fence_info, nullptr, &_inflight_fence) != VK_SUCCESS) {
        std::print("Failed to create inflight fence.\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Created synchronization primitives.\n");

    return {};
}

Motorino::Engine::~Engine() {
    vkDestroySemaphore(_device, _image_available_semaphore, nullptr);
    vkDestroySemaphore(_device, _render_finished_semaphore, nullptr);
    vkDestroyFence(_device, _inflight_fence, nullptr);
    vkDestroyCommandPool(_device, _command_pool, nullptr);

    for (auto buffer : _framebuffers) {
        vkDestroyFramebuffer(_device, buffer, nullptr);
    }

    vkDestroyPipeline(_device, _pipeline, nullptr);
    vkDestroyPipelineLayout(_device, _pipeline_layout, nullptr);
    vkDestroyRenderPass(_device, _render_pass, nullptr);

    for (auto view : _image_views) {
        vkDestroyImageView(_device, view, nullptr);
    }

    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    vkDestroyDevice(_device, nullptr);
#ifndef NDEBUG
    DestroyDebugUtilsMessengerEXT(_instance, _dbg_messenger);
#endif
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vkDestroyInstance(_instance, nullptr);
    glfwDestroyWindow(_handle);
    glfwTerminate();
}

auto Motorino::Engine::create_pipeline(
    std::span<ShaderInfo> shaders
) -> std::expected<void, Error> {
    if (shaders.empty()) {
        std::print("No shaders specified. Skipping.\n");
        return std::unexpected(Error::shader_compilation);
    }

    std::size_t max_file_size = 0;
    for (const auto& [_, path] : shaders) {
        if (!std::filesystem::exists(path)) {
            std::print("Shader file not found. Skipping. Path: {}\n", path.string());
            continue;
        }

        std::size_t new_size = std::filesystem::file_size(path);
        if (new_size > max_file_size) max_file_size = new_size;
    }

    std::vector<char> buffer(max_file_size);
    std::vector<VkShaderModule> shader_modules(shaders.size());
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    shader_stages.reserve(shaders.size());

    for(std::size_t i = 0; i < shaders.size(); ++i) {
        std::ifstream shader(shaders[i].path, std::ios::binary);

        if (!shader) continue;

        std::size_t code_size = std::filesystem::file_size(shaders[i].path);
        shader.read(buffer.data(), code_size);
        std::print("Read {}B from {}.\n", code_size, shaders[i].path.string());

        VkShaderModuleCreateInfo module_info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code_size,
            .pCode = reinterpret_cast<const std::uint32_t*>(buffer.data())
        };

        if (vkCreateShaderModule(_device, &module_info, nullptr, &shader_modules[i]) != VK_SUCCESS) {
            std::print("Failed to create shader module for shader: {}\n", shaders[i].path.string());
            return std::unexpected(Error::shader_compilation);
        }

        shader_stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = static_cast<VkShaderStageFlagBits>(shaders[i].type),
            .module = shader_modules[i],
            .pName = "main"
        });
    }

    constexpr VkPipelineVertexInputStateCreateInfo vertex_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };

    constexpr VkPipelineInputAssemblyStateCreateInfo assembly_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    constexpr VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = array_size(dynamic_states),
        .pDynamicStates = dynamic_states
    };

    constexpr VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    constexpr VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    constexpr VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    constexpr VkPipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    constexpr VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };

    if (vkCreatePipelineLayout(_device, &layout_info, nullptr, &_pipeline_layout) != VK_SUCCESS) {
        std::print("Failed to create Vulkan pipeline layout.\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Created Vulkan pipeline layout.\n");

    VkGraphicsPipelineCreateInfo pipeline_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_info,
        .pInputAssemblyState = &assembly_info,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = &dynamic_state,
        .layout = _pipeline_layout,
        .renderPass = _render_pass,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &_pipeline) != VK_SUCCESS) {
        std::print("Failed to create Vulkan pipeline.\n");
        return std::unexpected(Error::vulkan);
    }

    std::print("Created Vulkan pipeline.\n");

    for (auto mod : shader_modules) {
        vkDestroyShaderModule(_device, mod, nullptr);
    }


    return {};
}

auto Motorino::Engine::run() -> void {
    while (!glfwWindowShouldClose(_handle)) {
        glfwPollEvents();
        draw_frame();
    }

    vkDeviceWaitIdle(_device);
}

auto Motorino::Engine::record_command_buffer(
    std::uint32_t index
) -> void {
    constexpr VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    if (vkBeginCommandBuffer(_command_buffer, &begin_info) != VK_SUCCESS) {
        std::print("Failed to begin recording command buffer.\n");
        return;
    }

    VkClearValue clear_value = { {{0.0f, 0.0f, 0.0f, 1.0f}} };

    VkRenderPassBeginInfo pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = _render_pass,
        .framebuffer = _framebuffers[index],
        .renderArea = {.offset = {0,0}, .extent = {_width, _height}},
        .clearValueCount = 1,
        .pClearValues = &clear_value
    };

    vkCmdBeginRenderPass(_command_buffer, &pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(_width),
        .height = static_cast<float>(_height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(_command_buffer, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {_width, _height}
    };
    vkCmdSetScissor(_command_buffer, 0, 1, &scissor);

    vkCmdDraw(_command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(_command_buffer);

    if (vkEndCommandBuffer(_command_buffer) != VK_SUCCESS) {
        std::print("Failed to finish recording command buffer.\n");
        return;
    }
}

auto Motorino::Engine::draw_frame() -> void {
    vkWaitForFences(_device, 1, &_inflight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(_device, 1, &_inflight_fence);
    
    std::uint32_t image_index;
    vkAcquireNextImageKHR(
        _device,
        _swapchain,
        UINT64_MAX,
        _image_available_semaphore,
        VK_NULL_HANDLE,
        &image_index
    );

    vkResetCommandBuffer(_command_buffer, 0);
    record_command_buffer(image_index);

    VkSemaphore wait_semaphores[] = { _image_available_semaphore };
    VkSemaphore signal_semaphores[] = { _render_finished_semaphore };
    constexpr VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = array_size(wait_semaphores),
        .pWaitSemaphores = wait_semaphores,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &_command_buffer,
        .signalSemaphoreCount = array_size(signal_semaphores),
        .pSignalSemaphores = signal_semaphores
    };

    if (vkQueueSubmit(_graphics_queue, 1, &submit_info, _inflight_fence) != VK_SUCCESS) {
        std::print("Failed to submit draw command buffer.\n");
    }

    VkSwapchainKHR swap_chains[] = {_swapchain};

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = array_size(signal_semaphores),
        .pWaitSemaphores = signal_semaphores,
        .swapchainCount = array_size(swap_chains),
        .pSwapchains = swap_chains,
        .pImageIndices = &image_index
    };

    vkQueuePresentKHR(_present_queue, &present_info);
}
