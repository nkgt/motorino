#include <nkgt/renderer.hpp>

int main() {
    Motorino::Engine vroom(800, 600, "Triangle");
    if (!vroom.init_vulkan()) {
        return EXIT_FAILURE;
    }

    std::vector<Motorino::ShaderInfo> shaders = {
        {
            Motorino::ShaderStage::Fragment,
            "shaders/frag.spv"
        },
        {
            Motorino::ShaderStage::Vertex,
            "shaders/vert.spv"
        },
    };

    if (!vroom.create_pipeline(shaders)) {
        return EXIT_FAILURE;
    }

    Motorino::Vertex vertices[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}}
    };

    uint16_t indices[] = {
        0, 1, 2, 2, 3, 0
    };

    const std::size_t vertex_bytes = 4 * sizeof(Motorino::Vertex);
    const std::size_t index_bytes = 6 * sizeof(std::uint16_t);
    const std::size_t total_size = index_bytes + vertex_bytes;

    Motorino::Geometry geometry{
        .data = new unsigned char[total_size],
        .vertex_count = 4,
        .index_count = 6
    };

    std::memcpy(geometry.data, vertices, vertex_bytes);
    std::memcpy(geometry.data + vertex_bytes, indices, index_bytes);

    if (!vroom.submit_vertex_data(&geometry)) {
        return EXIT_FAILURE;
    }

    vroom.run();

    delete[] geometry.data;
    return EXIT_SUCCESS;
}