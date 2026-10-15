# Forge Tests
add_executable(ForgeTests)

# Catch2
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/catch2 ${CMAKE_BINARY_DIR}/vendor/catch2)

target_include_directories(ForgeTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/vendor/catch2/src
)

target_sources(ForgeTests
    PRIVATE
        src/CoreTests.cpp
        src/SceneTests.cpp
        src/ComprehensiveTests.cpp
        src/EngineFeatureTests.cpp
        src/ToolingTests.cpp
        src/RendererFeatureTests.cpp
        src/TestSceneTests.cpp
        src/RendererTests.cpp
        src/PhysicsTests.cpp
        src/ScriptingTests.cpp
        src/SerializationTests.cpp
        src/ExportTests.cpp
        src/AudioTests.cpp
        src/ReverbTests.cpp
)

target_link_libraries(ForgeTests
    PRIVATE
        ForgeCore
        Catch2::Catch2WithMain
        ${CMAKE_DL_LIBS}
)

target_compile_definitions(ForgeTests
    PRIVATE
        FG_NO_VULKAN_RUNTIME
        CATCH_CONFIG_NO_PARALLEL_EXECUTION
        FORGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Set properties
set_target_properties(ForgeTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Enable -Werror for tests
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeTests PRIVATE -Werror)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeTests PRIVATE /WX)
endif()

# Add test executable
add_test(NAME ForgeTests COMMAND ForgeTests)
