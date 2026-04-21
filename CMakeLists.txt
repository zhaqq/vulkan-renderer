cmake_minimum_required(VERSION 3.20)
project(Renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(ThirdParty/glfw)

# ImGui
add_library(imgui STATIC
    ThirdParty/imgui/imgui.cpp
    ThirdParty/imgui/imgui_draw.cpp
    ThirdParty/imgui/imgui_tables.cpp
    ThirdParty/imgui/imgui_widgets.cpp
    ThirdParty/imgui/backends/imgui_impl_glfw.cpp
    ThirdParty/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ThirdParty/imgui
    ThirdParty/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# GLM
add_subdirectory(ThirdParty/glm)

# fastgltf
add_subdirectory(ThirdParty/fastgltf)

# Tracy
option(TRACY_ENABLE "Enable Tracy profiler" ON)
add_subdirectory(ThirdParty/tracy)

# Compile test shader to SPIR-V via DXC at build time
find_program(DXC_EXECUTABLE dxc
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/test_vs.spv
    COMMAND ${DXC_EXECUTABLE} -T vs_6_0 -E VSMain -spirv -Fo ${CMAKE_BINARY_DIR}/test_vs.spv ${CMAKE_SOURCE_DIR}/Shaders/HLSL/test.hlsl
    DEPENDS ${CMAKE_SOURCE_DIR}/Shaders/HLSL/test.hlsl
    COMMENT "Compiling test.hlsl to SPIR-V"
)

add_custom_target(Shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/test_vs.spv)

# Main executable
add_executable(Renderer
    Source/main.cpp
)   

target_include_directories(Renderer PRIVATE
    Source
    ThirdParty/stb
    ThirdParty/vma/include
)

target_compile_definitions(Renderer PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
)

target_link_libraries(Renderer PRIVATE
    Vulkan::Vulkan
    glfw
    imgui
    glm::glm
    fastgltf
    Tracy::TracyClient
)