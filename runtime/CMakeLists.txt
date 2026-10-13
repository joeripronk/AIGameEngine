# Forge Runtime
add_executable(ForgeRuntime)

target_include_directories(ForgeRuntime
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/runtime
)

target_sources(ForgeRuntime
    PRIVATE
        src/EntryPoint.h
        src/RuntimeApp.h
        src/RuntimeApp.cpp
)

target_link_libraries(ForgeRuntime
    PRIVATE
        Forge
)

# Set properties
set_target_properties(ForgeRuntime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Copy resources
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
