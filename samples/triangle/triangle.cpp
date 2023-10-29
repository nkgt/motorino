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

    if (!vroom.create_pipeline(shaders).has_value()) {
        return EXIT_FAILURE;
    }

    vroom.run();

    return EXIT_SUCCESS;
}