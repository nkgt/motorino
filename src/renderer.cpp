#include "nkgt/renderer.hpp"
#include "nkgt/logger.hpp"

#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifndef NDEBUG
static void glfw_error_callback(int code, const char* message) {
    Motorino::Logger::error("GLFW error {}: {}\n", code, message);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_error_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        Motorino::Logger::error("{}\n", callback_data->pMessage);
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        Motorino::Logger::warn("{}\n", callback_data->pMessage);
    }
    else {
        Motorino::Logger::info("{}\n", callback_data->pMessage);
    }

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
        Motorino::Logger::error("Failed to fetch vkCreateDebugUtilsMessengerEXT.\n");
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

static auto is_complete(Motorino::queue_indices indices) -> bool {
    return indices.graphics.has_value() &&
           indices.present.has_value() &&
           indices.transfer.has_value();
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
        bool is_graphics = properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
        bool is_transfer = properties[i].queueFlags & VK_QUEUE_TRANSFER_BIT;

        if (is_graphics) {
            indices.graphics = i;
        }
        else if (is_transfer) {
            indices.transfer = i;
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

static void resize_callback(GLFWwindow* window, int width, int height) {
    Motorino::Engine* engine = reinterpret_cast<Motorino::Engine*>(glfwGetWindowUserPointer(window));
    engine->set_extent(width, height);
    engine->recreate_swapchain();
}

static inline auto find_memory_type(
    VkPhysicalDevice physical_device,
    std::uint32_t filter,
    VkMemoryPropertyFlags properties,
    std::uint32_t& memory_index
) -> bool {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (std::uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
        bool correct_type = filter & (1 << i);
        bool has_property = (mem_properties.memoryTypes[i].propertyFlags & properties) == properties;

        if (correct_type && has_property)
        {
            memory_index = i;
            return true;
        }
    }

    return false;
}

static inline auto create_command_buffer(
    VkDevice device,
    VkCommandPool pool,
    VkCommandBuffer buffers[Motorino::max_frames_in_flight]
) -> bool {
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = Motorino::max_frames_in_flight,
    };

    if (vkAllocateCommandBuffers(device, &alloc_info, buffers) != VK_SUCCESS) {
        Motorino::Logger::error("Failed to allocate command buffer.\n");
        return false;
    }

    Motorino::Logger::info("Allocated command buffers.\n");
    return true;
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
    _transfer_queue{ VK_NULL_HANDLE },
    _swapchain{ VK_NULL_HANDLE },
    _render_pass{ VK_NULL_HANDLE },
    _graphics_command_pool{ VK_NULL_HANDLE },
    _transfer_command_pool{ VK_NULL_HANDLE },
    _pipeline_layout{ VK_NULL_HANDLE },
    _graphics_command_buffers{},
    _pipeline{ VK_NULL_HANDLE },
    _image_available_semaphores{},
    _render_finished_semaphores{},
    _inflight_fences{},
    _index_count{ 0 },
    _vertex_count{ 0 },
    _vertex_buffer{ VK_NULL_HANDLE },
    _vertex_buffer_memory{ VK_NULL_HANDLE }
#ifndef NDEBUG
    , _dbg_messenger{ VK_NULL_HANDLE }
#endif
{
#ifndef NDEBUG
    glfwSetErrorCallback(glfw_error_callback);
#endif
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    _handle = glfwCreateWindow(_width, _height, _name, nullptr, nullptr);
    glfwSetWindowUserPointer(_handle, this);
    glfwSetFramebufferSizeCallback(_handle, resize_callback);

    Logger::info("GLFW window created.\n");
}

auto Motorino::Engine::init_vulkan() -> bool {
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
        .enabledExtensionCount = sizeof(extensions) / sizeof(const char*),
        .ppEnabledExtensionNames = extensions,
    };

#ifndef NDEBUG
    const char* validation_layers[] = { "VK_LAYER_KHRONOS_validation" };

    instance_info.pNext = &dbg_create_info;
    instance_info.enabledLayerCount = 1;
    instance_info.ppEnabledLayerNames = validation_layers;
#endif

    if (vkCreateInstance(&instance_info, nullptr, &_instance) != VK_SUCCESS) {
        Logger::error("Error while creating Vulkan instance\n");
        return false;
    }

    Logger::info("Vulkan instance created.\n");

#ifndef NDEBUG
    if (CreateDebugUtilsMessengerEXT(_instance, &_dbg_messenger) != VK_SUCCESS) {
        Logger::error("Failed to initialize Vulkan debug messenger.\n");
        return false;
    }
#endif

    VkWin32SurfaceCreateInfoKHR surface_info{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = glfwGetWin32Window(_handle),
    };

    if (vkCreateWin32SurfaceKHR(_instance, &surface_info, nullptr, &_surface) != VK_SUCCESS) {
        Logger::error("Failed to create Vulkan surface.\n");
        return false;
    }

    std::uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(_instance, &device_count, nullptr);

    if (device_count == 0) {
        Logger::error("No Vulkan physical devices available.\n");
        return false;
    }

    std::vector<VkPhysicalDevice> available_devices;
    available_devices.resize(device_count);

    if (vkEnumeratePhysicalDevices(_instance, &device_count, available_devices.data()) != VK_SUCCESS) {
        Logger::error("Failed to initialize Vulkan physical device.\n");
        return false;
    }

    _physical_device = available_devices[0];

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(_physical_device, &properties);

    Logger::info("Selected device: {}.\n", properties.deviceName);

    _indices = find_queue_indices(_physical_device, _surface);

    if (!is_complete(_indices)) {
        Logger::error("Failed to query required queue families.\n");
        return false;
    }

    float priorities = 1.f;
    VkDeviceQueueCreateInfo queue_infos[] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = _indices.graphics.value(),
            .queueCount = 1,
            .pQueuePriorities = &priorities,
        },
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = _indices.transfer.value(),
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

    std::uint32_t queue_count = 2;

    if (_indices.graphics != _indices.present) {
        queue_count = 3;
    }

    Logger::info(
        "Queue count: {} (graphics: {}, present: {}, transfer: {})\n",
        queue_count,
        *_indices.graphics,
        *_indices.present,
        *_indices.transfer
    );

    VkPhysicalDeviceFeatures device_features{};
    
    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &device_features,
    };

    if (vkCreateDevice(_physical_device, &device_info, nullptr, &_device) != VK_SUCCESS) {
        Logger::error("Failed to create Vulkan logical device.\n");
        return false;
    }

    vkGetDeviceQueue(_device, _indices.graphics.value(), 0, &_graphics_queue);
    vkGetDeviceQueue(_device, _indices.transfer.value(), 0, &_transfer_queue);
    vkGetDeviceQueue(_device, _indices.present.value(), 0, &_present_queue);

    Logger::info("Created Vulkan logical device.\n");

    if (!create_swapchain()) return false;
    if (!create_image_views()) return false;

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
        Logger::error("Failed to create Vulkan render pass.\n");
        return false;
    }

    Logger::info("Created Vulkan render pass.\n");

    if (!create_framebuffers()) return false;

    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = _indices.graphics.value(),
    };

    if (vkCreateCommandPool(_device, &pool_info, nullptr, &_graphics_command_pool) != VK_SUCCESS) {
        Logger::error("Failed to create command pool.\n");
        return false;
    }

    Logger::info("Created graphics command pool.\n");
    
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = _indices.transfer.value();

    if (vkCreateCommandPool(_device, &pool_info, nullptr, &_transfer_command_pool) != VK_SUCCESS) {
        Logger::error("Failed to create command pool.\n");
        return false;
    }

    Logger::info("Created transfer command pool.\n");

    if (!create_command_buffer(_device, _graphics_command_pool, _graphics_command_buffers)) {
        return false;
    }

    constexpr VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    constexpr VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (std::uint32_t i = 0; i < max_frames_in_flight; ++i) {
        if (vkCreateSemaphore(_device, &semaphore_info, nullptr, &_image_available_semaphores[i]) != VK_SUCCESS) {
            Logger::error("Failed to create image_available semaphore.\n");
            return false;
        }

        if (vkCreateSemaphore(_device, &semaphore_info, nullptr, &_render_finished_semaphores[i]) != VK_SUCCESS) {
            Logger::error("Failed to create render_finished semaphore.\n");
            return false;
        }

        if (vkCreateFence(_device, &fence_info, nullptr, &_inflight_fences[i]) != VK_SUCCESS) {
            Logger::error("Failed to create inflight fence.\n");
            return false;
        }
    }

    Logger::info("Created synchronization primitives.\n");

    return true;
}

Motorino::Engine::~Engine() {
    cleanup_swapchain();

    vkDestroyBuffer(_device, _vertex_buffer, nullptr);
    vkFreeMemory(_device, _vertex_buffer_memory, nullptr);

    for (std::uint32_t i = 0; i < max_frames_in_flight; ++i) {
        vkDestroySemaphore(_device, _image_available_semaphores[i], nullptr);
        vkDestroySemaphore(_device, _render_finished_semaphores[i], nullptr);
        vkDestroyFence(_device, _inflight_fences[i], nullptr);
    }

    vkDestroyCommandPool(_device, _graphics_command_pool, nullptr);
    vkDestroyCommandPool(_device, _transfer_command_pool, nullptr);

    vkDestroyPipeline(_device, _pipeline, nullptr);
    vkDestroyPipelineLayout(_device, _pipeline_layout, nullptr);
    vkDestroyRenderPass(_device, _render_pass, nullptr);

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
) -> bool {
    if (shaders.empty()) {
        Logger::error("No shaders specified. Skipping.\n");
        return false;
    }

    char buffer[2048];
    std::vector<VkShaderModule> shader_modules(shaders.size());
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    shader_stages.reserve(shaders.size());

    for(std::size_t i = 0; i < shaders.size(); ++i) {
        HANDLE file = CreateFile(
            shaders[i].path,
            GENERIC_READ,
            0,
            0,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0
        );

        if (file == INVALID_HANDLE_VALUE) {
            Logger::error("Failed to open shader file. Skipping. Path: {}", shaders[i].path);
            continue;
        }

        unsigned long code_size = 0;
        if (ReadFile(file, buffer, 2048, &code_size, 0) == 0) {
            Logger::error("Failed to read shader file. Skipping. Path: {}", shaders[i].path);
            continue;
        }

        Logger::info("Read {}B from {}.\n", code_size, shaders[i].path);

        VkShaderModuleCreateInfo module_info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code_size,
            .pCode = reinterpret_cast<const std::uint32_t*>(buffer)
        };

        if (vkCreateShaderModule(_device, &module_info, nullptr, &shader_modules[i]) != VK_SUCCESS) {
            Logger::error("Failed to create shader module for shader: {}\n", shaders[i].path);
            return false;
        }

        shader_stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = static_cast<VkShaderStageFlagBits>(shaders[i].type),
            .module = shader_modules[i],
            .pName = "main"
        });

        CloseHandle(file);
    }

    constexpr VkVertexInputBindingDescription binding_desc{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    constexpr VkVertexInputAttributeDescription attribute_desc[] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, pos) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) }
    };

    VkPipelineVertexInputStateCreateInfo vertex_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_desc,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attribute_desc
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
        .dynamicStateCount = 2,
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
        Logger::error("Failed to create Vulkan pipeline layout.\n");
        return false;
    }

    Logger::info("Created Vulkan pipeline layout.\n");

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
        Logger::error("Failed to create Vulkan pipeline.\n");
        return false;
    }

    Logger::info("Created Vulkan pipeline.\n");

    for (auto mod : shader_modules) {
        vkDestroyShaderModule(_device, mod, nullptr);
    }


    return true;
}

auto Motorino::Engine::submit_vertex_data(
    const Geometry* geometry
) -> bool {
    _index_count = geometry->index_count;
    _vertex_count = geometry->vertex_count;

    const std::uint64_t size = geometry->vertex_count * sizeof(Vertex) +
                               geometry->index_count  * sizeof(std::uint16_t);

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;

    bool result = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer,
        staging_buffer_memory
    );

    if (!result) return false;

    void* data;
    vkMapMemory(_device, staging_buffer_memory, 0, size, 0, &data);
    std::memcpy(data, geometry->data, size);
    vkUnmapMemory(_device, staging_buffer_memory);

    result = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        _vertex_buffer,
        _vertex_buffer_memory
    );

    if (!result) return false;

    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = _transfer_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmd_buffer;
    vkAllocateCommandBuffers(_device, &alloc_info, &cmd_buffer);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(cmd_buffer, &begin_info);

    VkBufferCopy copy_region{
        .size = size
    };

    vkCmdCopyBuffer(cmd_buffer, staging_buffer, _vertex_buffer, 1, &copy_region);
    vkEndCommandBuffer(cmd_buffer);

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buffer
    };

    vkQueueSubmit(_transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(_transfer_queue);

    vkFreeCommandBuffers(_device, _transfer_command_pool, 1, &cmd_buffer);
    vkDestroyBuffer(_device, staging_buffer, nullptr);
    vkFreeMemory(_device, staging_buffer_memory, nullptr);

    return true;
}

auto Motorino::Engine::run() -> void {
    while (!glfwWindowShouldClose(_handle)) {
        glfwPollEvents();
        draw_frame();
    }

    vkDeviceWaitIdle(_device);
}

auto Motorino::Engine::set_extent(
    std::uint32_t width,
    std::uint32_t height
) -> void {
    _width = width;
    _height = height;
}

auto Motorino::Engine::create_swapchain() -> bool {
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

    std::uint32_t index_values[] = {
        *_indices.graphics,
        *_indices.transfer,
        *_indices.present
    };

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

    if (_indices.graphics != _indices.present) {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_info.queueFamilyIndexCount = 3;
        swapchain_info.pQueueFamilyIndices = index_values;
    }
    else {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain_info.queueFamilyIndexCount = 2;
        swapchain_info.pQueueFamilyIndices = index_values;
    }

    if (vkCreateSwapchainKHR(_device, &swapchain_info, nullptr, &_swapchain) != VK_SUCCESS) {
        Logger::error("Failed to create Vulkan swapchain.\n");
        return false;
    }

    vkGetSwapchainImagesKHR(_device, _swapchain, &image_count, nullptr);
    
    _images.resize(image_count);
    vkGetSwapchainImagesKHR(_device, _swapchain, &image_count, _images.data());

    Logger::info(
        "Created Vulkan swapchain ({},{},{}).\n",
        surface_capabilities.currentExtent.width,
        surface_capabilities.currentExtent.height,
        image_count
    );

    return true;
}

auto Motorino::Engine::create_image_views() -> bool {
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
            Logger::error("Failed to obtain Vulkan image view.\n");
            return false;
        }
    }

    Logger::info("Created Vulkan image views.\n");
    return true;
}

auto Motorino::Engine::create_framebuffers() -> bool {
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
            Logger::error("Failed to create Vulkan framebuffer.\n");
            return false;
        }
    }

    Logger::info("Created {} framebuffers.\n", _framebuffers.size());
    return true;
}

auto Motorino::Engine::cleanup_swapchain() -> void {
    for (auto buffer : _framebuffers) {
        vkDestroyFramebuffer(_device, buffer, nullptr);
    }

    for (auto view : _image_views) {
        vkDestroyImageView(_device, view, nullptr);
    }

    vkDestroySwapchainKHR(_device, _swapchain, nullptr);
}

auto Motorino::Engine::recreate_swapchain() -> bool {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(_handle, &width, &height);

    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(_handle, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(_device);
 
    cleanup_swapchain();

    create_swapchain();
    create_image_views();
    create_framebuffers();

    return true;
}

auto Motorino::Engine::record_command_buffer(
    std::uint32_t current_frame,
    std::uint32_t image_index
) -> void {
    constexpr VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    if (vkBeginCommandBuffer(_graphics_command_buffers[current_frame], &begin_info) != VK_SUCCESS) {
        Logger::error("Failed to begin recording command buffer.\n");
        return;
    }

    VkClearValue clear_value = { {{0.0f, 0.0f, 0.0f, 1.0f}} };

    VkRenderPassBeginInfo pass_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = _render_pass,
        .framebuffer = _framebuffers[image_index],
        .renderArea = {.offset = {0,0}, .extent = {_width, _height}},
        .clearValueCount = 1,
        .pClearValues = &clear_value
    };

    vkCmdBeginRenderPass(
        _graphics_command_buffers[current_frame],
        &pass_info,
        VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        _graphics_command_buffers[current_frame],
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        _pipeline
    );

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(
        _graphics_command_buffers[current_frame],
        0,
        1,
        &_vertex_buffer,
        offsets
    );

    vkCmdBindIndexBuffer(
        _graphics_command_buffers[current_frame],
        _vertex_buffer,
        _vertex_count * sizeof(Vertex),
        VK_INDEX_TYPE_UINT16
    );

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(_width),
        .height = static_cast<float>(_height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(_graphics_command_buffers[current_frame], 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {_width, _height}
    };
    vkCmdSetScissor(_graphics_command_buffers[current_frame], 0, 1, &scissor);

    vkCmdDrawIndexed(_graphics_command_buffers[current_frame], _index_count, 1, 0, 0, 0);
    vkCmdEndRenderPass(_graphics_command_buffers[current_frame]);

    if (vkEndCommandBuffer(_graphics_command_buffers[current_frame]) != VK_SUCCESS) {
        Logger::error("Failed to finish recording command buffer.\n");
        return;
    }
}

auto Motorino::Engine::draw_frame() -> void {
    static std::uint32_t current_frame = 0;

    vkWaitForFences(
        _device,
        1,
        &_inflight_fences[current_frame],
        VK_TRUE,
        UINT64_MAX
    );

    std::uint32_t image_index;
    vkAcquireNextImageKHR(
        _device,
        _swapchain,
        UINT64_MAX,
        _image_available_semaphores[current_frame],
        VK_NULL_HANDLE,
        &image_index
    );

    vkResetFences(_device, 1, &_inflight_fences[current_frame]);
    vkResetCommandBuffer(_graphics_command_buffers[current_frame], 0);
    record_command_buffer(current_frame, image_index);

    constexpr VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &_image_available_semaphores[current_frame],
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &_graphics_command_buffers[current_frame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &_render_finished_semaphores[current_frame]
    };

    vkQueueSubmit(_graphics_queue, 1, &submit_info, _inflight_fences[current_frame]);

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &_render_finished_semaphores[current_frame],
        .swapchainCount = 1,
        .pSwapchains = &_swapchain,
        .pImageIndices = &image_index
    };

    vkQueuePresentKHR(_present_queue, &present_info);

    current_frame = (current_frame + 1) % max_frames_in_flight;
}

auto Motorino::Engine::create_buffer(
    std::uint64_t size,
    std::uint32_t usage,
    std::uint32_t properties,
    VkBuffer& buffer,
    VkDeviceMemory& buffer_memory
) -> bool{
    std::uint32_t indices[] = { *_indices.graphics, *_indices.transfer };

    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = 2,
        .pQueueFamilyIndices = indices
    };

    if (vkCreateBuffer(_device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        Logger::error("Failed to create buffer.\n");
        return false;
    }

    Logger::info("Created buffer.\n");

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(_device, buffer, &mem_requirements);

    std::uint32_t memory_index;
    const auto result = find_memory_type(
        _physical_device,
        mem_requirements.memoryTypeBits,
        properties,
        memory_index
    );

    if (!result) return false;

    VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = memory_index
    };

    if (vkAllocateMemory(_device, &allocate_info, nullptr, &buffer_memory) != VK_SUCCESS) {
        Logger::error("Failed to allocate buffer memory.\n");
        return false;
    }

    vkBindBufferMemory(_device, buffer, buffer_memory, 0);
    Logger::info("Allocated buffer memory.\n");

    return true;
}
