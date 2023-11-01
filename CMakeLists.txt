cmake_minimum_required(VERSION 3.26)
set(VCPKG_TARGET_TRIPLET x64-windows)
project(motorino)

find_package(Vulkan REQUIRED glslc)
find_package(glfw3 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

include(cmake/compiler_options.cmake)

set(CMAKE_CXX_STANDARD 23)

set(motorino_sources
    src/renderer.cpp
)

set(motorino_includes
    include/nkgt/logger.hpp
    include/nkgt/renderer.hpp
)

add_library(motorino STATIC ${motorino_sources} ${motorino_includes})
target_link_libraries(
    motorino
    PRIVATE Vulkan::Vulkan
            glfw
            fmt::fmt
)
target_include_directories(motorino PUBLIC include)
set_compiler_options(motorino)

add_subdirectory(samples)