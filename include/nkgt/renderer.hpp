#pragma once

#include <optional>
#include <span>
#include <vector>

// Forward declare GLFW types to avoid public include
typedef struct GLFWwindow GLFWwindow;

// Forward declare Vulkan types to avoid public include
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDebugUtilsMessengerEXT_T* VkDebugUtilsMessengerEXT;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkImage_T* VkImage;
typedef struct VkImageView_T* VkImageView;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkFramebuffer_T* VkFramebuffer;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkFence_T* VkFence;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkDeviceMemory_T* VkDeviceMemory;

namespace Motorino {

constexpr std::uint32_t max_frames_in_flight = 2;

enum class Error {
    vulkan,
    shader_compilation
};

enum class ShaderStage {
    Vertex   = 0x00000001,
    Fragment = 0x00000010,
};

struct ShaderInfo {
    ShaderStage type;
    const char* path;
};

struct queue_indices {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    std::optional<std::uint32_t> transfer;
};

struct Vertex {
    float pos[2];
    float color[3];
};

struct Geometry {
    unsigned char* data;

    std::uint32_t vertex_count;
    std::uint32_t index_count;
};

class Engine {
public:
    Engine(
        std::uint32_t width,
        std::uint32_t height,
        const char* name
    );
    ~Engine();

    auto init_vulkan() -> bool;
    auto run() -> void;
    auto recreate_swapchain() -> bool;

    auto set_extent(
        std::uint32_t width,
        std::uint32_t height
    ) -> void;

    auto create_pipeline(
        std::span<ShaderInfo> shaders
    ) -> bool;

    auto submit_vertex_data(
        const Geometry* geometry
    ) -> bool;

private:
    auto create_swapchain() -> bool;
    auto cleanup_swapchain() -> void;
    auto create_image_views() -> bool;
    auto create_framebuffers() -> bool;
    auto draw_frame() -> void;

    auto record_command_buffer(
        std::uint32_t current_frame,
        std::uint32_t image_index
    ) -> void;

    auto create_buffer(
        std::uint64_t size,
        std::uint32_t usage,
        std::uint32_t properties,
        VkBuffer& buffer,
        VkDeviceMemory& buffer_memory
    ) -> bool;

    std::uint32_t _width;
    std::uint32_t _height;
    const char* _name;

    GLFWwindow* _handle;

    queue_indices _indices;
    VkInstance _instance;
    VkSurfaceKHR _surface;
    VkPhysicalDevice _physical_device;
    VkDevice _device;
    VkQueue _graphics_queue;
    VkQueue _present_queue;
    VkQueue _transfer_queue;
    VkSwapchainKHR _swapchain;
    VkRenderPass _render_pass;
    VkCommandPool _graphics_command_pool;
    VkCommandPool _transfer_command_pool;
    VkCommandBuffer _graphics_command_buffers[max_frames_in_flight];
    VkPipelineLayout _pipeline_layout;
    VkPipeline _pipeline;
    VkSemaphore _image_available_semaphores[max_frames_in_flight];
    VkSemaphore _render_finished_semaphores[max_frames_in_flight];
    VkFence _inflight_fences[max_frames_in_flight];
    VkBuffer _vertex_buffer;
    VkDeviceMemory _vertex_buffer_memory;
    std::uint32_t _index_count;
    std::uint32_t _vertex_count;
    std::vector<VkImage> _images;
    std::vector<VkImageView> _image_views;
    std::vector<VkFramebuffer> _framebuffers;
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT _dbg_messenger;
#endif
};

}
