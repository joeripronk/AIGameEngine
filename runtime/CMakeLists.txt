# Forge Runtime
add_executable(ForgeRuntime)

target_include_directories(ForgeRuntime
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/runtime
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Platform/Vulkan
)

target_sources(ForgeRuntime
    PRIVATE
        src/Client.cpp
        src/EntryPoint.h
        src/RuntimeApp.h
        src/RuntimeApp.cpp
)

target_link_libraries(ForgeRuntime
    PRIVATE
        ForgeApp
        nvrhi
        forge_vk_dispatch
)

# Set properties
set_target_properties(ForgeRuntime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Enable -Werror for runtime code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeRuntime PRIVATE -Werror)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeRuntime PRIVATE /WX)
endif()

# Copy resources
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
