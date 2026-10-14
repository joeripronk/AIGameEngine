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
target_include_directories(imgui PUBLIC
    ${CMAKE_SOURCE_DIR}/vendor/imgui
    ${CMAKE_SOURCE_DIR}/vendor/imgui/backends
    ${CMAKE_SOURCE_DIR}/vendor/glfw/include
)

target_include_directories(ForgeEditor
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/editor
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Platform/Vulkan
        ${CMAKE_SOURCE_DIR}/vendor/imgui
        ${CMAKE_SOURCE_DIR}/vendor/imgui/backends
        /usr/include
)

target_sources(ForgeEditor
    PRIVATE
        src/Client.cpp
        src/EntryPoint.h
        src/EditorApp.h
        src/EditorApp.cpp
)

target_link_libraries(ForgeEditor
    PRIVATE
        ForgeApp
        nvrhi
        imgui
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
