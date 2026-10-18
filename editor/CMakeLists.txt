# Forge Editor
add_executable(ForgeEditor)

# Dear ImGui library (v1.91.9: split core translation units + backends/)
add_library(imgui STATIC)
target_sources(imgui PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_vulkan.cpp
)
# ImGui's GLFW backend includes <GLFW/glfw3.h> without defining this itself, so
# it picks up <GL/gl.h> - unlike our own sources, which define it at the
# include. Measured: 281 preprocessed lines came from /usr/include/GL/. Only
# the Vulkan backend is built, so nothing here actually reads them.
target_compile_definitions(imgui PRIVATE GLFW_INCLUDE_NONE)
target_include_directories(imgui PUBLIC
    ${CMAKE_SOURCE_DIR}/vendor/imgui
    ${CMAKE_SOURCE_DIR}/vendor/imgui/backends
    ${CMAKE_SOURCE_DIR}/vendor/glfw/include
)

# imgui_impl_vulkan.h includes <vulkan/vulkan.h> directly, so this target needs
# the Vulkan headers. On Windows that produced "Cannot open include file:
# 'vulkan/vulkan.h'"; on Linux it went unnoticed, because /usr/include is a
# default search path and the file happened to be there.
#
# Found here rather than reusing src/'s variable: imported targets and plain
# variables from find_package are scoped to the directory that ran it, and this
# directory is a sibling of src/, not a child. The result is cached, so the
# second call costs nothing.
find_package(Vulkan REQUIRED)
target_link_libraries(imgui PRIVATE Vulkan::Headers)

target_include_directories(ForgeEditor
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/editor
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Platform/Vulkan
        ${CMAKE_SOURCE_DIR}/vendor/imgui
        ${CMAKE_SOURCE_DIR}/vendor/imgui/backends
)

target_sources(ForgeEditor
    PRIVATE
        src/Client.cpp
        src/EntryPoint.h
        src/EditorApp.h
        src/EditorApp.cpp
        src/EditorInputLayer.h
        src/EditorInputLayer.cpp
        src/FileDialog.h
        src/FileDialog.cpp
)

target_link_libraries(ForgeEditor
    PRIVATE
        ForgeApp
        nvrhi
        imgui
        nfd
        forge_vk_dispatch
        ${CMAKE_DL_LIBS}
)

# Set properties
set_target_properties(ForgeEditor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Enable -Werror for editor code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeEditor PRIVATE -Werror)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeEditor PRIVATE /WX)
endif()

# Copy resources
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
