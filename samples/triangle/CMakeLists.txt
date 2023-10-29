add_executable(triangle triangle.cpp)
target_link_libraries(triangle PRIVATE motorino)
set_compiler_options(triangle)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

add_custom_command(TARGET triangle POST_BUILD
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag -O --target-env=vulkan1.3 -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/frag.spv
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert -O --target-env=vulkan1.3 -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/vert.spv
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/shaders/frag.spv ${CMAKE_CURRENT_BINARY_DIR}/shaders/vert.spv
)