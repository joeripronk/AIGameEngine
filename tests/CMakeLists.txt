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
        src/ImGuiLayerTests.cpp
        src/AssetWatcherTests.cpp
        src/ProfilerTests.cpp
        src/TestSceneTests.cpp
        src/RendererTests.cpp
        src/PhysicsTests.cpp
        src/ScriptingTests.cpp
        src/FrustumTests.cpp
        src/ShadowCameraTests.cpp
        src/RTTITests.cpp
        src/BloomTests.cpp
        src/IblTests.cpp
        src/AssetDatabaseTests.cpp
        src/SerializationTests.cpp
        src/ExportTests.cpp
        src/ProjectTests.cpp
        src/AudioTests.cpp
        src/ReverbTests.cpp
        src/BuildConfigTests.cpp
        src/NativeScriptTests.cpp
        src/StartupIntegrityTests.cpp
        src/DeviceLayerTests.cpp
        src/MemoryLayerTests.cpp
        src/FrameLayerTests.cpp
)

target_link_libraries(ForgeTests
    PRIVATE
        ForgeApp
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
    target_compile_options(ForgeTests PRIVATE /utf-8)
endif()

# Add test executable
add_test(NAME ForgeTests COMMAND ForgeTests)
set_tests_properties(ForgeTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Startup integration check: launches the real editor binary and classifies
# startup failures with hints. Skips gracefully where no window is possible,
# so it is CI-safe. Needs the editor target, which CI builds.
add_test(NAME StartupCheck COMMAND bash "${CMAKE_SOURCE_DIR}/tests/integration/startup_check.sh")
set_tests_properties(StartupCheck PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 120
)
