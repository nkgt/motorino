#pragma once

#include <expected>
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

namespace Motorino {

struct queue_indices {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
};

enum class ShaderStage {
    Vertex   = 0x00000001,
    Fragment = 0x00000010,
};

struct ShaderInfo {
    ShaderStage type;
    const char* path;
};

constexpr std::uint32_t max_frames_in_flight = 2;

enum class Error {
    vulkan,
    shader_compilation
};

class Engine {
public:
    Engine(
        std::uint32_t width,
        std::uint32_t height,
        const char* name
    );
    ~Engine();

    auto init_vulkan() -> std::expected<void, Error>;
    auto set_extent(
        std::uint32_t width,
        std::uint32_t height
    ) -> void;
    auto create_pipeline(std::span<ShaderInfo> shaders) -> std::expected<void, Error>;
    auto recreate_swapchain() -> std::expected<void, Error>;
    auto run() -> void;

private:
    auto create_swapchain() -> std::expected<void, Error>;
    auto cleanup_swapchain() -> void;
    auto create_image_views() -> std::expected<void, Error>;
    auto create_framebuffers() -> std::expected<void, Error>;
    auto record_command_buffer(
        std::uint32_t current_frame,
        std::uint32_t image_index
    ) -> void;

    auto draw_frame() -> std::expected<void, Error>;

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
    VkSwapchainKHR _swapchain;
    VkRenderPass _render_pass;
    VkCommandPool _command_pool;
    VkCommandBuffer _command_buffers[max_frames_in_flight];
    VkPipelineLayout _pipeline_layout;
    VkPipeline _pipeline;
    VkSemaphore _image_available_semaphores[max_frames_in_flight];
    VkSemaphore _render_finished_semaphores[max_frames_in_flight];
    VkFence _inflight_fences[max_frames_in_flight];
    std::vector<VkImage> _images;
    std::vector<VkImageView> _image_views;
    std::vector<VkFramebuffer> _framebuffers;
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT _dbg_messenger;
#endif
};

}
