# Forge Editor
add_executable(ForgeEditor)

# The Dear ImGui library is built in src/: the game uses it too, and the
# game is what is built for a phone, where the editor is not built at all.

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
        imgui
        nfd
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
