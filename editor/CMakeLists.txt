# Forge Editor
add_executable(ForgeEditor)

target_include_directories(ForgeEditor
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/editor
        ${CMAKE_SOURCE_DIR}/vendor/imgui
        ${CMAKE_SOURCE_DIR}/vendor/imgui/backends
)

target_sources(ForgeEditor
    PRIVATE
        src/EntryPoint.h
        src/EditorApp.h
        src/EditorApp.cpp
)

target_link_libraries(ForgeEditor
    PRIVATE
        Forge
        imgui
        ${CMAKE_DL_LIBS}
)

# Set properties
set_target_properties(ForgeEditor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Copy resources
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
